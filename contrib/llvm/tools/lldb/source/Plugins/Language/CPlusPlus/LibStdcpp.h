//===-- LibStdcpp.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LibStdCpp_h_
#define liblldb_LibStdCpp_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {
bool LibStdcppStringSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libcstdc++ c++11 std::string

bool LibStdcppWStringSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libcstdc++ c++11 std::wstring

bool LibStdcppSmartPointerSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions
        &options); // libstdc++ std::shared_ptr<> and std::weak_ptr<>

bool LibStdcppUniquePointerSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libstdc++ std::unique_ptr<>

SyntheticChildrenFrontEnd *
LibstdcppMapIteratorSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                             lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibStdcppTupleSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                       lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibStdcppVectorIteratorSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                                lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibStdcppSharedPtrSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                           lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibStdcppUniquePtrSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                           lldb::ValueObjectSP);

} // namespace formatters
} // namespace lldb_private

#endif // liblldb_LibStdCpp_h_
