//===-- DumpRegisterValue.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DUMPREGISTERVALUE_H
#define LLDB_CORE_DUMPREGISTERVALUE_H

#include "lldb/lldb-enumerations.h"
#include <cstdint>

namespace lldb_private {

class RegisterValue;
struct RegisterInfo;
class Stream;

// The default value of 0 for reg_name_right_align_at means no alignment at
// all.
bool DumpRegisterValue(const RegisterValue &reg_val, Stream *s,
                       const RegisterInfo *reg_info, bool prefix_with_name,
                       bool prefix_with_alt_name, lldb::Format format,
                       uint32_t reg_name_right_align_at = 0);

} // namespace lldb_private

#endif // LLDB_CORE_DUMPREGISTERVALUE_H
