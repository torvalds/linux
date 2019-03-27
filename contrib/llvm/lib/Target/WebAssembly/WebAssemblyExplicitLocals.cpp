//===-- WebAssemblyExplicitLocals.cpp - Make Locals Explicit --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file converts any remaining registers into WebAssembly locals.
///
/// After register stackification and register coloring, convert non-stackified
/// registers into locals, inserting explicit local.get and local.set
/// instructions.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-explicit-locals"

// A command-line option to disable this pass, and keep implicit locals
// for the purpose of testing with lit/llc ONLY.
// This produces output which is not valid WebAssembly, and is not supported
// by assemblers/disassemblers and other MC based tools.
static cl::opt<bool> WasmDisableExplicitLocals(
    "wasm-disable-explicit-locals", cl::Hidden,
    cl::desc("WebAssembly: output implicit locals in"
             " instruction output for test purposes only."),
    cl::init(false));

namespace {
class WebAssemblyExplicitLocals final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Explicit Locals";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<MachineBlockFrequencyInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyExplicitLocals() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyExplicitLocals::ID = 0;
INITIALIZE_PASS(WebAssemblyExplicitLocals, DEBUG_TYPE,
                "Convert registers to WebAssembly locals", false, false)

FunctionPass *llvm::createWebAssemblyExplicitLocals() {
  return new WebAssemblyExplicitLocals();
}

/// Return a local id number for the given register, assigning it a new one
/// if it doesn't yet have one.
static unsigned getLocalId(DenseMap<unsigned, unsigned> &Reg2Local,
                           unsigned &CurLocal, unsigned Reg) {
  auto P = Reg2Local.insert(std::make_pair(Reg, CurLocal));
  if (P.second)
    ++CurLocal;
  return P.first->second;
}

/// Get the appropriate drop opcode for the given register class.
static unsigned getDropOpcode(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return WebAssembly::DROP_I32;
  if (RC == &WebAssembly::I64RegClass)
    return WebAssembly::DROP_I64;
  if (RC == &WebAssembly::F32RegClass)
    return WebAssembly::DROP_F32;
  if (RC == &WebAssembly::F64RegClass)
    return WebAssembly::DROP_F64;
  if (RC == &WebAssembly::V128RegClass)
    return WebAssembly::DROP_V128;
  if (RC == &WebAssembly::EXCEPT_REFRegClass)
    return WebAssembly::DROP_EXCEPT_REF;
  llvm_unreachable("Unexpected register class");
}

/// Get the appropriate local.get opcode for the given register class.
static unsigned getGetLocalOpcode(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return WebAssembly::LOCAL_GET_I32;
  if (RC == &WebAssembly::I64RegClass)
    return WebAssembly::LOCAL_GET_I64;
  if (RC == &WebAssembly::F32RegClass)
    return WebAssembly::LOCAL_GET_F32;
  if (RC == &WebAssembly::F64RegClass)
    return WebAssembly::LOCAL_GET_F64;
  if (RC == &WebAssembly::V128RegClass)
    return WebAssembly::LOCAL_GET_V128;
  if (RC == &WebAssembly::EXCEPT_REFRegClass)
    return WebAssembly::LOCAL_GET_EXCEPT_REF;
  llvm_unreachable("Unexpected register class");
}

/// Get the appropriate local.set opcode for the given register class.
static unsigned getSetLocalOpcode(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return WebAssembly::LOCAL_SET_I32;
  if (RC == &WebAssembly::I64RegClass)
    return WebAssembly::LOCAL_SET_I64;
  if (RC == &WebAssembly::F32RegClass)
    return WebAssembly::LOCAL_SET_F32;
  if (RC == &WebAssembly::F64RegClass)
    return WebAssembly::LOCAL_SET_F64;
  if (RC == &WebAssembly::V128RegClass)
    return WebAssembly::LOCAL_SET_V128;
  if (RC == &WebAssembly::EXCEPT_REFRegClass)
    return WebAssembly::LOCAL_SET_EXCEPT_REF;
  llvm_unreachable("Unexpected register class");
}

/// Get the appropriate local.tee opcode for the given register class.
static unsigned getTeeLocalOpcode(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return WebAssembly::LOCAL_TEE_I32;
  if (RC == &WebAssembly::I64RegClass)
    return WebAssembly::LOCAL_TEE_I64;
  if (RC == &WebAssembly::F32RegClass)
    return WebAssembly::LOCAL_TEE_F32;
  if (RC == &WebAssembly::F64RegClass)
    return WebAssembly::LOCAL_TEE_F64;
  if (RC == &WebAssembly::V128RegClass)
    return WebAssembly::LOCAL_TEE_V128;
  if (RC == &WebAssembly::EXCEPT_REFRegClass)
    return WebAssembly::LOCAL_TEE_EXCEPT_REF;
  llvm_unreachable("Unexpected register class");
}

/// Get the type associated with the given register class.
static MVT typeForRegClass(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return MVT::i32;
  if (RC == &WebAssembly::I64RegClass)
    return MVT::i64;
  if (RC == &WebAssembly::F32RegClass)
    return MVT::f32;
  if (RC == &WebAssembly::F64RegClass)
    return MVT::f64;
  if (RC == &WebAssembly::V128RegClass)
    return MVT::v16i8;
  if (RC == &WebAssembly::EXCEPT_REFRegClass)
    return MVT::ExceptRef;
  llvm_unreachable("unrecognized register class");
}

/// Given a MachineOperand of a stackified vreg, return the instruction at the
/// start of the expression tree.
static MachineInstr *findStartOfTree(MachineOperand &MO,
                                     MachineRegisterInfo &MRI,
                                     WebAssemblyFunctionInfo &MFI) {
  unsigned Reg = MO.getReg();
  assert(MFI.isVRegStackified(Reg));
  MachineInstr *Def = MRI.getVRegDef(Reg);

  // Find the first stackified use and proceed from there.
  for (MachineOperand &DefMO : Def->explicit_uses()) {
    if (!DefMO.isReg())
      continue;
    return findStartOfTree(DefMO, MRI, MFI);
  }

  // If there were no stackified uses, we've reached the start.
  return Def;
}

bool WebAssemblyExplicitLocals::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Make Locals Explicit **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  // Disable this pass if directed to do so.
  if (WasmDisableExplicitLocals)
    return false;

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const auto *TII = MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  // Map non-stackified virtual registers to their local ids.
  DenseMap<unsigned, unsigned> Reg2Local;

