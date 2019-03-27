//===--- X86DomainReassignment.cpp - Selectively switch register classes---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass attempts to find instruction chains (closures) in one domain,
// and convert them to equivalent instructions in a different domain,
// if profitable.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Printable.h"
#include <bitset>

using namespace llvm;

#define DEBUG_TYPE "x86-domain-reassignment"

STATISTIC(NumClosuresConverted, "Number of closures converted by the pass");

static cl::opt<bool> DisableX86DomainReassignment(
    "disable-x86-domain-reassignment", cl::Hidden,
    cl::desc("X86: Disable Virtual Register Reassignment."), cl::init(false));

namespace {
enum RegDomain { NoDomain = -1, GPRDomain, MaskDomain, OtherDomain, NumDomains };

static bool isGPR(const TargetRegisterClass *RC) {
  return X86::GR64RegClass.hasSubClassEq(RC) ||
         X86::GR32RegClass.hasSubClassEq(RC) ||
         X86::GR16RegClass.hasSubClassEq(RC) ||
         X86::GR8RegClass.hasSubClassEq(RC);
}

static bool isMask(const TargetRegisterClass *RC,
                   const TargetRegisterInfo *TRI) {
  return X86::VK16RegClass.hasSubClassEq(RC);
}

static RegDomain getDomain(const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) {
  if (isGPR(RC))
    return GPRDomain;
  if (isMask(RC, TRI))
    return MaskDomain;
  return OtherDomain;
}

/// Return a register class equivalent to \p SrcRC, in \p Domain.
static const TargetRegisterClass *getDstRC(const TargetRegisterClass *SrcRC,
                                           RegDomain Domain) {
  assert(Domain == MaskDomain && "add domain");
  if (X86::GR8RegClass.hasSubClassEq(SrcRC))
    return &X86::VK8RegClass;
  if (X86::GR16RegClass.hasSubClassEq(SrcRC))
    return &X86::VK16RegClass;
  if (X86::GR32RegClass.hasSubClassEq(SrcRC))
    return &X86::VK32RegClass;
  if (X86::GR64RegClass.hasSubClassEq(SrcRC))
    return &X86::VK64RegClass;
  llvm_unreachable("add register class");
  return nullptr;
}

/// Abstract Instruction Converter class.
class InstrConverterBase {
protected:
  unsigned SrcOpcode;

public:
  InstrConverterBase(unsigned SrcOpcode) : SrcOpcode(SrcOpcode) {}

  virtual ~InstrConverterBase() {}

  /// \returns true if \p MI is legal to convert.
  virtual bool isLegal(const MachineInstr *MI,
                       const TargetInstrInfo *TII) const {
    assert(MI->getOpcode() == SrcOpcode &&
           "Wrong instruction passed to converter");
    return true;
  }

  /// Applies conversion to \p MI.
  ///
  /// \returns true if \p MI is no longer need, and can be deleted.
  virtual bool convertInstr(MachineInstr *MI, const TargetInstrInfo *TII,
                            MachineRegisterInfo *MRI) const = 0;

  /// \returns the cost increment incurred by converting \p MI.
  virtual double getExtraCost(const MachineInstr *MI,
                              MachineRegisterInfo *MRI) const = 0;
};

/// An Instruction Converter which ignores the given instruction.
/// For example, PHI instructions can be safely ignored since only the registers
/// need to change.
class InstrIgnore : public InstrConverterBase {
public:
  InstrIgnore(unsigned SrcOpcode) : InstrConverterBase(SrcOpcode) {}

  bool convertInstr(MachineInstr *MI, const TargetInstrInfo *TII,
                    MachineRegisterInfo *MRI) const override {
    assert(isLegal(MI, TII) && "Cannot convert instruction");
    return false;
  }

  double getExtraCost(const MachineInstr *MI,
                      MachineRegisterInfo *MRI) const override {
    return 0;
  }
};

/// An Instruction Converter which replaces an instruction with another.
class InstrReplacer : public InstrConverterBase {
public:
  /// Opcode of the destination instruction.
  unsigned DstOpcode;

  InstrReplacer(unsigned SrcOpcode, unsigned DstOpcode)
      : InstrConverterBase(SrcOpcode), DstOpcode(DstOpcode) {}

