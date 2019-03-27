//===--------------------- Instruction.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines abstractions used by the Pipeline to model register reads,
/// register writes and instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_INSTRUCTION_H
#define LLVM_MCA_INSTRUCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#ifndef NDEBUG
#include "llvm/Support/raw_ostream.h"
#endif

#include <memory>

namespace llvm {

namespace mca {

constexpr int UNKNOWN_CYCLES = -512;

/// A register write descriptor.
struct WriteDescriptor {
  // Operand index. The index is negative for implicit writes only.
  // For implicit writes, the actual operand index is computed performing
  // a bitwise not of the OpIndex.
  int OpIndex;
  // Write latency. Number of cycles before write-back stage.
  unsigned Latency;
  // This field is set to a value different than zero only if this
  // is an implicit definition.
  unsigned RegisterID;
  // Instruction itineraries would set this field to the SchedClass ID.
  // Otherwise, it defaults to the WriteResourceID from the MCWriteLatencyEntry
  // element associated to this write.
  // When computing read latencies, this value is matched against the
  // "ReadAdvance" information. The hardware backend may implement
  // dedicated forwarding paths to quickly propagate write results to dependent
  // instructions waiting in the reservation station (effectively bypassing the
  // write-back stage).
  unsigned SClassOrWriteResourceID;
  // True only if this is a write obtained from an optional definition.
  // Optional definitions are allowed to reference regID zero (i.e. "no
  // register").
  bool IsOptionalDef;

  bool isImplicitWrite() const { return OpIndex < 0; };
};

/// A register read descriptor.
struct ReadDescriptor {
  // A MCOperand index. This is used by the Dispatch logic to identify register
  // reads. Implicit reads have negative indices. The actual operand index of an
  // implicit read is the bitwise not of field OpIndex.
  int OpIndex;
  // The actual "UseIdx". This is used to query the ReadAdvance table. Explicit
  // uses always come first in the sequence of uses.
  unsigned UseIndex;
  // This field is only set if this is an implicit read.
  unsigned RegisterID;
  // Scheduling Class Index. It is used to query the scheduling model for the
  // MCSchedClassDesc object.
  unsigned SchedClassID;

  bool isImplicitRead() const { return OpIndex < 0; };
};

class ReadState;

/// Tracks uses of a register definition (e.g. register write).
///
/// Each implicit/explicit register write is associated with an instance of
/// this class. A WriteState object tracks the dependent users of a
/// register write. It also tracks how many cycles are left before the write
/// back stage.
class WriteState {
  const WriteDescriptor *WD;
  // On instruction issue, this field is set equal to the write latency.
  // Before instruction issue, this field defaults to -512, a special
  // value that represents an "unknown" number of cycles.
  int CyclesLeft;

  // Actual register defined by this write. This field is only used
  // to speedup queries on the register file.
  // For implicit writes, this field always matches the value of
  // field RegisterID from WD.
  unsigned RegisterID;

  // Physical register file that serves register RegisterID.
  unsigned PRFID;

  // True if this write implicitly clears the upper portion of RegisterID's
  // super-registers.
  bool ClearsSuperRegs;

  // True if this write is from a dependency breaking zero-idiom instruction.
  bool WritesZero;

  // True if this write has been eliminated at register renaming stage.
  // Example: a register move doesn't consume scheduler/pipleline resources if
  // it is eliminated at register renaming stage. It still consumes
  // decode bandwidth, and ROB entries.
  bool IsEliminated;

  // This field is set if this is a partial register write, and it has a false
  // dependency on any previous write of the same register (or a portion of it).
  // DependentWrite must be able to complete before this write completes, so
  // that we don't break the WAW, and the two writes can be merged together.
  const WriteState *DependentWrite;

  // A partial write that is in a false dependency with this write.
  WriteState *PartialWrite;

  unsigned DependentWriteCyclesLeft;

  // A list of dependent reads. Users is a set of dependent
  // reads. A dependent read is added to the set only if CyclesLeft
  // is "unknown". As soon as CyclesLeft is 'known', each user in the set
  // gets notified with the actual CyclesLeft.

  // The 'second' element of a pair is a "ReadAdvance" number of cycles.
  SmallVector<std::pair<ReadState *, int>, 4> Users;

public:
  WriteState(const WriteDescriptor &Desc, unsigned RegID,
             bool clearsSuperRegs = false, bool writesZero = false)
      : WD(&Desc), CyclesLeft(UNKNOWN_CYCLES), RegisterID(RegID), PRFID(0),
        ClearsSuperRegs(clearsSuperRegs), WritesZero(writesZero),
        IsEliminated(false), DependentWrite(nullptr), PartialWrite(nullptr),
        DependentWriteCyclesLeft(0) {}

  WriteState(const WriteState &Other) = default;
  WriteState &operator=(const WriteState &Other) = default;

