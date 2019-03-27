//===---- llvm/IRReader/IRReader.h - Reader for LLVM IR files ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines functions for reading LLVM IR. They support both
// Bitcode and Assembly, automatically detecting the input format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IRREADER_IRREADER_H
#define LLVM_IRREADER_IRREADER_H

#include "llvm/ADT/StringRef.h"
#include <memory>

namespace llvm {

class StringRef;
class MemoryBufferRef;
class Module;
class SMDiagnostic;
class LLVMContext;

/// If the given file holds a bitcode image, return a Module
/// for it which does lazy deserialization of function bodies.  Otherwise,
/// attempt to parse it as LLVM Assembly and return a fully populated
/// Module. The ShouldLazyLoadMetadata flag is passed down to the bitcode
/// reader to optionally enable lazy metadata loading.
std::unique_ptr<Module>
getLazyIRFileModule(StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
                    bool ShouldLazyLoadMetadata = false);

/// If the given MemoryBuffer holds a bitcode image, return a Module
/// for it.  Otherwise, attempt to parse it as LLVM Assembly and return
/// a Module for it.
/// \param UpgradeDebugInfo Run UpgradeDebugInfo, which runs the Verifier.
///                         This option should only be set to false by llvm-as
///                         for use inside the LLVM testuite!
/// \param DataLayoutString Override datalayout in the llvm assembly.
std::unique_ptr<Module> parseIR(MemoryBufferRef Buffer, SMDiagnostic &Err,
                                LLVMContext &Context,
                                bool UpgradeDebugInfo = true,
                                StringRef DataLayoutString = "");

/// If the given file holds a bitcode image, return a Module for it.
/// Otherwise, attempt to parse it as LLVM Assembly and return a Module
/// for it.
/// \param UpgradeDebugInfo Run UpgradeDebugInfo, which runs the Verifier.
///                         This option should only be set to false by llvm-as
///                         for use inside the LLVM testuite!
/// \param DataLayoutString Override datalayout in the llvm assembly.
std::unique_ptr<Module> parseIRFile(StringRef Filename, SMDiagnostic &Err,
                                    LLVMContext &Context,
                                    bool UpgradeDebugInfo = true,
                                    StringRef DataLayoutString = "");
}

#endif
