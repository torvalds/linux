//===-- SBError.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBError.h"
#include "lldb/API/SBStream.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include <stdarg.h>

using namespace lldb;
using namespace lldb_private;

SBError::SBError() : m_opaque_ap() {}

SBError::SBError(const SBError &rhs) : m_opaque_ap() {
  if (rhs.IsValid())
    m_opaque_ap.reset(new Status(*rhs));
}

SBError::~SBError() {}

const SBError &SBError::operator=(const SBError &rhs) {
  if (rhs.IsValid()) {
    if (m_opaque_ap)
      *m_opaque_ap = *rhs;
    else
      m_opaque_ap.reset(new Status(*rhs));
  } else
    m_opaque_ap.reset();

  return *this;
}

const char *SBError::GetCString() const {
  if (m_opaque_ap)
    return m_opaque_ap->AsCString();
  return NULL;
}

void SBError::Clear() {
  if (m_opaque_ap)
    m_opaque_ap->Clear();
}

bool SBError::Fail() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool ret_value = false;
  if (m_opaque_ap)
    ret_value = m_opaque_ap->Fail();

  if (log)
    log->Printf("SBError(%p)::Fail () => %i",
                static_cast<void *>(m_opaque_ap.get()), ret_value);

  return ret_value;
}

bool SBError::Success() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  bool ret_value = true;
  if (m_opaque_ap)
    ret_value = m_opaque_ap->Success();

  if (log)
    log->Printf("SBError(%p)::Success () => %i",
                static_cast<void *>(m_opaque_ap.get()), ret_value);

  return ret_value;
}

uint32_t SBError::GetError() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t err = 0;
  if (m_opaque_ap)
    err = m_opaque_ap->GetError();

  if (log)
    log->Printf("SBError(%p)::GetError () => 0x%8.8x",
                static_cast<void *>(m_opaque_ap.get()), err);

  return err;
}

ErrorType SBError::GetType() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ErrorType err_type = eErrorTypeInvalid;
  if (m_opaque_ap)
    err_type = m_opaque_ap->GetType();

  if (log)
    log->Printf("SBError(%p)::GetType () => %i",
                static_cast<void *>(m_opaque_ap.get()), err_type);

  return err_type;
}

void SBError::SetError(uint32_t err, ErrorType type) {
  CreateIfNeeded();
  m_opaque_ap->SetError(err, type);
}

void SBError::SetError(const Status &lldb_error) {
  CreateIfNeeded();
  *m_opaque_ap = lldb_error;
}

void SBError::SetErrorToErrno() {
  CreateIfNeeded();
  m_opaque_ap->SetErrorToErrno();
}

void SBError::SetErrorToGenericError() {
  CreateIfNeeded();
  m_opaque_ap->SetErrorToErrno();
}

void SBError::SetErrorString(const char *err_str) {
  CreateIfNeeded();
  m_opaque_ap->SetErrorString(err_str);
}

int SBError::SetErrorStringWithFormat(const char *format, ...) {
  CreateIfNeeded();
  va_list args;
  va_start(args, format);
  int num_chars = m_opaque_ap->SetErrorStringWithVarArg(format, args);
  va_end(args);
  return num_chars;
}

bool SBError::IsValid() const { return m_opaque_ap != NULL; }

void SBError::CreateIfNeeded() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new Status());
}

lldb_private::Status *SBError::operator->() { return m_opaque_ap.get(); }

lldb_private::Status *SBError::get() { return m_opaque_ap.get(); }

lldb_private::Status &SBError::ref() {
  CreateIfNeeded();
  return *m_opaque_ap;
}

const lldb_private::Status &SBError::operator*() const {
  // Be sure to call "IsValid()" before calling this function or it will crash
  return *m_opaque_ap;
}

bool SBError::GetDescription(SBStream &description) {
  if (m_opaque_ap) {
    if (m_opaque_ap->Success())
      description.Printf("success");
    else {
      const char *err_string = GetCString();
      description.Printf("error: %s", (err_string != NULL ? err_string : ""));
    }
  } else
    description.Printf("error: <NULL>");

  return true;
}
