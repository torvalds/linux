//===- DlltoolDriver.h - dlltool.exe-compatible driver ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines an interface to a dlltool.exe-compatible driver.
// Used by llvm-dlltool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLDRIVERS_LLVM_DLLTOOL_DLLTOOLDRIVER_H
#define LLVM_TOOLDRIVERS_LLVM_DLLTOOL_DLLTOOLDRIVER_H

namespace llvm {
template <typename T> class ArrayRef;

int dlltoolDriverMain(ArrayRef<const char *> ArgsArr);
} // namespace llvm

#endif
