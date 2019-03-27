//===- HexagonGenInsert.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BitTracker.h"
#include "HexagonBitTracker.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hexinsert"

using namespace llvm;

static cl::opt<unsigned> VRegIndexCutoff("insert-vreg-cutoff", cl::init(~0U),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Vreg# cutoff for insert generation."));
// The distance cutoff is selected based on the precheckin-perf results:
// cutoffs 20, 25, 35, and 40 are worse than 30.
static cl::opt<unsigned> VRegDistCutoff("insert-dist-cutoff", cl::init(30U),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Vreg distance cutoff for insert "
  "generation."));

// Limit the container sizes for extreme cases where we run out of memory.
static cl::opt<unsigned> MaxORLSize("insert-max-orl", cl::init(4096),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Maximum size of OrderedRegisterList"));
static cl::opt<unsigned> MaxIFMSize("insert-max-ifmap", cl::init(1024),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Maximum size of IFMap"));

static cl::opt<bool> OptTiming("insert-timing", cl::init(false), cl::Hidden,
  cl::ZeroOrMore, cl::desc("Enable timing of insert generation"));
static cl::opt<bool> OptTimingDetail("insert-timing-detail", cl::init(false),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Enable detailed timing of insert "
  "generation"));

static cl::opt<bool> OptSelectAll0("insert-all0", cl::init(false), cl::Hidden,
  cl::ZeroOrMore);
static cl::opt<bool> OptSelectHas0("insert-has0", cl::init(false), cl::Hidden,
  cl::ZeroOrMore);
// Whether to construct constant values via "insert". Could eliminate constant
// extenders, but often not practical.
static cl::opt<bool> OptConst("insert-const", cl::init(false), cl::Hidden,
  cl::ZeroOrMore);

// The preprocessor gets confused when the DEBUG macro is passed larger
// chunks of code. Use this function to detect debugging.
inline static bool isDebug() {
#ifndef NDEBUG
  return DebugFlag && isCurrentDebugType(DEBUG_TYPE);
#else
  return false;
#endif
}

namespace {

  // Set of virtual registers, based on BitVector.
  struct RegisterSet : private BitVector {
    RegisterSet() = default;
    explicit RegisterSet(unsigned s, bool t = false) : BitVector(s, t) {}
    RegisterSet(const RegisterSet &RS) : BitVector(RS) {}

    using BitVector::clear;

    unsigned find_first() const {
      int First = BitVector::find_first();
      if (First < 0)
        return 0;
      return x2v(First);
    }

    unsigned find_next(unsigned Prev) const {
      int Next = BitVector::find_next(v2x(Prev));
      if (Next < 0)
        return 0;
      return x2v(Next);
    }

    RegisterSet &insert(unsigned R) {
      unsigned Idx = v2x(R);
      ensure(Idx);
      return static_cast<RegisterSet&>(BitVector::set(Idx));
    }
    RegisterSet &remove(unsigned R) {
      unsigned Idx = v2x(R);
      if (Idx >= size())
        return *this;
      return static_cast<RegisterSet&>(BitVector::reset(Idx));
    }

    RegisterSet &insert(const RegisterSet &Rs) {
      return static_cast<RegisterSet&>(BitVector::operator|=(Rs));
    }
    RegisterSet &remove(const RegisterSet &Rs) {
      return static_cast<RegisterSet&>(BitVector::reset(Rs));
    }

    reference operator[](unsigned R) {
      unsigned Idx = v2x(R);
      ensure(Idx);
      return BitVector::operator[](Idx);
    }
    bool operator[](unsigned R) const {
      unsigned Idx = v2x(R);
      assert(Idx < size());
      return BitVector::operator[](Idx);
    }
    bool has(unsigned R) const {
      unsigned Idx = v2x(R);
      if (Idx >= size())
        return false;
      return BitVector::test(Idx);
    }

    bool empty() const {
      return !BitVector::any();
    }
    bool includes(const RegisterSet &Rs) const {
      // A.BitVector::test(B)  <=>  A-B != {}
      return !Rs.BitVector::test(*this);
    }
    bool intersects(const RegisterSet &Rs) const {
      return BitVector::anyCommon(Rs);
    }

  private:
    void ensure(unsigned Idx) {
      if (size() <= Idx)
        resize(std::max(Idx+1, 32U));
    }

    static inline unsigned v2x(unsigned v) {
      return TargetRegisterInfo::virtReg2Index(v);
    }

    static inline unsigned x2v(unsigned x) {
      return TargetRegisterInfo::index2VirtReg(x);
    }
  };

  struct PrintRegSet {
    PrintRegSet(const RegisterSet &S, const TargetRegisterInfo *RI)
      : RS(S), TRI(RI) {}

    friend raw_ostream &operator<< (raw_ostream &OS,
          const PrintRegSet &P);

  private:
    const RegisterSet &RS;
    const TargetRegisterInfo *TRI;
  };

  raw_ostream &operator<< (raw_ostream &OS, const PrintRegSet &P) {
    OS << '{';
    for (unsigned R = P.RS.find_first(); R; R = P.RS.find_next(R))
      OS << ' ' << printReg(R, P.TRI);
    OS << " }";
    return OS;
  }

  // A convenience class to associate unsigned numbers (such as virtual
  // registers) with unsigned numbers.
  struct UnsignedMap : public DenseMap<unsigned,unsigned> {
    UnsignedMap() = default;

  private:
    using BaseType = DenseMap<unsigned, unsigned>;
  };

  // A utility to establish an ordering between virtual registers:
  // VRegA < VRegB  <=>  RegisterOrdering[VRegA] < RegisterOrdering[VRegB]
  // This is meant as a cache for the ordering of virtual registers defined
  // by a potentially expensive comparison function, or obtained by a proce-
  // dure that should not be repeated each time two registers are compared.
  struct RegisterOrdering : public UnsignedMap {
    RegisterOrdering() = default;

    unsigned operator[](unsigned VR) const {
      const_iterator F = find(VR);
      assert(F != end());
      return F->second;
    }

    // Add operator(), so that objects of this class can be used as
    // comparators in std::sort et al.
    bool operator() (unsigned VR1, unsigned VR2) const {
      return operator[](VR1) < operator[](VR2);
    }
  };

  // Ordering of bit values. This class does not have operator[], but
  // is supplies a comparison operator() for use in std:: algorithms.
  // The order is as follows:
  // - 0 < 1 < ref
  // - ref1 < ref2, if ord(ref1.Reg) < ord(ref2.Reg),
  //   or ord(ref1.Reg) == ord(ref2.Reg), and ref1.Pos < ref2.Pos.
  struct BitValueOrdering {
    BitValueOrdering(const RegisterOrdering &RB) : BaseOrd(RB) {}

    bool operator() (const BitTracker::BitValue &V1,
          const BitTracker::BitValue &V2) const;

    const RegisterOrdering &BaseOrd;
  };

} // end anonymous namespace

bool BitValueOrdering::operator() (const BitTracker::BitValue &V1,
      const BitTracker::BitValue &V2) const {
  if (V1 == V2)
    return false;
  // V1==0 => true, V2==0 => false
  if (V1.is(0) || V2.is(0))
    return V1.is(0);
  // Neither of V1,V2 is 0, and V1!=V2.
  // V2==1 => false, V1==1 => true
  if (V2.is(1) || V1.is(1))
    return !V2.is(1);
  // Both V1,V2 are refs.
  unsigned Ind1 = BaseOrd[V1.RefI.Reg], Ind2 = BaseOrd[V2.RefI.Reg];
  if (Ind1 != Ind2)
    return Ind1 < Ind2;
  // If V1.Pos==V2.Pos
  assert(V1.RefI.Pos != V2.RefI.Pos && "Bit values should be different");
  return V1.RefI.Pos < V2.RefI.Pos;
}

namespace {

  // Cache for the BitTracker's cell map. Map lookup has a logarithmic
  // complexity, this class will memoize the lookup results to reduce
  // the access time for repeated lookups of the same cell.
  struct CellMapShadow {
    CellMapShadow(const BitTracker &T) : BT(T) {}

    const BitTracker::RegisterCell &lookup(unsigned VR) {
      unsigned RInd = TargetRegisterInfo::virtReg2Index(VR);
      // Grow the vector to at least 32 elements.
      if (RInd >= CVect.size())
        CVect.resize(std::max(RInd+16, 32U), nullptr);
      const BitTracker::RegisterCell *CP = CVect[RInd];
      if (CP == nullptr)
        CP = CVect[RInd] = &BT.lookup(VR);
      return *CP;
    }

    const BitTracker &BT;

  private:
    using CellVectType = std::vector<const BitTracker::RegisterCell *>;

    CellVectType CVect;
  };

  // Comparator class for lexicographic ordering of virtual registers
  // according to the corresponding BitTracker::RegisterCell objects.
  struct RegisterCellLexCompare {
    RegisterCellLexCompare(const BitValueOrdering &BO, CellMapShadow &M)
      : BitOrd(BO), CM(M) {}

    bool operator() (unsigned VR1, unsigned VR2) const;

  private:
    const BitValueOrdering &BitOrd;
    CellMapShadow &CM;
  };

  // Comparator class for lexicographic ordering of virtual registers
  // according to the specified bits of the corresponding BitTracker::
  // RegisterCell objects.
  // Specifically, this class will be used to compare bit B of a register
  // cell for a selected virtual register R with bit N of any register
  // other than R.
  struct RegisterCellBitCompareSel {
    RegisterCellBitCompareSel(unsigned R, unsigned B, unsigned N,
          const BitValueOrdering &BO, CellMapShadow &M)
      : SelR(R), SelB(B), BitN(N), BitOrd(BO), CM(M) {}

    bool operator() (unsigned VR1, unsigned VR2) const;

  private:
    const unsigned SelR, SelB;
    const unsigned BitN;
    const BitValueOrdering &BitOrd;
    CellMapShadow &CM;
  };

} // end anonymous namespace

bool RegisterCellLexCompare::operator() (unsigned VR1, unsigned VR2) const {
  // Ordering of registers, made up from two given orderings:
  // - the ordering of the register numbers, and
  // - the ordering of register cells.
  // Def. R1 < R2 if:
  // - cell(R1) < cell(R2), or
  // - cell(R1) == cell(R2), and index(R1) < index(R2).
  //
  // For register cells, the ordering is lexicographic, with index 0 being
  // the most significant.
  if (VR1 == VR2)
    return false;

  const BitTracker::RegisterCell &RC1 = CM.lookup(VR1), &RC2 = CM.lookup(VR2);
  uint16_t W1 = RC1.width(), W2 = RC2.width();
  for (uint16_t i = 0, w = std::min(W1, W2); i < w; ++i) {
    const BitTracker::BitValue &V1 = RC1[i], &V2 = RC2[i];
    if (V1 != V2)
      return BitOrd(V1, V2);
  }
  // Cells are equal up until the common length.
  if (W1 != W2)
    return W1 < W2;

  return BitOrd.BaseOrd[VR1] < BitOrd.BaseOrd[VR2];
}

bool RegisterCellBitCompareSel::operator() (unsigned VR1, unsigned VR2) const {
  if (VR1 == VR2)
    return false;
  const BitTracker::RegisterCell &RC1 = CM.lookup(VR1);
  const BitTracker::RegisterCell &RC2 = CM.lookup(VR2);
  uint16_t W1 = RC1.width(), W2 = RC2.width();
  uint16_t Bit1 = (VR1 == SelR) ? SelB : BitN;
  uint16_t Bit2 = (VR2 == SelR) ? SelB : BitN;
  // If Bit1 exceeds the width of VR1, then:
  // - return false, if at the same time Bit2 exceeds VR2, or
  // - return true, otherwise.
  // (I.e. "a bit value that does not exist is less than any bit value
  // that does exist".)
  if (W1 <= Bit1)
    return Bit2 < W2;
  // If Bit1 is within VR1, but Bit2 is not within VR2, return false.
  if (W2 <= Bit2)
    return false;

  const BitTracker::BitValue &V1 = RC1[Bit1], V2 = RC2[Bit2];
  if (V1 != V2)
    return BitOrd(V1, V2);
  return false;
}

namespace {

  class OrderedRegisterList {
    using ListType = std::vector<unsigned>;
    const unsigned MaxSize;

  public:
    OrderedRegisterList(const RegisterOrdering &RO)
      : MaxSize(MaxORLSize), Ord(RO) {}

    void insert(unsigned VR);
    void remove(unsigned VR);

    unsigned operator[](unsigned Idx) const {
      assert(Idx < Seq.size());
      return Seq[Idx];
    }

    unsigned size() const {
      return Seq.size();
    }

    using iterator = ListType::iterator;
    using const_iterator = ListType::const_iterator;

    iterator begin() { return Seq.begin(); }
    iterator end() { return Seq.end(); }
    const_iterator begin() const { return Seq.begin(); }
    const_iterator end() const { return Seq.end(); }

    // Convenience function to convert an iterator to the corresponding index.
    unsigned idx(iterator It) const { return It-begin(); }

  private:
    ListType Seq;
    const RegisterOrdering &Ord;
  };

  struct PrintORL {
    PrintORL(const OrderedRegisterList &L, const TargetRegisterInfo *RI)
      : RL(L), TRI(RI) {}

    friend raw_ostream &operator<< (raw_ostream &OS, const PrintORL &P);

  private:
    const OrderedRegisterList &RL;
    const TargetRegisterInfo *TRI;
  };

  raw_ostream &operator<< (raw_ostream &OS, const PrintORL &P) {
    OS << '(';
    OrderedRegisterList::const_iterator B = P.RL.begin(), E = P.RL.end();
    for (OrderedRegisterList::const_iterator I = B; I != E; ++I) {
      if (I != B)
        OS << ", ";
      OS << printReg(*I, P.TRI);
    }
    OS << ')';
    return OS;
  }

} // end anonymous namespace

void OrderedRegisterList::insert(unsigned VR) {
  iterator L = std::lower_bound(Seq.begin(), Seq.end(), VR, Ord);
  if (L == Seq.end())
    Seq.push_back(VR);
  else
    Seq.insert(L, VR);

  unsigned S = Seq.size();
  if (S > MaxSize)
    Seq.resize(MaxSize);
  assert(Seq.size() <= MaxSize);
}

void OrderedRegisterList::remove(unsigned VR) {
  iterator L = std::lower_bound(Seq.begin(), Seq.end(), VR, Ord);
  if (L != Seq.end())
    Seq.erase(L);
}

namespace {

  // A record of the insert form. The fields correspond to the operands
  // of the "insert" instruction:
  // ... = insert(SrcR, InsR, #Wdh, #Off)
  struct IFRecord {
    IFRecord(unsigned SR = 0, unsigned IR = 0, uint16_t W = 0, uint16_t O = 0)
      : SrcR(SR), InsR(IR), Wdh(W), Off(O) {}

    unsigned SrcR, InsR;
    uint16_t Wdh, Off;
  };

  struct PrintIFR {
    PrintIFR(const IFRecord &R, const TargetRegisterInfo *RI)
      : IFR(R), TRI(RI) {}

  private:
    friend raw_ostream &operator<< (raw_ostream &OS, const PrintIFR &P);

    const IFRecord &IFR;
    const TargetRegisterInfo *TRI;
  };

  raw_ostream &operator<< (raw_ostream &OS, const PrintIFR &P) {
    unsigned SrcR = P.IFR.SrcR, InsR = P.IFR.InsR;
    OS << '(' << printReg(SrcR, P.TRI) << ',' << printReg(InsR, P.TRI)
       << ",#" << P.IFR.Wdh << ",#" << P.IFR.Off << ')';
    return OS;
  }

  using IFRecordWithRegSet = std::pair<IFRecord, RegisterSet>;

} // end anonymous namespace

namespace llvm {

  void initializeHexagonGenInsertPass(PassRegistry&);
  FunctionPass *createHexagonGenInsert();

} // end namespace llvm

namespace {

  class HexagonGenInsert : public MachineFunctionPass {
  public:
    static char ID;

    HexagonGenInsert() : MachineFunctionPass(ID) {
      initializeHexagonGenInsertPass(*PassRegistry::getPassRegistry());
    }

    StringRef getPassName() const override {
      return "Hexagon generate \"insert\" instructions";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTree>();
      AU.addPreserved<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    using PairMapType = DenseMap<std::pair<unsigned, unsigned>, unsigned>;

    void buildOrderingMF(RegisterOrdering &RO) const;
    void buildOrderingBT(RegisterOrdering &RB, RegisterOrdering &RO) const;
    bool isIntClass(const TargetRegisterClass *RC) const;
    bool isConstant(unsigned VR) const;
    bool isSmallConstant(unsigned VR) const;
    bool isValidInsertForm(unsigned DstR, unsigned SrcR, unsigned InsR,
          uint16_t L, uint16_t S) const;
    bool findSelfReference(unsigned VR) const;
    bool findNonSelfReference(unsigned VR) const;
    void getInstrDefs(const MachineInstr *MI, RegisterSet &Defs) const;
    void getInstrUses(const MachineInstr *MI, RegisterSet &Uses) const;
    unsigned distance(const MachineBasicBlock *FromB,
          const MachineBasicBlock *ToB, const UnsignedMap &RPO,
          PairMapType &M) const;
    unsigned distance(MachineBasicBlock::const_iterator FromI,
          MachineBasicBlock::const_iterator ToI, const UnsignedMap &RPO,
          PairMapType &M) const;
    bool findRecordInsertForms(unsigned VR, OrderedRegisterList &AVs);
    void collectInBlock(MachineBasicBlock *B, OrderedRegisterList &AVs);
    void findRemovableRegisters(unsigned VR, IFRecord IF,
          RegisterSet &RMs) const;
    void computeRemovableRegisters();

    void pruneEmptyLists();
    void pruneCoveredSets(unsigned VR);
    void pruneUsesTooFar(unsigned VR, const UnsignedMap &RPO, PairMapType &M);
    void pruneRegCopies(unsigned VR);
    void pruneCandidates();
    void selectCandidates();
    bool generateInserts();

    bool removeDeadCode(MachineDomTreeNode *N);

    // IFRecord coupled with a set of potentially removable registers:
    using IFListType = std::vector<IFRecordWithRegSet>;
    using IFMapType = DenseMap<unsigned, IFListType>; // vreg -> IFListType

    void dump_map() const;

    const HexagonInstrInfo *HII = nullptr;
    const HexagonRegisterInfo *HRI = nullptr;

    MachineFunction *MFN;
    MachineRegisterInfo *MRI;
    MachineDominatorTree *MDT;
    CellMapShadow *CMS;

    RegisterOrdering BaseOrd;
    RegisterOrdering CellOrd;
    IFMapType IFMap;
  };

} // end anonymous namespace

char HexagonGenInsert::ID = 0;

void HexagonGenInsert::dump_map() const {
  using iterator = IFMapType::const_iterator;

  for (iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
    dbgs() << "  " << printReg(I->first, HRI) << ":\n";
    const IFListType &LL = I->second;
    for (unsigned i = 0, n = LL.size(); i < n; ++i)
      dbgs() << "    " << PrintIFR(LL[i].first, HRI) << ", "
             << PrintRegSet(LL[i].second, HRI) << '\n';
  }
}

void HexagonGenInsert::buildOrderingMF(RegisterOrdering &RO) const {
  unsigned Index = 0;

  using mf_iterator = MachineFunction::const_iterator;

  for (mf_iterator A = MFN->begin(), Z = MFN->end(); A != Z; ++A) {
    const MachineBasicBlock &B = *A;
    if (!CMS->BT.reached(&B))
      continue;

    using mb_iterator = MachineBasicBlock::const_iterator;

    for (mb_iterator I = B.begin(), E = B.end(); I != E; ++I) {
      const MachineInstr *MI = &*I;
      for (unsigned i = 0, n = MI->getNumOperands(); i < n; ++i) {
        const MachineOperand &MO = MI->getOperand(i);
        if (MO.isReg() && MO.isDef()) {
          unsigned R = MO.getReg();
          assert(MO.getSubReg() == 0 && "Unexpected subregister in definition");
          if (TargetRegisterInfo::isVirtualRegister(R))
            RO.insert(std::make_pair(R, Index++));
        }
      }
    }
  }
  // Since some virtual registers may have had their def and uses eliminated,
  // they are no longer referenced in the code, and so they will not appear
  // in the map.
}

void HexagonGenInsert::buildOrderingBT(RegisterOrdering &RB,
      RegisterOrdering &RO) const {
  // Create a vector of all virtual registers (collect them from the base
  // ordering RB), and then sort it using the RegisterCell comparator.
  BitValueOrdering BVO(RB);
  RegisterCellLexCompare LexCmp(BVO, *CMS);

  using SortableVectorType = std::vector<unsigned>;

  SortableVectorType VRs;
  for (RegisterOrdering::iterator I = RB.begin(), E = RB.end(); I != E; ++I)
    VRs.push_back(I->first);
  llvm::sort(VRs, LexCmp);
  // Transfer the results to the outgoing register ordering.
  for (unsigned i = 0, n = VRs.size(); i < n; ++i)
    RO.insert(std::make_pair(VRs[i], i));
}

inline bool HexagonGenInsert::isIntClass(const TargetRegisterClass *RC) const {
  return RC == &Hexagon::IntRegsRegClass || RC == &Hexagon::DoubleRegsRegClass;
}

bool HexagonGenInsert::isConstant(unsigned VR) const {
  const BitTracker::RegisterCell &RC = CMS->lookup(VR);
  uint16_t W = RC.width();
  for (uint16_t i = 0; i < W; ++i) {
    const BitTracker::BitValue &BV = RC[i];
    if (BV.is(0) || BV.is(1))
      continue;
    return false;
  }
  return true;
}

bool HexagonGenInsert::isSmallConstant(unsigned VR) const {
  const BitTracker::RegisterCell &RC = CMS->lookup(VR);
  uint16_t W = RC.width();
  if (W > 64)
    return false;
  uint64_t V = 0, B = 1;
  for (uint16_t i = 0; i < W; ++i) {
    const BitTracker::BitValue &BV = RC[i];
    if (BV.is(1))
      V |= B;
    else if (!BV.is(0))
      return false;
    B <<= 1;
  }

  // For 32-bit registers, consider: Rd = #s16.
  if (W == 32)
    return isInt<16>(V);

  // For 64-bit registers, it's Rdd = #s8 or Rdd = combine(#s8,#s8)
  return isInt<8>(Lo_32(V)) && isInt<8>(Hi_32(V));
}

bool HexagonGenInsert::isValidInsertForm(unsigned DstR, unsigned SrcR,
      unsigned InsR, uint16_t L, uint16_t S) const {
  const TargetRegisterClass *DstRC = MRI->getRegClass(DstR);
  const TargetRegisterClass *SrcRC = MRI->getRegClass(SrcR);
  const TargetRegisterClass *InsRC = MRI->getRegClass(InsR);
  // Only integet (32-/64-bit) register classes.
  if (!isIntClass(DstRC) || !isIntClass(SrcRC) || !isIntClass(InsRC))
    return false;
  // The "source" register must be of the same class as DstR.
  if (DstRC != SrcRC)
    return false;
  if (DstRC == InsRC)
    return true;
  // A 64-bit register can only be generated from other 64-bit registers.
  if (DstRC == &Hexagon::DoubleRegsRegClass)
    return false;
  // Otherwise, the L and S cannot span 32-bit word boundary.
  if (S < 32 && S+L > 32)
    return false;
  return true;
}

bool HexagonGenInsert::findSelfReference(unsigned VR) const {
  const BitTracker::RegisterCell &RC = CMS->lookup(VR);
  for (uint16_t i = 0, w = RC.width(); i < w; ++i) {
    const BitTracker::BitValue &V = RC[i];
    if (V.Type == BitTracker::BitValue::Ref && V.RefI.Reg == VR)
      return true;
  }
  return false;
}

bool HexagonGenInsert::findNonSelfReference(unsigned VR) const {
  BitTracker::RegisterCell RC = CMS->lookup(VR);
  for (uint16_t i = 0, w = RC.width(); i < w; ++i) {
    const BitTracker::BitValue &V = RC[i];
    if (V.Type == BitTracker::BitValue::Ref && V.RefI.Reg != VR)
      return true;
  }
  return false;
}

void HexagonGenInsert::getInstrDefs(const MachineInstr *MI,
      RegisterSet &Defs) const {
  for (unsigned i = 0, n = MI->getNumOperands(); i < n; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned R = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(R))
      continue;
    Defs.insert(R);
  }
}

void HexagonGenInsert::getInstrUses(const MachineInstr *MI,
      RegisterSet &Uses) const {
  for (unsigned i = 0, n = MI->getNumOperands(); i < n; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.isUse())
      continue;
    unsigned R = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(R))
      continue;
    Uses.insert(R);
  }
}

