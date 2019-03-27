//===-- ClangExpressionDeclMap.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangExpressionDeclMap.h"

#include "ASTDumper.h"
#include "ClangASTSource.h"
#include "ClangModulesDeclVendor.h"
#include "ClangPersistentVariables.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/CPPLanguageRuntime.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"

using namespace lldb;
using namespace lldb_private;
using namespace clang;

namespace {
const char *g_lldb_local_vars_namespace_cstr = "$__lldb_local_vars";
} // anonymous namespace

ClangExpressionDeclMap::ClangExpressionDeclMap(
    bool keep_result_in_memory,
    Materializer::PersistentVariableDelegate *result_delegate,
    ExecutionContext &exe_ctx)
    : ClangASTSource(exe_ctx.GetTargetSP()), m_found_entities(),
      m_struct_members(), m_keep_result_in_memory(keep_result_in_memory),
      m_result_delegate(result_delegate), m_parser_vars(), m_struct_vars() {
  EnableStructVars();
}

ClangExpressionDeclMap::~ClangExpressionDeclMap() {
  // Note: The model is now that the parser's AST context and all associated
  //   data does not vanish until the expression has been executed.  This means
  //   that valuable lookup data (like namespaces) doesn't vanish, but

  DidParse();
  DisableStructVars();
}

bool ClangExpressionDeclMap::WillParse(ExecutionContext &exe_ctx,
                                       Materializer *materializer) {
  ClangASTMetrics::ClearLocalCounters();

  EnableParserVars();
  m_parser_vars->m_exe_ctx = exe_ctx;

  Target *target = exe_ctx.GetTargetPtr();
  if (exe_ctx.GetFramePtr())
    m_parser_vars->m_sym_ctx =
        exe_ctx.GetFramePtr()->GetSymbolContext(lldb::eSymbolContextEverything);
  else if (exe_ctx.GetThreadPtr() &&
           exe_ctx.GetThreadPtr()->GetStackFrameAtIndex(0))
    m_parser_vars->m_sym_ctx =
        exe_ctx.GetThreadPtr()->GetStackFrameAtIndex(0)->GetSymbolContext(
            lldb::eSymbolContextEverything);
  else if (exe_ctx.GetProcessPtr()) {
    m_parser_vars->m_sym_ctx.Clear(true);
    m_parser_vars->m_sym_ctx.target_sp = exe_ctx.GetTargetSP();
  } else if (target) {
    m_parser_vars->m_sym_ctx.Clear(true);
    m_parser_vars->m_sym_ctx.target_sp = exe_ctx.GetTargetSP();
  }

  if (target) {
    m_parser_vars->m_persistent_vars = llvm::cast<ClangPersistentVariables>(
        target->GetPersistentExpressionStateForLanguage(eLanguageTypeC));

    if (!target->GetScratchClangASTContext())
      return false;
  }

  m_parser_vars->m_target_info = GetTargetInfo();
  m_parser_vars->m_materializer = materializer;

  return true;
}

void ClangExpressionDeclMap::InstallCodeGenerator(
    clang::ASTConsumer *code_gen) {
  assert(m_parser_vars);
  m_parser_vars->m_code_gen = code_gen;
}

void ClangExpressionDeclMap::DidParse() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log)
    ClangASTMetrics::DumpCounters(log);

  if (m_parser_vars.get()) {
    for (size_t entity_index = 0, num_entities = m_found_entities.GetSize();
         entity_index < num_entities; ++entity_index) {
      ExpressionVariableSP var_sp(
          m_found_entities.GetVariableAtIndex(entity_index));
      if (var_sp)
        llvm::cast<ClangExpressionVariable>(var_sp.get())
            ->DisableParserVars(GetParserID());
    }

    for (size_t pvar_index = 0,
                num_pvars = m_parser_vars->m_persistent_vars->GetSize();
         pvar_index < num_pvars; ++pvar_index) {
      ExpressionVariableSP pvar_sp(
          m_parser_vars->m_persistent_vars->GetVariableAtIndex(pvar_index));
      if (ClangExpressionVariable *clang_var =
              llvm::dyn_cast<ClangExpressionVariable>(pvar_sp.get()))
        clang_var->DisableParserVars(GetParserID());
    }

    DisableParserVars();
  }
}

// Interface for IRForTarget

ClangExpressionDeclMap::TargetInfo ClangExpressionDeclMap::GetTargetInfo() {
  assert(m_parser_vars.get());

  TargetInfo ret;

  ExecutionContext &exe_ctx = m_parser_vars->m_exe_ctx;

  Process *process = exe_ctx.GetProcessPtr();
  if (process) {
    ret.byte_order = process->GetByteOrder();
    ret.address_byte_size = process->GetAddressByteSize();
  } else {
    Target *target = exe_ctx.GetTargetPtr();
    if (target) {
      ret.byte_order = target->GetArchitecture().GetByteOrder();
      ret.address_byte_size = target->GetArchitecture().GetAddressByteSize();
    }
  }

  return ret;
}

namespace {
/// This class walks an AST and ensures that all DeclContexts defined inside the
/// current source file are properly complete.
///
/// This is used to ensure that persistent types defined in the current source
/// file migrate completely to the persistent AST context before they are
/// reused.  If that didn't happen, it would be impoossible to complete them
/// because their origin would be gone.
///
/// The stragtegy used by this class is to check the SourceLocation (to be
/// specific, the FileID) and see if it's the FileID for the current expression.
/// Alternate strategies could include checking whether an ExternalASTMerger,
/// set up to not have the current context as a source, can find an original for
/// the type.
class Completer : public clang::RecursiveASTVisitor<Completer> {
private:
  clang::ASTImporter &m_exporter;             /// Used to import Decl contents
  clang::FileID m_file;                       /// The file that's going away
  llvm::DenseSet<clang::Decl *> m_completed;  /// Visited Decls, to avoid cycles

  bool ImportAndCheckCompletable(clang::Decl *decl) {
    (void)m_exporter.Import(decl);
    if (m_completed.count(decl))
      return false;
    if (!llvm::isa<DeclContext>(decl))
      return false;
    const clang::SourceLocation loc = decl->getLocation();
    if (!loc.isValid())
      return false;
    const clang::FileID file =
        m_exporter.getFromContext().getSourceManager().getFileID(loc);
    if (file != m_file)
      return false;
    // We are assuming the Decl was parsed in this very expression, so it
    // should not have external storage.
    lldbassert(!llvm::cast<DeclContext>(decl)->hasExternalLexicalStorage());
    return true;
  }

  void Complete(clang::Decl *decl) {
    m_completed.insert(decl);
    auto *decl_context = llvm::cast<DeclContext>(decl);
    (void)m_exporter.Import(decl);
    m_exporter.CompleteDecl(decl);
    for (Decl *child : decl_context->decls())
      if (ImportAndCheckCompletable(child))
        Complete(child);
  }

  void MaybeComplete(clang::Decl *decl) {
    if (ImportAndCheckCompletable(decl))
      Complete(decl);
  }

public:
  Completer(clang::ASTImporter &exporter, clang::FileID file)
      : m_exporter(exporter), m_file(file) {}
  
  // Implements the RecursiveASTVisitor's core API.  It is called on each Decl
  // that the RecursiveASTVisitor encounters, and returns true if the traversal
  // should continue.
  bool VisitDecl(clang::Decl *decl) {
    MaybeComplete(decl);
    return true;
  }
};
}

static void CompleteAllDeclContexts(clang::ASTImporter &exporter,
                                    clang::FileID file,
                                    clang::QualType root) {
  clang::QualType canonical_type = root.getCanonicalType();
  if (clang::TagDecl *tag_decl = canonical_type->getAsTagDecl()) {
    Completer(exporter, file).TraverseDecl(tag_decl);
  } else if (auto interface_type = llvm::dyn_cast<ObjCInterfaceType>(
                 canonical_type.getTypePtr())) {
    Completer(exporter, file).TraverseDecl(interface_type->getDecl());
  } else {
    Completer(exporter, file).TraverseType(canonical_type);
  }
}

static clang::QualType ExportAllDeclaredTypes(
    clang::ExternalASTMerger &merger,
    clang::ASTContext &source, clang::FileManager &source_file_manager,
    const clang::ExternalASTMerger::OriginMap &source_origin_map,
    clang::FileID file, clang::QualType root) {
  clang::ExternalASTMerger::ImporterSource importer_source =
      { source, source_file_manager, source_origin_map };
  merger.AddSources(importer_source);
  clang::ASTImporter &exporter = merger.ImporterForOrigin(source);
  CompleteAllDeclContexts(exporter, file, root);
  clang::QualType ret = exporter.Import(root);
  merger.RemoveSources(importer_source);
  return ret;
}

