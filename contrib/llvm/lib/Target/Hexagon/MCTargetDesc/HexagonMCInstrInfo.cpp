//===- HexagonMCInstrInfo.cpp - Hexagon sub-class of MCInst ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class extends MCInstrInfo to allow Hexagon specific MCInstr queries
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "Hexagon.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCChecker.h"
#include "MCTargetDesc/HexagonMCExpr.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <limits>

using namespace llvm;

bool HexagonMCInstrInfo::PredicateInfo::isPredicated() const {
  return Register != Hexagon::NoRegister;
}

Hexagon::PacketIterator::PacketIterator(MCInstrInfo const &MCII,
                                        MCInst const &Inst)
    : MCII(MCII), BundleCurrent(Inst.begin() +
                                HexagonMCInstrInfo::bundleInstructionsOffset),
      BundleEnd(Inst.end()), DuplexCurrent(Inst.end()), DuplexEnd(Inst.end()) {}

Hexagon::PacketIterator::PacketIterator(MCInstrInfo const &MCII,
                                        MCInst const &Inst, std::nullptr_t)
    : MCII(MCII), BundleCurrent(Inst.end()), BundleEnd(Inst.end()),
      DuplexCurrent(Inst.end()), DuplexEnd(Inst.end()) {}

Hexagon::PacketIterator &Hexagon::PacketIterator::operator++() {
  if (DuplexCurrent != DuplexEnd) {
    ++DuplexCurrent;
    if (DuplexCurrent == DuplexEnd) {
      DuplexCurrent = BundleEnd;
      DuplexEnd = BundleEnd;
      ++BundleCurrent;
    }
    return *this;
  }
  ++BundleCurrent;
  if (BundleCurrent != BundleEnd) {
    MCInst const &Inst = *BundleCurrent->getInst();
    if (HexagonMCInstrInfo::isDuplex(MCII, Inst)) {
      DuplexCurrent = Inst.begin();
      DuplexEnd = Inst.end();
    }
  }
  return *this;
}

MCInst const &Hexagon::PacketIterator::operator*() const {
  if (DuplexCurrent != DuplexEnd)
    return *DuplexCurrent->getInst();
  return *BundleCurrent->getInst();
}

bool Hexagon::PacketIterator::operator==(PacketIterator const &Other) const {
  return BundleCurrent == Other.BundleCurrent && BundleEnd == Other.BundleEnd &&
         DuplexCurrent == Other.DuplexCurrent && DuplexEnd == Other.DuplexEnd;
}

void HexagonMCInstrInfo::addConstant(MCInst &MI, uint64_t Value,
                                     MCContext &Context) {
  MI.addOperand(MCOperand::createExpr(MCConstantExpr::create(Value, Context)));
}

void HexagonMCInstrInfo::addConstExtender(MCContext &Context,
                                          MCInstrInfo const &MCII, MCInst &MCB,
                                          MCInst const &MCI) {
  assert(HexagonMCInstrInfo::isBundle(MCB));
  MCOperand const &exOp =
      MCI.getOperand(HexagonMCInstrInfo::getExtendableOp(MCII, MCI));

  // Create the extender.
  MCInst *XMCI =
      new (Context) MCInst(HexagonMCInstrInfo::deriveExtender(MCII, MCI, exOp));
  XMCI->setLoc(MCI.getLoc());

  MCB.addOperand(MCOperand::createInst(XMCI));
}

iterator_range<Hexagon::PacketIterator>
HexagonMCInstrInfo::bundleInstructions(MCInstrInfo const &MCII,
                                       MCInst const &MCI) {
  assert(isBundle(MCI));
  return make_range(Hexagon::PacketIterator(MCII, MCI),
                    Hexagon::PacketIterator(MCII, MCI, nullptr));
}

iterator_range<MCInst::const_iterator>
HexagonMCInstrInfo::bundleInstructions(MCInst const &MCI) {
  assert(isBundle(MCI));
  return make_range(MCI.begin() + bundleInstructionsOffset, MCI.end());
}

size_t HexagonMCInstrInfo::bundleSize(MCInst const &MCI) {
  if (HexagonMCInstrInfo::isBundle(MCI))
    return (MCI.size() - bundleInstructionsOffset);
  else
    return (1);
}

