//===-- SBMemoryRegionInfo.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBMemoryRegionInfo_h_
#define LLDB_SBMemoryRegionInfo_h_

#include "lldb/API/SBData.h"
#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBMemoryRegionInfo {
public:
  SBMemoryRegionInfo();

  SBMemoryRegionInfo(const lldb::SBMemoryRegionInfo &rhs);

  ~SBMemoryRegionInfo();

  const lldb::SBMemoryRegionInfo &
  operator=(const lldb::SBMemoryRegionInfo &rhs);

  void Clear();

  //------------------------------------------------------------------
  /// Get the base address of this memory range.
  ///
  /// @return
  ///     The base address of this memory range.
  //------------------------------------------------------------------
  lldb::addr_t GetRegionBase();

  //------------------------------------------------------------------
  /// Get the end address of this memory range.
  ///
  /// @return
  ///     The base address of this memory range.
  //------------------------------------------------------------------
  lldb::addr_t GetRegionEnd();

  //------------------------------------------------------------------
  /// Check if this memory address is marked readable to the process.
  ///
  /// @return
  ///     true if this memory address is marked readable
  //------------------------------------------------------------------
  bool IsReadable();

  //------------------------------------------------------------------
  /// Check if this memory address is marked writable to the process.
  ///
  /// @return
  ///     true if this memory address is marked writable
  //------------------------------------------------------------------
  bool IsWritable();

  //------------------------------------------------------------------
  /// Check if this memory address is marked executable to the process.
  ///
  /// @return
  ///     true if this memory address is marked executable
  //------------------------------------------------------------------
  bool IsExecutable();

  //------------------------------------------------------------------
  /// Check if this memory address is mapped into the process address
  /// space.
  ///
  /// @return
  ///     true if this memory address is in the process address space.
  //------------------------------------------------------------------
  bool IsMapped();

  //------------------------------------------------------------------
  /// Returns the name of the memory region mapped at the given
  /// address.
  ///
  /// @return
  ///     In case of memory mapped files it is the absolute path of
  ///     the file otherwise it is a name associated with the memory
  ///     region. If no name can be determined the returns nullptr.
  //------------------------------------------------------------------
  const char *GetName();

  bool operator==(const lldb::SBMemoryRegionInfo &rhs) const;

  bool operator!=(const lldb::SBMemoryRegionInfo &rhs) const;

  bool GetDescription(lldb::SBStream &description);

private:
  friend class SBProcess;
  friend class SBMemoryRegionInfoList;

  lldb_private::MemoryRegionInfo &ref();

  const lldb_private::MemoryRegionInfo &ref() const;

  // Unused.
  SBMemoryRegionInfo(const lldb_private::MemoryRegionInfo *lldb_object_ptr);

  lldb::MemoryRegionInfoUP m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBMemoryRegionInfo_h_