TypeFromUser ClangExpressionDeclMap::DeportType(ClangASTContext &target,
                                                ClangASTContext &source,
                                                TypeFromParser parser_type) {
  assert (&target == m_target->GetScratchClangASTContext());
  assert ((TypeSystem*)&source == parser_type.GetTypeSystem());
  assert (source.getASTContext() == m_ast_context);
  
  if (m_ast_importer_sp) {
    return TypeFromUser(m_ast_importer_sp->DeportType(
                            target.getASTContext(), source.getASTContext(),
                            parser_type.GetOpaqueQualType()),
                        &target);
  } else if (m_merger_up) {
    clang::FileID source_file =
        source.getASTContext()->getSourceManager().getFileID(
            source.getASTContext()->getTranslationUnitDecl()->getLocation());
    auto scratch_ast_context = static_cast<ClangASTContextForExpressions*>(
        m_target->GetScratchClangASTContext());
    clang::QualType exported_type = ExportAllDeclaredTypes(
        scratch_ast_context->GetMergerUnchecked(),
        *source.getASTContext(), *source.getFileManager(),
        m_merger_up->GetOrigins(),
        source_file,
        clang::QualType::getFromOpaquePtr(parser_type.GetOpaqueQualType()));
    return TypeFromUser(exported_type.getAsOpaquePtr(), &target);
  } else {
    lldbassert(0 && "No mechanism for deporting a type!");
    return TypeFromUser();
  }
}

bool ClangExpressionDeclMap::AddPersistentVariable(const NamedDecl *decl,
                                                   const ConstString &name,
                                                   TypeFromParser parser_type,
                                                   bool is_result,
                                                   bool is_lvalue) {
  assert(m_parser_vars.get());

  ClangASTContext *ast =
      llvm::dyn_cast_or_null<ClangASTContext>(parser_type.GetTypeSystem());
  if (ast == nullptr)
    return false;

  if (m_parser_vars->m_materializer && is_result) {
    Status err;

    ExecutionContext &exe_ctx = m_parser_vars->m_exe_ctx;
    Target *target = exe_ctx.GetTargetPtr();
    if (target == nullptr)
      return false;

    TypeFromUser user_type =
        DeportType(*target->GetScratchClangASTContext(), *ast, parser_type);

    uint32_t offset = m_parser_vars->m_materializer->AddResultVariable(
        user_type, is_lvalue, m_keep_result_in_memory, m_result_delegate, err);

    ClangExpressionVariable *var = new ClangExpressionVariable(
        exe_ctx.GetBestExecutionContextScope(), name, user_type,
        m_parser_vars->m_target_info.byte_order,
        m_parser_vars->m_target_info.address_byte_size);

    m_found_entities.AddNewlyConstructedVariable(var);

    var->EnableParserVars(GetParserID());

    ClangExpressionVariable::ParserVars *parser_vars =
        var->GetParserVars(GetParserID());

    parser_vars->m_named_decl = decl;
    parser_vars->m_parser_type = parser_type;

    var->EnableJITVars(GetParserID());

    ClangExpressionVariable::JITVars *jit_vars = var->GetJITVars(GetParserID());

    jit_vars->m_offset = offset;

    return true;
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));
  ExecutionContext &exe_ctx = m_parser_vars->m_exe_ctx;
  Target *target = exe_ctx.GetTargetPtr();
  if (target == NULL)
    return false;

  ClangASTContext *context(target->GetScratchClangASTContext());

  TypeFromUser user_type = DeportType(*context, *ast, parser_type);

  if (!user_type.GetOpaqueQualType()) {
    if (log)
      log->Printf("Persistent variable's type wasn't copied successfully");
    return false;
  }

  if (!m_parser_vars->m_target_info.IsValid())
    return false;

  ClangExpressionVariable *var = llvm::cast<ClangExpressionVariable>(
      m_parser_vars->m_persistent_vars
          ->CreatePersistentVariable(
              exe_ctx.GetBestExecutionContextScope(), name, user_type,
              m_parser_vars->m_target_info.byte_order,
              m_parser_vars->m_target_info.address_byte_size)
          .get());

  if (!var)
    return false;

  var->m_frozen_sp->SetHasCompleteType();

  if (is_result)
    var->m_flags |= ClangExpressionVariable::EVNeedsFreezeDry;
  else
    var->m_flags |=
        ClangExpressionVariable::EVKeepInTarget; // explicitly-declared
                                                 // persistent variables should
                                                 // persist

  if (is_lvalue) {
    var->m_flags |= ClangExpressionVariable::EVIsProgramReference;
  } else {
    var->m_flags |= ClangExpressionVariable::EVIsLLDBAllocated;
    var->m_flags |= ClangExpressionVariable::EVNeedsAllocation;
  }

  if (m_keep_result_in_memory) {
    var->m_flags |= ClangExpressionVariable::EVKeepInTarget;
  }

  if (log)
    log->Printf("Created persistent variable with flags 0x%hx", var->m_flags);

  var->EnableParserVars(GetParserID());

  ClangExpressionVariable::ParserVars *parser_vars =
      var->GetParserVars(GetParserID());

  parser_vars->m_named_decl = decl;
  parser_vars->m_parser_type = parser_type;

  return true;
}

bool ClangExpressionDeclMap::AddValueToStruct(const NamedDecl *decl,
                                              const ConstString &name,
                                              llvm::Value *value, size_t size,
                                              lldb::offset_t alignment) {
  assert(m_struct_vars.get());
  assert(m_parser_vars.get());

  bool is_persistent_variable = false;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  m_struct_vars->m_struct_laid_out = false;

  if (ClangExpressionVariable::FindVariableInList(m_struct_members, decl,
                                                  GetParserID()))
    return true;

  ClangExpressionVariable *var(ClangExpressionVariable::FindVariableInList(
      m_found_entities, decl, GetParserID()));

  if (!var) {
    var = ClangExpressionVariable::FindVariableInList(
        *m_parser_vars->m_persistent_vars, decl, GetParserID());
    is_persistent_variable = true;
  }

  if (!var)
    return false;

  if (log)
    log->Printf("Adding value for (NamedDecl*)%p [%s - %s] to the structure",
                static_cast<const void *>(decl), name.GetCString(),
                var->GetName().GetCString());

  // We know entity->m_parser_vars is valid because we used a parser variable
  // to find it

  ClangExpressionVariable::ParserVars *parser_vars =
      llvm::cast<ClangExpressionVariable>(var)->GetParserVars(GetParserID());

  parser_vars->m_llvm_value = value;

  if (ClangExpressionVariable::JITVars *jit_vars =
          llvm::cast<ClangExpressionVariable>(var)->GetJITVars(GetParserID())) {
    // We already laid this out; do not touch

    if (log)
      log->Printf("Already placed at 0x%llx",
                  (unsigned long long)jit_vars->m_offset);
  }

  llvm::cast<ClangExpressionVariable>(var)->EnableJITVars(GetParserID());

  ClangExpressionVariable::JITVars *jit_vars =
      llvm::cast<ClangExpressionVariable>(var)->GetJITVars(GetParserID());

  jit_vars->m_alignment = alignment;
  jit_vars->m_size = size;

  m_struct_members.AddVariable(var->shared_from_this());

  if (m_parser_vars->m_materializer) {
    uint32_t offset = 0;

    Status err;

    if (is_persistent_variable) {
      ExpressionVariableSP var_sp(var->shared_from_this());
      offset = m_parser_vars->m_materializer->AddPersistentVariable(
          var_sp, nullptr, err);
    } else {
      if (const lldb_private::Symbol *sym = parser_vars->m_lldb_sym)
        offset = m_parser_vars->m_materializer->AddSymbol(*sym, err);
      else if (const RegisterInfo *reg_info = var->GetRegisterInfo())
        offset = m_parser_vars->m_materializer->AddRegister(*reg_info, err);
      else if (parser_vars->m_lldb_var)
        offset = m_parser_vars->m_materializer->AddVariable(
            parser_vars->m_lldb_var, err);
    }

    if (!err.Success())
      return false;

    if (log)
      log->Printf("Placed at 0x%llx", (unsigned long long)offset);

    jit_vars->m_offset =
        offset; // TODO DoStructLayout() should not change this.
  }

  return true;
}

bool ClangExpressionDeclMap::DoStructLayout() {
  assert(m_struct_vars.get());

  if (m_struct_vars->m_struct_laid_out)
    return true;

  if (!m_parser_vars->m_materializer)
    return false;

  m_struct_vars->m_struct_alignment =
      m_parser_vars->m_materializer->GetStructAlignment();
  m_struct_vars->m_struct_size =
      m_parser_vars->m_materializer->GetStructByteSize();
  m_struct_vars->m_struct_laid_out = true;
  return true;
}

bool ClangExpressionDeclMap::GetStructInfo(uint32_t &num_elements, size_t &size,
                                           lldb::offset_t &alignment) {
  assert(m_struct_vars.get());

  if (!m_struct_vars->m_struct_laid_out)
    return false;

  num_elements = m_struct_members.GetSize();
  size = m_struct_vars->m_struct_size;
  alignment = m_struct_vars->m_struct_alignment;

  return true;
}

bool ClangExpressionDeclMap::GetStructElement(const NamedDecl *&decl,
                                              llvm::Value *&value,
                                              lldb::offset_t &offset,
                                              ConstString &name,
                                              uint32_t index) {
  assert(m_struct_vars.get());

  if (!m_struct_vars->m_struct_laid_out)
    return false;

  if (index >= m_struct_members.GetSize())
    return false;

  ExpressionVariableSP member_sp(m_struct_members.GetVariableAtIndex(index));

  if (!member_sp)
    return false;

  ClangExpressionVariable::ParserVars *parser_vars =
      llvm::cast<ClangExpressionVariable>(member_sp.get())
          ->GetParserVars(GetParserID());
  ClangExpressionVariable::JITVars *jit_vars =
      llvm::cast<ClangExpressionVariable>(member_sp.get())
          ->GetJITVars(GetParserID());

  if (!parser_vars || !jit_vars || !member_sp->GetValueObject())
    return false;

  decl = parser_vars->m_named_decl;
  value = parser_vars->m_llvm_value;
  offset = jit_vars->m_offset;
  name = member_sp->GetName();

  return true;
}