  bool isLegal(const MachineInstr *MI,
               const TargetInstrInfo *TII) const override {
    if (!InstrConverterBase::isLegal(MI, TII))
      return false;
    // It's illegal to replace an instruction that implicitly defines a register
    // with an instruction that doesn't, unless that register dead.
    for (auto &MO : MI->implicit_operands())
      if (MO.isReg() && MO.isDef() && !MO.isDead() &&
          !TII->get(DstOpcode).hasImplicitDefOfPhysReg(MO.getReg()))
        return false;
    return true;
  }

  bool convertInstr(MachineInstr *MI, const TargetInstrInfo *TII,
                    MachineRegisterInfo *MRI) const override {
    assert(isLegal(MI, TII) && "Cannot convert instruction");
    MachineInstrBuilder Bld =
        BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), TII->get(DstOpcode));
    // Transfer explicit operands from original instruction. Implicit operands
    // are handled by BuildMI.
    for (auto &Op : MI->explicit_operands())
      Bld.add(Op);
    return true;
  }

  double getExtraCost(const MachineInstr *MI,
                      MachineRegisterInfo *MRI) const override {
    // Assuming instructions have the same cost.
    return 0;
  }
};

/// An Instruction Converter which replaces an instruction with another, and
/// adds a COPY from the new instruction's destination to the old one's.
class InstrReplacerDstCOPY : public InstrConverterBase {
public:
  unsigned DstOpcode;

  InstrReplacerDstCOPY(unsigned SrcOpcode, unsigned DstOpcode)
      : InstrConverterBase(SrcOpcode), DstOpcode(DstOpcode) {}

  bool convertInstr(MachineInstr *MI, const TargetInstrInfo *TII,
                    MachineRegisterInfo *MRI) const override {
    assert(isLegal(MI, TII) && "Cannot convert instruction");
    MachineBasicBlock *MBB = MI->getParent();
    auto &DL = MI->getDebugLoc();

    unsigned Reg = MRI->createVirtualRegister(
        TII->getRegClass(TII->get(DstOpcode), 0, MRI->getTargetRegisterInfo(),
                         *MBB->getParent()));
    MachineInstrBuilder Bld = BuildMI(*MBB, MI, DL, TII->get(DstOpcode), Reg);
    for (unsigned Idx = 1, End = MI->getNumOperands(); Idx < End; ++Idx)
      Bld.add(MI->getOperand(Idx));

    BuildMI(*MBB, MI, DL, TII->get(TargetOpcode::COPY))
        .add(MI->getOperand(0))
        .addReg(Reg);

    return true;
  }

  double getExtraCost(const MachineInstr *MI,
                      MachineRegisterInfo *MRI) const override {
    // Assuming instructions have the same cost, and that COPY is in the same
    // domain so it will be eliminated.
    return 0;
  }
};

/// An Instruction Converter for replacing COPY instructions.
class InstrCOPYReplacer : public InstrReplacer {
public:
  RegDomain DstDomain;

  InstrCOPYReplacer(unsigned SrcOpcode, RegDomain DstDomain, unsigned DstOpcode)
      : InstrReplacer(SrcOpcode, DstOpcode), DstDomain(DstDomain) {}

  bool isLegal(const MachineInstr *MI,
               const TargetInstrInfo *TII) const override {
    if (!InstrConverterBase::isLegal(MI, TII))
      return false;

    // Don't allow copies to/flow GR8/GR16 physical registers.
    // FIXME: Is there some better way to support this?
    unsigned DstReg = MI->getOperand(0).getReg();
    if (TargetRegisterInfo::isPhysicalRegister(DstReg) &&
        (X86::GR8RegClass.contains(DstReg) ||
         X86::GR16RegClass.contains(DstReg)))
      return false;
    unsigned SrcReg = MI->getOperand(1).getReg();
    if (TargetRegisterInfo::isPhysicalRegister(SrcReg) &&
        (X86::GR8RegClass.contains(SrcReg) ||
         X86::GR16RegClass.contains(SrcReg)))
      return false;

    return true;
  }

