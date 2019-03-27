//===-- DumpRegisterValue.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-private-types.h"

using namespace lldb;

bool lldb_private::DumpRegisterValue(const RegisterValue &reg_val, Stream *s,
                                     const RegisterInfo *reg_info,
                                     bool prefix_with_name,
                                     bool prefix_with_alt_name, Format format,
                                     uint32_t reg_name_right_align_at) {
  DataExtractor data;
  if (reg_val.GetData(data)) {
    bool name_printed = false;
    // For simplicity, alignment of the register name printing applies only in
    // the most common case where:
    //
    //     prefix_with_name^prefix_with_alt_name is true
    //
    StreamString format_string;
    if (reg_name_right_align_at && (prefix_with_name ^ prefix_with_alt_name))
      format_string.Printf("%%%us", reg_name_right_align_at);
    else
      format_string.Printf("%%s");
    std::string fmt = format_string.GetString();
    if (prefix_with_name) {
      if (reg_info->name) {
        s->Printf(fmt.c_str(), reg_info->name);
        name_printed = true;
      } else if (reg_info->alt_name) {
        s->Printf(fmt.c_str(), reg_info->alt_name);
        prefix_with_alt_name = false;
        name_printed = true;
      }
    }
    if (prefix_with_alt_name) {
      if (name_printed)
        s->PutChar('/');
      if (reg_info->alt_name) {
        s->Printf(fmt.c_str(), reg_info->alt_name);
        name_printed = true;
      } else if (!name_printed) {
        // No alternate name but we were asked to display a name, so show the
        // main name
        s->Printf(fmt.c_str(), reg_info->name);
        name_printed = true;
      }
    }
    if (name_printed)
      s->PutCString(" = ");

    if (format == eFormatDefault)
      format = reg_info->format;

    DumpDataExtractor(data, s,
                      0,                    // Offset in "data"
                      format,               // Format to use when dumping
                      reg_info->byte_size,  // item_byte_size
                      1,                    // item_count
                      UINT32_MAX,           // num_per_line
                      LLDB_INVALID_ADDRESS, // base_addr
                      0,                    // item_bit_size
                      0);                   // item_bit_offset
    return true;
  }
  return false;
}
