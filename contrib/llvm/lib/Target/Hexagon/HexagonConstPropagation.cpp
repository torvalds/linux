//===- HexagonConstPropagation.cpp ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "hcp"

#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

  // Properties of a value that are tracked by the propagation.
  // A property that is marked as present (i.e. bit is set) dentes that the
  // value is known (proven) to have this property. Not all combinations
  // of bits make sense, for example Zero and NonZero are mutually exclusive,
  // but on the other hand, Zero implies Finite. In this case, whenever
  // the Zero property is present, Finite should also be present.
  class ConstantProperties {
  public:
    enum {
      Unknown   = 0x0000,
      Zero      = 0x0001,
      NonZero   = 0x0002,
      Finite    = 0x0004,
      Infinity  = 0x0008,
      NaN       = 0x0010,
      SignedZero = 0x0020,
      NumericProperties = (Zero|NonZero|Finite|Infinity|NaN|SignedZero),
      PosOrZero       = 0x0100,
      NegOrZero       = 0x0200,
      SignProperties  = (PosOrZero|NegOrZero),
      Everything      = (NumericProperties|SignProperties)
    };

    // For a given constant, deduce the set of trackable properties that this
    // constant has.
    static uint32_t deduce(const Constant *C);
  };

  // A representation of a register as it can appear in a MachineOperand,
  // i.e. a pair register:subregister.
  struct Register {
    unsigned Reg, SubReg;

    explicit Register(unsigned R, unsigned SR = 0) : Reg(R), SubReg(SR) {}
    explicit Register(const MachineOperand &MO)
      : Reg(MO.getReg()), SubReg(MO.getSubReg()) {}

    void print(const TargetRegisterInfo *TRI = nullptr) const {
      dbgs() << printReg(Reg, TRI, SubReg);
    }

    bool operator== (const Register &R) const {
      return (Reg == R.Reg) && (SubReg == R.SubReg);
    }
  };

  // Lattice cell, based on that was described in the W-Z paper on constant
  // propagation.
  // Latice cell will be allowed to hold multiple constant values. While
  // multiple values would normally indicate "bottom", we can still derive
  // some useful information from them. For example, comparison X > 0
  // could be folded if all the values in the cell associated with X are
  // positive.
  class LatticeCell {
  private:
    enum { Normal, Top, Bottom };

    static const unsigned MaxCellSize = 4;

    unsigned Kind:2;
    unsigned Size:3;
    unsigned IsSpecial:1;
    unsigned :0;

  public:
    union {
      uint32_t Properties;
      const Constant *Value;
      const Constant *Values[MaxCellSize];
    };

    LatticeCell() : Kind(Top), Size(0), IsSpecial(false) {
      for (unsigned i = 0; i < MaxCellSize; ++i)
        Values[i] = nullptr;
    }

    bool meet(const LatticeCell &L);
    bool add(const Constant *C);
    bool add(uint32_t Property);
    uint32_t properties() const;
    unsigned size() const { return Size; }

    LatticeCell &operator= (const LatticeCell &L) {
      if (this != &L) {
        // This memcpy also copies Properties (when L.Size == 0).
        uint32_t N = L.IsSpecial ? sizeof L.Properties
                                 : L.Size*sizeof(const Constant*);
        memcpy(Values, L.Values, N);
        Kind = L.Kind;
        Size = L.Size;
        IsSpecial = L.IsSpecial;
      }
      return *this;
    }

    bool isSingle() const { return size() == 1; }
    bool isProperty() const { return IsSpecial; }
    bool isTop() const { return Kind == Top; }
    bool isBottom() const { return Kind == Bottom; }

    bool setBottom() {
      bool Changed = (Kind != Bottom);
      Kind = Bottom;
      Size = 0;
      IsSpecial = false;
      return Changed;
    }

    void print(raw_ostream &os) const;

  private:
    void setProperty() {
      IsSpecial = true;
      Size = 0;
      Kind = Normal;
    }

    bool convertToProperty();
  };

#ifndef NDEBUG
  raw_ostream &operator<< (raw_ostream &os, const LatticeCell &L) {
    L.print(os);
    return os;
  }