bool HexagonMCInstrInfo::canonicalizePacket(MCInstrInfo const &MCII,
                                            MCSubtargetInfo const &STI,
                                            MCContext &Context, MCInst &MCB,
                                            HexagonMCChecker *Check) {
  // Check the bundle for errors.
  bool CheckOk = Check ? Check->check(false) : true;
  if (!CheckOk)
    return false;
  // Examine the packet and convert pairs of instructions to compound
  // instructions when possible.
  if (!HexagonDisableCompound)
    HexagonMCInstrInfo::tryCompound(MCII, STI, Context, MCB);
  HexagonMCShuffle(Context, false, MCII, STI, MCB);
  // Examine the packet and convert pairs of instructions to duplex
  // instructions when possible.
  MCInst InstBundlePreDuplex = MCInst(MCB);
  if (STI.getFeatureBits() [Hexagon::FeatureDuplex]) {
    SmallVector<DuplexCandidate, 8> possibleDuplexes;
    possibleDuplexes =
        HexagonMCInstrInfo::getDuplexPossibilties(MCII, STI, MCB);
    HexagonMCShuffle(Context, MCII, STI, MCB, possibleDuplexes);
  }
  // Examines packet and pad the packet, if needed, when an
  // end-loop is in the bundle.
  HexagonMCInstrInfo::padEndloop(MCB, Context);
  // If compounding and duplexing didn't reduce the size below
  // 4 or less we have a packet that is too big.
  if (HexagonMCInstrInfo::bundleSize(MCB) > HEXAGON_PACKET_SIZE)
    return false;
  // Check the bundle for errors.
  CheckOk = Check ? Check->check(true) : true;
  if (!CheckOk)
    return false;
  HexagonMCShuffle(Context, true, MCII, STI, MCB);
  return true;
}

MCInst HexagonMCInstrInfo::deriveExtender(MCInstrInfo const &MCII,
                                          MCInst const &Inst,
                                          MCOperand const &MO) {
  assert(HexagonMCInstrInfo::isExtendable(MCII, Inst) ||
         HexagonMCInstrInfo::isExtended(MCII, Inst));

  MCInst XMI;
  XMI.setOpcode(Hexagon::A4_ext);
  if (MO.isImm())
    XMI.addOperand(MCOperand::createImm(MO.getImm() & (~0x3f)));
  else if (MO.isExpr())
    XMI.addOperand(MCOperand::createExpr(MO.getExpr()));
  else
    llvm_unreachable("invalid extendable operand");
  return XMI;
}

MCInst *HexagonMCInstrInfo::deriveDuplex(MCContext &Context, unsigned iClass,
                                         MCInst const &inst0,
                                         MCInst const &inst1) {
  assert((iClass <= 0xf) && "iClass must have range of 0 to 0xf");
  MCInst *duplexInst = new (Context) MCInst;
  duplexInst->setOpcode(Hexagon::DuplexIClass0 + iClass);

  MCInst *SubInst0 = new (Context) MCInst(deriveSubInst(inst0));
  MCInst *SubInst1 = new (Context) MCInst(deriveSubInst(inst1));
  duplexInst->addOperand(MCOperand::createInst(SubInst0));
  duplexInst->addOperand(MCOperand::createInst(SubInst1));
  return duplexInst;
}

MCInst const *HexagonMCInstrInfo::extenderForIndex(MCInst const &MCB,
                                                   size_t Index) {
  assert(Index <= bundleSize(MCB));
  if (Index == 0)
    return nullptr;
  MCInst const *Inst =
      MCB.getOperand(Index + bundleInstructionsOffset - 1).getInst();
  if (isImmext(*Inst))
    return Inst;
  return nullptr;
}

void HexagonMCInstrInfo::extendIfNeeded(MCContext &Context,
                                        MCInstrInfo const &MCII, MCInst &MCB,
                                        MCInst const &MCI) {
  if (isConstExtended(MCII, MCI))
    addConstExtender(Context, MCII, MCB, MCI);
}

unsigned HexagonMCInstrInfo::getMemAccessSize(MCInstrInfo const &MCII,
      MCInst const &MCI) {
  uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  unsigned S = (F >> HexagonII::MemAccessSizePos) & HexagonII::MemAccesSizeMask;
  return HexagonII::getMemAccessSizeInBytes(HexagonII::MemAccessSize(S));
}

unsigned HexagonMCInstrInfo::getAddrMode(MCInstrInfo const &MCII,
                                         MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return static_cast<unsigned>((F >> HexagonII::AddrModePos) &
                               HexagonII::AddrModeMask);
}

MCInstrDesc const &HexagonMCInstrInfo::getDesc(MCInstrInfo const &MCII,
                                               MCInst const &MCI) {
  return MCII.get(MCI.getOpcode());
}

