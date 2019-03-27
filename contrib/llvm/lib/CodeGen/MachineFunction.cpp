//===- MachineFunction.cpp ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect native machine code information for a function.  This allows
// target-specific information about the generated code to be stored with each
// function.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "codegen"

static cl::opt<unsigned>
AlignAllFunctions("align-all-functions",
                  cl::desc("Force the alignment of all functions."),
                  cl::init(0), cl::Hidden);

static const char *getPropertyName(MachineFunctionProperties::Property Prop) {
  using P = MachineFunctionProperties::Property;

  switch(Prop) {
  case P::FailedISel: return "FailedISel";
  case P::IsSSA: return "IsSSA";
  case P::Legalized: return "Legalized";
  case P::NoPHIs: return "NoPHIs";
  case P::NoVRegs: return "NoVRegs";
  case P::RegBankSelected: return "RegBankSelected";
  case P::Selected: return "Selected";
  case P::TracksLiveness: return "TracksLiveness";
  }
  llvm_unreachable("Invalid machine function property");
}

// Pin the vtable to this file.
void MachineFunction::Delegate::anchor() {}

void MachineFunctionProperties::print(raw_ostream &OS) const {
  const char *Separator = "";
  for (BitVector::size_type I = 0; I < Properties.size(); ++I) {
    if (!Properties[I])
      continue;
    OS << Separator << getPropertyName(static_cast<Property>(I));
    Separator = ", ";
  }
}

//===----------------------------------------------------------------------===//
// MachineFunction implementation
//===----------------------------------------------------------------------===//

// Out-of-line virtual method.
MachineFunctionInfo::~MachineFunctionInfo() = default;

void ilist_alloc_traits<MachineBasicBlock>::deleteNode(MachineBasicBlock *MBB) {
  MBB->getParent()->DeleteMachineBasicBlock(MBB);
}

static inline unsigned getFnStackAlignment(const TargetSubtargetInfo *STI,
                                           const Function &F) {
  if (F.hasFnAttribute(Attribute::StackAlignment))
    return F.getFnStackAlignment();
  return STI->getFrameLowering()->getStackAlignment();
}

MachineFunction::MachineFunction(const Function &F,
                                 const LLVMTargetMachine &Target,
                                 const TargetSubtargetInfo &STI,
                                 unsigned FunctionNum, MachineModuleInfo &mmi)
    : F(F), Target(Target), STI(&STI), Ctx(mmi.getContext()), MMI(mmi) {
  FunctionNumber = FunctionNum;
  init();
}

void MachineFunction::handleInsertion(MachineInstr &MI) {
  if (TheDelegate)
    TheDelegate->MF_HandleInsertion(MI);
}

void MachineFunction::handleRemoval(MachineInstr &MI) {
  if (TheDelegate)
    TheDelegate->MF_HandleRemoval(MI);
}

void MachineFunction::init() {
  // Assume the function starts in SSA form with correct liveness.
  Properties.set(MachineFunctionProperties::Property::IsSSA);
  Properties.set(MachineFunctionProperties::Property::TracksLiveness);
  if (STI->getRegisterInfo())
    RegInfo = new (Allocator) MachineRegisterInfo(this);
  else
    RegInfo = nullptr;

  MFInfo = nullptr;
  // We can realign the stack if the target supports it and the user hasn't
  // explicitly asked us not to.
  bool CanRealignSP = STI->getFrameLowering()->isStackRealignable() &&
                      !F.hasFnAttribute("no-realign-stack");
  FrameInfo = new (Allocator) MachineFrameInfo(
      getFnStackAlignment(STI, F), /*StackRealignable=*/CanRealignSP,
      /*ForceRealign=*/CanRealignSP &&
          F.hasFnAttribute(Attribute::StackAlignment));

  if (F.hasFnAttribute(Attribute::StackAlignment))
    FrameInfo->ensureMaxAlignment(F.getFnStackAlignment());

  ConstantPool = new (Allocator) MachineConstantPool(getDataLayout());
  Alignment = STI->getTargetLowering()->getMinFunctionAlignment();

  // FIXME: Shouldn't use pref alignment if explicit alignment is set on F.
  // FIXME: Use Function::optForSize().
  if (!F.hasFnAttribute(Attribute::OptimizeForSize))
    Alignment = std::max(Alignment,
                         STI->getTargetLowering()->getPrefFunctionAlignment());

  if (AlignAllFunctions)
    Alignment = AlignAllFunctions;

  JumpTableInfo = nullptr;

  if (isFuncletEHPersonality(classifyEHPersonality(
          F.hasPersonalityFn() ? F.getPersonalityFn() : nullptr))) {
    WinEHInfo = new (Allocator) WinEHFuncInfo();
  }

  if (isScopedEHPersonality(classifyEHPersonality(
          F.hasPersonalityFn() ? F.getPersonalityFn() : nullptr))) {
    WasmEHInfo = new (Allocator) WasmEHFuncInfo();
  }

  assert(Target.isCompatibleDataLayout(getDataLayout()) &&
         "Can't create a MachineFunction using a Module with a "
         "Target-incompatible DataLayout attached\n");

  PSVManager =
    llvm::make_unique<PseudoSourceValueManager>(*(getSubtarget().
                                                  getInstrInfo()));
}

