//===-- DYLDRendezvous.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/Path.h"

#include "DYLDRendezvous.h"

using namespace lldb;
using namespace lldb_private;

/// Locates the address of the rendezvous structure.  Returns the address on
/// success and LLDB_INVALID_ADDRESS on failure.
static addr_t ResolveRendezvousAddress(Process *process) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));
  addr_t info_location;
  addr_t info_addr;
  Status error;

  if (!process) {
    if (log)
      log->Printf("%s null process provided", __FUNCTION__);
    return LLDB_INVALID_ADDRESS;
  }

  // Try to get it from our process.  This might be a remote process and might
  // grab it via some remote-specific mechanism.
  info_location = process->GetImageInfoAddress();
  if (log)
    log->Printf("%s info_location = 0x%" PRIx64, __FUNCTION__, info_location);

  // If the process fails to return an address, fall back to seeing if the
  // local object file can help us find it.
  if (info_location == LLDB_INVALID_ADDRESS) {
    Target *target = &process->GetTarget();
    if (target) {
      ObjectFile *obj_file = target->GetExecutableModule()->GetObjectFile();
      Address addr = obj_file->GetImageInfoAddress(target);

      if (addr.IsValid()) {
        info_location = addr.GetLoadAddress(target);
        if (log)
          log->Printf(
              "%s resolved via direct object file approach to 0x%" PRIx64,
              __FUNCTION__, info_location);
      } else {
        if (log)
          log->Printf("%s FAILED - direct object file approach did not yield a "
                      "valid address",
                      __FUNCTION__);
      }
    }
  }

  if (info_location == LLDB_INVALID_ADDRESS) {
    if (log)
      log->Printf("%s FAILED - invalid info address", __FUNCTION__);
    return LLDB_INVALID_ADDRESS;
  }

  if (log)
    log->Printf("%s reading pointer (%" PRIu32 " bytes) from 0x%" PRIx64,
                __FUNCTION__, process->GetAddressByteSize(), info_location);

  info_addr = process->ReadPointerFromMemory(info_location, error);
  if (error.Fail()) {
    if (log)
      log->Printf("%s FAILED - could not read from the info location: %s",
                  __FUNCTION__, error.AsCString());
    return LLDB_INVALID_ADDRESS;
  }

  if (info_addr == 0) {
    if (log)
      log->Printf("%s FAILED - the rendezvous address contained at 0x%" PRIx64
                  " returned a null value",
                  __FUNCTION__, info_location);
    return LLDB_INVALID_ADDRESS;
  }

  return info_addr;
}

DYLDRendezvous::DYLDRendezvous(Process *process)
    : m_process(process), m_rendezvous_addr(LLDB_INVALID_ADDRESS), m_current(),
      m_previous(), m_loaded_modules(), m_soentries(), m_added_soentries(),
      m_removed_soentries() {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

  m_thread_info.valid = false;

  // Cache a copy of the executable path
  if (m_process) {
    Module *exe_mod = m_process->GetTarget().GetExecutableModulePointer();
    if (exe_mod) {
      m_exe_file_spec = exe_mod->GetPlatformFileSpec();
      if (log)
        log->Printf("DYLDRendezvous::%s exe module executable path set: '%s'",
                    __FUNCTION__, m_exe_file_spec.GetCString());
    } else {
      if (log)
        log->Printf("DYLDRendezvous::%s cannot cache exe module path: null "
                    "executable module pointer",
                    __FUNCTION__);
    }
  }
}

bool DYLDRendezvous::Resolve() {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

  const size_t word_size = 4;
  Rendezvous info;
  size_t address_size;
  size_t padding;
  addr_t info_addr;
  addr_t cursor;

  address_size = m_process->GetAddressByteSize();
  padding = address_size - word_size;
  if (log)
    log->Printf("DYLDRendezvous::%s address size: %" PRIu64
                ", padding %" PRIu64,
                __FUNCTION__, uint64_t(address_size), uint64_t(padding));

  if (m_rendezvous_addr == LLDB_INVALID_ADDRESS)
    cursor = info_addr = ResolveRendezvousAddress(m_process);
  else
    cursor = info_addr = m_rendezvous_addr;
  if (log)
    log->Printf("DYLDRendezvous::%s cursor = 0x%" PRIx64, __FUNCTION__, cursor);

  if (cursor == LLDB_INVALID_ADDRESS)
    return false;

  if (!(cursor = ReadWord(cursor, &info.version, word_size)))
    return false;

  if (!(cursor = ReadPointer(cursor + padding, &info.map_addr)))
    return false;

  if (!(cursor = ReadPointer(cursor, &info.brk)))
    return false;

  if (!(cursor = ReadWord(cursor, &info.state, word_size)))
    return false;

  if (!(cursor = ReadPointer(cursor + padding, &info.ldbase)))
    return false;

  // The rendezvous was successfully read.  Update our internal state.
  m_rendezvous_addr = info_addr;
  m_previous = m_current;
  m_current = info;

  if (UpdateSOEntries(true))
    return true;

  return UpdateSOEntries();
}