unsigned HexagonMCInstrInfo::getDuplexRegisterNumbering(unsigned Reg) {
  using namespace Hexagon;

  switch (Reg) {
  default:
    llvm_unreachable("unknown duplex register");
  // Rs       Rss
  case R0:
  case D0:
    return 0;
  case R1:
  case D1:
    return 1;
  case R2:
  case D2:
    return 2;
  case R3:
  case D3:
    return 3;
  case R4:
  case D8:
    return 4;
  case R5:
  case D9:
    return 5;
  case R6:
  case D10:
    return 6;
  case R7:
  case D11:
    return 7;
  case R16:
    return 8;
  case R17:
    return 9;
  case R18:
    return 10;
  case R19:
    return 11;
  case R20:
    return 12;
  case R21:
    return 13;
  case R22:
    return 14;
  case R23:
    return 15;
  }
}

MCExpr const &HexagonMCInstrInfo::getExpr(MCExpr const &Expr) {
  const auto &HExpr = cast<HexagonMCExpr>(Expr);
  assert(HExpr.getExpr());
  return *HExpr.getExpr();
}

unsigned short HexagonMCInstrInfo::getExtendableOp(MCInstrInfo const &MCII,
                                                   MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::ExtendableOpPos) & HexagonII::ExtendableOpMask);
}

MCOperand const &
HexagonMCInstrInfo::getExtendableOperand(MCInstrInfo const &MCII,
                                         MCInst const &MCI) {
  unsigned O = HexagonMCInstrInfo::getExtendableOp(MCII, MCI);
  MCOperand const &MO = MCI.getOperand(O);

  assert((HexagonMCInstrInfo::isExtendable(MCII, MCI) ||
          HexagonMCInstrInfo::isExtended(MCII, MCI)) &&
         (MO.isImm() || MO.isExpr()));
  return (MO);
}

unsigned HexagonMCInstrInfo::getExtentAlignment(MCInstrInfo const &MCII,
                                                MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::ExtentAlignPos) & HexagonII::ExtentAlignMask);
}

unsigned HexagonMCInstrInfo::getExtentBits(MCInstrInfo const &MCII,
                                           MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::ExtentBitsPos) & HexagonII::ExtentBitsMask);
}

bool HexagonMCInstrInfo::isExtentSigned(MCInstrInfo const &MCII,
                                        MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (F >> HexagonII::ExtentSignedPos) & HexagonII::ExtentSignedMask;
}

/// Return the maximum value of an extendable operand.
int HexagonMCInstrInfo::getMaxValue(MCInstrInfo const &MCII,
                                    MCInst const &MCI) {
  assert(HexagonMCInstrInfo::isExtendable(MCII, MCI) ||
         HexagonMCInstrInfo::isExtended(MCII, MCI));

  if (HexagonMCInstrInfo::isExtentSigned(MCII, MCI)) // if value is signed
    return (1 << (HexagonMCInstrInfo::getExtentBits(MCII, MCI) - 1)) - 1;
  return (1 << HexagonMCInstrInfo::getExtentBits(MCII, MCI)) - 1;
}

/// Return the minimum value of an extendable operand.
int HexagonMCInstrInfo::getMinValue(MCInstrInfo const &MCII,
                                    MCInst const &MCI) {
  assert(HexagonMCInstrInfo::isExtendable(MCII, MCI) ||
         HexagonMCInstrInfo::isExtended(MCII, MCI));

  if (HexagonMCInstrInfo::isExtentSigned(MCII, MCI)) // if value is signed
    return -(1 << (HexagonMCInstrInfo::getExtentBits(MCII, MCI) - 1));
  return 0;
}

StringRef HexagonMCInstrInfo::getName(MCInstrInfo const &MCII,
                                      MCInst const &MCI) {
  return MCII.getName(MCI.getOpcode());
}

unsigned short HexagonMCInstrInfo::getNewValueOp(MCInstrInfo const &MCII,
                                                 MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::NewValueOpPos) & HexagonII::NewValueOpMask);
}

MCOperand const &HexagonMCInstrInfo::getNewValueOperand(MCInstrInfo const &MCII,
                                                        MCInst const &MCI) {
  if (HexagonMCInstrInfo::hasTmpDst(MCII, MCI)) {
    // VTMP doesn't actually exist in the encodings for these 184
    // 3 instructions so go ahead and create it here.
    static MCOperand MCO = MCOperand::createReg(Hexagon::VTMP);
    return (MCO);
  } else {
    unsigned O = HexagonMCInstrInfo::getNewValueOp(MCII, MCI);
    MCOperand const &MCO = MCI.getOperand(O);

    assert((HexagonMCInstrInfo::isNewValue(MCII, MCI) ||
            HexagonMCInstrInfo::hasNewValue(MCII, MCI)) &&
           MCO.isReg());
    return (MCO);
  }
}

