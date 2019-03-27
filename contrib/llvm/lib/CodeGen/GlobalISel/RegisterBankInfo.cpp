//===- llvm/CodeGen/GlobalISel/RegisterBankInfo.cpp --------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the RegisterBankInfo class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm> // For std::max.

#define DEBUG_TYPE "registerbankinfo"

using namespace llvm;

STATISTIC(NumPartialMappingsCreated,
          "Number of partial mappings dynamically created");
STATISTIC(NumPartialMappingsAccessed,
          "Number of partial mappings dynamically accessed");
STATISTIC(NumValueMappingsCreated,
          "Number of value mappings dynamically created");
STATISTIC(NumValueMappingsAccessed,
          "Number of value mappings dynamically accessed");
STATISTIC(NumOperandsMappingsCreated,
          "Number of operands mappings dynamically created");
STATISTIC(NumOperandsMappingsAccessed,
          "Number of operands mappings dynamically accessed");
STATISTIC(NumInstructionMappingsCreated,
          "Number of instruction mappings dynamically created");
STATISTIC(NumInstructionMappingsAccessed,
          "Number of instruction mappings dynamically accessed");

const unsigned RegisterBankInfo::DefaultMappingID = UINT_MAX;
const unsigned RegisterBankInfo::InvalidMappingID = UINT_MAX - 1;

//------------------------------------------------------------------------------
// RegisterBankInfo implementation.
//------------------------------------------------------------------------------
RegisterBankInfo::RegisterBankInfo(RegisterBank **RegBanks,
                                   unsigned NumRegBanks)
    : RegBanks(RegBanks), NumRegBanks(NumRegBanks) {
#ifndef NDEBUG
  for (unsigned Idx = 0, End = getNumRegBanks(); Idx != End; ++Idx) {
    assert(RegBanks[Idx] != nullptr && "Invalid RegisterBank");
    assert(RegBanks[Idx]->isValid() && "RegisterBank should be valid");
  }
#endif // NDEBUG
}

bool RegisterBankInfo::verify(const TargetRegisterInfo &TRI) const {
#ifndef NDEBUG
  for (unsigned Idx = 0, End = getNumRegBanks(); Idx != End; ++Idx) {
    const RegisterBank &RegBank = getRegBank(Idx);
    assert(Idx == RegBank.getID() &&
           "ID does not match the index in the array");
    LLVM_DEBUG(dbgs() << "Verify " << RegBank << '\n');
    assert(RegBank.verify(TRI) && "RegBank is invalid");
  }
#endif // NDEBUG
  return true;
}

const RegisterBank *
RegisterBankInfo::getRegBank(unsigned Reg, const MachineRegisterInfo &MRI,
                             const TargetRegisterInfo &TRI) const {
  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return &getRegBankFromRegClass(getMinimalPhysRegClass(Reg, TRI));

  assert(Reg && "NoRegister does not have a register bank");
  const RegClassOrRegBank &RegClassOrBank = MRI.getRegClassOrRegBank(Reg);
  if (auto *RB = RegClassOrBank.dyn_cast<const RegisterBank *>())
    return RB;
  if (auto *RC = RegClassOrBank.dyn_cast<const TargetRegisterClass *>())
    return &getRegBankFromRegClass(*RC);
  return nullptr;
}

const TargetRegisterClass &
RegisterBankInfo::getMinimalPhysRegClass(unsigned Reg,
                                         const TargetRegisterInfo &TRI) const {
  assert(TargetRegisterInfo::isPhysicalRegister(Reg) &&
         "Reg must be a physreg");
  const auto &RegRCIt = PhysRegMinimalRCs.find(Reg);
  if (RegRCIt != PhysRegMinimalRCs.end())
    return *RegRCIt->second;
  const TargetRegisterClass *PhysRC = TRI.getMinimalPhysRegClass(Reg);
  PhysRegMinimalRCs[Reg] = PhysRC;
  return *PhysRC;
}

