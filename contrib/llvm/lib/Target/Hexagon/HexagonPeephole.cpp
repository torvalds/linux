//===-- HexagonPeephole.cpp - Hexagon Peephole Optimiztions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// This peephole pass optimizes in the following cases.
// 1. Optimizes redundant sign extends for the following case
//    Transform the following pattern
//    %170 = SXTW %166
//    ...
//    %176 = COPY %170:isub_lo
//
//    Into
//    %176 = COPY %166
//
//  2. Optimizes redundant negation of predicates.
//     %15 = CMPGTrr %6, %2
//     ...
//     %16 = NOT_p killed %15
//     ...
//     JMP_c killed %16, <%bb.1>, implicit dead %pc
//
//     Into
//     %15 = CMPGTrr %6, %2;
//     ...
//     JMP_cNot killed %15, <%bb.1>, implicit dead %pc;
//
// Note: The peephole pass makes the instrucstions like
// %170 = SXTW %166 or %16 = NOT_p killed %15
// redundant and relies on some form of dead removal instructions, like
// DCE or DIE to actually eliminate them.

//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "hexagon-peephole"

static cl::opt<bool> DisableHexagonPeephole("disable-hexagon-peephole",
    cl::Hidden, cl::ZeroOrMore, cl::init(false),
    cl::desc("Disable Peephole Optimization"));

static cl::opt<bool> DisablePNotP("disable-hexagon-pnotp",
    cl::Hidden, cl::ZeroOrMore, cl::init(false),
    cl::desc("Disable Optimization of PNotP"));

static cl::opt<bool> DisableOptSZExt("disable-hexagon-optszext",
    cl::Hidden, cl::ZeroOrMore, cl::init(true),
    cl::desc("Disable Optimization of Sign/Zero Extends"));

static cl::opt<bool> DisableOptExtTo64("disable-hexagon-opt-ext-to-64",
    cl::Hidden, cl::ZeroOrMore, cl::init(true),
    cl::desc("Disable Optimization of extensions to i64."));

namespace llvm {
  FunctionPass *createHexagonPeephole();
  void initializeHexagonPeepholePass(PassRegistry&);
}

namespace {
  struct HexagonPeephole : public MachineFunctionPass {
    const HexagonInstrInfo    *QII;
    const HexagonRegisterInfo *QRI;
    const MachineRegisterInfo *MRI;