MachineFunction::~MachineFunction() {
  clear();
}

void MachineFunction::clear() {
  Properties.reset();
  // Don't call destructors on MachineInstr and MachineOperand. All of their
  // memory comes from the BumpPtrAllocator which is about to be purged.
  //
  // Do call MachineBasicBlock destructors, it contains std::vectors.
  for (iterator I = begin(), E = end(); I != E; I = BasicBlocks.erase(I))
    I->Insts.clearAndLeakNodesUnsafely();
  MBBNumbering.clear();

  InstructionRecycler.clear(Allocator);
  OperandRecycler.clear(Allocator);
  BasicBlockRecycler.clear(Allocator);
  CodeViewAnnotations.clear();
  VariableDbgInfos.clear();
  if (RegInfo) {
    RegInfo->~MachineRegisterInfo();
    Allocator.Deallocate(RegInfo);
  }
  if (MFInfo) {
    MFInfo->~MachineFunctionInfo();
    Allocator.Deallocate(MFInfo);
  }

  FrameInfo->~MachineFrameInfo();
  Allocator.Deallocate(FrameInfo);

  ConstantPool->~MachineConstantPool();
  Allocator.Deallocate(ConstantPool);

  if (JumpTableInfo) {
    JumpTableInfo->~MachineJumpTableInfo();
    Allocator.Deallocate(JumpTableInfo);
  }

  if (WinEHInfo) {
    WinEHInfo->~WinEHFuncInfo();
    Allocator.Deallocate(WinEHInfo);
  }

  if (WasmEHInfo) {
    WasmEHInfo->~WasmEHFuncInfo();
    Allocator.Deallocate(WasmEHInfo);
  }
}

const DataLayout &MachineFunction::getDataLayout() const {
  return F.getParent()->getDataLayout();
}

/// Get the JumpTableInfo for this function.
/// If it does not already exist, allocate one.
MachineJumpTableInfo *MachineFunction::
getOrCreateJumpTableInfo(unsigned EntryKind) {
  if (JumpTableInfo) return JumpTableInfo;

  JumpTableInfo = new (Allocator)
    MachineJumpTableInfo((MachineJumpTableInfo::JTEntryKind)EntryKind);
  return JumpTableInfo;
}

/// Should we be emitting segmented stack stuff for the function
bool MachineFunction::shouldSplitStack() const {
  return getFunction().hasFnAttribute("split-stack");
}

/// This discards all of the MachineBasicBlock numbers and recomputes them.
/// This guarantees that the MBB numbers are sequential, dense, and match the
/// ordering of the blocks within the function.  If a specific MachineBasicBlock
/// is specified, only that block and those after it are renumbered.
void MachineFunction::RenumberBlocks(MachineBasicBlock *MBB) {
  if (empty()) { MBBNumbering.clear(); return; }
  MachineFunction::iterator MBBI, E = end();
  if (MBB == nullptr)
    MBBI = begin();
  else
    MBBI = MBB->getIterator();

  // Figure out the block number this should have.
  unsigned BlockNo = 0;
  if (MBBI != begin())
    BlockNo = std::prev(MBBI)->getNumber() + 1;

  for (; MBBI != E; ++MBBI, ++BlockNo) {
    if (MBBI->getNumber() != (int)BlockNo) {
      // Remove use of the old number.
      if (MBBI->getNumber() != -1) {
        assert(MBBNumbering[MBBI->getNumber()] == &*MBBI &&
               "MBB number mismatch!");
        MBBNumbering[MBBI->getNumber()] = nullptr;
      }

      // If BlockNo is already taken, set that block's number to -1.
      if (MBBNumbering[BlockNo])
        MBBNumbering[BlockNo]->setNumber(-1);

      MBBNumbering[BlockNo] = &*MBBI;
      MBBI->setNumber(BlockNo);
    }
  }

  // Okay, all the blocks are renumbered.  If we have compactified the block
  // numbering, shrink MBBNumbering now.
  assert(BlockNo <= MBBNumbering.size() && "Mismatch!");
  MBBNumbering.resize(BlockNo);
}

/// Allocate a new MachineInstr. Use this instead of `new MachineInstr'.
MachineInstr *MachineFunction::CreateMachineInstr(const MCInstrDesc &MCID,
                                                  const DebugLoc &DL,
                                                  bool NoImp) {
  return new (InstructionRecycler.Allocate<MachineInstr>(Allocator))
    MachineInstr(*this, MCID, DL, NoImp);
}

