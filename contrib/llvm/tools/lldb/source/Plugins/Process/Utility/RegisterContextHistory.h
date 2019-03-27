//===-- RegisterContextHistory.h ----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_RegisterContextHistory_h_
#define lldb_RegisterContextHistory_h_

#include <vector>

#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class RegisterContextHistory : public lldb_private::RegisterContext {
public:
  typedef std::shared_ptr<RegisterContextHistory> SharedPtr;

  RegisterContextHistory(Thread &thread, uint32_t concrete_frame_idx,
                         uint32_t address_byte_size, lldb::addr_t pc_value);

  ~RegisterContextHistory() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t reg_set) override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

  bool ReadAllRegisterValues(lldb::DataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

private:
  //------------------------------------------------------------------
  // For RegisterContextLLDB only
  //------------------------------------------------------------------

  lldb_private::RegisterSet m_reg_set0; // register set 0 (PC only)
  lldb_private::RegisterInfo m_pc_reg_info;

  lldb::addr_t m_pc_value;

  DISALLOW_COPY_AND_ASSIGN(RegisterContextHistory);
};
} // namespace lldb_private

#endif // lldb_RegisterContextHistory_h_
