//===- MC/MCRegisterInfo.h - Target Register Description --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes an abstract interface used to get information about a
// target machines register file.  This information is used for a variety of
// purposed, especially register allocation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCREGISTERINFO_H
#define LLVM_MC_MCREGISTERINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/LaneBitmask.h"
#include <cassert>
#include <cstdint>
#include <utility>

namespace llvm {

/// An unsigned integer type large enough to represent all physical registers,
/// but not necessarily virtual registers.
using MCPhysReg = uint16_t;

/// MCRegisterClass - Base class of TargetRegisterClass.
class MCRegisterClass {
public:
  using iterator = const MCPhysReg*;
  using const_iterator = const MCPhysReg*;

  const iterator RegsBegin;
  const uint8_t *const RegSet;
  const uint32_t NameIdx;
  const uint16_t RegsSize;
  const uint16_t RegSetSize;
  const uint16_t ID;
  const int8_t CopyCost;
  const bool Allocatable;

  /// getID() - Return the register class ID number.
  ///
  unsigned getID() const { return ID; }

  /// begin/end - Return all of the registers in this class.
  ///
  iterator       begin() const { return RegsBegin; }
  iterator         end() const { return RegsBegin + RegsSize; }

  /// getNumRegs - Return the number of registers in this class.
  ///
  unsigned getNumRegs() const { return RegsSize; }

  /// getRegister - Return the specified register in the class.
  ///
  unsigned getRegister(unsigned i) const {
    assert(i < getNumRegs() && "Register number out of range!");
    return RegsBegin[i];
  }

  /// contains - Return true if the specified register is included in this
  /// register class.  This does not include virtual registers.
  bool contains(unsigned Reg) const {
    unsigned InByte = Reg % 8;
    unsigned Byte = Reg / 8;
    if (Byte >= RegSetSize)
      return false;
    return (RegSet[Byte] & (1 << InByte)) != 0;
  }

  /// contains - Return true if both registers are in this class.
  bool contains(unsigned Reg1, unsigned Reg2) const {
    return contains(Reg1) && contains(Reg2);
  }

  /// getCopyCost - Return the cost of copying a value between two registers in
  /// this class. A negative number means the register class is very expensive
  /// to copy e.g. status flag register classes.
  int getCopyCost() const { return CopyCost; }

  /// isAllocatable - Return true if this register class may be used to create
  /// virtual registers.
  bool isAllocatable() const { return Allocatable; }
};

/// MCRegisterDesc - This record contains information about a particular
/// register.  The SubRegs field is a zero terminated array of registers that
/// are sub-registers of the specific register, e.g. AL, AH are sub-registers
/// of AX. The SuperRegs field is a zero terminated array of registers that are
/// super-registers of the specific register, e.g. RAX, EAX, are
/// super-registers of AX.
///
struct MCRegisterDesc {
  uint32_t Name;      // Printable name for the reg (for debugging)
  uint32_t SubRegs;   // Sub-register set, described above
  uint32_t SuperRegs; // Super-register set, described above

  // Offset into MCRI::SubRegIndices of a list of sub-register indices for each
  // sub-register in SubRegs.
  uint32_t SubRegIndices;

  // RegUnits - Points to the list of register units. The low 4 bits holds the
  // Scale, the high bits hold an offset into DiffLists. See MCRegUnitIterator.
  uint32_t RegUnits;

  /// Index into list with lane mask sequences. The sequence contains a lanemask
  /// for every register unit.
  uint16_t RegUnitLaneMasks;
};

/// MCRegisterInfo base class - We assume that the target defines a static
/// array of MCRegisterDesc objects that represent all of the machine
/// registers that the target has.  As such, we simply have to track a pointer
/// to this array so that we can turn register number into a register
/// descriptor.
///
/// Note this class is designed to be a base class of TargetRegisterInfo, which
/// is the interface used by codegen. However, specific targets *should never*
/// specialize this class. MCRegisterInfo should only contain getters to access
/// TableGen generated physical register data. It must not be extended with
/// virtual methods.
///
class MCRegisterInfo {
public:
  using regclass_iterator = const MCRegisterClass *;

  /// DwarfLLVMRegPair - Emitted by tablegen so Dwarf<->LLVM reg mappings can be
  /// performed with a binary search.
  struct DwarfLLVMRegPair {
    unsigned FromReg;
    unsigned ToReg;

