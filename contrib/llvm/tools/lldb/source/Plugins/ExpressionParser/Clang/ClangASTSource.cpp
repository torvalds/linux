//===-- ClangASTSource.cpp ---------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangASTSource.h"

#include "ASTDumper.h"
#include "ClangModulesDeclVendor.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"

#include <vector>

using namespace clang;
using namespace lldb_private;

//------------------------------------------------------------------
// Scoped class that will remove an active lexical decl from the set when it
// goes out of scope.
//------------------------------------------------------------------
namespace {
class ScopedLexicalDeclEraser {
public:
  ScopedLexicalDeclEraser(std::set<const clang::Decl *> &decls,
                          const clang::Decl *decl)
      : m_active_lexical_decls(decls), m_decl(decl) {}

  ~ScopedLexicalDeclEraser() { m_active_lexical_decls.erase(m_decl); }

private:
  std::set<const clang::Decl *> &m_active_lexical_decls;
  const clang::Decl *m_decl;
};
}

ClangASTSource::ClangASTSource(const lldb::TargetSP &target)
    : m_import_in_progress(false), m_lookups_enabled(false), m_target(target),
      m_ast_context(NULL), m_active_lexical_decls(), m_active_lookups() {
  if (!target->GetUseModernTypeLookup()) {
    m_ast_importer_sp = m_target->GetClangASTImporter();
  }
}

void ClangASTSource::InstallASTContext(clang::ASTContext &ast_context,
                                       clang::FileManager &file_manager,
                                       bool is_shared_context) {
  m_ast_context = &ast_context;
  m_file_manager = &file_manager;
  if (m_target->GetUseModernTypeLookup()) {
    // Configure the ExternalASTMerger.  The merger needs to be able to import
    // types from any source that we would do lookups in, which includes the
    // persistent AST context as well as the modules and Objective-C runtime
    // AST contexts.

    lldbassert(!m_merger_up);
    clang::ExternalASTMerger::ImporterTarget target = {ast_context,
                                                       file_manager};
    std::vector<clang::ExternalASTMerger::ImporterSource> sources;
    for (lldb::ModuleSP module_sp : m_target->GetImages().Modules()) {
      if (auto *module_ast_ctx = llvm::cast_or_null<ClangASTContext>(
              module_sp->GetTypeSystemForLanguage(lldb::eLanguageTypeC))) {
        lldbassert(module_ast_ctx->getASTContext());
        lldbassert(module_ast_ctx->getFileManager());
        sources.push_back({*module_ast_ctx->getASTContext(),
                           *module_ast_ctx->getFileManager(),
                           module_ast_ctx->GetOriginMap()
        });
      }
    }

    do {
      lldb::ProcessSP process(m_target->GetProcessSP());

      if (!process)
        break;

      ObjCLanguageRuntime *language_runtime(process->GetObjCLanguageRuntime());

      if (!language_runtime)
        break;

      DeclVendor *runtime_decl_vendor = language_runtime->GetDeclVendor();

      if (!runtime_decl_vendor)
        break;

      sources.push_back(runtime_decl_vendor->GetImporterSource());
    } while (0);

    do {
      DeclVendor *modules_decl_vendor =
          m_target->GetClangModulesDeclVendor();

      if (!modules_decl_vendor)
        break;

      sources.push_back(modules_decl_vendor->GetImporterSource());
    } while (0);

    if (!is_shared_context) {
      // Update the scratch AST context's merger to reflect any new sources we
      // might have come across since the last time an expression was parsed.

      auto scratch_ast_context = static_cast<ClangASTContextForExpressions*>(
          m_target->GetScratchClangASTContext());

      scratch_ast_context->GetMergerUnchecked().AddSources(sources);

      sources.push_back({*scratch_ast_context->getASTContext(),
                         *scratch_ast_context->getFileManager(),
                         scratch_ast_context->GetOriginMap()});
    } while (0);

    m_merger_up =
        llvm::make_unique<clang::ExternalASTMerger>(target, sources);
  } else {
    m_ast_importer_sp->InstallMapCompleter(&ast_context, *this);
  }
}

ClangASTSource::~ClangASTSource() {
  if (m_ast_importer_sp)
    m_ast_importer_sp->ForgetDestination(m_ast_context);

  // We are in the process of destruction, don't create clang ast context on
  // demand by passing false to
  // Target::GetScratchClangASTContext(create_on_demand).
  ClangASTContext *scratch_clang_ast_context =
      m_target->GetScratchClangASTContext(false);

  if (!scratch_clang_ast_context)
    return;

  clang::ASTContext *scratch_ast_context =
      scratch_clang_ast_context->getASTContext();

  if (!scratch_ast_context)
    return;

  if (m_ast_context != scratch_ast_context && m_ast_importer_sp)
    m_ast_importer_sp->ForgetSource(scratch_ast_context, m_ast_context);
}

void ClangASTSource::StartTranslationUnit(ASTConsumer *Consumer) {
  if (!m_ast_context)
    return;

  m_ast_context->getTranslationUnitDecl()->setHasExternalVisibleStorage();
  m_ast_context->getTranslationUnitDecl()->setHasExternalLexicalStorage();
}

// The core lookup interface.
bool ClangASTSource::FindExternalVisibleDeclsByName(
    const DeclContext *decl_ctx, DeclarationName clang_decl_name) {
  if (!m_ast_context) {
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }

  if (GetImportInProgress()) {
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }

  std::string decl_name(clang_decl_name.getAsString());

  //    if (m_decl_map.DoingASTImport ())
  //      return DeclContext::lookup_result();
  //
  switch (clang_decl_name.getNameKind()) {
  // Normal identifiers.
  case DeclarationName::Identifier: {
    clang::IdentifierInfo *identifier_info =
        clang_decl_name.getAsIdentifierInfo();

    if (!identifier_info || identifier_info->getBuiltinID() != 0) {
      SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
      return false;
    }
  } break;

  // Operator names.
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
    break;

  // Using directives found in this context.
  // Tell Sema we didn't find any or we'll end up getting asked a *lot*.
  case DeclarationName::CXXUsingDirective:
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector: {
    llvm::SmallVector<NamedDecl *, 1> method_decls;

    NameSearchContext method_search_context(*this, method_decls,
                                            clang_decl_name, decl_ctx);

    FindObjCMethodDecls(method_search_context);

    SetExternalVisibleDeclsForName(decl_ctx, clang_decl_name, method_decls);
    return (method_decls.size() > 0);
  }
  // These aren't possible in the global context.
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXDeductionGuideName:
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }

  if (!GetLookupsEnabled()) {
    // Wait until we see a '$' at the start of a name before we start doing any
    // lookups so we can avoid lookup up all of the builtin types.
    if (!decl_name.empty() && decl_name[0] == '$') {
      SetLookupsEnabled(true);
    } else {
      SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
      return false;
    }
  }

  ConstString const_decl_name(decl_name.c_str());

  const char *uniqued_const_decl_name = const_decl_name.GetCString();
  if (m_active_lookups.find(uniqued_const_decl_name) !=
      m_active_lookups.end()) {
    // We are currently looking up this name...
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }
  m_active_lookups.insert(uniqued_const_decl_name);
  //  static uint32_t g_depth = 0;
  //  ++g_depth;
  //  printf("[%5u] FindExternalVisibleDeclsByName() \"%s\"\n", g_depth,
  //  uniqued_const_decl_name);
  llvm::SmallVector<NamedDecl *, 4> name_decls;
  NameSearchContext name_search_context(*this, name_decls, clang_decl_name,
                                        decl_ctx);
  FindExternalVisibleDecls(name_search_context);
  SetExternalVisibleDeclsForName(decl_ctx, clang_decl_name, name_decls);
  //  --g_depth;
  m_active_lookups.erase(uniqued_const_decl_name);
  return (name_decls.size() != 0);
}

