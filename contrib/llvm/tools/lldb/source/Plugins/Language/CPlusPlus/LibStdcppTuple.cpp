//===-- LibStdcppTuple.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibStdcpp.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/ConstString.h"

#include <memory>
#include <vector>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace {

class LibStdcppTupleSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  explicit LibStdcppTupleSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  std::vector<ValueObjectSP> m_members;
};

} // end of anonymous namespace

LibStdcppTupleSyntheticFrontEnd::LibStdcppTupleSyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  Update();
}

bool LibStdcppTupleSyntheticFrontEnd::Update() {
  m_members.clear();

  ValueObjectSP valobj_backend_sp = m_backend.GetSP();
  if (!valobj_backend_sp)
    return false;

  ValueObjectSP next_child_sp = valobj_backend_sp->GetNonSyntheticValue();
  while (next_child_sp != nullptr) {
    ValueObjectSP current_child = next_child_sp;
    next_child_sp = nullptr;

    size_t child_count = current_child->GetNumChildren();
    for (size_t i = 0; i < child_count; ++i) {
      ValueObjectSP child_sp = current_child->GetChildAtIndex(i, true);
      llvm::StringRef name_str = child_sp->GetName().GetStringRef();
      if (name_str.startswith("std::_Tuple_impl<")) {
        next_child_sp = child_sp;
      } else if (name_str.startswith("std::_Head_base<")) {
        ValueObjectSP value_sp =
            child_sp->GetChildMemberWithName(ConstString("_M_head_impl"), true);
        if (value_sp) {
          StreamString name;
          name.Printf("[%zd]", m_members.size());
          m_members.push_back(value_sp->Clone(ConstString(name.GetString())));
        }
      }
    }
  }

  return false;
}

bool LibStdcppTupleSyntheticFrontEnd::MightHaveChildren() { return true; }

lldb::ValueObjectSP
LibStdcppTupleSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx < m_members.size())
    return m_members[idx];
  return lldb::ValueObjectSP();
}

size_t LibStdcppTupleSyntheticFrontEnd::CalculateNumChildren() {
  return m_members.size();
}

size_t LibStdcppTupleSyntheticFrontEnd::GetIndexOfChildWithName(
    const ConstString &name) {
  return ExtractIndexFromString(name.GetCString());
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibStdcppTupleSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibStdcppTupleSyntheticFrontEnd(valobj_sp) : nullptr);
}