unsigned HexagonGenInsert::distance(const MachineBasicBlock *FromB,
      const MachineBasicBlock *ToB, const UnsignedMap &RPO,
      PairMapType &M) const {
  // Forward distance from the end of a block to the beginning of it does
  // not make sense. This function should not be called with FromB == ToB.
  assert(FromB != ToB);

  unsigned FromN = FromB->getNumber(), ToN = ToB->getNumber();
  // If we have already computed it, return the cached result.
  PairMapType::iterator F = M.find(std::make_pair(FromN, ToN));
  if (F != M.end())
    return F->second;
  unsigned ToRPO = RPO.lookup(ToN);

  unsigned MaxD = 0;

  using pred_iterator = MachineBasicBlock::const_pred_iterator;

  for (pred_iterator I = ToB->pred_begin(), E = ToB->pred_end(); I != E; ++I) {
    const MachineBasicBlock *PB = *I;
    // Skip back edges. Also, if FromB is a predecessor of ToB, the distance
    // along that path will be 0, and we don't need to do any calculations
    // on it.
    if (PB == FromB || RPO.lookup(PB->getNumber()) >= ToRPO)
      continue;
    unsigned D = PB->size() + distance(FromB, PB, RPO, M);
    if (D > MaxD)
      MaxD = D;
  }

  // Memoize the result for later lookup.
  M.insert(std::make_pair(std::make_pair(FromN, ToN), MaxD));
  return MaxD;
}

