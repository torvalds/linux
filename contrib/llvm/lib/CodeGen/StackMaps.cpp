//===- StackMaps.cpp ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/StackMaps.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "stackmaps"

static cl::opt<int> StackMapVersion(
    "stackmap-version", cl::init(3), cl::Hidden,
    cl::desc("Specify the stackmap encoding version (default = 3)"));

const char *StackMaps::WSMP = "Stack Maps: ";

StackMapOpers::StackMapOpers(const MachineInstr *MI)
  : MI(MI) {
  assert(getVarIdx() <= MI->getNumOperands() &&
         "invalid stackmap definition");
}

PatchPointOpers::PatchPointOpers(const MachineInstr *MI)
    : MI(MI), HasDef(MI->getOperand(0).isReg() && MI->getOperand(0).isDef() &&
                     !MI->getOperand(0).isImplicit()) {
#ifndef NDEBUG
  unsigned CheckStartIdx = 0, e = MI->getNumOperands();
  while (CheckStartIdx < e && MI->getOperand(CheckStartIdx).isReg() &&
         MI->getOperand(CheckStartIdx).isDef() &&
         !MI->getOperand(CheckStartIdx).isImplicit())
    ++CheckStartIdx;

  assert(getMetaIdx() == CheckStartIdx &&
         "Unexpected additional definition in Patchpoint intrinsic.");
#endif
}

unsigned PatchPointOpers::getNextScratchIdx(unsigned StartIdx) const {
  if (!StartIdx)
    StartIdx = getVarIdx();

  // Find the next scratch register (implicit def and early clobber)
  unsigned ScratchIdx = StartIdx, e = MI->getNumOperands();
  while (ScratchIdx < e &&
         !(MI->getOperand(ScratchIdx).isReg() &&
           MI->getOperand(ScratchIdx).isDef() &&
           MI->getOperand(ScratchIdx).isImplicit() &&
           MI->getOperand(ScratchIdx).isEarlyClobber()))
    ++ScratchIdx;

  assert(ScratchIdx != e && "No scratch register available");
  return ScratchIdx;
}

StackMaps::StackMaps(AsmPrinter &AP) : AP(AP) {
  if (StackMapVersion != 3)
    llvm_unreachable("Unsupported stackmap version!");
}

/// Go up the super-register chain until we hit a valid dwarf register number.
static unsigned getDwarfRegNum(unsigned Reg, const TargetRegisterInfo *TRI) {
  int RegNum = TRI->getDwarfRegNum(Reg, false);
  for (MCSuperRegIterator SR(Reg, TRI); SR.isValid() && RegNum < 0; ++SR)
    RegNum = TRI->getDwarfRegNum(*SR, false);

  assert(RegNum >= 0 && "Invalid Dwarf register number.");
  return (unsigned)RegNum;
}

MachineInstr::const_mop_iterator
StackMaps::parseOperand(MachineInstr::const_mop_iterator MOI,
                        MachineInstr::const_mop_iterator MOE, LocationVec &Locs,
                        LiveOutVec &LiveOuts) const {
  const TargetRegisterInfo *TRI = AP.MF->getSubtarget().getRegisterInfo();
  if (MOI->isImm()) {
    switch (MOI->getImm()) {
    default:
      llvm_unreachable("Unrecognized operand type.");
    case StackMaps::DirectMemRefOp: {
      auto &DL = AP.MF->getDataLayout();

      unsigned Size = DL.getPointerSizeInBits();
      assert((Size % 8) == 0 && "Need pointer size in bytes.");
      Size /= 8;
      unsigned Reg = (++MOI)->getReg();
      int64_t Imm = (++MOI)->getImm();
      Locs.emplace_back(StackMaps::Location::Direct, Size,
                        getDwarfRegNum(Reg, TRI), Imm);
      break;
    }
    case StackMaps::IndirectMemRefOp: {
      int64_t Size = (++MOI)->getImm();
      assert(Size > 0 && "Need a valid size for indirect memory locations.");
      unsigned Reg = (++MOI)->getReg();
      int64_t Imm = (++MOI)->getImm();
      Locs.emplace_back(StackMaps::Location::Indirect, Size,
                        getDwarfRegNum(Reg, TRI), Imm);
      break;
    }
    case StackMaps::ConstantOp: {
      ++MOI;
      assert(MOI->isImm() && "Expected constant operand.");
      int64_t Imm = MOI->getImm();
      Locs.emplace_back(Location::Constant, sizeof(int64_t), 0, Imm);
      break;
    }
    }
    return ++MOI;
  }

  // The physical register number will ultimately be encoded as a DWARF regno.
  // The stack map also records the size of a spill slot that can hold the
  // register content. (The runtime can track the actual size of the data type
  // if it needs to.)
  if (MOI->isReg()) {
    // Skip implicit registers (this includes our scratch registers)
    if (MOI->isImplicit())
      return ++MOI;

    assert(TargetRegisterInfo::isPhysicalRegister(MOI->getReg()) &&
           "Virtreg operands should have been rewritten before now.");
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(MOI->getReg());
    assert(!MOI->getSubReg() && "Physical subreg still around.");

    unsigned Offset = 0;
    unsigned DwarfRegNum = getDwarfRegNum(MOI->getReg(), TRI);
    unsigned LLVMRegNum = TRI->getLLVMRegNum(DwarfRegNum, false);
    unsigned SubRegIdx = TRI->getSubRegIndex(LLVMRegNum, MOI->getReg());
    if (SubRegIdx)
      Offset = TRI->getSubRegIdxOffset(SubRegIdx);

    Locs.emplace_back(Location::Register, TRI->getSpillSize(*RC),
                      DwarfRegNum, Offset);
    return ++MOI;
  }

  if (MOI->isRegLiveOut())
    LiveOuts = parseRegisterLiveOutMask(MOI->getRegLiveOut());

  return ++MOI;
}

