//===-- SBCommandReturnObject.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBCommandReturnObject_h_
#define LLDB_SBCommandReturnObject_h_

#include <stdio.h>

#include <memory>

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBCommandReturnObject {
public:
  SBCommandReturnObject();

  SBCommandReturnObject(const lldb::SBCommandReturnObject &rhs);

  ~SBCommandReturnObject();

  const lldb::SBCommandReturnObject &
  operator=(const lldb::SBCommandReturnObject &rhs);

  SBCommandReturnObject(lldb_private::CommandReturnObject *ptr);

  lldb_private::CommandReturnObject *Release();

  bool IsValid() const;

  const char *GetOutput();

  const char *GetError();

  size_t PutOutput(FILE *fh);

  size_t GetOutputSize();

  size_t GetErrorSize();

  size_t PutError(FILE *fh);

  void Clear();

  lldb::ReturnStatus GetStatus();

  void SetStatus(lldb::ReturnStatus status);

  bool Succeeded();

  bool HasResult();

  void AppendMessage(const char *message);

  void AppendWarning(const char *message);

  bool GetDescription(lldb::SBStream &description);

  // deprecated, these two functions do not take ownership of file handle
  void SetImmediateOutputFile(FILE *fh);

  void SetImmediateErrorFile(FILE *fh);

  void SetImmediateOutputFile(FILE *fh, bool transfer_ownership);

  void SetImmediateErrorFile(FILE *fh, bool transfer_ownership);

  void PutCString(const char *string, int len = -1);

  size_t Printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  const char *GetOutput(bool only_if_no_immediate);

  const char *GetError(bool only_if_no_immediate);

  void SetError(lldb::SBError &error,
                const char *fallback_error_cstr = nullptr);

  void SetError(const char *error_cstr);

protected:
  friend class SBCommandInterpreter;
  friend class SBOptions;

  lldb_private::CommandReturnObject *operator->() const;

  lldb_private::CommandReturnObject *get() const;

  lldb_private::CommandReturnObject &operator*() const;

  lldb_private::CommandReturnObject &ref() const;

  void SetLLDBObjectPtr(lldb_private::CommandReturnObject *ptr);

private:
  std::unique_ptr<lldb_private::CommandReturnObject> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBCommandReturnObject_h_