    bool operator<(DwarfLLVMRegPair RHS) const { return FromReg < RHS.FromReg; }
  };

  /// SubRegCoveredBits - Emitted by tablegen: bit range covered by a subreg
  /// index, -1 in any being invalid.
  struct SubRegCoveredBits {
    uint16_t Offset;
    uint16_t Size;
  };

private:
  const MCRegisterDesc *Desc;                 // Pointer to the descriptor array
  unsigned NumRegs;                           // Number of entries in the array
  unsigned RAReg;                             // Return address register
  unsigned PCReg;                             // Program counter register
  const MCRegisterClass *Classes;             // Pointer to the regclass array
  unsigned NumClasses;                        // Number of entries in the array
  unsigned NumRegUnits;                       // Number of regunits.
  const MCPhysReg (*RegUnitRoots)[2];         // Pointer to regunit root table.
  const MCPhysReg *DiffLists;                 // Pointer to the difflists array
  const LaneBitmask *RegUnitMaskSequences;    // Pointer to lane mask sequences
                                              // for register units.
  const char *RegStrings;                     // Pointer to the string table.
  const char *RegClassStrings;                // Pointer to the class strings.
  const uint16_t *SubRegIndices;              // Pointer to the subreg lookup
                                              // array.
  const SubRegCoveredBits *SubRegIdxRanges;   // Pointer to the subreg covered
                                              // bit ranges array.
  unsigned NumSubRegIndices;                  // Number of subreg indices.
  const uint16_t *RegEncodingTable;           // Pointer to array of register
                                              // encodings.

  unsigned L2DwarfRegsSize;
  unsigned EHL2DwarfRegsSize;
  unsigned Dwarf2LRegsSize;
  unsigned EHDwarf2LRegsSize;
  const DwarfLLVMRegPair *L2DwarfRegs;        // LLVM to Dwarf regs mapping
  const DwarfLLVMRegPair *EHL2DwarfRegs;      // LLVM to Dwarf regs mapping EH
  const DwarfLLVMRegPair *Dwarf2LRegs;        // Dwarf to LLVM regs mapping
  const DwarfLLVMRegPair *EHDwarf2LRegs;      // Dwarf to LLVM regs mapping EH
  DenseMap<unsigned, int> L2SEHRegs;          // LLVM to SEH regs mapping
  DenseMap<unsigned, int> L2CVRegs;           // LLVM to CV regs mapping

public:
  /// DiffListIterator - Base iterator class that can traverse the
  /// differentially encoded register and regunit lists in DiffLists.
  /// Don't use this class directly, use one of the specialized sub-classes
  /// defined below.
  class DiffListIterator {
    uint16_t Val = 0;
    const MCPhysReg *List = nullptr;

  protected:
    /// Create an invalid iterator. Call init() to point to something useful.
    DiffListIterator() = default;

    /// init - Point the iterator to InitVal, decoding subsequent values from
    /// DiffList. The iterator will initially point to InitVal, sub-classes are
    /// responsible for skipping the seed value if it is not part of the list.
    void init(MCPhysReg InitVal, const MCPhysReg *DiffList) {
      Val = InitVal;
      List = DiffList;
    }

    /// advance - Move to the next list position, return the applied
    /// differential. This function does not detect the end of the list, that
    /// is the caller's responsibility (by checking for a 0 return value).
    unsigned advance() {
      assert(isValid() && "Cannot move off the end of the list.");
      MCPhysReg D = *List++;
      Val += D;
      return D;
    }

  public:
    /// isValid - returns true if this iterator is not yet at the end.
    bool isValid() const { return List; }

    /// Dereference the iterator to get the value at the current position.
    unsigned operator*() const { return Val; }

    /// Pre-increment to move to the next position.
    void operator++() {
      // The end of the list is encoded as a 0 differential.
      if (!advance())
        List = nullptr;
    }
  };

  // These iterators are allowed to sub-class DiffListIterator and access
  // internal list pointers.
  friend class MCSubRegIterator;
  friend class MCSubRegIndexIterator;
  friend class MCSuperRegIterator;
  friend class MCRegUnitIterator;
  friend class MCRegUnitMaskIterator;
  friend class MCRegUnitRootIterator;

