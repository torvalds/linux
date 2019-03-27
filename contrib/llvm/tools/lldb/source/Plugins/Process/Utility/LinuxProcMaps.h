//===-- LinuxProcMaps.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LinuxProcMaps_H_
#define liblldb_LinuxProcMaps_H_

#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"
#include <functional>


namespace lldb_private {

typedef std::function<bool(const lldb_private::MemoryRegionInfo &,
                           const lldb_private::Status &)> LinuxMapCallback;

void ParseLinuxMapRegions(llvm::StringRef linux_map,
                          LinuxMapCallback const &callback);

} // namespace lldb_private

#endif // liblldb_LinuxProcMaps_H_
