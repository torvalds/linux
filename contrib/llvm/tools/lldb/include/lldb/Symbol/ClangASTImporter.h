//===-- ClangASTImporter.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangASTImporter_h_
#define liblldb_ClangASTImporter_h_

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "clang/AST/ASTImporter.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"

#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseMap.h"

namespace lldb_private {

class ClangASTMetrics {
public:
  static void DumpCounters(Log *log);
  static void ClearLocalCounters() { local_counters = {0, 0, 0, 0, 0, 0}; }

  static void RegisterVisibleQuery() {
    ++global_counters.m_visible_query_count;
    ++local_counters.m_visible_query_count;
  }

  static void RegisterLexicalQuery() {
    ++global_counters.m_lexical_query_count;
    ++local_counters.m_lexical_query_count;
  }

  static void RegisterLLDBImport() {
    ++global_counters.m_lldb_import_count;
    ++local_counters.m_lldb_import_count;
  }

  static void RegisterClangImport() {
    ++global_counters.m_clang_import_count;
    ++local_counters.m_clang_import_count;
  }

  static void RegisterDeclCompletion() {
    ++global_counters.m_decls_completed_count;
    ++local_counters.m_decls_completed_count;
  }

  static void RegisterRecordLayout() {
    ++global_counters.m_record_layout_count;
    ++local_counters.m_record_layout_count;
  }

private:
  struct Counters {
    uint64_t m_visible_query_count;
    uint64_t m_lexical_query_count;
    uint64_t m_lldb_import_count;
    uint64_t m_clang_import_count;
    uint64_t m_decls_completed_count;
    uint64_t m_record_layout_count;
  };

  static Counters global_counters;
  static Counters local_counters;

  static void DumpCounters(Log *log, Counters &counters);
};

class ClangASTImporter {
public:
  struct LayoutInfo {
    LayoutInfo()
        : bit_size(0), alignment(0), field_offsets(), base_offsets(),
          vbase_offsets() {}
    uint64_t bit_size;
    uint64_t alignment;
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> field_offsets;
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> base_offsets;
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        vbase_offsets;
  };

  ClangASTImporter() : m_file_manager(clang::FileSystemOptions()) {}

  clang::QualType CopyType(clang::ASTContext *dst_ctx,
                           clang::ASTContext *src_ctx, clang::QualType type);

  lldb::opaque_compiler_type_t CopyType(clang::ASTContext *dst_ctx,
                                        clang::ASTContext *src_ctx,
                                        lldb::opaque_compiler_type_t type);

  CompilerType CopyType(ClangASTContext &dst, const CompilerType &src_type);

  clang::Decl *CopyDecl(clang::ASTContext *dst_ctx, clang::ASTContext *src_ctx,
                        clang::Decl *decl);

  lldb::opaque_compiler_type_t DeportType(clang::ASTContext *dst_ctx,
                                          clang::ASTContext *src_ctx,
                                          lldb::opaque_compiler_type_t type);

  clang::Decl *DeportDecl(clang::ASTContext *dst_ctx,
                          clang::ASTContext *src_ctx, clang::Decl *decl);

  void InsertRecordDecl(clang::RecordDecl *decl, const LayoutInfo &layout);

  bool LayoutRecordType(
      const clang::RecordDecl *record_decl, uint64_t &bit_size,
      uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);

  bool CanImport(const CompilerType &type);

  bool Import(const CompilerType &type);

  bool CompleteType(const CompilerType &compiler_type);

  void CompleteDecl(clang::Decl *decl);

  bool CompleteTagDecl(clang::TagDecl *decl);

  bool CompleteTagDeclWithOrigin(clang::TagDecl *decl, clang::TagDecl *origin);

  bool CompleteObjCInterfaceDecl(clang::ObjCInterfaceDecl *interface_decl);

  bool CompleteAndFetchChildren(clang::QualType type);

  bool RequireCompleteType(clang::QualType type);

  bool ResolveDeclOrigin(const clang::Decl *decl, clang::Decl **original_decl,
                         clang::ASTContext **original_ctx) {
    DeclOrigin origin = GetDeclOrigin(decl);

    if (original_decl)
      *original_decl = origin.decl;

    if (original_ctx)
      *original_ctx = origin.ctx;

    return origin.Valid();
  }

  void SetDeclOrigin(const clang::Decl *decl, clang::Decl *original_decl);

  ClangASTMetadata *GetDeclMetadata(const clang::Decl *decl);

  //
  // Namespace maps
  //

  typedef std::vector<std::pair<lldb::ModuleSP, CompilerDeclContext>>
      NamespaceMap;
  typedef std::shared_ptr<NamespaceMap> NamespaceMapSP;

  void RegisterNamespaceMap(const clang::NamespaceDecl *decl,
                            NamespaceMapSP &namespace_map);

  NamespaceMapSP GetNamespaceMap(const clang::NamespaceDecl *decl);

  void BuildNamespaceMap(const clang::NamespaceDecl *decl);

  //
  // Completers for maps
  //

  class MapCompleter {
  public:
    virtual ~MapCompleter();

    virtual void CompleteNamespaceMap(NamespaceMapSP &namespace_map,
                                      const ConstString &name,
                                      NamespaceMapSP &parent_map) const = 0;
  };

  void InstallMapCompleter(clang::ASTContext *dst_ctx,
                           MapCompleter &completer) {
    ASTContextMetadataSP context_md;
    ContextMetadataMap::iterator context_md_iter = m_metadata_map.find(dst_ctx);

    if (context_md_iter == m_metadata_map.end()) {
      context_md = ASTContextMetadataSP(new ASTContextMetadata(dst_ctx));
      m_metadata_map[dst_ctx] = context_md;
    } else {
      context_md = context_md_iter->second;
    }

    context_md->m_map_completer = &completer;
  }

