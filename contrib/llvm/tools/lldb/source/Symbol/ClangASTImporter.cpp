//===-- ClangASTImporter.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;
using namespace clang;

ClangASTMetrics::Counters ClangASTMetrics::global_counters = {0, 0, 0, 0, 0, 0};
ClangASTMetrics::Counters ClangASTMetrics::local_counters = {0, 0, 0, 0, 0, 0};

void ClangASTMetrics::DumpCounters(Log *log,
                                   ClangASTMetrics::Counters &counters) {
  log->Printf("  Number of visible Decl queries by name     : %" PRIu64,
              counters.m_visible_query_count);
  log->Printf("  Number of lexical Decl queries             : %" PRIu64,
              counters.m_lexical_query_count);
  log->Printf("  Number of imports initiated by LLDB        : %" PRIu64,
              counters.m_lldb_import_count);
  log->Printf("  Number of imports conducted by Clang       : %" PRIu64,
              counters.m_clang_import_count);
  log->Printf("  Number of Decls completed                  : %" PRIu64,
              counters.m_decls_completed_count);
  log->Printf("  Number of records laid out                 : %" PRIu64,
              counters.m_record_layout_count);
}

void ClangASTMetrics::DumpCounters(Log *log) {
  if (!log)
    return;

  log->Printf("== ClangASTMetrics output ==");
  log->Printf("-- Global metrics --");
  DumpCounters(log, global_counters);
  log->Printf("-- Local metrics --");
  DumpCounters(log, local_counters);
}

clang::QualType ClangASTImporter::CopyType(clang::ASTContext *dst_ast,
                                           clang::ASTContext *src_ast,
                                           clang::QualType type) {
  MinionSP minion_sp(GetMinion(dst_ast, src_ast));

  if (minion_sp)
    return minion_sp->Import(type);

  return QualType();
}

lldb::opaque_compiler_type_t
ClangASTImporter::CopyType(clang::ASTContext *dst_ast,
                           clang::ASTContext *src_ast,
                           lldb::opaque_compiler_type_t type) {
  return CopyType(dst_ast, src_ast, QualType::getFromOpaquePtr(type))
      .getAsOpaquePtr();
}

CompilerType ClangASTImporter::CopyType(ClangASTContext &dst_ast,
                                        const CompilerType &src_type) {
  clang::ASTContext *dst_clang_ast = dst_ast.getASTContext();
  if (dst_clang_ast) {
    ClangASTContext *src_ast =
        llvm::dyn_cast_or_null<ClangASTContext>(src_type.GetTypeSystem());
    if (src_ast) {
      clang::ASTContext *src_clang_ast = src_ast->getASTContext();
      if (src_clang_ast) {
        lldb::opaque_compiler_type_t dst_clang_type = CopyType(
            dst_clang_ast, src_clang_ast, src_type.GetOpaqueQualType());

        if (dst_clang_type)
          return CompilerType(&dst_ast, dst_clang_type);
      }
    }
  }
  return CompilerType();
}

clang::Decl *ClangASTImporter::CopyDecl(clang::ASTContext *dst_ast,
                                        clang::ASTContext *src_ast,
                                        clang::Decl *decl) {
  MinionSP minion_sp;

  minion_sp = GetMinion(dst_ast, src_ast);

  if (minion_sp) {
    clang::Decl *result = minion_sp->Import(decl);

    if (!result) {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

      if (log) {
        lldb::user_id_t user_id = LLDB_INVALID_UID;
        ClangASTMetadata *metadata = GetDeclMetadata(decl);
        if (metadata)
          user_id = metadata->GetUserID();

        if (NamedDecl *named_decl = dyn_cast<NamedDecl>(decl))
          log->Printf("  [ClangASTImporter] WARNING: Failed to import a %s "
                      "'%s', metadata 0x%" PRIx64,
                      decl->getDeclKindName(),
                      named_decl->getNameAsString().c_str(), user_id);
        else
          log->Printf("  [ClangASTImporter] WARNING: Failed to import a %s, "
                      "metadata 0x%" PRIx64,
                      decl->getDeclKindName(), user_id);
      }
    }

    return result;
  }

  return nullptr;
}

class DeclContextOverride {
private:
  struct Backup {
    clang::DeclContext *decl_context;
    clang::DeclContext *lexical_decl_context;
  };

  std::map<clang::Decl *, Backup> m_backups;

  void OverrideOne(clang::Decl *decl) {
    if (m_backups.find(decl) != m_backups.end()) {
      return;
    }

    m_backups[decl] = {decl->getDeclContext(), decl->getLexicalDeclContext()};

    decl->setDeclContext(decl->getASTContext().getTranslationUnitDecl());
    decl->setLexicalDeclContext(decl->getASTContext().getTranslationUnitDecl());
  }

