//===-- SymbolVendor.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/SymbolVendor.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// FindPlugin
//
// Platforms can register a callback to use when creating symbol vendors to
// allow for complex debug information file setups, and to also allow for
// finding separate debug information files.
//----------------------------------------------------------------------
SymbolVendor *SymbolVendor::FindPlugin(const lldb::ModuleSP &module_sp,
                                       lldb_private::Stream *feedback_strm) {
  std::unique_ptr<SymbolVendor> instance_ap;
  SymbolVendorCreateInstance create_callback;

  for (size_t idx = 0;
       (create_callback = PluginManager::GetSymbolVendorCreateCallbackAtIndex(
            idx)) != nullptr;
       ++idx) {
    instance_ap.reset(create_callback(module_sp, feedback_strm));

    if (instance_ap.get()) {
      return instance_ap.release();
    }
  }
  // The default implementation just tries to create debug information using
  // the file representation for the module.
  instance_ap.reset(new SymbolVendor(module_sp));
  if (instance_ap.get()) {
    ObjectFile *objfile = module_sp->GetObjectFile();
    if (objfile)
      instance_ap->AddSymbolFileRepresentation(objfile->shared_from_this());
  }
  return instance_ap.release();
}

//----------------------------------------------------------------------
// SymbolVendor constructor
//----------------------------------------------------------------------
SymbolVendor::SymbolVendor(const lldb::ModuleSP &module_sp)
    : ModuleChild(module_sp), m_type_list(), m_compile_units(),
      m_sym_file_ap(), m_symtab() {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SymbolVendor::~SymbolVendor() {}

//----------------------------------------------------------------------
// Add a representation given an object file.
//----------------------------------------------------------------------
void SymbolVendor::AddSymbolFileRepresentation(const ObjectFileSP &objfile_sp) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (objfile_sp) {
      m_objfile_sp = objfile_sp;
      m_sym_file_ap.reset(SymbolFile::FindPlugin(objfile_sp.get()));
    }
  }
}

bool SymbolVendor::SetCompileUnitAtIndex(size_t idx, const CompUnitSP &cu_sp) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    const size_t num_compile_units = GetNumCompileUnits();
    if (idx < num_compile_units) {
      // Fire off an assertion if this compile unit already exists for now. The
      // partial parsing should take care of only setting the compile unit
      // once, so if this assertion fails, we need to make sure that we don't
      // have a race condition, or have a second parse of the same compile
      // unit.
      assert(m_compile_units[idx].get() == nullptr);
      m_compile_units[idx] = cu_sp;
      return true;
    } else {
      // This should NOT happen, and if it does, we want to crash and know
      // about it
      assert(idx < num_compile_units);
    }
  }
  return false;
}

size_t SymbolVendor::GetNumCompileUnits() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_compile_units.empty()) {
      if (m_sym_file_ap.get()) {
        // Resize our array of compile unit shared pointers -- which will each
        // remain NULL until someone asks for the actual compile unit
        // information. When this happens, the symbol file will be asked to
        // parse this compile unit information.
        m_compile_units.resize(m_sym_file_ap->GetNumCompileUnits());
      }
    }
  }
  return m_compile_units.size();
}

lldb::LanguageType SymbolVendor::ParseLanguage(CompileUnit &comp_unit) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseLanguage(comp_unit);
  }
  return eLanguageTypeUnknown;
}

size_t SymbolVendor::ParseFunctions(CompileUnit &comp_unit) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseFunctions(comp_unit);
  }
  return 0;
}

bool SymbolVendor::ParseLineTable(CompileUnit &comp_unit) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseLineTable(comp_unit);
  }
  return false;
}

bool SymbolVendor::ParseDebugMacros(CompileUnit &comp_unit) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseDebugMacros(comp_unit);
  }
  return false;
}
bool SymbolVendor::ParseSupportFiles(CompileUnit &comp_unit,
                                     FileSpecList &support_files) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseSupportFiles(comp_unit, support_files);
  }
  return false;
}

bool SymbolVendor::ParseIsOptimized(CompileUnit &comp_unit) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseIsOptimized(comp_unit);
  }
  return false;
}

bool SymbolVendor::ParseImportedModules(
    const SymbolContext &sc, std::vector<ConstString> &imported_modules) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseImportedModules(sc, imported_modules);
  }
  return false;
}

size_t SymbolVendor::ParseBlocksRecursive(Function &func) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseBlocksRecursive(func);
  }
  return 0;
}

size_t SymbolVendor::ParseTypes(CompileUnit &comp_unit) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseTypes(comp_unit);
  }
  return 0;
}

size_t SymbolVendor::ParseVariablesForContext(const SymbolContext &sc) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ParseVariablesForContext(sc);
  }
  return 0;
}

Type *SymbolVendor::ResolveTypeUID(lldb::user_id_t type_uid) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ResolveTypeUID(type_uid);
  }
  return nullptr;
}

uint32_t SymbolVendor::ResolveSymbolContext(const Address &so_addr,
                                            SymbolContextItem resolve_scope,
                                            SymbolContext &sc) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ResolveSymbolContext(so_addr, resolve_scope, sc);
  }
  return 0;
}

uint32_t SymbolVendor::ResolveSymbolContext(const FileSpec &file_spec,
                                            uint32_t line, bool check_inlines,
                                            SymbolContextItem resolve_scope,
                                            SymbolContextList &sc_list) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->ResolveSymbolContext(file_spec, line, check_inlines,
                                                 resolve_scope, sc_list);
  }
  return 0;
}

size_t
SymbolVendor::FindGlobalVariables(const ConstString &name,
                                  const CompilerDeclContext *parent_decl_ctx,
                                  size_t max_matches, VariableList &variables) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->FindGlobalVariables(name, parent_decl_ctx,
                                                max_matches, variables);
  }
  return 0;
}

