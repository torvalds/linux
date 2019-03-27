//===-- SBModule.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBModule.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBModuleSpec.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBSymbolContextList.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBModule::SBModule() : m_opaque_sp() {}

SBModule::SBModule(const lldb::ModuleSP &module_sp) : m_opaque_sp(module_sp) {}

SBModule::SBModule(const SBModuleSpec &module_spec) : m_opaque_sp() {
  ModuleSP module_sp;
  Status error = ModuleList::GetSharedModule(*module_spec.m_opaque_ap,
                                             module_sp, NULL, NULL, NULL);
  if (module_sp)
    SetSP(module_sp);
}

SBModule::SBModule(const SBModule &rhs) : m_opaque_sp(rhs.m_opaque_sp) {}

SBModule::SBModule(lldb::SBProcess &process, lldb::addr_t header_addr)
    : m_opaque_sp() {
  ProcessSP process_sp(process.GetSP());
  if (process_sp) {
    m_opaque_sp = process_sp->ReadModuleFromMemory(FileSpec(), header_addr);
    if (m_opaque_sp) {
      Target &target = process_sp->GetTarget();
      bool changed = false;
      m_opaque_sp->SetLoadAddress(target, 0, true, changed);
      target.GetImages().Append(m_opaque_sp);
    }
  }
}

const SBModule &SBModule::operator=(const SBModule &rhs) {
  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBModule::~SBModule() {}

bool SBModule::IsValid() const { return m_opaque_sp.get() != NULL; }

void SBModule::Clear() { m_opaque_sp.reset(); }

SBFileSpec SBModule::GetFileSpec() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBFileSpec file_spec;
  ModuleSP module_sp(GetSP());
  if (module_sp)
    file_spec.SetFileSpec(module_sp->GetFileSpec());

  if (log)
    log->Printf("SBModule(%p)::GetFileSpec () => SBFileSpec(%p)",
                static_cast<void *>(module_sp.get()),
                static_cast<const void *>(file_spec.get()));

  return file_spec;
}

lldb::SBFileSpec SBModule::GetPlatformFileSpec() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBFileSpec file_spec;
  ModuleSP module_sp(GetSP());
  if (module_sp)
    file_spec.SetFileSpec(module_sp->GetPlatformFileSpec());

  if (log)
    log->Printf("SBModule(%p)::GetPlatformFileSpec () => SBFileSpec(%p)",
                static_cast<void *>(module_sp.get()),
                static_cast<const void *>(file_spec.get()));

  return file_spec;
}

bool SBModule::SetPlatformFileSpec(const lldb::SBFileSpec &platform_file) {
  bool result = false;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  ModuleSP module_sp(GetSP());
  if (module_sp) {
    module_sp->SetPlatformFileSpec(*platform_file);
    result = true;
  }

  if (log)
    log->Printf("SBModule(%p)::SetPlatformFileSpec (SBFileSpec(%p (%s)) => %i",
                static_cast<void *>(module_sp.get()),
                static_cast<const void *>(platform_file.get()),
                platform_file->GetPath().c_str(), result);
  return result;
}

lldb::SBFileSpec SBModule::GetRemoteInstallFileSpec() {
  SBFileSpec sb_file_spec;
  ModuleSP module_sp(GetSP());
  if (module_sp)
    sb_file_spec.SetFileSpec(module_sp->GetRemoteInstallFileSpec());
  return sb_file_spec;
}

bool SBModule::SetRemoteInstallFileSpec(lldb::SBFileSpec &file) {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    module_sp->SetRemoteInstallFileSpec(file.ref());
    return true;
  }
  return false;
}

const uint8_t *SBModule::GetUUIDBytes() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  const uint8_t *uuid_bytes = NULL;
  ModuleSP module_sp(GetSP());
  if (module_sp)
    uuid_bytes = module_sp->GetUUID().GetBytes().data();

  if (log) {
    if (uuid_bytes) {
      StreamString s;
      module_sp->GetUUID().Dump(&s);
      log->Printf("SBModule(%p)::GetUUIDBytes () => %s",
                  static_cast<void *>(module_sp.get()), s.GetData());
    } else
      log->Printf("SBModule(%p)::GetUUIDBytes () => NULL",
                  static_cast<void *>(module_sp.get()));
  }
  return uuid_bytes;
}