/// Create a new MachineInstr which is a copy of the 'Orig' instruction,
/// identical in all ways except the instruction has no parent, prev, or next.
MachineInstr *
MachineFunction::CloneMachineInstr(const MachineInstr *Orig) {
  return new (InstructionRecycler.Allocate<MachineInstr>(Allocator))
             MachineInstr(*this, *Orig);
}

MachineInstr &MachineFunction::CloneMachineInstrBundle(MachineBasicBlock &MBB,
    MachineBasicBlock::iterator InsertBefore, const MachineInstr &Orig) {
  MachineInstr *FirstClone = nullptr;
  MachineBasicBlock::const_instr_iterator I = Orig.getIterator();
  while (true) {
    MachineInstr *Cloned = CloneMachineInstr(&*I);
    MBB.insert(InsertBefore, Cloned);
    if (FirstClone == nullptr) {
      FirstClone = Cloned;
    } else {
      Cloned->bundleWithPred();
    }

    if (!I->isBundledWithSucc())
      break;
    ++I;
  }
  return *FirstClone;
}

/// Delete the given MachineInstr.
///
/// This function also serves as the MachineInstr destructor - the real
/// ~MachineInstr() destructor must be empty.
void
MachineFunction::DeleteMachineInstr(MachineInstr *MI) {
  // Strip it for parts. The operand array and the MI object itself are
  // independently recyclable.
  if (MI->Operands)
    deallocateOperandArray(MI->CapOperands, MI->Operands);
  // Don't call ~MachineInstr() which must be trivial anyway because
  // ~MachineFunction drops whole lists of MachineInstrs wihout calling their
  // destructors.
  InstructionRecycler.Deallocate(Allocator, MI);
}

/// Allocate a new MachineBasicBlock. Use this instead of
/// `new MachineBasicBlock'.
MachineBasicBlock *
MachineFunction::CreateMachineBasicBlock(const BasicBlock *bb) {
  return new (BasicBlockRecycler.Allocate<MachineBasicBlock>(Allocator))
             MachineBasicBlock(*this, bb);
}

/// Delete the given MachineBasicBlock.
void
MachineFunction::DeleteMachineBasicBlock(MachineBasicBlock *MBB) {
  assert(MBB->getParent() == this && "MBB parent mismatch!");
  MBB->~MachineBasicBlock();
  BasicBlockRecycler.Deallocate(Allocator, MBB);
}

MachineMemOperand *MachineFunction::getMachineMemOperand(
    MachinePointerInfo PtrInfo, MachineMemOperand::Flags f, uint64_t s,
    unsigned base_alignment, const AAMDNodes &AAInfo, const MDNode *Ranges,
    SyncScope::ID SSID, AtomicOrdering Ordering,
    AtomicOrdering FailureOrdering) {
  return new (Allocator)
      MachineMemOperand(PtrInfo, f, s, base_alignment, AAInfo, Ranges,
                        SSID, Ordering, FailureOrdering);
}

MachineMemOperand *
MachineFunction::getMachineMemOperand(const MachineMemOperand *MMO,
                                      int64_t Offset, uint64_t Size) {
  if (MMO->getValue())
    return new (Allocator)
               MachineMemOperand(MachinePointerInfo(MMO->getValue(),
                                                    MMO->getOffset()+Offset),
                                 MMO->getFlags(), Size, MMO->getBaseAlignment(),
                                 AAMDNodes(), nullptr, MMO->getSyncScopeID(),
                                 MMO->getOrdering(), MMO->getFailureOrdering());
  return new (Allocator)
             MachineMemOperand(MachinePointerInfo(MMO->getPseudoValue(),
                                                  MMO->getOffset()+Offset),
                               MMO->getFlags(), Size, MMO->getBaseAlignment(),
                               AAMDNodes(), nullptr, MMO->getSyncScopeID(),
                               MMO->getOrdering(), MMO->getFailureOrdering());
}

MachineMemOperand *
MachineFunction::getMachineMemOperand(const MachineMemOperand *MMO,
                                      const AAMDNodes &AAInfo) {
  MachinePointerInfo MPI = MMO->getValue() ?
             MachinePointerInfo(MMO->getValue(), MMO->getOffset()) :
             MachinePointerInfo(MMO->getPseudoValue(), MMO->getOffset());

  return new (Allocator)
             MachineMemOperand(MPI, MMO->getFlags(), MMO->getSize(),
                               MMO->getBaseAlignment(), AAInfo,
                               MMO->getRanges(), MMO->getSyncScopeID(),
                               MMO->getOrdering(), MMO->getFailureOrdering());
}

MachineInstr::ExtraInfo *
MachineFunction::createMIExtraInfo(ArrayRef<MachineMemOperand *> MMOs,
                                   MCSymbol *PreInstrSymbol,
                                   MCSymbol *PostInstrSymbol) {
  return MachineInstr::ExtraInfo::create(Allocator, MMOs, PreInstrSymbol,
                                         PostInstrSymbol);
}

