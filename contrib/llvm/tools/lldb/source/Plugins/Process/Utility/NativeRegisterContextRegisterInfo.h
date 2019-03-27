//===-- NativeRegisterContextRegisterInfo.h ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_PROCESS_UTIILTY_NATIVE_REGISTER_CONTEXT_REGISTER_INFO
#define LLDB_PLUGINS_PROCESS_UTIILTY_NATIVE_REGISTER_CONTEXT_REGISTER_INFO

#include <memory>

#include "RegisterInfoInterface.h"
#include "lldb/Host/common/NativeRegisterContext.h"

namespace lldb_private {
class NativeRegisterContextRegisterInfo : public NativeRegisterContext {
public:
  ///
  /// Construct a NativeRegisterContextRegisterInfo, taking ownership
  /// of the register_info_interface pointer.
  ///
  NativeRegisterContextRegisterInfo(
      NativeThreadProtocol &thread,
      RegisterInfoInterface *register_info_interface);

  uint32_t GetRegisterCount() const override;

  uint32_t GetUserRegisterCount() const override;

  const RegisterInfo *GetRegisterInfoAtIndex(uint32_t reg_index) const override;

  const RegisterInfoInterface &GetRegisterInfoInterface() const;

private:
  std::unique_ptr<RegisterInfoInterface> m_register_info_interface_up;
};
}
#endif
