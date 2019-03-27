//===-- ClangASTSource.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangASTSource_h_
#define liblldb_ClangASTSource_h_

#include <set>

#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Target.h"
#include "clang/AST/ExternalASTMerger.h"
#include "clang/Basic/IdentifierTable.h"

#include "llvm/ADT/SmallSet.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ClangASTSource ClangASTSource.h "lldb/Expression/ClangASTSource.h"
/// Provider for named objects defined in the debug info for Clang
///
/// As Clang parses an expression, it may encounter names that are not defined
/// inside the expression, including variables, functions, and types.  Clang
/// knows the name it is looking for, but nothing else. The ExternalSemaSource
/// class provides Decls (VarDecl, FunDecl, TypeDecl) to Clang for these
/// names, consulting the ClangExpressionDeclMap to do the actual lookups.
//----------------------------------------------------------------------
class ClangASTSource : public ClangExternalASTSourceCommon,
                       public ClangASTImporter::MapCompleter {
public:
  //------------------------------------------------------------------
  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// @param[in] target
  ///     A reference to the target containing debug information to use.
  //------------------------------------------------------------------
  ClangASTSource(const lldb::TargetSP &target);

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~ClangASTSource() override;

  //------------------------------------------------------------------
  /// Interface stubs.
  //------------------------------------------------------------------
  clang::Decl *GetExternalDecl(uint32_t) override { return NULL; }
  clang::Stmt *GetExternalDeclStmt(uint64_t) override { return NULL; }
  clang::Selector GetExternalSelector(uint32_t) override {
    return clang::Selector();
  }
  uint32_t GetNumExternalSelectors() override { return 0; }
  clang::CXXBaseSpecifier *
  GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
    return NULL;
  }
  void MaterializeVisibleDecls(const clang::DeclContext *DC) { return; }

  void InstallASTContext(clang::ASTContext &ast_context,
                         clang::FileManager &file_manager,
                         bool is_shared_context = false);

  //
  // APIs for ExternalASTSource
  //

  //------------------------------------------------------------------
  /// Look up all Decls that match a particular name.  Only handles
  /// Identifiers and DeclContexts that are either NamespaceDecls or
  /// TranslationUnitDecls.  Calls SetExternalVisibleDeclsForName with the
  /// result.
  ///
  /// The work for this function is done by
  /// void FindExternalVisibleDecls (NameSearchContext &);
  ///
  /// @param[in] DC
  ///     The DeclContext to register the found Decls in.
  ///
  /// @param[in] Name
  ///     The name to find entries for.
  ///
  /// @return
  ///     Whatever SetExternalVisibleDeclsForName returns.
  //------------------------------------------------------------------
  bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                      clang::DeclarationName Name) override;

  //------------------------------------------------------------------
  /// Enumerate all Decls in a given lexical context.
  ///
  /// @param[in] DC
  ///     The DeclContext being searched.
  ///
  /// @param[in] isKindWeWant
  ///     A callback function that returns true given the
  ///     DeclKinds of desired Decls, and false otherwise.
  ///
  /// @param[in] Decls
  ///     A vector that is filled in with matching Decls.
  //------------------------------------------------------------------
  void FindExternalLexicalDecls(
      const clang::DeclContext *DC,
      llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
      llvm::SmallVectorImpl<clang::Decl *> &Decls) override;

  //------------------------------------------------------------------
  /// Specify the layout of the contents of a RecordDecl.
  ///
  /// @param[in] Record
  ///     The record (in the parser's AST context) that needs to be
  ///     laid out.
  ///
  /// @param[out] Size
  ///     The total size of the record in bits.
  ///
  /// @param[out] Alignment
  ///     The alignment of the record in bits.
  ///
  /// @param[in] FieldOffsets
  ///     A map that must be populated with pairs of the record's
  ///     fields (in the parser's AST context) and their offsets
  ///     (measured in bits).
  ///
  /// @param[in] BaseOffsets
  ///     A map that must be populated with pairs of the record's
  ///     C++ concrete base classes (in the parser's AST context,
  ///     and only if the record is a CXXRecordDecl and has base
  ///     classes) and their offsets (measured in bytes).
  ///
  /// @param[in] VirtualBaseOffsets
  ///     A map that must be populated with pairs of the record's
  ///     C++ virtual base classes (in the parser's AST context,
  ///     and only if the record is a CXXRecordDecl and has base
  ///     classes) and their offsets (measured in bytes).
  ///
  /// @return
  ///     True <=> the layout is valid.
  //-----------------------------------------------------------------
  bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &VirtualBaseOffsets) override;

  //------------------------------------------------------------------
  /// Complete a TagDecl.
  ///
  /// @param[in] Tag
  ///     The Decl to be completed in place.
  //------------------------------------------------------------------
  void CompleteType(clang::TagDecl *Tag) override;

  //------------------------------------------------------------------
  /// Complete an ObjCInterfaceDecl.
  ///
  /// @param[in] Class
  ///     The Decl to be completed in place.
  //------------------------------------------------------------------
  void CompleteType(clang::ObjCInterfaceDecl *Class) override;

  //------------------------------------------------------------------
  /// Called on entering a translation unit.  Tells Clang by calling
  /// setHasExternalVisibleStorage() and setHasExternalLexicalStorage() that
  /// this object has something to say about undefined names.
  ///
  /// @param[in] ASTConsumer
  ///     Unused.
  //------------------------------------------------------------------
  void StartTranslationUnit(clang::ASTConsumer *Consumer) override;

  //
  // APIs for NamespaceMapCompleter
  //

  //------------------------------------------------------------------
  /// Look up the modules containing a given namespace and put the appropriate
  /// entries in the namespace map.
  ///
  /// @param[in] namespace_map
  ///     The map to be completed.
  ///
  /// @param[in] name
  ///     The name of the namespace to be found.
  ///
  /// @param[in] parent_map
  ///     The map for the namespace's parent namespace, if there is
  ///     one.
  //------------------------------------------------------------------
  void CompleteNamespaceMap(
      ClangASTImporter::NamespaceMapSP &namespace_map, const ConstString &name,
      ClangASTImporter::NamespaceMapSP &parent_map) const override;

  //
  // Helper APIs
  //

  clang::NamespaceDecl *
  AddNamespace(NameSearchContext &context,
               ClangASTImporter::NamespaceMapSP &namespace_decls);

  //------------------------------------------------------------------
  /// The worker function for FindExternalVisibleDeclsByName.
  ///
  /// @param[in] context
  ///     The NameSearchContext to use when filing results.
  //------------------------------------------------------------------
  virtual void FindExternalVisibleDecls(NameSearchContext &context);

  void SetImportInProgress(bool import_in_progress) {
    m_import_in_progress = import_in_progress;
  }
  bool GetImportInProgress() { return m_import_in_progress; }

  void SetLookupsEnabled(bool lookups_enabled) {
    m_lookups_enabled = lookups_enabled;
  }
  bool GetLookupsEnabled() { return m_lookups_enabled; }

  //----------------------------------------------------------------------
  /// @class ClangASTSourceProxy ClangASTSource.h
  /// "lldb/Expression/ClangASTSource.h" Proxy for ClangASTSource
  ///
  /// Clang AST contexts like to own their AST sources, so this is a state-
  /// free proxy object.
  //----------------------------------------------------------------------
  class ClangASTSourceProxy : public ClangExternalASTSourceCommon {
  public:
    ClangASTSourceProxy(ClangASTSource &original) : m_original(original) {}

    bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                        clang::DeclarationName Name) override {
      return m_original.FindExternalVisibleDeclsByName(DC, Name);
    }

    void FindExternalLexicalDecls(
        const clang::DeclContext *DC,
        llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
        llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
      return m_original.FindExternalLexicalDecls(DC, IsKindWeWant, Decls);
    }

    void CompleteType(clang::TagDecl *Tag) override {
      return m_original.CompleteType(Tag);
    }

    void CompleteType(clang::ObjCInterfaceDecl *Class) override {
      return m_original.CompleteType(Class);
    }

    bool layoutRecordType(
        const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
        llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
        llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
            &BaseOffsets,
        llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
            &VirtualBaseOffsets) override {
      return m_original.layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                         BaseOffsets, VirtualBaseOffsets);
    }

    void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
      return m_original.StartTranslationUnit(Consumer);
    }

    ClangASTMetadata *GetMetadata(const void *object) {
      return m_original.GetMetadata(object);
    }

    void SetMetadata(const void *object, ClangASTMetadata &metadata) {
      return m_original.SetMetadata(object, metadata);
    }

    bool HasMetadata(const void *object) {
      return m_original.HasMetadata(object);
    }

  private:
    ClangASTSource &m_original;
  };

  clang::ExternalASTSource *CreateProxy() {
    return new ClangASTSourceProxy(*this);
  }

