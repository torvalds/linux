//===-- UnwindAssemblyInstEmulation.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_UnwindAssemblyInstEmulation_h_
#define liblldb_UnwindAssemblyInstEmulation_h_

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/UnwindAssembly.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-private.h"

class UnwindAssemblyInstEmulation : public lldb_private::UnwindAssembly {
public:
  ~UnwindAssemblyInstEmulation() override = default;

  bool GetNonCallSiteUnwindPlanFromAssembly(
      lldb_private::AddressRange &func, lldb_private::Thread &thread,
      lldb_private::UnwindPlan &unwind_plan) override;

  bool
  GetNonCallSiteUnwindPlanFromAssembly(lldb_private::AddressRange &func,
                                       uint8_t *opcode_data, size_t opcode_size,
                                       lldb_private::UnwindPlan &unwind_plan);

  bool
  AugmentUnwindPlanFromCallSite(lldb_private::AddressRange &func,
                                lldb_private::Thread &thread,
                                lldb_private::UnwindPlan &unwind_plan) override;

  bool GetFastUnwindPlan(lldb_private::AddressRange &func,
                         lldb_private::Thread &thread,
                         lldb_private::UnwindPlan &unwind_plan) override;

  // thread may be NULL in which case we only use the Target (e.g. if this is
  // called pre-process-launch).
  bool
  FirstNonPrologueInsn(lldb_private::AddressRange &func,
                       const lldb_private::ExecutionContext &exe_ctx,
                       lldb_private::Address &first_non_prologue_insn) override;

  static lldb_private::UnwindAssembly *
  CreateInstance(const lldb_private::ArchSpec &arch);

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

private:
  // Call CreateInstance to get an instance of this class
  UnwindAssemblyInstEmulation(const lldb_private::ArchSpec &arch,
                              lldb_private::EmulateInstruction *inst_emulator)
      : UnwindAssembly(arch), m_inst_emulator_ap(inst_emulator),
        m_range_ptr(NULL), m_unwind_plan_ptr(NULL), m_curr_row(),
        m_cfa_reg_info(), m_fp_is_cfa(false), m_register_values(),
        m_pushed_regs(), m_curr_row_modified(false),
        m_forward_branch_offset(0) {
    if (m_inst_emulator_ap.get()) {
      m_inst_emulator_ap->SetBaton(this);
      m_inst_emulator_ap->SetCallbacks(ReadMemory, WriteMemory, ReadRegister,
                                       WriteRegister);
    }
  }

  static size_t
  ReadMemory(lldb_private::EmulateInstruction *instruction, void *baton,
             const lldb_private::EmulateInstruction::Context &context,
             lldb::addr_t addr, void *dst, size_t length);

  static size_t
  WriteMemory(lldb_private::EmulateInstruction *instruction, void *baton,
              const lldb_private::EmulateInstruction::Context &context,
              lldb::addr_t addr, const void *dst, size_t length);

  static bool ReadRegister(lldb_private::EmulateInstruction *instruction,
                           void *baton,
                           const lldb_private::RegisterInfo *reg_info,
                           lldb_private::RegisterValue &reg_value);

  static bool
  WriteRegister(lldb_private::EmulateInstruction *instruction, void *baton,
                const lldb_private::EmulateInstruction::Context &context,
                const lldb_private::RegisterInfo *reg_info,
                const lldb_private::RegisterValue &reg_value);

  //    size_t
  //    ReadMemory (lldb_private::EmulateInstruction *instruction,
  //                const lldb_private::EmulateInstruction::Context &context,
  //                lldb::addr_t addr,
  //                void *dst,
  //                size_t length);

  size_t WriteMemory(lldb_private::EmulateInstruction *instruction,
                     const lldb_private::EmulateInstruction::Context &context,
                     lldb::addr_t addr, const void *dst, size_t length);

  bool ReadRegister(lldb_private::EmulateInstruction *instruction,
                    const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &reg_value);

  bool WriteRegister(lldb_private::EmulateInstruction *instruction,
                     const lldb_private::EmulateInstruction::Context &context,
                     const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &reg_value);

  static uint64_t
  MakeRegisterKindValuePair(const lldb_private::RegisterInfo &reg_info);

  void SetRegisterValue(const lldb_private::RegisterInfo &reg_info,
                        const lldb_private::RegisterValue &reg_value);

  bool GetRegisterValue(const lldb_private::RegisterInfo &reg_info,
                        lldb_private::RegisterValue &reg_value);

  std::unique_ptr<lldb_private::EmulateInstruction> m_inst_emulator_ap;
  lldb_private::AddressRange *m_range_ptr;
  lldb_private::UnwindPlan *m_unwind_plan_ptr;
  lldb_private::UnwindPlan::RowSP m_curr_row;
  typedef std::map<uint64_t, uint64_t> PushedRegisterToAddrMap;
  uint64_t m_initial_sp;
  lldb_private::RegisterInfo m_cfa_reg_info;
  bool m_fp_is_cfa;
  typedef std::map<uint64_t, lldb_private::RegisterValue> RegisterValueMap;
  RegisterValueMap m_register_values;
  PushedRegisterToAddrMap m_pushed_regs;

  // While processing the instruction stream, we need to communicate some state
  // change
  // information up to the higher level loop that makes decisions about how to
  // push
  // the unwind instructions for the UnwindPlan we're constructing.

  // The instruction we're processing updated the UnwindPlan::Row contents
  bool m_curr_row_modified;
  // The instruction is branching forward with the given offset. 0 value means
  // no branching.
  uint32_t m_forward_branch_offset;
};

#endif // liblldb_UnwindAssemblyInstEmulation_h_