unsigned HexagonGenInsert::distance(MachineBasicBlock::const_iterator FromI,
      MachineBasicBlock::const_iterator ToI, const UnsignedMap &RPO,
      PairMapType &M) const {
  const MachineBasicBlock *FB = FromI->getParent(), *TB = ToI->getParent();
  if (FB == TB)
    return std::distance(FromI, ToI);
  unsigned D1 = std::distance(TB->begin(), ToI);
  unsigned D2 = distance(FB, TB, RPO, M);
  unsigned D3 = std::distance(FromI, FB->end());
  return D1+D2+D3;
}

bool HexagonGenInsert::findRecordInsertForms(unsigned VR,
      OrderedRegisterList &AVs) {
  if (isDebug()) {
    dbgs() << __func__ << ": " << printReg(VR, HRI)
           << "  AVs: " << PrintORL(AVs, HRI) << "\n";
  }
  if (AVs.size() == 0)
    return false;

  using iterator = OrderedRegisterList::iterator;

  BitValueOrdering BVO(BaseOrd);
  const BitTracker::RegisterCell &RC = CMS->lookup(VR);
  uint16_t W = RC.width();

  using RSRecord = std::pair<unsigned, uint16_t>; // (reg,shift)
  using RSListType = std::vector<RSRecord>;
  // Have a map, with key being the matching prefix length, and the value
  // being the list of pairs (R,S), where R's prefix matches VR at S.
  // (DenseMap<uint16_t,RSListType> fails to instantiate.)
  using LRSMapType = DenseMap<unsigned, RSListType>;
  LRSMapType LM;

  // Conceptually, rotate the cell RC right (i.e. towards the LSB) by S,
  // and find matching prefixes from AVs with the rotated RC. Such a prefix
  // would match a string of bits (of length L) in RC starting at S.
  for (uint16_t S = 0; S < W; ++S) {
    iterator B = AVs.begin(), E = AVs.end();
    // The registers in AVs are ordered according to the lexical order of
    // the corresponding register cells. This means that the range of regis-
    // ters in AVs that match a prefix of length L+1 will be contained in
    // the range that matches a prefix of length L. This means that we can
    // keep narrowing the search space as the prefix length goes up. This
    // helps reduce the overall complexity of the search.
    uint16_t L;
    for (L = 0; L < W-S; ++L) {
      // Compare against VR's bits starting at S, which emulates rotation
      // of VR by S.
      RegisterCellBitCompareSel RCB(VR, S+L, L, BVO, *CMS);
      iterator NewB = std::lower_bound(B, E, VR, RCB);
      iterator NewE = std::upper_bound(NewB, E, VR, RCB);
      // For the registers that are eliminated from the next range, L is
      // the longest prefix matching VR at position S (their prefixes
      // differ from VR at S+L). If L>0, record this information for later
      // use.
      if (L > 0) {
        for (iterator I = B; I != NewB; ++I)
          LM[L].push_back(std::make_pair(*I, S));
        for (iterator I = NewE; I != E; ++I)
          LM[L].push_back(std::make_pair(*I, S));
      }
      B = NewB, E = NewE;
      if (B == E)
        break;
    }
    // Record the final register range. If this range is non-empty, then
    // L=W-S.
    assert(B == E || L == W-S);
    if (B != E) {
      for (iterator I = B; I != E; ++I)
        LM[L].push_back(std::make_pair(*I, S));
      // If B!=E, then we found a range of registers whose prefixes cover the
      // rest of VR from position S. There is no need to further advance S.
      break;
    }
  }

  if (isDebug()) {
    dbgs() << "Prefixes matching register " << printReg(VR, HRI) << "\n";
    for (LRSMapType::iterator I = LM.begin(), E = LM.end(); I != E; ++I) {
      dbgs() << "  L=" << I->first << ':';
      const RSListType &LL = I->second;
      for (unsigned i = 0, n = LL.size(); i < n; ++i)
        dbgs() << " (" << printReg(LL[i].first, HRI) << ",@"
               << LL[i].second << ')';
      dbgs() << '\n';
    }
  }

  bool Recorded = false;

  for (iterator I = AVs.begin(), E = AVs.end(); I != E; ++I) {
    unsigned SrcR = *I;
    int FDi = -1, LDi = -1;   // First/last different bit.
    const BitTracker::RegisterCell &AC = CMS->lookup(SrcR);
    uint16_t AW = AC.width();
    for (uint16_t i = 0, w = std::min(W, AW); i < w; ++i) {
      if (RC[i] == AC[i])
        continue;
      if (FDi == -1)
        FDi = i;
      LDi = i;
    }
    if (FDi == -1)
      continue;  // TODO (future): Record identical registers.
    // Look for a register whose prefix could patch the range [FD..LD]
    // where VR and SrcR differ.
    uint16_t FD = FDi, LD = LDi;  // Switch to unsigned type.
    uint16_t MinL = LD-FD+1;
    for (uint16_t L = MinL; L < W; ++L) {
      LRSMapType::iterator F = LM.find(L);
      if (F == LM.end())
        continue;
      RSListType &LL = F->second;
      for (unsigned i = 0, n = LL.size(); i < n; ++i) {
        uint16_t S = LL[i].second;
        // MinL is the minimum length of the prefix. Any length above MinL
        // allows some flexibility as to where the prefix can start:
        // given the extra length EL=L-MinL, the prefix must start between
        // max(0,FD-EL) and FD.
        if (S > FD)   // Starts too late.
          continue;
        uint16_t EL = L-MinL;
        uint16_t LowS = (EL < FD) ? FD-EL : 0;
        if (S < LowS) // Starts too early.
          continue;
        unsigned InsR = LL[i].first;
        if (!isValidInsertForm(VR, SrcR, InsR, L, S))
          continue;
        if (isDebug()) {
          dbgs() << printReg(VR, HRI) << " = insert(" << printReg(SrcR, HRI)
                 << ',' << printReg(InsR, HRI) << ",#" << L << ",#"
                 << S << ")\n";
        }
        IFRecordWithRegSet RR(IFRecord(SrcR, InsR, L, S), RegisterSet());
        IFMap[VR].push_back(RR);
        Recorded = true;
      }
    }
  }

  return Recorded;
}

void HexagonGenInsert::collectInBlock(MachineBasicBlock *B,
      OrderedRegisterList &AVs) {
  if (isDebug())
    dbgs() << "visiting block " << printMBBReference(*B) << "\n";

  // First, check if this block is reachable at all. If not, the bit tracker
  // will not have any information about registers in it.
  if (!CMS->BT.reached(B))
    return;

  bool DoConst = OptConst;
  // Keep a separate set of registers defined in this block, so that we
  // can remove them from the list of available registers once all DT
  // successors have been processed.
  RegisterSet BlockDefs, InsDefs;
  for (MachineBasicBlock::iterator I = B->begin(), E = B->end(); I != E; ++I) {
    MachineInstr *MI = &*I;
    InsDefs.clear();
    getInstrDefs(MI, InsDefs);
    // Leave those alone. They are more transparent than "insert".
    bool Skip = MI->isCopy() || MI->isRegSequence();

    if (!Skip) {
      // Visit all defined registers, and attempt to find the corresponding
      // "insert" representations.
      for (unsigned VR = InsDefs.find_first(); VR; VR = InsDefs.find_next(VR)) {
        // Do not collect registers that are known to be compile-time cons-
        // tants, unless requested.
        if (!DoConst && isConstant(VR))
          continue;
        // If VR's cell contains a reference to VR, then VR cannot be defined
        // via "insert". If VR is a constant that can be generated in a single
        // instruction (without constant extenders), generating it via insert
        // makes no sense.
        if (findSelfReference(VR) || isSmallConstant(VR))
          continue;

        findRecordInsertForms(VR, AVs);
        // Stop if the map size is too large.
        if (IFMap.size() > MaxIFMSize)
          return;
      }
    }

    // Insert the defined registers into the list of available registers
    // after they have been processed.
    for (unsigned VR = InsDefs.find_first(); VR; VR = InsDefs.find_next(VR))
      AVs.insert(VR);
    BlockDefs.insert(InsDefs);
  }

  for (auto *DTN : children<MachineDomTreeNode*>(MDT->getNode(B))) {
    MachineBasicBlock *SB = DTN->getBlock();
    collectInBlock(SB, AVs);
  }

  for (unsigned VR = BlockDefs.find_first(); VR; VR = BlockDefs.find_next(VR))
    AVs.remove(VR);
}