size_t SymbolVendor::FindGlobalVariables(const RegularExpression &regex,
                                         size_t max_matches,
                                         VariableList &variables) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->FindGlobalVariables(regex, max_matches, variables);
  }
  return 0;
}

size_t SymbolVendor::FindFunctions(const ConstString &name,
                                   const CompilerDeclContext *parent_decl_ctx,
                                   FunctionNameType name_type_mask,
                                   bool include_inlines, bool append,
                                   SymbolContextList &sc_list) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->FindFunctions(name, parent_decl_ctx, name_type_mask,
                                          include_inlines, append, sc_list);
  }
  return 0;
}

size_t SymbolVendor::FindFunctions(const RegularExpression &regex,
                                   bool include_inlines, bool append,
                                   SymbolContextList &sc_list) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->FindFunctions(regex, include_inlines, append,
                                          sc_list);
  }
  return 0;
}

size_t SymbolVendor::FindTypes(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    bool append, size_t max_matches,
    llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
    TypeMap &types) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->FindTypes(name, parent_decl_ctx, append,
                                      max_matches, searched_symbol_files,
                                      types);
  }
  if (!append)
    types.Clear();
  return 0;
}

size_t SymbolVendor::FindTypes(const std::vector<CompilerContext> &context,
                               bool append, TypeMap &types) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->FindTypes(context, append, types);
  }
  if (!append)
    types.Clear();
  return 0;
}

size_t SymbolVendor::GetTypes(SymbolContextScope *sc_scope, TypeClass type_mask,
                              lldb_private::TypeList &type_list) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      return m_sym_file_ap->GetTypes(sc_scope, type_mask, type_list);
  }
  return 0;
}

CompilerDeclContext
SymbolVendor::FindNamespace(const ConstString &name,
                            const CompilerDeclContext *parent_decl_ctx) {
  CompilerDeclContext namespace_decl_ctx;
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (m_sym_file_ap.get())
      namespace_decl_ctx = m_sym_file_ap->FindNamespace(name, parent_decl_ctx);
  }
  return namespace_decl_ctx;
}

void SymbolVendor::Dump(Stream *s) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

    bool show_context = false;

    s->Printf("%p: ", static_cast<void *>(this));
    s->Indent();
    s->PutCString("SymbolVendor");
    if (m_sym_file_ap.get()) {
      *s << " " << m_sym_file_ap->GetPluginName();
      ObjectFile *objfile = m_sym_file_ap->GetObjectFile();
      if (objfile) {
        const FileSpec &objfile_file_spec = objfile->GetFileSpec();
        if (objfile_file_spec) {
          s->PutCString(" (");
          objfile_file_spec.Dump(s);
          s->PutChar(')');
        }
      }
    }
    s->EOL();
    if (m_sym_file_ap)
      m_sym_file_ap->Dump(*s);
    s->IndentMore();
    m_type_list.Dump(s, show_context);

    CompileUnitConstIter cu_pos, cu_end;
    cu_end = m_compile_units.end();
    for (cu_pos = m_compile_units.begin(); cu_pos != cu_end; ++cu_pos) {
      // We currently only dump the compile units that have been parsed
      if (cu_pos->get())
        (*cu_pos)->Dump(s, show_context);
    }

    if (Symtab *symtab = GetSymtab())
      symtab->Dump(s, nullptr, eSortOrderNone);

    s->IndentLess();
  }
}

CompUnitSP SymbolVendor::GetCompileUnitAtIndex(size_t idx) {
  CompUnitSP cu_sp;
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    const size_t num_compile_units = GetNumCompileUnits();
    if (idx < num_compile_units) {
      cu_sp = m_compile_units[idx];
      if (cu_sp.get() == nullptr) {
        m_compile_units[idx] = m_sym_file_ap->ParseCompileUnitAtIndex(idx);
        cu_sp = m_compile_units[idx];
      }
    }
  }
  return cu_sp;
}

FileSpec SymbolVendor::GetMainFileSpec() const {
  if (m_sym_file_ap.get()) {
    const ObjectFile *symfile_objfile = m_sym_file_ap->GetObjectFile();
    if (symfile_objfile)
      return symfile_objfile->GetFileSpec();
  }

  return FileSpec();
}

Symtab *SymbolVendor::GetSymtab() {
  ModuleSP module_sp(GetModule());
  if (!module_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

  if (m_symtab)
    return m_symtab;

  ObjectFile *objfile = module_sp->GetObjectFile();
  if (!objfile)
    return nullptr;

  m_symtab = objfile->GetSymtab();
  if (m_symtab && m_sym_file_ap)
    m_sym_file_ap->AddSymbols(*m_symtab);

  return m_symtab;
}

void SymbolVendor::ClearSymtab() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    ObjectFile *objfile = module_sp->GetObjectFile();
    if (objfile) {
      // Clear symbol table from unified section list.
      objfile->ClearSymtab();
    }
  }
}

void SymbolVendor::SectionFileAddressesChanged() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    ObjectFile *module_objfile = module_sp->GetObjectFile();
    if (m_sym_file_ap.get()) {
      ObjectFile *symfile_objfile = m_sym_file_ap->GetObjectFile();
      if (symfile_objfile != module_objfile)
        symfile_objfile->SectionFileAddressesChanged();
    }
    Symtab *symtab = GetSymtab();
    if (symtab) {
      symtab->SectionFileAddressesChanged();
    }
  }
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
lldb_private::ConstString SymbolVendor::GetPluginName() {
  static ConstString g_name("vendor-default");
  return g_name;
}

uint32_t SymbolVendor::GetPluginVersion() { return 1; }
