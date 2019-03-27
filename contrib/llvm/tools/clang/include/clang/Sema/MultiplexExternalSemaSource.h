//===--- MultiplexExternalSemaSource.h - External Sema Interface-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines ExternalSemaSource interface, dispatching to all clients
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_SEMA_MULTIPLEXEXTERNALSEMASOURCE_H
#define LLVM_CLANG_SEMA_MULTIPLEXEXTERNALSEMASOURCE_H

#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/Weak.h"
#include "llvm/ADT/SmallVector.h"
#include <utility>

namespace clang {

  class CXXConstructorDecl;
  class CXXRecordDecl;
  class DeclaratorDecl;
  struct ExternalVTableUse;
  class LookupResult;
  class NamespaceDecl;
  class Scope;
  class Sema;
  class TypedefNameDecl;
  class ValueDecl;
  class VarDecl;


/// An abstract interface that should be implemented by
/// external AST sources that also provide information for semantic
/// analysis.
class MultiplexExternalSemaSource : public ExternalSemaSource {

private:
  SmallVector<ExternalSemaSource *, 2> Sources; // doesn't own them.

public:

  ///Constructs a new multiplexing external sema source and appends the
  /// given element to it.
  ///
  ///\param[in] s1 - A non-null (old) ExternalSemaSource.
  ///\param[in] s2 - A non-null (new) ExternalSemaSource.
  ///
  MultiplexExternalSemaSource(ExternalSemaSource& s1, ExternalSemaSource& s2);

  ~MultiplexExternalSemaSource() override;

  ///Appends new source to the source list.
  ///
  ///\param[in] source - An ExternalSemaSource.
  ///
  void addSource(ExternalSemaSource &source);

  //===--------------------------------------------------------------------===//
  // ExternalASTSource.
  //===--------------------------------------------------------------------===//

  /// Resolve a declaration ID into a declaration, potentially
  /// building a new declaration.
  Decl *GetExternalDecl(uint32_t ID) override;

  /// Complete the redeclaration chain if it's been extended since the
  /// previous generation of the AST source.
  void CompleteRedeclChain(const Decl *D) override;

  /// Resolve a selector ID into a selector.
  Selector GetExternalSelector(uint32_t ID) override;

  /// Returns the number of selectors known to the external AST
  /// source.
  uint32_t GetNumExternalSelectors() override;

  /// Resolve the offset of a statement in the decl stream into
  /// a statement.
  Stmt *GetExternalDeclStmt(uint64_t Offset) override;

  /// Resolve the offset of a set of C++ base specifiers in the decl
  /// stream into an array of specifiers.
  CXXBaseSpecifier *GetExternalCXXBaseSpecifiers(uint64_t Offset) override;

  /// Resolve a handle to a list of ctor initializers into the list of
  /// initializers themselves.
  CXXCtorInitializer **GetExternalCXXCtorInitializers(uint64_t Offset) override;

  ExtKind hasExternalDefinitions(const Decl *D) override;

  /// Find all declarations with the given name in the
  /// given context.
  bool FindExternalVisibleDeclsByName(const DeclContext *DC,
                                      DeclarationName Name) override;

  /// Ensures that the table of all visible declarations inside this
  /// context is up to date.
  void completeVisibleDeclsMap(const DeclContext *DC) override;

  /// Finds all declarations lexically contained within the given
  /// DeclContext, after applying an optional filter predicate.
  ///
  /// \param IsKindWeWant a predicate function that returns true if the passed
  /// declaration kind is one we are looking for.
  void
  FindExternalLexicalDecls(const DeclContext *DC,
                           llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
                           SmallVectorImpl<Decl *> &Result) override;

  /// Get the decls that are contained in a file in the Offset/Length
  /// range. \p Length can be 0 to indicate a point at \p Offset instead of
  /// a range.
  void FindFileRegionDecls(FileID File, unsigned Offset,unsigned Length,
                           SmallVectorImpl<Decl *> &Decls) override;

  /// Gives the external AST source an opportunity to complete
  /// an incomplete type.
  void CompleteType(TagDecl *Tag) override;

  /// Gives the external AST source an opportunity to complete an
  /// incomplete Objective-C class.
  ///
  /// This routine will only be invoked if the "externally completed" bit is
  /// set on the ObjCInterfaceDecl via the function
  /// \c ObjCInterfaceDecl::setExternallyCompleted().
  void CompleteType(ObjCInterfaceDecl *Class) override;

