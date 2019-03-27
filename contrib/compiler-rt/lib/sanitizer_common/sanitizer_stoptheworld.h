//===-- sanitizer_stoptheworld.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines the StopTheWorld function which suspends the execution of the current
// process and runs the user-supplied callback in the same address space.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_STOPTHEWORLD_H
#define SANITIZER_STOPTHEWORLD_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_common.h"

namespace __sanitizer {

enum PtraceRegistersStatus {
  REGISTERS_UNAVAILABLE_FATAL = -1,
  REGISTERS_UNAVAILABLE = 0,
  REGISTERS_AVAILABLE = 1
};

// Holds the list of suspended threads and provides an interface to dump their
// register contexts.
class SuspendedThreadsList {
 public:
  SuspendedThreadsList() = default;

  // Can't declare pure virtual functions in sanitizer runtimes:
  // __cxa_pure_virtual might be unavailable. Use UNIMPLEMENTED() instead.
  virtual PtraceRegistersStatus GetRegistersAndSP(uptr index, uptr *buffer,
                                                  uptr *sp) const {
    UNIMPLEMENTED();
  }

  // The buffer in GetRegistersAndSP should be at least this big.
  virtual uptr RegisterCount() const { UNIMPLEMENTED(); }
  virtual uptr ThreadCount() const { UNIMPLEMENTED(); }
  virtual tid_t GetThreadID(uptr index) const { UNIMPLEMENTED(); }

 private:
  // Prohibit copy and assign.
  SuspendedThreadsList(const SuspendedThreadsList&);
  void operator=(const SuspendedThreadsList&);
};

typedef void (*StopTheWorldCallback)(
    const SuspendedThreadsList &suspended_threads_list,
    void *argument);

// Suspend all threads in the current process and run the callback on the list
// of suspended threads. This function will resume the threads before returning.
// The callback should not call any libc functions. The callback must not call
// exit() nor _exit() and instead return to the caller.
// This function should NOT be called from multiple threads simultaneously.
void StopTheWorld(StopTheWorldCallback callback, void *argument);

}  // namespace __sanitizer

#endif  // SANITIZER_STOPTHEWORLD_H