const char *MachineFunction::createExternalSymbolName(StringRef Name) {
  char *Dest = Allocator.Allocate<char>(Name.size() + 1);
  llvm::copy(Name, Dest);
  Dest[Name.size()] = 0;
  return Dest;
}

uint32_t *MachineFunction::allocateRegMask() {
  unsigned NumRegs = getSubtarget().getRegisterInfo()->getNumRegs();
  unsigned Size = MachineOperand::getRegMaskSize(NumRegs);
  uint32_t *Mask = Allocator.Allocate<uint32_t>(Size);
  memset(Mask, 0, Size * sizeof(Mask[0]));
  return Mask;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineFunction::dump() const {
  print(dbgs());
}
#endif

StringRef MachineFunction::getName() const {
  return getFunction().getName();
}

void MachineFunction::print(raw_ostream &OS, const SlotIndexes *Indexes) const {
  OS << "# Machine code for function " << getName() << ": ";
  getProperties().print(OS);
  OS << '\n';

  // Print Frame Information
  FrameInfo->print(*this, OS);

  // Print JumpTable Information
  if (JumpTableInfo)
    JumpTableInfo->print(OS);

  // Print Constant Pool
  ConstantPool->print(OS);

  const TargetRegisterInfo *TRI = getSubtarget().getRegisterInfo();

  if (RegInfo && !RegInfo->livein_empty()) {
    OS << "Function Live Ins: ";
    for (MachineRegisterInfo::livein_iterator
         I = RegInfo->livein_begin(), E = RegInfo->livein_end(); I != E; ++I) {
      OS << printReg(I->first, TRI);
      if (I->second)
        OS << " in " << printReg(I->second, TRI);
      if (std::next(I) != E)
        OS << ", ";
    }
    OS << '\n';
  }

  ModuleSlotTracker MST(getFunction().getParent());
  MST.incorporateFunction(getFunction());
  for (const auto &BB : *this) {
    OS << '\n';
    // If we print the whole function, print it at its most verbose level.
    BB.print(OS, MST, Indexes, /*IsStandalone=*/true);
  }

  OS << "\n# End machine code for function " << getName() << ".\n\n";
}

namespace llvm {

  template<>
  struct DOTGraphTraits<const MachineFunction*> : public DefaultDOTGraphTraits {
    DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

    static std::string getGraphName(const MachineFunction *F) {
      return ("CFG for '" + F->getName() + "' function").str();
    }

    std::string getNodeLabel(const MachineBasicBlock *Node,
                             const MachineFunction *Graph) {
      std::string OutStr;
      {
        raw_string_ostream OSS(OutStr);

        if (isSimple()) {
          OSS << printMBBReference(*Node);
          if (const BasicBlock *BB = Node->getBasicBlock())
            OSS << ": " << BB->getName();
        } else
          Node->print(OSS);
      }

      if (OutStr[0] == '\n') OutStr.erase(OutStr.begin());

      // Process string output to make it nicer...
      for (unsigned i = 0; i != OutStr.length(); ++i)
        if (OutStr[i] == '\n') {                            // Left justify
          OutStr[i] = '\\';
          OutStr.insert(OutStr.begin()+i+1, 'l');
        }
      return OutStr;
    }
  };

} // end namespace llvm

void MachineFunction::viewCFG() const
{
#ifndef NDEBUG
  ViewGraph(this, "mf" + getName());
#else
  errs() << "MachineFunction::viewCFG is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif // NDEBUG
}

void MachineFunction::viewCFGOnly() const
{
#ifndef NDEBUG
  ViewGraph(this, "mf" + getName(), true);
#else
  errs() << "MachineFunction::viewCFGOnly is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif // NDEBUG
}

/// Add the specified physical register as a live-in value and
/// create a corresponding virtual register for it.
unsigned MachineFunction::addLiveIn(unsigned PReg,
                                    const TargetRegisterClass *RC) {
  MachineRegisterInfo &MRI = getRegInfo();
  unsigned VReg = MRI.getLiveInVirtReg(PReg);
  if (VReg) {
    const TargetRegisterClass *VRegRC = MRI.getRegClass(VReg);
    (void)VRegRC;
    // A physical register can be added several times.
    // Between two calls, the register class of the related virtual register
    // may have been constrained to match some operation constraints.
    // In that case, check that the current register class includes the
    // physical register and is a sub class of the specified RC.
    assert((VRegRC == RC || (VRegRC->contains(PReg) &&
                             RC->hasSubClassEq(VRegRC))) &&
            "Register class mismatch!");
    return VReg;
  }
  VReg = MRI.createVirtualRegister(RC);
  MRI.addLiveIn(PReg, VReg);
  return VReg;
}

/// Return the MCSymbol for the specified non-empty jump table.
/// If isLinkerPrivate is specified, an 'l' label is returned, otherwise a
/// normal 'L' label is returned.
MCSymbol *MachineFunction::getJTISymbol(unsigned JTI, MCContext &Ctx,
                                        bool isLinkerPrivate) const {
  const DataLayout &DL = getDataLayout();
  assert(JumpTableInfo && "No jump tables");
  assert(JTI < JumpTableInfo->getJumpTables().size() && "Invalid JTI!");

  StringRef Prefix = isLinkerPrivate ? DL.getLinkerPrivateGlobalPrefix()
                                     : DL.getPrivateGlobalPrefix();
  SmallString<60> Name;
  raw_svector_ostream(Name)
    << Prefix << "JTI" << getFunctionNumber() << '_' << JTI;
  return Ctx.getOrCreateSymbol(Name);
}

/// Return a function-local symbol to represent the PIC base.
MCSymbol *MachineFunction::getPICBaseSymbol() const {
  const DataLayout &DL = getDataLayout();
  return Ctx.getOrCreateSymbol(Twine(DL.getPrivateGlobalPrefix()) +
                               Twine(getFunctionNumber()) + "$pb");
}

/// \name Exception Handling
/// \{

LandingPadInfo &
MachineFunction::getOrCreateLandingPadInfo(MachineBasicBlock *LandingPad) {
  unsigned N = LandingPads.size();
  for (unsigned i = 0; i < N; ++i) {
    LandingPadInfo &LP = LandingPads[i];
    if (LP.LandingPadBlock == LandingPad)
      return LP;
  }

  LandingPads.push_back(LandingPadInfo(LandingPad));
  return LandingPads[N];
}

void MachineFunction::addInvoke(MachineBasicBlock *LandingPad,
                                MCSymbol *BeginLabel, MCSymbol *EndLabel) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  LP.BeginLabels.push_back(BeginLabel);
  LP.EndLabels.push_back(EndLabel);
}

