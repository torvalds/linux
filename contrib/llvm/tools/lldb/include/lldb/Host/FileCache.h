//===-- FileCache.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef liblldb_Host_FileCache_h
#define liblldb_Host_FileCache_h

#include <map>
#include <stdint.h>

#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"

namespace lldb_private {
class FileCache {
private:
  FileCache() {}

  typedef std::map<lldb::user_id_t, lldb::FileSP> FDToFileMap;

public:
  static FileCache &GetInstance();

  lldb::user_id_t OpenFile(const FileSpec &file_spec, uint32_t flags,
                           uint32_t mode, Status &error);
  bool CloseFile(lldb::user_id_t fd, Status &error);

  uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset, const void *src,
                     uint64_t src_len, Status &error);
  uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                    uint64_t dst_len, Status &error);

private:
  static FileCache *m_instance;

  FDToFileMap m_cache;
};
}

#endif