  int getCyclesLeft() const { return CyclesLeft; }
  unsigned getWriteResourceID() const { return WD->SClassOrWriteResourceID; }
  unsigned getRegisterID() const { return RegisterID; }
  unsigned getRegisterFileID() const { return PRFID; }
  unsigned getLatency() const { return WD->Latency; }

  void addUser(ReadState *Use, int ReadAdvance);
  void addUser(WriteState *Use);

  unsigned getDependentWriteCyclesLeft() const {
    return DependentWriteCyclesLeft;
  }

  unsigned getNumUsers() const {
    unsigned NumUsers = Users.size();
    if (PartialWrite)
      ++NumUsers;
    return NumUsers;
  }

  bool clearsSuperRegisters() const { return ClearsSuperRegs; }
  bool isWriteZero() const { return WritesZero; }
  bool isEliminated() const { return IsEliminated; }
  bool isExecuted() const {
    return CyclesLeft != UNKNOWN_CYCLES && CyclesLeft <= 0;
  }

  const WriteState *getDependentWrite() const { return DependentWrite; }
  void setDependentWrite(WriteState *Other) { DependentWrite = Other; }
  void writeStartEvent(unsigned Cycles) {
    DependentWriteCyclesLeft = Cycles;
    DependentWrite = nullptr;
  }

  void setWriteZero() { WritesZero = true; }
  void setEliminated() {
    assert(Users.empty() && "Write is in an inconsistent state.");
    CyclesLeft = 0;
    IsEliminated = true;
  }

  void setPRF(unsigned PRF) { PRFID = PRF; }

  // On every cycle, update CyclesLeft and notify dependent users.
  void cycleEvent();
  void onInstructionIssued();

#ifndef NDEBUG
  void dump() const;
#endif
};

/// Tracks register operand latency in cycles.
///
/// A read may be dependent on more than one write. This occurs when some
/// writes only partially update the register associated to this read.
class ReadState {
  const ReadDescriptor *RD;
  // Physical register identified associated to this read.
  unsigned RegisterID;
  // Physical register file that serves register RegisterID.
  unsigned PRFID;
  // Number of writes that contribute to the definition of RegisterID.
  // In the absence of partial register updates, the number of DependentWrites
  // cannot be more than one.
  unsigned DependentWrites;
  // Number of cycles left before RegisterID can be read. This value depends on
  // the latency of all the dependent writes. It defaults to UNKNOWN_CYCLES.
  // It gets set to the value of field TotalCycles only when the 'CyclesLeft' of
  // every dependent write is known.
  int CyclesLeft;
  // This field is updated on every writeStartEvent(). When the number of
  // dependent writes (i.e. field DependentWrite) is zero, this value is
  // propagated to field CyclesLeft.
  unsigned TotalCycles;
  // This field is set to true only if there are no dependent writes, and
  // there are no `CyclesLeft' to wait.
  bool IsReady;
  // True if this is a read from a known zero register.
  bool IsZero;
  // True if this register read is from a dependency-breaking instruction.
  bool IndependentFromDef;

public:
  ReadState(const ReadDescriptor &Desc, unsigned RegID)
      : RD(&Desc), RegisterID(RegID), PRFID(0), DependentWrites(0),
        CyclesLeft(UNKNOWN_CYCLES), TotalCycles(0), IsReady(true),
        IsZero(false), IndependentFromDef(false) {}

  const ReadDescriptor &getDescriptor() const { return *RD; }
  unsigned getSchedClass() const { return RD->SchedClassID; }
  unsigned getRegisterID() const { return RegisterID; }
  unsigned getRegisterFileID() const { return PRFID; }

  bool isReady() const { return IsReady; }
  bool isImplicitRead() const { return RD->isImplicitRead(); }

  bool isIndependentFromDef() const { return IndependentFromDef; }
  void setIndependentFromDef() { IndependentFromDef = true; }

  void cycleEvent();
  void writeStartEvent(unsigned Cycles);
  void setDependentWrites(unsigned Writes) {
    DependentWrites = Writes;
    IsReady = !Writes;
  }

  bool isReadZero() const { return IsZero; }
  void setReadZero() { IsZero = true; }
  void setPRF(unsigned ID) { PRFID = ID; }
};

/// A sequence of cycles.
///
/// This class can be used as a building block to construct ranges of cycles.
class CycleSegment {
  unsigned Begin; // Inclusive.
  unsigned End;   // Exclusive.
  bool Reserved;  // Resources associated to this segment must be reserved.

public:
  CycleSegment(unsigned StartCycle, unsigned EndCycle, bool IsReserved = false)
      : Begin(StartCycle), End(EndCycle), Reserved(IsReserved) {}

