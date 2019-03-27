//===- BitTracker.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_BITTRACKER_H
#define LLVM_LIB_TARGET_HEXAGON_BITTRACKER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace llvm {

class BitVector;
class ConstantInt;
class MachineRegisterInfo;
class MachineBasicBlock;
class MachineFunction;
class raw_ostream;
class TargetRegisterClass;
class TargetRegisterInfo;

struct BitTracker {
  struct BitRef;
  struct RegisterRef;
  struct BitValue;
  struct BitMask;
  struct RegisterCell;
  struct MachineEvaluator;

  using BranchTargetList = SetVector<const MachineBasicBlock *>;
  using CellMapType = std::map<unsigned, RegisterCell>;

  BitTracker(const MachineEvaluator &E, MachineFunction &F);
  ~BitTracker();

  void run();
  void trace(bool On = false) { Trace = On; }
  bool has(unsigned Reg) const;
  const RegisterCell &lookup(unsigned Reg) const;
  RegisterCell get(RegisterRef RR) const;
  void put(RegisterRef RR, const RegisterCell &RC);
  void subst(RegisterRef OldRR, RegisterRef NewRR);
  bool reached(const MachineBasicBlock *B) const;
  void visit(const MachineInstr &MI);

  void print_cells(raw_ostream &OS) const;

private:
  void visitPHI(const MachineInstr &PI);
  void visitNonBranch(const MachineInstr &MI);
  void visitBranchesFrom(const MachineInstr &BI);
  void visitUsesOf(unsigned Reg);

  using CFGEdge = std::pair<int, int>;
  using EdgeSetType = std::set<CFGEdge>;
  using InstrSetType = std::set<const MachineInstr *>;
  using EdgeQueueType = std::queue<CFGEdge>;

  // Priority queue of instructions using modified registers, ordered by
  // their relative position in a basic block.
  struct UseQueueType {
    UseQueueType() : Uses(Dist) {}

    unsigned size() const {
      return Uses.size();
    }
    bool empty() const {
      return size() == 0;
    }
    MachineInstr *front() const {
      return Uses.top();
    }
    void push(MachineInstr *MI) {
      if (Set.insert(MI).second)
        Uses.push(MI);
    }
    void pop() {
      Set.erase(front());
      Uses.pop();
    }
    void reset() {
      Dist.clear();
    }
  private:
    struct Cmp {
      Cmp(DenseMap<const MachineInstr*,unsigned> &Map) : Dist(Map) {}
      bool operator()(const MachineInstr *MI, const MachineInstr *MJ) const;
      DenseMap<const MachineInstr*,unsigned> &Dist;
    };
    std::priority_queue<MachineInstr*, std::vector<MachineInstr*>, Cmp> Uses;
    DenseSet<const MachineInstr*> Set; // Set to avoid adding duplicate entries.
    DenseMap<const MachineInstr*,unsigned> Dist;
  };

  void reset();
  void runEdgeQueue(BitVector &BlockScanned);
  void runUseQueue();

  const MachineEvaluator &ME;
  MachineFunction &MF;
  MachineRegisterInfo &MRI;
  CellMapType &Map;

  EdgeSetType EdgeExec;         // Executable flow graph edges.
  InstrSetType InstrExec;       // Executable instructions.
  UseQueueType UseQ;            // Work queue of register uses.
  EdgeQueueType FlowQ;          // Work queue of CFG edges.
  DenseSet<unsigned> ReachedBB; // Cache of reached blocks.
  bool Trace;                   // Enable tracing for debugging.
};

// Abstraction of a reference to bit at position Pos from a register Reg.
struct BitTracker::BitRef {
  BitRef(unsigned R = 0, uint16_t P = 0) : Reg(R), Pos(P) {}

  bool operator== (const BitRef &BR) const {
    // If Reg is 0, disregard Pos.
    return Reg == BR.Reg && (Reg == 0 || Pos == BR.Pos);
  }

  unsigned Reg;
  uint16_t Pos;
};

// Abstraction of a register reference in MachineOperand.  It contains the
// register number and the subregister index.
struct BitTracker::RegisterRef {
  RegisterRef(unsigned R = 0, unsigned S = 0)
    : Reg(R), Sub(S) {}
  RegisterRef(const MachineOperand &MO)
      : Reg(MO.getReg()), Sub(MO.getSubReg()) {}

  unsigned Reg, Sub;
};

