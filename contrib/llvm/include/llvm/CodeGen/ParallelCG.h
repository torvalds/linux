//===-- llvm/CodeGen/ParallelCG.h - Parallel code generation ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header declares functions that can be used for parallel code generation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PARALLELCG_H
#define LLVM_CODEGEN_PARALLELCG_H

#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetMachine.h"

#include <functional>

namespace llvm {

template <typename T> class ArrayRef;
class Module;
class TargetOptions;
class raw_pwrite_stream;

/// Split M into OSs.size() partitions, and generate code for each. Takes a
/// factory function for the TargetMachine TMFactory. Writes OSs.size() output
/// files to the output streams in OSs. The resulting output files if linked
/// together are intended to be equivalent to the single output file that would
/// have been code generated from M.
///
/// Writes bitcode for individual partitions into output streams in BCOSs, if
/// BCOSs is not empty.
///
/// \returns M if OSs.size() == 1, otherwise returns std::unique_ptr<Module>().
std::unique_ptr<Module>
splitCodeGen(std::unique_ptr<Module> M, ArrayRef<raw_pwrite_stream *> OSs,
             ArrayRef<llvm::raw_pwrite_stream *> BCOSs,
             const std::function<std::unique_ptr<TargetMachine>()> &TMFactory,
             TargetMachine::CodeGenFileType FileType = TargetMachine::CGFT_ObjectFile,
             bool PreserveLocals = false);

} // namespace llvm

#endif
