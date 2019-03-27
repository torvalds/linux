//===----- HexagonMCShuffler.cpp - MC bundle shuffling --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the shuffling of insns inside a bundle according to the
// packet formation rules of the Hexagon ISA.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "hexagon-shuffle"

#include "MCTargetDesc/HexagonMCShuffler.h"
#include "Hexagon.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonShuffler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

static cl::opt<bool>
    DisableShuffle("disable-hexagon-shuffle", cl::Hidden, cl::init(false),
                   cl::desc("Disable Hexagon instruction shuffling"));

void HexagonMCShuffler::init(MCInst &MCB) {
  if (HexagonMCInstrInfo::isBundle(MCB)) {
    MCInst const *Extender = nullptr;
    // Copy the bundle for the shuffling.
    for (const auto &I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
      MCInst &MI = *const_cast<MCInst *>(I.getInst());
      LLVM_DEBUG(dbgs() << "Shuffling: " << MCII.getName(MI.getOpcode())
                        << '\n');
      assert(!HexagonMCInstrInfo::getDesc(MCII, MI).isPseudo());

      if (!HexagonMCInstrInfo::isImmext(MI)) {
        append(MI, Extender, HexagonMCInstrInfo::getUnits(MCII, STI, MI));
        Extender = nullptr;
      } else
        Extender = &MI;
    }
  }

  Loc = MCB.getLoc();
  BundleFlags = MCB.getOperand(0).getImm();
}

void HexagonMCShuffler::init(MCInst &MCB, MCInst const &AddMI,
                             bool bInsertAtFront) {
  if (HexagonMCInstrInfo::isBundle(MCB)) {
    if (bInsertAtFront)
      append(AddMI, nullptr, HexagonMCInstrInfo::getUnits(MCII, STI, AddMI));
    MCInst const *Extender = nullptr;
    // Copy the bundle for the shuffling.
    for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
      assert(!HexagonMCInstrInfo::getDesc(MCII, *I.getInst()).isPseudo());
      MCInst &MI = *const_cast<MCInst *>(I.getInst());
      if (!HexagonMCInstrInfo::isImmext(MI)) {
        append(MI, Extender, HexagonMCInstrInfo::getUnits(MCII, STI, MI));
        Extender = nullptr;
      } else
        Extender = &MI;
    }
    if (!bInsertAtFront)
      append(AddMI, nullptr, HexagonMCInstrInfo::getUnits(MCII, STI, AddMI));
  }

  Loc = MCB.getLoc();
  BundleFlags = MCB.getOperand(0).getImm();
}

void HexagonMCShuffler::copyTo(MCInst &MCB) {
  MCB.clear();
  MCB.addOperand(MCOperand::createImm(BundleFlags));
  MCB.setLoc(Loc);
  // Copy the results into the bundle.
  for (HexagonShuffler::iterator I = begin(); I != end(); ++I) {

    MCInst const &MI = I->getDesc();
    MCInst const *Extender = I->getExtender();
    if (Extender)
      MCB.addOperand(MCOperand::createInst(Extender));
    MCB.addOperand(MCOperand::createInst(&MI));
  }
}

bool HexagonMCShuffler::reshuffleTo(MCInst &MCB) {
  if (shuffle()) {
    // Copy the results into the bundle.
    copyTo(MCB);
    return true;
  }
  LLVM_DEBUG(MCB.dump());
  return false;
}

bool llvm::HexagonMCShuffle(MCContext &Context, bool Fatal,
                            MCInstrInfo const &MCII, MCSubtargetInfo const &STI,
                            MCInst &MCB) {
  HexagonMCShuffler MCS(Context, Fatal, MCII, STI, MCB);

  if (DisableShuffle)
    // Ignore if user chose so.
    return false;

  if (!HexagonMCInstrInfo::bundleSize(MCB)) {
    // There once was a bundle:
    //    BUNDLE implicit-def %d2, implicit-def %r4, implicit-def %r5,
    //    implicit-def %d7, ...
    //      * %d2 = IMPLICIT_DEF; flags:
    //      * %d7 = IMPLICIT_DEF; flags:
    // After the IMPLICIT_DEFs were removed by the asm printer, the bundle
    // became empty.
    LLVM_DEBUG(dbgs() << "Skipping empty bundle");
    return false;
  } else if (!HexagonMCInstrInfo::isBundle(MCB)) {
    LLVM_DEBUG(dbgs() << "Skipping stand-alone insn");
    return false;
  }

  return MCS.reshuffleTo(MCB);
}

