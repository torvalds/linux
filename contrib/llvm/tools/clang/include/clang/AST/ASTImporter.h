//===- ASTImporter.h - Importing ASTs from other Contexts -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporter class which imports AST nodes from one
//  context into another context.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTIMPORTER_H
#define LLVM_CLANG_AST_ASTIMPORTER_H

#include "clang/AST/DeclarationName.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <utility>

namespace clang {

class ASTContext;
class ASTImporterLookupTable;
class CXXBaseSpecifier;
class CXXCtorInitializer;
class Decl;
class DeclContext;
class Expr;
class FileManager;
class NamedDecl;
class Stmt;
class TagDecl;
class TypeSourceInfo;
class Attr;

  class ImportError : public llvm::ErrorInfo<ImportError> {
  public:
    /// \brief Kind of error when importing an AST component.
    enum ErrorKind {
        NameConflict, /// Naming ambiguity (likely ODR violation).
        UnsupportedConstruct, /// Not supported node or case.
        Unknown /// Other error.
    };

    ErrorKind Error;

    static char ID;

    ImportError() : Error(Unknown) { }
    ImportError(const ImportError &Other) : Error(Other.Error) { }
    ImportError(ErrorKind Error) : Error(Error) { }

    std::string toString() const;

    void log(raw_ostream &OS) const override;
    std::error_code convertToErrorCode() const override;
  };

  // \brief Returns with a list of declarations started from the canonical decl
  // then followed by subsequent decls in the translation unit.
  // This gives a canonical list for each entry in the redecl chain.
  // `Decl::redecls()` gives a list of decls which always start from the
  // previous decl and the next item is actually the previous item in the order
  // of source locations.  Thus, `Decl::redecls()` gives different lists for
  // the different entries in a given redecl chain.
  llvm::SmallVector<Decl*, 2> getCanonicalForwardRedeclChain(Decl* D);

  /// Imports selected nodes from one AST context into another context,
  /// merging AST nodes where appropriate.
  class ASTImporter {
    friend class ASTNodeImporter;
  public:
    using NonEquivalentDeclSet = llvm::DenseSet<std::pair<Decl *, Decl *>>;
    using ImportedCXXBaseSpecifierMap =
        llvm::DenseMap<const CXXBaseSpecifier *, CXXBaseSpecifier *>;

  private:

    /// Pointer to the import specific lookup table, which may be shared
    /// amongst several ASTImporter objects.
    /// This is an externally managed resource (and should exist during the
    /// lifetime of the ASTImporter object)
    /// If not set then the original C/C++ lookup is used.
    ASTImporterLookupTable *LookupTable = nullptr;

    /// The contexts we're importing to and from.
    ASTContext &ToContext, &FromContext;

    /// The file managers we're importing to and from.
    FileManager &ToFileManager, &FromFileManager;

    /// Whether to perform a minimal import.
    bool Minimal;

    /// Whether the last diagnostic came from the "from" context.
    bool LastDiagFromFrom = false;

    /// Mapping from the already-imported types in the "from" context
    /// to the corresponding types in the "to" context.
    llvm::DenseMap<const Type *, const Type *> ImportedTypes;

    /// Mapping from the already-imported declarations in the "from"
    /// context to the corresponding declarations in the "to" context.
    llvm::DenseMap<Decl *, Decl *> ImportedDecls;

    /// Mapping from the already-imported statements in the "from"
    /// context to the corresponding statements in the "to" context.
    llvm::DenseMap<Stmt *, Stmt *> ImportedStmts;

    /// Mapping from the already-imported FileIDs in the "from" source
    /// manager to the corresponding FileIDs in the "to" source manager.
    llvm::DenseMap<FileID, FileID> ImportedFileIDs;

    /// Mapping from the already-imported CXXBasesSpecifier in
    ///  the "from" source manager to the corresponding CXXBasesSpecifier
    ///  in the "to" source manager.
    ImportedCXXBaseSpecifierMap ImportedCXXBaseSpecifiers;

    /// Declaration (from, to) pairs that are known not to be equivalent
    /// (which we have already complained about).
    NonEquivalentDeclSet NonEquivalentDecls;

    using FoundDeclsTy = SmallVector<NamedDecl *, 2>;
    FoundDeclsTy findDeclsInToCtx(DeclContext *DC, DeclarationName Name);

    void AddToLookupTable(Decl *ToD);

  public:

    /// \param ToContext The context we'll be importing into.
    ///
    /// \param ToFileManager The file manager we'll be importing into.
    ///
    /// \param FromContext The context we'll be importing from.
    ///
    /// \param FromFileManager The file manager we'll be importing into.
    ///
    /// \param MinimalImport If true, the importer will attempt to import
    /// as little as it can, e.g., by importing declarations as forward
    /// declarations that can be completed at a later point.
    ///
    /// \param LookupTable The importer specific lookup table which may be
    /// shared amongst several ASTImporter objects.
    /// If not set then the original C/C++ lookup is used.
    ASTImporter(ASTContext &ToContext, FileManager &ToFileManager,
                ASTContext &FromContext, FileManager &FromFileManager,
                bool MinimalImport,
                ASTImporterLookupTable *LookupTable = nullptr);

    virtual ~ASTImporter();

    /// Whether the importer will perform a minimal import, creating
    /// to-be-completed forward declarations when possible.
    bool isMinimalImport() const { return Minimal; }

    /// \brief Import the given object, returns the result.
    ///
    /// \param To Import the object into this variable.
    /// \param From Object to import.
    /// \return Error information (success or error).
    template <typename ImportT>
    LLVM_NODISCARD llvm::Error importInto(ImportT &To, const ImportT &From) {
      To = Import(From);
      if (From && !To)
          return llvm::make_error<ImportError>();
      return llvm::Error::success();
      // FIXME: this should be the final code
      //auto ToOrErr = Import(From);
      //if (ToOrErr)
      //  To = *ToOrErr;
      //return ToOrErr.takeError();
    }

    /// Import the given type from the "from" context into the "to"
    /// context. A null type is imported as a null type (no error).
    ///
    /// \returns The equivalent type in the "to" context, or the import error.
    llvm::Expected<QualType> Import_New(QualType FromT);
    // FIXME: Remove this version.
    QualType Import(QualType FromT);

    /// Import the given type source information from the
    /// "from" context into the "to" context.
    ///
    /// \returns The equivalent type source information in the "to"
    /// context, or the import error.
    llvm::Expected<TypeSourceInfo *> Import_New(TypeSourceInfo *FromTSI);
    // FIXME: Remove this version.
    TypeSourceInfo *Import(TypeSourceInfo *FromTSI);

    /// Import the given attribute from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent attribute in the "to" context, or the import
    /// error.
    llvm::Expected<Attr *> Import_New(const Attr *FromAttr);
    // FIXME: Remove this version.
    Attr *Import(const Attr *FromAttr);

    /// Import the given declaration from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent declaration in the "to" context, or the import
    /// error.
    llvm::Expected<Decl *> Import_New(Decl *FromD);
    llvm::Expected<Decl *> Import_New(const Decl *FromD) {
      return Import_New(const_cast<Decl *>(FromD));
    }
    // FIXME: Remove this version.
    Decl *Import(Decl *FromD);
    Decl *Import(const Decl *FromD) {
      return Import(const_cast<Decl *>(FromD));
    }

    /// Return the copy of the given declaration in the "to" context if
    /// it has already been imported from the "from" context.  Otherwise return
    /// NULL.
    Decl *GetAlreadyImportedOrNull(const Decl *FromD) const;

    /// Import the given declaration context from the "from"
    /// AST context into the "to" AST context.
    ///
    /// \returns the equivalent declaration context in the "to"
    /// context, or error value.
    llvm::Expected<DeclContext *> ImportContext(DeclContext *FromDC);

    /// Import the given expression from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent expression in the "to" context, or the import
    /// error.
    llvm::Expected<Expr *> Import_New(Expr *FromE);
    // FIXME: Remove this version.
    Expr *Import(Expr *FromE);

    /// Import the given statement from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent statement in the "to" context, or the import
    /// error.
    llvm::Expected<Stmt *> Import_New(Stmt *FromS);
    // FIXME: Remove this version.
    Stmt *Import(Stmt *FromS);

    /// Import the given nested-name-specifier from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent nested-name-specifier in the "to"
    /// context, or the import error.
    llvm::Expected<NestedNameSpecifier *>
    Import_New(NestedNameSpecifier *FromNNS);
    // FIXME: Remove this version.
    NestedNameSpecifier *Import(NestedNameSpecifier *FromNNS);

    /// Import the given nested-name-specifier-loc from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent nested-name-specifier-loc in the "to"
    /// context, or the import error.
    llvm::Expected<NestedNameSpecifierLoc>
    Import_New(NestedNameSpecifierLoc FromNNS);
    // FIXME: Remove this version.
    NestedNameSpecifierLoc Import(NestedNameSpecifierLoc FromNNS);

    /// Import the given template name from the "from" context into the
    /// "to" context, or the import error.
    llvm::Expected<TemplateName> Import_New(TemplateName From);
    // FIXME: Remove this version.
    TemplateName Import(TemplateName From);

    /// Import the given source location from the "from" context into
    /// the "to" context.
    ///
    /// \returns The equivalent source location in the "to" context, or the
    /// import error.
    llvm::Expected<SourceLocation> Import_New(SourceLocation FromLoc);
    // FIXME: Remove this version.
    SourceLocation Import(SourceLocation FromLoc);

    /// Import the given source range from the "from" context into
    /// the "to" context.
    ///
    /// \returns The equivalent source range in the "to" context, or the import
    /// error.
    llvm::Expected<SourceRange> Import_New(SourceRange FromRange);
    // FIXME: Remove this version.
    SourceRange Import(SourceRange FromRange);

    /// Import the given declaration name from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent declaration name in the "to" context, or the
    /// import error.
    llvm::Expected<DeclarationName> Import_New(DeclarationName FromName);
    // FIXME: Remove this version.
    DeclarationName Import(DeclarationName FromName);

    /// Import the given identifier from the "from" context
    /// into the "to" context.
    ///
    /// \returns The equivalent identifier in the "to" context. Note: It
    /// returns nullptr only if the FromId was nullptr.
    IdentifierInfo *Import(const IdentifierInfo *FromId);

    /// Import the given Objective-C selector from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent selector in the "to" context, or the import
    /// error.
    llvm::Expected<Selector> Import_New(Selector FromSel);
    // FIXME: Remove this version.
    Selector Import(Selector FromSel);

    /// Import the given file ID from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent file ID in the source manager of the "to"
    /// context, or the import error.
    llvm::Expected<FileID> Import_New(FileID);
    // FIXME: Remove this version.
    FileID Import(FileID);

    /// Import the given C++ constructor initializer from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent initializer in the "to" context, or the import
    /// error.
    llvm::Expected<CXXCtorInitializer *>
    Import_New(CXXCtorInitializer *FromInit);
    // FIXME: Remove this version.
    CXXCtorInitializer *Import(CXXCtorInitializer *FromInit);

    /// Import the given CXXBaseSpecifier from the "from" context into
    /// the "to" context.
    ///
    /// \returns The equivalent CXXBaseSpecifier in the source manager of the
    /// "to" context, or the import error.
    llvm::Expected<CXXBaseSpecifier *>
    Import_New(const CXXBaseSpecifier *FromSpec);
    // FIXME: Remove this version.
    CXXBaseSpecifier *Import(const CXXBaseSpecifier *FromSpec);

    /// Import the definition of the given declaration, including all of
    /// the declarations it contains.
    LLVM_NODISCARD llvm::Error ImportDefinition_New(Decl *From);

    // FIXME: Compatibility function.
    // Usages of this should be changed to ImportDefinition_New.
    void ImportDefinition(Decl *From);

    /// Cope with a name conflict when importing a declaration into the
    /// given context.
    ///
    /// This routine is invoked whenever there is a name conflict while
    /// importing a declaration. The returned name will become the name of the
    /// imported declaration. By default, the returned name is the same as the
    /// original name, leaving the conflict unresolve such that name lookup
    /// for this name is likely to find an ambiguity later.
    ///
    /// Subclasses may override this routine to resolve the conflict, e.g., by
    /// renaming the declaration being imported.
    ///
    /// \param Name the name of the declaration being imported, which conflicts
    /// with other declarations.
    ///
    /// \param DC the declaration context (in the "to" AST context) in which
    /// the name is being imported.
    ///
    /// \param IDNS the identifier namespace in which the name will be found.
    ///
    /// \param Decls the set of declarations with the same name as the
    /// declaration being imported.
    ///
    /// \param NumDecls the number of conflicting declarations in \p Decls.
    ///
    /// \returns the name that the newly-imported declaration should have.
    virtual DeclarationName HandleNameConflict(DeclarationName Name,
                                               DeclContext *DC,
                                               unsigned IDNS,
                                               NamedDecl **Decls,
                                               unsigned NumDecls);

    /// Retrieve the context that AST nodes are being imported into.
    ASTContext &getToContext() const { return ToContext; }

    /// Retrieve the context that AST nodes are being imported from.
    ASTContext &getFromContext() const { return FromContext; }

    /// Retrieve the file manager that AST nodes are being imported into.
    FileManager &getToFileManager() const { return ToFileManager; }

    /// Retrieve the file manager that AST nodes are being imported from.
    FileManager &getFromFileManager() const { return FromFileManager; }

    /// Report a diagnostic in the "to" context.
    DiagnosticBuilder ToDiag(SourceLocation Loc, unsigned DiagID);

    /// Report a diagnostic in the "from" context.
    DiagnosticBuilder FromDiag(SourceLocation Loc, unsigned DiagID);

    /// Return the set of declarations that we know are not equivalent.
    NonEquivalentDeclSet &getNonEquivalentDecls() { return NonEquivalentDecls; }

    /// Called for ObjCInterfaceDecl, ObjCProtocolDecl, and TagDecl.
    /// Mark the Decl as complete, filling it in as much as possible.
    ///
    /// \param D A declaration in the "to" context.
    virtual void CompleteDecl(Decl* D);

    /// Subclasses can override this function to observe all of the \c From ->
    /// \c To declaration mappings as they are imported.
    virtual Decl *Imported(Decl *From, Decl *To) { return To; }

    /// Store and assign the imported declaration to its counterpart.
    Decl *MapImported(Decl *From, Decl *To);

    /// Called by StructuralEquivalenceContext.  If a RecordDecl is
    /// being compared to another RecordDecl as part of import, completing the
    /// other RecordDecl may trigger importation of the first RecordDecl. This
    /// happens especially for anonymous structs.  If the original of the second
    /// RecordDecl can be found, we can complete it without the need for
    /// importation, eliminating this loop.
    virtual Decl *GetOriginalDecl(Decl *To) { return nullptr; }

    /// Determine whether the given types are structurally
    /// equivalent.
    bool IsStructurallyEquivalent(QualType From, QualType To,
                                  bool Complain = true);

    /// Determine the index of a field in its parent record.
    /// F should be a field (or indirect field) declaration.
    /// \returns The index of the field in its parent context (starting from 0).
    /// On error `None` is returned (parent context is non-record).
    static llvm::Optional<unsigned> getFieldIndex(Decl *F);

  };

} // namespace clang

#endif // LLVM_CLANG_AST_ASTIMPORTER_H
