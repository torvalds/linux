//===------- ShadowCallStack.cpp - Shadow Call Stack pass -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The ShadowCallStack pass instruments function prologs/epilogs to check that
// the return address has not been corrupted during the execution of the
// function. The return address is stored in a 'shadow call stack' addressed
// using the %gs segment register.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class ShadowCallStack : public MachineFunctionPass {
public:
  static char ID;

  ShadowCallStack() : MachineFunctionPass(ID) {
    initializeShadowCallStackPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

private:
  // Do not instrument leaf functions with this many or fewer instructions. The
  // shadow call stack instrumented prolog/epilog are slightly race-y reading
  // and checking the saved return address, so it is better to not instrument
  // functions that have fewer instructions than the instrumented prolog/epilog
  // race.
  static const size_t SkipLeafInstructions = 3;
};

char ShadowCallStack::ID = 0;
} // end anonymous namespace.

static void addProlog(MachineFunction &Fn, const TargetInstrInfo *TII,
                      MachineBasicBlock &MBB, const DebugLoc &DL);
static void addPrologLeaf(MachineFunction &Fn, const TargetInstrInfo *TII,
                          MachineBasicBlock &MBB, const DebugLoc &DL,
                          MCPhysReg FreeRegister);

static void addEpilog(const TargetInstrInfo *TII, MachineBasicBlock &MBB,
                      MachineInstr &MI, MachineBasicBlock &TrapBB);
static void addEpilogLeaf(const TargetInstrInfo *TII, MachineBasicBlock &MBB,
                          MachineInstr &MI, MachineBasicBlock &TrapBB,
                          MCPhysReg FreeRegister);
// Generate a longer epilog that only uses r10 when a tailcall branches to r11.
static void addEpilogOnlyR10(const TargetInstrInfo *TII, MachineBasicBlock &MBB,
                             MachineInstr &MI, MachineBasicBlock &TrapBB);

// Helper function to add ModR/M references for [Seg: Reg + Offset] memory
// accesses
static inline const MachineInstrBuilder &
addSegmentedMem(const MachineInstrBuilder &MIB, MCPhysReg Seg, MCPhysReg Reg,
                int Offset = 0) {
  return MIB.addReg(Reg).addImm(1).addReg(0).addImm(Offset).addReg(Seg);
}

static void addProlog(MachineFunction &Fn, const TargetInstrInfo *TII,
                      MachineBasicBlock &MBB, const DebugLoc &DL) {
  const MCPhysReg ReturnReg = X86::R10;
  const MCPhysReg OffsetReg = X86::R11;

  auto MBBI = MBB.begin();
  // mov r10, [rsp]
  addDirectMem(BuildMI(MBB, MBBI, DL, TII->get(X86::MOV64rm)).addDef(ReturnReg),
               X86::RSP);
  // xor r11, r11
  BuildMI(MBB, MBBI, DL, TII->get(X86::XOR64rr))
      .addDef(OffsetReg)
      .addReg(OffsetReg, RegState::Undef)
      .addReg(OffsetReg, RegState::Undef);
  // add QWORD [gs:r11], 8
  addSegmentedMem(BuildMI(MBB, MBBI, DL, TII->get(X86::ADD64mi8)), X86::GS,
                  OffsetReg)
      .addImm(8);
  // mov r11, [gs:r11]
  addSegmentedMem(
      BuildMI(MBB, MBBI, DL, TII->get(X86::MOV64rm)).addDef(OffsetReg), X86::GS,
      OffsetReg);
  // mov [gs:r11], r10
  addSegmentedMem(BuildMI(MBB, MBBI, DL, TII->get(X86::MOV64mr)), X86::GS,
                  OffsetReg)
      .addReg(ReturnReg);
}

static void addPrologLeaf(MachineFunction &Fn, const TargetInstrInfo *TII,
                          MachineBasicBlock &MBB, const DebugLoc &DL,
                          MCPhysReg FreeRegister) {
  // mov REG, [rsp]
  addDirectMem(BuildMI(MBB, MBB.begin(), DL, TII->get(X86::MOV64rm))
                   .addDef(FreeRegister),
               X86::RSP);
}

