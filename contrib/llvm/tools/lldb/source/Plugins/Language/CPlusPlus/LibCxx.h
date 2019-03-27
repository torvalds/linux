//===-- LibCxx.h ---------------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LibCxx_h_
#define liblldb_LibCxx_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {

bool LibcxxStringSummaryProviderASCII(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options); // libc++ std::string

bool LibcxxStringSummaryProviderUTF16(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options); // libc++ std::u16string

bool LibcxxStringSummaryProviderUTF32(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options); // libc++ std::u32string

bool LibcxxWStringSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libc++ std::wstring

bool LibcxxOptionalSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libc++ std::optional<>

bool LibcxxSmartPointerSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions
        &options); // libc++ std::shared_ptr<> and std::weak_ptr<>

bool LibcxxFunctionSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &options); // libc++ std::function<>

SyntheticChildrenFrontEnd *
LibcxxVectorBoolSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                         lldb::ValueObjectSP);

bool LibcxxContainerSummaryProvider(ValueObject &valobj, Stream &stream,
                                    const TypeSummaryOptions &options);

class LibCxxMapIteratorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibCxxMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

  ~LibCxxMapIteratorSyntheticFrontEnd() override;

private:
  ValueObject *m_pair_ptr;
  lldb::ValueObjectSP m_pair_sp;
};

SyntheticChildrenFrontEnd *
LibCxxMapIteratorSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                          lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibCxxVectorIteratorSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                             lldb::ValueObjectSP);

class LibcxxSharedPtrSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxSharedPtrSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

  ~LibcxxSharedPtrSyntheticFrontEnd() override;

private:
  ValueObject *m_cntrl;
  lldb::ValueObjectSP m_count_sp;
  lldb::ValueObjectSP m_weak_count_sp;
  uint8_t m_ptr_size;
  lldb::ByteOrder m_byte_order;
};

SyntheticChildrenFrontEnd *
LibcxxBitsetSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                     lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxSharedPtrSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                        lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxStdVectorSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                        lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxStdListSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                      lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxStdForwardListSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                             lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxStdMapSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                     lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxStdUnorderedMapSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                              lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxInitializerListSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                              lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *LibcxxQueueFrontEndCreator(CXXSyntheticChildren *,
                                                      lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *LibcxxTupleFrontEndCreator(CXXSyntheticChildren *,
                                                      lldb::ValueObjectSP);

SyntheticChildrenFrontEnd *
LibcxxOptionalFrontEndCreator(CXXSyntheticChildren *,
                              lldb::ValueObjectSP valobj_sp);

SyntheticChildrenFrontEnd *
LibcxxVariantFrontEndCreator(CXXSyntheticChildren *,
                             lldb::ValueObjectSP valobj_sp);

} // namespace formatters
} // namespace lldb_private

#endif // liblldb_LibCxx_h_