void HexagonGenInsert::findRemovableRegisters(unsigned VR, IFRecord IF,
      RegisterSet &RMs) const {
  // For a given register VR and a insert form, find the registers that are
  // used by the current definition of VR, and which would no longer be
  // needed for it after the definition of VR is replaced with the insert
  // form. These are the registers that could potentially become dead.
  RegisterSet Regs[2];

  unsigned S = 0;  // Register set selector.
  Regs[S].insert(VR);

  while (!Regs[S].empty()) {
    // Breadth-first search.
    unsigned OtherS = 1-S;
    Regs[OtherS].clear();
    for (unsigned R = Regs[S].find_first(); R; R = Regs[S].find_next(R)) {
      Regs[S].remove(R);
      if (R == IF.SrcR || R == IF.InsR)
        continue;
      // Check if a given register has bits that are references to any other
      // registers. This is to detect situations where the instruction that
      // defines register R takes register Q as an operand, but R itself does
      // not contain any bits from Q. Loads are examples of how this could
      // happen:
      //   R = load Q
      // In this case (assuming we do not have any knowledge about the loaded
      // value), we must not treat R as a "conveyance" of the bits from Q.
      // (The information in BT about R's bits would have them as constants,
      // in case of zero-extending loads, or refs to R.)
      if (!findNonSelfReference(R))
        continue;
      RMs.insert(R);
      const MachineInstr *DefI = MRI->getVRegDef(R);
      assert(DefI);
      // Do not iterate past PHI nodes to avoid infinite loops. This can
      // make the final set a bit less accurate, but the removable register
      // sets are an approximation anyway.
      if (DefI->isPHI())
        continue;
      getInstrUses(DefI, Regs[OtherS]);
    }
    S = OtherS;
  }
  // The register VR is added to the list as a side-effect of the algorithm,
  // but it is not "potentially removable". A potentially removable register
  // is one that may become unused (dead) after conversion to the insert form
  // IF, and obviously VR (or its replacement) will not become dead by apply-
  // ing IF.
  RMs.remove(VR);
}