const RegisterBank *RegisterBankInfo::getRegBankFromConstraints(
    const MachineInstr &MI, unsigned OpIdx, const TargetInstrInfo &TII,
    const TargetRegisterInfo &TRI) const {
  // The mapping of the registers may be available via the
  // register class constraints.
  const TargetRegisterClass *RC = MI.getRegClassConstraint(OpIdx, &TII, &TRI);

  if (!RC)
    return nullptr;

  const RegisterBank &RegBank = getRegBankFromRegClass(*RC);
  // Sanity check that the target properly implemented getRegBankFromRegClass.
  assert(RegBank.covers(*RC) &&
         "The mapping of the register bank does not make sense");
  return &RegBank;
}

const TargetRegisterClass *RegisterBankInfo::constrainGenericRegister(
    unsigned Reg, const TargetRegisterClass &RC, MachineRegisterInfo &MRI) {

  // If the register already has a class, fallback to MRI::constrainRegClass.
  auto &RegClassOrBank = MRI.getRegClassOrRegBank(Reg);
  if (RegClassOrBank.is<const TargetRegisterClass *>())
    return MRI.constrainRegClass(Reg, &RC);

  const RegisterBank *RB = RegClassOrBank.get<const RegisterBank *>();
  // Otherwise, all we can do is ensure the bank covers the class, and set it.
  if (RB && !RB->covers(RC))
    return nullptr;

  // If nothing was set or the class is simply compatible, set it.
  MRI.setRegClass(Reg, &RC);
  return &RC;
}

/// Check whether or not \p MI should be treated like a copy
/// for the mappings.
/// Copy like instruction are special for mapping because
/// they don't have actual register constraints. Moreover,
/// they sometimes have register classes assigned and we can
/// just use that instead of failing to provide a generic mapping.
static bool isCopyLike(const MachineInstr &MI) {
  return MI.isCopy() || MI.isPHI() ||
         MI.getOpcode() == TargetOpcode::REG_SEQUENCE;
}

const RegisterBankInfo::InstructionMapping &
RegisterBankInfo::getInstrMappingImpl(const MachineInstr &MI) const {
  // For copies we want to walk over the operands and try to find one
  // that has a register bank since the instruction itself will not get
  // us any constraint.
  bool IsCopyLike = isCopyLike(MI);
  // For copy like instruction, only the mapping of the definition
  // is important. The rest is not constrained.
  unsigned NumOperandsForMapping = IsCopyLike ? 1 : MI.getNumOperands();

  const MachineFunction &MF = *MI.getMF();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  // We may need to query the instruction encoding to guess the mapping.
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  // Before doing anything complicated check if the mapping is not
  // directly available.
  bool CompleteMapping = true;

  SmallVector<const ValueMapping *, 8> OperandsMapping(NumOperandsForMapping);
  for (unsigned OpIdx = 0, EndIdx = MI.getNumOperands(); OpIdx != EndIdx;
       ++OpIdx) {
    const MachineOperand &MO = MI.getOperand(OpIdx);
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    // The register bank of Reg is just a side effect of the current
    // excution and in particular, there is no reason to believe this
    // is the best default mapping for the current instruction.  Keep
    // it as an alternative register bank if we cannot figure out
    // something.
    const RegisterBank *AltRegBank = getRegBank(Reg, MRI, TRI);
    // For copy-like instruction, we want to reuse the register bank
    // that is already set on Reg, if any, since those instructions do
    // not have any constraints.
    const RegisterBank *CurRegBank = IsCopyLike ? AltRegBank : nullptr;
    if (!CurRegBank) {
      // If this is a target specific instruction, we can deduce
      // the register bank from the encoding constraints.
      CurRegBank = getRegBankFromConstraints(MI, OpIdx, TII, TRI);
      if (!CurRegBank) {
        // All our attempts failed, give up.
        CompleteMapping = false;

        if (!IsCopyLike)
          // MI does not carry enough information to guess the mapping.
          return getInvalidInstructionMapping();
        continue;
      }
    }
    const ValueMapping *ValMapping =
        &getValueMapping(0, getSizeInBits(Reg, MRI, TRI), *CurRegBank);
    if (IsCopyLike) {
      OperandsMapping[0] = ValMapping;
      CompleteMapping = true;
      break;
    }
    OperandsMapping[OpIdx] = ValMapping;
  }

  if (IsCopyLike && !CompleteMapping)
    // No way to deduce the type from what we have.
    return getInvalidInstructionMapping();

  assert(CompleteMapping && "Setting an uncomplete mapping");
  return getInstructionMapping(
      DefaultMappingID, /*Cost*/ 1,
      /*OperandsMapping*/ getOperandsMapping(OperandsMapping),
      NumOperandsForMapping);
}