/// Return the new value or the newly produced value.
unsigned short HexagonMCInstrInfo::getNewValueOp2(MCInstrInfo const &MCII,
                                                  MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::NewValueOpPos2) & HexagonII::NewValueOpMask2);
}

MCOperand const &
HexagonMCInstrInfo::getNewValueOperand2(MCInstrInfo const &MCII,
                                        MCInst const &MCI) {
  unsigned O = HexagonMCInstrInfo::getNewValueOp2(MCII, MCI);
  MCOperand const &MCO = MCI.getOperand(O);

  assert((HexagonMCInstrInfo::isNewValue(MCII, MCI) ||
          HexagonMCInstrInfo::hasNewValue2(MCII, MCI)) &&
         MCO.isReg());
  return (MCO);
}

/// Return the Hexagon ISA class for the insn.
unsigned HexagonMCInstrInfo::getType(MCInstrInfo const &MCII,
                                     MCInst const &MCI) {
  const uint64_t F = MCII.get(MCI.getOpcode()).TSFlags;
  return ((F >> HexagonII::TypePos) & HexagonII::TypeMask);
}

/// Return the slots this instruction can execute out of
unsigned HexagonMCInstrInfo::getUnits(MCInstrInfo const &MCII,
                                      MCSubtargetInfo const &STI,
                                      MCInst const &MCI) {
  const InstrItinerary *II = STI.getSchedModel().InstrItineraries;
  int SchedClass = HexagonMCInstrInfo::getDesc(MCII, MCI).getSchedClass();
  return ((II[SchedClass].FirstStage + HexagonStages)->getUnits());
}

/// Return the slots this instruction consumes in addition to
/// the slot(s) it can execute out of

unsigned HexagonMCInstrInfo::getOtherReservedSlots(MCInstrInfo const &MCII,
                                                   MCSubtargetInfo const &STI,
                                                   MCInst const &MCI) {
  const InstrItinerary *II = STI.getSchedModel().InstrItineraries;
  int SchedClass = HexagonMCInstrInfo::getDesc(MCII, MCI).getSchedClass();
  unsigned Slots = 0;

  // FirstStage are slots that this instruction can execute in.
  // FirstStage+1 are slots that are also consumed by this instruction.
  // For example: vmemu can only execute in slot 0 but also consumes slot 1.
  for (unsigned Stage = II[SchedClass].FirstStage + 1;
       Stage < II[SchedClass].LastStage; ++Stage) {
    unsigned Units = (Stage + HexagonStages)->getUnits();
    if (Units > HexagonGetLastSlot())
      break;
    // fyi: getUnits() will return 0x1, 0x2, 0x4 or 0x8
    Slots |= Units;
  }

  // if 0 is returned, then no additional slots are consumed by this inst.
  return Slots;
}

bool HexagonMCInstrInfo::hasDuplex(MCInstrInfo const &MCII, MCInst const &MCI) {
  if (!HexagonMCInstrInfo::isBundle(MCI))
    return false;

  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCI)) {
    if (HexagonMCInstrInfo::isDuplex(MCII, *I.getInst()))
      return true;
  }

  return false;
}

bool HexagonMCInstrInfo::hasExtenderForIndex(MCInst const &MCB, size_t Index) {
  return extenderForIndex(MCB, Index) != nullptr;
}

bool HexagonMCInstrInfo::hasImmExt(MCInst const &MCI) {
  if (!HexagonMCInstrInfo::isBundle(MCI))
    return false;

  for (const auto &I : HexagonMCInstrInfo::bundleInstructions(MCI)) {
    if (isImmext(*I.getInst()))
      return true;
  }

  return false;
}

/// Return whether the insn produces a value.
bool HexagonMCInstrInfo::hasNewValue(MCInstrInfo const &MCII,
                                     MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::hasNewValuePos) & HexagonII::hasNewValueMask);
}

/// Return whether the insn produces a second value.
bool HexagonMCInstrInfo::hasNewValue2(MCInstrInfo const &MCII,
                                      MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::hasNewValuePos2) & HexagonII::hasNewValueMask2);
}

MCInst const &HexagonMCInstrInfo::instruction(MCInst const &MCB, size_t Index) {
  assert(isBundle(MCB));
  assert(Index < HEXAGON_PACKET_SIZE);
  return *MCB.getOperand(bundleInstructionsOffset + Index).getInst();
}