  public:
    static char ID;
    HexagonPeephole() : MachineFunctionPass(ID) {
      initializeHexagonPeepholePass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    StringRef getPassName() const override {
      return "Hexagon optimize redundant zero and size extends";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

char HexagonPeephole::ID = 0;

INITIALIZE_PASS(HexagonPeephole, "hexagon-peephole", "Hexagon Peephole",
                false, false)

bool HexagonPeephole::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  QII = static_cast<const HexagonInstrInfo *>(MF.getSubtarget().getInstrInfo());
  QRI = MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  MRI = &MF.getRegInfo();

  DenseMap<unsigned, unsigned> PeepholeMap;
  DenseMap<unsigned, std::pair<unsigned, unsigned> > PeepholeDoubleRegsMap;

  if (DisableHexagonPeephole) return false;

  // Loop over all of the basic blocks.
  for (MachineFunction::iterator MBBb = MF.begin(), MBBe = MF.end();
       MBBb != MBBe; ++MBBb) {
    MachineBasicBlock *MBB = &*MBBb;
    PeepholeMap.clear();
    PeepholeDoubleRegsMap.clear();

    // Traverse the basic block.
    for (auto I = MBB->begin(), E = MBB->end(), NextI = I; I != E; I = NextI) {
      NextI = std::next(I);
      MachineInstr &MI = *I;
      // Look for sign extends:
      // %170 = SXTW %166
      if (!DisableOptSZExt && MI.getOpcode() == Hexagon::A2_sxtw) {
        assert(MI.getNumOperands() == 2);
        MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src = MI.getOperand(1);
        unsigned DstReg = Dst.getReg();
        unsigned SrcReg = Src.getReg();
        // Just handle virtual registers.
        if (TargetRegisterInfo::isVirtualRegister(DstReg) &&
            TargetRegisterInfo::isVirtualRegister(SrcReg)) {
          // Map the following:
          // %170 = SXTW %166
          // PeepholeMap[170] = %166
          PeepholeMap[DstReg] = SrcReg;
        }
      }

      // Look for  %170 = COMBINE_ir_V4 (0, %169)
      // %170:DoublRegs, %169:IntRegs
      if (!DisableOptExtTo64 && MI.getOpcode() == Hexagon::A4_combineir) {
        assert(MI.getNumOperands() == 3);
        MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src1 = MI.getOperand(1);
        MachineOperand &Src2 = MI.getOperand(2);
        if (Src1.getImm() != 0)
          continue;
        unsigned DstReg = Dst.getReg();
        unsigned SrcReg = Src2.getReg();
        PeepholeMap[DstReg] = SrcReg;
      }

      // Look for this sequence below
      // %DoubleReg1 = LSRd_ri %DoubleReg0, 32
      // %IntReg = COPY %DoubleReg1:isub_lo.
      // and convert into
      // %IntReg = COPY %DoubleReg0:isub_hi.
      if (MI.getOpcode() == Hexagon::S2_lsr_i_p) {
        assert(MI.getNumOperands() == 3);
        MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src1 = MI.getOperand(1);
        MachineOperand &Src2 = MI.getOperand(2);
        if (Src2.getImm() != 32)
          continue;
        unsigned DstReg = Dst.getReg();
        unsigned SrcReg = Src1.getReg();
        PeepholeDoubleRegsMap[DstReg] =
          std::make_pair(*&SrcReg, Hexagon::isub_hi);
      }

      // Look for P=NOT(P).
      if (!DisablePNotP && MI.getOpcode() == Hexagon::C2_not) {
        assert(MI.getNumOperands() == 2);
        MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src = MI.getOperand(1);
        unsigned DstReg = Dst.getReg();
        unsigned SrcReg = Src.getReg();
        // Just handle virtual registers.
        if (TargetRegisterInfo::isVirtualRegister(DstReg) &&
            TargetRegisterInfo::isVirtualRegister(SrcReg)) {
          // Map the following:
          // %170 = NOT_xx %166
          // PeepholeMap[170] = %166
          PeepholeMap[DstReg] = SrcReg;
        }
      }

      // Look for copy:
      // %176 = COPY %170:isub_lo
      if (!DisableOptSZExt && MI.isCopy()) {
        assert(MI.getNumOperands() == 2);
        MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src = MI.getOperand(1);

        // Make sure we are copying the lower 32 bits.
        if (Src.getSubReg() != Hexagon::isub_lo)
          continue;

        unsigned DstReg = Dst.getReg();
        unsigned SrcReg = Src.getReg();
        if (TargetRegisterInfo::isVirtualRegister(DstReg) &&
            TargetRegisterInfo::isVirtualRegister(SrcReg)) {
          // Try to find in the map.
          if (unsigned PeepholeSrc = PeepholeMap.lookup(SrcReg)) {
            // Change the 1st operand.
            MI.RemoveOperand(1);
            MI.addOperand(MachineOperand::CreateReg(PeepholeSrc, false));
          } else  {
            DenseMap<unsigned, std::pair<unsigned, unsigned> >::iterator DI =
              PeepholeDoubleRegsMap.find(SrcReg);
            if (DI != PeepholeDoubleRegsMap.end()) {
              std::pair<unsigned,unsigned> PeepholeSrc = DI->second;
              MI.RemoveOperand(1);
              MI.addOperand(MachineOperand::CreateReg(
                  PeepholeSrc.first, false /*isDef*/, false /*isImp*/,
                  false /*isKill*/, false /*isDead*/, false /*isUndef*/,
                  false /*isEarlyClobber*/, PeepholeSrc.second));
            }
          }
        }
      }

      // Look for Predicated instructions.
      if (!DisablePNotP) {
        bool Done = false;
        if (QII->isPredicated(MI)) {
          MachineOperand &Op0 = MI.getOperand(0);
          unsigned Reg0 = Op0.getReg();
          const TargetRegisterClass *RC0 = MRI->getRegClass(Reg0);
          if (RC0->getID() == Hexagon::PredRegsRegClassID) {
            // Handle instructions that have a prediate register in op0
            // (most cases of predicable instructions).
            if (TargetRegisterInfo::isVirtualRegister(Reg0)) {
              // Try to find in the map.
              if (unsigned PeepholeSrc = PeepholeMap.lookup(Reg0)) {
                // Change the 1st operand and, flip the opcode.
                MI.getOperand(0).setReg(PeepholeSrc);
                MRI->clearKillFlags(PeepholeSrc);
                int NewOp = QII->getInvertedPredicatedOpcode(MI.getOpcode());
                MI.setDesc(QII->get(NewOp));
                Done = true;
              }
            }
          }
        }

        if (!Done) {
          // Handle special instructions.
          unsigned Op = MI.getOpcode();
          unsigned NewOp = 0;
          unsigned PR = 1, S1 = 2, S2 = 3;   // Operand indices.

          switch (Op) {
            case Hexagon::C2_mux:
            case Hexagon::C2_muxii:
              NewOp = Op;
              break;
            case Hexagon::C2_muxri:
              NewOp = Hexagon::C2_muxir;
              break;
            case Hexagon::C2_muxir:
              NewOp = Hexagon::C2_muxri;
              break;
          }
          if (NewOp) {
            unsigned PSrc = MI.getOperand(PR).getReg();
            if (unsigned POrig = PeepholeMap.lookup(PSrc)) {
              BuildMI(*MBB, MI.getIterator(), MI.getDebugLoc(),
                      QII->get(NewOp), MI.getOperand(0).getReg())
                .addReg(POrig)
                .add(MI.getOperand(S2))
                .add(MI.getOperand(S1));
              MRI->clearKillFlags(POrig);
              MI.eraseFromParent();
            }
          } // if (NewOp)
        } // if (!Done)

      } // if (!DisablePNotP)

    } // Instruction
  } // Basic Block
  return true;
}

FunctionPass *llvm::createHexagonPeephole() {
  return new HexagonPeephole();
}