MCSymbol *MachineFunction::addLandingPad(MachineBasicBlock *LandingPad) {
  MCSymbol *LandingPadLabel = Ctx.createTempSymbol();
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  LP.LandingPadLabel = LandingPadLabel;

  const Instruction *FirstI = LandingPad->getBasicBlock()->getFirstNonPHI();
  if (const auto *LPI = dyn_cast<LandingPadInst>(FirstI)) {
    if (const auto *PF =
            dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts()))
      getMMI().addPersonality(PF);

    if (LPI->isCleanup())
      addCleanup(LandingPad);

    // FIXME: New EH - Add the clauses in reverse order. This isn't 100%
    //        correct, but we need to do it this way because of how the DWARF EH
    //        emitter processes the clauses.
    for (unsigned I = LPI->getNumClauses(); I != 0; --I) {
      Value *Val = LPI->getClause(I - 1);
      if (LPI->isCatch(I - 1)) {
        addCatchTypeInfo(LandingPad,
                         dyn_cast<GlobalValue>(Val->stripPointerCasts()));
      } else {
        // Add filters in a list.
        auto *CVal = cast<Constant>(Val);
        SmallVector<const GlobalValue *, 4> FilterList;
        for (User::op_iterator II = CVal->op_begin(), IE = CVal->op_end();
             II != IE; ++II)
          FilterList.push_back(cast<GlobalValue>((*II)->stripPointerCasts()));

        addFilterTypeInfo(LandingPad, FilterList);
      }
    }

  } else if (const auto *CPI = dyn_cast<CatchPadInst>(FirstI)) {
    for (unsigned I = CPI->getNumArgOperands(); I != 0; --I) {
      Value *TypeInfo = CPI->getArgOperand(I - 1)->stripPointerCasts();
      addCatchTypeInfo(LandingPad, dyn_cast<GlobalValue>(TypeInfo));
    }

  } else {
    assert(isa<CleanupPadInst>(FirstI) && "Invalid landingpad!");
  }

  return LandingPadLabel;
}

void MachineFunction::addCatchTypeInfo(MachineBasicBlock *LandingPad,
                                       ArrayRef<const GlobalValue *> TyInfo) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  for (unsigned N = TyInfo.size(); N; --N)
    LP.TypeIds.push_back(getTypeIDFor(TyInfo[N - 1]));
}

void MachineFunction::addFilterTypeInfo(MachineBasicBlock *LandingPad,
                                        ArrayRef<const GlobalValue *> TyInfo) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  std::vector<unsigned> IdsInFilter(TyInfo.size());
  for (unsigned I = 0, E = TyInfo.size(); I != E; ++I)
    IdsInFilter[I] = getTypeIDFor(TyInfo[I]);
  LP.TypeIds.push_back(getFilterIDFor(IdsInFilter));
}

