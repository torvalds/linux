//===- HexagonGenPredicate.cpp --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <map>
#include <queue>
#include <set>
#include <utility>

#define DEBUG_TYPE "gen-pred"

using namespace llvm;

namespace llvm {

  void initializeHexagonGenPredicatePass(PassRegistry& Registry);
  FunctionPass *createHexagonGenPredicate();

} // end namespace llvm

namespace {

  struct Register {
    unsigned R, S;

    Register(unsigned r = 0, unsigned s = 0) : R(r), S(s) {}
    Register(const MachineOperand &MO) : R(MO.getReg()), S(MO.getSubReg()) {}

    bool operator== (const Register &Reg) const {
      return R == Reg.R && S == Reg.S;
    }

    bool operator< (const Register &Reg) const {
      return R < Reg.R || (R == Reg.R && S < Reg.S);
    }
  };

  struct PrintRegister {
    friend raw_ostream &operator<< (raw_ostream &OS, const PrintRegister &PR);

    PrintRegister(Register R, const TargetRegisterInfo &I) : Reg(R), TRI(I) {}

  private:
    Register Reg;
    const TargetRegisterInfo &TRI;
  };

  raw_ostream &operator<< (raw_ostream &OS, const PrintRegister &PR)
    LLVM_ATTRIBUTE_UNUSED;
  raw_ostream &operator<< (raw_ostream &OS, const PrintRegister &PR) {
    return OS << printReg(PR.Reg.R, &PR.TRI, PR.Reg.S);
  }

  class HexagonGenPredicate : public MachineFunctionPass {
  public:
    static char ID;

    HexagonGenPredicate() : MachineFunctionPass(ID) {
      initializeHexagonGenPredicatePass(*PassRegistry::getPassRegistry());
    }