void HexagonGenInsert::computeRemovableRegisters() {
  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
    IFListType &LL = I->second;
    for (unsigned i = 0, n = LL.size(); i < n; ++i)
      findRemovableRegisters(I->first, LL[i].first, LL[i].second);
  }
}

void HexagonGenInsert::pruneEmptyLists() {
  // Remove all entries from the map, where the register has no insert forms
  // associated with it.
  using IterListType = SmallVector<IFMapType::iterator, 16>;
  IterListType Prune;
  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
    if (I->second.empty())
      Prune.push_back(I);
  }
  for (unsigned i = 0, n = Prune.size(); i < n; ++i)
    IFMap.erase(Prune[i]);
}

void HexagonGenInsert::pruneCoveredSets(unsigned VR) {
  IFMapType::iterator F = IFMap.find(VR);
  assert(F != IFMap.end());
  IFListType &LL = F->second;

  // First, examine the IF candidates for register VR whose removable-regis-
  // ter sets are empty. This means that a given candidate will not help eli-
  // minate any registers, but since "insert" is not a constant-extendable
  // instruction, using such a candidate may reduce code size if the defini-
  // tion of VR is constant-extended.
  // If there exists a candidate with a non-empty set, the ones with empty
  // sets will not be used and can be removed.
  MachineInstr *DefVR = MRI->getVRegDef(VR);
  bool DefEx = HII->isConstExtended(*DefVR);
  bool HasNE = false;
  for (unsigned i = 0, n = LL.size(); i < n; ++i) {
    if (LL[i].second.empty())
      continue;
    HasNE = true;
    break;
  }
  if (!DefEx || HasNE) {
    // The definition of VR is not constant-extended, or there is a candidate
    // with a non-empty set. Remove all candidates with empty sets.
    auto IsEmpty = [] (const IFRecordWithRegSet &IR) -> bool {
      return IR.second.empty();
    };
    auto End = llvm::remove_if(LL, IsEmpty);
    if (End != LL.end())
      LL.erase(End, LL.end());
  } else {
    // The definition of VR is constant-extended, and all candidates have
    // empty removable-register sets. Pick the maximum candidate, and remove
    // all others. The "maximum" does not have any special meaning here, it
    // is only so that the candidate that will remain on the list is selec-
    // ted deterministically.
    IFRecord MaxIF = LL[0].first;
    for (unsigned i = 1, n = LL.size(); i < n; ++i) {
      // If LL[MaxI] < LL[i], then MaxI = i.
      const IFRecord &IF = LL[i].first;
      unsigned M0 = BaseOrd[MaxIF.SrcR], M1 = BaseOrd[MaxIF.InsR];
      unsigned R0 = BaseOrd[IF.SrcR], R1 = BaseOrd[IF.InsR];
      if (M0 > R0)
        continue;
      if (M0 == R0) {
        if (M1 > R1)
          continue;
        if (M1 == R1) {
          if (MaxIF.Wdh > IF.Wdh)
            continue;
          if (MaxIF.Wdh == IF.Wdh && MaxIF.Off >= IF.Off)
            continue;
        }
      }
      // MaxIF < IF.
      MaxIF = IF;
    }
    // Remove everything except the maximum candidate. All register sets
    // are empty, so no need to preserve anything.
    LL.clear();
    LL.push_back(std::make_pair(MaxIF, RegisterSet()));
  }

  // Now, remove those whose sets of potentially removable registers are
  // contained in another IF candidate for VR. For example, given these
  // candidates for %45,
  //   %45:
  //     (%44,%41,#9,#8), { %42 }
  //     (%43,%41,#9,#8), { %42 %44 }
  // remove the first one, since it is contained in the second one.
  for (unsigned i = 0, n = LL.size(); i < n; ) {
    const RegisterSet &RMi = LL[i].second;
    unsigned j = 0;
    while (j < n) {
      if (j != i && LL[j].second.includes(RMi))
        break;
      j++;
    }
    if (j == n) {   // RMi not contained in anything else.
      i++;
      continue;
    }
    LL.erase(LL.begin()+i);
    n = LL.size();
  }
}

void HexagonGenInsert::pruneUsesTooFar(unsigned VR, const UnsignedMap &RPO,
      PairMapType &M) {
  IFMapType::iterator F = IFMap.find(VR);
  assert(F != IFMap.end());
  IFListType &LL = F->second;
  unsigned Cutoff = VRegDistCutoff;
  const MachineInstr *DefV = MRI->getVRegDef(VR);

  for (unsigned i = LL.size(); i > 0; --i) {
    unsigned SR = LL[i-1].first.SrcR, IR = LL[i-1].first.InsR;
    const MachineInstr *DefS = MRI->getVRegDef(SR);
    const MachineInstr *DefI = MRI->getVRegDef(IR);
    unsigned DSV = distance(DefS, DefV, RPO, M);
    if (DSV < Cutoff) {
      unsigned DIV = distance(DefI, DefV, RPO, M);
      if (DIV < Cutoff)
        continue;
    }
    LL.erase(LL.begin()+(i-1));
  }
}

void HexagonGenInsert::pruneRegCopies(unsigned VR) {
  IFMapType::iterator F = IFMap.find(VR);
  assert(F != IFMap.end());
  IFListType &LL = F->second;

  auto IsCopy = [] (const IFRecordWithRegSet &IR) -> bool {
    return IR.first.Wdh == 32 && (IR.first.Off == 0 || IR.first.Off == 32);
  };
  auto End = llvm::remove_if(LL, IsCopy);
  if (End != LL.end())
    LL.erase(End, LL.end());
}

void HexagonGenInsert::pruneCandidates() {
  // Remove candidates that are not beneficial, regardless of the final
  // selection method.
  // First, remove candidates whose potentially removable set is a subset
  // of another candidate's set.
  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I)
    pruneCoveredSets(I->first);

  UnsignedMap RPO;

  using RPOTType = ReversePostOrderTraversal<const MachineFunction *>;

  RPOTType RPOT(MFN);
  unsigned RPON = 0;
  for (RPOTType::rpo_iterator I = RPOT.begin(), E = RPOT.end(); I != E; ++I)
    RPO[(*I)->getNumber()] = RPON++;

  PairMapType Memo; // Memoization map for distance calculation.
  // Remove candidates that would use registers defined too far away.
  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I)
    pruneUsesTooFar(I->first, RPO, Memo);

  pruneEmptyLists();

  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I)
    pruneRegCopies(I->first);
}

namespace {

