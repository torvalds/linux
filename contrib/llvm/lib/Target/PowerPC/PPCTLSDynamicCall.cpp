//===---------- PPCTLSDynamicCall.cpp - TLS Dynamic Call Fixup ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass expands ADDItls{ld,gd}LADDR[32] machine instructions into
// separate ADDItls[gd]L[32] and GETtlsADDR[32] instructions, both of
// which define GPR3.  A copy is added from GPR3 to the target virtual
// register of the original instruction.  The GETtlsADDR[32] is really
// a call instruction, so its target register is constrained to be GPR3.
// This is not true of ADDItls[gd]L[32], but there is a legacy linker
// optimization bug that requires the target register of the addi of
// a local- or general-dynamic TLS access sequence to be GPR3.
//
// This is done in a late pass so that TLS variable accesses can be
// fully commoned by MachineCSE.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-tls-dynamic-call"

namespace llvm {
  void initializePPCTLSDynamicCallPass(PassRegistry&);
}

namespace {
  struct PPCTLSDynamicCall : public MachineFunctionPass {
    static char ID;
    PPCTLSDynamicCall() : MachineFunctionPass(ID) {
      initializePPCTLSDynamicCallPass(*PassRegistry::getPassRegistry());
    }

    const PPCInstrInfo *TII;
    LiveIntervals *LIS;

protected:
    bool processBlock(MachineBasicBlock &MBB) {
      bool Changed = false;
      bool NeedFence = true;
      bool Is64Bit = MBB.getParent()->getSubtarget<PPCSubtarget>().isPPC64();

      for (MachineBasicBlock::iterator I = MBB.begin(), IE = MBB.end();
           I != IE;) {
        MachineInstr &MI = *I;

        if (MI.getOpcode() != PPC::ADDItlsgdLADDR &&
            MI.getOpcode() != PPC::ADDItlsldLADDR &&
            MI.getOpcode() != PPC::ADDItlsgdLADDR32 &&
            MI.getOpcode() != PPC::ADDItlsldLADDR32) {

          // Although we create ADJCALLSTACKDOWN and ADJCALLSTACKUP
          // as scheduling fences, we skip creating fences if we already
          // have existing ADJCALLSTACKDOWN/UP to avoid nesting,
          // which causes verification error with -verify-machineinstrs.
          if (MI.getOpcode() == PPC::ADJCALLSTACKDOWN)
            NeedFence = false;
          else if (MI.getOpcode() == PPC::ADJCALLSTACKUP)
            NeedFence = true;

          ++I;
          continue;
        }

        LLVM_DEBUG(dbgs() << "TLS Dynamic Call Fixup:\n    " << MI);

        unsigned OutReg = MI.getOperand(0).getReg();
        unsigned InReg = MI.getOperand(1).getReg();
        DebugLoc DL = MI.getDebugLoc();
        unsigned GPR3 = Is64Bit ? PPC::X3 : PPC::R3;
        unsigned Opc1, Opc2;
        const unsigned OrigRegs[] = {OutReg, InReg, GPR3};

        switch (MI.getOpcode()) {
        default:
          llvm_unreachable("Opcode inconsistency error");
        case PPC::ADDItlsgdLADDR:
          Opc1 = PPC::ADDItlsgdL;
          Opc2 = PPC::GETtlsADDR;
          break;
        case PPC::ADDItlsldLADDR:
          Opc1 = PPC::ADDItlsldL;
          Opc2 = PPC::GETtlsldADDR;
          break;
        case PPC::ADDItlsgdLADDR32:
          Opc1 = PPC::ADDItlsgdL32;
          Opc2 = PPC::GETtlsADDR32;
          break;
        case PPC::ADDItlsldLADDR32:
          Opc1 = PPC::ADDItlsldL32;
          Opc2 = PPC::GETtlsldADDR32;
          break;
        }

        // We create ADJCALLSTACKUP and ADJCALLSTACKDOWN around _tls_get_addr
        // as scheduling fence to avoid it is scheduled before
        // mflr in the prologue and the address in LR is clobbered (PR25839).
        // We don't really need to save data to the stack - the clobbered
        // registers are already saved when the SDNode (e.g. PPCaddiTlsgdLAddr)
        // gets translated to the pseudo instruction (e.g. ADDItlsgdLADDR).
        if (NeedFence)
          BuildMI(MBB, I, DL, TII->get(PPC::ADJCALLSTACKDOWN)).addImm(0)
                                                              .addImm(0);

        // Expand into two ops built prior to the existing instruction.
        MachineInstr *Addi = BuildMI(MBB, I, DL, TII->get(Opc1), GPR3)
          .addReg(InReg);
        Addi->addOperand(MI.getOperand(2));

        // The ADDItls* instruction is the first instruction in the
        // repair range.
        MachineBasicBlock::iterator First = I;
        --First;

        MachineInstr *Call = (BuildMI(MBB, I, DL, TII->get(Opc2), GPR3)
                              .addReg(GPR3));
        Call->addOperand(MI.getOperand(3));

        if (NeedFence)
          BuildMI(MBB, I, DL, TII->get(PPC::ADJCALLSTACKUP)).addImm(0).addImm(0);

        BuildMI(MBB, I, DL, TII->get(TargetOpcode::COPY), OutReg)
          .addReg(GPR3);

        // The COPY is the last instruction in the repair range.
        MachineBasicBlock::iterator Last = I;
        --Last;

        // Move past the original instruction and remove it.
        ++I;
        MI.removeFromParent();

        // Repair the live intervals.
        LIS->repairIntervalsInRange(&MBB, First, Last, OrigRegs);
        Changed = true;
      }

      return Changed;
    }

public:
    bool runOnMachineFunction(MachineFunction &MF) override {
      TII = MF.getSubtarget<PPCSubtarget>().getInstrInfo();
      LIS = &getAnalysis<LiveIntervals>();

      bool Changed = false;

      for (MachineFunction::iterator I = MF.begin(); I != MF.end();) {
        MachineBasicBlock &B = *I++;
        if (processBlock(B))
          Changed = true;
      }

      return Changed;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LiveIntervals>();
      AU.addPreserved<LiveIntervals>();
      AU.addRequired<SlotIndexes>();
      AU.addPreserved<SlotIndexes>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS_BEGIN(PPCTLSDynamicCall, DEBUG_TYPE,
                      "PowerPC TLS Dynamic Call Fixup", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_END(PPCTLSDynamicCall, DEBUG_TYPE,
                    "PowerPC TLS Dynamic Call Fixup", false, false)

char PPCTLSDynamicCall::ID = 0;
FunctionPass*
llvm::createPPCTLSDynamicCallPass() { return new PPCTLSDynamicCall(); }
