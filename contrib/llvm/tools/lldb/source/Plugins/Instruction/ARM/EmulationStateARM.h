//===-- lldb_EmulationStateARM.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_EmulationStateARM_h_
#define lldb_EmulationStateARM_h_

#include <map>

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Core/Opcode.h"

class EmulationStateARM {
public:
  EmulationStateARM();

  virtual ~EmulationStateARM();

  bool StorePseudoRegisterValue(uint32_t reg_num, uint64_t value);

  uint64_t ReadPseudoRegisterValue(uint32_t reg_num, bool &success);

  bool StoreToPseudoAddress(lldb::addr_t p_address, uint32_t value);

  uint32_t ReadFromPseudoAddress(lldb::addr_t p_address, bool &success);

  void ClearPseudoRegisters();

  void ClearPseudoMemory();

  bool LoadPseudoRegistersFromFrame(lldb_private::StackFrame &frame);

  bool LoadStateFromDictionary(lldb_private::OptionValueDictionary *test_data);

  bool CompareState(EmulationStateARM &other_state);

  static size_t
  ReadPseudoMemory(lldb_private::EmulateInstruction *instruction, void *baton,
                   const lldb_private::EmulateInstruction::Context &context,
                   lldb::addr_t addr, void *dst, size_t length);

  static size_t
  WritePseudoMemory(lldb_private::EmulateInstruction *instruction, void *baton,
                    const lldb_private::EmulateInstruction::Context &context,
                    lldb::addr_t addr, const void *dst, size_t length);

  static bool ReadPseudoRegister(lldb_private::EmulateInstruction *instruction,
                                 void *baton,
                                 const lldb_private::RegisterInfo *reg_info,
                                 lldb_private::RegisterValue &reg_value);

  static bool
  WritePseudoRegister(lldb_private::EmulateInstruction *instruction,
                      void *baton,
                      const lldb_private::EmulateInstruction::Context &context,
                      const lldb_private::RegisterInfo *reg_info,
                      const lldb_private::RegisterValue &reg_value);

private:
  uint32_t m_gpr[17];
  struct _sd_regs {
    uint32_t s_regs[32]; // sregs 0 - 31 & dregs 0 - 15

    uint64_t d_regs[16]; // dregs 16-31

  } m_vfp_regs;

  std::map<lldb::addr_t, uint32_t> m_memory; // Eventually will want to change
                                             // uint32_t to a data buffer heap
                                             // type.

  DISALLOW_COPY_AND_ASSIGN(EmulationStateARM);
};

#endif // lldb_EmulationStateARM_h_
