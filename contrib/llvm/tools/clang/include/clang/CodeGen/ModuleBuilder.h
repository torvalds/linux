//===--- CodeGen/ModuleBuilder.h - Build LLVM from ASTs ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ModuleBuilder interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_MODULEBUILDER_H
#define LLVM_CLANG_CODEGEN_MODULEBUILDER_H

#include "clang/AST/ASTConsumer.h"

namespace llvm {
  class Constant;
  class LLVMContext;
  class Module;
  class StringRef;
}

namespace clang {
  class CodeGenOptions;
  class CoverageSourceInfo;
  class Decl;
  class DiagnosticsEngine;
  class GlobalDecl;
  class HeaderSearchOptions;
  class LangOptions;
  class PreprocessorOptions;

namespace CodeGen {
  class CodeGenModule;
  class CGDebugInfo;
}

/// The primary public interface to the Clang code generator.
///
/// This is not really an abstract interface.
class CodeGenerator : public ASTConsumer {
  virtual void anchor();

public:
  /// Return an opaque reference to the CodeGenModule object, which can
  /// be used in various secondary APIs.  It is valid as long as the
  /// CodeGenerator exists.
  CodeGen::CodeGenModule &CGM();

  /// Return the module that this code generator is building into.
  ///
  /// This may return null after HandleTranslationUnit is called;
  /// this signifies that there was an error generating code.  A
  /// diagnostic will have been generated in this case, and the module
  /// will be deleted.
  ///
  /// It will also return null if the module is released.
  llvm::Module *GetModule();

  /// Release ownership of the module to the caller.
  ///
  /// It is illegal to call methods other than GetModule on the
  /// CodeGenerator after releasing its module.
  llvm::Module *ReleaseModule();

  /// Return debug info code generator.
  CodeGen::CGDebugInfo *getCGDebugInfo();

  /// Given a mangled name, return a declaration which mangles that way
  /// which has been added to this code generator via a Handle method.
  ///
  /// This may return null if there was no matching declaration.
  const Decl *GetDeclForMangledName(llvm::StringRef MangledName);

  /// Return the LLVM address of the given global entity.
  ///
  /// \param isForDefinition If true, the caller intends to define the
  ///   entity; the object returned will be an llvm::GlobalValue of
  ///   some sort.  If false, the caller just intends to use the entity;
  ///   the object returned may be any sort of constant value, and the
  ///   code generator will schedule the entity for emission if a
  ///   definition has been registered with this code generator.
  llvm::Constant *GetAddrOfGlobal(GlobalDecl decl, bool isForDefinition);

  /// Create a new \c llvm::Module after calling HandleTranslationUnit. This
  /// enable codegen in interactive processing environments.
  llvm::Module* StartModule(llvm::StringRef ModuleName, llvm::LLVMContext &C);
};

/// CreateLLVMCodeGen - Create a CodeGenerator instance.
/// It is the responsibility of the caller to call delete on
/// the allocated CodeGenerator instance.
CodeGenerator *CreateLLVMCodeGen(DiagnosticsEngine &Diags,
                                 llvm::StringRef ModuleName,
                                 const HeaderSearchOptions &HeaderSearchOpts,
                                 const PreprocessorOptions &PreprocessorOpts,
                                 const CodeGenOptions &CGO,
                                 llvm::LLVMContext& C,
                                 CoverageSourceInfo *CoverageInfo = nullptr);

} // end namespace clang

#endif
