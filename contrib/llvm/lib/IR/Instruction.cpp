//===-- Instruction.cpp - Implement the Instruction class -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Instruction class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
using namespace llvm;

Instruction::Instruction(Type *ty, unsigned it, Use *Ops, unsigned NumOps,
                         Instruction *InsertBefore)
  : User(ty, Value::InstructionVal + it, Ops, NumOps), Parent(nullptr) {

  // If requested, insert this instruction into a basic block...
  if (InsertBefore) {
    BasicBlock *BB = InsertBefore->getParent();
    assert(BB && "Instruction to insert before is not in a basic block!");
    BB->getInstList().insert(InsertBefore->getIterator(), this);
  }
}

Instruction::Instruction(Type *ty, unsigned it, Use *Ops, unsigned NumOps,
                         BasicBlock *InsertAtEnd)
  : User(ty, Value::InstructionVal + it, Ops, NumOps), Parent(nullptr) {

  // append this instruction into the basic block
  assert(InsertAtEnd && "Basic block to append to may not be NULL!");
  InsertAtEnd->getInstList().push_back(this);
}

Instruction::~Instruction() {
  assert(!Parent && "Instruction still linked in the program!");
  if (hasMetadataHashEntry())
    clearMetadataHashEntries();
}


void Instruction::setParent(BasicBlock *P) {
  Parent = P;
}

const Module *Instruction::getModule() const {
  return getParent()->getModule();
}

const Function *Instruction::getFunction() const {
  return getParent()->getParent();
}

void Instruction::removeFromParent() {
  getParent()->getInstList().remove(getIterator());
}

iplist<Instruction>::iterator Instruction::eraseFromParent() {
  return getParent()->getInstList().erase(getIterator());
}

/// Insert an unlinked instruction into a basic block immediately before the
/// specified instruction.
void Instruction::insertBefore(Instruction *InsertPos) {
  InsertPos->getParent()->getInstList().insert(InsertPos->getIterator(), this);
}

/// Insert an unlinked instruction into a basic block immediately after the
/// specified instruction.
void Instruction::insertAfter(Instruction *InsertPos) {
  InsertPos->getParent()->getInstList().insertAfter(InsertPos->getIterator(),
                                                    this);
}

/// Unlink this instruction from its current basic block and insert it into the
/// basic block that MovePos lives in, right before MovePos.
void Instruction::moveBefore(Instruction *MovePos) {
  moveBefore(*MovePos->getParent(), MovePos->getIterator());
}

void Instruction::moveAfter(Instruction *MovePos) {
  moveBefore(*MovePos->getParent(), ++MovePos->getIterator());
}

void Instruction::moveBefore(BasicBlock &BB,
                             SymbolTableList<Instruction>::iterator I) {
  assert(I == BB.end() || I->getParent() == &BB);
  BB.getInstList().splice(I, getParent()->getInstList(), getIterator());
}

void Instruction::setHasNoUnsignedWrap(bool b) {
  cast<OverflowingBinaryOperator>(this)->setHasNoUnsignedWrap(b);
}

void Instruction::setHasNoSignedWrap(bool b) {
  cast<OverflowingBinaryOperator>(this)->setHasNoSignedWrap(b);
}

void Instruction::setIsExact(bool b) {
  cast<PossiblyExactOperator>(this)->setIsExact(b);
}

bool Instruction::hasNoUnsignedWrap() const {
  return cast<OverflowingBinaryOperator>(this)->hasNoUnsignedWrap();
}

bool Instruction::hasNoSignedWrap() const {
  return cast<OverflowingBinaryOperator>(this)->hasNoSignedWrap();
}

void Instruction::dropPoisonGeneratingFlags() {
  switch (getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Shl:
    cast<OverflowingBinaryOperator>(this)->setHasNoUnsignedWrap(false);
    cast<OverflowingBinaryOperator>(this)->setHasNoSignedWrap(false);
    break;

  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::AShr:
  case Instruction::LShr:
    cast<PossiblyExactOperator>(this)->setIsExact(false);
    break;

  case Instruction::GetElementPtr:
    cast<GetElementPtrInst>(this)->setIsInBounds(false);
    break;
  }
}

bool Instruction::isExact() const {
  return cast<PossiblyExactOperator>(this)->isExact();
}

void Instruction::setFast(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setFast(B);
}

void Instruction::setHasAllowReassoc(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasAllowReassoc(B);
}

void Instruction::setHasNoNaNs(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasNoNaNs(B);
}

