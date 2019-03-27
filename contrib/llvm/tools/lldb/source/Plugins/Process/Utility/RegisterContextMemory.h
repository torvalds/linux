//===-- RegisterContextMemory.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_RegisterContextMemory_h_
#define lldb_RegisterContextMemory_h_

#include <vector>

#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/lldb-private.h"

class DynamicRegisterInfo;

class RegisterContextMemory : public lldb_private::RegisterContext {
public:
  RegisterContextMemory(lldb_private::Thread &thread,
                        uint32_t concrete_frame_idx,
                        DynamicRegisterInfo &reg_info,
                        lldb::addr_t reg_data_addr);

  ~RegisterContextMemory() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t reg_set) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

  //------------------------------------------------------------------
  // If all of the thread register are in a contiguous buffer in
  // memory, then the default ReadRegister/WriteRegister and
  // ReadAllRegisterValues/WriteAllRegisterValues will work. If thread
  // registers are not contiguous, clients will want to subclass this
  // class and modify the read/write functions as needed.
  //------------------------------------------------------------------

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &reg_value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &reg_value) override;

  bool ReadAllRegisterValues(lldb::DataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  void SetAllRegisterData(const lldb::DataBufferSP &data_sp);

protected:
  void SetAllRegisterValid(bool b);

  DynamicRegisterInfo &m_reg_infos;
  std::vector<bool> m_reg_valid;
  lldb_private::DataExtractor m_reg_data;
  lldb::addr_t m_reg_data_addr; // If this is valid, then we have a register
                                // context that is stored in memmory

private:
  DISALLOW_COPY_AND_ASSIGN(RegisterContextMemory);
};

#endif // lldb_RegisterContextMemory_h_
