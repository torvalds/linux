//===-- TypeSummary.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeSummary.h"




#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

TypeSummaryOptions::TypeSummaryOptions()
    : m_lang(eLanguageTypeUnknown), m_capping(eTypeSummaryCapped) {}

TypeSummaryOptions::TypeSummaryOptions(const TypeSummaryOptions &rhs)
    : m_lang(rhs.m_lang), m_capping(rhs.m_capping) {}

TypeSummaryOptions &TypeSummaryOptions::
operator=(const TypeSummaryOptions &rhs) {
  m_lang = rhs.m_lang;
  m_capping = rhs.m_capping;
  return *this;
}

lldb::LanguageType TypeSummaryOptions::GetLanguage() const { return m_lang; }

lldb::TypeSummaryCapping TypeSummaryOptions::GetCapping() const {
  return m_capping;
}

TypeSummaryOptions &TypeSummaryOptions::SetLanguage(lldb::LanguageType lang) {
  m_lang = lang;
  return *this;
}

TypeSummaryOptions &
TypeSummaryOptions::SetCapping(lldb::TypeSummaryCapping cap) {
  m_capping = cap;
  return *this;
}

TypeSummaryImpl::TypeSummaryImpl(Kind kind, const TypeSummaryImpl::Flags &flags)
    : m_flags(flags), m_kind(kind) {}

StringSummaryFormat::StringSummaryFormat(const TypeSummaryImpl::Flags &flags,
                                         const char *format_cstr)
    : TypeSummaryImpl(Kind::eSummaryString, flags), m_format_str() {
  SetSummaryString(format_cstr);
}

void StringSummaryFormat::SetSummaryString(const char *format_cstr) {
  m_format.Clear();
  if (format_cstr && format_cstr[0]) {
    m_format_str = format_cstr;
    m_error = FormatEntity::Parse(format_cstr, m_format);
  } else {
    m_format_str.clear();
    m_error.Clear();
  }
}

bool StringSummaryFormat::FormatObject(ValueObject *valobj, std::string &retval,
                                       const TypeSummaryOptions &options) {
  if (!valobj) {
    retval.assign("NULL ValueObject");
    return false;
  }

  StreamString s;
  ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
  SymbolContext sc;
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame)
    sc = frame->GetSymbolContext(lldb::eSymbolContextEverything);

  if (IsOneLiner()) {
    ValueObjectPrinter printer(valobj, &s, DumpValueObjectOptions());
    printer.PrintChildrenOneLiner(HideNames(valobj));
    retval = s.GetString();
    return true;
  } else {
    if (FormatEntity::Format(m_format, s, &sc, &exe_ctx,
                             &sc.line_entry.range.GetBaseAddress(), valobj,
                             false, false)) {
      retval.assign(s.GetString());
      return true;
    } else {
      retval.assign("error: summary string parsing error");
      return false;
    }
  }
}

std::string StringSummaryFormat::GetDescription() {
  StreamString sstr;

  sstr.Printf("`%s`%s%s%s%s%s%s%s%s%s", m_format_str.c_str(),
              m_error.Fail() ? " error: " : "",
              m_error.Fail() ? m_error.AsCString() : "",
              Cascades() ? "" : " (not cascading)",
              !DoesPrintChildren(nullptr) ? "" : " (show children)",
              !DoesPrintValue(nullptr) ? " (hide value)" : "",
              IsOneLiner() ? " (one-line printout)" : "",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "",
              HideNames(nullptr) ? " (hide member names)" : "");
  return sstr.GetString();
}

CXXFunctionSummaryFormat::CXXFunctionSummaryFormat(
    const TypeSummaryImpl::Flags &flags, Callback impl, const char *description)
    : TypeSummaryImpl(Kind::eCallback, flags), m_impl(impl),
      m_description(description ? description : "") {}

bool CXXFunctionSummaryFormat::FormatObject(ValueObject *valobj,
                                            std::string &dest,
                                            const TypeSummaryOptions &options) {
  dest.clear();
  StreamString stream;
  if (!m_impl || !m_impl(*valobj, stream, options))
    return false;
  dest = stream.GetString();
  return true;
}

std::string CXXFunctionSummaryFormat::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s%s%s%s%s %s", Cascades() ? "" : " (not cascading)",
              !DoesPrintChildren(nullptr) ? "" : " (show children)",
              !DoesPrintValue(nullptr) ? " (hide value)" : "",
              IsOneLiner() ? " (one-line printout)" : "",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "",
              HideNames(nullptr) ? " (hide member names)" : "",
              m_description.c_str());
  return sstr.GetString();
}

ScriptSummaryFormat::ScriptSummaryFormat(const TypeSummaryImpl::Flags &flags,
                                         const char *function_name,
                                         const char *python_script)
    : TypeSummaryImpl(Kind::eScript, flags), m_function_name(),
      m_python_script(), m_script_function_sp() {
  if (function_name)
    m_function_name.assign(function_name);
  if (python_script)
    m_python_script.assign(python_script);
}

bool ScriptSummaryFormat::FormatObject(ValueObject *valobj, std::string &retval,
                                       const TypeSummaryOptions &options) {
  if (!valobj)
    return false;

  TargetSP target_sp(valobj->GetTargetSP());

  if (!target_sp) {
    retval.assign("error: no target");
    return false;
  }

  ScriptInterpreter *script_interpreter =
      target_sp->GetDebugger().GetCommandInterpreter().GetScriptInterpreter();

  if (!script_interpreter) {
    retval.assign("error: no ScriptInterpreter");
    return false;
  }

  return script_interpreter->GetScriptedSummary(
      m_function_name.c_str(), valobj->GetSP(), m_script_function_sp, options,
      retval);
}

std::string ScriptSummaryFormat::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s%s%s%s%s\n  ", Cascades() ? "" : " (not cascading)",
              !DoesPrintChildren(nullptr) ? "" : " (show children)",
              !DoesPrintValue(nullptr) ? " (hide value)" : "",
              IsOneLiner() ? " (one-line printout)" : "",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "",
              HideNames(nullptr) ? " (hide member names)" : "");
  if (m_python_script.empty()) {
    if (m_function_name.empty()) {
      sstr.PutCString("no backing script");
    } else {
      sstr.PutCString(m_function_name);
    }
  } else {
    sstr.PutCString(m_python_script);
  }
  return sstr.GetString();
}