// Value that a single bit can take.  This is outside of the context of
// any register, it is more of an abstraction of the two-element set of
// possible bit values.  One extension here is the "Ref" type, which
// indicates that this bit takes the same value as the bit described by
// RefInfo.
struct BitTracker::BitValue {
  enum ValueType {
    Top,    // Bit not yet defined.
    Zero,   // Bit = 0.
    One,    // Bit = 1.
    Ref     // Bit value same as the one described in RefI.
    // Conceptually, there is no explicit "bottom" value: the lattice's
    // bottom will be expressed as a "ref to itself", which, in the context
    // of registers, could be read as "this value of this bit is defined by
    // this bit".
    // The ordering is:
    //   x <= Top,
    //   Self <= x, where "Self" is "ref to itself".
    // This makes the value lattice different for each virtual register
    // (even for each bit in the same virtual register), since the "bottom"
    // for one register will be a simple "ref" for another register.
    // Since we do not store the "Self" bit and register number, the meet
    // operation will need to take it as a parameter.
    //
    // In practice there is a special case for values that are not associa-
    // ted with any specific virtual register. An example would be a value
    // corresponding to a bit of a physical register, or an intermediate
    // value obtained in some computation (such as instruction evaluation).
    // Such cases are identical to the usual Ref type, but the register
    // number is 0. In such case the Pos field of the reference is ignored.
    //
    // What is worthy of notice is that in value V (that is a "ref"), as long
    // as the RefI.Reg is not 0, it may actually be the same register as the
    // one in which V will be contained.  If the RefI.Pos refers to the posi-
    // tion of V, then V is assumed to be "bottom" (as a "ref to itself"),
    // otherwise V is taken to be identical to the referenced bit of the
    // same register.
    // If RefI.Reg is 0, however, such a reference to the same register is
    // not possible.  Any value V that is a "ref", and whose RefI.Reg is 0
    // is treated as "bottom".
  };
  ValueType Type;
  BitRef RefI;

  BitValue(ValueType T = Top) : Type(T) {}
  BitValue(bool B) : Type(B ? One : Zero) {}
  BitValue(unsigned Reg, uint16_t Pos) : Type(Ref), RefI(Reg, Pos) {}

  bool operator== (const BitValue &V) const {
    if (Type != V.Type)
      return false;
    if (Type == Ref && !(RefI == V.RefI))
      return false;
    return true;
  }
  bool operator!= (const BitValue &V) const {
    return !operator==(V);
  }

  bool is(unsigned T) const {
    assert(T == 0 || T == 1);
    return T == 0 ? Type == Zero
                  : (T == 1 ? Type == One : false);
  }

  // The "meet" operation is the "." operation in a semilattice (L, ., T, B):
  // (1)  x.x = x
  // (2)  x.y = y.x
  // (3)  x.(y.z) = (x.y).z
  // (4)  x.T = x  (i.e. T = "top")
  // (5)  x.B = B  (i.e. B = "bottom")
  //
  // This "meet" function will update the value of the "*this" object with
  // the newly calculated one, and return "true" if the value of *this has
  // changed, and "false" otherwise.
  // To prove that it satisfies the conditions (1)-(5), it is sufficient
  // to show that a relation
  //   x <= y  <=>  x.y = x
  // defines a partial order (i.e. that "meet" is same as "infimum").
  bool meet(const BitValue &V, const BitRef &Self) {
    // First, check the cases where there is nothing to be done.
    if (Type == Ref && RefI == Self)    // Bottom.meet(V) = Bottom (i.e. This)
      return false;
    if (V.Type == Top)                  // This.meet(Top) = This
      return false;
    if (*this == V)                     // This.meet(This) = This
      return false;

    // At this point, we know that the value of "this" will change.
    // If it is Top, it will become the same as V, otherwise it will
    // become "bottom" (i.e. Self).
    if (Type == Top) {
      Type = V.Type;
      RefI = V.RefI;  // This may be irrelevant, but copy anyway.
      return true;
    }
    // Become "bottom".
    Type = Ref;
    RefI = Self;
    return true;
  }

  // Create a reference to the bit value V.
  static BitValue ref(const BitValue &V);
  // Create a "self".
  static BitValue self(const BitRef &Self = BitRef());

  bool num() const {
    return Type == Zero || Type == One;
  }

  operator bool() const {
    assert(Type == Zero || Type == One);
    return Type == One;
  }

  friend raw_ostream &operator<<(raw_ostream &OS, const BitValue &BV);
};

// This operation must be idempotent, i.e. ref(ref(V)) == ref(V).
inline BitTracker::BitValue
BitTracker::BitValue::ref(const BitValue &V) {
  if (V.Type != Ref)
    return BitValue(V.Type);
  if (V.RefI.Reg != 0)
    return BitValue(V.RefI.Reg, V.RefI.Pos);
  return self();
}