void Instruction::setHasNoInfs(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasNoInfs(B);
}

void Instruction::setHasNoSignedZeros(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasNoSignedZeros(B);
}

void Instruction::setHasAllowReciprocal(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasAllowReciprocal(B);
}

void Instruction::setHasApproxFunc(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasApproxFunc(B);
}

void Instruction::setFastMathFlags(FastMathFlags FMF) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setFastMathFlags(FMF);
}

void Instruction::copyFastMathFlags(FastMathFlags FMF) {
  assert(isa<FPMathOperator>(this) && "copying fast-math flag on invalid op");
  cast<FPMathOperator>(this)->copyFastMathFlags(FMF);
}

bool Instruction::isFast() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->isFast();
}

bool Instruction::hasAllowReassoc() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasAllowReassoc();
}

bool Instruction::hasNoNaNs() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasNoNaNs();
}

bool Instruction::hasNoInfs() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasNoInfs();
}

bool Instruction::hasNoSignedZeros() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasNoSignedZeros();
}

bool Instruction::hasAllowReciprocal() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasAllowReciprocal();
}

bool Instruction::hasAllowContract() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasAllowContract();
}

bool Instruction::hasApproxFunc() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasApproxFunc();
}

FastMathFlags Instruction::getFastMathFlags() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->getFastMathFlags();
}

void Instruction::copyFastMathFlags(const Instruction *I) {
  copyFastMathFlags(I->getFastMathFlags());
}

void Instruction::copyIRFlags(const Value *V, bool IncludeWrapFlags) {
  // Copy the wrapping flags.
  if (IncludeWrapFlags && isa<OverflowingBinaryOperator>(this)) {
    if (auto *OB = dyn_cast<OverflowingBinaryOperator>(V)) {
      setHasNoSignedWrap(OB->hasNoSignedWrap());
      setHasNoUnsignedWrap(OB->hasNoUnsignedWrap());
    }
  }

  // Copy the exact flag.
  if (auto *PE = dyn_cast<PossiblyExactOperator>(V))
    if (isa<PossiblyExactOperator>(this))
      setIsExact(PE->isExact());

  // Copy the fast-math flags.
  if (auto *FP = dyn_cast<FPMathOperator>(V))
    if (isa<FPMathOperator>(this))
      copyFastMathFlags(FP->getFastMathFlags());

  if (auto *SrcGEP = dyn_cast<GetElementPtrInst>(V))
    if (auto *DestGEP = dyn_cast<GetElementPtrInst>(this))
      DestGEP->setIsInBounds(SrcGEP->isInBounds() | DestGEP->isInBounds());
}

void Instruction::andIRFlags(const Value *V) {
  if (auto *OB = dyn_cast<OverflowingBinaryOperator>(V)) {
    if (isa<OverflowingBinaryOperator>(this)) {
      setHasNoSignedWrap(hasNoSignedWrap() & OB->hasNoSignedWrap());
      setHasNoUnsignedWrap(hasNoUnsignedWrap() & OB->hasNoUnsignedWrap());
    }
  }

  if (auto *PE = dyn_cast<PossiblyExactOperator>(V))
    if (isa<PossiblyExactOperator>(this))
      setIsExact(isExact() & PE->isExact());

  if (auto *FP = dyn_cast<FPMathOperator>(V)) {
    if (isa<FPMathOperator>(this)) {
      FastMathFlags FM = getFastMathFlags();
      FM &= FP->getFastMathFlags();
      copyFastMathFlags(FM);
    }
  }

  if (auto *SrcGEP = dyn_cast<GetElementPtrInst>(V))
    if (auto *DestGEP = dyn_cast<GetElementPtrInst>(this))
      DestGEP->setIsInBounds(SrcGEP->isInBounds() & DestGEP->isInBounds());
}

