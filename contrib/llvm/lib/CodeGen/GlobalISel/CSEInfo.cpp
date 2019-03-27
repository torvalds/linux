//===- CSEInfo.cpp ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

#define DEBUG_TYPE "cseinfo"

using namespace llvm;
char llvm::GISelCSEAnalysisWrapperPass::ID = 0;
INITIALIZE_PASS_BEGIN(GISelCSEAnalysisWrapperPass, DEBUG_TYPE,
                      "Analysis containing CSE Info", false, true)
INITIALIZE_PASS_END(GISelCSEAnalysisWrapperPass, DEBUG_TYPE,
                    "Analysis containing CSE Info", false, true)

/// -------- UniqueMachineInstr -------------//

void UniqueMachineInstr::Profile(FoldingSetNodeID &ID) {
  GISelInstProfileBuilder(ID, MI->getMF()->getRegInfo()).addNodeID(MI);
}
/// -----------------------------------------

/// --------- CSEConfig ---------- ///
bool CSEConfig::shouldCSEOpc(unsigned Opc) {
  switch (Opc) {
  default:
    break;
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_AND:
  case TargetOpcode::G_ASHR:
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_XOR:
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_UREM:
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_CONSTANT:
  case TargetOpcode::G_FCONSTANT:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ANYEXT:
  case TargetOpcode::G_UNMERGE_VALUES:
  case TargetOpcode::G_TRUNC:
    return true;
  }
  return false;
}

bool CSEConfigConstantOnly::shouldCSEOpc(unsigned Opc) {
  return Opc == TargetOpcode::G_CONSTANT;
}
/// -----------------------------------------

/// -------- GISelCSEInfo -------------//
void GISelCSEInfo::setMF(MachineFunction &MF) {
  this->MF = &MF;
  this->MRI = &MF.getRegInfo();
}

GISelCSEInfo::~GISelCSEInfo() {}

bool GISelCSEInfo::isUniqueMachineInstValid(
    const UniqueMachineInstr &UMI) const {
  // Should we check here and assert that the instruction has been fully
  // constructed?
  // FIXME: Any other checks required to be done here? Remove this method if
  // none.
  return true;
}

void GISelCSEInfo::invalidateUniqueMachineInstr(UniqueMachineInstr *UMI) {
  bool Removed = CSEMap.RemoveNode(UMI);
  (void)Removed;
  assert(Removed && "Invalidation called on invalid UMI");
  // FIXME: Should UMI be deallocated/destroyed?
}

UniqueMachineInstr *GISelCSEInfo::getNodeIfExists(FoldingSetNodeID &ID,
                                                  MachineBasicBlock *MBB,
                                                  void *&InsertPos) {
  auto *Node = CSEMap.FindNodeOrInsertPos(ID, InsertPos);
  if (Node) {
    if (!isUniqueMachineInstValid(*Node)) {
      invalidateUniqueMachineInstr(Node);
      return nullptr;
    }

    if (Node->MI->getParent() != MBB)
      return nullptr;
  }
  return Node;
}

void GISelCSEInfo::insertNode(UniqueMachineInstr *UMI, void *InsertPos) {
  handleRecordedInsts();
  assert(UMI);
  UniqueMachineInstr *MaybeNewNode = UMI;
  if (InsertPos)
    CSEMap.InsertNode(UMI, InsertPos);
  else
    MaybeNewNode = CSEMap.GetOrInsertNode(UMI);
  if (MaybeNewNode != UMI) {
    // A similar node exists in the folding set. Let's ignore this one.
    return;
  }
  assert(InstrMapping.count(UMI->MI) == 0 &&
         "This instruction should not be in the map");
  InstrMapping[UMI->MI] = MaybeNewNode;
}

UniqueMachineInstr *GISelCSEInfo::getUniqueInstrForMI(const MachineInstr *MI) {
  assert(shouldCSE(MI->getOpcode()) && "Trying to CSE an unsupported Node");
  auto *Node = new (UniqueInstrAllocator) UniqueMachineInstr(MI);
  return Node;
}