/// Return where the instruction is an accumulator.
bool HexagonMCInstrInfo::isAccumulator(MCInstrInfo const &MCII,
                                       MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::AccumulatorPos) & HexagonII::AccumulatorMask);
}

bool HexagonMCInstrInfo::isBundle(MCInst const &MCI) {
  auto Result = Hexagon::BUNDLE == MCI.getOpcode();
  assert(!Result || (MCI.size() > 0 && MCI.getOperand(0).isImm()));
  return Result;
}

bool HexagonMCInstrInfo::isConstExtended(MCInstrInfo const &MCII,
                                         MCInst const &MCI) {
  if (HexagonMCInstrInfo::isExtended(MCII, MCI))
    return true;
  if (!HexagonMCInstrInfo::isExtendable(MCII, MCI))
    return false;
  MCOperand const &MO = HexagonMCInstrInfo::getExtendableOperand(MCII, MCI);
  if (isa<HexagonMCExpr>(MO.getExpr()) &&
      HexagonMCInstrInfo::mustExtend(*MO.getExpr()))
    return true;
  // Branch insns are handled as necessary by relaxation.
  if ((HexagonMCInstrInfo::getType(MCII, MCI) == HexagonII::TypeJ) ||
      (HexagonMCInstrInfo::getType(MCII, MCI) == HexagonII::TypeCJ &&
       HexagonMCInstrInfo::getDesc(MCII, MCI).isBranch()) ||
      (HexagonMCInstrInfo::getType(MCII, MCI) == HexagonII::TypeNCJ &&
       HexagonMCInstrInfo::getDesc(MCII, MCI).isBranch()))
    return false;
  // Otherwise loop instructions and other CR insts are handled by relaxation
  else if ((HexagonMCInstrInfo::getType(MCII, MCI) == HexagonII::TypeCR) &&
           (MCI.getOpcode() != Hexagon::C4_addipc))
    return false;

  assert(!MO.isImm());
  if (isa<HexagonMCExpr>(MO.getExpr()) &&
      HexagonMCInstrInfo::mustNotExtend(*MO.getExpr()))
    return false;
  int64_t Value;
  if (!MO.getExpr()->evaluateAsAbsolute(Value))
    return true;
  int MinValue = HexagonMCInstrInfo::getMinValue(MCII, MCI);
  int MaxValue = HexagonMCInstrInfo::getMaxValue(MCII, MCI);
  return (MinValue > Value || Value > MaxValue);
}

bool HexagonMCInstrInfo::isCanon(MCInstrInfo const &MCII, MCInst const &MCI) {
  return !HexagonMCInstrInfo::getDesc(MCII, MCI).isPseudo() &&
         !HexagonMCInstrInfo::isPrefix(MCII, MCI);
}

bool HexagonMCInstrInfo::isCofMax1(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::CofMax1Pos) & HexagonII::CofMax1Mask);
}

bool HexagonMCInstrInfo::isCofRelax1(MCInstrInfo const &MCII,
                                     MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::CofRelax1Pos) & HexagonII::CofRelax1Mask);
}

bool HexagonMCInstrInfo::isCofRelax2(MCInstrInfo const &MCII,
                                     MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::CofRelax2Pos) & HexagonII::CofRelax2Mask);
}

bool HexagonMCInstrInfo::isCompound(MCInstrInfo const &MCII,
                                    MCInst const &MCI) {
  return (getType(MCII, MCI) == HexagonII::TypeCJ);
}

bool HexagonMCInstrInfo::isCVINew(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::CVINewPos) & HexagonII::CVINewMask);
}

bool HexagonMCInstrInfo::isDblRegForSubInst(unsigned Reg) {
  return ((Reg >= Hexagon::D0 && Reg <= Hexagon::D3) ||
          (Reg >= Hexagon::D8 && Reg <= Hexagon::D11));
}

bool HexagonMCInstrInfo::isDuplex(MCInstrInfo const &MCII, MCInst const &MCI) {
  return HexagonII::TypeDUPLEX == HexagonMCInstrInfo::getType(MCII, MCI);
}

bool HexagonMCInstrInfo::isExtendable(MCInstrInfo const &MCII,
                                      MCInst const &MCI) {
  uint64_t const F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (F >> HexagonII::ExtendablePos) & HexagonII::ExtendableMask;
}

bool HexagonMCInstrInfo::isExtended(MCInstrInfo const &MCII,
                                    MCInst const &MCI) {
  uint64_t const F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (F >> HexagonII::ExtendedPos) & HexagonII::ExtendedMask;
}

