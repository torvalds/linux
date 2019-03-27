//===-- MCTargetOptionsCommandFlags.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains machine code-specific flags that are shared between
// different command line tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H
#define LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H

#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

static cl::opt<MCTargetOptions::AsmInstrumentation> AsmInstrumentation(
    "asm-instrumentation", cl::desc("Instrumentation of inline assembly and "
                                    "assembly source files"),
    cl::init(MCTargetOptions::AsmInstrumentationNone),
    cl::values(clEnumValN(MCTargetOptions::AsmInstrumentationNone, "none",
                          "no instrumentation at all"),
               clEnumValN(MCTargetOptions::AsmInstrumentationAddress, "address",
                          "instrument instructions with memory arguments")));

static cl::opt<bool> RelaxAll("mc-relax-all",
                       cl::desc("When used with filetype=obj, "
                                "relax all fixups in the emitted object file"));

static cl::opt<bool> IncrementalLinkerCompatible(
    "incremental-linker-compatible",
    cl::desc(
        "When used with filetype=obj, "
        "emit an object file which can be used with an incremental linker"));

static cl::opt<bool> PIECopyRelocations("pie-copy-relocations", cl::desc("PIE Copy Relocations"));

static cl::opt<int> DwarfVersion("dwarf-version", cl::desc("Dwarf version"),
                          cl::init(0));

static cl::opt<bool> ShowMCInst("asm-show-inst",
                         cl::desc("Emit internal instruction representation to "
                                  "assembly file"));

static cl::opt<bool> FatalWarnings("fatal-warnings",
                            cl::desc("Treat warnings as errors"));

static cl::opt<bool> NoWarn("no-warn", cl::desc("Suppress all warnings"));
static cl::alias NoWarnW("W", cl::desc("Alias for --no-warn"), cl::aliasopt(NoWarn));

static cl::opt<bool> NoDeprecatedWarn("no-deprecated-warn",
                               cl::desc("Suppress all deprecated warnings"));

static cl::opt<std::string>
ABIName("target-abi", cl::Hidden,
        cl::desc("The name of the ABI to be targeted from the backend."),
        cl::init(""));

static MCTargetOptions InitMCTargetOptionsFromFlags() {
  MCTargetOptions Options;
  Options.SanitizeAddress =
      (AsmInstrumentation == MCTargetOptions::AsmInstrumentationAddress);
  Options.MCRelaxAll = RelaxAll;
  Options.MCIncrementalLinkerCompatible = IncrementalLinkerCompatible;
  Options.MCPIECopyRelocations = PIECopyRelocations;
  Options.DwarfVersion = DwarfVersion;
  Options.ShowMCInst = ShowMCInst;
  Options.ABIName = ABIName;
  Options.MCFatalWarnings = FatalWarnings;
  Options.MCNoWarn = NoWarn;
  Options.MCNoDeprecatedWarn = NoDeprecatedWarn;
  return Options;
}

#endif