#endif

  class MachineConstEvaluator;

  class MachineConstPropagator {
  public:
    MachineConstPropagator(MachineConstEvaluator &E) : MCE(E) {
      Bottom.setBottom();
    }

    // Mapping: vreg -> cell
    // The keys are registers _without_ subregisters. This won't allow
    // definitions in the form of "vreg:subreg = ...". Such definitions
    // would be questionable from the point of view of SSA, since the "vreg"
    // could not be initialized in its entirety (specifically, an instruction
    // defining the "other part" of "vreg" would also count as a definition
    // of "vreg", which would violate the SSA).
    // If a value of a pair vreg:subreg needs to be obtained, the cell for
    // "vreg" needs to be looked up, and then the value of subregister "subreg"
    // needs to be evaluated.
    class CellMap {
    public:
      CellMap() {
        assert(Top.isTop());
        Bottom.setBottom();
      }

      void clear() { Map.clear(); }

      bool has(unsigned R) const {
        // All non-virtual registers are considered "bottom".
        if (!TargetRegisterInfo::isVirtualRegister(R))
          return true;
        MapType::const_iterator F = Map.find(R);
        return F != Map.end();
      }

      const LatticeCell &get(unsigned R) const {
        if (!TargetRegisterInfo::isVirtualRegister(R))
          return Bottom;
        MapType::const_iterator F = Map.find(R);
        if (F != Map.end())
          return F->second;
        return Top;
      }

      // Invalidates any const references.
      void update(unsigned R, const LatticeCell &L) {
        Map[R] = L;
      }

      void print(raw_ostream &os, const TargetRegisterInfo &TRI) const;

    private:
      using MapType = std::map<unsigned, LatticeCell>;

      MapType Map;
      // To avoid creating "top" entries, return a const reference to
      // this cell in "get". Also, have a "Bottom" cell to return from
      // get when a value of a physical register is requested.
      LatticeCell Top, Bottom;

    public:
      using const_iterator = MapType::const_iterator;

      const_iterator begin() const { return Map.begin(); }
      const_iterator end() const { return Map.end(); }
    };

    bool run(MachineFunction &MF);

  private:
    void visitPHI(const MachineInstr &PN);
    void visitNonBranch(const MachineInstr &MI);
    void visitBranchesFrom(const MachineInstr &BrI);
    void visitUsesOf(unsigned R);
    bool computeBlockSuccessors(const MachineBasicBlock *MB,
          SetVector<const MachineBasicBlock*> &Targets);
    void removeCFGEdge(MachineBasicBlock *From, MachineBasicBlock *To);

    void propagate(MachineFunction &MF);
    bool rewrite(MachineFunction &MF);

    MachineRegisterInfo      *MRI;
    MachineConstEvaluator    &MCE;

    using CFGEdge = std::pair<unsigned, unsigned>;
    using SetOfCFGEdge = std::set<CFGEdge>;
    using SetOfInstr = std::set<const MachineInstr *>;
    using QueueOfCFGEdge = std::queue<CFGEdge>;

    LatticeCell     Bottom;
    CellMap         Cells;
    SetOfCFGEdge    EdgeExec;
    SetOfInstr      InstrExec;
    QueueOfCFGEdge  FlowQ;
  };

  // The "evaluator/rewriter" of machine instructions. This is an abstract
  // base class that provides the interface that the propagator will use,
  // as well as some helper functions that are target-independent.
  class MachineConstEvaluator {
  public:
    MachineConstEvaluator(MachineFunction &Fn)
      : TRI(*Fn.getSubtarget().getRegisterInfo()),
        MF(Fn), CX(Fn.getFunction().getContext()) {}
    virtual ~MachineConstEvaluator() = default;

    // The required interface:
    // - A set of three "evaluate" functions. Each returns "true" if the
    //       computation succeeded, "false" otherwise.
    //   (1) Given an instruction MI, and the map with input values "Inputs",
    //       compute the set of output values "Outputs". An example of when
    //       the computation can "fail" is if MI is not an instruction that
    //       is recognized by the evaluator.
    //   (2) Given a register R (as reg:subreg), compute the cell that
    //       corresponds to the "subreg" part of the given register.
    //   (3) Given a branch instruction BrI, compute the set of target blocks.
    //       If the branch can fall-through, add null (0) to the list of
    //       possible targets.
    // - A function "rewrite", that given the cell map after propagation,
    //   could rewrite instruction MI in a more beneficial form. Return
    //   "true" if a change has been made, "false" otherwise.
    using CellMap = MachineConstPropagator::CellMap;
    virtual bool evaluate(const MachineInstr &MI, const CellMap &Inputs,
                          CellMap &Outputs) = 0;
    virtual bool evaluate(const Register &R, const LatticeCell &SrcC,
                          LatticeCell &Result) = 0;
    virtual bool evaluate(const MachineInstr &BrI, const CellMap &Inputs,
                          SetVector<const MachineBasicBlock*> &Targets,
                          bool &CanFallThru) = 0;
    virtual bool rewrite(MachineInstr &MI, const CellMap &Inputs) = 0;

    const TargetRegisterInfo &TRI;

  protected:
    MachineFunction &MF;
    LLVMContext     &CX;

    struct Comparison {
      enum {
        Unk = 0x00,
        EQ  = 0x01,
        NE  = 0x02,
        L   = 0x04, // Less-than property.
        G   = 0x08, // Greater-than property.
        U   = 0x40, // Unsigned property.
        LTs = L,
        LEs = L | EQ,
        GTs = G,
        GEs = G | EQ,
        LTu = L      | U,
        LEu = L | EQ | U,
        GTu = G      | U,
        GEu = G | EQ | U
      };

      static uint32_t negate(uint32_t Cmp) {
        if (Cmp == EQ)
          return NE;
        if (Cmp == NE)
          return EQ;
        assert((Cmp & (L|G)) != (L|G));
        return Cmp ^ (L|G);
      }
    };

    // Helper functions.

    bool getCell(const Register &R, const CellMap &Inputs, LatticeCell &RC);
    bool constToInt(const Constant *C, APInt &Val) const;
    bool constToFloat(const Constant *C, APFloat &Val) const;
    const ConstantInt *intToConst(const APInt &Val) const;

    // Compares.
    bool evaluateCMPrr(uint32_t Cmp, const Register &R1, const Register &R2,
          const CellMap &Inputs, bool &Result);
    bool evaluateCMPri(uint32_t Cmp, const Register &R1, const APInt &A2,
          const CellMap &Inputs, bool &Result);
    bool evaluateCMPrp(uint32_t Cmp, const Register &R1, uint64_t Props2,
          const CellMap &Inputs, bool &Result);
    bool evaluateCMPii(uint32_t Cmp, const APInt &A1, const APInt &A2,
          bool &Result);
    bool evaluateCMPpi(uint32_t Cmp, uint32_t Props, const APInt &A2,
          bool &Result);
    bool evaluateCMPpp(uint32_t Cmp, uint32_t Props1, uint32_t Props2,
          bool &Result);

    bool evaluateCOPY(const Register &R1, const CellMap &Inputs,
          LatticeCell &Result);

    // Logical operations.
    bool evaluateANDrr(const Register &R1, const Register &R2,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateANDri(const Register &R1, const APInt &A2,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateANDii(const APInt &A1, const APInt &A2, APInt &Result);
    bool evaluateORrr(const Register &R1, const Register &R2,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateORri(const Register &R1, const APInt &A2,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateORii(const APInt &A1, const APInt &A2, APInt &Result);
    bool evaluateXORrr(const Register &R1, const Register &R2,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateXORri(const Register &R1, const APInt &A2,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateXORii(const APInt &A1, const APInt &A2, APInt &Result);

    // Extensions.
    bool evaluateZEXTr(const Register &R1, unsigned Width, unsigned Bits,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateZEXTi(const APInt &A1, unsigned Width, unsigned Bits,
          APInt &Result);
    bool evaluateSEXTr(const Register &R1, unsigned Width, unsigned Bits,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateSEXTi(const APInt &A1, unsigned Width, unsigned Bits,
          APInt &Result);

    // Leading/trailing bits.
    bool evaluateCLBr(const Register &R1, bool Zeros, bool Ones,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateCLBi(const APInt &A1, bool Zeros, bool Ones, APInt &Result);
    bool evaluateCTBr(const Register &R1, bool Zeros, bool Ones,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateCTBi(const APInt &A1, bool Zeros, bool Ones, APInt &Result);

    // Bitfield extract.
    bool evaluateEXTRACTr(const Register &R1, unsigned Width, unsigned Bits,
          unsigned Offset, bool Signed, const CellMap &Inputs,
          LatticeCell &Result);
    bool evaluateEXTRACTi(const APInt &A1, unsigned Bits, unsigned Offset,
          bool Signed, APInt &Result);
    // Vector operations.
    bool evaluateSplatr(const Register &R1, unsigned Bits, unsigned Count,
          const CellMap &Inputs, LatticeCell &Result);
    bool evaluateSplati(const APInt &A1, unsigned Bits, unsigned Count,
          APInt &Result);
  };

} // end anonymous namespace

uint32_t ConstantProperties::deduce(const Constant *C) {
  if (isa<ConstantInt>(C)) {
    const ConstantInt *CI = cast<ConstantInt>(C);
    if (CI->isZero())
      return Zero | PosOrZero | NegOrZero | Finite;
    uint32_t Props = (NonZero | Finite);
    if (CI->isNegative())
      return Props | NegOrZero;
    return Props | PosOrZero;
  }

  if (isa<ConstantFP>(C)) {
    const ConstantFP *CF = cast<ConstantFP>(C);
    uint32_t Props = CF->isNegative() ? (NegOrZero|NonZero)
                                      : PosOrZero;
    if (CF->isZero())
      return (Props & ~NumericProperties) | (Zero|Finite);
    Props = (Props & ~NumericProperties) | NonZero;
    if (CF->isNaN())
      return (Props & ~NumericProperties) | NaN;
    const APFloat &Val = CF->getValueAPF();
    if (Val.isInfinity())
      return (Props & ~NumericProperties) | Infinity;
    Props |= Finite;
    return Props;
  }

  return Unknown;
}

// Convert a cell from a set of specific values to a cell that tracks
// properties.
bool LatticeCell::convertToProperty() {
  if (isProperty())
    return false;
  // Corner case: converting a fresh (top) cell to "special".
  // This can happen, when adding a property to a top cell.
  uint32_t Everything = ConstantProperties::Everything;
  uint32_t Ps = !isTop() ? properties()
                         : Everything;
  if (Ps != ConstantProperties::Unknown) {
    Properties = Ps;
    setProperty();
  } else {
    setBottom();
  }
  return true;
}

#ifndef NDEBUG
void LatticeCell::print(raw_ostream &os) const {
  if (isProperty()) {
    os << "{ ";
    uint32_t Ps = properties();
    if (Ps & ConstantProperties::Zero)
      os << "zero ";
    if (Ps & ConstantProperties::NonZero)
      os << "nonzero ";
    if (Ps & ConstantProperties::Finite)
      os << "finite ";
    if (Ps & ConstantProperties::Infinity)
      os << "infinity ";
    if (Ps & ConstantProperties::NaN)
      os << "nan ";
    if (Ps & ConstantProperties::PosOrZero)
      os << "poz ";
    if (Ps & ConstantProperties::NegOrZero)
      os << "nez ";
    os << '}';
    return;
  }

  os << "{ ";
  if (isBottom()) {
    os << "bottom";
  } else if (isTop()) {
    os << "top";
  } else {
    for (unsigned i = 0; i < size(); ++i) {
      const Constant *C = Values[i];
      if (i != 0)
        os << ", ";
      C->print(os);
    }
  }
  os << " }";
}
#endif

// "Meet" operation on two cells. This is the key of the propagation
// algorithm.
bool LatticeCell::meet(const LatticeCell &L) {
  bool Changed = false;
  if (L.isBottom())
    Changed = setBottom();
  if (isBottom() || L.isTop())
    return Changed;
  if (isTop()) {
    *this = L;
    // L can be neither Top nor Bottom, so *this must have changed.
    return true;
  }

  // Top/bottom cases covered. Need to integrate L's set into ours.
  if (L.isProperty())
    return add(L.properties());
  for (unsigned i = 0; i < L.size(); ++i) {
    const Constant *LC = L.Values[i];
    Changed |= add(LC);
  }
  return Changed;
}

// Add a new constant to the cell. This is actually where the cell update
// happens. If a cell has room for more constants, the new constant is added.
// Otherwise, the cell is converted to a "property" cell (i.e. a cell that
// will track properties of the associated values, and not the values
// themselves. Care is taken to handle special cases, like "bottom", etc.
bool LatticeCell::add(const Constant *LC) {
  assert(LC);
  if (isBottom())
    return false;

  if (!isProperty()) {
    // Cell is not special. Try to add the constant here first,
    // if there is room.
    unsigned Index = 0;
    while (Index < Size) {
      const Constant *C = Values[Index];
      // If the constant is already here, no change is needed.
      if (C == LC)
        return false;
      Index++;
    }
    if (Index < MaxCellSize) {
      Values[Index] = LC;
      Kind = Normal;
      Size++;
      return true;
    }
  }

  bool Changed = false;

  // This cell is special, or is not special, but is full. After this
  // it will be special.
  Changed = convertToProperty();
  uint32_t Ps = properties();
  uint32_t NewPs = Ps & ConstantProperties::deduce(LC);
  if (NewPs == ConstantProperties::Unknown) {
    setBottom();
    return true;
  }
  if (Ps != NewPs) {
    Properties = NewPs;
    Changed = true;
  }
  return Changed;
}

// Add a property to the cell. This will force the cell to become a property-
// tracking cell.
bool LatticeCell::add(uint32_t Property) {
  bool Changed = convertToProperty();
  uint32_t Ps = properties();
  if (Ps == (Ps & Property))
    return Changed;
  Properties = Property & Ps;
  return true;
}

// Return the properties of the values in the cell. This is valid for any
// cell, and does not alter the cell itself.
uint32_t LatticeCell::properties() const {
  if (isProperty())
    return Properties;
  assert(!isTop() && "Should not call this for a top cell");
  if (isBottom())
    return ConstantProperties::Unknown;

  assert(size() > 0 && "Empty cell");
  uint32_t Ps = ConstantProperties::deduce(Values[0]);
  for (unsigned i = 1; i < size(); ++i) {
    if (Ps == ConstantProperties::Unknown)
      break;
    Ps &= ConstantProperties::deduce(Values[i]);
  }
  return Ps;
}

#ifndef NDEBUG
void MachineConstPropagator::CellMap::print(raw_ostream &os,
      const TargetRegisterInfo &TRI) const {
  for (auto &I : Map)
    dbgs() << "  " << printReg(I.first, &TRI) << " -> " << I.second << '\n';
}
#endif

void MachineConstPropagator::visitPHI(const MachineInstr &PN) {
  const MachineBasicBlock *MB = PN.getParent();
  unsigned MBN = MB->getNumber();
  LLVM_DEBUG(dbgs() << "Visiting FI(" << printMBBReference(*MB) << "): " << PN);

  const MachineOperand &MD = PN.getOperand(0);
  Register DefR(MD);
  assert(TargetRegisterInfo::isVirtualRegister(DefR.Reg));

  bool Changed = false;

  // If the def has a sub-register, set the corresponding cell to "bottom".
  if (DefR.SubReg) {
Bottomize:
    const LatticeCell &T = Cells.get(DefR.Reg);
    Changed = !T.isBottom();
    Cells.update(DefR.Reg, Bottom);
    if (Changed)
      visitUsesOf(DefR.Reg);
    return;
  }

  LatticeCell DefC = Cells.get(DefR.Reg);

  for (unsigned i = 1, n = PN.getNumOperands(); i < n; i += 2) {
    const MachineBasicBlock *PB = PN.getOperand(i+1).getMBB();
    unsigned PBN = PB->getNumber();
    if (!EdgeExec.count(CFGEdge(PBN, MBN))) {
      LLVM_DEBUG(dbgs() << "  edge " << printMBBReference(*PB) << "->"
                        << printMBBReference(*MB) << " not executable\n");
      continue;
    }
    const MachineOperand &SO = PN.getOperand(i);
    Register UseR(SO);
    // If the input is not a virtual register, we don't really know what
    // value it holds.
    if (!TargetRegisterInfo::isVirtualRegister(UseR.Reg))
      goto Bottomize;
    // If there is no cell for an input register, it means top.
    if (!Cells.has(UseR.Reg))
      continue;

    LatticeCell SrcC;
    bool Eval = MCE.evaluate(UseR, Cells.get(UseR.Reg), SrcC);
    LLVM_DEBUG(dbgs() << "  edge from " << printMBBReference(*PB) << ": "
                      << printReg(UseR.Reg, &MCE.TRI, UseR.SubReg) << SrcC
                      << '\n');
    Changed |= Eval ? DefC.meet(SrcC)
                    : DefC.setBottom();
    Cells.update(DefR.Reg, DefC);
    if (DefC.isBottom())
      break;
  }
  if (Changed)
    visitUsesOf(DefR.Reg);
}

void MachineConstPropagator::visitNonBranch(const MachineInstr &MI) {
  LLVM_DEBUG(dbgs() << "Visiting MI(" << printMBBReference(*MI.getParent())
                    << "): " << MI);
  CellMap Outputs;
  bool Eval = MCE.evaluate(MI, Cells, Outputs);
  LLVM_DEBUG({
    if (Eval) {
      dbgs() << "  outputs:";
      for (auto &I : Outputs)
        dbgs() << ' ' << I.second;
      dbgs() << '\n';
    }
  });

  // Update outputs. If the value was not computed, set all the
  // def cells to bottom.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register DefR(MO);
    // Only track virtual registers.
    if (!TargetRegisterInfo::isVirtualRegister(DefR.Reg))
      continue;
    bool Changed = false;
    // If the evaluation failed, set cells for all output registers to bottom.
    if (!Eval) {
      const LatticeCell &T = Cells.get(DefR.Reg);
      Changed = !T.isBottom();
      Cells.update(DefR.Reg, Bottom);
    } else {
      // Find the corresponding cell in the computed outputs.
      // If it's not there, go on to the next def.
      if (!Outputs.has(DefR.Reg))
        continue;
      LatticeCell RC = Cells.get(DefR.Reg);
      Changed = RC.meet(Outputs.get(DefR.Reg));
      Cells.update(DefR.Reg, RC);
    }
    if (Changed)
      visitUsesOf(DefR.Reg);
  }
}

// Starting at a given branch, visit remaining branches in the block.
// Traverse over the subsequent branches for as long as the preceding one
// can fall through. Add all the possible targets to the flow work queue,
// including the potential fall-through to the layout-successor block.
void MachineConstPropagator::visitBranchesFrom(const MachineInstr &BrI) {
  const MachineBasicBlock &B = *BrI.getParent();
  unsigned MBN = B.getNumber();
  MachineBasicBlock::const_iterator It = BrI.getIterator();
  MachineBasicBlock::const_iterator End = B.end();

  SetVector<const MachineBasicBlock*> Targets;
  bool EvalOk = true, FallsThru = true;
  while (It != End) {
    const MachineInstr &MI = *It;
    InstrExec.insert(&MI);
    LLVM_DEBUG(dbgs() << "Visiting " << (EvalOk ? "BR" : "br") << "("
                      << printMBBReference(B) << "): " << MI);
    // Do not evaluate subsequent branches if the evaluation of any of the
    // previous branches failed. Keep iterating over the branches only
    // to mark them as executable.
    EvalOk = EvalOk && MCE.evaluate(MI, Cells, Targets, FallsThru);
    if (!EvalOk)
      FallsThru = true;
    if (!FallsThru)
      break;
    ++It;
  }

  if (EvalOk) {
    // Need to add all CFG successors that lead to EH landing pads.
    // There won't be explicit branches to these blocks, but they must
    // be processed.
    for (const MachineBasicBlock *SB : B.successors()) {
      if (SB->isEHPad())
        Targets.insert(SB);
    }
    if (FallsThru) {
      const MachineFunction &MF = *B.getParent();
      MachineFunction::const_iterator BI = B.getIterator();
      MachineFunction::const_iterator Next = std::next(BI);
      if (Next != MF.end())
        Targets.insert(&*Next);
    }
  } else {
    // If the evaluation of the branches failed, make "Targets" to be the
    // set of all successors of the block from the CFG.
    // If the evaluation succeeded for all visited branches, then if the
    // last one set "FallsThru", then add an edge to the layout successor
    // to the targets.
    Targets.clear();
    LLVM_DEBUG(dbgs() << "  failed to evaluate a branch...adding all CFG "
                         "successors\n");
    for (const MachineBasicBlock *SB : B.successors())
      Targets.insert(SB);
  }

  for (const MachineBasicBlock *TB : Targets) {
    unsigned TBN = TB->getNumber();
    LLVM_DEBUG(dbgs() << "  pushing edge " << printMBBReference(B) << " -> "
                      << printMBBReference(*TB) << "\n");
    FlowQ.push(CFGEdge(MBN, TBN));
  }
}

void MachineConstPropagator::visitUsesOf(unsigned Reg) {
  LLVM_DEBUG(dbgs() << "Visiting uses of " << printReg(Reg, &MCE.TRI)
                    << Cells.get(Reg) << '\n');
  for (MachineInstr &MI : MRI->use_nodbg_instructions(Reg)) {
    // Do not process non-executable instructions. They can become exceutable
    // later (via a flow-edge in the work queue). In such case, the instruc-
    // tion will be visited at that time.
    if (!InstrExec.count(&MI))
      continue;
    if (MI.isPHI())
      visitPHI(MI);
    else if (!MI.isBranch())
      visitNonBranch(MI);
    else
      visitBranchesFrom(MI);
  }
}

bool MachineConstPropagator::computeBlockSuccessors(const MachineBasicBlock *MB,
      SetVector<const MachineBasicBlock*> &Targets) {
  MachineBasicBlock::const_iterator FirstBr = MB->end();
  for (const MachineInstr &MI : *MB) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isBranch()) {
      FirstBr = MI.getIterator();
      break;
    }
  }

  Targets.clear();
  MachineBasicBlock::const_iterator End = MB->end();

  bool DoNext = true;
  for (MachineBasicBlock::const_iterator I = FirstBr; I != End; ++I) {
    const MachineInstr &MI = *I;
    // Can there be debug instructions between branches?
    if (MI.isDebugInstr())
      continue;
    if (!InstrExec.count(&MI))
      continue;
    bool Eval = MCE.evaluate(MI, Cells, Targets, DoNext);
    if (!Eval)
      return false;
    if (!DoNext)
      break;
  }
  // If the last branch could fall-through, add block's layout successor.
  if (DoNext) {
    MachineFunction::const_iterator BI = MB->getIterator();
    MachineFunction::const_iterator NextI = std::next(BI);
    if (NextI != MB->getParent()->end())
      Targets.insert(&*NextI);
  }

  // Add all the EH landing pads.
  for (const MachineBasicBlock *SB : MB->successors())
    if (SB->isEHPad())
      Targets.insert(SB);

  return true;
}

void MachineConstPropagator::removeCFGEdge(MachineBasicBlock *From,
      MachineBasicBlock *To) {
  // First, remove the CFG successor/predecessor information.
  From->removeSuccessor(To);
  // Remove all corresponding PHI operands in the To block.
  for (auto I = To->begin(), E = To->getFirstNonPHI(); I != E; ++I) {
    MachineInstr *PN = &*I;
    // reg0 = PHI reg1, bb2, reg3, bb4, ...
    int N = PN->getNumOperands()-2;
    while (N > 0) {
      if (PN->getOperand(N+1).getMBB() == From) {
        PN->RemoveOperand(N+1);
        PN->RemoveOperand(N);
      }
      N -= 2;
    }
  }
}

void MachineConstPropagator::propagate(MachineFunction &MF) {
  MachineBasicBlock *Entry = GraphTraits<MachineFunction*>::getEntryNode(&MF);
  unsigned EntryNum = Entry->getNumber();

  // Start with a fake edge, just to process the entry node.
  FlowQ.push(CFGEdge(EntryNum, EntryNum));

  while (!FlowQ.empty()) {
    CFGEdge Edge = FlowQ.front();
    FlowQ.pop();

    LLVM_DEBUG(
        dbgs() << "Picked edge "
               << printMBBReference(*MF.getBlockNumbered(Edge.first)) << "->"
               << printMBBReference(*MF.getBlockNumbered(Edge.second)) << '\n');
    if (Edge.first != EntryNum)
      if (EdgeExec.count(Edge))
        continue;
    EdgeExec.insert(Edge);
    MachineBasicBlock *SB = MF.getBlockNumbered(Edge.second);

    // Process the block in three stages:
    // - visit all PHI nodes,
    // - visit all non-branch instructions,
    // - visit block branches.
    MachineBasicBlock::const_iterator It = SB->begin(), End = SB->end();

    // Visit PHI nodes in the successor block.
    while (It != End && It->isPHI()) {
      InstrExec.insert(&*It);
      visitPHI(*It);
      ++It;
    }

    // If the successor block just became executable, visit all instructions.
    // To see if this is the first time we're visiting it, check the first
    // non-debug instruction to see if it is executable.
    while (It != End && It->isDebugInstr())
      ++It;
    assert(It == End || !It->isPHI());
    // If this block has been visited, go on to the next one.
    if (It != End && InstrExec.count(&*It))
      continue;
    // For now, scan all non-branch instructions. Branches require different
    // processing.
    while (It != End && !It->isBranch()) {
      if (!It->isDebugInstr()) {
        InstrExec.insert(&*It);
        visitNonBranch(*It);
      }
      ++It;
    }

    // Time to process the end of the block. This is different from
    // processing regular (non-branch) instructions, because there can
    // be multiple branches in a block, and they can cause the block to
    // terminate early.
    if (It != End) {
      visitBranchesFrom(*It);
    } else {
      // If the block didn't have a branch, add all successor edges to the
      // work queue. (There should really be only one successor in such case.)
      unsigned SBN = SB->getNumber();
      for (const MachineBasicBlock *SSB : SB->successors())
        FlowQ.push(CFGEdge(SBN, SSB->getNumber()));
    }
  } // while (FlowQ)

  LLVM_DEBUG({
    dbgs() << "Cells after propagation:\n";
    Cells.print(dbgs(), MCE.TRI);
    dbgs() << "Dead CFG edges:\n";
    for (const MachineBasicBlock &B : MF) {
      unsigned BN = B.getNumber();
      for (const MachineBasicBlock *SB : B.successors()) {
        unsigned SN = SB->getNumber();
        if (!EdgeExec.count(CFGEdge(BN, SN)))
          dbgs() << "  " << printMBBReference(B) << " -> "
                 << printMBBReference(*SB) << '\n';
      }
    }
  });
}

bool MachineConstPropagator::rewrite(MachineFunction &MF) {
  bool Changed = false;
  // Rewrite all instructions based on the collected cell information.
  //
  // Traverse the instructions in a post-order, so that rewriting an
  // instruction can make changes "downstream" in terms of control-flow
  // without affecting the rewriting process. (We should not change
  // instructions that have not yet been visited by the rewriter.)
  // The reason for this is that the rewriter can introduce new vregs,
  // and replace uses of old vregs (which had corresponding cells
  // computed during propagation) with these new vregs (which at this
  // point would not have any cells, and would appear to be "top").
  // If an attempt was made to evaluate an instruction with a fresh
  // "top" vreg, it would cause an error (abend) in the evaluator.

  // Collect the post-order-traversal block ordering. The subsequent
  // traversal/rewrite will update block successors, so it's safer
  // if the visiting order it computed ahead of time.
  std::vector<MachineBasicBlock*> POT;
  for (MachineBasicBlock *B : post_order(&MF))
    if (!B->empty())
      POT.push_back(B);

  for (MachineBasicBlock *B : POT) {
    // Walk the block backwards (which usually begin with the branches).
    // If any branch is rewritten, we may need to update the successor
    // information for this block. Unless the block's successors can be
    // precisely determined (which may not be the case for indirect
    // branches), we cannot modify any branch.

    // Compute the successor information.
    SetVector<const MachineBasicBlock*> Targets;
    bool HaveTargets = computeBlockSuccessors(B, Targets);
    // Rewrite the executable instructions. Skip branches if we don't
    // have block successor information.
    for (auto I = B->rbegin(), E = B->rend(); I != E; ++I) {
      MachineInstr &MI = *I;
      if (InstrExec.count(&MI)) {
        if (MI.isBranch() && !HaveTargets)
          continue;
        Changed |= MCE.rewrite(MI, Cells);
      }
    }
    // The rewriting could rewrite PHI nodes to non-PHI nodes, causing
    // regular instructions to appear in between PHI nodes. Bring all
    // the PHI nodes to the beginning of the block.
    for (auto I = B->begin(), E = B->end(); I != E; ++I) {
      if (I->isPHI())
        continue;
      // I is not PHI. Find the next PHI node P.
      auto P = I;
      while (++P != E)
        if (P->isPHI())
          break;
      // Not found.
      if (P == E)
        break;
      // Splice P right before I.
      B->splice(I, B, P);
      // Reset I to point at the just spliced PHI node.
      --I;
    }
    // Update the block successor information: remove unnecessary successors.
    if (HaveTargets) {
      SmallVector<MachineBasicBlock*,2> ToRemove;
      for (MachineBasicBlock *SB : B->successors()) {
        if (!Targets.count(SB))
          ToRemove.push_back(const_cast<MachineBasicBlock*>(SB));
        Targets.remove(SB);
      }
      for (unsigned i = 0, n = ToRemove.size(); i < n; ++i)
        removeCFGEdge(B, ToRemove[i]);
      // If there are any blocks left in the computed targets, it means that
      // we think that the block could go somewhere, but the CFG does not.
      // This could legitimately happen in blocks that have non-returning
      // calls---we would think that the execution can continue, but the
      // CFG will not have a successor edge.
    }
  }
  // Need to do some final post-processing.
  // If a branch was not executable, it will not get rewritten, but should
  // be removed (or replaced with something equivalent to a A2_nop). We can't
  // erase instructions during rewriting, so this needs to be delayed until
  // now.
  for (MachineBasicBlock &B : MF) {
    MachineBasicBlock::iterator I = B.begin(), E = B.end();
    while (I != E) {
      auto Next = std::next(I);
      if (I->isBranch() && !InstrExec.count(&*I))
        B.erase(I);
      I = Next;
    }
  }
  return Changed;
}

// This is the constant propagation algorithm as described by Wegman-Zadeck.
// Most of the terminology comes from there.
bool MachineConstPropagator::run(MachineFunction &MF) {
  LLVM_DEBUG(MF.print(dbgs() << "Starting MachineConstPropagator\n", nullptr));

  MRI = &MF.getRegInfo();

  Cells.clear();
  EdgeExec.clear();
  InstrExec.clear();
  assert(FlowQ.empty());

  propagate(MF);
  bool Changed = rewrite(MF);

  LLVM_DEBUG({
    dbgs() << "End of MachineConstPropagator (Changed=" << Changed << ")\n";
    if (Changed)
      MF.print(dbgs(), nullptr);
  });
  return Changed;
}

// --------------------------------------------------------------------
// Machine const evaluator.

bool MachineConstEvaluator::getCell(const Register &R, const CellMap &Inputs,
      LatticeCell &RC) {
  if (!TargetRegisterInfo::isVirtualRegister(R.Reg))
    return false;
  const LatticeCell &L = Inputs.get(R.Reg);
  if (!R.SubReg) {
    RC = L;
    return !RC.isBottom();
  }
  bool Eval = evaluate(R, L, RC);
  return Eval && !RC.isBottom();
}

bool MachineConstEvaluator::constToInt(const Constant *C,
      APInt &Val) const {
  const ConstantInt *CI = dyn_cast<ConstantInt>(C);
  if (!CI)
    return false;
  Val = CI->getValue();
  return true;
}

const ConstantInt *MachineConstEvaluator::intToConst(const APInt &Val) const {
  return ConstantInt::get(CX, Val);
}

bool MachineConstEvaluator::evaluateCMPrr(uint32_t Cmp, const Register &R1,
      const Register &R2, const CellMap &Inputs, bool &Result) {
  assert(Inputs.has(R1.Reg) && Inputs.has(R2.Reg));
  LatticeCell LS1, LS2;
  if (!getCell(R1, Inputs, LS1) || !getCell(R2, Inputs, LS2))
    return false;

  bool IsProp1 = LS1.isProperty();
  bool IsProp2 = LS2.isProperty();
  if (IsProp1) {
    uint32_t Prop1 = LS1.properties();
    if (IsProp2)
      return evaluateCMPpp(Cmp, Prop1, LS2.properties(), Result);
    uint32_t NegCmp = Comparison::negate(Cmp);
    return evaluateCMPrp(NegCmp, R2, Prop1, Inputs, Result);
  }
  if (IsProp2) {
    uint32_t Prop2 = LS2.properties();
    return evaluateCMPrp(Cmp, R1, Prop2, Inputs, Result);
  }

  APInt A;
  bool IsTrue = true, IsFalse = true;
  for (unsigned i = 0; i < LS2.size(); ++i) {
    bool Res;
    bool Computed = constToInt(LS2.Values[i], A) &&
                    evaluateCMPri(Cmp, R1, A, Inputs, Res);
    if (!Computed)
      return false;
    IsTrue &= Res;
    IsFalse &= !Res;
  }
  assert(!IsTrue || !IsFalse);
  // The actual logical value of the comparison is same as IsTrue.
  Result = IsTrue;
  // Return true if the result was proven to be true or proven to be false.
  return IsTrue || IsFalse;
}

bool MachineConstEvaluator::evaluateCMPri(uint32_t Cmp, const Register &R1,
      const APInt &A2, const CellMap &Inputs, bool &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS;
  if (!getCell(R1, Inputs, LS))
    return false;
  if (LS.isProperty())
    return evaluateCMPpi(Cmp, LS.properties(), A2, Result);

  APInt A;
  bool IsTrue = true, IsFalse = true;
  for (unsigned i = 0; i < LS.size(); ++i) {
    bool Res;
    bool Computed = constToInt(LS.Values[i], A) &&
                    evaluateCMPii(Cmp, A, A2, Res);
    if (!Computed)
      return false;
    IsTrue &= Res;
    IsFalse &= !Res;
  }
  assert(!IsTrue || !IsFalse);
  // The actual logical value of the comparison is same as IsTrue.
  Result = IsTrue;
  // Return true if the result was proven to be true or proven to be false.
  return IsTrue || IsFalse;
}

bool MachineConstEvaluator::evaluateCMPrp(uint32_t Cmp, const Register &R1,
      uint64_t Props2, const CellMap &Inputs, bool &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS;
  if (!getCell(R1, Inputs, LS))
    return false;
  if (LS.isProperty())
    return evaluateCMPpp(Cmp, LS.properties(), Props2, Result);

  APInt A;
  uint32_t NegCmp = Comparison::negate(Cmp);
  bool IsTrue = true, IsFalse = true;
  for (unsigned i = 0; i < LS.size(); ++i) {
    bool Res;
    bool Computed = constToInt(LS.Values[i], A) &&
                    evaluateCMPpi(NegCmp, Props2, A, Res);
    if (!Computed)
      return false;
    IsTrue &= Res;
    IsFalse &= !Res;
  }
  assert(!IsTrue || !IsFalse);
  Result = IsTrue;
  return IsTrue || IsFalse;
}

bool MachineConstEvaluator::evaluateCMPii(uint32_t Cmp, const APInt &A1,
      const APInt &A2, bool &Result) {
  // NE is a special kind of comparison (not composed of smaller properties).
  if (Cmp == Comparison::NE) {
    Result = !APInt::isSameValue(A1, A2);
    return true;
  }
  if (Cmp == Comparison::EQ) {
    Result = APInt::isSameValue(A1, A2);
    return true;
  }
  if (Cmp & Comparison::EQ) {
    if (APInt::isSameValue(A1, A2))
      return (Result = true);
  }
  assert((Cmp & (Comparison::L | Comparison::G)) && "Malformed comparison");
  Result = false;

  unsigned W1 = A1.getBitWidth();
  unsigned W2 = A2.getBitWidth();
  unsigned MaxW = (W1 >= W2) ? W1 : W2;
  if (Cmp & Comparison::U) {
    const APInt Zx1 = A1.zextOrSelf(MaxW);
    const APInt Zx2 = A2.zextOrSelf(MaxW);
    if (Cmp & Comparison::L)
      Result = Zx1.ult(Zx2);
    else if (Cmp & Comparison::G)
      Result = Zx2.ult(Zx1);
    return true;
  }

  // Signed comparison.
  const APInt Sx1 = A1.sextOrSelf(MaxW);
  const APInt Sx2 = A2.sextOrSelf(MaxW);
  if (Cmp & Comparison::L)
    Result = Sx1.slt(Sx2);
  else if (Cmp & Comparison::G)
    Result = Sx2.slt(Sx1);
  return true;
}

bool MachineConstEvaluator::evaluateCMPpi(uint32_t Cmp, uint32_t Props,
      const APInt &A2, bool &Result) {
  if (Props == ConstantProperties::Unknown)
    return false;

  // Should never see NaN here, but check for it for completeness.
  if (Props & ConstantProperties::NaN)
    return false;
  // Infinity could theoretically be compared to a number, but the
  // presence of infinity here would be very suspicious. If we don't
  // know for sure that the number is finite, bail out.
  if (!(Props & ConstantProperties::Finite))
    return false;

  // Let X be a number that has properties Props.

  if (Cmp & Comparison::U) {
    // In case of unsigned comparisons, we can only compare against 0.
    if (A2 == 0) {
      // Any x!=0 will be considered >0 in an unsigned comparison.
      if (Props & ConstantProperties::Zero)
        Result = (Cmp & Comparison::EQ);
      else if (Props & ConstantProperties::NonZero)
        Result = (Cmp & Comparison::G) || (Cmp == Comparison::NE);
      else
        return false;
      return true;
    }
    // A2 is not zero. The only handled case is if X = 0.
    if (Props & ConstantProperties::Zero) {
      Result = (Cmp & Comparison::L) || (Cmp == Comparison::NE);
      return true;
    }
    return false;
  }

  // Signed comparisons are different.
  if (Props & ConstantProperties::Zero) {
    if (A2 == 0)
      Result = (Cmp & Comparison::EQ);
    else
      Result = (Cmp == Comparison::NE) ||
               ((Cmp & Comparison::L) && !A2.isNegative()) ||
               ((Cmp & Comparison::G) &&  A2.isNegative());
    return true;
  }
  if (Props & ConstantProperties::PosOrZero) {
    // X >= 0 and !(A2 < 0) => cannot compare
    if (!A2.isNegative())
      return false;
    // X >= 0 and A2 < 0
    Result = (Cmp & Comparison::G) || (Cmp == Comparison::NE);
    return true;
  }
  if (Props & ConstantProperties::NegOrZero) {
    // X <= 0 and Src1 < 0 => cannot compare
    if (A2 == 0 || A2.isNegative())
      return false;
    // X <= 0 and A2 > 0
    Result = (Cmp & Comparison::L) || (Cmp == Comparison::NE);
    return true;
  }

  return false;
}

bool MachineConstEvaluator::evaluateCMPpp(uint32_t Cmp, uint32_t Props1,
      uint32_t Props2, bool &Result) {
  using P = ConstantProperties;

  if ((Props1 & P::NaN) && (Props2 & P::NaN))
    return false;
  if (!(Props1 & P::Finite) || !(Props2 & P::Finite))
    return false;

  bool Zero1 = (Props1 & P::Zero), Zero2 = (Props2 & P::Zero);
  bool NonZero1 = (Props1 & P::NonZero), NonZero2 = (Props2 & P::NonZero);
  if (Zero1 && Zero2) {
    Result = (Cmp & Comparison::EQ);
    return true;
  }
  if (Cmp == Comparison::NE) {
    if ((Zero1 && NonZero2) || (NonZero1 && Zero2))
      return (Result = true);
    return false;
  }

  if (Cmp & Comparison::U) {
    // In unsigned comparisons, we can only compare against a known zero,
    // or a known non-zero.
    if (Zero1 && NonZero2) {
      Result = (Cmp & Comparison::L);
      return true;
    }
    if (NonZero1 && Zero2) {
      Result = (Cmp & Comparison::G);
      return true;
    }
    return false;
  }

  // Signed comparison. The comparison is not NE.
  bool Poz1 = (Props1 & P::PosOrZero), Poz2 = (Props2 & P::PosOrZero);
  bool Nez1 = (Props1 & P::NegOrZero), Nez2 = (Props2 & P::NegOrZero);
  if (Nez1 && Poz2) {
    if (NonZero1 || NonZero2) {
      Result = (Cmp & Comparison::L);
      return true;
    }
    // Either (or both) could be zero. Can only say that X <= Y.
    if ((Cmp & Comparison::EQ) && (Cmp & Comparison::L))
      return (Result = true);
  }
  if (Poz1 && Nez2) {
    if (NonZero1 || NonZero2) {
      Result = (Cmp & Comparison::G);
      return true;
    }
    // Either (or both) could be zero. Can only say that X >= Y.
    if ((Cmp & Comparison::EQ) && (Cmp & Comparison::G))
      return (Result = true);
  }

  return false;
}

bool MachineConstEvaluator::evaluateCOPY(const Register &R1,
      const CellMap &Inputs, LatticeCell &Result) {
  return getCell(R1, Inputs, Result);
}

bool MachineConstEvaluator::evaluateANDrr(const Register &R1,
      const Register &R2, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg) && Inputs.has(R2.Reg));
  const LatticeCell &L1 = Inputs.get(R2.Reg);
  const LatticeCell &L2 = Inputs.get(R2.Reg);
  // If both sources are bottom, exit. Otherwise try to evaluate ANDri
  // with the non-bottom argument passed as the immediate. This is to
  // catch cases of ANDing with 0.
  if (L2.isBottom()) {
    if (L1.isBottom())
      return false;
    return evaluateANDrr(R2, R1, Inputs, Result);
  }
  LatticeCell LS2;
  if (!evaluate(R2, L2, LS2))
    return false;
  if (LS2.isBottom() || LS2.isProperty())
    return false;

  APInt A;
  for (unsigned i = 0; i < LS2.size(); ++i) {
    LatticeCell RC;
    bool Eval = constToInt(LS2.Values[i], A) &&
                evaluateANDri(R1, A, Inputs, RC);
    if (!Eval)
      return false;
    Result.meet(RC);
  }
  return !Result.isBottom();
}

bool MachineConstEvaluator::evaluateANDri(const Register &R1,
      const APInt &A2, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  if (A2 == -1)
    return getCell(R1, Inputs, Result);
  if (A2 == 0) {
    LatticeCell RC;
    RC.add(intToConst(A2));
    // Overwrite Result.
    Result = RC;
    return true;
  }
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom() || LS1.isProperty())
    return false;

  APInt A, ResA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateANDii(A, A2, ResA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(ResA);
    Result.add(C);
  }
  return !Result.isBottom();
}

bool MachineConstEvaluator::evaluateANDii(const APInt &A1,
      const APInt &A2, APInt &Result) {
  Result = A1 & A2;
  return true;
}

bool MachineConstEvaluator::evaluateORrr(const Register &R1,
      const Register &R2, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg) && Inputs.has(R2.Reg));
  const LatticeCell &L1 = Inputs.get(R2.Reg);
  const LatticeCell &L2 = Inputs.get(R2.Reg);
  // If both sources are bottom, exit. Otherwise try to evaluate ORri
  // with the non-bottom argument passed as the immediate. This is to
  // catch cases of ORing with -1.
  if (L2.isBottom()) {
    if (L1.isBottom())
      return false;
    return evaluateORrr(R2, R1, Inputs, Result);
  }
  LatticeCell LS2;
  if (!evaluate(R2, L2, LS2))
    return false;
  if (LS2.isBottom() || LS2.isProperty())
    return false;

  APInt A;
  for (unsigned i = 0; i < LS2.size(); ++i) {
    LatticeCell RC;
    bool Eval = constToInt(LS2.Values[i], A) &&
                evaluateORri(R1, A, Inputs, RC);
    if (!Eval)
      return false;
    Result.meet(RC);
  }
  return !Result.isBottom();
}

bool MachineConstEvaluator::evaluateORri(const Register &R1,
      const APInt &A2, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  if (A2 == 0)
    return getCell(R1, Inputs, Result);
  if (A2 == -1) {
    LatticeCell RC;
    RC.add(intToConst(A2));
    // Overwrite Result.
    Result = RC;
    return true;
  }
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom() || LS1.isProperty())
    return false;

  APInt A, ResA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateORii(A, A2, ResA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(ResA);
    Result.add(C);
  }
  return !Result.isBottom();
}

