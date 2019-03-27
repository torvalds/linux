//===-- NativeRegisterContextNetBSD.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextNetBSD_h
#define lldb_NativeRegisterContextNetBSD_h

#include "lldb/Host/common/NativeThreadProtocol.h"

#include "Plugins/Process/NetBSD/NativeProcessNetBSD.h"
#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

namespace lldb_private {
namespace process_netbsd {

class NativeRegisterContextNetBSD : public NativeRegisterContextRegisterInfo {
public:
  NativeRegisterContextNetBSD(NativeThreadProtocol &native_thread,
                              RegisterInfoInterface *reg_info_interface_p);

  // This function is implemented in the NativeRegisterContextNetBSD_*
  // subclasses to create a new instance of the host specific
  // NativeRegisterContextNetBSD. The implementations can't collide as only one
  // NativeRegisterContextNetBSD_* variant should be compiled into the final
  // executable.
  static NativeRegisterContextNetBSD *
  CreateHostNativeRegisterContextNetBSD(const ArchSpec &target_arch,
                                        NativeThreadProtocol &native_thread);

protected:
  virtual Status ReadGPR();
  virtual Status WriteGPR();

  virtual Status ReadFPR();
  virtual Status WriteFPR();

  virtual Status ReadDBR();
  virtual Status WriteDBR();

  virtual void *GetGPRBuffer() { return nullptr; }
  virtual size_t GetGPRSize() {
    return GetRegisterInfoInterface().GetGPRSize();
  }

  virtual void *GetFPRBuffer() { return nullptr; }
  virtual size_t GetFPRSize() { return 0; }

  virtual void *GetDBRBuffer() { return nullptr; }
  virtual size_t GetDBRSize() { return 0; }

  virtual Status DoReadGPR(void *buf);
  virtual Status DoWriteGPR(void *buf);

  virtual Status DoReadFPR(void *buf);
  virtual Status DoWriteFPR(void *buf);

  virtual Status DoReadDBR(void *buf);
  virtual Status DoWriteDBR(void *buf);

  virtual NativeProcessNetBSD &GetProcess();
  virtual ::pid_t GetProcessPid();
};

} // namespace process_netbsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextNetBSD_h