void GISelCSEInfo::insertInstr(MachineInstr *MI, void *InsertPos) {
  assert(MI);
  // If it exists in temporary insts, remove it.
  TemporaryInsts.remove(MI);
  auto *Node = getUniqueInstrForMI(MI);
  insertNode(Node, InsertPos);
}

MachineInstr *GISelCSEInfo::getMachineInstrIfExists(FoldingSetNodeID &ID,
                                                    MachineBasicBlock *MBB,
                                                    void *&InsertPos) {
  handleRecordedInsts();
  if (auto *Inst = getNodeIfExists(ID, MBB, InsertPos)) {
    LLVM_DEBUG(dbgs() << "CSEInfo: Found Instr " << *Inst->MI << "\n";);
    return const_cast<MachineInstr *>(Inst->MI);
  }
  return nullptr;
}

void GISelCSEInfo::countOpcodeHit(unsigned Opc) {
#ifndef NDEBUG
  if (OpcodeHitTable.count(Opc))
    OpcodeHitTable[Opc] += 1;
  else
    OpcodeHitTable[Opc] = 1;
#endif
  // Else do nothing.
}

void GISelCSEInfo::recordNewInstruction(MachineInstr *MI) {
  if (shouldCSE(MI->getOpcode())) {
    TemporaryInsts.insert(MI);
    LLVM_DEBUG(dbgs() << "CSEInfo: Recording new MI" << *MI << "\n";);
  }
}

void GISelCSEInfo::handleRecordedInst(MachineInstr *MI) {
  assert(shouldCSE(MI->getOpcode()) && "Invalid instruction for CSE");
  auto *UMI = InstrMapping.lookup(MI);
  LLVM_DEBUG(dbgs() << "CSEInfo: Handling recorded MI" << *MI << "\n";);
  if (UMI) {
    // Invalidate this MI.
    invalidateUniqueMachineInstr(UMI);
    InstrMapping.erase(MI);
  }
  /// Now insert the new instruction.
  if (UMI) {
    /// We'll reuse the same UniqueMachineInstr to avoid the new
    /// allocation.
    *UMI = UniqueMachineInstr(MI);
    insertNode(UMI, nullptr);
  } else {
    /// This is a new instruction. Allocate a new UniqueMachineInstr and
    /// Insert.
    insertInstr(MI);
  }
}

void GISelCSEInfo::handleRemoveInst(MachineInstr *MI) {
  if (auto *UMI = InstrMapping.lookup(MI)) {
    invalidateUniqueMachineInstr(UMI);
    InstrMapping.erase(MI);
  }
  TemporaryInsts.remove(MI);
}

void GISelCSEInfo::handleRecordedInsts() {
  while (!TemporaryInsts.empty()) {
    auto *MI = TemporaryInsts.pop_back_val();
    handleRecordedInst(MI);
  }
}

bool GISelCSEInfo::shouldCSE(unsigned Opc) const {
  // Only GISel opcodes are CSEable
  if (!isPreISelGenericOpcode(Opc))
    return false;
  assert(CSEOpt.get() && "CSEConfig not set");
  return CSEOpt->shouldCSEOpc(Opc);
}

void GISelCSEInfo::erasingInstr(MachineInstr &MI) { handleRemoveInst(&MI); }
void GISelCSEInfo::createdInstr(MachineInstr &MI) { recordNewInstruction(&MI); }
void GISelCSEInfo::changingInstr(MachineInstr &MI) {
  // For now, perform erase, followed by insert.
  erasingInstr(MI);
  createdInstr(MI);
}
void GISelCSEInfo::changedInstr(MachineInstr &MI) { changingInstr(MI); }