bool ClangExpressionDeclMap::GetFunctionInfo(const NamedDecl *decl,
                                             uint64_t &ptr) {
  ClangExpressionVariable *entity(ClangExpressionVariable::FindVariableInList(
      m_found_entities, decl, GetParserID()));

  if (!entity)
    return false;

  // We know m_parser_vars is valid since we searched for the variable by its
  // NamedDecl

  ClangExpressionVariable::ParserVars *parser_vars =
      entity->GetParserVars(GetParserID());

  ptr = parser_vars->m_lldb_value.GetScalar().ULongLong();

  return true;
}

addr_t ClangExpressionDeclMap::GetSymbolAddress(Target &target,
                                                Process *process,
                                                const ConstString &name,
                                                lldb::SymbolType symbol_type,
                                                lldb_private::Module *module) {
  SymbolContextList sc_list;

  if (module)
    module->FindSymbolsWithNameAndType(name, symbol_type, sc_list);
  else
    target.GetImages().FindSymbolsWithNameAndType(name, symbol_type, sc_list);

  const uint32_t num_matches = sc_list.GetSize();
  addr_t symbol_load_addr = LLDB_INVALID_ADDRESS;

  for (uint32_t i = 0;
       i < num_matches &&
       (symbol_load_addr == 0 || symbol_load_addr == LLDB_INVALID_ADDRESS);
       i++) {
    SymbolContext sym_ctx;
    sc_list.GetContextAtIndex(i, sym_ctx);

    const Address sym_address = sym_ctx.symbol->GetAddress();

    if (!sym_address.IsValid())
      continue;

    switch (sym_ctx.symbol->GetType()) {
    case eSymbolTypeCode:
    case eSymbolTypeTrampoline:
      symbol_load_addr = sym_address.GetCallableLoadAddress(&target);
      break;

    case eSymbolTypeResolver:
      symbol_load_addr = sym_address.GetCallableLoadAddress(&target, true);
      break;

    case eSymbolTypeReExported: {
      ConstString reexport_name = sym_ctx.symbol->GetReExportedSymbolName();
      if (reexport_name) {
        ModuleSP reexport_module_sp;
        ModuleSpec reexport_module_spec;
        reexport_module_spec.GetPlatformFileSpec() =
            sym_ctx.symbol->GetReExportedSymbolSharedLibrary();
        if (reexport_module_spec.GetPlatformFileSpec()) {
          reexport_module_sp =
              target.GetImages().FindFirstModule(reexport_module_spec);
          if (!reexport_module_sp) {
            reexport_module_spec.GetPlatformFileSpec().GetDirectory().Clear();
            reexport_module_sp =
                target.GetImages().FindFirstModule(reexport_module_spec);
          }
        }
        symbol_load_addr = GetSymbolAddress(
            target, process, sym_ctx.symbol->GetReExportedSymbolName(),
            symbol_type, reexport_module_sp.get());
      }
    } break;

    case eSymbolTypeData:
    case eSymbolTypeRuntime:
    case eSymbolTypeVariable:
    case eSymbolTypeLocal:
    case eSymbolTypeParam:
    case eSymbolTypeInvalid:
    case eSymbolTypeAbsolute:
    case eSymbolTypeException:
    case eSymbolTypeSourceFile:
    case eSymbolTypeHeaderFile:
    case eSymbolTypeObjectFile:
    case eSymbolTypeCommonBlock:
    case eSymbolTypeBlock:
    case eSymbolTypeVariableType:
    case eSymbolTypeLineEntry:
    case eSymbolTypeLineHeader:
    case eSymbolTypeScopeBegin:
    case eSymbolTypeScopeEnd:
    case eSymbolTypeAdditional:
    case eSymbolTypeCompiler:
    case eSymbolTypeInstrumentation:
    case eSymbolTypeUndefined:
    case eSymbolTypeObjCClass:
    case eSymbolTypeObjCMetaClass:
    case eSymbolTypeObjCIVar:
      symbol_load_addr = sym_address.GetLoadAddress(&target);
      break;
    }
  }

  if (symbol_load_addr == LLDB_INVALID_ADDRESS && process) {
    ObjCLanguageRuntime *runtime = process->GetObjCLanguageRuntime();

    if (runtime) {
      symbol_load_addr = runtime->LookupRuntimeSymbol(name);
    }
  }

  return symbol_load_addr;
}

addr_t ClangExpressionDeclMap::GetSymbolAddress(const ConstString &name,
                                                lldb::SymbolType symbol_type) {
  assert(m_parser_vars.get());

  if (!m_parser_vars->m_exe_ctx.GetTargetPtr())
    return false;

  return GetSymbolAddress(m_parser_vars->m_exe_ctx.GetTargetRef(),
                          m_parser_vars->m_exe_ctx.GetProcessPtr(), name,
                          symbol_type);
}

lldb::VariableSP ClangExpressionDeclMap::FindGlobalVariable(
    Target &target, ModuleSP &module, const ConstString &name,
    CompilerDeclContext *namespace_decl, TypeFromUser *type) {
  VariableList vars;

  if (module && namespace_decl)
    module->FindGlobalVariables(name, namespace_decl, -1, vars);
  else
    target.GetImages().FindGlobalVariables(name, -1, vars);

  if (vars.GetSize()) {
    if (type) {
      for (size_t i = 0; i < vars.GetSize(); ++i) {
        VariableSP var_sp = vars.GetVariableAtIndex(i);

        if (ClangASTContext::AreTypesSame(
                *type, var_sp->GetType()->GetFullCompilerType()))
          return var_sp;
      }
    } else {
      return vars.GetVariableAtIndex(0);
    }
  }

  return VariableSP();
}

ClangASTContext *ClangExpressionDeclMap::GetClangASTContext() {
  StackFrame *frame = m_parser_vars->m_exe_ctx.GetFramePtr();
  if (frame == nullptr)
    return nullptr;

  SymbolContext sym_ctx = frame->GetSymbolContext(lldb::eSymbolContextFunction |
                                                  lldb::eSymbolContextBlock);
  if (sym_ctx.block == nullptr)
    return nullptr;

  CompilerDeclContext frame_decl_context = sym_ctx.block->GetDeclContext();
  if (!frame_decl_context)
    return nullptr;

  return llvm::dyn_cast_or_null<ClangASTContext>(
      frame_decl_context.GetTypeSystem());
}

// Interface for ClangASTSource

void ClangExpressionDeclMap::FindExternalVisibleDecls(
    NameSearchContext &context) {
  assert(m_ast_context);

  ClangASTMetrics::RegisterVisibleQuery();

  const ConstString name(context.m_decl_name.getAsString().c_str());

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (GetImportInProgress()) {
    if (log && log->GetVerbose())
      log->Printf("Ignoring a query during an import");
    return;
  }

  static unsigned int invocation_id = 0;
  unsigned int current_id = invocation_id++;

  if (log) {
    if (!context.m_decl_context)
      log->Printf("ClangExpressionDeclMap::FindExternalVisibleDecls[%u] for "
                  "'%s' in a NULL DeclContext",
                  current_id, name.GetCString());
    else if (const NamedDecl *context_named_decl =
                 dyn_cast<NamedDecl>(context.m_decl_context))
      log->Printf("ClangExpressionDeclMap::FindExternalVisibleDecls[%u] for "
                  "'%s' in '%s'",
                  current_id, name.GetCString(),
                  context_named_decl->getNameAsString().c_str());
    else
      log->Printf("ClangExpressionDeclMap::FindExternalVisibleDecls[%u] for "
                  "'%s' in a '%s'",
                  current_id, name.GetCString(),
                  context.m_decl_context->getDeclKindName());
  }

  if (const NamespaceDecl *namespace_context =
          dyn_cast<NamespaceDecl>(context.m_decl_context)) {
    if (namespace_context->getName().str() ==
        std::string(g_lldb_local_vars_namespace_cstr)) {
      CompilerDeclContext compiler_decl_ctx(
          GetClangASTContext(), const_cast<void *>(static_cast<const void *>(
                                    context.m_decl_context)));
      FindExternalVisibleDecls(context, lldb::ModuleSP(), compiler_decl_ctx,
                               current_id);
      return;
    }

    ClangASTImporter::NamespaceMapSP namespace_map =
        m_ast_importer_sp
            ? m_ast_importer_sp->GetNamespaceMap(namespace_context)
            : ClangASTImporter::NamespaceMapSP();

    if (!namespace_map)
      return;

    if (log && log->GetVerbose())
      log->Printf("  CEDM::FEVD[%u] Inspecting (NamespaceMap*)%p (%d entries)",
                  current_id, static_cast<void *>(namespace_map.get()),
                  (int)namespace_map->size());
    
    for (ClangASTImporter::NamespaceMap::iterator i = namespace_map->begin(),
                                                  e = namespace_map->end();
         i != e; ++i) {
      if (log)
        log->Printf("  CEDM::FEVD[%u] Searching namespace %s in module %s",
                    current_id, i->second.GetName().AsCString(),
                    i->first->GetFileSpec().GetFilename().GetCString());

      FindExternalVisibleDecls(context, i->first, i->second, current_id);
    }
  } else if (isa<TranslationUnitDecl>(context.m_decl_context)) {
    CompilerDeclContext namespace_decl;

    if (log)
      log->Printf("  CEDM::FEVD[%u] Searching the root namespace", current_id);

    FindExternalVisibleDecls(context, lldb::ModuleSP(), namespace_decl,
                             current_id);
  }
  
  ClangASTSource::FindExternalVisibleDecls(context);
}