const char *SBModule::GetUUIDString() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  const char *uuid_cstr = NULL;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    // We are going to return a "const char *" value through the public API, so
    // we need to constify it so it gets added permanently the string pool and
    // then we don't need to worry about the lifetime of the string as it will
    // never go away once it has been put into the ConstString string pool
    uuid_cstr = ConstString(module_sp->GetUUID().GetAsString()).GetCString();
  }

  if (uuid_cstr && uuid_cstr[0]) {
    if (log)
      log->Printf("SBModule(%p)::GetUUIDString () => %s",
                  static_cast<void *>(module_sp.get()), uuid_cstr);
    return uuid_cstr;
  }

  if (log)
    log->Printf("SBModule(%p)::GetUUIDString () => NULL",
                static_cast<void *>(module_sp.get()));
  return NULL;
}

bool SBModule::operator==(const SBModule &rhs) const {
  if (m_opaque_sp)
    return m_opaque_sp.get() == rhs.m_opaque_sp.get();
  return false;
}

bool SBModule::operator!=(const SBModule &rhs) const {
  if (m_opaque_sp)
    return m_opaque_sp.get() != rhs.m_opaque_sp.get();
  return false;
}

ModuleSP SBModule::GetSP() const { return m_opaque_sp; }

void SBModule::SetSP(const ModuleSP &module_sp) { m_opaque_sp = module_sp; }

SBAddress SBModule::ResolveFileAddress(lldb::addr_t vm_addr) {
  lldb::SBAddress sb_addr;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    Address addr;
    if (module_sp->ResolveFileAddress(vm_addr, addr))
      sb_addr.ref() = addr;
  }
  return sb_addr;
}

SBSymbolContext
SBModule::ResolveSymbolContextForAddress(const SBAddress &addr,
                                         uint32_t resolve_scope) {
  SBSymbolContext sb_sc;
  ModuleSP module_sp(GetSP());
  SymbolContextItem scope = static_cast<SymbolContextItem>(resolve_scope);
  if (module_sp && addr.IsValid())
    module_sp->ResolveSymbolContextForAddress(addr.ref(), scope, *sb_sc);
  return sb_sc;
}

bool SBModule::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  ModuleSP module_sp(GetSP());
  if (module_sp) {
    module_sp->GetDescription(&strm);
  } else
    strm.PutCString("No value");

  return true;
}

uint32_t SBModule::GetNumCompileUnits() {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    return module_sp->GetNumCompileUnits();
  }
  return 0;
}

SBCompileUnit SBModule::GetCompileUnitAtIndex(uint32_t index) {
  SBCompileUnit sb_cu;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    CompUnitSP cu_sp = module_sp->GetCompileUnitAtIndex(index);
    sb_cu.reset(cu_sp.get());
  }
  return sb_cu;
}

SBSymbolContextList
SBModule::FindCompileUnits(const SBFileSpec &sb_file_spec) {
  SBSymbolContextList sb_sc_list;
  const ModuleSP module_sp(GetSP());
  if (sb_file_spec.IsValid() && module_sp) {
    const bool append = true;
    module_sp->FindCompileUnits(*sb_file_spec, append, *sb_sc_list);
  }
  return sb_sc_list;
}

static Symtab *GetUnifiedSymbolTable(const lldb::ModuleSP &module_sp) {
  if (module_sp) {
    SymbolVendor *symbols = module_sp->GetSymbolVendor();
    if (symbols)
      return symbols->GetSymtab();
  }
  return NULL;
}

size_t SBModule::GetNumSymbols() {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    Symtab *symtab = GetUnifiedSymbolTable(module_sp);
    if (symtab)
      return symtab->GetNumSymbols();
  }
  return 0;
}

SBSymbol SBModule::GetSymbolAtIndex(size_t idx) {
  SBSymbol sb_symbol;
  ModuleSP module_sp(GetSP());
  Symtab *symtab = GetUnifiedSymbolTable(module_sp);
  if (symtab)
    sb_symbol.SetSymbol(symtab->SymbolAtIndex(idx));
  return sb_symbol;
}

lldb::SBSymbol SBModule::FindSymbol(const char *name,
                                    lldb::SymbolType symbol_type) {
  SBSymbol sb_symbol;
  if (name && name[0]) {
    ModuleSP module_sp(GetSP());
    Symtab *symtab = GetUnifiedSymbolTable(module_sp);
    if (symtab)
      sb_symbol.SetSymbol(symtab->FindFirstSymbolWithNameAndType(
          ConstString(name), symbol_type, Symtab::eDebugAny,
          Symtab::eVisibilityAny));
  }
  return sb_symbol;
}