/// Hashing function for PartialMapping.
static hash_code hashPartialMapping(unsigned StartIdx, unsigned Length,
                                    const RegisterBank *RegBank) {
  return hash_combine(StartIdx, Length, RegBank ? RegBank->getID() : 0);
}

/// Overloaded version of hash_value for a PartialMapping.
hash_code
llvm::hash_value(const RegisterBankInfo::PartialMapping &PartMapping) {
  return hashPartialMapping(PartMapping.StartIdx, PartMapping.Length,
                            PartMapping.RegBank);
}

const RegisterBankInfo::PartialMapping &
RegisterBankInfo::getPartialMapping(unsigned StartIdx, unsigned Length,
                                    const RegisterBank &RegBank) const {
  ++NumPartialMappingsAccessed;

  hash_code Hash = hashPartialMapping(StartIdx, Length, &RegBank);
  const auto &It = MapOfPartialMappings.find(Hash);
  if (It != MapOfPartialMappings.end())
    return *It->second;

  ++NumPartialMappingsCreated;

  auto &PartMapping = MapOfPartialMappings[Hash];
  PartMapping = llvm::make_unique<PartialMapping>(StartIdx, Length, RegBank);
  return *PartMapping;
}

const RegisterBankInfo::ValueMapping &
RegisterBankInfo::getValueMapping(unsigned StartIdx, unsigned Length,
                                  const RegisterBank &RegBank) const {
  return getValueMapping(&getPartialMapping(StartIdx, Length, RegBank), 1);
}

static hash_code
hashValueMapping(const RegisterBankInfo::PartialMapping *BreakDown,
                 unsigned NumBreakDowns) {
  if (LLVM_LIKELY(NumBreakDowns == 1))
    return hash_value(*BreakDown);
  SmallVector<size_t, 8> Hashes(NumBreakDowns);
  for (unsigned Idx = 0; Idx != NumBreakDowns; ++Idx)
    Hashes.push_back(hash_value(BreakDown[Idx]));
  return hash_combine_range(Hashes.begin(), Hashes.end());
}

const RegisterBankInfo::ValueMapping &
RegisterBankInfo::getValueMapping(const PartialMapping *BreakDown,
                                  unsigned NumBreakDowns) const {
  ++NumValueMappingsAccessed;

  hash_code Hash = hashValueMapping(BreakDown, NumBreakDowns);
  const auto &It = MapOfValueMappings.find(Hash);
  if (It != MapOfValueMappings.end())
    return *It->second;

  ++NumValueMappingsCreated;

  auto &ValMapping = MapOfValueMappings[Hash];
  ValMapping = llvm::make_unique<ValueMapping>(BreakDown, NumBreakDowns);
  return *ValMapping;
}