const char *Instruction::getOpcodeName(unsigned OpCode) {
  switch (OpCode) {
  // Terminators
  case Ret:    return "ret";
  case Br:     return "br";
  case Switch: return "switch";
  case IndirectBr: return "indirectbr";
  case Invoke: return "invoke";
  case Resume: return "resume";
  case Unreachable: return "unreachable";
  case CleanupRet: return "cleanupret";
  case CatchRet: return "catchret";
  case CatchPad: return "catchpad";
  case CatchSwitch: return "catchswitch";

  // Standard unary operators...
  case FNeg: return "fneg";

  // Standard binary operators...
  case Add: return "add";
  case FAdd: return "fadd";
  case Sub: return "sub";
  case FSub: return "fsub";
  case Mul: return "mul";
  case FMul: return "fmul";
  case UDiv: return "udiv";
  case SDiv: return "sdiv";
  case FDiv: return "fdiv";
  case URem: return "urem";
  case SRem: return "srem";
  case FRem: return "frem";

  // Logical operators...
  case And: return "and";
  case Or : return "or";
  case Xor: return "xor";

  // Memory instructions...
  case Alloca:        return "alloca";
  case Load:          return "load";
  case Store:         return "store";
  case AtomicCmpXchg: return "cmpxchg";
  case AtomicRMW:     return "atomicrmw";
  case Fence:         return "fence";
  case GetElementPtr: return "getelementptr";

  // Convert instructions...
  case Trunc:         return "trunc";
  case ZExt:          return "zext";
  case SExt:          return "sext";
  case FPTrunc:       return "fptrunc";
  case FPExt:         return "fpext";
  case FPToUI:        return "fptoui";
  case FPToSI:        return "fptosi";
  case UIToFP:        return "uitofp";
  case SIToFP:        return "sitofp";
  case IntToPtr:      return "inttoptr";
  case PtrToInt:      return "ptrtoint";
  case BitCast:       return "bitcast";
  case AddrSpaceCast: return "addrspacecast";

  // Other instructions...
  case ICmp:           return "icmp";
  case FCmp:           return "fcmp";
  case PHI:            return "phi";
  case Select:         return "select";
  case Call:           return "call";
  case Shl:            return "shl";
  case LShr:           return "lshr";
  case AShr:           return "ashr";
  case VAArg:          return "va_arg";
  case ExtractElement: return "extractelement";
  case InsertElement:  return "insertelement";
  case ShuffleVector:  return "shufflevector";
  case ExtractValue:   return "extractvalue";
  case InsertValue:    return "insertvalue";
  case LandingPad:     return "landingpad";
  case CleanupPad:     return "cleanuppad";

  default: return "<Invalid operator> ";
  }
}

/// Return true if both instructions have the same special state. This must be
/// kept in sync with FunctionComparator::cmpOperations in
/// lib/Transforms/IPO/MergeFunctions.cpp.
static bool haveSameSpecialState(const Instruction *I1, const Instruction *I2,
                                 bool IgnoreAlignment = false) {
  assert(I1->getOpcode() == I2->getOpcode() &&
         "Can not compare special state of different instructions");

  if (const AllocaInst *AI = dyn_cast<AllocaInst>(I1))
    return AI->getAllocatedType() == cast<AllocaInst>(I2)->getAllocatedType() &&
           (AI->getAlignment() == cast<AllocaInst>(I2)->getAlignment() ||
            IgnoreAlignment);
  if (const LoadInst *LI = dyn_cast<LoadInst>(I1))
    return LI->isVolatile() == cast<LoadInst>(I2)->isVolatile() &&
           (LI->getAlignment() == cast<LoadInst>(I2)->getAlignment() ||
            IgnoreAlignment) &&
           LI->getOrdering() == cast<LoadInst>(I2)->getOrdering() &&
           LI->getSyncScopeID() == cast<LoadInst>(I2)->getSyncScopeID();
  if (const StoreInst *SI = dyn_cast<StoreInst>(I1))
    return SI->isVolatile() == cast<StoreInst>(I2)->isVolatile() &&
           (SI->getAlignment() == cast<StoreInst>(I2)->getAlignment() ||
            IgnoreAlignment) &&
           SI->getOrdering() == cast<StoreInst>(I2)->getOrdering() &&
           SI->getSyncScopeID() == cast<StoreInst>(I2)->getSyncScopeID();
  if (const CmpInst *CI = dyn_cast<CmpInst>(I1))
    return CI->getPredicate() == cast<CmpInst>(I2)->getPredicate();
  if (const CallInst *CI = dyn_cast<CallInst>(I1))
    return CI->isTailCall() == cast<CallInst>(I2)->isTailCall() &&
           CI->getCallingConv() == cast<CallInst>(I2)->getCallingConv() &&
           CI->getAttributes() == cast<CallInst>(I2)->getAttributes() &&
           CI->hasIdenticalOperandBundleSchema(*cast<CallInst>(I2));
  if (const InvokeInst *CI = dyn_cast<InvokeInst>(I1))
    return CI->getCallingConv() == cast<InvokeInst>(I2)->getCallingConv() &&
           CI->getAttributes() == cast<InvokeInst>(I2)->getAttributes() &&
           CI->hasIdenticalOperandBundleSchema(*cast<InvokeInst>(I2));
  if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(I1))
    return IVI->getIndices() == cast<InsertValueInst>(I2)->getIndices();
  if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(I1))
    return EVI->getIndices() == cast<ExtractValueInst>(I2)->getIndices();
  if (const FenceInst *FI = dyn_cast<FenceInst>(I1))
    return FI->getOrdering() == cast<FenceInst>(I2)->getOrdering() &&
           FI->getSyncScopeID() == cast<FenceInst>(I2)->getSyncScopeID();
  if (const AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(I1))
    return CXI->isVolatile() == cast<AtomicCmpXchgInst>(I2)->isVolatile() &&
           CXI->isWeak() == cast<AtomicCmpXchgInst>(I2)->isWeak() &&
           CXI->getSuccessOrdering() ==
               cast<AtomicCmpXchgInst>(I2)->getSuccessOrdering() &&
           CXI->getFailureOrdering() ==
               cast<AtomicCmpXchgInst>(I2)->getFailureOrdering() &&
           CXI->getSyncScopeID() ==
               cast<AtomicCmpXchgInst>(I2)->getSyncScopeID();
  if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I1))
    return RMWI->getOperation() == cast<AtomicRMWInst>(I2)->getOperation() &&
           RMWI->isVolatile() == cast<AtomicRMWInst>(I2)->isVolatile() &&
           RMWI->getOrdering() == cast<AtomicRMWInst>(I2)->getOrdering() &&
           RMWI->getSyncScopeID() == cast<AtomicRMWInst>(I2)->getSyncScopeID();

  return true;
}

