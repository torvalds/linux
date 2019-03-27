//===-- AppleObjCDeclVendor.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AppleObjCDeclVendor.h"

#include "Plugins/ExpressionParser/Clang/ASTDumper.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"

using namespace lldb_private;

class lldb_private::AppleObjCExternalASTSource
    : public ClangExternalASTSourceCommon {
public:
  AppleObjCExternalASTSource(AppleObjCDeclVendor &decl_vendor)
      : m_decl_vendor(decl_vendor) {}

  bool FindExternalVisibleDeclsByName(const clang::DeclContext *decl_ctx,
                                      clang::DeclarationName name) override {
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;

    Log *log(GetLogIfAllCategoriesSet(
        LIBLLDB_LOG_EXPRESSIONS)); // FIXME - a more appropriate log channel?

    if (log) {
      log->Printf("AppleObjCExternalASTSource::FindExternalVisibleDeclsByName[%"
                  "u] on (ASTContext*)%p Looking for %s in (%sDecl*)%p",
                  current_id,
                  static_cast<void *>(&decl_ctx->getParentASTContext()),
                  name.getAsString().c_str(), decl_ctx->getDeclKindName(),
                  static_cast<const void *>(decl_ctx));
    }

    do {
      const clang::ObjCInterfaceDecl *interface_decl =
          llvm::dyn_cast<clang::ObjCInterfaceDecl>(decl_ctx);

      if (!interface_decl)
        break;

      clang::ObjCInterfaceDecl *non_const_interface_decl =
          const_cast<clang::ObjCInterfaceDecl *>(interface_decl);

      if (!m_decl_vendor.FinishDecl(non_const_interface_decl))
        break;

      clang::DeclContext::lookup_result result =
          non_const_interface_decl->lookup(name);

      return (result.size() != 0);
    } while (0);

    SetNoExternalVisibleDeclsForName(decl_ctx, name);
    return false;
  }

  void CompleteType(clang::TagDecl *tag_decl) override {
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;

    Log *log(GetLogIfAllCategoriesSet(
        LIBLLDB_LOG_EXPRESSIONS)); // FIXME - a more appropriate log channel?

    if (log) {
      log->Printf("AppleObjCExternalASTSource::CompleteType[%u] on "
                  "(ASTContext*)%p Completing (TagDecl*)%p named %s",
                  current_id, static_cast<void *>(&tag_decl->getASTContext()),
                  static_cast<void *>(tag_decl),
                  tag_decl->getName().str().c_str());

      log->Printf("  AOEAS::CT[%u] Before:", current_id);
      ASTDumper dumper((clang::Decl *)tag_decl);
      dumper.ToLog(log, "    [CT] ");
    }

    if (log) {
      log->Printf("  AOEAS::CT[%u] After:", current_id);
      ASTDumper dumper((clang::Decl *)tag_decl);
      dumper.ToLog(log, "    [CT] ");
    }
    return;
  }

  void CompleteType(clang::ObjCInterfaceDecl *interface_decl) override {
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;

    Log *log(GetLogIfAllCategoriesSet(
        LIBLLDB_LOG_EXPRESSIONS)); // FIXME - a more appropriate log channel?

    if (log) {
      log->Printf("AppleObjCExternalASTSource::CompleteType[%u] on "
                  "(ASTContext*)%p Completing (ObjCInterfaceDecl*)%p named %s",
                  current_id,
                  static_cast<void *>(&interface_decl->getASTContext()),
                  static_cast<void *>(interface_decl),
                  interface_decl->getName().str().c_str());

      log->Printf("  AOEAS::CT[%u] Before:", current_id);
      ASTDumper dumper((clang::Decl *)interface_decl);
      dumper.ToLog(log, "    [CT] ");
    }

    m_decl_vendor.FinishDecl(interface_decl);

    if (log) {
      log->Printf("  [CT] After:");
      ASTDumper dumper((clang::Decl *)interface_decl);
      dumper.ToLog(log, "    [CT] ");
    }
    return;
  }

  bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &VirtualBaseOffsets) override {
    return false;
  }

  void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
    clang::TranslationUnitDecl *translation_unit_decl =
        m_decl_vendor.m_ast_ctx.getASTContext()->getTranslationUnitDecl();
    translation_unit_decl->setHasExternalVisibleStorage();
    translation_unit_decl->setHasExternalLexicalStorage();
  }

