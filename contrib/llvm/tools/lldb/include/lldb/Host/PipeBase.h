//===-- PipeBase.h -----------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_PipeBase_h_
#define liblldb_Host_PipeBase_h_

#include <chrono>
#include <string>

#include "lldb/Utility/Status.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace lldb_private {
class PipeBase {
public:
  virtual ~PipeBase();

  virtual Status CreateNew(bool child_process_inherit) = 0;
  virtual Status CreateNew(llvm::StringRef name,
                           bool child_process_inherit) = 0;
  virtual Status CreateWithUniqueName(llvm::StringRef prefix,
                                      bool child_process_inherit,
                                      llvm::SmallVectorImpl<char> &name) = 0;

  virtual Status OpenAsReader(llvm::StringRef name,
                              bool child_process_inherit) = 0;

  Status OpenAsWriter(llvm::StringRef name, bool child_process_inherit);
  virtual Status
  OpenAsWriterWithTimeout(llvm::StringRef name, bool child_process_inherit,
                          const std::chrono::microseconds &timeout) = 0;

  virtual bool CanRead() const = 0;
  virtual bool CanWrite() const = 0;

  virtual lldb::pipe_t GetReadPipe() const = 0;
  virtual lldb::pipe_t GetWritePipe() const = 0;

  virtual int GetReadFileDescriptor() const = 0;
  virtual int GetWriteFileDescriptor() const = 0;
  virtual int ReleaseReadFileDescriptor() = 0;
  virtual int ReleaseWriteFileDescriptor() = 0;
  virtual void CloseReadFileDescriptor() = 0;
  virtual void CloseWriteFileDescriptor() = 0;

  // Close both descriptors
  virtual void Close() = 0;

  // Delete named pipe.
  virtual Status Delete(llvm::StringRef name) = 0;

  virtual Status Write(const void *buf, size_t size, size_t &bytes_written) = 0;
  virtual Status ReadWithTimeout(void *buf, size_t size,
                                 const std::chrono::microseconds &timeout,
                                 size_t &bytes_read) = 0;
  Status Read(void *buf, size_t size, size_t &bytes_read);
};
}

#endif
