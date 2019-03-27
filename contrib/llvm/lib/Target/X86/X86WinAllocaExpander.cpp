//===----- X86WinAllocaExpander.cpp - Expand WinAlloca pseudo instruction -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that expands WinAlloca pseudo-instructions.
//
// It performs a conservative analysis to determine whether each allocation
// falls within a region of the stack that is safe to use, or whether stack
// probes must be emitted.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class X86WinAllocaExpander : public MachineFunctionPass {
public:
  X86WinAllocaExpander() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  /// Strategies for lowering a WinAlloca.
  enum Lowering { TouchAndSub, Sub, Probe };

  /// Deterministic-order map from WinAlloca instruction to desired lowering.
  typedef MapVector<MachineInstr*, Lowering> LoweringMap;

  /// Compute which lowering to use for each WinAlloca instruction.
  void computeLowerings(MachineFunction &MF, LoweringMap& Lowerings);

  /// Get the appropriate lowering based on current offset and amount.
  Lowering getLowering(int64_t CurrentOffset, int64_t AllocaAmount);

  /// Lower a WinAlloca instruction.
  void lower(MachineInstr* MI, Lowering L);

  MachineRegisterInfo *MRI;
  const X86Subtarget *STI;
  const TargetInstrInfo *TII;
  const X86RegisterInfo *TRI;
  unsigned StackPtr;
  unsigned SlotSize;
  int64_t StackProbeSize;
  bool NoStackArgProbe;

  StringRef getPassName() const override { return "X86 WinAlloca Expander"; }
  static char ID;
};

char X86WinAllocaExpander::ID = 0;

} // end anonymous namespace

FunctionPass *llvm::createX86WinAllocaExpander() {
  return new X86WinAllocaExpander();
}

/// Return the allocation amount for a WinAlloca instruction, or -1 if unknown.
static int64_t getWinAllocaAmount(MachineInstr *MI, MachineRegisterInfo *MRI) {
  assert(MI->getOpcode() == X86::WIN_ALLOCA_32 ||
         MI->getOpcode() == X86::WIN_ALLOCA_64);
  assert(MI->getOperand(0).isReg());

  unsigned AmountReg = MI->getOperand(0).getReg();
  MachineInstr *Def = MRI->getUniqueVRegDef(AmountReg);

  // Look through copies.
  while (Def && Def->isCopy() && Def->getOperand(1).isReg())
    Def = MRI->getUniqueVRegDef(Def->getOperand(1).getReg());

  if (!Def ||
      (Def->getOpcode() != X86::MOV32ri && Def->getOpcode() != X86::MOV64ri) ||
      !Def->getOperand(1).isImm())
    return -1;

  return Def->getOperand(1).getImm();
}

X86WinAllocaExpander::Lowering
X86WinAllocaExpander::getLowering(int64_t CurrentOffset,
                                  int64_t AllocaAmount) {
  // For a non-constant amount or a large amount, we have to probe.
  if (AllocaAmount < 0 || AllocaAmount > StackProbeSize)
    return Probe;

  // If it fits within the safe region of the stack, just subtract.
  if (CurrentOffset + AllocaAmount <= StackProbeSize)
    return Sub;

  // Otherwise, touch the current tip of the stack, then subtract.
  return TouchAndSub;
}

static bool isPushPop(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case X86::PUSH32i8:
  case X86::PUSH32r:
  case X86::PUSH32rmm:
  case X86::PUSH32rmr:
  case X86::PUSHi32:
  case X86::PUSH64i8:
  case X86::PUSH64r:
  case X86::PUSH64rmm:
  case X86::PUSH64rmr:
  case X86::PUSH64i32:
  case X86::POP32r:
  case X86::POP64r:
    return true;
  default:
    return false;
  }
}

void X86WinAllocaExpander::computeLowerings(MachineFunction &MF,
                                            LoweringMap &Lowerings) {
  // Do a one-pass reverse post-order walk of the CFG to conservatively estimate
  // the offset between the stack pointer and the lowest touched part of the
  // stack, and use that to decide how to lower each WinAlloca instruction.

  // Initialize OutOffset[B], the stack offset at exit from B, to something big.
  DenseMap<MachineBasicBlock *, int64_t> OutOffset;
  for (MachineBasicBlock &MBB : MF)
    OutOffset[&MBB] = INT32_MAX;

  // Note: we don't know the offset at the start of the entry block since the
  // prologue hasn't been inserted yet, and how much that will adjust the stack
  // pointer depends on register spills, which have not been computed yet.

  // Compute the reverse post-order.
  ReversePostOrderTraversal<MachineFunction*> RPO(&MF);

  for (MachineBasicBlock *MBB : RPO) {
    int64_t Offset = -1;
    for (MachineBasicBlock *Pred : MBB->predecessors())
      Offset = std::max(Offset, OutOffset[Pred]);
    if (Offset == -1) Offset = INT32_MAX;

    for (MachineInstr &MI : *MBB) {
      if (MI.getOpcode() == X86::WIN_ALLOCA_32 ||
          MI.getOpcode() == X86::WIN_ALLOCA_64) {
        // A WinAlloca moves StackPtr, and potentially touches it.
        int64_t Amount = getWinAllocaAmount(&MI, MRI);
        Lowering L = getLowering(Offset, Amount);
        Lowerings[&MI] = L;
        switch (L) {
        case Sub:
          Offset += Amount;
          break;
        case TouchAndSub:
          Offset = Amount;
          break;
        case Probe:
          Offset = 0;
          break;
        }
      } else if (MI.isCall() || isPushPop(MI)) {
        // Calls, pushes and pops touch the tip of the stack.
        Offset = 0;
      } else if (MI.getOpcode() == X86::ADJCALLSTACKUP32 ||
                 MI.getOpcode() == X86::ADJCALLSTACKUP64) {
        Offset -= MI.getOperand(0).getImm();
      } else if (MI.getOpcode() == X86::ADJCALLSTACKDOWN32 ||
                 MI.getOpcode() == X86::ADJCALLSTACKDOWN64) {
        Offset += MI.getOperand(0).getImm();
      } else if (MI.modifiesRegister(StackPtr, TRI)) {
        // Any other modification of SP means we've lost track of it.
        Offset = INT32_MAX;
      }
    }

    OutOffset[MBB] = Offset;
  }
}