bool HexagonMCInstrInfo::isFloat(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::FPPos) & HexagonII::FPMask);
}

bool HexagonMCInstrInfo::isHVX(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t V = getType(MCII, MCI);
  return HexagonII::TypeCVI_FIRST <= V && V <= HexagonII::TypeCVI_LAST;
}

bool HexagonMCInstrInfo::isImmext(MCInst const &MCI) {
  return MCI.getOpcode() == Hexagon::A4_ext;
}

bool HexagonMCInstrInfo::isInnerLoop(MCInst const &MCI) {
  assert(isBundle(MCI));
  int64_t Flags = MCI.getOperand(0).getImm();
  return (Flags & innerLoopMask) != 0;
}

bool HexagonMCInstrInfo::isIntReg(unsigned Reg) {
  return (Reg >= Hexagon::R0 && Reg <= Hexagon::R31);
}

bool HexagonMCInstrInfo::isIntRegForSubInst(unsigned Reg) {
  return ((Reg >= Hexagon::R0 && Reg <= Hexagon::R7) ||
          (Reg >= Hexagon::R16 && Reg <= Hexagon::R23));
}

/// Return whether the insn expects newly produced value.
bool HexagonMCInstrInfo::isNewValue(MCInstrInfo const &MCII,
                                    MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::NewValuePos) & HexagonII::NewValueMask);
}

/// Return whether the operand is extendable.
bool HexagonMCInstrInfo::isOpExtendable(MCInstrInfo const &MCII,
                                        MCInst const &MCI, unsigned short O) {
  return (O == HexagonMCInstrInfo::getExtendableOp(MCII, MCI));
}

bool HexagonMCInstrInfo::isOuterLoop(MCInst const &MCI) {
  assert(isBundle(MCI));
  int64_t Flags = MCI.getOperand(0).getImm();
  return (Flags & outerLoopMask) != 0;
}

bool HexagonMCInstrInfo::isPredicated(MCInstrInfo const &MCII,
                                      MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::PredicatedPos) & HexagonII::PredicatedMask);
}

bool HexagonMCInstrInfo::isPrefix(MCInstrInfo const &MCII, MCInst const &MCI) {
  return HexagonII::TypeEXTENDER == HexagonMCInstrInfo::getType(MCII, MCI);
}

bool HexagonMCInstrInfo::isPredicateLate(MCInstrInfo const &MCII,
                                         MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (F >> HexagonII::PredicateLatePos & HexagonII::PredicateLateMask);
}

/// Return whether the insn is newly predicated.
bool HexagonMCInstrInfo::isPredicatedNew(MCInstrInfo const &MCII,
                                         MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::PredicatedNewPos) & HexagonII::PredicatedNewMask);
}

bool HexagonMCInstrInfo::isPredicatedTrue(MCInstrInfo const &MCII,
                                          MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (
      !((F >> HexagonII::PredicatedFalsePos) & HexagonII::PredicatedFalseMask));
}

bool HexagonMCInstrInfo::isPredReg(unsigned Reg) {
  return (Reg >= Hexagon::P0 && Reg <= Hexagon::P3_0);
}

/// Return whether the insn can be packaged only with A and X-type insns.
bool HexagonMCInstrInfo::isSoloAX(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::SoloAXPos) & HexagonII::SoloAXMask);
}

/// Return whether the insn can be packaged only with an A-type insn in slot #1.
bool HexagonMCInstrInfo::isRestrictSlot1AOK(MCInstrInfo const &MCII,
                                            MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::RestrictSlot1AOKPos) &
          HexagonII::RestrictSlot1AOKMask);
}

bool HexagonMCInstrInfo::isRestrictNoSlot1Store(MCInstrInfo const &MCII,
                                                MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return ((F >> HexagonII::RestrictNoSlot1StorePos) &
          HexagonII::RestrictNoSlot1StoreMask);
}

/// Return whether the insn is solo, i.e., cannot be in a packet.
bool HexagonMCInstrInfo::isSolo(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t F = MCII.get(MCI.getOpcode()).TSFlags;
  return ((F >> HexagonII::SoloPos) & HexagonII::SoloMask);
}

bool HexagonMCInstrInfo::isMemReorderDisabled(MCInst const &MCI) {
  assert(isBundle(MCI));
  auto Flags = MCI.getOperand(0).getImm();
  return (Flags & memReorderDisabledMask) != 0;
}

