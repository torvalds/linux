//===--- MultiplexExternalSemaSource.cpp  ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the event dispatching to the subscribed clients.
//
//===----------------------------------------------------------------------===//
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/Sema/Lookup.h"

using namespace clang;

///Constructs a new multiplexing external sema source and appends the
/// given element to it.
///
MultiplexExternalSemaSource::MultiplexExternalSemaSource(ExternalSemaSource &s1,
                                                        ExternalSemaSource &s2){
  Sources.push_back(&s1);
  Sources.push_back(&s2);
}

// pin the vtable here.
MultiplexExternalSemaSource::~MultiplexExternalSemaSource() {}

///Appends new source to the source list.
///
///\param[in] source - An ExternalSemaSource.
///
void MultiplexExternalSemaSource::addSource(ExternalSemaSource &source) {
  Sources.push_back(&source);
}

//===----------------------------------------------------------------------===//
// ExternalASTSource.
//===----------------------------------------------------------------------===//

Decl *MultiplexExternalSemaSource::GetExternalDecl(uint32_t ID) {
  for(size_t i = 0; i < Sources.size(); ++i)
    if (Decl *Result = Sources[i]->GetExternalDecl(ID))
      return Result;
  return nullptr;
}

void MultiplexExternalSemaSource::CompleteRedeclChain(const Decl *D) {
  for (size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->CompleteRedeclChain(D);
}

Selector MultiplexExternalSemaSource::GetExternalSelector(uint32_t ID) {
  Selector Sel;
  for(size_t i = 0; i < Sources.size(); ++i) {
    Sel = Sources[i]->GetExternalSelector(ID);
    if (!Sel.isNull())
      return Sel;
  }
  return Sel;
}

uint32_t MultiplexExternalSemaSource::GetNumExternalSelectors() {
  uint32_t total = 0;
  for(size_t i = 0; i < Sources.size(); ++i)
    total += Sources[i]->GetNumExternalSelectors();
  return total;
}

Stmt *MultiplexExternalSemaSource::GetExternalDeclStmt(uint64_t Offset) {
  for(size_t i = 0; i < Sources.size(); ++i)
    if (Stmt *Result = Sources[i]->GetExternalDeclStmt(Offset))
      return Result;
  return nullptr;
}

CXXBaseSpecifier *MultiplexExternalSemaSource::GetExternalCXXBaseSpecifiers(
                                                               uint64_t Offset){
  for(size_t i = 0; i < Sources.size(); ++i)
    if (CXXBaseSpecifier *R = Sources[i]->GetExternalCXXBaseSpecifiers(Offset))
      return R;
  return nullptr;
}

CXXCtorInitializer **
MultiplexExternalSemaSource::GetExternalCXXCtorInitializers(uint64_t Offset) {
  for (auto *S : Sources)
    if (auto *R = S->GetExternalCXXCtorInitializers(Offset))
      return R;
  return nullptr;
}

ExternalASTSource::ExtKind
MultiplexExternalSemaSource::hasExternalDefinitions(const Decl *D) {
  for (const auto &S : Sources)
    if (auto EK = S->hasExternalDefinitions(D))
      if (EK != EK_ReplyHazy)
        return EK;
  return EK_ReplyHazy;
}

bool MultiplexExternalSemaSource::
FindExternalVisibleDeclsByName(const DeclContext *DC, DeclarationName Name) {
  bool AnyDeclsFound = false;
  for (size_t i = 0; i < Sources.size(); ++i)
    AnyDeclsFound |= Sources[i]->FindExternalVisibleDeclsByName(DC, Name);
  return AnyDeclsFound;
}

void MultiplexExternalSemaSource::completeVisibleDeclsMap(const DeclContext *DC){
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->completeVisibleDeclsMap(DC);
}

void MultiplexExternalSemaSource::FindExternalLexicalDecls(
    const DeclContext *DC, llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
    SmallVectorImpl<Decl *> &Result) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
}

void MultiplexExternalSemaSource::FindFileRegionDecls(FileID File,
                                                      unsigned Offset,
                                                      unsigned Length,
                                                SmallVectorImpl<Decl *> &Decls){
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->FindFileRegionDecls(File, Offset, Length, Decls);
}

void MultiplexExternalSemaSource::CompleteType(TagDecl *Tag) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->CompleteType(Tag);
}

void MultiplexExternalSemaSource::CompleteType(ObjCInterfaceDecl *Class) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->CompleteType(Class);
}

void MultiplexExternalSemaSource::ReadComments() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadComments();
}

void MultiplexExternalSemaSource::StartedDeserializing() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->StartedDeserializing();
}

void MultiplexExternalSemaSource::FinishedDeserializing() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->FinishedDeserializing();
}

void MultiplexExternalSemaSource::StartTranslationUnit(ASTConsumer *Consumer) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->StartTranslationUnit(Consumer);
}

void MultiplexExternalSemaSource::PrintStats() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->PrintStats();
}

Module *MultiplexExternalSemaSource::getModule(unsigned ID) {
  for (size_t i = 0; i < Sources.size(); ++i)
    if (auto M = Sources[i]->getModule(ID))
      return M;
  return nullptr;
}

bool MultiplexExternalSemaSource::DeclIsFromPCHWithObjectFile(const Decl *D) {
  for (auto *S : Sources)
    if (S->DeclIsFromPCHWithObjectFile(D))
      return true;
  return false;
}