  // Handle ARGUMENTS first to ensure that they get the designated numbers.
  for (MachineBasicBlock::iterator I = MF.begin()->begin(),
                                   E = MF.begin()->end();
       I != E;) {
    MachineInstr &MI = *I++;
    if (!WebAssembly::isArgument(MI))
      break;
    unsigned Reg = MI.getOperand(0).getReg();
    assert(!MFI.isVRegStackified(Reg));
    Reg2Local[Reg] = static_cast<unsigned>(MI.getOperand(1).getImm());
    MI.eraseFromParent();
    Changed = true;
  }

  // Start assigning local numbers after the last parameter.
  unsigned CurLocal = static_cast<unsigned>(MFI.getParams().size());

  // Precompute the set of registers that are unused, so that we can insert
  // drops to their defs.
  BitVector UseEmpty(MRI.getNumVirtRegs());
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I)
    UseEmpty[I] = MRI.use_empty(TargetRegisterInfo::index2VirtReg(I));

  // Visit each instruction in the function.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;
      assert(!WebAssembly::isArgument(MI));

      if (MI.isDebugInstr() || MI.isLabel())
        continue;

      // Replace tee instructions with local.tee. The difference is that tee
      // instructions have two defs, while local.tee instructions have one def
      // and an index of a local to write to.
      if (WebAssembly::isTee(MI)) {
        assert(MFI.isVRegStackified(MI.getOperand(0).getReg()));
        assert(!MFI.isVRegStackified(MI.getOperand(1).getReg()));
        unsigned OldReg = MI.getOperand(2).getReg();
        const TargetRegisterClass *RC = MRI.getRegClass(OldReg);

        // Stackify the input if it isn't stackified yet.
        if (!MFI.isVRegStackified(OldReg)) {
          unsigned LocalId = getLocalId(Reg2Local, CurLocal, OldReg);
          unsigned NewReg = MRI.createVirtualRegister(RC);
          unsigned Opc = getGetLocalOpcode(RC);
          BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(Opc), NewReg)
              .addImm(LocalId);
          MI.getOperand(2).setReg(NewReg);
          MFI.stackifyVReg(NewReg);
        }

        // Replace the TEE with a LOCAL_TEE.
        unsigned LocalId =
            getLocalId(Reg2Local, CurLocal, MI.getOperand(1).getReg());
        unsigned Opc = getTeeLocalOpcode(RC);
        BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(Opc),
                MI.getOperand(0).getReg())
            .addImm(LocalId)
            .addReg(MI.getOperand(2).getReg());

        MI.eraseFromParent();
        Changed = true;
        continue;
      }

      // Insert local.sets for any defs that aren't stackified yet. Currently
      // we handle at most one def.
      assert(MI.getDesc().getNumDefs() <= 1);
      if (MI.getDesc().getNumDefs() == 1) {
        unsigned OldReg = MI.getOperand(0).getReg();
        if (!MFI.isVRegStackified(OldReg)) {
          const TargetRegisterClass *RC = MRI.getRegClass(OldReg);
          unsigned NewReg = MRI.createVirtualRegister(RC);
          auto InsertPt = std::next(MachineBasicBlock::iterator(&MI));
          if (MI.getOpcode() == WebAssembly::IMPLICIT_DEF) {
            MI.eraseFromParent();
            Changed = true;
            continue;
          }
          if (UseEmpty[TargetRegisterInfo::virtReg2Index(OldReg)]) {
            unsigned Opc = getDropOpcode(RC);
            MachineInstr *Drop =
                BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII->get(Opc))
                    .addReg(NewReg);
            // After the drop instruction, this reg operand will not be used
            Drop->getOperand(0).setIsKill();
          } else {
            unsigned LocalId = getLocalId(Reg2Local, CurLocal, OldReg);
            unsigned Opc = getSetLocalOpcode(RC);
            BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII->get(Opc))
                .addImm(LocalId)
                .addReg(NewReg);
          }
          MI.getOperand(0).setReg(NewReg);
          // This register operand of the original instruction is now being used
          // by the inserted drop or local.set instruction, so make it not dead
          // yet.
          MI.getOperand(0).setIsDead(false);
          MFI.stackifyVReg(NewReg);
          Changed = true;
        }
      }

      // Insert local.gets for any uses that aren't stackified yet.
      MachineInstr *InsertPt = &MI;
      for (MachineOperand &MO : reverse(MI.explicit_uses())) {
        if (!MO.isReg())
          continue;

        unsigned OldReg = MO.getReg();

        // Inline asm may have a def in the middle of the operands. Our contract
        // with inline asm register operands is to provide local indices as
        // immediates.
        if (MO.isDef()) {
          assert(MI.getOpcode() == TargetOpcode::INLINEASM);
          unsigned LocalId = getLocalId(Reg2Local, CurLocal, OldReg);
          // If this register operand is tied to another operand, we can't
          // change it to an immediate. Untie it first.
          MI.untieRegOperand(MI.getOperandNo(&MO));
          MO.ChangeToImmediate(LocalId);
          continue;
        }

        // If we see a stackified register, prepare to insert subsequent
        // local.gets before the start of its tree.
        if (MFI.isVRegStackified(OldReg)) {
          InsertPt = findStartOfTree(MO, MRI, MFI);
          continue;
        }

        // Our contract with inline asm register operands is to provide local
        // indices as immediates.
        if (MI.getOpcode() == TargetOpcode::INLINEASM) {
          unsigned LocalId = getLocalId(Reg2Local, CurLocal, OldReg);
          // Untie it first if this reg operand is tied to another operand.
          MI.untieRegOperand(MI.getOperandNo(&MO));
          MO.ChangeToImmediate(LocalId);
          continue;
        }

        // Insert a local.get.
        unsigned LocalId = getLocalId(Reg2Local, CurLocal, OldReg);
        const TargetRegisterClass *RC = MRI.getRegClass(OldReg);
        unsigned NewReg = MRI.createVirtualRegister(RC);
        unsigned Opc = getGetLocalOpcode(RC);
        InsertPt =
            BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII->get(Opc), NewReg)
                .addImm(LocalId);
        MO.setReg(NewReg);
        MFI.stackifyVReg(NewReg);
        Changed = true;
      }

      // Coalesce and eliminate COPY instructions.
      if (WebAssembly::isCopy(MI)) {
        MRI.replaceRegWith(MI.getOperand(1).getReg(),
                           MI.getOperand(0).getReg());
        MI.eraseFromParent();
        Changed = true;
      }
    }
  }

  // Define the locals.
  // TODO: Sort the locals for better compression.
  MFI.setNumLocals(CurLocal - MFI.getParams().size());
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(I);
    auto RL = Reg2Local.find(Reg);
    if (RL == Reg2Local.end() || RL->second < MFI.getParams().size())
      continue;

    MFI.setLocal(RL->second - MFI.getParams().size(),
                 typeForRegClass(MRI.getRegClass(Reg)));
    Changed = true;
  }

#ifndef NDEBUG
  // Assert that all registers have been stackified at this point.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isLabel())
        continue;
      for (const MachineOperand &MO : MI.explicit_operands()) {
        assert(
            (!MO.isReg() || MRI.use_empty(MO.getReg()) ||
             MFI.isVRegStackified(MO.getReg())) &&
            "WebAssemblyExplicitLocals failed to stackify a register operand");
      }
    }
  }
#endif

  return Changed;
}
