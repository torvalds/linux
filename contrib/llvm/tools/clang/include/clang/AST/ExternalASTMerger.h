//===--- ExternalASTMerger.h - Merging External AST Interface ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares the ExternalASTMerger, which vends a combination of ASTs
//  from several different ASTContext/FileManager pairs
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_EXTERNALASTMERGER_H
#define LLVM_CLANG_AST_EXTERNALASTMERGER_H

#include "clang/AST/ASTImporter.h"
#include "clang/AST/ExternalASTSource.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {

/// ExternalASTSource implementation that merges information from several
/// ASTContexts.
///
/// ExtermalASTMerger maintains a vector of ASTImporters that it uses to import
/// (potentially incomplete) Decls and DeclContexts from the source ASTContexts
/// in response to ExternalASTSource API calls.
///
/// When lookup occurs in the resulting imported DeclContexts, the original
/// DeclContexts need to be queried.  Roughly, there are three cases here:
///
/// - The DeclContext of origin can be found by simple name lookup.  In this
///   case, no additional state is required.
///
/// - The DeclContext of origin is different from what would be found by name
///   lookup.  In this case, Origins contains an entry overriding lookup and
///   specifying the correct pair of DeclContext/ASTContext.
///
/// - The DeclContext of origin was determined by another ExterenalASTMerger.
///   (This is possible when the source ASTContext for one of the Importers has
///   its own ExternalASTMerger).  The origin must be properly forwarded in this
///   case.
///
/// ExternalASTMerger's job is to maintain the data structures necessary to
/// allow this.  The data structures themselves can be extracted (read-only) and
/// copied for re-use.
class ExternalASTMerger : public ExternalASTSource {
public:
  /// A single origin for a DeclContext.  Unlike Decls, DeclContexts do
  /// not allow their containing ASTContext to be determined in all cases.
  struct DCOrigin {
    DeclContext *DC;
    ASTContext *AST;
  };

  typedef std::map<const DeclContext *, DCOrigin> OriginMap;
  typedef std::vector<std::unique_ptr<ASTImporter>> ImporterVector;
private:
  /// One importer exists for each source.
  ImporterVector Importers;
  /// Overrides in case name lookup would return nothing or would return
  /// the wrong thing.
  OriginMap Origins;
  /// The installed log stream.
  llvm::raw_ostream *LogStream;

public:
  /// The target for an ExternalASTMerger.
  ///
  /// ASTImporters require both ASTContext and FileManager to be able to
  /// import SourceLocations properly.
  struct ImporterTarget {
    ASTContext &AST;
    FileManager &FM;
  };
  /// A source for an ExternalASTMerger.
  ///
  /// ASTImporters require both ASTContext and FileManager to be able to
  /// import SourceLocations properly.  Additionally, when import occurs for
  /// a DeclContext whose origin has been overridden, then this
  /// ExternalASTMerger must be able to determine that.
  struct ImporterSource {
    ASTContext &AST;
    FileManager &FM;
    const OriginMap &OM;
  };

private:
  /// The target for this ExtenralASTMerger.
  ImporterTarget Target;

public:
  ExternalASTMerger(const ImporterTarget &Target,
                    llvm::ArrayRef<ImporterSource> Sources);

  /// Add a set of ASTContexts as possible origins.
  ///
  /// Usually the set will be initialized in the constructor, but long-lived
  /// ExternalASTMergers may need to import from new sources (for example,
  /// newly-parsed source files).
  ///
  /// Ensures that Importers does not gain duplicate entries as a result.
  void AddSources(llvm::ArrayRef<ImporterSource> Sources);

  /// Remove a set of ASTContexts as possible origins.
  ///
  /// Sometimes an origin goes away (for example, if a source file gets
  /// superseded by a newer version).
  ///
  /// The caller is responsible for ensuring that this doesn't leave
  /// DeclContexts that can't be completed.
  void RemoveSources(llvm::ArrayRef<ImporterSource> Sources);

  /// Implementation of the ExternalASTSource API.
  bool FindExternalVisibleDeclsByName(const DeclContext *DC,
                                      DeclarationName Name) override;

  /// Implementation of the ExternalASTSource API.
  void
  FindExternalLexicalDecls(const DeclContext *DC,
                           llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
                           SmallVectorImpl<Decl *> &Result) override;

  /// Implementation of the ExternalASTSource API.
  void CompleteType(TagDecl *Tag) override;

  /// Implementation of the ExternalASTSource API.
  void CompleteType(ObjCInterfaceDecl *Interface) override;

  /// Returns true if DC can be found in any source AST context.
  bool CanComplete(DeclContext *DC);

  /// Records an origin in Origins only if name lookup would find
  /// something different or nothing at all.
  void MaybeRecordOrigin(const DeclContext *ToDC, DCOrigin Origin);

  /// Regardless of any checks, override the Origin for a DeclContext.
  void ForceRecordOrigin(const DeclContext *ToDC, DCOrigin Origin);

  /// Get a read-only view of the Origins map, for use in constructing
  /// an ImporterSource for another ExternalASTMerger.
  const OriginMap &GetOrigins() { return Origins; }

  /// Returns true if Importers contains an ASTImporter whose source is
  /// OriginContext.
  bool HasImporterForOrigin(ASTContext &OriginContext);

  /// Returns a reference to the ASTRImporter from Importers whose origin
  /// is OriginContext.  This allows manual import of ASTs while preserving the
  /// OriginMap correctly.
  ASTImporter &ImporterForOrigin(ASTContext &OriginContext);

  /// Sets the current log stream.
  void SetLogStream(llvm::raw_string_ostream &Stream) { LogStream = &Stream; }
private:
  /// Records and origin in Origins.
  void RecordOriginImpl(const DeclContext *ToDC, DCOrigin Origin,
                                  ASTImporter &importer);

  /// Performs an action for every DeclContext that is identified as
  /// corresponding (either by forced origin or by name lookup) to DC.
  template <typename CallbackType>
  void ForEachMatchingDC(const DeclContext *DC, CallbackType Callback);

public:
  /// Log something if there is a logging callback installed.
  llvm::raw_ostream &logs() { return *LogStream; }

  /// True if the log stream is not llvm::nulls();
  bool LoggingEnabled() { return LogStream != &llvm::nulls(); }
};

} // end namespace clang

#endif
