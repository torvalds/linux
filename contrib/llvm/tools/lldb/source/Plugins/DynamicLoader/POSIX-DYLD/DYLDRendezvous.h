//===-- DYLDRendezvous.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Rendezvous_H_
#define liblldb_Rendezvous_H_

#include <list>
#include <string>

#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "lldb/Core/LoadedModuleInfoList.h"

using lldb_private::LoadedModuleInfoList;

namespace lldb_private {
class Process;
}

/// @class DYLDRendezvous
/// Interface to the runtime linker.
///
/// A structure is present in a processes memory space which is updated by the
/// runtime liker each time a module is loaded or unloaded.  This class
/// provides an interface to this structure and maintains a consistent
/// snapshot of the currently loaded modules.
class DYLDRendezvous {

  // This structure is used to hold the contents of the debug rendezvous
  // information (struct r_debug) as found in the inferiors memory.  Note that
  // the layout of this struct is not binary compatible, it is simply large
  // enough to hold the information on both 32 and 64 bit platforms.
  struct Rendezvous {
    uint64_t version;
    lldb::addr_t map_addr;
    lldb::addr_t brk;
    uint64_t state;
    lldb::addr_t ldbase;

    Rendezvous() : version(0), map_addr(0), brk(0), state(0), ldbase(0) {}
  };

public:
  // Various metadata supplied by the inferior's threading library to describe
  // the per-thread state.
  struct ThreadInfo {
    bool valid;             // whether we read valid metadata
    uint32_t dtv_offset;    // offset of DTV pointer within pthread
    uint32_t dtv_slot_size; // size of one DTV slot
    uint32_t modid_offset;  // offset of module ID within link_map
    uint32_t tls_offset;    // offset of TLS pointer within DTV slot
  };

  DYLDRendezvous(lldb_private::Process *process);

  /// Update the internal snapshot of runtime linker rendezvous and recompute
  /// the currently loaded modules.
  ///
  /// This method should be called once one start up, then once each time the
  /// runtime linker enters the function given by GetBreakAddress().
  ///
  /// @returns true on success and false on failure.
  ///
  /// @see GetBreakAddress().
  bool Resolve();

  /// @returns true if this rendezvous has been located in the inferiors
  /// address space and false otherwise.
  bool IsValid();

  /// @returns the address of the rendezvous structure in the inferiors
  /// address space.
  lldb::addr_t GetRendezvousAddress() const { return m_rendezvous_addr; }

  /// @returns the version of the rendezvous protocol being used.
  uint64_t GetVersion() const { return m_current.version; }

  /// @returns address in the inferiors address space containing the linked
  /// list of shared object descriptors.
  lldb::addr_t GetLinkMapAddress() const { return m_current.map_addr; }

  /// A breakpoint should be set at this address and Resolve called on each
  /// hit.
  ///
  /// @returns the address of a function called by the runtime linker each
  /// time a module is loaded/unloaded, or about to be loaded/unloaded.
  ///
  /// @see Resolve()
  lldb::addr_t GetBreakAddress() const { return m_current.brk; }

  /// Returns the current state of the rendezvous structure.
  uint64_t GetState() const { return m_current.state; }

  /// @returns the base address of the runtime linker in the inferiors address
  /// space.
  lldb::addr_t GetLDBase() const { return m_current.ldbase; }

  /// @returns the thread layout metadata from the inferiors thread library.
  const ThreadInfo &GetThreadInfo();

  /// @returns true if modules have been loaded into the inferior since the
  /// last call to Resolve().
  bool ModulesDidLoad() const { return !m_added_soentries.empty(); }

  /// @returns true if modules have been unloaded from the inferior since the
  /// last call to Resolve().
  bool ModulesDidUnload() const { return !m_removed_soentries.empty(); }

  void DumpToLog(lldb_private::Log *log) const;

  /// Constants describing the state of the rendezvous.
  ///
  /// @see GetState().
  enum RendezvousState { eConsistent, eAdd, eDelete };

