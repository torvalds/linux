//===- llvm/Support/BuryPointer.h - Memory Manipulation/Leak ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BURYPOINTER_H
#define LLVM_SUPPORT_BURYPOINTER_H

#include <memory>

namespace llvm {

// In tools that will exit soon anyway, going through the process of explicitly
// deallocating resources can be unnecessary - better to leak the resources and
// let the OS clean them up when the process ends. Use this function to ensure
// the memory is not misdiagnosed as an unintentional leak by leak detection
// tools (this is achieved by preserving pointers to the object in a globally
// visible array).
void BuryPointer(const void *Ptr);
template <typename T> void BuryPointer(std::unique_ptr<T> Ptr) {
  BuryPointer(Ptr.release());
}

} // namespace llvm

#endif
