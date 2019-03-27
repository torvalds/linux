//===- HexagonMCShuffler.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This declares the shuffling of insns inside a bundle according to the
// packet formation rules of the Hexagon ISA.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCSHUFFLER_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCSHUFFLER_H

#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonShuffler.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCSubtargetInfo;

// Insn bundle shuffler.
class HexagonMCShuffler : public HexagonShuffler {
public:
  HexagonMCShuffler(MCContext &Context, bool Fatal, MCInstrInfo const &MCII,
                    MCSubtargetInfo const &STI, MCInst &MCB)
      : HexagonShuffler(Context, Fatal, MCII, STI) {
    init(MCB);
  }

  HexagonMCShuffler(MCContext &Context, bool Fatal, MCInstrInfo const &MCII,
                    MCSubtargetInfo const &STI, MCInst &MCB,
                    MCInst const &AddMI, bool InsertAtFront)
      : HexagonShuffler(Context, Fatal, MCII, STI) {
    init(MCB, AddMI, InsertAtFront);
  }

  // Copy reordered bundle to another.
  void copyTo(MCInst &MCB);

  // Reorder and copy result to another.
  bool reshuffleTo(MCInst &MCB);

private:
  void init(MCInst &MCB);
  void init(MCInst &MCB, MCInst const &AddMI, bool InsertAtFront);
};

// Invocation of the shuffler.
bool HexagonMCShuffle(MCContext &Context, bool Fatal, MCInstrInfo const &MCII,
                      MCSubtargetInfo const &STI, MCInst &MCB);
bool HexagonMCShuffle(MCContext &Context, MCInstrInfo const &MCII,
                      MCSubtargetInfo const &STI, MCInst &MCB,
                      MCInst const &AddMI, int fixupCount);
bool HexagonMCShuffle(MCContext &Context, MCInstrInfo const &MCII,
                      MCSubtargetInfo const &STI, MCInst &MCB,
                      SmallVector<DuplexCandidate, 8> possibleDuplexes);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCSHUFFLER_H
