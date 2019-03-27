//===-- HexagonDYLDRendezvous.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

#include "HexagonDYLDRendezvous.h"

using namespace lldb;
using namespace lldb_private;

/// Locates the address of the rendezvous structure.  Returns the address on
/// success and LLDB_INVALID_ADDRESS on failure.
static addr_t ResolveRendezvousAddress(Process *process) {
  addr_t info_location;
  addr_t info_addr;
  Status error;

  info_location = process->GetImageInfoAddress();

  if (info_location == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;

  info_addr = process->ReadPointerFromMemory(info_location, error);
  if (error.Fail())
    return LLDB_INVALID_ADDRESS;

  if (info_addr == 0)
    return LLDB_INVALID_ADDRESS;

  return info_addr;
}

HexagonDYLDRendezvous::HexagonDYLDRendezvous(Process *process)
    : m_process(process), m_rendezvous_addr(LLDB_INVALID_ADDRESS), m_current(),
      m_previous(), m_soentries(), m_added_soentries(), m_removed_soentries() {
  m_thread_info.valid = false;

  // Cache a copy of the executable path
  if (m_process) {
    Module *exe_mod = m_process->GetTarget().GetExecutableModulePointer();
    if (exe_mod)
      exe_mod->GetFileSpec().GetPath(m_exe_path, PATH_MAX);
  }
}

bool HexagonDYLDRendezvous::Resolve() {
  const size_t word_size = 4;
  Rendezvous info;
  size_t address_size;
  size_t padding;
  addr_t info_addr;
  addr_t cursor;

  address_size = m_process->GetAddressByteSize();
  padding = address_size - word_size;

  if (m_rendezvous_addr == LLDB_INVALID_ADDRESS)
    cursor = info_addr = ResolveRendezvousAddress(m_process);
  else
    cursor = info_addr = m_rendezvous_addr;

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

  return UpdateSOEntries();
}

void HexagonDYLDRendezvous::SetRendezvousAddress(lldb::addr_t addr) {
  m_rendezvous_addr = addr;
}

bool HexagonDYLDRendezvous::IsValid() {
  return m_rendezvous_addr != LLDB_INVALID_ADDRESS;
}

bool HexagonDYLDRendezvous::UpdateSOEntries() {
  SOEntry entry;

  if (m_current.map_addr == 0)
    return false;

  // When the previous and current states are consistent this is the first time
  // we have been asked to update.  Just take a snapshot of the currently
  // loaded modules.
  if (m_previous.state == eConsistent && m_current.state == eConsistent)
    return TakeSnapshot(m_soentries);

  // If we are about to add or remove a shared object clear out the current
  // state and take a snapshot of the currently loaded images.
  if (m_current.state == eAdd || m_current.state == eDelete) {
    // this is a fudge so that we can clear the assert below.
    m_previous.state = eConsistent;
    // We hit this assert on the 2nd run of this function after running the
    // calc example
    assert(m_previous.state == eConsistent);
    m_soentries.clear();
    m_added_soentries.clear();
    m_removed_soentries.clear();
    return TakeSnapshot(m_soentries);
  }
  assert(m_current.state == eConsistent);

  // Otherwise check the previous state to determine what to expect and update
  // accordingly.
  if (m_previous.state == eAdd)
    return UpdateSOEntriesForAddition();
  else if (m_previous.state == eDelete)
    return UpdateSOEntriesForDeletion();

  return false;
}

bool HexagonDYLDRendezvous::UpdateSOEntriesForAddition() {
  SOEntry entry;
  iterator pos;

  assert(m_previous.state == eAdd);

  if (m_current.map_addr == 0)
    return false;

  for (addr_t cursor = m_current.map_addr; cursor != 0; cursor = entry.next) {
    if (!ReadSOEntryFromMemory(cursor, entry))
      return false;

    // Only add shared libraries and not the executable. On Linux this is
    // indicated by an empty path in the entry. On FreeBSD it is the name of
    // the executable.
    if (entry.path.empty() || ::strcmp(entry.path.c_str(), m_exe_path) == 0)
      continue;

    pos = std::find(m_soentries.begin(), m_soentries.end(), entry);
    if (pos == m_soentries.end()) {
      m_soentries.push_back(entry);
      m_added_soentries.push_back(entry);
    }
  }

  return true;
}

bool HexagonDYLDRendezvous::UpdateSOEntriesForDeletion() {
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

bool HexagonDYLDRendezvous::TakeSnapshot(SOEntryList &entry_list) {
  SOEntry entry;

  if (m_current.map_addr == 0)
    return false;

  for (addr_t cursor = m_current.map_addr; cursor != 0; cursor = entry.next) {
    if (!ReadSOEntryFromMemory(cursor, entry))
      return false;

    // Only add shared libraries and not the executable. On Linux this is
    // indicated by an empty path in the entry. On FreeBSD it is the name of
    // the executable.
    if (entry.path.empty() || ::strcmp(entry.path.c_str(), m_exe_path) == 0)
      continue;

    entry_list.push_back(entry);
  }

  return true;
}

addr_t HexagonDYLDRendezvous::ReadWord(addr_t addr, uint64_t *dst,
                                       size_t size) {
  Status error;

  *dst = m_process->ReadUnsignedIntegerFromMemory(addr, size, 0, error);
  if (error.Fail())
    return 0;

  return addr + size;
}

addr_t HexagonDYLDRendezvous::ReadPointer(addr_t addr, addr_t *dst) {
  Status error;

  *dst = m_process->ReadPointerFromMemory(addr, error);
  if (error.Fail())
    return 0;

  return addr + m_process->GetAddressByteSize();
}

std::string HexagonDYLDRendezvous::ReadStringFromMemory(addr_t addr) {
  std::string str;
  Status error;
  size_t size;
  char c;

  if (addr == LLDB_INVALID_ADDRESS)
    return std::string();

  for (;;) {
    size = m_process->DoReadMemory(addr, &c, 1, error);
    if (size != 1 || error.Fail())
      return std::string();
    if (c == 0)
      break;
    else {
      str.push_back(c);
      addr++;
    }
  }

  return str;
}

bool HexagonDYLDRendezvous::ReadSOEntryFromMemory(lldb::addr_t addr,
                                                  SOEntry &entry) {
  entry.clear();
  entry.link_addr = addr;

  if (!(addr = ReadPointer(addr, &entry.base_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.path_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.dyn_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.next)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.prev)))
    return false;

  entry.path = ReadStringFromMemory(entry.path_addr);

  return true;
}

bool HexagonDYLDRendezvous::FindMetadata(const char *name, PThreadField field,
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

const HexagonDYLDRendezvous::ThreadInfo &
HexagonDYLDRendezvous::GetThreadInfo() {
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

void HexagonDYLDRendezvous::DumpToLog(Log *log) const {
  int state = GetState();

  if (!log)
    return;

  log->PutCString("HexagonDYLDRendezvous:");
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
    log->PutCString("HexagonDYLDRendezvous SOEntries:");

  for (int i = 1; I != E; ++I, ++i) {
    log->Printf("\n   SOEntry [%d] %s", i, I->path.c_str());
    log->Printf("      Base : %" PRIx64, I->base_addr);
    log->Printf("      Path : %" PRIx64, I->path_addr);
    log->Printf("      Dyn  : %" PRIx64, I->dyn_addr);
    log->Printf("      Next : %" PRIx64, I->next);
    log->Printf("      Prev : %" PRIx64, I->prev);
  }
}
