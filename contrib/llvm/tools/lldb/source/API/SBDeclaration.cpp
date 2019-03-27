//===-- SBDeclaration.cpp ----------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBStream.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Symbol/Declaration.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include <limits.h>

using namespace lldb;
using namespace lldb_private;

SBDeclaration::SBDeclaration() : m_opaque_ap() {}

SBDeclaration::SBDeclaration(const SBDeclaration &rhs) : m_opaque_ap() {
  if (rhs.IsValid())
    ref() = rhs.ref();
}

SBDeclaration::SBDeclaration(const lldb_private::Declaration *lldb_object_ptr)
    : m_opaque_ap() {
  if (lldb_object_ptr)
    ref() = *lldb_object_ptr;
}

const SBDeclaration &SBDeclaration::operator=(const SBDeclaration &rhs) {
  if (this != &rhs) {
    if (rhs.IsValid())
      ref() = rhs.ref();
    else
      m_opaque_ap.reset();
  }
  return *this;
}

void SBDeclaration::SetDeclaration(
    const lldb_private::Declaration &lldb_object_ref) {
  ref() = lldb_object_ref;
}

SBDeclaration::~SBDeclaration() {}

bool SBDeclaration::IsValid() const {
  return m_opaque_ap.get() && m_opaque_ap->IsValid();
}

SBFileSpec SBDeclaration::GetFileSpec() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBFileSpec sb_file_spec;
  if (m_opaque_ap.get() && m_opaque_ap->GetFile())
    sb_file_spec.SetFileSpec(m_opaque_ap->GetFile());

  if (log) {
    SBStream sstr;
    sb_file_spec.GetDescription(sstr);
    log->Printf("SBLineEntry(%p)::GetFileSpec () => SBFileSpec(%p): %s",
                static_cast<void *>(m_opaque_ap.get()),
                static_cast<const void *>(sb_file_spec.get()), sstr.GetData());
  }

  return sb_file_spec;
}

uint32_t SBDeclaration::GetLine() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t line = 0;
  if (m_opaque_ap)
    line = m_opaque_ap->GetLine();

  if (log)
    log->Printf("SBLineEntry(%p)::GetLine () => %u",
                static_cast<void *>(m_opaque_ap.get()), line);

  return line;
}

uint32_t SBDeclaration::GetColumn() const {
  if (m_opaque_ap)
    return m_opaque_ap->GetColumn();
  return 0;
}

void SBDeclaration::SetFileSpec(lldb::SBFileSpec filespec) {
  if (filespec.IsValid())
    ref().SetFile(filespec.ref());
  else
    ref().SetFile(FileSpec());
}
void SBDeclaration::SetLine(uint32_t line) { ref().SetLine(line); }

void SBDeclaration::SetColumn(uint32_t column) { ref().SetColumn(column); }

bool SBDeclaration::operator==(const SBDeclaration &rhs) const {
  lldb_private::Declaration *lhs_ptr = m_opaque_ap.get();
  lldb_private::Declaration *rhs_ptr = rhs.m_opaque_ap.get();

  if (lhs_ptr && rhs_ptr)
    return lldb_private::Declaration::Compare(*lhs_ptr, *rhs_ptr) == 0;

  return lhs_ptr == rhs_ptr;
}

bool SBDeclaration::operator!=(const SBDeclaration &rhs) const {
  lldb_private::Declaration *lhs_ptr = m_opaque_ap.get();
  lldb_private::Declaration *rhs_ptr = rhs.m_opaque_ap.get();

  if (lhs_ptr && rhs_ptr)
    return lldb_private::Declaration::Compare(*lhs_ptr, *rhs_ptr) != 0;

  return lhs_ptr != rhs_ptr;
}

const lldb_private::Declaration *SBDeclaration::operator->() const {
  return m_opaque_ap.get();
}

lldb_private::Declaration &SBDeclaration::ref() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new lldb_private::Declaration());
  return *m_opaque_ap;
}

const lldb_private::Declaration &SBDeclaration::ref() const {
  return *m_opaque_ap;
}

bool SBDeclaration::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  if (m_opaque_ap) {
    char file_path[PATH_MAX * 2];
    m_opaque_ap->GetFile().GetPath(file_path, sizeof(file_path));
    strm.Printf("%s:%u", file_path, GetLine());
    if (GetColumn() > 0)
      strm.Printf(":%u", GetColumn());
  } else
    strm.PutCString("No value");

  return true;
}

lldb_private::Declaration *SBDeclaration::get() { return m_opaque_ap.get(); }