void ClangASTSource::CompleteType(TagDecl *tag_decl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  if (log) {
    log->Printf("    CompleteTagDecl[%u] on (ASTContext*)%p Completing "
                "(TagDecl*)%p named %s",
                current_id, static_cast<void *>(m_ast_context),
                static_cast<void *>(tag_decl),
                tag_decl->getName().str().c_str());

    log->Printf("      CTD[%u] Before:", current_id);
    ASTDumper dumper((Decl *)tag_decl);
    dumper.ToLog(log, "      [CTD] ");
  }

  auto iter = m_active_lexical_decls.find(tag_decl);
  if (iter != m_active_lexical_decls.end())
    return;
  m_active_lexical_decls.insert(tag_decl);
  ScopedLexicalDeclEraser eraser(m_active_lexical_decls, tag_decl);

  if (!m_ast_importer_sp) {
    if (HasMerger()) {
      GetMergerUnchecked().CompleteType(tag_decl);
    }
    return;
  }

  if (!m_ast_importer_sp->CompleteTagDecl(tag_decl)) {
    // We couldn't complete the type.  Maybe there's a definition somewhere
    // else that can be completed.

    if (log)
      log->Printf("      CTD[%u] Type could not be completed in the module in "
                  "which it was first found.",
                  current_id);

    bool found = false;

    DeclContext *decl_ctx = tag_decl->getDeclContext();

    if (const NamespaceDecl *namespace_context =
            dyn_cast<NamespaceDecl>(decl_ctx)) {
      ClangASTImporter::NamespaceMapSP namespace_map =
          m_ast_importer_sp->GetNamespaceMap(namespace_context);

      if (log && log->GetVerbose())
        log->Printf("      CTD[%u] Inspecting namespace map %p (%d entries)",
                    current_id, static_cast<void *>(namespace_map.get()),
                    static_cast<int>(namespace_map->size()));

      if (!namespace_map)
        return;

      for (ClangASTImporter::NamespaceMap::iterator i = namespace_map->begin(),
                                                    e = namespace_map->end();
           i != e && !found; ++i) {
        if (log)
          log->Printf("      CTD[%u] Searching namespace %s in module %s",
                      current_id, i->second.GetName().AsCString(),
                      i->first->GetFileSpec().GetFilename().GetCString());

        TypeList types;

        ConstString name(tag_decl->getName().str().c_str());

        i->first->FindTypesInNamespace(name, &i->second, UINT32_MAX, types);

        for (uint32_t ti = 0, te = types.GetSize(); ti != te && !found; ++ti) {
          lldb::TypeSP type = types.GetTypeAtIndex(ti);

          if (!type)
            continue;

          CompilerType clang_type(type->GetFullCompilerType());

          if (!ClangUtil::IsClangType(clang_type))
            continue;

          const TagType *tag_type =
              ClangUtil::GetQualType(clang_type)->getAs<TagType>();

          if (!tag_type)
            continue;

          TagDecl *candidate_tag_decl =
              const_cast<TagDecl *>(tag_type->getDecl());

          if (m_ast_importer_sp->CompleteTagDeclWithOrigin(tag_decl,
                                                           candidate_tag_decl))
            found = true;
        }
      }
    } else {
      TypeList types;

      ConstString name(tag_decl->getName().str().c_str());
      CompilerDeclContext namespace_decl;

      const ModuleList &module_list = m_target->GetImages();

      bool exact_match = false;
      llvm::DenseSet<SymbolFile *> searched_symbol_files;
      module_list.FindTypes(nullptr, name, exact_match, UINT32_MAX,
                            searched_symbol_files, types);

      for (uint32_t ti = 0, te = types.GetSize(); ti != te && !found; ++ti) {
        lldb::TypeSP type = types.GetTypeAtIndex(ti);

        if (!type)
          continue;

        CompilerType clang_type(type->GetFullCompilerType());

        if (!ClangUtil::IsClangType(clang_type))
          continue;

        const TagType *tag_type =
            ClangUtil::GetQualType(clang_type)->getAs<TagType>();

        if (!tag_type)
          continue;

        TagDecl *candidate_tag_decl =
            const_cast<TagDecl *>(tag_type->getDecl());

        // We have found a type by basename and we need to make sure the decl
        // contexts are the same before we can try to complete this type with
        // another
        if (!ClangASTContext::DeclsAreEquivalent(tag_decl, candidate_tag_decl))
          continue;

        if (m_ast_importer_sp->CompleteTagDeclWithOrigin(tag_decl,
                                                         candidate_tag_decl))
          found = true;
      }
    }
  }

  if (log) {
    log->Printf("      [CTD] After:");
    ASTDumper dumper((Decl *)tag_decl);
    dumper.ToLog(log, "      [CTD] ");
  }
}

void ClangASTSource::CompleteType(clang::ObjCInterfaceDecl *interface_decl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log) {
    log->Printf("    [CompleteObjCInterfaceDecl] on (ASTContext*)%p Completing "
                "an ObjCInterfaceDecl named %s",
                static_cast<void *>(m_ast_context),
                interface_decl->getName().str().c_str());
    log->Printf("      [COID] Before:");
    ASTDumper dumper((Decl *)interface_decl);
    dumper.ToLog(log, "      [COID] ");
  }

  if (!m_ast_importer_sp) {
    if (HasMerger()) {
      ObjCInterfaceDecl *complete_iface_decl =
        GetCompleteObjCInterface(interface_decl);

      if (complete_iface_decl && (complete_iface_decl != interface_decl)) {
        m_merger_up->ForceRecordOrigin(interface_decl, {complete_iface_decl, &complete_iface_decl->getASTContext()});
      }

      GetMergerUnchecked().CompleteType(interface_decl);
    } else {
      lldbassert(0 && "No mechanism for completing a type!");
    }
    return;
  }

  Decl *original_decl = NULL;
  ASTContext *original_ctx = NULL;

  if (m_ast_importer_sp->ResolveDeclOrigin(interface_decl, &original_decl,
                                           &original_ctx)) {
    if (ObjCInterfaceDecl *original_iface_decl =
            dyn_cast<ObjCInterfaceDecl>(original_decl)) {
      ObjCInterfaceDecl *complete_iface_decl =
          GetCompleteObjCInterface(original_iface_decl);

      if (complete_iface_decl && (complete_iface_decl != original_iface_decl)) {
        m_ast_importer_sp->SetDeclOrigin(interface_decl, complete_iface_decl);
      }
    }
  }

  m_ast_importer_sp->CompleteObjCInterfaceDecl(interface_decl);

  if (interface_decl->getSuperClass() &&
      interface_decl->getSuperClass() != interface_decl)
    CompleteType(interface_decl->getSuperClass());

  if (log) {
    log->Printf("      [COID] After:");
    ASTDumper dumper((Decl *)interface_decl);
    dumper.ToLog(log, "      [COID] ");
  }
}

clang::ObjCInterfaceDecl *ClangASTSource::GetCompleteObjCInterface(
    const clang::ObjCInterfaceDecl *interface_decl) {
  lldb::ProcessSP process(m_target->GetProcessSP());

  if (!process)
    return NULL;

  ObjCLanguageRuntime *language_runtime(process->GetObjCLanguageRuntime());

  if (!language_runtime)
    return NULL;

  ConstString class_name(interface_decl->getNameAsString().c_str());

  lldb::TypeSP complete_type_sp(
      language_runtime->LookupInCompleteClassCache(class_name));

  if (!complete_type_sp)
    return NULL;

  TypeFromUser complete_type =
      TypeFromUser(complete_type_sp->GetFullCompilerType());
  lldb::opaque_compiler_type_t complete_opaque_type =
      complete_type.GetOpaqueQualType();

  if (!complete_opaque_type)
    return NULL;

  const clang::Type *complete_clang_type =
      QualType::getFromOpaquePtr(complete_opaque_type).getTypePtr();
  const ObjCInterfaceType *complete_interface_type =
      dyn_cast<ObjCInterfaceType>(complete_clang_type);

  if (!complete_interface_type)
    return NULL;

  ObjCInterfaceDecl *complete_iface_decl(complete_interface_type->getDecl());

  return complete_iface_decl;
}