  bool ChainPassesThrough(
      clang::Decl *decl, clang::DeclContext *base,
      clang::DeclContext *(clang::Decl::*contextFromDecl)(),
      clang::DeclContext *(clang::DeclContext::*contextFromContext)()) {
    for (DeclContext *decl_ctx = (decl->*contextFromDecl)(); decl_ctx;
         decl_ctx = (decl_ctx->*contextFromContext)()) {
      if (decl_ctx == base) {
        return true;
      }
    }

    return false;
  }

  clang::Decl *GetEscapedChild(clang::Decl *decl,
                               clang::DeclContext *base = nullptr) {
    if (base) {
      // decl's DeclContext chains must pass through base.

      if (!ChainPassesThrough(decl, base, &clang::Decl::getDeclContext,
                              &clang::DeclContext::getParent) ||
          !ChainPassesThrough(decl, base, &clang::Decl::getLexicalDeclContext,
                              &clang::DeclContext::getLexicalParent)) {
        return decl;
      }
    } else {
      base = clang::dyn_cast<clang::DeclContext>(decl);

      if (!base) {
        return nullptr;
      }
    }

    if (clang::DeclContext *context =
            clang::dyn_cast<clang::DeclContext>(decl)) {
      for (clang::Decl *decl : context->decls()) {
        if (clang::Decl *escaped_child = GetEscapedChild(decl)) {
          return escaped_child;
        }
      }
    }

    return nullptr;
  }

  void Override(clang::Decl *decl) {
    if (clang::Decl *escaped_child = GetEscapedChild(decl)) {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

      if (log)
        log->Printf("    [ClangASTImporter] DeclContextOverride couldn't "
                    "override (%sDecl*)%p - its child (%sDecl*)%p escapes",
                    decl->getDeclKindName(), static_cast<void *>(decl),
                    escaped_child->getDeclKindName(),
                    static_cast<void *>(escaped_child));
      lldbassert(0 && "Couldn't override!");
    }

    OverrideOne(decl);
  }

public:
  DeclContextOverride() {}

  void OverrideAllDeclsFromContainingFunction(clang::Decl *decl) {
    for (DeclContext *decl_context = decl->getLexicalDeclContext();
         decl_context; decl_context = decl_context->getLexicalParent()) {
      DeclContext *redecl_context = decl_context->getRedeclContext();

      if (llvm::isa<FunctionDecl>(redecl_context) &&
          llvm::isa<TranslationUnitDecl>(redecl_context->getLexicalParent())) {
        for (clang::Decl *child_decl : decl_context->decls()) {
          Override(child_decl);
        }
      }
    }
  }

  ~DeclContextOverride() {
    for (const std::pair<clang::Decl *, Backup> &backup : m_backups) {
      backup.first->setDeclContext(backup.second.decl_context);
      backup.first->setLexicalDeclContext(backup.second.lexical_decl_context);
    }
  }
};

lldb::opaque_compiler_type_t
ClangASTImporter::DeportType(clang::ASTContext *dst_ctx,
                             clang::ASTContext *src_ctx,
                             lldb::opaque_compiler_type_t type) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    log->Printf("    [ClangASTImporter] DeportType called on (%sType*)0x%llx "
                "from (ASTContext*)%p to (ASTContext*)%p",
                QualType::getFromOpaquePtr(type)->getTypeClassName(),
                (unsigned long long)type, static_cast<void *>(src_ctx),
                static_cast<void *>(dst_ctx));

  MinionSP minion_sp(GetMinion(dst_ctx, src_ctx));

  if (!minion_sp)
    return nullptr;

  std::set<NamedDecl *> decls_to_deport;
  std::set<NamedDecl *> decls_already_deported;

  DeclContextOverride decl_context_override;

  if (const clang::TagType *tag_type =
          clang::QualType::getFromOpaquePtr(type)->getAs<TagType>()) {
    decl_context_override.OverrideAllDeclsFromContainingFunction(
        tag_type->getDecl());
  }

  minion_sp->InitDeportWorkQueues(&decls_to_deport, &decls_already_deported);

  lldb::opaque_compiler_type_t result = CopyType(dst_ctx, src_ctx, type);

  minion_sp->ExecuteDeportWorkQueues();

  if (!result)
    return nullptr;

  return result;
}