lldb::SBSymbolContextList SBModule::FindSymbols(const char *name,
                                                lldb::SymbolType symbol_type) {
  SBSymbolContextList sb_sc_list;
  if (name && name[0]) {
    ModuleSP module_sp(GetSP());
    Symtab *symtab = GetUnifiedSymbolTable(module_sp);
    if (symtab) {
      std::vector<uint32_t> matching_symbol_indexes;
      const size_t num_matches = symtab->FindAllSymbolsWithNameAndType(
          ConstString(name), symbol_type, matching_symbol_indexes);
      if (num_matches) {
        SymbolContext sc;
        sc.module_sp = module_sp;
        SymbolContextList &sc_list = *sb_sc_list;
        for (size_t i = 0; i < num_matches; ++i) {
          sc.symbol = symtab->SymbolAtIndex(matching_symbol_indexes[i]);
          if (sc.symbol)
            sc_list.Append(sc);
        }
      }
    }
  }
  return sb_sc_list;
}

size_t SBModule::GetNumSections() {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    // Give the symbol vendor a chance to add to the unified section list.
    module_sp->GetSymbolVendor();
    SectionList *section_list = module_sp->GetSectionList();
    if (section_list)
      return section_list->GetSize();
  }
  return 0;
}

SBSection SBModule::GetSectionAtIndex(size_t idx) {
  SBSection sb_section;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    // Give the symbol vendor a chance to add to the unified section list.
    module_sp->GetSymbolVendor();
    SectionList *section_list = module_sp->GetSectionList();

    if (section_list)
      sb_section.SetSP(section_list->GetSectionAtIndex(idx));
  }
  return sb_section;
}

lldb::SBSymbolContextList SBModule::FindFunctions(const char *name,
                                                  uint32_t name_type_mask) {
  lldb::SBSymbolContextList sb_sc_list;
  ModuleSP module_sp(GetSP());
  if (name && module_sp) {
    const bool append = true;
    const bool symbols_ok = true;
    const bool inlines_ok = true;
    FunctionNameType type = static_cast<FunctionNameType>(name_type_mask);
    module_sp->FindFunctions(ConstString(name), NULL, type, symbols_ok,
                             inlines_ok, append, *sb_sc_list);
  }
  return sb_sc_list;
}

SBValueList SBModule::FindGlobalVariables(SBTarget &target, const char *name,
                                          uint32_t max_matches) {
  SBValueList sb_value_list;
  ModuleSP module_sp(GetSP());
  if (name && module_sp) {
    VariableList variable_list;
    const uint32_t match_count = module_sp->FindGlobalVariables(
        ConstString(name), NULL, max_matches, variable_list);

    if (match_count > 0) {
      for (uint32_t i = 0; i < match_count; ++i) {
        lldb::ValueObjectSP valobj_sp;
        TargetSP target_sp(target.GetSP());
        valobj_sp = ValueObjectVariable::Create(
            target_sp.get(), variable_list.GetVariableAtIndex(i));
        if (valobj_sp)
          sb_value_list.Append(SBValue(valobj_sp));
      }
    }
  }

  return sb_value_list;
}

lldb::SBValue SBModule::FindFirstGlobalVariable(lldb::SBTarget &target,
                                                const char *name) {
  SBValueList sb_value_list(FindGlobalVariables(target, name, 1));
  if (sb_value_list.IsValid() && sb_value_list.GetSize() > 0)
    return sb_value_list.GetValueAtIndex(0);
  return SBValue();
}

lldb::SBType SBModule::FindFirstType(const char *name_cstr) {
  SBType sb_type;
  ModuleSP module_sp(GetSP());
  if (name_cstr && module_sp) {
    SymbolContext sc;
    const bool exact_match = false;
    ConstString name(name_cstr);

    sb_type = SBType(module_sp->FindFirstType(sc, name, exact_match));

    if (!sb_type.IsValid()) {
      TypeSystem *type_system =
          module_sp->GetTypeSystemForLanguage(eLanguageTypeC);
      if (type_system)
        sb_type = SBType(type_system->GetBuiltinTypeByName(name));
    }
  }
  return sb_type;
}

lldb::SBType SBModule::GetBasicType(lldb::BasicType type) {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    TypeSystem *type_system =
        module_sp->GetTypeSystemForLanguage(eLanguageTypeC);
    if (type_system)
      return SBType(type_system->GetBasicTypeFromAST(type));
  }
  return SBType();
}

lldb::SBTypeList SBModule::FindTypes(const char *type) {
  SBTypeList retval;

  ModuleSP module_sp(GetSP());
  if (type && module_sp) {
    TypeList type_list;
    const bool exact_match = false;
    ConstString name(type);
    llvm::DenseSet<SymbolFile *> searched_symbol_files;
    const uint32_t num_matches = module_sp->FindTypes(
        name, exact_match, UINT32_MAX, searched_symbol_files, type_list);

    if (num_matches > 0) {
      for (size_t idx = 0; idx < num_matches; idx++) {
        TypeSP type_sp(type_list.GetTypeAtIndex(idx));
        if (type_sp)
          retval.Append(SBType(type_sp));
      }
    } else {
      TypeSystem *type_system =
          module_sp->GetTypeSystemForLanguage(eLanguageTypeC);
      if (type_system) {
        CompilerType compiler_type = type_system->GetBuiltinTypeByName(name);
        if (compiler_type)
          retval.Append(SBType(compiler_type));
      }
    }
  }

  return retval;
}