    StringRef getPassName() const override {
      return "Hexagon generate predicate operations";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTree>();
      AU.addPreserved<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    using VectOfInst = SetVector<MachineInstr *>;
    using SetOfReg = std::set<Register>;
    using RegToRegMap = std::map<Register, Register>;

    const HexagonInstrInfo *TII = nullptr;
    const HexagonRegisterInfo *TRI = nullptr;
    MachineRegisterInfo *MRI = nullptr;
    SetOfReg PredGPRs;
    VectOfInst PUsers;
    RegToRegMap G2P;

    bool isPredReg(unsigned R);
    void collectPredicateGPR(MachineFunction &MF);
    void processPredicateGPR(const Register &Reg);
    unsigned getPredForm(unsigned Opc);
    bool isConvertibleToPredForm(const MachineInstr *MI);
    bool isScalarCmp(unsigned Opc);
    bool isScalarPred(Register PredReg);
    Register getPredRegFor(const Register &Reg);
    bool convertToPredForm(MachineInstr *MI);
    bool eliminatePredCopies(MachineFunction &MF);
  };

} // end anonymous namespace

char HexagonGenPredicate::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonGenPredicate, "hexagon-gen-pred",
  "Hexagon generate predicate operations", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(HexagonGenPredicate, "hexagon-gen-pred",
  "Hexagon generate predicate operations", false, false)

bool HexagonGenPredicate::isPredReg(unsigned R) {
  if (!TargetRegisterInfo::isVirtualRegister(R))
    return false;
  const TargetRegisterClass *RC = MRI->getRegClass(R);
  return RC == &Hexagon::PredRegsRegClass;
}

unsigned HexagonGenPredicate::getPredForm(unsigned Opc) {
  using namespace Hexagon;

  switch (Opc) {
    case A2_and:
    case A2_andp:
      return C2_and;
    case A4_andn:
    case A4_andnp:
      return C2_andn;
    case M4_and_and:
      return C4_and_and;
    case M4_and_andn:
      return C4_and_andn;
    case M4_and_or:
      return C4_and_or;

    case A2_or:
    case A2_orp:
      return C2_or;
    case A4_orn:
    case A4_ornp:
      return C2_orn;
    case M4_or_and:
      return C4_or_and;
    case M4_or_andn:
      return C4_or_andn;
    case M4_or_or:
      return C4_or_or;

    case A2_xor:
    case A2_xorp:
      return C2_xor;

    case C2_tfrrp:
      return COPY;
  }
  // The opcode corresponding to 0 is TargetOpcode::PHI. We can use 0 here
  // to denote "none", but we need to make sure that none of the valid opcodes
  // that we return will ever be 0.
  static_assert(PHI == 0, "Use different value for <none>");
  return 0;
}

bool HexagonGenPredicate::isConvertibleToPredForm(const MachineInstr *MI) {
  unsigned Opc = MI->getOpcode();
  if (getPredForm(Opc) != 0)
    return true;

  // Comparisons against 0 are also convertible. This does not apply to
  // A4_rcmpeqi or A4_rcmpneqi, since they produce values 0 or 1, which
  // may not match the value that the predicate register would have if
  // it was converted to a predicate form.
  switch (Opc) {
    case Hexagon::C2_cmpeqi:
    case Hexagon::C4_cmpneqi:
      if (MI->getOperand(2).isImm() && MI->getOperand(2).getImm() == 0)
        return true;
      break;
  }
  return false;
}

void HexagonGenPredicate::collectPredicateGPR(MachineFunction &MF) {
  for (MachineFunction::iterator A = MF.begin(), Z = MF.end(); A != Z; ++A) {
    MachineBasicBlock &B = *A;
    for (MachineBasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I) {
      MachineInstr *MI = &*I;
      unsigned Opc = MI->getOpcode();
      switch (Opc) {
        case Hexagon::C2_tfrpr:
        case TargetOpcode::COPY:
          if (isPredReg(MI->getOperand(1).getReg())) {
            Register RD = MI->getOperand(0);
            if (TargetRegisterInfo::isVirtualRegister(RD.R))
              PredGPRs.insert(RD);
          }
          break;
      }
    }
  }
}

void HexagonGenPredicate::processPredicateGPR(const Register &Reg) {
  LLVM_DEBUG(dbgs() << __func__ << ": " << printReg(Reg.R, TRI, Reg.S) << "\n");
  using use_iterator = MachineRegisterInfo::use_iterator;

  use_iterator I = MRI->use_begin(Reg.R), E = MRI->use_end();
  if (I == E) {
    LLVM_DEBUG(dbgs() << "Dead reg: " << printReg(Reg.R, TRI, Reg.S) << '\n');
    MachineInstr *DefI = MRI->getVRegDef(Reg.R);
    DefI->eraseFromParent();
    return;
  }

  for (; I != E; ++I) {
    MachineInstr *UseI = I->getParent();
    if (isConvertibleToPredForm(UseI))
      PUsers.insert(UseI);
  }
}

Register HexagonGenPredicate::getPredRegFor(const Register &Reg) {
  // Create a predicate register for a given Reg. The newly created register
  // will have its value copied from Reg, so that it can be later used as
  // an operand in other instructions.
  assert(TargetRegisterInfo::isVirtualRegister(Reg.R));
  RegToRegMap::iterator F = G2P.find(Reg);
  if (F != G2P.end())
    return F->second;

  LLVM_DEBUG(dbgs() << __func__ << ": " << PrintRegister(Reg, *TRI));
  MachineInstr *DefI = MRI->getVRegDef(Reg.R);
  assert(DefI);
  unsigned Opc = DefI->getOpcode();
  if (Opc == Hexagon::C2_tfrpr || Opc == TargetOpcode::COPY) {
    assert(DefI->getOperand(0).isDef() && DefI->getOperand(1).isUse());
    Register PR = DefI->getOperand(1);
    G2P.insert(std::make_pair(Reg, PR));
    LLVM_DEBUG(dbgs() << " -> " << PrintRegister(PR, *TRI) << '\n');
    return PR;
  }

  MachineBasicBlock &B = *DefI->getParent();
  DebugLoc DL = DefI->getDebugLoc();
  const TargetRegisterClass *PredRC = &Hexagon::PredRegsRegClass;
  unsigned NewPR = MRI->createVirtualRegister(PredRC);

  // For convertible instructions, do not modify them, so that they can
  // be converted later.  Generate a copy from Reg to NewPR.
  if (isConvertibleToPredForm(DefI)) {
    MachineBasicBlock::iterator DefIt = DefI;
    BuildMI(B, std::next(DefIt), DL, TII->get(TargetOpcode::COPY), NewPR)
      .addReg(Reg.R, 0, Reg.S);
    G2P.insert(std::make_pair(Reg, Register(NewPR)));
    LLVM_DEBUG(dbgs() << " -> !" << PrintRegister(Register(NewPR), *TRI)
                      << '\n');
    return Register(NewPR);
  }

  llvm_unreachable("Invalid argument");
}

bool HexagonGenPredicate::isScalarCmp(unsigned Opc) {
  switch (Opc) {
    case Hexagon::C2_cmpeq:
    case Hexagon::C2_cmpgt:
    case Hexagon::C2_cmpgtu:
    case Hexagon::C2_cmpeqp:
    case Hexagon::C2_cmpgtp:
    case Hexagon::C2_cmpgtup:
    case Hexagon::C2_cmpeqi:
    case Hexagon::C2_cmpgti:
    case Hexagon::C2_cmpgtui:
    case Hexagon::C2_cmpgei:
    case Hexagon::C2_cmpgeui:
    case Hexagon::C4_cmpneqi:
    case Hexagon::C4_cmpltei:
    case Hexagon::C4_cmplteui:
    case Hexagon::C4_cmpneq:
    case Hexagon::C4_cmplte:
    case Hexagon::C4_cmplteu:
    case Hexagon::A4_cmpbeq:
    case Hexagon::A4_cmpbeqi:
    case Hexagon::A4_cmpbgtu:
    case Hexagon::A4_cmpbgtui:
    case Hexagon::A4_cmpbgt:
    case Hexagon::A4_cmpbgti:
    case Hexagon::A4_cmpheq:
    case Hexagon::A4_cmphgt:
    case Hexagon::A4_cmphgtu:
    case Hexagon::A4_cmpheqi:
    case Hexagon::A4_cmphgti:
    case Hexagon::A4_cmphgtui:
      return true;
  }
  return false;
}

bool HexagonGenPredicate::isScalarPred(Register PredReg) {
  std::queue<Register> WorkQ;
  WorkQ.push(PredReg);

  while (!WorkQ.empty()) {
    Register PR = WorkQ.front();
    WorkQ.pop();
    const MachineInstr *DefI = MRI->getVRegDef(PR.R);
    if (!DefI)
      return false;
    unsigned DefOpc = DefI->getOpcode();
    switch (DefOpc) {
      case TargetOpcode::COPY: {
        const TargetRegisterClass *PredRC = &Hexagon::PredRegsRegClass;
        if (MRI->getRegClass(PR.R) != PredRC)
          return false;
        // If it is a copy between two predicate registers, fall through.
        LLVM_FALLTHROUGH;
      }
      case Hexagon::C2_and:
      case Hexagon::C2_andn:
      case Hexagon::C4_and_and:
      case Hexagon::C4_and_andn:
      case Hexagon::C4_and_or:
      case Hexagon::C2_or:
      case Hexagon::C2_orn:
      case Hexagon::C4_or_and:
      case Hexagon::C4_or_andn:
      case Hexagon::C4_or_or:
      case Hexagon::C4_or_orn:
      case Hexagon::C2_xor:
        // Add operands to the queue.
        for (const MachineOperand &MO : DefI->operands())
          if (MO.isReg() && MO.isUse())
            WorkQ.push(Register(MO.getReg()));
        break;

      // All non-vector compares are ok, everything else is bad.
      default:
        return isScalarCmp(DefOpc);
    }
  }

  return true;
}

bool HexagonGenPredicate::convertToPredForm(MachineInstr *MI) {
  LLVM_DEBUG(dbgs() << __func__ << ": " << MI << " " << *MI);

  unsigned Opc = MI->getOpcode();
  assert(isConvertibleToPredForm(MI));
  unsigned NumOps = MI->getNumOperands();
  for (unsigned i = 0; i < NumOps; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.isUse())
      continue;
    Register Reg(MO);
    if (Reg.S && Reg.S != Hexagon::isub_lo)
      return false;
    if (!PredGPRs.count(Reg))
      return false;
  }

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  unsigned NewOpc = getPredForm(Opc);
  // Special case for comparisons against 0.
  if (NewOpc == 0) {
    switch (Opc) {
      case Hexagon::C2_cmpeqi:
        NewOpc = Hexagon::C2_not;
        break;
      case Hexagon::C4_cmpneqi:
        NewOpc = TargetOpcode::COPY;
        break;
      default:
        return false;
    }

    // If it's a scalar predicate register, then all bits in it are
    // the same. Otherwise, to determine whether all bits are 0 or not
    // we would need to use any8.
    Register PR = getPredRegFor(MI->getOperand(1));
    if (!isScalarPred(PR))
      return false;
    // This will skip the immediate argument when creating the predicate
    // version instruction.
    NumOps = 2;
  }