bool MachineConstEvaluator::evaluateORii(const APInt &A1,
      const APInt &A2, APInt &Result) {
  Result = A1 | A2;
  return true;
}

bool MachineConstEvaluator::evaluateXORrr(const Register &R1,
      const Register &R2, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg) && Inputs.has(R2.Reg));
  LatticeCell LS1, LS2;
  if (!getCell(R1, Inputs, LS1) || !getCell(R2, Inputs, LS2))
    return false;
  if (LS1.isProperty()) {
    if (LS1.properties() & ConstantProperties::Zero)
      return !(Result = LS2).isBottom();
    return false;
  }
  if (LS2.isProperty()) {
    if (LS2.properties() & ConstantProperties::Zero)
      return !(Result = LS1).isBottom();
    return false;
  }

  APInt A;
  for (unsigned i = 0; i < LS2.size(); ++i) {
    LatticeCell RC;
    bool Eval = constToInt(LS2.Values[i], A) &&
                evaluateXORri(R1, A, Inputs, RC);
    if (!Eval)
      return false;
    Result.meet(RC);
  }
  return !Result.isBottom();
}

bool MachineConstEvaluator::evaluateXORri(const Register &R1,
      const APInt &A2, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isProperty()) {
    if (LS1.properties() & ConstantProperties::Zero) {
      const Constant *C = intToConst(A2);
      Result.add(C);
      return !Result.isBottom();
    }
    return false;
  }

  APInt A, XA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateXORii(A, A2, XA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(XA);
    Result.add(C);
  }
  return !Result.isBottom();
}