bool HexagonMCInstrInfo::isSubInstruction(MCInst const &MCI) {
  switch (MCI.getOpcode()) {
  default:
    return false;
  case Hexagon::SA1_addi:
  case Hexagon::SA1_addrx:
  case Hexagon::SA1_addsp:
  case Hexagon::SA1_and1:
  case Hexagon::SA1_clrf:
  case Hexagon::SA1_clrfnew:
  case Hexagon::SA1_clrt:
  case Hexagon::SA1_clrtnew:
  case Hexagon::SA1_cmpeqi:
  case Hexagon::SA1_combine0i:
  case Hexagon::SA1_combine1i:
  case Hexagon::SA1_combine2i:
  case Hexagon::SA1_combine3i:
  case Hexagon::SA1_combinerz:
  case Hexagon::SA1_combinezr:
  case Hexagon::SA1_dec:
  case Hexagon::SA1_inc:
  case Hexagon::SA1_seti:
  case Hexagon::SA1_setin1:
  case Hexagon::SA1_sxtb:
  case Hexagon::SA1_sxth:
  case Hexagon::SA1_tfr:
  case Hexagon::SA1_zxtb:
  case Hexagon::SA1_zxth:
  case Hexagon::SL1_loadri_io:
  case Hexagon::SL1_loadrub_io:
  case Hexagon::SL2_deallocframe:
  case Hexagon::SL2_jumpr31:
  case Hexagon::SL2_jumpr31_f:
  case Hexagon::SL2_jumpr31_fnew:
  case Hexagon::SL2_jumpr31_t:
  case Hexagon::SL2_jumpr31_tnew:
  case Hexagon::SL2_loadrb_io:
  case Hexagon::SL2_loadrd_sp:
  case Hexagon::SL2_loadrh_io:
  case Hexagon::SL2_loadri_sp:
  case Hexagon::SL2_loadruh_io:
  case Hexagon::SL2_return:
  case Hexagon::SL2_return_f:
  case Hexagon::SL2_return_fnew:
  case Hexagon::SL2_return_t:
  case Hexagon::SL2_return_tnew:
  case Hexagon::SS1_storeb_io:
  case Hexagon::SS1_storew_io:
  case Hexagon::SS2_allocframe:
  case Hexagon::SS2_storebi0:
  case Hexagon::SS2_storebi1:
  case Hexagon::SS2_stored_sp:
  case Hexagon::SS2_storeh_io:
  case Hexagon::SS2_storew_sp:
  case Hexagon::SS2_storewi0:
  case Hexagon::SS2_storewi1:
    return true;
  }
}

bool HexagonMCInstrInfo::isVector(MCInstrInfo const &MCII, MCInst const &MCI) {
  if ((getType(MCII, MCI) <= HexagonII::TypeCVI_LAST) &&
      (getType(MCII, MCI) >= HexagonII::TypeCVI_FIRST))
    return true;
  return false;
}

int64_t HexagonMCInstrInfo::minConstant(MCInst const &MCI, size_t Index) {
  auto Sentinal = static_cast<int64_t>(std::numeric_limits<uint32_t>::max())
                  << 8;
  if (MCI.size() <= Index)
    return Sentinal;
  MCOperand const &MCO = MCI.getOperand(Index);
  if (!MCO.isExpr())
    return Sentinal;
  int64_t Value;
  if (!MCO.getExpr()->evaluateAsAbsolute(Value))
    return Sentinal;
  return Value;
}

void HexagonMCInstrInfo::setMustExtend(MCExpr const &Expr, bool Val) {
  HexagonMCExpr &HExpr = const_cast<HexagonMCExpr &>(cast<HexagonMCExpr>(Expr));
  HExpr.setMustExtend(Val);
}

bool HexagonMCInstrInfo::mustExtend(MCExpr const &Expr) {
  HexagonMCExpr const &HExpr = cast<HexagonMCExpr>(Expr);
  return HExpr.mustExtend();
}
void HexagonMCInstrInfo::setMustNotExtend(MCExpr const &Expr, bool Val) {
  HexagonMCExpr &HExpr = const_cast<HexagonMCExpr &>(cast<HexagonMCExpr>(Expr));
  HExpr.setMustNotExtend(Val);
}
bool HexagonMCInstrInfo::mustNotExtend(MCExpr const &Expr) {
  HexagonMCExpr const &HExpr = cast<HexagonMCExpr>(Expr);
  return HExpr.mustNotExtend();
}
void HexagonMCInstrInfo::setS27_2_reloc(MCExpr const &Expr, bool Val) {
  HexagonMCExpr &HExpr =
      const_cast<HexagonMCExpr &>(*cast<HexagonMCExpr>(&Expr));
  HExpr.setS27_2_reloc(Val);
}
bool HexagonMCInstrInfo::s27_2_reloc(MCExpr const &Expr) {
  HexagonMCExpr const *HExpr = dyn_cast<HexagonMCExpr>(&Expr);
  if (!HExpr)
    return false;
  return HExpr->s27_2_reloc();
}