  // Some sanity: check that def is in operand #0.
  MachineOperand &Op0 = MI->getOperand(0);
  assert(Op0.isDef());
  Register OutR(Op0);

  // Don't use getPredRegFor, since it will create an association between
  // the argument and a created predicate register (i.e. it will insert a
  // copy if a new predicate register is created).
  const TargetRegisterClass *PredRC = &Hexagon::PredRegsRegClass;
  Register NewPR = MRI->createVirtualRegister(PredRC);
  MachineInstrBuilder MIB = BuildMI(B, MI, DL, TII->get(NewOpc), NewPR.R);

  // Add predicate counterparts of the GPRs.
  for (unsigned i = 1; i < NumOps; ++i) {
    Register GPR = MI->getOperand(i);
    Register Pred = getPredRegFor(GPR);
    MIB.addReg(Pred.R, 0, Pred.S);
  }
  LLVM_DEBUG(dbgs() << "generated: " << *MIB);

  // Generate a copy-out: NewGPR = NewPR, and replace all uses of OutR
  // with NewGPR.
  const TargetRegisterClass *RC = MRI->getRegClass(OutR.R);
  unsigned NewOutR = MRI->createVirtualRegister(RC);
  BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), NewOutR)
    .addReg(NewPR.R, 0, NewPR.S);
  MRI->replaceRegWith(OutR.R, NewOutR);
  MI->eraseFromParent();

  // If the processed instruction was C2_tfrrp (i.e. Rn = Pm; Pk = Rn),
  // then the output will be a predicate register.  Do not visit the
  // users of it.
  if (!isPredReg(NewOutR)) {
    Register R(NewOutR);
    PredGPRs.insert(R);
    processPredicateGPR(R);
  }
  return true;
}

