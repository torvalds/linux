//===-- NtStructures.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Minidump_NtStructures_h_
#define liblldb_Plugins_Process_Minidump_NtStructures_h_

#include "llvm/Support/Endian.h"

namespace lldb_private {

namespace minidump {

// This describes the layout of a TEB (Thread Environment Block) for a 64-bit
// process.  It's adapted from the 32-bit TEB in winternl.h.  Currently, we care
// only about the position of the tls_slots.
struct TEB64 {
  llvm::support::ulittle64_t reserved1[12];
  llvm::support::ulittle64_t process_environment_block;
  llvm::support::ulittle64_t reserved2[399];
  uint8_t reserved3[1952];
  llvm::support::ulittle64_t tls_slots[64];
  uint8_t reserved4[8];
  llvm::support::ulittle64_t reserved5[26];
  llvm::support::ulittle64_t reserved_for_ole; // Windows 2000 only
  llvm::support::ulittle64_t reserved6[4];
  llvm::support::ulittle64_t tls_expansion_slots;
};

#endif // liblldb_Plugins_Process_Minidump_NtStructures_h_
} // namespace minidump
} // namespace lldb_private