  /// Initialize MCRegisterInfo, called by TableGen
  /// auto-generated routines. *DO NOT USE*.
  void InitMCRegisterInfo(const MCRegisterDesc *D, unsigned NR, unsigned RA,
                          unsigned PC,
                          const MCRegisterClass *C, unsigned NC,
                          const MCPhysReg (*RURoots)[2],
                          unsigned NRU,
                          const MCPhysReg *DL,
                          const LaneBitmask *RUMS,
                          const char *Strings,
                          const char *ClassStrings,
                          const uint16_t *SubIndices,
                          unsigned NumIndices,
                          const SubRegCoveredBits *SubIdxRanges,
                          const uint16_t *RET) {
    Desc = D;
    NumRegs = NR;
    RAReg = RA;
    PCReg = PC;
    Classes = C;
    DiffLists = DL;
    RegUnitMaskSequences = RUMS;
    RegStrings = Strings;
    RegClassStrings = ClassStrings;
    NumClasses = NC;
    RegUnitRoots = RURoots;
    NumRegUnits = NRU;
    SubRegIndices = SubIndices;
    NumSubRegIndices = NumIndices;
    SubRegIdxRanges = SubIdxRanges;
    RegEncodingTable = RET;

    // Initialize DWARF register mapping variables
    EHL2DwarfRegs = nullptr;
    EHL2DwarfRegsSize = 0;
    L2DwarfRegs = nullptr;
    L2DwarfRegsSize = 0;
    EHDwarf2LRegs = nullptr;
    EHDwarf2LRegsSize = 0;
    Dwarf2LRegs = nullptr;
    Dwarf2LRegsSize = 0;
  }

  /// Used to initialize LLVM register to Dwarf
  /// register number mapping. Called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  void mapLLVMRegsToDwarfRegs(const DwarfLLVMRegPair *Map, unsigned Size,
                              bool isEH) {
    if (isEH) {
      EHL2DwarfRegs = Map;
      EHL2DwarfRegsSize = Size;
    } else {
      L2DwarfRegs = Map;
      L2DwarfRegsSize = Size;
    }
  }

