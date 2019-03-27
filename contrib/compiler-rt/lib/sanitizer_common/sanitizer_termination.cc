//===-- sanitizer_termination.cc --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// This file contains the Sanitizer termination functions CheckFailed and Die,
/// and the callback functionalities associated with them.
///
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_libc.h"

namespace __sanitizer {

static const int kMaxNumOfInternalDieCallbacks = 5;
static DieCallbackType InternalDieCallbacks[kMaxNumOfInternalDieCallbacks];

bool AddDieCallback(DieCallbackType callback) {
  for (int i = 0; i < kMaxNumOfInternalDieCallbacks; i++) {
    if (InternalDieCallbacks[i] == nullptr) {
      InternalDieCallbacks[i] = callback;
      return true;
    }
  }
  return false;
}

bool RemoveDieCallback(DieCallbackType callback) {
  for (int i = 0; i < kMaxNumOfInternalDieCallbacks; i++) {
    if (InternalDieCallbacks[i] == callback) {
      internal_memmove(&InternalDieCallbacks[i], &InternalDieCallbacks[i + 1],
                       sizeof(InternalDieCallbacks[0]) *
                           (kMaxNumOfInternalDieCallbacks - i - 1));
      InternalDieCallbacks[kMaxNumOfInternalDieCallbacks - 1] = nullptr;
      return true;
    }
  }
  return false;
}

static DieCallbackType UserDieCallback;
void SetUserDieCallback(DieCallbackType callback) {
  UserDieCallback = callback;
}

void NORETURN Die() {
  if (UserDieCallback)
    UserDieCallback();
  for (int i = kMaxNumOfInternalDieCallbacks - 1; i >= 0; i--) {
    if (InternalDieCallbacks[i])
      InternalDieCallbacks[i]();
  }
  if (common_flags()->abort_on_error)
    Abort();
  internal__exit(common_flags()->exitcode);
}

static CheckFailedCallbackType CheckFailedCallback;
void SetCheckFailedCallback(CheckFailedCallbackType callback) {
  CheckFailedCallback = callback;
}

const int kSecondsToSleepWhenRecursiveCheckFailed = 2;

void NORETURN CheckFailed(const char *file, int line, const char *cond,
                          u64 v1, u64 v2) {
  static atomic_uint32_t num_calls;
  if (atomic_fetch_add(&num_calls, 1, memory_order_relaxed) > 10) {
    SleepForSeconds(kSecondsToSleepWhenRecursiveCheckFailed);
    Trap();
  }

  if (CheckFailedCallback) {
    CheckFailedCallback(file, line, cond, v1, v2);
  }
  Report("Sanitizer CHECK failed: %s:%d %s (%lld, %lld)\n", file, line, cond,
                                                            v1, v2);
  Die();
}

} // namespace __sanitizer

using namespace __sanitizer;  // NOLINT

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_set_death_callback(void (*callback)(void)) {
  SetUserDieCallback(callback);
}
}  // extern "C"
