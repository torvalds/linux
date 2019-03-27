//===-- llvm/Support/Threading.h - Control multithreading mode --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares helper functions for running LLVM in a multi-threaded
// environment.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREADING_H
#define LLVM_SUPPORT_THREADING_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h" // for LLVM_ON_UNIX
#include "llvm/Support/Compiler.h"
#include <ciso646> // So we can check the C++ standard lib macros.
#include <functional>

#if defined(_MSC_VER)
// MSVC's call_once implementation worked since VS 2015, which is the minimum
// supported version as of this writing.
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#elif defined(LLVM_ON_UNIX) &&                                                 \
    (defined(_LIBCPP_VERSION) ||                                               \
     !(defined(__NetBSD__) || defined(__OpenBSD__) ||                          \
       (defined(__ppc__) || defined(__PPC__))))
// std::call_once from libc++ is used on all Unix platforms. Other
// implementations like libstdc++ are known to have problems on NetBSD,
// OpenBSD and PowerPC.
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#else
#define LLVM_THREADING_USE_STD_CALL_ONCE 0
#endif

#if LLVM_THREADING_USE_STD_CALL_ONCE
#include <mutex>
#else
#include "llvm/Support/Atomic.h"
#endif

namespace llvm {
class Twine;

/// Returns true if LLVM is compiled with support for multi-threading, and
/// false otherwise.
bool llvm_is_multithreaded();

/// llvm_execute_on_thread - Execute the given \p UserFn on a separate
/// thread, passing it the provided \p UserData and waits for thread
/// completion.
///
/// This function does not guarantee that the code will actually be executed
/// on a separate thread or honoring the requested stack size, but tries to do
/// so where system support is available.
///
/// \param UserFn - The callback to execute.
/// \param UserData - An argument to pass to the callback function.
/// \param RequestedStackSize - If non-zero, a requested size (in bytes) for
/// the thread stack.
void llvm_execute_on_thread(void (*UserFn)(void *), void *UserData,
                            unsigned RequestedStackSize = 0);

#if LLVM_THREADING_USE_STD_CALL_ONCE

  typedef std::once_flag once_flag;

#else

  enum InitStatus { Uninitialized = 0, Wait = 1, Done = 2 };

  /// The llvm::once_flag structure
  ///
  /// This type is modeled after std::once_flag to use with llvm::call_once.
  /// This structure must be used as an opaque object. It is a struct to force
  /// autoinitialization and behave like std::once_flag.
  struct once_flag {
    volatile sys::cas_flag status = Uninitialized;
  };

#endif

  /// Execute the function specified as a parameter once.
  ///
  /// Typical usage:
  /// \code
  ///   void foo() {...};
  ///   ...
  ///   static once_flag flag;
  ///   call_once(flag, foo);
  /// \endcode
  ///
  /// \param flag Flag used for tracking whether or not this has run.
  /// \param F Function to call once.
  template <typename Function, typename... Args>
  void call_once(once_flag &flag, Function &&F, Args &&... ArgList) {
#if LLVM_THREADING_USE_STD_CALL_ONCE
    std::call_once(flag, std::forward<Function>(F),
                   std::forward<Args>(ArgList)...);
#else
    // For other platforms we use a generic (if brittle) version based on our
    // atomics.
    sys::cas_flag old_val = sys::CompareAndSwap(&flag.status, Wait, Uninitialized);
    if (old_val == Uninitialized) {
      std::forward<Function>(F)(std::forward<Args>(ArgList)...);
      sys::MemoryFence();
      TsanIgnoreWritesBegin();
      TsanHappensBefore(&flag.status);
      flag.status = Done;
      TsanIgnoreWritesEnd();
    } else {
      // Wait until any thread doing the call has finished.
      sys::cas_flag tmp = flag.status;
      sys::MemoryFence();
      while (tmp != Done) {
        tmp = flag.status;
        sys::MemoryFence();
      }
    }
    TsanHappensAfter(&flag.status);
#endif
  }

  /// Get the amount of currency to use for tasks requiring significant
  /// memory or other resources. Currently based on physical cores, if
  /// available for the host system, otherwise falls back to
  /// thread::hardware_concurrency().
  /// Returns 1 when LLVM is configured with LLVM_ENABLE_THREADS=OFF
  unsigned heavyweight_hardware_concurrency();

  /// Get the number of threads that the current program can execute
  /// concurrently. On some systems std::thread::hardware_concurrency() returns
  /// the total number of cores, without taking affinity into consideration.
  /// Returns 1 when LLVM is configured with LLVM_ENABLE_THREADS=OFF.
  /// Fallback to std::thread::hardware_concurrency() if sched_getaffinity is
  /// not available.
  unsigned hardware_concurrency();

  /// Return the current thread id, as used in various OS system calls.
  /// Note that not all platforms guarantee that the value returned will be
  /// unique across the entire system, so portable code should not assume
  /// this.
  uint64_t get_threadid();

  /// Get the maximum length of a thread name on this platform.
  /// A value of 0 means there is no limit.
  uint32_t get_max_thread_name_length();

  /// Set the name of the current thread.  Setting a thread's name can
  /// be helpful for enabling useful diagnostics under a debugger or when
  /// logging.  The level of support for setting a thread's name varies
  /// wildly across operating systems, and we only make a best effort to
  /// perform the operation on supported platforms.  No indication of success
  /// or failure is returned.
  void set_thread_name(const Twine &Name);

  /// Get the name of the current thread.  The level of support for
  /// getting a thread's name varies wildly across operating systems, and it
  /// is not even guaranteed that if you can successfully set a thread's name
  /// that you can later get it back.  This function is intended for diagnostic
  /// purposes, and as with setting a thread's name no indication of whether
  /// the operation succeeded or failed is returned.
  void get_thread_name(SmallVectorImpl<char> &Name);
}

#endif
