//===-- sanitizer_file.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.  It defines filesystem-related interfaces.  This
// is separate from sanitizer_common.cc so that it's simpler to disable
// all the filesystem support code for a port that doesn't use it.
//
//===---------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if !SANITIZER_FUCHSIA

#include "sanitizer_common.h"
#include "sanitizer_file.h"

namespace __sanitizer {

void CatastrophicErrorWrite(const char *buffer, uptr length) {
  WriteToFile(kStderrFd, buffer, length);
}

StaticSpinMutex report_file_mu;
ReportFile report_file = {&report_file_mu, kStderrFd, "", "", 0};

void RawWrite(const char *buffer) {
  report_file.Write(buffer, internal_strlen(buffer));
}

void ReportFile::ReopenIfNecessary() {
  mu->CheckLocked();
  if (fd == kStdoutFd || fd == kStderrFd) return;

  uptr pid = internal_getpid();
  // If in tracer, use the parent's file.
  if (pid == stoptheworld_tracer_pid)
    pid = stoptheworld_tracer_ppid;
  if (fd != kInvalidFd) {
    // If the report file is already opened by the current process,
    // do nothing. Otherwise the report file was opened by the parent
    // process, close it now.
    if (fd_pid == pid)
      return;
    else
      CloseFile(fd);
  }

  const char *exe_name = GetProcessName();
  if (common_flags()->log_exe_name && exe_name) {
    internal_snprintf(full_path, kMaxPathLength, "%s.%s.%zu", path_prefix,
                      exe_name, pid);
  } else {
    internal_snprintf(full_path, kMaxPathLength, "%s.%zu", path_prefix, pid);
  }
  fd = OpenFile(full_path, WrOnly);
  if (fd == kInvalidFd) {
    const char *ErrorMsgPrefix = "ERROR: Can't open file: ";
    WriteToFile(kStderrFd, ErrorMsgPrefix, internal_strlen(ErrorMsgPrefix));
    WriteToFile(kStderrFd, full_path, internal_strlen(full_path));
    Die();
  }
  fd_pid = pid;
}

void ReportFile::SetReportPath(const char *path) {
  if (!path)
    return;
  uptr len = internal_strlen(path);
  if (len > sizeof(path_prefix) - 100) {
    Report("ERROR: Path is too long: %c%c%c%c%c%c%c%c...\n",
           path[0], path[1], path[2], path[3],
           path[4], path[5], path[6], path[7]);
    Die();
  }

  SpinMutexLock l(mu);
  if (fd != kStdoutFd && fd != kStderrFd && fd != kInvalidFd)
    CloseFile(fd);
  fd = kInvalidFd;
  if (internal_strcmp(path, "stdout") == 0) {
    fd = kStdoutFd;
  } else if (internal_strcmp(path, "stderr") == 0) {
    fd = kStderrFd;
  } else {
    internal_snprintf(path_prefix, kMaxPathLength, "%s", path);
  }
}

bool ReadFileToBuffer(const char *file_name, char **buff, uptr *buff_size,
                      uptr *read_len, uptr max_len, error_t *errno_p) {
  *buff = nullptr;
  *buff_size = 0;
  *read_len = 0;
  if (!max_len)
    return true;
  uptr PageSize = GetPageSizeCached();
  uptr kMinFileLen = Min(PageSize, max_len);

  // The files we usually open are not seekable, so try different buffer sizes.
  for (uptr size = kMinFileLen;; size = Min(size * 2, max_len)) {
    UnmapOrDie(*buff, *buff_size);
    *buff = (char*)MmapOrDie(size, __func__);
    *buff_size = size;
    fd_t fd = OpenFile(file_name, RdOnly, errno_p);
    if (fd == kInvalidFd) {
      UnmapOrDie(*buff, *buff_size);
      return false;
    }
    *read_len = 0;
    // Read up to one page at a time.
    bool reached_eof = false;
    while (*read_len < size) {
      uptr just_read;
      if (!ReadFromFile(fd, *buff + *read_len, size - *read_len, &just_read,
                        errno_p)) {
        UnmapOrDie(*buff, *buff_size);
        CloseFile(fd);
        return false;
      }
      *read_len += just_read;
      if (just_read == 0 || *read_len == max_len) {
        reached_eof = true;
        break;
      }
    }
    CloseFile(fd);
    if (reached_eof)  // We've read the whole file.
      break;
  }
  return true;
}

bool ReadFileToVector(const char *file_name,
                      InternalMmapVectorNoCtor<char> *buff, uptr max_len,
                      error_t *errno_p) {
  buff->clear();
  if (!max_len)
    return true;
  uptr PageSize = GetPageSizeCached();
  fd_t fd = OpenFile(file_name, RdOnly, errno_p);
  if (fd == kInvalidFd)
    return false;
  uptr read_len = 0;
  while (read_len < max_len) {
    if (read_len >= buff->size())
      buff->resize(Min(Max(PageSize, read_len * 2), max_len));
    CHECK_LT(read_len, buff->size());
    CHECK_LE(buff->size(), max_len);
    uptr just_read;
    if (!ReadFromFile(fd, buff->data() + read_len, buff->size() - read_len,
                      &just_read, errno_p)) {
      CloseFile(fd);
      return false;
    }
    read_len += just_read;
    if (!just_read)
      break;
  }
  CloseFile(fd);
  buff->resize(read_len);
  return true;
}

static const char kPathSeparator = SANITIZER_WINDOWS ? ';' : ':';

char *FindPathToBinary(const char *name) {
  if (FileExists(name)) {
    return internal_strdup(name);
  }

  const char *path = GetEnv("PATH");
  if (!path)
    return nullptr;
  uptr name_len = internal_strlen(name);
  InternalMmapVector<char> buffer(kMaxPathLength);
  const char *beg = path;
  while (true) {
    const char *end = internal_strchrnul(beg, kPathSeparator);
    uptr prefix_len = end - beg;
    if (prefix_len + name_len + 2 <= kMaxPathLength) {
      internal_memcpy(buffer.data(), beg, prefix_len);
      buffer[prefix_len] = '/';
      internal_memcpy(&buffer[prefix_len + 1], name, name_len);
      buffer[prefix_len + 1 + name_len] = '\0';
      if (FileExists(buffer.data()))
        return internal_strdup(buffer.data());
    }
    if (*end == '\0') break;
    beg = end + 1;
  }
  return nullptr;
}

} // namespace __sanitizer

using namespace __sanitizer;  // NOLINT

extern "C" {
void __sanitizer_set_report_path(const char *path) {
  report_file.SetReportPath(path);
}

void __sanitizer_set_report_fd(void *fd) {
  report_file.fd = (fd_t)reinterpret_cast<uptr>(fd);
  report_file.fd_pid = internal_getpid();
}
} // extern "C"

#endif  // !SANITIZER_FUCHSIA
