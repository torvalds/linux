//===-- ClangModulesDeclVendor.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <mutex>

#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Lookup.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"

#include "ClangHost.h"
#include "ClangModulesDeclVendor.h"

#include "lldb/Core/ModuleList.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;

namespace {
// Any Clang compiler requires a consumer for diagnostics.  This one stores
// them as strings so we can provide them to the user in case a module failed
// to load.
class StoringDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  StoringDiagnosticConsumer();

  void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel,
                        const clang::Diagnostic &info) override;

  void ClearDiagnostics();

  void DumpDiagnostics(Stream &error_stream);

private:
  typedef std::pair<clang::DiagnosticsEngine::Level, std::string>
      IDAndDiagnostic;
  std::vector<IDAndDiagnostic> m_diagnostics;
  Log *m_log;
};

// The private implementation of our ClangModulesDeclVendor.  Contains all the
// Clang state required to load modules.
class ClangModulesDeclVendorImpl : public ClangModulesDeclVendor {
public:
  ClangModulesDeclVendorImpl(
      llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diagnostics_engine,
      std::shared_ptr<clang::CompilerInvocation> compiler_invocation,
      std::unique_ptr<clang::CompilerInstance> compiler_instance,
      std::unique_ptr<clang::Parser> parser);

  ~ClangModulesDeclVendorImpl() override = default;

  bool AddModule(ModulePath &path, ModuleVector *exported_modules,
                 Stream &error_stream) override;

  bool AddModulesForCompileUnit(CompileUnit &cu, ModuleVector &exported_modules,
                                Stream &error_stream) override;

  uint32_t FindDecls(const ConstString &name, bool append, uint32_t max_matches,
                     std::vector<clang::NamedDecl *> &decls) override;

  void ForEachMacro(const ModuleVector &modules,
                    std::function<bool(const std::string &)> handler) override;

  clang::ExternalASTMerger::ImporterSource GetImporterSource() override;
private:
  void
  ReportModuleExportsHelper(std::set<ClangModulesDeclVendor::ModuleID> &exports,
                            clang::Module *module);

  void ReportModuleExports(ModuleVector &exports, clang::Module *module);

  clang::ModuleLoadResult DoGetModule(clang::ModuleIdPath path,
                                      bool make_visible);

  bool m_enabled = false;

  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> m_diagnostics_engine;
  std::shared_ptr<clang::CompilerInvocation> m_compiler_invocation;
  std::unique_ptr<clang::CompilerInstance> m_compiler_instance;
  std::unique_ptr<clang::Parser> m_parser;
  size_t m_source_location_index =
      0; // used to give name components fake SourceLocations

  typedef std::vector<ConstString> ImportedModule;
  typedef std::map<ImportedModule, clang::Module *> ImportedModuleMap;
  typedef std::set<ModuleID> ImportedModuleSet;
  ImportedModuleMap m_imported_modules;
  ImportedModuleSet m_user_imported_modules;
  const clang::ExternalASTMerger::OriginMap m_origin_map;
};
} // anonymous namespace

StoringDiagnosticConsumer::StoringDiagnosticConsumer() {
  m_log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS);
}

void StoringDiagnosticConsumer::HandleDiagnostic(
    clang::DiagnosticsEngine::Level DiagLevel, const clang::Diagnostic &info) {
  llvm::SmallVector<char, 256> diagnostic_string;

  info.FormatDiagnostic(diagnostic_string);

  m_diagnostics.push_back(
      IDAndDiagnostic(DiagLevel, std::string(diagnostic_string.data(),
                                             diagnostic_string.size())));
}

void StoringDiagnosticConsumer::ClearDiagnostics() { m_diagnostics.clear(); }

void StoringDiagnosticConsumer::DumpDiagnostics(Stream &error_stream) {
  for (IDAndDiagnostic &diag : m_diagnostics) {
    switch (diag.first) {
    default:
      error_stream.PutCString(diag.second);
      error_stream.PutChar('\n');
      break;
    case clang::DiagnosticsEngine::Level::Ignored:
      break;
    }
  }
}

ClangModulesDeclVendor::ClangModulesDeclVendor() {}

ClangModulesDeclVendor::~ClangModulesDeclVendor() {}

