//===-- DiagnosticManager.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/DiagnosticManager.h"

#include "llvm/Support/ErrorHandling.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;

void DiagnosticManager::Dump(Log *log) {
  if (!log)
    return;

  std::string str = GetString();

  // GetString() puts a separator after each diagnostic. We want to remove the
  // last '\n' because log->PutCString will add one for us.

  if (str.size() && str.back() == '\n') {
    str.pop_back();
  }

  log->PutCString(str.c_str());
}

static const char *StringForSeverity(DiagnosticSeverity severity) {
  switch (severity) {
  // this should be exhaustive
  case lldb_private::eDiagnosticSeverityError:
    return "error: ";
  case lldb_private::eDiagnosticSeverityWarning:
    return "warning: ";
  case lldb_private::eDiagnosticSeverityRemark:
    return "";
  }
  llvm_unreachable("switch needs another case for DiagnosticSeverity enum");
}

std::string DiagnosticManager::GetString(char separator) {
  std::string ret;

  for (const Diagnostic *diagnostic : Diagnostics()) {
    ret.append(StringForSeverity(diagnostic->GetSeverity()));
    ret.append(diagnostic->GetMessage());
    ret.push_back(separator);
  }

  return ret;
}

size_t DiagnosticManager::Printf(DiagnosticSeverity severity,
                                 const char *format, ...) {
  StreamString ss;

  va_list args;
  va_start(args, format);
  size_t result = ss.PrintfVarArg(format, args);
  va_end(args);

  AddDiagnostic(ss.GetString(), severity, eDiagnosticOriginLLDB);

  return result;
}

size_t DiagnosticManager::PutString(DiagnosticSeverity severity,
                                    llvm::StringRef str) {
  if (str.empty())
    return 0;
  AddDiagnostic(str, severity, eDiagnosticOriginLLDB);
  return str.size();
}

void DiagnosticManager::CopyDiagnostics(DiagnosticManager &otherDiagnostics) {
  for (const DiagnosticList::value_type &other_diagnostic:
       otherDiagnostics.Diagnostics()) {
    AddDiagnostic(
        other_diagnostic->GetMessage(), other_diagnostic->GetSeverity(),
        other_diagnostic->getKind(), other_diagnostic->GetCompilerID());
  }
}
