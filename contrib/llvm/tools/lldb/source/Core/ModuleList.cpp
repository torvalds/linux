//===-- ModuleList.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ModuleList.h"
#include "lldb/Core/FileSpecList.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Symbols.h"
#include "lldb/Interpreter/OptionValueFileSpec.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-defines.h"

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#endif

#include "clang/Driver/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace lldb_private {
class Function;
}
namespace lldb_private {
class RegularExpression;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolFile;
}
namespace lldb_private {
class Target;
}
namespace lldb_private {
class TypeList;
}

using namespace lldb;
using namespace lldb_private;

namespace {

static constexpr PropertyDefinition g_properties[] = {
    {"enable-external-lookup", OptionValue::eTypeBoolean, true, true, nullptr,
     {},
     "Control the use of external tools and repositories to locate symbol "
     "files. Directories listed in target.debug-file-search-paths and "
     "directory of the executable are always checked first for separate debug "
     "info files. Then depending on this setting: "
     "On macOS, Spotlight would be also used to locate a matching .dSYM "
     "bundle based on the UUID of the executable. "
     "On NetBSD, directory /usr/libdata/debug would be also searched. "
     "On platforms other than NetBSD directory /usr/lib/debug would be "
     "also searched."
    },
    {"clang-modules-cache-path", OptionValue::eTypeFileSpec, true, 0, nullptr,
     {},
     "The path to the clang modules cache directory (-fmodules-cache-path)."}};

enum { ePropertyEnableExternalLookup, ePropertyClangModulesCachePath };

} // namespace

ModuleListProperties::ModuleListProperties() {
  m_collection_sp.reset(new OptionValueProperties(ConstString("symbols")));
  m_collection_sp->Initialize(g_properties);

  llvm::SmallString<128> path;
  clang::driver::Driver::getDefaultModuleCachePath(path);
  SetClangModulesCachePath(path);
}

bool ModuleListProperties::GetEnableExternalLookup() const {
  const uint32_t idx = ePropertyEnableExternalLookup;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(
      nullptr, idx, g_properties[idx].default_uint_value != 0);
}

FileSpec ModuleListProperties::GetClangModulesCachePath() const {
  return m_collection_sp
      ->GetPropertyAtIndexAsOptionValueFileSpec(nullptr, false,
                                                ePropertyClangModulesCachePath)
      ->GetCurrentValue();
}

bool ModuleListProperties::SetClangModulesCachePath(llvm::StringRef path) {
  return m_collection_sp->SetPropertyAtIndexAsString(
      nullptr, ePropertyClangModulesCachePath, path);
}


ModuleList::ModuleList()
    : m_modules(), m_modules_mutex(), m_notifier(nullptr) {}

ModuleList::ModuleList(const ModuleList &rhs)
    : m_modules(), m_modules_mutex(), m_notifier(nullptr) {
  std::lock_guard<std::recursive_mutex> lhs_guard(m_modules_mutex);
  std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_modules_mutex);
  m_modules = rhs.m_modules;
}

ModuleList::ModuleList(ModuleList::Notifier *notifier)
    : m_modules(), m_modules_mutex(), m_notifier(notifier) {}

const ModuleList &ModuleList::operator=(const ModuleList &rhs) {
  if (this != &rhs) {
    // That's probably me nit-picking, but in theoretical situation:
    //
    // * that two threads A B and
    // * two ModuleList's x y do opposite assignments ie.:
    //
    //  in thread A: | in thread B:
    //    x = y;     |   y = x;
    //
    // This establishes correct(same) lock taking order and thus avoids
    // priority inversion.
    if (uintptr_t(this) > uintptr_t(&rhs)) {
      std::lock_guard<std::recursive_mutex> lhs_guard(m_modules_mutex);
      std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_modules_mutex);
      m_modules = rhs.m_modules;
    } else {
      std::lock_guard<std::recursive_mutex> lhs_guard(m_modules_mutex);
      std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_modules_mutex);
      m_modules = rhs.m_modules;
    }
  }
  return *this;
}

