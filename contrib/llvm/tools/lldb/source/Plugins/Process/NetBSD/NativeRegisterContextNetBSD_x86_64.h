//===-- NativeRegisterContextNetBSD_x86_64.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if defined(__x86_64__)

#ifndef lldb_NativeRegisterContextNetBSD_x86_64_h
#define lldb_NativeRegisterContextNetBSD_x86_64_h

// clang-format off
#include <sys/param.h>
#include <sys/types.h>
#include <machine/reg.h>
// clang-format on

#include "Plugins/Process/NetBSD/NativeRegisterContextNetBSD.h"
#include "Plugins/Process/Utility/RegisterContext_x86.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

namespace lldb_private {
namespace process_netbsd {

class NativeProcessNetBSD;

class NativeRegisterContextNetBSD_x86_64 : public NativeRegisterContextNetBSD {
public:
  NativeRegisterContextNetBSD_x86_64(const ArchSpec &target_arch,
                                     NativeThreadProtocol &native_thread);
  uint32_t GetRegisterSetCount() const override;

  const RegisterSet *GetRegisterSet(uint32_t set_index) const override;

  Status ReadRegister(const RegisterInfo *reg_info,
                      RegisterValue &reg_value) override;

  Status WriteRegister(const RegisterInfo *reg_info,
                       const RegisterValue &reg_value) override;

  Status ReadAllRegisterValues(lldb::DataBufferSP &data_sp) override;

  Status WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  Status IsWatchpointHit(uint32_t wp_index, bool &is_hit) override;

  Status GetWatchpointHitIndex(uint32_t &wp_index,
                               lldb::addr_t trap_addr) override;

  Status IsWatchpointVacant(uint32_t wp_index, bool &is_vacant) override;

  bool ClearHardwareWatchpoint(uint32_t wp_index) override;

  Status ClearAllHardwareWatchpoints() override;

  Status SetHardwareWatchpointWithIndex(lldb::addr_t addr, size_t size,
                                        uint32_t watch_flags,
                                        uint32_t wp_index);

  uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                 uint32_t watch_flags) override;

  lldb::addr_t GetWatchpointAddress(uint32_t wp_index) override;

  uint32_t NumSupportedHardwareWatchpoints() override;

protected:
  void *GetGPRBuffer() override { return &m_gpr_x86_64; }
  void *GetFPRBuffer() override { return &m_fpr_x86_64; }
  void *GetDBRBuffer() override { return &m_dbr_x86_64; }

private:
  // Private member types.
  enum { GPRegSet, FPRegSet, DBRegSet };

  // Private member variables.
  struct reg m_gpr_x86_64;
  struct fpreg m_fpr_x86_64;
  struct dbreg m_dbr_x86_64;

  int GetSetForNativeRegNum(int reg_num) const;

  int ReadRegisterSet(uint32_t set);
  int WriteRegisterSet(uint32_t set);
};

} // namespace process_netbsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextNetBSD_x86_64_h

#endif // defined(__x86_64__)
