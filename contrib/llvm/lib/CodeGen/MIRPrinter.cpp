//===- MIRPrinter.cpp - MIR serialization format printer ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the class that prints out the LLVM IR and machine
// functions using the MIR serialization format.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LowLevelTypeImpl.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<bool> SimplifyMIR(
    "simplify-mir", cl::Hidden,
    cl::desc("Leave out unnecessary information when printing MIR"));

namespace {

/// This structure describes how to print out stack object references.
struct FrameIndexOperand {
  std::string Name;
  unsigned ID;
  bool IsFixed;

  FrameIndexOperand(StringRef Name, unsigned ID, bool IsFixed)
      : Name(Name.str()), ID(ID), IsFixed(IsFixed) {}

  /// Return an ordinary stack object reference.
  static FrameIndexOperand create(StringRef Name, unsigned ID) {
    return FrameIndexOperand(Name, ID, /*IsFixed=*/false);
  }

  /// Return a fixed stack object reference.
  static FrameIndexOperand createFixed(unsigned ID) {
    return FrameIndexOperand("", ID, /*IsFixed=*/true);
  }
};

} // end anonymous namespace

namespace llvm {

/// This class prints out the machine functions using the MIR serialization
/// format.
class MIRPrinter {
  raw_ostream &OS;
  DenseMap<const uint32_t *, unsigned> RegisterMaskIds;
  /// Maps from stack object indices to operand indices which will be used when
  /// printing frame index machine operands.
  DenseMap<int, FrameIndexOperand> StackObjectOperandMapping;

public:
  MIRPrinter(raw_ostream &OS) : OS(OS) {}

  void print(const MachineFunction &MF);

  void convert(yaml::MachineFunction &MF, const MachineRegisterInfo &RegInfo,
               const TargetRegisterInfo *TRI);
  void convert(ModuleSlotTracker &MST, yaml::MachineFrameInfo &YamlMFI,
               const MachineFrameInfo &MFI);
  void convert(yaml::MachineFunction &MF,
               const MachineConstantPool &ConstantPool);
  void convert(ModuleSlotTracker &MST, yaml::MachineJumpTable &YamlJTI,
               const MachineJumpTableInfo &JTI);
  void convertStackObjects(yaml::MachineFunction &YMF,
                           const MachineFunction &MF, ModuleSlotTracker &MST);

private:
  void initRegisterMaskIds(const MachineFunction &MF);
};

/// This class prints out the machine instructions using the MIR serialization
/// format.
class MIPrinter {
  raw_ostream &OS;
  ModuleSlotTracker &MST;
  const DenseMap<const uint32_t *, unsigned> &RegisterMaskIds;
  const DenseMap<int, FrameIndexOperand> &StackObjectOperandMapping;
  /// Synchronization scope names registered with LLVMContext.
  SmallVector<StringRef, 8> SSNs;

  bool canPredictBranchProbabilities(const MachineBasicBlock &MBB) const;
  bool canPredictSuccessors(const MachineBasicBlock &MBB) const;

public:
  MIPrinter(raw_ostream &OS, ModuleSlotTracker &MST,
            const DenseMap<const uint32_t *, unsigned> &RegisterMaskIds,
            const DenseMap<int, FrameIndexOperand> &StackObjectOperandMapping)
      : OS(OS), MST(MST), RegisterMaskIds(RegisterMaskIds),
        StackObjectOperandMapping(StackObjectOperandMapping) {}

  void print(const MachineBasicBlock &MBB);

  void print(const MachineInstr &MI);
  void printStackObjectReference(int FrameIndex);
  void print(const MachineInstr &MI, unsigned OpIdx,
             const TargetRegisterInfo *TRI, bool ShouldPrintRegisterTies,
             LLT TypeToPrint, bool PrintDef = true);
};

} // end namespace llvm

