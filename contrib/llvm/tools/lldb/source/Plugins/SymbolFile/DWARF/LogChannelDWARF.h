//===-- LogChannelDWARF.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_LogChannelDWARF_h_
#define SymbolFileDWARF_LogChannelDWARF_h_

#include "lldb/Utility/Log.h"

#define DWARF_LOG_DEBUG_INFO (1u << 1)
#define DWARF_LOG_DEBUG_LINE (1u << 2)
#define DWARF_LOG_DEBUG_PUBNAMES (1u << 3)
#define DWARF_LOG_DEBUG_PUBTYPES (1u << 4)
#define DWARF_LOG_DEBUG_ARANGES (1u << 5)
#define DWARF_LOG_LOOKUPS (1u << 6)
#define DWARF_LOG_TYPE_COMPLETION (1u << 7)
#define DWARF_LOG_DEBUG_MAP (1u << 8)
#define DWARF_LOG_ALL (UINT32_MAX)
#define DWARF_LOG_DEFAULT (DWARF_LOG_DEBUG_INFO)

namespace lldb_private {
class LogChannelDWARF {
  static Log::Channel g_channel;

public:
  static void Initialize();
  static void Terminate();

  static Log *GetLogIfAll(uint32_t mask) { return g_channel.GetLogIfAll(mask); }
  static Log *GetLogIfAny(uint32_t mask) { return g_channel.GetLogIfAny(mask); }
};
}

#endif // SymbolFileDWARF_LogChannelDWARF_h_