  void ForgetDestination(clang::ASTContext *dst_ctx);
  void ForgetSource(clang::ASTContext *dst_ctx, clang::ASTContext *src_ctx);

private:
  struct DeclOrigin {
    DeclOrigin() : ctx(nullptr), decl(nullptr) {}

    DeclOrigin(clang::ASTContext *_ctx, clang::Decl *_decl)
        : ctx(_ctx), decl(_decl) {}

    DeclOrigin(const DeclOrigin &rhs) {
      ctx = rhs.ctx;
      decl = rhs.decl;
    }

    void operator=(const DeclOrigin &rhs) {
      ctx = rhs.ctx;
      decl = rhs.decl;
    }

    bool Valid() { return (ctx != nullptr || decl != nullptr); }

    clang::ASTContext *ctx;
    clang::Decl *decl;
  };

  typedef std::map<const clang::Decl *, DeclOrigin> OriginMap;

  class Minion : public clang::ASTImporter {
  public:
    Minion(ClangASTImporter &master, clang::ASTContext *target_ctx,
           clang::ASTContext *source_ctx)
        : clang::ASTImporter(*target_ctx, master.m_file_manager, *source_ctx,
                             master.m_file_manager, true /*minimal*/),
          m_decls_to_deport(nullptr), m_decls_already_deported(nullptr),
          m_master(master), m_source_ctx(source_ctx) {}

    // A call to "InitDeportWorkQueues" puts the minion into deport mode.
    // In deport mode, every copied Decl that could require completion is
    // recorded and placed into the decls_to_deport set.
    //
    // A call to "ExecuteDeportWorkQueues" completes all the Decls that
    // are in decls_to_deport, adding any Decls it sees along the way that it
    // hasn't already deported.  It proceeds until decls_to_deport is empty.
    //
    // These calls must be paired.  Leaving a minion in deport mode or trying
    // to start deport minion with a new pair of queues will result in an
    // assertion failure.

    void
    InitDeportWorkQueues(std::set<clang::NamedDecl *> *decls_to_deport,
                         std::set<clang::NamedDecl *> *decls_already_deported);
    void ExecuteDeportWorkQueues();

    void ImportDefinitionTo(clang::Decl *to, clang::Decl *from);

    clang::Decl *Imported(clang::Decl *from, clang::Decl *to) override;

    clang::Decl *GetOriginalDecl(clang::Decl *To) override;

    std::set<clang::NamedDecl *> *m_decls_to_deport;
    std::set<clang::NamedDecl *> *m_decls_already_deported;
    ClangASTImporter &m_master;
    clang::ASTContext *m_source_ctx;
  };

  typedef std::shared_ptr<Minion> MinionSP;
  typedef std::map<clang::ASTContext *, MinionSP> MinionMap;
  typedef std::map<const clang::NamespaceDecl *, NamespaceMapSP>
      NamespaceMetaMap;

  struct ASTContextMetadata {
    ASTContextMetadata(clang::ASTContext *dst_ctx)
        : m_dst_ctx(dst_ctx), m_minions(), m_origins(), m_namespace_maps(),
          m_map_completer(nullptr) {}

    clang::ASTContext *m_dst_ctx;
    MinionMap m_minions;
    OriginMap m_origins;

    NamespaceMetaMap m_namespace_maps;
    MapCompleter *m_map_completer;
  };

  typedef std::shared_ptr<ASTContextMetadata> ASTContextMetadataSP;
  typedef std::map<const clang::ASTContext *, ASTContextMetadataSP>
      ContextMetadataMap;

  ContextMetadataMap m_metadata_map;

  ASTContextMetadataSP GetContextMetadata(clang::ASTContext *dst_ctx) {
    ContextMetadataMap::iterator context_md_iter = m_metadata_map.find(dst_ctx);

    if (context_md_iter == m_metadata_map.end()) {
      ASTContextMetadataSP context_md =
          ASTContextMetadataSP(new ASTContextMetadata(dst_ctx));
      m_metadata_map[dst_ctx] = context_md;
      return context_md;
    } else {
      return context_md_iter->second;
    }
  }

  ASTContextMetadataSP MaybeGetContextMetadata(clang::ASTContext *dst_ctx) {
    ContextMetadataMap::iterator context_md_iter = m_metadata_map.find(dst_ctx);

    if (context_md_iter != m_metadata_map.end())
      return context_md_iter->second;
    else
      return ASTContextMetadataSP();
  }

  MinionSP GetMinion(clang::ASTContext *dst_ctx, clang::ASTContext *src_ctx) {
    ASTContextMetadataSP context_md = GetContextMetadata(dst_ctx);

    MinionMap &minions = context_md->m_minions;
    MinionMap::iterator minion_iter = minions.find(src_ctx);

    if (minion_iter == minions.end()) {
      MinionSP minion = MinionSP(new Minion(*this, dst_ctx, src_ctx));
      minions[src_ctx] = minion;
      return minion;
    } else {
      return minion_iter->second;
    }
  }

  DeclOrigin GetDeclOrigin(const clang::Decl *decl);

  clang::FileManager m_file_manager;
  typedef llvm::DenseMap<const clang::RecordDecl *, LayoutInfo>
      RecordDeclToLayoutMap;

  RecordDeclToLayoutMap m_record_decl_to_layout_map;
};

} // namespace lldb_private

#endif // liblldb_ClangASTImporter_h_