namespace llvm {
namespace yaml {

/// This struct serializes the LLVM IR module.
template <> struct BlockScalarTraits<Module> {
  static void output(const Module &Mod, void *Ctxt, raw_ostream &OS) {
    Mod.print(OS, nullptr);
  }

  static StringRef input(StringRef Str, void *Ctxt, Module &Mod) {
    llvm_unreachable("LLVM Module is supposed to be parsed separately");
    return "";
  }
};

} // end namespace yaml
} // end namespace llvm

static void printRegMIR(unsigned Reg, yaml::StringValue &Dest,
                        const TargetRegisterInfo *TRI) {
  raw_string_ostream OS(Dest.Value);
  OS << printReg(Reg, TRI);
}

void MIRPrinter::print(const MachineFunction &MF) {
  initRegisterMaskIds(MF);

  yaml::MachineFunction YamlMF;
  YamlMF.Name = MF.getName();
  YamlMF.Alignment = MF.getAlignment();
  YamlMF.ExposesReturnsTwice = MF.exposesReturnsTwice();
  YamlMF.HasWinCFI = MF.hasWinCFI();

  YamlMF.Legalized = MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::Legalized);
  YamlMF.RegBankSelected = MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::RegBankSelected);
  YamlMF.Selected = MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::Selected);
  YamlMF.FailedISel = MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::FailedISel);

  convert(YamlMF, MF.getRegInfo(), MF.getSubtarget().getRegisterInfo());
  ModuleSlotTracker MST(MF.getFunction().getParent());
  MST.incorporateFunction(MF.getFunction());
  convert(MST, YamlMF.FrameInfo, MF.getFrameInfo());
  convertStackObjects(YamlMF, MF, MST);
  if (const auto *ConstantPool = MF.getConstantPool())
    convert(YamlMF, *ConstantPool);
  if (const auto *JumpTableInfo = MF.getJumpTableInfo())
    convert(MST, YamlMF.JumpTableInfo, *JumpTableInfo);
  raw_string_ostream StrOS(YamlMF.Body.Value.Value);
  bool IsNewlineNeeded = false;
  for (const auto &MBB : MF) {
    if (IsNewlineNeeded)
      StrOS << "\n";
    MIPrinter(StrOS, MST, RegisterMaskIds, StackObjectOperandMapping)
        .print(MBB);
    IsNewlineNeeded = true;
  }
  StrOS.flush();
  yaml::Output Out(OS);
  if (!SimplifyMIR)
      Out.setWriteDefaultValues(true);
  Out << YamlMF;
}

static void printCustomRegMask(const uint32_t *RegMask, raw_ostream &OS,
                               const TargetRegisterInfo *TRI) {
  assert(RegMask && "Can't print an empty register mask");
  OS << StringRef("CustomRegMask(");

  bool IsRegInRegMaskFound = false;
  for (int I = 0, E = TRI->getNumRegs(); I < E; I++) {
    // Check whether the register is asserted in regmask.
    if (RegMask[I / 32] & (1u << (I % 32))) {
      if (IsRegInRegMaskFound)
        OS << ',';
      OS << printReg(I, TRI);
      IsRegInRegMaskFound = true;
    }
  }

  OS << ')';
}

static void printRegClassOrBank(unsigned Reg, yaml::StringValue &Dest,
                                const MachineRegisterInfo &RegInfo,
                                const TargetRegisterInfo *TRI) {
  raw_string_ostream OS(Dest.Value);
  OS << printRegClassOrBank(Reg, RegInfo, TRI);
}

template <typename T>
static void
printStackObjectDbgInfo(const MachineFunction::VariableDbgInfo &DebugVar,
                        T &Object, ModuleSlotTracker &MST) {
  std::array<std::string *, 3> Outputs{{&Object.DebugVar.Value,
                                        &Object.DebugExpr.Value,
                                        &Object.DebugLoc.Value}};
  std::array<const Metadata *, 3> Metas{{DebugVar.Var,
                                        DebugVar.Expr,
                                        DebugVar.Loc}};
  for (unsigned i = 0; i < 3; ++i) {
    raw_string_ostream StrOS(*Outputs[i]);
    Metas[i]->printAsOperand(StrOS, MST);
  }
}

void MIRPrinter::convert(yaml::MachineFunction &MF,
                         const MachineRegisterInfo &RegInfo,
                         const TargetRegisterInfo *TRI) {
  MF.TracksRegLiveness = RegInfo.tracksLiveness();

  // Print the virtual register definitions.
  for (unsigned I = 0, E = RegInfo.getNumVirtRegs(); I < E; ++I) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(I);
    yaml::VirtualRegisterDefinition VReg;
    VReg.ID = I;
    if (RegInfo.getVRegName(Reg) != "")
      continue;
    ::printRegClassOrBank(Reg, VReg.Class, RegInfo, TRI);
    unsigned PreferredReg = RegInfo.getSimpleHint(Reg);
    if (PreferredReg)
      printRegMIR(PreferredReg, VReg.PreferredRegister, TRI);
    MF.VirtualRegisters.push_back(VReg);
  }

  // Print the live ins.
  for (std::pair<unsigned, unsigned> LI : RegInfo.liveins()) {
    yaml::MachineFunctionLiveIn LiveIn;
    printRegMIR(LI.first, LiveIn.Register, TRI);
    if (LI.second)
      printRegMIR(LI.second, LiveIn.VirtualRegister, TRI);
    MF.LiveIns.push_back(LiveIn);
  }

  // Prints the callee saved registers.
  if (RegInfo.isUpdatedCSRsInitialized()) {
    const MCPhysReg *CalleeSavedRegs = RegInfo.getCalleeSavedRegs();
    std::vector<yaml::FlowStringValue> CalleeSavedRegisters;
    for (const MCPhysReg *I = CalleeSavedRegs; *I; ++I) {
      yaml::FlowStringValue Reg;
      printRegMIR(*I, Reg, TRI);
      CalleeSavedRegisters.push_back(Reg);
    }
    MF.CalleeSavedRegisters = CalleeSavedRegisters;
  }
}

void MIRPrinter::convert(ModuleSlotTracker &MST,
                         yaml::MachineFrameInfo &YamlMFI,
                         const MachineFrameInfo &MFI) {
  YamlMFI.IsFrameAddressTaken = MFI.isFrameAddressTaken();
  YamlMFI.IsReturnAddressTaken = MFI.isReturnAddressTaken();
  YamlMFI.HasStackMap = MFI.hasStackMap();
  YamlMFI.HasPatchPoint = MFI.hasPatchPoint();
  YamlMFI.StackSize = MFI.getStackSize();
  YamlMFI.OffsetAdjustment = MFI.getOffsetAdjustment();
  YamlMFI.MaxAlignment = MFI.getMaxAlignment();
  YamlMFI.AdjustsStack = MFI.adjustsStack();
  YamlMFI.HasCalls = MFI.hasCalls();
  YamlMFI.MaxCallFrameSize = MFI.isMaxCallFrameSizeComputed()
    ? MFI.getMaxCallFrameSize() : ~0u;
  YamlMFI.CVBytesOfCalleeSavedRegisters =
      MFI.getCVBytesOfCalleeSavedRegisters();
  YamlMFI.HasOpaqueSPAdjustment = MFI.hasOpaqueSPAdjustment();
  YamlMFI.HasVAStart = MFI.hasVAStart();
  YamlMFI.HasMustTailInVarArgFunc = MFI.hasMustTailInVarArgFunc();
  YamlMFI.LocalFrameSize = MFI.getLocalFrameSize();
  if (MFI.getSavePoint()) {
    raw_string_ostream StrOS(YamlMFI.SavePoint.Value);
    StrOS << printMBBReference(*MFI.getSavePoint());
  }
  if (MFI.getRestorePoint()) {
    raw_string_ostream StrOS(YamlMFI.RestorePoint.Value);
    StrOS << printMBBReference(*MFI.getRestorePoint());
  }
}