clang::Decl *ClangASTImporter::DeportDecl(clang::ASTContext *dst_ctx,
                                          clang::ASTContext *src_ctx,
                                          clang::Decl *decl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    log->Printf("    [ClangASTImporter] DeportDecl called on (%sDecl*)%p from "
                "(ASTContext*)%p to (ASTContext*)%p",
                decl->getDeclKindName(), static_cast<void *>(decl),
                static_cast<void *>(src_ctx), static_cast<void *>(dst_ctx));

  MinionSP minion_sp(GetMinion(dst_ctx, src_ctx));

  if (!minion_sp)
    return nullptr;

  std::set<NamedDecl *> decls_to_deport;
  std::set<NamedDecl *> decls_already_deported;

  DeclContextOverride decl_context_override;

  decl_context_override.OverrideAllDeclsFromContainingFunction(decl);

  minion_sp->InitDeportWorkQueues(&decls_to_deport, &decls_already_deported);

  clang::Decl *result = CopyDecl(dst_ctx, src_ctx, decl);

  minion_sp->ExecuteDeportWorkQueues();

  if (!result)
    return nullptr;

  if (log)
    log->Printf(
        "    [ClangASTImporter] DeportDecl deported (%sDecl*)%p to (%sDecl*)%p",
        decl->getDeclKindName(), static_cast<void *>(decl),
        result->getDeclKindName(), static_cast<void *>(result));

  return result;
}

bool ClangASTImporter::CanImport(const CompilerType &type) {
  if (!ClangUtil::IsClangType(type))
    return false;

  // TODO: remove external completion BOOL
  // CompleteAndFetchChildren should get the Decl out and check for the

  clang::QualType qual_type(
      ClangUtil::GetCanonicalQualType(ClangUtil::RemoveFastQualifiers(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    const clang::CXXRecordDecl *cxx_record_decl =
        qual_type->getAsCXXRecordDecl();
    if (cxx_record_decl) {
      if (ResolveDeclOrigin(cxx_record_decl, NULL, NULL))
        return true;
    }
  } break;

  case clang::Type::Enum: {
    clang::EnumDecl *enum_decl =
        llvm::cast<clang::EnumType>(qual_type)->getDecl();
    if (enum_decl) {
      if (ResolveDeclOrigin(enum_decl, NULL, NULL))
        return true;
    }
  } break;

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface: {
    const clang::ObjCObjectType *objc_class_type =
        llvm::dyn_cast<clang::ObjCObjectType>(qual_type);
    if (objc_class_type) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_class_type->getInterface();
      // We currently can't complete objective C types through the newly added
      // ASTContext because it only supports TagDecl objects right now...
      if (class_interface_decl) {
        if (ResolveDeclOrigin(class_interface_decl, NULL, NULL))
          return true;
      }
    }
  } break;

  case clang::Type::Typedef:
    return CanImport(CompilerType(type.GetTypeSystem(),
                                  llvm::cast<clang::TypedefType>(qual_type)
                                      ->getDecl()
                                      ->getUnderlyingType()
                                      .getAsOpaquePtr()));

  case clang::Type::Auto:
    return CanImport(CompilerType(type.GetTypeSystem(),
                                  llvm::cast<clang::AutoType>(qual_type)
                                      ->getDeducedType()
                                      .getAsOpaquePtr()));

  case clang::Type::Elaborated:
    return CanImport(CompilerType(type.GetTypeSystem(),
                                  llvm::cast<clang::ElaboratedType>(qual_type)
                                      ->getNamedType()
                                      .getAsOpaquePtr()));

  case clang::Type::Paren:
    return CanImport(CompilerType(
        type.GetTypeSystem(),
        llvm::cast<clang::ParenType>(qual_type)->desugar().getAsOpaquePtr()));

  default:
    break;
  }

  return false;
}

bool ClangASTImporter::Import(const CompilerType &type) {
  if (!ClangUtil::IsClangType(type))
    return false;
  // TODO: remove external completion BOOL
  // CompleteAndFetchChildren should get the Decl out and check for the

  clang::QualType qual_type(
      ClangUtil::GetCanonicalQualType(ClangUtil::RemoveFastQualifiers(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    const clang::CXXRecordDecl *cxx_record_decl =
        qual_type->getAsCXXRecordDecl();
    if (cxx_record_decl) {
      if (ResolveDeclOrigin(cxx_record_decl, NULL, NULL))
        return CompleteAndFetchChildren(qual_type);
    }
  } break;

  case clang::Type::Enum: {
    clang::EnumDecl *enum_decl =
        llvm::cast<clang::EnumType>(qual_type)->getDecl();
    if (enum_decl) {
      if (ResolveDeclOrigin(enum_decl, NULL, NULL))
        return CompleteAndFetchChildren(qual_type);
    }
  } break;

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface: {
    const clang::ObjCObjectType *objc_class_type =
        llvm::dyn_cast<clang::ObjCObjectType>(qual_type);
    if (objc_class_type) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_class_type->getInterface();
      // We currently can't complete objective C types through the newly added
      // ASTContext because it only supports TagDecl objects right now...
      if (class_interface_decl) {
        if (ResolveDeclOrigin(class_interface_decl, NULL, NULL))
          return CompleteAndFetchChildren(qual_type);
      }
    }
  } break;

  case clang::Type::Typedef:
    return Import(CompilerType(type.GetTypeSystem(),
                               llvm::cast<clang::TypedefType>(qual_type)
                                   ->getDecl()
                                   ->getUnderlyingType()
                                   .getAsOpaquePtr()));

  case clang::Type::Auto:
    return Import(CompilerType(type.GetTypeSystem(),
                               llvm::cast<clang::AutoType>(qual_type)
                                   ->getDeducedType()
                                   .getAsOpaquePtr()));

  case clang::Type::Elaborated:
    return Import(CompilerType(type.GetTypeSystem(),
                               llvm::cast<clang::ElaboratedType>(qual_type)
                                   ->getNamedType()
                                   .getAsOpaquePtr()));

  case clang::Type::Paren:
    return Import(CompilerType(
        type.GetTypeSystem(),
        llvm::cast<clang::ParenType>(qual_type)->desugar().getAsOpaquePtr()));

  default:
    break;
  }
  return false;
}

