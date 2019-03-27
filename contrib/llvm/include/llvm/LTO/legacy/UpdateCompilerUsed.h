//==------ UpdateCompilerUsed.h - LLVM Link Time Optimizer Utility --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares a helper class to update llvm.compiler_used metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_UPDATE_COMPILER_USED_H
#define LLVM_LTO_UPDATE_COMPILER_USED_H

#include "llvm/ADT/StringSet.h"
#include "llvm/IR/GlobalValue.h"

namespace llvm {
class Module;
class TargetMachine;

/// Find all globals in \p TheModule that are referenced in
/// \p AsmUndefinedRefs, as well as the user-supplied functions definitions that
/// are also libcalls, and create or update the magic "llvm.compiler_used"
/// global in \p TheModule.
void updateCompilerUsed(Module &TheModule, const TargetMachine &TM,
                        const StringSet<> &AsmUndefinedRefs);
}

#endif // LLVM_LTO_UPDATE_COMPILER_USED_H