  bool contains(unsigned Cycle) const { return Cycle >= Begin && Cycle < End; }
  bool startsAfter(const CycleSegment &CS) const { return End <= CS.Begin; }
  bool endsBefore(const CycleSegment &CS) const { return Begin >= CS.End; }
  bool overlaps(const CycleSegment &CS) const {
    return !startsAfter(CS) && !endsBefore(CS);
  }
  bool isExecuting() const { return Begin == 0 && End != 0; }
  bool isExecuted() const { return End == 0; }
  bool operator<(const CycleSegment &Other) const {
    return Begin < Other.Begin;
  }
  CycleSegment &operator--(void) {
    if (Begin)
      Begin--;
    if (End)
      End--;
    return *this;
  }

  bool isValid() const { return Begin <= End; }
  unsigned size() const { return End - Begin; };
  void subtract(unsigned Cycles) {
    assert(End >= Cycles);
    End -= Cycles;
  }

  unsigned begin() const { return Begin; }
  unsigned end() const { return End; }
  void setEnd(unsigned NewEnd) { End = NewEnd; }
  bool isReserved() const { return Reserved; }
  void setReserved() { Reserved = true; }
};

/// Helper used by class InstrDesc to describe how hardware resources
/// are used.
///
/// This class describes how many resource units of a specific resource kind
/// (and how many cycles) are "used" by an instruction.
struct ResourceUsage {
  CycleSegment CS;
  unsigned NumUnits;
  ResourceUsage(CycleSegment Cycles, unsigned Units = 1)
      : CS(Cycles), NumUnits(Units) {}
  unsigned size() const { return CS.size(); }
  bool isReserved() const { return CS.isReserved(); }
  void setReserved() { CS.setReserved(); }
};

/// An instruction descriptor
struct InstrDesc {
  SmallVector<WriteDescriptor, 4> Writes; // Implicit writes are at the end.
  SmallVector<ReadDescriptor, 4> Reads;   // Implicit reads are at the end.

  // For every resource used by an instruction of this kind, this vector
  // reports the number of "consumed cycles".
  SmallVector<std::pair<uint64_t, ResourceUsage>, 4> Resources;

  // A list of buffered resources consumed by this instruction.
  SmallVector<uint64_t, 4> Buffers;

  unsigned MaxLatency;
  // Number of MicroOps for this instruction.
  unsigned NumMicroOps;

  bool MayLoad;
  bool MayStore;
  bool HasSideEffects;
  bool BeginGroup;
  bool EndGroup;

  // True if all buffered resources are in-order, and there is at least one
  // buffer which is a dispatch hazard (BufferSize = 0).
  bool MustIssueImmediately;

  // A zero latency instruction doesn't consume any scheduler resources.
  bool isZeroLatency() const { return !MaxLatency && Resources.empty(); }

  InstrDesc() = default;
  InstrDesc(const InstrDesc &Other) = delete;
  InstrDesc &operator=(const InstrDesc &Other) = delete;
};

/// Base class for instructions consumed by the simulation pipeline.
///
/// This class tracks data dependencies as well as generic properties
/// of the instruction.
class InstructionBase {
  const InstrDesc &Desc;

  // This field is set for instructions that are candidates for move
  // elimination. For more information about move elimination, see the
  // definition of RegisterMappingTracker in RegisterFile.h
  bool IsOptimizableMove;

  // Output dependencies.
  // One entry per each implicit and explicit register definition.
  SmallVector<WriteState, 4> Defs;

  // Input dependencies.
  // One entry per each implicit and explicit register use.
  SmallVector<ReadState, 4> Uses;

public:
  InstructionBase(const InstrDesc &D) : Desc(D), IsOptimizableMove(false) {}

  SmallVectorImpl<WriteState> &getDefs() { return Defs; }
  const ArrayRef<WriteState> getDefs() const { return Defs; }
  SmallVectorImpl<ReadState> &getUses() { return Uses; }
  const ArrayRef<ReadState> getUses() const { return Uses; }
  const InstrDesc &getDesc() const { return Desc; }

  unsigned getLatency() const { return Desc.MaxLatency; }

  bool hasDependentUsers() const {
    return any_of(Defs,
                  [](const WriteState &Def) { return Def.getNumUsers() > 0; });
  }

  unsigned getNumUsers() const {
    unsigned NumUsers = 0;
    for (const WriteState &Def : Defs)
      NumUsers += Def.getNumUsers();
    return NumUsers;
  }

  // Returns true if this instruction is a candidate for move elimination.
  bool isOptimizableMove() const { return IsOptimizableMove; }
  void setOptimizableMove() { IsOptimizableMove = true; }
};

/// An instruction propagated through the simulated instruction pipeline.
///
/// This class is used to monitor changes to the internal state of instructions
/// that are sent to the various components of the simulated hardware pipeline.
class Instruction : public InstructionBase {
  enum InstrStage {
    IS_INVALID,   // Instruction in an invalid state.
    IS_AVAILABLE, // Instruction dispatched but operands are not ready.
    IS_READY,     // Instruction dispatched and operands ready.
    IS_EXECUTING, // Instruction issued.
    IS_EXECUTED,  // Instruction executed. Values are written back.
    IS_RETIRED    // Instruction retired.
  };