void StackMaps::print(raw_ostream &OS) {
  const TargetRegisterInfo *TRI =
      AP.MF ? AP.MF->getSubtarget().getRegisterInfo() : nullptr;
  OS << WSMP << "callsites:\n";
  for (const auto &CSI : CSInfos) {
    const LocationVec &CSLocs = CSI.Locations;
    const LiveOutVec &LiveOuts = CSI.LiveOuts;

    OS << WSMP << "callsite " << CSI.ID << "\n";
    OS << WSMP << "  has " << CSLocs.size() << " locations\n";

    unsigned Idx = 0;
    for (const auto &Loc : CSLocs) {
      OS << WSMP << "\t\tLoc " << Idx << ": ";
      switch (Loc.Type) {
      case Location::Unprocessed:
        OS << "<Unprocessed operand>";
        break;
      case Location::Register:
        OS << "Register ";
        if (TRI)
          OS << printReg(Loc.Reg, TRI);
        else
          OS << Loc.Reg;
        break;
      case Location::Direct:
        OS << "Direct ";
        if (TRI)
          OS << printReg(Loc.Reg, TRI);
        else
          OS << Loc.Reg;
        if (Loc.Offset)
          OS << " + " << Loc.Offset;
        break;
      case Location::Indirect:
        OS << "Indirect ";
        if (TRI)
          OS << printReg(Loc.Reg, TRI);
        else
          OS << Loc.Reg;
        OS << "+" << Loc.Offset;
        break;
      case Location::Constant:
        OS << "Constant " << Loc.Offset;
        break;
      case Location::ConstantIndex:
        OS << "Constant Index " << Loc.Offset;
        break;
      }
      OS << "\t[encoding: .byte " << Loc.Type << ", .byte 0"
         << ", .short " << Loc.Size << ", .short " << Loc.Reg << ", .short 0"
         << ", .int " << Loc.Offset << "]\n";
      Idx++;
    }

    OS << WSMP << "\thas " << LiveOuts.size() << " live-out registers\n";

    Idx = 0;
    for (const auto &LO : LiveOuts) {
      OS << WSMP << "\t\tLO " << Idx << ": ";
      if (TRI)
        OS << printReg(LO.Reg, TRI);
      else
        OS << LO.Reg;
      OS << "\t[encoding: .short " << LO.DwarfRegNum << ", .byte 0, .byte "
         << LO.Size << "]\n";
      Idx++;
    }
  }
}

/// Create a live-out register record for the given register Reg.
StackMaps::LiveOutReg
StackMaps::createLiveOutReg(unsigned Reg, const TargetRegisterInfo *TRI) const {
  unsigned DwarfRegNum = getDwarfRegNum(Reg, TRI);
  unsigned Size = TRI->getSpillSize(*TRI->getMinimalPhysRegClass(Reg));
  return LiveOutReg(Reg, DwarfRegNum, Size);
}