  /// Loads comment ranges.
  void ReadComments() override;

  /// Notify ExternalASTSource that we started deserialization of
  /// a decl or type so until FinishedDeserializing is called there may be
  /// decls that are initializing. Must be paired with FinishedDeserializing.
  void StartedDeserializing() override;

  /// Notify ExternalASTSource that we finished the deserialization of
  /// a decl or type. Must be paired with StartedDeserializing.
  void FinishedDeserializing() override;

  /// Function that will be invoked when we begin parsing a new
  /// translation unit involving this external AST source.
  void StartTranslationUnit(ASTConsumer *Consumer) override;

  /// Print any statistics that have been gathered regarding
  /// the external AST source.
  void PrintStats() override;

  /// Retrieve the module that corresponds to the given module ID.
  Module *getModule(unsigned ID) override;

  bool DeclIsFromPCHWithObjectFile(const Decl *D) override;

  /// Perform layout on the given record.
  ///
  /// This routine allows the external AST source to provide an specific
  /// layout for a record, overriding the layout that would normally be
  /// constructed. It is intended for clients who receive specific layout
  /// details rather than source code (such as LLDB). The client is expected
  /// to fill in the field offsets, base offsets, virtual base offsets, and
  /// complete object size.
  ///
  /// \param Record The record whose layout is being requested.
  ///
  /// \param Size The final size of the record, in bits.
  ///
  /// \param Alignment The final alignment of the record, in bits.
  ///
  /// \param FieldOffsets The offset of each of the fields within the record,
  /// expressed in bits. All of the fields must be provided with offsets.
  ///
  /// \param BaseOffsets The offset of each of the direct, non-virtual base
  /// classes. If any bases are not given offsets, the bases will be laid
  /// out according to the ABI.
  ///
  /// \param VirtualBaseOffsets The offset of each of the virtual base classes
  /// (either direct or not). If any bases are not given offsets, the bases will
  /// be laid out according to the ABI.
  ///
  /// \returns true if the record layout was provided, false otherwise.
  bool
  layoutRecordType(const RecordDecl *Record,
                   uint64_t &Size, uint64_t &Alignment,
                   llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
                 llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
                 llvm::DenseMap<const CXXRecordDecl *,
                                CharUnits> &VirtualBaseOffsets) override;

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  void getMemoryBufferSizes(MemoryBufferSizes &sizes) const override;

  //===--------------------------------------------------------------------===//
  // ExternalSemaSource.
  //===--------------------------------------------------------------------===//

  /// Initialize the semantic source with the Sema instance
  /// being used to perform semantic analysis on the abstract syntax
  /// tree.
  void InitializeSema(Sema &S) override;

  /// Inform the semantic consumer that Sema is no longer available.
  void ForgetSema() override;

  /// Load the contents of the global method pool for a given
  /// selector.
  void ReadMethodPool(Selector Sel) override;

  /// Load the contents of the global method pool for a given
  /// selector if necessary.
  void updateOutOfDateSelector(Selector Sel) override;

  /// Load the set of namespaces that are known to the external source,
  /// which will be used during typo correction.
  void
  ReadKnownNamespaces(SmallVectorImpl<NamespaceDecl*> &Namespaces) override;

  /// Load the set of used but not defined functions or variables with
  /// internal linkage, or used but not defined inline functions.
  void ReadUndefinedButUsed(
      llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) override;

  void ReadMismatchingDeleteExpressions(llvm::MapVector<
      FieldDecl *, llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &
                                            Exprs) override;

  /// Do last resort, unqualified lookup on a LookupResult that
  /// Sema cannot find.
  ///
  /// \param R a LookupResult that is being recovered.
  ///
  /// \param S the Scope of the identifier occurrence.
  ///
  /// \return true to tell Sema to recover using the LookupResult.
  bool LookupUnqualified(LookupResult &R, Scope *S) override;

  /// Read the set of tentative definitions known to the external Sema
  /// source.
  ///
  /// The external source should append its own tentative definitions to the
  /// given vector of tentative definitions. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadTentativeDefinitions(SmallVectorImpl<VarDecl*> &Defs) override;