bool Instruction::isIdenticalTo(const Instruction *I) const {
  return isIdenticalToWhenDefined(I) &&
         SubclassOptionalData == I->SubclassOptionalData;
}

bool Instruction::isIdenticalToWhenDefined(const Instruction *I) const {
  if (getOpcode() != I->getOpcode() ||
      getNumOperands() != I->getNumOperands() ||
      getType() != I->getType())
    return false;

  // If both instructions have no operands, they are identical.
  if (getNumOperands() == 0 && I->getNumOperands() == 0)
    return haveSameSpecialState(this, I);

  // We have two instructions of identical opcode and #operands.  Check to see
  // if all operands are the same.
  if (!std::equal(op_begin(), op_end(), I->op_begin()))
    return false;

  if (const PHINode *thisPHI = dyn_cast<PHINode>(this)) {
    const PHINode *otherPHI = cast<PHINode>(I);
    return std::equal(thisPHI->block_begin(), thisPHI->block_end(),
                      otherPHI->block_begin());
  }

  return haveSameSpecialState(this, I);
}

// Keep this in sync with FunctionComparator::cmpOperations in
// lib/Transforms/IPO/MergeFunctions.cpp.
bool Instruction::isSameOperationAs(const Instruction *I,
                                    unsigned flags) const {
  bool IgnoreAlignment = flags & CompareIgnoringAlignment;
  bool UseScalarTypes  = flags & CompareUsingScalarTypes;

  if (getOpcode() != I->getOpcode() ||
      getNumOperands() != I->getNumOperands() ||
      (UseScalarTypes ?
       getType()->getScalarType() != I->getType()->getScalarType() :
       getType() != I->getType()))
    return false;

  // We have two instructions of identical opcode and #operands.  Check to see
  // if all operands are the same type
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
    if (UseScalarTypes ?
        getOperand(i)->getType()->getScalarType() !=
          I->getOperand(i)->getType()->getScalarType() :
        getOperand(i)->getType() != I->getOperand(i)->getType())
      return false;

  return haveSameSpecialState(this, I, IgnoreAlignment);
}

bool Instruction::isUsedOutsideOfBlock(const BasicBlock *BB) const {
  for (const Use &U : uses()) {
    // PHI nodes uses values in the corresponding predecessor block.  For other
    // instructions, just check to see whether the parent of the use matches up.
    const Instruction *I = cast<Instruction>(U.getUser());
    const PHINode *PN = dyn_cast<PHINode>(I);
    if (!PN) {
      if (I->getParent() != BB)
        return true;
      continue;
    }

    if (PN->getIncomingBlock(U) != BB)
      return true;
  }
  return false;
}

