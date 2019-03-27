//=- PassPrinters.h - Utilities to print analysis info for passes -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Utilities to print analysis info for various kinds of passes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OPT_PASSPRINTERS_H
#define LLVM_TOOLS_OPT_PASSPRINTERS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class BasicBlockPass;
class CallGraphSCCPass;
class FunctionPass;
class ModulePass;
class LoopPass;
class PassInfo;
class raw_ostream;
class RegionPass;
class Module;

FunctionPass *createFunctionPassPrinter(const PassInfo *PI, raw_ostream &out,
                                        bool Quiet);

CallGraphSCCPass *createCallGraphPassPrinter(const PassInfo *PI,
                                             raw_ostream &out, bool Quiet);

ModulePass *createModulePassPrinter(const PassInfo *PI, raw_ostream &out,
                                    bool Quiet);

LoopPass *createLoopPassPrinter(const PassInfo *PI, raw_ostream &out,
                                bool Quiet);

RegionPass *createRegionPassPrinter(const PassInfo *PI, raw_ostream &out,
                                    bool Quiet);

BasicBlockPass *createBasicBlockPassPrinter(const PassInfo *PI,
                                            raw_ostream &out, bool Quiet);

} // end namespace llvm

#endif // LLVM_TOOLS_OPT_PASSPRINTERS_H
