//===- HexagonShuffler.h - Instruction bundle shuffling ---------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONSHUFFLER_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONSHUFFLER_H

#include "Hexagon.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <utility>

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCSubtargetInfo;

// Insn resources.
class HexagonResource {
  // Mask of the slots or units that may execute the insn and
  // the weight or priority that the insn requires to be assigned a slot.
  unsigned Slots, Weight;

public:
  HexagonResource(unsigned s) { setUnits(s); }

  void setUnits(unsigned s) {
    Slots = s & ((1u << HEXAGON_PACKET_SIZE) - 1);
    setWeight(s);
  }

  unsigned setWeight(unsigned s);

  unsigned getUnits() const { return (Slots); }
  unsigned getWeight() const { return (Weight); }

  // Check if the resources are in ascending slot order.
  static bool lessUnits(const HexagonResource &A, const HexagonResource &B) {
    return (countPopulation(A.getUnits()) < countPopulation(B.getUnits()));
  }

  // Check if the resources are in ascending weight order.
  static bool lessWeight(const HexagonResource &A, const HexagonResource &B) {
    return (A.getWeight() < B.getWeight());
  }
};

// HVX insn resources.
class HexagonCVIResource : public HexagonResource {
public:
  using UnitsAndLanes = std::pair<unsigned, unsigned>;
  using TypeUnitsAndLanes = DenseMap<unsigned, UnitsAndLanes>;

private:
  // Available HVX slots.
  enum {
    CVI_NONE = 0,
    CVI_XLANE = 1 << 0,
    CVI_SHIFT = 1 << 1,
    CVI_MPY0 = 1 << 2,
    CVI_MPY1 = 1 << 3,
    CVI_ZW = 1 << 4
  };

  // Count of adjacent slots that the insn requires to be executed.
  unsigned Lanes;
  // Flag whether the insn is a load or a store.
  bool Load, Store;
  // Flag whether the HVX resources are valid.
  bool Valid;

  void setLanes(unsigned l) { Lanes = l; }
  void setLoad(bool f = true) { Load = f; }
  void setStore(bool f = true) { Store = f; }

public:
  HexagonCVIResource(TypeUnitsAndLanes *TUL, MCInstrInfo const &MCII,
                     unsigned s, MCInst const *id);

  static void SetupTUL(TypeUnitsAndLanes *TUL, StringRef CPU);

  bool isValid() const { return Valid; }
  unsigned getLanes() const { return Lanes; }
  bool mayLoad() const { return Load; }
  bool mayStore() const { return Store; }
};

// Handle to an insn used by the shuffling algorithm.
class HexagonInstr {
  friend class HexagonShuffler;

  MCInst const *ID;
  MCInst const *Extender;
  HexagonResource Core;
  HexagonCVIResource CVI;

public:
  HexagonInstr(HexagonCVIResource::TypeUnitsAndLanes *T,
               MCInstrInfo const &MCII, MCInst const *id,
               MCInst const *Extender, unsigned s)
      : ID(id), Extender(Extender), Core(s), CVI(T, MCII, s, id) {}

  MCInst const &getDesc() const { return *ID; }
  MCInst const *getExtender() const { return Extender; }

  // Check if the handles are in ascending order for shuffling purposes.
  bool operator<(const HexagonInstr &B) const {
    return (HexagonResource::lessWeight(B.Core, Core));
  }

  // Check if the handles are in ascending order by core slots.
  static bool lessCore(const HexagonInstr &A, const HexagonInstr &B) {
    return (HexagonResource::lessUnits(A.Core, B.Core));
  }

  // Check if the handles are in ascending order by HVX slots.
  static bool lessCVI(const HexagonInstr &A, const HexagonInstr &B) {
    return (HexagonResource::lessUnits(A.CVI, B.CVI));
  }
};

// Bundle shuffler.
class HexagonShuffler {
  using HexagonPacket =
      SmallVector<HexagonInstr, HEXAGON_PRESHUFFLE_PACKET_SIZE>;

  // Insn handles in a bundle.
  HexagonPacket Packet;
  HexagonPacket PacketSave;

  HexagonCVIResource::TypeUnitsAndLanes TUL;

protected:
  MCContext &Context;
  int64_t BundleFlags;
  MCInstrInfo const &MCII;
  MCSubtargetInfo const &STI;
  SMLoc Loc;
  bool ReportErrors;
  std::vector<std::pair<SMLoc, std::string>> AppliedRestrictions;
  void applySlotRestrictions();
  void restrictSlot1AOK();
  void restrictNoSlot1Store();

public:
  using iterator = HexagonPacket::iterator;

  HexagonShuffler(MCContext &Context, bool ReportErrors,
                  MCInstrInfo const &MCII, MCSubtargetInfo const &STI);

  // Reset to initial state.
  void reset();
  // Check if the bundle may be validly shuffled.
  bool check();
  // Reorder the insn handles in the bundle.
  bool shuffle();

  unsigned size() const { return (Packet.size()); }

  bool isMemReorderDisabled() const {
    return (BundleFlags & HexagonMCInstrInfo::memReorderDisabledMask) != 0;
  }

  iterator begin() { return (Packet.begin()); }
  iterator end() { return (Packet.end()); }

  // Add insn handle to the bundle .
  void append(MCInst const &ID, MCInst const *Extender, unsigned S);

  // Return the error code for the last check or shuffling of the bundle.
  void reportError(Twine const &Msg);
};

} // end namespace llvm

#endif //  LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONSHUFFLER_H
