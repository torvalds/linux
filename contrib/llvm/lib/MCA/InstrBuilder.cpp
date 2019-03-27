//===--------------------- InstrBuilder.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the InstrBuilder interface.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/InstrBuilder.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

InstrBuilder::InstrBuilder(const llvm::MCSubtargetInfo &sti,
                           const llvm::MCInstrInfo &mcii,
                           const llvm::MCRegisterInfo &mri,
                           const llvm::MCInstrAnalysis *mcia)
    : STI(sti), MCII(mcii), MRI(mri), MCIA(mcia), FirstCallInst(true),
      FirstReturnInst(true) {
  const MCSchedModel &SM = STI.getSchedModel();
  ProcResourceMasks.resize(SM.getNumProcResourceKinds());
  computeProcResourceMasks(STI.getSchedModel(), ProcResourceMasks);
}

static void initializeUsedResources(InstrDesc &ID,
                                    const MCSchedClassDesc &SCDesc,
                                    const MCSubtargetInfo &STI,
                                    ArrayRef<uint64_t> ProcResourceMasks) {
  const MCSchedModel &SM = STI.getSchedModel();

  // Populate resources consumed.
  using ResourcePlusCycles = std::pair<uint64_t, ResourceUsage>;
  std::vector<ResourcePlusCycles> Worklist;

  // Track cycles contributed by resources that are in a "Super" relationship.
  // This is required if we want to correctly match the behavior of method
  // SubtargetEmitter::ExpandProcResource() in Tablegen. When computing the set
  // of "consumed" processor resources and resource cycles, the logic in
  // ExpandProcResource() doesn't update the number of resource cycles
  // contributed by a "Super" resource to a group.
  // We need to take this into account when we find that a processor resource is
  // part of a group, and it is also used as the "Super" of other resources.
  // This map stores the number of cycles contributed by sub-resources that are
  // part of a "Super" resource. The key value is the "Super" resource mask ID.
  DenseMap<uint64_t, unsigned> SuperResources;

  unsigned NumProcResources = SM.getNumProcResourceKinds();
  APInt Buffers(NumProcResources, 0);

  bool AllInOrderResources = true;
  bool AnyDispatchHazards = false;
  for (unsigned I = 0, E = SCDesc.NumWriteProcResEntries; I < E; ++I) {
    const MCWriteProcResEntry *PRE = STI.getWriteProcResBegin(&SCDesc) + I;
    const MCProcResourceDesc &PR = *SM.getProcResource(PRE->ProcResourceIdx);
    uint64_t Mask = ProcResourceMasks[PRE->ProcResourceIdx];
    if (PR.BufferSize < 0) {
      AllInOrderResources = false;
    } else {
      Buffers.setBit(PRE->ProcResourceIdx);
      AnyDispatchHazards |= (PR.BufferSize == 0);
      AllInOrderResources &= (PR.BufferSize <= 1);
    }

    CycleSegment RCy(0, PRE->Cycles, false);
    Worklist.emplace_back(ResourcePlusCycles(Mask, ResourceUsage(RCy)));
    if (PR.SuperIdx) {
      uint64_t Super = ProcResourceMasks[PR.SuperIdx];
      SuperResources[Super] += PRE->Cycles;
    }
  }

  ID.MustIssueImmediately = AllInOrderResources && AnyDispatchHazards;

  // Sort elements by mask popcount, so that we prioritize resource units over
  // resource groups, and smaller groups over larger groups.
  sort(Worklist, [](const ResourcePlusCycles &A, const ResourcePlusCycles &B) {
    unsigned popcntA = countPopulation(A.first);
    unsigned popcntB = countPopulation(B.first);
    if (popcntA < popcntB)
      return true;
    if (popcntA > popcntB)
      return false;
    return A.first < B.first;
  });

  uint64_t UsedResourceUnits = 0;

  // Remove cycles contributed by smaller resources.
  for (unsigned I = 0, E = Worklist.size(); I < E; ++I) {
    ResourcePlusCycles &A = Worklist[I];
    if (!A.second.size()) {
      A.second.NumUnits = 0;
      A.second.setReserved();
      ID.Resources.emplace_back(A);
      continue;
    }

    ID.Resources.emplace_back(A);
    uint64_t NormalizedMask = A.first;
    if (countPopulation(A.first) == 1) {
      UsedResourceUnits |= A.first;
    } else {
      // Remove the leading 1 from the resource group mask.
      NormalizedMask ^= PowerOf2Floor(NormalizedMask);
    }

    for (unsigned J = I + 1; J < E; ++J) {
      ResourcePlusCycles &B = Worklist[J];
      if ((NormalizedMask & B.first) == NormalizedMask) {
        B.second.CS.subtract(A.second.size() - SuperResources[A.first]);
        if (countPopulation(B.first) > 1)
          B.second.NumUnits++;
      }
    }
  }

  // A SchedWrite may specify a number of cycles in which a resource group
  // is reserved. For example (on target x86; cpu Haswell):
  //
  //  SchedWriteRes<[HWPort0, HWPort1, HWPort01]> {
  //    let ResourceCycles = [2, 2, 3];
  //  }
  //
  // This means:
  // Resource units HWPort0 and HWPort1 are both used for 2cy.
  // Resource group HWPort01 is the union of HWPort0 and HWPort1.
  // Since this write touches both HWPort0 and HWPort1 for 2cy, HWPort01
  // will not be usable for 2 entire cycles from instruction issue.
  //
  // On top of those 2cy, SchedWriteRes explicitly specifies an extra latency
  // of 3 cycles for HWPort01. This tool assumes that the 3cy latency is an
  // extra delay on top of the 2 cycles latency.
  // During those extra cycles, HWPort01 is not usable by other instructions.
  for (ResourcePlusCycles &RPC : ID.Resources) {
    if (countPopulation(RPC.first) > 1 && !RPC.second.isReserved()) {
      // Remove the leading 1 from the resource group mask.
      uint64_t Mask = RPC.first ^ PowerOf2Floor(RPC.first);
      if ((Mask & UsedResourceUnits) == Mask)
        RPC.second.setReserved();
    }
  }

  // Identify extra buffers that are consumed through super resources.
  for (const std::pair<uint64_t, unsigned> &SR : SuperResources) {
    for (unsigned I = 1, E = NumProcResources; I < E; ++I) {
      const MCProcResourceDesc &PR = *SM.getProcResource(I);
      if (PR.BufferSize == -1)
        continue;

      uint64_t Mask = ProcResourceMasks[I];
      if (Mask != SR.first && ((Mask & SR.first) == SR.first))
        Buffers.setBit(I);
    }
  }

  // Now set the buffers.
  if (unsigned NumBuffers = Buffers.countPopulation()) {
    ID.Buffers.resize(NumBuffers);
    for (unsigned I = 0, E = NumProcResources; I < E && NumBuffers; ++I) {
      if (Buffers[I]) {
        --NumBuffers;
        ID.Buffers[NumBuffers] = ProcResourceMasks[I];
      }
    }
  }

  LLVM_DEBUG({
    for (const std::pair<uint64_t, ResourceUsage> &R : ID.Resources)
      dbgs() << "\t\tMask=" << format_hex(R.first, 16) << ", "
             << "cy=" << R.second.size() << '\n';
    for (const uint64_t R : ID.Buffers)
      dbgs() << "\t\tBuffer Mask=" << format_hex(R, 16) << '\n';
  });
}

static void computeMaxLatency(InstrDesc &ID, const MCInstrDesc &MCDesc,
                              const MCSchedClassDesc &SCDesc,
                              const MCSubtargetInfo &STI) {
  if (MCDesc.isCall()) {
    // We cannot estimate how long this call will take.
    // Artificially set an arbitrarily high latency (100cy).
    ID.MaxLatency = 100U;
    return;
  }

  int Latency = MCSchedModel::computeInstrLatency(STI, SCDesc);
  // If latency is unknown, then conservatively assume a MaxLatency of 100cy.
  ID.MaxLatency = Latency < 0 ? 100U : static_cast<unsigned>(Latency);
}

static Error verifyOperands(const MCInstrDesc &MCDesc, const MCInst &MCI) {
  // Count register definitions, and skip non register operands in the process.
  unsigned I, E;
  unsigned NumExplicitDefs = MCDesc.getNumDefs();
  for (I = 0, E = MCI.getNumOperands(); NumExplicitDefs && I < E; ++I) {
    const MCOperand &Op = MCI.getOperand(I);
    if (Op.isReg())
      --NumExplicitDefs;
  }

  if (NumExplicitDefs) {
    return make_error<InstructionError<MCInst>>(
        "Expected more register operand definitions.", MCI);
  }

  if (MCDesc.hasOptionalDef()) {
    // Always assume that the optional definition is the last operand.
    const MCOperand &Op = MCI.getOperand(MCDesc.getNumOperands() - 1);
    if (I == MCI.getNumOperands() || !Op.isReg()) {
      std::string Message =
          "expected a register operand for an optional definition. Instruction "
          "has not been correctly analyzed.";
      return make_error<InstructionError<MCInst>>(Message, MCI);
    }
  }

  return ErrorSuccess();
}

