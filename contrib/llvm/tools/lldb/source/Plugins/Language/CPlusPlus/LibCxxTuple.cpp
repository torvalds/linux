//===-- LibCxxTuple.cpp -----------------------------------------*- C++ -*-===//
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

class TupleFrontEnd: public SyntheticChildrenFrontEnd {
public:
  TupleFrontEnd(ValueObject &valobj) : SyntheticChildrenFrontEnd(valobj) {
    Update();
  }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    return formatters::ExtractIndexFromString(name.GetCString());
  }

  bool MightHaveChildren() override { return true; }
  bool Update() override;
  size_t CalculateNumChildren() override { return m_elements.size(); }
  ValueObjectSP GetChildAtIndex(size_t idx) override;

private:
  std::vector<ValueObjectSP> m_elements;
  ValueObjectSP m_base_sp;
};
}

bool TupleFrontEnd::Update() {
  m_elements.clear();
  m_base_sp = m_backend.GetChildMemberWithName(ConstString("__base_"), true);
  if (! m_base_sp) {
    // Pre r304382 name of the base element.
    m_base_sp = m_backend.GetChildMemberWithName(ConstString("base_"), true);
  }
  if (! m_base_sp)
    return false;
  m_elements.assign(m_base_sp->GetCompilerType().GetNumDirectBaseClasses(),
                    ValueObjectSP());
  return false;
}

ValueObjectSP TupleFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx >= m_elements.size())
    return ValueObjectSP();
  if (!m_base_sp)
    return ValueObjectSP();
  if (m_elements[idx])
    return m_elements[idx];

  CompilerType holder_type =
      m_base_sp->GetCompilerType().GetDirectBaseClassAtIndex(idx, nullptr);
  if (!holder_type)
    return ValueObjectSP();
  ValueObjectSP holder_sp = m_base_sp->GetChildAtIndex(idx, true);
  if (!holder_sp)
    return ValueObjectSP();

  ValueObjectSP elem_sp = holder_sp->GetChildAtIndex(0, true);
  if (elem_sp)
    m_elements[idx] =
        elem_sp->Clone(ConstString(llvm::formatv("[{0}]", idx).str()));

  return m_elements[idx];
}

SyntheticChildrenFrontEnd *
formatters::LibcxxTupleFrontEndCreator(CXXSyntheticChildren *,
                                       lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new TupleFrontEnd(*valobj_sp);
  return nullptr;
}