ClangModulesDeclVendorImpl::ClangModulesDeclVendorImpl(
    llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diagnostics_engine,
    std::shared_ptr<clang::CompilerInvocation> compiler_invocation,
    std::unique_ptr<clang::CompilerInstance> compiler_instance,
    std::unique_ptr<clang::Parser> parser)
    : m_diagnostics_engine(std::move(diagnostics_engine)),
      m_compiler_invocation(std::move(compiler_invocation)),
      m_compiler_instance(std::move(compiler_instance)),
      m_parser(std::move(parser)), m_origin_map() {}

void ClangModulesDeclVendorImpl::ReportModuleExportsHelper(
    std::set<ClangModulesDeclVendor::ModuleID> &exports,
    clang::Module *module) {
  if (exports.count(reinterpret_cast<ClangModulesDeclVendor::ModuleID>(module)))
    return;

  exports.insert(reinterpret_cast<ClangModulesDeclVendor::ModuleID>(module));

  llvm::SmallVector<clang::Module *, 2> sub_exports;

  module->getExportedModules(sub_exports);

  for (clang::Module *module : sub_exports) {
    ReportModuleExportsHelper(exports, module);
  }
}

void ClangModulesDeclVendorImpl::ReportModuleExports(
    ClangModulesDeclVendor::ModuleVector &exports, clang::Module *module) {
  std::set<ClangModulesDeclVendor::ModuleID> exports_set;

  ReportModuleExportsHelper(exports_set, module);

  for (ModuleID module : exports_set) {
    exports.push_back(module);
  }
}

bool ClangModulesDeclVendorImpl::AddModule(ModulePath &path,
                                           ModuleVector *exported_modules,
                                           Stream &error_stream) {
  // Fail early.

  if (m_compiler_instance->hadModuleLoaderFatalFailure()) {
    error_stream.PutCString("error: Couldn't load a module because the module "
                            "loader is in a fatal state.\n");
    return false;
  }

  // Check if we've already imported this module.

  std::vector<ConstString> imported_module;

  for (ConstString path_component : path) {
    imported_module.push_back(path_component);
  }

  {
    ImportedModuleMap::iterator mi = m_imported_modules.find(imported_module);

    if (mi != m_imported_modules.end()) {
      if (exported_modules) {
        ReportModuleExports(*exported_modules, mi->second);
      }
      return true;
    }
  }

  if (!m_compiler_instance->getPreprocessor()
           .getHeaderSearchInfo()
           .lookupModule(path[0].GetStringRef())) {
    error_stream.Printf("error: Header search couldn't locate module %s\n",
                        path[0].AsCString());
    return false;
  }

  llvm::SmallVector<std::pair<clang::IdentifierInfo *, clang::SourceLocation>,
                    4>
      clang_path;

  {
    clang::SourceManager &source_manager =
        m_compiler_instance->getASTContext().getSourceManager();

    for (ConstString path_component : path) {
      clang_path.push_back(std::make_pair(
          &m_compiler_instance->getASTContext().Idents.get(
              path_component.GetStringRef()),
          source_manager.getLocForStartOfFile(source_manager.getMainFileID())
              .getLocWithOffset(m_source_location_index++)));
    }
  }

  StoringDiagnosticConsumer *diagnostic_consumer =
      static_cast<StoringDiagnosticConsumer *>(
          m_compiler_instance->getDiagnostics().getClient());

  diagnostic_consumer->ClearDiagnostics();

  clang::Module *top_level_module = DoGetModule(clang_path.front(), false);

  if (!top_level_module) {
    diagnostic_consumer->DumpDiagnostics(error_stream);
    error_stream.Printf("error: Couldn't load top-level module %s\n",
                        path[0].AsCString());
    return false;
  }

  clang::Module *submodule = top_level_module;

  for (size_t ci = 1; ci < path.size(); ++ci) {
    llvm::StringRef component = path[ci].GetStringRef();
    submodule = submodule->findSubmodule(component.str());
    if (!submodule) {
      diagnostic_consumer->DumpDiagnostics(error_stream);
      error_stream.Printf("error: Couldn't load submodule %s\n",
                          component.str().c_str());
      return false;
    }
  }

  clang::Module *requested_module = DoGetModule(clang_path, true);

  if (requested_module != nullptr) {
    if (exported_modules) {
      ReportModuleExports(*exported_modules, requested_module);
    }

    m_imported_modules[imported_module] = requested_module;

    m_enabled = true;

    return true;
  }

  return false;
}