bool
llvm::HexagonMCShuffle(MCContext &Context, MCInstrInfo const &MCII,
                       MCSubtargetInfo const &STI, MCInst &MCB,
                       SmallVector<DuplexCandidate, 8> possibleDuplexes) {
  if (DisableShuffle)
    return false;

  if (!HexagonMCInstrInfo::bundleSize(MCB)) {
    // There once was a bundle:
    //    BUNDLE implicit-def %d2, implicit-def %r4, implicit-def %r5,
    //    implicit-def %d7, ...
    //      * %d2 = IMPLICIT_DEF; flags:
    //      * %d7 = IMPLICIT_DEF; flags:
    // After the IMPLICIT_DEFs were removed by the asm printer, the bundle
    // became empty.
    LLVM_DEBUG(dbgs() << "Skipping empty bundle");
    return false;
  } else if (!HexagonMCInstrInfo::isBundle(MCB)) {
    LLVM_DEBUG(dbgs() << "Skipping stand-alone insn");
    return false;
  }

  bool doneShuffling = false;
  while (possibleDuplexes.size() > 0 && (!doneShuffling)) {
    // case of Duplex Found
    DuplexCandidate duplexToTry = possibleDuplexes.pop_back_val();
    MCInst Attempt(MCB);
    HexagonMCInstrInfo::replaceDuplex(Context, Attempt, duplexToTry);
    HexagonMCShuffler MCS(Context, false, MCII, STI, Attempt); // copy packet to the shuffler
    if (MCS.size() == 1) {                     // case of one duplex
      // copy the created duplex in the shuffler to the bundle
      MCS.copyTo(MCB);
      return false;
    }
    // try shuffle with this duplex
    doneShuffling = MCS.reshuffleTo(MCB);

    if (doneShuffling)
      break;
  }

  if (!doneShuffling) {
    HexagonMCShuffler MCS(Context, false, MCII, STI, MCB);
    doneShuffling = MCS.reshuffleTo(MCB); // shuffle
  }
  if (!doneShuffling)
    return true;

  return false;
}

bool llvm::HexagonMCShuffle(MCContext &Context, MCInstrInfo const &MCII,
                            MCSubtargetInfo const &STI, MCInst &MCB,
                            MCInst const &AddMI, int fixupCount) {
  if (!HexagonMCInstrInfo::isBundle(MCB))
    return false;

  // if fixups present, make sure we don't insert too many nops that would
  // later prevent an extender from being inserted.
  unsigned int bundleSize = HexagonMCInstrInfo::bundleSize(MCB);
  if (bundleSize >= HEXAGON_PACKET_SIZE)
    return false;
  bool bhasDuplex = HexagonMCInstrInfo::hasDuplex(MCII, MCB);
  if (fixupCount >= 2) {
    if (bhasDuplex) {
      if (bundleSize >= HEXAGON_PACKET_SIZE - 1) {
        return false;
      }
    } else {
      return false;
    }
  } else {
    if (bundleSize == HEXAGON_PACKET_SIZE - 1 && fixupCount)
      return false;
  }

  if (DisableShuffle)
    return false;

  // mgl: temporary code (shuffler doesn't take into account the fact that
  // a duplex takes up two slots.  for example, 3 nops can be put into a packet
  // containing a duplex oversubscribing slots by 1).
  unsigned maxBundleSize = (HexagonMCInstrInfo::hasImmExt(MCB))
                               ? HEXAGON_PACKET_SIZE
                               : HEXAGON_PACKET_SIZE - 1;
  if (bhasDuplex && bundleSize >= maxBundleSize)
    return false;

  HexagonMCShuffler MCS(Context, false, MCII, STI, MCB, AddMI, false);
  return MCS.reshuffleTo(MCB);
}