template <typename Iterator>
const RegisterBankInfo::ValueMapping *
RegisterBankInfo::getOperandsMapping(Iterator Begin, Iterator End) const {

  ++NumOperandsMappingsAccessed;

  // The addresses of the value mapping are unique.
  // Therefore, we can use them directly to hash the operand mapping.
  hash_code Hash = hash_combine_range(Begin, End);
  auto &Res = MapOfOperandsMappings[Hash];
  if (Res)
    return Res.get();

  ++NumOperandsMappingsCreated;

  // Create the array of ValueMapping.
  // Note: this array will not hash to this instance of operands
  // mapping, because we use the pointer of the ValueMapping
  // to hash and we expect them to uniquely identify an instance
  // of value mapping.
  Res = llvm::make_unique<ValueMapping[]>(std::distance(Begin, End));
  unsigned Idx = 0;
  for (Iterator It = Begin; It != End; ++It, ++Idx) {
    const ValueMapping *ValMap = *It;
    if (!ValMap)
      continue;
    Res[Idx] = *ValMap;
  }
  return Res.get();
}

const RegisterBankInfo::ValueMapping *RegisterBankInfo::getOperandsMapping(
    const SmallVectorImpl<const RegisterBankInfo::ValueMapping *> &OpdsMapping)
    const {
  return getOperandsMapping(OpdsMapping.begin(), OpdsMapping.end());
}

const RegisterBankInfo::ValueMapping *RegisterBankInfo::getOperandsMapping(
    std::initializer_list<const RegisterBankInfo::ValueMapping *> OpdsMapping)
    const {
  return getOperandsMapping(OpdsMapping.begin(), OpdsMapping.end());
}

static hash_code
hashInstructionMapping(unsigned ID, unsigned Cost,
                       const RegisterBankInfo::ValueMapping *OperandsMapping,
                       unsigned NumOperands) {
  return hash_combine(ID, Cost, OperandsMapping, NumOperands);
}

const RegisterBankInfo::InstructionMapping &
RegisterBankInfo::getInstructionMappingImpl(
    bool IsInvalid, unsigned ID, unsigned Cost,
    const RegisterBankInfo::ValueMapping *OperandsMapping,
    unsigned NumOperands) const {
  assert(((IsInvalid && ID == InvalidMappingID && Cost == 0 &&
           OperandsMapping == nullptr && NumOperands == 0) ||
          !IsInvalid) &&
         "Mismatch argument for invalid input");
  ++NumInstructionMappingsAccessed;

  hash_code Hash =
      hashInstructionMapping(ID, Cost, OperandsMapping, NumOperands);
  const auto &It = MapOfInstructionMappings.find(Hash);
  if (It != MapOfInstructionMappings.end())
    return *It->second;

  ++NumInstructionMappingsCreated;

  auto &InstrMapping = MapOfInstructionMappings[Hash];
  if (IsInvalid)
    InstrMapping = llvm::make_unique<InstructionMapping>();
  else
    InstrMapping = llvm::make_unique<InstructionMapping>(
        ID, Cost, OperandsMapping, NumOperands);
  return *InstrMapping;
}

const RegisterBankInfo::InstructionMapping &
RegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  const RegisterBankInfo::InstructionMapping &Mapping = getInstrMappingImpl(MI);
  if (Mapping.isValid())
    return Mapping;
  llvm_unreachable("The target must implement this");
}

RegisterBankInfo::InstructionMappings
RegisterBankInfo::getInstrPossibleMappings(const MachineInstr &MI) const {
  InstructionMappings PossibleMappings;
  // Put the default mapping first.
  PossibleMappings.push_back(&getInstrMapping(MI));
  // Then the alternative mapping, if any.
  InstructionMappings AltMappings = getInstrAlternativeMappings(MI);
  for (const InstructionMapping *AltMapping : AltMappings)
    PossibleMappings.push_back(AltMapping);
#ifndef NDEBUG
  for (const InstructionMapping *Mapping : PossibleMappings)
    assert(Mapping->verify(MI) && "Mapping is invalid");
#endif
  return PossibleMappings;
}

RegisterBankInfo::InstructionMappings
RegisterBankInfo::getInstrAlternativeMappings(const MachineInstr &MI) const {
  // No alternative for MI.
  return InstructionMappings();
}

