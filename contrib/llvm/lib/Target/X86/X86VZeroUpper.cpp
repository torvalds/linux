//===- X86VZeroUpper.cpp - AVX vzeroupper instruction inserter ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the pass which inserts x86 AVX vzeroupper instructions
// before calls to SSE encoded functions. This avoids transition latency
// penalty when transferring control between AVX encoded instructions and old
// SSE encoding mode.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "x86-vzeroupper"

STATISTIC(NumVZU, "Number of vzeroupper instructions inserted");

namespace {

  class VZeroUpperInserter : public MachineFunctionPass {
  public:
    VZeroUpperInserter() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override { return "X86 vzeroupper inserter"; }

  private:
    void processBasicBlock(MachineBasicBlock &MBB);
    void insertVZeroUpper(MachineBasicBlock::iterator I,
                          MachineBasicBlock &MBB);
    void addDirtySuccessor(MachineBasicBlock &MBB);

    using BlockExitState = enum { PASS_THROUGH, EXITS_CLEAN, EXITS_DIRTY };

    static const char* getBlockExitStateName(BlockExitState ST);

    // Core algorithm state:
    // BlockState - Each block is either:
    //   - PASS_THROUGH: There are neither YMM/ZMM dirtying instructions nor
    //                   vzeroupper instructions in this block.
    //   - EXITS_CLEAN: There is (or will be) a vzeroupper instruction in this
    //                  block that will ensure that YMM/ZMM is clean on exit.
    //   - EXITS_DIRTY: An instruction in the block dirties YMM/ZMM and no
    //                  subsequent vzeroupper in the block clears it.
    //
    // AddedToDirtySuccessors - This flag is raised when a block is added to the
    //                          DirtySuccessors list to ensure that it's not
    //                          added multiple times.
    //
    // FirstUnguardedCall - Records the location of the first unguarded call in
    //                      each basic block that may need to be guarded by a
    //                      vzeroupper. We won't know whether it actually needs
    //                      to be guarded until we discover a predecessor that
    //                      is DIRTY_OUT.
    struct BlockState {
      BlockExitState ExitState = PASS_THROUGH;
      bool AddedToDirtySuccessors = false;
      MachineBasicBlock::iterator FirstUnguardedCall;

      BlockState() = default;
    };

    using BlockStateMap = SmallVector<BlockState, 8>;
    using DirtySuccessorsWorkList = SmallVector<MachineBasicBlock *, 8>;

    BlockStateMap BlockStates;
    DirtySuccessorsWorkList DirtySuccessors;
    bool EverMadeChange;
    bool IsX86INTR;
    const TargetInstrInfo *TII;

    static char ID;
  };

} // end anonymous namespace

char VZeroUpperInserter::ID = 0;

FunctionPass *llvm::createX86IssueVZeroUpperPass() {
  return new VZeroUpperInserter();
}

#ifndef NDEBUG
const char* VZeroUpperInserter::getBlockExitStateName(BlockExitState ST) {
  switch (ST) {
    case PASS_THROUGH: return "Pass-through";
    case EXITS_DIRTY: return "Exits-dirty";
    case EXITS_CLEAN: return "Exits-clean";
  }
  llvm_unreachable("Invalid block exit state.");
}
#endif

/// VZEROUPPER cleans state that is related to Y/ZMM0-15 only.
/// Thus, there is no need to check for Y/ZMM16 and above.
static bool isYmmOrZmmReg(unsigned Reg) {
  return (Reg >= X86::YMM0 && Reg <= X86::YMM15) ||
         (Reg >= X86::ZMM0 && Reg <= X86::ZMM15);
}

static bool checkFnHasLiveInYmmOrZmm(MachineRegisterInfo &MRI) {
  for (std::pair<unsigned, unsigned> LI : MRI.liveins())
    if (isYmmOrZmmReg(LI.first))
      return true;

  return false;
}

static bool clobbersAllYmmAndZmmRegs(const MachineOperand &MO) {
  for (unsigned reg = X86::YMM0; reg <= X86::YMM15; ++reg) {
    if (!MO.clobbersPhysReg(reg))
      return false;
  }
  for (unsigned reg = X86::ZMM0; reg <= X86::ZMM15; ++reg) {
    if (!MO.clobbersPhysReg(reg))
      return false;
  }
  return true;
}