  // Class for comparing IF candidates for registers that have multiple of
  // them. The smaller the candidate, according to this ordering, the better.
  // First, compare the number of zeros in the associated potentially remova-
  // ble register sets. "Zero" indicates that the register is very likely to
  // become dead after this transformation.
  // Second, compare "averages", i.e. use-count per size. The lower wins.
  // After that, it does not really matter which one is smaller. Resolve
  // the tie in some deterministic way.
  struct IFOrdering {
    IFOrdering(const UnsignedMap &UC, const RegisterOrdering &BO)
      : UseC(UC), BaseOrd(BO) {}

    bool operator() (const IFRecordWithRegSet &A,
                     const IFRecordWithRegSet &B) const;

  private:
    void stats(const RegisterSet &Rs, unsigned &Size, unsigned &Zero,
          unsigned &Sum) const;

    const UnsignedMap &UseC;
    const RegisterOrdering &BaseOrd;
  };

} // end anonymous namespace

bool IFOrdering::operator() (const IFRecordWithRegSet &A,
      const IFRecordWithRegSet &B) const {
  unsigned SizeA = 0, ZeroA = 0, SumA = 0;
  unsigned SizeB = 0, ZeroB = 0, SumB = 0;
  stats(A.second, SizeA, ZeroA, SumA);
  stats(B.second, SizeB, ZeroB, SumB);

  // We will pick the minimum element. The more zeros, the better.
  if (ZeroA != ZeroB)
    return ZeroA > ZeroB;
  // Compare SumA/SizeA with SumB/SizeB, lower is better.
  uint64_t AvgA = SumA*SizeB, AvgB = SumB*SizeA;
  if (AvgA != AvgB)
    return AvgA < AvgB;

  // The sets compare identical so far. Resort to comparing the IF records.
  // The actual values don't matter, this is only for determinism.
  unsigned OSA = BaseOrd[A.first.SrcR], OSB = BaseOrd[B.first.SrcR];
  if (OSA != OSB)
    return OSA < OSB;
  unsigned OIA = BaseOrd[A.first.InsR], OIB = BaseOrd[B.first.InsR];
  if (OIA != OIB)
    return OIA < OIB;
  if (A.first.Wdh != B.first.Wdh)
    return A.first.Wdh < B.first.Wdh;
  return A.first.Off < B.first.Off;
}

void IFOrdering::stats(const RegisterSet &Rs, unsigned &Size, unsigned &Zero,
      unsigned &Sum) const {
  for (unsigned R = Rs.find_first(); R; R = Rs.find_next(R)) {
    UnsignedMap::const_iterator F = UseC.find(R);
    assert(F != UseC.end());
    unsigned UC = F->second;
    if (UC == 0)
      Zero++;
    Sum += UC;
    Size++;
  }
}

void HexagonGenInsert::selectCandidates() {
  // Some registers may have multiple valid candidates. Pick the best one
  // (or decide not to use any).

  // Compute the "removability" measure of R:
  // For each potentially removable register R, record the number of regis-
  // ters with IF candidates, where R appears in at least one set.
  RegisterSet AllRMs;
  UnsignedMap UseC, RemC;
  IFMapType::iterator End = IFMap.end();

  for (IFMapType::iterator I = IFMap.begin(); I != End; ++I) {
    const IFListType &LL = I->second;
    RegisterSet TT;
    for (unsigned i = 0, n = LL.size(); i < n; ++i)
      TT.insert(LL[i].second);
    for (unsigned R = TT.find_first(); R; R = TT.find_next(R))
      RemC[R]++;
    AllRMs.insert(TT);
  }

  for (unsigned R = AllRMs.find_first(); R; R = AllRMs.find_next(R)) {
    using use_iterator = MachineRegisterInfo::use_nodbg_iterator;
    using InstrSet = SmallSet<const MachineInstr *, 16>;

    InstrSet UIs;
    // Count as the number of instructions in which R is used, not the
    // number of operands.
    use_iterator E = MRI->use_nodbg_end();
    for (use_iterator I = MRI->use_nodbg_begin(R); I != E; ++I)
      UIs.insert(I->getParent());
    unsigned C = UIs.size();
    // Calculate a measure, which is the number of instructions using R,
    // minus the "removability" count computed earlier.
    unsigned D = RemC[R];
    UseC[R] = (C > D) ? C-D : 0;  // doz
  }

  bool SelectAll0 = OptSelectAll0, SelectHas0 = OptSelectHas0;
  if (!SelectAll0 && !SelectHas0)
    SelectAll0 = true;

  // The smaller the number UseC for a given register R, the "less used"
  // R is aside from the opportunities for removal offered by generating
  // "insert" instructions.
  // Iterate over the IF map, and for those registers that have multiple
  // candidates, pick the minimum one according to IFOrdering.
  IFOrdering IFO(UseC, BaseOrd);
  for (IFMapType::iterator I = IFMap.begin(); I != End; ++I) {
    IFListType &LL = I->second;
    if (LL.empty())
      continue;
    // Get the minimum element, remember it and clear the list. If the
    // element found is adequate, we will put it back on the list, other-
    // wise the list will remain empty, and the entry for this register
    // will be removed (i.e. this register will not be replaced by insert).
    IFListType::iterator MinI = std::min_element(LL.begin(), LL.end(), IFO);
    assert(MinI != LL.end());
    IFRecordWithRegSet M = *MinI;
    LL.clear();

    // We want to make sure that this replacement will have a chance to be
    // beneficial, and that means that we want to have indication that some
    // register will be removed. The most likely registers to be eliminated
    // are the use operands in the definition of I->first. Accept/reject a
    // candidate based on how many of its uses it can potentially eliminate.

    RegisterSet Us;
    const MachineInstr *DefI = MRI->getVRegDef(I->first);
    getInstrUses(DefI, Us);
    bool Accept = false;

    if (SelectAll0) {
      bool All0 = true;
      for (unsigned R = Us.find_first(); R; R = Us.find_next(R)) {
        if (UseC[R] == 0)
          continue;
        All0 = false;
        break;
      }
      Accept = All0;
    } else if (SelectHas0) {
      bool Has0 = false;
      for (unsigned R = Us.find_first(); R; R = Us.find_next(R)) {
        if (UseC[R] != 0)
          continue;
        Has0 = true;
        break;
      }
      Accept = Has0;
    }
    if (Accept)
      LL.push_back(M);
  }

  // Remove candidates that add uses of removable registers, unless the
  // removable registers are among replacement candidates.
  // Recompute the removable registers, since some candidates may have
  // been eliminated.
  AllRMs.clear();
  for (IFMapType::iterator I = IFMap.begin(); I != End; ++I) {
    const IFListType &LL = I->second;
    if (!LL.empty())
      AllRMs.insert(LL[0].second);
  }
  for (IFMapType::iterator I = IFMap.begin(); I != End; ++I) {
    IFListType &LL = I->second;
    if (LL.empty())
      continue;
    unsigned SR = LL[0].first.SrcR, IR = LL[0].first.InsR;
    if (AllRMs[SR] || AllRMs[IR])
      LL.clear();
  }

  pruneEmptyLists();
}

