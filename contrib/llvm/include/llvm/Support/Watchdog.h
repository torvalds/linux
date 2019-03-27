//===--- Watchdog.h - Watchdog timer ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares the llvm::sys::Watchdog class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WATCHDOG_H
#define LLVM_SUPPORT_WATCHDOG_H

#include "llvm/Support/Compiler.h"

namespace llvm {
  namespace sys {

    /// This class provides an abstraction for a timeout around an operation
    /// that must complete in a given amount of time. Failure to complete before
    /// the timeout is an unrecoverable situation and no mechanisms to attempt
    /// to handle it are provided.
    class Watchdog {
    public:
      Watchdog(unsigned int seconds);
      ~Watchdog();
    private:
      // Noncopyable.
      Watchdog(const Watchdog &other) = delete;
      Watchdog &operator=(const Watchdog &other) = delete;
    };
  }
}

#endif