ModuleList::~ModuleList() = default;

void ModuleList::AppendImpl(const ModuleSP &module_sp, bool use_notifier) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    m_modules.push_back(module_sp);
    if (use_notifier && m_notifier)
      m_notifier->ModuleAdded(*this, module_sp);
  }
}

void ModuleList::Append(const ModuleSP &module_sp) { AppendImpl(module_sp); }

void ModuleList::ReplaceEquivalent(const ModuleSP &module_sp) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);

    // First remove any equivalent modules. Equivalent modules are modules
    // whose path, platform path and architecture match.
    ModuleSpec equivalent_module_spec(module_sp->GetFileSpec(),
                                      module_sp->GetArchitecture());
    equivalent_module_spec.GetPlatformFileSpec() =
        module_sp->GetPlatformFileSpec();

    size_t idx = 0;
    while (idx < m_modules.size()) {
      ModuleSP module_sp(m_modules[idx]);
      if (module_sp->MatchesModuleSpec(equivalent_module_spec))
        RemoveImpl(m_modules.begin() + idx);
      else
        ++idx;
    }
    // Now add the new module to the list
    Append(module_sp);
  }
}

bool ModuleList::AppendIfNeeded(const ModuleSP &module_sp) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      if (pos->get() == module_sp.get())
        return false; // Already in the list
    }
    // Only push module_sp on the list if it wasn't already in there.
    Append(module_sp);
    return true;
  }
  return false;
}

void ModuleList::Append(const ModuleList &module_list) {
  for (auto pos : module_list.m_modules)
    Append(pos);
}

bool ModuleList::AppendIfNeeded(const ModuleList &module_list) {
  bool any_in = false;
  for (auto pos : module_list.m_modules) {
    if (AppendIfNeeded(pos))
      any_in = true;
  }
  return any_in;
}

bool ModuleList::RemoveImpl(const ModuleSP &module_sp, bool use_notifier) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      if (pos->get() == module_sp.get()) {
        m_modules.erase(pos);
        if (use_notifier && m_notifier)
          m_notifier->ModuleRemoved(*this, module_sp);
        return true;
      }
    }
  }
  return false;
}

ModuleList::collection::iterator
ModuleList::RemoveImpl(ModuleList::collection::iterator pos,
                       bool use_notifier) {
  ModuleSP module_sp(*pos);
  collection::iterator retval = m_modules.erase(pos);
  if (use_notifier && m_notifier)
    m_notifier->ModuleRemoved(*this, module_sp);
  return retval;
}

bool ModuleList::Remove(const ModuleSP &module_sp) {
  return RemoveImpl(module_sp);
}

bool ModuleList::ReplaceModule(const lldb::ModuleSP &old_module_sp,
                               const lldb::ModuleSP &new_module_sp) {
  if (!RemoveImpl(old_module_sp, false))
    return false;
  AppendImpl(new_module_sp, false);
  if (m_notifier)
    m_notifier->ModuleUpdated(*this, old_module_sp, new_module_sp);
  return true;
}

bool ModuleList::RemoveIfOrphaned(const Module *module_ptr) {
  if (module_ptr) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      if (pos->get() == module_ptr) {
        if (pos->unique()) {
          pos = RemoveImpl(pos);
          return true;
        } else
          return false;
      }
    }
  }
  return false;
}

size_t ModuleList::RemoveOrphans(bool mandatory) {
  std::unique_lock<std::recursive_mutex> lock(m_modules_mutex, std::defer_lock);

  if (mandatory) {
    lock.lock();
  } else {
    // Not mandatory, remove orphans if we can get the mutex
    if (!lock.try_lock())
      return 0;
  }
  collection::iterator pos = m_modules.begin();
  size_t remove_count = 0;
  while (pos != m_modules.end()) {
    if (pos->unique()) {
      pos = RemoveImpl(pos);
      ++remove_count;
    } else {
      ++pos;
    }
  }
  return remove_count;
}

size_t ModuleList::Remove(ModuleList &module_list) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  size_t num_removed = 0;
  collection::iterator pos, end = module_list.m_modules.end();
  for (pos = module_list.m_modules.begin(); pos != end; ++pos) {
    if (Remove(*pos))
      ++num_removed;
  }
  return num_removed;
}

void ModuleList::Clear() { ClearImpl(); }

void ModuleList::Destroy() { ClearImpl(); }

void ModuleList::ClearImpl(bool use_notifier) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  if (use_notifier && m_notifier)
    m_notifier->WillClearList(*this);
  m_modules.clear();
}

Module *ModuleList::GetModulePointerAtIndex(size_t idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  return GetModulePointerAtIndexUnlocked(idx);
}

Module *ModuleList::GetModulePointerAtIndexUnlocked(size_t idx) const {
  if (idx < m_modules.size())
    return m_modules[idx].get();
  return nullptr;
}

ModuleSP ModuleList::GetModuleAtIndex(size_t idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  return GetModuleAtIndexUnlocked(idx);
}

ModuleSP ModuleList::GetModuleAtIndexUnlocked(size_t idx) const {
  ModuleSP module_sp;
  if (idx < m_modules.size())
    module_sp = m_modules[idx];
  return module_sp;
}

size_t ModuleList::FindFunctions(const ConstString &name,
                                 FunctionNameType name_type_mask,
                                 bool include_symbols, bool include_inlines,
                                 bool append,
                                 SymbolContextList &sc_list) const {
  if (!append)
    sc_list.Clear();

  const size_t old_size = sc_list.GetSize();

  if (name_type_mask & eFunctionNameTypeAuto) {
    Module::LookupInfo lookup_info(name, name_type_mask, eLanguageTypeUnknown);

    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      (*pos)->FindFunctions(lookup_info.GetLookupName(), nullptr,
                            lookup_info.GetNameTypeMask(), include_symbols,
                            include_inlines, true, sc_list);
    }

    const size_t new_size = sc_list.GetSize();

    if (old_size < new_size)
      lookup_info.Prune(sc_list, old_size);
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      (*pos)->FindFunctions(name, nullptr, name_type_mask, include_symbols,
                            include_inlines, true, sc_list);
    }
  }
  return sc_list.GetSize() - old_size;
}

size_t ModuleList::FindFunctionSymbols(const ConstString &name,
                                       lldb::FunctionNameType name_type_mask,
                                       SymbolContextList &sc_list) {
  const size_t old_size = sc_list.GetSize();

  if (name_type_mask & eFunctionNameTypeAuto) {
    Module::LookupInfo lookup_info(name, name_type_mask, eLanguageTypeUnknown);

    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      (*pos)->FindFunctionSymbols(lookup_info.GetLookupName(),
                                  lookup_info.GetNameTypeMask(), sc_list);
    }

    const size_t new_size = sc_list.GetSize();

    if (old_size < new_size)
      lookup_info.Prune(sc_list, old_size);
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      (*pos)->FindFunctionSymbols(name, name_type_mask, sc_list);
    }
  }

  return sc_list.GetSize() - old_size;
}

size_t ModuleList::FindFunctions(const RegularExpression &name,
                                 bool include_symbols, bool include_inlines,
                                 bool append, SymbolContextList &sc_list) {
  const size_t old_size = sc_list.GetSize();

  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->FindFunctions(name, include_symbols, include_inlines, append,
                          sc_list);
  }

  return sc_list.GetSize() - old_size;
}

size_t ModuleList::FindCompileUnits(const FileSpec &path, bool append,
                                    SymbolContextList &sc_list) const {
  if (!append)
    sc_list.Clear();

  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->FindCompileUnits(path, true, sc_list);
  }

  return sc_list.GetSize();
}

size_t ModuleList::FindGlobalVariables(const ConstString &name,
                                       size_t max_matches,
                                       VariableList &variable_list) const {
  size_t initial_size = variable_list.GetSize();
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->FindGlobalVariables(name, nullptr, max_matches, variable_list);
  }
  return variable_list.GetSize() - initial_size;
}

size_t ModuleList::FindGlobalVariables(const RegularExpression &regex,
                                       size_t max_matches,
                                       VariableList &variable_list) const {
  size_t initial_size = variable_list.GetSize();
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->FindGlobalVariables(regex, max_matches, variable_list);
  }
  return variable_list.GetSize() - initial_size;
}

size_t ModuleList::FindSymbolsWithNameAndType(const ConstString &name,
                                              SymbolType symbol_type,
                                              SymbolContextList &sc_list,
                                              bool append) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  if (!append)
    sc_list.Clear();
  size_t initial_size = sc_list.GetSize();

  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos)
    (*pos)->FindSymbolsWithNameAndType(name, symbol_type, sc_list);
  return sc_list.GetSize() - initial_size;
}

size_t ModuleList::FindSymbolsMatchingRegExAndType(
    const RegularExpression &regex, lldb::SymbolType symbol_type,
    SymbolContextList &sc_list, bool append) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  if (!append)
    sc_list.Clear();
  size_t initial_size = sc_list.GetSize();

  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos)
    (*pos)->FindSymbolsMatchingRegExAndType(regex, symbol_type, sc_list);
  return sc_list.GetSize() - initial_size;
}

size_t ModuleList::FindModules(const ModuleSpec &module_spec,
                               ModuleList &matching_module_list) const {
  size_t existing_matches = matching_module_list.GetSize();

  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    ModuleSP module_sp(*pos);
    if (module_sp->MatchesModuleSpec(module_spec))
      matching_module_list.Append(module_sp);
  }
  return matching_module_list.GetSize() - existing_matches;
}

ModuleSP ModuleList::FindModule(const Module *module_ptr) const {
  ModuleSP module_sp;

  // Scope for "locker"
  {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();

    for (pos = m_modules.begin(); pos != end; ++pos) {
      if ((*pos).get() == module_ptr) {
        module_sp = (*pos);
        break;
      }
    }
  }
  return module_sp;
}

ModuleSP ModuleList::FindModule(const UUID &uuid) const {
  ModuleSP module_sp;

  if (uuid.IsValid()) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();

    for (pos = m_modules.begin(); pos != end; ++pos) {
      if ((*pos)->GetUUID() == uuid) {
        module_sp = (*pos);
        break;
      }
    }
  }
  return module_sp;
}

size_t
ModuleList::FindTypes(Module *search_first, const ConstString &name,
                      bool name_is_fully_qualified, size_t max_matches,
                      llvm::DenseSet<SymbolFile *> &searched_symbol_files,
                      TypeList &types) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);

  size_t total_matches = 0;
  collection::const_iterator pos, end = m_modules.end();
  if (search_first) {
    for (pos = m_modules.begin(); pos != end; ++pos) {
      if (search_first == pos->get()) {
        total_matches +=
            search_first->FindTypes(name, name_is_fully_qualified, max_matches,
                                    searched_symbol_files, types);

        if (total_matches >= max_matches)
          break;
      }
    }
  }

  if (total_matches < max_matches) {
    for (pos = m_modules.begin(); pos != end; ++pos) {
      // Search the module if the module is not equal to the one in the symbol
      // context "sc". If "sc" contains a empty module shared pointer, then the
      // comparison will always be true (valid_module_ptr != nullptr).
      if (search_first != pos->get())
        total_matches +=
            (*pos)->FindTypes(name, name_is_fully_qualified, max_matches,
                              searched_symbol_files, types);

      if (total_matches >= max_matches)
        break;
    }
  }

  return total_matches;
}