void RegisterBankInfo::applyDefaultMapping(const OperandsMapper &OpdMapper) {
  MachineInstr &MI = OpdMapper.getMI();
  MachineRegisterInfo &MRI = OpdMapper.getMRI();
  LLVM_DEBUG(dbgs() << "Applying default-like mapping\n");
  for (unsigned OpIdx = 0,
                EndIdx = OpdMapper.getInstrMapping().getNumOperands();
       OpIdx != EndIdx; ++OpIdx) {
    LLVM_DEBUG(dbgs() << "OpIdx " << OpIdx);
    MachineOperand &MO = MI.getOperand(OpIdx);
    if (!MO.isReg()) {
      LLVM_DEBUG(dbgs() << " is not a register, nothing to be done\n");
      continue;
    }
    if (!MO.getReg()) {
      LLVM_DEBUG(dbgs() << " is %%noreg, nothing to be done\n");
      continue;
    }
    assert(OpdMapper.getInstrMapping().getOperandMapping(OpIdx).NumBreakDowns !=
               0 &&
           "Invalid mapping");
    assert(OpdMapper.getInstrMapping().getOperandMapping(OpIdx).NumBreakDowns ==
               1 &&
           "This mapping is too complex for this function");
    iterator_range<SmallVectorImpl<unsigned>::const_iterator> NewRegs =
        OpdMapper.getVRegs(OpIdx);
    if (empty(NewRegs)) {
      LLVM_DEBUG(dbgs() << " has not been repaired, nothing to be done\n");
      continue;
    }
    unsigned OrigReg = MO.getReg();
    unsigned NewReg = *NewRegs.begin();
    LLVM_DEBUG(dbgs() << " changed, replace " << printReg(OrigReg, nullptr));
    MO.setReg(NewReg);
    LLVM_DEBUG(dbgs() << " with " << printReg(NewReg, nullptr));

    // The OperandsMapper creates plain scalar, we may have to fix that.
    // Check if the types match and if not, fix that.
    LLT OrigTy = MRI.getType(OrigReg);
    LLT NewTy = MRI.getType(NewReg);
    if (OrigTy != NewTy) {
      // The default mapping is not supposed to change the size of
      // the storage. However, right now we don't necessarily bump all
      // the types to storage size. For instance, we can consider
      // s16 G_AND legal whereas the storage size is going to be 32.
      assert(OrigTy.getSizeInBits() <= NewTy.getSizeInBits() &&
             "Types with difference size cannot be handled by the default "
             "mapping");
      LLVM_DEBUG(dbgs() << "\nChange type of new opd from " << NewTy << " to "
                        << OrigTy);
      MRI.setType(NewReg, OrigTy);
    }
    LLVM_DEBUG(dbgs() << '\n');
  }
}

unsigned RegisterBankInfo::getSizeInBits(unsigned Reg,
                                         const MachineRegisterInfo &MRI,
                                         const TargetRegisterInfo &TRI) const {
  if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
    // The size is not directly available for physical registers.
    // Instead, we need to access a register class that contains Reg and
    // get the size of that register class.
    // Because this is expensive, we'll cache the register class by calling
    auto *RC = &getMinimalPhysRegClass(Reg, TRI);
    assert(RC && "Expecting Register class");
    return TRI.getRegSizeInBits(*RC);
  }
  return TRI.getRegSizeInBits(Reg, MRI);
}

//------------------------------------------------------------------------------
// Helper classes implementation.
//------------------------------------------------------------------------------
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegisterBankInfo::PartialMapping::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

bool RegisterBankInfo::PartialMapping::verify() const {
  assert(RegBank && "Register bank not set");
  assert(Length && "Empty mapping");
  assert((StartIdx <= getHighBitIdx()) && "Overflow, switch to APInt?");
  // Check if the minimum width fits into RegBank.
  assert(RegBank->getSize() >= Length && "Register bank too small for Mask");
  return true;
}

