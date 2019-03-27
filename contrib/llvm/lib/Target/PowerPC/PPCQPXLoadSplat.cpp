//===----- PPCQPXLoadSplat.cpp - QPX Load Splat Simplification ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The QPX vector registers overlay the scalar floating-point registers, and
// any scalar floating-point loads splat their value across all vector lanes.
// Thus, if we have a scalar load followed by a splat, we can remove the splat
// (i.e. replace the load with a load-and-splat pseudo instruction).
//
// This pass must run after anything that might do store-to-load forwarding.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "ppc-qpx-load-splat"

STATISTIC(NumSimplified, "Number of QPX load splats simplified");

namespace llvm {
  void initializePPCQPXLoadSplatPass(PassRegistry&);
}

namespace {
  struct PPCQPXLoadSplat : public MachineFunctionPass {
    static char ID;
    PPCQPXLoadSplat() : MachineFunctionPass(ID) {
      initializePPCQPXLoadSplatPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &Fn) override;

    StringRef getPassName() const override {
      return "PowerPC QPX Load Splat Simplification";
    }
  };
  char PPCQPXLoadSplat::ID = 0;
}

INITIALIZE_PASS(PPCQPXLoadSplat, "ppc-qpx-load-splat",
                "PowerPC QPX Load Splat Simplification",
                false, false)

FunctionPass *llvm::createPPCQPXLoadSplatPass() {
  return new PPCQPXLoadSplat();
}

bool PPCQPXLoadSplat::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  bool MadeChange = false;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  for (auto MFI = MF.begin(), MFIE = MF.end(); MFI != MFIE; ++MFI) {
    MachineBasicBlock *MBB = &*MFI;
    SmallVector<MachineInstr *, 4> Splats;

    for (auto MBBI = MBB->rbegin(); MBBI != MBB->rend(); ++MBBI) {
      MachineInstr *MI = &*MBBI;

      if (MI->hasUnmodeledSideEffects() || MI->isCall()) {
        Splats.clear();
        continue;
      }

      // We're looking for a sequence like this:
      // %f0 = LFD 0, killed %x3, implicit-def %qf0; mem:LD8[%a](tbaa=!2)
      // %qf1 = QVESPLATI killed %qf0, 0, implicit %rm

      for (auto SI = Splats.begin(); SI != Splats.end();) {
        MachineInstr *SMI = *SI;
        unsigned SplatReg = SMI->getOperand(0).getReg();
        unsigned SrcReg = SMI->getOperand(1).getReg();

        if (MI->modifiesRegister(SrcReg, TRI)) {
          switch (MI->getOpcode()) {
          default:
            SI = Splats.erase(SI);
            continue;
          case PPC::LFS:
          case PPC::LFD:
          case PPC::LFSU:
          case PPC::LFDU:
          case PPC::LFSUX:
          case PPC::LFDUX:
          case PPC::LFSX:
          case PPC::LFDX:
          case PPC::LFIWAX:
          case PPC::LFIWZX:
            if (SplatReg != SrcReg) {
              // We need to change the load to define the scalar subregister of
              // the QPX splat source register.
              unsigned SubRegIndex =
                TRI->getSubRegIndex(SrcReg, MI->getOperand(0).getReg());
              unsigned SplatSubReg = TRI->getSubReg(SplatReg, SubRegIndex);

              // Substitute both the explicit defined register, and also the
              // implicit def of the containing QPX register.
              MI->getOperand(0).setReg(SplatSubReg);
              MI->substituteRegister(SrcReg, SplatReg, 0, *TRI);
            }

            SI = Splats.erase(SI);

            // If SMI is directly after MI, then MBBI's base iterator is
            // pointing at SMI.  Adjust MBBI around the call to erase SMI to
            // avoid invalidating MBBI.
            ++MBBI;
            SMI->eraseFromParent();
            --MBBI;

            ++NumSimplified;
            MadeChange = true;
            continue;
          }
        }

        // If this instruction defines the splat register, then we cannot move
        // the previous definition above it. If it reads from the splat
        // register, then it must already be alive from some previous
        // definition, and if the splat register is different from the source
        // register, then this definition must not be the load for which we're
        // searching.
        if (MI->modifiesRegister(SplatReg, TRI) ||
            (SrcReg != SplatReg &&
             MI->readsRegister(SplatReg, TRI))) {
          SI = Splats.erase(SI);
          continue;
        }

        ++SI;
      }

      if (MI->getOpcode() != PPC::QVESPLATI &&
          MI->getOpcode() != PPC::QVESPLATIs &&
          MI->getOpcode() != PPC::QVESPLATIb)
        continue;
      if (MI->getOperand(2).getImm() != 0)
        continue;

      // If there are other uses of the scalar value after this, replacing
      // those uses might be non-trivial.
      if (!MI->getOperand(1).isKill())
        continue;

      Splats.push_back(MI);
    }
  }

  return MadeChange;
}