/// Parse the register live-out mask and return a vector of live-out registers
/// that need to be recorded in the stackmap.
StackMaps::LiveOutVec
StackMaps::parseRegisterLiveOutMask(const uint32_t *Mask) const {
  assert(Mask && "No register mask specified");
  const TargetRegisterInfo *TRI = AP.MF->getSubtarget().getRegisterInfo();
  LiveOutVec LiveOuts;

  // Create a LiveOutReg for each bit that is set in the register mask.
  for (unsigned Reg = 0, NumRegs = TRI->getNumRegs(); Reg != NumRegs; ++Reg)
    if ((Mask[Reg / 32] >> Reg % 32) & 1)
      LiveOuts.push_back(createLiveOutReg(Reg, TRI));

  // We don't need to keep track of a register if its super-register is already
  // in the list. Merge entries that refer to the same dwarf register and use
  // the maximum size that needs to be spilled.

  llvm::sort(LiveOuts, [](const LiveOutReg &LHS, const LiveOutReg &RHS) {
    // Only sort by the dwarf register number.
    return LHS.DwarfRegNum < RHS.DwarfRegNum;
  });

  for (auto I = LiveOuts.begin(), E = LiveOuts.end(); I != E; ++I) {
    for (auto II = std::next(I); II != E; ++II) {
      if (I->DwarfRegNum != II->DwarfRegNum) {
        // Skip all the now invalid entries.
        I = --II;
        break;
      }
      I->Size = std::max(I->Size, II->Size);
      if (TRI->isSuperRegister(I->Reg, II->Reg))
        I->Reg = II->Reg;
      II->Reg = 0; // mark for deletion.
    }
  }

  LiveOuts.erase(
      llvm::remove_if(LiveOuts,
                      [](const LiveOutReg &LO) { return LO.Reg == 0; }),
      LiveOuts.end());

  return LiveOuts;
}

void StackMaps::recordStackMapOpers(const MachineInstr &MI, uint64_t ID,
                                    MachineInstr::const_mop_iterator MOI,
                                    MachineInstr::const_mop_iterator MOE,
                                    bool recordResult) {
  MCContext &OutContext = AP.OutStreamer->getContext();
  MCSymbol *MILabel = OutContext.createTempSymbol();
  AP.OutStreamer->EmitLabel(MILabel);

  LocationVec Locations;
  LiveOutVec LiveOuts;

  if (recordResult) {
    assert(PatchPointOpers(&MI).hasDef() && "Stackmap has no return value.");
    parseOperand(MI.operands_begin(), std::next(MI.operands_begin()), Locations,
                 LiveOuts);
  }

  // Parse operands.
  while (MOI != MOE) {
    MOI = parseOperand(MOI, MOE, Locations, LiveOuts);
  }

  // Move large constants into the constant pool.
  for (auto &Loc : Locations) {
    // Constants are encoded as sign-extended integers.
    // -1 is directly encoded as .long 0xFFFFFFFF with no constant pool.
    if (Loc.Type == Location::Constant && !isInt<32>(Loc.Offset)) {
      Loc.Type = Location::ConstantIndex;
      // ConstPool is intentionally a MapVector of 'uint64_t's (as
      // opposed to 'int64_t's).  We should never be in a situation
      // where we have to insert either the tombstone or the empty
      // keys into a map, and for a DenseMap<uint64_t, T> these are
      // (uint64_t)0 and (uint64_t)-1.  They can be and are
      // represented using 32 bit integers.
      assert((uint64_t)Loc.Offset != DenseMapInfo<uint64_t>::getEmptyKey() &&
             (uint64_t)Loc.Offset !=
                 DenseMapInfo<uint64_t>::getTombstoneKey() &&
             "empty and tombstone keys should fit in 32 bits!");
      auto Result = ConstPool.insert(std::make_pair(Loc.Offset, Loc.Offset));
      Loc.Offset = Result.first - ConstPool.begin();
    }
  }

  // Create an expression to calculate the offset of the callsite from function
  // entry.
  const MCExpr *CSOffsetExpr = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(MILabel, OutContext),
      MCSymbolRefExpr::create(AP.CurrentFnSymForSize, OutContext), OutContext);

  CSInfos.emplace_back(CSOffsetExpr, ID, std::move(Locations),
                       std::move(LiveOuts));

  // Record the stack size of the current function and update callsite count.
  const MachineFrameInfo &MFI = AP.MF->getFrameInfo();
  const TargetRegisterInfo *RegInfo = AP.MF->getSubtarget().getRegisterInfo();
  bool HasDynamicFrameSize =
      MFI.hasVarSizedObjects() || RegInfo->needsStackRealignment(*(AP.MF));
  uint64_t FrameSize = HasDynamicFrameSize ? UINT64_MAX : MFI.getStackSize();

  auto CurrentIt = FnInfos.find(AP.CurrentFnSym);
  if (CurrentIt != FnInfos.end())
    CurrentIt->second.RecordCount++;
  else
    FnInfos.insert(std::make_pair(AP.CurrentFnSym, FunctionInfo(FrameSize)));
}