void ClangExpressionDeclMap::FindExternalVisibleDecls(
    NameSearchContext &context, lldb::ModuleSP module_sp,
    CompilerDeclContext &namespace_decl, unsigned int current_id) {
  assert(m_ast_context);

  std::function<void(clang::FunctionDecl *)> MaybeRegisterFunctionBody =
      [this](clang::FunctionDecl *copied_function_decl) {
        if (copied_function_decl->getBody() && m_parser_vars->m_code_gen) {
          DeclGroupRef decl_group_ref(copied_function_decl);
          m_parser_vars->m_code_gen->HandleTopLevelDecl(decl_group_ref);
        }
      };

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  SymbolContextList sc_list;

  const ConstString name(context.m_decl_name.getAsString().c_str());
  if (IgnoreName(name, false))
    return;

  // Only look for functions by name out in our symbols if the function doesn't
  // start with our phony prefix of '$'
  Target *target = m_parser_vars->m_exe_ctx.GetTargetPtr();
  StackFrame *frame = m_parser_vars->m_exe_ctx.GetFramePtr();
  SymbolContext sym_ctx;
  if (frame != nullptr)
    sym_ctx = frame->GetSymbolContext(lldb::eSymbolContextFunction |
                                      lldb::eSymbolContextBlock);

  // Try the persistent decls, which take precedence over all else.
  if (!namespace_decl) {
    do {
      if (!target)
        break;

      ClangASTContext *scratch_clang_ast_context =
          target->GetScratchClangASTContext();

      if (!scratch_clang_ast_context)
        break;

      ASTContext *scratch_ast_context =
          scratch_clang_ast_context->getASTContext();

      if (!scratch_ast_context)
        break;

      NamedDecl *persistent_decl =
          m_parser_vars->m_persistent_vars->GetPersistentDecl(name);

      if (!persistent_decl)
        break;

      Decl *parser_persistent_decl = CopyDecl(persistent_decl);

      if (!parser_persistent_decl)
        break;

      NamedDecl *parser_named_decl =
          dyn_cast<NamedDecl>(parser_persistent_decl);

      if (!parser_named_decl)
        break;

      if (clang::FunctionDecl *parser_function_decl =
              llvm::dyn_cast<clang::FunctionDecl>(parser_named_decl)) {
        MaybeRegisterFunctionBody(parser_function_decl);
      }

      if (log)
        log->Printf("  CEDM::FEVD[%u] Found persistent decl %s", current_id,
                    name.GetCString());

      context.AddNamedDecl(parser_named_decl);
    } while (0);
  }

  if (name.GetCString()[0] == '$' && !namespace_decl) {
    static ConstString g_lldb_class_name("$__lldb_class");

    if (name == g_lldb_class_name) {
      // Clang is looking for the type of "this"

      if (frame == NULL)
        return;

      // Find the block that defines the function represented by "sym_ctx"
      Block *function_block = sym_ctx.GetFunctionBlock();

      if (!function_block)
        return;

      CompilerDeclContext function_decl_ctx = function_block->GetDeclContext();

      if (!function_decl_ctx)
        return;

      clang::CXXMethodDecl *method_decl =
          ClangASTContext::DeclContextGetAsCXXMethodDecl(function_decl_ctx);

      if (method_decl) {
        clang::CXXRecordDecl *class_decl = method_decl->getParent();

        QualType class_qual_type(class_decl->getTypeForDecl(), 0);

        TypeFromUser class_user_type(
            class_qual_type.getAsOpaquePtr(),
            ClangASTContext::GetASTContext(&class_decl->getASTContext()));

        if (log) {
          ASTDumper ast_dumper(class_qual_type);
          log->Printf("  CEDM::FEVD[%u] Adding type for $__lldb_class: %s",
                      current_id, ast_dumper.GetCString());
        }

        AddThisType(context, class_user_type, current_id);

        if (method_decl->isInstance()) {
          // self is a pointer to the object

          QualType class_pointer_type =
              method_decl->getASTContext().getPointerType(class_qual_type);

          TypeFromUser self_user_type(
              class_pointer_type.getAsOpaquePtr(),
              ClangASTContext::GetASTContext(&method_decl->getASTContext()));

          m_struct_vars->m_object_pointer_type = self_user_type;
        }
      } else {
        // This branch will get hit if we are executing code in the context of
        // a function that claims to have an object pointer (through
        // DW_AT_object_pointer?) but is not formally a method of the class.
        // In that case, just look up the "this" variable in the current scope
        // and use its type.
        // FIXME: This code is formally correct, but clang doesn't currently
        // emit DW_AT_object_pointer
        // for C++ so it hasn't actually been tested.

        VariableList *vars = frame->GetVariableList(false);

        lldb::VariableSP this_var = vars->FindVariable(ConstString("this"));

        if (this_var && this_var->IsInScope(frame) &&
            this_var->LocationIsValidForFrame(frame)) {
          Type *this_type = this_var->GetType();

          if (!this_type)
            return;

          TypeFromUser pointee_type =
              this_type->GetForwardCompilerType().GetPointeeType();

          if (pointee_type.IsValid()) {
            if (log) {
              ASTDumper ast_dumper(pointee_type);
              log->Printf("  FEVD[%u] Adding type for $__lldb_class: %s",
                          current_id, ast_dumper.GetCString());
            }

            AddThisType(context, pointee_type, current_id);
            TypeFromUser this_user_type(this_type->GetFullCompilerType());
            m_struct_vars->m_object_pointer_type = this_user_type;
            return;
          }
        }
      }

      return;
    }

    static ConstString g_lldb_objc_class_name("$__lldb_objc_class");
    if (name == g_lldb_objc_class_name) {
      // Clang is looking for the type of "*self"

      if (!frame)
        return;

      SymbolContext sym_ctx = frame->GetSymbolContext(
          lldb::eSymbolContextFunction | lldb::eSymbolContextBlock);

      // Find the block that defines the function represented by "sym_ctx"
      Block *function_block = sym_ctx.GetFunctionBlock();

      if (!function_block)
        return;

      CompilerDeclContext function_decl_ctx = function_block->GetDeclContext();

      if (!function_decl_ctx)
        return;

      clang::ObjCMethodDecl *method_decl =
          ClangASTContext::DeclContextGetAsObjCMethodDecl(function_decl_ctx);

      if (method_decl) {
        ObjCInterfaceDecl *self_interface = method_decl->getClassInterface();

        if (!self_interface)
          return;

        const clang::Type *interface_type = self_interface->getTypeForDecl();

        if (!interface_type)
          return; // This is unlikely, but we have seen crashes where this
                  // occurred

        TypeFromUser class_user_type(
            QualType(interface_type, 0).getAsOpaquePtr(),
            ClangASTContext::GetASTContext(&method_decl->getASTContext()));

        if (log) {
          ASTDumper ast_dumper(interface_type);
          log->Printf("  FEVD[%u] Adding type for $__lldb_objc_class: %s",
                      current_id, ast_dumper.GetCString());
        }

        AddOneType(context, class_user_type, current_id);

        if (method_decl->isInstanceMethod()) {
          // self is a pointer to the object

          QualType class_pointer_type =
              method_decl->getASTContext().getObjCObjectPointerType(
                  QualType(interface_type, 0));

          TypeFromUser self_user_type(
              class_pointer_type.getAsOpaquePtr(),
              ClangASTContext::GetASTContext(&method_decl->getASTContext()));

          m_struct_vars->m_object_pointer_type = self_user_type;
        } else {
          // self is a Class pointer
          QualType class_type = method_decl->getASTContext().getObjCClassType();

          TypeFromUser self_user_type(
              class_type.getAsOpaquePtr(),
              ClangASTContext::GetASTContext(&method_decl->getASTContext()));

          m_struct_vars->m_object_pointer_type = self_user_type;
        }

        return;
      } else {
        // This branch will get hit if we are executing code in the context of
        // a function that claims to have an object pointer (through
        // DW_AT_object_pointer?) but is not formally a method of the class.
        // In that case, just look up the "self" variable in the current scope
        // and use its type.

        VariableList *vars = frame->GetVariableList(false);

        lldb::VariableSP self_var = vars->FindVariable(ConstString("self"));

        if (self_var && self_var->IsInScope(frame) &&
            self_var->LocationIsValidForFrame(frame)) {
          Type *self_type = self_var->GetType();

          if (!self_type)
            return;

          CompilerType self_clang_type = self_type->GetFullCompilerType();

          if (ClangASTContext::IsObjCClassType(self_clang_type)) {
            return;
          } else if (ClangASTContext::IsObjCObjectPointerType(
                         self_clang_type)) {
            self_clang_type = self_clang_type.GetPointeeType();

            if (!self_clang_type)
              return;

            if (log) {
              ASTDumper ast_dumper(self_type->GetFullCompilerType());
              log->Printf("  FEVD[%u] Adding type for $__lldb_objc_class: %s",
                          current_id, ast_dumper.GetCString());
            }

            TypeFromUser class_user_type(self_clang_type);

            AddOneType(context, class_user_type, current_id);

            TypeFromUser self_user_type(self_type->GetFullCompilerType());

            m_struct_vars->m_object_pointer_type = self_user_type;
            return;
          }
        }
      }

      return;
    }

    if (name == ConstString(g_lldb_local_vars_namespace_cstr)) {
      CompilerDeclContext frame_decl_context =
          sym_ctx.block != nullptr ? sym_ctx.block->GetDeclContext()
                                   : CompilerDeclContext();

      if (frame_decl_context) {
        ClangASTContext *ast = llvm::dyn_cast_or_null<ClangASTContext>(
            frame_decl_context.GetTypeSystem());

        if (ast) {
          clang::NamespaceDecl *namespace_decl =
              ClangASTContext::GetUniqueNamespaceDeclaration(
                  m_ast_context, name.GetCString(), nullptr);
          if (namespace_decl) {
            context.AddNamedDecl(namespace_decl);
            clang::DeclContext *clang_decl_ctx =
                clang::Decl::castToDeclContext(namespace_decl);
            clang_decl_ctx->setHasExternalVisibleStorage(true);
            context.m_found.local_vars_nsp = true;
          }
        }
      }

      return;
    }

    // any other $__lldb names should be weeded out now
    if (name.GetStringRef().startswith("$__lldb"))
      return;

    ExpressionVariableSP pvar_sp(
        m_parser_vars->m_persistent_vars->GetVariable(name));

    if (pvar_sp) {
      AddOneVariable(context, pvar_sp, current_id);
      return;
    }

    const char *reg_name(&name.GetCString()[1]);

    if (m_parser_vars->m_exe_ctx.GetRegisterContext()) {
      const RegisterInfo *reg_info(
          m_parser_vars->m_exe_ctx.GetRegisterContext()->GetRegisterInfoByName(
              reg_name));

      if (reg_info) {
        if (log)
          log->Printf("  CEDM::FEVD[%u] Found register %s", current_id,
                      reg_info->name);

        AddOneRegister(context, reg_info, current_id);
      }
    }
  } else {
    ValueObjectSP valobj;
    VariableSP var;

    bool local_var_lookup =
        !namespace_decl || (namespace_decl.GetName() ==
                            ConstString(g_lldb_local_vars_namespace_cstr));
    if (frame && local_var_lookup) {
      CompilerDeclContext compiler_decl_context =
          sym_ctx.block != nullptr ? sym_ctx.block->GetDeclContext()
                                   : CompilerDeclContext();

      if (compiler_decl_context) {
        // Make sure that the variables are parsed so that we have the
        // declarations.
        VariableListSP vars = frame->GetInScopeVariableList(true);
        for (size_t i = 0; i < vars->GetSize(); i++)
          vars->GetVariableAtIndex(i)->GetDecl();

        // Search for declarations matching the name. Do not include imported
        // decls in the search if we are looking for decls in the artificial
        // namespace $__lldb_local_vars.
        std::vector<CompilerDecl> found_decls =
            compiler_decl_context.FindDeclByName(name,
                                                 namespace_decl.IsValid());

        bool variable_found = false;
        for (CompilerDecl decl : found_decls) {
          for (size_t vi = 0, ve = vars->GetSize(); vi != ve; ++vi) {
            VariableSP candidate_var = vars->GetVariableAtIndex(vi);
            if (candidate_var->GetDecl() == decl) {
              var = candidate_var;
              break;
            }
          }

          if (var && !variable_found) {
            variable_found = true;
            valobj = ValueObjectVariable::Create(frame, var);
            AddOneVariable(context, var, valobj, current_id);
            context.m_found.variable = true;
          }
        }
        if (variable_found)
          return;
      }
    }
    if (target) {
      var = FindGlobalVariable(*target, module_sp, name, &namespace_decl, NULL);

      if (var) {
        valobj = ValueObjectVariable::Create(target, var);
        AddOneVariable(context, var, valobj, current_id);
        context.m_found.variable = true;
        return;
      }
    }

    std::vector<clang::NamedDecl *> decls_from_modules;

    if (target) {
      if (ClangModulesDeclVendor *decl_vendor =
              target->GetClangModulesDeclVendor()) {
        decl_vendor->FindDecls(name, false, UINT32_MAX, decls_from_modules);
      }
    }

    const bool include_inlines = false;
    const bool append = false;

    if (namespace_decl && module_sp) {
      const bool include_symbols = false;

      module_sp->FindFunctions(name, &namespace_decl, eFunctionNameTypeBase,
                               include_symbols, include_inlines, append,
                               sc_list);
    } else if (target && !namespace_decl) {
      const bool include_symbols = true;

      // TODO Fix FindFunctions so that it doesn't return
      //   instance methods for eFunctionNameTypeBase.

      target->GetImages().FindFunctions(name, eFunctionNameTypeFull,
                                        include_symbols, include_inlines,
                                        append, sc_list);
    }

    // If we found more than one function, see if we can use the frame's decl
    // context to remove functions that are shadowed by other functions which
    // match in type but are nearer in scope.
    //
    // AddOneFunction will not add a function whose type has already been
    // added, so if there's another function in the list with a matching type,
    // check to see if their decl context is a parent of the current frame's or
    // was imported via a and using statement, and pick the best match
    // according to lookup rules.
    if (sc_list.GetSize() > 1) {
      // Collect some info about our frame's context.
      StackFrame *frame = m_parser_vars->m_exe_ctx.GetFramePtr();
      SymbolContext frame_sym_ctx;
      if (frame != nullptr)
        frame_sym_ctx = frame->GetSymbolContext(lldb::eSymbolContextFunction |
                                                lldb::eSymbolContextBlock);
      CompilerDeclContext frame_decl_context =
          frame_sym_ctx.block != nullptr ? frame_sym_ctx.block->GetDeclContext()
                                         : CompilerDeclContext();

      // We can't do this without a compiler decl context for our frame.
      if (frame_decl_context) {
        clang::DeclContext *frame_decl_ctx =
            (clang::DeclContext *)frame_decl_context.GetOpaqueDeclContext();
        ClangASTContext *ast = llvm::dyn_cast_or_null<ClangASTContext>(
            frame_decl_context.GetTypeSystem());

        // Structure to hold the info needed when comparing function
        // declarations.
        struct FuncDeclInfo {
          ConstString m_name;
          CompilerType m_copied_type;
          uint32_t m_decl_lvl;
          SymbolContext m_sym_ctx;
        };

        // First, symplify things by looping through the symbol contexts to
        // remove unwanted functions and separate out the functions we want to
        // compare and prune into a separate list. Cache the info needed about
        // the function declarations in a vector for efficiency.
        SymbolContextList sc_sym_list;
        uint32_t num_indices = sc_list.GetSize();
        std::vector<FuncDeclInfo> fdi_cache;
        fdi_cache.reserve(num_indices);
        for (uint32_t index = 0; index < num_indices; ++index) {
          FuncDeclInfo fdi;
          SymbolContext sym_ctx;
          sc_list.GetContextAtIndex(index, sym_ctx);

          // We don't know enough about symbols to compare them, but we should
          // keep them in the list.
          Function *function = sym_ctx.function;
          if (!function) {
            sc_sym_list.Append(sym_ctx);
            continue;
          }
          // Filter out functions without declaration contexts, as well as
          // class/instance methods, since they'll be skipped in the code that
          // follows anyway.
          CompilerDeclContext func_decl_context = function->GetDeclContext();
          if (!func_decl_context ||
              func_decl_context.IsClassMethod(nullptr, nullptr, nullptr))
            continue;
          // We can only prune functions for which we can copy the type.
          CompilerType func_clang_type =
              function->GetType()->GetFullCompilerType();
          CompilerType copied_func_type = GuardedCopyType(func_clang_type);
          if (!copied_func_type) {
            sc_sym_list.Append(sym_ctx);
            continue;
          }

          fdi.m_sym_ctx = sym_ctx;
          fdi.m_name = function->GetName();
          fdi.m_copied_type = copied_func_type;
          fdi.m_decl_lvl = LLDB_INVALID_DECL_LEVEL;
          if (fdi.m_copied_type && func_decl_context) {
            // Call CountDeclLevels to get the number of parent scopes we have
            // to look through before we find the function declaration. When
            // comparing functions of the same type, the one with a lower count
            // will be closer to us in the lookup scope and shadows the other.
            clang::DeclContext *func_decl_ctx =
                (clang::DeclContext *)func_decl_context.GetOpaqueDeclContext();
            fdi.m_decl_lvl = ast->CountDeclLevels(
                frame_decl_ctx, func_decl_ctx, &fdi.m_name, &fdi.m_copied_type);
          }
          fdi_cache.emplace_back(fdi);
        }

        // Loop through the functions in our cache looking for matching types,
        // then compare their scope levels to see which is closer.
        std::multimap<CompilerType, const FuncDeclInfo *> matches;
        for (const FuncDeclInfo &fdi : fdi_cache) {
          const CompilerType t = fdi.m_copied_type;
          auto q = matches.find(t);
          if (q != matches.end()) {
            if (q->second->m_decl_lvl > fdi.m_decl_lvl)
              // This function is closer; remove the old set.
              matches.erase(t);
            else if (q->second->m_decl_lvl < fdi.m_decl_lvl)
              // The functions in our set are closer - skip this one.
              continue;
          }
          matches.insert(std::make_pair(t, &fdi));
        }

        // Loop through our matches and add their symbol contexts to our list.
        SymbolContextList sc_func_list;
        for (const auto &q : matches)
          sc_func_list.Append(q.second->m_sym_ctx);

        // Rejoin the lists with the functions in front.
        sc_list = sc_func_list;
        sc_list.Append(sc_sym_list);
      }
    }

    if (sc_list.GetSize()) {
      Symbol *extern_symbol = NULL;
      Symbol *non_extern_symbol = NULL;

      for (uint32_t index = 0, num_indices = sc_list.GetSize();
           index < num_indices; ++index) {
        SymbolContext sym_ctx;
        sc_list.GetContextAtIndex(index, sym_ctx);

        if (sym_ctx.function) {
          CompilerDeclContext decl_ctx = sym_ctx.function->GetDeclContext();

          if (!decl_ctx)
            continue;

          // Filter out class/instance methods.
          if (decl_ctx.IsClassMethod(nullptr, nullptr, nullptr))
            continue;

          AddOneFunction(context, sym_ctx.function, NULL, current_id);
          context.m_found.function_with_type_info = true;
          context.m_found.function = true;
        } else if (sym_ctx.symbol) {
          if (sym_ctx.symbol->GetType() == eSymbolTypeReExported && target) {
            sym_ctx.symbol = sym_ctx.symbol->ResolveReExportedSymbol(*target);
            if (sym_ctx.symbol == NULL)
              continue;
          }

          if (sym_ctx.symbol->IsExternal())
            extern_symbol = sym_ctx.symbol;
          else
            non_extern_symbol = sym_ctx.symbol;
        }
      }

      if (!context.m_found.function_with_type_info) {
        for (clang::NamedDecl *decl : decls_from_modules) {
          if (llvm::isa<clang::FunctionDecl>(decl)) {
            clang::NamedDecl *copied_decl =
                llvm::cast_or_null<FunctionDecl>(CopyDecl(decl));
            if (copied_decl) {
              context.AddNamedDecl(copied_decl);
              context.m_found.function_with_type_info = true;
            }
          }
        }
      }

      if (!context.m_found.function_with_type_info) {
        if (extern_symbol) {
          AddOneFunction(context, NULL, extern_symbol, current_id);
          context.m_found.function = true;
        } else if (non_extern_symbol) {
          AddOneFunction(context, NULL, non_extern_symbol, current_id);
          context.m_found.function = true;
        }
      }
    }

    if (!context.m_found.function_with_type_info) {
      // Try the modules next.

      do {
        if (ClangModulesDeclVendor *modules_decl_vendor =
                m_target->GetClangModulesDeclVendor()) {
          bool append = false;
          uint32_t max_matches = 1;
          std::vector<clang::NamedDecl *> decls;

          if (!modules_decl_vendor->FindDecls(name, append, max_matches, decls))
            break;

          clang::NamedDecl *const decl_from_modules = decls[0];

          if (llvm::isa<clang::FunctionDecl>(decl_from_modules)) {
            if (log) {
              log->Printf("  CAS::FEVD[%u] Matching function found for "
                          "\"%s\" in the modules",
                          current_id, name.GetCString());
            }

            clang::Decl *copied_decl = CopyDecl(decl_from_modules);
            clang::FunctionDecl *copied_function_decl =
                copied_decl ? dyn_cast<clang::FunctionDecl>(copied_decl)
                            : nullptr;

            if (!copied_function_decl) {
              if (log)
                log->Printf("  CAS::FEVD[%u] - Couldn't export a function "
                            "declaration from the modules",
                            current_id);

              break;
            }

            MaybeRegisterFunctionBody(copied_function_decl);

            context.AddNamedDecl(copied_function_decl);

            context.m_found.function_with_type_info = true;
            context.m_found.function = true;
          } else if (llvm::isa<clang::VarDecl>(decl_from_modules)) {
            if (log) {
              log->Printf("  CAS::FEVD[%u] Matching variable found for "
                          "\"%s\" in the modules",
                          current_id, name.GetCString());
            }

            clang::Decl *copied_decl = CopyDecl(decl_from_modules);
            clang::VarDecl *copied_var_decl =
                copied_decl ? dyn_cast_or_null<clang::VarDecl>(copied_decl)
                            : nullptr;

            if (!copied_var_decl) {
              if (log)
                log->Printf("  CAS::FEVD[%u] - Couldn't export a variable "
                            "declaration from the modules",
                            current_id);

              break;
            }

            context.AddNamedDecl(copied_var_decl);

            context.m_found.variable = true;
          }
        }
      } while (0);
    }

    if (target && !context.m_found.variable && !namespace_decl) {
      // We couldn't find a non-symbol variable for this.  Now we'll hunt for a
      // generic data symbol, and -- if it is found -- treat it as a variable.
      Status error;

      const Symbol *data_symbol =
          m_parser_vars->m_sym_ctx.FindBestGlobalDataSymbol(name, error);

      if (!error.Success()) {
        const unsigned diag_id =
            m_ast_context->getDiagnostics().getCustomDiagID(
                clang::DiagnosticsEngine::Level::Error, "%0");
        m_ast_context->getDiagnostics().Report(diag_id) << error.AsCString();
      }

      if (data_symbol) {
        std::string warning("got name from symbols: ");
        warning.append(name.AsCString());
        const unsigned diag_id =
            m_ast_context->getDiagnostics().getCustomDiagID(
                clang::DiagnosticsEngine::Level::Warning, "%0");
        m_ast_context->getDiagnostics().Report(diag_id) << warning.c_str();
        AddOneGenericVariable(context, *data_symbol, current_id);
        context.m_found.variable = true;
      }
    }
  }
}