protected:
  //------------------------------------------------------------------
  /// Look for the complete version of an Objective-C interface, and return it
  /// if found.
  ///
  /// @param[in] interface_decl
  ///     An ObjCInterfaceDecl that may not be the complete one.
  ///
  /// @return
  ///     NULL if the complete interface couldn't be found;
  ///     the complete interface otherwise.
  //------------------------------------------------------------------
  clang::ObjCInterfaceDecl *
  GetCompleteObjCInterface(const clang::ObjCInterfaceDecl *interface_decl);

  //------------------------------------------------------------------
  /// Find all entities matching a given name in a given module, using a
  /// NameSearchContext to make Decls for them.
  ///
  /// @param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// @param[in] module
  ///     If non-NULL, the module to query.
  ///
  /// @param[in] namespace_decl
  ///     If valid and module is non-NULL, the parent namespace.
  ///
  /// @param[in] current_id
  ///     The ID for the current FindExternalVisibleDecls invocation,
  ///     for logging purposes.
  //------------------------------------------------------------------
  void FindExternalVisibleDecls(NameSearchContext &context,
                                lldb::ModuleSP module,
                                CompilerDeclContext &namespace_decl,
                                unsigned int current_id);

  //------------------------------------------------------------------
  /// Find all Objective-C methods matching a given selector.
  ///
  /// @param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///     Its m_decl_name contains the selector and its m_decl_context
  ///     is the containing object.
  //------------------------------------------------------------------
  void FindObjCMethodDecls(NameSearchContext &context);

  //------------------------------------------------------------------
  /// Find all Objective-C properties and ivars with a given name.
  ///
  /// @param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///     Its m_decl_name contains the name and its m_decl_context
  ///     is the containing object.
  //------------------------------------------------------------------
  void FindObjCPropertyAndIvarDecls(NameSearchContext &context);

  //------------------------------------------------------------------
  /// A wrapper for ClangASTContext::CopyType that sets a flag that
  /// indicates that we should not respond to queries during import.
  ///
  /// @param[in] dest_context
  ///     The target AST context, typically the parser's AST context.
  ///
  /// @param[in] source_context
  ///     The source AST context, typically the AST context of whatever
  ///     symbol file the type was found in.
  ///
  /// @param[in] src_type
  ///     The source type.
  ///
  /// @return
  ///     The imported type.
  //------------------------------------------------------------------
  CompilerType GuardedCopyType(const CompilerType &src_type);