static bool hasYmmOrZmmReg(MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MI.isCall() && MO.isRegMask() && !clobbersAllYmmAndZmmRegs(MO))
      return true;
    if (!MO.isReg())
      continue;
    if (MO.isDebug())
      continue;
    if (isYmmOrZmmReg(MO.getReg()))
      return true;
  }
  return false;
}

/// Check if given call instruction has a RegMask operand.
static bool callHasRegMask(MachineInstr &MI) {
  assert(MI.isCall() && "Can only be called on call instructions.");
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isRegMask())
      return true;
  }
  return false;
}

/// Insert a vzeroupper instruction before I.
void VZeroUpperInserter::insertVZeroUpper(MachineBasicBlock::iterator I,
                                          MachineBasicBlock &MBB) {
  DebugLoc dl = I->getDebugLoc();
  BuildMI(MBB, I, dl, TII->get(X86::VZEROUPPER));
  ++NumVZU;
  EverMadeChange = true;
}

/// Add MBB to the DirtySuccessors list if it hasn't already been added.
void VZeroUpperInserter::addDirtySuccessor(MachineBasicBlock &MBB) {
  if (!BlockStates[MBB.getNumber()].AddedToDirtySuccessors) {
    DirtySuccessors.push_back(&MBB);
    BlockStates[MBB.getNumber()].AddedToDirtySuccessors = true;
  }
}

/// Loop over all of the instructions in the basic block, inserting vzeroupper
/// instructions before function calls.
void VZeroUpperInserter::processBasicBlock(MachineBasicBlock &MBB) {
  // Start by assuming that the block is PASS_THROUGH which implies no unguarded
  // calls.
  BlockExitState CurState = PASS_THROUGH;
  BlockStates[MBB.getNumber()].FirstUnguardedCall = MBB.end();

  for (MachineInstr &MI : MBB) {
    bool IsCall = MI.isCall();
    bool IsReturn = MI.isReturn();
    bool IsControlFlow = IsCall || IsReturn;

    // No need for vzeroupper before iret in interrupt handler function,
    // epilogue will restore YMM/ZMM registers if needed.
    if (IsX86INTR && IsReturn)
      continue;

    // An existing VZERO* instruction resets the state.
    if (MI.getOpcode() == X86::VZEROALL || MI.getOpcode() == X86::VZEROUPPER) {
      CurState = EXITS_CLEAN;
      continue;
    }

    // Shortcut: don't need to check regular instructions in dirty state.
    if (!IsControlFlow && CurState == EXITS_DIRTY)
      continue;

    if (hasYmmOrZmmReg(MI)) {
      // We found a ymm/zmm-using instruction; this could be an AVX/AVX512
      // instruction, or it could be control flow.
      CurState = EXITS_DIRTY;
      continue;
    }

    // Check for control-flow out of the current function (which might
    // indirectly execute SSE instructions).
    if (!IsControlFlow)
      continue;

    // If the call has no RegMask, skip it as well. It usually happens on
    // helper function calls (such as '_chkstk', '_ftol2') where standard
    // calling convention is not used (RegMask is not used to mark register
    // clobbered and register usage (def/implicit-def/use) is well-defined and
    // explicitly specified.
    if (IsCall && !callHasRegMask(MI))
      continue;

    // The VZEROUPPER instruction resets the upper 128 bits of YMM0-YMM15
    // registers. In addition, the processor changes back to Clean state, after
    // which execution of SSE instructions or AVX instructions has no transition
    // penalty. Add the VZEROUPPER instruction before any function call/return
    // that might execute SSE code.
    // FIXME: In some cases, we may want to move the VZEROUPPER into a
    // predecessor block.
    if (CurState == EXITS_DIRTY) {
      // After the inserted VZEROUPPER the state becomes clean again, but
      // other YMM/ZMM may appear before other subsequent calls or even before
      // the end of the BB.
      insertVZeroUpper(MI, MBB);
      CurState = EXITS_CLEAN;
    } else if (CurState == PASS_THROUGH) {
      // If this block is currently in pass-through state and we encounter a
      // call then whether we need a vzeroupper or not depends on whether this
      // block has successors that exit dirty. Record the location of the call,
      // and set the state to EXITS_CLEAN, but do not insert the vzeroupper yet.
      // It will be inserted later if necessary.
      BlockStates[MBB.getNumber()].FirstUnguardedCall = MI;
      CurState = EXITS_CLEAN;
    }
  }

  LLVM_DEBUG(dbgs() << "MBB #" << MBB.getNumber() << " exit state: "
                    << getBlockExitStateName(CurState) << '\n');

  if (CurState == EXITS_DIRTY)
    for (MachineBasicBlock::succ_iterator SI = MBB.succ_begin(),
                                          SE = MBB.succ_end();
         SI != SE; ++SI)
      addDirtySuccessor(**SI);

  BlockStates[MBB.getNumber()].ExitState = CurState;
}