bool ClangASTImporter::CompleteType(const CompilerType &compiler_type) {
  if (!CanImport(compiler_type))
    return false;

  if (Import(compiler_type)) {
    ClangASTContext::CompleteTagDeclarationDefinition(compiler_type);
    return true;
  }

  ClangASTContext::SetHasExternalStorage(compiler_type.GetOpaqueQualType(),
                                         false);
  return false;
}

bool ClangASTImporter::LayoutRecordType(
    const clang::RecordDecl *record_decl, uint64_t &bit_size,
    uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  RecordDeclToLayoutMap::iterator pos =
      m_record_decl_to_layout_map.find(record_decl);
  bool success = false;
  base_offsets.clear();
  vbase_offsets.clear();
  if (pos != m_record_decl_to_layout_map.end()) {
    bit_size = pos->second.bit_size;
    alignment = pos->second.alignment;
    field_offsets.swap(pos->second.field_offsets);
    base_offsets.swap(pos->second.base_offsets);
    vbase_offsets.swap(pos->second.vbase_offsets);
    m_record_decl_to_layout_map.erase(pos);
    success = true;
  } else {
    bit_size = 0;
    alignment = 0;
    field_offsets.clear();
  }
  return success;
}

void ClangASTImporter::InsertRecordDecl(clang::RecordDecl *decl,
                                        const LayoutInfo &layout) {
  m_record_decl_to_layout_map.insert(std::make_pair(decl, layout));
}

void ClangASTImporter::CompleteDecl(clang::Decl *decl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    log->Printf("    [ClangASTImporter] CompleteDecl called on (%sDecl*)%p",
                decl->getDeclKindName(), static_cast<void *>(decl));

  if (ObjCInterfaceDecl *interface_decl = dyn_cast<ObjCInterfaceDecl>(decl)) {
    if (!interface_decl->getDefinition()) {
      interface_decl->startDefinition();
      CompleteObjCInterfaceDecl(interface_decl);
    }
  } else if (ObjCProtocolDecl *protocol_decl =
                 dyn_cast<ObjCProtocolDecl>(decl)) {
    if (!protocol_decl->getDefinition())
      protocol_decl->startDefinition();
  } else if (TagDecl *tag_decl = dyn_cast<TagDecl>(decl)) {
    if (!tag_decl->getDefinition() && !tag_decl->isBeingDefined()) {
      tag_decl->startDefinition();
      CompleteTagDecl(tag_decl);
      tag_decl->setCompleteDefinition(true);
    }
  } else {
    assert(0 && "CompleteDecl called on a Decl that can't be completed");
  }
}

bool ClangASTImporter::CompleteTagDecl(clang::TagDecl *decl) {
  ClangASTMetrics::RegisterDeclCompletion();

  DeclOrigin decl_origin = GetDeclOrigin(decl);

  if (!decl_origin.Valid())
    return false;

  if (!ClangASTContext::GetCompleteDecl(decl_origin.ctx, decl_origin.decl))
    return false;

  MinionSP minion_sp(GetMinion(&decl->getASTContext(), decl_origin.ctx));

  if (minion_sp)
    minion_sp->ImportDefinitionTo(decl, decl_origin.decl);

  return true;
}

bool ClangASTImporter::CompleteTagDeclWithOrigin(clang::TagDecl *decl,
                                                 clang::TagDecl *origin_decl) {
  ClangASTMetrics::RegisterDeclCompletion();

  clang::ASTContext *origin_ast_ctx = &origin_decl->getASTContext();

  if (!ClangASTContext::GetCompleteDecl(origin_ast_ctx, origin_decl))
    return false;

  MinionSP minion_sp(GetMinion(&decl->getASTContext(), origin_ast_ctx));

  if (minion_sp)
    minion_sp->ImportDefinitionTo(decl, origin_decl);

  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  OriginMap &origins = context_md->m_origins;

  origins[decl] = DeclOrigin(origin_ast_ctx, origin_decl);

  return true;
}

