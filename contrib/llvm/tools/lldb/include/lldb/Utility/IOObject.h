//===-- IOObject.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_Common_IOObject_h_
#define liblldb_Host_Common_IOObject_h_

#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>

#include "lldb/lldb-private.h"

namespace lldb_private {

class IOObject {
public:
  typedef enum {
    eFDTypeFile,   // Other FD requiring read/write
    eFDTypeSocket, // Socket requiring send/recv
  } FDType;

  // TODO: On Windows this should be a HANDLE, and wait should use
  // WaitForMultipleObjects
  typedef int WaitableHandle;
  static const WaitableHandle kInvalidHandleValue;

  IOObject(FDType type, bool should_close)
      : m_fd_type(type), m_should_close_fd(should_close) {}
  virtual ~IOObject();

  virtual Status Read(void *buf, size_t &num_bytes) = 0;
  virtual Status Write(const void *buf, size_t &num_bytes) = 0;
  virtual bool IsValid() const = 0;
  virtual Status Close() = 0;

  FDType GetFdType() const { return m_fd_type; }

  virtual WaitableHandle GetWaitableHandle() = 0;

protected:
  FDType m_fd_type;
  bool m_should_close_fd; // True if this class should close the file descriptor
                          // when it goes away.

private:
  DISALLOW_COPY_AND_ASSIGN(IOObject);
};
} // namespace lldb_private

#endif