public:
  //------------------------------------------------------------------
  /// Returns true if a name should be ignored by name lookup.
  ///
  /// @param[in] name
  ///     The name to be considered.
  ///
  /// @param[in] ignore_all_dollar_nmmes
  ///     True if $-names of all sorts should be ignored.
  ///
  /// @return
  ///     True if the name is one of a class of names that are ignored by
  ///     global lookup for performance reasons.
  //------------------------------------------------------------------
  bool IgnoreName(const ConstString name, bool ignore_all_dollar_names);

public:
  //------------------------------------------------------------------
  /// Copies a single Decl into the parser's AST context.
  ///
  /// @param[in] src_decl
  ///     The Decl to copy.
  ///
  /// @return
  ///     A copy of the Decl in m_ast_context, or NULL if the copy failed.
  //------------------------------------------------------------------
  clang::Decl *CopyDecl(clang::Decl *src_decl);
                         
  //------------------------------------------------------------------
  /// Copies a single Type to the target of the given ExternalASTMerger.
  ///
  /// @param[in] src_context
  ///     The ASTContext containing the type.
  ///
  /// @param[in] merger
  ///     The merger to use.  This isn't just *m_merger_up because it might be
  ///     the persistent AST context's merger.
  ///
  /// @param[in] type
  ///     The type to copy.
  ///
  /// @return
  ///     A copy of the Type in the merger's target context.
  //------------------------------------------------------------------
	clang::QualType CopyTypeWithMerger(clang::ASTContext &src_context,
                                     clang::ExternalASTMerger &merger,
                                     clang::QualType type);

  //------------------------------------------------------------------
  /// Determined the origin of a single Decl, if it can be found.
  ///
  /// @param[in] decl
  ///     The Decl whose origin is to be found.
  ///
  /// @param[out] original_decl
  ///     A pointer whose target is filled in with the original Decl.
  ///
  /// @param[in] original_ctx
  ///     A pointer whose target is filled in with the original's ASTContext.
  ///
  /// @return
  ///     True if lookup succeeded; false otherwise.
  //------------------------------------------------------------------
  bool ResolveDeclOrigin(const clang::Decl *decl, clang::Decl **original_decl,
                         clang::ASTContext **original_ctx);
 
  //------------------------------------------------------------------
  /// Returns m_merger_up.  Only call this if the target is configured to use
  /// modern lookup,
  //------------------------------------------------------------------
	clang::ExternalASTMerger &GetMergerUnchecked();
 
  //------------------------------------------------------------------
  /// Returns true if there is a merger.  This only occurs if the target is
  /// using modern lookup.
  //------------------------------------------------------------------
  bool HasMerger() { return (bool)m_merger_up; }