  // The current instruction stage.
  enum InstrStage Stage;

  // This value defaults to the instruction latency. This instruction is
  // considered executed when field CyclesLeft goes to zero.
  int CyclesLeft;

  // Retire Unit token ID for this instruction.
  unsigned RCUTokenID;

public:
  Instruction(const InstrDesc &D)
      : InstructionBase(D), Stage(IS_INVALID), CyclesLeft(UNKNOWN_CYCLES),
        RCUTokenID(0) {}

  unsigned getRCUTokenID() const { return RCUTokenID; }
  int getCyclesLeft() const { return CyclesLeft; }

  // Transition to the dispatch stage, and assign a RCUToken to this
  // instruction. The RCUToken is used to track the completion of every
  // register write performed by this instruction.
  void dispatch(unsigned RCUTokenID);

  // Instruction issued. Transition to the IS_EXECUTING state, and update
  // all the definitions.
  void execute();

  // Force a transition from the IS_AVAILABLE state to the IS_READY state if
  // input operands are all ready. State transitions normally occur at the
  // beginning of a new cycle (see method cycleEvent()). However, the scheduler
  // may decide to promote instructions from the wait queue to the ready queue
  // as the result of another issue event.  This method is called every time the
  // instruction might have changed in state.
  void update();

  bool isDispatched() const { return Stage == IS_AVAILABLE; }
  bool isReady() const { return Stage == IS_READY; }
  bool isExecuting() const { return Stage == IS_EXECUTING; }
  bool isExecuted() const { return Stage == IS_EXECUTED; }
  bool isRetired() const { return Stage == IS_RETIRED; }

  bool isEliminated() const {
    return isReady() && getDefs().size() &&
           all_of(getDefs(),
                  [](const WriteState &W) { return W.isEliminated(); });
  }

  // Forces a transition from state IS_AVAILABLE to state IS_EXECUTED.
  void forceExecuted();

  void retire() {
    assert(isExecuted() && "Instruction is in an invalid state!");
    Stage = IS_RETIRED;
  }

  void cycleEvent();
};

/// An InstRef contains both a SourceMgr index and Instruction pair.  The index
/// is used as a unique identifier for the instruction.  MCA will make use of
/// this index as a key throughout MCA.
class InstRef {
  std::pair<unsigned, Instruction *> Data;

public:
  InstRef() : Data(std::make_pair(0, nullptr)) {}
  InstRef(unsigned Index, Instruction *I) : Data(std::make_pair(Index, I)) {}

  bool operator==(const InstRef &Other) const { return Data == Other.Data; }

  unsigned getSourceIndex() const { return Data.first; }
  Instruction *getInstruction() { return Data.second; }
  const Instruction *getInstruction() const { return Data.second; }

  /// Returns true if this references a valid instruction.
  operator bool() const { return Data.second != nullptr; }

  /// Invalidate this reference.
  void invalidate() { Data.second = nullptr; }

#ifndef NDEBUG
  void print(raw_ostream &OS) const { OS << getSourceIndex(); }
#endif
};

#ifndef NDEBUG
inline raw_ostream &operator<<(raw_ostream &OS, const InstRef &IR) {
  IR.print(OS);
  return OS;
}
#endif

/// A reference to a register write.
///
/// This class is mainly used by the register file to describe register
/// mappings. It correlates a register write to the source index of the
/// defining instruction.
class WriteRef {
  std::pair<unsigned, WriteState *> Data;
  static const unsigned INVALID_IID;

public:
  WriteRef() : Data(INVALID_IID, nullptr) {}
  WriteRef(unsigned SourceIndex, WriteState *WS) : Data(SourceIndex, WS) {}

  unsigned getSourceIndex() const { return Data.first; }
  const WriteState *getWriteState() const { return Data.second; }
  WriteState *getWriteState() { return Data.second; }
  void invalidate() { Data.second = nullptr; }
  bool isWriteZero() const {
    assert(isValid() && "Invalid null WriteState found!");
    return getWriteState()->isWriteZero();
  }

  /// Returns true if this register write has been executed, and the new
  /// register value is therefore available to users.
  bool isAvailable() const {
    if (getSourceIndex() == INVALID_IID)
      return false;
    const WriteState *WS = getWriteState();
    return !WS || WS->isExecuted();
  }

  bool isValid() const { return Data.first != INVALID_IID && Data.second; }
  bool operator==(const WriteRef &Other) const { return Data == Other.Data; }

#ifndef NDEBUG
  void dump() const;
#endif
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_INSTRUCTION_H