  /// Used to initialize Dwarf register to LLVM
  /// register number mapping. Called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  void mapDwarfRegsToLLVMRegs(const DwarfLLVMRegPair *Map, unsigned Size,
                              bool isEH) {
    if (isEH) {
      EHDwarf2LRegs = Map;
      EHDwarf2LRegsSize = Size;
    } else {
      Dwarf2LRegs = Map;
      Dwarf2LRegsSize = Size;
    }
  }

  /// mapLLVMRegToSEHReg - Used to initialize LLVM register to SEH register
  /// number mapping. By default the SEH register number is just the same
  /// as the LLVM register number.
  /// FIXME: TableGen these numbers. Currently this requires target specific
  /// initialization code.
  void mapLLVMRegToSEHReg(unsigned LLVMReg, int SEHReg) {
    L2SEHRegs[LLVMReg] = SEHReg;
  }

  void mapLLVMRegToCVReg(unsigned LLVMReg, int CVReg) {
    L2CVRegs[LLVMReg] = CVReg;
  }

  /// This method should return the register where the return
  /// address can be found.
  unsigned getRARegister() const {
    return RAReg;
  }

  /// Return the register which is the program counter.
  unsigned getProgramCounter() const {
    return PCReg;
  }

  const MCRegisterDesc &operator[](unsigned RegNo) const {
    assert(RegNo < NumRegs &&
           "Attempting to access record for invalid register number!");
    return Desc[RegNo];
  }

  /// Provide a get method, equivalent to [], but more useful with a
  /// pointer to this object.
  const MCRegisterDesc &get(unsigned RegNo) const {
    return operator[](RegNo);
  }

  /// Returns the physical register number of sub-register "Index"
  /// for physical register RegNo. Return zero if the sub-register does not
  /// exist.
  unsigned getSubReg(unsigned Reg, unsigned Idx) const;

  /// Return a super-register of the specified register
  /// Reg so its sub-register of index SubIdx is Reg.
  unsigned getMatchingSuperReg(unsigned Reg, unsigned SubIdx,
                               const MCRegisterClass *RC) const;

  /// For a given register pair, return the sub-register index
  /// if the second register is a sub-register of the first. Return zero
  /// otherwise.
  unsigned getSubRegIndex(unsigned RegNo, unsigned SubRegNo) const;

  /// Get the size of the bit range covered by a sub-register index.
  /// If the index isn't continuous, return the sum of the sizes of its parts.
  /// If the index is used to access subregisters of different sizes, return -1.
  unsigned getSubRegIdxSize(unsigned Idx) const;

  /// Get the offset of the bit range covered by a sub-register index.
  /// If an Offset doesn't make sense (the index isn't continuous, or is used to
  /// access sub-registers at different offsets), return -1.
  unsigned getSubRegIdxOffset(unsigned Idx) const;

  /// Return the human-readable symbolic target-specific name for the
  /// specified physical register.
  const char *getName(unsigned RegNo) const {
    return RegStrings + get(RegNo).Name;
  }

  /// Return the number of registers this target has (useful for
  /// sizing arrays holding per register information)
  unsigned getNumRegs() const {
    return NumRegs;
  }

  /// Return the number of sub-register indices
  /// understood by the target. Index 0 is reserved for the no-op sub-register,
  /// while 1 to getNumSubRegIndices() - 1 represent real sub-registers.
  unsigned getNumSubRegIndices() const {
    return NumSubRegIndices;
  }

  /// Return the number of (native) register units in the
  /// target. Register units are numbered from 0 to getNumRegUnits() - 1. They
  /// can be accessed through MCRegUnitIterator defined below.
  unsigned getNumRegUnits() const {
    return NumRegUnits;
  }

  /// Map a target register to an equivalent dwarf register
  /// number.  Returns -1 if there is no equivalent value.  The second
  /// parameter allows targets to use different numberings for EH info and
  /// debugging info.
  int getDwarfRegNum(unsigned RegNum, bool isEH) const;

  /// Map a dwarf register back to a target register.
  int getLLVMRegNum(unsigned RegNum, bool isEH) const;

  /// Map a DWARF EH register back to a target register (same as
  /// getLLVMRegNum(RegNum, true)) but return -1 if there is no mapping,
  /// rather than asserting that there must be one.
  int getLLVMRegNumFromEH(unsigned RegNum) const;

  /// Map a target EH register number to an equivalent DWARF register
  /// number.
  int getDwarfRegNumFromDwarfEHRegNum(unsigned RegNum) const;

  /// Map a target register to an equivalent SEH register
  /// number.  Returns LLVM register number if there is no equivalent value.
  int getSEHRegNum(unsigned RegNum) const;

  /// Map a target register to an equivalent CodeView register
  /// number.
  int getCodeViewRegNum(unsigned RegNum) const;

  regclass_iterator regclass_begin() const { return Classes; }
  regclass_iterator regclass_end() const { return Classes+NumClasses; }
  iterator_range<regclass_iterator> regclasses() const {
    return make_range(regclass_begin(), regclass_end());
  }

  unsigned getNumRegClasses() const {
    return (unsigned)(regclass_end()-regclass_begin());
  }

  /// Returns the register class associated with the enumeration
  /// value.  See class MCOperandInfo.
  const MCRegisterClass& getRegClass(unsigned i) const {
    assert(i < getNumRegClasses() && "Register Class ID out of range");
    return Classes[i];
  }

  const char *getRegClassName(const MCRegisterClass *Class) const {
    return RegClassStrings + Class->NameIdx;
  }

   /// Returns the encoding for RegNo
  uint16_t getEncodingValue(unsigned RegNo) const {
    assert(RegNo < NumRegs &&
           "Attempting to get encoding for invalid register number!");
    return RegEncodingTable[RegNo];
  }

  /// Returns true if RegB is a sub-register of RegA.
  bool isSubRegister(unsigned RegA, unsigned RegB) const {
    return isSuperRegister(RegB, RegA);
  }

  /// Returns true if RegB is a super-register of RegA.
  bool isSuperRegister(unsigned RegA, unsigned RegB) const;

  /// Returns true if RegB is a sub-register of RegA or if RegB == RegA.
  bool isSubRegisterEq(unsigned RegA, unsigned RegB) const {
    return isSuperRegisterEq(RegB, RegA);
  }

  /// Returns true if RegB is a super-register of RegA or if
  /// RegB == RegA.
  bool isSuperRegisterEq(unsigned RegA, unsigned RegB) const {
    return RegA == RegB || isSuperRegister(RegA, RegB);
  }

  /// Returns true if RegB is a super-register or sub-register of RegA
  /// or if RegB == RegA.
  bool isSuperOrSubRegisterEq(unsigned RegA, unsigned RegB) const {
    return isSubRegisterEq(RegA, RegB) || isSuperRegister(RegA, RegB);
  }
};