protected:
  bool FindObjCMethodDeclsWithOrigin(
      unsigned int current_id, NameSearchContext &context,
      clang::ObjCInterfaceDecl *original_interface_decl, const char *log_info);

  friend struct NameSearchContext;

  bool m_import_in_progress;
  bool m_lookups_enabled;

  const lldb::TargetSP
      m_target; ///< The target to use in finding variables and types.
  clang::ASTContext
      *m_ast_context; ///< The AST context requests are coming in for.
  clang::FileManager
      *m_file_manager; ///< The file manager paired with the AST context.
  lldb::ClangASTImporterSP m_ast_importer_sp; ///< The target's AST importer.
  std::unique_ptr<clang::ExternalASTMerger> m_merger_up;
      ///< The ExternalASTMerger for this parse.
  std::set<const clang::Decl *> m_active_lexical_decls;
  std::set<const char *> m_active_lookups;
};

//----------------------------------------------------------------------
/// @class NameSearchContext ClangASTSource.h
/// "lldb/Expression/ClangASTSource.h" Container for all objects relevant to a
/// single name lookup
///
/// LLDB needs to create Decls for entities it finds.  This class communicates
/// what name is being searched for and provides helper functions to construct
/// Decls given appropriate type information.
//----------------------------------------------------------------------
struct NameSearchContext {
  ClangASTSource &m_ast_source; ///< The AST source making the request
  llvm::SmallVectorImpl<clang::NamedDecl *>
      &m_decls; ///< The list of declarations already constructed
  ClangASTImporter::NamespaceMapSP m_namespace_map; ///< The mapping of all
                                                    ///namespaces found for this
                                                    ///request back to their
                                                    ///modules
  const clang::DeclarationName &m_decl_name; ///< The name being looked for
  const clang::DeclContext
      *m_decl_context; ///< The DeclContext to put declarations into
  llvm::SmallSet<CompilerType, 5> m_function_types; ///< All the types of
                                                    ///functions that have been
                                                    ///reported, so we don't
                                                    ///report conflicts

  struct {
    bool variable : 1;
    bool function_with_type_info : 1;
    bool function : 1;
    bool local_vars_nsp : 1;
    bool type : 1;
  } m_found;

  //------------------------------------------------------------------
  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// @param[in] astSource
  ///     A reference to the AST source making a request.
  ///
  /// @param[in] decls
  ///     A reference to a list into which new Decls will be placed.  This
  ///     list is typically empty when the function is called.
  ///
  /// @param[in] name
  ///     The name being searched for (always an Identifier).
  ///
  /// @param[in] dc
  ///     The DeclContext to register Decls in.
  //------------------------------------------------------------------
  NameSearchContext(ClangASTSource &astSource,
                    llvm::SmallVectorImpl<clang::NamedDecl *> &decls,
                    clang::DeclarationName &name, const clang::DeclContext *dc)
      : m_ast_source(astSource), m_decls(decls), m_decl_name(name),
        m_decl_context(dc) {
    memset(&m_found, 0, sizeof(m_found));
  }

  //------------------------------------------------------------------
  /// Create a VarDecl with the name being searched for and the provided type
  /// and register it in the right places.
  ///
  /// @param[in] type
  ///     The opaque QualType for the VarDecl being registered.
  //------------------------------------------------------------------
  clang::NamedDecl *AddVarDecl(const CompilerType &type);

  //------------------------------------------------------------------
  /// Create a FunDecl with the name being searched for and the provided type
  /// and register it in the right places.
  ///
  /// @param[in] type
  ///     The opaque QualType for the FunDecl being registered.
  ///
  /// @param[in] extern_c
  ///     If true, build an extern "C" linkage specification for this.
  //------------------------------------------------------------------
  clang::NamedDecl *AddFunDecl(const CompilerType &type, bool extern_c = false);

  //------------------------------------------------------------------
  /// Create a FunDecl with the name being searched for and generic type (i.e.
  /// intptr_t NAME_GOES_HERE(...)) and register it in the right places.
  //------------------------------------------------------------------
  clang::NamedDecl *AddGenericFunDecl();

  //------------------------------------------------------------------
  /// Create a TypeDecl with the name being searched for and the provided type
  /// and register it in the right places.
  ///
  /// @param[in] compiler_type
  ///     The opaque QualType for the TypeDecl being registered.
  //------------------------------------------------------------------
  clang::NamedDecl *AddTypeDecl(const CompilerType &compiler_type);

  //------------------------------------------------------------------
  /// Add Decls from the provided DeclContextLookupResult to the list of
  /// results.
  ///
  /// @param[in] result
  ///     The DeclContextLookupResult, usually returned as the result
  ///     of querying a DeclContext.
  //------------------------------------------------------------------
  void AddLookupResult(clang::DeclContextLookupResult result);

  //------------------------------------------------------------------
  /// Add a NamedDecl to the list of results.
  ///
  /// @param[in] decl
  ///     The NamedDecl, usually returned as the result
  ///     of querying a DeclContext.
  //------------------------------------------------------------------
  void AddNamedDecl(clang::NamedDecl *decl);
};

} // namespace lldb_private

#endif // liblldb_ClangASTSource_h_