bool Instruction::mayReadFromMemory() const {
  switch (getOpcode()) {
  default: return false;
  case Instruction::VAArg:
  case Instruction::Load:
  case Instruction::Fence: // FIXME: refine definition of mayReadFromMemory
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
    return true;
  case Instruction::Call:
    return !cast<CallInst>(this)->doesNotAccessMemory();
  case Instruction::Invoke:
    return !cast<InvokeInst>(this)->doesNotAccessMemory();
  case Instruction::Store:
    return !cast<StoreInst>(this)->isUnordered();
  }
}

bool Instruction::mayWriteToMemory() const {
  switch (getOpcode()) {
  default: return false;
  case Instruction::Fence: // FIXME: refine definition of mayWriteToMemory
  case Instruction::Store:
  case Instruction::VAArg:
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
    return true;
  case Instruction::Call:
    return !cast<CallInst>(this)->onlyReadsMemory();
  case Instruction::Invoke:
    return !cast<InvokeInst>(this)->onlyReadsMemory();
  case Instruction::Load:
    return !cast<LoadInst>(this)->isUnordered();
  }
}

bool Instruction::isAtomic() const {
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::Fence:
    return true;
  case Instruction::Load:
    return cast<LoadInst>(this)->getOrdering() != AtomicOrdering::NotAtomic;
  case Instruction::Store:
    return cast<StoreInst>(this)->getOrdering() != AtomicOrdering::NotAtomic;
  }
}

bool Instruction::hasAtomicLoad() const {
  assert(isAtomic());
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::Load:
    return true;
  }
}

bool Instruction::hasAtomicStore() const {
  assert(isAtomic());
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::Store:
    return true;
  }
}

bool Instruction::mayThrow() const {
  if (const CallInst *CI = dyn_cast<CallInst>(this))
    return !CI->doesNotThrow();
  if (const auto *CRI = dyn_cast<CleanupReturnInst>(this))
    return CRI->unwindsToCaller();
  if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(this))
    return CatchSwitch->unwindsToCaller();
  return isa<ResumeInst>(this);
}

bool Instruction::isSafeToRemove() const {
  return (!isa<CallInst>(this) || !this->mayHaveSideEffects()) &&
         !this->isTerminator();
}

bool Instruction::isLifetimeStartOrEnd() const {
  auto II = dyn_cast<IntrinsicInst>(this);
  if (!II)
    return false;
  Intrinsic::ID ID = II->getIntrinsicID();
  return ID == Intrinsic::lifetime_start || ID == Intrinsic::lifetime_end;
}

const Instruction *Instruction::getNextNonDebugInstruction() const {
  for (const Instruction *I = getNextNode(); I; I = I->getNextNode())
    if (!isa<DbgInfoIntrinsic>(I))
      return I;
  return nullptr;
}

const Instruction *Instruction::getPrevNonDebugInstruction() const {
  for (const Instruction *I = getPrevNode(); I; I = I->getPrevNode())
    if (!isa<DbgInfoIntrinsic>(I))
      return I;
  return nullptr;
}

bool Instruction::isAssociative() const {
  unsigned Opcode = getOpcode();
  if (isAssociative(Opcode))
    return true;

  switch (Opcode) {
  case FMul:
  case FAdd:
    return cast<FPMathOperator>(this)->hasAllowReassoc() &&
           cast<FPMathOperator>(this)->hasNoSignedZeros();
  default:
    return false;
  }
}

unsigned Instruction::getNumSuccessors() const {
  switch (getOpcode()) {
#define HANDLE_TERM_INST(N, OPC, CLASS)                                        \
  case Instruction::OPC:                                                       \
    return static_cast<const CLASS *>(this)->getNumSuccessors();
#include "llvm/IR/Instruction.def"
  default:
    break;
  }
  llvm_unreachable("not a terminator");
}

BasicBlock *Instruction::getSuccessor(unsigned idx) const {
  switch (getOpcode()) {
#define HANDLE_TERM_INST(N, OPC, CLASS)                                        \
  case Instruction::OPC:                                                       \
    return static_cast<const CLASS *>(this)->getSuccessor(idx);
#include "llvm/IR/Instruction.def"
  default:
    break;
  }
  llvm_unreachable("not a terminator");
}

void Instruction::setSuccessor(unsigned idx, BasicBlock *B) {
  switch (getOpcode()) {
#define HANDLE_TERM_INST(N, OPC, CLASS)                                        \
  case Instruction::OPC:                                                       \
    return static_cast<CLASS *>(this)->setSuccessor(idx, B);
#include "llvm/IR/Instruction.def"
  default:
    break;
  }
  llvm_unreachable("not a terminator");
}

Instruction *Instruction::cloneImpl() const {
  llvm_unreachable("Subclass of Instruction failed to implement cloneImpl");
}