//===----------------------------------------------------------------------===//
//                          Register List Iterators
//===----------------------------------------------------------------------===//

// MCRegisterInfo provides lists of super-registers, sub-registers, and
// aliasing registers. Use these iterator classes to traverse the lists.

/// MCSubRegIterator enumerates all sub-registers of Reg.
/// If IncludeSelf is set, Reg itself is included in the list.
class MCSubRegIterator : public MCRegisterInfo::DiffListIterator {
public:
  MCSubRegIterator(unsigned Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf = false) {
    init(Reg, MCRI->DiffLists + MCRI->get(Reg).SubRegs);
    // Initially, the iterator points to Reg itself.
    if (!IncludeSelf)
      ++*this;
  }
};

/// Iterator that enumerates the sub-registers of a Reg and the associated
/// sub-register indices.
class MCSubRegIndexIterator {
  MCSubRegIterator SRIter;
  const uint16_t *SRIndex;

public:
  /// Constructs an iterator that traverses subregisters and their
  /// associated subregister indices.
  MCSubRegIndexIterator(unsigned Reg, const MCRegisterInfo *MCRI)
    : SRIter(Reg, MCRI) {
    SRIndex = MCRI->SubRegIndices + MCRI->get(Reg).SubRegIndices;
  }

  /// Returns current sub-register.
  unsigned getSubReg() const {
    return *SRIter;
  }

  /// Returns sub-register index of the current sub-register.
  unsigned getSubRegIndex() const {
    return *SRIndex;
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return SRIter.isValid(); }

  /// Moves to the next position.
  void operator++() {
    ++SRIter;
    ++SRIndex;
  }
};

/// MCSuperRegIterator enumerates all super-registers of Reg.
/// If IncludeSelf is set, Reg itself is included in the list.
class MCSuperRegIterator : public MCRegisterInfo::DiffListIterator {
public:
  MCSuperRegIterator() = default;

  MCSuperRegIterator(unsigned Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf = false) {
    init(Reg, MCRI->DiffLists + MCRI->get(Reg).SuperRegs);
    // Initially, the iterator points to Reg itself.
    if (!IncludeSelf)
      ++*this;
  }
};

// Definition for isSuperRegister. Put it down here since it needs the
// iterator defined above in addition to the MCRegisterInfo class itself.
inline bool MCRegisterInfo::isSuperRegister(unsigned RegA, unsigned RegB) const{
  for (MCSuperRegIterator I(RegA, this); I.isValid(); ++I)
    if (*I == RegB)
      return true;
  return false;
}

//===----------------------------------------------------------------------===//
//                               Register Units
//===----------------------------------------------------------------------===//

// Register units are used to compute register aliasing. Every register has at
// least one register unit, but it can have more. Two registers overlap if and
// only if they have a common register unit.
//
// A target with a complicated sub-register structure will typically have many
// fewer register units than actual registers. MCRI::getNumRegUnits() returns
// the number of register units in the target.

// MCRegUnitIterator enumerates a list of register units for Reg. The list is
// in ascending numerical order.
class MCRegUnitIterator : public MCRegisterInfo::DiffListIterator {
public:
  /// MCRegUnitIterator - Create an iterator that traverses the register units
  /// in Reg.
  MCRegUnitIterator() = default;

  MCRegUnitIterator(unsigned Reg, const MCRegisterInfo *MCRI) {
    assert(Reg && "Null register has no regunits");
    // Decode the RegUnits MCRegisterDesc field.
    unsigned RU = MCRI->get(Reg).RegUnits;
    unsigned Scale = RU & 15;
    unsigned Offset = RU >> 4;

    // Initialize the iterator to Reg * Scale, and the List pointer to
    // DiffLists + Offset.
    init(Reg * Scale, MCRI->DiffLists + Offset);

    // That may not be a valid unit, we need to advance by one to get the real
    // unit number. The first differential can be 0 which would normally
    // terminate the list, but since we know every register has at least one
    // unit, we can allow a 0 differential here.
    advance();
  }
};

