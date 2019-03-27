//===- WindowsResourceDumper.h - Windows Resource printer -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_WINDOWSRESOURCEDUMPER_H
#define LLVM_TOOLS_LLVM_READOBJ_WINDOWSRESOURCEDUMPER_H

#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {
namespace object {
namespace WindowsRes {

class Dumper {
public:
  Dumper(WindowsResource *Res, ScopedPrinter &SW) : SW(SW), WinRes(Res) {}

  Error printData();

private:
  ScopedPrinter &SW;
  WindowsResource *WinRes;

  void printEntry(const ResourceEntryRef &Ref);
};

} // namespace WindowsRes
} // namespace object
} // namespace llvm

#endif