static void addEpilog(const TargetInstrInfo *TII, MachineBasicBlock &MBB,
                      MachineInstr &MI, MachineBasicBlock &TrapBB) {
  const DebugLoc &DL = MI.getDebugLoc();

  // xor r11, r11
  BuildMI(MBB, MI, DL, TII->get(X86::XOR64rr))
      .addDef(X86::R11)
      .addReg(X86::R11, RegState::Undef)
      .addReg(X86::R11, RegState::Undef);
  // mov r10, [gs:r11]
  addSegmentedMem(BuildMI(MBB, MI, DL, TII->get(X86::MOV64rm)).addDef(X86::R10),
                  X86::GS, X86::R11);
  // mov r10, [gs:r10]
  addSegmentedMem(BuildMI(MBB, MI, DL, TII->get(X86::MOV64rm)).addDef(X86::R10),
                  X86::GS, X86::R10);
  // sub QWORD [gs:r11], 8
  // This instruction should not be moved up to avoid a signal race.
  addSegmentedMem(BuildMI(MBB, MI, DL, TII->get(X86::SUB64mi8)),
                  X86::GS, X86::R11)
      .addImm(8);
  // cmp [rsp], r10
  addDirectMem(BuildMI(MBB, MI, DL, TII->get(X86::CMP64mr)), X86::RSP)
      .addReg(X86::R10);
  // jne trap
  BuildMI(MBB, MI, DL, TII->get(X86::JNE_1)).addMBB(&TrapBB);
  MBB.addSuccessor(&TrapBB);
}

static void addEpilogLeaf(const TargetInstrInfo *TII, MachineBasicBlock &MBB,
                          MachineInstr &MI, MachineBasicBlock &TrapBB,
                          MCPhysReg FreeRegister) {
  const DebugLoc &DL = MI.getDebugLoc();

  // cmp [rsp], REG
  addDirectMem(BuildMI(MBB, MI, DL, TII->get(X86::CMP64mr)), X86::RSP)
      .addReg(FreeRegister);
  // jne trap
  BuildMI(MBB, MI, DL, TII->get(X86::JNE_1)).addMBB(&TrapBB);
  MBB.addSuccessor(&TrapBB);
}

static void addEpilogOnlyR10(const TargetInstrInfo *TII, MachineBasicBlock &MBB,
                             MachineInstr &MI, MachineBasicBlock &TrapBB) {
  const DebugLoc &DL = MI.getDebugLoc();

  // xor r10, r10
  BuildMI(MBB, MI, DL, TII->get(X86::XOR64rr))
      .addDef(X86::R10)
      .addReg(X86::R10, RegState::Undef)
      .addReg(X86::R10, RegState::Undef);
  // mov r10, [gs:r10]
  addSegmentedMem(BuildMI(MBB, MI, DL, TII->get(X86::MOV64rm)).addDef(X86::R10),
                  X86::GS, X86::R10);
  // mov r10, [gs:r10]
  addSegmentedMem(BuildMI(MBB, MI, DL, TII->get(X86::MOV64rm)).addDef(X86::R10),
                  X86::GS, X86::R10);
  // sub QWORD [gs:0], 8
  // This instruction should not be moved up to avoid a signal race.
  addSegmentedMem(BuildMI(MBB, MI, DL, TII->get(X86::SUB64mi8)), X86::GS, 0)
      .addImm(8);
  // cmp [rsp], r10
  addDirectMem(BuildMI(MBB, MI, DL, TII->get(X86::CMP64mr)), X86::RSP)
      .addReg(X86::R10);
  // jne trap
  BuildMI(MBB, MI, DL, TII->get(X86::JNE_1)).addMBB(&TrapBB);
  MBB.addSuccessor(&TrapBB);
}