void HexagonMCInstrInfo::padEndloop(MCInst &MCB, MCContext &Context) {
  MCInst Nop;
  Nop.setOpcode(Hexagon::A2_nop);
  assert(isBundle(MCB));
  while ((HexagonMCInstrInfo::isInnerLoop(MCB) &&
          (HexagonMCInstrInfo::bundleSize(MCB) < HEXAGON_PACKET_INNER_SIZE)) ||
         ((HexagonMCInstrInfo::isOuterLoop(MCB) &&
           (HexagonMCInstrInfo::bundleSize(MCB) < HEXAGON_PACKET_OUTER_SIZE))))
    MCB.addOperand(MCOperand::createInst(new (Context) MCInst(Nop)));
}

HexagonMCInstrInfo::PredicateInfo
HexagonMCInstrInfo::predicateInfo(MCInstrInfo const &MCII, MCInst const &MCI) {
  if (!isPredicated(MCII, MCI))
    return {0, 0, false};
  MCInstrDesc const &Desc = getDesc(MCII, MCI);
  for (auto I = Desc.getNumDefs(), N = Desc.getNumOperands(); I != N; ++I)
    if (Desc.OpInfo[I].RegClass == Hexagon::PredRegsRegClassID)
      return {MCI.getOperand(I).getReg(), I, isPredicatedTrue(MCII, MCI)};
  return {0, 0, false};
}

bool HexagonMCInstrInfo::prefersSlot3(MCInstrInfo const &MCII,
                                      MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (F >> HexagonII::PrefersSlot3Pos) & HexagonII::PrefersSlot3Mask;
}

/// return true if instruction has hasTmpDst attribute.
bool HexagonMCInstrInfo::hasTmpDst(MCInstrInfo const &MCII, MCInst const &MCI) {
  const uint64_t F = HexagonMCInstrInfo::getDesc(MCII, MCI).TSFlags;
  return (F >> HexagonII::HasTmpDstPos) & HexagonII::HasTmpDstMask;
}

void HexagonMCInstrInfo::replaceDuplex(MCContext &Context, MCInst &MCB,
                                       DuplexCandidate Candidate) {
  assert(Candidate.packetIndexI < MCB.size());
  assert(Candidate.packetIndexJ < MCB.size());
  assert(isBundle(MCB));
  MCInst *Duplex =
      deriveDuplex(Context, Candidate.iClass,
                   *MCB.getOperand(Candidate.packetIndexJ).getInst(),
                   *MCB.getOperand(Candidate.packetIndexI).getInst());
  assert(Duplex != nullptr);
  MCB.getOperand(Candidate.packetIndexI).setInst(Duplex);
  MCB.erase(MCB.begin() + Candidate.packetIndexJ);
}

void HexagonMCInstrInfo::setInnerLoop(MCInst &MCI) {
  assert(isBundle(MCI));
  MCOperand &Operand = MCI.getOperand(0);
  Operand.setImm(Operand.getImm() | innerLoopMask);
}

void HexagonMCInstrInfo::setMemReorderDisabled(MCInst &MCI) {
  assert(isBundle(MCI));
  MCOperand &Operand = MCI.getOperand(0);
  Operand.setImm(Operand.getImm() | memReorderDisabledMask);
  assert(isMemReorderDisabled(MCI));
}

void HexagonMCInstrInfo::setOuterLoop(MCInst &MCI) {
  assert(isBundle(MCI));
  MCOperand &Operand = MCI.getOperand(0);
  Operand.setImm(Operand.getImm() | outerLoopMask);
}

unsigned HexagonMCInstrInfo::SubregisterBit(unsigned Consumer,
                                            unsigned Producer,
                                            unsigned Producer2) {
  // If we're a single vector consumer of a double producer, set subreg bit
  // based on if we're accessing the lower or upper register component
  if (Producer >= Hexagon::W0 && Producer <= Hexagon::W15)
    if (Consumer >= Hexagon::V0 && Consumer <= Hexagon::V31)
      return (Consumer - Hexagon::V0) & 0x1;
  if (Producer2 != Hexagon::NoRegister)
    return Consumer == Producer;
  return 0;
}
