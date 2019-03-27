//===-- OptionValueRegex.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueRegex.h"

#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueRegex::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                 uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    if (m_regex.IsValid()) {
      llvm::StringRef regex_text = m_regex.GetText();
      strm.Printf("%s", regex_text.str().c_str());
    }
  }
}

Status OptionValueRegex::SetValueFromString(llvm::StringRef value,
                                            VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationInvalid:
  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
    error = OptionValue::SetValueFromString(value, op);
    break;

  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
    if (m_regex.Compile(value)) {
      m_value_was_set = true;
      NotifyValueChanged();
    } else {
      char regex_error[1024];
      if (m_regex.GetErrorAsCString(regex_error, sizeof(regex_error)))
        error.SetErrorString(regex_error);
      else
        error.SetErrorStringWithFormat("regex error %u",
                                       m_regex.GetErrorCode());
    }
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueRegex::DeepCopy() const {
  return OptionValueSP(new OptionValueRegex(m_regex.GetText().str().c_str()));
}
