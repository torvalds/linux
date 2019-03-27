//===-- OptionValueBoolean.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueBoolean.h"

#include "lldb/Host/PosixApi.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueBoolean::DumpValue(const ExecutionContext *exe_ctx,
                                   Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  //    if (dump_mask & eDumpOptionName)
  //        DumpQualifiedName (strm);
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    strm.PutCString(m_current_value ? "true" : "false");
  }
}

Status OptionValueBoolean::SetValueFromString(llvm::StringRef value_str,
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
    bool value = OptionArgParser::ToBoolean(value_str, false, &success);
    if (success) {
      m_value_was_set = true;
      m_current_value = value;
      NotifyValueChanged();
    } else {
      if (value_str.size() == 0)
        error.SetErrorString("invalid boolean string value <empty>");
      else
        error.SetErrorStringWithFormat("invalid boolean string value: '%s'",
                                       value_str.str().c_str());
    }
  } break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value_str, op);
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueBoolean::DeepCopy() const {
  return OptionValueSP(new OptionValueBoolean(*this));
}

size_t OptionValueBoolean::AutoComplete(CommandInterpreter &interpreter,
                                        CompletionRequest &request) {
  request.SetWordComplete(false);
  static const llvm::StringRef g_autocomplete_entries[] = {
      "true", "false", "on", "off", "yes", "no", "1", "0"};

  auto entries = llvm::makeArrayRef(g_autocomplete_entries);

  // only suggest "true" or "false" by default
  if (request.GetCursorArgumentPrefix().empty())
    entries = entries.take_front(2);

  for (auto entry : entries) {
    if (entry.startswith_lower(request.GetCursorArgumentPrefix()))
      request.AddCompletion(entry);
  }
  return request.GetNumberOfMatches();
}
