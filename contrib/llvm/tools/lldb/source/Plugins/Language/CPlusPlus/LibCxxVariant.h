//===-- LibCxxVariant.h -------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LibCxxVariant_h_
#define liblldb_LibCxxVariant_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {
bool LibcxxVariantSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libc++ std::variant<>

SyntheticChildrenFrontEnd *LibcxxVariantFrontEndCreator(CXXSyntheticChildren *,
                                                        lldb::ValueObjectSP);

} // namespace formatters
} // namespace lldb_private

#endif // liblldb_LibCxxVariant_h_
