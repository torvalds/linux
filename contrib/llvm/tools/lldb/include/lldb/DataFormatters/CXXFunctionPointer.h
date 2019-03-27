//===-- CXXFunctionPointer.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CXXFunctionPointer_h_
#define liblldb_CXXFunctionPointer_h_

#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
bool CXXFunctionPointerSummaryProvider(ValueObject &valobj, Stream &stream,
                                       const TypeSummaryOptions &options);
} // namespace formatters
} // namespace lldb_private

#endif // liblldb_CXXFunctionPointer_h_
