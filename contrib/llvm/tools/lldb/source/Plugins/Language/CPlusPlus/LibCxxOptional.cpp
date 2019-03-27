//===-- LibCxxOptional.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"
#include "lldb/DataFormatters/FormattersHelpers.h"

using namespace lldb;
using namespace lldb_private;

namespace {

class OptionalFrontEnd : public SyntheticChildrenFrontEnd {
public:
  OptionalFrontEnd(ValueObject &valobj) : SyntheticChildrenFrontEnd(valobj) {
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

bool OptionalFrontEnd::Update() {
  ValueObjectSP engaged_sp(
      m_backend.GetChildMemberWithName(ConstString("__engaged_"), true));

  if (!engaged_sp)
    return false;

  // __engaged_ is a bool flag and is true if the optional contains a value.
  // Converting it to unsigned gives us a size of 1 if it contains a value
  // and 0 if not.
  m_size = engaged_sp->GetValueAsUnsigned(0);

  return false;
}

ValueObjectSP OptionalFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx >= m_size)
    return ValueObjectSP();

  // __val_ contains the underlying value of an optional if it has one.
  // Currently because it is part of an anonymous union GetChildMemberWithName()
  // does not peer through and find it unless we are at the parent itself.
  // We can obtain the parent through __engaged_.
  ValueObjectSP val_sp(
      m_backend.GetChildMemberWithName(ConstString("__engaged_"), true)
          ->GetParent()
          ->GetChildAtIndex(0, true)
          ->GetChildMemberWithName(ConstString("__val_"), true));

  if (!val_sp)
    return ValueObjectSP();

  CompilerType holder_type = val_sp->GetCompilerType();

  if (!holder_type)
    return ValueObjectSP();

  return val_sp->Clone(ConstString(llvm::formatv("Value").str()));
}

SyntheticChildrenFrontEnd *
formatters::LibcxxOptionalFrontEndCreator(CXXSyntheticChildren *,
                                          lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new OptionalFrontEnd(*valobj_sp);
  return nullptr;
}
