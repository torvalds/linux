//===-- SBError.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBError_h_
#define LLDB_SBError_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBError {
public:
  SBError();

  SBError(const lldb::SBError &rhs);

  ~SBError();

  const SBError &operator=(const lldb::SBError &rhs);

  const char *GetCString() const;

  void Clear();

  bool Fail() const;

  bool Success() const;

  uint32_t GetError() const;

  lldb::ErrorType GetType() const;

  void SetError(uint32_t err, lldb::ErrorType type);

  void SetErrorToErrno();

  void SetErrorToGenericError();

  void SetErrorString(const char *err_str);

  int SetErrorStringWithFormat(const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  bool IsValid() const;

  bool GetDescription(lldb::SBStream &description);

protected:
  friend class SBCommandReturnObject;
  friend class SBData;
  friend class SBDebugger;
  friend class SBCommunication;
  friend class SBHostOS;
  friend class SBPlatform;
  friend class SBProcess;
  friend class SBStructuredData;
  friend class SBThread;
  friend class SBTrace;
  friend class SBTarget;
  friend class SBValue;
  friend class SBWatchpoint;
  friend class SBBreakpoint;
  friend class SBBreakpointLocation;
  friend class SBBreakpointName;

  lldb_private::Status *get();

  lldb_private::Status *operator->();

  const lldb_private::Status &operator*() const;

  lldb_private::Status &ref();

  void SetError(const lldb_private::Status &lldb_error);

private:
  std::unique_ptr<lldb_private::Status> m_opaque_ap;

  void CreateIfNeeded();
};

} // namespace lldb

#endif // LLDB_SBError_h_
