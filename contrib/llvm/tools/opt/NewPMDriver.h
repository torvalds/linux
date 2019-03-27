//===- NewPMDriver.h - Function to drive opt with the new PM ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A single function which is called to drive the opt behavior for the new
/// PassManager.
///
/// This is only in a separate TU with a header to avoid including all of the
/// old pass manager headers and the new pass manager headers into the same
/// file. Eventually all of the routines here will get folded back into
/// opt.cpp.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OPT_NEWPMDRIVER_H
#define LLVM_TOOLS_OPT_NEWPMDRIVER_H

namespace llvm {
class StringRef;
class LLVMContext;
class Module;
class TargetMachine;
class ToolOutputFile;

namespace opt_tool {
enum OutputKind {
  OK_NoOutput,
  OK_OutputAssembly,
  OK_OutputBitcode,
  OK_OutputThinLTOBitcode,
};
enum VerifierKind {
  VK_NoVerifier,
  VK_VerifyInAndOut,
  VK_VerifyEachPass
};
}

/// Driver function to run the new pass manager over a module.
///
/// This function only exists factored away from opt.cpp in order to prevent
/// inclusion of the new pass manager headers and the old headers into the same
/// file. It's interface is consequentially somewhat ad-hoc, but will go away
/// when the transition finishes.
///
/// ThinLTOLinkOut is only used when OK is OK_OutputThinLTOBitcode, and can be
/// nullptr.
bool runPassPipeline(StringRef Arg0, Module &M, TargetMachine *TM,
                     ToolOutputFile *Out, ToolOutputFile *ThinLinkOut,
                     ToolOutputFile *OptRemarkFile, StringRef PassPipeline,
                     opt_tool::OutputKind OK, opt_tool::VerifierKind VK,
                     bool ShouldPreserveAssemblyUseListOrder,
                     bool ShouldPreserveBitcodeUseListOrder,
                     bool EmitSummaryIndex, bool EmitModuleHash,
                     bool EnableDebugify);
} // namespace llvm

#endif
