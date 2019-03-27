//===-- OptionValueFileSpec.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueFileSpec.h"

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

OptionValueFileSpec::OptionValueFileSpec(bool resolve)
    : OptionValue(), m_current_value(), m_default_value(), m_data_sp(),
      m_data_mod_time(),
      m_completion_mask(CommandCompletions::eDiskFileCompletion),
      m_resolve(resolve) {}

OptionValueFileSpec::OptionValueFileSpec(const FileSpec &value, bool resolve)
    : OptionValue(), m_current_value(value), m_default_value(value),
      m_data_sp(), m_data_mod_time(),
      m_completion_mask(CommandCompletions::eDiskFileCompletion),
      m_resolve(resolve) {}

OptionValueFileSpec::OptionValueFileSpec(const FileSpec &current_value,
                                         const FileSpec &default_value,
                                         bool resolve)
    : OptionValue(), m_current_value(current_value),
      m_default_value(default_value), m_data_sp(), m_data_mod_time(),
      m_completion_mask(CommandCompletions::eDiskFileCompletion),
      m_resolve(resolve) {}

void OptionValueFileSpec::DumpValue(const ExecutionContext *exe_ctx,
                                    Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");

    if (m_current_value) {
      strm << '"' << m_current_value.GetPath().c_str() << '"';
    }
  }
}

Status OptionValueFileSpec::SetValueFromString(llvm::StringRef value,
                                               VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
    if (value.size() > 0) {
      // The setting value may have whitespace, double-quotes, or single-quotes
      // around the file path to indicate that internal spaces are not word
      // breaks.  Strip off any ws & quotes from the start and end of the file
      // path - we aren't doing any word // breaking here so the quoting is
      // unnecessary.  NB this will cause a problem if someone tries to specify
      // a file path that legitimately begins or ends with a " or ' character,
      // or whitespace.
      value = value.trim("\"' \t");
      m_value_was_set = true;
      m_current_value.SetFile(value.str(), FileSpec::Style::native);
      if (m_resolve)
        FileSystem::Instance().Resolve(m_current_value);
      m_data_sp.reset();
      m_data_mod_time = llvm::sys::TimePoint<>();
      NotifyValueChanged();
    } else {
      error.SetErrorString("invalid value string");
    }
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value, op);
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueFileSpec::DeepCopy() const {
  return OptionValueSP(new OptionValueFileSpec(*this));
}

size_t OptionValueFileSpec::AutoComplete(CommandInterpreter &interpreter,
                                         CompletionRequest &request) {
  request.SetWordComplete(false);
  CommandCompletions::InvokeCommonCompletionCallbacks(
      interpreter, m_completion_mask, request, nullptr);
  return request.GetNumberOfMatches();
}

const lldb::DataBufferSP &OptionValueFileSpec::GetFileContents() {
  if (m_current_value) {
    const auto file_mod_time = FileSystem::Instance().GetModificationTime(m_current_value);
    if (m_data_sp && m_data_mod_time == file_mod_time)
      return m_data_sp;
    m_data_sp =
        FileSystem::Instance().CreateDataBuffer(m_current_value.GetPath());
    m_data_mod_time = file_mod_time;
  }
  return m_data_sp;
}