bool HexagonGenPredicate::eliminatePredCopies(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << __func__ << "\n");
  const TargetRegisterClass *PredRC = &Hexagon::PredRegsRegClass;
  bool Changed = false;
  VectOfInst Erase;

  // First, replace copies
  //   IntR = PredR1
  //   PredR2 = IntR
  // with
  //   PredR2 = PredR1
  // Such sequences can be generated when a copy-into-pred is generated from
  // a gpr register holding a result of a convertible instruction. After
  // the convertible instruction is converted, its predicate result will be
  // copied back into the original gpr.

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != TargetOpcode::COPY)
        continue;
      Register DR = MI.getOperand(0);
      Register SR = MI.getOperand(1);
      if (!TargetRegisterInfo::isVirtualRegister(DR.R))
        continue;
      if (!TargetRegisterInfo::isVirtualRegister(SR.R))
        continue;
      if (MRI->getRegClass(DR.R) != PredRC)
        continue;
      if (MRI->getRegClass(SR.R) != PredRC)
        continue;
      assert(!DR.S && !SR.S && "Unexpected subregister");
      MRI->replaceRegWith(DR.R, SR.R);
      Erase.insert(&MI);
      Changed = true;
    }
  }

  for (VectOfInst::iterator I = Erase.begin(), E = Erase.end(); I != E; ++I)
    (*I)->eraseFromParent();

  return Changed;
}

bool HexagonGenPredicate::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TII = MF.getSubtarget<HexagonSubtarget>().getInstrInfo();
  TRI = MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  MRI = &MF.getRegInfo();
  PredGPRs.clear();
  PUsers.clear();
  G2P.clear();

  bool Changed = false;
  collectPredicateGPR(MF);
  for (SetOfReg::iterator I = PredGPRs.begin(), E = PredGPRs.end(); I != E; ++I)
    processPredicateGPR(*I);

  bool Again;
  do {
    Again = false;
    VectOfInst Processed, Copy;

    using iterator = VectOfInst::iterator;

    Copy = PUsers;
    for (iterator I = Copy.begin(), E = Copy.end(); I != E; ++I) {
      MachineInstr *MI = *I;
      bool Done = convertToPredForm(MI);
      if (Done) {
        Processed.insert(MI);
        Again = true;
      }
    }
    Changed |= Again;

    auto Done = [Processed] (MachineInstr *MI) -> bool {
      return Processed.count(MI);
    };
    PUsers.remove_if(Done);
  } while (Again);

  Changed |= eliminatePredCopies(MF);
  return Changed;
}

FunctionPass *llvm::createHexagonGenPredicate() {
  return new HexagonGenPredicate();
}
