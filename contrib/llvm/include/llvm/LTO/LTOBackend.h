//===-LTOBackend.h - LLVM Link Time Optimizer Backend ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the "backend" phase of LTO, i.e. it performs
// optimization and code generation on a loaded module. It is generally used
// internally by the LTO class but can also be used independently, for example
// to implement a standalone ThinLTO backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTOBACKEND_H
#define LLVM_LTO_LTOBACKEND_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO/FunctionImport.h"

namespace llvm {

class BitcodeModule;
class Error;
class Module;
class Target;

namespace lto {

/// Runs a regular LTO backend. The regular LTO backend can also act as the
/// regular LTO phase of ThinLTO, which may need to access the combined index.
Error backend(Config &C, AddStreamFn AddStream,
              unsigned ParallelCodeGenParallelismLevel,
              std::unique_ptr<Module> M, ModuleSummaryIndex &CombinedIndex);

/// Runs a ThinLTO backend.
Error thinBackend(Config &C, unsigned Task, AddStreamFn AddStream, Module &M,
                  const ModuleSummaryIndex &CombinedIndex,
                  const FunctionImporter::ImportMapTy &ImportList,
                  const GVSummaryMapTy &DefinedGlobals,
                  MapVector<StringRef, BitcodeModule> &ModuleMap);
}
}

#endif
