//===-- Symtab.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_UnwindTable_h
#define liblldb_UnwindTable_h

#include <map>
#include <mutex>

#include "lldb/lldb-private.h"

namespace lldb_private {

// A class which holds all the FuncUnwinders objects for a given ObjectFile.
// The UnwindTable is populated with FuncUnwinders objects lazily during the
// debug session.

class UnwindTable {
public:
  UnwindTable(ObjectFile &objfile);
  ~UnwindTable();

  lldb_private::DWARFCallFrameInfo *GetEHFrameInfo();
  lldb_private::DWARFCallFrameInfo *GetDebugFrameInfo();

  lldb_private::CompactUnwindInfo *GetCompactUnwindInfo();

  ArmUnwindInfo *GetArmUnwindInfo();

  lldb::FuncUnwindersSP GetFuncUnwindersContainingAddress(const Address &addr,
                                                          SymbolContext &sc);

  bool GetAllowAssemblyEmulationUnwindPlans();

  // Normally when we create a new FuncUnwinders object we track it in this
  // UnwindTable so it can be reused later.  But for the target modules show-
  // unwind we want to create brand new UnwindPlans for the function of
  // interest - so ignore any existing FuncUnwinders for that function and
  // don't add this new one to our UnwindTable. This FuncUnwinders object does
  // have a reference to the UnwindTable but the lifetime of this uncached
  // FuncUnwinders is expected to be short so in practice this will not be a
  // problem.
  lldb::FuncUnwindersSP
  GetUncachedFuncUnwindersContainingAddress(const Address &addr,
                                            SymbolContext &sc);

  ArchSpec GetArchitecture();

private:
  void Dump(Stream &s);

  void Initialize();
  llvm::Optional<AddressRange> GetAddressRange(const Address &addr,
                                               SymbolContext &sc);

  typedef std::map<lldb::addr_t, lldb::FuncUnwindersSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  ObjectFile &m_object_file;
  collection m_unwinds;

  bool m_initialized; // delay some initialization until ObjectFile is set up
  std::mutex m_mutex;

  std::unique_ptr<DWARFCallFrameInfo> m_eh_frame_up;
  std::unique_ptr<DWARFCallFrameInfo> m_debug_frame_up;
  std::unique_ptr<CompactUnwindInfo> m_compact_unwind_up;
  std::unique_ptr<ArmUnwindInfo> m_arm_unwind_up;

  DISALLOW_COPY_AND_ASSIGN(UnwindTable);
};

} // namespace lldb_private

#endif // liblldb_UnwindTable_h