void GISelCSEInfo::analyze(MachineFunction &MF) {
  setMF(MF);
  for (auto &MBB : MF) {
    if (MBB.empty())
      continue;
    for (MachineInstr &MI : MBB) {
      if (!shouldCSE(MI.getOpcode()))
        continue;
      LLVM_DEBUG(dbgs() << "CSEInfo::Add MI: " << MI << "\n";);
      insertInstr(&MI);
    }
  }
}

void GISelCSEInfo::releaseMemory() {
  // print();
  CSEMap.clear();
  InstrMapping.clear();
  UniqueInstrAllocator.Reset();
  TemporaryInsts.clear();
  CSEOpt.reset();
  MRI = nullptr;
  MF = nullptr;
#ifndef NDEBUG
  OpcodeHitTable.clear();
#endif
}

void GISelCSEInfo::print() {
#ifndef NDEBUG
  for (auto &It : OpcodeHitTable) {
    dbgs() << "CSE Count for Opc " << It.first << " : " << It.second << "\n";
  };
#endif
}
/// -----------------------------------------
// ---- Profiling methods for FoldingSetNode --- //
const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeID(const MachineInstr *MI) const {
  addNodeIDMBB(MI->getParent());
  addNodeIDOpcode(MI->getOpcode());
  for (auto &Op : MI->operands())
    addNodeIDMachineOperand(Op);
  addNodeIDFlag(MI->getFlags());
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDOpcode(unsigned Opc) const {
  ID.AddInteger(Opc);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDRegType(const LLT &Ty) const {
  uint64_t Val = Ty.getUniqueRAWLLTData();
  ID.AddInteger(Val);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDRegType(const TargetRegisterClass *RC) const {
  ID.AddPointer(RC);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDRegType(const RegisterBank *RB) const {
  ID.AddPointer(RB);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDImmediate(int64_t Imm) const {
  ID.AddInteger(Imm);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDRegNum(unsigned Reg) const {
  ID.AddInteger(Reg);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDRegType(const unsigned Reg) const {
  addNodeIDMachineOperand(MachineOperand::CreateReg(Reg, false));
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDMBB(const MachineBasicBlock *MBB) const {
  ID.AddPointer(MBB);
  return *this;
}

const GISelInstProfileBuilder &
GISelInstProfileBuilder::addNodeIDFlag(unsigned Flag) const {
  if (Flag)
    ID.AddInteger(Flag);
  return *this;
}

const GISelInstProfileBuilder &GISelInstProfileBuilder::addNodeIDMachineOperand(
    const MachineOperand &MO) const {
  if (MO.isReg()) {
    unsigned Reg = MO.getReg();
    if (!MO.isDef())
      addNodeIDRegNum(Reg);
    LLT Ty = MRI.getType(Reg);
    if (Ty.isValid())
      addNodeIDRegType(Ty);
    auto *RB = MRI.getRegBankOrNull(Reg);
    if (RB)
      addNodeIDRegType(RB);
    auto *RC = MRI.getRegClassOrNull(Reg);
    if (RC)
      addNodeIDRegType(RC);
    assert(!MO.isImplicit() && "Unhandled case");
  } else if (MO.isImm())
    ID.AddInteger(MO.getImm());
  else if (MO.isCImm())
    ID.AddPointer(MO.getCImm());
  else if (MO.isFPImm())
    ID.AddPointer(MO.getFPImm());
  else if (MO.isPredicate())
    ID.AddInteger(MO.getPredicate());
  else
    llvm_unreachable("Unhandled operand type");
  // Handle other types
  return *this;
}

GISelCSEInfo &GISelCSEAnalysisWrapper::get(std::unique_ptr<CSEConfig> CSEOpt,
                                           bool Recompute) {
  if (!AlreadyComputed || Recompute) {
    Info.setCSEConfig(std::move(CSEOpt));
    Info.analyze(*MF);
    AlreadyComputed = true;
  }
  return Info;
}
void GISelCSEAnalysisWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool GISelCSEAnalysisWrapperPass::runOnMachineFunction(MachineFunction &MF) {
  releaseMemory();
  Wrapper.setMF(MF);
  return false;
}
