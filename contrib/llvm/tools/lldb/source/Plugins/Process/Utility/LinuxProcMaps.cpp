//===-- LinuxProcMaps.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LinuxProcMaps.h"
#include "llvm/ADT/StringRef.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StringExtractor.h"

using namespace lldb_private;

static Status
ParseMemoryRegionInfoFromProcMapsLine(llvm::StringRef maps_line,
                                      MemoryRegionInfo &memory_region_info) {
  memory_region_info.Clear();
  
  StringExtractor line_extractor(maps_line);
  
  // Format: {address_start_hex}-{address_end_hex} perms offset  dev   inode
  // pathname perms: rwxp   (letter is present if set, '-' if not, final
  // character is p=private, s=shared).
  
  // Parse out the starting address
  lldb::addr_t start_address = line_extractor.GetHexMaxU64(false, 0);
  
  // Parse out hyphen separating start and end address from range.
  if (!line_extractor.GetBytesLeft() || (line_extractor.GetChar() != '-'))
    return Status(
        "malformed /proc/{pid}/maps entry, missing dash between address range");
  
  // Parse out the ending address
  lldb::addr_t end_address = line_extractor.GetHexMaxU64(false, start_address);
  
  // Parse out the space after the address.
  if (!line_extractor.GetBytesLeft() || (line_extractor.GetChar() != ' '))
    return Status(
        "malformed /proc/{pid}/maps entry, missing space after range");
  
  // Save the range.
  memory_region_info.GetRange().SetRangeBase(start_address);
  memory_region_info.GetRange().SetRangeEnd(end_address);
  
  // Any memory region in /proc/{pid}/maps is by definition mapped into the
  // process.
  memory_region_info.SetMapped(MemoryRegionInfo::OptionalBool::eYes);
  
  // Parse out each permission entry.
  if (line_extractor.GetBytesLeft() < 4)
    return Status("malformed /proc/{pid}/maps entry, missing some portion of "
                  "permissions");
  
  // Handle read permission.
  const char read_perm_char = line_extractor.GetChar();
  if (read_perm_char == 'r')
    memory_region_info.SetReadable(MemoryRegionInfo::OptionalBool::eYes);
  else if (read_perm_char == '-')
    memory_region_info.SetReadable(MemoryRegionInfo::OptionalBool::eNo);
  else
    return Status("unexpected /proc/{pid}/maps read permission char");
  
  // Handle write permission.
  const char write_perm_char = line_extractor.GetChar();
  if (write_perm_char == 'w')
    memory_region_info.SetWritable(MemoryRegionInfo::OptionalBool::eYes);
  else if (write_perm_char == '-')
    memory_region_info.SetWritable(MemoryRegionInfo::OptionalBool::eNo);
  else
    return Status("unexpected /proc/{pid}/maps write permission char");
  
  // Handle execute permission.
  const char exec_perm_char = line_extractor.GetChar();
  if (exec_perm_char == 'x')
    memory_region_info.SetExecutable(MemoryRegionInfo::OptionalBool::eYes);
  else if (exec_perm_char == '-')
    memory_region_info.SetExecutable(MemoryRegionInfo::OptionalBool::eNo);
  else
    return Status("unexpected /proc/{pid}/maps exec permission char");
  
  line_extractor.GetChar();              // Read the private bit
  line_extractor.SkipSpaces();           // Skip the separator
  line_extractor.GetHexMaxU64(false, 0); // Read the offset
  line_extractor.GetHexMaxU64(false, 0); // Read the major device number
  line_extractor.GetChar();              // Read the device id separator
  line_extractor.GetHexMaxU64(false, 0); // Read the major device number
  line_extractor.SkipSpaces();           // Skip the separator
  line_extractor.GetU64(0, 10);          // Read the inode number
  
  line_extractor.SkipSpaces();
  const char *name = line_extractor.Peek();
  if (name)
    memory_region_info.SetName(name);
  
  return Status();
}

void lldb_private::ParseLinuxMapRegions(llvm::StringRef linux_map,
                                        LinuxMapCallback const &callback) {
  llvm::StringRef lines(linux_map);
  llvm::StringRef line;
  while (!lines.empty()) {
    std::tie(line, lines) = lines.split('\n');
    MemoryRegionInfo region;
    Status error = ParseMemoryRegionInfoFromProcMapsLine(line, region);
    if (!callback(region, error))
      break;
  }
}