void ClangASTSource::FindExternalLexicalDecls(
    const DeclContext *decl_context,
    llvm::function_ref<bool(Decl::Kind)> predicate,
    llvm::SmallVectorImpl<Decl *> &decls) {

  if (HasMerger()) {
    if (auto *interface_decl = dyn_cast<ObjCInterfaceDecl>(decl_context)) {
      ObjCInterfaceDecl *complete_iface_decl =
         GetCompleteObjCInterface(interface_decl);

      if (complete_iface_decl && (complete_iface_decl != interface_decl)) {
        m_merger_up->ForceRecordOrigin(interface_decl, {complete_iface_decl, &complete_iface_decl->getASTContext()});
      }
    }
    return GetMergerUnchecked().FindExternalLexicalDecls(decl_context,
                                                         predicate,
                                                         decls);
  } else if (!m_ast_importer_sp)
    return;

  ClangASTMetrics::RegisterLexicalQuery();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  const Decl *context_decl = dyn_cast<Decl>(decl_context);

  if (!context_decl)
    return;

  auto iter = m_active_lexical_decls.find(context_decl);
  if (iter != m_active_lexical_decls.end())
    return;
  m_active_lexical_decls.insert(context_decl);
  ScopedLexicalDeclEraser eraser(m_active_lexical_decls, context_decl);

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  if (log) {
    if (const NamedDecl *context_named_decl = dyn_cast<NamedDecl>(context_decl))
      log->Printf(
          "FindExternalLexicalDecls[%u] on (ASTContext*)%p in '%s' (%sDecl*)%p",
          current_id, static_cast<void *>(m_ast_context),
          context_named_decl->getNameAsString().c_str(),
          context_decl->getDeclKindName(),
          static_cast<const void *>(context_decl));
    else if (context_decl)
      log->Printf(
          "FindExternalLexicalDecls[%u] on (ASTContext*)%p in (%sDecl*)%p",
          current_id, static_cast<void *>(m_ast_context),
          context_decl->getDeclKindName(),
          static_cast<const void *>(context_decl));
    else
      log->Printf(
          "FindExternalLexicalDecls[%u] on (ASTContext*)%p in a NULL context",
          current_id, static_cast<const void *>(m_ast_context));
  }

  Decl *original_decl = NULL;
  ASTContext *original_ctx = NULL;

  if (!m_ast_importer_sp->ResolveDeclOrigin(context_decl, &original_decl,
                                            &original_ctx))
    return;

  if (log) {
    log->Printf("  FELD[%u] Original decl (ASTContext*)%p (Decl*)%p:",
                current_id, static_cast<void *>(original_ctx),
                static_cast<void *>(original_decl));
    ASTDumper(original_decl).ToLog(log, "    ");
  }

  if (ObjCInterfaceDecl *original_iface_decl =
          dyn_cast<ObjCInterfaceDecl>(original_decl)) {
    ObjCInterfaceDecl *complete_iface_decl =
        GetCompleteObjCInterface(original_iface_decl);

    if (complete_iface_decl && (complete_iface_decl != original_iface_decl)) {
      original_decl = complete_iface_decl;
      original_ctx = &complete_iface_decl->getASTContext();

      m_ast_importer_sp->SetDeclOrigin(context_decl, complete_iface_decl);
    }
  }

  if (TagDecl *original_tag_decl = dyn_cast<TagDecl>(original_decl)) {
    ExternalASTSource *external_source = original_ctx->getExternalSource();

    if (external_source)
      external_source->CompleteType(original_tag_decl);
  }

  const DeclContext *original_decl_context =
      dyn_cast<DeclContext>(original_decl);

  if (!original_decl_context)
    return;

  for (TagDecl::decl_iterator iter = original_decl_context->decls_begin();
       iter != original_decl_context->decls_end(); ++iter) {
    Decl *decl = *iter;

    if (predicate(decl->getKind())) {
      if (log) {
        ASTDumper ast_dumper(decl);
        if (const NamedDecl *context_named_decl =
                dyn_cast<NamedDecl>(context_decl))
          log->Printf("  FELD[%d] Adding [to %sDecl %s] lexical %sDecl %s",
                      current_id, context_named_decl->getDeclKindName(),
                      context_named_decl->getNameAsString().c_str(),
                      decl->getDeclKindName(), ast_dumper.GetCString());
        else
          log->Printf("  FELD[%d] Adding lexical %sDecl %s", current_id,
                      decl->getDeclKindName(), ast_dumper.GetCString());
      }

      Decl *copied_decl = CopyDecl(decl);

      if (!copied_decl)
        continue;

      if (FieldDecl *copied_field = dyn_cast<FieldDecl>(copied_decl)) {
        QualType copied_field_type = copied_field->getType();

        m_ast_importer_sp->RequireCompleteType(copied_field_type);
      }

      DeclContext *decl_context_non_const =
          const_cast<DeclContext *>(decl_context);

      if (copied_decl->getDeclContext() != decl_context) {
        if (copied_decl->getDeclContext()->containsDecl(copied_decl))
          copied_decl->getDeclContext()->removeDecl(copied_decl);
        copied_decl->setDeclContext(decl_context_non_const);
      }

      if (!decl_context_non_const->containsDecl(copied_decl))
        decl_context_non_const->addDeclInternal(copied_decl);
    }
  }

  return;
}

void ClangASTSource::FindExternalVisibleDecls(NameSearchContext &context) {
  assert(m_ast_context);

  ClangASTMetrics::RegisterVisibleQuery();

  const ConstString name(context.m_decl_name.getAsString().c_str());

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  if (log) {
    if (!context.m_decl_context)
      log->Printf("ClangASTSource::FindExternalVisibleDecls[%u] on "
                  "(ASTContext*)%p for '%s' in a NULL DeclContext",
                  current_id, static_cast<void *>(m_ast_context),
                  name.GetCString());
    else if (const NamedDecl *context_named_decl =
                 dyn_cast<NamedDecl>(context.m_decl_context))
      log->Printf("ClangASTSource::FindExternalVisibleDecls[%u] on "
                  "(ASTContext*)%p for '%s' in '%s'",
                  current_id, static_cast<void *>(m_ast_context),
                  name.GetCString(),
                  context_named_decl->getNameAsString().c_str());
    else
      log->Printf("ClangASTSource::FindExternalVisibleDecls[%u] on "
                  "(ASTContext*)%p for '%s' in a '%s'",
                  current_id, static_cast<void *>(m_ast_context),
                  name.GetCString(), context.m_decl_context->getDeclKindName());
  }

  if (HasMerger() && !isa<TranslationUnitDecl>(context.m_decl_context)
      /* possibly handle NamespaceDecls here? */) {
    if (auto *interface_decl =
    dyn_cast<ObjCInterfaceDecl>(context.m_decl_context)) {
      ObjCInterfaceDecl *complete_iface_decl =
      GetCompleteObjCInterface(interface_decl);

      if (complete_iface_decl && (complete_iface_decl != interface_decl)) {
        GetMergerUnchecked().ForceRecordOrigin(
            interface_decl,
            {complete_iface_decl, &complete_iface_decl->getASTContext()});
      }
    }

    GetMergerUnchecked().FindExternalVisibleDeclsByName(context.m_decl_context,
                                                context.m_decl_name);
    return; // otherwise we may need to fall back
  }

  context.m_namespace_map.reset(new ClangASTImporter::NamespaceMap);

  if (const NamespaceDecl *namespace_context =
          dyn_cast<NamespaceDecl>(context.m_decl_context)) {
    ClangASTImporter::NamespaceMapSP namespace_map =  m_ast_importer_sp ?
        m_ast_importer_sp->GetNamespaceMap(namespace_context) : nullptr;

    if (log && log->GetVerbose())
      log->Printf("  CAS::FEVD[%u] Inspecting namespace map %p (%d entries)",
                  current_id, static_cast<void *>(namespace_map.get()),
                  static_cast<int>(namespace_map->size()));

    if (!namespace_map)
      return;

    for (ClangASTImporter::NamespaceMap::iterator i = namespace_map->begin(),
                                                  e = namespace_map->end();
         i != e; ++i) {
      if (log)
        log->Printf("  CAS::FEVD[%u] Searching namespace %s in module %s",
                    current_id, i->second.GetName().AsCString(),
                    i->first->GetFileSpec().GetFilename().GetCString());

      FindExternalVisibleDecls(context, i->first, i->second, current_id);
    }
  } else if (isa<ObjCInterfaceDecl>(context.m_decl_context) && !HasMerger()) {
    FindObjCPropertyAndIvarDecls(context);
  } else if (!isa<TranslationUnitDecl>(context.m_decl_context)) {
    // we shouldn't be getting FindExternalVisibleDecls calls for these
    return;
  } else {
    CompilerDeclContext namespace_decl;

    if (log)
      log->Printf("  CAS::FEVD[%u] Searching the root namespace", current_id);

    FindExternalVisibleDecls(context, lldb::ModuleSP(), namespace_decl,
                             current_id);
  }

  if (!context.m_namespace_map->empty()) {
    if (log && log->GetVerbose())
      log->Printf("  CAS::FEVD[%u] Registering namespace map %p (%d entries)",
                  current_id,
                  static_cast<void *>(context.m_namespace_map.get()),
                  static_cast<int>(context.m_namespace_map->size()));

    NamespaceDecl *clang_namespace_decl =
        AddNamespace(context, context.m_namespace_map);

    if (clang_namespace_decl)
      clang_namespace_decl->setHasExternalVisibleStorage();
  }
}

bool ClangASTSource::IgnoreName(const ConstString name,
                                bool ignore_all_dollar_names) {
  static const ConstString id_name("id");
  static const ConstString Class_name("Class");

  if (m_ast_context->getLangOpts().ObjC)
    if (name == id_name || name == Class_name)
      return true;

  StringRef name_string_ref = name.GetStringRef();

  // The ClangASTSource is not responsible for finding $-names.
  return name_string_ref.empty() ||
         (ignore_all_dollar_names && name_string_ref.startswith("$")) ||
         name_string_ref.startswith("_$");
}

