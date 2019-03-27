//=- llvm/CodeGen/ScheduleHazardRecognizer.h - Scheduling Support -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScheduleHazardRecognizer class, which implements
// hazard-avoidance heuristics for scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_SCHEDULEHAZARDRECOGNIZER_H

namespace llvm {

class MachineInstr;
class SUnit;

/// HazardRecognizer - This determines whether or not an instruction can be
/// issued this cycle, and whether or not a noop needs to be inserted to handle
/// the hazard.
class ScheduleHazardRecognizer {
protected:
  /// MaxLookAhead - Indicate the number of cycles in the scoreboard
  /// state. Important to restore the state after backtracking. Additionally,
  /// MaxLookAhead=0 identifies a fake recognizer, allowing the client to
  /// bypass virtual calls. Currently the PostRA scheduler ignores it.
  unsigned MaxLookAhead = 0;

public:
  ScheduleHazardRecognizer() = default;
  virtual ~ScheduleHazardRecognizer();

  enum HazardType {
    NoHazard,      // This instruction can be emitted at this cycle.
    Hazard,        // This instruction can't be emitted at this cycle.
    NoopHazard     // This instruction can't be emitted, and needs noops.
  };

  unsigned getMaxLookAhead() const { return MaxLookAhead; }

  bool isEnabled() const { return MaxLookAhead != 0; }

  /// atIssueLimit - Return true if no more instructions may be issued in this
  /// cycle.
  ///
  /// FIXME: remove this once MachineScheduler is the only client.
  virtual bool atIssueLimit() const { return false; }

  /// getHazardType - Return the hazard type of emitting this node.  There are
  /// three possible results.  Either:
  ///  * NoHazard: it is legal to issue this instruction on this cycle.
  ///  * Hazard: issuing this instruction would stall the machine.  If some
  ///     other instruction is available, issue it first.
  ///  * NoopHazard: issuing this instruction would break the program.  If
  ///     some other instruction can be issued, do so, otherwise issue a noop.
  virtual HazardType getHazardType(SUnit *m, int Stalls = 0) {
    return NoHazard;
  }

  /// Reset - This callback is invoked when a new block of
  /// instructions is about to be schedule. The hazard state should be
  /// set to an initialized state.
  virtual void Reset() {}

  /// EmitInstruction - This callback is invoked when an instruction is
  /// emitted, to advance the hazard state.
  virtual void EmitInstruction(SUnit *) {}

  /// This overload will be used when the hazard recognizer is being used
  /// by a non-scheduling pass, which does not use SUnits.
  virtual void EmitInstruction(MachineInstr *) {}

  /// PreEmitNoops - This callback is invoked prior to emitting an instruction.
  /// It should return the number of noops to emit prior to the provided
  /// instruction.
  /// Note: This is only used during PostRA scheduling. EmitNoop is not called
  /// for these noops.
  virtual unsigned PreEmitNoops(SUnit *) {
    return 0;
  }

  /// This overload will be used when the hazard recognizer is being used
  /// by a non-scheduling pass, which does not use SUnits.
  virtual unsigned PreEmitNoops(MachineInstr *) {
    return 0;
  }

  /// ShouldPreferAnother - This callback may be invoked if getHazardType
  /// returns NoHazard. If, even though there is no hazard, it would be better to
  /// schedule another available instruction, this callback should return true.
  virtual bool ShouldPreferAnother(SUnit *) {
    return false;
  }

  /// AdvanceCycle - This callback is invoked whenever the next top-down
  /// instruction to be scheduled cannot issue in the current cycle, either
  /// because of latency or resource conflicts.  This should increment the
  /// internal state of the hazard recognizer so that previously "Hazard"
  /// instructions will now not be hazards.
  virtual void AdvanceCycle() {}

  /// RecedeCycle - This callback is invoked whenever the next bottom-up
  /// instruction to be scheduled cannot issue in the current cycle, either
  /// because of latency or resource conflicts.
  virtual void RecedeCycle() {}

  /// EmitNoop - This callback is invoked when a noop was added to the
  /// instruction stream.
  virtual void EmitNoop() {
    // Default implementation: count it as a cycle.
    AdvanceCycle();
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEHAZARDRECOGNIZER_H
