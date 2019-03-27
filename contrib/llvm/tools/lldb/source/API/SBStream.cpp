//===-- SBStream.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBStream.h"

#include "lldb/Core/StreamFile.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBStream::SBStream() : m_opaque_ap(new StreamString()), m_is_file(false) {}

SBStream::SBStream(SBStream &&rhs)
    : m_opaque_ap(std::move(rhs.m_opaque_ap)), m_is_file(rhs.m_is_file) {}

SBStream::~SBStream() {}

bool SBStream::IsValid() const { return (m_opaque_ap != NULL); }

// If this stream is not redirected to a file, it will maintain a local cache
// for the stream data which can be accessed using this accessor.
const char *SBStream::GetData() {
  if (m_is_file || m_opaque_ap == NULL)
    return NULL;

  return static_cast<StreamString *>(m_opaque_ap.get())->GetData();
}

// If this stream is not redirected to a file, it will maintain a local cache
// for the stream output whose length can be accessed using this accessor.
size_t SBStream::GetSize() {
  if (m_is_file || m_opaque_ap == NULL)
    return 0;

  return static_cast<StreamString *>(m_opaque_ap.get())->GetSize();
}

void SBStream::Printf(const char *format, ...) {
  if (!format)
    return;
  va_list args;
  va_start(args, format);
  ref().PrintfVarArg(format, args);
  va_end(args);
}

void SBStream::RedirectToFile(const char *path, bool append) {
  if (path == nullptr)
    return;

  std::string local_data;
  if (m_opaque_ap) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (!m_is_file)
      local_data = static_cast<StreamString *>(m_opaque_ap.get())->GetString();
  }
  StreamFile *stream_file = new StreamFile;
  uint32_t open_options = File::eOpenOptionWrite | File::eOpenOptionCanCreate;
  if (append)
    open_options |= File::eOpenOptionAppend;
  else
    open_options |= File::eOpenOptionTruncate;

  FileSystem::Instance().Open(stream_file->GetFile(), FileSpec(path),
                              open_options);
  m_opaque_ap.reset(stream_file);

  if (m_opaque_ap) {
    m_is_file = true;

    // If we had any data locally in our StreamString, then pass that along to
    // the to new file we are redirecting to.
    if (!local_data.empty())
      m_opaque_ap->Write(&local_data[0], local_data.size());
  } else
    m_is_file = false;
}

void SBStream::RedirectToFileHandle(FILE *fh, bool transfer_fh_ownership) {
  if (fh == nullptr)
    return;

  std::string local_data;
  if (m_opaque_ap) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (!m_is_file)
      local_data = static_cast<StreamString *>(m_opaque_ap.get())->GetString();
  }
  m_opaque_ap.reset(new StreamFile(fh, transfer_fh_ownership));

  if (m_opaque_ap) {
    m_is_file = true;

    // If we had any data locally in our StreamString, then pass that along to
    // the to new file we are redirecting to.
    if (!local_data.empty())
      m_opaque_ap->Write(&local_data[0], local_data.size());
  } else
    m_is_file = false;
}

void SBStream::RedirectToFileDescriptor(int fd, bool transfer_fh_ownership) {
  std::string local_data;
  if (m_opaque_ap) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (!m_is_file)
      local_data = static_cast<StreamString *>(m_opaque_ap.get())->GetString();
  }

  m_opaque_ap.reset(new StreamFile(::fdopen(fd, "w"), transfer_fh_ownership));
  if (m_opaque_ap) {
    m_is_file = true;

    // If we had any data locally in our StreamString, then pass that along to
    // the to new file we are redirecting to.
    if (!local_data.empty())
      m_opaque_ap->Write(&local_data[0], local_data.size());
  } else
    m_is_file = false;
}

lldb_private::Stream *SBStream::operator->() { return m_opaque_ap.get(); }

lldb_private::Stream *SBStream::get() { return m_opaque_ap.get(); }

lldb_private::Stream &SBStream::ref() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new StreamString());
  return *m_opaque_ap;
}

void SBStream::Clear() {
  if (m_opaque_ap) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (m_is_file)
      m_opaque_ap.reset();
    else
      static_cast<StreamString *>(m_opaque_ap.get())->Clear();
  }
}
