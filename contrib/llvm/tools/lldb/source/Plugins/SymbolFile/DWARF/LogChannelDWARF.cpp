//===-- LogChannelDWARF.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LogChannelDWARF.h"

using namespace lldb_private;

static constexpr Log::Category g_categories[] = {
    {{"aranges"},
     {"log the parsing of .debug_aranges"},
     DWARF_LOG_DEBUG_ARANGES},
    {{"comp"},
     {"log insertions of object files into DWARF debug maps"},
     DWARF_LOG_TYPE_COMPLETION},
    {{"info"}, {"log the parsing of .debug_info"}, DWARF_LOG_DEBUG_INFO},
    {{"line"}, {"log the parsing of .debug_line"}, DWARF_LOG_DEBUG_LINE},
    {{"lookups"},
     {"log any lookups that happen by name, regex, or address"},
     DWARF_LOG_LOOKUPS},
    {{"map"},
     {"log struct/unions/class type completions"},
     DWARF_LOG_DEBUG_MAP},
    {{"pubnames"},
     {"log the parsing of .debug_pubnames"},
     DWARF_LOG_DEBUG_PUBNAMES},
    {{"pubtypes"},
     {"log the parsing of .debug_pubtypes"},
     DWARF_LOG_DEBUG_PUBTYPES},
};

Log::Channel LogChannelDWARF::g_channel(g_categories, DWARF_LOG_DEFAULT);

void LogChannelDWARF::Initialize() {
  Log::Register("dwarf", g_channel);
}

void LogChannelDWARF::Terminate() { Log::Unregister("dwarf"); }