void MIRPrinter::convertStackObjects(yaml::MachineFunction &YMF,
                                     const MachineFunction &MF,
                                     ModuleSlotTracker &MST) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  // Process fixed stack objects.
  unsigned ID = 0;
  for (int I = MFI.getObjectIndexBegin(); I < 0; ++I) {
    if (MFI.isDeadObjectIndex(I))
      continue;

    yaml::FixedMachineStackObject YamlObject;
    YamlObject.ID = ID;
    YamlObject.Type = MFI.isSpillSlotObjectIndex(I)
                          ? yaml::FixedMachineStackObject::SpillSlot
                          : yaml::FixedMachineStackObject::DefaultType;
    YamlObject.Offset = MFI.getObjectOffset(I);
    YamlObject.Size = MFI.getObjectSize(I);
    YamlObject.Alignment = MFI.getObjectAlignment(I);
    YamlObject.StackID = MFI.getStackID(I);
    YamlObject.IsImmutable = MFI.isImmutableObjectIndex(I);
    YamlObject.IsAliased = MFI.isAliasedObjectIndex(I);
    YMF.FixedStackObjects.push_back(YamlObject);
    StackObjectOperandMapping.insert(
        std::make_pair(I, FrameIndexOperand::createFixed(ID++)));
  }

  // Process ordinary stack objects.
  ID = 0;
  for (int I = 0, E = MFI.getObjectIndexEnd(); I < E; ++I) {
    if (MFI.isDeadObjectIndex(I))
      continue;

    yaml::MachineStackObject YamlObject;
    YamlObject.ID = ID;
    if (const auto *Alloca = MFI.getObjectAllocation(I))
      YamlObject.Name.Value =
          Alloca->hasName() ? Alloca->getName() : "<unnamed alloca>";
    YamlObject.Type = MFI.isSpillSlotObjectIndex(I)
                          ? yaml::MachineStackObject::SpillSlot
                          : MFI.isVariableSizedObjectIndex(I)
                                ? yaml::MachineStackObject::VariableSized
                                : yaml::MachineStackObject::DefaultType;
    YamlObject.Offset = MFI.getObjectOffset(I);
    YamlObject.Size = MFI.getObjectSize(I);
    YamlObject.Alignment = MFI.getObjectAlignment(I);
    YamlObject.StackID = MFI.getStackID(I);

    YMF.StackObjects.push_back(YamlObject);
    StackObjectOperandMapping.insert(std::make_pair(
        I, FrameIndexOperand::create(YamlObject.Name.Value, ID++)));
  }

  for (const auto &CSInfo : MFI.getCalleeSavedInfo()) {
    yaml::StringValue Reg;
    printRegMIR(CSInfo.getReg(), Reg, TRI);
    if (!CSInfo.isSpilledToReg()) {
      auto StackObjectInfo = StackObjectOperandMapping.find(CSInfo.getFrameIdx());
      assert(StackObjectInfo != StackObjectOperandMapping.end() &&
             "Invalid stack object index");
      const FrameIndexOperand &StackObject = StackObjectInfo->second;
      if (StackObject.IsFixed) {
        YMF.FixedStackObjects[StackObject.ID].CalleeSavedRegister = Reg;
        YMF.FixedStackObjects[StackObject.ID].CalleeSavedRestored =
          CSInfo.isRestored();
      } else {
        YMF.StackObjects[StackObject.ID].CalleeSavedRegister = Reg;
        YMF.StackObjects[StackObject.ID].CalleeSavedRestored =
          CSInfo.isRestored();
      }
    }
  }
  for (unsigned I = 0, E = MFI.getLocalFrameObjectCount(); I < E; ++I) {
    auto LocalObject = MFI.getLocalFrameObjectMap(I);
    auto StackObjectInfo = StackObjectOperandMapping.find(LocalObject.first);
    assert(StackObjectInfo != StackObjectOperandMapping.end() &&
           "Invalid stack object index");
    const FrameIndexOperand &StackObject = StackObjectInfo->second;
    assert(!StackObject.IsFixed && "Expected a locally mapped stack object");
    YMF.StackObjects[StackObject.ID].LocalOffset = LocalObject.second;
  }

  // Print the stack object references in the frame information class after
  // converting the stack objects.
  if (MFI.hasStackProtectorIndex()) {
    raw_string_ostream StrOS(YMF.FrameInfo.StackProtector.Value);
    MIPrinter(StrOS, MST, RegisterMaskIds, StackObjectOperandMapping)
        .printStackObjectReference(MFI.getStackProtectorIndex());
  }

  // Print the debug variable information.
  for (const MachineFunction::VariableDbgInfo &DebugVar :
       MF.getVariableDbgInfo()) {
    auto StackObjectInfo = StackObjectOperandMapping.find(DebugVar.Slot);
    assert(StackObjectInfo != StackObjectOperandMapping.end() &&
           "Invalid stack object index");
    const FrameIndexOperand &StackObject = StackObjectInfo->second;
    if (StackObject.IsFixed) {
      auto &Object = YMF.FixedStackObjects[StackObject.ID];
      printStackObjectDbgInfo(DebugVar, Object, MST);
    } else {
      auto &Object = YMF.StackObjects[StackObject.ID];
      printStackObjectDbgInfo(DebugVar, Object, MST);
    }
  }
}