bool ClangExpressionDeclMap::GetVariableValue(VariableSP &var,
                                              lldb_private::Value &var_location,
                                              TypeFromUser *user_type,
                                              TypeFromParser *parser_type) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  Type *var_type = var->GetType();

  if (!var_type) {
    if (log)
      log->PutCString("Skipped a definition because it has no type");
    return false;
  }

  CompilerType var_clang_type = var_type->GetFullCompilerType();

  if (!var_clang_type) {
    if (log)
      log->PutCString("Skipped a definition because it has no Clang type");
    return false;
  }

  ClangASTContext *clang_ast = llvm::dyn_cast_or_null<ClangASTContext>(
      var_type->GetForwardCompilerType().GetTypeSystem());

  if (!clang_ast) {
    if (log)
      log->PutCString("Skipped a definition because it has no Clang AST");
    return false;
  }

  ASTContext *ast = clang_ast->getASTContext();

  if (!ast) {
    if (log)
      log->PutCString(
          "There is no AST context for the current execution context");
    return false;
  }

  DWARFExpression &var_location_expr = var->LocationExpression();

  Target *target = m_parser_vars->m_exe_ctx.GetTargetPtr();
  Status err;

  if (var->GetLocationIsConstantValueData()) {
    DataExtractor const_value_extractor;

    if (var_location_expr.GetExpressionData(const_value_extractor)) {
      var_location = Value(const_value_extractor.GetDataStart(),
                           const_value_extractor.GetByteSize());
      var_location.SetValueType(Value::eValueTypeHostAddress);
    } else {
      if (log)
        log->Printf("Error evaluating constant variable: %s", err.AsCString());
      return false;
    }
  }

  CompilerType type_to_use = GuardedCopyType(var_clang_type);

  if (!type_to_use) {
    if (log)
      log->Printf(
          "Couldn't copy a variable's type into the parser's AST context");

    return false;
  }

  if (parser_type)
    *parser_type = TypeFromParser(type_to_use);

  if (var_location.GetContextType() == Value::eContextTypeInvalid)
    var_location.SetCompilerType(type_to_use);

  if (var_location.GetValueType() == Value::eValueTypeFileAddress) {
    SymbolContext var_sc;
    var->CalculateSymbolContext(&var_sc);

    if (!var_sc.module_sp)
      return false;

    Address so_addr(var_location.GetScalar().ULongLong(),
                    var_sc.module_sp->GetSectionList());

    lldb::addr_t load_addr = so_addr.GetLoadAddress(target);

    if (load_addr != LLDB_INVALID_ADDRESS) {
      var_location.GetScalar() = load_addr;
      var_location.SetValueType(Value::eValueTypeLoadAddress);
    }
  }

  if (user_type)
    *user_type = TypeFromUser(var_clang_type);

  return true;
}

