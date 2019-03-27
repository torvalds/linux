//===-- StringConvert.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StringConvert_h_
#define liblldb_StringConvert_h_

#include <stdint.h>



namespace lldb_private {

namespace StringConvert {

//----------------------------------------------------------------------
/// @namespace StringConvert StringConvert.h "lldb/Host/StringConvert.h"
/// Utility classes for converting strings into Integers
//----------------------------------------------------------------------

int32_t ToSInt32(const char *s, int32_t fail_value = 0, int base = 0,
                 bool *success_ptr = nullptr);

uint32_t ToUInt32(const char *s, uint32_t fail_value = 0, int base = 0,
                  bool *success_ptr = nullptr);

int64_t ToSInt64(const char *s, int64_t fail_value = 0, int base = 0,
                 bool *success_ptr = nullptr);

uint64_t ToUInt64(const char *s, uint64_t fail_value = 0, int base = 0,
                  bool *success_ptr = nullptr);

double ToDouble(const char *s, double fail_value = 0.0,
                bool *success_ptr = nullptr);
} // namespace StringConvert
} // namespace lldb_private

#endif