void RegisterBankInfo::PartialMapping::print(raw_ostream &OS) const {
  OS << "[" << StartIdx << ", " << getHighBitIdx() << "], RegBank = ";
  if (RegBank)
    OS << *RegBank;
  else
    OS << "nullptr";
}

bool RegisterBankInfo::ValueMapping::verify(unsigned MeaningfulBitWidth) const {
  assert(NumBreakDowns && "Value mapped nowhere?!");
  unsigned OrigValueBitWidth = 0;
  for (const RegisterBankInfo::PartialMapping &PartMap : *this) {
    // Check that each register bank is big enough to hold the partial value:
    // this check is done by PartialMapping::verify
    assert(PartMap.verify() && "Partial mapping is invalid");
    // The original value should completely be mapped.
    // Thus the maximum accessed index + 1 is the size of the original value.
    OrigValueBitWidth =
        std::max(OrigValueBitWidth, PartMap.getHighBitIdx() + 1);
  }
  assert(OrigValueBitWidth >= MeaningfulBitWidth &&
         "Meaningful bits not covered by the mapping");
  APInt ValueMask(OrigValueBitWidth, 0);
  for (const RegisterBankInfo::PartialMapping &PartMap : *this) {
    // Check that the union of the partial mappings covers the whole value,
    // without overlaps.
    // The high bit is exclusive in the APInt API, thus getHighBitIdx + 1.
    APInt PartMapMask = APInt::getBitsSet(OrigValueBitWidth, PartMap.StartIdx,
                                          PartMap.getHighBitIdx() + 1);
    ValueMask ^= PartMapMask;
    assert((ValueMask & PartMapMask) == PartMapMask &&
           "Some partial mappings overlap");
  }
  assert(ValueMask.isAllOnesValue() && "Value is not fully mapped");
  return true;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegisterBankInfo::ValueMapping::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

void RegisterBankInfo::ValueMapping::print(raw_ostream &OS) const {
  OS << "#BreakDown: " << NumBreakDowns << " ";
  bool IsFirst = true;
  for (const PartialMapping &PartMap : *this) {
    if (!IsFirst)
      OS << ", ";
    OS << '[' << PartMap << ']';
    IsFirst = false;
  }
}

bool RegisterBankInfo::InstructionMapping::verify(
    const MachineInstr &MI) const {
  // Check that all the register operands are properly mapped.
  // Check the constructor invariant.
  // For PHI, we only care about mapping the definition.
  assert(NumOperands == (isCopyLike(MI) ? 1 : MI.getNumOperands()) &&
         "NumOperands must match, see constructor");
  assert(MI.getParent() && MI.getMF() &&
         "MI must be connected to a MachineFunction");
  const MachineFunction &MF = *MI.getMF();
  const RegisterBankInfo *RBI = MF.getSubtarget().getRegBankInfo();
  (void)RBI;

  for (unsigned Idx = 0; Idx < NumOperands; ++Idx) {
    const MachineOperand &MO = MI.getOperand(Idx);
    if (!MO.isReg()) {
      assert(!getOperandMapping(Idx).isValid() &&
             "We should not care about non-reg mapping");
      continue;
    }
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    assert(getOperandMapping(Idx).isValid() &&
           "We must have a mapping for reg operands");
    const RegisterBankInfo::ValueMapping &MOMapping = getOperandMapping(Idx);
    (void)MOMapping;
    // Register size in bits.
    // This size must match what the mapping expects.
    assert(MOMapping.verify(RBI->getSizeInBits(
               Reg, MF.getRegInfo(), *MF.getSubtarget().getRegisterInfo())) &&
           "Value mapping is invalid");
  }
  return true;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegisterBankInfo::InstructionMapping::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

void RegisterBankInfo::InstructionMapping::print(raw_ostream &OS) const {
  OS << "ID: " << getID() << " Cost: " << getCost() << " Mapping: ";

  for (unsigned OpIdx = 0; OpIdx != NumOperands; ++OpIdx) {
    const ValueMapping &ValMapping = getOperandMapping(OpIdx);
    if (OpIdx)
      OS << ", ";
    OS << "{ Idx: " << OpIdx << " Map: " << ValMapping << '}';
  }
}

const int RegisterBankInfo::OperandsMapper::DontKnowIdx = -1;

RegisterBankInfo::OperandsMapper::OperandsMapper(
    MachineInstr &MI, const InstructionMapping &InstrMapping,
    MachineRegisterInfo &MRI)
    : MRI(MRI), MI(MI), InstrMapping(InstrMapping) {
  unsigned NumOpds = InstrMapping.getNumOperands();
  OpToNewVRegIdx.resize(NumOpds, OperandsMapper::DontKnowIdx);
  assert(InstrMapping.verify(MI) && "Invalid mapping for MI");
}

iterator_range<SmallVectorImpl<unsigned>::iterator>
RegisterBankInfo::OperandsMapper::getVRegsMem(unsigned OpIdx) {
  assert(OpIdx < getInstrMapping().getNumOperands() && "Out-of-bound access");
  unsigned NumPartialVal =
      getInstrMapping().getOperandMapping(OpIdx).NumBreakDowns;
  int StartIdx = OpToNewVRegIdx[OpIdx];

  if (StartIdx == OperandsMapper::DontKnowIdx) {
    // This is the first time we try to access OpIdx.
    // Create the cells that will hold all the partial values at the
    // end of the list of NewVReg.
    StartIdx = NewVRegs.size();
    OpToNewVRegIdx[OpIdx] = StartIdx;
    for (unsigned i = 0; i < NumPartialVal; ++i)
      NewVRegs.push_back(0);
  }
  SmallVectorImpl<unsigned>::iterator End =
      getNewVRegsEnd(StartIdx, NumPartialVal);

  return make_range(&NewVRegs[StartIdx], End);
}

SmallVectorImpl<unsigned>::const_iterator
RegisterBankInfo::OperandsMapper::getNewVRegsEnd(unsigned StartIdx,
                                                 unsigned NumVal) const {
  return const_cast<OperandsMapper *>(this)->getNewVRegsEnd(StartIdx, NumVal);
}
SmallVectorImpl<unsigned>::iterator
RegisterBankInfo::OperandsMapper::getNewVRegsEnd(unsigned StartIdx,
                                                 unsigned NumVal) {
  assert((NewVRegs.size() == StartIdx + NumVal ||
          NewVRegs.size() > StartIdx + NumVal) &&
         "NewVRegs too small to contain all the partial mapping");
  return NewVRegs.size() <= StartIdx + NumVal ? NewVRegs.end()
                                              : &NewVRegs[StartIdx + NumVal];
}

void RegisterBankInfo::OperandsMapper::createVRegs(unsigned OpIdx) {
  assert(OpIdx < getInstrMapping().getNumOperands() && "Out-of-bound access");
  iterator_range<SmallVectorImpl<unsigned>::iterator> NewVRegsForOpIdx =
      getVRegsMem(OpIdx);
  const ValueMapping &ValMapping = getInstrMapping().getOperandMapping(OpIdx);
  const PartialMapping *PartMap = ValMapping.begin();
  for (unsigned &NewVReg : NewVRegsForOpIdx) {
    assert(PartMap != ValMapping.end() && "Out-of-bound access");
    assert(NewVReg == 0 && "Register has already been created");
    // The new registers are always bound to scalar with the right size.
    // The actual type has to be set when the target does the mapping
    // of the instruction.
    // The rationale is that this generic code cannot guess how the
    // target plans to split the input type.
    NewVReg = MRI.createGenericVirtualRegister(LLT::scalar(PartMap->Length));
    MRI.setRegBank(NewVReg, *PartMap->RegBank);
    ++PartMap;
  }
}

void RegisterBankInfo::OperandsMapper::setVRegs(unsigned OpIdx,
                                                unsigned PartialMapIdx,
                                                unsigned NewVReg) {
  assert(OpIdx < getInstrMapping().getNumOperands() && "Out-of-bound access");
  assert(getInstrMapping().getOperandMapping(OpIdx).NumBreakDowns >
             PartialMapIdx &&
         "Out-of-bound access for partial mapping");
  // Make sure the memory is initialized for that operand.
  (void)getVRegsMem(OpIdx);
  assert(NewVRegs[OpToNewVRegIdx[OpIdx] + PartialMapIdx] == 0 &&
         "This value is already set");
  NewVRegs[OpToNewVRegIdx[OpIdx] + PartialMapIdx] = NewVReg;
}

iterator_range<SmallVectorImpl<unsigned>::const_iterator>
RegisterBankInfo::OperandsMapper::getVRegs(unsigned OpIdx,
                                           bool ForDebug) const {
  (void)ForDebug;
  assert(OpIdx < getInstrMapping().getNumOperands() && "Out-of-bound access");
  int StartIdx = OpToNewVRegIdx[OpIdx];

  if (StartIdx == OperandsMapper::DontKnowIdx)
    return make_range(NewVRegs.end(), NewVRegs.end());

  unsigned PartMapSize =
      getInstrMapping().getOperandMapping(OpIdx).NumBreakDowns;
  SmallVectorImpl<unsigned>::const_iterator End =
      getNewVRegsEnd(StartIdx, PartMapSize);
  iterator_range<SmallVectorImpl<unsigned>::const_iterator> Res =
      make_range(&NewVRegs[StartIdx], End);
#ifndef NDEBUG
  for (unsigned VReg : Res)
    assert((VReg || ForDebug) && "Some registers are uninitialized");
#endif
  return Res;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegisterBankInfo::OperandsMapper::dump() const {
  print(dbgs(), true);
  dbgs() << '\n';
}
#endif

void RegisterBankInfo::OperandsMapper::print(raw_ostream &OS,
                                             bool ForDebug) const {
  unsigned NumOpds = getInstrMapping().getNumOperands();
  if (ForDebug) {
    OS << "Mapping for " << getMI() << "\nwith " << getInstrMapping() << '\n';
    // Print out the internal state of the index table.
    OS << "Populated indices (CellNumber, IndexInNewVRegs): ";
    bool IsFirst = true;
    for (unsigned Idx = 0; Idx != NumOpds; ++Idx) {
      if (OpToNewVRegIdx[Idx] != DontKnowIdx) {
        if (!IsFirst)
          OS << ", ";
        OS << '(' << Idx << ", " << OpToNewVRegIdx[Idx] << ')';
        IsFirst = false;
      }
    }
    OS << '\n';
  } else
    OS << "Mapping ID: " << getInstrMapping().getID() << ' ';

  OS << "Operand Mapping: ";
  // If we have a function, we can pretty print the name of the registers.
  // Otherwise we will print the raw numbers.
  const TargetRegisterInfo *TRI =
      getMI().getParent() && getMI().getMF()
          ? getMI().getMF()->getSubtarget().getRegisterInfo()
          : nullptr;
  bool IsFirst = true;
  for (unsigned Idx = 0; Idx != NumOpds; ++Idx) {
    if (OpToNewVRegIdx[Idx] == DontKnowIdx)
      continue;
    if (!IsFirst)
      OS << ", ";
    IsFirst = false;
    OS << '(' << printReg(getMI().getOperand(Idx).getReg(), TRI) << ", [";
    bool IsFirstNewVReg = true;
    for (unsigned VReg : getVRegs(Idx)) {
      if (!IsFirstNewVReg)
        OS << ", ";
      IsFirstNewVReg = false;
      OS << printReg(VReg, TRI);
    }
    OS << "])";
  }
}