void MachineFunction::tidyLandingPads(DenseMap<MCSymbol *, uintptr_t> *LPMap,
                                      bool TidyIfNoBeginLabels) {
  for (unsigned i = 0; i != LandingPads.size(); ) {
    LandingPadInfo &LandingPad = LandingPads[i];
    if (LandingPad.LandingPadLabel &&
        !LandingPad.LandingPadLabel->isDefined() &&
        (!LPMap || (*LPMap)[LandingPad.LandingPadLabel] == 0))
      LandingPad.LandingPadLabel = nullptr;

    // Special case: we *should* emit LPs with null LP MBB. This indicates
    // "nounwind" case.
    if (!LandingPad.LandingPadLabel && LandingPad.LandingPadBlock) {
      LandingPads.erase(LandingPads.begin() + i);
      continue;
    }

    if (TidyIfNoBeginLabels) {
      for (unsigned j = 0, e = LandingPads[i].BeginLabels.size(); j != e; ++j) {
        MCSymbol *BeginLabel = LandingPad.BeginLabels[j];
        MCSymbol *EndLabel = LandingPad.EndLabels[j];
        if ((BeginLabel->isDefined() || (LPMap && (*LPMap)[BeginLabel] != 0)) &&
            (EndLabel->isDefined() || (LPMap && (*LPMap)[EndLabel] != 0)))
          continue;

        LandingPad.BeginLabels.erase(LandingPad.BeginLabels.begin() + j);
        LandingPad.EndLabels.erase(LandingPad.EndLabels.begin() + j);
        --j;
        --e;
      }

      // Remove landing pads with no try-ranges.
      if (LandingPads[i].BeginLabels.empty()) {
        LandingPads.erase(LandingPads.begin() + i);
        continue;
      }
    }

    // If there is no landing pad, ensure that the list of typeids is empty.
    // If the only typeid is a cleanup, this is the same as having no typeids.
    if (!LandingPad.LandingPadBlock ||
        (LandingPad.TypeIds.size() == 1 && !LandingPad.TypeIds[0]))
      LandingPad.TypeIds.clear();
    ++i;
  }
}

void MachineFunction::addCleanup(MachineBasicBlock *LandingPad) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  LP.TypeIds.push_back(0);
}

void MachineFunction::addSEHCatchHandler(MachineBasicBlock *LandingPad,
                                         const Function *Filter,
                                         const BlockAddress *RecoverBA) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  SEHHandler Handler;
  Handler.FilterOrFinally = Filter;
  Handler.RecoverBA = RecoverBA;
  LP.SEHHandlers.push_back(Handler);
}

void MachineFunction::addSEHCleanupHandler(MachineBasicBlock *LandingPad,
                                           const Function *Cleanup) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  SEHHandler Handler;
  Handler.FilterOrFinally = Cleanup;
  Handler.RecoverBA = nullptr;
  LP.SEHHandlers.push_back(Handler);
}

void MachineFunction::setCallSiteLandingPad(MCSymbol *Sym,
                                            ArrayRef<unsigned> Sites) {
  LPadToCallSiteMap[Sym].append(Sites.begin(), Sites.end());
}

unsigned MachineFunction::getTypeIDFor(const GlobalValue *TI) {
  for (unsigned i = 0, N = TypeInfos.size(); i != N; ++i)
    if (TypeInfos[i] == TI) return i + 1;

  TypeInfos.push_back(TI);
  return TypeInfos.size();
}

int MachineFunction::getFilterIDFor(std::vector<unsigned> &TyIds) {
  // If the new filter coincides with the tail of an existing filter, then
  // re-use the existing filter.  Folding filters more than this requires
  // re-ordering filters and/or their elements - probably not worth it.
  for (std::vector<unsigned>::iterator I = FilterEnds.begin(),
       E = FilterEnds.end(); I != E; ++I) {
    unsigned i = *I, j = TyIds.size();

    while (i && j)
      if (FilterIds[--i] != TyIds[--j])
        goto try_next;

    if (!j)
      // The new filter coincides with range [i, end) of the existing filter.
      return -(1 + i);

try_next:;
  }

  // Add the new filter.
  int FilterID = -(1 + FilterIds.size());
  FilterIds.reserve(FilterIds.size() + TyIds.size() + 1);
  FilterIds.insert(FilterIds.end(), TyIds.begin(), TyIds.end());
  FilterEnds.push_back(FilterIds.size());
  FilterIds.push_back(0); // terminator
  return FilterID;
}

/// \}

//===----------------------------------------------------------------------===//
//  MachineJumpTableInfo implementation
//===----------------------------------------------------------------------===//

/// Return the size of each entry in the jump table.
unsigned MachineJumpTableInfo::getEntrySize(const DataLayout &TD) const {
  // The size of a jump table entry is 4 bytes unless the entry is just the
  // address of a block, in which case it is the pointer size.
  switch (getEntryKind()) {
  case MachineJumpTableInfo::EK_BlockAddress:
    return TD.getPointerSize();
  case MachineJumpTableInfo::EK_GPRel64BlockAddress:
    return 8;
  case MachineJumpTableInfo::EK_GPRel32BlockAddress:
  case MachineJumpTableInfo::EK_LabelDifference32:
  case MachineJumpTableInfo::EK_Custom32:
    return 4;
  case MachineJumpTableInfo::EK_Inline:
    return 0;
  }
  llvm_unreachable("Unknown jump table encoding!");
}

/// Return the alignment of each entry in the jump table.
unsigned MachineJumpTableInfo::getEntryAlignment(const DataLayout &TD) const {
  // The alignment of a jump table entry is the alignment of int32 unless the
  // entry is just the address of a block, in which case it is the pointer
  // alignment.
  switch (getEntryKind()) {
  case MachineJumpTableInfo::EK_BlockAddress:
    return TD.getPointerABIAlignment(0);
  case MachineJumpTableInfo::EK_GPRel64BlockAddress:
    return TD.getABIIntegerTypeAlignment(64);
  case MachineJumpTableInfo::EK_GPRel32BlockAddress:
  case MachineJumpTableInfo::EK_LabelDifference32:
  case MachineJumpTableInfo::EK_Custom32:
    return TD.getABIIntegerTypeAlignment(32);
  case MachineJumpTableInfo::EK_Inline:
    return 1;
  }
  llvm_unreachable("Unknown jump table encoding!");
}

/// Create a new jump table entry in the jump table info.
unsigned MachineJumpTableInfo::createJumpTableIndex(
                               const std::vector<MachineBasicBlock*> &DestBBs) {
  assert(!DestBBs.empty() && "Cannot create an empty jump table!");
  JumpTables.push_back(MachineJumpTableEntry(DestBBs));
  return JumpTables.size()-1;
}

/// If Old is the target of any jump tables, update the jump tables to branch
/// to New instead.
bool MachineJumpTableInfo::ReplaceMBBInJumpTables(MachineBasicBlock *Old,
                                                  MachineBasicBlock *New) {
  assert(Old != New && "Not making a change?");
  bool MadeChange = false;
  for (size_t i = 0, e = JumpTables.size(); i != e; ++i)
    ReplaceMBBInJumpTable(i, Old, New);
  return MadeChange;
}

/// If Old is a target of the jump tables, update the jump table to branch to
/// New instead.
bool MachineJumpTableInfo::ReplaceMBBInJumpTable(unsigned Idx,
                                                 MachineBasicBlock *Old,
                                                 MachineBasicBlock *New) {
  assert(Old != New && "Not making a change?");
  bool MadeChange = false;
  MachineJumpTableEntry &JTE = JumpTables[Idx];
  for (size_t j = 0, e = JTE.MBBs.size(); j != e; ++j)
    if (JTE.MBBs[j] == Old) {
      JTE.MBBs[j] = New;
      MadeChange = true;
    }
  return MadeChange;
}