bool MachineConstEvaluator::evaluateXORii(const APInt &A1,
      const APInt &A2, APInt &Result) {
  Result = A1 ^ A2;
  return true;
}

bool MachineConstEvaluator::evaluateZEXTr(const Register &R1, unsigned Width,
      unsigned Bits, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isProperty())
    return false;

  APInt A, XA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateZEXTi(A, Width, Bits, XA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(XA);
    Result.add(C);
  }
  return true;
}

bool MachineConstEvaluator::evaluateZEXTi(const APInt &A1, unsigned Width,
      unsigned Bits, APInt &Result) {
  unsigned BW = A1.getBitWidth();
  (void)BW;
  assert(Width >= Bits && BW >= Bits);
  APInt Mask = APInt::getLowBitsSet(Width, Bits);
  Result = A1.zextOrTrunc(Width) & Mask;
  return true;
}

bool MachineConstEvaluator::evaluateSEXTr(const Register &R1, unsigned Width,
      unsigned Bits, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom() || LS1.isProperty())
    return false;

  APInt A, XA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateSEXTi(A, Width, Bits, XA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(XA);
    Result.add(C);
  }
  return true;
}

bool MachineConstEvaluator::evaluateSEXTi(const APInt &A1, unsigned Width,
      unsigned Bits, APInt &Result) {
  unsigned BW = A1.getBitWidth();
  assert(Width >= Bits && BW >= Bits);
  // Special case to make things faster for smaller source widths.
  // Sign extension of 0 bits generates 0 as a result. This is consistent
  // with what the HW does.
  if (Bits == 0) {
    Result = APInt(Width, 0);
    return true;
  }
  // In C, shifts by 64 invoke undefined behavior: handle that case in APInt.
  if (BW <= 64 && Bits != 0) {
    int64_t V = A1.getSExtValue();
    switch (Bits) {
      case 8:
        V = static_cast<int8_t>(V);
        break;
      case 16:
        V = static_cast<int16_t>(V);
        break;
      case 32:
        V = static_cast<int32_t>(V);
        break;
      default:
        // Shift left to lose all bits except lower "Bits" bits, then shift
        // the value back, replicating what was a sign bit after the first
        // shift.
        V = (V << (64-Bits)) >> (64-Bits);
        break;
    }
    // V is a 64-bit sign-extended value. Convert it to APInt of desired
    // width.
    Result = APInt(Width, V, true);
    return true;
  }
  // Slow case: the value doesn't fit in int64_t.
  if (Bits < BW)
    Result = A1.trunc(Bits).sext(Width);
  else // Bits == BW
    Result = A1.sext(Width);
  return true;
}

bool MachineConstEvaluator::evaluateCLBr(const Register &R1, bool Zeros,
      bool Ones, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom() || LS1.isProperty())
    return false;

  APInt A, CA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateCLBi(A, Zeros, Ones, CA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(CA);
    Result.add(C);
  }
  return true;
}

bool MachineConstEvaluator::evaluateCLBi(const APInt &A1, bool Zeros,
      bool Ones, APInt &Result) {
  unsigned BW = A1.getBitWidth();
  if (!Zeros && !Ones)
    return false;
  unsigned Count = 0;
  if (Zeros && (Count == 0))
    Count = A1.countLeadingZeros();
  if (Ones && (Count == 0))
    Count = A1.countLeadingOnes();
  Result = APInt(BW, static_cast<uint64_t>(Count), false);
  return true;
}

bool MachineConstEvaluator::evaluateCTBr(const Register &R1, bool Zeros,
      bool Ones, const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom() || LS1.isProperty())
    return false;

  APInt A, CA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateCTBi(A, Zeros, Ones, CA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(CA);
    Result.add(C);
  }
  return true;
}

bool MachineConstEvaluator::evaluateCTBi(const APInt &A1, bool Zeros,
      bool Ones, APInt &Result) {
  unsigned BW = A1.getBitWidth();
  if (!Zeros && !Ones)
    return false;
  unsigned Count = 0;
  if (Zeros && (Count == 0))
    Count = A1.countTrailingZeros();
  if (Ones && (Count == 0))
    Count = A1.countTrailingOnes();
  Result = APInt(BW, static_cast<uint64_t>(Count), false);
  return true;
}

bool MachineConstEvaluator::evaluateEXTRACTr(const Register &R1,
      unsigned Width, unsigned Bits, unsigned Offset, bool Signed,
      const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  assert(Bits+Offset <= Width);
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom())
    return false;
  if (LS1.isProperty()) {
    uint32_t Ps = LS1.properties();
    if (Ps & ConstantProperties::Zero) {
      const Constant *C = intToConst(APInt(Width, 0, false));
      Result.add(C);
      return true;
    }
    return false;
  }

  APInt A, CA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateEXTRACTi(A, Bits, Offset, Signed, CA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(CA);
    Result.add(C);
  }
  return true;
}

bool MachineConstEvaluator::evaluateEXTRACTi(const APInt &A1, unsigned Bits,
      unsigned Offset, bool Signed, APInt &Result) {
  unsigned BW = A1.getBitWidth();
  assert(Bits+Offset <= BW);
  // Extracting 0 bits generates 0 as a result (as indicated by the HW people).
  if (Bits == 0) {
    Result = APInt(BW, 0);
    return true;
  }
  if (BW <= 64) {
    int64_t V = A1.getZExtValue();
    V <<= (64-Bits-Offset);
    if (Signed)
      V >>= (64-Bits);
    else
      V = static_cast<uint64_t>(V) >> (64-Bits);
    Result = APInt(BW, V, Signed);
    return true;
  }
  if (Signed)
    Result = A1.shl(BW-Bits-Offset).ashr(BW-Bits);
  else
    Result = A1.shl(BW-Bits-Offset).lshr(BW-Bits);
  return true;
}

bool MachineConstEvaluator::evaluateSplatr(const Register &R1,
      unsigned Bits, unsigned Count, const CellMap &Inputs,
      LatticeCell &Result) {
  assert(Inputs.has(R1.Reg));
  LatticeCell LS1;
  if (!getCell(R1, Inputs, LS1))
    return false;
  if (LS1.isBottom() || LS1.isProperty())
    return false;

  APInt A, SA;
  for (unsigned i = 0; i < LS1.size(); ++i) {
    bool Eval = constToInt(LS1.Values[i], A) &&
                evaluateSplati(A, Bits, Count, SA);
    if (!Eval)
      return false;
    const Constant *C = intToConst(SA);
    Result.add(C);
  }
  return true;
}

bool MachineConstEvaluator::evaluateSplati(const APInt &A1, unsigned Bits,
      unsigned Count, APInt &Result) {
  assert(Count > 0);
  unsigned BW = A1.getBitWidth(), SW = Count*Bits;
  APInt LoBits = (Bits < BW) ? A1.trunc(Bits) : A1.zextOrSelf(Bits);
  if (Count > 1)
    LoBits = LoBits.zext(SW);

  APInt Res(SW, 0, false);
  for (unsigned i = 0; i < Count; ++i) {
    Res <<= Bits;
    Res |= LoBits;
  }
  Result = Res;
  return true;
}

// ----------------------------------------------------------------------
// Hexagon-specific code.

namespace llvm {

  FunctionPass *createHexagonConstPropagationPass();
  void initializeHexagonConstPropagationPass(PassRegistry &Registry);

} // end namespace llvm

namespace {

  class HexagonConstEvaluator : public MachineConstEvaluator {
  public:
    HexagonConstEvaluator(MachineFunction &Fn);

    bool evaluate(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs) override;
    bool evaluate(const Register &R, const LatticeCell &SrcC,
          LatticeCell &Result) override;
    bool evaluate(const MachineInstr &BrI, const CellMap &Inputs,
          SetVector<const MachineBasicBlock*> &Targets, bool &FallsThru)
          override;
    bool rewrite(MachineInstr &MI, const CellMap &Inputs) override;

  private:
    unsigned getRegBitWidth(unsigned Reg) const;

    static uint32_t getCmp(unsigned Opc);
    static APInt getCmpImm(unsigned Opc, unsigned OpX,
          const MachineOperand &MO);
    void replaceWithNop(MachineInstr &MI);

    bool evaluateHexRSEQ32(Register RL, Register RH, const CellMap &Inputs,
          LatticeCell &Result);
    bool evaluateHexCompare(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs);
    // This is suitable to be called for compare-and-jump instructions.
    bool evaluateHexCompare2(uint32_t Cmp, const MachineOperand &Src1,
          const MachineOperand &Src2, const CellMap &Inputs, bool &Result);
    bool evaluateHexLogical(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs);
    bool evaluateHexCondMove(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs);
    bool evaluateHexExt(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs);
    bool evaluateHexVector1(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs);
    bool evaluateHexVector2(const MachineInstr &MI, const CellMap &Inputs,
          CellMap &Outputs);

    void replaceAllRegUsesWith(unsigned FromReg, unsigned ToReg);
    bool rewriteHexBranch(MachineInstr &BrI, const CellMap &Inputs);
    bool rewriteHexConstDefs(MachineInstr &MI, const CellMap &Inputs,
          bool &AllDefs);
    bool rewriteHexConstUses(MachineInstr &MI, const CellMap &Inputs);

    MachineRegisterInfo *MRI;
    const HexagonInstrInfo &HII;
    const HexagonRegisterInfo &HRI;
  };

  class HexagonConstPropagation : public MachineFunctionPass {
  public:
    static char ID;

    HexagonConstPropagation() : MachineFunctionPass(ID) {}

    StringRef getPassName() const override {
      return "Hexagon Constant Propagation";
    }

    bool runOnMachineFunction(MachineFunction &MF) override {
      const Function &F = MF.getFunction();
      if (skipFunction(F))
        return false;

      HexagonConstEvaluator HCE(MF);
      return MachineConstPropagator(HCE).run(MF);
    }
  };

} // end anonymous namespace

char HexagonConstPropagation::ID = 0;

INITIALIZE_PASS(HexagonConstPropagation, "hexagon-constp",
  "Hexagon Constant Propagation", false, false)

HexagonConstEvaluator::HexagonConstEvaluator(MachineFunction &Fn)
  : MachineConstEvaluator(Fn),
    HII(*Fn.getSubtarget<HexagonSubtarget>().getInstrInfo()),
    HRI(*Fn.getSubtarget<HexagonSubtarget>().getRegisterInfo()) {
  MRI = &Fn.getRegInfo();
}

bool HexagonConstEvaluator::evaluate(const MachineInstr &MI,
      const CellMap &Inputs, CellMap &Outputs) {
  if (MI.isCall())
    return false;
  if (MI.getNumOperands() == 0 || !MI.getOperand(0).isReg())
    return false;
  const MachineOperand &MD = MI.getOperand(0);
  if (!MD.isDef())
    return false;

  unsigned Opc = MI.getOpcode();
  Register DefR(MD);
  assert(!DefR.SubReg);
  if (!TargetRegisterInfo::isVirtualRegister(DefR.Reg))
    return false;

  if (MI.isCopy()) {
    LatticeCell RC;
    Register SrcR(MI.getOperand(1));
    bool Eval = evaluateCOPY(SrcR, Inputs, RC);
    if (!Eval)
      return false;
    Outputs.update(DefR.Reg, RC);
    return true;
  }
  if (MI.isRegSequence()) {
    unsigned Sub1 = MI.getOperand(2).getImm();
    unsigned Sub2 = MI.getOperand(4).getImm();
    const TargetRegisterClass &DefRC = *MRI->getRegClass(DefR.Reg);
    unsigned SubLo = HRI.getHexagonSubRegIndex(DefRC, Hexagon::ps_sub_lo);
    unsigned SubHi = HRI.getHexagonSubRegIndex(DefRC, Hexagon::ps_sub_hi);
    if (Sub1 != SubLo && Sub1 != SubHi)
      return false;
    if (Sub2 != SubLo && Sub2 != SubHi)
      return false;
    assert(Sub1 != Sub2);
    bool LoIs1 = (Sub1 == SubLo);
    const MachineOperand &OpLo = LoIs1 ? MI.getOperand(1) : MI.getOperand(3);
    const MachineOperand &OpHi = LoIs1 ? MI.getOperand(3) : MI.getOperand(1);
    LatticeCell RC;
    Register SrcRL(OpLo), SrcRH(OpHi);
    bool Eval = evaluateHexRSEQ32(SrcRL, SrcRH, Inputs, RC);
    if (!Eval)
      return false;
    Outputs.update(DefR.Reg, RC);
    return true;
  }
  if (MI.isCompare()) {
    bool Eval = evaluateHexCompare(MI, Inputs, Outputs);
    return Eval;
  }

  switch (Opc) {
    default:
      return false;
    case Hexagon::A2_tfrsi:
    case Hexagon::A2_tfrpi:
    case Hexagon::CONST32:
    case Hexagon::CONST64:
    {
      const MachineOperand &VO = MI.getOperand(1);
      // The operand of CONST32 can be a blockaddress, e.g.
      //   %0 = CONST32 <blockaddress(@eat, %l)>
      // Do this check for all instructions for safety.
      if (!VO.isImm())
        return false;
      int64_t V = MI.getOperand(1).getImm();
      unsigned W = getRegBitWidth(DefR.Reg);
      if (W != 32 && W != 64)
        return false;
      IntegerType *Ty = (W == 32) ? Type::getInt32Ty(CX)
                                  : Type::getInt64Ty(CX);
      const ConstantInt *CI = ConstantInt::get(Ty, V, true);
      LatticeCell RC = Outputs.get(DefR.Reg);
      RC.add(CI);
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::PS_true:
    case Hexagon::PS_false:
    {
      LatticeCell RC = Outputs.get(DefR.Reg);
      bool NonZero = (Opc == Hexagon::PS_true);
      uint32_t P = NonZero ? ConstantProperties::NonZero
                           : ConstantProperties::Zero;
      RC.add(P);
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::A2_and:
    case Hexagon::A2_andir:
    case Hexagon::A2_andp:
    case Hexagon::A2_or:
    case Hexagon::A2_orir:
    case Hexagon::A2_orp:
    case Hexagon::A2_xor:
    case Hexagon::A2_xorp:
    {
      bool Eval = evaluateHexLogical(MI, Inputs, Outputs);
      if (!Eval)
        return false;
      break;
    }

    case Hexagon::A2_combineii:  // combine(#s8Ext, #s8)
    case Hexagon::A4_combineii:  // combine(#s8, #u6Ext)
    {
      if (!MI.getOperand(1).isImm() || !MI.getOperand(2).isImm())
        return false;
      uint64_t Hi = MI.getOperand(1).getImm();
      uint64_t Lo = MI.getOperand(2).getImm();
      uint64_t Res = (Hi << 32) | (Lo & 0xFFFFFFFF);
      IntegerType *Ty = Type::getInt64Ty(CX);
      const ConstantInt *CI = ConstantInt::get(Ty, Res, false);
      LatticeCell RC = Outputs.get(DefR.Reg);
      RC.add(CI);
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::S2_setbit_i:
    {
      int64_t B = MI.getOperand(2).getImm();
      assert(B >=0 && B < 32);
      APInt A(32, (1ull << B), false);
      Register R(MI.getOperand(1));
      LatticeCell RC = Outputs.get(DefR.Reg);
      bool Eval = evaluateORri(R, A, Inputs, RC);
      if (!Eval)
        return false;
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::C2_mux:
    case Hexagon::C2_muxir:
    case Hexagon::C2_muxri:
    case Hexagon::C2_muxii:
    {
      bool Eval = evaluateHexCondMove(MI, Inputs, Outputs);
      if (!Eval)
        return false;
      break;
    }

    case Hexagon::A2_sxtb:
    case Hexagon::A2_sxth:
    case Hexagon::A2_sxtw:
    case Hexagon::A2_zxtb:
    case Hexagon::A2_zxth:
    {
      bool Eval = evaluateHexExt(MI, Inputs, Outputs);
      if (!Eval)
        return false;
      break;
    }

    case Hexagon::S2_ct0:
    case Hexagon::S2_ct0p:
    case Hexagon::S2_ct1:
    case Hexagon::S2_ct1p:
    {
      using namespace Hexagon;

      bool Ones = (Opc == S2_ct1) || (Opc == S2_ct1p);
      Register R1(MI.getOperand(1));
      assert(Inputs.has(R1.Reg));
      LatticeCell T;
      bool Eval = evaluateCTBr(R1, !Ones, Ones, Inputs, T);
      if (!Eval)
        return false;
      // All of these instructions return a 32-bit value. The evaluate
      // will generate the same type as the operand, so truncate the
      // result if necessary.
      APInt C;
      LatticeCell RC = Outputs.get(DefR.Reg);
      for (unsigned i = 0; i < T.size(); ++i) {
        const Constant *CI = T.Values[i];
        if (constToInt(CI, C) && C.getBitWidth() > 32)
          CI = intToConst(C.trunc(32));
        RC.add(CI);
      }
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::S2_cl0:
    case Hexagon::S2_cl0p:
    case Hexagon::S2_cl1:
    case Hexagon::S2_cl1p:
    case Hexagon::S2_clb:
    case Hexagon::S2_clbp:
    {
      using namespace Hexagon;

      bool OnlyZeros = (Opc == S2_cl0) || (Opc == S2_cl0p);
      bool OnlyOnes =  (Opc == S2_cl1) || (Opc == S2_cl1p);
      Register R1(MI.getOperand(1));
      assert(Inputs.has(R1.Reg));
      LatticeCell T;
      bool Eval = evaluateCLBr(R1, !OnlyOnes, !OnlyZeros, Inputs, T);
      if (!Eval)
        return false;
      // All of these instructions return a 32-bit value. The evaluate
      // will generate the same type as the operand, so truncate the
      // result if necessary.
      APInt C;
      LatticeCell RC = Outputs.get(DefR.Reg);
      for (unsigned i = 0; i < T.size(); ++i) {
        const Constant *CI = T.Values[i];
        if (constToInt(CI, C) && C.getBitWidth() > 32)
          CI = intToConst(C.trunc(32));
        RC.add(CI);
      }
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::S4_extract:
    case Hexagon::S4_extractp:
    case Hexagon::S2_extractu:
    case Hexagon::S2_extractup:
    {
      bool Signed = (Opc == Hexagon::S4_extract) ||
                    (Opc == Hexagon::S4_extractp);
      Register R1(MI.getOperand(1));
      unsigned BW = getRegBitWidth(R1.Reg);
      unsigned Bits = MI.getOperand(2).getImm();
      unsigned Offset = MI.getOperand(3).getImm();
      LatticeCell RC = Outputs.get(DefR.Reg);
      if (Offset >= BW) {
        APInt Zero(BW, 0, false);
        RC.add(intToConst(Zero));
        break;
      }
      if (Offset+Bits > BW) {
        // If the requested bitfield extends beyond the most significant bit,
        // the extra bits are treated as 0s. To emulate this behavior, reduce
        // the number of requested bits, and make the extract unsigned.
        Bits = BW-Offset;
        Signed = false;
      }
      bool Eval = evaluateEXTRACTr(R1, BW, Bits, Offset, Signed, Inputs, RC);
      if (!Eval)
        return false;
      Outputs.update(DefR.Reg, RC);
      break;
    }

    case Hexagon::S2_vsplatrb:
    case Hexagon::S2_vsplatrh:
    // vabsh, vabsh:sat
    // vabsw, vabsw:sat
    // vconj:sat
    // vrndwh, vrndwh:sat
    // vsathb, vsathub, vsatwuh
    // vsxtbh, vsxthw
    // vtrunehb, vtrunohb
    // vzxtbh, vzxthw
    {
      bool Eval = evaluateHexVector1(MI, Inputs, Outputs);
      if (!Eval)
        return false;
      break;
    }

    // TODO:
    // A2_vaddh
    // A2_vaddhs
    // A2_vaddw
    // A2_vaddws
  }

  return true;
}

bool HexagonConstEvaluator::evaluate(const Register &R,
      const LatticeCell &Input, LatticeCell &Result) {
  if (!R.SubReg) {
    Result = Input;
    return true;
  }
  const TargetRegisterClass *RC = MRI->getRegClass(R.Reg);
  if (RC != &Hexagon::DoubleRegsRegClass)
    return false;
  if (R.SubReg != Hexagon::isub_lo && R.SubReg != Hexagon::isub_hi)
    return false;

  assert(!Input.isTop());
  if (Input.isBottom())
    return false;

  using P = ConstantProperties;

  if (Input.isProperty()) {
    uint32_t Ps = Input.properties();
    if (Ps & (P::Zero|P::NaN)) {
      uint32_t Ns = (Ps & (P::Zero|P::NaN|P::SignProperties));
      Result.add(Ns);
      return true;
    }
    if (R.SubReg == Hexagon::isub_hi) {
      uint32_t Ns = (Ps & P::SignProperties);
      Result.add(Ns);
      return true;
    }
    return false;
  }

  // The Input cell contains some known values. Pick the word corresponding
  // to the subregister.
  APInt A;
  for (unsigned i = 0; i < Input.size(); ++i) {
    const Constant *C = Input.Values[i];
    if (!constToInt(C, A))
      return false;
    if (!A.isIntN(64))
      return false;
    uint64_t U = A.getZExtValue();
    if (R.SubReg == Hexagon::isub_hi)
      U >>= 32;
    U &= 0xFFFFFFFFULL;
    uint32_t U32 = Lo_32(U);
    int32_t V32;
    memcpy(&V32, &U32, sizeof V32);
    IntegerType *Ty = Type::getInt32Ty(CX);
    const ConstantInt *C32 = ConstantInt::get(Ty, static_cast<int64_t>(V32));
    Result.add(C32);
  }
  return true;
}

bool HexagonConstEvaluator::evaluate(const MachineInstr &BrI,
      const CellMap &Inputs, SetVector<const MachineBasicBlock*> &Targets,
      bool &FallsThru) {
  // We need to evaluate one branch at a time. TII::analyzeBranch checks
  // all the branches in a basic block at once, so we cannot use it.
  unsigned Opc = BrI.getOpcode();
  bool SimpleBranch = false;
  bool Negated = false;
  switch (Opc) {
    case Hexagon::J2_jumpf:
    case Hexagon::J2_jumpfnew:
    case Hexagon::J2_jumpfnewpt:
      Negated = true;
      LLVM_FALLTHROUGH;
    case Hexagon::J2_jumpt:
    case Hexagon::J2_jumptnew:
    case Hexagon::J2_jumptnewpt:
      // Simple branch:  if([!]Pn) jump ...
      // i.e. Op0 = predicate, Op1 = branch target.
      SimpleBranch = true;
      break;
    case Hexagon::J2_jump:
      Targets.insert(BrI.getOperand(0).getMBB());
      FallsThru = false;
      return true;
    default:
Undetermined:
      // If the branch is of unknown type, assume that all successors are
      // executable.
      FallsThru = !BrI.isUnconditionalBranch();
      return false;
  }

  if (SimpleBranch) {
    const MachineOperand &MD = BrI.getOperand(0);
    Register PR(MD);
    // If the condition operand has a subregister, this is not something
    // we currently recognize.
    if (PR.SubReg)
      goto Undetermined;
    assert(Inputs.has(PR.Reg));
    const LatticeCell &PredC = Inputs.get(PR.Reg);
    if (PredC.isBottom())
      goto Undetermined;

    uint32_t Props = PredC.properties();
    bool CTrue = false, CFalse = false;
    if (Props & ConstantProperties::Zero)
      CFalse = true;
    else if (Props & ConstantProperties::NonZero)
      CTrue = true;
    // If the condition is not known to be either, bail out.
    if (!CTrue && !CFalse)
      goto Undetermined;

    const MachineBasicBlock *BranchTarget = BrI.getOperand(1).getMBB();

    FallsThru = false;
    if ((!Negated && CTrue) || (Negated && CFalse))
      Targets.insert(BranchTarget);
    else if ((!Negated && CFalse) || (Negated && CTrue))
      FallsThru = true;
    else
      goto Undetermined;
  }

  return true;
}

bool HexagonConstEvaluator::rewrite(MachineInstr &MI, const CellMap &Inputs) {
  if (MI.isBranch())
    return rewriteHexBranch(MI, Inputs);

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    default:
      break;
    case Hexagon::A2_tfrsi:
    case Hexagon::A2_tfrpi:
    case Hexagon::CONST32:
    case Hexagon::CONST64:
    case Hexagon::PS_true:
    case Hexagon::PS_false:
      return false;
  }

  unsigned NumOp = MI.getNumOperands();
  if (NumOp == 0)
    return false;

  bool AllDefs, Changed;
  Changed = rewriteHexConstDefs(MI, Inputs, AllDefs);
  // If not all defs have been rewritten (i.e. the instruction defines
  // a register that is not compile-time constant), then try to rewrite
  // register operands that are known to be constant with immediates.
  if (!AllDefs)
    Changed |= rewriteHexConstUses(MI, Inputs);

  return Changed;
}

unsigned HexagonConstEvaluator::getRegBitWidth(unsigned Reg) const {
  const TargetRegisterClass *RC = MRI->getRegClass(Reg);
  if (Hexagon::IntRegsRegClass.hasSubClassEq(RC))
    return 32;
  if (Hexagon::DoubleRegsRegClass.hasSubClassEq(RC))
    return 64;
  if (Hexagon::PredRegsRegClass.hasSubClassEq(RC))
    return 8;
  llvm_unreachable("Invalid register");
  return 0;
}

uint32_t HexagonConstEvaluator::getCmp(unsigned Opc) {
  switch (Opc) {
    case Hexagon::C2_cmpeq:
    case Hexagon::C2_cmpeqp:
    case Hexagon::A4_cmpbeq:
    case Hexagon::A4_cmpheq:
    case Hexagon::A4_cmpbeqi:
    case Hexagon::A4_cmpheqi:
    case Hexagon::C2_cmpeqi:
    case Hexagon::J4_cmpeqn1_t_jumpnv_nt:
    case Hexagon::J4_cmpeqn1_t_jumpnv_t:
    case Hexagon::J4_cmpeqi_t_jumpnv_nt:
    case Hexagon::J4_cmpeqi_t_jumpnv_t:
    case Hexagon::J4_cmpeq_t_jumpnv_nt:
    case Hexagon::J4_cmpeq_t_jumpnv_t:
      return Comparison::EQ;

    case Hexagon::C4_cmpneq:
    case Hexagon::C4_cmpneqi:
    case Hexagon::J4_cmpeqn1_f_jumpnv_nt:
    case Hexagon::J4_cmpeqn1_f_jumpnv_t:
    case Hexagon::J4_cmpeqi_f_jumpnv_nt:
    case Hexagon::J4_cmpeqi_f_jumpnv_t:
    case Hexagon::J4_cmpeq_f_jumpnv_nt:
    case Hexagon::J4_cmpeq_f_jumpnv_t:
      return Comparison::NE;

    case Hexagon::C2_cmpgt:
    case Hexagon::C2_cmpgtp:
    case Hexagon::A4_cmpbgt:
    case Hexagon::A4_cmphgt:
    case Hexagon::A4_cmpbgti:
    case Hexagon::A4_cmphgti:
    case Hexagon::C2_cmpgti:
    case Hexagon::J4_cmpgtn1_t_jumpnv_nt:
    case Hexagon::J4_cmpgtn1_t_jumpnv_t:
    case Hexagon::J4_cmpgti_t_jumpnv_nt:
    case Hexagon::J4_cmpgti_t_jumpnv_t:
    case Hexagon::J4_cmpgt_t_jumpnv_nt:
    case Hexagon::J4_cmpgt_t_jumpnv_t:
      return Comparison::GTs;

    case Hexagon::C4_cmplte:
    case Hexagon::C4_cmpltei:
    case Hexagon::J4_cmpgtn1_f_jumpnv_nt:
    case Hexagon::J4_cmpgtn1_f_jumpnv_t:
    case Hexagon::J4_cmpgti_f_jumpnv_nt:
    case Hexagon::J4_cmpgti_f_jumpnv_t:
    case Hexagon::J4_cmpgt_f_jumpnv_nt:
    case Hexagon::J4_cmpgt_f_jumpnv_t:
      return Comparison::LEs;

    case Hexagon::C2_cmpgtu:
    case Hexagon::C2_cmpgtup:
    case Hexagon::A4_cmpbgtu:
    case Hexagon::A4_cmpbgtui:
    case Hexagon::A4_cmphgtu:
    case Hexagon::A4_cmphgtui:
    case Hexagon::C2_cmpgtui:
    case Hexagon::J4_cmpgtui_t_jumpnv_nt:
    case Hexagon::J4_cmpgtui_t_jumpnv_t:
    case Hexagon::J4_cmpgtu_t_jumpnv_nt:
    case Hexagon::J4_cmpgtu_t_jumpnv_t:
      return Comparison::GTu;

    case Hexagon::J4_cmpltu_f_jumpnv_nt:
    case Hexagon::J4_cmpltu_f_jumpnv_t:
      return Comparison::GEu;

    case Hexagon::J4_cmpltu_t_jumpnv_nt:
    case Hexagon::J4_cmpltu_t_jumpnv_t:
      return Comparison::LTu;

    case Hexagon::J4_cmplt_f_jumpnv_nt:
    case Hexagon::J4_cmplt_f_jumpnv_t:
      return Comparison::GEs;

    case Hexagon::C4_cmplteu:
    case Hexagon::C4_cmplteui:
    case Hexagon::J4_cmpgtui_f_jumpnv_nt:
    case Hexagon::J4_cmpgtui_f_jumpnv_t:
    case Hexagon::J4_cmpgtu_f_jumpnv_nt:
    case Hexagon::J4_cmpgtu_f_jumpnv_t:
      return Comparison::LEu;

    case Hexagon::J4_cmplt_t_jumpnv_nt:
    case Hexagon::J4_cmplt_t_jumpnv_t:
      return Comparison::LTs;

    default:
      break;
  }
  return Comparison::Unk;
}

APInt HexagonConstEvaluator::getCmpImm(unsigned Opc, unsigned OpX,
      const MachineOperand &MO) {
  bool Signed = false;
  switch (Opc) {
    case Hexagon::A4_cmpbgtui:   // u7
    case Hexagon::A4_cmphgtui:   // u7
      break;
    case Hexagon::A4_cmpheqi:    // s8
    case Hexagon::C4_cmpneqi:   // s8
      Signed = true;
      break;
    case Hexagon::A4_cmpbeqi:    // u8
      break;
    case Hexagon::C2_cmpgtui:      // u9
    case Hexagon::C4_cmplteui:  // u9
      break;
    case Hexagon::C2_cmpeqi:       // s10
    case Hexagon::C2_cmpgti:       // s10
    case Hexagon::C4_cmpltei:   // s10
      Signed = true;
      break;
    case Hexagon::J4_cmpeqi_f_jumpnv_nt:   // u5
    case Hexagon::J4_cmpeqi_f_jumpnv_t:    // u5
    case Hexagon::J4_cmpeqi_t_jumpnv_nt:   // u5
    case Hexagon::J4_cmpeqi_t_jumpnv_t:    // u5
    case Hexagon::J4_cmpgti_f_jumpnv_nt:   // u5
    case Hexagon::J4_cmpgti_f_jumpnv_t:    // u5
    case Hexagon::J4_cmpgti_t_jumpnv_nt:   // u5
    case Hexagon::J4_cmpgti_t_jumpnv_t:    // u5
    case Hexagon::J4_cmpgtui_f_jumpnv_nt:  // u5
    case Hexagon::J4_cmpgtui_f_jumpnv_t:   // u5
    case Hexagon::J4_cmpgtui_t_jumpnv_nt:  // u5
    case Hexagon::J4_cmpgtui_t_jumpnv_t:   // u5
      break;
    default:
      llvm_unreachable("Unhandled instruction");
      break;
  }

  uint64_t Val = MO.getImm();
  return APInt(32, Val, Signed);
}

void HexagonConstEvaluator::replaceWithNop(MachineInstr &MI) {
  MI.setDesc(HII.get(Hexagon::A2_nop));
  while (MI.getNumOperands() > 0)
    MI.RemoveOperand(0);
}

bool HexagonConstEvaluator::evaluateHexRSEQ32(Register RL, Register RH,
      const CellMap &Inputs, LatticeCell &Result) {
  assert(Inputs.has(RL.Reg) && Inputs.has(RH.Reg));
  LatticeCell LSL, LSH;
  if (!getCell(RL, Inputs, LSL) || !getCell(RH, Inputs, LSH))
    return false;
  if (LSL.isProperty() || LSH.isProperty())
    return false;

  unsigned LN = LSL.size(), HN = LSH.size();
  SmallVector<APInt,4> LoVs(LN), HiVs(HN);
  for (unsigned i = 0; i < LN; ++i) {
    bool Eval = constToInt(LSL.Values[i], LoVs[i]);
    if (!Eval)
      return false;
    assert(LoVs[i].getBitWidth() == 32);
  }
  for (unsigned i = 0; i < HN; ++i) {
    bool Eval = constToInt(LSH.Values[i], HiVs[i]);
    if (!Eval)
      return false;
    assert(HiVs[i].getBitWidth() == 32);
  }

  for (unsigned i = 0; i < HiVs.size(); ++i) {
    APInt HV = HiVs[i].zextOrSelf(64) << 32;
    for (unsigned j = 0; j < LoVs.size(); ++j) {
      APInt LV = LoVs[j].zextOrSelf(64);
      const Constant *C = intToConst(HV | LV);
      Result.add(C);
      if (Result.isBottom())
        return false;
    }
  }
  return !Result.isBottom();
}

bool HexagonConstEvaluator::evaluateHexCompare(const MachineInstr &MI,
      const CellMap &Inputs, CellMap &Outputs) {
  unsigned Opc = MI.getOpcode();
  bool Classic = false;
  switch (Opc) {
    case Hexagon::C2_cmpeq:
    case Hexagon::C2_cmpeqp:
    case Hexagon::C2_cmpgt:
    case Hexagon::C2_cmpgtp:
    case Hexagon::C2_cmpgtu:
    case Hexagon::C2_cmpgtup:
    case Hexagon::C2_cmpeqi:
    case Hexagon::C2_cmpgti:
    case Hexagon::C2_cmpgtui:
      // Classic compare:  Dst0 = CMP Src1, Src2
      Classic = true;
      break;
    default:
      // Not handling other compare instructions now.
      return false;
  }

  if (Classic) {
    const MachineOperand &Src1 = MI.getOperand(1);
    const MachineOperand &Src2 = MI.getOperand(2);

    bool Result;
    unsigned Opc = MI.getOpcode();
    bool Computed = evaluateHexCompare2(Opc, Src1, Src2, Inputs, Result);
    if (Computed) {
      // Only create a zero/non-zero cell. At this time there isn't really
      // much need for specific values.
      Register DefR(MI.getOperand(0));
      LatticeCell L = Outputs.get(DefR.Reg);
      uint32_t P = Result ? ConstantProperties::NonZero
                          : ConstantProperties::Zero;
      L.add(P);
      Outputs.update(DefR.Reg, L);
      return true;
    }
  }

  return false;
}

bool HexagonConstEvaluator::evaluateHexCompare2(unsigned Opc,
      const MachineOperand &Src1, const MachineOperand &Src2,
      const CellMap &Inputs, bool &Result) {
  uint32_t Cmp = getCmp(Opc);
  bool Reg1 = Src1.isReg(), Reg2 = Src2.isReg();
  bool Imm1 = Src1.isImm(), Imm2 = Src2.isImm();
  if (Reg1) {
    Register R1(Src1);
    if (Reg2) {
      Register R2(Src2);
      return evaluateCMPrr(Cmp, R1, R2, Inputs, Result);
    } else if (Imm2) {
      APInt A2 = getCmpImm(Opc, 2, Src2);
      return evaluateCMPri(Cmp, R1, A2, Inputs, Result);
    }
  } else if (Imm1) {
    APInt A1 = getCmpImm(Opc, 1, Src1);
    if (Reg2) {
      Register R2(Src2);
      uint32_t NegCmp = Comparison::negate(Cmp);
      return evaluateCMPri(NegCmp, R2, A1, Inputs, Result);
    } else if (Imm2) {
      APInt A2 = getCmpImm(Opc, 2, Src2);
      return evaluateCMPii(Cmp, A1, A2, Result);
    }
  }
  // Unknown kind of comparison.
  return false;
}

bool HexagonConstEvaluator::evaluateHexLogical(const MachineInstr &MI,
      const CellMap &Inputs, CellMap &Outputs) {
  unsigned Opc = MI.getOpcode();
  if (MI.getNumOperands() != 3)
    return false;
  const MachineOperand &Src1 = MI.getOperand(1);
  const MachineOperand &Src2 = MI.getOperand(2);
  Register R1(Src1);
  bool Eval = false;
  LatticeCell RC;
  switch (Opc) {
    default:
      return false;
    case Hexagon::A2_and:
    case Hexagon::A2_andp:
      Eval = evaluateANDrr(R1, Register(Src2), Inputs, RC);
      break;
    case Hexagon::A2_andir: {
      if (!Src2.isImm())
        return false;
      APInt A(32, Src2.getImm(), true);
      Eval = evaluateANDri(R1, A, Inputs, RC);
      break;
    }
    case Hexagon::A2_or:
    case Hexagon::A2_orp:
      Eval = evaluateORrr(R1, Register(Src2), Inputs, RC);
      break;
    case Hexagon::A2_orir: {
      if (!Src2.isImm())
        return false;
      APInt A(32, Src2.getImm(), true);
      Eval = evaluateORri(R1, A, Inputs, RC);
      break;
    }
    case Hexagon::A2_xor:
    case Hexagon::A2_xorp:
      Eval = evaluateXORrr(R1, Register(Src2), Inputs, RC);
      break;
  }
  if (Eval) {
    Register DefR(MI.getOperand(0));
    Outputs.update(DefR.Reg, RC);
  }
  return Eval;
}

bool HexagonConstEvaluator::evaluateHexCondMove(const MachineInstr &MI,
      const CellMap &Inputs, CellMap &Outputs) {
  // Dst0 = Cond1 ? Src2 : Src3
  Register CR(MI.getOperand(1));
  assert(Inputs.has(CR.Reg));
  LatticeCell LS;
  if (!getCell(CR, Inputs, LS))
    return false;
  uint32_t Ps = LS.properties();
  unsigned TakeOp;
  if (Ps & ConstantProperties::Zero)
    TakeOp = 3;
  else if (Ps & ConstantProperties::NonZero)
    TakeOp = 2;
  else
    return false;

  const MachineOperand &ValOp = MI.getOperand(TakeOp);
  Register DefR(MI.getOperand(0));
  LatticeCell RC = Outputs.get(DefR.Reg);

  if (ValOp.isImm()) {
    int64_t V = ValOp.getImm();
    unsigned W = getRegBitWidth(DefR.Reg);
    APInt A(W, V, true);
    const Constant *C = intToConst(A);
    RC.add(C);
    Outputs.update(DefR.Reg, RC);
    return true;
  }
  if (ValOp.isReg()) {
    Register R(ValOp);
    const LatticeCell &LR = Inputs.get(R.Reg);
    LatticeCell LSR;
    if (!evaluate(R, LR, LSR))
      return false;
    RC.meet(LSR);
    Outputs.update(DefR.Reg, RC);
    return true;
  }
  return false;
}

bool HexagonConstEvaluator::evaluateHexExt(const MachineInstr &MI,
      const CellMap &Inputs, CellMap &Outputs) {
  // Dst0 = ext R1
  Register R1(MI.getOperand(1));
  assert(Inputs.has(R1.Reg));

  unsigned Opc = MI.getOpcode();
  unsigned Bits;
  switch (Opc) {
    case Hexagon::A2_sxtb:
    case Hexagon::A2_zxtb:
      Bits = 8;
      break;
    case Hexagon::A2_sxth:
    case Hexagon::A2_zxth:
      Bits = 16;
      break;
    case Hexagon::A2_sxtw:
      Bits = 32;
      break;
  }

  bool Signed = false;
  switch (Opc) {
    case Hexagon::A2_sxtb:
    case Hexagon::A2_sxth:
    case Hexagon::A2_sxtw:
      Signed = true;
      break;
  }

  Register DefR(MI.getOperand(0));
  unsigned BW = getRegBitWidth(DefR.Reg);
  LatticeCell RC = Outputs.get(DefR.Reg);
  bool Eval = Signed ? evaluateSEXTr(R1, BW, Bits, Inputs, RC)
                     : evaluateZEXTr(R1, BW, Bits, Inputs, RC);
  if (!Eval)
    return false;
  Outputs.update(DefR.Reg, RC);
  return true;
}

bool HexagonConstEvaluator::evaluateHexVector1(const MachineInstr &MI,
      const CellMap &Inputs, CellMap &Outputs) {
  // DefR = op R1
  Register DefR(MI.getOperand(0));
  Register R1(MI.getOperand(1));
  assert(Inputs.has(R1.Reg));
  LatticeCell RC = Outputs.get(DefR.Reg);
  bool Eval;

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::S2_vsplatrb:
      // Rd = 4 times Rs:0..7
      Eval = evaluateSplatr(R1, 8, 4, Inputs, RC);
      break;
    case Hexagon::S2_vsplatrh:
      // Rdd = 4 times Rs:0..15
      Eval = evaluateSplatr(R1, 16, 4, Inputs, RC);
      break;
    default:
      return false;
  }

  if (!Eval)
    return false;
  Outputs.update(DefR.Reg, RC);
  return true;
}

bool HexagonConstEvaluator::rewriteHexConstDefs(MachineInstr &MI,
      const CellMap &Inputs, bool &AllDefs) {
  AllDefs = false;

  // Some diagnostics.
  // LLVM_DEBUG({...}) gets confused with all this code as an argument.
#ifndef NDEBUG
  bool Debugging = DebugFlag && isCurrentDebugType(DEBUG_TYPE);
  if (Debugging) {
    bool Const = true, HasUse = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isUse() || MO.isImplicit())
        continue;
      Register R(MO);
      if (!TargetRegisterInfo::isVirtualRegister(R.Reg))
        continue;
      HasUse = true;
      // PHIs can legitimately have "top" cells after propagation.
      if (!MI.isPHI() && !Inputs.has(R.Reg)) {
        dbgs() << "Top " << printReg(R.Reg, &HRI, R.SubReg)
               << " in MI: " << MI;
        continue;
      }
      const LatticeCell &L = Inputs.get(R.Reg);
      Const &= L.isSingle();
      if (!Const)
        break;
    }
    if (HasUse && Const) {
      if (!MI.isCopy()) {
        dbgs() << "CONST: " << MI;
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isReg() || !MO.isUse() || MO.isImplicit())
            continue;
          unsigned R = MO.getReg();
          dbgs() << printReg(R, &TRI) << ": " << Inputs.get(R) << "\n";
        }
      }
    }
  }
#endif

  // Avoid generating TFRIs for register transfers---this will keep the
  // coalescing opportunities.
  if (MI.isCopy())
    return false;

  // Collect all virtual register-def operands.
  SmallVector<unsigned,2> DefRegs;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned R = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(R))
      continue;
    assert(!MO.getSubReg());
    assert(Inputs.has(R));
    DefRegs.push_back(R);
  }

  MachineBasicBlock &B = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  unsigned ChangedNum = 0;
