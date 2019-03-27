//===-- ArchitectureArm.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/Architecture/Arm/ArchitectureArm.h"
#include "Plugins/Process/Utility/ARMDefines.h"
#include "Plugins/Process/Utility/InstructionUtils.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ArchSpec.h"

using namespace lldb_private;
using namespace lldb;

ConstString ArchitectureArm::GetPluginNameStatic() {
  return ConstString("arm");
}

void ArchitectureArm::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Arm-specific algorithms",
                                &ArchitectureArm::Create);
}

void ArchitectureArm::Terminate() {
  PluginManager::UnregisterPlugin(&ArchitectureArm::Create);
}

std::unique_ptr<Architecture> ArchitectureArm::Create(const ArchSpec &arch) {
  if (arch.GetMachine() != llvm::Triple::arm)
    return nullptr;
  return std::unique_ptr<Architecture>(new ArchitectureArm());
}

ConstString ArchitectureArm::GetPluginName() { return GetPluginNameStatic(); }
uint32_t ArchitectureArm::GetPluginVersion() { return 1; }

void ArchitectureArm::OverrideStopInfo(Thread &thread) const {
  // We need to check if we are stopped in Thumb mode in a IT instruction and
  // detect if the condition doesn't pass. If this is the case it means we
  // won't actually execute this instruction. If this happens we need to clear
  // the stop reason to no thread plans think we are stopped for a reason and
  // the plans should keep going.
  //
  // We do this because when single stepping many ARM processes, debuggers
  // often use the BVR/BCR registers that says "stop when the PC is not equal
  // to its current value". This method of stepping means we can end up
  // stopping on instructions inside an if/then block that wouldn't get
  // executed. By fixing this we can stop the debugger from seeming like you
  // stepped through both the "if" _and_ the "else" clause when source level
  // stepping because the debugger stops regardless due to the BVR/BCR
  // triggering a stop.
  //
  // It also means we can set breakpoints on instructions inside an an if/then
  // block and correctly skip them if we use the BKPT instruction. The ARM and
  // Thumb BKPT instructions are unconditional even when executed in a Thumb IT
  // block.
  //
  // If your debugger inserts software traps in ARM/Thumb code, it will need to
  // use 16 and 32 bit instruction for 16 and 32 bit thumb instructions
  // respectively. If your debugger inserts a 16 bit thumb trap on top of a 32
  // bit thumb instruction for an opcode that is inside an if/then, it will
  // change the it/then to conditionally execute your
  // 16 bit trap and then cause your program to crash if it executes the
  // trailing 16 bits (the second half of the 32 bit thumb instruction you
  // partially overwrote).

  RegisterContextSP reg_ctx_sp(thread.GetRegisterContext());
  if (!reg_ctx_sp)
    return;

  const uint32_t cpsr = reg_ctx_sp->GetFlags(0);
  if (cpsr == 0)
    return;

  // Read the J and T bits to get the ISETSTATE
  const uint32_t J = Bit32(cpsr, 24);
  const uint32_t T = Bit32(cpsr, 5);
  const uint32_t ISETSTATE = J << 1 | T;
  if (ISETSTATE == 0) {
// NOTE: I am pretty sure we want to enable the code below
// that detects when we stop on an instruction in ARM mode that is conditional
// and the condition doesn't pass. This can happen if you set a breakpoint on
// an instruction that is conditional. We currently will _always_ stop on the
// instruction which is bad. You can also run into this while single stepping
// and you could appear to run code in the "if" and in the "else" clause
// because it would stop at all of the conditional instructions in both. In
// such cases, we really don't want to stop at this location.
// I will check with the lldb-dev list first before I enable this.
#if 0
    // ARM mode: check for condition on instruction
    const addr_t pc = reg_ctx_sp->GetPC();
    Status error;
    // If we fail to read the opcode we will get UINT64_MAX as the result in
    // "opcode" which we can use to detect if we read a valid opcode.
    const uint64_t opcode = thread.GetProcess()->ReadUnsignedIntegerFromMemory(pc, 4, UINT64_MAX, error);
    if (opcode <= UINT32_MAX)
    {
        const uint32_t condition = Bits32((uint32_t)opcode, 31, 28);
        if (!ARMConditionPassed(condition, cpsr))
        {
            // We ARE stopped on an ARM instruction whose condition doesn't
            // pass so this instruction won't get executed. Regardless of why
            // it stopped, we need to clear the stop info
            thread.SetStopInfo (StopInfoSP());
        }
    }
#endif
  } else if (ISETSTATE == 1) {
    // Thumb mode
    const uint32_t ITSTATE = Bits32(cpsr, 15, 10) << 2 | Bits32(cpsr, 26, 25);
    if (ITSTATE != 0) {
      const uint32_t condition = Bits32(ITSTATE, 7, 4);
      if (!ARMConditionPassed(condition, cpsr)) {
        // We ARE stopped in a Thumb IT instruction on an instruction whose
        // condition doesn't pass so this instruction won't get executed.
        // Regardless of why it stopped, we need to clear the stop info
        thread.SetStopInfo(StopInfoSP());
      }
    }
  }
}

addr_t ArchitectureArm::GetCallableLoadAddress(addr_t code_addr,
                                               AddressClass addr_class) const {
  bool is_alternate_isa = false;

  switch (addr_class) {
  case AddressClass::eData:
  case AddressClass::eDebug:
    return LLDB_INVALID_ADDRESS;
  case AddressClass::eCodeAlternateISA:
    is_alternate_isa = true;
    break;
  default: break;
  }

  if ((code_addr & 2u) || is_alternate_isa)
    return code_addr | 1u;
  return code_addr;
}

addr_t ArchitectureArm::GetOpcodeLoadAddress(addr_t opcode_addr,
                                             AddressClass addr_class) const {
  switch (addr_class) {
  case AddressClass::eData:
  case AddressClass::eDebug:
    return LLDB_INVALID_ADDRESS;
  default: break;
  }
  return opcode_addr & ~(1ull);
}
