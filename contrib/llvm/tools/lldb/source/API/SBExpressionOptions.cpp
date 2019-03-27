//===-- SBExpressionOptions.cpp ---------------------------------------------*-
//C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBStream.h"

#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

SBExpressionOptions::SBExpressionOptions()
    : m_opaque_ap(new EvaluateExpressionOptions()) {}

SBExpressionOptions::SBExpressionOptions(const SBExpressionOptions &rhs) {
  m_opaque_ap.reset(new EvaluateExpressionOptions());
  *(m_opaque_ap.get()) = rhs.ref();
}

const SBExpressionOptions &SBExpressionOptions::
operator=(const SBExpressionOptions &rhs) {
  if (this != &rhs) {
    this->ref() = rhs.ref();
  }
  return *this;
}

SBExpressionOptions::~SBExpressionOptions() {}

bool SBExpressionOptions::GetCoerceResultToId() const {
  return m_opaque_ap->DoesCoerceToId();
}

void SBExpressionOptions::SetCoerceResultToId(bool coerce) {
  m_opaque_ap->SetCoerceToId(coerce);
}

bool SBExpressionOptions::GetUnwindOnError() const {
  return m_opaque_ap->DoesUnwindOnError();
}

void SBExpressionOptions::SetUnwindOnError(bool unwind) {
  m_opaque_ap->SetUnwindOnError(unwind);
}

bool SBExpressionOptions::GetIgnoreBreakpoints() const {
  return m_opaque_ap->DoesIgnoreBreakpoints();
}

void SBExpressionOptions::SetIgnoreBreakpoints(bool ignore) {
  m_opaque_ap->SetIgnoreBreakpoints(ignore);
}

lldb::DynamicValueType SBExpressionOptions::GetFetchDynamicValue() const {
  return m_opaque_ap->GetUseDynamic();
}

void SBExpressionOptions::SetFetchDynamicValue(lldb::DynamicValueType dynamic) {
  m_opaque_ap->SetUseDynamic(dynamic);
}

uint32_t SBExpressionOptions::GetTimeoutInMicroSeconds() const {
  return m_opaque_ap->GetTimeout() ? m_opaque_ap->GetTimeout()->count() : 0;
}

void SBExpressionOptions::SetTimeoutInMicroSeconds(uint32_t timeout) {
  m_opaque_ap->SetTimeout(timeout == 0 ? Timeout<std::micro>(llvm::None)
                                       : std::chrono::microseconds(timeout));
}

uint32_t SBExpressionOptions::GetOneThreadTimeoutInMicroSeconds() const {
  return m_opaque_ap->GetOneThreadTimeout() ? m_opaque_ap->GetOneThreadTimeout()->count() : 0;
}

void SBExpressionOptions::SetOneThreadTimeoutInMicroSeconds(uint32_t timeout) {
  m_opaque_ap->SetOneThreadTimeout(timeout == 0
                                       ? Timeout<std::micro>(llvm::None)
                                       : std::chrono::microseconds(timeout));
}

bool SBExpressionOptions::GetTryAllThreads() const {
  return m_opaque_ap->GetTryAllThreads();
}

void SBExpressionOptions::SetTryAllThreads(bool run_others) {
  m_opaque_ap->SetTryAllThreads(run_others);
}

bool SBExpressionOptions::GetStopOthers() const {
  return m_opaque_ap->GetStopOthers();
}

void SBExpressionOptions::SetStopOthers(bool run_others) {
  m_opaque_ap->SetStopOthers(run_others);
}

bool SBExpressionOptions::GetTrapExceptions() const {
  return m_opaque_ap->GetTrapExceptions();
}

void SBExpressionOptions::SetTrapExceptions(bool trap_exceptions) {
  m_opaque_ap->SetTrapExceptions(trap_exceptions);
}

void SBExpressionOptions::SetLanguage(lldb::LanguageType language) {
  m_opaque_ap->SetLanguage(language);
}

void SBExpressionOptions::SetCancelCallback(
    lldb::ExpressionCancelCallback callback, void *baton) {
  m_opaque_ap->SetCancelCallback(callback, baton);
}

bool SBExpressionOptions::GetGenerateDebugInfo() {
  return m_opaque_ap->GetGenerateDebugInfo();
}

void SBExpressionOptions::SetGenerateDebugInfo(bool b) {
  return m_opaque_ap->SetGenerateDebugInfo(b);
}

bool SBExpressionOptions::GetSuppressPersistentResult() {
  return m_opaque_ap->GetResultIsInternal();
}

void SBExpressionOptions::SetSuppressPersistentResult(bool b) {
  return m_opaque_ap->SetResultIsInternal(b);
}

const char *SBExpressionOptions::GetPrefix() const {
  return m_opaque_ap->GetPrefix();
}

void SBExpressionOptions::SetPrefix(const char *prefix) {
  return m_opaque_ap->SetPrefix(prefix);
}

bool SBExpressionOptions::GetAutoApplyFixIts() {
  return m_opaque_ap->GetAutoApplyFixIts();
}

void SBExpressionOptions::SetAutoApplyFixIts(bool b) {
  return m_opaque_ap->SetAutoApplyFixIts(b);
}

bool SBExpressionOptions::GetTopLevel() {
  return m_opaque_ap->GetExecutionPolicy() == eExecutionPolicyTopLevel;
}

void SBExpressionOptions::SetTopLevel(bool b) {
  m_opaque_ap->SetExecutionPolicy(b ? eExecutionPolicyTopLevel
                                    : m_opaque_ap->default_execution_policy);
}

bool SBExpressionOptions::GetAllowJIT() {
  return m_opaque_ap->GetExecutionPolicy() != eExecutionPolicyNever;
}

void SBExpressionOptions::SetAllowJIT(bool allow) {
  m_opaque_ap->SetExecutionPolicy(allow ? m_opaque_ap->default_execution_policy
                                    : eExecutionPolicyNever);
}

EvaluateExpressionOptions *SBExpressionOptions::get() const {
  return m_opaque_ap.get();
}

EvaluateExpressionOptions &SBExpressionOptions::ref() const {
  return *(m_opaque_ap.get());
}