bool MultiplexExternalSemaSource::layoutRecordType(const RecordDecl *Record,
                                                   uint64_t &Size,
                                                   uint64_t &Alignment,
                      llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
                  llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
          llvm::DenseMap<const CXXRecordDecl *, CharUnits> &VirtualBaseOffsets){
  for(size_t i = 0; i < Sources.size(); ++i)
    if (Sources[i]->layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                     BaseOffsets, VirtualBaseOffsets))
      return true;
  return false;
}

void MultiplexExternalSemaSource::
getMemoryBufferSizes(MemoryBufferSizes &sizes) const {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->getMemoryBufferSizes(sizes);

}

//===----------------------------------------------------------------------===//
// ExternalSemaSource.
//===----------------------------------------------------------------------===//


void MultiplexExternalSemaSource::InitializeSema(Sema &S) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->InitializeSema(S);
}

void MultiplexExternalSemaSource::ForgetSema() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ForgetSema();
}

void MultiplexExternalSemaSource::ReadMethodPool(Selector Sel) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadMethodPool(Sel);
}

void MultiplexExternalSemaSource::updateOutOfDateSelector(Selector Sel) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->updateOutOfDateSelector(Sel);
}

void MultiplexExternalSemaSource::ReadKnownNamespaces(
                                   SmallVectorImpl<NamespaceDecl*> &Namespaces){
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadKnownNamespaces(Namespaces);
}

void MultiplexExternalSemaSource::ReadUndefinedButUsed(
    llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadUndefinedButUsed(Undefined);
}

void MultiplexExternalSemaSource::ReadMismatchingDeleteExpressions(
    llvm::MapVector<FieldDecl *,
                    llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &
        Exprs) {
  for (auto &Source : Sources)
    Source->ReadMismatchingDeleteExpressions(Exprs);
}

bool MultiplexExternalSemaSource::LookupUnqualified(LookupResult &R, Scope *S){
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->LookupUnqualified(R, S);

  return !R.empty();
}

void MultiplexExternalSemaSource::ReadTentativeDefinitions(
                                     SmallVectorImpl<VarDecl*> &TentativeDefs) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadTentativeDefinitions(TentativeDefs);
}

void MultiplexExternalSemaSource::ReadUnusedFileScopedDecls(
                                SmallVectorImpl<const DeclaratorDecl*> &Decls) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadUnusedFileScopedDecls(Decls);
}

void MultiplexExternalSemaSource::ReadDelegatingConstructors(
                                  SmallVectorImpl<CXXConstructorDecl*> &Decls) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadDelegatingConstructors(Decls);
}

void MultiplexExternalSemaSource::ReadExtVectorDecls(
                                     SmallVectorImpl<TypedefNameDecl*> &Decls) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadExtVectorDecls(Decls);
}

void MultiplexExternalSemaSource::ReadUnusedLocalTypedefNameCandidates(
    llvm::SmallSetVector<const TypedefNameDecl *, 4> &Decls) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadUnusedLocalTypedefNameCandidates(Decls);
}

void MultiplexExternalSemaSource::ReadReferencedSelectors(
                  SmallVectorImpl<std::pair<Selector, SourceLocation> > &Sels) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadReferencedSelectors(Sels);
}

void MultiplexExternalSemaSource::ReadWeakUndeclaredIdentifiers(
                   SmallVectorImpl<std::pair<IdentifierInfo*, WeakInfo> > &WI) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadWeakUndeclaredIdentifiers(WI);
}

void MultiplexExternalSemaSource::ReadUsedVTables(
                                  SmallVectorImpl<ExternalVTableUse> &VTables) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadUsedVTables(VTables);
}

void MultiplexExternalSemaSource::ReadPendingInstantiations(
                                           SmallVectorImpl<std::pair<ValueDecl*,
                                                   SourceLocation> > &Pending) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadPendingInstantiations(Pending);
}

void MultiplexExternalSemaSource::ReadLateParsedTemplates(
    llvm::MapVector<const FunctionDecl *, std::unique_ptr<LateParsedTemplate>>
        &LPTMap) {
  for (size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadLateParsedTemplates(LPTMap);
}

TypoCorrection MultiplexExternalSemaSource::CorrectTypo(
                                     const DeclarationNameInfo &Typo,
                                     int LookupKind, Scope *S, CXXScopeSpec *SS,
                                     CorrectionCandidateCallback &CCC,
                                     DeclContext *MemberContext,
                                     bool EnteringContext,
                                     const ObjCObjectPointerType *OPT) {
  for (size_t I = 0, E = Sources.size(); I < E; ++I) {
    if (TypoCorrection C = Sources[I]->CorrectTypo(Typo, LookupKind, S, SS, CCC,
                                                   MemberContext,
                                                   EnteringContext, OPT))
      return C;
  }
  return TypoCorrection();
}

bool MultiplexExternalSemaSource::MaybeDiagnoseMissingCompleteType(
    SourceLocation Loc, QualType T) {
  for (size_t I = 0, E = Sources.size(); I < E; ++I) {
    if (Sources[I]->MaybeDiagnoseMissingCompleteType(Loc, T))
      return true;
  }
  return false;
}