void ClangASTSource::FindExternalVisibleDecls(
    NameSearchContext &context, lldb::ModuleSP module_sp,
    CompilerDeclContext &namespace_decl, unsigned int current_id) {
  assert(m_ast_context);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  SymbolContextList sc_list;

  const ConstString name(context.m_decl_name.getAsString().c_str());
  if (IgnoreName(name, true))
    return;

  if (module_sp && namespace_decl) {
    CompilerDeclContext found_namespace_decl;

    SymbolVendor *symbol_vendor = module_sp->GetSymbolVendor();

    if (symbol_vendor) {
      found_namespace_decl =
          symbol_vendor->FindNamespace(name, &namespace_decl);

      if (found_namespace_decl) {
        context.m_namespace_map->push_back(
            std::pair<lldb::ModuleSP, CompilerDeclContext>(
                module_sp, found_namespace_decl));

        if (log)
          log->Printf("  CAS::FEVD[%u] Found namespace %s in module %s",
                      current_id, name.GetCString(),
                      module_sp->GetFileSpec().GetFilename().GetCString());
      }
    }
  } else if (!HasMerger()) {
    const ModuleList &target_images = m_target->GetImages();
    std::lock_guard<std::recursive_mutex> guard(target_images.GetMutex());

    for (size_t i = 0, e = target_images.GetSize(); i < e; ++i) {
      lldb::ModuleSP image = target_images.GetModuleAtIndexUnlocked(i);

      if (!image)
        continue;

      CompilerDeclContext found_namespace_decl;

      SymbolVendor *symbol_vendor = image->GetSymbolVendor();

      if (!symbol_vendor)
        continue;

      found_namespace_decl =
          symbol_vendor->FindNamespace(name, &namespace_decl);

      if (found_namespace_decl) {
        context.m_namespace_map->push_back(
            std::pair<lldb::ModuleSP, CompilerDeclContext>(
                image, found_namespace_decl));

        if (log)
          log->Printf("  CAS::FEVD[%u] Found namespace %s in module %s",
                      current_id, name.GetCString(),
                      image->GetFileSpec().GetFilename().GetCString());
      }
    }
  }

  do {
    if (context.m_found.type)
      break;

    TypeList types;
    const bool exact_match = true;
    llvm::DenseSet<lldb_private::SymbolFile *> searched_symbol_files;
    if (module_sp && namespace_decl)
      module_sp->FindTypesInNamespace(name, &namespace_decl, 1, types);
    else {
      m_target->GetImages().FindTypes(module_sp.get(), name, exact_match, 1,
                                      searched_symbol_files, types);
    }

    if (size_t num_types = types.GetSize()) {
      for (size_t ti = 0; ti < num_types; ++ti) {
        lldb::TypeSP type_sp = types.GetTypeAtIndex(ti);

        if (log) {
          const char *name_string = type_sp->GetName().GetCString();

          log->Printf("  CAS::FEVD[%u] Matching type found for \"%s\": %s",
                      current_id, name.GetCString(),
                      (name_string ? name_string : "<anonymous>"));
        }

        CompilerType full_type = type_sp->GetFullCompilerType();

        CompilerType copied_clang_type(GuardedCopyType(full_type));

        if (!copied_clang_type) {
          if (log)
            log->Printf("  CAS::FEVD[%u] - Couldn't export a type", current_id);

          continue;
        }

        context.AddTypeDecl(copied_clang_type);

        context.m_found.type = true;
        break;
      }
    }

    if (!context.m_found.type) {
      // Try the modules next.

      do {
        if (ClangModulesDeclVendor *modules_decl_vendor =
                m_target->GetClangModulesDeclVendor()) {
          bool append = false;
          uint32_t max_matches = 1;
          std::vector<clang::NamedDecl *> decls;

          if (!modules_decl_vendor->FindDecls(name, append, max_matches, decls))
            break;

          if (log) {
            log->Printf("  CAS::FEVD[%u] Matching entity found for \"%s\" in "
                        "the modules",
                        current_id, name.GetCString());
          }

          clang::NamedDecl *const decl_from_modules = decls[0];

          if (llvm::isa<clang::TypeDecl>(decl_from_modules) ||
              llvm::isa<clang::ObjCContainerDecl>(decl_from_modules) ||
              llvm::isa<clang::EnumConstantDecl>(decl_from_modules)) {
            clang::Decl *copied_decl = CopyDecl(decl_from_modules);
            clang::NamedDecl *copied_named_decl =
                copied_decl ? dyn_cast<clang::NamedDecl>(copied_decl) : nullptr;

            if (!copied_named_decl) {
              if (log)
                log->Printf(
                    "  CAS::FEVD[%u] - Couldn't export a type from the modules",
                    current_id);

              break;
            }

            context.AddNamedDecl(copied_named_decl);

            context.m_found.type = true;
          }
        }
      } while (0);
    }

    if (!context.m_found.type) {
      do {
        // Couldn't find any types elsewhere.  Try the Objective-C runtime if
        // one exists.

        lldb::ProcessSP process(m_target->GetProcessSP());

        if (!process)
          break;

        ObjCLanguageRuntime *language_runtime(
            process->GetObjCLanguageRuntime());

        if (!language_runtime)
          break;

        DeclVendor *decl_vendor = language_runtime->GetDeclVendor();

        if (!decl_vendor)
          break;

        bool append = false;
        uint32_t max_matches = 1;
        std::vector<clang::NamedDecl *> decls;

        if (!decl_vendor->FindDecls(name, append, max_matches, decls))
          break;

        if (log) {
          log->Printf(
              "  CAS::FEVD[%u] Matching type found for \"%s\" in the runtime",
              current_id, name.GetCString());
        }

        clang::Decl *copied_decl = CopyDecl(decls[0]);
        clang::NamedDecl *copied_named_decl =
            copied_decl ? dyn_cast<clang::NamedDecl>(copied_decl) : nullptr;

        if (!copied_named_decl) {
          if (log)
            log->Printf(
                "  CAS::FEVD[%u] - Couldn't export a type from the runtime",
                current_id);

          break;
        }

        context.AddNamedDecl(copied_named_decl);
      } while (0);
    }

  } while (0);
}

template <class D> class TaggedASTDecl {
public:
  TaggedASTDecl() : decl(NULL) {}
  TaggedASTDecl(D *_decl) : decl(_decl) {}
  bool IsValid() const { return (decl != NULL); }
  bool IsInvalid() const { return !IsValid(); }
  D *operator->() const { return decl; }
  D *decl;
};

template <class D2, template <class D> class TD, class D1>
TD<D2> DynCast(TD<D1> source) {
  return TD<D2>(dyn_cast<D2>(source.decl));
}

template <class D = Decl> class DeclFromParser;
template <class D = Decl> class DeclFromUser;

template <class D> class DeclFromParser : public TaggedASTDecl<D> {
public:
  DeclFromParser() : TaggedASTDecl<D>() {}
  DeclFromParser(D *_decl) : TaggedASTDecl<D>(_decl) {}

  DeclFromUser<D> GetOrigin(ClangASTSource &source);
};

template <class D> class DeclFromUser : public TaggedASTDecl<D> {
public:
  DeclFromUser() : TaggedASTDecl<D>() {}
  DeclFromUser(D *_decl) : TaggedASTDecl<D>(_decl) {}

  DeclFromParser<D> Import(ClangASTSource &source);
};

template <class D>
DeclFromUser<D> DeclFromParser<D>::GetOrigin(ClangASTSource &source) {
  DeclFromUser<> origin_decl;
  source.ResolveDeclOrigin(this->decl, &origin_decl.decl, NULL);
  if (origin_decl.IsInvalid())
    return DeclFromUser<D>();
  return DeclFromUser<D>(dyn_cast<D>(origin_decl.decl));
}

template <class D>
DeclFromParser<D> DeclFromUser<D>::Import(ClangASTSource &source) {
  DeclFromParser<> parser_generic_decl(source.CopyDecl(this->decl));
  if (parser_generic_decl.IsInvalid())
    return DeclFromParser<D>();
  return DeclFromParser<D>(dyn_cast<D>(parser_generic_decl.decl));
}

bool ClangASTSource::FindObjCMethodDeclsWithOrigin(
    unsigned int current_id, NameSearchContext &context,
    ObjCInterfaceDecl *original_interface_decl, const char *log_info) {
  const DeclarationName &decl_name(context.m_decl_name);
  clang::ASTContext *original_ctx = &original_interface_decl->getASTContext();

  Selector original_selector;

  if (decl_name.isObjCZeroArgSelector()) {
    IdentifierInfo *ident = &original_ctx->Idents.get(decl_name.getAsString());
    original_selector = original_ctx->Selectors.getSelector(0, &ident);
  } else if (decl_name.isObjCOneArgSelector()) {
    const std::string &decl_name_string = decl_name.getAsString();
    std::string decl_name_string_without_colon(decl_name_string.c_str(),
                                               decl_name_string.length() - 1);
    IdentifierInfo *ident =
        &original_ctx->Idents.get(decl_name_string_without_colon);
    original_selector = original_ctx->Selectors.getSelector(1, &ident);
  } else {
    SmallVector<IdentifierInfo *, 4> idents;

    clang::Selector sel = decl_name.getObjCSelector();

    unsigned num_args = sel.getNumArgs();

    for (unsigned i = 0; i != num_args; ++i) {
      idents.push_back(&original_ctx->Idents.get(sel.getNameForSlot(i)));
    }

    original_selector =
        original_ctx->Selectors.getSelector(num_args, idents.data());
  }

  DeclarationName original_decl_name(original_selector);

  llvm::SmallVector<NamedDecl *, 1> methods;

  ClangASTContext::GetCompleteDecl(original_ctx, original_interface_decl);

  if (ObjCMethodDecl *instance_method_decl =
          original_interface_decl->lookupInstanceMethod(original_selector)) {
    methods.push_back(instance_method_decl);
  } else if (ObjCMethodDecl *class_method_decl =
                 original_interface_decl->lookupClassMethod(
                     original_selector)) {
    methods.push_back(class_method_decl);
  }

  if (methods.empty()) {
    return false;
  }

  for (NamedDecl *named_decl : methods) {
    if (!named_decl)
      continue;

    ObjCMethodDecl *result_method = dyn_cast<ObjCMethodDecl>(named_decl);

    if (!result_method)
      continue;

    Decl *copied_decl = CopyDecl(result_method);

    if (!copied_decl)
      continue;

    ObjCMethodDecl *copied_method_decl = dyn_cast<ObjCMethodDecl>(copied_decl);

    if (!copied_method_decl)
      continue;

    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    if (log) {
      ASTDumper dumper((Decl *)copied_method_decl);
      log->Printf("  CAS::FOMD[%d] found (%s) %s", current_id, log_info,
                  dumper.GetCString());
    }

    context.AddNamedDecl(copied_method_decl);
  }

  return true;
}