void MIRPrinter::convert(yaml::MachineFunction &MF,
                         const MachineConstantPool &ConstantPool) {
  unsigned ID = 0;
  for (const MachineConstantPoolEntry &Constant : ConstantPool.getConstants()) {
    std::string Str;
    raw_string_ostream StrOS(Str);
    if (Constant.isMachineConstantPoolEntry()) {
      Constant.Val.MachineCPVal->print(StrOS);
    } else {
      Constant.Val.ConstVal->printAsOperand(StrOS);
    }

    yaml::MachineConstantPoolValue YamlConstant;
    YamlConstant.ID = ID++;
    YamlConstant.Value = StrOS.str();
    YamlConstant.Alignment = Constant.getAlignment();
    YamlConstant.IsTargetSpecific = Constant.isMachineConstantPoolEntry();

    MF.Constants.push_back(YamlConstant);
  }
}

void MIRPrinter::convert(ModuleSlotTracker &MST,
                         yaml::MachineJumpTable &YamlJTI,
                         const MachineJumpTableInfo &JTI) {
  YamlJTI.Kind = JTI.getEntryKind();
  unsigned ID = 0;
  for (const auto &Table : JTI.getJumpTables()) {
    std::string Str;
    yaml::MachineJumpTable::Entry Entry;
    Entry.ID = ID++;
    for (const auto *MBB : Table.MBBs) {
      raw_string_ostream StrOS(Str);
      StrOS << printMBBReference(*MBB);
      Entry.Blocks.push_back(StrOS.str());
      Str.clear();
    }
    YamlJTI.Entries.push_back(Entry);
  }
}

void MIRPrinter::initRegisterMaskIds(const MachineFunction &MF) {
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  unsigned I = 0;
  for (const uint32_t *Mask : TRI->getRegMasks())
    RegisterMaskIds.insert(std::make_pair(Mask, I++));
}

void llvm::guessSuccessors(const MachineBasicBlock &MBB,
                           SmallVectorImpl<MachineBasicBlock*> &Result,
                           bool &IsFallthrough) {
  SmallPtrSet<MachineBasicBlock*,8> Seen;

  for (const MachineInstr &MI : MBB) {
    if (MI.isPHI())
      continue;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isMBB())
        continue;
      MachineBasicBlock *Succ = MO.getMBB();
      auto RP = Seen.insert(Succ);
      if (RP.second)
        Result.push_back(Succ);
    }
  }
  MachineBasicBlock::const_iterator I = MBB.getLastNonDebugInstr();
  IsFallthrough = I == MBB.end() || !I->isBarrier();
}