inline BitTracker::BitValue
BitTracker::BitValue::self(const BitRef &Self) {
  return BitValue(Self.Reg, Self.Pos);
}

// A sequence of bits starting from index B up to and including index E.
// If E < B, the mask represents two sections: [0..E] and [B..W) where
// W is the width of the register.
struct BitTracker::BitMask {
  BitMask() = default;
  BitMask(uint16_t b, uint16_t e) : B(b), E(e) {}

  uint16_t first() const { return B; }
  uint16_t last() const { return E; }

private:
  uint16_t B = 0;
  uint16_t E = 0;
};

// Representation of a register: a list of BitValues.
struct BitTracker::RegisterCell {
  RegisterCell(uint16_t Width = DefaultBitN) : Bits(Width) {}

  uint16_t width() const {
    return Bits.size();
  }

  const BitValue &operator[](uint16_t BitN) const {
    assert(BitN < Bits.size());
    return Bits[BitN];
  }
  BitValue &operator[](uint16_t BitN) {
    assert(BitN < Bits.size());
    return Bits[BitN];
  }

  bool meet(const RegisterCell &RC, unsigned SelfR);
  RegisterCell &insert(const RegisterCell &RC, const BitMask &M);
  RegisterCell extract(const BitMask &M) const;  // Returns a new cell.
  RegisterCell &rol(uint16_t Sh);    // Rotate left.
  RegisterCell &fill(uint16_t B, uint16_t E, const BitValue &V);
  RegisterCell &cat(const RegisterCell &RC);  // Concatenate.
  uint16_t cl(bool B) const;
  uint16_t ct(bool B) const;

  bool operator== (const RegisterCell &RC) const;
  bool operator!= (const RegisterCell &RC) const {
    return !operator==(RC);
  }

  // Replace the ref-to-reg-0 bit values with the given register.
  RegisterCell &regify(unsigned R);

  // Generate a "ref" cell for the corresponding register. In the resulting
  // cell each bit will be described as being the same as the corresponding
  // bit in register Reg (i.e. the cell is "defined" by register Reg).
  static RegisterCell self(unsigned Reg, uint16_t Width);
  // Generate a "top" cell of given size.
  static RegisterCell top(uint16_t Width);
  // Generate a cell that is a "ref" to another cell.
  static RegisterCell ref(const RegisterCell &C);

private:
  // The DefaultBitN is here only to avoid frequent reallocation of the
  // memory in the vector.
  static const unsigned DefaultBitN = 32;
  using BitValueList = SmallVector<BitValue, DefaultBitN>;
  BitValueList Bits;

  friend raw_ostream &operator<<(raw_ostream &OS, const RegisterCell &RC);
};

inline bool BitTracker::has(unsigned Reg) const {
  return Map.find(Reg) != Map.end();
}

inline const BitTracker::RegisterCell&
BitTracker::lookup(unsigned Reg) const {
  CellMapType::const_iterator F = Map.find(Reg);
  assert(F != Map.end());
  return F->second;
}

inline BitTracker::RegisterCell
BitTracker::RegisterCell::self(unsigned Reg, uint16_t Width) {
  RegisterCell RC(Width);
  for (uint16_t i = 0; i < Width; ++i)
    RC.Bits[i] = BitValue::self(BitRef(Reg, i));
  return RC;
}

inline BitTracker::RegisterCell
BitTracker::RegisterCell::top(uint16_t Width) {
  RegisterCell RC(Width);
  for (uint16_t i = 0; i < Width; ++i)
    RC.Bits[i] = BitValue(BitValue::Top);
  return RC;
}

inline BitTracker::RegisterCell
BitTracker::RegisterCell::ref(const RegisterCell &C) {
  uint16_t W = C.width();
  RegisterCell RC(W);
  for (unsigned i = 0; i < W; ++i)
    RC[i] = BitValue::ref(C[i]);
  return RC;
}

// A class to evaluate target's instructions and update the cell maps.
// This is used internally by the bit tracker.  A target that wants to
// utilize this should implement the evaluation functions (noted below)
// in a subclass of this class.
struct BitTracker::MachineEvaluator {
  MachineEvaluator(const TargetRegisterInfo &T, MachineRegisterInfo &M)
      : TRI(T), MRI(M) {}
  virtual ~MachineEvaluator() = default;

  uint16_t getRegBitWidth(const RegisterRef &RR) const;

  RegisterCell getCell(const RegisterRef &RR, const CellMapType &M) const;
  void putCell(const RegisterRef &RR, RegisterCell RC, CellMapType &M) const;