void ClangASTSource::FindObjCMethodDecls(NameSearchContext &context) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (HasMerger()) {
    if (auto *interface_decl = dyn_cast<ObjCInterfaceDecl>(context.m_decl_context)) {
      ObjCInterfaceDecl *complete_iface_decl =
          GetCompleteObjCInterface(interface_decl);

      if (complete_iface_decl && (complete_iface_decl != context.m_decl_context)) {
        m_merger_up->ForceRecordOrigin(interface_decl, {complete_iface_decl, &complete_iface_decl->getASTContext()});
      }
    }

    GetMergerUnchecked().FindExternalVisibleDeclsByName(context.m_decl_context,
                                                        context.m_decl_name);
    return;
  }

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  const DeclarationName &decl_name(context.m_decl_name);
  const DeclContext *decl_ctx(context.m_decl_context);

  const ObjCInterfaceDecl *interface_decl =
      dyn_cast<ObjCInterfaceDecl>(decl_ctx);

  if (!interface_decl)
    return;

  do {
    Decl *original_decl = NULL;
    ASTContext *original_ctx = NULL;

    m_ast_importer_sp->ResolveDeclOrigin(interface_decl, &original_decl,
                                         &original_ctx);

    if (!original_decl)
      break;

    ObjCInterfaceDecl *original_interface_decl =
        dyn_cast<ObjCInterfaceDecl>(original_decl);

    if (FindObjCMethodDeclsWithOrigin(current_id, context,
                                      original_interface_decl, "at origin"))
      return; // found it, no need to look any further
  } while (0);

  StreamString ss;

  if (decl_name.isObjCZeroArgSelector()) {
    ss.Printf("%s", decl_name.getAsString().c_str());
  } else if (decl_name.isObjCOneArgSelector()) {
    ss.Printf("%s", decl_name.getAsString().c_str());
  } else {
    clang::Selector sel = decl_name.getObjCSelector();

    for (unsigned i = 0, e = sel.getNumArgs(); i != e; ++i) {
      llvm::StringRef r = sel.getNameForSlot(i);
      ss.Printf("%s:", r.str().c_str());
    }
  }
  ss.Flush();

  if (ss.GetString().contains("$__lldb"))
    return; // we don't need any results

  ConstString selector_name(ss.GetString());

  if (log)
    log->Printf("ClangASTSource::FindObjCMethodDecls[%d] on (ASTContext*)%p "
                "for selector [%s %s]",
                current_id, static_cast<void *>(m_ast_context),
                interface_decl->getNameAsString().c_str(),
                selector_name.AsCString());
  SymbolContextList sc_list;

  const bool include_symbols = false;
  const bool include_inlines = false;
  const bool append = false;

  std::string interface_name = interface_decl->getNameAsString();

  do {
    StreamString ms;
    ms.Printf("-[%s %s]", interface_name.c_str(), selector_name.AsCString());
    ms.Flush();
    ConstString instance_method_name(ms.GetString());

    m_target->GetImages().FindFunctions(
        instance_method_name, lldb::eFunctionNameTypeFull, include_symbols,
        include_inlines, append, sc_list);

    if (sc_list.GetSize())
      break;

    ms.Clear();
    ms.Printf("+[%s %s]", interface_name.c_str(), selector_name.AsCString());
    ms.Flush();
    ConstString class_method_name(ms.GetString());

    m_target->GetImages().FindFunctions(
        class_method_name, lldb::eFunctionNameTypeFull, include_symbols,
        include_inlines, append, sc_list);

    if (sc_list.GetSize())
      break;

    // Fall back and check for methods in categories.  If we find methods this
    // way, we need to check that they're actually in categories on the desired
    // class.

    SymbolContextList candidate_sc_list;

    m_target->GetImages().FindFunctions(
        selector_name, lldb::eFunctionNameTypeSelector, include_symbols,
        include_inlines, append, candidate_sc_list);

    for (uint32_t ci = 0, ce = candidate_sc_list.GetSize(); ci != ce; ++ci) {
      SymbolContext candidate_sc;

      if (!candidate_sc_list.GetContextAtIndex(ci, candidate_sc))
        continue;

      if (!candidate_sc.function)
        continue;

      const char *candidate_name = candidate_sc.function->GetName().AsCString();

      const char *cursor = candidate_name;

      if (*cursor != '+' && *cursor != '-')
        continue;

      ++cursor;

      if (*cursor != '[')
        continue;

      ++cursor;

      size_t interface_len = interface_name.length();

      if (strncmp(cursor, interface_name.c_str(), interface_len))
        continue;

      cursor += interface_len;

      if (*cursor == ' ' || *cursor == '(')
        sc_list.Append(candidate_sc);
    }
  } while (0);

  if (sc_list.GetSize()) {
    // We found a good function symbol.  Use that.

    for (uint32_t i = 0, e = sc_list.GetSize(); i != e; ++i) {
      SymbolContext sc;

      if (!sc_list.GetContextAtIndex(i, sc))
        continue;

      if (!sc.function)
        continue;

      CompilerDeclContext function_decl_ctx = sc.function->GetDeclContext();
      if (!function_decl_ctx)
        continue;

      ObjCMethodDecl *method_decl =
          ClangASTContext::DeclContextGetAsObjCMethodDecl(function_decl_ctx);

      if (!method_decl)
        continue;

      ObjCInterfaceDecl *found_interface_decl =
          method_decl->getClassInterface();

      if (!found_interface_decl)
        continue;

      if (found_interface_decl->getName() == interface_decl->getName()) {
        Decl *copied_decl = CopyDecl(method_decl);

        if (!copied_decl)
          continue;

        ObjCMethodDecl *copied_method_decl =
            dyn_cast<ObjCMethodDecl>(copied_decl);

        if (!copied_method_decl)
          continue;

        if (log) {
          ASTDumper dumper((Decl *)copied_method_decl);
          log->Printf("  CAS::FOMD[%d] found (in symbols) %s", current_id,
                      dumper.GetCString());
        }

        context.AddNamedDecl(copied_method_decl);
      }
    }

    return;
  }

  // Try the debug information.

  do {
    ObjCInterfaceDecl *complete_interface_decl = GetCompleteObjCInterface(
        const_cast<ObjCInterfaceDecl *>(interface_decl));

    if (!complete_interface_decl)
      break;

    // We found the complete interface.  The runtime never needs to be queried
    // in this scenario.

    DeclFromUser<const ObjCInterfaceDecl> complete_iface_decl(
        complete_interface_decl);

    if (complete_interface_decl == interface_decl)
      break; // already checked this one

    if (log)
      log->Printf("CAS::FOPD[%d] trying origin "
                  "(ObjCInterfaceDecl*)%p/(ASTContext*)%p...",
                  current_id, static_cast<void *>(complete_interface_decl),
                  static_cast<void *>(&complete_iface_decl->getASTContext()));

    FindObjCMethodDeclsWithOrigin(current_id, context, complete_interface_decl,
                                  "in debug info");

    return;
  } while (0);

  do {
    // Check the modules only if the debug information didn't have a complete
    // interface.

    if (ClangModulesDeclVendor *modules_decl_vendor =
            m_target->GetClangModulesDeclVendor()) {
      ConstString interface_name(interface_decl->getNameAsString().c_str());
      bool append = false;
      uint32_t max_matches = 1;
      std::vector<clang::NamedDecl *> decls;

      if (!modules_decl_vendor->FindDecls(interface_name, append, max_matches,
                                          decls))
        break;

      ObjCInterfaceDecl *interface_decl_from_modules =
          dyn_cast<ObjCInterfaceDecl>(decls[0]);

      if (!interface_decl_from_modules)
        break;

      if (FindObjCMethodDeclsWithOrigin(
              current_id, context, interface_decl_from_modules, "in modules"))
        return;
    }
  } while (0);

  do {
    // Check the runtime only if the debug information didn't have a complete
    // interface and the modules don't get us anywhere.

    lldb::ProcessSP process(m_target->GetProcessSP());

    if (!process)
      break;

    ObjCLanguageRuntime *language_runtime(process->GetObjCLanguageRuntime());

    if (!language_runtime)
      break;

    DeclVendor *decl_vendor = language_runtime->GetDeclVendor();

    if (!decl_vendor)
      break;

    ConstString interface_name(interface_decl->getNameAsString().c_str());
    bool append = false;
    uint32_t max_matches = 1;
    std::vector<clang::NamedDecl *> decls;

    if (!decl_vendor->FindDecls(interface_name, append, max_matches, decls))
      break;

    ObjCInterfaceDecl *runtime_interface_decl =
        dyn_cast<ObjCInterfaceDecl>(decls[0]);

    if (!runtime_interface_decl)
      break;

    FindObjCMethodDeclsWithOrigin(current_id, context, runtime_interface_decl,
                                  "in runtime");
  } while (0);
}

