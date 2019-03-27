//===-- NSString.h ---------------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NSString_h_
#define liblldb_NSString_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {
bool NSStringSummaryProvider(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &options);

bool NSTaggedString_SummaryProvider(
    ValueObject &valobj, ObjCLanguageRuntime::ClassDescriptorSP descriptor,
    Stream &stream, const TypeSummaryOptions &summary_options);

bool NSAttributedStringSummaryProvider(ValueObject &valobj, Stream &stream,
                                       const TypeSummaryOptions &options);

bool NSMutableAttributedStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options);

class NSString_Additionals {
public:
  static std::map<ConstString, CXXFunctionSummaryFormat::Callback> &
  GetAdditionalSummaries();
};
} // namespace formatters
} // namespace lldb_private

#endif // liblldb_CF_h_