bool ClangModulesDeclVendor::LanguageSupportsClangModules(
    lldb::LanguageType language) {
  switch (language) {
  default:
    return false;
  // C++ and friends to be added
  case lldb::LanguageType::eLanguageTypeC:
  case lldb::LanguageType::eLanguageTypeC11:
  case lldb::LanguageType::eLanguageTypeC89:
  case lldb::LanguageType::eLanguageTypeC99:
  case lldb::LanguageType::eLanguageTypeObjC:
    return true;
  }
}

bool ClangModulesDeclVendorImpl::AddModulesForCompileUnit(
    CompileUnit &cu, ClangModulesDeclVendor::ModuleVector &exported_modules,
    Stream &error_stream) {
  if (LanguageSupportsClangModules(cu.GetLanguage())) {
    std::vector<ConstString> imported_modules = cu.GetImportedModules();

    for (ConstString imported_module : imported_modules) {
      std::vector<ConstString> path;

      path.push_back(imported_module);

      if (!AddModule(path, &exported_modules, error_stream)) {
        return false;
      }
    }

    return true;
  }

  return true;
}

// ClangImporter::lookupValue

uint32_t
ClangModulesDeclVendorImpl::FindDecls(const ConstString &name, bool append,
                                      uint32_t max_matches,
                                      std::vector<clang::NamedDecl *> &decls) {
  if (!m_enabled) {
    return 0;
  }

  if (!append)
    decls.clear();

  clang::IdentifierInfo &ident =
      m_compiler_instance->getASTContext().Idents.get(name.GetStringRef());

  clang::LookupResult lookup_result(
      m_compiler_instance->getSema(), clang::DeclarationName(&ident),
      clang::SourceLocation(), clang::Sema::LookupOrdinaryName);

  m_compiler_instance->getSema().LookupName(
      lookup_result,
      m_compiler_instance->getSema().getScopeForContext(
          m_compiler_instance->getASTContext().getTranslationUnitDecl()));

  uint32_t num_matches = 0;

  for (clang::NamedDecl *named_decl : lookup_result) {
    if (num_matches >= max_matches)
      return num_matches;

    decls.push_back(named_decl);
    ++num_matches;
  }

  return num_matches;
}