void MachineJumpTableInfo::print(raw_ostream &OS) const {
  if (JumpTables.empty()) return;

  OS << "Jump Tables:\n";

  for (unsigned i = 0, e = JumpTables.size(); i != e; ++i) {
    OS << printJumpTableEntryReference(i) << ": ";
    for (unsigned j = 0, f = JumpTables[i].MBBs.size(); j != f; ++j)
      OS << ' ' << printMBBReference(*JumpTables[i].MBBs[j]);
  }

  OS << '\n';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineJumpTableInfo::dump() const { print(dbgs()); }
#endif

Printable llvm::printJumpTableEntryReference(unsigned Idx) {
  return Printable([Idx](raw_ostream &OS) { OS << "%jump-table." << Idx; });
}

//===----------------------------------------------------------------------===//
//  MachineConstantPool implementation
//===----------------------------------------------------------------------===//

void MachineConstantPoolValue::anchor() {}

Type *MachineConstantPoolEntry::getType() const {
  if (isMachineConstantPoolEntry())
    return Val.MachineCPVal->getType();
  return Val.ConstVal->getType();
}

bool MachineConstantPoolEntry::needsRelocation() const {
  if (isMachineConstantPoolEntry())
    return true;
  return Val.ConstVal->needsRelocation();
}

SectionKind
MachineConstantPoolEntry::getSectionKind(const DataLayout *DL) const {
  if (needsRelocation())
    return SectionKind::getReadOnlyWithRel();
  switch (DL->getTypeAllocSize(getType())) {
  case 4:
    return SectionKind::getMergeableConst4();
  case 8:
    return SectionKind::getMergeableConst8();
  case 16:
    return SectionKind::getMergeableConst16();
  case 32:
    return SectionKind::getMergeableConst32();
  default:
    return SectionKind::getReadOnly();
  }
}

MachineConstantPool::~MachineConstantPool() {
  // A constant may be a member of both Constants and MachineCPVsSharingEntries,
  // so keep track of which we've deleted to avoid double deletions.
  DenseSet<MachineConstantPoolValue*> Deleted;
  for (unsigned i = 0, e = Constants.size(); i != e; ++i)
    if (Constants[i].isMachineConstantPoolEntry()) {
      Deleted.insert(Constants[i].Val.MachineCPVal);
      delete Constants[i].Val.MachineCPVal;
    }
  for (DenseSet<MachineConstantPoolValue*>::iterator I =
       MachineCPVsSharingEntries.begin(), E = MachineCPVsSharingEntries.end();
       I != E; ++I) {
    if (Deleted.count(*I) == 0)
      delete *I;
  }
}

/// Test whether the given two constants can be allocated the same constant pool
/// entry.
static bool CanShareConstantPoolEntry(const Constant *A, const Constant *B,
                                      const DataLayout &DL) {
  // Handle the trivial case quickly.
  if (A == B) return true;

  // If they have the same type but weren't the same constant, quickly
  // reject them.
  if (A->getType() == B->getType()) return false;

  // We can't handle structs or arrays.
  if (isa<StructType>(A->getType()) || isa<ArrayType>(A->getType()) ||
      isa<StructType>(B->getType()) || isa<ArrayType>(B->getType()))
    return false;

  // For now, only support constants with the same size.
  uint64_t StoreSize = DL.getTypeStoreSize(A->getType());
  if (StoreSize != DL.getTypeStoreSize(B->getType()) || StoreSize > 128)
    return false;

  Type *IntTy = IntegerType::get(A->getContext(), StoreSize*8);

  // Try constant folding a bitcast of both instructions to an integer.  If we
  // get two identical ConstantInt's, then we are good to share them.  We use
  // the constant folding APIs to do this so that we get the benefit of
  // DataLayout.
  if (isa<PointerType>(A->getType()))
    A = ConstantFoldCastOperand(Instruction::PtrToInt,
                                const_cast<Constant *>(A), IntTy, DL);
  else if (A->getType() != IntTy)
    A = ConstantFoldCastOperand(Instruction::BitCast, const_cast<Constant *>(A),
                                IntTy, DL);
  if (isa<PointerType>(B->getType()))
    B = ConstantFoldCastOperand(Instruction::PtrToInt,
                                const_cast<Constant *>(B), IntTy, DL);
  else if (B->getType() != IntTy)
    B = ConstantFoldCastOperand(Instruction::BitCast, const_cast<Constant *>(B),
                                IntTy, DL);

  return A == B;
}

/// Create a new entry in the constant pool or return an existing one.
/// User must specify the log2 of the minimum required alignment for the object.
unsigned MachineConstantPool::getConstantPoolIndex(const Constant *C,
                                                   unsigned Alignment) {
  assert(Alignment && "Alignment must be specified!");
  if (Alignment > PoolAlignment) PoolAlignment = Alignment;

  // Check to see if we already have this constant.
  //
  // FIXME, this could be made much more efficient for large constant pools.
  for (unsigned i = 0, e = Constants.size(); i != e; ++i)
    if (!Constants[i].isMachineConstantPoolEntry() &&
        CanShareConstantPoolEntry(Constants[i].Val.ConstVal, C, DL)) {
      if ((unsigned)Constants[i].getAlignment() < Alignment)
        Constants[i].Alignment = Alignment;
      return i;
    }

  Constants.push_back(MachineConstantPoolEntry(C, Alignment));
  return Constants.size()-1;
}

unsigned MachineConstantPool::getConstantPoolIndex(MachineConstantPoolValue *V,
                                                   unsigned Alignment) {
  assert(Alignment && "Alignment must be specified!");
  if (Alignment > PoolAlignment) PoolAlignment = Alignment;

  // Check to see if we already have this constant.
  //
  // FIXME, this could be made much more efficient for large constant pools.
  int Idx = V->getExistingMachineCPValue(this, Alignment);
  if (Idx != -1) {
    MachineCPVsSharingEntries.insert(V);
    return (unsigned)Idx;
  }

  Constants.push_back(MachineConstantPoolEntry(V, Alignment));
  return Constants.size()-1;
}

void MachineConstantPool::print(raw_ostream &OS) const {
  if (Constants.empty()) return;

  OS << "Constant Pool:\n";
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    OS << "  cp#" << i << ": ";
    if (Constants[i].isMachineConstantPoolEntry())
      Constants[i].Val.MachineCPVal->print(OS);
    else
      Constants[i].Val.ConstVal->printAsOperand(OS, /*PrintType=*/false);
    OS << ", align=" << Constants[i].getAlignment();
    OS << "\n";
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineConstantPool::dump() const { print(dbgs()); }
#endif