bool ClangASTImporter::CompleteObjCInterfaceDecl(
    clang::ObjCInterfaceDecl *interface_decl) {
  ClangASTMetrics::RegisterDeclCompletion();

  DeclOrigin decl_origin = GetDeclOrigin(interface_decl);

  if (!decl_origin.Valid())
    return false;

  if (!ClangASTContext::GetCompleteDecl(decl_origin.ctx, decl_origin.decl))
    return false;

  MinionSP minion_sp(
      GetMinion(&interface_decl->getASTContext(), decl_origin.ctx));

  if (minion_sp)
    minion_sp->ImportDefinitionTo(interface_decl, decl_origin.decl);

  if (ObjCInterfaceDecl *super_class = interface_decl->getSuperClass())
    RequireCompleteType(clang::QualType(super_class->getTypeForDecl(), 0));

  return true;
}

bool ClangASTImporter::CompleteAndFetchChildren(clang::QualType type) {
  if (!RequireCompleteType(type))
    return false;

  if (const TagType *tag_type = type->getAs<TagType>()) {
    TagDecl *tag_decl = tag_type->getDecl();

    DeclOrigin decl_origin = GetDeclOrigin(tag_decl);

    if (!decl_origin.Valid())
      return false;

    MinionSP minion_sp(GetMinion(&tag_decl->getASTContext(), decl_origin.ctx));

    TagDecl *origin_tag_decl = llvm::dyn_cast<TagDecl>(decl_origin.decl);

    for (Decl *origin_child_decl : origin_tag_decl->decls()) {
      minion_sp->Import(origin_child_decl);
    }

    if (RecordDecl *record_decl = dyn_cast<RecordDecl>(origin_tag_decl)) {
      record_decl->setHasLoadedFieldsFromExternalStorage(true);
    }

    return true;
  }

  if (const ObjCObjectType *objc_object_type = type->getAs<ObjCObjectType>()) {
    if (ObjCInterfaceDecl *objc_interface_decl =
            objc_object_type->getInterface()) {
      DeclOrigin decl_origin = GetDeclOrigin(objc_interface_decl);

      if (!decl_origin.Valid())
        return false;

      MinionSP minion_sp(
          GetMinion(&objc_interface_decl->getASTContext(), decl_origin.ctx));

      ObjCInterfaceDecl *origin_interface_decl =
          llvm::dyn_cast<ObjCInterfaceDecl>(decl_origin.decl);

      for (Decl *origin_child_decl : origin_interface_decl->decls()) {
        minion_sp->Import(origin_child_decl);
      }

      return true;
    } else {
      return false;
    }
  }

  return true;
}

bool ClangASTImporter::RequireCompleteType(clang::QualType type) {
  if (type.isNull())
    return false;

  if (const TagType *tag_type = type->getAs<TagType>()) {
    TagDecl *tag_decl = tag_type->getDecl();

    if (tag_decl->getDefinition() || tag_decl->isBeingDefined())
      return true;

    return CompleteTagDecl(tag_decl);
  }
  if (const ObjCObjectType *objc_object_type = type->getAs<ObjCObjectType>()) {
    if (ObjCInterfaceDecl *objc_interface_decl =
            objc_object_type->getInterface())
      return CompleteObjCInterfaceDecl(objc_interface_decl);
    else
      return false;
  }
  if (const ArrayType *array_type = type->getAsArrayTypeUnsafe()) {
    return RequireCompleteType(array_type->getElementType());
  }
  if (const AtomicType *atomic_type = type->getAs<AtomicType>()) {
    return RequireCompleteType(atomic_type->getPointeeType());
  }

  return true;
}

ClangASTMetadata *ClangASTImporter::GetDeclMetadata(const clang::Decl *decl) {
  DeclOrigin decl_origin = GetDeclOrigin(decl);

  if (decl_origin.Valid())
    return ClangASTContext::GetMetadata(decl_origin.ctx, decl_origin.decl);
  else
    return ClangASTContext::GetMetadata(&decl->getASTContext(), decl);
}

ClangASTImporter::DeclOrigin
ClangASTImporter::GetDeclOrigin(const clang::Decl *decl) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  OriginMap &origins = context_md->m_origins;

  OriginMap::iterator iter = origins.find(decl);

  if (iter != origins.end())
    return iter->second;
  else
    return DeclOrigin();
}

void ClangASTImporter::SetDeclOrigin(const clang::Decl *decl,
                                     clang::Decl *original_decl) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  OriginMap &origins = context_md->m_origins;

  OriginMap::iterator iter = origins.find(decl);

  if (iter != origins.end()) {
    iter->second.decl = original_decl;
    iter->second.ctx = &original_decl->getASTContext();
  } else {
    origins[decl] = DeclOrigin(&original_decl->getASTContext(), original_decl);
  }
}