void ClangModulesDeclVendorImpl::ForEachMacro(
    const ClangModulesDeclVendor::ModuleVector &modules,
    std::function<bool(const std::string &)> handler) {
  if (!m_enabled) {
    return;
  }

  typedef std::map<ModuleID, ssize_t> ModulePriorityMap;
  ModulePriorityMap module_priorities;

  ssize_t priority = 0;

  for (ModuleID module : modules) {
    module_priorities[module] = priority++;
  }

  if (m_compiler_instance->getPreprocessor().getExternalSource()) {
    m_compiler_instance->getPreprocessor()
        .getExternalSource()
        ->ReadDefinedMacros();
  }

  for (clang::Preprocessor::macro_iterator
           mi = m_compiler_instance->getPreprocessor().macro_begin(),
           me = m_compiler_instance->getPreprocessor().macro_end();
       mi != me; ++mi) {
    const clang::IdentifierInfo *ii = nullptr;

    {
      if (clang::IdentifierInfoLookup *lookup =
              m_compiler_instance->getPreprocessor()
                  .getIdentifierTable()
                  .getExternalIdentifierLookup()) {
        lookup->get(mi->first->getName());
      }
      if (!ii) {
        ii = mi->first;
      }
    }

    ssize_t found_priority = -1;
    clang::MacroInfo *macro_info = nullptr;

    for (clang::ModuleMacro *module_macro :
         m_compiler_instance->getPreprocessor().getLeafModuleMacros(ii)) {
      clang::Module *module = module_macro->getOwningModule();

      {
        ModulePriorityMap::iterator pi =
            module_priorities.find(reinterpret_cast<ModuleID>(module));

        if (pi != module_priorities.end() && pi->second > found_priority) {
          macro_info = module_macro->getMacroInfo();
          found_priority = pi->second;
        }
      }

      clang::Module *top_level_module = module->getTopLevelModule();

      if (top_level_module != module) {
        ModulePriorityMap::iterator pi = module_priorities.find(
            reinterpret_cast<ModuleID>(top_level_module));

        if ((pi != module_priorities.end()) && pi->second > found_priority) {
          macro_info = module_macro->getMacroInfo();
          found_priority = pi->second;
        }
      }
    }

    if (macro_info) {
      std::string macro_expansion = "#define ";
      macro_expansion.append(mi->first->getName().str());

      {
        if (macro_info->isFunctionLike()) {
          macro_expansion.append("(");

          bool first_arg = true;

          for (auto pi = macro_info->param_begin(),
                    pe = macro_info->param_end();
               pi != pe; ++pi) {
            if (!first_arg) {
              macro_expansion.append(", ");
            } else {
              first_arg = false;
            }

            macro_expansion.append((*pi)->getName().str());
          }

          if (macro_info->isC99Varargs()) {
            if (first_arg) {
              macro_expansion.append("...");
            } else {
              macro_expansion.append(", ...");
            }
          } else if (macro_info->isGNUVarargs()) {
            macro_expansion.append("...");
          }

          macro_expansion.append(")");
        }

        macro_expansion.append(" ");

        bool first_token = true;

        for (clang::MacroInfo::tokens_iterator ti = macro_info->tokens_begin(),
                                               te = macro_info->tokens_end();
             ti != te; ++ti) {
          if (!first_token) {
            macro_expansion.append(" ");
          } else {
            first_token = false;
          }

          if (ti->isLiteral()) {
            if (const char *literal_data = ti->getLiteralData()) {
              std::string token_str(literal_data, ti->getLength());
              macro_expansion.append(token_str);
            } else {
              bool invalid = false;
              const char *literal_source =
                  m_compiler_instance->getSourceManager().getCharacterData(
                      ti->getLocation(), &invalid);

              if (invalid) {
                lldbassert(0 && "Unhandled token kind");
                macro_expansion.append("<unknown literal value>");
              } else {
                macro_expansion.append(
                    std::string(literal_source, ti->getLength()));
              }
            }
          } else if (const char *punctuator_spelling =
                         clang::tok::getPunctuatorSpelling(ti->getKind())) {
            macro_expansion.append(punctuator_spelling);
          } else if (const char *keyword_spelling =
                         clang::tok::getKeywordSpelling(ti->getKind())) {
            macro_expansion.append(keyword_spelling);
          } else {
            switch (ti->getKind()) {
            case clang::tok::TokenKind::identifier:
              macro_expansion.append(ti->getIdentifierInfo()->getName().str());
              break;
            case clang::tok::TokenKind::raw_identifier:
              macro_expansion.append(ti->getRawIdentifier().str());
              break;
            default:
              macro_expansion.append(ti->getName());
              break;
            }
          }
        }

        if (handler(macro_expansion)) {
          return;
        }
      }
    }
  }
}

clang::ModuleLoadResult
ClangModulesDeclVendorImpl::DoGetModule(clang::ModuleIdPath path,
                                        bool make_visible) {
  clang::Module::NameVisibilityKind visibility =
      make_visible ? clang::Module::AllVisible : clang::Module::Hidden;

  const bool is_inclusion_directive = false;

  return m_compiler_instance->loadModule(path.front().second, path, visibility,
                                         is_inclusion_directive);
}

clang::ExternalASTMerger::ImporterSource
ClangModulesDeclVendorImpl::GetImporterSource() {
  return {m_compiler_instance->getASTContext(),
          m_compiler_instance->getFileManager(), m_origin_map};
}

static const char *ModuleImportBufferName = "LLDBModulesMemoryBuffer";