bool DYLDRendezvous::IsValid() {
  return m_rendezvous_addr != LLDB_INVALID_ADDRESS;
}

bool DYLDRendezvous::UpdateSOEntries(bool fromRemote) {
  SOEntry entry;
  LoadedModuleInfoList module_list;

  // If we can't get the SO info from the remote, return failure.
  if (fromRemote && m_process->LoadModules(module_list) == 0)
    return false;

  if (!fromRemote && m_current.map_addr == 0)
    return false;

  // When the previous and current states are consistent this is the first time
  // we have been asked to update.  Just take a snapshot of the currently
  // loaded modules.
  if (m_previous.state == eConsistent && m_current.state == eConsistent)
    return fromRemote ? SaveSOEntriesFromRemote(module_list)
                      : TakeSnapshot(m_soentries);

  // If we are about to add or remove a shared object clear out the current
  // state and take a snapshot of the currently loaded images.
  if (m_current.state == eAdd || m_current.state == eDelete) {
    // Some versions of the android dynamic linker might send two notifications
    // with state == eAdd back to back. Ignore them until we get an eConsistent
    // notification.
    if (!(m_previous.state == eConsistent ||
          (m_previous.state == eAdd && m_current.state == eDelete)))
      return false;

    m_soentries.clear();
    if (fromRemote)
      return SaveSOEntriesFromRemote(module_list);

    m_added_soentries.clear();
    m_removed_soentries.clear();
    return TakeSnapshot(m_soentries);
  }
  assert(m_current.state == eConsistent);

  // Otherwise check the previous state to determine what to expect and update
  // accordingly.
  if (m_previous.state == eAdd)
    return fromRemote ? AddSOEntriesFromRemote(module_list) : AddSOEntries();
  else if (m_previous.state == eDelete)
    return fromRemote ? RemoveSOEntriesFromRemote(module_list)
                      : RemoveSOEntries();

  return false;
}

bool DYLDRendezvous::FillSOEntryFromModuleInfo(
    LoadedModuleInfoList::LoadedModuleInfo const &modInfo, SOEntry &entry) {
  addr_t link_map_addr;
  addr_t base_addr;
  addr_t dyn_addr;
  std::string name;

  if (!modInfo.get_link_map(link_map_addr) || !modInfo.get_base(base_addr) ||
      !modInfo.get_dynamic(dyn_addr) || !modInfo.get_name(name))
    return false;

  entry.link_addr = link_map_addr;
  entry.base_addr = base_addr;
  entry.dyn_addr = dyn_addr;

  entry.file_spec.SetFile(name, FileSpec::Style::native);

  UpdateBaseAddrIfNecessary(entry, name);

  // not needed if we're using ModuleInfos
  entry.next = 0;
  entry.prev = 0;
  entry.path_addr = 0;

  return true;
}