bool HexagonGenInsert::generateInserts() {
  // Create a new register for each one from IFMap, and store them in the
  // map.
  UnsignedMap RegMap;
  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
    unsigned VR = I->first;
    const TargetRegisterClass *RC = MRI->getRegClass(VR);
    unsigned NewVR = MRI->createVirtualRegister(RC);
    RegMap[VR] = NewVR;
  }

  // We can generate the "insert" instructions using potentially stale re-
  // gisters: SrcR and InsR for a given VR may be among other registers that
  // are also replaced. This is fine, we will do the mass "rauw" a bit later.
  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
    MachineInstr *MI = MRI->getVRegDef(I->first);
    MachineBasicBlock &B = *MI->getParent();
    DebugLoc DL = MI->getDebugLoc();
    unsigned NewR = RegMap[I->first];
    bool R32 = MRI->getRegClass(NewR) == &Hexagon::IntRegsRegClass;
    const MCInstrDesc &D = R32 ? HII->get(Hexagon::S2_insert)
                               : HII->get(Hexagon::S2_insertp);
    IFRecord IF = I->second[0].first;
    unsigned Wdh = IF.Wdh, Off = IF.Off;
    unsigned InsS = 0;
    if (R32 && MRI->getRegClass(IF.InsR) == &Hexagon::DoubleRegsRegClass) {
      InsS = Hexagon::isub_lo;
      if (Off >= 32) {
        InsS = Hexagon::isub_hi;
        Off -= 32;
      }
    }
    // Advance to the proper location for inserting instructions. This could
    // be B.end().
    MachineBasicBlock::iterator At = MI;
    if (MI->isPHI())
      At = B.getFirstNonPHI();

    BuildMI(B, At, DL, D, NewR)
      .addReg(IF.SrcR)
      .addReg(IF.InsR, 0, InsS)
      .addImm(Wdh)
      .addImm(Off);

    MRI->clearKillFlags(IF.SrcR);
    MRI->clearKillFlags(IF.InsR);
  }

  for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
    MachineInstr *DefI = MRI->getVRegDef(I->first);
    MRI->replaceRegWith(I->first, RegMap[I->first]);
    DefI->eraseFromParent();
  }

  return true;
}

bool HexagonGenInsert::removeDeadCode(MachineDomTreeNode *N) {
  bool Changed = false;

  for (auto *DTN : children<MachineDomTreeNode*>(N))
    Changed |= removeDeadCode(DTN);

  MachineBasicBlock *B = N->getBlock();
  std::vector<MachineInstr*> Instrs;
  for (auto I = B->rbegin(), E = B->rend(); I != E; ++I)
    Instrs.push_back(&*I);

  for (auto I = Instrs.begin(), E = Instrs.end(); I != E; ++I) {
    MachineInstr *MI = *I;
    unsigned Opc = MI->getOpcode();
    // Do not touch lifetime markers. This is why the target-independent DCE
    // cannot be used.
    if (Opc == TargetOpcode::LIFETIME_START ||
        Opc == TargetOpcode::LIFETIME_END)
      continue;
    bool Store = false;
    if (MI->isInlineAsm() || !MI->isSafeToMove(nullptr, Store))
      continue;

    bool AllDead = true;
    SmallVector<unsigned,2> Regs;
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      unsigned R = MO.getReg();
      if (!TargetRegisterInfo::isVirtualRegister(R) ||
          !MRI->use_nodbg_empty(R)) {
        AllDead = false;
        break;
      }
      Regs.push_back(R);
    }
    if (!AllDead)
      continue;

    B->erase(MI);
    for (unsigned I = 0, N = Regs.size(); I != N; ++I)
      MRI->markUsesInDebugValueAsUndef(Regs[I]);
    Changed = true;
  }

  return Changed;
}

bool HexagonGenInsert::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  bool Timing = OptTiming, TimingDetail = Timing && OptTimingDetail;
  bool Changed = false;

  // Sanity check: one, but not both.
  assert(!OptSelectAll0 || !OptSelectHas0);

  IFMap.clear();
  BaseOrd.clear();
  CellOrd.clear();

  const auto &ST = MF.getSubtarget<HexagonSubtarget>();
  HII = ST.getInstrInfo();
  HRI = ST.getRegisterInfo();
  MFN = &MF;
  MRI = &MF.getRegInfo();
  MDT = &getAnalysis<MachineDominatorTree>();

  // Clean up before any further processing, so that dead code does not
  // get used in a newly generated "insert" instruction. Have a custom
  // version of DCE that preserves lifetime markers. Without it, merging
  // of stack objects can fail to recognize and merge disjoint objects
  // leading to unnecessary stack growth.
  Changed = removeDeadCode(MDT->getRootNode());

  const HexagonEvaluator HE(*HRI, *MRI, *HII, MF);
  BitTracker BTLoc(HE, MF);
  BTLoc.trace(isDebug());
  BTLoc.run();
  CellMapShadow MS(BTLoc);
  CMS = &MS;

  buildOrderingMF(BaseOrd);
  buildOrderingBT(BaseOrd, CellOrd);

  if (isDebug()) {
    dbgs() << "Cell ordering:\n";
    for (RegisterOrdering::iterator I = CellOrd.begin(), E = CellOrd.end();
        I != E; ++I) {
      unsigned VR = I->first, Pos = I->second;
      dbgs() << printReg(VR, HRI) << " -> " << Pos << "\n";
    }
  }

  // Collect candidates for conversion into the insert forms.
  MachineBasicBlock *RootB = MDT->getRoot();
  OrderedRegisterList AvailR(CellOrd);

  const char *const TGName = "hexinsert";
  const char *const TGDesc = "Generate Insert Instructions";

  {
    NamedRegionTimer _T("collection", "collection", TGName, TGDesc,
                        TimingDetail);
    collectInBlock(RootB, AvailR);
    // Complete the information gathered in IFMap.
    computeRemovableRegisters();
  }

  if (isDebug()) {
    dbgs() << "Candidates after collection:\n";
    dump_map();
  }

  if (IFMap.empty())
    return Changed;

  {
    NamedRegionTimer _T("pruning", "pruning", TGName, TGDesc, TimingDetail);
    pruneCandidates();
  }

  if (isDebug()) {
    dbgs() << "Candidates after pruning:\n";
    dump_map();
  }

  if (IFMap.empty())
    return Changed;

  {
    NamedRegionTimer _T("selection", "selection", TGName, TGDesc, TimingDetail);
    selectCandidates();
  }

  if (isDebug()) {
    dbgs() << "Candidates after selection:\n";
    dump_map();
  }

  // Filter out vregs beyond the cutoff.
  if (VRegIndexCutoff.getPosition()) {
    unsigned Cutoff = VRegIndexCutoff;

    using IterListType = SmallVector<IFMapType::iterator, 16>;

    IterListType Out;
    for (IFMapType::iterator I = IFMap.begin(), E = IFMap.end(); I != E; ++I) {
      unsigned Idx = TargetRegisterInfo::virtReg2Index(I->first);
      if (Idx >= Cutoff)
        Out.push_back(I);
    }
    for (unsigned i = 0, n = Out.size(); i < n; ++i)
      IFMap.erase(Out[i]);
  }
  if (IFMap.empty())
    return Changed;

  {
    NamedRegionTimer _T("generation", "generation", TGName, TGDesc,
                        TimingDetail);
    generateInserts();
  }

  return true;
}

FunctionPass *llvm::createHexagonGenInsert() {
  return new HexagonGenInsert();
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

INITIALIZE_PASS_BEGIN(HexagonGenInsert, "hexinsert",
  "Hexagon generate \"insert\" instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(HexagonGenInsert, "hexinsert",
  "Hexagon generate \"insert\" instructions", false, false)
