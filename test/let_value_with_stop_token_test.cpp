/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/any_sender_of.hpp>
#include <unifex/just_done.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/when_all.hpp>

#include <unifex/variant.hpp>
#include <chrono>
#include <iostream>

#include "stoppable_receiver.hpp"

#include <gtest/gtest.h>

using namespace unifex;
using namespace unifex_test;
using namespace std::chrono;
using namespace std::chrono_literals;

struct LetWithStopToken : testing::Test {
  struct DestructionCounter {
    int value;
    bool wasMoved{false};

    DestructionCounter(DestructionCounter&& c) : value(c.value) {
      c.wasMoved = true;
    }

    DestructionCounter(int value) : value(value) {}

    static int& destroyCount() {
      static int destroyCount = 0;
      return destroyCount;
    }

    ~DestructionCounter() {
      if (!wasMoved) {
        destroyCount()++;
      }
    }
  };

  void SetUp() override { DestructionCounter::destroyCount() = 0; }
};

TEST_F(LetWithStopToken, Simple) {
  timed_single_thread_context context;

  // Simple usage of 'let_value_with_stop_token()'
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  optional<int> result =
      sync_wait(let_value_with_stop_source([&](auto& stopSource) {
        return let_value_with_stop_token([&](inplace_stop_token stopToken) {
          // Needs to pass the stop token by value into the capture list
          // to prevent accessing stopToken reference after function has
          // returned.
          return let_value_with(
              [stopToken, &external_context]() noexcept {
                auto stopCallback = [&]() mutable noexcept {
                  external_context = 42;
                };
                using stop_callback_t =
                    typename inplace_stop_token::template callback_type<
                        decltype(stopCallback)>;
                return stop_callback_t{stopToken, stopCallback};
              },
              [&stopSource](auto&) -> unifex::any_sender_of<int> {
                stopSource.request_stop();
                return just_done();
              });
        });
      }));

  EXPECT_TRUE(!result);
  EXPECT_EQ(external_context, 42);
}

TEST_F(LetWithStopToken, SimpleInplaceStoppableNoexcept) {
  static_assert(std::is_same_v<
                stop_token_type_t<InplaceStoppableIntReceiver>,
                inplace_stop_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with inplace
  // stop token
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  inplace_stop_source stopSource;
  auto stop_token_functor = [&](inplace_stop_token stopToken) noexcept {
    return let_value_with(
        [stopToken, &external_context]() noexcept {
          auto stopCallback = [&]() noexcept {
            external_context = 42;
          };
          using stop_callback_t =
                    typename inplace_stop_token::template callback_type<
                        decltype(stopCallback)>;
          return stop_callback_t{stopToken, stopCallback};
        },
        [&](auto&) noexcept {
          stopSource.request_stop();
          return just_done();
        });
  };
  static_assert(is_nothrow_connectable_v<
                callable_result_t<
                    decltype(let_value_with_stop_token),
                    decltype(stop_token_functor)>,
                InplaceStoppableIntReceiver>);
  auto op = unifex::connect(
      let_value_with_stop_token(std::move(stop_token_functor)),
      InplaceStoppableIntReceiver{stopSource});
  unifex::start(op);

  EXPECT_EQ(external_context, 42);
}

TEST_F(LetWithStopToken, SimpleUnstoppable) {
  static_assert(unifex::same_as<
                stop_token_type_t<UnstoppableSimpleIntReceiver>,
                unifex::unstoppable_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with
  // unstoppable stop token
  // - Sets up some work to execute when receiver is cancelled
  // - Work is never completed since token is unstoppable
  int external_context = 0;
  auto op = unifex::connect(
      let_value_with_stop_token([&](inplace_stop_token stopToken) noexcept {
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                external_context = 42;
              };
              using stop_callback_t =
                  typename inplace_stop_token::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&](auto&) -> unifex::any_sender_of<int> { return just_done(); });
      }),
      UnstoppableSimpleIntReceiver{});
  unifex::start(op);

  EXPECT_EQ(external_context, 0);
}

TEST_F(LetWithStopToken, SimpleInplaceStoppable) {
  static_assert(std::is_same_v<
                stop_token_type_t<InplaceStoppableIntReceiver>,
                inplace_stop_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with inplace
  // stop token
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  inplace_stop_source stopSource;
  auto op = unifex::connect(
      let_value_with_stop_token([&](inplace_stop_token stopToken) noexcept {
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                external_context = 42;
              };
              using stop_callback_t =
                  typename inplace_stop_token::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&](auto&) -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      }),
      InplaceStoppableIntReceiver{stopSource});
  unifex::start(op);

  EXPECT_EQ(external_context, 42);
}

TEST_F(LetWithStopToken, SimpleNonInplaceStoppable) {
  static_assert(!std::is_same_v<
                stop_token_type_t<NonInplaceStoppableIntReceiver>,
                inplace_stop_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with stop token
  // that's stoppable but not inplace stop token
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  inplace_stop_source stopSource;
  auto op = unifex::connect(
      let_value_with_stop_token([&](inplace_stop_token stopToken) noexcept {
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                external_context = 42;
              };
              using stop_callback_t =
                  typename inplace_stop_token::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&](auto&) -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      }),
      NonInplaceStoppableIntReceiver{stopSource});
  unifex::start(op);

  EXPECT_EQ(external_context, 42);
}

using counter = LetWithStopToken::DestructionCounter;

template <typename ConnectOperation>
void testPreserveOperationState(ConnectOperation connect) {
  {
    auto op = connect();
    EXPECT_EQ(counter::destroyCount(), 0);
    unifex::start(op);
    EXPECT_EQ(counter::destroyCount(), 0);
  }
  EXPECT_EQ(counter::destroyCount(), 1);
}

template <typename StopSource>
auto destructionCountingLetValueWithStopToken(StopSource& stopSource) {
  return let_value_with_stop_token(
      [&, external_context = counter{42}](
          inplace_stop_token stopToken) {
        // Needs to pass the stop token by value into the capture list
        // to prevent accessing stopToken reference after function has
        // returned.
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                EXPECT_EQ(external_context.value, 42);
              };
              using stop_callback_t =
                  typename inplace_stop_token::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&stopSource](auto&) -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      });
}

TEST_F(LetWithStopToken, PreserveOperationStateUnstoppable) {
  testPreserveOperationState([]() {
    return unifex::connect(
        let_value_with_stop_source([&](auto& stopSource) {
          return destructionCountingLetValueWithStopToken(stopSource);
        }),
        UnstoppableSimpleIntReceiver{});
  });
}

TEST_F(LetWithStopToken, PreserveOperationStateNonInPlaceStoppable) {
  inplace_stop_source stopSource;
  testPreserveOperationState([&]() {
    return unifex::connect(
        destructionCountingLetValueWithStopToken(stopSource),
        NonInplaceStoppableIntReceiver{stopSource});
  });
}