bool DYLDRendezvous::SaveSOEntriesFromRemote(
    LoadedModuleInfoList &module_list) {
  for (auto const &modInfo : module_list.m_list) {
    SOEntry entry;
    if (!FillSOEntryFromModuleInfo(modInfo, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (!SOEntryIsMainExecutable(entry))
      m_soentries.push_back(entry);
  }

  m_loaded_modules = module_list;
  return true;
}

bool DYLDRendezvous::AddSOEntriesFromRemote(LoadedModuleInfoList &module_list) {
  for (auto const &modInfo : module_list.m_list) {
    bool found = false;
    for (auto const &existing : m_loaded_modules.m_list) {
      if (modInfo == existing) {
        found = true;
        break;
      }
    }

    if (found)
      continue;

    SOEntry entry;
    if (!FillSOEntryFromModuleInfo(modInfo, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (!SOEntryIsMainExecutable(entry))
      m_soentries.push_back(entry);
  }

  m_loaded_modules = module_list;
  return true;
}

bool DYLDRendezvous::RemoveSOEntriesFromRemote(
    LoadedModuleInfoList &module_list) {
  for (auto const &existing : m_loaded_modules.m_list) {
    bool found = false;
    for (auto const &modInfo : module_list.m_list) {
      if (modInfo == existing) {
        found = true;
        break;
      }
    }

    if (found)
      continue;

    SOEntry entry;
    if (!FillSOEntryFromModuleInfo(existing, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (!SOEntryIsMainExecutable(entry)) {
      auto pos = std::find(m_soentries.begin(), m_soentries.end(), entry);
      if (pos == m_soentries.end())
        return false;

      m_soentries.erase(pos);
    }
  }

  m_loaded_modules = module_list;
  return true;
}

bool DYLDRendezvous::AddSOEntries() {
  SOEntry entry;
  iterator pos;

  assert(m_previous.state == eAdd);

  if (m_current.map_addr == 0)
    return false;

  for (addr_t cursor = m_current.map_addr; cursor != 0; cursor = entry.next) {
    if (!ReadSOEntryFromMemory(cursor, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (SOEntryIsMainExecutable(entry))
      continue;

    pos = std::find(m_soentries.begin(), m_soentries.end(), entry);
    if (pos == m_soentries.end()) {
      m_soentries.push_back(entry);
      m_added_soentries.push_back(entry);
    }
  }

  return true;
}

bool DYLDRendezvous::RemoveSOEntries() {
  SOEntryList entry_list;
  iterator pos;

  assert(m_previous.state == eDelete);

  if (!TakeSnapshot(entry_list))
    return false;

  for (iterator I = begin(); I != end(); ++I) {
    pos = std::find(entry_list.begin(), entry_list.end(), *I);
    if (pos == entry_list.end())
      m_removed_soentries.push_back(*I);
  }

  m_soentries = entry_list;
  return true;
}

bool DYLDRendezvous::SOEntryIsMainExecutable(const SOEntry &entry) {
  // On some systes the executable is indicated by an empty path in the entry.
  // On others it is the full path to the executable.

  auto triple = m_process->GetTarget().GetArchitecture().GetTriple();
  switch (triple.getOS()) {
  case llvm::Triple::FreeBSD:
  case llvm::Triple::NetBSD:
    return entry.file_spec == m_exe_file_spec;
  case llvm::Triple::Linux:
    if (triple.isAndroid())
      return entry.file_spec == m_exe_file_spec;
    return !entry.file_spec;
  default:
    return false;
  }
}

bool DYLDRendezvous::TakeSnapshot(SOEntryList &entry_list) {
  SOEntry entry;

  if (m_current.map_addr == 0)
    return false;

  // Clear previous entries since we are about to obtain an up to date list.
  entry_list.clear();

  for (addr_t cursor = m_current.map_addr; cursor != 0; cursor = entry.next) {
    if (!ReadSOEntryFromMemory(cursor, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (SOEntryIsMainExecutable(entry))
      continue;

    entry_list.push_back(entry);
  }

  return true;
}

addr_t DYLDRendezvous::ReadWord(addr_t addr, uint64_t *dst, size_t size) {
  Status error;

  *dst = m_process->ReadUnsignedIntegerFromMemory(addr, size, 0, error);
  if (error.Fail())
    return 0;

  return addr + size;
}

addr_t DYLDRendezvous::ReadPointer(addr_t addr, addr_t *dst) {
  Status error;

  *dst = m_process->ReadPointerFromMemory(addr, error);
  if (error.Fail())
    return 0;

  return addr + m_process->GetAddressByteSize();
}

std::string DYLDRendezvous::ReadStringFromMemory(addr_t addr) {
  std::string str;
  Status error;

  if (addr == LLDB_INVALID_ADDRESS)
    return std::string();

  m_process->ReadCStringFromMemory(addr, str, error);

  return str;
}

// Returns true if the load bias reported by the linker is incorrect for the
// given entry. This function is used to handle cases where we want to work
// around a bug in the system linker.
static bool isLoadBiasIncorrect(Target &target, const std::string &file_path) {
  // On Android L (API 21, 22) the load address of the "/system/bin/linker"
  // isn't filled in correctly.
  unsigned os_major = target.GetPlatform()->GetOSVersion().getMajor();
  return target.GetArchitecture().GetTriple().isAndroid() &&
         (os_major == 21 || os_major == 22) &&
         (file_path == "/system/bin/linker" ||
          file_path == "/system/bin/linker64");
}

void DYLDRendezvous::UpdateBaseAddrIfNecessary(SOEntry &entry,
                                               std::string const &file_path) {
  // If the load bias reported by the linker is incorrect then fetch the load
  // address of the file from the proc file system.
  if (isLoadBiasIncorrect(m_process->GetTarget(), file_path)) {
    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    bool is_loaded = false;
    Status error =
        m_process->GetFileLoadAddress(entry.file_spec, is_loaded, load_addr);
    if (error.Success() && is_loaded)
      entry.base_addr = load_addr;
  }
}

bool DYLDRendezvous::ReadSOEntryFromMemory(lldb::addr_t addr, SOEntry &entry) {
  entry.clear();

  entry.link_addr = addr;

  if (!(addr = ReadPointer(addr, &entry.base_addr)))
    return false;

  // mips adds an extra load offset field to the link map struct on FreeBSD and
  // NetBSD (need to validate other OSes).
  // http://svnweb.freebsd.org/base/head/sys/sys/link_elf.h?revision=217153&view=markup#l57
  const ArchSpec &arch = m_process->GetTarget().GetArchitecture();
  if ((arch.GetTriple().getOS() == llvm::Triple::FreeBSD ||
       arch.GetTriple().getOS() == llvm::Triple::NetBSD) &&
      (arch.GetMachine() == llvm::Triple::mips ||
       arch.GetMachine() == llvm::Triple::mipsel ||
       arch.GetMachine() == llvm::Triple::mips64 ||
       arch.GetMachine() == llvm::Triple::mips64el)) {
    addr_t mips_l_offs;
    if (!(addr = ReadPointer(addr, &mips_l_offs)))
      return false;
    if (mips_l_offs != 0 && mips_l_offs != entry.base_addr)
      return false;
  }

  if (!(addr = ReadPointer(addr, &entry.path_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.dyn_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.next)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.prev)))
    return false;

  std::string file_path = ReadStringFromMemory(entry.path_addr);
  entry.file_spec.SetFile(file_path, FileSpec::Style::native);

  UpdateBaseAddrIfNecessary(entry, file_path);

  return true;
}

bool DYLDRendezvous::FindMetadata(const char *name, PThreadField field,
                                  uint32_t &value) {
  Target &target = m_process->GetTarget();

  SymbolContextList list;
  if (!target.GetImages().FindSymbolsWithNameAndType(ConstString(name),
                                                     eSymbolTypeAny, list))
    return false;

  Address address = list[0].symbol->GetAddress();
  addr_t addr = address.GetLoadAddress(&target);
  if (addr == LLDB_INVALID_ADDRESS)
    return false;

  Status error;
  value = (uint32_t)m_process->ReadUnsignedIntegerFromMemory(
      addr + field * sizeof(uint32_t), sizeof(uint32_t), 0, error);
  if (error.Fail())
    return false;

  if (field == eSize)
    value /= 8; // convert bits to bytes

  return true;
}

const DYLDRendezvous::ThreadInfo &DYLDRendezvous::GetThreadInfo() {
  if (!m_thread_info.valid) {
    bool ok = true;

    ok &= FindMetadata("_thread_db_pthread_dtvp", eOffset,
                       m_thread_info.dtv_offset);
    ok &=
        FindMetadata("_thread_db_dtv_dtv", eSize, m_thread_info.dtv_slot_size);
    ok &= FindMetadata("_thread_db_link_map_l_tls_modid", eOffset,
                       m_thread_info.modid_offset);
    ok &= FindMetadata("_thread_db_dtv_t_pointer_val", eOffset,
                       m_thread_info.tls_offset);

    if (ok)
      m_thread_info.valid = true;
  }

  return m_thread_info;
}

void DYLDRendezvous::DumpToLog(Log *log) const {
  int state = GetState();

  if (!log)
    return;

  log->PutCString("DYLDRendezvous:");
  log->Printf("   Address: %" PRIx64, GetRendezvousAddress());
  log->Printf("   Version: %" PRIu64, GetVersion());
  log->Printf("   Link   : %" PRIx64, GetLinkMapAddress());
  log->Printf("   Break  : %" PRIx64, GetBreakAddress());
  log->Printf("   LDBase : %" PRIx64, GetLDBase());
  log->Printf("   State  : %s",
              (state == eConsistent)
                  ? "consistent"
                  : (state == eAdd) ? "add" : (state == eDelete) ? "delete"
                                                                 : "unknown");

  iterator I = begin();
  iterator E = end();

  if (I != E)
    log->PutCString("DYLDRendezvous SOEntries:");

  for (int i = 1; I != E; ++I, ++i) {
    log->Printf("\n   SOEntry [%d] %s", i, I->file_spec.GetCString());
    log->Printf("      Base : %" PRIx64, I->base_addr);
    log->Printf("      Path : %" PRIx64, I->path_addr);
    log->Printf("      Dyn  : %" PRIx64, I->dyn_addr);
    log->Printf("      Next : %" PRIx64, I->next);
    log->Printf("      Prev : %" PRIx64, I->prev);
  }
}