void ClangASTImporter::RegisterNamespaceMap(const clang::NamespaceDecl *decl,
                                            NamespaceMapSP &namespace_map) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  context_md->m_namespace_maps[decl] = namespace_map;
}

ClangASTImporter::NamespaceMapSP
ClangASTImporter::GetNamespaceMap(const clang::NamespaceDecl *decl) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  NamespaceMetaMap &namespace_maps = context_md->m_namespace_maps;

  NamespaceMetaMap::iterator iter = namespace_maps.find(decl);

  if (iter != namespace_maps.end())
    return iter->second;
  else
    return NamespaceMapSP();
}

void ClangASTImporter::BuildNamespaceMap(const clang::NamespaceDecl *decl) {
  assert(decl);
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  const DeclContext *parent_context = decl->getDeclContext();
  const NamespaceDecl *parent_namespace =
      dyn_cast<NamespaceDecl>(parent_context);
  NamespaceMapSP parent_map;

  if (parent_namespace)
    parent_map = GetNamespaceMap(parent_namespace);

  NamespaceMapSP new_map;

  new_map.reset(new NamespaceMap);

  if (context_md->m_map_completer) {
    std::string namespace_string = decl->getDeclName().getAsString();

    context_md->m_map_completer->CompleteNamespaceMap(
        new_map, ConstString(namespace_string.c_str()), parent_map);
  }

  context_md->m_namespace_maps[decl] = new_map;
}

void ClangASTImporter::ForgetDestination(clang::ASTContext *dst_ast) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    log->Printf("    [ClangASTImporter] Forgetting destination (ASTContext*)%p",
                static_cast<void *>(dst_ast));

  m_metadata_map.erase(dst_ast);
}

void ClangASTImporter::ForgetSource(clang::ASTContext *dst_ast,
                                    clang::ASTContext *src_ast) {
  ASTContextMetadataSP md = MaybeGetContextMetadata(dst_ast);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    log->Printf("    [ClangASTImporter] Forgetting source->dest "
                "(ASTContext*)%p->(ASTContext*)%p",
                static_cast<void *>(src_ast), static_cast<void *>(dst_ast));

  if (!md)
    return;

  md->m_minions.erase(src_ast);

  for (OriginMap::iterator iter = md->m_origins.begin();
       iter != md->m_origins.end();) {
    if (iter->second.ctx == src_ast)
      md->m_origins.erase(iter++);
    else
      ++iter;
  }
}

ClangASTImporter::MapCompleter::~MapCompleter() { return; }

void ClangASTImporter::Minion::InitDeportWorkQueues(
    std::set<clang::NamedDecl *> *decls_to_deport,
    std::set<clang::NamedDecl *> *decls_already_deported) {
  assert(!m_decls_to_deport);
  assert(!m_decls_already_deported);

  m_decls_to_deport = decls_to_deport;
  m_decls_already_deported = decls_already_deported;
}

void ClangASTImporter::Minion::ExecuteDeportWorkQueues() {
  assert(m_decls_to_deport);
  assert(m_decls_already_deported);

  ASTContextMetadataSP to_context_md =
      m_master.GetContextMetadata(&getToContext());

  while (!m_decls_to_deport->empty()) {
    NamedDecl *decl = *m_decls_to_deport->begin();

    m_decls_already_deported->insert(decl);
    m_decls_to_deport->erase(decl);

    DeclOrigin &origin = to_context_md->m_origins[decl];
    UNUSED_IF_ASSERT_DISABLED(origin);

    assert(origin.ctx ==
           m_source_ctx); // otherwise we should never have added this
                          // because it doesn't need to be deported

    Decl *original_decl = to_context_md->m_origins[decl].decl;

    ClangASTContext::GetCompleteDecl(m_source_ctx, original_decl);

    if (TagDecl *tag_decl = dyn_cast<TagDecl>(decl)) {
      if (TagDecl *original_tag_decl = dyn_cast<TagDecl>(original_decl)) {
        if (original_tag_decl->isCompleteDefinition()) {
          ImportDefinitionTo(tag_decl, original_tag_decl);
          tag_decl->setCompleteDefinition(true);
        }
      }

      tag_decl->setHasExternalLexicalStorage(false);
      tag_decl->setHasExternalVisibleStorage(false);
    } else if (ObjCContainerDecl *container_decl =
                   dyn_cast<ObjCContainerDecl>(decl)) {
      container_decl->setHasExternalLexicalStorage(false);
      container_decl->setHasExternalVisibleStorage(false);
    }

    to_context_md->m_origins.erase(decl);
  }

  m_decls_to_deport = nullptr;
  m_decls_already_deported = nullptr;
}

