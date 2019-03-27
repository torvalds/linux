//===-- LockFilePosix.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/LockFilePosix.h"

#include <fcntl.h>
#include <unistd.h>

using namespace lldb;
using namespace lldb_private;

namespace {

Status fileLock(int fd, int cmd, int lock_type, const uint64_t start,
                const uint64_t len) {
  struct flock fl;

  fl.l_type = lock_type;
  fl.l_whence = SEEK_SET;
  fl.l_start = start;
  fl.l_len = len;
  fl.l_pid = ::getpid();

  Status error;
  if (::fcntl(fd, cmd, &fl) == -1)
    error.SetErrorToErrno();

  return error;
}

} // namespace

LockFilePosix::LockFilePosix(int fd) : LockFileBase(fd) {}

LockFilePosix::~LockFilePosix() { Unlock(); }

Status LockFilePosix::DoWriteLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLKW, F_WRLCK, start, len);
}

Status LockFilePosix::DoTryWriteLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLK, F_WRLCK, start, len);
}

Status LockFilePosix::DoReadLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLKW, F_RDLCK, start, len);
}

Status LockFilePosix::DoTryReadLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLK, F_RDLCK, start, len);
}

Status LockFilePosix::DoUnlock() {
  return fileLock(m_fd, F_SETLK, F_UNLCK, m_start, m_len);
}