bool
MIPrinter::canPredictBranchProbabilities(const MachineBasicBlock &MBB) const {
  if (MBB.succ_size() <= 1)
    return true;
  if (!MBB.hasSuccessorProbabilities())
    return true;

  SmallVector<BranchProbability,8> Normalized(MBB.Probs.begin(),
                                              MBB.Probs.end());
  BranchProbability::normalizeProbabilities(Normalized.begin(),
                                            Normalized.end());
  SmallVector<BranchProbability,8> Equal(Normalized.size());
  BranchProbability::normalizeProbabilities(Equal.begin(), Equal.end());

  return std::equal(Normalized.begin(), Normalized.end(), Equal.begin());
}

bool MIPrinter::canPredictSuccessors(const MachineBasicBlock &MBB) const {
  SmallVector<MachineBasicBlock*,8> GuessedSuccs;
  bool GuessedFallthrough;
  guessSuccessors(MBB, GuessedSuccs, GuessedFallthrough);
  if (GuessedFallthrough) {
    const MachineFunction &MF = *MBB.getParent();
    MachineFunction::const_iterator NextI = std::next(MBB.getIterator());
    if (NextI != MF.end()) {
      MachineBasicBlock *Next = const_cast<MachineBasicBlock*>(&*NextI);
      if (!is_contained(GuessedSuccs, Next))
        GuessedSuccs.push_back(Next);
    }
  }
  if (GuessedSuccs.size() != MBB.succ_size())
    return false;
  return std::equal(MBB.succ_begin(), MBB.succ_end(), GuessedSuccs.begin());
}

void MIPrinter::print(const MachineBasicBlock &MBB) {
  assert(MBB.getNumber() >= 0 && "Invalid MBB number");
  OS << "bb." << MBB.getNumber();
  bool HasAttributes = false;
  if (const auto *BB = MBB.getBasicBlock()) {
    if (BB->hasName()) {
      OS << "." << BB->getName();
    } else {
      HasAttributes = true;
      OS << " (";
      int Slot = MST.getLocalSlot(BB);
      if (Slot == -1)
        OS << "<ir-block badref>";
      else
        OS << (Twine("%ir-block.") + Twine(Slot)).str();
    }
  }
  if (MBB.hasAddressTaken()) {
    OS << (HasAttributes ? ", " : " (");
    OS << "address-taken";
    HasAttributes = true;
  }
  if (MBB.isEHPad()) {
    OS << (HasAttributes ? ", " : " (");
    OS << "landing-pad";
    HasAttributes = true;
  }
  if (MBB.getAlignment()) {
    OS << (HasAttributes ? ", " : " (");
    OS << "align " << MBB.getAlignment();
    HasAttributes = true;
  }
  if (HasAttributes)
    OS << ")";
  OS << ":\n";

  bool HasLineAttributes = false;
  // Print the successors
  bool canPredictProbs = canPredictBranchProbabilities(MBB);
  // Even if the list of successors is empty, if we cannot guess it,
  // we need to print it to tell the parser that the list is empty.
  // This is needed, because MI model unreachable as empty blocks
  // with an empty successor list. If the parser would see that
  // without the successor list, it would guess the code would
  // fallthrough.
  if ((!MBB.succ_empty() && !SimplifyMIR) || !canPredictProbs ||
      !canPredictSuccessors(MBB)) {
    OS.indent(2) << "successors: ";
    for (auto I = MBB.succ_begin(), E = MBB.succ_end(); I != E; ++I) {
      if (I != MBB.succ_begin())
        OS << ", ";
      OS << printMBBReference(**I);
      if (!SimplifyMIR || !canPredictProbs)
        OS << '('
           << format("0x%08" PRIx32, MBB.getSuccProbability(I).getNumerator())
           << ')';
    }
    OS << "\n";
    HasLineAttributes = true;
  }

  // Print the live in registers.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  if (MRI.tracksLiveness() && !MBB.livein_empty()) {
    const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
    OS.indent(2) << "liveins: ";
    bool First = true;
    for (const auto &LI : MBB.liveins()) {
      if (!First)
        OS << ", ";
      First = false;
      OS << printReg(LI.PhysReg, &TRI);
      if (!LI.LaneMask.all())
        OS << ":0x" << PrintLaneMask(LI.LaneMask);
    }
    OS << "\n";
    HasLineAttributes = true;
  }

  if (HasLineAttributes)
    OS << "\n";
  bool IsInBundle = false;
  for (auto I = MBB.instr_begin(), E = MBB.instr_end(); I != E; ++I) {
    const MachineInstr &MI = *I;
    if (IsInBundle && !MI.isInsideBundle()) {
      OS.indent(2) << "}\n";
      IsInBundle = false;
    }
    OS.indent(IsInBundle ? 4 : 2);
    print(MI);
    if (!IsInBundle && MI.getFlag(MachineInstr::BundledSucc)) {
      OS << " {";
      IsInBundle = true;
    }
    OS << "\n";
  }
  if (IsInBundle)
    OS.indent(2) << "}\n";
}

