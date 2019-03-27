//===- SampleProfile.h - SamplePGO pass ---------- --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the sampled PGO loader pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLEPROFILE_H
#define LLVM_TRANSFORMS_IPO_SAMPLEPROFILE_H

#include "llvm/IR/PassManager.h"
#include <string>

namespace llvm {

class Module;

/// The sample profiler data loader pass.
class SampleProfileLoaderPass : public PassInfoMixin<SampleProfileLoaderPass> {
public:
  SampleProfileLoaderPass(std::string File = "", std::string RemappingFile = "",
                          bool IsThinLTOPreLink = false)
      : ProfileFileName(File), ProfileRemappingFileName(RemappingFile),
        IsThinLTOPreLink(IsThinLTOPreLink) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  std::string ProfileFileName;
  std::string ProfileRemappingFileName;
  bool IsThinLTOPreLink;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SAMPLEPROFILE_H