  double getExtraCost(const MachineInstr *MI,
                      MachineRegisterInfo *MRI) const override {
    assert(MI->getOpcode() == TargetOpcode::COPY && "Expected a COPY");

    for (auto &MO : MI->operands()) {
      // Physical registers will not be converted. Assume that converting the
      // COPY to the destination domain will eventually result in a actual
      // instruction.
      if (TargetRegisterInfo::isPhysicalRegister(MO.getReg()))
        return 1;

      RegDomain OpDomain = getDomain(MRI->getRegClass(MO.getReg()),
                                     MRI->getTargetRegisterInfo());
      // Converting a cross domain COPY to a same domain COPY should eliminate
      // an insturction
      if (OpDomain == DstDomain)
        return -1;
    }
    return 0;
  }
};

/// An Instruction Converter which replaces an instruction with a COPY.
class InstrReplaceWithCopy : public InstrConverterBase {
public:
  // Source instruction operand Index, to be used as the COPY source.
  unsigned SrcOpIdx;

  InstrReplaceWithCopy(unsigned SrcOpcode, unsigned SrcOpIdx)
      : InstrConverterBase(SrcOpcode), SrcOpIdx(SrcOpIdx) {}

  bool convertInstr(MachineInstr *MI, const TargetInstrInfo *TII,
                    MachineRegisterInfo *MRI) const override {
    assert(isLegal(MI, TII) && "Cannot convert instruction");
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
            TII->get(TargetOpcode::COPY))
        .add({MI->getOperand(0), MI->getOperand(SrcOpIdx)});
    return true;
  }

  double getExtraCost(const MachineInstr *MI,
                      MachineRegisterInfo *MRI) const override {
    return 0;
  }
};

// Key type to be used by the Instruction Converters map.
// A converter is identified by <destination domain, source opcode>
typedef std::pair<int, unsigned> InstrConverterBaseKeyTy;

typedef DenseMap<InstrConverterBaseKeyTy, InstrConverterBase *>
    InstrConverterBaseMap;

/// A closure is a set of virtual register representing all of the edges in
/// the closure, as well as all of the instructions connected by those edges.
///
/// A closure may encompass virtual registers in the same register bank that
/// have different widths. For example, it may contain 32-bit GPRs as well as
/// 64-bit GPRs.
///
/// A closure that computes an address (i.e. defines a virtual register that is
/// used in a memory operand) excludes the instructions that contain memory
/// operands using the address. Such an instruction will be included in a
/// different closure that manipulates the loaded or stored value.
class Closure {
private:
  /// Virtual registers in the closure.
  DenseSet<unsigned> Edges;

  /// Instructions in the closure.
  SmallVector<MachineInstr *, 8> Instrs;

  /// Domains which this closure can legally be reassigned to.
  std::bitset<NumDomains> LegalDstDomains;

  /// An ID to uniquely identify this closure, even when it gets
  /// moved around
  unsigned ID;

public:
  Closure(unsigned ID, std::initializer_list<RegDomain> LegalDstDomainList) : ID(ID) {
    for (RegDomain D : LegalDstDomainList)
      LegalDstDomains.set(D);
  }

  /// Mark this closure as illegal for reassignment to all domains.
  void setAllIllegal() { LegalDstDomains.reset(); }

  /// \returns true if this closure has domains which are legal to reassign to.
  bool hasLegalDstDomain() const { return LegalDstDomains.any(); }

  /// \returns true if is legal to reassign this closure to domain \p RD.
  bool isLegal(RegDomain RD) const { return LegalDstDomains[RD]; }

  /// Mark this closure as illegal for reassignment to domain \p RD.
  void setIllegal(RegDomain RD) { LegalDstDomains[RD] = false; }

  bool empty() const { return Edges.empty(); }

  bool insertEdge(unsigned Reg) {
    return Edges.insert(Reg).second;
  }

  using const_edge_iterator = DenseSet<unsigned>::const_iterator;
  iterator_range<const_edge_iterator> edges() const {
    return iterator_range<const_edge_iterator>(Edges.begin(), Edges.end());
  }

  void addInstruction(MachineInstr *I) {
    Instrs.push_back(I);
  }

  ArrayRef<MachineInstr *> instructions() const {
    return Instrs;
  }

