//===-- SaveAndRestore.h - Utility  -------------------------------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility classes that use RAII to save and restore
/// values.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SAVEANDRESTORE_H
#define LLVM_SUPPORT_SAVEANDRESTORE_H

namespace llvm {

/// A utility class that uses RAII to save and restore the value of a variable.
template <typename T> struct SaveAndRestore {
  SaveAndRestore(T &X) : X(X), OldValue(X) {}
  SaveAndRestore(T &X, const T &NewValue) : X(X), OldValue(X) {
    X = NewValue;
  }
  ~SaveAndRestore() { X = OldValue; }
  T get() { return OldValue; }

private:
  T &X;
  T OldValue;
};

} // namespace llvm

#endif
