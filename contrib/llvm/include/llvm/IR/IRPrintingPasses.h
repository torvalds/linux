//===- IRPrintingPasses.h - Passes to print out IR constructs ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines passes to print out IR in various granularities. The
/// PrintModulePass pass simply prints out the entire module when it is
/// executed. The PrintFunctionPass class is designed to be pipelined with
/// other FunctionPass's, and prints out the functions of the module as they
/// are processed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRPRINTINGPASSES_H
#define LLVM_IR_IRPRINTINGPASSES_H

#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {
class Pass;
class BasicBlockPass;
class Function;
class FunctionPass;
class Module;
class ModulePass;
class PreservedAnalyses;
class raw_ostream;
template <typename IRUnitT, typename... ExtraArgTs> class AnalysisManager;

/// Create and return a pass that writes the module to the specified
/// \c raw_ostream.
ModulePass *createPrintModulePass(raw_ostream &OS,
                                  const std::string &Banner = "",
                                  bool ShouldPreserveUseListOrder = false);

/// Create and return a pass that prints functions to the specified
/// \c raw_ostream as they are processed.
FunctionPass *createPrintFunctionPass(raw_ostream &OS,
                                      const std::string &Banner = "");

/// Create and return a pass that writes the BB to the specified
/// \c raw_ostream.
BasicBlockPass *createPrintBasicBlockPass(raw_ostream &OS,
                                          const std::string &Banner = "");

/// Print out a name of an LLVM value without any prefixes.
///
/// The name is surrounded with ""'s and escaped if it has any special or
/// non-printable characters in it.
void printLLVMNameWithoutPrefix(raw_ostream &OS, StringRef Name);

/// Return true if a pass is for IR printing.
bool isIRPrintingPass(Pass *P);

/// isFunctionInPrintList - returns true if a function should be printed via
//  debugging options like -print-after-all/-print-before-all.
//  Tells if the function IR should be printed by PrinterPass.
extern bool isFunctionInPrintList(StringRef FunctionName);

/// forcePrintModuleIR - returns true if IR printing passes should
//  be printing module IR (even for local-pass printers e.g. function-pass)
//  to provide more context, as enabled by debugging option -print-module-scope
//  Tells if IR printer should be printing module IR
extern bool forcePrintModuleIR();

extern bool shouldPrintBeforePass();
extern bool shouldPrintBeforePass(StringRef);
extern bool shouldPrintAfterPass();
extern bool shouldPrintAfterPass(StringRef);

/// Pass for printing a Module as LLVM's text IR assembly.
///
/// Note: This pass is for use with the new pass manager. Use the create...Pass
/// functions above to create passes for use with the legacy pass manager.
class PrintModulePass {
  raw_ostream &OS;
  std::string Banner;
  bool ShouldPreserveUseListOrder;

public:
  PrintModulePass();
  PrintModulePass(raw_ostream &OS, const std::string &Banner = "",
                  bool ShouldPreserveUseListOrder = false);

  PreservedAnalyses run(Module &M, AnalysisManager<Module> &);

  static StringRef name() { return "PrintModulePass"; }
};

/// Pass for printing a Function as LLVM's text IR assembly.
///
/// Note: This pass is for use with the new pass manager. Use the create...Pass
/// functions above to create passes for use with the legacy pass manager.
class PrintFunctionPass {
  raw_ostream &OS;
  std::string Banner;

public:
  PrintFunctionPass();
  PrintFunctionPass(raw_ostream &OS, const std::string &Banner = "");

  PreservedAnalyses run(Function &F, AnalysisManager<Function> &);

  static StringRef name() { return "PrintFunctionPass"; }
};

} // End llvm namespace

#endif