private:
  AppleObjCDeclVendor &m_decl_vendor;
};

AppleObjCDeclVendor::AppleObjCDeclVendor(ObjCLanguageRuntime &runtime)
    : DeclVendor(), m_runtime(runtime), m_ast_ctx(runtime.GetProcess()
                                                      ->GetTarget()
                                                      .GetArchitecture()
                                                      .GetTriple()
                                                      .getTriple()
                                                      .c_str()),
      m_type_realizer_sp(m_runtime.GetEncodingToType()) {
  m_external_source = new AppleObjCExternalASTSource(*this);
  llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> external_source_owning_ptr(
      m_external_source);
  m_ast_ctx.getASTContext()->setExternalSource(external_source_owning_ptr);
}

clang::ObjCInterfaceDecl *
AppleObjCDeclVendor::GetDeclForISA(ObjCLanguageRuntime::ObjCISA isa) {
  ISAToInterfaceMap::const_iterator iter = m_isa_to_interface.find(isa);

  if (iter != m_isa_to_interface.end())
    return iter->second;

  clang::ASTContext *ast_ctx = m_ast_ctx.getASTContext();

  ObjCLanguageRuntime::ClassDescriptorSP descriptor =
      m_runtime.GetClassDescriptorFromISA(isa);

  if (!descriptor)
    return NULL;

  const ConstString &name(descriptor->GetClassName());

  clang::IdentifierInfo &identifier_info =
      ast_ctx->Idents.get(name.GetStringRef());

  clang::ObjCInterfaceDecl *new_iface_decl = clang::ObjCInterfaceDecl::Create(
      *ast_ctx, ast_ctx->getTranslationUnitDecl(), clang::SourceLocation(),
      &identifier_info, nullptr, nullptr);

  ClangASTMetadata meta_data;
  meta_data.SetISAPtr(isa);
  m_external_source->SetMetadata(new_iface_decl, meta_data);

  new_iface_decl->setHasExternalVisibleStorage();
  new_iface_decl->setHasExternalLexicalStorage();

  ast_ctx->getTranslationUnitDecl()->addDecl(new_iface_decl);

  m_isa_to_interface[isa] = new_iface_decl;

  return new_iface_decl;
}

class ObjCRuntimeMethodType {
public:
  ObjCRuntimeMethodType(const char *types) : m_is_valid(false) {
    const char *cursor = types;
    enum ParserState { Start = 0, InType, InPos } state = Start;
    const char *type = NULL;
    int brace_depth = 0;

    uint32_t stepsLeft = 256;

    while (1) {
      if (--stepsLeft == 0) {
        m_is_valid = false;
        return;
      }

      switch (state) {
      case Start: {
        switch (*cursor) {
        default:
          state = InType;
          type = cursor;
          break;
        case '\0':
          m_is_valid = true;
          return;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          m_is_valid = false;
          return;
        }
      } break;
      case InType: {
        switch (*cursor) {
        default:
          ++cursor;
          break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          if (!brace_depth) {
            state = InPos;
            if (type) {
              m_type_vector.push_back(std::string(type, (cursor - type)));
            } else {
              m_is_valid = false;
              return;
            }
            type = NULL;
          } else {
            ++cursor;
          }
          break;
        case '[':
        case '{':
        case '(':
          ++brace_depth;
          ++cursor;
          break;
        case ']':
        case '}':
        case ')':
          if (!brace_depth) {
            m_is_valid = false;
            return;
          }
          --brace_depth;
          ++cursor;
          break;
        case '\0':
          m_is_valid = false;
          return;
        }
      } break;
      case InPos: {
        switch (*cursor) {
        default:
          state = InType;
          type = cursor;
          break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          ++cursor;
          break;
        case '\0':
          m_is_valid = true;
          return;
        }
      } break;
      }
    }
  }

