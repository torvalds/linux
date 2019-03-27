//===-- BlockPointer.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BlockPointer_h_
#define liblldb_BlockPointer_h_

#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
bool BlockPointerSummaryProvider(ValueObject &, Stream &,
                                 const TypeSummaryOptions &);

SyntheticChildrenFrontEnd *
BlockPointerSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                     lldb::ValueObjectSP);
} // namespace formatters
} // namespace lldb_private

#endif // liblldb_BlockPointer_h_