bool ShadowCallStack::runOnMachineFunction(MachineFunction &Fn) {
  if (!Fn.getFunction().hasFnAttribute(Attribute::ShadowCallStack) ||
      Fn.getFunction().hasFnAttribute(Attribute::Naked))
    return false;

  if (Fn.empty() || !Fn.getRegInfo().tracksLiveness())
    return false;

  // FIXME: Skip functions that have r10 or r11 live on entry (r10 can be live
  // on entry for parameters with the nest attribute.)
  if (Fn.front().isLiveIn(X86::R10) || Fn.front().isLiveIn(X86::R11))
    return false;

  // FIXME: Skip functions with conditional and r10 tail calls for now.
  bool HasReturn = false;
  for (auto &MBB : Fn) {
    if (MBB.empty())
      continue;

    const MachineInstr &MI = MBB.instr_back();
    if (MI.isReturn())
      HasReturn = true;

    if (MI.isReturn() && MI.isCall()) {
      if (MI.findRegisterUseOperand(X86::EFLAGS))
        return false;
      // This should only be possible on Windows 64 (see GR64_TC versus
      // GR64_TCW64.)
      if (MI.findRegisterUseOperand(X86::R10) ||
          MI.hasRegisterImplicitUseOperand(X86::R10))
        return false;
    }
  }

  if (!HasReturn)
    return false;

  // For leaf functions:
  // 1. Do not instrument very short functions where it would not improve that
  //    function's security.
  // 2. Detect if there is an unused caller-saved register we can reserve to
  //    hold the return address instead of writing/reading it from the shadow
  //    call stack.
  MCPhysReg LeafFuncRegister = X86::NoRegister;
  if (!Fn.getFrameInfo().adjustsStack()) {
    size_t InstructionCount = 0;
    std::bitset<X86::NUM_TARGET_REGS> UsedRegs;
    for (auto &MBB : Fn) {
      for (auto &LiveIn : MBB.liveins())
        UsedRegs.set(LiveIn.PhysReg);
      for (auto &MI : MBB) {
        if (!MI.isDebugValue() && !MI.isCFIInstruction() && !MI.isLabel())
          InstructionCount++;
        for (auto &Op : MI.operands())
          if (Op.isReg() && Op.isDef())
            UsedRegs.set(Op.getReg());
      }
    }

    if (InstructionCount <= SkipLeafInstructions)
      return false;

    std::bitset<X86::NUM_TARGET_REGS> CalleeSavedRegs;
    const MCPhysReg *CSRegs = Fn.getRegInfo().getCalleeSavedRegs();
    for (size_t i = 0; CSRegs[i]; i++)
      CalleeSavedRegs.set(CSRegs[i]);

    const TargetRegisterInfo *TRI = Fn.getSubtarget().getRegisterInfo();
    for (auto &Reg : X86::GR64_NOSPRegClass.getRegisters()) {
      // FIXME: Optimization opportunity: spill/restore a callee-saved register
      // if a caller-saved register is unavailable.
      if (CalleeSavedRegs.test(Reg))
        continue;

      bool Used = false;
      for (MCSubRegIterator SR(Reg, TRI, true); SR.isValid(); ++SR)
        if ((Used = UsedRegs.test(*SR)))
          break;

      if (!Used) {
        LeafFuncRegister = Reg;
        break;
      }
    }
  }

  const bool LeafFuncOptimization = LeafFuncRegister != X86::NoRegister;
  if (LeafFuncOptimization)
    // Mark the leaf function register live-in for all MBBs except the entry MBB
    for (auto I = ++Fn.begin(), E = Fn.end(); I != E; ++I)
      I->addLiveIn(LeafFuncRegister);

  MachineBasicBlock &MBB = Fn.front();
  const MachineBasicBlock *NonEmpty = MBB.empty() ? MBB.getFallThrough() : &MBB;
  const DebugLoc &DL = NonEmpty->front().getDebugLoc();

  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();
  if (LeafFuncOptimization)
    addPrologLeaf(Fn, TII, MBB, DL, LeafFuncRegister);
  else
    addProlog(Fn, TII, MBB, DL);

  MachineBasicBlock *Trap = nullptr;
  for (auto &MBB : Fn) {
    if (MBB.empty())
      continue;

    MachineInstr &MI = MBB.instr_back();
    if (MI.isReturn()) {
      if (!Trap) {
        Trap = Fn.CreateMachineBasicBlock();
        BuildMI(Trap, MI.getDebugLoc(), TII->get(X86::TRAP));
        Fn.push_back(Trap);
      }

      if (LeafFuncOptimization)
        addEpilogLeaf(TII, MBB, MI, *Trap, LeafFuncRegister);
      else if (MI.findRegisterUseOperand(X86::R11))
        addEpilogOnlyR10(TII, MBB, MI, *Trap);
      else
        addEpilog(TII, MBB, MI, *Trap);
    }
  }

  return true;
}

INITIALIZE_PASS(ShadowCallStack, "shadow-call-stack", "Shadow Call Stack",
                false, false)

FunctionPass *llvm::createShadowCallStackPass() {
  return new ShadowCallStack();
}