/// MCRegUnitMaskIterator enumerates a list of register units and their
/// associated lane masks for Reg. The register units are in ascending
/// numerical order.
class MCRegUnitMaskIterator {
  MCRegUnitIterator RUIter;
  const LaneBitmask *MaskListIter;

public:
  MCRegUnitMaskIterator() = default;

  /// Constructs an iterator that traverses the register units and their
  /// associated LaneMasks in Reg.
  MCRegUnitMaskIterator(unsigned Reg, const MCRegisterInfo *MCRI)
    : RUIter(Reg, MCRI) {
      uint16_t Idx = MCRI->get(Reg).RegUnitLaneMasks;
      MaskListIter = &MCRI->RegUnitMaskSequences[Idx];
  }

  /// Returns a (RegUnit, LaneMask) pair.
  std::pair<unsigned,LaneBitmask> operator*() const {
    return std::make_pair(*RUIter, *MaskListIter);
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return RUIter.isValid(); }

  /// Moves to the next position.
  void operator++() {
    ++MaskListIter;
    ++RUIter;
  }
};

// Each register unit has one or two root registers. The complete set of
// registers containing a register unit is the union of the roots and their
// super-registers. All registers aliasing Unit can be visited like this:
//
//   for (MCRegUnitRootIterator RI(Unit, MCRI); RI.isValid(); ++RI) {
//     for (MCSuperRegIterator SI(*RI, MCRI, true); SI.isValid(); ++SI)
//       visit(*SI);
//    }

/// MCRegUnitRootIterator enumerates the root registers of a register unit.
class MCRegUnitRootIterator {
  uint16_t Reg0 = 0;
  uint16_t Reg1 = 0;

public:
  MCRegUnitRootIterator() = default;

  MCRegUnitRootIterator(unsigned RegUnit, const MCRegisterInfo *MCRI) {
    assert(RegUnit < MCRI->getNumRegUnits() && "Invalid register unit");
    Reg0 = MCRI->RegUnitRoots[RegUnit][0];
    Reg1 = MCRI->RegUnitRoots[RegUnit][1];
  }

  /// Dereference to get the current root register.
  unsigned operator*() const {
    return Reg0;
  }

  /// Check if the iterator is at the end of the list.
  bool isValid() const {
    return Reg0;
  }

  /// Preincrement to move to the next root register.
  void operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    Reg0 = Reg1;
    Reg1 = 0;
  }
};

/// MCRegAliasIterator enumerates all registers aliasing Reg.  If IncludeSelf is
/// set, Reg itself is included in the list.  This iterator does not guarantee
/// any ordering or that entries are unique.
class MCRegAliasIterator {
private:
  unsigned Reg;
  const MCRegisterInfo *MCRI;
  bool IncludeSelf;

  MCRegUnitIterator RI;
  MCRegUnitRootIterator RRI;
  MCSuperRegIterator SI;

public:
  MCRegAliasIterator(unsigned Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf)
    : Reg(Reg), MCRI(MCRI), IncludeSelf(IncludeSelf) {
    // Initialize the iterators.
    for (RI = MCRegUnitIterator(Reg, MCRI); RI.isValid(); ++RI) {
      for (RRI = MCRegUnitRootIterator(*RI, MCRI); RRI.isValid(); ++RRI) {
        for (SI = MCSuperRegIterator(*RRI, MCRI, true); SI.isValid(); ++SI) {
          if (!(!IncludeSelf && Reg == *SI))
            return;
        }
      }
    }
  }

  bool isValid() const { return RI.isValid(); }

  unsigned operator*() const {
    assert(SI.isValid() && "Cannot dereference an invalid iterator.");
    return *SI;
  }

  void advance() {
    // Assuming SI is valid.
    ++SI;
    if (SI.isValid()) return;

    ++RRI;
    if (RRI.isValid()) {
      SI = MCSuperRegIterator(*RRI, MCRI, true);
      return;
    }

    ++RI;
    if (RI.isValid()) {
      RRI = MCRegUnitRootIterator(*RI, MCRI);
      SI = MCSuperRegIterator(*RRI, MCRI, true);
    }
  }

  void operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    do advance();
    while (!IncludeSelf && isValid() && *SI == Reg);
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCREGISTERINFO_H
