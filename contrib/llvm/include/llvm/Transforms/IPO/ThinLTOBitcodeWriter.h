//===- ThinLTOBitcodeWriter.h - Bitcode writing pass for ThinLTO ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass prepares a module containing type metadata for ThinLTO by splitting
// it into regular and thin LTO parts if possible, and writing both parts to
// a multi-module bitcode file. Modules that do not contain type metadata are
// written unmodified as a single module.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_THINLTOBITCODEWRITER_H
#define LLVM_TRANSFORMS_IPO_THINLTOBITCODEWRITER_H

#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>

namespace llvm {

class ThinLTOBitcodeWriterPass
    : public PassInfoMixin<ThinLTOBitcodeWriterPass> {
  raw_ostream &OS;
  raw_ostream *ThinLinkOS;

public:
  // Writes bitcode to OS. Also write thin link file to ThinLinkOS, if
  // it's not nullptr.
  ThinLTOBitcodeWriterPass(raw_ostream &OS, raw_ostream *ThinLinkOS)
      : OS(OS), ThinLinkOS(ThinLinkOS) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif
