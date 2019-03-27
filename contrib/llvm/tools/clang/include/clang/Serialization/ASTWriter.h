//===- ASTWriter.h - AST File Writer ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTWriter class, which writes an AST file
//  containing a serialized representation of a translation unit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_ASTWRITER_H
#define LLVM_CLANG_SERIALIZATION_ASTWRITER_H

#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitstreamWriter.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class APFloat;
class APInt;
class APSInt;

} // namespace llvm

namespace clang {

class ASTContext;
class ASTReader;
class ASTUnresolvedSet;
class Attr;
class CXXBaseSpecifier;
class CXXCtorInitializer;
class CXXRecordDecl;
class CXXTemporary;
class FileEntry;
class FPOptions;
class FunctionDecl;
class HeaderSearch;
class HeaderSearchOptions;
class IdentifierResolver;
class LangOptions;
class MacroDefinitionRecord;
class MacroInfo;
class MemoryBufferCache;
class Module;
class ModuleFileExtension;
class ModuleFileExtensionWriter;
class NamedDecl;
class NestedNameSpecifier;
class ObjCInterfaceDecl;
class PreprocessingRecord;
class Preprocessor;
struct QualifierInfo;
class RecordDecl;
class Sema;
class SourceManager;
class Stmt;
struct StoredDeclsList;
class SwitchCase;
class TemplateParameterList;
class Token;
class TypeSourceInfo;

/// Writes an AST file containing the contents of a translation unit.
///
/// The ASTWriter class produces a bitstream containing the serialized
/// representation of a given abstract syntax tree and its supporting
/// data structures. This bitstream can be de-serialized via an
/// instance of the ASTReader class.
class ASTWriter : public ASTDeserializationListener,
                  public ASTMutationListener {
public:
  friend class ASTDeclWriter;
  friend class ASTRecordWriter;
  friend class ASTStmtWriter;
  friend class ASTTypeWriter;

  using RecordData = SmallVector<uint64_t, 64>;
  using RecordDataImpl = SmallVectorImpl<uint64_t>;
  using RecordDataRef = ArrayRef<uint64_t>;

private:
  /// Map that provides the ID numbers of each type within the
  /// output stream, plus those deserialized from a chained PCH.
  ///
  /// The ID numbers of types are consecutive (in order of discovery)
  /// and start at 1. 0 is reserved for NULL. When types are actually
  /// stored in the stream, the ID number is shifted by 2 bits to
  /// allow for the const/volatile qualifiers.
  ///
  /// Keys in the map never have const/volatile qualifiers.
  using TypeIdxMap = llvm::DenseMap<QualType, serialization::TypeIdx,
                                    serialization::UnsafeQualTypeDenseMapInfo>;

  /// The bitstream writer used to emit this precompiled header.
  llvm::BitstreamWriter &Stream;

  /// The buffer associated with the bitstream.
  const SmallVectorImpl<char> &Buffer;

  /// The PCM manager which manages memory buffers for pcm files.
  MemoryBufferCache &PCMCache;

  /// The ASTContext we're writing.
  ASTContext *Context = nullptr;

  /// The preprocessor we're writing.
  Preprocessor *PP = nullptr;

  /// The reader of existing AST files, if we're chaining.
  ASTReader *Chain = nullptr;

  /// The module we're currently writing, if any.
  Module *WritingModule = nullptr;

  /// The base directory for any relative paths we emit.
  std::string BaseDirectory;

  /// Indicates whether timestamps should be written to the produced
  /// module file. This is the case for files implicitly written to the
  /// module cache, where we need the timestamps to determine if the module
  /// file is up to date, but not otherwise.
  bool IncludeTimestamps;

  /// Indicates when the AST writing is actively performing
  /// serialization, rather than just queueing updates.
  bool WritingAST = false;

  /// Indicates that we are done serializing the collection of decls
  /// and types to emit.
  bool DoneWritingDeclsAndTypes = false;

  /// Indicates that the AST contained compiler errors.
  bool ASTHasCompilerErrors = false;

  /// Mapping from input file entries to the index into the
  /// offset table where information about that input file is stored.
  llvm::DenseMap<const FileEntry *, uint32_t> InputFileIDs;

  /// Stores a declaration or a type to be written to the AST file.
  class DeclOrType {
  public:
    DeclOrType(Decl *D) : Stored(D), IsType(false) {}
    DeclOrType(QualType T) : Stored(T.getAsOpaquePtr()), IsType(true) {}

    bool isType() const { return IsType; }
    bool isDecl() const { return !IsType; }

    QualType getType() const {
      assert(isType() && "Not a type!");
      return QualType::getFromOpaquePtr(Stored);
    }

    Decl *getDecl() const {
      assert(isDecl() && "Not a decl!");
      return static_cast<Decl *>(Stored);
    }

  private:
    void *Stored;
    bool IsType;
  };

  /// The declarations and types to emit.
  std::queue<DeclOrType> DeclTypesToEmit;

  /// The first ID number we can use for our own declarations.
  serialization::DeclID FirstDeclID = serialization::NUM_PREDEF_DECL_IDS;

  /// The decl ID that will be assigned to the next new decl.
  serialization::DeclID NextDeclID = FirstDeclID;

  /// Map that provides the ID numbers of each declaration within
  /// the output stream, as well as those deserialized from a chained PCH.
  ///
  /// The ID numbers of declarations are consecutive (in order of
  /// discovery) and start at 2. 1 is reserved for the translation
  /// unit, while 0 is reserved for NULL.
  llvm::DenseMap<const Decl *, serialization::DeclID> DeclIDs;

  /// Offset of each declaration in the bitstream, indexed by
  /// the declaration's ID.
  std::vector<serialization::DeclOffset> DeclOffsets;

  /// Sorted (by file offset) vector of pairs of file offset/DeclID.
  using LocDeclIDsTy =
      SmallVector<std::pair<unsigned, serialization::DeclID>, 64>;
  struct DeclIDInFileInfo {
    LocDeclIDsTy DeclIDs;

    /// Set when the DeclIDs vectors from all files are joined, this
    /// indicates the index that this particular vector has in the global one.
    unsigned FirstDeclIndex;
  };
  using FileDeclIDsTy = llvm::DenseMap<FileID, DeclIDInFileInfo *>;

  /// Map from file SLocEntries to info about the file-level declarations
  /// that it contains.
  FileDeclIDsTy FileDeclIDs;

  void associateDeclWithFile(const Decl *D, serialization::DeclID);

  /// The first ID number we can use for our own types.
  serialization::TypeID FirstTypeID = serialization::NUM_PREDEF_TYPE_IDS;

  /// The type ID that will be assigned to the next new type.
  serialization::TypeID NextTypeID = FirstTypeID;

  /// Map that provides the ID numbers of each type within the
  /// output stream, plus those deserialized from a chained PCH.
  ///
  /// The ID numbers of types are consecutive (in order of discovery)
  /// and start at 1. 0 is reserved for NULL. When types are actually
  /// stored in the stream, the ID number is shifted by 2 bits to
  /// allow for the const/volatile qualifiers.
  ///
  /// Keys in the map never have const/volatile qualifiers.
  TypeIdxMap TypeIdxs;

  /// Offset of each type in the bitstream, indexed by
  /// the type's ID.
  std::vector<uint32_t> TypeOffsets;

  /// The first ID number we can use for our own identifiers.
  serialization::IdentID FirstIdentID = serialization::NUM_PREDEF_IDENT_IDS;

  /// The identifier ID that will be assigned to the next new identifier.
  serialization::IdentID NextIdentID = FirstIdentID;

  /// Map that provides the ID numbers of each identifier in
  /// the output stream.
  ///
  /// The ID numbers for identifiers are consecutive (in order of
  /// discovery), starting at 1. An ID of zero refers to a NULL
  /// IdentifierInfo.
  llvm::MapVector<const IdentifierInfo *, serialization::IdentID> IdentifierIDs;

  /// The first ID number we can use for our own macros.
  serialization::MacroID FirstMacroID = serialization::NUM_PREDEF_MACRO_IDS;

  /// The identifier ID that will be assigned to the next new identifier.
  serialization::MacroID NextMacroID = FirstMacroID;

  /// Map that provides the ID numbers of each macro.
  llvm::DenseMap<MacroInfo *, serialization::MacroID> MacroIDs;

  struct MacroInfoToEmitData {
    const IdentifierInfo *Name;
    MacroInfo *MI;
    serialization::MacroID ID;
  };

  /// The macro infos to emit.
  std::vector<MacroInfoToEmitData> MacroInfosToEmit;

  llvm::DenseMap<const IdentifierInfo *, uint64_t> IdentMacroDirectivesOffsetMap;

  /// @name FlushStmt Caches
  /// @{

  /// Set of parent Stmts for the currently serializing sub-stmt.
  llvm::DenseSet<Stmt *> ParentStmts;

  /// Offsets of sub-stmts already serialized. The offset points
  /// just after the stmt record.
  llvm::DenseMap<Stmt *, uint64_t> SubStmtEntries;

  /// @}

  /// Offsets of each of the identifier IDs into the identifier
  /// table.
  std::vector<uint32_t> IdentifierOffsets;

  /// The first ID number we can use for our own submodules.
  serialization::SubmoduleID FirstSubmoduleID =
      serialization::NUM_PREDEF_SUBMODULE_IDS;

  /// The submodule ID that will be assigned to the next new submodule.
  serialization::SubmoduleID NextSubmoduleID = FirstSubmoduleID;

  /// The first ID number we can use for our own selectors.
  serialization::SelectorID FirstSelectorID =
      serialization::NUM_PREDEF_SELECTOR_IDS;

  /// The selector ID that will be assigned to the next new selector.
  serialization::SelectorID NextSelectorID = FirstSelectorID;

  /// Map that provides the ID numbers of each Selector.
  llvm::MapVector<Selector, serialization::SelectorID> SelectorIDs;

  /// Offset of each selector within the method pool/selector
  /// table, indexed by the Selector ID (-1).
  std::vector<uint32_t> SelectorOffsets;

  /// Mapping from macro definitions (as they occur in the preprocessing
  /// record) to the macro IDs.
  llvm::DenseMap<const MacroDefinitionRecord *,
                 serialization::PreprocessedEntityID> MacroDefinitions;

  /// Cache of indices of anonymous declarations within their lexical
  /// contexts.
  llvm::DenseMap<const Decl *, unsigned> AnonymousDeclarationNumbers;

  /// An update to a Decl.
  class DeclUpdate {
    /// A DeclUpdateKind.
    unsigned Kind;
    union {
      const Decl *Dcl;
      void *Type;
      unsigned Loc;
      unsigned Val;
      Module *Mod;
      const Attr *Attribute;
    };

  public:
    DeclUpdate(unsigned Kind) : Kind(Kind), Dcl(nullptr) {}
    DeclUpdate(unsigned Kind, const Decl *Dcl) : Kind(Kind), Dcl(Dcl) {}
    DeclUpdate(unsigned Kind, QualType Type)
        : Kind(Kind), Type(Type.getAsOpaquePtr()) {}
    DeclUpdate(unsigned Kind, SourceLocation Loc)
        : Kind(Kind), Loc(Loc.getRawEncoding()) {}
    DeclUpdate(unsigned Kind, unsigned Val) : Kind(Kind), Val(Val) {}
    DeclUpdate(unsigned Kind, Module *M) : Kind(Kind), Mod(M) {}
    DeclUpdate(unsigned Kind, const Attr *Attribute)
          : Kind(Kind), Attribute(Attribute) {}

    unsigned getKind() const { return Kind; }
    const Decl *getDecl() const { return Dcl; }
    QualType getType() const { return QualType::getFromOpaquePtr(Type); }

    SourceLocation getLoc() const {
      return SourceLocation::getFromRawEncoding(Loc);
    }

    unsigned getNumber() const { return Val; }
    Module *getModule() const { return Mod; }
    const Attr *getAttr() const { return Attribute; }
  };

  using UpdateRecord = SmallVector<DeclUpdate, 1>;
  using DeclUpdateMap = llvm::MapVector<const Decl *, UpdateRecord>;

  /// Mapping from declarations that came from a chained PCH to the
  /// record containing modifications to them.
  DeclUpdateMap DeclUpdates;

  using FirstLatestDeclMap = llvm::DenseMap<Decl *, Decl *>;

  /// Map of first declarations from a chained PCH that point to the
  /// most recent declarations in another PCH.
  FirstLatestDeclMap FirstLatestDecls;

  /// Declarations encountered that might be external
  /// definitions.
  ///
  /// We keep track of external definitions and other 'interesting' declarations
  /// as we are emitting declarations to the AST file. The AST file contains a
  /// separate record for these declarations, which are provided to the AST
  /// consumer by the AST reader. This is behavior is required to properly cope with,
  /// e.g., tentative variable definitions that occur within
  /// headers. The declarations themselves are stored as declaration
  /// IDs, since they will be written out to an EAGERLY_DESERIALIZED_DECLS
  /// record.
  SmallVector<uint64_t, 16> EagerlyDeserializedDecls;
  SmallVector<uint64_t, 16> ModularCodegenDecls;

  /// DeclContexts that have received extensions since their serialized
  /// form.
  ///
  /// For namespaces, when we're chaining and encountering a namespace, we check
  /// if its primary namespace comes from the chain. If it does, we add the
  /// primary to this set, so that we can write out lexical content updates for
  /// it.
  llvm::SmallSetVector<const DeclContext *, 16> UpdatedDeclContexts;

  /// Keeps track of declarations that we must emit, even though we're
  /// not guaranteed to be able to find them by walking the AST starting at the
  /// translation unit.
  SmallVector<const Decl *, 16> DeclsToEmitEvenIfUnreferenced;

  /// The set of Objective-C class that have categories we
  /// should serialize.
  llvm::SetVector<ObjCInterfaceDecl *> ObjCClassesWithCategories;

  /// The set of declarations that may have redeclaration chains that
  /// need to be serialized.
  llvm::SmallVector<const Decl *, 16> Redeclarations;

  /// A cache of the first local declaration for "interesting"
  /// redeclaration chains.
  llvm::DenseMap<const Decl *, const Decl *> FirstLocalDeclCache;

  /// Mapping from SwitchCase statements to IDs.
  llvm::DenseMap<SwitchCase *, unsigned> SwitchCaseIDs;

  /// The number of statements written to the AST file.
  unsigned NumStatements = 0;

  /// The number of macros written to the AST file.
  unsigned NumMacros = 0;

  /// The number of lexical declcontexts written to the AST
  /// file.
  unsigned NumLexicalDeclContexts = 0;

  /// The number of visible declcontexts written to the AST
  /// file.
  unsigned NumVisibleDeclContexts = 0;

  /// A mapping from each known submodule to its ID number, which will
  /// be a positive integer.
  llvm::DenseMap<Module *, unsigned> SubmoduleIDs;

  /// A list of the module file extension writers.
  std::vector<std::unique_ptr<ModuleFileExtensionWriter>>
    ModuleFileExtensionWriters;

  /// Retrieve or create a submodule ID for this module.
  unsigned getSubmoduleID(Module *Mod);

  /// Write the given subexpression to the bitstream.
  void WriteSubStmt(Stmt *S);

  void WriteBlockInfoBlock();
  void WriteControlBlock(Preprocessor &PP, ASTContext &Context,
                         StringRef isysroot, const std::string &OutputFile);

  /// Write out the signature and diagnostic options, and return the signature.
  ASTFileSignature writeUnhashedControlBlock(Preprocessor &PP,
                                             ASTContext &Context);

  /// Calculate hash of the pcm content.
  static ASTFileSignature createSignature(StringRef Bytes);

  void WriteInputFiles(SourceManager &SourceMgr, HeaderSearchOptions &HSOpts,
                       bool Modules);
  void WriteSourceManagerBlock(SourceManager &SourceMgr,
                               const Preprocessor &PP);
  void WritePreprocessor(const Preprocessor &PP, bool IsModule);
  void WriteHeaderSearch(const HeaderSearch &HS);
  void WritePreprocessorDetail(PreprocessingRecord &PPRec);
  void WriteSubmodules(Module *WritingModule);

  void WritePragmaDiagnosticMappings(const DiagnosticsEngine &Diag,
                                     bool isModule);

  unsigned TypeExtQualAbbrev = 0;
  unsigned TypeFunctionProtoAbbrev = 0;
  void WriteTypeAbbrevs();
  void WriteType(QualType T);

  bool isLookupResultExternal(StoredDeclsList &Result, DeclContext *DC);
  bool isLookupResultEntirelyExternal(StoredDeclsList &Result, DeclContext *DC);

  void GenerateNameLookupTable(const DeclContext *DC,
                               llvm::SmallVectorImpl<char> &LookupTable);
  uint64_t WriteDeclContextLexicalBlock(ASTContext &Context, DeclContext *DC);
  uint64_t WriteDeclContextVisibleBlock(ASTContext &Context, DeclContext *DC);
  void WriteTypeDeclOffsets();
  void WriteFileDeclIDsMap();
  void WriteComments();
  void WriteSelectors(Sema &SemaRef);
  void WriteReferencedSelectorsPool(Sema &SemaRef);
  void WriteIdentifierTable(Preprocessor &PP, IdentifierResolver &IdResolver,
                            bool IsModule);
  void WriteDeclUpdatesBlocks(RecordDataImpl &OffsetsRecord);
  void WriteDeclContextVisibleUpdate(const DeclContext *DC);
  void WriteFPPragmaOptions(const FPOptions &Opts);
  void WriteOpenCLExtensions(Sema &SemaRef);
  void WriteOpenCLExtensionTypes(Sema &SemaRef);
  void WriteOpenCLExtensionDecls(Sema &SemaRef);
  void WriteCUDAPragmas(Sema &SemaRef);
  void WriteObjCCategories();
  void WriteLateParsedTemplates(Sema &SemaRef);
  void WriteOptimizePragmaOptions(Sema &SemaRef);
  void WriteMSStructPragmaOptions(Sema &SemaRef);
  void WriteMSPointersToMembersPragmaOptions(Sema &SemaRef);
  void WritePackPragmaOptions(Sema &SemaRef);
  void WriteModuleFileExtension(Sema &SemaRef,
                                ModuleFileExtensionWriter &Writer);

  unsigned DeclParmVarAbbrev = 0;
  unsigned DeclContextLexicalAbbrev = 0;
  unsigned DeclContextVisibleLookupAbbrev = 0;
  unsigned UpdateVisibleAbbrev = 0;
  unsigned DeclRecordAbbrev = 0;
  unsigned DeclTypedefAbbrev = 0;
  unsigned DeclVarAbbrev = 0;
  unsigned DeclFieldAbbrev = 0;
  unsigned DeclEnumAbbrev = 0;
  unsigned DeclObjCIvarAbbrev = 0;
  unsigned DeclCXXMethodAbbrev = 0;

  unsigned DeclRefExprAbbrev = 0;
  unsigned CharacterLiteralAbbrev = 0;
  unsigned IntegerLiteralAbbrev = 0;
  unsigned ExprImplicitCastAbbrev = 0;

  void WriteDeclAbbrevs();
  void WriteDecl(ASTContext &Context, Decl *D);

  ASTFileSignature WriteASTCore(Sema &SemaRef, StringRef isysroot,
                                const std::string &OutputFile,
                                Module *WritingModule);

public:
  /// Create a new precompiled header writer that outputs to
  /// the given bitstream.
  ASTWriter(llvm::BitstreamWriter &Stream, SmallVectorImpl<char> &Buffer,
            MemoryBufferCache &PCMCache,
            ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
            bool IncludeTimestamps = true);
  ~ASTWriter() override;

  const LangOptions &getLangOpts() const;

  /// Get a timestamp for output into the AST file. The actual timestamp
  /// of the specified file may be ignored if we have been instructed to not
  /// include timestamps in the output file.
  time_t getTimestampForOutput(const FileEntry *E) const;

  /// Write a precompiled header for the given semantic analysis.
  ///
  /// \param SemaRef a reference to the semantic analysis object that processed
  /// the AST to be written into the precompiled header.
  ///
  /// \param WritingModule The module that we are writing. If null, we are
  /// writing a precompiled header.
  ///
  /// \param isysroot if non-empty, write a relocatable file whose headers
  /// are relative to the given system root. If we're writing a module, its
  /// build directory will be used in preference to this if both are available.
  ///
  /// \return the module signature, which eventually will be a hash of
  /// the module but currently is merely a random 32-bit number.
  ASTFileSignature WriteAST(Sema &SemaRef, const std::string &OutputFile,
                            Module *WritingModule, StringRef isysroot,
                            bool hasErrors = false);

  /// Emit a token.
  void AddToken(const Token &Tok, RecordDataImpl &Record);

  /// Emit a source location.
  void AddSourceLocation(SourceLocation Loc, RecordDataImpl &Record);

  /// Emit a source range.
  void AddSourceRange(SourceRange Range, RecordDataImpl &Record);

  /// Emit a reference to an identifier.
  void AddIdentifierRef(const IdentifierInfo *II, RecordDataImpl &Record);

  /// Get the unique number used to refer to the given selector.
  serialization::SelectorID getSelectorRef(Selector Sel);

  /// Get the unique number used to refer to the given identifier.
  serialization::IdentID getIdentifierRef(const IdentifierInfo *II);

  /// Get the unique number used to refer to the given macro.
  serialization::MacroID getMacroRef(MacroInfo *MI, const IdentifierInfo *Name);

  /// Determine the ID of an already-emitted macro.
  serialization::MacroID getMacroID(MacroInfo *MI);

  uint64_t getMacroDirectivesOffset(const IdentifierInfo *Name);

  /// Emit a reference to a type.
  void AddTypeRef(QualType T, RecordDataImpl &Record);

  /// Force a type to be emitted and get its ID.
  serialization::TypeID GetOrCreateTypeID(QualType T);

  /// Determine the type ID of an already-emitted type.
  serialization::TypeID getTypeID(QualType T) const;

  /// Find the first local declaration of a given local redeclarable
  /// decl.
  const Decl *getFirstLocalDecl(const Decl *D);

  /// Is this a local declaration (that is, one that will be written to
  /// our AST file)? This is the case for declarations that are neither imported
  /// from another AST file nor predefined.
  bool IsLocalDecl(const Decl *D) {
    if (D->isFromASTFile())
      return false;
    auto I = DeclIDs.find(D);
    return (I == DeclIDs.end() ||
            I->second >= serialization::NUM_PREDEF_DECL_IDS);
  };

  /// Emit a reference to a declaration.
  void AddDeclRef(const Decl *D, RecordDataImpl &Record);

  /// Force a declaration to be emitted and get its ID.
  serialization::DeclID GetDeclRef(const Decl *D);

  /// Determine the declaration ID of an already-emitted
  /// declaration.
  serialization::DeclID getDeclID(const Decl *D);

  unsigned getAnonymousDeclarationNumber(const NamedDecl *D);

  /// Add a string to the given record.
  void AddString(StringRef Str, RecordDataImpl &Record);

  /// Convert a path from this build process into one that is appropriate
  /// for emission in the module file.
  bool PreparePathForOutput(SmallVectorImpl<char> &Path);

  /// Add a path to the given record.
  void AddPath(StringRef Path, RecordDataImpl &Record);

  /// Emit the current record with the given path as a blob.
  void EmitRecordWithPath(unsigned Abbrev, RecordDataRef Record,
                          StringRef Path);

  /// Add a version tuple to the given record
  void AddVersionTuple(const VersionTuple &Version, RecordDataImpl &Record);

  /// Retrieve or create a submodule ID for this module, or return 0 if
  /// the submodule is neither local (a submodle of the currently-written module)
  /// nor from an imported module.
  unsigned getLocalOrImportedSubmoduleID(Module *Mod);

  /// Note that the identifier II occurs at the given offset
  /// within the identifier table.
  void SetIdentifierOffset(const IdentifierInfo *II, uint32_t Offset);

  /// Note that the selector Sel occurs at the given offset
  /// within the method pool/selector table.
  void SetSelectorOffset(Selector Sel, uint32_t Offset);

  /// Record an ID for the given switch-case statement.
  unsigned RecordSwitchCaseID(SwitchCase *S);

  /// Retrieve the ID for the given switch-case statement.
  unsigned getSwitchCaseID(SwitchCase *S);

  void ClearSwitchCaseIDs();

  unsigned getTypeExtQualAbbrev() const {
    return TypeExtQualAbbrev;
  }

  unsigned getTypeFunctionProtoAbbrev() const {
    return TypeFunctionProtoAbbrev;
  }

  unsigned getDeclParmVarAbbrev() const { return DeclParmVarAbbrev; }
  unsigned getDeclRecordAbbrev() const { return DeclRecordAbbrev; }
  unsigned getDeclTypedefAbbrev() const { return DeclTypedefAbbrev; }
  unsigned getDeclVarAbbrev() const { return DeclVarAbbrev; }
  unsigned getDeclFieldAbbrev() const { return DeclFieldAbbrev; }
  unsigned getDeclEnumAbbrev() const { return DeclEnumAbbrev; }
  unsigned getDeclObjCIvarAbbrev() const { return DeclObjCIvarAbbrev; }
  unsigned getDeclCXXMethodAbbrev() const { return DeclCXXMethodAbbrev; }

  unsigned getDeclRefExprAbbrev() const { return DeclRefExprAbbrev; }
  unsigned getCharacterLiteralAbbrev() const { return CharacterLiteralAbbrev; }
  unsigned getIntegerLiteralAbbrev() const { return IntegerLiteralAbbrev; }
  unsigned getExprImplicitCastAbbrev() const { return ExprImplicitCastAbbrev; }

  bool hasChain() const { return Chain; }
  ASTReader *getChain() const { return Chain; }

private:
  // ASTDeserializationListener implementation
  void ReaderInitialized(ASTReader *Reader) override;
  void IdentifierRead(serialization::IdentID ID, IdentifierInfo *II) override;
  void MacroRead(serialization::MacroID ID, MacroInfo *MI) override;
  void TypeRead(serialization::TypeIdx Idx, QualType T) override;
  void SelectorRead(serialization::SelectorID ID, Selector Sel) override;
  void MacroDefinitionRead(serialization::PreprocessedEntityID ID,
                           MacroDefinitionRecord *MD) override;
  void ModuleRead(serialization::SubmoduleID ID, Module *Mod) override;

  // ASTMutationListener implementation.
  void CompletedTagDefinition(const TagDecl *D) override;
  void AddedVisibleDecl(const DeclContext *DC, const Decl *D) override;
  void AddedCXXImplicitMember(const CXXRecordDecl *RD, const Decl *D) override;
  void AddedCXXTemplateSpecialization(
      const ClassTemplateDecl *TD,
      const ClassTemplateSpecializationDecl *D) override;
  void AddedCXXTemplateSpecialization(
      const VarTemplateDecl *TD,
      const VarTemplateSpecializationDecl *D) override;
  void AddedCXXTemplateSpecialization(const FunctionTemplateDecl *TD,
                                      const FunctionDecl *D) override;
  void ResolvedExceptionSpec(const FunctionDecl *FD) override;
  void DeducedReturnType(const FunctionDecl *FD, QualType ReturnType) override;
  void ResolvedOperatorDelete(const CXXDestructorDecl *DD,
                              const FunctionDecl *Delete,
                              Expr *ThisArg) override;
  void CompletedImplicitDefinition(const FunctionDecl *D) override;
  void InstantiationRequested(const ValueDecl *D) override;
  void VariableDefinitionInstantiated(const VarDecl *D) override;
  void FunctionDefinitionInstantiated(const FunctionDecl *D) override;
  void DefaultArgumentInstantiated(const ParmVarDecl *D) override;
  void DefaultMemberInitializerInstantiated(const FieldDecl *D) override;
  void AddedObjCCategoryToInterface(const ObjCCategoryDecl *CatD,
                                    const ObjCInterfaceDecl *IFD) override;
  void DeclarationMarkedUsed(const Decl *D) override;
  void DeclarationMarkedOpenMPThreadPrivate(const Decl *D) override;
  void DeclarationMarkedOpenMPDeclareTarget(const Decl *D,
                                            const Attr *Attr) override;
  void RedefinedHiddenDefinition(const NamedDecl *D, Module *M) override;
  void AddedAttributeToRecord(const Attr *Attr,
                              const RecordDecl *Record) override;
};

/// An object for streaming information to a record.
class ASTRecordWriter {
  ASTWriter *Writer;
  ASTWriter::RecordDataImpl *Record;

  /// Statements that we've encountered while serializing a
  /// declaration or type.
  SmallVector<Stmt *, 16> StmtsToEmit;

  /// Indices of record elements that describe offsets within the
  /// bitcode. These will be converted to offsets relative to the current
  /// record when emitted.
  SmallVector<unsigned, 8> OffsetIndices;

  /// Flush all of the statements and expressions that have
  /// been added to the queue via AddStmt().
  void FlushStmts();
  void FlushSubStmts();

  void PrepareToEmit(uint64_t MyOffset) {
    // Convert offsets into relative form.
    for (unsigned I : OffsetIndices) {
      auto &StoredOffset = (*Record)[I];
      assert(StoredOffset < MyOffset && "invalid offset");
      if (StoredOffset)
        StoredOffset = MyOffset - StoredOffset;
    }
    OffsetIndices.clear();
  }

public:
  /// Construct a ASTRecordWriter that uses the default encoding scheme.
  ASTRecordWriter(ASTWriter &Writer, ASTWriter::RecordDataImpl &Record)
      : Writer(&Writer), Record(&Record) {}

  /// Construct a ASTRecordWriter that uses the same encoding scheme as another
  /// ASTRecordWriter.
  ASTRecordWriter(ASTRecordWriter &Parent, ASTWriter::RecordDataImpl &Record)
      : Writer(Parent.Writer), Record(&Record) {}

  /// Copying an ASTRecordWriter is almost certainly a bug.
  ASTRecordWriter(const ASTRecordWriter &) = delete;
  ASTRecordWriter &operator=(const ASTRecordWriter &) = delete;

  /// Extract the underlying record storage.
  ASTWriter::RecordDataImpl &getRecordData() const { return *Record; }

  /// Minimal vector-like interface.
  /// @{
  void push_back(uint64_t N) { Record->push_back(N); }
  template<typename InputIterator>
  void append(InputIterator begin, InputIterator end) {
    Record->append(begin, end);
  }
  bool empty() const { return Record->empty(); }
  size_t size() const { return Record->size(); }
  uint64_t &operator[](size_t N) { return (*Record)[N]; }
  /// @}

  /// Emit the record to the stream, followed by its substatements, and
  /// return its offset.
  // FIXME: Allow record producers to suggest Abbrevs.
  uint64_t Emit(unsigned Code, unsigned Abbrev = 0) {
    uint64_t Offset = Writer->Stream.GetCurrentBitNo();
    PrepareToEmit(Offset);
    Writer->Stream.EmitRecord(Code, *Record, Abbrev);
    FlushStmts();
    return Offset;
  }

  /// Emit the record to the stream, preceded by its substatements.
  uint64_t EmitStmt(unsigned Code, unsigned Abbrev = 0) {
    FlushSubStmts();
    PrepareToEmit(Writer->Stream.GetCurrentBitNo());
    Writer->Stream.EmitRecord(Code, *Record, Abbrev);
    return Writer->Stream.GetCurrentBitNo();
  }

  /// Add a bit offset into the record. This will be converted into an
  /// offset relative to the current record when emitted.
  void AddOffset(uint64_t BitOffset) {
    OffsetIndices.push_back(Record->size());
    Record->push_back(BitOffset);
  }

  /// Add the given statement or expression to the queue of
  /// statements to emit.
  ///
  /// This routine should be used when emitting types and declarations
  /// that have expressions as part of their formulation. Once the
  /// type or declaration has been written, Emit() will write
  /// the corresponding statements just after the record.
  void AddStmt(Stmt *S) {
    StmtsToEmit.push_back(S);
  }

  /// Add a definition for the given function to the queue of statements
  /// to emit.
  void AddFunctionDefinition(const FunctionDecl *FD);

  /// Emit a source location.
  void AddSourceLocation(SourceLocation Loc) {
    return Writer->AddSourceLocation(Loc, *Record);
  }

  /// Emit a source range.
  void AddSourceRange(SourceRange Range) {
    return Writer->AddSourceRange(Range, *Record);
  }

  /// Emit an integral value.
  void AddAPInt(const llvm::APInt &Value);

  /// Emit a signed integral value.
  void AddAPSInt(const llvm::APSInt &Value);

  /// Emit a floating-point value.
  void AddAPFloat(const llvm::APFloat &Value);

  /// Emit a reference to an identifier.
  void AddIdentifierRef(const IdentifierInfo *II) {
    return Writer->AddIdentifierRef(II, *Record);
  }

  /// Emit a Selector (which is a smart pointer reference).
  void AddSelectorRef(Selector S);

  /// Emit a CXXTemporary.
  void AddCXXTemporary(const CXXTemporary *Temp);

  /// Emit a C++ base specifier.
  void AddCXXBaseSpecifier(const CXXBaseSpecifier &Base);

  /// Emit a set of C++ base specifiers.
  void AddCXXBaseSpecifiers(ArrayRef<CXXBaseSpecifier> Bases);

  /// Emit a reference to a type.
  void AddTypeRef(QualType T) {
    return Writer->AddTypeRef(T, *Record);
  }

  /// Emits a reference to a declarator info.
  void AddTypeSourceInfo(TypeSourceInfo *TInfo);

  /// Emits source location information for a type. Does not emit the type.
  void AddTypeLoc(TypeLoc TL);

  /// Emits a template argument location info.
  void AddTemplateArgumentLocInfo(TemplateArgument::ArgKind Kind,
                                  const TemplateArgumentLocInfo &Arg);

  /// Emits a template argument location.
  void AddTemplateArgumentLoc(const TemplateArgumentLoc &Arg);

  /// Emits an AST template argument list info.
  void AddASTTemplateArgumentListInfo(
      const ASTTemplateArgumentListInfo *ASTTemplArgList);

  /// Emit a reference to a declaration.
  void AddDeclRef(const Decl *D) {
    return Writer->AddDeclRef(D, *Record);
  }

  /// Emit a declaration name.
  void AddDeclarationName(DeclarationName Name);

  void AddDeclarationNameLoc(const DeclarationNameLoc &DNLoc,
                             DeclarationName Name);
  void AddDeclarationNameInfo(const DeclarationNameInfo &NameInfo);

  void AddQualifierInfo(const QualifierInfo &Info);

  /// Emit a nested name specifier.
  void AddNestedNameSpecifier(NestedNameSpecifier *NNS);

  /// Emit a nested name specifier with source-location information.
  void AddNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS);

  /// Emit a template name.
  void AddTemplateName(TemplateName Name);

  /// Emit a template argument.
  void AddTemplateArgument(const TemplateArgument &Arg);

  /// Emit a template parameter list.
  void AddTemplateParameterList(const TemplateParameterList *TemplateParams);

  /// Emit a template argument list.
  void AddTemplateArgumentList(const TemplateArgumentList *TemplateArgs);

  /// Emit a UnresolvedSet structure.
  void AddUnresolvedSet(const ASTUnresolvedSet &Set);

  /// Emit a CXXCtorInitializer array.
  void AddCXXCtorInitializers(ArrayRef<CXXCtorInitializer *> CtorInits);

  void AddCXXDefinitionData(const CXXRecordDecl *D);

  /// Emit a string.
  void AddString(StringRef Str) {
    return Writer->AddString(Str, *Record);
  }

  /// Emit a path.
  void AddPath(StringRef Path) {
    return Writer->AddPath(Path, *Record);
  }

  /// Emit a version tuple.
  void AddVersionTuple(const VersionTuple &Version) {
    return Writer->AddVersionTuple(Version, *Record);
  }

  // Emit an attribute.
  void AddAttr(const Attr *A);

  /// Emit a list of attributes.
  void AddAttributes(ArrayRef<const Attr*> Attrs);
};

/// AST and semantic-analysis consumer that generates a
/// precompiled header from the parsed source code.
class PCHGenerator : public SemaConsumer {
  const Preprocessor &PP;
  std::string OutputFile;
  std::string isysroot;
  Sema *SemaPtr;
  std::shared_ptr<PCHBuffer> Buffer;
  llvm::BitstreamWriter Stream;
  ASTWriter Writer;
  bool AllowASTWithErrors;

protected:
  ASTWriter &getWriter() { return Writer; }
  const ASTWriter &getWriter() const { return Writer; }
  SmallVectorImpl<char> &getPCH() const { return Buffer->Data; }

public:
  PCHGenerator(const Preprocessor &PP, StringRef OutputFile, StringRef isysroot,
               std::shared_ptr<PCHBuffer> Buffer,
               ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
               bool AllowASTWithErrors = false, bool IncludeTimestamps = true);
  ~PCHGenerator() override;

  void InitializeSema(Sema &S) override { SemaPtr = &S; }
  void HandleTranslationUnit(ASTContext &Ctx) override;
  ASTMutationListener *GetASTMutationListener() override;
  ASTDeserializationListener *GetASTDeserializationListener() override;
  bool hasEmittedPCH() const { return Buffer->IsComplete; }
};

class OMPClauseWriter : public OMPClauseVisitor<OMPClauseWriter> {
  ASTRecordWriter &Record;

public:
  OMPClauseWriter(ASTRecordWriter &Record) : Record(Record) {}
#define OPENMP_CLAUSE(Name, Class) void Visit##Class(Class *S);
#include "clang/Basic/OpenMPKinds.def"
  void writeClause(OMPClause *C);
  void VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C);
  void VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C);
};

} // namespace clang

#endif // LLVM_CLANG_SERIALIZATION_ASTWRITER_H
