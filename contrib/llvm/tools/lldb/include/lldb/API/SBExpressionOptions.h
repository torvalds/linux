//===-- SBEvent.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBExpressionOptions_h_
#define LLDB_SBExpressionOptions_h_

#include "lldb/API/SBDefines.h"

#include <vector>

namespace lldb {

class LLDB_API SBExpressionOptions {
public:
  SBExpressionOptions();

  SBExpressionOptions(const lldb::SBExpressionOptions &rhs);

  ~SBExpressionOptions();

  const SBExpressionOptions &operator=(const lldb::SBExpressionOptions &rhs);

  bool GetCoerceResultToId() const;

  void SetCoerceResultToId(bool coerce = true);

  bool GetUnwindOnError() const;

  void SetUnwindOnError(bool unwind = true);

  bool GetIgnoreBreakpoints() const;

  void SetIgnoreBreakpoints(bool ignore = true);

  lldb::DynamicValueType GetFetchDynamicValue() const;

  void SetFetchDynamicValue(
      lldb::DynamicValueType dynamic = lldb::eDynamicCanRunTarget);

  uint32_t GetTimeoutInMicroSeconds() const;

  // Set the timeout for the expression, 0 means wait forever.
  void SetTimeoutInMicroSeconds(uint32_t timeout = 0);

  uint32_t GetOneThreadTimeoutInMicroSeconds() const;

  // Set the timeout for running on one thread, 0 means use the default
  // behavior. If you set this higher than the overall timeout, you'll get an
  // error when you try to run the expression.
  void SetOneThreadTimeoutInMicroSeconds(uint32_t timeout = 0);

  bool GetTryAllThreads() const;

  void SetTryAllThreads(bool run_others = true);

  bool GetStopOthers() const;

  void SetStopOthers(bool stop_others = true);

  bool GetTrapExceptions() const;

  void SetTrapExceptions(bool trap_exceptions = true);

  void SetLanguage(lldb::LanguageType language);

  void SetCancelCallback(lldb::ExpressionCancelCallback callback, void *baton);

  bool GetGenerateDebugInfo();

  void SetGenerateDebugInfo(bool b = true);

  bool GetSuppressPersistentResult();

  void SetSuppressPersistentResult(bool b = false);

  const char *GetPrefix() const;

  void SetPrefix(const char *prefix);

  void SetAutoApplyFixIts(bool b = true);

  bool GetAutoApplyFixIts();

  bool GetTopLevel();

  void SetTopLevel(bool b = true);
  
  // Gets whether we will JIT an expression if it cannot be interpreted
  bool GetAllowJIT();
  
  // Sets whether we will JIT an expression if it cannot be interpreted
  void SetAllowJIT(bool allow);

protected:
  SBExpressionOptions(
      lldb_private::EvaluateExpressionOptions &expression_options);

  lldb_private::EvaluateExpressionOptions *get() const;

  lldb_private::EvaluateExpressionOptions &ref() const;

  friend class SBFrame;
  friend class SBValue;
  friend class SBTarget;

private:
  // This auto_pointer is made in the constructor and is always valid.
  mutable std::unique_ptr<lldb_private::EvaluateExpressionOptions> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBExpressionOptions_h_