void ClangASTImporter::Minion::ImportDefinitionTo(clang::Decl *to,
                                                  clang::Decl *from) {
  ASTImporter::Imported(from, to);

  /*
  if (to_objc_interface)
      to_objc_interface->startDefinition();

  CXXRecordDecl *to_cxx_record = dyn_cast<CXXRecordDecl>(to);

  if (to_cxx_record)
      to_cxx_record->startDefinition();
  */

  ImportDefinition(from);

  if (clang::TagDecl *to_tag = dyn_cast<clang::TagDecl>(to)) {
    if (clang::TagDecl *from_tag = dyn_cast<clang::TagDecl>(from)) {
      to_tag->setCompleteDefinition(from_tag->isCompleteDefinition());
    }
  }

  // If we're dealing with an Objective-C class, ensure that the inheritance
  // has been set up correctly.  The ASTImporter may not do this correctly if
  // the class was originally sourced from symbols.

  if (ObjCInterfaceDecl *to_objc_interface = dyn_cast<ObjCInterfaceDecl>(to)) {
    do {
      ObjCInterfaceDecl *to_superclass = to_objc_interface->getSuperClass();

      if (to_superclass)
        break; // we're not going to override it if it's set

      ObjCInterfaceDecl *from_objc_interface =
          dyn_cast<ObjCInterfaceDecl>(from);

      if (!from_objc_interface)
        break;

      ObjCInterfaceDecl *from_superclass = from_objc_interface->getSuperClass();

      if (!from_superclass)
        break;

      Decl *imported_from_superclass_decl = Import(from_superclass);

      if (!imported_from_superclass_decl)
        break;

      ObjCInterfaceDecl *imported_from_superclass =
          dyn_cast<ObjCInterfaceDecl>(imported_from_superclass_decl);

      if (!imported_from_superclass)
        break;

      if (!to_objc_interface->hasDefinition())
        to_objc_interface->startDefinition();

      to_objc_interface->setSuperClass(m_source_ctx->getTrivialTypeSourceInfo(
          m_source_ctx->getObjCInterfaceType(imported_from_superclass)));
    } while (0);
  }
}