  LLVM_DUMP_METHOD void dump(const MachineRegisterInfo *MRI) const {
    dbgs() << "Registers: ";
    bool First = true;
    for (unsigned Reg : Edges) {
      if (!First)
        dbgs() << ", ";
      First = false;
      dbgs() << printReg(Reg, MRI->getTargetRegisterInfo(), 0, MRI);
    }
    dbgs() << "\n" << "Instructions:";
    for (MachineInstr *MI : Instrs) {
      dbgs() << "\n  ";
      MI->print(dbgs());
    }
    dbgs() << "\n";
  }

  unsigned getID() const {
    return ID;
  }

};

class X86DomainReassignment : public MachineFunctionPass {
  const X86Subtarget *STI;
  MachineRegisterInfo *MRI;
  const X86InstrInfo *TII;

  /// All edges that are included in some closure
  DenseSet<unsigned> EnclosedEdges;

  /// All instructions that are included in some closure.
  DenseMap<MachineInstr *, unsigned> EnclosedInstrs;

public:
  static char ID;

  X86DomainReassignment() : MachineFunctionPass(ID) {
    initializeX86DomainReassignmentPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return "X86 Domain Reassignment Pass";
  }

private:
  /// A map of available Instruction Converters.
  InstrConverterBaseMap Converters;

  /// Initialize Converters map.
  void initConverters();

  /// Starting from \Reg, expand the closure as much as possible.
  void buildClosure(Closure &, unsigned Reg);

  /// Enqueue \p Reg to be considered for addition to the closure.
  void visitRegister(Closure &, unsigned Reg, RegDomain &Domain,
                     SmallVectorImpl<unsigned> &Worklist);

  /// Reassign the closure to \p Domain.
  void reassign(const Closure &C, RegDomain Domain) const;

  /// Add \p MI to the closure.
  void encloseInstr(Closure &C, MachineInstr *MI);

  /// /returns true if it is profitable to reassign the closure to \p Domain.
  bool isReassignmentProfitable(const Closure &C, RegDomain Domain) const;

  /// Calculate the total cost of reassigning the closure to \p Domain.
  double calculateCost(const Closure &C, RegDomain Domain) const;
};

char X86DomainReassignment::ID = 0;

} // End anonymous namespace.

void X86DomainReassignment::visitRegister(Closure &C, unsigned Reg,
                                          RegDomain &Domain,
                                          SmallVectorImpl<unsigned> &Worklist) {
  if (EnclosedEdges.count(Reg))
    return;

  if (!TargetRegisterInfo::isVirtualRegister(Reg))
    return;

  if (!MRI->hasOneDef(Reg))
    return;

  RegDomain RD = getDomain(MRI->getRegClass(Reg), MRI->getTargetRegisterInfo());
  // First edge in closure sets the domain.
  if (Domain == NoDomain)
    Domain = RD;

  if (Domain != RD)
    return;

  Worklist.push_back(Reg);
}

void X86DomainReassignment::encloseInstr(Closure &C, MachineInstr *MI) {
  auto I = EnclosedInstrs.find(MI);
  if (I != EnclosedInstrs.end()) {
    if (I->second != C.getID())
      // Instruction already belongs to another closure, avoid conflicts between
      // closure and mark this closure as illegal.
      C.setAllIllegal();
    return;
  }

  EnclosedInstrs[MI] = C.getID();
  C.addInstruction(MI);

  // Mark closure as illegal for reassignment to domains, if there is no
  // converter for the instruction or if the converter cannot convert the
  // instruction.
  for (int i = 0; i != NumDomains; ++i) {
    if (C.isLegal((RegDomain)i)) {
      InstrConverterBase *IC = Converters.lookup({i, MI->getOpcode()});
      if (!IC || !IC->isLegal(MI, TII))
        C.setIllegal((RegDomain)i);
    }
  }
}

double X86DomainReassignment::calculateCost(const Closure &C,
                                            RegDomain DstDomain) const {
  assert(C.isLegal(DstDomain) && "Cannot calculate cost for illegal closure");

  double Cost = 0.0;
  for (auto *MI : C.instructions())
    Cost +=
        Converters.lookup({DstDomain, MI->getOpcode()})->getExtraCost(MI, MRI);
  return Cost;
}