  /// Structure representing the shared objects currently loaded into the
  /// inferior process.
  ///
  /// This object is a rough analogue to the struct link_map object which
  /// actually lives in the inferiors memory.
  struct SOEntry {
    lldb::addr_t link_addr;           ///< Address of this link_map.
    lldb::addr_t base_addr;           ///< Base address of the loaded object.
    lldb::addr_t path_addr;           ///< String naming the shared object.
    lldb::addr_t dyn_addr;            ///< Dynamic section of shared object.
    lldb::addr_t next;                ///< Address of next so_entry.
    lldb::addr_t prev;                ///< Address of previous so_entry.
    lldb_private::FileSpec file_spec; ///< File spec of shared object.

    SOEntry() { clear(); }

    bool operator==(const SOEntry &entry) {
      return file_spec == entry.file_spec;
    }

    void clear() {
      link_addr = 0;
      base_addr = 0;
      path_addr = 0;
      dyn_addr = 0;
      next = 0;
      prev = 0;
      file_spec.Clear();
    }
  };

protected:
  typedef std::list<SOEntry> SOEntryList;

public:
  typedef SOEntryList::const_iterator iterator;

  /// Iterators over all currently loaded modules.
  iterator begin() const { return m_soentries.begin(); }
  iterator end() const { return m_soentries.end(); }

  /// Iterators over all modules loaded into the inferior since the last call
  /// to Resolve().
  iterator loaded_begin() const { return m_added_soentries.begin(); }
  iterator loaded_end() const { return m_added_soentries.end(); }

  /// Iterators over all modules unloaded from the inferior since the last
  /// call to Resolve().
  iterator unloaded_begin() const { return m_removed_soentries.begin(); }
  iterator unloaded_end() const { return m_removed_soentries.end(); }

protected:
  lldb_private::Process *m_process;

  // Cached copy of executable file spec
  lldb_private::FileSpec m_exe_file_spec;

  /// Location of the r_debug structure in the inferiors address space.
  lldb::addr_t m_rendezvous_addr;

  /// Current and previous snapshots of the rendezvous structure.
  Rendezvous m_current;
  Rendezvous m_previous;

  /// List of currently loaded SO modules
  LoadedModuleInfoList m_loaded_modules;

  /// List of SOEntry objects corresponding to the current link map state.
  SOEntryList m_soentries;

  /// List of SOEntry's added to the link map since the last call to
  /// Resolve().
  SOEntryList m_added_soentries;

  /// List of SOEntry's removed from the link map since the last call to
  /// Resolve().
  SOEntryList m_removed_soentries;

  /// Threading metadata read from the inferior.
  ThreadInfo m_thread_info;

  /// Reads an unsigned integer of @p size bytes from the inferior's address
  /// space starting at @p addr.
  ///
  /// @returns addr + size if the read was successful and false otherwise.
  lldb::addr_t ReadWord(lldb::addr_t addr, uint64_t *dst, size_t size);

  /// Reads an address from the inferior's address space starting at @p addr.
  ///
  /// @returns addr + target address size if the read was successful and
  /// 0 otherwise.
  lldb::addr_t ReadPointer(lldb::addr_t addr, lldb::addr_t *dst);

  /// Reads a null-terminated C string from the memory location starting at @p
  /// addr.
  std::string ReadStringFromMemory(lldb::addr_t addr);

  /// Reads an SOEntry starting at @p addr.
  bool ReadSOEntryFromMemory(lldb::addr_t addr, SOEntry &entry);

  /// Updates the current set of SOEntries, the set of added entries, and the
  /// set of removed entries.
  bool UpdateSOEntries(bool fromRemote = false);

  bool FillSOEntryFromModuleInfo(
      LoadedModuleInfoList::LoadedModuleInfo const &modInfo, SOEntry &entry);

  bool SaveSOEntriesFromRemote(LoadedModuleInfoList &module_list);

  bool AddSOEntriesFromRemote(LoadedModuleInfoList &module_list);

  bool RemoveSOEntriesFromRemote(LoadedModuleInfoList &module_list);

  bool AddSOEntries();

  bool RemoveSOEntries();

  void UpdateBaseAddrIfNecessary(SOEntry &entry, std::string const &file_path);

  bool SOEntryIsMainExecutable(const SOEntry &entry);

  /// Reads the current list of shared objects according to the link map
  /// supplied by the runtime linker.
  bool TakeSnapshot(SOEntryList &entry_list);

  enum PThreadField { eSize, eNElem, eOffset };

  bool FindMetadata(const char *name, PThreadField field, uint32_t &value);
};

#endif