clang::Decl *ClangASTImporter::Minion::Imported(clang::Decl *from,
                                                clang::Decl *to) {
  ClangASTMetrics::RegisterClangImport();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  lldb::user_id_t user_id = LLDB_INVALID_UID;
  ClangASTMetadata *metadata = m_master.GetDeclMetadata(from);
  if (metadata)
    user_id = metadata->GetUserID();

  if (log) {
    if (NamedDecl *from_named_decl = dyn_cast<clang::NamedDecl>(from)) {
      std::string name_string;
      llvm::raw_string_ostream name_stream(name_string);
      from_named_decl->printName(name_stream);
      name_stream.flush();

      log->Printf("    [ClangASTImporter] Imported (%sDecl*)%p, named %s (from "
                  "(Decl*)%p), metadata 0x%" PRIx64,
                  from->getDeclKindName(), static_cast<void *>(to),
                  name_string.c_str(), static_cast<void *>(from), user_id);
    } else {
      log->Printf("    [ClangASTImporter] Imported (%sDecl*)%p (from "
                  "(Decl*)%p), metadata 0x%" PRIx64,
                  from->getDeclKindName(), static_cast<void *>(to),
                  static_cast<void *>(from), user_id);
    }
  }

  ASTContextMetadataSP to_context_md =
      m_master.GetContextMetadata(&to->getASTContext());
  ASTContextMetadataSP from_context_md =
      m_master.MaybeGetContextMetadata(m_source_ctx);

  if (from_context_md) {
    OriginMap &origins = from_context_md->m_origins;

    OriginMap::iterator origin_iter = origins.find(from);

    if (origin_iter != origins.end()) {
      if (to_context_md->m_origins.find(to) == to_context_md->m_origins.end() ||
          user_id != LLDB_INVALID_UID) {
        if (origin_iter->second.ctx != &to->getASTContext())
          to_context_md->m_origins[to] = origin_iter->second;
      }

      MinionSP direct_completer =
          m_master.GetMinion(&to->getASTContext(), origin_iter->second.ctx);

      if (direct_completer.get() != this)
        direct_completer->ASTImporter::Imported(origin_iter->second.decl, to);

      if (log)
        log->Printf("    [ClangASTImporter] Propagated origin "
                    "(Decl*)%p/(ASTContext*)%p from (ASTContext*)%p to "
                    "(ASTContext*)%p",
                    static_cast<void *>(origin_iter->second.decl),
                    static_cast<void *>(origin_iter->second.ctx),
                    static_cast<void *>(&from->getASTContext()),
                    static_cast<void *>(&to->getASTContext()));
    } else {
      if (m_decls_to_deport && m_decls_already_deported) {
        if (isa<TagDecl>(to) || isa<ObjCInterfaceDecl>(to)) {
          RecordDecl *from_record_decl = dyn_cast<RecordDecl>(from);
          if (from_record_decl == nullptr ||
              !from_record_decl->isInjectedClassName()) {
            NamedDecl *to_named_decl = dyn_cast<NamedDecl>(to);

            if (!m_decls_already_deported->count(to_named_decl))
              m_decls_to_deport->insert(to_named_decl);
          }
        }
      }

      if (to_context_md->m_origins.find(to) == to_context_md->m_origins.end() ||
          user_id != LLDB_INVALID_UID) {
        to_context_md->m_origins[to] = DeclOrigin(m_source_ctx, from);
      }

      if (log)
        log->Printf("    [ClangASTImporter] Decl has no origin information in "
                    "(ASTContext*)%p",
                    static_cast<void *>(&from->getASTContext()));
    }

    if (clang::NamespaceDecl *to_namespace =
            dyn_cast<clang::NamespaceDecl>(to)) {
      clang::NamespaceDecl *from_namespace =
          dyn_cast<clang::NamespaceDecl>(from);

      NamespaceMetaMap &namespace_maps = from_context_md->m_namespace_maps;

      NamespaceMetaMap::iterator namespace_map_iter =
          namespace_maps.find(from_namespace);

      if (namespace_map_iter != namespace_maps.end())
        to_context_md->m_namespace_maps[to_namespace] =
            namespace_map_iter->second;
    }
  } else {
    to_context_md->m_origins[to] = DeclOrigin(m_source_ctx, from);

    if (log)
      log->Printf("    [ClangASTImporter] Sourced origin "
                  "(Decl*)%p/(ASTContext*)%p into (ASTContext*)%p",
                  static_cast<void *>(from), static_cast<void *>(m_source_ctx),
                  static_cast<void *>(&to->getASTContext()));
  }

  if (TagDecl *from_tag_decl = dyn_cast<TagDecl>(from)) {
    TagDecl *to_tag_decl = dyn_cast<TagDecl>(to);

    to_tag_decl->setHasExternalLexicalStorage();
    to_tag_decl->getPrimaryContext()->setMustBuildLookupTable();

    if (log)
      log->Printf(
          "    [ClangASTImporter] To is a TagDecl - attributes %s%s [%s->%s]",
          (to_tag_decl->hasExternalLexicalStorage() ? " Lexical" : ""),
          (to_tag_decl->hasExternalVisibleStorage() ? " Visible" : ""),
          (from_tag_decl->isCompleteDefinition() ? "complete" : "incomplete"),
          (to_tag_decl->isCompleteDefinition() ? "complete" : "incomplete"));
  }

  if (isa<NamespaceDecl>(from)) {
    NamespaceDecl *to_namespace_decl = dyn_cast<NamespaceDecl>(to);

    m_master.BuildNamespaceMap(to_namespace_decl);

    to_namespace_decl->setHasExternalVisibleStorage();
  }

  if (isa<ObjCContainerDecl>(from)) {
    ObjCContainerDecl *to_container_decl = dyn_cast<ObjCContainerDecl>(to);

    to_container_decl->setHasExternalLexicalStorage();
    to_container_decl->setHasExternalVisibleStorage();

    /*to_interface_decl->setExternallyCompleted();*/

    if (log) {
      if (ObjCInterfaceDecl *to_interface_decl =
              llvm::dyn_cast<ObjCInterfaceDecl>(to_container_decl)) {
        log->Printf(
            "    [ClangASTImporter] To is an ObjCInterfaceDecl - attributes "
            "%s%s%s",
            (to_interface_decl->hasExternalLexicalStorage() ? " Lexical" : ""),
            (to_interface_decl->hasExternalVisibleStorage() ? " Visible" : ""),
            (to_interface_decl->hasDefinition() ? " HasDefinition" : ""));
      } else {
        log->Printf(
            "    [ClangASTImporter] To is an %sDecl - attributes %s%s",
            ((Decl *)to_container_decl)->getDeclKindName(),
            (to_container_decl->hasExternalLexicalStorage() ? " Lexical" : ""),
            (to_container_decl->hasExternalVisibleStorage() ? " Visible" : ""));
      }
    }
  }

  return clang::ASTImporter::Imported(from, to);
}

clang::Decl *ClangASTImporter::Minion::GetOriginalDecl(clang::Decl *To) {
  ASTContextMetadataSP to_context_md =
      m_master.GetContextMetadata(&To->getASTContext());

  if (!to_context_md)
    return nullptr;

  OriginMap::iterator iter = to_context_md->m_origins.find(To);

  if (iter == to_context_md->m_origins.end())
    return nullptr;

  return const_cast<clang::Decl *>(iter->second.decl);
}