bool X86DomainReassignment::isReassignmentProfitable(const Closure &C,
                                                     RegDomain Domain) const {
  return calculateCost(C, Domain) < 0.0;
}

void X86DomainReassignment::reassign(const Closure &C, RegDomain Domain) const {
  assert(C.isLegal(Domain) && "Cannot convert illegal closure");

  // Iterate all instructions in the closure, convert each one using the
  // appropriate converter.
  SmallVector<MachineInstr *, 8> ToErase;
  for (auto *MI : C.instructions())
    if (Converters.lookup({Domain, MI->getOpcode()})
            ->convertInstr(MI, TII, MRI))
      ToErase.push_back(MI);

  // Iterate all registers in the closure, replace them with registers in the
  // destination domain.
  for (unsigned Reg : C.edges()) {
    MRI->setRegClass(Reg, getDstRC(MRI->getRegClass(Reg), Domain));
    for (auto &MO : MRI->use_operands(Reg)) {
      if (MO.isReg())
        // Remove all subregister references as they are not valid in the
        // destination domain.
        MO.setSubReg(0);
    }
  }

  for (auto MI : ToErase)
    MI->eraseFromParent();
}

/// \returns true when \p Reg is used as part of an address calculation in \p
/// MI.
static bool usedAsAddr(const MachineInstr &MI, unsigned Reg,
                       const TargetInstrInfo *TII) {
  if (!MI.mayLoadOrStore())
    return false;

  const MCInstrDesc &Desc = TII->get(MI.getOpcode());
  int MemOpStart = X86II::getMemoryOperandNo(Desc.TSFlags);
  if (MemOpStart == -1)
    return false;

  MemOpStart += X86II::getOperandBias(Desc);
  for (unsigned MemOpIdx = MemOpStart,
                MemOpEnd = MemOpStart + X86::AddrNumOperands;
       MemOpIdx < MemOpEnd; ++MemOpIdx) {
    auto &Op = MI.getOperand(MemOpIdx);
    if (Op.isReg() && Op.getReg() == Reg)
      return true;
  }
  return false;
}

void X86DomainReassignment::buildClosure(Closure &C, unsigned Reg) {
  SmallVector<unsigned, 4> Worklist;
  RegDomain Domain = NoDomain;
  visitRegister(C, Reg, Domain, Worklist);
  while (!Worklist.empty()) {
    unsigned CurReg = Worklist.pop_back_val();

    // Register already in this closure.
    if (!C.insertEdge(CurReg))
      continue;

    MachineInstr *DefMI = MRI->getVRegDef(CurReg);
    encloseInstr(C, DefMI);

    // Add register used by the defining MI to the worklist.
    // Do not add registers which are used in address calculation, they will be
    // added to a different closure.
    int OpEnd = DefMI->getNumOperands();
    const MCInstrDesc &Desc = DefMI->getDesc();
    int MemOp = X86II::getMemoryOperandNo(Desc.TSFlags);
    if (MemOp != -1)
      MemOp += X86II::getOperandBias(Desc);
    for (int OpIdx = 0; OpIdx < OpEnd; ++OpIdx) {
      if (OpIdx == MemOp) {
        // skip address calculation.
        OpIdx += (X86::AddrNumOperands - 1);
        continue;
      }
      auto &Op = DefMI->getOperand(OpIdx);
      if (!Op.isReg() || !Op.isUse())
        continue;
      visitRegister(C, Op.getReg(), Domain, Worklist);
    }

    // Expand closure through register uses.
    for (auto &UseMI : MRI->use_nodbg_instructions(CurReg)) {
      // We would like to avoid converting closures which calculare addresses,
      // as this should remain in GPRs.
      if (usedAsAddr(UseMI, CurReg, TII)) {
        C.setAllIllegal();
        continue;
      }
      encloseInstr(C, &UseMI);

      for (auto &DefOp : UseMI.defs()) {
        if (!DefOp.isReg())
          continue;

        unsigned DefReg = DefOp.getReg();
        if (!TargetRegisterInfo::isVirtualRegister(DefReg)) {
          C.setAllIllegal();
          continue;
        }
        visitRegister(C, DefReg, Domain, Worklist);
      }
    }
  }
}