/// Loop over all of the basic blocks, inserting vzeroupper instructions before
/// function calls.
bool VZeroUpperInserter::runOnMachineFunction(MachineFunction &MF) {
  const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
  if (!ST.hasAVX() || ST.hasFastPartialYMMorZMMWrite())
    return false;
  TII = ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  EverMadeChange = false;
  IsX86INTR = MF.getFunction().getCallingConv() == CallingConv::X86_INTR;

  bool FnHasLiveInYmmOrZmm = checkFnHasLiveInYmmOrZmm(MRI);

  // Fast check: if the function doesn't use any ymm/zmm registers, we don't
  // need to insert any VZEROUPPER instructions.  This is constant-time, so it
  // is cheap in the common case of no ymm/zmm use.
  bool YmmOrZmmUsed = FnHasLiveInYmmOrZmm;
  const TargetRegisterClass *RCs[2] = {&X86::VR256RegClass, &X86::VR512RegClass};
  for (auto *RC : RCs) {
    if (!YmmOrZmmUsed) {
      for (TargetRegisterClass::iterator i = RC->begin(), e = RC->end(); i != e;
           i++) {
        if (!MRI.reg_nodbg_empty(*i)) {
          YmmOrZmmUsed = true;
          break;
        }
      }
    }
  }
  if (!YmmOrZmmUsed) {
    return false;
  }

  assert(BlockStates.empty() && DirtySuccessors.empty() &&
         "X86VZeroUpper state should be clear");
  BlockStates.resize(MF.getNumBlockIDs());

  // Process all blocks. This will compute block exit states, record the first
  // unguarded call in each block, and add successors of dirty blocks to the
  // DirtySuccessors list.
  for (MachineBasicBlock &MBB : MF)
    processBasicBlock(MBB);

  // If any YMM/ZMM regs are live-in to this function, add the entry block to
  // the DirtySuccessors list
  if (FnHasLiveInYmmOrZmm)
    addDirtySuccessor(MF.front());

  // Re-visit all blocks that are successors of EXITS_DIRTY blocks. Add
  // vzeroupper instructions to unguarded calls, and propagate EXITS_DIRTY
  // through PASS_THROUGH blocks.
  while (!DirtySuccessors.empty()) {
    MachineBasicBlock &MBB = *DirtySuccessors.back();
    DirtySuccessors.pop_back();
    BlockState &BBState = BlockStates[MBB.getNumber()];

    // MBB is a successor of a dirty block, so its first call needs to be
    // guarded.
    if (BBState.FirstUnguardedCall != MBB.end())
      insertVZeroUpper(BBState.FirstUnguardedCall, MBB);

    // If this successor was a pass-through block, then it is now dirty. Its
    // successors need to be added to the worklist (if they haven't been
    // already).
    if (BBState.ExitState == PASS_THROUGH) {
      LLVM_DEBUG(dbgs() << "MBB #" << MBB.getNumber()
                        << " was Pass-through, is now Dirty-out.\n");
      for (MachineBasicBlock *Succ : MBB.successors())
        addDirtySuccessor(*Succ);
    }
  }

  BlockStates.clear();
  return EverMadeChange;
}