void InstrBuilder::populateWrites(InstrDesc &ID, const MCInst &MCI,
                                  unsigned SchedClassID) {
  const MCInstrDesc &MCDesc = MCII.get(MCI.getOpcode());
  const MCSchedModel &SM = STI.getSchedModel();
  const MCSchedClassDesc &SCDesc = *SM.getSchedClassDesc(SchedClassID);

  // Assumptions made by this algorithm:
  //  1. The number of explicit and implicit register definitions in a MCInst
  //     matches the number of explicit and implicit definitions according to
  //     the opcode descriptor (MCInstrDesc).
  //  2. Uses start at index #(MCDesc.getNumDefs()).
  //  3. There can only be a single optional register definition, an it is
  //     always the last operand of the sequence (excluding extra operands
  //     contributed by variadic opcodes).
  //
  // These assumptions work quite well for most out-of-order in-tree targets
  // like x86. This is mainly because the vast majority of instructions is
  // expanded to MCInst using a straightforward lowering logic that preserves
  // the ordering of the operands.
  //
  // About assumption 1.
  // The algorithm allows non-register operands between register operand
  // definitions. This helps to handle some special ARM instructions with
  // implicit operand increment (-mtriple=armv7):
  //
  // vld1.32  {d18, d19}, [r1]!  @ <MCInst #1463 VLD1q32wb_fixed
  //                             @  <MCOperand Reg:59>
  //                             @  <MCOperand Imm:0>     (!!)
  //                             @  <MCOperand Reg:67>
  //                             @  <MCOperand Imm:0>
  //                             @  <MCOperand Imm:14>
  //                             @  <MCOperand Reg:0>>
  //
  // MCDesc reports:
  //  6 explicit operands.
  //  1 optional definition
  //  2 explicit definitions (!!)
  //
  // The presence of an 'Imm' operand between the two register definitions
  // breaks the assumption that "register definitions are always at the
  // beginning of the operand sequence".
  //
  // To workaround this issue, this algorithm ignores (i.e. skips) any
  // non-register operands between register definitions.  The optional
  // definition is still at index #(NumOperands-1).
  //
  // According to assumption 2. register reads start at #(NumExplicitDefs-1).
  // That means, register R1 from the example is both read and written.
  unsigned NumExplicitDefs = MCDesc.getNumDefs();
  unsigned NumImplicitDefs = MCDesc.getNumImplicitDefs();
  unsigned NumWriteLatencyEntries = SCDesc.NumWriteLatencyEntries;
  unsigned TotalDefs = NumExplicitDefs + NumImplicitDefs;
  if (MCDesc.hasOptionalDef())
    TotalDefs++;

  unsigned NumVariadicOps = MCI.getNumOperands() - MCDesc.getNumOperands();
  ID.Writes.resize(TotalDefs + NumVariadicOps);
  // Iterate over the operands list, and skip non-register operands.
  // The first NumExplictDefs register operands are expected to be register
  // definitions.
  unsigned CurrentDef = 0;
  unsigned i = 0;
  for (; i < MCI.getNumOperands() && CurrentDef < NumExplicitDefs; ++i) {
    const MCOperand &Op = MCI.getOperand(i);
    if (!Op.isReg())
      continue;

    WriteDescriptor &Write = ID.Writes[CurrentDef];
    Write.OpIndex = i;
    if (CurrentDef < NumWriteLatencyEntries) {
      const MCWriteLatencyEntry &WLE =
          *STI.getWriteLatencyEntry(&SCDesc, CurrentDef);
      // Conservatively default to MaxLatency.
      Write.Latency =
          WLE.Cycles < 0 ? ID.MaxLatency : static_cast<unsigned>(WLE.Cycles);
      Write.SClassOrWriteResourceID = WLE.WriteResourceID;
    } else {
      // Assign a default latency for this write.
      Write.Latency = ID.MaxLatency;
      Write.SClassOrWriteResourceID = 0;
    }
    Write.IsOptionalDef = false;
    LLVM_DEBUG({
      dbgs() << "\t\t[Def]    OpIdx=" << Write.OpIndex
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
    CurrentDef++;
  }

  assert(CurrentDef == NumExplicitDefs &&
         "Expected more register operand definitions.");
  for (CurrentDef = 0; CurrentDef < NumImplicitDefs; ++CurrentDef) {
    unsigned Index = NumExplicitDefs + CurrentDef;
    WriteDescriptor &Write = ID.Writes[Index];
    Write.OpIndex = ~CurrentDef;
    Write.RegisterID = MCDesc.getImplicitDefs()[CurrentDef];
    if (Index < NumWriteLatencyEntries) {
      const MCWriteLatencyEntry &WLE =
          *STI.getWriteLatencyEntry(&SCDesc, Index);
      // Conservatively default to MaxLatency.
      Write.Latency =
          WLE.Cycles < 0 ? ID.MaxLatency : static_cast<unsigned>(WLE.Cycles);
      Write.SClassOrWriteResourceID = WLE.WriteResourceID;
    } else {
      // Assign a default latency for this write.
      Write.Latency = ID.MaxLatency;
      Write.SClassOrWriteResourceID = 0;
    }

    Write.IsOptionalDef = false;
    assert(Write.RegisterID != 0 && "Expected a valid phys register!");
    LLVM_DEBUG({
      dbgs() << "\t\t[Def][I] OpIdx=" << ~Write.OpIndex
             << ", PhysReg=" << MRI.getName(Write.RegisterID)
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
  }

  if (MCDesc.hasOptionalDef()) {
    WriteDescriptor &Write = ID.Writes[NumExplicitDefs + NumImplicitDefs];
    Write.OpIndex = MCDesc.getNumOperands() - 1;
    // Assign a default latency for this write.
    Write.Latency = ID.MaxLatency;
    Write.SClassOrWriteResourceID = 0;
    Write.IsOptionalDef = true;
    LLVM_DEBUG({
      dbgs() << "\t\t[Def][O] OpIdx=" << Write.OpIndex
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
  }

  if (!NumVariadicOps)
    return;

  // FIXME: if an instruction opcode is flagged 'mayStore', and it has no
  // "unmodeledSideEffects', then this logic optimistically assumes that any
  // extra register operands in the variadic sequence is not a register
  // definition.
  //
  // Otherwise, we conservatively assume that any register operand from the
  // variadic sequence is both a register read and a register write.
  bool AssumeUsesOnly = MCDesc.mayStore() && !MCDesc.mayLoad() &&
                        !MCDesc.hasUnmodeledSideEffects();
  CurrentDef = NumExplicitDefs + NumImplicitDefs + MCDesc.hasOptionalDef();
  for (unsigned I = 0, OpIndex = MCDesc.getNumOperands();
       I < NumVariadicOps && !AssumeUsesOnly; ++I, ++OpIndex) {
    const MCOperand &Op = MCI.getOperand(OpIndex);
    if (!Op.isReg())
      continue;

    WriteDescriptor &Write = ID.Writes[CurrentDef];
    Write.OpIndex = OpIndex;
    // Assign a default latency for this write.
    Write.Latency = ID.MaxLatency;
    Write.SClassOrWriteResourceID = 0;
    Write.IsOptionalDef = false;
    ++CurrentDef;
    LLVM_DEBUG({
      dbgs() << "\t\t[Def][V] OpIdx=" << Write.OpIndex
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
  }

  ID.Writes.resize(CurrentDef);
}

void InstrBuilder::populateReads(InstrDesc &ID, const MCInst &MCI,
                                 unsigned SchedClassID) {
  const MCInstrDesc &MCDesc = MCII.get(MCI.getOpcode());
  unsigned NumExplicitUses = MCDesc.getNumOperands() - MCDesc.getNumDefs();
  unsigned NumImplicitUses = MCDesc.getNumImplicitUses();
  // Remove the optional definition.
  if (MCDesc.hasOptionalDef())
    --NumExplicitUses;
  unsigned NumVariadicOps = MCI.getNumOperands() - MCDesc.getNumOperands();
  unsigned TotalUses = NumExplicitUses + NumImplicitUses + NumVariadicOps;
  ID.Reads.resize(TotalUses);
  unsigned CurrentUse = 0;
  for (unsigned I = 0, OpIndex = MCDesc.getNumDefs(); I < NumExplicitUses;
       ++I, ++OpIndex) {
    const MCOperand &Op = MCI.getOperand(OpIndex);
    if (!Op.isReg())
      continue;

    ReadDescriptor &Read = ID.Reads[CurrentUse];
    Read.OpIndex = OpIndex;
    Read.UseIndex = I;
    Read.SchedClassID = SchedClassID;
    ++CurrentUse;
    LLVM_DEBUG(dbgs() << "\t\t[Use]    OpIdx=" << Read.OpIndex
                      << ", UseIndex=" << Read.UseIndex << '\n');
  }

  // For the purpose of ReadAdvance, implicit uses come directly after explicit
  // uses. The "UseIndex" must be updated according to that implicit layout.
  for (unsigned I = 0; I < NumImplicitUses; ++I) {
    ReadDescriptor &Read = ID.Reads[CurrentUse + I];
    Read.OpIndex = ~I;
    Read.UseIndex = NumExplicitUses + I;
    Read.RegisterID = MCDesc.getImplicitUses()[I];
    Read.SchedClassID = SchedClassID;
    LLVM_DEBUG(dbgs() << "\t\t[Use][I] OpIdx=" << ~Read.OpIndex
                      << ", UseIndex=" << Read.UseIndex << ", RegisterID="
                      << MRI.getName(Read.RegisterID) << '\n');
  }

  CurrentUse += NumImplicitUses;

  // FIXME: If an instruction opcode is marked as 'mayLoad', and it has no
  // "unmodeledSideEffects", then this logic optimistically assumes that any
  // extra register operands in the variadic sequence are not register
  // definition.

  bool AssumeDefsOnly = !MCDesc.mayStore() && MCDesc.mayLoad() &&
                        !MCDesc.hasUnmodeledSideEffects();
  for (unsigned I = 0, OpIndex = MCDesc.getNumOperands();
       I < NumVariadicOps && !AssumeDefsOnly; ++I, ++OpIndex) {
    const MCOperand &Op = MCI.getOperand(OpIndex);
    if (!Op.isReg())
      continue;

    ReadDescriptor &Read = ID.Reads[CurrentUse];
    Read.OpIndex = OpIndex;
    Read.UseIndex = NumExplicitUses + NumImplicitUses + I;
    Read.SchedClassID = SchedClassID;
    ++CurrentUse;
    LLVM_DEBUG(dbgs() << "\t\t[Use][V] OpIdx=" << Read.OpIndex
                      << ", UseIndex=" << Read.UseIndex << '\n');
  }

  ID.Reads.resize(CurrentUse);
}

Error InstrBuilder::verifyInstrDesc(const InstrDesc &ID,
                                    const MCInst &MCI) const {
  if (ID.NumMicroOps != 0)
    return ErrorSuccess();

  bool UsesMemory = ID.MayLoad || ID.MayStore;
  bool UsesBuffers = !ID.Buffers.empty();
  bool UsesResources = !ID.Resources.empty();
  if (!UsesMemory && !UsesBuffers && !UsesResources)
    return ErrorSuccess();

  StringRef Message;
  if (UsesMemory) {
    Message = "found an inconsistent instruction that decodes "
              "into zero opcodes and that consumes load/store "
              "unit resources.";
  } else {
    Message = "found an inconsistent instruction that decodes "
              "to zero opcodes and that consumes scheduler "
              "resources.";
  }

  return make_error<InstructionError<MCInst>>(Message, MCI);
}

Expected<const InstrDesc &>
InstrBuilder::createInstrDescImpl(const MCInst &MCI) {
  assert(STI.getSchedModel().hasInstrSchedModel() &&
         "Itineraries are not yet supported!");

  // Obtain the instruction descriptor from the opcode.
  unsigned short Opcode = MCI.getOpcode();
  const MCInstrDesc &MCDesc = MCII.get(Opcode);
  const MCSchedModel &SM = STI.getSchedModel();

  // Then obtain the scheduling class information from the instruction.
  unsigned SchedClassID = MCDesc.getSchedClass();
  bool IsVariant = SM.getSchedClassDesc(SchedClassID)->isVariant();

  // Try to solve variant scheduling classes.
  if (IsVariant) {
    unsigned CPUID = SM.getProcessorID();
    while (SchedClassID && SM.getSchedClassDesc(SchedClassID)->isVariant())
      SchedClassID = STI.resolveVariantSchedClass(SchedClassID, &MCI, CPUID);

    if (!SchedClassID) {
      return make_error<InstructionError<MCInst>>(
          "unable to resolve scheduling class for write variant.", MCI);
    }
  }

  // Check if this instruction is supported. Otherwise, report an error.
  const MCSchedClassDesc &SCDesc = *SM.getSchedClassDesc(SchedClassID);
  if (SCDesc.NumMicroOps == MCSchedClassDesc::InvalidNumMicroOps) {
    return make_error<InstructionError<MCInst>>(
        "found an unsupported instruction in the input assembly sequence.",
        MCI);
  }

  LLVM_DEBUG(dbgs() << "\n\t\tOpcode Name= " << MCII.getName(Opcode) << '\n');
  LLVM_DEBUG(dbgs() << "\t\tSchedClassID=" << SchedClassID << '\n');

  // Create a new empty descriptor.
  std::unique_ptr<InstrDesc> ID = llvm::make_unique<InstrDesc>();
  ID->NumMicroOps = SCDesc.NumMicroOps;

  if (MCDesc.isCall() && FirstCallInst) {
    // We don't correctly model calls.
    WithColor::warning() << "found a call in the input assembly sequence.\n";
    WithColor::note() << "call instructions are not correctly modeled. "
                      << "Assume a latency of 100cy.\n";
    FirstCallInst = false;
  }

  if (MCDesc.isReturn() && FirstReturnInst) {
    WithColor::warning() << "found a return instruction in the input"
                         << " assembly sequence.\n";
    WithColor::note() << "program counter updates are ignored.\n";
    FirstReturnInst = false;
  }

  ID->MayLoad = MCDesc.mayLoad();
  ID->MayStore = MCDesc.mayStore();
  ID->HasSideEffects = MCDesc.hasUnmodeledSideEffects();
  ID->BeginGroup = SCDesc.BeginGroup;
  ID->EndGroup = SCDesc.EndGroup;

  initializeUsedResources(*ID, SCDesc, STI, ProcResourceMasks);
  computeMaxLatency(*ID, MCDesc, SCDesc, STI);

  if (Error Err = verifyOperands(MCDesc, MCI))
    return std::move(Err);

  populateWrites(*ID, MCI, SchedClassID);
  populateReads(*ID, MCI, SchedClassID);

  LLVM_DEBUG(dbgs() << "\t\tMaxLatency=" << ID->MaxLatency << '\n');
  LLVM_DEBUG(dbgs() << "\t\tNumMicroOps=" << ID->NumMicroOps << '\n');

  // Sanity check on the instruction descriptor.
  if (Error Err = verifyInstrDesc(*ID, MCI))
    return std::move(Err);

  // Now add the new descriptor.
  SchedClassID = MCDesc.getSchedClass();
  bool IsVariadic = MCDesc.isVariadic();
  if (!IsVariadic && !IsVariant) {
    Descriptors[MCI.getOpcode()] = std::move(ID);
    return *Descriptors[MCI.getOpcode()];
  }

  VariantDescriptors[&MCI] = std::move(ID);
  return *VariantDescriptors[&MCI];
}

Expected<const InstrDesc &>
InstrBuilder::getOrCreateInstrDesc(const MCInst &MCI) {
  if (Descriptors.find_as(MCI.getOpcode()) != Descriptors.end())
    return *Descriptors[MCI.getOpcode()];

  if (VariantDescriptors.find(&MCI) != VariantDescriptors.end())
    return *VariantDescriptors[&MCI];

  return createInstrDescImpl(MCI);
}

Expected<std::unique_ptr<Instruction>>
InstrBuilder::createInstruction(const MCInst &MCI) {
  Expected<const InstrDesc &> DescOrErr = getOrCreateInstrDesc(MCI);
  if (!DescOrErr)
    return DescOrErr.takeError();
  const InstrDesc &D = *DescOrErr;
  std::unique_ptr<Instruction> NewIS = llvm::make_unique<Instruction>(D);

  // Check if this is a dependency breaking instruction.
  APInt Mask;

  bool IsZeroIdiom = false;
  bool IsDepBreaking = false;
  if (MCIA) {
    unsigned ProcID = STI.getSchedModel().getProcessorID();
    IsZeroIdiom = MCIA->isZeroIdiom(MCI, Mask, ProcID);
    IsDepBreaking =
        IsZeroIdiom || MCIA->isDependencyBreaking(MCI, Mask, ProcID);
    if (MCIA->isOptimizableRegisterMove(MCI, ProcID))
      NewIS->setOptimizableMove();
  }

  // Initialize Reads first.
  for (const ReadDescriptor &RD : D.Reads) {
    int RegID = -1;
    if (!RD.isImplicitRead()) {
      // explicit read.
      const MCOperand &Op = MCI.getOperand(RD.OpIndex);
      // Skip non-register operands.
      if (!Op.isReg())
        continue;
      RegID = Op.getReg();
    } else {
      // Implicit read.
      RegID = RD.RegisterID;
    }

    // Skip invalid register operands.
    if (!RegID)
      continue;

    // Okay, this is a register operand. Create a ReadState for it.
    assert(RegID > 0 && "Invalid register ID found!");
    NewIS->getUses().emplace_back(RD, RegID);
    ReadState &RS = NewIS->getUses().back();

    if (IsDepBreaking) {
      // A mask of all zeroes means: explicit input operands are not
      // independent.
      if (Mask.isNullValue()) {
        if (!RD.isImplicitRead())
          RS.setIndependentFromDef();
      } else {
        // Check if this register operand is independent according to `Mask`.
        // Note that Mask may not have enough bits to describe all explicit and
        // implicit input operands. If this register operand doesn't have a
        // corresponding bit in Mask, then conservatively assume that it is
        // dependent.
        if (Mask.getBitWidth() > RD.UseIndex) {
          // Okay. This map describe register use `RD.UseIndex`.
          if (Mask[RD.UseIndex])
            RS.setIndependentFromDef();
        }
      }
    }
  }

  // Early exit if there are no writes.
  if (D.Writes.empty())
    return std::move(NewIS);

  // Track register writes that implicitly clear the upper portion of the
  // underlying super-registers using an APInt.
  APInt WriteMask(D.Writes.size(), 0);

  // Now query the MCInstrAnalysis object to obtain information about which
  // register writes implicitly clear the upper portion of a super-register.
  if (MCIA)
    MCIA->clearsSuperRegisters(MRI, MCI, WriteMask);

  // Initialize writes.
  unsigned WriteIndex = 0;
  for (const WriteDescriptor &WD : D.Writes) {
    unsigned RegID = WD.isImplicitWrite() ? WD.RegisterID
                                          : MCI.getOperand(WD.OpIndex).getReg();
    // Check if this is a optional definition that references NoReg.
    if (WD.IsOptionalDef && !RegID) {
      ++WriteIndex;
      continue;
    }

    assert(RegID && "Expected a valid register ID!");
    NewIS->getDefs().emplace_back(WD, RegID,
                                  /* ClearsSuperRegs */ WriteMask[WriteIndex],
                                  /* WritesZero */ IsZeroIdiom);
    ++WriteIndex;
  }

  return std::move(NewIS);
}
} // namespace mca
} // namespace llvm