  // A result of any operation should use refs to the source cells, not
  // the cells directly. This function is a convenience wrapper to quickly
  // generate a ref for a cell corresponding to a register reference.
  RegisterCell getRef(const RegisterRef &RR, const CellMapType &M) const {
    RegisterCell RC = getCell(RR, M);
    return RegisterCell::ref(RC);
  }

  // Helper functions.
  // Check if a cell is an immediate value (i.e. all bits are either 0 or 1).
  bool isInt(const RegisterCell &A) const;
  // Convert cell to an immediate value.
  uint64_t toInt(const RegisterCell &A) const;

  // Generate cell from an immediate value.
  RegisterCell eIMM(int64_t V, uint16_t W) const;
  RegisterCell eIMM(const ConstantInt *CI) const;

  // Arithmetic.
  RegisterCell eADD(const RegisterCell &A1, const RegisterCell &A2) const;
  RegisterCell eSUB(const RegisterCell &A1, const RegisterCell &A2) const;
  RegisterCell eMLS(const RegisterCell &A1, const RegisterCell &A2) const;
  RegisterCell eMLU(const RegisterCell &A1, const RegisterCell &A2) const;

  // Shifts.
  RegisterCell eASL(const RegisterCell &A1, uint16_t Sh) const;
  RegisterCell eLSR(const RegisterCell &A1, uint16_t Sh) const;
  RegisterCell eASR(const RegisterCell &A1, uint16_t Sh) const;

  // Logical.
  RegisterCell eAND(const RegisterCell &A1, const RegisterCell &A2) const;
  RegisterCell eORL(const RegisterCell &A1, const RegisterCell &A2) const;
  RegisterCell eXOR(const RegisterCell &A1, const RegisterCell &A2) const;
  RegisterCell eNOT(const RegisterCell &A1) const;

  // Set bit, clear bit.
  RegisterCell eSET(const RegisterCell &A1, uint16_t BitN) const;
  RegisterCell eCLR(const RegisterCell &A1, uint16_t BitN) const;

  // Count leading/trailing bits (zeros/ones).
  RegisterCell eCLB(const RegisterCell &A1, bool B, uint16_t W) const;
  RegisterCell eCTB(const RegisterCell &A1, bool B, uint16_t W) const;

  // Sign/zero extension.
  RegisterCell eSXT(const RegisterCell &A1, uint16_t FromN) const;
  RegisterCell eZXT(const RegisterCell &A1, uint16_t FromN) const;

  // Extract/insert
  // XTR R,b,e:  extract bits from A1 starting at bit b, ending at e-1.
  // INS R,S,b:  take R and replace bits starting from b with S.
  RegisterCell eXTR(const RegisterCell &A1, uint16_t B, uint16_t E) const;
  RegisterCell eINS(const RegisterCell &A1, const RegisterCell &A2,
                    uint16_t AtN) const;

  // User-provided functions for individual targets:

  // Return a sub-register mask that indicates which bits in Reg belong
  // to the subregister Sub. These bits are assumed to be contiguous in
  // the super-register, and have the same ordering in the sub-register
  // as in the super-register. It is valid to call this function with
  // Sub == 0, in this case, the function should return a mask that spans
  // the entire register Reg (which is what the default implementation
  // does).
  virtual BitMask mask(unsigned Reg, unsigned Sub) const;
  // Indicate whether a given register class should be tracked.
  virtual bool track(const TargetRegisterClass *RC) const { return true; }
  // Evaluate a non-branching machine instruction, given the cell map with
  // the input values. Place the results in the Outputs map. Return "true"
  // if evaluation succeeded, "false" otherwise.
  virtual bool evaluate(const MachineInstr &MI, const CellMapType &Inputs,
                        CellMapType &Outputs) const;
  // Evaluate a branch, given the cell map with the input values. Fill out
  // a list of all possible branch targets and indicate (through a flag)
  // whether the branch could fall-through. Return "true" if this information
  // has been successfully computed, "false" otherwise.
  virtual bool evaluate(const MachineInstr &BI, const CellMapType &Inputs,
                        BranchTargetList &Targets, bool &FallsThru) const = 0;
  // Given a register class RC, return a register class that should be assumed
  // when a register from class RC is used with a subregister of index Idx.
  virtual const TargetRegisterClass&
  composeWithSubRegIndex(const TargetRegisterClass &RC, unsigned Idx) const {
    if (Idx == 0)
      return RC;
    llvm_unreachable("Unimplemented composeWithSubRegIndex");
  }
  // Return the size in bits of the physical register Reg.
  virtual uint16_t getPhysRegBitWidth(unsigned Reg) const;

  const TargetRegisterInfo &TRI;
  MachineRegisterInfo &MRI;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_BITTRACKER_H
