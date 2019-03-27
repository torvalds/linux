//===-- EmulateInstructionPPC64.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef EmulateInstructionPPC64_h_
#define EmulateInstructionPPC64_h_

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/Log.h"

namespace lldb_private {

class EmulateInstructionPPC64 : public EmulateInstruction {
public:
  EmulateInstructionPPC64(const ArchSpec &arch);

  static void Initialize();

  static void Terminate();

  static ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static EmulateInstruction *CreateInstance(const ArchSpec &arch,
                                            InstructionType inst_type);

  static bool
  SupportsEmulatingInstructionsOfTypeStatic(InstructionType inst_type) {
    switch (inst_type) {
    case eInstructionTypeAny:
    case eInstructionTypePrologueEpilogue:
      return true;

    case eInstructionTypePCModifying:
    case eInstructionTypeAll:
      return false;
    }
    return false;
  }

  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override { return 1; }

  bool SetTargetTriple(const ArchSpec &arch) override;

  bool SupportsEmulatingInstructionsOfType(InstructionType inst_type) override {
    return SupportsEmulatingInstructionsOfTypeStatic(inst_type);
  }

  bool ReadInstruction() override;

  bool EvaluateInstruction(uint32_t evaluate_options) override;

  bool TestEmulation(Stream *out_stream, ArchSpec &arch,
                     OptionValueDictionary *test_data) override {
    return false;
  }

  bool GetRegisterInfo(lldb::RegisterKind reg_kind, uint32_t reg_num,
                       RegisterInfo &reg_info) override;

  bool CreateFunctionEntryUnwind(UnwindPlan &unwind_plan) override;

private:
  struct Opcode {
    uint32_t mask;
    uint32_t value;
    bool (EmulateInstructionPPC64::*callback)(uint32_t opcode);
    const char *name;
  };

  uint32_t m_fp = LLDB_INVALID_REGNUM;

  Opcode *GetOpcodeForInstruction(uint32_t opcode);

  bool EmulateMFSPR(uint32_t opcode);
  bool EmulateLD(uint32_t opcode);
  bool EmulateSTD(uint32_t opcode);
  bool EmulateOR(uint32_t opcode);
  bool EmulateADDI(uint32_t opcode);
};

} // namespace lldb_private

#endif // EmulateInstructionPPC64_h_