void MIPrinter::print(const MachineInstr &MI) {
  const auto *MF = MI.getMF();
  const auto &MRI = MF->getRegInfo();
  const auto &SubTarget = MF->getSubtarget();
  const auto *TRI = SubTarget.getRegisterInfo();
  assert(TRI && "Expected target register info");
  const auto *TII = SubTarget.getInstrInfo();
  assert(TII && "Expected target instruction info");
  if (MI.isCFIInstruction())
    assert(MI.getNumOperands() == 1 && "Expected 1 operand in CFI instruction");

  SmallBitVector PrintedTypes(8);
  bool ShouldPrintRegisterTies = MI.hasComplexRegisterTies();
  unsigned I = 0, E = MI.getNumOperands();
  for (; I < E && MI.getOperand(I).isReg() && MI.getOperand(I).isDef() &&
         !MI.getOperand(I).isImplicit();
       ++I) {
    if (I)
      OS << ", ";
    print(MI, I, TRI, ShouldPrintRegisterTies,
          MI.getTypeToPrint(I, PrintedTypes, MRI),
          /*PrintDef=*/false);
  }

  if (I)
    OS << " = ";
  if (MI.getFlag(MachineInstr::FrameSetup))
    OS << "frame-setup ";
  if (MI.getFlag(MachineInstr::FrameDestroy))
    OS << "frame-destroy ";
  if (MI.getFlag(MachineInstr::FmNoNans))
    OS << "nnan ";
  if (MI.getFlag(MachineInstr::FmNoInfs))
    OS << "ninf ";
  if (MI.getFlag(MachineInstr::FmNsz))
    OS << "nsz ";
  if (MI.getFlag(MachineInstr::FmArcp))
    OS << "arcp ";
  if (MI.getFlag(MachineInstr::FmContract))
    OS << "contract ";
  if (MI.getFlag(MachineInstr::FmAfn))
    OS << "afn ";
  if (MI.getFlag(MachineInstr::FmReassoc))
    OS << "reassoc ";
  if (MI.getFlag(MachineInstr::NoUWrap))
    OS << "nuw ";
  if (MI.getFlag(MachineInstr::NoSWrap))
    OS << "nsw ";
  if (MI.getFlag(MachineInstr::IsExact))
    OS << "exact ";

  OS << TII->getName(MI.getOpcode());
  if (I < E)
    OS << ' ';

  bool NeedComma = false;
  for (; I < E; ++I) {
    if (NeedComma)
      OS << ", ";
    print(MI, I, TRI, ShouldPrintRegisterTies,
          MI.getTypeToPrint(I, PrintedTypes, MRI));
    NeedComma = true;
  }

  // Print any optional symbols attached to this instruction as-if they were
  // operands.
  if (MCSymbol *PreInstrSymbol = MI.getPreInstrSymbol()) {
    if (NeedComma)
      OS << ',';
    OS << " pre-instr-symbol ";
    MachineOperand::printSymbol(OS, *PreInstrSymbol);
    NeedComma = true;
  }
  if (MCSymbol *PostInstrSymbol = MI.getPostInstrSymbol()) {
    if (NeedComma)
      OS << ',';
    OS << " post-instr-symbol ";
    MachineOperand::printSymbol(OS, *PostInstrSymbol);
    NeedComma = true;
  }

  if (const DebugLoc &DL = MI.getDebugLoc()) {
    if (NeedComma)
      OS << ',';
    OS << " debug-location ";
    DL->printAsOperand(OS, MST);
  }

  if (!MI.memoperands_empty()) {
    OS << " :: ";
    const LLVMContext &Context = MF->getFunction().getContext();
    const MachineFrameInfo &MFI = MF->getFrameInfo();
    bool NeedComma = false;
    for (const auto *Op : MI.memoperands()) {
      if (NeedComma)
        OS << ", ";
      Op->print(OS, MST, SSNs, Context, &MFI, TII);
      NeedComma = true;
    }
  }
}