bool ModuleList::FindSourceFile(const FileSpec &orig_spec,
                                FileSpec &new_spec) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    if ((*pos)->FindSourceFile(orig_spec, new_spec))
      return true;
  }
  return false;
}

void ModuleList::FindAddressesForLine(const lldb::TargetSP target_sp,
                                      const FileSpec &file, uint32_t line,
                                      Function *function,
                                      std::vector<Address> &output_local,
                                      std::vector<Address> &output_extern) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->FindAddressesForLine(target_sp, file, line, function, output_local,
                                 output_extern);
  }
}

ModuleSP ModuleList::FindFirstModule(const ModuleSpec &module_spec) const {
  ModuleSP module_sp;
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    ModuleSP module_sp(*pos);
    if (module_sp->MatchesModuleSpec(module_spec))
      return module_sp;
  }
  return module_sp;
}

size_t ModuleList::GetSize() const {
  size_t size = 0;
  {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    size = m_modules.size();
  }
  return size;
}

void ModuleList::Dump(Stream *s) const {
  //  s.Printf("%.*p: ", (int)sizeof(void*) * 2, this);
  //  s.Indent();
  //  s << "ModuleList\n";

  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->Dump(s);
  }
}

void ModuleList::LogUUIDAndPaths(Log *log, const char *prefix_cstr) {
  if (log != nullptr) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, begin = m_modules.begin(),
                                    end = m_modules.end();
    for (pos = begin; pos != end; ++pos) {
      Module *module = pos->get();
      const FileSpec &module_file_spec = module->GetFileSpec();
      log->Printf("%s[%u] %s (%s) \"%s\"", prefix_cstr ? prefix_cstr : "",
                  (uint32_t)std::distance(begin, pos),
                  module->GetUUID().GetAsString().c_str(),
                  module->GetArchitecture().GetArchitectureName(),
                  module_file_spec.GetPath().c_str());
    }
  }
}

bool ModuleList::ResolveFileAddress(lldb::addr_t vm_addr,
                                    Address &so_addr) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    if ((*pos)->ResolveFileAddress(vm_addr, so_addr))
      return true;
  }

  return false;
}

uint32_t
ModuleList::ResolveSymbolContextForAddress(const Address &so_addr,
                                           SymbolContextItem resolve_scope,
                                           SymbolContext &sc) const {
  // The address is already section offset so it has a module
  uint32_t resolved_flags = 0;
  ModuleSP module_sp(so_addr.GetModule());
  if (module_sp) {
    resolved_flags =
        module_sp->ResolveSymbolContextForAddress(so_addr, resolve_scope, sc);
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      resolved_flags =
          (*pos)->ResolveSymbolContextForAddress(so_addr, resolve_scope, sc);
      if (resolved_flags != 0)
        break;
    }
  }

  return resolved_flags;
}

uint32_t ModuleList::ResolveSymbolContextForFilePath(
    const char *file_path, uint32_t line, bool check_inlines,
    SymbolContextItem resolve_scope, SymbolContextList &sc_list) const {
  FileSpec file_spec(file_path);
  return ResolveSymbolContextsForFileSpec(file_spec, line, check_inlines,
                                          resolve_scope, sc_list);
}

uint32_t ModuleList::ResolveSymbolContextsForFileSpec(
    const FileSpec &file_spec, uint32_t line, bool check_inlines,
    SymbolContextItem resolve_scope, SymbolContextList &sc_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    (*pos)->ResolveSymbolContextsForFileSpec(file_spec, line, check_inlines,
                                             resolve_scope, sc_list);
  }

  return sc_list.GetSize();
}

size_t ModuleList::GetIndexForModule(const Module *module) const {
  if (module) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos;
    collection::const_iterator begin = m_modules.begin();
    collection::const_iterator end = m_modules.end();
    for (pos = begin; pos != end; ++pos) {
      if ((*pos).get() == module)
        return std::distance(begin, pos);
    }
  }
  return LLDB_INVALID_INDEX32;
}