void StackMaps::recordStackMap(const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::STACKMAP && "expected stackmap");

  StackMapOpers opers(&MI);
  const int64_t ID = MI.getOperand(PatchPointOpers::IDPos).getImm();
  recordStackMapOpers(MI, ID, std::next(MI.operands_begin(), opers.getVarIdx()),
                      MI.operands_end());
}

void StackMaps::recordPatchPoint(const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::PATCHPOINT && "expected patchpoint");

  PatchPointOpers opers(&MI);
  const int64_t ID = opers.getID();
  auto MOI = std::next(MI.operands_begin(), opers.getStackMapStartIdx());
  recordStackMapOpers(MI, ID, MOI, MI.operands_end(),
                      opers.isAnyReg() && opers.hasDef());

#ifndef NDEBUG
  // verify anyregcc
  auto &Locations = CSInfos.back().Locations;
  if (opers.isAnyReg()) {
    unsigned NArgs = opers.getNumCallArgs();
    for (unsigned i = 0, e = (opers.hasDef() ? NArgs + 1 : NArgs); i != e; ++i)
      assert(Locations[i].Type == Location::Register &&
             "anyreg arg must be in reg.");
  }
#endif
}

void StackMaps::recordStatepoint(const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::STATEPOINT && "expected statepoint");

  StatepointOpers opers(&MI);
  // Record all the deopt and gc operands (they're contiguous and run from the
  // initial index to the end of the operand list)
  const unsigned StartIdx = opers.getVarIdx();
  recordStackMapOpers(MI, opers.getID(), MI.operands_begin() + StartIdx,
                      MI.operands_end(), false);
}

/// Emit the stackmap header.
///
/// Header {
///   uint8  : Stack Map Version (currently 2)
///   uint8  : Reserved (expected to be 0)
///   uint16 : Reserved (expected to be 0)
/// }
/// uint32 : NumFunctions
/// uint32 : NumConstants
/// uint32 : NumRecords
void StackMaps::emitStackmapHeader(MCStreamer &OS) {
  // Header.
  OS.EmitIntValue(StackMapVersion, 1); // Version.
  OS.EmitIntValue(0, 1);               // Reserved.
  OS.EmitIntValue(0, 2);               // Reserved.

  // Num functions.
  LLVM_DEBUG(dbgs() << WSMP << "#functions = " << FnInfos.size() << '\n');
  OS.EmitIntValue(FnInfos.size(), 4);
  // Num constants.
  LLVM_DEBUG(dbgs() << WSMP << "#constants = " << ConstPool.size() << '\n');
  OS.EmitIntValue(ConstPool.size(), 4);
  // Num callsites.
  LLVM_DEBUG(dbgs() << WSMP << "#callsites = " << CSInfos.size() << '\n');
  OS.EmitIntValue(CSInfos.size(), 4);
}

/// Emit the function frame record for each function.
///
/// StkSizeRecord[NumFunctions] {
///   uint64 : Function Address
///   uint64 : Stack Size
///   uint64 : Record Count
/// }
void StackMaps::emitFunctionFrameRecords(MCStreamer &OS) {
  // Function Frame records.
  LLVM_DEBUG(dbgs() << WSMP << "functions:\n");
  for (auto const &FR : FnInfos) {
    LLVM_DEBUG(dbgs() << WSMP << "function addr: " << FR.first
                      << " frame size: " << FR.second.StackSize
                      << " callsite count: " << FR.second.RecordCount << '\n');
    OS.EmitSymbolValue(FR.first, 8);
    OS.EmitIntValue(FR.second.StackSize, 8);
    OS.EmitIntValue(FR.second.RecordCount, 8);
  }
}

/// Emit the constant pool.
///
/// int64  : Constants[NumConstants]
void StackMaps::emitConstantPoolEntries(MCStreamer &OS) {
  // Constant pool entries.
  LLVM_DEBUG(dbgs() << WSMP << "constants:\n");
  for (const auto &ConstEntry : ConstPool) {
    LLVM_DEBUG(dbgs() << WSMP << ConstEntry.second << '\n');
    OS.EmitIntValue(ConstEntry.second, 8);
  }
}