lldb_private::ClangModulesDeclVendor *
ClangModulesDeclVendor::Create(Target &target) {
  // FIXME we should insure programmatically that the expression parser's
  // compiler and the modules runtime's
  // compiler are both initialized in the same way – preferably by the same
  // code.

  if (!target.GetPlatform()->SupportsModules())
    return nullptr;

  const ArchSpec &arch = target.GetArchitecture();

  std::vector<std::string> compiler_invocation_arguments = {
      "clang",
      "-fmodules",
      "-fimplicit-module-maps",
      "-fcxx-modules",
      "-fsyntax-only",
      "-femit-all-decls",
      "-target",
      arch.GetTriple().str(),
      "-fmodules-validate-system-headers",
      "-Werror=non-modular-include-in-framework-module"};

  target.GetPlatform()->AddClangModuleCompilationOptions(
      &target, compiler_invocation_arguments);

  compiler_invocation_arguments.push_back(ModuleImportBufferName);

  // Add additional search paths with { "-I", path } or { "-F", path } here.

  {
    llvm::SmallString<128> path;
    auto props = ModuleList::GetGlobalModuleListProperties();
    props.GetClangModulesCachePath().GetPath(path);
    std::string module_cache_argument("-fmodules-cache-path=");
    module_cache_argument.append(path.str());
    compiler_invocation_arguments.push_back(module_cache_argument);
  }

  FileSpecList &module_search_paths = target.GetClangModuleSearchPaths();

  for (size_t spi = 0, spe = module_search_paths.GetSize(); spi < spe; ++spi) {
    const FileSpec &search_path = module_search_paths.GetFileSpecAtIndex(spi);

    std::string search_path_argument = "-I";
    search_path_argument.append(search_path.GetPath());

    compiler_invocation_arguments.push_back(search_path_argument);
  }

  {
    FileSpec clang_resource_dir = GetClangResourceDir();

    if (FileSystem::Instance().IsDirectory(clang_resource_dir.GetPath())) {
      compiler_invocation_arguments.push_back("-resource-dir");
      compiler_invocation_arguments.push_back(clang_resource_dir.GetPath());
    }
  }

  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diagnostics_engine =
      clang::CompilerInstance::createDiagnostics(new clang::DiagnosticOptions,
                                                 new StoringDiagnosticConsumer);

  std::vector<const char *> compiler_invocation_argument_cstrs;
  compiler_invocation_argument_cstrs.reserve(
      compiler_invocation_arguments.size());
  for (const std::string &arg : compiler_invocation_arguments) {
    compiler_invocation_argument_cstrs.push_back(arg.c_str());
  }

  std::shared_ptr<clang::CompilerInvocation> invocation =
      clang::createInvocationFromCommandLine(compiler_invocation_argument_cstrs,
                                             diagnostics_engine);

  if (!invocation)
    return nullptr;

  std::unique_ptr<llvm::MemoryBuffer> source_buffer =
      llvm::MemoryBuffer::getMemBuffer(
          "extern int __lldb __attribute__((unavailable));",
          ModuleImportBufferName);

  invocation->getPreprocessorOpts().addRemappedFile(ModuleImportBufferName,
                                                    source_buffer.release());

  std::unique_ptr<clang::CompilerInstance> instance(
      new clang::CompilerInstance);

  instance->setDiagnostics(diagnostics_engine.get());
  instance->setInvocation(invocation);

  std::unique_ptr<clang::FrontendAction> action(new clang::SyntaxOnlyAction);

  instance->setTarget(clang::TargetInfo::CreateTargetInfo(
      *diagnostics_engine, instance->getInvocation().TargetOpts));

  if (!instance->hasTarget())
    return nullptr;

  instance->getTarget().adjust(instance->getLangOpts());

  if (!action->BeginSourceFile(*instance,
                               instance->getFrontendOpts().Inputs[0]))
    return nullptr;

  instance->getPreprocessor().enableIncrementalProcessing();

  instance->createModuleManager();

  instance->createSema(action->getTranslationUnitKind(), nullptr);

  const bool skipFunctionBodies = false;
  std::unique_ptr<clang::Parser> parser(new clang::Parser(
      instance->getPreprocessor(), instance->getSema(), skipFunctionBodies));

  instance->getPreprocessor().EnterMainSourceFile();
  parser->Initialize();

  clang::Parser::DeclGroupPtrTy parsed;

  while (!parser->ParseTopLevelDecl(parsed))
    ;

  return new ClangModulesDeclVendorImpl(std::move(diagnostics_engine),
                                        std::move(invocation),
                                        std::move(instance), std::move(parser));
}
