//===-- OptionValueString.cpp ------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueString.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueString::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                  uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    if (!m_current_value.empty() || m_value_was_set) {
      if (m_options.Test(eOptionEncodeCharacterEscapeSequences)) {
        std::string expanded_escape_value;
        Args::ExpandEscapedCharacters(m_current_value.c_str(),
                                      expanded_escape_value);
        if (dump_mask & eDumpOptionRaw)
          strm.Printf("%s", expanded_escape_value.c_str());
        else
          strm.Printf("\"%s\"", expanded_escape_value.c_str());
      } else {
        if (dump_mask & eDumpOptionRaw)
          strm.Printf("%s", m_current_value.c_str());
        else
          strm.Printf("\"%s\"", m_current_value.c_str());
      }
    }
  }
}

Status OptionValueString::SetValueFromString(llvm::StringRef value,
                                             VarSetOperationType op) {
  Status error;

  std::string value_str = value.str();
  value = value.trim();
  if (value.size() > 0) {
    switch (value.front()) {
    case '"':
    case '\'': {
      if (value.size() <= 1 || value.back() != value.front()) {
        error.SetErrorString("mismatched quotes");
        return error;
      }
      value = value.drop_front().drop_back();
    } break;
    }
    value_str = value.str();
  }

  switch (op) {
  case eVarSetOperationInvalid:
  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
    if (m_validator) {
      error = m_validator(value_str.c_str(), m_validator_baton);
      if (error.Fail())
        return error;
    }
    error = OptionValue::SetValueFromString(value, op);
    break;

  case eVarSetOperationAppend: {
    std::string new_value(m_current_value);
    if (value.size() > 0) {
      if (m_options.Test(eOptionEncodeCharacterEscapeSequences)) {
        std::string str;
        Args::EncodeEscapeSequences(value_str.c_str(), str);
        new_value.append(str);
      } else
        new_value.append(value);
    }
    if (m_validator) {
      error = m_validator(new_value.c_str(), m_validator_baton);
      if (error.Fail())
        return error;
    }
    m_current_value.assign(new_value);
    NotifyValueChanged();
  } break;

  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
    if (m_validator) {
      error = m_validator(value_str.c_str(), m_validator_baton);
      if (error.Fail())
        return error;
    }
    m_value_was_set = true;
    if (m_options.Test(eOptionEncodeCharacterEscapeSequences)) {
      Args::EncodeEscapeSequences(value_str.c_str(), m_current_value);
    } else {
      SetCurrentValue(value_str);
    }
    NotifyValueChanged();
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueString::DeepCopy() const {
  return OptionValueSP(new OptionValueString(*this));
}

Status OptionValueString::SetCurrentValue(llvm::StringRef value) {
  if (m_validator) {
    Status error(m_validator(value.str().c_str(), m_validator_baton));
    if (error.Fail())
      return error;
  }
  m_current_value.assign(value);
  return Status();
}

Status OptionValueString::AppendToCurrentValue(const char *value) {
  if (value && value[0]) {
    if (m_validator) {
      std::string new_value(m_current_value);
      new_value.append(value);
      Status error(m_validator(value, m_validator_baton));
      if (error.Fail())
        return error;
      m_current_value.assign(new_value);
    } else
      m_current_value.append(value);
  }
  return Status();
}