  clang::ObjCMethodDecl *
  BuildMethod(clang::ObjCInterfaceDecl *interface_decl, const char *name,
              bool instance,
              ObjCLanguageRuntime::EncodingToTypeSP type_realizer_sp) {
    if (!m_is_valid || m_type_vector.size() < 3)
      return NULL;

    clang::ASTContext &ast_ctx(interface_decl->getASTContext());

    const bool isInstance = instance;
    const bool isVariadic = false;
    const bool isSynthesized = false;
    const bool isImplicitlyDeclared = true;
    const bool isDefined = false;
    const clang::ObjCMethodDecl::ImplementationControl impControl =
        clang::ObjCMethodDecl::None;
    const bool HasRelatedResultType = false;
    const bool for_expression = true;

    std::vector<clang::IdentifierInfo *> selector_components;

    const char *name_cursor = name;
    bool is_zero_argument = true;

    while (*name_cursor != '\0') {
      const char *colon_loc = strchr(name_cursor, ':');
      if (!colon_loc) {
        selector_components.push_back(
            &ast_ctx.Idents.get(llvm::StringRef(name_cursor)));
        break;
      } else {
        is_zero_argument = false;
        selector_components.push_back(&ast_ctx.Idents.get(
            llvm::StringRef(name_cursor, colon_loc - name_cursor)));
        name_cursor = colon_loc + 1;
      }
    }

    clang::IdentifierInfo **identifier_infos = selector_components.data();
    if (!identifier_infos) {
      return NULL;
    }

    clang::Selector sel = ast_ctx.Selectors.getSelector(
        is_zero_argument ? 0 : selector_components.size(),
        identifier_infos);

    clang::QualType ret_type =
        ClangUtil::GetQualType(type_realizer_sp->RealizeType(
            interface_decl->getASTContext(), m_type_vector[0].c_str(),
            for_expression));

    if (ret_type.isNull())
      return NULL;

    clang::ObjCMethodDecl *ret = clang::ObjCMethodDecl::Create(
        ast_ctx, clang::SourceLocation(), clang::SourceLocation(), sel,
        ret_type, NULL, interface_decl, isInstance, isVariadic, isSynthesized,
        isImplicitlyDeclared, isDefined, impControl, HasRelatedResultType);

    std::vector<clang::ParmVarDecl *> parm_vars;

    for (size_t ai = 3, ae = m_type_vector.size(); ai != ae; ++ai) {
      const bool for_expression = true;
      clang::QualType arg_type =
          ClangUtil::GetQualType(type_realizer_sp->RealizeType(
              ast_ctx, m_type_vector[ai].c_str(), for_expression));

      if (arg_type.isNull())
        return NULL; // well, we just wasted a bunch of time.  Wish we could
                     // delete the stuff we'd just made!

      parm_vars.push_back(clang::ParmVarDecl::Create(
          ast_ctx, ret, clang::SourceLocation(), clang::SourceLocation(), NULL,
          arg_type, NULL, clang::SC_None, NULL));
    }

    ret->setMethodParams(ast_ctx,
                         llvm::ArrayRef<clang::ParmVarDecl *>(parm_vars),
                         llvm::ArrayRef<clang::SourceLocation>());

    return ret;
  }

  explicit operator bool() { return m_is_valid; }

  size_t GetNumTypes() { return m_type_vector.size(); }

  const char *GetTypeAtIndex(size_t idx) { return m_type_vector[idx].c_str(); }

private:
  typedef std::vector<std::string> TypeVector;

  TypeVector m_type_vector;
  bool m_is_valid;
};

