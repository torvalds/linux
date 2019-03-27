//===-- MinidumpParser.h -----------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_MinidumpParser_h_
#define liblldb_MinidumpParser_h_

#include "MinidumpTypes.h"

#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

// C includes

// C++ includes
#include <cstring>
#include <unordered_map>

namespace lldb_private {

namespace minidump {

// Describes a range of memory captured in the Minidump
struct Range {
  lldb::addr_t start; // virtual address of the beginning of the range
  // range_ref - absolute pointer to the first byte of the range and size
  llvm::ArrayRef<uint8_t> range_ref;

  Range(lldb::addr_t start, llvm::ArrayRef<uint8_t> range_ref)
      : start(start), range_ref(range_ref) {}
};

class MinidumpParser {
public:
  static llvm::Optional<MinidumpParser>
  Create(const lldb::DataBufferSP &data_buf_sp);

  llvm::ArrayRef<uint8_t> GetData();

  llvm::ArrayRef<uint8_t> GetStream(MinidumpStreamType stream_type);

  llvm::Optional<std::string> GetMinidumpString(uint32_t rva);

  UUID GetModuleUUID(const MinidumpModule* module);

  llvm::ArrayRef<MinidumpThread> GetThreads();

  llvm::ArrayRef<uint8_t>
  GetThreadContext(const MinidumpLocationDescriptor &location);

  llvm::ArrayRef<uint8_t> GetThreadContext(const MinidumpThread &td);

  llvm::ArrayRef<uint8_t> GetThreadContextWow64(const MinidumpThread &td);

  const MinidumpSystemInfo *GetSystemInfo();

  ArchSpec GetArchitecture();

  const MinidumpMiscInfo *GetMiscInfo();

  llvm::Optional<LinuxProcStatus> GetLinuxProcStatus();

  llvm::Optional<lldb::pid_t> GetPid();

  llvm::ArrayRef<MinidumpModule> GetModuleList();

  // There are cases in which there is more than one record in the ModuleList
  // for the same module name.(e.g. when the binary has non contiguous segments)
  // So this function returns a filtered module list - if it finds records that
  // have the same name, it keeps the copy with the lowest load address.
  std::vector<const MinidumpModule *> GetFilteredModuleList();

  const MinidumpExceptionStream *GetExceptionStream();

  llvm::Optional<Range> FindMemoryRange(lldb::addr_t addr);

  llvm::ArrayRef<uint8_t> GetMemory(lldb::addr_t addr, size_t size);

  MemoryRegionInfo GetMemoryRegionInfo(lldb::addr_t load_addr);

  const MemoryRegionInfos &GetMemoryRegions();

  // Perform consistency checks and initialize internal data structures
  Status Initialize();

  static llvm::StringRef GetStreamTypeAsString(uint32_t stream_type);

  const llvm::DenseMap<uint32_t, MinidumpLocationDescriptor> &
  GetDirectoryMap() const {
    return m_directory_map;
  }

private:
  MinidumpParser(const lldb::DataBufferSP &data_buf_sp);

  MemoryRegionInfo FindMemoryRegion(lldb::addr_t load_addr) const;

private:
  lldb::DataBufferSP m_data_sp;
  llvm::DenseMap<uint32_t, MinidumpLocationDescriptor> m_directory_map;
  ArchSpec m_arch;
  MemoryRegionInfos m_regions;
  bool m_parsed_regions = false;
};

} // end namespace minidump
} // end namespace lldb_private
#endif // liblldb_MinidumpParser_h_