static bool FindObjCPropertyAndIvarDeclsWithOrigin(
    unsigned int current_id, NameSearchContext &context, ClangASTSource &source,
    DeclFromUser<const ObjCInterfaceDecl> &origin_iface_decl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (origin_iface_decl.IsInvalid())
    return false;

  std::string name_str = context.m_decl_name.getAsString();
  StringRef name(name_str);
  IdentifierInfo &name_identifier(
      origin_iface_decl->getASTContext().Idents.get(name));

  DeclFromUser<ObjCPropertyDecl> origin_property_decl(
      origin_iface_decl->FindPropertyDeclaration(
          &name_identifier, ObjCPropertyQueryKind::OBJC_PR_query_instance));

  bool found = false;

  if (origin_property_decl.IsValid()) {
    DeclFromParser<ObjCPropertyDecl> parser_property_decl(
        origin_property_decl.Import(source));
    if (parser_property_decl.IsValid()) {
      if (log) {
        ASTDumper dumper((Decl *)parser_property_decl.decl);
        log->Printf("  CAS::FOPD[%d] found %s", current_id,
                    dumper.GetCString());
      }

      context.AddNamedDecl(parser_property_decl.decl);
      found = true;
    }
  }

  DeclFromUser<ObjCIvarDecl> origin_ivar_decl(
      origin_iface_decl->getIvarDecl(&name_identifier));

  if (origin_ivar_decl.IsValid()) {
    DeclFromParser<ObjCIvarDecl> parser_ivar_decl(
        origin_ivar_decl.Import(source));
    if (parser_ivar_decl.IsValid()) {
      if (log) {
        ASTDumper dumper((Decl *)parser_ivar_decl.decl);
        log->Printf("  CAS::FOPD[%d] found %s", current_id,
                    dumper.GetCString());
      }

      context.AddNamedDecl(parser_ivar_decl.decl);
      found = true;
    }
  }

  return found;
}

void ClangASTSource::FindObjCPropertyAndIvarDecls(NameSearchContext &context) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  DeclFromParser<const ObjCInterfaceDecl> parser_iface_decl(
      cast<ObjCInterfaceDecl>(context.m_decl_context));
  DeclFromUser<const ObjCInterfaceDecl> origin_iface_decl(
      parser_iface_decl.GetOrigin(*this));

  ConstString class_name(parser_iface_decl->getNameAsString().c_str());

  if (log)
    log->Printf("ClangASTSource::FindObjCPropertyAndIvarDecls[%d] on "
                "(ASTContext*)%p for '%s.%s'",
                current_id, static_cast<void *>(m_ast_context),
                parser_iface_decl->getNameAsString().c_str(),
                context.m_decl_name.getAsString().c_str());

  if (FindObjCPropertyAndIvarDeclsWithOrigin(
          current_id, context, *this, origin_iface_decl))
    return;

  if (log)
    log->Printf("CAS::FOPD[%d] couldn't find the property on origin "
                "(ObjCInterfaceDecl*)%p/(ASTContext*)%p, searching "
                "elsewhere...",
                current_id, static_cast<const void *>(origin_iface_decl.decl),
                static_cast<void *>(&origin_iface_decl->getASTContext()));

  SymbolContext null_sc;
  TypeList type_list;

  do {
    ObjCInterfaceDecl *complete_interface_decl = GetCompleteObjCInterface(
        const_cast<ObjCInterfaceDecl *>(parser_iface_decl.decl));

    if (!complete_interface_decl)
      break;

    // We found the complete interface.  The runtime never needs to be queried
    // in this scenario.

    DeclFromUser<const ObjCInterfaceDecl> complete_iface_decl(
        complete_interface_decl);

    if (complete_iface_decl.decl == origin_iface_decl.decl)
      break; // already checked this one

    if (log)
      log->Printf("CAS::FOPD[%d] trying origin "
                  "(ObjCInterfaceDecl*)%p/(ASTContext*)%p...",
                  current_id,
                  static_cast<const void *>(complete_iface_decl.decl),
                  static_cast<void *>(&complete_iface_decl->getASTContext()));

    FindObjCPropertyAndIvarDeclsWithOrigin(current_id, context, *this,
                                           complete_iface_decl);

    return;
  } while (0);

  do {
    // Check the modules only if the debug information didn't have a complete
    // interface.

    ClangModulesDeclVendor *modules_decl_vendor =
        m_target->GetClangModulesDeclVendor();

    if (!modules_decl_vendor)
      break;

    bool append = false;
    uint32_t max_matches = 1;
    std::vector<clang::NamedDecl *> decls;

    if (!modules_decl_vendor->FindDecls(class_name, append, max_matches, decls))
      break;

    DeclFromUser<const ObjCInterfaceDecl> interface_decl_from_modules(
        dyn_cast<ObjCInterfaceDecl>(decls[0]));

    if (!interface_decl_from_modules.IsValid())
      break;

    if (log)
      log->Printf(
          "CAS::FOPD[%d] trying module "
          "(ObjCInterfaceDecl*)%p/(ASTContext*)%p...",
          current_id,
          static_cast<const void *>(interface_decl_from_modules.decl),
          static_cast<void *>(&interface_decl_from_modules->getASTContext()));

    if (FindObjCPropertyAndIvarDeclsWithOrigin(current_id, context, *this,
                                               interface_decl_from_modules))
      return;
  } while (0);

  do {
    // Check the runtime only if the debug information didn't have a complete
    // interface and nothing was in the modules.

    lldb::ProcessSP process(m_target->GetProcessSP());

    if (!process)
      return;

    ObjCLanguageRuntime *language_runtime(process->GetObjCLanguageRuntime());

    if (!language_runtime)
      return;

    DeclVendor *decl_vendor = language_runtime->GetDeclVendor();

    if (!decl_vendor)
      break;

    bool append = false;
    uint32_t max_matches = 1;
    std::vector<clang::NamedDecl *> decls;

    if (!decl_vendor->FindDecls(class_name, append, max_matches, decls))
      break;

    DeclFromUser<const ObjCInterfaceDecl> interface_decl_from_runtime(
        dyn_cast<ObjCInterfaceDecl>(decls[0]));

    if (!interface_decl_from_runtime.IsValid())
      break;

    if (log)
      log->Printf(
          "CAS::FOPD[%d] trying runtime "
          "(ObjCInterfaceDecl*)%p/(ASTContext*)%p...",
          current_id,
          static_cast<const void *>(interface_decl_from_runtime.decl),
          static_cast<void *>(&interface_decl_from_runtime->getASTContext()));

    if (FindObjCPropertyAndIvarDeclsWithOrigin(
            current_id, context, *this, interface_decl_from_runtime))
      return;
  } while (0);
}

typedef llvm::DenseMap<const FieldDecl *, uint64_t> FieldOffsetMap;
typedef llvm::DenseMap<const CXXRecordDecl *, CharUnits> BaseOffsetMap;

template <class D, class O>
static bool ImportOffsetMap(llvm::DenseMap<const D *, O> &destination_map,
                            llvm::DenseMap<const D *, O> &source_map,
                            ClangASTSource &source) {
  // When importing fields into a new record, clang has a hard requirement that
  // fields be imported in field offset order.  Since they are stored in a
  // DenseMap with a pointer as the key type, this means we cannot simply
  // iterate over the map, as the order will be non-deterministic.  Instead we
  // have to sort by the offset and then insert in sorted order.
  typedef llvm::DenseMap<const D *, O> MapType;
  typedef typename MapType::value_type PairType;
  std::vector<PairType> sorted_items;
  sorted_items.reserve(source_map.size());
  sorted_items.assign(source_map.begin(), source_map.end());
  llvm::sort(sorted_items.begin(), sorted_items.end(),
             [](const PairType &lhs, const PairType &rhs) {
               return lhs.second < rhs.second;
             });

  for (const auto &item : sorted_items) {
    DeclFromUser<D> user_decl(const_cast<D *>(item.first));
    DeclFromParser<D> parser_decl(user_decl.Import(source));
    if (parser_decl.IsInvalid())
      return false;
    destination_map.insert(
        std::pair<const D *, O>(parser_decl.decl, item.second));
  }

  return true;
}

template <bool IsVirtual>
bool ExtractBaseOffsets(const ASTRecordLayout &record_layout,
                        DeclFromUser<const CXXRecordDecl> &record,
                        BaseOffsetMap &base_offsets) {
  for (CXXRecordDecl::base_class_const_iterator
           bi = (IsVirtual ? record->vbases_begin() : record->bases_begin()),
           be = (IsVirtual ? record->vbases_end() : record->bases_end());
       bi != be; ++bi) {
    if (!IsVirtual && bi->isVirtual())
      continue;

    const clang::Type *origin_base_type = bi->getType().getTypePtr();
    const clang::RecordType *origin_base_record_type =
        origin_base_type->getAs<RecordType>();

    if (!origin_base_record_type)
      return false;

    DeclFromUser<RecordDecl> origin_base_record(
        origin_base_record_type->getDecl());

    if (origin_base_record.IsInvalid())
      return false;

    DeclFromUser<CXXRecordDecl> origin_base_cxx_record(
        DynCast<CXXRecordDecl>(origin_base_record));

    if (origin_base_cxx_record.IsInvalid())
      return false;

    CharUnits base_offset;

    if (IsVirtual)
      base_offset =
          record_layout.getVBaseClassOffset(origin_base_cxx_record.decl);
    else
      base_offset =
          record_layout.getBaseClassOffset(origin_base_cxx_record.decl);

    base_offsets.insert(std::pair<const CXXRecordDecl *, CharUnits>(
        origin_base_cxx_record.decl, base_offset));
  }

  return true;
}

