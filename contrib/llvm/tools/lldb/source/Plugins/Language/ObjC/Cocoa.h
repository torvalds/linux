//===-- Cocoa.h ---------------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Cocoa_h_
#define liblldb_Cocoa_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {
bool NSIndexSetSummaryProvider(ValueObject &valobj, Stream &stream,
                               const TypeSummaryOptions &options);

bool NSArraySummaryProvider(ValueObject &valobj, Stream &stream,
                            const TypeSummaryOptions &options);

template <bool needs_at>
bool NSDataSummaryProvider(ValueObject &valobj, Stream &stream,
                           const TypeSummaryOptions &options);

bool NSNumberSummaryProvider(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &options);

bool NSDecimalNumberSummaryProvider(ValueObject &valobj, Stream &stream,
                                    const TypeSummaryOptions &options);

bool NSNotificationSummaryProvider(ValueObject &valobj, Stream &stream,
                                   const TypeSummaryOptions &options);

bool NSTimeZoneSummaryProvider(ValueObject &valobj, Stream &stream,
                               const TypeSummaryOptions &options);

bool NSMachPortSummaryProvider(ValueObject &valobj, Stream &stream,
                               const TypeSummaryOptions &options);

bool NSDateSummaryProvider(ValueObject &valobj, Stream &stream,
                           const TypeSummaryOptions &options);

bool NSBundleSummaryProvider(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &options);

bool NSURLSummaryProvider(ValueObject &valobj, Stream &stream,
                          const TypeSummaryOptions &options);

extern template bool NSDataSummaryProvider<true>(ValueObject &, Stream &,
                                                 const TypeSummaryOptions &);

extern template bool NSDataSummaryProvider<false>(ValueObject &, Stream &,
                                                  const TypeSummaryOptions &);

SyntheticChildrenFrontEnd *
NSArraySyntheticFrontEndCreator(CXXSyntheticChildren *, lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
NSIndexPathSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                    lldb::ValueObjectSP);

bool ObjCClassSummaryProvider(ValueObject &valobj, Stream &stream,
                              const TypeSummaryOptions &options);

SyntheticChildrenFrontEnd *
ObjCClassSyntheticFrontEndCreator(CXXSyntheticChildren *, lldb::ValueObjectSP);

bool ObjCBOOLSummaryProvider(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &options);

bool ObjCBooleanSummaryProvider(ValueObject &valobj, Stream &stream,
                                const TypeSummaryOptions &options);

template <bool is_sel_ptr>
bool ObjCSELSummaryProvider(ValueObject &valobj, Stream &stream,
                            const TypeSummaryOptions &options);

extern template bool ObjCSELSummaryProvider<true>(ValueObject &, Stream &,
                                                  const TypeSummaryOptions &);

extern template bool ObjCSELSummaryProvider<false>(ValueObject &, Stream &,
                                                   const TypeSummaryOptions &);

bool NSError_SummaryProvider(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &options);

bool NSException_SummaryProvider(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &options);

SyntheticChildrenFrontEnd *
NSErrorSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                lldb::ValueObjectSP valobj_sp);

SyntheticChildrenFrontEnd *
NSExceptionSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                    lldb::ValueObjectSP valobj_sp);

class NSArray_Additionals {
public:
  static std::map<ConstString, CXXFunctionSummaryFormat::Callback> &
  GetAdditionalSummaries();

  static std::map<ConstString, CXXSyntheticChildren::CreateFrontEndCallback> &
  GetAdditionalSynthetics();
};
} // namespace formatters
} // namespace lldb_private

#endif // liblldb_Cocoa_h_