namespace {
struct SharedModuleListInfo {
  ModuleList module_list;
  ModuleListProperties module_list_properties;
};
}
static SharedModuleListInfo &GetSharedModuleListInfo()
{
  static SharedModuleListInfo *g_shared_module_list_info = nullptr;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    // NOTE: Intentionally leak the module list so a program doesn't have to
    // cleanup all modules and object files as it exits. This just wastes time
    // doing a bunch of cleanup that isn't required.
    if (g_shared_module_list_info == nullptr)
      g_shared_module_list_info = new SharedModuleListInfo();
  });
  return *g_shared_module_list_info;
}

static ModuleList &GetSharedModuleList() {
  return GetSharedModuleListInfo().module_list;
}

ModuleListProperties &ModuleList::GetGlobalModuleListProperties() {
  return GetSharedModuleListInfo().module_list_properties;
}

bool ModuleList::ModuleIsInCache(const Module *module_ptr) {
  if (module_ptr) {
    ModuleList &shared_module_list = GetSharedModuleList();
    return shared_module_list.FindModule(module_ptr).get() != nullptr;
  }
  return false;
}

size_t ModuleList::FindSharedModules(const ModuleSpec &module_spec,
                                     ModuleList &matching_module_list) {
  return GetSharedModuleList().FindModules(module_spec, matching_module_list);
}

size_t ModuleList::RemoveOrphanSharedModules(bool mandatory) {
  return GetSharedModuleList().RemoveOrphans(mandatory);
}