lldb::SBType SBModule::GetTypeByID(lldb::user_id_t uid) {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    SymbolVendor *vendor = module_sp->GetSymbolVendor();
    if (vendor) {
      Type *type_ptr = vendor->ResolveTypeUID(uid);
      if (type_ptr)
        return SBType(type_ptr->shared_from_this());
    }
  }
  return SBType();
}

lldb::SBTypeList SBModule::GetTypes(uint32_t type_mask) {
  SBTypeList sb_type_list;

  ModuleSP module_sp(GetSP());
  if (!module_sp)
    return sb_type_list;
  SymbolVendor *vendor = module_sp->GetSymbolVendor();
  if (!vendor)
    return sb_type_list;

  TypeClass type_class = static_cast<TypeClass>(type_mask);
  TypeList type_list;
  vendor->GetTypes(NULL, type_class, type_list);
  sb_type_list.m_opaque_ap->Append(type_list);
  return sb_type_list;
}

SBSection SBModule::FindSection(const char *sect_name) {
  SBSection sb_section;

  ModuleSP module_sp(GetSP());
  if (sect_name && module_sp) {
    // Give the symbol vendor a chance to add to the unified section list.
    module_sp->GetSymbolVendor();
    SectionList *section_list = module_sp->GetSectionList();
    if (section_list) {
      ConstString const_sect_name(sect_name);
      SectionSP section_sp(section_list->FindSectionByName(const_sect_name));
      if (section_sp) {
        sb_section.SetSP(section_sp);
      }
    }
  }
  return sb_section;
}

lldb::ByteOrder SBModule::GetByteOrder() {
  ModuleSP module_sp(GetSP());
  if (module_sp)
    return module_sp->GetArchitecture().GetByteOrder();
  return eByteOrderInvalid;
}

const char *SBModule::GetTriple() {
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    std::string triple(module_sp->GetArchitecture().GetTriple().str());
    // Unique the string so we don't run into ownership issues since the const
    // strings put the string into the string pool once and the strings never
    // comes out
    ConstString const_triple(triple.c_str());
    return const_triple.GetCString();
  }
  return NULL;
}

uint32_t SBModule::GetAddressByteSize() {
  ModuleSP module_sp(GetSP());
  if (module_sp)
    return module_sp->GetArchitecture().GetAddressByteSize();
  return sizeof(void *);
}

uint32_t SBModule::GetVersion(uint32_t *versions, uint32_t num_versions) {
  llvm::VersionTuple version;
  if (ModuleSP module_sp = GetSP())
    version = module_sp->GetVersion();
  uint32_t result = 0;
  if (!version.empty())
    ++result;
  if (version.getMinor())
    ++result;
  if(version.getSubminor())
    ++result;

  if (!versions)
    return result;

  if (num_versions > 0)
    versions[0] = version.empty() ? UINT32_MAX : version.getMajor();
  if (num_versions > 1)
    versions[1] = version.getMinor().getValueOr(UINT32_MAX);
  if (num_versions > 2)
    versions[2] = version.getSubminor().getValueOr(UINT32_MAX);
  for (uint32_t i = 3; i < num_versions; ++i)
    versions[i] = UINT32_MAX;
  return result;
}

lldb::SBFileSpec SBModule::GetSymbolFileSpec() const {
  lldb::SBFileSpec sb_file_spec;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    SymbolVendor *symbol_vendor_ptr = module_sp->GetSymbolVendor();
    if (symbol_vendor_ptr)
      sb_file_spec.SetFileSpec(symbol_vendor_ptr->GetMainFileSpec());
  }
  return sb_file_spec;
}

lldb::SBAddress SBModule::GetObjectFileHeaderAddress() const {
  lldb::SBAddress sb_addr;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    ObjectFile *objfile_ptr = module_sp->GetObjectFile();
    if (objfile_ptr)
      sb_addr.ref() = objfile_ptr->GetBaseAddress();
  }
  return sb_addr;
}

lldb::SBAddress SBModule::GetObjectFileEntryPointAddress() const {
  lldb::SBAddress sb_addr;
  ModuleSP module_sp(GetSP());
  if (module_sp) {
    ObjectFile *objfile_ptr = module_sp->GetObjectFile();
    if (objfile_ptr)
      sb_addr.ref() = objfile_ptr->GetEntryPointAddress();
  }
  return sb_addr;
}