void Instruction::swapProfMetadata() {
  MDNode *ProfileData = getMetadata(LLVMContext::MD_prof);
  if (!ProfileData || ProfileData->getNumOperands() != 3 ||
      !isa<MDString>(ProfileData->getOperand(0)))
    return;

  MDString *MDName = cast<MDString>(ProfileData->getOperand(0));
  if (MDName->getString() != "branch_weights")
    return;

  // The first operand is the name. Fetch them backwards and build a new one.
  Metadata *Ops[] = {ProfileData->getOperand(0), ProfileData->getOperand(2),
                     ProfileData->getOperand(1)};
  setMetadata(LLVMContext::MD_prof,
              MDNode::get(ProfileData->getContext(), Ops));
}

void Instruction::copyMetadata(const Instruction &SrcInst,
                               ArrayRef<unsigned> WL) {
  if (!SrcInst.hasMetadata())
    return;

  DenseSet<unsigned> WLS;
  for (unsigned M : WL)
    WLS.insert(M);

  // Otherwise, enumerate and copy over metadata from the old instruction to the
  // new one.
  SmallVector<std::pair<unsigned, MDNode *>, 4> TheMDs;
  SrcInst.getAllMetadataOtherThanDebugLoc(TheMDs);
  for (const auto &MD : TheMDs) {
    if (WL.empty() || WLS.count(MD.first))
      setMetadata(MD.first, MD.second);
  }
  if (WL.empty() || WLS.count(LLVMContext::MD_dbg))
    setDebugLoc(SrcInst.getDebugLoc());
}

Instruction *Instruction::clone() const {
  Instruction *New = nullptr;
  switch (getOpcode()) {
  default:
    llvm_unreachable("Unhandled Opcode.");
#define HANDLE_INST(num, opc, clas)                                            \
  case Instruction::opc:                                                       \
    New = cast<clas>(this)->cloneImpl();                                       \
    break;
#include "llvm/IR/Instruction.def"
#undef HANDLE_INST
  }

  New->SubclassOptionalData = SubclassOptionalData;
  New->copyMetadata(*this);
  return New;
}

void Instruction::updateProfWeight(uint64_t S, uint64_t T) {
  auto *ProfileData = getMetadata(LLVMContext::MD_prof);
  if (ProfileData == nullptr)
    return;

  auto *ProfDataName = dyn_cast<MDString>(ProfileData->getOperand(0));
  if (!ProfDataName || (!ProfDataName->getString().equals("branch_weights") &&
                        !ProfDataName->getString().equals("VP")))
    return;

  MDBuilder MDB(getContext());
  SmallVector<Metadata *, 3> Vals;
  Vals.push_back(ProfileData->getOperand(0));
  APInt APS(128, S), APT(128, T);
  if (ProfDataName->getString().equals("branch_weights"))
    for (unsigned i = 1; i < ProfileData->getNumOperands(); i++) {
      // Using APInt::div may be expensive, but most cases should fit 64 bits.
      APInt Val(128,
                mdconst::dyn_extract<ConstantInt>(ProfileData->getOperand(i))
                    ->getValue()
                    .getZExtValue());
      Val *= APS;
      Vals.push_back(MDB.createConstant(
          ConstantInt::get(Type::getInt64Ty(getContext()),
                           Val.udiv(APT).getLimitedValue())));
    }
  else if (ProfDataName->getString().equals("VP"))
    for (unsigned i = 1; i < ProfileData->getNumOperands(); i += 2) {
      // The first value is the key of the value profile, which will not change.
      Vals.push_back(ProfileData->getOperand(i));
      // Using APInt::div may be expensive, but most cases should fit 64 bits.
      APInt Val(128,
                mdconst::dyn_extract<ConstantInt>(ProfileData->getOperand(i + 1))
                    ->getValue()
                    .getZExtValue());
      Val *= APS;
      Vals.push_back(MDB.createConstant(
          ConstantInt::get(Type::getInt64Ty(getContext()),
                           Val.udiv(APT).getLimitedValue())));
    }
  setMetadata(LLVMContext::MD_prof, MDNode::get(getContext(), Vals));
}

void Instruction::setProfWeight(uint64_t W) {
  assert((isa<CallInst>(this) || isa<InvokeInst>(this)) &&
         "Can only set weights for call and invoke instrucitons");
  SmallVector<uint32_t, 1> Weights;
  Weights.push_back(W);
  MDBuilder MDB(getContext());
  setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(Weights));
}