void X86DomainReassignment::initConverters() {
  Converters[{MaskDomain, TargetOpcode::PHI}] =
      new InstrIgnore(TargetOpcode::PHI);

  Converters[{MaskDomain, TargetOpcode::IMPLICIT_DEF}] =
      new InstrIgnore(TargetOpcode::IMPLICIT_DEF);

  Converters[{MaskDomain, TargetOpcode::INSERT_SUBREG}] =
      new InstrReplaceWithCopy(TargetOpcode::INSERT_SUBREG, 2);

  Converters[{MaskDomain, TargetOpcode::COPY}] =
      new InstrCOPYReplacer(TargetOpcode::COPY, MaskDomain, TargetOpcode::COPY);

  auto createReplacerDstCOPY = [&](unsigned From, unsigned To) {
    Converters[{MaskDomain, From}] = new InstrReplacerDstCOPY(From, To);
  };

  createReplacerDstCOPY(X86::MOVZX32rm16, X86::KMOVWkm);
  createReplacerDstCOPY(X86::MOVZX64rm16, X86::KMOVWkm);

  createReplacerDstCOPY(X86::MOVZX32rr16, X86::KMOVWkk);
  createReplacerDstCOPY(X86::MOVZX64rr16, X86::KMOVWkk);

  if (STI->hasDQI()) {
    createReplacerDstCOPY(X86::MOVZX16rm8, X86::KMOVBkm);
    createReplacerDstCOPY(X86::MOVZX32rm8, X86::KMOVBkm);
    createReplacerDstCOPY(X86::MOVZX64rm8, X86::KMOVBkm);

    createReplacerDstCOPY(X86::MOVZX16rr8, X86::KMOVBkk);
    createReplacerDstCOPY(X86::MOVZX32rr8, X86::KMOVBkk);
    createReplacerDstCOPY(X86::MOVZX64rr8, X86::KMOVBkk);
  }

  auto createReplacer = [&](unsigned From, unsigned To) {
    Converters[{MaskDomain, From}] = new InstrReplacer(From, To);
  };

  createReplacer(X86::MOV16rm, X86::KMOVWkm);
  createReplacer(X86::MOV16mr, X86::KMOVWmk);
  createReplacer(X86::MOV16rr, X86::KMOVWkk);
  createReplacer(X86::SHR16ri, X86::KSHIFTRWri);
  createReplacer(X86::SHL16ri, X86::KSHIFTLWri);
  createReplacer(X86::NOT16r, X86::KNOTWrr);
  createReplacer(X86::OR16rr, X86::KORWrr);
  createReplacer(X86::AND16rr, X86::KANDWrr);
  createReplacer(X86::XOR16rr, X86::KXORWrr);

  if (STI->hasBWI()) {
    createReplacer(X86::MOV32rm, X86::KMOVDkm);
    createReplacer(X86::MOV64rm, X86::KMOVQkm);

    createReplacer(X86::MOV32mr, X86::KMOVDmk);
    createReplacer(X86::MOV64mr, X86::KMOVQmk);

    createReplacer(X86::MOV32rr, X86::KMOVDkk);
    createReplacer(X86::MOV64rr, X86::KMOVQkk);

    createReplacer(X86::SHR32ri, X86::KSHIFTRDri);
    createReplacer(X86::SHR64ri, X86::KSHIFTRQri);

    createReplacer(X86::SHL32ri, X86::KSHIFTLDri);
    createReplacer(X86::SHL64ri, X86::KSHIFTLQri);

    createReplacer(X86::ADD32rr, X86::KADDDrr);
    createReplacer(X86::ADD64rr, X86::KADDQrr);

    createReplacer(X86::NOT32r, X86::KNOTDrr);
    createReplacer(X86::NOT64r, X86::KNOTQrr);

    createReplacer(X86::OR32rr, X86::KORDrr);
    createReplacer(X86::OR64rr, X86::KORQrr);

    createReplacer(X86::AND32rr, X86::KANDDrr);
    createReplacer(X86::AND64rr, X86::KANDQrr);

    createReplacer(X86::ANDN32rr, X86::KANDNDrr);
    createReplacer(X86::ANDN64rr, X86::KANDNQrr);

    createReplacer(X86::XOR32rr, X86::KXORDrr);
    createReplacer(X86::XOR64rr, X86::KXORQrr);

    // TODO: KTEST is not a replacement for TEST due to flag differences. Need
    // to prove only Z flag is used.
    //createReplacer(X86::TEST32rr, X86::KTESTDrr);
    //createReplacer(X86::TEST64rr, X86::KTESTQrr);
  }

  if (STI->hasDQI()) {
    createReplacer(X86::ADD8rr, X86::KADDBrr);
    createReplacer(X86::ADD16rr, X86::KADDWrr);

    createReplacer(X86::AND8rr, X86::KANDBrr);

    createReplacer(X86::MOV8rm, X86::KMOVBkm);
    createReplacer(X86::MOV8mr, X86::KMOVBmk);
    createReplacer(X86::MOV8rr, X86::KMOVBkk);

    createReplacer(X86::NOT8r, X86::KNOTBrr);

    createReplacer(X86::OR8rr, X86::KORBrr);

    createReplacer(X86::SHR8ri, X86::KSHIFTRBri);
    createReplacer(X86::SHL8ri, X86::KSHIFTLBri);

    // TODO: KTEST is not a replacement for TEST due to flag differences. Need
    // to prove only Z flag is used.
    //createReplacer(X86::TEST8rr, X86::KTESTBrr);
    //createReplacer(X86::TEST16rr, X86::KTESTWrr);

    createReplacer(X86::XOR8rr, X86::KXORBrr);
  }
}