  /// Read the set of unused file-scope declarations known to the
  /// external Sema source.
  ///
  /// The external source should append its own unused, filed-scope to the
  /// given vector of declarations. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadUnusedFileScopedDecls(
                        SmallVectorImpl<const DeclaratorDecl*> &Decls) override;

  /// Read the set of delegating constructors known to the
  /// external Sema source.
  ///
  /// The external source should append its own delegating constructors to the
  /// given vector of declarations. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadDelegatingConstructors(
                          SmallVectorImpl<CXXConstructorDecl*> &Decls) override;

  /// Read the set of ext_vector type declarations known to the
  /// external Sema source.
  ///
  /// The external source should append its own ext_vector type declarations to
  /// the given vector of declarations. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadExtVectorDecls(SmallVectorImpl<TypedefNameDecl*> &Decls) override;

  /// Read the set of potentially unused typedefs known to the source.
  ///
  /// The external source should append its own potentially unused local
  /// typedefs to the given vector of declarations. Note that this routine may
  /// be invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadUnusedLocalTypedefNameCandidates(
      llvm::SmallSetVector<const TypedefNameDecl *, 4> &Decls) override;

  /// Read the set of referenced selectors known to the
  /// external Sema source.
  ///
  /// The external source should append its own referenced selectors to the
  /// given vector of selectors. Note that this routine
  /// may be invoked multiple times; the external source should take care not
  /// to introduce the same selectors repeatedly.
  void ReadReferencedSelectors(SmallVectorImpl<std::pair<Selector,
                                              SourceLocation> > &Sels) override;

  /// Read the set of weak, undeclared identifiers known to the
  /// external Sema source.
  ///
  /// The external source should append its own weak, undeclared identifiers to
  /// the given vector. Note that this routine may be invoked multiple times;
  /// the external source should take care not to introduce the same identifiers
  /// repeatedly.
  void ReadWeakUndeclaredIdentifiers(
           SmallVectorImpl<std::pair<IdentifierInfo*, WeakInfo> > &WI) override;

  /// Read the set of used vtables known to the external Sema source.
  ///
  /// The external source should append its own used vtables to the given
  /// vector. Note that this routine may be invoked multiple times; the external
  /// source should take care not to introduce the same vtables repeatedly.
  void ReadUsedVTables(SmallVectorImpl<ExternalVTableUse> &VTables) override;

  /// Read the set of pending instantiations known to the external
  /// Sema source.
  ///
  /// The external source should append its own pending instantiations to the
  /// given vector. Note that this routine may be invoked multiple times; the
  /// external source should take care not to introduce the same instantiations
  /// repeatedly.
  void ReadPendingInstantiations(
     SmallVectorImpl<std::pair<ValueDecl*, SourceLocation> >& Pending) override;

  /// Read the set of late parsed template functions for this source.
  ///
  /// The external source should insert its own late parsed template functions
  /// into the map. Note that this routine may be invoked multiple times; the
  /// external source should take care not to introduce the same map entries
  /// repeatedly.
  void ReadLateParsedTemplates(
      llvm::MapVector<const FunctionDecl *, std::unique_ptr<LateParsedTemplate>>
          &LPTMap) override;

  /// \copydoc ExternalSemaSource::CorrectTypo
  /// \note Returns the first nonempty correction.
  TypoCorrection CorrectTypo(const DeclarationNameInfo &Typo,
                             int LookupKind, Scope *S, CXXScopeSpec *SS,
                             CorrectionCandidateCallback &CCC,
                             DeclContext *MemberContext,
                             bool EnteringContext,
                             const ObjCObjectPointerType *OPT) override;

  /// Produces a diagnostic note if one of the attached sources
  /// contains a complete definition for \p T. Queries the sources in list
  /// order until the first one claims that a diagnostic was produced.
  ///
  /// \param Loc the location at which a complete type was required but not
  /// provided
  ///
  /// \param T the \c QualType that should have been complete at \p Loc
  ///
  /// \return true if a diagnostic was produced, false otherwise.
  bool MaybeDiagnoseMissingCompleteType(SourceLocation Loc,
                                        QualType T) override;

  // isa/cast/dyn_cast support
  static bool classof(const MultiplexExternalSemaSource*) { return true; }
  //static bool classof(const ExternalSemaSource*) { return true; }
};

} // end namespace clang

#endif