bool AppleObjCDeclVendor::FinishDecl(clang::ObjCInterfaceDecl *interface_decl) {
  Log *log(GetLogIfAllCategoriesSet(
      LIBLLDB_LOG_EXPRESSIONS)); // FIXME - a more appropriate log channel?

  ClangASTMetadata *metadata = m_external_source->GetMetadata(interface_decl);
  ObjCLanguageRuntime::ObjCISA objc_isa = 0;
  if (metadata)
    objc_isa = metadata->GetISAPtr();

  if (!objc_isa)
    return false;

  if (!interface_decl->hasExternalVisibleStorage())
    return true;

  interface_decl->startDefinition();

  interface_decl->setHasExternalVisibleStorage(false);
  interface_decl->setHasExternalLexicalStorage(false);

  ObjCLanguageRuntime::ClassDescriptorSP descriptor =
      m_runtime.GetClassDescriptorFromISA(objc_isa);

  if (!descriptor)
    return false;

  auto superclass_func = [interface_decl,
                          this](ObjCLanguageRuntime::ObjCISA isa) {
    clang::ObjCInterfaceDecl *superclass_decl = GetDeclForISA(isa);

    if (!superclass_decl)
      return;

    FinishDecl(superclass_decl);
    clang::ASTContext *context = m_ast_ctx.getASTContext();
    interface_decl->setSuperClass(context->getTrivialTypeSourceInfo(
        context->getObjCInterfaceType(superclass_decl)));
  };

  auto instance_method_func =
      [log, interface_decl, this](const char *name, const char *types) -> bool {
    if (!name || !types)
      return false; // skip this one

    ObjCRuntimeMethodType method_type(types);

    clang::ObjCMethodDecl *method_decl =
        method_type.BuildMethod(interface_decl, name, true, m_type_realizer_sp);

    if (log)
      log->Printf("[  AOTV::FD] Instance method [%s] [%s]", name, types);

    if (method_decl)
      interface_decl->addDecl(method_decl);

    return false;
  };

  auto class_method_func = [log, interface_decl,
                            this](const char *name, const char *types) -> bool {
    if (!name || !types)
      return false; // skip this one

    ObjCRuntimeMethodType method_type(types);

    clang::ObjCMethodDecl *method_decl = method_type.BuildMethod(
        interface_decl, name, false, m_type_realizer_sp);

    if (log)
      log->Printf("[  AOTV::FD] Class method [%s] [%s]", name, types);

    if (method_decl)
      interface_decl->addDecl(method_decl);

    return false;
  };

  auto ivar_func = [log, interface_decl,
                    this](const char *name, const char *type,
                          lldb::addr_t offset_ptr, uint64_t size) -> bool {
    if (!name || !type)
      return false;

    const bool for_expression = false;

    if (log)
      log->Printf(
          "[  AOTV::FD] Instance variable [%s] [%s], offset at %" PRIx64, name,
          type, offset_ptr);

    CompilerType ivar_type = m_runtime.GetEncodingToType()->RealizeType(
        m_ast_ctx, type, for_expression);

    if (ivar_type.IsValid()) {
      clang::TypeSourceInfo *const type_source_info = nullptr;
      const bool is_synthesized = false;
      clang::ObjCIvarDecl *ivar_decl = clang::ObjCIvarDecl::Create(
          *m_ast_ctx.getASTContext(), interface_decl, clang::SourceLocation(),
          clang::SourceLocation(), &m_ast_ctx.getASTContext()->Idents.get(name),
          ClangUtil::GetQualType(ivar_type),
          type_source_info, // TypeSourceInfo *
          clang::ObjCIvarDecl::Public, 0, is_synthesized);

      if (ivar_decl) {
        interface_decl->addDecl(ivar_decl);
      }
    }

    return false;
  };

  if (log) {
    ASTDumper method_dumper((clang::Decl *)interface_decl);

    log->Printf("[AppleObjCDeclVendor::FinishDecl] Finishing Objective-C "
                "interface for %s",
                descriptor->GetClassName().AsCString());
  }

  if (!descriptor->Describe(superclass_func, instance_method_func,
                            class_method_func, ivar_func))
    return false;

  if (log) {
    ASTDumper method_dumper((clang::Decl *)interface_decl);

    log->Printf(
        "[AppleObjCDeclVendor::FinishDecl] Finished Objective-C interface");

    method_dumper.ToLog(log, "  [AOTV::FD] ");
  }

  return true;
}