Status ModuleList::GetSharedModule(const ModuleSpec &module_spec,
                                   ModuleSP &module_sp,
                                   const FileSpecList *module_search_paths_ptr,
                                   ModuleSP *old_module_sp_ptr,
                                   bool *did_create_ptr, bool always_create) {
  ModuleList &shared_module_list = GetSharedModuleList();
  std::lock_guard<std::recursive_mutex> guard(
      shared_module_list.m_modules_mutex);
  char path[PATH_MAX];

  Status error;

  module_sp.reset();

  if (did_create_ptr)
    *did_create_ptr = false;
  if (old_module_sp_ptr)
    old_module_sp_ptr->reset();

  const UUID *uuid_ptr = module_spec.GetUUIDPtr();
  const FileSpec &module_file_spec = module_spec.GetFileSpec();
  const ArchSpec &arch = module_spec.GetArchitecture();

  // Make sure no one else can try and get or create a module while this
  // function is actively working on it by doing an extra lock on the global
  // mutex list.
  if (!always_create) {
    ModuleList matching_module_list;
    const size_t num_matching_modules =
        shared_module_list.FindModules(module_spec, matching_module_list);
    if (num_matching_modules > 0) {
      for (size_t module_idx = 0; module_idx < num_matching_modules;
           ++module_idx) {
        module_sp = matching_module_list.GetModuleAtIndex(module_idx);

        // Make sure the file for the module hasn't been modified
        if (module_sp->FileHasChanged()) {
          if (old_module_sp_ptr && !*old_module_sp_ptr)
            *old_module_sp_ptr = module_sp;

          Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_MODULES));
          if (log != nullptr)
            log->Printf("module changed: %p, removing from global module list",
                        static_cast<void *>(module_sp.get()));

          shared_module_list.Remove(module_sp);
          module_sp.reset();
        } else {
          // The module matches and the module was not modified from when it
          // was last loaded.
          return error;
        }
      }
    }
  }

  if (module_sp)
    return error;

  module_sp.reset(new Module(module_spec));
  // Make sure there are a module and an object file since we can specify a
  // valid file path with an architecture that might not be in that file. By
  // getting the object file we can guarantee that the architecture matches
  if (module_sp->GetObjectFile()) {
    // If we get in here we got the correct arch, now we just need to verify
    // the UUID if one was given
    if (uuid_ptr && *uuid_ptr != module_sp->GetUUID()) {
      module_sp.reset();
    } else {
      if (module_sp->GetObjectFile() &&
          module_sp->GetObjectFile()->GetType() ==
              ObjectFile::eTypeStubLibrary) {
        module_sp.reset();
      } else {
        if (did_create_ptr) {
          *did_create_ptr = true;
        }

        shared_module_list.ReplaceEquivalent(module_sp);
        return error;
      }
    }
  } else {
    module_sp.reset();
  }

  if (module_search_paths_ptr) {
    const auto num_directories = module_search_paths_ptr->GetSize();
    for (size_t idx = 0; idx < num_directories; ++idx) {
      auto search_path_spec = module_search_paths_ptr->GetFileSpecAtIndex(idx);
      FileSystem::Instance().Resolve(search_path_spec);
      namespace fs = llvm::sys::fs;
      if (!FileSystem::Instance().IsDirectory(search_path_spec))
        continue;
      search_path_spec.AppendPathComponent(
          module_spec.GetFileSpec().GetFilename().AsCString());
      if (!FileSystem::Instance().Exists(search_path_spec))
        continue;

      auto resolved_module_spec(module_spec);
      resolved_module_spec.GetFileSpec() = search_path_spec;
      module_sp.reset(new Module(resolved_module_spec));
      if (module_sp->GetObjectFile()) {
        // If we get in here we got the correct arch, now we just need to
        // verify the UUID if one was given
        if (uuid_ptr && *uuid_ptr != module_sp->GetUUID()) {
          module_sp.reset();
        } else {
          if (module_sp->GetObjectFile()->GetType() ==
              ObjectFile::eTypeStubLibrary) {
            module_sp.reset();
          } else {
            if (did_create_ptr)
              *did_create_ptr = true;

            shared_module_list.ReplaceEquivalent(module_sp);
            return Status();
          }
        }
      } else {
        module_sp.reset();
      }
    }
  }

  // Either the file didn't exist where at the path, or no path was given, so
  // we now have to use more extreme measures to try and find the appropriate
  // module.

  // Fixup the incoming path in case the path points to a valid file, yet the
  // arch or UUID (if one was passed in) don't match.
  ModuleSpec located_binary_modulespec =
      Symbols::LocateExecutableObjectFile(module_spec);

  // Don't look for the file if it appears to be the same one we already
  // checked for above...
  if (located_binary_modulespec.GetFileSpec() != module_file_spec) {
    if (!FileSystem::Instance().Exists(
            located_binary_modulespec.GetFileSpec())) {
      located_binary_modulespec.GetFileSpec().GetPath(path, sizeof(path));
      if (path[0] == '\0')
        module_file_spec.GetPath(path, sizeof(path));
      // How can this check ever be true? This branch it is false, and we
      // haven't modified file_spec.
      if (FileSystem::Instance().Exists(
              located_binary_modulespec.GetFileSpec())) {
        std::string uuid_str;
        if (uuid_ptr && uuid_ptr->IsValid())
          uuid_str = uuid_ptr->GetAsString();

        if (arch.IsValid()) {
          if (!uuid_str.empty())
            error.SetErrorStringWithFormat(
                "'%s' does not contain the %s architecture and UUID %s", path,
                arch.GetArchitectureName(), uuid_str.c_str());
          else
            error.SetErrorStringWithFormat(
                "'%s' does not contain the %s architecture.", path,
                arch.GetArchitectureName());
        }
      } else {
        error.SetErrorStringWithFormat("'%s' does not exist", path);
      }
      if (error.Fail())
        module_sp.reset();
      return error;
    }

    // Make sure no one else can try and get or create a module while this
    // function is actively working on it by doing an extra lock on the global
    // mutex list.
    ModuleSpec platform_module_spec(module_spec);
    platform_module_spec.GetFileSpec() =
        located_binary_modulespec.GetFileSpec();
    platform_module_spec.GetPlatformFileSpec() =
        located_binary_modulespec.GetFileSpec();
    platform_module_spec.GetSymbolFileSpec() =
        located_binary_modulespec.GetSymbolFileSpec();
    ModuleList matching_module_list;
    if (shared_module_list.FindModules(platform_module_spec,
                                       matching_module_list) > 0) {
      module_sp = matching_module_list.GetModuleAtIndex(0);

      // If we didn't have a UUID in mind when looking for the object file,
      // then we should make sure the modification time hasn't changed!
      if (platform_module_spec.GetUUIDPtr() == nullptr) {
        auto file_spec_mod_time = FileSystem::Instance().GetModificationTime(
            located_binary_modulespec.GetFileSpec());
        if (file_spec_mod_time != llvm::sys::TimePoint<>()) {
          if (file_spec_mod_time != module_sp->GetModificationTime()) {
            if (old_module_sp_ptr)
              *old_module_sp_ptr = module_sp;
            shared_module_list.Remove(module_sp);
            module_sp.reset();
          }
        }
      }
    }

    if (!module_sp) {
      module_sp.reset(new Module(platform_module_spec));
      // Make sure there are a module and an object file since we can specify a
      // valid file path with an architecture that might not be in that file.
      // By getting the object file we can guarantee that the architecture
      // matches
      if (module_sp && module_sp->GetObjectFile()) {
        if (module_sp->GetObjectFile()->GetType() ==
            ObjectFile::eTypeStubLibrary) {
          module_sp.reset();
        } else {
          if (did_create_ptr)
            *did_create_ptr = true;

          shared_module_list.ReplaceEquivalent(module_sp);
        }
      } else {
        located_binary_modulespec.GetFileSpec().GetPath(path, sizeof(path));

        if (located_binary_modulespec.GetFileSpec()) {
          if (arch.IsValid())
            error.SetErrorStringWithFormat(
                "unable to open %s architecture in '%s'",
                arch.GetArchitectureName(), path);
          else
            error.SetErrorStringWithFormat("unable to open '%s'", path);
        } else {
          std::string uuid_str;
          if (uuid_ptr && uuid_ptr->IsValid())
            uuid_str = uuid_ptr->GetAsString();

          if (!uuid_str.empty())
            error.SetErrorStringWithFormat(
                "cannot locate a module for UUID '%s'", uuid_str.c_str());
          else
            error.SetErrorStringWithFormat("cannot locate a module");
        }
      }
    }
  }

  return error;
}

