//===-- MultiplexConsumer.h - AST Consumer for PCH Generation ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares the MultiplexConsumer class, which can be used to
//  multiplex ASTConsumer and SemaConsumer messages to many consumers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_MULTIPLEXCONSUMER_H
#define LLVM_CLANG_FRONTEND_MULTIPLEXCONSUMER_H

#include "clang/Basic/LLVM.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include <memory>
#include <vector>

namespace clang {

class MultiplexASTMutationListener;

// This ASTDeserializationListener forwards its notifications to a set of
// child listeners.
class MultiplexASTDeserializationListener : public ASTDeserializationListener {
public:
  // Does NOT take ownership of the elements in L.
  MultiplexASTDeserializationListener(
      const std::vector<ASTDeserializationListener *> &L);
  void ReaderInitialized(ASTReader *Reader) override;
  void IdentifierRead(serialization::IdentID ID, IdentifierInfo *II) override;
  void MacroRead(serialization::MacroID ID, MacroInfo *MI) override;
  void TypeRead(serialization::TypeIdx Idx, QualType T) override;
  void DeclRead(serialization::DeclID ID, const Decl *D) override;
  void SelectorRead(serialization::SelectorID iD, Selector Sel) override;
  void MacroDefinitionRead(serialization::PreprocessedEntityID,
                           MacroDefinitionRecord *MD) override;
  void ModuleRead(serialization::SubmoduleID ID, Module *Mod) override;

private:
  std::vector<ASTDeserializationListener *> Listeners;
};

// Has a list of ASTConsumers and calls each of them. Owns its children.
class MultiplexConsumer : public SemaConsumer {
public:
  // Takes ownership of the pointers in C.
  MultiplexConsumer(std::vector<std::unique_ptr<ASTConsumer>> C);
  ~MultiplexConsumer() override;

  // ASTConsumer
  void Initialize(ASTContext &Context) override;
  void HandleCXXStaticMemberVarInstantiation(VarDecl *VD) override;
  bool HandleTopLevelDecl(DeclGroupRef D) override;
  void HandleInlineFunctionDefinition(FunctionDecl *D) override;
  void HandleInterestingDecl(DeclGroupRef D) override;
  void HandleTranslationUnit(ASTContext &Ctx) override;
  void HandleTagDeclDefinition(TagDecl *D) override;
  void HandleTagDeclRequiredDefinition(const TagDecl *D) override;
  void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) override;
  void HandleTopLevelDeclInObjCContainer(DeclGroupRef D) override;
  void HandleImplicitImportDecl(ImportDecl *D) override;
  void CompleteTentativeDefinition(VarDecl *D) override;
  void AssignInheritanceModel(CXXRecordDecl *RD) override;
  void HandleVTable(CXXRecordDecl *RD) override;
  ASTMutationListener *GetASTMutationListener() override;
  ASTDeserializationListener *GetASTDeserializationListener() override;
  void PrintStats() override;
  bool shouldSkipFunctionBody(Decl *D) override;

  // SemaConsumer
  void InitializeSema(Sema &S) override;
  void ForgetSema() override;

private:
  std::vector<std::unique_ptr<ASTConsumer>> Consumers; // Owns these.
  std::unique_ptr<MultiplexASTMutationListener> MutationListener;
  std::unique_ptr<MultiplexASTDeserializationListener> DeserializationListener;
};

}  // end namespace clang

#endif