uint32_t
AppleObjCDeclVendor::FindDecls(const ConstString &name, bool append,
                               uint32_t max_matches,
                               std::vector<clang::NamedDecl *> &decls) {
  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  Log *log(GetLogIfAllCategoriesSet(
      LIBLLDB_LOG_EXPRESSIONS)); // FIXME - a more appropriate log channel?

  if (log)
    log->Printf("AppleObjCDeclVendor::FindDecls [%u] ('%s', %s, %u, )",
                current_id, (const char *)name.AsCString(),
                append ? "true" : "false", max_matches);

  if (!append)
    decls.clear();

  uint32_t ret = 0;

  do {
    // See if the type is already in our ASTContext.

    clang::ASTContext *ast_ctx = m_ast_ctx.getASTContext();

    clang::IdentifierInfo &identifier_info =
        ast_ctx->Idents.get(name.GetStringRef());
    clang::DeclarationName decl_name =
        ast_ctx->DeclarationNames.getIdentifier(&identifier_info);

    clang::DeclContext::lookup_result lookup_result =
        ast_ctx->getTranslationUnitDecl()->lookup(decl_name);

    if (!lookup_result.empty()) {
      if (clang::ObjCInterfaceDecl *result_iface_decl =
              llvm::dyn_cast<clang::ObjCInterfaceDecl>(lookup_result[0])) {
        if (log) {
          clang::QualType result_iface_type =
              ast_ctx->getObjCInterfaceType(result_iface_decl);
          ASTDumper dumper(result_iface_type);

          uint64_t isa_value = LLDB_INVALID_ADDRESS;
          ClangASTMetadata *metadata =
              m_external_source->GetMetadata(result_iface_decl);
          if (metadata)
            isa_value = metadata->GetISAPtr();

          log->Printf("AOCTV::FT [%u] Found %s (isa 0x%" PRIx64
                      ") in the ASTContext",
                      current_id, dumper.GetCString(), isa_value);
        }

        decls.push_back(result_iface_decl);
        ret++;
        break;
      } else {
        if (log)
          log->Printf("AOCTV::FT [%u] There's something in the ASTContext, but "
                      "it's not something we know about",
                      current_id);
        break;
      }
    } else if (log) {
      log->Printf("AOCTV::FT [%u] Couldn't find %s in the ASTContext",
                  current_id, name.AsCString());
    }

    // It's not.  If it exists, we have to put it into our ASTContext.

    ObjCLanguageRuntime::ObjCISA isa = m_runtime.GetISA(name);

    if (!isa) {
      if (log)
        log->Printf("AOCTV::FT [%u] Couldn't find the isa", current_id);

      break;
    }

    clang::ObjCInterfaceDecl *iface_decl = GetDeclForISA(isa);

    if (!iface_decl) {
      if (log)
        log->Printf("AOCTV::FT [%u] Couldn't get the Objective-C interface for "
                    "isa 0x%" PRIx64,
                    current_id, (uint64_t)isa);

      break;
    }

    if (log) {
      clang::QualType new_iface_type =
          ast_ctx->getObjCInterfaceType(iface_decl);
      ASTDumper dumper(new_iface_type);
      log->Printf("AOCTV::FT [%u] Created %s (isa 0x%" PRIx64 ")", current_id,
                  dumper.GetCString(), (uint64_t)isa);
    }

    decls.push_back(iface_decl);
    ret++;
    break;
  } while (0);

  return ret;
}

clang::ExternalASTMerger::ImporterSource
AppleObjCDeclVendor::GetImporterSource() {
        return {*m_ast_ctx.getASTContext(),
                *m_ast_ctx.getFileManager(),
                m_ast_ctx.GetOriginMap()
        };
}