#ifndef NDEBUG
  SmallVector<const MachineInstr*,4> NewInstrs;
#endif

  // For each defined register, if it is a constant, create an instruction
  //   NewR = const
  // and replace all uses of the defined register with NewR.
  for (unsigned i = 0, n = DefRegs.size(); i < n; ++i) {
    unsigned R = DefRegs[i];
    const LatticeCell &L = Inputs.get(R);
    if (L.isBottom())
      continue;
    const TargetRegisterClass *RC = MRI->getRegClass(R);
    MachineBasicBlock::iterator At = MI.getIterator();

    if (!L.isSingle()) {
      // If this a zero/non-zero cell, we can fold a definition
      // of a predicate register.
      using P = ConstantProperties;

      uint64_t Ps = L.properties();
      if (!(Ps & (P::Zero|P::NonZero)))
        continue;
      const TargetRegisterClass *PredRC = &Hexagon::PredRegsRegClass;
      if (RC != PredRC)
        continue;
      const MCInstrDesc *NewD = (Ps & P::Zero) ?
        &HII.get(Hexagon::PS_false) :
        &HII.get(Hexagon::PS_true);
      unsigned NewR = MRI->createVirtualRegister(PredRC);
      const MachineInstrBuilder &MIB = BuildMI(B, At, DL, *NewD, NewR);
      (void)MIB;
#ifndef NDEBUG
      NewInstrs.push_back(&*MIB);
#endif
      replaceAllRegUsesWith(R, NewR);
    } else {
      // This cell has a single value.
      APInt A;
      if (!constToInt(L.Value, A) || !A.isSignedIntN(64))
        continue;
      const TargetRegisterClass *NewRC;
      const MCInstrDesc *NewD;

      unsigned W = getRegBitWidth(R);
      int64_t V = A.getSExtValue();
      assert(W == 32 || W == 64);
      if (W == 32)
        NewRC = &Hexagon::IntRegsRegClass;
      else
        NewRC = &Hexagon::DoubleRegsRegClass;
      unsigned NewR = MRI->createVirtualRegister(NewRC);
      const MachineInstr *NewMI;

      if (W == 32) {
        NewD = &HII.get(Hexagon::A2_tfrsi);
        NewMI = BuildMI(B, At, DL, *NewD, NewR)
                  .addImm(V);
      } else {
        if (A.isSignedIntN(8)) {
          NewD = &HII.get(Hexagon::A2_tfrpi);
          NewMI = BuildMI(B, At, DL, *NewD, NewR)
                    .addImm(V);
        } else {
          int32_t Hi = V >> 32;
          int32_t Lo = V & 0xFFFFFFFFLL;
          if (isInt<8>(Hi) && isInt<8>(Lo)) {
            NewD = &HII.get(Hexagon::A2_combineii);
            NewMI = BuildMI(B, At, DL, *NewD, NewR)
                      .addImm(Hi)
                      .addImm(Lo);
          } else {
            NewD = &HII.get(Hexagon::CONST64);
            NewMI = BuildMI(B, At, DL, *NewD, NewR)
                      .addImm(V);
          }
        }
      }
      (void)NewMI;
#ifndef NDEBUG
      NewInstrs.push_back(NewMI);
#endif
      replaceAllRegUsesWith(R, NewR);
    }
    ChangedNum++;
  }

  LLVM_DEBUG({
    if (!NewInstrs.empty()) {
      MachineFunction &MF = *MI.getParent()->getParent();
      dbgs() << "In function: " << MF.getName() << "\n";
      dbgs() << "Rewrite: for " << MI << "  created " << *NewInstrs[0];
      for (unsigned i = 1; i < NewInstrs.size(); ++i)
        dbgs() << "          " << *NewInstrs[i];
    }
  });

  AllDefs = (ChangedNum == DefRegs.size());
  return ChangedNum > 0;
}

