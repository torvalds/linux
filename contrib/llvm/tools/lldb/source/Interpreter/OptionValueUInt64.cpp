//===-- OptionValueUInt64.cpp ------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueUInt64.h"

#include "lldb/Host/StringConvert.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

lldb::OptionValueSP OptionValueUInt64::Create(llvm::StringRef value_str,
                                              Status &error) {
  lldb::OptionValueSP value_sp(new OptionValueUInt64());
  error = value_sp->SetValueFromString(value_str);
  if (error.Fail())
    value_sp.reset();
  return value_sp;
}

void OptionValueUInt64::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                  uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    strm.Printf("%" PRIu64, m_current_value);
  }
}

Status OptionValueUInt64::SetValueFromString(llvm::StringRef value_ref,
                                             VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    bool success = false;
    std::string value_str = value_ref.trim().str();
    uint64_t value = StringConvert::ToUInt64(value_str.c_str(), 0, 0, &success);
    if (success) {
      m_value_was_set = true;
      m_current_value = value;
      NotifyValueChanged();
    } else {
      error.SetErrorStringWithFormat("invalid uint64_t string value: '%s'",
                                     value_str.c_str());
    }
  } break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value_ref, op);
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueUInt64::DeepCopy() const {
  return OptionValueSP(new OptionValueUInt64(*this));
}