void MIPrinter::printStackObjectReference(int FrameIndex) {
  auto ObjectInfo = StackObjectOperandMapping.find(FrameIndex);
  assert(ObjectInfo != StackObjectOperandMapping.end() &&
         "Invalid frame index");
  const FrameIndexOperand &Operand = ObjectInfo->second;
  MachineOperand::printStackObjectReference(OS, Operand.ID, Operand.IsFixed,
                                            Operand.Name);
}

void MIPrinter::print(const MachineInstr &MI, unsigned OpIdx,
                      const TargetRegisterInfo *TRI,
                      bool ShouldPrintRegisterTies, LLT TypeToPrint,
                      bool PrintDef) {
  const MachineOperand &Op = MI.getOperand(OpIdx);
  switch (Op.getType()) {
  case MachineOperand::MO_Immediate:
    if (MI.isOperandSubregIdx(OpIdx)) {
      MachineOperand::printTargetFlags(OS, Op);
      MachineOperand::printSubRegIdx(OS, Op.getImm(), TRI);
      break;
    }
    LLVM_FALLTHROUGH;
  case MachineOperand::MO_Register:
  case MachineOperand::MO_CImmediate:
  case MachineOperand::MO_FPImmediate:
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_TargetIndex:
  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_RegisterLiveOut:
  case MachineOperand::MO_Metadata:
  case MachineOperand::MO_MCSymbol:
  case MachineOperand::MO_CFIIndex:
  case MachineOperand::MO_IntrinsicID:
  case MachineOperand::MO_Predicate:
  case MachineOperand::MO_BlockAddress: {
    unsigned TiedOperandIdx = 0;
    if (ShouldPrintRegisterTies && Op.isReg() && Op.isTied() && !Op.isDef())
      TiedOperandIdx = Op.getParent()->findTiedOperandIdx(OpIdx);
    const TargetIntrinsicInfo *TII = MI.getMF()->getTarget().getIntrinsicInfo();
    Op.print(OS, MST, TypeToPrint, PrintDef, /*IsStandalone=*/false,
             ShouldPrintRegisterTies, TiedOperandIdx, TRI, TII);
    break;
  }
  case MachineOperand::MO_FrameIndex:
    printStackObjectReference(Op.getIndex());
    break;
  case MachineOperand::MO_RegisterMask: {
    auto RegMaskInfo = RegisterMaskIds.find(Op.getRegMask());
    if (RegMaskInfo != RegisterMaskIds.end())
      OS << StringRef(TRI->getRegMaskNames()[RegMaskInfo->second]).lower();
    else
      printCustomRegMask(Op.getRegMask(), OS, TRI);
    break;
  }
  }
}

void llvm::printMIR(raw_ostream &OS, const Module &M) {
  yaml::Output Out(OS);
  Out << const_cast<Module &>(M);
}

void llvm::printMIR(raw_ostream &OS, const MachineFunction &MF) {
  MIRPrinter Printer(OS);
  Printer.print(MF);
}
