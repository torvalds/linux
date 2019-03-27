//===-- GDBRemoteRegisterContext.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_GDBRemoteRegisterContext_h_
#define lldb_GDBRemoteRegisterContext_h_

#include <vector>

#include "Plugins/Process/Utility/DynamicRegisterInfo.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "GDBRemoteCommunicationClient.h"

class StringExtractor;

namespace lldb_private {
namespace process_gdb_remote {

class ThreadGDBRemote;
class ProcessGDBRemote;

class GDBRemoteDynamicRegisterInfo : public DynamicRegisterInfo {
public:
  GDBRemoteDynamicRegisterInfo() : DynamicRegisterInfo() {}

  ~GDBRemoteDynamicRegisterInfo() override = default;

  void HardcodeARMRegisters(bool from_scratch);
};

class GDBRemoteRegisterContext : public RegisterContext {
public:
  GDBRemoteRegisterContext(ThreadGDBRemote &thread, uint32_t concrete_frame_idx,
                           GDBRemoteDynamicRegisterInfo &reg_info,
                           bool read_all_at_once);

  ~GDBRemoteRegisterContext() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const RegisterSet *GetRegisterSet(size_t reg_set) override;

  bool ReadRegister(const RegisterInfo *reg_info,
                    RegisterValue &value) override;

  bool WriteRegister(const RegisterInfo *reg_info,
                     const RegisterValue &value) override;

  bool ReadAllRegisterValues(lldb::DataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  bool ReadAllRegisterValues(RegisterCheckpoint &reg_checkpoint) override;

  bool
  WriteAllRegisterValues(const RegisterCheckpoint &reg_checkpoint) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

protected:
  friend class ThreadGDBRemote;

  bool ReadRegisterBytes(const RegisterInfo *reg_info, DataExtractor &data);

  bool WriteRegisterBytes(const RegisterInfo *reg_info, DataExtractor &data,
                          uint32_t data_offset);

  bool PrivateSetRegisterValue(uint32_t reg, llvm::ArrayRef<uint8_t> data);

  bool PrivateSetRegisterValue(uint32_t reg, uint64_t val);

  void SetAllRegisterValid(bool b);

  bool GetRegisterIsValid(uint32_t reg) const {
#if defined(LLDB_CONFIGURATION_DEBUG)
    assert(reg < m_reg_valid.size());
#endif
    if (reg < m_reg_valid.size())
      return m_reg_valid[reg];
    return false;
  }

  void SetRegisterIsValid(const RegisterInfo *reg_info, bool valid) {
    if (reg_info)
      return SetRegisterIsValid(reg_info->kinds[lldb::eRegisterKindLLDB],
                                valid);
  }

  void SetRegisterIsValid(uint32_t reg, bool valid) {
#if defined(LLDB_CONFIGURATION_DEBUG)
    assert(reg < m_reg_valid.size());
#endif
    if (reg < m_reg_valid.size())
      m_reg_valid[reg] = valid;
  }

  GDBRemoteDynamicRegisterInfo &m_reg_info;
  std::vector<bool> m_reg_valid;
  DataExtractor m_reg_data;
  bool m_read_all_at_once;

private:
  // Helper function for ReadRegisterBytes().
  bool GetPrimordialRegister(const RegisterInfo *reg_info,
                             GDBRemoteCommunicationClient &gdb_comm);
  // Helper function for WriteRegisterBytes().
  bool SetPrimordialRegister(const RegisterInfo *reg_info,
                             GDBRemoteCommunicationClient &gdb_comm);

  DISALLOW_COPY_AND_ASSIGN(GDBRemoteRegisterContext);
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // lldb_GDBRemoteRegisterContext_h_
