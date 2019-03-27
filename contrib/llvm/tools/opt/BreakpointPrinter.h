//===- BreakpointPrinter.h - Breakpoint location printer ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Breakpoint location printer.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_OPT_BREAKPOINTPRINTER_H
#define LLVM_TOOLS_OPT_BREAKPOINTPRINTER_H

namespace llvm {

class ModulePass;
class raw_ostream;

ModulePass *createBreakpointPrinter(raw_ostream &out);
}

#endif // LLVM_TOOLS_OPT_BREAKPOINTPRINTER_H