bool ClangASTSource::layoutRecordType(const RecordDecl *record, uint64_t &size,
                                      uint64_t &alignment,
                                      FieldOffsetMap &field_offsets,
                                      BaseOffsetMap &base_offsets,
                                      BaseOffsetMap &virtual_base_offsets) {
  ClangASTMetrics::RegisterRecordLayout();

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    log->Printf("LayoutRecordType[%u] on (ASTContext*)%p for (RecordDecl*)%p "
                "[name = '%s']",
                current_id, static_cast<void *>(m_ast_context),
                static_cast<const void *>(record),
                record->getNameAsString().c_str());

  DeclFromParser<const RecordDecl> parser_record(record);
  DeclFromUser<const RecordDecl> origin_record(
      parser_record.GetOrigin(*this));

  if (origin_record.IsInvalid())
    return false;

  FieldOffsetMap origin_field_offsets;
  BaseOffsetMap origin_base_offsets;
  BaseOffsetMap origin_virtual_base_offsets;

  ClangASTContext::GetCompleteDecl(
      &origin_record->getASTContext(),
      const_cast<RecordDecl *>(origin_record.decl));

  clang::RecordDecl *definition = origin_record.decl->getDefinition();
  if (!definition || !definition->isCompleteDefinition())
    return false;

  const ASTRecordLayout &record_layout(
      origin_record->getASTContext().getASTRecordLayout(origin_record.decl));

  int field_idx = 0, field_count = record_layout.getFieldCount();

  for (RecordDecl::field_iterator fi = origin_record->field_begin(),
                                  fe = origin_record->field_end();
       fi != fe; ++fi) {
    if (field_idx >= field_count)
      return false; // Layout didn't go well.  Bail out.

    uint64_t field_offset = record_layout.getFieldOffset(field_idx);

    origin_field_offsets.insert(
        std::pair<const FieldDecl *, uint64_t>(*fi, field_offset));

    field_idx++;
  }

  lldbassert(&record->getASTContext() == m_ast_context);

  DeclFromUser<const CXXRecordDecl> origin_cxx_record(
      DynCast<const CXXRecordDecl>(origin_record));

  if (origin_cxx_record.IsValid()) {
    if (!ExtractBaseOffsets<false>(record_layout, origin_cxx_record,
                                   origin_base_offsets) ||
        !ExtractBaseOffsets<true>(record_layout, origin_cxx_record,
                                  origin_virtual_base_offsets))
      return false;
  }

  if (!ImportOffsetMap(field_offsets, origin_field_offsets, *this) ||
      !ImportOffsetMap(base_offsets, origin_base_offsets, *this) ||
      !ImportOffsetMap(virtual_base_offsets, origin_virtual_base_offsets,
                       *this))
    return false;

  size = record_layout.getSize().getQuantity() * m_ast_context->getCharWidth();
  alignment = record_layout.getAlignment().getQuantity() *
              m_ast_context->getCharWidth();

  if (log) {
    log->Printf("LRT[%u] returned:", current_id);
    log->Printf("LRT[%u]   Original = (RecordDecl*)%p", current_id,
                static_cast<const void *>(origin_record.decl));
    log->Printf("LRT[%u]   Size = %" PRId64, current_id, size);
    log->Printf("LRT[%u]   Alignment = %" PRId64, current_id, alignment);
    log->Printf("LRT[%u]   Fields:", current_id);
    for (RecordDecl::field_iterator fi = record->field_begin(),
                                    fe = record->field_end();
         fi != fe; ++fi) {
      log->Printf("LRT[%u]     (FieldDecl*)%p, Name = '%s', Offset = %" PRId64
                  " bits",
                  current_id, static_cast<void *>(*fi),
                  fi->getNameAsString().c_str(), field_offsets[*fi]);
    }
    DeclFromParser<const CXXRecordDecl> parser_cxx_record =
        DynCast<const CXXRecordDecl>(parser_record);
    if (parser_cxx_record.IsValid()) {
      log->Printf("LRT[%u]   Bases:", current_id);
      for (CXXRecordDecl::base_class_const_iterator
               bi = parser_cxx_record->bases_begin(),
               be = parser_cxx_record->bases_end();
           bi != be; ++bi) {
        bool is_virtual = bi->isVirtual();

        QualType base_type = bi->getType();
        const RecordType *base_record_type = base_type->getAs<RecordType>();
        DeclFromParser<RecordDecl> base_record(base_record_type->getDecl());
        DeclFromParser<CXXRecordDecl> base_cxx_record =
            DynCast<CXXRecordDecl>(base_record);

        log->Printf(
            "LRT[%u]     %s(CXXRecordDecl*)%p, Name = '%s', Offset = %" PRId64
            " chars",
            current_id, (is_virtual ? "Virtual " : ""),
            static_cast<void *>(base_cxx_record.decl),
            base_cxx_record.decl->getNameAsString().c_str(),
            (is_virtual
                 ? virtual_base_offsets[base_cxx_record.decl].getQuantity()
                 : base_offsets[base_cxx_record.decl].getQuantity()));
      }
    } else {
      log->Printf("LRD[%u]   Not a CXXRecord, so no bases", current_id);
    }
  }

  return true;
}

void ClangASTSource::CompleteNamespaceMap(
    ClangASTImporter::NamespaceMapSP &namespace_map, const ConstString &name,
    ClangASTImporter::NamespaceMapSP &parent_map) const {
  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log) {
    if (parent_map && parent_map->size())
      log->Printf("CompleteNamespaceMap[%u] on (ASTContext*)%p Searching for "
                  "namespace %s in namespace %s",
                  current_id, static_cast<void *>(m_ast_context),
                  name.GetCString(),
                  parent_map->begin()->second.GetName().AsCString());
    else
      log->Printf("CompleteNamespaceMap[%u] on (ASTContext*)%p Searching for "
                  "namespace %s",
                  current_id, static_cast<void *>(m_ast_context),
                  name.GetCString());
  }

  if (parent_map) {
    for (ClangASTImporter::NamespaceMap::iterator i = parent_map->begin(),
                                                  e = parent_map->end();
         i != e; ++i) {
      CompilerDeclContext found_namespace_decl;

      lldb::ModuleSP module_sp = i->first;
      CompilerDeclContext module_parent_namespace_decl = i->second;

      SymbolVendor *symbol_vendor = module_sp->GetSymbolVendor();

      if (!symbol_vendor)
        continue;

      found_namespace_decl =
          symbol_vendor->FindNamespace(name, &module_parent_namespace_decl);

      if (!found_namespace_decl)
        continue;

      namespace_map->push_back(std::pair<lldb::ModuleSP, CompilerDeclContext>(
          module_sp, found_namespace_decl));

      if (log)
        log->Printf("  CMN[%u] Found namespace %s in module %s", current_id,
                    name.GetCString(),
                    module_sp->GetFileSpec().GetFilename().GetCString());
    }
  } else {
    const ModuleList &target_images = m_target->GetImages();
    std::lock_guard<std::recursive_mutex> guard(target_images.GetMutex());

    CompilerDeclContext null_namespace_decl;

    for (size_t i = 0, e = target_images.GetSize(); i < e; ++i) {
      lldb::ModuleSP image = target_images.GetModuleAtIndexUnlocked(i);

      if (!image)
        continue;

      CompilerDeclContext found_namespace_decl;

      SymbolVendor *symbol_vendor = image->GetSymbolVendor();

      if (!symbol_vendor)
        continue;

      found_namespace_decl =
          symbol_vendor->FindNamespace(name, &null_namespace_decl);

      if (!found_namespace_decl)
        continue;

      namespace_map->push_back(std::pair<lldb::ModuleSP, CompilerDeclContext>(
          image, found_namespace_decl));

      if (log)
        log->Printf("  CMN[%u] Found namespace %s in module %s", current_id,
                    name.GetCString(),
                    image->GetFileSpec().GetFilename().GetCString());
    }
  }
}

NamespaceDecl *ClangASTSource::AddNamespace(
    NameSearchContext &context,
    ClangASTImporter::NamespaceMapSP &namespace_decls) {
  if (!namespace_decls)
    return nullptr;

  const CompilerDeclContext &namespace_decl = namespace_decls->begin()->second;

  clang::ASTContext *src_ast =
      ClangASTContext::DeclContextGetClangASTContext(namespace_decl);
  if (!src_ast)
    return nullptr;
  clang::NamespaceDecl *src_namespace_decl =
      ClangASTContext::DeclContextGetAsNamespaceDecl(namespace_decl);

  if (!src_namespace_decl)
    return nullptr;

  Decl *copied_decl = CopyDecl(src_namespace_decl);

  if (!copied_decl)
    return nullptr;

  NamespaceDecl *copied_namespace_decl = dyn_cast<NamespaceDecl>(copied_decl);

  if (!copied_namespace_decl)
    return nullptr;

  context.m_decls.push_back(copied_namespace_decl);

  m_ast_importer_sp->RegisterNamespaceMap(copied_namespace_decl,
                                          namespace_decls);

  return dyn_cast<NamespaceDecl>(copied_decl);
}