static unsigned getSubOpcode(bool Is64Bit, int64_t Amount) {
  if (Is64Bit)
    return isInt<8>(Amount) ? X86::SUB64ri8 : X86::SUB64ri32;
  return isInt<8>(Amount) ? X86::SUB32ri8 : X86::SUB32ri;
}

void X86WinAllocaExpander::lower(MachineInstr* MI, Lowering L) {
  DebugLoc DL = MI->getDebugLoc();
  MachineBasicBlock *MBB = MI->getParent();
  MachineBasicBlock::iterator I = *MI;

  int64_t Amount = getWinAllocaAmount(MI, MRI);
  if (Amount == 0) {
    MI->eraseFromParent();
    return;
  }

  bool Is64Bit = STI->is64Bit();
  assert(SlotSize == 4 || SlotSize == 8);
  unsigned RegA = (SlotSize == 8) ? X86::RAX : X86::EAX;

  switch (L) {
  case TouchAndSub:
    assert(Amount >= SlotSize);

    // Use a push to touch the top of the stack.
    BuildMI(*MBB, I, DL, TII->get(Is64Bit ? X86::PUSH64r : X86::PUSH32r))
        .addReg(RegA, RegState::Undef);
    Amount -= SlotSize;
    if (!Amount)
      break;

    // Fall through to make any remaining adjustment.
    LLVM_FALLTHROUGH;
  case Sub:
    assert(Amount > 0);
    if (Amount == SlotSize) {
      // Use push to save size.
      BuildMI(*MBB, I, DL, TII->get(Is64Bit ? X86::PUSH64r : X86::PUSH32r))
          .addReg(RegA, RegState::Undef);
    } else {
      // Sub.
      BuildMI(*MBB, I, DL, TII->get(getSubOpcode(Is64Bit, Amount)), StackPtr)
          .addReg(StackPtr)
          .addImm(Amount);
    }
    break;
  case Probe:
    if (!NoStackArgProbe) {
      // The probe lowering expects the amount in RAX/EAX.
      BuildMI(*MBB, MI, DL, TII->get(TargetOpcode::COPY), RegA)
          .addReg(MI->getOperand(0).getReg());

      // Do the probe.
      STI->getFrameLowering()->emitStackProbe(*MBB->getParent(), *MBB, MI, DL,
                                              /*InPrologue=*/false);
    } else {
      // Sub
      BuildMI(*MBB, I, DL, TII->get(Is64Bit ? X86::SUB64rr : X86::SUB32rr),
              StackPtr)
          .addReg(StackPtr)
          .addReg(MI->getOperand(0).getReg());
    }
    break;
  }

  unsigned AmountReg = MI->getOperand(0).getReg();
  MI->eraseFromParent();

  // Delete the definition of AmountReg, possibly walking a chain of copies.
  for (;;) {
    if (!MRI->use_empty(AmountReg))
      break;
    MachineInstr *AmountDef = MRI->getUniqueVRegDef(AmountReg);
    if (!AmountDef)
      break;
    if (AmountDef->isCopy() && AmountDef->getOperand(1).isReg())
      AmountReg = AmountDef->getOperand(1).isReg();
    AmountDef->eraseFromParent();
    break;
  }
}

bool X86WinAllocaExpander::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getInfo<X86MachineFunctionInfo>()->hasWinAlloca())
    return false;

  MRI = &MF.getRegInfo();
  STI = &MF.getSubtarget<X86Subtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  StackPtr = TRI->getStackRegister();
  SlotSize = TRI->getSlotSize();

  StackProbeSize = 4096;
  if (MF.getFunction().hasFnAttribute("stack-probe-size")) {
    MF.getFunction()
        .getFnAttribute("stack-probe-size")
        .getValueAsString()
        .getAsInteger(0, StackProbeSize);
  }
  NoStackArgProbe = MF.getFunction().hasFnAttribute("no-stack-arg-probe");
  if (NoStackArgProbe)
    StackProbeSize = INT64_MAX;

  LoweringMap Lowerings;
  computeLowerings(MF, Lowerings);
  for (auto &P : Lowerings)
    lower(P.first, P.second);

  return true;
}
