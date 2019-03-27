//===-- EmulateInstructionMIPS64.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef EmulateInstructionMIPS64_h_
#define EmulateInstructionMIPS64_h_

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/Status.h"

namespace llvm {
class MCDisassembler;
class MCSubtargetInfo;
class MCRegisterInfo;
class MCAsmInfo;
class MCContext;
class MCInstrInfo;
class MCInst;
} // namespace llvm

class EmulateInstructionMIPS64 : public lldb_private::EmulateInstruction {
public:
  EmulateInstructionMIPS64(const lldb_private::ArchSpec &arch);

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb_private::EmulateInstruction *
  CreateInstance(const lldb_private::ArchSpec &arch,
                 lldb_private::InstructionType inst_type);

  static bool SupportsEmulatingInstructionsOfTypeStatic(
      lldb_private::InstructionType inst_type) {
    switch (inst_type) {
    case lldb_private::eInstructionTypeAny:
    case lldb_private::eInstructionTypePrologueEpilogue:
    case lldb_private::eInstructionTypePCModifying:
      return true;

    case lldb_private::eInstructionTypeAll:
      return false;
    }
    return false;
  }

  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override { return 1; }

  bool SetTargetTriple(const lldb_private::ArchSpec &arch) override;

  bool SupportsEmulatingInstructionsOfType(
      lldb_private::InstructionType inst_type) override {
    return SupportsEmulatingInstructionsOfTypeStatic(inst_type);
  }

  bool ReadInstruction() override;

  bool EvaluateInstruction(uint32_t evaluate_options) override;

  bool TestEmulation(lldb_private::Stream *out_stream,
                     lldb_private::ArchSpec &arch,
                     lldb_private::OptionValueDictionary *test_data) override {
    return false;
  }

  bool GetRegisterInfo(lldb::RegisterKind reg_kind, uint32_t reg_num,
                       lldb_private::RegisterInfo &reg_info) override;

  bool
  CreateFunctionEntryUnwind(lldb_private::UnwindPlan &unwind_plan) override;

protected:
  typedef struct {
    const char *op_name;
    bool (EmulateInstructionMIPS64::*callback)(llvm::MCInst &insn);
    const char *insn_name;
  } MipsOpcode;

  static MipsOpcode *GetOpcodeForInstruction(const char *op_name);

  bool Emulate_DADDiu(llvm::MCInst &insn);

  bool Emulate_DSUBU_DADDU(llvm::MCInst &insn);

  bool Emulate_LUI(llvm::MCInst &insn);

  bool Emulate_SD(llvm::MCInst &insn);

  bool Emulate_LD(llvm::MCInst &insn);

  bool Emulate_LDST_Imm(llvm::MCInst &insn);

  bool Emulate_LDST_Reg(llvm::MCInst &insn);

  bool Emulate_BXX_3ops(llvm::MCInst &insn);

  bool Emulate_BXX_3ops_C(llvm::MCInst &insn);

  bool Emulate_BXX_2ops(llvm::MCInst &insn);

  bool Emulate_BXX_2ops_C(llvm::MCInst &insn);

  bool Emulate_Bcond_Link_C(llvm::MCInst &insn);

  bool Emulate_Bcond_Link(llvm::MCInst &insn);

  bool Emulate_FP_branch(llvm::MCInst &insn);

  bool Emulate_3D_branch(llvm::MCInst &insn);

  bool Emulate_BAL(llvm::MCInst &insn);

  bool Emulate_BALC(llvm::MCInst &insn);

  bool Emulate_BC(llvm::MCInst &insn);

  bool Emulate_J(llvm::MCInst &insn);

  bool Emulate_JAL(llvm::MCInst &insn);

  bool Emulate_JALR(llvm::MCInst &insn);

  bool Emulate_JIALC(llvm::MCInst &insn);

  bool Emulate_JIC(llvm::MCInst &insn);

  bool Emulate_JR(llvm::MCInst &insn);

  bool Emulate_BC1EQZ(llvm::MCInst &insn);

  bool Emulate_BC1NEZ(llvm::MCInst &insn);

  bool Emulate_BNZB(llvm::MCInst &insn);

  bool Emulate_BNZH(llvm::MCInst &insn);

  bool Emulate_BNZW(llvm::MCInst &insn);

  bool Emulate_BNZD(llvm::MCInst &insn);

  bool Emulate_BZB(llvm::MCInst &insn);

  bool Emulate_BZH(llvm::MCInst &insn);

  bool Emulate_BZW(llvm::MCInst &insn);

  bool Emulate_BZD(llvm::MCInst &insn);

  bool Emulate_MSA_Branch_DF(llvm::MCInst &insn, int element_byte_size,
                             bool bnz);

  bool Emulate_BNZV(llvm::MCInst &insn);

  bool Emulate_BZV(llvm::MCInst &insn);

  bool Emulate_MSA_Branch_V(llvm::MCInst &insn, bool bnz);

  bool nonvolatile_reg_p(uint64_t regnum);

  const char *GetRegisterName(unsigned reg_num, bool altnernate_name);

private:
  std::unique_ptr<llvm::MCDisassembler> m_disasm;
  std::unique_ptr<llvm::MCSubtargetInfo> m_subtype_info;
  std::unique_ptr<llvm::MCRegisterInfo> m_reg_info;
  std::unique_ptr<llvm::MCAsmInfo> m_asm_info;
  std::unique_ptr<llvm::MCContext> m_context;
  std::unique_ptr<llvm::MCInstrInfo> m_insn_info;
};

#endif // EmulateInstructionMIPS64_h_
