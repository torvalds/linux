//===-- FileCache.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/FileCache.h"

#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"

using namespace lldb;
using namespace lldb_private;

FileCache *FileCache::m_instance = nullptr;

FileCache &FileCache::GetInstance() {
  if (m_instance == nullptr)
    m_instance = new FileCache();

  return *m_instance;
}

lldb::user_id_t FileCache::OpenFile(const FileSpec &file_spec, uint32_t flags,
                                    uint32_t mode, Status &error) {
  if (!file_spec) {
    error.SetErrorString("empty path");
    return UINT64_MAX;
  }
  FileSP file_sp(new File());
  error = FileSystem::Instance().Open(*file_sp, file_spec, flags, mode);
  if (!file_sp->IsValid())
    return UINT64_MAX;
  lldb::user_id_t fd = file_sp->GetDescriptor();
  m_cache[fd] = file_sp;
  return fd;
}

bool FileCache::CloseFile(lldb::user_id_t fd, Status &error) {
  if (fd == UINT64_MAX) {
    error.SetErrorString("invalid file descriptor");
    return false;
  }
  FDToFileMap::iterator pos = m_cache.find(fd);
  if (pos == m_cache.end()) {
    error.SetErrorStringWithFormat("invalid host file descriptor %" PRIu64, fd);
    return false;
  }
  FileSP file_sp = pos->second;
  if (!file_sp) {
    error.SetErrorString("invalid host backing file");
    return false;
  }
  error = file_sp->Close();
  m_cache.erase(pos);
  return error.Success();
}

uint64_t FileCache::WriteFile(lldb::user_id_t fd, uint64_t offset,
                              const void *src, uint64_t src_len,
                              Status &error) {
  if (fd == UINT64_MAX) {
    error.SetErrorString("invalid file descriptor");
    return UINT64_MAX;
  }
  FDToFileMap::iterator pos = m_cache.find(fd);
  if (pos == m_cache.end()) {
    error.SetErrorStringWithFormat("invalid host file descriptor %" PRIu64, fd);
    return false;
  }
  FileSP file_sp = pos->second;
  if (!file_sp) {
    error.SetErrorString("invalid host backing file");
    return UINT64_MAX;
  }
  if (static_cast<uint64_t>(file_sp->SeekFromStart(offset, &error)) != offset ||
      error.Fail())
    return UINT64_MAX;
  size_t bytes_written = src_len;
  error = file_sp->Write(src, bytes_written);
  if (error.Fail())
    return UINT64_MAX;
  return bytes_written;
}

uint64_t FileCache::ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                             uint64_t dst_len, Status &error) {
  if (fd == UINT64_MAX) {
    error.SetErrorString("invalid file descriptor");
    return UINT64_MAX;
  }
  FDToFileMap::iterator pos = m_cache.find(fd);
  if (pos == m_cache.end()) {
    error.SetErrorStringWithFormat("invalid host file descriptor %" PRIu64, fd);
    return false;
  }
  FileSP file_sp = pos->second;
  if (!file_sp) {
    error.SetErrorString("invalid host backing file");
    return UINT64_MAX;
  }
  if (static_cast<uint64_t>(file_sp->SeekFromStart(offset, &error)) != offset ||
      error.Fail())
    return UINT64_MAX;
  size_t bytes_read = dst_len;
  error = file_sp->Read(dst, bytes_read);
  if (error.Fail())
    return UINT64_MAX;
  return bytes_read;
}