bool ModuleList::RemoveSharedModule(lldb::ModuleSP &module_sp) {
  return GetSharedModuleList().Remove(module_sp);
}

bool ModuleList::RemoveSharedModuleIfOrphaned(const Module *module_ptr) {
  return GetSharedModuleList().RemoveIfOrphaned(module_ptr);
}

bool ModuleList::LoadScriptingResourcesInTarget(Target *target,
                                                std::list<Status> &errors,
                                                Stream *feedback_stream,
                                                bool continue_on_error) {
  if (!target)
    return false;
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (auto module : m_modules) {
    Status error;
    if (module) {
      if (!module->LoadScriptingResourceInTarget(target, error,
                                                 feedback_stream)) {
        if (error.Fail() && error.AsCString()) {
          error.SetErrorStringWithFormat("unable to load scripting data for "
                                         "module %s - error reported was %s",
                                         module->GetFileSpec()
                                             .GetFileNameStrippingExtension()
                                             .GetCString(),
                                         error.AsCString());
          errors.push_back(error);

          if (!continue_on_error)
            return false;
        }
      }
    }
  }
  return errors.empty();
}

void ModuleList::ForEach(
    std::function<bool(const ModuleSP &module_sp)> const &callback) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const auto &module : m_modules) {
    // If the callback returns false, then stop iterating and break out
    if (!callback(module))
      break;
  }
}