void ClangExpressionDeclMap::AddOneVariable(NameSearchContext &context,
                                            VariableSP var,
                                            ValueObjectSP valobj,
                                            unsigned int current_id) {
  assert(m_parser_vars.get());

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  TypeFromUser ut;
  TypeFromParser pt;
  Value var_location;

  if (!GetVariableValue(var, var_location, &ut, &pt))
    return;

  clang::QualType parser_opaque_type =
      QualType::getFromOpaquePtr(pt.GetOpaqueQualType());

  if (parser_opaque_type.isNull())
    return;

  if (const clang::Type *parser_type = parser_opaque_type.getTypePtr()) {
    if (const TagType *tag_type = dyn_cast<TagType>(parser_type))
      CompleteType(tag_type->getDecl());
    if (const ObjCObjectPointerType *objc_object_ptr_type =
            dyn_cast<ObjCObjectPointerType>(parser_type))
      CompleteType(objc_object_ptr_type->getInterfaceDecl());
  }

  bool is_reference = pt.IsReferenceType();

  NamedDecl *var_decl = NULL;
  if (is_reference)
    var_decl = context.AddVarDecl(pt);
  else
    var_decl = context.AddVarDecl(pt.GetLValueReferenceType());

  std::string decl_name(context.m_decl_name.getAsString());
  ConstString entity_name(decl_name.c_str());
  ClangExpressionVariable *entity(new ClangExpressionVariable(valobj));
  m_found_entities.AddNewlyConstructedVariable(entity);

  assert(entity);
  entity->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      entity->GetParserVars(GetParserID());
  parser_vars->m_parser_type = pt;
  parser_vars->m_named_decl = var_decl;
  parser_vars->m_llvm_value = NULL;
  parser_vars->m_lldb_value = var_location;
  parser_vars->m_lldb_var = var;

  if (is_reference)
    entity->m_flags |= ClangExpressionVariable::EVTypeIsReference;

  if (log) {
    ASTDumper orig_dumper(ut.GetOpaqueQualType());
    ASTDumper ast_dumper(var_decl);
    log->Printf("  CEDM::FEVD[%u] Found variable %s, returned %s (original %s)",
                current_id, decl_name.c_str(), ast_dumper.GetCString(),
                orig_dumper.GetCString());
  }
}

