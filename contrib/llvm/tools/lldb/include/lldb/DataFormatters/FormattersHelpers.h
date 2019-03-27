//===-- FormattersHelpers.h --------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_FormattersHelpers_h_
#define lldb_FormattersHelpers_h_

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include "lldb/DataFormatters/TypeCategory.h"
#include "lldb/DataFormatters/TypeFormat.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"

namespace lldb_private {
namespace formatters {
void AddFormat(TypeCategoryImpl::SharedPointer category_sp, lldb::Format format,
               ConstString type_name, TypeFormatImpl::Flags flags,
               bool regex = false);

void AddSummary(TypeCategoryImpl::SharedPointer category_sp,
                lldb::TypeSummaryImplSP summary_sp, ConstString type_name,
                bool regex = false);

void AddStringSummary(TypeCategoryImpl::SharedPointer category_sp,
                      const char *string, ConstString type_name,
                      TypeSummaryImpl::Flags flags, bool regex = false);

void AddOneLineSummary(TypeCategoryImpl::SharedPointer category_sp,
                       ConstString type_name, TypeSummaryImpl::Flags flags,
                       bool regex = false);

#ifndef LLDB_DISABLE_PYTHON
void AddCXXSummary(TypeCategoryImpl::SharedPointer category_sp,
                   CXXFunctionSummaryFormat::Callback funct,
                   const char *description, ConstString type_name,
                   TypeSummaryImpl::Flags flags, bool regex = false);

void AddCXXSynthetic(TypeCategoryImpl::SharedPointer category_sp,
                     CXXSyntheticChildren::CreateFrontEndCallback generator,
                     const char *description, ConstString type_name,
                     ScriptedSyntheticChildren::Flags flags,
                     bool regex = false);

void AddFilter(TypeCategoryImpl::SharedPointer category_sp,
               std::vector<std::string> children, const char *description,
               ConstString type_name, ScriptedSyntheticChildren::Flags flags,
               bool regex = false);
#endif

size_t ExtractIndexFromString(const char *item_name);

lldb::addr_t GetArrayAddressOrPointerValue(ValueObject &valobj);

time_t GetOSXEpoch();

struct InferiorSizedWord {

  InferiorSizedWord(const InferiorSizedWord &word) : ptr_size(word.ptr_size) {
    if (ptr_size == 4)
      thirty_two = word.thirty_two;
    else
      sixty_four = word.sixty_four;
  }

  InferiorSizedWord operator=(const InferiorSizedWord &word) {
    ptr_size = word.ptr_size;
    if (ptr_size == 4)
      thirty_two = word.thirty_two;
    else
      sixty_four = word.sixty_four;
    return *this;
  }

  InferiorSizedWord(uint64_t val, Process &process)
      : ptr_size(process.GetAddressByteSize()) {
    if (ptr_size == 4)
      thirty_two = (uint32_t)val;
    else if (ptr_size == 8)
      sixty_four = val;
    else
      assert(false && "new pointer size is unknown");
  }

  bool IsNegative() const {
    if (ptr_size == 4)
      return ((int32_t)thirty_two) < 0;
    else
      return ((int64_t)sixty_four) < 0;
  }

  bool IsZero() const {
    if (ptr_size == 4)
      return thirty_two == 0;
    else
      return sixty_four == 0;
  }

  static InferiorSizedWord GetMaximum(Process &process) {
    if (process.GetAddressByteSize() == 4)
      return InferiorSizedWord(UINT32_MAX, 4);
    else
      return InferiorSizedWord(UINT64_MAX, 8);
  }

  InferiorSizedWord operator>>(int rhs) const {
    if (ptr_size == 4)
      return InferiorSizedWord(thirty_two >> rhs, 4);
    return InferiorSizedWord(sixty_four >> rhs, 8);
  }

  InferiorSizedWord operator<<(int rhs) const {
    if (ptr_size == 4)
      return InferiorSizedWord(thirty_two << rhs, 4);
    return InferiorSizedWord(sixty_four << rhs, 8);
  }

  InferiorSizedWord operator&(const InferiorSizedWord &word) const {
    if (ptr_size != word.ptr_size)
      return InferiorSizedWord(0, ptr_size);
    if (ptr_size == 4)
      return InferiorSizedWord(thirty_two & word.thirty_two, 4);
    return InferiorSizedWord(sixty_four & word.sixty_four, 8);
  }

  InferiorSizedWord operator&(int x) const {
    if (ptr_size == 4)
      return InferiorSizedWord(thirty_two & x, 4);
    return InferiorSizedWord(sixty_four & x, 8);
  }

  size_t GetBitSize() const { return ptr_size << 3; }

  size_t GetByteSize() const { return ptr_size; }

  uint64_t GetValue() const {
    if (ptr_size == 4)
      return (uint64_t)thirty_two;
    return sixty_four;
  }

  InferiorSizedWord SignExtend() const {
    if (ptr_size == 4)
      return InferiorSizedWord((int32_t)thirty_two, 4);
    return InferiorSizedWord((int64_t)sixty_four, 8);
  }

  uint8_t *CopyToBuffer(uint8_t *buffer) const {
    if (ptr_size == 4) {
      memcpy(buffer, &thirty_two, 4);
      return buffer + 4;
    } else {
      memcpy(buffer, &sixty_four, 8);
      return buffer + 8;
    }
  }

  DataExtractor
  GetAsData(lldb::ByteOrder byte_order = lldb::eByteOrderInvalid) const {
    if (ptr_size == 4)
      return DataExtractor(&thirty_two, 4, byte_order, 4);
    else
      return DataExtractor(&sixty_four, 8, byte_order, 8);
  }

private:
  InferiorSizedWord(uint64_t val, size_t psz) : ptr_size(psz) {
    if (ptr_size == 4)
      thirty_two = (uint32_t)val;
    else
      sixty_four = val;
  }

  size_t ptr_size;
  union {
    uint32_t thirty_two;
    uint64_t sixty_four;
  };
};
} // namespace formatters
} // namespace lldb_private

#endif // lldb_FormattersHelpers_h_
