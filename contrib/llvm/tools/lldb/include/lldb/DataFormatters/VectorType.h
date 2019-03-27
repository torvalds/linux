//===-- VectorType.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_VectorType_h_
#define liblldb_VectorType_h_

#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
bool VectorTypeSummaryProvider(ValueObject &, Stream &,
                               const TypeSummaryOptions &);

SyntheticChildrenFrontEnd *
VectorTypeSyntheticFrontEndCreator(CXXSyntheticChildren *, lldb::ValueObjectSP);
} // namespace formatters
} // namespace lldb_private

#endif // liblldb_VectorType_h_