bool X86DomainReassignment::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  if (DisableX86DomainReassignment)
    return false;

  LLVM_DEBUG(
      dbgs() << "***** Machine Function before Domain Reassignment *****\n");
  LLVM_DEBUG(MF.print(dbgs()));

  STI = &MF.getSubtarget<X86Subtarget>();
  // GPR->K is the only transformation currently supported, bail out early if no
  // AVX512.
  // TODO: We're also bailing of AVX512BW isn't supported since we use VK32 and
  // VK64 for GR32/GR64, but those aren't legal classes on KNL. If the register
  // coalescer doesn't clean it up and we generate a spill we will crash.
  if (!STI->hasAVX512() || !STI->hasBWI())
    return false;

  MRI = &MF.getRegInfo();
  assert(MRI->isSSA() && "Expected MIR to be in SSA form");

  TII = STI->getInstrInfo();
  initConverters();
  bool Changed = false;

  EnclosedEdges.clear();
  EnclosedInstrs.clear();

  std::vector<Closure> Closures;

  // Go over all virtual registers and calculate a closure.
  unsigned ClosureID = 0;
  for (unsigned Idx = 0; Idx < MRI->getNumVirtRegs(); ++Idx) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(Idx);

    // GPR only current source domain supported.
    if (!isGPR(MRI->getRegClass(Reg)))
      continue;

    // Register already in closure.
    if (EnclosedEdges.count(Reg))
      continue;

    // Calculate closure starting with Reg.
    Closure C(ClosureID++, {MaskDomain});
    buildClosure(C, Reg);

    // Collect all closures that can potentially be converted.
    if (!C.empty() && C.isLegal(MaskDomain))
      Closures.push_back(std::move(C));
  }

  for (Closure &C : Closures) {
    LLVM_DEBUG(C.dump(MRI));
    if (isReassignmentProfitable(C, MaskDomain)) {
      reassign(C, MaskDomain);
      ++NumClosuresConverted;
      Changed = true;
    }
  }

  DeleteContainerSeconds(Converters);

  LLVM_DEBUG(
      dbgs() << "***** Machine Function after Domain Reassignment *****\n");
  LLVM_DEBUG(MF.print(dbgs()));

  return Changed;
}

INITIALIZE_PASS(X86DomainReassignment, "x86-domain-reassignment",
                "X86 Domain Reassignment Pass", false, false)

/// Returns an instance of the Domain Reassignment pass.
FunctionPass *llvm::createX86DomainReassignmentPass() {
  return new X86DomainReassignment();
}