/// Emit the callsite info for each callsite.
///
/// StkMapRecord[NumRecords] {
///   uint64 : PatchPoint ID
///   uint32 : Instruction Offset
///   uint16 : Reserved (record flags)
///   uint16 : NumLocations
///   Location[NumLocations] {
///     uint8  : Register | Direct | Indirect | Constant | ConstantIndex
///     uint8  : Size in Bytes
///     uint16 : Dwarf RegNum
///     int32  : Offset
///   }
///   uint16 : Padding
///   uint16 : NumLiveOuts
///   LiveOuts[NumLiveOuts] {
///     uint16 : Dwarf RegNum
///     uint8  : Reserved
///     uint8  : Size in Bytes
///   }
///   uint32 : Padding (only if required to align to 8 byte)
/// }
///
/// Location Encoding, Type, Value:
///   0x1, Register, Reg                 (value in register)
///   0x2, Direct, Reg + Offset          (frame index)
///   0x3, Indirect, [Reg + Offset]      (spilled value)
///   0x4, Constant, Offset              (small constant)
///   0x5, ConstIndex, Constants[Offset] (large constant)
void StackMaps::emitCallsiteEntries(MCStreamer &OS) {
  LLVM_DEBUG(print(dbgs()));
  // Callsite entries.
  for (const auto &CSI : CSInfos) {
    const LocationVec &CSLocs = CSI.Locations;
    const LiveOutVec &LiveOuts = CSI.LiveOuts;

    // Verify stack map entry. It's better to communicate a problem to the
    // runtime than crash in case of in-process compilation. Currently, we do
    // simple overflow checks, but we may eventually communicate other
    // compilation errors this way.
    if (CSLocs.size() > UINT16_MAX || LiveOuts.size() > UINT16_MAX) {
      OS.EmitIntValue(UINT64_MAX, 8); // Invalid ID.
      OS.EmitValue(CSI.CSOffsetExpr, 4);
      OS.EmitIntValue(0, 2); // Reserved.
      OS.EmitIntValue(0, 2); // 0 locations.
      OS.EmitIntValue(0, 2); // padding.
      OS.EmitIntValue(0, 2); // 0 live-out registers.
      OS.EmitIntValue(0, 4); // padding.
      continue;
    }

    OS.EmitIntValue(CSI.ID, 8);
    OS.EmitValue(CSI.CSOffsetExpr, 4);

    // Reserved for flags.
    OS.EmitIntValue(0, 2);
    OS.EmitIntValue(CSLocs.size(), 2);

    for (const auto &Loc : CSLocs) {
      OS.EmitIntValue(Loc.Type, 1);
      OS.EmitIntValue(0, 1);  // Reserved
      OS.EmitIntValue(Loc.Size, 2);
      OS.EmitIntValue(Loc.Reg, 2);
      OS.EmitIntValue(0, 2);  // Reserved
      OS.EmitIntValue(Loc.Offset, 4);
    }

    // Emit alignment to 8 byte.
    OS.EmitValueToAlignment(8);

    // Num live-out registers and padding to align to 4 byte.
    OS.EmitIntValue(0, 2);
    OS.EmitIntValue(LiveOuts.size(), 2);

    for (const auto &LO : LiveOuts) {
      OS.EmitIntValue(LO.DwarfRegNum, 2);
      OS.EmitIntValue(0, 1);
      OS.EmitIntValue(LO.Size, 1);
    }
    // Emit alignment to 8 byte.
    OS.EmitValueToAlignment(8);
  }
}

/// Serialize the stackmap data.
void StackMaps::serializeToStackMapSection() {
  (void)WSMP;
  // Bail out if there's no stack map data.
  assert((!CSInfos.empty() || ConstPool.empty()) &&
         "Expected empty constant pool too!");
  assert((!CSInfos.empty() || FnInfos.empty()) &&
         "Expected empty function record too!");
  if (CSInfos.empty())
    return;

  MCContext &OutContext = AP.OutStreamer->getContext();
  MCStreamer &OS = *AP.OutStreamer;

  // Create the section.
  MCSection *StackMapSection =
      OutContext.getObjectFileInfo()->getStackMapSection();
  OS.SwitchSection(StackMapSection);

  // Emit a dummy symbol to force section inclusion.
  OS.EmitLabel(OutContext.getOrCreateSymbol(Twine("__LLVM_StackMaps")));

  // Serialize data.
  LLVM_DEBUG(dbgs() << "********** Stack Map Output **********\n");
  emitStackmapHeader(OS);
  emitFunctionFrameRecords(OS);
  emitConstantPoolEntries(OS);
  emitCallsiteEntries(OS);
  OS.AddBlankLine();

  // Clean up.
  CSInfos.clear();
  ConstPool.clear();
}