void ClangExpressionDeclMap::AddOneVariable(NameSearchContext &context,
                                            ExpressionVariableSP &pvar_sp,
                                            unsigned int current_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  TypeFromUser user_type(
      llvm::cast<ClangExpressionVariable>(pvar_sp.get())->GetTypeFromUser());

  TypeFromParser parser_type(GuardedCopyType(user_type));

  if (!parser_type.GetOpaqueQualType()) {
    if (log)
      log->Printf("  CEDM::FEVD[%u] Couldn't import type for pvar %s",
                  current_id, pvar_sp->GetName().GetCString());
    return;
  }

  NamedDecl *var_decl =
      context.AddVarDecl(parser_type.GetLValueReferenceType());

  llvm::cast<ClangExpressionVariable>(pvar_sp.get())
      ->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      llvm::cast<ClangExpressionVariable>(pvar_sp.get())
          ->GetParserVars(GetParserID());
  parser_vars->m_parser_type = parser_type;
  parser_vars->m_named_decl = var_decl;
  parser_vars->m_llvm_value = NULL;
  parser_vars->m_lldb_value.Clear();

  if (log) {
    ASTDumper ast_dumper(var_decl);
    log->Printf("  CEDM::FEVD[%u] Added pvar %s, returned %s", current_id,
                pvar_sp->GetName().GetCString(), ast_dumper.GetCString());
  }
}

void ClangExpressionDeclMap::AddOneGenericVariable(NameSearchContext &context,
                                                   const Symbol &symbol,
                                                   unsigned int current_id) {
  assert(m_parser_vars.get());

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  Target *target = m_parser_vars->m_exe_ctx.GetTargetPtr();

  if (target == NULL)
    return;

  ASTContext *scratch_ast_context =
      target->GetScratchClangASTContext()->getASTContext();

  TypeFromUser user_type(
      ClangASTContext::GetBasicType(scratch_ast_context, eBasicTypeVoid)
          .GetPointerType()
          .GetLValueReferenceType());
  TypeFromParser parser_type(
      ClangASTContext::GetBasicType(m_ast_context, eBasicTypeVoid)
          .GetPointerType()
          .GetLValueReferenceType());
  NamedDecl *var_decl = context.AddVarDecl(parser_type);

  std::string decl_name(context.m_decl_name.getAsString());
  ConstString entity_name(decl_name.c_str());
  ClangExpressionVariable *entity(new ClangExpressionVariable(
      m_parser_vars->m_exe_ctx.GetBestExecutionContextScope(), entity_name,
      user_type, m_parser_vars->m_target_info.byte_order,
      m_parser_vars->m_target_info.address_byte_size));
  m_found_entities.AddNewlyConstructedVariable(entity);

  entity->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      entity->GetParserVars(GetParserID());

  const Address symbol_address = symbol.GetAddress();
  lldb::addr_t symbol_load_addr = symbol_address.GetLoadAddress(target);

  // parser_vars->m_lldb_value.SetContext(Value::eContextTypeClangType,
  // user_type.GetOpaqueQualType());
  parser_vars->m_lldb_value.SetCompilerType(user_type);
  parser_vars->m_lldb_value.GetScalar() = symbol_load_addr;
  parser_vars->m_lldb_value.SetValueType(Value::eValueTypeLoadAddress);

  parser_vars->m_parser_type = parser_type;
  parser_vars->m_named_decl = var_decl;
  parser_vars->m_llvm_value = NULL;
  parser_vars->m_lldb_sym = &symbol;

  if (log) {
    ASTDumper ast_dumper(var_decl);

    log->Printf("  CEDM::FEVD[%u] Found variable %s, returned %s", current_id,
                decl_name.c_str(), ast_dumper.GetCString());
  }
}

bool ClangExpressionDeclMap::ResolveUnknownTypes() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));
  Target *target = m_parser_vars->m_exe_ctx.GetTargetPtr();

  ClangASTContextForExpressions *scratch_ast_context =
      static_cast<ClangASTContextForExpressions*>(
          target->GetScratchClangASTContext());

  for (size_t index = 0, num_entities = m_found_entities.GetSize();
       index < num_entities; ++index) {
    ExpressionVariableSP entity = m_found_entities.GetVariableAtIndex(index);

    ClangExpressionVariable::ParserVars *parser_vars =
        llvm::cast<ClangExpressionVariable>(entity.get())
            ->GetParserVars(GetParserID());

    if (entity->m_flags & ClangExpressionVariable::EVUnknownType) {
      const NamedDecl *named_decl = parser_vars->m_named_decl;
      const VarDecl *var_decl = dyn_cast<VarDecl>(named_decl);

      if (!var_decl) {
        if (log)
          log->Printf("Entity of unknown type does not have a VarDecl");
        return false;
      }

      if (log) {
        ASTDumper ast_dumper(const_cast<VarDecl *>(var_decl));
        log->Printf("Variable of unknown type now has Decl %s",
                    ast_dumper.GetCString());
      }

      QualType var_type = var_decl->getType();
      TypeFromParser parser_type(
          var_type.getAsOpaquePtr(),
          ClangASTContext::GetASTContext(&var_decl->getASTContext()));

      lldb::opaque_compiler_type_t copied_type = 0;
      if (m_ast_importer_sp) {
        copied_type = m_ast_importer_sp->CopyType(
            scratch_ast_context->getASTContext(), &var_decl->getASTContext(),
            var_type.getAsOpaquePtr());
      } else if (HasMerger()) {
        copied_type = CopyTypeWithMerger(
            var_decl->getASTContext(),
            scratch_ast_context->GetMergerUnchecked(),
            var_type).getAsOpaquePtr();
      } else {
        lldbassert(0 && "No mechanism to copy a resolved unknown type!");
        return false;
      }

      if (!copied_type) {
        if (log)
          log->Printf("ClangExpressionDeclMap::ResolveUnknownType - Couldn't "
                      "import the type for a variable");

        return (bool)lldb::ExpressionVariableSP();
      }

      TypeFromUser user_type(copied_type, scratch_ast_context);

      //            parser_vars->m_lldb_value.SetContext(Value::eContextTypeClangType,
      //            user_type.GetOpaqueQualType());
      parser_vars->m_lldb_value.SetCompilerType(user_type);
      parser_vars->m_parser_type = parser_type;

      entity->SetCompilerType(user_type);

      entity->m_flags &= ~(ClangExpressionVariable::EVUnknownType);
    }
  }

  return true;
}

void ClangExpressionDeclMap::AddOneRegister(NameSearchContext &context,
                                            const RegisterInfo *reg_info,
                                            unsigned int current_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  CompilerType clang_type =
      ClangASTContext::GetBuiltinTypeForEncodingAndBitSize(
          m_ast_context, reg_info->encoding, reg_info->byte_size * 8);

  if (!clang_type) {
    if (log)
      log->Printf("  Tried to add a type for %s, but couldn't get one",
                  context.m_decl_name.getAsString().c_str());
    return;
  }

  TypeFromParser parser_clang_type(clang_type);

  NamedDecl *var_decl = context.AddVarDecl(parser_clang_type);

  ClangExpressionVariable *entity(new ClangExpressionVariable(
      m_parser_vars->m_exe_ctx.GetBestExecutionContextScope(),
      m_parser_vars->m_target_info.byte_order,
      m_parser_vars->m_target_info.address_byte_size));
  m_found_entities.AddNewlyConstructedVariable(entity);

  std::string decl_name(context.m_decl_name.getAsString());
  entity->SetName(ConstString(decl_name.c_str()));
  entity->SetRegisterInfo(reg_info);
  entity->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      entity->GetParserVars(GetParserID());
  parser_vars->m_parser_type = parser_clang_type;
  parser_vars->m_named_decl = var_decl;
  parser_vars->m_llvm_value = NULL;
  parser_vars->m_lldb_value.Clear();
  entity->m_flags |= ClangExpressionVariable::EVBareRegister;

  if (log) {
    ASTDumper ast_dumper(var_decl);
    log->Printf("  CEDM::FEVD[%d] Added register %s, returned %s", current_id,
                context.m_decl_name.getAsString().c_str(),
                ast_dumper.GetCString());
  }
}