clang::QualType ClangASTSource::CopyTypeWithMerger(
    clang::ASTContext &from_context,
    clang::ExternalASTMerger &merger,
    clang::QualType type) {
  if (!merger.HasImporterForOrigin(from_context)) {
    lldbassert(0 && "Couldn't find the importer for a source context!");
    return QualType();
  }

  return merger.ImporterForOrigin(from_context).Import(type);
}

clang::Decl *ClangASTSource::CopyDecl(Decl *src_decl) {
  clang::ASTContext &from_context = src_decl->getASTContext();
  if (m_ast_importer_sp) {
    return m_ast_importer_sp->CopyDecl(m_ast_context, &from_context, src_decl);
  } else if (m_merger_up) {
    if (!m_merger_up->HasImporterForOrigin(from_context)) {
      lldbassert(0 && "Couldn't find the importer for a source context!");
      return nullptr;
    }

    return m_merger_up->ImporterForOrigin(from_context).Import(src_decl);
  } else {
    lldbassert(0 && "No mechanism for copying a decl!");
    return nullptr;
  }
}

bool ClangASTSource::ResolveDeclOrigin(const clang::Decl *decl,
                                       clang::Decl **original_decl,
                                       clang::ASTContext **original_ctx) {
  if (m_ast_importer_sp) {
    return m_ast_importer_sp->ResolveDeclOrigin(decl, original_decl,
                                                original_ctx);
  } else if (m_merger_up) {
    return false; // Implement this correctly in ExternalASTMerger
  } else {
    // this can happen early enough that no ExternalASTSource is installed.
    return false;
  }
}

clang::ExternalASTMerger &ClangASTSource::GetMergerUnchecked() {
  lldbassert(m_merger_up != nullptr);
  return *m_merger_up;
}

CompilerType ClangASTSource::GuardedCopyType(const CompilerType &src_type) {
  ClangASTContext *src_ast =
      llvm::dyn_cast_or_null<ClangASTContext>(src_type.GetTypeSystem());
  if (src_ast == nullptr)
    return CompilerType();

  ClangASTMetrics::RegisterLLDBImport();

  SetImportInProgress(true);

  QualType copied_qual_type;

  if (m_ast_importer_sp) {
    copied_qual_type =
        m_ast_importer_sp->CopyType(m_ast_context, src_ast->getASTContext(),
                                    ClangUtil::GetQualType(src_type));
  } else if (m_merger_up) {
    copied_qual_type =
        CopyTypeWithMerger(*src_ast->getASTContext(), *m_merger_up,
                 ClangUtil::GetQualType(src_type));
  } else {
    lldbassert(0 && "No mechanism for copying a type!");
    return CompilerType();
  }

  SetImportInProgress(false);

  if (copied_qual_type.getAsOpaquePtr() &&
      copied_qual_type->getCanonicalTypeInternal().isNull())
    // this shouldn't happen, but we're hardening because the AST importer
    // seems to be generating bad types on occasion.
    return CompilerType();

  return CompilerType(m_ast_context, copied_qual_type);
}

clang::NamedDecl *NameSearchContext::AddVarDecl(const CompilerType &type) {
  assert(type && "Type for variable must be valid!");

  if (!type.IsValid())
    return NULL;

  ClangASTContext *lldb_ast =
      llvm::dyn_cast<ClangASTContext>(type.GetTypeSystem());
  if (!lldb_ast)
    return NULL;

  IdentifierInfo *ii = m_decl_name.getAsIdentifierInfo();

  clang::ASTContext *ast = lldb_ast->getASTContext();

  clang::NamedDecl *Decl = VarDecl::Create(
      *ast, const_cast<DeclContext *>(m_decl_context), SourceLocation(),
      SourceLocation(), ii, ClangUtil::GetQualType(type), 0, SC_Static);
  m_decls.push_back(Decl);

  return Decl;
}

clang::NamedDecl *NameSearchContext::AddFunDecl(const CompilerType &type,
                                                bool extern_c) {
  assert(type && "Type for variable must be valid!");

  if (!type.IsValid())
    return NULL;

  if (m_function_types.count(type))
    return NULL;

  ClangASTContext *lldb_ast =
      llvm::dyn_cast<ClangASTContext>(type.GetTypeSystem());
  if (!lldb_ast)
    return NULL;

  m_function_types.insert(type);

  QualType qual_type(ClangUtil::GetQualType(type));

  clang::ASTContext *ast = lldb_ast->getASTContext();

  const bool isInlineSpecified = false;
  const bool hasWrittenPrototype = true;
  const bool isConstexprSpecified = false;

  clang::DeclContext *context = const_cast<DeclContext *>(m_decl_context);

  if (extern_c) {
    context = LinkageSpecDecl::Create(
        *ast, context, SourceLocation(), SourceLocation(),
        clang::LinkageSpecDecl::LanguageIDs::lang_c, false);
  }

  // Pass the identifier info for functions the decl_name is needed for
  // operators
  clang::DeclarationName decl_name =
      m_decl_name.getNameKind() == DeclarationName::Identifier
          ? m_decl_name.getAsIdentifierInfo()
          : m_decl_name;

  clang::FunctionDecl *func_decl = FunctionDecl::Create(
      *ast, context, SourceLocation(), SourceLocation(), decl_name, qual_type,
      NULL, SC_Extern, isInlineSpecified, hasWrittenPrototype,
      isConstexprSpecified);

  // We have to do more than just synthesize the FunctionDecl.  We have to
  // synthesize ParmVarDecls for all of the FunctionDecl's arguments.  To do
  // this, we raid the function's FunctionProtoType for types.

  const FunctionProtoType *func_proto_type =
      qual_type.getTypePtr()->getAs<FunctionProtoType>();

  if (func_proto_type) {
    unsigned NumArgs = func_proto_type->getNumParams();
    unsigned ArgIndex;

    SmallVector<ParmVarDecl *, 5> parm_var_decls;

    for (ArgIndex = 0; ArgIndex < NumArgs; ++ArgIndex) {
      QualType arg_qual_type(func_proto_type->getParamType(ArgIndex));

      parm_var_decls.push_back(ParmVarDecl::Create(
          *ast, const_cast<DeclContext *>(context), SourceLocation(),
          SourceLocation(), NULL, arg_qual_type, NULL, SC_Static, NULL));
    }

    func_decl->setParams(ArrayRef<ParmVarDecl *>(parm_var_decls));
  } else {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    if (log)
      log->Printf("Function type wasn't a FunctionProtoType");
  }

  // If this is an operator (e.g. operator new or operator==), only insert the
  // declaration we inferred from the symbol if we can provide the correct
  // number of arguments. We shouldn't really inject random decl(s) for
  // functions that are analyzed semantically in a special way, otherwise we
  // will crash in clang.
  clang::OverloadedOperatorKind op_kind = clang::NUM_OVERLOADED_OPERATORS;
  if (func_proto_type &&
      ClangASTContext::IsOperator(decl_name.getAsString().c_str(), op_kind)) {
    if (!ClangASTContext::CheckOverloadedOperatorKindParameterCount(
            false, op_kind, func_proto_type->getNumParams()))
      return NULL;
  }
  m_decls.push_back(func_decl);

  return func_decl;
}

clang::NamedDecl *NameSearchContext::AddGenericFunDecl() {
  FunctionProtoType::ExtProtoInfo proto_info;

  proto_info.Variadic = true;

  QualType generic_function_type(m_ast_source.m_ast_context->getFunctionType(
      m_ast_source.m_ast_context->UnknownAnyTy, // result
      ArrayRef<QualType>(),                     // argument types
      proto_info));

  return AddFunDecl(
      CompilerType(m_ast_source.m_ast_context, generic_function_type), true);
}

clang::NamedDecl *
NameSearchContext::AddTypeDecl(const CompilerType &clang_type) {
  if (ClangUtil::IsClangType(clang_type)) {
    QualType qual_type = ClangUtil::GetQualType(clang_type);

    if (const TypedefType *typedef_type =
            llvm::dyn_cast<TypedefType>(qual_type)) {
      TypedefNameDecl *typedef_name_decl = typedef_type->getDecl();

      m_decls.push_back(typedef_name_decl);

      return (NamedDecl *)typedef_name_decl;
    } else if (const TagType *tag_type = qual_type->getAs<TagType>()) {
      TagDecl *tag_decl = tag_type->getDecl();

      m_decls.push_back(tag_decl);

      return tag_decl;
    } else if (const ObjCObjectType *objc_object_type =
                   qual_type->getAs<ObjCObjectType>()) {
      ObjCInterfaceDecl *interface_decl = objc_object_type->getInterface();

      m_decls.push_back((NamedDecl *)interface_decl);

      return (NamedDecl *)interface_decl;
    }
  }
  return NULL;
}

void NameSearchContext::AddLookupResult(clang::DeclContextLookupResult result) {
  for (clang::NamedDecl *decl : result)
    m_decls.push_back(decl);
}

void NameSearchContext::AddNamedDecl(clang::NamedDecl *decl) {
  m_decls.push_back(decl);
}
