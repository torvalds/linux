//===-- SBFileSpecList.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <limits.h>

#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFileSpecList.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/FileSpecList.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SBFileSpecList::SBFileSpecList() : m_opaque_ap(new FileSpecList()) {}

SBFileSpecList::SBFileSpecList(const SBFileSpecList &rhs) : m_opaque_ap() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (rhs.m_opaque_ap)
    m_opaque_ap.reset(new FileSpecList(*(rhs.get())));

  if (log) {
    log->Printf("SBFileSpecList::SBFileSpecList (const SBFileSpecList "
                "rhs.ap=%p) => SBFileSpecList(%p)",
                static_cast<void *>(rhs.m_opaque_ap.get()),
                static_cast<void *>(m_opaque_ap.get()));
  }
}

SBFileSpecList::~SBFileSpecList() {}

const SBFileSpecList &SBFileSpecList::operator=(const SBFileSpecList &rhs) {
  if (this != &rhs) {
    m_opaque_ap.reset(new lldb_private::FileSpecList(*(rhs.get())));
  }
  return *this;
}

uint32_t SBFileSpecList::GetSize() const { return m_opaque_ap->GetSize(); }

void SBFileSpecList::Append(const SBFileSpec &sb_file) {
  m_opaque_ap->Append(sb_file.ref());
}

bool SBFileSpecList::AppendIfUnique(const SBFileSpec &sb_file) {
  return m_opaque_ap->AppendIfUnique(sb_file.ref());
}

void SBFileSpecList::Clear() { m_opaque_ap->Clear(); }

uint32_t SBFileSpecList::FindFileIndex(uint32_t idx, const SBFileSpec &sb_file,
                                       bool full) {
  return m_opaque_ap->FindFileIndex(idx, sb_file.ref(), full);
}

const SBFileSpec SBFileSpecList::GetFileSpecAtIndex(uint32_t idx) const {
  SBFileSpec new_spec;
  new_spec.SetFileSpec(m_opaque_ap->GetFileSpecAtIndex(idx));
  return new_spec;
}

const lldb_private::FileSpecList *SBFileSpecList::operator->() const {
  return m_opaque_ap.get();
}

const lldb_private::FileSpecList *SBFileSpecList::get() const {
  return m_opaque_ap.get();
}

const lldb_private::FileSpecList &SBFileSpecList::operator*() const {
  return *m_opaque_ap;
}

const lldb_private::FileSpecList &SBFileSpecList::ref() const {
  return *m_opaque_ap;
}

bool SBFileSpecList::GetDescription(SBStream &description) const {
  Stream &strm = description.ref();

  if (m_opaque_ap) {
    uint32_t num_files = m_opaque_ap->GetSize();
    strm.Printf("%d files: ", num_files);
    for (uint32_t i = 0; i < num_files; i++) {
      char path[PATH_MAX];
      if (m_opaque_ap->GetFileSpecAtIndex(i).GetPath(path, sizeof(path)))
        strm.Printf("\n    %s", path);
    }
  } else
    strm.PutCString("No value");

  return true;
}
