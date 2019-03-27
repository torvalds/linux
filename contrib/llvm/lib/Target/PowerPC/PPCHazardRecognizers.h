//===-- PPCHazardRecognizers.h - PowerPC Hazard Recognizers -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines hazard recognizers for scheduling on PowerPC processors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCHAZARDRECOGNIZERS_H
#define LLVM_LIB_TARGET_POWERPC_PPCHAZARDRECOGNIZERS_H

#include "PPCInstrInfo.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"

namespace llvm {

/// PPCDispatchGroupSBHazardRecognizer - This class implements a scoreboard-based
/// hazard recognizer for PPC ooo processors with dispatch-group hazards.
class PPCDispatchGroupSBHazardRecognizer : public ScoreboardHazardRecognizer {
  const ScheduleDAG *DAG;
  SmallVector<SUnit *, 7> CurGroup;
  unsigned CurSlots, CurBranches;

  bool isLoadAfterStore(SUnit *SU);
  bool isBCTRAfterSet(SUnit *SU);
  bool mustComeFirst(const MCInstrDesc *MCID, unsigned &NSlots);
public:
  PPCDispatchGroupSBHazardRecognizer(const InstrItineraryData *ItinData,
                         const ScheduleDAG *DAG_) :
    ScoreboardHazardRecognizer(ItinData, DAG_), DAG(DAG_),
    CurSlots(0), CurBranches(0) {}

  HazardType getHazardType(SUnit *SU, int Stalls) override;
  bool ShouldPreferAnother(SUnit* SU) override;
  unsigned PreEmitNoops(SUnit *SU) override;
  void EmitInstruction(SUnit *SU) override;
  void AdvanceCycle() override;
  void RecedeCycle() override;
  void Reset() override;
  void EmitNoop() override;
};

/// PPCHazardRecognizer970 - This class defines a finite state automata that
/// models the dispatch logic on the PowerPC 970 (aka G5) processor.  This
/// promotes good dispatch group formation and implements noop insertion to
/// avoid structural hazards that cause significant performance penalties (e.g.
/// setting the CTR register then branching through it within a dispatch group),
/// or storing then loading from the same address within a dispatch group.
class PPCHazardRecognizer970 : public ScheduleHazardRecognizer {
  const ScheduleDAG &DAG;

  unsigned NumIssued;  // Number of insts issued, including advanced cycles.

  // Various things that can cause a structural hazard.

  // HasCTRSet - If the CTR register is set in this group, disallow BCTRL.
  bool HasCTRSet;

  // StoredPtr - Keep track of the address of any store.  If we see a load from
  // the same address (or one that aliases it), disallow the store.  We can have
  // up to four stores in one dispatch group, hence we track up to 4.
  //
  // This is null if we haven't seen a store yet.  We keep track of both
  // operands of the store here, since we support [r+r] and [r+i] addressing.
  const Value *StoreValue[4];
  int64_t StoreOffset[4];
  uint64_t StoreSize[4];
  unsigned NumStores;

public:
  PPCHazardRecognizer970(const ScheduleDAG &DAG);
  HazardType getHazardType(SUnit *SU, int Stalls) override;
  void EmitInstruction(SUnit *SU) override;
  void AdvanceCycle() override;
  void Reset() override;

private:
  /// EndDispatchGroup - Called when we are finishing a new dispatch group.
  ///
  void EndDispatchGroup();

  /// GetInstrType - Classify the specified powerpc opcode according to its
  /// pipeline.
  PPCII::PPC970_Unit GetInstrType(unsigned Opcode,
                                  bool &isFirst, bool &isSingle,bool &isCracked,
                                  bool &isLoad, bool &isStore);

  bool isLoadOfStoredAddress(uint64_t LoadSize, int64_t LoadOffset,
                             const Value *LoadValue) const;
};

} // end namespace llvm

#endif