void ClangExpressionDeclMap::AddOneFunction(NameSearchContext &context,
                                            Function *function, Symbol *symbol,
                                            unsigned int current_id) {
  assert(m_parser_vars.get());

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  NamedDecl *function_decl = NULL;
  Address fun_address;
  CompilerType function_clang_type;

  bool is_indirect_function = false;

  if (function) {
    Type *function_type = function->GetType();

    const auto lang = function->GetCompileUnit()->GetLanguage();
    const auto name = function->GetMangled().GetMangledName().AsCString();
    const bool extern_c = (Language::LanguageIsC(lang) &&
                           !CPlusPlusLanguage::IsCPPMangledName(name)) ||
                          (Language::LanguageIsObjC(lang) &&
                           !Language::LanguageIsCPlusPlus(lang));

    if (!extern_c) {
      TypeSystem *type_system = function->GetDeclContext().GetTypeSystem();
      if (llvm::isa<ClangASTContext>(type_system)) {
        clang::DeclContext *src_decl_context =
            (clang::DeclContext *)function->GetDeclContext()
                .GetOpaqueDeclContext();
        clang::FunctionDecl *src_function_decl =
            llvm::dyn_cast_or_null<clang::FunctionDecl>(src_decl_context);
        if (src_function_decl &&
            src_function_decl->getTemplateSpecializationInfo()) {
          clang::FunctionTemplateDecl *function_template =
              src_function_decl->getTemplateSpecializationInfo()->getTemplate();
          clang::FunctionTemplateDecl *copied_function_template =
              llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(
                  CopyDecl(function_template));
          if (copied_function_template) {
            if (log) {
              ASTDumper ast_dumper((clang::Decl *)copied_function_template);
              
              StreamString ss;
              
              function->DumpSymbolContext(&ss);
              
              log->Printf("  CEDM::FEVD[%u] Imported decl for function template"
                          " %s (description %s), returned %s",
                          current_id,
                          copied_function_template->getNameAsString().c_str(),
                          ss.GetData(), ast_dumper.GetCString());
            }
            
            context.AddNamedDecl(copied_function_template);
          }
        } else if (src_function_decl) {
          if (clang::FunctionDecl *copied_function_decl =
                  llvm::dyn_cast_or_null<clang::FunctionDecl>(
                       CopyDecl(src_function_decl))) {
            if (log) {
              ASTDumper ast_dumper((clang::Decl *)copied_function_decl);

              StreamString ss;

              function->DumpSymbolContext(&ss);

              log->Printf("  CEDM::FEVD[%u] Imported decl for function %s "
                          "(description %s), returned %s",
                          current_id,
                          copied_function_decl->getNameAsString().c_str(),
                          ss.GetData(), ast_dumper.GetCString());
            }

            context.AddNamedDecl(copied_function_decl);
            return;
          } else {
            if (log) {
              log->Printf("  Failed to import the function decl for '%s'",
                          src_function_decl->getName().str().c_str());
            }
          }
        }
      }
    }

    if (!function_type) {
      if (log)
        log->PutCString("  Skipped a function because it has no type");
      return;
    }

    function_clang_type = function_type->GetFullCompilerType();

    if (!function_clang_type) {
      if (log)
        log->PutCString("  Skipped a function because it has no Clang type");
      return;
    }

    fun_address = function->GetAddressRange().GetBaseAddress();

    CompilerType copied_function_type = GuardedCopyType(function_clang_type);
    if (copied_function_type) {
      function_decl = context.AddFunDecl(copied_function_type, extern_c);

      if (!function_decl) {
        if (log) {
          log->Printf(
              "  Failed to create a function decl for '%s' {0x%8.8" PRIx64 "}",
              function_type->GetName().GetCString(), function_type->GetID());
        }

        return;
      }
    } else {
      // We failed to copy the type we found
      if (log) {
        log->Printf("  Failed to import the function type '%s' {0x%8.8" PRIx64
                    "} into the expression parser AST contenxt",
                    function_type->GetName().GetCString(),
                    function_type->GetID());
      }

      return;
    }
  } else if (symbol) {
    fun_address = symbol->GetAddress();
    function_decl = context.AddGenericFunDecl();
    is_indirect_function = symbol->IsIndirect();
  } else {
    if (log)
      log->PutCString("  AddOneFunction called with no function and no symbol");
    return;
  }

  Target *target = m_parser_vars->m_exe_ctx.GetTargetPtr();

  lldb::addr_t load_addr =
      fun_address.GetCallableLoadAddress(target, is_indirect_function);

  ClangExpressionVariable *entity(new ClangExpressionVariable(
      m_parser_vars->m_exe_ctx.GetBestExecutionContextScope(),
      m_parser_vars->m_target_info.byte_order,
      m_parser_vars->m_target_info.address_byte_size));
  m_found_entities.AddNewlyConstructedVariable(entity);

  std::string decl_name(context.m_decl_name.getAsString());
  entity->SetName(ConstString(decl_name.c_str()));
  entity->SetCompilerType(function_clang_type);
  entity->EnableParserVars(GetParserID());

  ClangExpressionVariable::ParserVars *parser_vars =
      entity->GetParserVars(GetParserID());

  if (load_addr != LLDB_INVALID_ADDRESS) {
    parser_vars->m_lldb_value.SetValueType(Value::eValueTypeLoadAddress);
    parser_vars->m_lldb_value.GetScalar() = load_addr;
  } else {
    // We have to try finding a file address.

    lldb::addr_t file_addr = fun_address.GetFileAddress();

    parser_vars->m_lldb_value.SetValueType(Value::eValueTypeFileAddress);
    parser_vars->m_lldb_value.GetScalar() = file_addr;
  }

  parser_vars->m_named_decl = function_decl;
  parser_vars->m_llvm_value = NULL;

  if (log) {
    std::string function_str =
        function_decl ? ASTDumper(function_decl).GetCString() : "nullptr";

    StreamString ss;

    fun_address.Dump(&ss,
                     m_parser_vars->m_exe_ctx.GetBestExecutionContextScope(),
                     Address::DumpStyleResolvedDescription);

    log->Printf(
        "  CEDM::FEVD[%u] Found %s function %s (description %s), returned %s",
        current_id, (function ? "specific" : "generic"), decl_name.c_str(),
        ss.GetData(), function_str.c_str());
  }
}

void ClangExpressionDeclMap::AddThisType(NameSearchContext &context,
                                         TypeFromUser &ut,
                                         unsigned int current_id) {
  CompilerType copied_clang_type = GuardedCopyType(ut);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (!copied_clang_type) {
    if (log)
      log->Printf(
          "ClangExpressionDeclMap::AddThisType - Couldn't import the type");

    return;
  }

  if (copied_clang_type.IsAggregateType() &&
      copied_clang_type.GetCompleteType()) {
    CompilerType void_clang_type =
        ClangASTContext::GetBasicType(m_ast_context, eBasicTypeVoid);
    CompilerType void_ptr_clang_type = void_clang_type.GetPointerType();

    CompilerType method_type = ClangASTContext::CreateFunctionType(
        m_ast_context, void_clang_type, &void_ptr_clang_type, 1, false, 0);

    const bool is_virtual = false;
    const bool is_static = false;
    const bool is_inline = false;
    const bool is_explicit = false;
    const bool is_attr_used = true;
    const bool is_artificial = false;

    CXXMethodDecl *method_decl =
        ClangASTContext::GetASTContext(m_ast_context)
            ->AddMethodToCXXRecordType(
                copied_clang_type.GetOpaqueQualType(), "$__lldb_expr", NULL,
                method_type, lldb::eAccessPublic, is_virtual, is_static,
                is_inline, is_explicit, is_attr_used, is_artificial);

    if (log) {
      ASTDumper method_ast_dumper((clang::Decl *)method_decl);
      ASTDumper type_ast_dumper(copied_clang_type);

      log->Printf("  CEDM::AddThisType Added function $__lldb_expr "
                  "(description %s) for this type %s",
                  method_ast_dumper.GetCString(), type_ast_dumper.GetCString());
    }
  }

  if (!copied_clang_type.IsValid())
    return;

  TypeSourceInfo *type_source_info = m_ast_context->getTrivialTypeSourceInfo(
      QualType::getFromOpaquePtr(copied_clang_type.GetOpaqueQualType()));

  if (!type_source_info)
    return;

  // Construct a typedef type because if "*this" is a templated type we can't
  // just return ClassTemplateSpecializationDecls in response to name queries.
  // Using a typedef makes this much more robust.

  TypedefDecl *typedef_decl = TypedefDecl::Create(
      *m_ast_context, m_ast_context->getTranslationUnitDecl(), SourceLocation(),
      SourceLocation(), context.m_decl_name.getAsIdentifierInfo(),
      type_source_info);

  if (!typedef_decl)
    return;

  context.AddNamedDecl(typedef_decl);

  return;
}

void ClangExpressionDeclMap::AddOneType(NameSearchContext &context,
                                        TypeFromUser &ut,
                                        unsigned int current_id) {
  CompilerType copied_clang_type = GuardedCopyType(ut);

  if (!copied_clang_type) {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    if (log)
      log->Printf(
          "ClangExpressionDeclMap::AddOneType - Couldn't import the type");

    return;
  }

  context.AddTypeDecl(copied_clang_type);
}