bool HexagonConstEvaluator::rewriteHexConstUses(MachineInstr &MI,
      const CellMap &Inputs) {
  bool Changed = false;
  unsigned Opc = MI.getOpcode();
  MachineBasicBlock &B = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator At = MI.getIterator();
  MachineInstr *NewMI = nullptr;

  switch (Opc) {
    case Hexagon::M2_maci:
    // Convert DefR += mpyi(R2, R3)
    //   to   DefR += mpyi(R, #imm),
    //   or   DefR -= mpyi(R, #imm).
    {
      Register DefR(MI.getOperand(0));
      assert(!DefR.SubReg);
      Register R2(MI.getOperand(2));
      Register R3(MI.getOperand(3));
      assert(Inputs.has(R2.Reg) && Inputs.has(R3.Reg));
      LatticeCell LS2, LS3;
      // It is enough to get one of the input cells, since we will only try
      // to replace one argument---whichever happens to be a single constant.
      bool HasC2 = getCell(R2, Inputs, LS2), HasC3 = getCell(R3, Inputs, LS3);
      if (!HasC2 && !HasC3)
        return false;
      bool Zero = ((HasC2 && (LS2.properties() & ConstantProperties::Zero)) ||
                   (HasC3 && (LS3.properties() & ConstantProperties::Zero)));
      // If one of the operands is zero, eliminate the multiplication.
      if (Zero) {
        // DefR == R1 (tied operands).
        MachineOperand &Acc = MI.getOperand(1);
        Register R1(Acc);
        unsigned NewR = R1.Reg;
        if (R1.SubReg) {
          // Generate COPY. FIXME: Replace with the register:subregister.
          const TargetRegisterClass *RC = MRI->getRegClass(DefR.Reg);
          NewR = MRI->createVirtualRegister(RC);
          NewMI = BuildMI(B, At, DL, HII.get(TargetOpcode::COPY), NewR)
                    .addReg(R1.Reg, getRegState(Acc), R1.SubReg);
        }
        replaceAllRegUsesWith(DefR.Reg, NewR);
        MRI->clearKillFlags(NewR);
        Changed = true;
        break;
      }

      bool Swap = false;
      if (!LS3.isSingle()) {
        if (!LS2.isSingle())
          return false;
        Swap = true;
      }
      const LatticeCell &LI = Swap ? LS2 : LS3;
      const MachineOperand &OpR2 = Swap ? MI.getOperand(3)
                                        : MI.getOperand(2);
      // LI is single here.
      APInt A;
      if (!constToInt(LI.Value, A) || !A.isSignedIntN(8))
        return false;
      int64_t V = A.getSExtValue();
      const MCInstrDesc &D = (V >= 0) ? HII.get(Hexagon::M2_macsip)
                                      : HII.get(Hexagon::M2_macsin);
      if (V < 0)
        V = -V;
      const TargetRegisterClass *RC = MRI->getRegClass(DefR.Reg);
      unsigned NewR = MRI->createVirtualRegister(RC);
      const MachineOperand &Src1 = MI.getOperand(1);
      NewMI = BuildMI(B, At, DL, D, NewR)
                .addReg(Src1.getReg(), getRegState(Src1), Src1.getSubReg())
                .addReg(OpR2.getReg(), getRegState(OpR2), OpR2.getSubReg())
                .addImm(V);
      replaceAllRegUsesWith(DefR.Reg, NewR);
      Changed = true;
      break;
    }

    case Hexagon::A2_and:
    {
      Register R1(MI.getOperand(1));
      Register R2(MI.getOperand(2));
      assert(Inputs.has(R1.Reg) && Inputs.has(R2.Reg));
      LatticeCell LS1, LS2;
      unsigned CopyOf = 0;
      // Check if any of the operands is -1 (i.e. all bits set).
      if (getCell(R1, Inputs, LS1) && LS1.isSingle()) {
        APInt M1;
        if (constToInt(LS1.Value, M1) && !~M1)
          CopyOf = 2;
      }
      else if (getCell(R2, Inputs, LS2) && LS2.isSingle()) {
        APInt M1;
        if (constToInt(LS2.Value, M1) && !~M1)
          CopyOf = 1;
      }
      if (!CopyOf)
        return false;
      MachineOperand &SO = MI.getOperand(CopyOf);
      Register SR(SO);
      Register DefR(MI.getOperand(0));
      unsigned NewR = SR.Reg;
      if (SR.SubReg) {
        const TargetRegisterClass *RC = MRI->getRegClass(DefR.Reg);
        NewR = MRI->createVirtualRegister(RC);
        NewMI = BuildMI(B, At, DL, HII.get(TargetOpcode::COPY), NewR)
                  .addReg(SR.Reg, getRegState(SO), SR.SubReg);
      }
      replaceAllRegUsesWith(DefR.Reg, NewR);
      MRI->clearKillFlags(NewR);
      Changed = true;
    }
    break;

    case Hexagon::A2_or:
    {
      Register R1(MI.getOperand(1));
      Register R2(MI.getOperand(2));
      assert(Inputs.has(R1.Reg) && Inputs.has(R2.Reg));
      LatticeCell LS1, LS2;
      unsigned CopyOf = 0;

      using P = ConstantProperties;

      if (getCell(R1, Inputs, LS1) && (LS1.properties() & P::Zero))
        CopyOf = 2;
      else if (getCell(R2, Inputs, LS2) && (LS2.properties() & P::Zero))
        CopyOf = 1;
      if (!CopyOf)
        return false;
      MachineOperand &SO = MI.getOperand(CopyOf);
      Register SR(SO);
      Register DefR(MI.getOperand(0));
      unsigned NewR = SR.Reg;
      if (SR.SubReg) {
        const TargetRegisterClass *RC = MRI->getRegClass(DefR.Reg);
        NewR = MRI->createVirtualRegister(RC);
        NewMI = BuildMI(B, At, DL, HII.get(TargetOpcode::COPY), NewR)
                  .addReg(SR.Reg, getRegState(SO), SR.SubReg);
      }
      replaceAllRegUsesWith(DefR.Reg, NewR);
      MRI->clearKillFlags(NewR);
      Changed = true;
    }
    break;
  }

  if (NewMI) {
    // clear all the kill flags of this new instruction.
    for (MachineOperand &MO : NewMI->operands())
      if (MO.isReg() && MO.isUse())
        MO.setIsKill(false);
  }

  LLVM_DEBUG({
    if (NewMI) {
      dbgs() << "Rewrite: for " << MI;
      if (NewMI != &MI)
        dbgs() << "  created " << *NewMI;
      else
        dbgs() << "  modified the instruction itself and created:" << *NewMI;
    }
  });

  return Changed;
}

void HexagonConstEvaluator::replaceAllRegUsesWith(unsigned FromReg,
      unsigned ToReg) {
  assert(TargetRegisterInfo::isVirtualRegister(FromReg));
  assert(TargetRegisterInfo::isVirtualRegister(ToReg));
  for (auto I = MRI->use_begin(FromReg), E = MRI->use_end(); I != E;) {
    MachineOperand &O = *I;
    ++I;
    O.setReg(ToReg);
  }
}

bool HexagonConstEvaluator::rewriteHexBranch(MachineInstr &BrI,
      const CellMap &Inputs) {
  MachineBasicBlock &B = *BrI.getParent();
  unsigned NumOp = BrI.getNumOperands();
  if (!NumOp)
    return false;

  bool FallsThru;
  SetVector<const MachineBasicBlock*> Targets;
  bool Eval = evaluate(BrI, Inputs, Targets, FallsThru);
  unsigned NumTargets = Targets.size();
  if (!Eval || NumTargets > 1 || (NumTargets == 1 && FallsThru))
    return false;
  if (BrI.getOpcode() == Hexagon::J2_jump)
    return false;

  LLVM_DEBUG(dbgs() << "Rewrite(" << printMBBReference(B) << "):" << BrI);
  bool Rewritten = false;
  if (NumTargets > 0) {
    assert(!FallsThru && "This should have been checked before");
    // MIB.addMBB needs non-const pointer.
    MachineBasicBlock *TargetB = const_cast<MachineBasicBlock*>(Targets[0]);
    bool Moot = B.isLayoutSuccessor(TargetB);
    if (!Moot) {
      // If we build a branch here, we must make sure that it won't be
      // erased as "non-executable". We can't mark any new instructions
      // as executable here, so we need to overwrite the BrI, which we
      // know is executable.
      const MCInstrDesc &JD = HII.get(Hexagon::J2_jump);
      auto NI = BuildMI(B, BrI.getIterator(), BrI.getDebugLoc(), JD)
                  .addMBB(TargetB);
      BrI.setDesc(JD);
      while (BrI.getNumOperands() > 0)
        BrI.RemoveOperand(0);
      // This ensures that all implicit operands (e.g. implicit-def %r31, etc)
      // are present in the rewritten branch.
      for (auto &Op : NI->operands())
        BrI.addOperand(Op);
      NI->eraseFromParent();
      Rewritten = true;
    }
  }

  // Do not erase instructions. A newly created instruction could get
  // the same address as an instruction marked as executable during the
  // propagation.
  if (!Rewritten)
    replaceWithNop(BrI);
  return true;
}

FunctionPass *llvm::createHexagonConstPropagationPass() {
  return new HexagonConstPropagation();
}
