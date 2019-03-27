//===-- LibCxxVariant.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibCxxVariant.h"
#include "lldb/DataFormatters/FormattersHelpers.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/ScopeExit.h"

using namespace lldb;
using namespace lldb_private;

// libc++ variant implementation contains two members that we care about both
// are contained in the __impl member.
// - __index which tells us which of the variadic template types is the active
//   type for the variant
// - __data is a variadic union which recursively contains itself as member
//   which refers to the tailing variadic types.
//   - __head which refers to the leading non pack type
//     - __value refers to the actual value contained
//   - __tail which refers to the remaining pack types
//
// e.g. given std::variant<int,double,char> v1
//
// (lldb) frame var -R v1.__impl.__data
//(... __union<... 0, int, double, char>) v1.__impl.__data = {
// ...
//  __head = {
//    __value = ...
//  }
//  __tail = {
//  ...
//    __head = {
//      __value = ...
//    }
//    __tail = {
//    ...
//      __head = {
//        __value = ...
//  ...
//
// So given
// - __index equal to 0 the active value is contained in
//
//     __data.__head.__value
//
// - __index equal to 1 the active value is contained in
//
//     __data.__tail.__head.__value
//
// - __index equal to 2 the active value is contained in
//
//      __data.__tail.__tail.__head.__value
//

namespace {
// libc++ std::variant index could have one of three states
// 1) VALID, we can obtain it and its not variant_npos
// 2) INVALID, we can't obtain it or it is not a type we expect
// 3) NPOS, its value is variant_npos which means the variant has no value
enum class LibcxxVariantIndexValidity { VALID, INVALID, NPOS };

LibcxxVariantIndexValidity
LibcxxVariantGetIndexValidity(ValueObjectSP &impl_sp) {
  ValueObjectSP index_sp(
      impl_sp->GetChildMemberWithName(ConstString("__index"), true));

  if (!index_sp)
    return LibcxxVariantIndexValidity::INVALID;

  int64_t index_value = index_sp->GetValueAsSigned(0);

  if (index_value == -1)
    return LibcxxVariantIndexValidity::NPOS;

  return LibcxxVariantIndexValidity::VALID;
}

llvm::Optional<uint64_t> LibcxxVariantIndexValue(ValueObjectSP &impl_sp) {
  ValueObjectSP index_sp(
      impl_sp->GetChildMemberWithName(ConstString("__index"), true));

  if (!index_sp)
    return {};

  return {index_sp->GetValueAsUnsigned(0)};
}

ValueObjectSP LibcxxVariantGetNthHead(ValueObjectSP &impl_sp, uint64_t index) {
  ValueObjectSP data_sp(
      impl_sp->GetChildMemberWithName(ConstString("__data"), true));

  if (!data_sp)
    return ValueObjectSP{};

  ValueObjectSP current_level = data_sp;
  for (uint64_t n = index; n != 0; --n) {
    ValueObjectSP tail_sp(
        current_level->GetChildMemberWithName(ConstString("__tail"), true));

    if (!tail_sp)
      return ValueObjectSP{};

    current_level = tail_sp;
  }

  return current_level->GetChildMemberWithName(ConstString("__head"), true);
}
} // namespace

namespace lldb_private {
namespace formatters {
bool LibcxxVariantSummaryProvider(ValueObject &valobj, Stream &stream,
                                  const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  ValueObjectSP impl_sp(
      valobj_sp->GetChildMemberWithName(ConstString("__impl"), true));

  if (!impl_sp)
    return false;

  LibcxxVariantIndexValidity validity = LibcxxVariantGetIndexValidity(impl_sp);

  if (validity == LibcxxVariantIndexValidity::INVALID)
    return false;

  if (validity == LibcxxVariantIndexValidity::NPOS) {
    stream.Printf(" No Value");
    return true;
  }

  auto optional_index_value = LibcxxVariantIndexValue(impl_sp);

  if (!optional_index_value)
    return false;

  uint64_t index_value = *optional_index_value;

  ValueObjectSP nth_head = LibcxxVariantGetNthHead(impl_sp, index_value);

  if (!nth_head)
    return false;

  CompilerType head_type = nth_head->GetCompilerType();

  if (!head_type)
    return false;

  CompilerType template_type = head_type.GetTypeTemplateArgument(1);

  if (!template_type)
    return false;

  stream.Printf(" Active Type = %s ", template_type.GetTypeName().GetCString());

  return true;
}
} // namespace formatters
} // namespace lldb_private

namespace {
class VariantFrontEnd : public SyntheticChildrenFrontEnd {
public:
  VariantFrontEnd(ValueObject &valobj) : SyntheticChildrenFrontEnd(valobj) {
    Update();
  }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    return formatters::ExtractIndexFromString(name.GetCString());
  }

  bool MightHaveChildren() override { return true; }
  bool Update() override;
  size_t CalculateNumChildren() override { return m_size; }
  ValueObjectSP GetChildAtIndex(size_t idx) override;

private:
  size_t m_size = 0;
  ValueObjectSP m_base_sp;
};
} // namespace

bool VariantFrontEnd::Update() {
  m_size = 0;
  ValueObjectSP impl_sp(
      m_backend.GetChildMemberWithName(ConstString("__impl"), true));
  if (!impl_sp)
    return false;

  LibcxxVariantIndexValidity validity = LibcxxVariantGetIndexValidity(impl_sp);

  if (validity == LibcxxVariantIndexValidity::INVALID)
    return false;

  if (validity == LibcxxVariantIndexValidity::NPOS)
    return true;

  m_size = 1;

  return false;
}

ValueObjectSP VariantFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx >= m_size)
    return ValueObjectSP();

  ValueObjectSP impl_sp(
      m_backend.GetChildMemberWithName(ConstString("__impl"), true));

  auto optional_index_value = LibcxxVariantIndexValue(impl_sp);

  if (!optional_index_value)
    return ValueObjectSP();

  uint64_t index_value = *optional_index_value;

  ValueObjectSP nth_head = LibcxxVariantGetNthHead(impl_sp, index_value);

  if (!nth_head)
    return ValueObjectSP();

  CompilerType head_type = nth_head->GetCompilerType();

  if (!head_type)
    return ValueObjectSP();

  CompilerType template_type = head_type.GetTypeTemplateArgument(1);

  if (!template_type)
    return ValueObjectSP();

  ValueObjectSP head_value(
      nth_head->GetChildMemberWithName(ConstString("__value"), true));

  if (!head_value)
    return ValueObjectSP();

  return head_value->Clone(ConstString(ConstString("Value").AsCString()));
}

SyntheticChildrenFrontEnd *
formatters::LibcxxVariantFrontEndCreator(CXXSyntheticChildren *,
                                         lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new VariantFrontEnd(*valobj_sp);
  return nullptr;
}
