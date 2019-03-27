//===-- Verifier.cpp - Implement the Module Verifier -----------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the function verifier interface, that can be used for some
// sanity checking of input to the system.
//
// Note that this does not provide full `Java style' security and verifications,
// instead it just tries to ensure that code is well-formed.
//
//  * Both of a binary operator's parameters are of the same type
//  * Verify that the indices of mem access instructions match other operands
//  * Verify that arithmetic and other things are only performed on first-class
//    types.  Verify that shifts & logicals only happen on integrals f.e.
//  * All of the constants in a switch statement are of the correct type
//  * The code is in valid SSA form
//  * It should be illegal to put a label into any other type (like a structure)
//    or to return one. [except constant arrays!]
//  * Only phi nodes can be self referential: 'add i32 %0, %0 ; <int>:0' is bad
//  * PHI nodes must have an entry for each predecessor, with no extras.
//  * PHI nodes must be the first thing in a basic block, all grouped together
//  * PHI nodes must have at least one entry
//  * All basic blocks should only end with terminator insts, not contain them
//  * The entry node to a function must not have predecessors
//  * All Instructions must be embedded into a basic block
//  * Functions cannot take a void-typed parameter
//  * Verify that a function's argument list agrees with it's declared type.
//  * It is illegal to specify a name for a void value.
//  * It is illegal to have a internal global value with no initializer
//  * It is illegal to have a ret instruction that returns a value that does not
//    agree with the function return value type.
//  * Function call argument types match the function prototype
//  * A landing pad is defined by a landingpad instruction, and can be jumped to
//    only by the unwind edge of an invoke instruction.
//  * A landingpad instruction must be the first non-PHI instruction in the
//    block.
//  * Landingpad instructions must be in a function with a personality function.
//  * All other things that are tested by asserts spread about the code...
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Verifier.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

using namespace llvm;

namespace llvm {

struct VerifierSupport {
  raw_ostream *OS;
  const Module &M;
  ModuleSlotTracker MST;
  const DataLayout &DL;
  LLVMContext &Context;

  /// Track the brokenness of the module while recursively visiting.
  bool Broken = false;
  /// Broken debug info can be "recovered" from by stripping the debug info.
  bool BrokenDebugInfo = false;
  /// Whether to treat broken debug info as an error.
  bool TreatBrokenDebugInfoAsError = true;

  explicit VerifierSupport(raw_ostream *OS, const Module &M)
      : OS(OS), M(M), MST(&M), DL(M.getDataLayout()), Context(M.getContext()) {}

private:
  void Write(const Module *M) {
    *OS << "; ModuleID = '" << M->getModuleIdentifier() << "'\n";
  }

  void Write(const Value *V) {
    if (V)
      Write(*V);
  }

  void Write(const Value &V) {
    if (isa<Instruction>(V)) {
      V.print(*OS, MST);
      *OS << '\n';
    } else {
      V.printAsOperand(*OS, true, MST);
      *OS << '\n';
    }
  }

  void Write(const Metadata *MD) {
    if (!MD)
      return;
    MD->print(*OS, MST, &M);
    *OS << '\n';
  }

  template <class T> void Write(const MDTupleTypedArrayWrapper<T> &MD) {
    Write(MD.get());
  }

  void Write(const NamedMDNode *NMD) {
    if (!NMD)
      return;
    NMD->print(*OS, MST);
    *OS << '\n';
  }

  void Write(Type *T) {
    if (!T)
      return;
    *OS << ' ' << *T;
  }

  void Write(const Comdat *C) {
    if (!C)
      return;
    *OS << *C;
  }

  void Write(const APInt *AI) {
    if (!AI)
      return;
    *OS << *AI << '\n';
  }

  void Write(const unsigned i) { *OS << i << '\n'; }

  template <typename T> void Write(ArrayRef<T> Vs) {
    for (const T &V : Vs)
      Write(V);
  }

  template <typename T1, typename... Ts>
  void WriteTs(const T1 &V1, const Ts &... Vs) {
    Write(V1);
    WriteTs(Vs...);
  }

  template <typename... Ts> void WriteTs() {}

public:
  /// A check failed, so printout out the condition and the message.
  ///
  /// This provides a nice place to put a breakpoint if you want to see why
  /// something is not correct.
  void CheckFailed(const Twine &Message) {
    if (OS)
      *OS << Message << '\n';
    Broken = true;
  }

  /// A check failed (with values to print).
  ///
  /// This calls the Message-only version so that the above is easier to set a
  /// breakpoint on.
  template <typename T1, typename... Ts>
  void CheckFailed(const Twine &Message, const T1 &V1, const Ts &... Vs) {
    CheckFailed(Message);
    if (OS)
      WriteTs(V1, Vs...);
  }

  /// A debug info check failed.
  void DebugInfoCheckFailed(const Twine &Message) {
    if (OS)
      *OS << Message << '\n';
    Broken |= TreatBrokenDebugInfoAsError;
    BrokenDebugInfo = true;
  }

  /// A debug info check failed (with values to print).
  template <typename T1, typename... Ts>
  void DebugInfoCheckFailed(const Twine &Message, const T1 &V1,
                            const Ts &... Vs) {
    DebugInfoCheckFailed(Message);
    if (OS)
      WriteTs(V1, Vs...);
  }
};

} // namespace llvm

namespace {

class Verifier : public InstVisitor<Verifier>, VerifierSupport {
  friend class InstVisitor<Verifier>;

  DominatorTree DT;

  /// When verifying a basic block, keep track of all of the
  /// instructions we have seen so far.
  ///
  /// This allows us to do efficient dominance checks for the case when an
  /// instruction has an operand that is an instruction in the same block.
  SmallPtrSet<Instruction *, 16> InstsInThisBlock;

  /// Keep track of the metadata nodes that have been checked already.
  SmallPtrSet<const Metadata *, 32> MDNodes;

  /// Keep track which DISubprogram is attached to which function.
  DenseMap<const DISubprogram *, const Function *> DISubprogramAttachments;

  /// Track all DICompileUnits visited.
  SmallPtrSet<const Metadata *, 2> CUVisited;

  /// The result type for a landingpad.
  Type *LandingPadResultTy;

  /// Whether we've seen a call to @llvm.localescape in this function
  /// already.
  bool SawFrameEscape;

  /// Whether the current function has a DISubprogram attached to it.
  bool HasDebugInfo = false;

  /// Whether source was present on the first DIFile encountered in each CU.
  DenseMap<const DICompileUnit *, bool> HasSourceDebugInfo;

  /// Stores the count of how many objects were passed to llvm.localescape for a
  /// given function and the largest index passed to llvm.localrecover.
  DenseMap<Function *, std::pair<unsigned, unsigned>> FrameEscapeInfo;

  // Maps catchswitches and cleanuppads that unwind to siblings to the
  // terminators that indicate the unwind, used to detect cycles therein.
  MapVector<Instruction *, Instruction *> SiblingFuncletInfo;

  /// Cache of constants visited in search of ConstantExprs.
  SmallPtrSet<const Constant *, 32> ConstantExprVisited;

  /// Cache of declarations of the llvm.experimental.deoptimize.<ty> intrinsic.
  SmallVector<const Function *, 4> DeoptimizeDeclarations;

  // Verify that this GlobalValue is only used in this module.
  // This map is used to avoid visiting uses twice. We can arrive at a user
  // twice, if they have multiple operands. In particular for very large
  // constant expressions, we can arrive at a particular user many times.
  SmallPtrSet<const Value *, 32> GlobalValueVisited;

  // Keeps track of duplicate function argument debug info.
  SmallVector<const DILocalVariable *, 16> DebugFnArgs;

  TBAAVerifier TBAAVerifyHelper;

  void checkAtomicMemAccessSize(Type *Ty, const Instruction *I);

public:
  explicit Verifier(raw_ostream *OS, bool ShouldTreatBrokenDebugInfoAsError,
                    const Module &M)
      : VerifierSupport(OS, M), LandingPadResultTy(nullptr),
        SawFrameEscape(false), TBAAVerifyHelper(this) {
    TreatBrokenDebugInfoAsError = ShouldTreatBrokenDebugInfoAsError;
  }

  bool hasBrokenDebugInfo() const { return BrokenDebugInfo; }

  bool verify(const Function &F) {
    assert(F.getParent() == &M &&
           "An instance of this class only works with a specific module!");

    // First ensure the function is well-enough formed to compute dominance
    // information, and directly compute a dominance tree. We don't rely on the
    // pass manager to provide this as it isolates us from a potentially
    // out-of-date dominator tree and makes it significantly more complex to run
    // this code outside of a pass manager.
    // FIXME: It's really gross that we have to cast away constness here.
    if (!F.empty())
      DT.recalculate(const_cast<Function &>(F));

    for (const BasicBlock &BB : F) {
      if (!BB.empty() && BB.back().isTerminator())
        continue;

      if (OS) {
        *OS << "Basic Block in function '" << F.getName()
            << "' does not have terminator!\n";
        BB.printAsOperand(*OS, true, MST);
        *OS << "\n";
      }
      return false;
    }

    Broken = false;
    // FIXME: We strip const here because the inst visitor strips const.
    visit(const_cast<Function &>(F));
    verifySiblingFuncletUnwinds();
    InstsInThisBlock.clear();
    DebugFnArgs.clear();
    LandingPadResultTy = nullptr;
    SawFrameEscape = false;
    SiblingFuncletInfo.clear();

    return !Broken;
  }

  /// Verify the module that this instance of \c Verifier was initialized with.
  bool verify() {
    Broken = false;

    // Collect all declarations of the llvm.experimental.deoptimize intrinsic.
    for (const Function &F : M)
      if (F.getIntrinsicID() == Intrinsic::experimental_deoptimize)
        DeoptimizeDeclarations.push_back(&F);

    // Now that we've visited every function, verify that we never asked to
    // recover a frame index that wasn't escaped.
    verifyFrameRecoverIndices();
    for (const GlobalVariable &GV : M.globals())
      visitGlobalVariable(GV);

    for (const GlobalAlias &GA : M.aliases())
      visitGlobalAlias(GA);

    for (const NamedMDNode &NMD : M.named_metadata())
      visitNamedMDNode(NMD);

    for (const StringMapEntry<Comdat> &SMEC : M.getComdatSymbolTable())
      visitComdat(SMEC.getValue());

    visitModuleFlags(M);
    visitModuleIdents(M);
    visitModuleCommandLines(M);

    verifyCompileUnits();

    verifyDeoptimizeCallingConvs();
    DISubprogramAttachments.clear();
    return !Broken;
  }

private:
  // Verification methods...
  void visitGlobalValue(const GlobalValue &GV);
  void visitGlobalVariable(const GlobalVariable &GV);
  void visitGlobalAlias(const GlobalAlias &GA);
  void visitAliaseeSubExpr(const GlobalAlias &A, const Constant &C);
  void visitAliaseeSubExpr(SmallPtrSetImpl<const GlobalAlias *> &Visited,
                           const GlobalAlias &A, const Constant &C);
  void visitNamedMDNode(const NamedMDNode &NMD);
  void visitMDNode(const MDNode &MD);
  void visitMetadataAsValue(const MetadataAsValue &MD, Function *F);
  void visitValueAsMetadata(const ValueAsMetadata &MD, Function *F);
  void visitComdat(const Comdat &C);
  void visitModuleIdents(const Module &M);
  void visitModuleCommandLines(const Module &M);
  void visitModuleFlags(const Module &M);
  void visitModuleFlag(const MDNode *Op,
                       DenseMap<const MDString *, const MDNode *> &SeenIDs,
                       SmallVectorImpl<const MDNode *> &Requirements);
  void visitModuleFlagCGProfileEntry(const MDOperand &MDO);
  void visitFunction(const Function &F);
  void visitBasicBlock(BasicBlock &BB);
  void visitRangeMetadata(Instruction &I, MDNode *Range, Type *Ty);
  void visitDereferenceableMetadata(Instruction &I, MDNode *MD);

  template <class Ty> bool isValidMetadataArray(const MDTuple &N);
#define HANDLE_SPECIALIZED_MDNODE_LEAF(CLASS) void visit##CLASS(const CLASS &N);
#include "llvm/IR/Metadata.def"
  void visitDIScope(const DIScope &N);
  void visitDIVariable(const DIVariable &N);
  void visitDILexicalBlockBase(const DILexicalBlockBase &N);
  void visitDITemplateParameter(const DITemplateParameter &N);

  void visitTemplateParams(const MDNode &N, const Metadata &RawParams);

  // InstVisitor overrides...
  using InstVisitor<Verifier>::visit;
  void visit(Instruction &I);

  void visitTruncInst(TruncInst &I);
  void visitZExtInst(ZExtInst &I);
  void visitSExtInst(SExtInst &I);
  void visitFPTruncInst(FPTruncInst &I);
  void visitFPExtInst(FPExtInst &I);
  void visitFPToUIInst(FPToUIInst &I);
  void visitFPToSIInst(FPToSIInst &I);
  void visitUIToFPInst(UIToFPInst &I);
  void visitSIToFPInst(SIToFPInst &I);
  void visitIntToPtrInst(IntToPtrInst &I);
  void visitPtrToIntInst(PtrToIntInst &I);
  void visitBitCastInst(BitCastInst &I);
  void visitAddrSpaceCastInst(AddrSpaceCastInst &I);
  void visitPHINode(PHINode &PN);
  void visitCallBase(CallBase &Call);
  void visitUnaryOperator(UnaryOperator &U);
  void visitBinaryOperator(BinaryOperator &B);
  void visitICmpInst(ICmpInst &IC);
  void visitFCmpInst(FCmpInst &FC);
  void visitExtractElementInst(ExtractElementInst &EI);
  void visitInsertElementInst(InsertElementInst &EI);
  void visitShuffleVectorInst(ShuffleVectorInst &EI);
  void visitVAArgInst(VAArgInst &VAA) { visitInstruction(VAA); }
  void visitCallInst(CallInst &CI);
  void visitInvokeInst(InvokeInst &II);
  void visitGetElementPtrInst(GetElementPtrInst &GEP);
  void visitLoadInst(LoadInst &LI);
  void visitStoreInst(StoreInst &SI);
  void verifyDominatesUse(Instruction &I, unsigned i);
  void visitInstruction(Instruction &I);
  void visitTerminator(Instruction &I);
  void visitBranchInst(BranchInst &BI);
  void visitReturnInst(ReturnInst &RI);
  void visitSwitchInst(SwitchInst &SI);
  void visitIndirectBrInst(IndirectBrInst &BI);
  void visitSelectInst(SelectInst &SI);
  void visitUserOp1(Instruction &I);
  void visitUserOp2(Instruction &I) { visitUserOp1(I); }
  void visitIntrinsicCall(Intrinsic::ID ID, CallBase &Call);
  void visitConstrainedFPIntrinsic(ConstrainedFPIntrinsic &FPI);
  void visitDbgIntrinsic(StringRef Kind, DbgVariableIntrinsic &DII);
  void visitDbgLabelIntrinsic(StringRef Kind, DbgLabelInst &DLI);
  void visitAtomicCmpXchgInst(AtomicCmpXchgInst &CXI);
  void visitAtomicRMWInst(AtomicRMWInst &RMWI);
  void visitFenceInst(FenceInst &FI);
  void visitAllocaInst(AllocaInst &AI);
  void visitExtractValueInst(ExtractValueInst &EVI);
  void visitInsertValueInst(InsertValueInst &IVI);
  void visitEHPadPredecessors(Instruction &I);
  void visitLandingPadInst(LandingPadInst &LPI);
  void visitResumeInst(ResumeInst &RI);
  void visitCatchPadInst(CatchPadInst &CPI);
  void visitCatchReturnInst(CatchReturnInst &CatchReturn);
  void visitCleanupPadInst(CleanupPadInst &CPI);
  void visitFuncletPadInst(FuncletPadInst &FPI);
  void visitCatchSwitchInst(CatchSwitchInst &CatchSwitch);
  void visitCleanupReturnInst(CleanupReturnInst &CRI);

  void verifySwiftErrorCall(CallBase &Call, const Value *SwiftErrorVal);
  void verifySwiftErrorValue(const Value *SwiftErrorVal);
  void verifyMustTailCall(CallInst &CI);
  bool performTypeCheck(Intrinsic::ID ID, Function *F, Type *Ty, int VT,
                        unsigned ArgNo, std::string &Suffix);
  bool verifyAttributeCount(AttributeList Attrs, unsigned Params);
  void verifyAttributeTypes(AttributeSet Attrs, bool IsFunction,
                            const Value *V);
  void verifyParameterAttrs(AttributeSet Attrs, Type *Ty, const Value *V);
  void verifyFunctionAttrs(FunctionType *FT, AttributeList Attrs,
                           const Value *V);
  void verifyFunctionMetadata(ArrayRef<std::pair<unsigned, MDNode *>> MDs);

  void visitConstantExprsRecursively(const Constant *EntryC);
  void visitConstantExpr(const ConstantExpr *CE);
  void verifyStatepoint(const CallBase &Call);
  void verifyFrameRecoverIndices();
  void verifySiblingFuncletUnwinds();

  void verifyFragmentExpression(const DbgVariableIntrinsic &I);
  template <typename ValueOrMetadata>
  void verifyFragmentExpression(const DIVariable &V,
                                DIExpression::FragmentInfo Fragment,
                                ValueOrMetadata *Desc);
  void verifyFnArgs(const DbgVariableIntrinsic &I);

  /// Module-level debug info verification...
  void verifyCompileUnits();

  /// Module-level verification that all @llvm.experimental.deoptimize
  /// declarations share the same calling convention.
  void verifyDeoptimizeCallingConvs();

  /// Verify all-or-nothing property of DIFile source attribute within a CU.
  void verifySourceDebugInfo(const DICompileUnit &U, const DIFile &F);
};

} // end anonymous namespace

/// We know that cond should be true, if not print an error message.
#define Assert(C, ...) \
  do { if (!(C)) { CheckFailed(__VA_ARGS__); return; } } while (false)

/// We know that a debug info condition should be true, if not print
/// an error message.
#define AssertDI(C, ...) \
  do { if (!(C)) { DebugInfoCheckFailed(__VA_ARGS__); return; } } while (false)

void Verifier::visit(Instruction &I) {
  for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i)
    Assert(I.getOperand(i) != nullptr, "Operand is null", &I);
  InstVisitor<Verifier>::visit(I);
}

// Helper to recursively iterate over indirect users. By
// returning false, the callback can ask to stop recursing
// further.
static void forEachUser(const Value *User,
                        SmallPtrSet<const Value *, 32> &Visited,
                        llvm::function_ref<bool(const Value *)> Callback) {
  if (!Visited.insert(User).second)
    return;
  for (const Value *TheNextUser : User->materialized_users())
    if (Callback(TheNextUser))
      forEachUser(TheNextUser, Visited, Callback);
}

void Verifier::visitGlobalValue(const GlobalValue &GV) {
  Assert(!GV.isDeclaration() || GV.hasValidDeclarationLinkage(),
         "Global is external, but doesn't have external or weak linkage!", &GV);

  Assert(GV.getAlignment() <= Value::MaximumAlignment,
         "huge alignment values are unsupported", &GV);
  Assert(!GV.hasAppendingLinkage() || isa<GlobalVariable>(GV),
         "Only global variables can have appending linkage!", &GV);

  if (GV.hasAppendingLinkage()) {
    const GlobalVariable *GVar = dyn_cast<GlobalVariable>(&GV);
    Assert(GVar && GVar->getValueType()->isArrayTy(),
           "Only global arrays can have appending linkage!", GVar);
  }

  if (GV.isDeclarationForLinker())
    Assert(!GV.hasComdat(), "Declaration may not be in a Comdat!", &GV);

  if (GV.hasDLLImportStorageClass()) {
    Assert(!GV.isDSOLocal(),
           "GlobalValue with DLLImport Storage is dso_local!", &GV);

    Assert((GV.isDeclaration() && GV.hasExternalLinkage()) ||
               GV.hasAvailableExternallyLinkage(),
           "Global is marked as dllimport, but not external", &GV);
  }

  if (GV.hasLocalLinkage())
    Assert(GV.isDSOLocal(),
           "GlobalValue with private or internal linkage must be dso_local!",
           &GV);

  if (!GV.hasDefaultVisibility() && !GV.hasExternalWeakLinkage())
    Assert(GV.isDSOLocal(),
           "GlobalValue with non default visibility must be dso_local!", &GV);

  forEachUser(&GV, GlobalValueVisited, [&](const Value *V) -> bool {
    if (const Instruction *I = dyn_cast<Instruction>(V)) {
      if (!I->getParent() || !I->getParent()->getParent())
        CheckFailed("Global is referenced by parentless instruction!", &GV, &M,
                    I);
      else if (I->getParent()->getParent()->getParent() != &M)
        CheckFailed("Global is referenced in a different module!", &GV, &M, I,
                    I->getParent()->getParent(),
                    I->getParent()->getParent()->getParent());
      return false;
    } else if (const Function *F = dyn_cast<Function>(V)) {
      if (F->getParent() != &M)
        CheckFailed("Global is used by function in a different module", &GV, &M,
                    F, F->getParent());
      return false;
    }
    return true;
  });
}

void Verifier::visitGlobalVariable(const GlobalVariable &GV) {
  if (GV.hasInitializer()) {
    Assert(GV.getInitializer()->getType() == GV.getValueType(),
           "Global variable initializer type does not match global "
           "variable type!",
           &GV);
    // If the global has common linkage, it must have a zero initializer and
    // cannot be constant.
    if (GV.hasCommonLinkage()) {
      Assert(GV.getInitializer()->isNullValue(),
             "'common' global must have a zero initializer!", &GV);
      Assert(!GV.isConstant(), "'common' global may not be marked constant!",
             &GV);
      Assert(!GV.hasComdat(), "'common' global may not be in a Comdat!", &GV);
    }
  }

  if (GV.hasName() && (GV.getName() == "llvm.global_ctors" ||
                       GV.getName() == "llvm.global_dtors")) {
    Assert(!GV.hasInitializer() || GV.hasAppendingLinkage(),
           "invalid linkage for intrinsic global variable", &GV);
    // Don't worry about emitting an error for it not being an array,
    // visitGlobalValue will complain on appending non-array.
    if (ArrayType *ATy = dyn_cast<ArrayType>(GV.getValueType())) {
      StructType *STy = dyn_cast<StructType>(ATy->getElementType());
      PointerType *FuncPtrTy =
          FunctionType::get(Type::getVoidTy(Context), false)->
          getPointerTo(DL.getProgramAddressSpace());
      // FIXME: Reject the 2-field form in LLVM 4.0.
      Assert(STy &&
                 (STy->getNumElements() == 2 || STy->getNumElements() == 3) &&
                 STy->getTypeAtIndex(0u)->isIntegerTy(32) &&
                 STy->getTypeAtIndex(1) == FuncPtrTy,
             "wrong type for intrinsic global variable", &GV);
      if (STy->getNumElements() == 3) {
        Type *ETy = STy->getTypeAtIndex(2);
        Assert(ETy->isPointerTy() &&
                   cast<PointerType>(ETy)->getElementType()->isIntegerTy(8),
               "wrong type for intrinsic global variable", &GV);
      }
    }
  }

  if (GV.hasName() && (GV.getName() == "llvm.used" ||
                       GV.getName() == "llvm.compiler.used")) {
    Assert(!GV.hasInitializer() || GV.hasAppendingLinkage(),
           "invalid linkage for intrinsic global variable", &GV);
    Type *GVType = GV.getValueType();
    if (ArrayType *ATy = dyn_cast<ArrayType>(GVType)) {
      PointerType *PTy = dyn_cast<PointerType>(ATy->getElementType());
      Assert(PTy, "wrong type for intrinsic global variable", &GV);
      if (GV.hasInitializer()) {
        const Constant *Init = GV.getInitializer();
        const ConstantArray *InitArray = dyn_cast<ConstantArray>(Init);
        Assert(InitArray, "wrong initalizer for intrinsic global variable",
               Init);
        for (Value *Op : InitArray->operands()) {
          Value *V = Op->stripPointerCastsNoFollowAliases();
          Assert(isa<GlobalVariable>(V) || isa<Function>(V) ||
                     isa<GlobalAlias>(V),
                 "invalid llvm.used member", V);
          Assert(V->hasName(), "members of llvm.used must be named", V);
        }
      }
    }
  }

  // Visit any debug info attachments.
  SmallVector<MDNode *, 1> MDs;
  GV.getMetadata(LLVMContext::MD_dbg, MDs);
  for (auto *MD : MDs) {
    if (auto *GVE = dyn_cast<DIGlobalVariableExpression>(MD))
      visitDIGlobalVariableExpression(*GVE);
    else
      AssertDI(false, "!dbg attachment of global variable must be a "
                      "DIGlobalVariableExpression");
  }

  if (!GV.hasInitializer()) {
    visitGlobalValue(GV);
    return;
  }

  // Walk any aggregate initializers looking for bitcasts between address spaces
  visitConstantExprsRecursively(GV.getInitializer());

  visitGlobalValue(GV);
}

void Verifier::visitAliaseeSubExpr(const GlobalAlias &GA, const Constant &C) {
  SmallPtrSet<const GlobalAlias*, 4> Visited;
  Visited.insert(&GA);
  visitAliaseeSubExpr(Visited, GA, C);
}

void Verifier::visitAliaseeSubExpr(SmallPtrSetImpl<const GlobalAlias*> &Visited,
                                   const GlobalAlias &GA, const Constant &C) {
  if (const auto *GV = dyn_cast<GlobalValue>(&C)) {
    Assert(!GV->isDeclarationForLinker(), "Alias must point to a definition",
           &GA);

    if (const auto *GA2 = dyn_cast<GlobalAlias>(GV)) {
      Assert(Visited.insert(GA2).second, "Aliases cannot form a cycle", &GA);

      Assert(!GA2->isInterposable(), "Alias cannot point to an interposable alias",
             &GA);
    } else {
      // Only continue verifying subexpressions of GlobalAliases.
      // Do not recurse into global initializers.
      return;
    }
  }

  if (const auto *CE = dyn_cast<ConstantExpr>(&C))
    visitConstantExprsRecursively(CE);

  for (const Use &U : C.operands()) {
    Value *V = &*U;
    if (const auto *GA2 = dyn_cast<GlobalAlias>(V))
      visitAliaseeSubExpr(Visited, GA, *GA2->getAliasee());
    else if (const auto *C2 = dyn_cast<Constant>(V))
      visitAliaseeSubExpr(Visited, GA, *C2);
  }
}

void Verifier::visitGlobalAlias(const GlobalAlias &GA) {
  Assert(GlobalAlias::isValidLinkage(GA.getLinkage()),
         "Alias should have private, internal, linkonce, weak, linkonce_odr, "
         "weak_odr, or external linkage!",
         &GA);
  const Constant *Aliasee = GA.getAliasee();
  Assert(Aliasee, "Aliasee cannot be NULL!", &GA);
  Assert(GA.getType() == Aliasee->getType(),
         "Alias and aliasee types should match!", &GA);

  Assert(isa<GlobalValue>(Aliasee) || isa<ConstantExpr>(Aliasee),
         "Aliasee should be either GlobalValue or ConstantExpr", &GA);

  visitAliaseeSubExpr(GA, *Aliasee);

  visitGlobalValue(GA);
}

void Verifier::visitNamedMDNode(const NamedMDNode &NMD) {
  // There used to be various other llvm.dbg.* nodes, but we don't support
  // upgrading them and we want to reserve the namespace for future uses.
  if (NMD.getName().startswith("llvm.dbg."))
    AssertDI(NMD.getName() == "llvm.dbg.cu",
             "unrecognized named metadata node in the llvm.dbg namespace",
             &NMD);
  for (const MDNode *MD : NMD.operands()) {
    if (NMD.getName() == "llvm.dbg.cu")
      AssertDI(MD && isa<DICompileUnit>(MD), "invalid compile unit", &NMD, MD);

    if (!MD)
      continue;

    visitMDNode(*MD);
  }
}

void Verifier::visitMDNode(const MDNode &MD) {
  // Only visit each node once.  Metadata can be mutually recursive, so this
  // avoids infinite recursion here, as well as being an optimization.
  if (!MDNodes.insert(&MD).second)
    return;

  switch (MD.getMetadataID()) {
  default:
    llvm_unreachable("Invalid MDNode subclass");
  case Metadata::MDTupleKind:
    break;
#define HANDLE_SPECIALIZED_MDNODE_LEAF(CLASS)                                  \
  case Metadata::CLASS##Kind:                                                  \
    visit##CLASS(cast<CLASS>(MD));                                             \
    break;
#include "llvm/IR/Metadata.def"
  }

  for (const Metadata *Op : MD.operands()) {
    if (!Op)
      continue;
    Assert(!isa<LocalAsMetadata>(Op), "Invalid operand for global metadata!",
           &MD, Op);
    if (auto *N = dyn_cast<MDNode>(Op)) {
      visitMDNode(*N);
      continue;
    }
    if (auto *V = dyn_cast<ValueAsMetadata>(Op)) {
      visitValueAsMetadata(*V, nullptr);
      continue;
    }
  }

  // Check these last, so we diagnose problems in operands first.
  Assert(!MD.isTemporary(), "Expected no forward declarations!", &MD);
  Assert(MD.isResolved(), "All nodes should be resolved!", &MD);
}

void Verifier::visitValueAsMetadata(const ValueAsMetadata &MD, Function *F) {
  Assert(MD.getValue(), "Expected valid value", &MD);
  Assert(!MD.getValue()->getType()->isMetadataTy(),
         "Unexpected metadata round-trip through values", &MD, MD.getValue());

  auto *L = dyn_cast<LocalAsMetadata>(&MD);
  if (!L)
    return;

  Assert(F, "function-local metadata used outside a function", L);

  // If this was an instruction, bb, or argument, verify that it is in the
  // function that we expect.
  Function *ActualF = nullptr;
  if (Instruction *I = dyn_cast<Instruction>(L->getValue())) {
    Assert(I->getParent(), "function-local metadata not in basic block", L, I);
    ActualF = I->getParent()->getParent();
  } else if (BasicBlock *BB = dyn_cast<BasicBlock>(L->getValue()))
    ActualF = BB->getParent();
  else if (Argument *A = dyn_cast<Argument>(L->getValue()))
    ActualF = A->getParent();
  assert(ActualF && "Unimplemented function local metadata case!");

  Assert(ActualF == F, "function-local metadata used in wrong function", L);
}

void Verifier::visitMetadataAsValue(const MetadataAsValue &MDV, Function *F) {
  Metadata *MD = MDV.getMetadata();
  if (auto *N = dyn_cast<MDNode>(MD)) {
    visitMDNode(*N);
    return;
  }

  // Only visit each node once.  Metadata can be mutually recursive, so this
  // avoids infinite recursion here, as well as being an optimization.
  if (!MDNodes.insert(MD).second)
    return;

  if (auto *V = dyn_cast<ValueAsMetadata>(MD))
    visitValueAsMetadata(*V, F);
}

static bool isType(const Metadata *MD) { return !MD || isa<DIType>(MD); }
static bool isScope(const Metadata *MD) { return !MD || isa<DIScope>(MD); }
static bool isDINode(const Metadata *MD) { return !MD || isa<DINode>(MD); }

void Verifier::visitDILocation(const DILocation &N) {
  AssertDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
           "location requires a valid scope", &N, N.getRawScope());
  if (auto *IA = N.getRawInlinedAt())
    AssertDI(isa<DILocation>(IA), "inlined-at should be a location", &N, IA);
  if (auto *SP = dyn_cast<DISubprogram>(N.getRawScope()))
    AssertDI(SP->isDefinition(), "scope points into the type hierarchy", &N);
}

void Verifier::visitGenericDINode(const GenericDINode &N) {
  AssertDI(N.getTag(), "invalid tag", &N);
}

void Verifier::visitDIScope(const DIScope &N) {
  if (auto *F = N.getRawFile())
    AssertDI(isa<DIFile>(F), "invalid file", &N, F);
}

void Verifier::visitDISubrange(const DISubrange &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_subrange_type, "invalid tag", &N);
  auto Count = N.getCount();
  AssertDI(Count, "Count must either be a signed constant or a DIVariable",
           &N);
  AssertDI(!Count.is<ConstantInt*>() ||
               Count.get<ConstantInt*>()->getSExtValue() >= -1,
           "invalid subrange count", &N);
}

void Verifier::visitDIEnumerator(const DIEnumerator &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_enumerator, "invalid tag", &N);
}

void Verifier::visitDIBasicType(const DIBasicType &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_base_type ||
               N.getTag() == dwarf::DW_TAG_unspecified_type,
           "invalid tag", &N);
  AssertDI(!(N.isBigEndian() && N.isLittleEndian()) ,
            "has conflicting flags", &N);
}

void Verifier::visitDIDerivedType(const DIDerivedType &N) {
  // Common scope checks.
  visitDIScope(N);

  AssertDI(N.getTag() == dwarf::DW_TAG_typedef ||
               N.getTag() == dwarf::DW_TAG_pointer_type ||
               N.getTag() == dwarf::DW_TAG_ptr_to_member_type ||
               N.getTag() == dwarf::DW_TAG_reference_type ||
               N.getTag() == dwarf::DW_TAG_rvalue_reference_type ||
               N.getTag() == dwarf::DW_TAG_const_type ||
               N.getTag() == dwarf::DW_TAG_volatile_type ||
               N.getTag() == dwarf::DW_TAG_restrict_type ||
               N.getTag() == dwarf::DW_TAG_atomic_type ||
               N.getTag() == dwarf::DW_TAG_member ||
               N.getTag() == dwarf::DW_TAG_inheritance ||
               N.getTag() == dwarf::DW_TAG_friend,
           "invalid tag", &N);
  if (N.getTag() == dwarf::DW_TAG_ptr_to_member_type) {
    AssertDI(isType(N.getRawExtraData()), "invalid pointer to member type", &N,
             N.getRawExtraData());
  }

  AssertDI(isScope(N.getRawScope()), "invalid scope", &N, N.getRawScope());
  AssertDI(isType(N.getRawBaseType()), "invalid base type", &N,
           N.getRawBaseType());

  if (N.getDWARFAddressSpace()) {
    AssertDI(N.getTag() == dwarf::DW_TAG_pointer_type ||
                 N.getTag() == dwarf::DW_TAG_reference_type,
             "DWARF address space only applies to pointer or reference types",
             &N);
  }
}

/// Detect mutually exclusive flags.
static bool hasConflictingReferenceFlags(unsigned Flags) {
  return ((Flags & DINode::FlagLValueReference) &&
          (Flags & DINode::FlagRValueReference)) ||
         ((Flags & DINode::FlagTypePassByValue) &&
          (Flags & DINode::FlagTypePassByReference));
}

void Verifier::visitTemplateParams(const MDNode &N, const Metadata &RawParams) {
  auto *Params = dyn_cast<MDTuple>(&RawParams);
  AssertDI(Params, "invalid template params", &N, &RawParams);
  for (Metadata *Op : Params->operands()) {
    AssertDI(Op && isa<DITemplateParameter>(Op), "invalid template parameter",
             &N, Params, Op);
  }
}

void Verifier::visitDICompositeType(const DICompositeType &N) {
  // Common scope checks.
  visitDIScope(N);

  AssertDI(N.getTag() == dwarf::DW_TAG_array_type ||
               N.getTag() == dwarf::DW_TAG_structure_type ||
               N.getTag() == dwarf::DW_TAG_union_type ||
               N.getTag() == dwarf::DW_TAG_enumeration_type ||
               N.getTag() == dwarf::DW_TAG_class_type ||
               N.getTag() == dwarf::DW_TAG_variant_part,
           "invalid tag", &N);

  AssertDI(isScope(N.getRawScope()), "invalid scope", &N, N.getRawScope());
  AssertDI(isType(N.getRawBaseType()), "invalid base type", &N,
           N.getRawBaseType());

  AssertDI(!N.getRawElements() || isa<MDTuple>(N.getRawElements()),
           "invalid composite elements", &N, N.getRawElements());
  AssertDI(isType(N.getRawVTableHolder()), "invalid vtable holder", &N,
           N.getRawVTableHolder());
  AssertDI(!hasConflictingReferenceFlags(N.getFlags()),
           "invalid reference flags", &N);

  if (N.isVector()) {
    const DINodeArray Elements = N.getElements();
    AssertDI(Elements.size() == 1 &&
             Elements[0]->getTag() == dwarf::DW_TAG_subrange_type,
             "invalid vector, expected one element of type subrange", &N);
  }

  if (auto *Params = N.getRawTemplateParams())
    visitTemplateParams(N, *Params);

  if (N.getTag() == dwarf::DW_TAG_class_type ||
      N.getTag() == dwarf::DW_TAG_union_type) {
    AssertDI(N.getFile() && !N.getFile()->getFilename().empty(),
             "class/union requires a filename", &N, N.getFile());
  }

  if (auto *D = N.getRawDiscriminator()) {
    AssertDI(isa<DIDerivedType>(D) && N.getTag() == dwarf::DW_TAG_variant_part,
             "discriminator can only appear on variant part");
  }
}

void Verifier::visitDISubroutineType(const DISubroutineType &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_subroutine_type, "invalid tag", &N);
  if (auto *Types = N.getRawTypeArray()) {
    AssertDI(isa<MDTuple>(Types), "invalid composite elements", &N, Types);
    for (Metadata *Ty : N.getTypeArray()->operands()) {
      AssertDI(isType(Ty), "invalid subroutine type ref", &N, Types, Ty);
    }
  }
  AssertDI(!hasConflictingReferenceFlags(N.getFlags()),
           "invalid reference flags", &N);
}

void Verifier::visitDIFile(const DIFile &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_file_type, "invalid tag", &N);
  Optional<DIFile::ChecksumInfo<StringRef>> Checksum = N.getChecksum();
  if (Checksum) {
    AssertDI(Checksum->Kind <= DIFile::ChecksumKind::CSK_Last,
             "invalid checksum kind", &N);
    size_t Size;
    switch (Checksum->Kind) {
    case DIFile::CSK_MD5:
      Size = 32;
      break;
    case DIFile::CSK_SHA1:
      Size = 40;
      break;
    }
    AssertDI(Checksum->Value.size() == Size, "invalid checksum length", &N);
    AssertDI(Checksum->Value.find_if_not(llvm::isHexDigit) == StringRef::npos,
             "invalid checksum", &N);
  }
}

void Verifier::visitDICompileUnit(const DICompileUnit &N) {
  AssertDI(N.isDistinct(), "compile units must be distinct", &N);
  AssertDI(N.getTag() == dwarf::DW_TAG_compile_unit, "invalid tag", &N);

  // Don't bother verifying the compilation directory or producer string
  // as those could be empty.
  AssertDI(N.getRawFile() && isa<DIFile>(N.getRawFile()), "invalid file", &N,
           N.getRawFile());
  AssertDI(!N.getFile()->getFilename().empty(), "invalid filename", &N,
           N.getFile());

  verifySourceDebugInfo(N, *N.getFile());

  AssertDI((N.getEmissionKind() <= DICompileUnit::LastEmissionKind),
           "invalid emission kind", &N);

  if (auto *Array = N.getRawEnumTypes()) {
    AssertDI(isa<MDTuple>(Array), "invalid enum list", &N, Array);
    for (Metadata *Op : N.getEnumTypes()->operands()) {
      auto *Enum = dyn_cast_or_null<DICompositeType>(Op);
      AssertDI(Enum && Enum->getTag() == dwarf::DW_TAG_enumeration_type,
               "invalid enum type", &N, N.getEnumTypes(), Op);
    }
  }
  if (auto *Array = N.getRawRetainedTypes()) {
    AssertDI(isa<MDTuple>(Array), "invalid retained type list", &N, Array);
    for (Metadata *Op : N.getRetainedTypes()->operands()) {
      AssertDI(Op && (isa<DIType>(Op) ||
                      (isa<DISubprogram>(Op) &&
                       !cast<DISubprogram>(Op)->isDefinition())),
               "invalid retained type", &N, Op);
    }
  }
  if (auto *Array = N.getRawGlobalVariables()) {
    AssertDI(isa<MDTuple>(Array), "invalid global variable list", &N, Array);
    for (Metadata *Op : N.getGlobalVariables()->operands()) {
      AssertDI(Op && (isa<DIGlobalVariableExpression>(Op)),
               "invalid global variable ref", &N, Op);
    }
  }
  if (auto *Array = N.getRawImportedEntities()) {
    AssertDI(isa<MDTuple>(Array), "invalid imported entity list", &N, Array);
    for (Metadata *Op : N.getImportedEntities()->operands()) {
      AssertDI(Op && isa<DIImportedEntity>(Op), "invalid imported entity ref",
               &N, Op);
    }
  }
  if (auto *Array = N.getRawMacros()) {
    AssertDI(isa<MDTuple>(Array), "invalid macro list", &N, Array);
    for (Metadata *Op : N.getMacros()->operands()) {
      AssertDI(Op && isa<DIMacroNode>(Op), "invalid macro ref", &N, Op);
    }
  }
  CUVisited.insert(&N);
}

void Verifier::visitDISubprogram(const DISubprogram &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_subprogram, "invalid tag", &N);
  AssertDI(isScope(N.getRawScope()), "invalid scope", &N, N.getRawScope());
  if (auto *F = N.getRawFile())
    AssertDI(isa<DIFile>(F), "invalid file", &N, F);
  else
    AssertDI(N.getLine() == 0, "line specified with no file", &N, N.getLine());
  if (auto *T = N.getRawType())
    AssertDI(isa<DISubroutineType>(T), "invalid subroutine type", &N, T);
  AssertDI(isType(N.getRawContainingType()), "invalid containing type", &N,
           N.getRawContainingType());
  if (auto *Params = N.getRawTemplateParams())
    visitTemplateParams(N, *Params);
  if (auto *S = N.getRawDeclaration())
    AssertDI(isa<DISubprogram>(S) && !cast<DISubprogram>(S)->isDefinition(),
             "invalid subprogram declaration", &N, S);
  if (auto *RawNode = N.getRawRetainedNodes()) {
    auto *Node = dyn_cast<MDTuple>(RawNode);
    AssertDI(Node, "invalid retained nodes list", &N, RawNode);
    for (Metadata *Op : Node->operands()) {
      AssertDI(Op && (isa<DILocalVariable>(Op) || isa<DILabel>(Op)),
               "invalid retained nodes, expected DILocalVariable or DILabel",
               &N, Node, Op);
    }
  }
  AssertDI(!hasConflictingReferenceFlags(N.getFlags()),
           "invalid reference flags", &N);

  auto *Unit = N.getRawUnit();
  if (N.isDefinition()) {
    // Subprogram definitions (not part of the type hierarchy).
    AssertDI(N.isDistinct(), "subprogram definitions must be distinct", &N);
    AssertDI(Unit, "subprogram definitions must have a compile unit", &N);
    AssertDI(isa<DICompileUnit>(Unit), "invalid unit type", &N, Unit);
    if (N.getFile())
      verifySourceDebugInfo(*N.getUnit(), *N.getFile());
  } else {
    // Subprogram declarations (part of the type hierarchy).
    AssertDI(!Unit, "subprogram declarations must not have a compile unit", &N);
  }

  if (auto *RawThrownTypes = N.getRawThrownTypes()) {
    auto *ThrownTypes = dyn_cast<MDTuple>(RawThrownTypes);
    AssertDI(ThrownTypes, "invalid thrown types list", &N, RawThrownTypes);
    for (Metadata *Op : ThrownTypes->operands())
      AssertDI(Op && isa<DIType>(Op), "invalid thrown type", &N, ThrownTypes,
               Op);
  }

  if (N.areAllCallsDescribed())
    AssertDI(N.isDefinition(),
             "DIFlagAllCallsDescribed must be attached to a definition");
}

void Verifier::visitDILexicalBlockBase(const DILexicalBlockBase &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_lexical_block, "invalid tag", &N);
  AssertDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
           "invalid local scope", &N, N.getRawScope());
  if (auto *SP = dyn_cast<DISubprogram>(N.getRawScope()))
    AssertDI(SP->isDefinition(), "scope points into the type hierarchy", &N);
}

void Verifier::visitDILexicalBlock(const DILexicalBlock &N) {
  visitDILexicalBlockBase(N);

  AssertDI(N.getLine() || !N.getColumn(),
           "cannot have column info without line info", &N);
}

void Verifier::visitDILexicalBlockFile(const DILexicalBlockFile &N) {
  visitDILexicalBlockBase(N);
}

void Verifier::visitDINamespace(const DINamespace &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_namespace, "invalid tag", &N);
  if (auto *S = N.getRawScope())
    AssertDI(isa<DIScope>(S), "invalid scope ref", &N, S);
}

void Verifier::visitDIMacro(const DIMacro &N) {
  AssertDI(N.getMacinfoType() == dwarf::DW_MACINFO_define ||
               N.getMacinfoType() == dwarf::DW_MACINFO_undef,
           "invalid macinfo type", &N);
  AssertDI(!N.getName().empty(), "anonymous macro", &N);
  if (!N.getValue().empty()) {
    assert(N.getValue().data()[0] != ' ' && "Macro value has a space prefix");
  }
}

void Verifier::visitDIMacroFile(const DIMacroFile &N) {
  AssertDI(N.getMacinfoType() == dwarf::DW_MACINFO_start_file,
           "invalid macinfo type", &N);
  if (auto *F = N.getRawFile())
    AssertDI(isa<DIFile>(F), "invalid file", &N, F);

  if (auto *Array = N.getRawElements()) {
    AssertDI(isa<MDTuple>(Array), "invalid macro list", &N, Array);
    for (Metadata *Op : N.getElements()->operands()) {
      AssertDI(Op && isa<DIMacroNode>(Op), "invalid macro ref", &N, Op);
    }
  }
}

void Verifier::visitDIModule(const DIModule &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_module, "invalid tag", &N);
  AssertDI(!N.getName().empty(), "anonymous module", &N);
}

void Verifier::visitDITemplateParameter(const DITemplateParameter &N) {
  AssertDI(isType(N.getRawType()), "invalid type ref", &N, N.getRawType());
}

void Verifier::visitDITemplateTypeParameter(const DITemplateTypeParameter &N) {
  visitDITemplateParameter(N);

  AssertDI(N.getTag() == dwarf::DW_TAG_template_type_parameter, "invalid tag",
           &N);
}

void Verifier::visitDITemplateValueParameter(
    const DITemplateValueParameter &N) {
  visitDITemplateParameter(N);

  AssertDI(N.getTag() == dwarf::DW_TAG_template_value_parameter ||
               N.getTag() == dwarf::DW_TAG_GNU_template_template_param ||
               N.getTag() == dwarf::DW_TAG_GNU_template_parameter_pack,
           "invalid tag", &N);
}

void Verifier::visitDIVariable(const DIVariable &N) {
  if (auto *S = N.getRawScope())
    AssertDI(isa<DIScope>(S), "invalid scope", &N, S);
  if (auto *F = N.getRawFile())
    AssertDI(isa<DIFile>(F), "invalid file", &N, F);
}

void Verifier::visitDIGlobalVariable(const DIGlobalVariable &N) {
  // Checks common to all variables.
  visitDIVariable(N);

  AssertDI(N.getTag() == dwarf::DW_TAG_variable, "invalid tag", &N);
  AssertDI(!N.getName().empty(), "missing global variable name", &N);
  AssertDI(isType(N.getRawType()), "invalid type ref", &N, N.getRawType());
  AssertDI(N.getType(), "missing global variable type", &N);
  if (auto *Member = N.getRawStaticDataMemberDeclaration()) {
    AssertDI(isa<DIDerivedType>(Member),
             "invalid static data member declaration", &N, Member);
  }
}

void Verifier::visitDILocalVariable(const DILocalVariable &N) {
  // Checks common to all variables.
  visitDIVariable(N);

  AssertDI(isType(N.getRawType()), "invalid type ref", &N, N.getRawType());
  AssertDI(N.getTag() == dwarf::DW_TAG_variable, "invalid tag", &N);
  AssertDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
           "local variable requires a valid scope", &N, N.getRawScope());
  if (auto Ty = N.getType())
    AssertDI(!isa<DISubroutineType>(Ty), "invalid type", &N, N.getType());
}

void Verifier::visitDILabel(const DILabel &N) {
  if (auto *S = N.getRawScope())
    AssertDI(isa<DIScope>(S), "invalid scope", &N, S);
  if (auto *F = N.getRawFile())
    AssertDI(isa<DIFile>(F), "invalid file", &N, F);

  AssertDI(N.getTag() == dwarf::DW_TAG_label, "invalid tag", &N);
  AssertDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
           "label requires a valid scope", &N, N.getRawScope());
}

void Verifier::visitDIExpression(const DIExpression &N) {
  AssertDI(N.isValid(), "invalid expression", &N);
}

void Verifier::visitDIGlobalVariableExpression(
    const DIGlobalVariableExpression &GVE) {
  AssertDI(GVE.getVariable(), "missing variable");
  if (auto *Var = GVE.getVariable())
    visitDIGlobalVariable(*Var);
  if (auto *Expr = GVE.getExpression()) {
    visitDIExpression(*Expr);
    if (auto Fragment = Expr->getFragmentInfo())
      verifyFragmentExpression(*GVE.getVariable(), *Fragment, &GVE);
  }
}

void Verifier::visitDIObjCProperty(const DIObjCProperty &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_APPLE_property, "invalid tag", &N);
  if (auto *T = N.getRawType())
    AssertDI(isType(T), "invalid type ref", &N, T);
  if (auto *F = N.getRawFile())
    AssertDI(isa<DIFile>(F), "invalid file", &N, F);
}

void Verifier::visitDIImportedEntity(const DIImportedEntity &N) {
  AssertDI(N.getTag() == dwarf::DW_TAG_imported_module ||
               N.getTag() == dwarf::DW_TAG_imported_declaration,
           "invalid tag", &N);
  if (auto *S = N.getRawScope())
    AssertDI(isa<DIScope>(S), "invalid scope for imported entity", &N, S);
  AssertDI(isDINode(N.getRawEntity()), "invalid imported entity", &N,
           N.getRawEntity());
}

void Verifier::visitComdat(const Comdat &C) {
  // The Module is invalid if the GlobalValue has private linkage.  Entities
  // with private linkage don't have entries in the symbol table.
  if (const GlobalValue *GV = M.getNamedValue(C.getName()))
    Assert(!GV->hasPrivateLinkage(), "comdat global value has private linkage",
           GV);
}

void Verifier::visitModuleIdents(const Module &M) {
  const NamedMDNode *Idents = M.getNamedMetadata("llvm.ident");
  if (!Idents)
    return;

  // llvm.ident takes a list of metadata entry. Each entry has only one string.
  // Scan each llvm.ident entry and make sure that this requirement is met.
  for (const MDNode *N : Idents->operands()) {
    Assert(N->getNumOperands() == 1,
           "incorrect number of operands in llvm.ident metadata", N);
    Assert(dyn_cast_or_null<MDString>(N->getOperand(0)),
           ("invalid value for llvm.ident metadata entry operand"
            "(the operand should be a string)"),
           N->getOperand(0));
  }
}

void Verifier::visitModuleCommandLines(const Module &M) {
  const NamedMDNode *CommandLines = M.getNamedMetadata("llvm.commandline");
  if (!CommandLines)
    return;

  // llvm.commandline takes a list of metadata entry. Each entry has only one
  // string. Scan each llvm.commandline entry and make sure that this
  // requirement is met.
  for (const MDNode *N : CommandLines->operands()) {
    Assert(N->getNumOperands() == 1,
           "incorrect number of operands in llvm.commandline metadata", N);
    Assert(dyn_cast_or_null<MDString>(N->getOperand(0)),
           ("invalid value for llvm.commandline metadata entry operand"
            "(the operand should be a string)"),
           N->getOperand(0));
  }
}

void Verifier::visitModuleFlags(const Module &M) {
  const NamedMDNode *Flags = M.getModuleFlagsMetadata();
  if (!Flags) return;

  // Scan each flag, and track the flags and requirements.
  DenseMap<const MDString*, const MDNode*> SeenIDs;
  SmallVector<const MDNode*, 16> Requirements;
  for (const MDNode *MDN : Flags->operands())
    visitModuleFlag(MDN, SeenIDs, Requirements);

  // Validate that the requirements in the module are valid.
  for (const MDNode *Requirement : Requirements) {
    const MDString *Flag = cast<MDString>(Requirement->getOperand(0));
    const Metadata *ReqValue = Requirement->getOperand(1);

    const MDNode *Op = SeenIDs.lookup(Flag);
    if (!Op) {
      CheckFailed("invalid requirement on flag, flag is not present in module",
                  Flag);
      continue;
    }

    if (Op->getOperand(2) != ReqValue) {
      CheckFailed(("invalid requirement on flag, "
                   "flag does not have the required value"),
                  Flag);
      continue;
    }
  }
}

void
Verifier::visitModuleFlag(const MDNode *Op,
                          DenseMap<const MDString *, const MDNode *> &SeenIDs,
                          SmallVectorImpl<const MDNode *> &Requirements) {
  // Each module flag should have three arguments, the merge behavior (a
  // constant int), the flag ID (an MDString), and the value.
  Assert(Op->getNumOperands() == 3,
         "incorrect number of operands in module flag", Op);
  Module::ModFlagBehavior MFB;
  if (!Module::isValidModFlagBehavior(Op->getOperand(0), MFB)) {
    Assert(
        mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(0)),
        "invalid behavior operand in module flag (expected constant integer)",
        Op->getOperand(0));
    Assert(false,
           "invalid behavior operand in module flag (unexpected constant)",
           Op->getOperand(0));
  }
  MDString *ID = dyn_cast_or_null<MDString>(Op->getOperand(1));
  Assert(ID, "invalid ID operand in module flag (expected metadata string)",
         Op->getOperand(1));

  // Sanity check the values for behaviors with additional requirements.
  switch (MFB) {
  case Module::Error:
  case Module::Warning:
  case Module::Override:
    // These behavior types accept any value.
    break;

  case Module::Max: {
    Assert(mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(2)),
           "invalid value for 'max' module flag (expected constant integer)",
           Op->getOperand(2));
    break;
  }

  case Module::Require: {
    // The value should itself be an MDNode with two operands, a flag ID (an
    // MDString), and a value.
    MDNode *Value = dyn_cast<MDNode>(Op->getOperand(2));
    Assert(Value && Value->getNumOperands() == 2,
           "invalid value for 'require' module flag (expected metadata pair)",
           Op->getOperand(2));
    Assert(isa<MDString>(Value->getOperand(0)),
           ("invalid value for 'require' module flag "
            "(first value operand should be a string)"),
           Value->getOperand(0));

    // Append it to the list of requirements, to check once all module flags are
    // scanned.
    Requirements.push_back(Value);
    break;
  }

  case Module::Append:
  case Module::AppendUnique: {
    // These behavior types require the operand be an MDNode.
    Assert(isa<MDNode>(Op->getOperand(2)),
           "invalid value for 'append'-type module flag "
           "(expected a metadata node)",
           Op->getOperand(2));
    break;
  }
  }

  // Unless this is a "requires" flag, check the ID is unique.
  if (MFB != Module::Require) {
    bool Inserted = SeenIDs.insert(std::make_pair(ID, Op)).second;
    Assert(Inserted,
           "module flag identifiers must be unique (or of 'require' type)", ID);
  }

  if (ID->getString() == "wchar_size") {
    ConstantInt *Value
      = mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(2));
    Assert(Value, "wchar_size metadata requires constant integer argument");
  }

  if (ID->getString() == "Linker Options") {
    // If the llvm.linker.options named metadata exists, we assume that the
    // bitcode reader has upgraded the module flag. Otherwise the flag might
    // have been created by a client directly.
    Assert(M.getNamedMetadata("llvm.linker.options"),
           "'Linker Options' named metadata no longer supported");
  }

  if (ID->getString() == "CG Profile") {
    for (const MDOperand &MDO : cast<MDNode>(Op->getOperand(2))->operands())
      visitModuleFlagCGProfileEntry(MDO);
  }
}

void Verifier::visitModuleFlagCGProfileEntry(const MDOperand &MDO) {
  auto CheckFunction = [&](const MDOperand &FuncMDO) {
    if (!FuncMDO)
      return;
    auto F = dyn_cast<ValueAsMetadata>(FuncMDO);
    Assert(F && isa<Function>(F->getValue()), "expected a Function or null",
           FuncMDO);
  };
  auto Node = dyn_cast_or_null<MDNode>(MDO);
  Assert(Node && Node->getNumOperands() == 3, "expected a MDNode triple", MDO);
  CheckFunction(Node->getOperand(0));
  CheckFunction(Node->getOperand(1));
  auto Count = dyn_cast_or_null<ConstantAsMetadata>(Node->getOperand(2));
  Assert(Count && Count->getType()->isIntegerTy(),
         "expected an integer constant", Node->getOperand(2));
}

/// Return true if this attribute kind only applies to functions.
static bool isFuncOnlyAttr(Attribute::AttrKind Kind) {
  switch (Kind) {
  case Attribute::NoReturn:
  case Attribute::NoCfCheck:
  case Attribute::NoUnwind:
  case Attribute::NoInline:
  case Attribute::AlwaysInline:
  case Attribute::OptimizeForSize:
  case Attribute::StackProtect:
  case Attribute::StackProtectReq:
  case Attribute::StackProtectStrong:
  case Attribute::SafeStack:
  case Attribute::ShadowCallStack:
  case Attribute::NoRedZone:
  case Attribute::NoImplicitFloat:
  case Attribute::Naked:
  case Attribute::InlineHint:
  case Attribute::StackAlignment:
  case Attribute::UWTable:
  case Attribute::NonLazyBind:
  case Attribute::ReturnsTwice:
  case Attribute::SanitizeAddress:
  case Attribute::SanitizeHWAddress:
  case Attribute::SanitizeThread:
  case Attribute::SanitizeMemory:
  case Attribute::MinSize:
  case Attribute::NoDuplicate:
  case Attribute::Builtin:
  case Attribute::NoBuiltin:
  case Attribute::Cold:
  case Attribute::OptForFuzzing:
  case Attribute::OptimizeNone:
  case Attribute::JumpTable:
  case Attribute::Convergent:
  case Attribute::ArgMemOnly:
  case Attribute::NoRecurse:
  case Attribute::InaccessibleMemOnly:
  case Attribute::InaccessibleMemOrArgMemOnly:
  case Attribute::AllocSize:
  case Attribute::SpeculativeLoadHardening:
  case Attribute::Speculatable:
  case Attribute::StrictFP:
    return true;
  default:
    break;
  }
  return false;
}

/// Return true if this is a function attribute that can also appear on
/// arguments.
static bool isFuncOrArgAttr(Attribute::AttrKind Kind) {
  return Kind == Attribute::ReadOnly || Kind == Attribute::WriteOnly ||
         Kind == Attribute::ReadNone;
}

void Verifier::verifyAttributeTypes(AttributeSet Attrs, bool IsFunction,
                                    const Value *V) {
  for (Attribute A : Attrs) {
    if (A.isStringAttribute())
      continue;

    if (isFuncOnlyAttr(A.getKindAsEnum())) {
      if (!IsFunction) {
        CheckFailed("Attribute '" + A.getAsString() +
                        "' only applies to functions!",
                    V);
        return;
      }
    } else if (IsFunction && !isFuncOrArgAttr(A.getKindAsEnum())) {
      CheckFailed("Attribute '" + A.getAsString() +
                      "' does not apply to functions!",
                  V);
      return;
    }
  }
}

// VerifyParameterAttrs - Check the given attributes for an argument or return
// value of the specified type.  The value V is printed in error messages.
void Verifier::verifyParameterAttrs(AttributeSet Attrs, Type *Ty,
                                    const Value *V) {
  if (!Attrs.hasAttributes())
    return;

  verifyAttributeTypes(Attrs, /*IsFunction=*/false, V);

  // Check for mutually incompatible attributes.  Only inreg is compatible with
  // sret.
  unsigned AttrCount = 0;
  AttrCount += Attrs.hasAttribute(Attribute::ByVal);
  AttrCount += Attrs.hasAttribute(Attribute::InAlloca);
  AttrCount += Attrs.hasAttribute(Attribute::StructRet) ||
               Attrs.hasAttribute(Attribute::InReg);
  AttrCount += Attrs.hasAttribute(Attribute::Nest);
  Assert(AttrCount <= 1, "Attributes 'byval', 'inalloca', 'inreg', 'nest', "
                         "and 'sret' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::InAlloca) &&
           Attrs.hasAttribute(Attribute::ReadOnly)),
         "Attributes "
         "'inalloca and readonly' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::StructRet) &&
           Attrs.hasAttribute(Attribute::Returned)),
         "Attributes "
         "'sret and returned' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::ZExt) &&
           Attrs.hasAttribute(Attribute::SExt)),
         "Attributes "
         "'zeroext and signext' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::ReadNone) &&
           Attrs.hasAttribute(Attribute::ReadOnly)),
         "Attributes "
         "'readnone and readonly' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::ReadNone) &&
           Attrs.hasAttribute(Attribute::WriteOnly)),
         "Attributes "
         "'readnone and writeonly' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::ReadOnly) &&
           Attrs.hasAttribute(Attribute::WriteOnly)),
         "Attributes "
         "'readonly and writeonly' are incompatible!",
         V);

  Assert(!(Attrs.hasAttribute(Attribute::NoInline) &&
           Attrs.hasAttribute(Attribute::AlwaysInline)),
         "Attributes "
         "'noinline and alwaysinline' are incompatible!",
         V);

  AttrBuilder IncompatibleAttrs = AttributeFuncs::typeIncompatible(Ty);
  Assert(!AttrBuilder(Attrs).overlaps(IncompatibleAttrs),
         "Wrong types for attribute: " +
             AttributeSet::get(Context, IncompatibleAttrs).getAsString(),
         V);

  if (PointerType *PTy = dyn_cast<PointerType>(Ty)) {
    SmallPtrSet<Type*, 4> Visited;
    if (!PTy->getElementType()->isSized(&Visited)) {
      Assert(!Attrs.hasAttribute(Attribute::ByVal) &&
                 !Attrs.hasAttribute(Attribute::InAlloca),
             "Attributes 'byval' and 'inalloca' do not support unsized types!",
             V);
    }
    if (!isa<PointerType>(PTy->getElementType()))
      Assert(!Attrs.hasAttribute(Attribute::SwiftError),
             "Attribute 'swifterror' only applies to parameters "
             "with pointer to pointer type!",
             V);
  } else {
    Assert(!Attrs.hasAttribute(Attribute::ByVal),
           "Attribute 'byval' only applies to parameters with pointer type!",
           V);
    Assert(!Attrs.hasAttribute(Attribute::SwiftError),
           "Attribute 'swifterror' only applies to parameters "
           "with pointer type!",
           V);
  }
}

// Check parameter attributes against a function type.
// The value V is printed in error messages.
void Verifier::verifyFunctionAttrs(FunctionType *FT, AttributeList Attrs,
                                   const Value *V) {
  if (Attrs.isEmpty())
    return;

  bool SawNest = false;
  bool SawReturned = false;
  bool SawSRet = false;
  bool SawSwiftSelf = false;
  bool SawSwiftError = false;

  // Verify return value attributes.
  AttributeSet RetAttrs = Attrs.getRetAttributes();
  Assert((!RetAttrs.hasAttribute(Attribute::ByVal) &&
          !RetAttrs.hasAttribute(Attribute::Nest) &&
          !RetAttrs.hasAttribute(Attribute::StructRet) &&
          !RetAttrs.hasAttribute(Attribute::NoCapture) &&
          !RetAttrs.hasAttribute(Attribute::Returned) &&
          !RetAttrs.hasAttribute(Attribute::InAlloca) &&
          !RetAttrs.hasAttribute(Attribute::SwiftSelf) &&
          !RetAttrs.hasAttribute(Attribute::SwiftError)),
         "Attributes 'byval', 'inalloca', 'nest', 'sret', 'nocapture', "
         "'returned', 'swiftself', and 'swifterror' do not apply to return "
         "values!",
         V);
  Assert((!RetAttrs.hasAttribute(Attribute::ReadOnly) &&
          !RetAttrs.hasAttribute(Attribute::WriteOnly) &&
          !RetAttrs.hasAttribute(Attribute::ReadNone)),
         "Attribute '" + RetAttrs.getAsString() +
             "' does not apply to function returns",
         V);
  verifyParameterAttrs(RetAttrs, FT->getReturnType(), V);

  // Verify parameter attributes.
  for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
    Type *Ty = FT->getParamType(i);
    AttributeSet ArgAttrs = Attrs.getParamAttributes(i);

    verifyParameterAttrs(ArgAttrs, Ty, V);

    if (ArgAttrs.hasAttribute(Attribute::Nest)) {
      Assert(!SawNest, "More than one parameter has attribute nest!", V);
      SawNest = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::Returned)) {
      Assert(!SawReturned, "More than one parameter has attribute returned!",
             V);
      Assert(Ty->canLosslesslyBitCastTo(FT->getReturnType()),
             "Incompatible argument and return types for 'returned' attribute",
             V);
      SawReturned = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::StructRet)) {
      Assert(!SawSRet, "Cannot have multiple 'sret' parameters!", V);
      Assert(i == 0 || i == 1,
             "Attribute 'sret' is not on first or second parameter!", V);
      SawSRet = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::SwiftSelf)) {
      Assert(!SawSwiftSelf, "Cannot have multiple 'swiftself' parameters!", V);
      SawSwiftSelf = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::SwiftError)) {
      Assert(!SawSwiftError, "Cannot have multiple 'swifterror' parameters!",
             V);
      SawSwiftError = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::InAlloca)) {
      Assert(i == FT->getNumParams() - 1,
             "inalloca isn't on the last parameter!", V);
    }
  }

  if (!Attrs.hasAttributes(AttributeList::FunctionIndex))
    return;

  verifyAttributeTypes(Attrs.getFnAttributes(), /*IsFunction=*/true, V);

  Assert(!(Attrs.hasFnAttribute(Attribute::ReadNone) &&
           Attrs.hasFnAttribute(Attribute::ReadOnly)),
         "Attributes 'readnone and readonly' are incompatible!", V);

  Assert(!(Attrs.hasFnAttribute(Attribute::ReadNone) &&
           Attrs.hasFnAttribute(Attribute::WriteOnly)),
         "Attributes 'readnone and writeonly' are incompatible!", V);

  Assert(!(Attrs.hasFnAttribute(Attribute::ReadOnly) &&
           Attrs.hasFnAttribute(Attribute::WriteOnly)),
         "Attributes 'readonly and writeonly' are incompatible!", V);

  Assert(!(Attrs.hasFnAttribute(Attribute::ReadNone) &&
           Attrs.hasFnAttribute(Attribute::InaccessibleMemOrArgMemOnly)),
         "Attributes 'readnone and inaccessiblemem_or_argmemonly' are "
         "incompatible!",
         V);

  Assert(!(Attrs.hasFnAttribute(Attribute::ReadNone) &&
           Attrs.hasFnAttribute(Attribute::InaccessibleMemOnly)),
         "Attributes 'readnone and inaccessiblememonly' are incompatible!", V);

  Assert(!(Attrs.hasFnAttribute(Attribute::NoInline) &&
           Attrs.hasFnAttribute(Attribute::AlwaysInline)),
         "Attributes 'noinline and alwaysinline' are incompatible!", V);

  if (Attrs.hasFnAttribute(Attribute::OptimizeNone)) {
    Assert(Attrs.hasFnAttribute(Attribute::NoInline),
           "Attribute 'optnone' requires 'noinline'!", V);

    Assert(!Attrs.hasFnAttribute(Attribute::OptimizeForSize),
           "Attributes 'optsize and optnone' are incompatible!", V);

    Assert(!Attrs.hasFnAttribute(Attribute::MinSize),
           "Attributes 'minsize and optnone' are incompatible!", V);
  }

  if (Attrs.hasFnAttribute(Attribute::JumpTable)) {
    const GlobalValue *GV = cast<GlobalValue>(V);
    Assert(GV->hasGlobalUnnamedAddr(),
           "Attribute 'jumptable' requires 'unnamed_addr'", V);
  }

  if (Attrs.hasFnAttribute(Attribute::AllocSize)) {
    std::pair<unsigned, Optional<unsigned>> Args =
        Attrs.getAllocSizeArgs(AttributeList::FunctionIndex);

    auto CheckParam = [&](StringRef Name, unsigned ParamNo) {
      if (ParamNo >= FT->getNumParams()) {
        CheckFailed("'allocsize' " + Name + " argument is out of bounds", V);
        return false;
      }

      if (!FT->getParamType(ParamNo)->isIntegerTy()) {
        CheckFailed("'allocsize' " + Name +
                        " argument must refer to an integer parameter",
                    V);
        return false;
      }

      return true;
    };

    if (!CheckParam("element size", Args.first))
      return;

    if (Args.second && !CheckParam("number of elements", *Args.second))
      return;
  }
}

void Verifier::verifyFunctionMetadata(
    ArrayRef<std::pair<unsigned, MDNode *>> MDs) {
  for (const auto &Pair : MDs) {
    if (Pair.first == LLVMContext::MD_prof) {
      MDNode *MD = Pair.second;
      Assert(MD->getNumOperands() >= 2,
             "!prof annotations should have no less than 2 operands", MD);

      // Check first operand.
      Assert(MD->getOperand(0) != nullptr, "first operand should not be null",
             MD);
      Assert(isa<MDString>(MD->getOperand(0)),
             "expected string with name of the !prof annotation", MD);
      MDString *MDS = cast<MDString>(MD->getOperand(0));
      StringRef ProfName = MDS->getString();
      Assert(ProfName.equals("function_entry_count") ||
                 ProfName.equals("synthetic_function_entry_count"),
             "first operand should be 'function_entry_count'"
             " or 'synthetic_function_entry_count'",
             MD);

      // Check second operand.
      Assert(MD->getOperand(1) != nullptr, "second operand should not be null",
             MD);
      Assert(isa<ConstantAsMetadata>(MD->getOperand(1)),
             "expected integer argument to function_entry_count", MD);
    }
  }
}

void Verifier::visitConstantExprsRecursively(const Constant *EntryC) {
  if (!ConstantExprVisited.insert(EntryC).second)
    return;

  SmallVector<const Constant *, 16> Stack;
  Stack.push_back(EntryC);

  while (!Stack.empty()) {
    const Constant *C = Stack.pop_back_val();

    // Check this constant expression.
    if (const auto *CE = dyn_cast<ConstantExpr>(C))
      visitConstantExpr(CE);

    if (const auto *GV = dyn_cast<GlobalValue>(C)) {
      // Global Values get visited separately, but we do need to make sure
      // that the global value is in the correct module
      Assert(GV->getParent() == &M, "Referencing global in another module!",
             EntryC, &M, GV, GV->getParent());
      continue;
    }

    // Visit all sub-expressions.
    for (const Use &U : C->operands()) {
      const auto *OpC = dyn_cast<Constant>(U);
      if (!OpC)
        continue;
      if (!ConstantExprVisited.insert(OpC).second)
        continue;
      Stack.push_back(OpC);
    }
  }
}

void Verifier::visitConstantExpr(const ConstantExpr *CE) {
  if (CE->getOpcode() == Instruction::BitCast)
    Assert(CastInst::castIsValid(Instruction::BitCast, CE->getOperand(0),
                                 CE->getType()),
           "Invalid bitcast", CE);

  if (CE->getOpcode() == Instruction::IntToPtr ||
      CE->getOpcode() == Instruction::PtrToInt) {
    auto *PtrTy = CE->getOpcode() == Instruction::IntToPtr
                      ? CE->getType()
                      : CE->getOperand(0)->getType();
    StringRef Msg = CE->getOpcode() == Instruction::IntToPtr
                        ? "inttoptr not supported for non-integral pointers"
                        : "ptrtoint not supported for non-integral pointers";
    Assert(
        !DL.isNonIntegralPointerType(cast<PointerType>(PtrTy->getScalarType())),
        Msg);
  }
}

bool Verifier::verifyAttributeCount(AttributeList Attrs, unsigned Params) {
  // There shouldn't be more attribute sets than there are parameters plus the
  // function and return value.
  return Attrs.getNumAttrSets() <= Params + 2;
}

/// Verify that statepoint intrinsic is well formed.
void Verifier::verifyStatepoint(const CallBase &Call) {
  assert(Call.getCalledFunction() &&
         Call.getCalledFunction()->getIntrinsicID() ==
             Intrinsic::experimental_gc_statepoint);

  Assert(!Call.doesNotAccessMemory() && !Call.onlyReadsMemory() &&
             !Call.onlyAccessesArgMemory(),
         "gc.statepoint must read and write all memory to preserve "
         "reordering restrictions required by safepoint semantics",
         Call);

  const Value *IDV = Call.getArgOperand(0);
  Assert(isa<ConstantInt>(IDV), "gc.statepoint ID must be a constant integer",
         Call);

  const Value *NumPatchBytesV = Call.getArgOperand(1);
  Assert(isa<ConstantInt>(NumPatchBytesV),
         "gc.statepoint number of patchable bytes must be a constant integer",
         Call);
  const int64_t NumPatchBytes =
      cast<ConstantInt>(NumPatchBytesV)->getSExtValue();
  assert(isInt<32>(NumPatchBytes) && "NumPatchBytesV is an i32!");
  Assert(NumPatchBytes >= 0,
         "gc.statepoint number of patchable bytes must be "
         "positive",
         Call);

  const Value *Target = Call.getArgOperand(2);
  auto *PT = dyn_cast<PointerType>(Target->getType());
  Assert(PT && PT->getElementType()->isFunctionTy(),
         "gc.statepoint callee must be of function pointer type", Call, Target);
  FunctionType *TargetFuncType = cast<FunctionType>(PT->getElementType());

  const Value *NumCallArgsV = Call.getArgOperand(3);
  Assert(isa<ConstantInt>(NumCallArgsV),
         "gc.statepoint number of arguments to underlying call "
         "must be constant integer",
         Call);
  const int NumCallArgs = cast<ConstantInt>(NumCallArgsV)->getZExtValue();
  Assert(NumCallArgs >= 0,
         "gc.statepoint number of arguments to underlying call "
         "must be positive",
         Call);
  const int NumParams = (int)TargetFuncType->getNumParams();
  if (TargetFuncType->isVarArg()) {
    Assert(NumCallArgs >= NumParams,
           "gc.statepoint mismatch in number of vararg call args", Call);

    // TODO: Remove this limitation
    Assert(TargetFuncType->getReturnType()->isVoidTy(),
           "gc.statepoint doesn't support wrapping non-void "
           "vararg functions yet",
           Call);
  } else
    Assert(NumCallArgs == NumParams,
           "gc.statepoint mismatch in number of call args", Call);

  const Value *FlagsV = Call.getArgOperand(4);
  Assert(isa<ConstantInt>(FlagsV),
         "gc.statepoint flags must be constant integer", Call);
  const uint64_t Flags = cast<ConstantInt>(FlagsV)->getZExtValue();
  Assert((Flags & ~(uint64_t)StatepointFlags::MaskAll) == 0,
         "unknown flag used in gc.statepoint flags argument", Call);

  // Verify that the types of the call parameter arguments match
  // the type of the wrapped callee.
  AttributeList Attrs = Call.getAttributes();
  for (int i = 0; i < NumParams; i++) {
    Type *ParamType = TargetFuncType->getParamType(i);
    Type *ArgType = Call.getArgOperand(5 + i)->getType();
    Assert(ArgType == ParamType,
           "gc.statepoint call argument does not match wrapped "
           "function type",
           Call);

    if (TargetFuncType->isVarArg()) {
      AttributeSet ArgAttrs = Attrs.getParamAttributes(5 + i);
      Assert(!ArgAttrs.hasAttribute(Attribute::StructRet),
             "Attribute 'sret' cannot be used for vararg call arguments!",
             Call);
    }
  }

  const int EndCallArgsInx = 4 + NumCallArgs;

  const Value *NumTransitionArgsV = Call.getArgOperand(EndCallArgsInx + 1);
  Assert(isa<ConstantInt>(NumTransitionArgsV),
         "gc.statepoint number of transition arguments "
         "must be constant integer",
         Call);
  const int NumTransitionArgs =
      cast<ConstantInt>(NumTransitionArgsV)->getZExtValue();
  Assert(NumTransitionArgs >= 0,
         "gc.statepoint number of transition arguments must be positive", Call);
  const int EndTransitionArgsInx = EndCallArgsInx + 1 + NumTransitionArgs;

  const Value *NumDeoptArgsV = Call.getArgOperand(EndTransitionArgsInx + 1);
  Assert(isa<ConstantInt>(NumDeoptArgsV),
         "gc.statepoint number of deoptimization arguments "
         "must be constant integer",
         Call);
  const int NumDeoptArgs = cast<ConstantInt>(NumDeoptArgsV)->getZExtValue();
  Assert(NumDeoptArgs >= 0,
         "gc.statepoint number of deoptimization arguments "
         "must be positive",
         Call);

  const int ExpectedNumArgs =
      7 + NumCallArgs + NumTransitionArgs + NumDeoptArgs;
  Assert(ExpectedNumArgs <= (int)Call.arg_size(),
         "gc.statepoint too few arguments according to length fields", Call);

  // Check that the only uses of this gc.statepoint are gc.result or
  // gc.relocate calls which are tied to this statepoint and thus part
  // of the same statepoint sequence
  for (const User *U : Call.users()) {
    const CallInst *UserCall = dyn_cast<const CallInst>(U);
    Assert(UserCall, "illegal use of statepoint token", Call, U);
    if (!UserCall)
      continue;
    Assert(isa<GCRelocateInst>(UserCall) || isa<GCResultInst>(UserCall),
           "gc.result or gc.relocate are the only value uses "
           "of a gc.statepoint",
           Call, U);
    if (isa<GCResultInst>(UserCall)) {
      Assert(UserCall->getArgOperand(0) == &Call,
             "gc.result connected to wrong gc.statepoint", Call, UserCall);
    } else if (isa<GCRelocateInst>(Call)) {
      Assert(UserCall->getArgOperand(0) == &Call,
             "gc.relocate connected to wrong gc.statepoint", Call, UserCall);
    }
  }

  // Note: It is legal for a single derived pointer to be listed multiple
  // times.  It's non-optimal, but it is legal.  It can also happen after
  // insertion if we strip a bitcast away.
  // Note: It is really tempting to check that each base is relocated and
  // that a derived pointer is never reused as a base pointer.  This turns
  // out to be problematic since optimizations run after safepoint insertion
  // can recognize equality properties that the insertion logic doesn't know
  // about.  See example statepoint.ll in the verifier subdirectory
}

void Verifier::verifyFrameRecoverIndices() {
  for (auto &Counts : FrameEscapeInfo) {
    Function *F = Counts.first;
    unsigned EscapedObjectCount = Counts.second.first;
    unsigned MaxRecoveredIndex = Counts.second.second;
    Assert(MaxRecoveredIndex <= EscapedObjectCount,
           "all indices passed to llvm.localrecover must be less than the "
           "number of arguments passed ot llvm.localescape in the parent "
           "function",
           F);
  }
}

static Instruction *getSuccPad(Instruction *Terminator) {
  BasicBlock *UnwindDest;
  if (auto *II = dyn_cast<InvokeInst>(Terminator))
    UnwindDest = II->getUnwindDest();
  else if (auto *CSI = dyn_cast<CatchSwitchInst>(Terminator))
    UnwindDest = CSI->getUnwindDest();
  else
    UnwindDest = cast<CleanupReturnInst>(Terminator)->getUnwindDest();
  return UnwindDest->getFirstNonPHI();
}

void Verifier::verifySiblingFuncletUnwinds() {
  SmallPtrSet<Instruction *, 8> Visited;
  SmallPtrSet<Instruction *, 8> Active;
  for (const auto &Pair : SiblingFuncletInfo) {
    Instruction *PredPad = Pair.first;
    if (Visited.count(PredPad))
      continue;
    Active.insert(PredPad);
    Instruction *Terminator = Pair.second;
    do {
      Instruction *SuccPad = getSuccPad(Terminator);
      if (Active.count(SuccPad)) {
        // Found a cycle; report error
        Instruction *CyclePad = SuccPad;
        SmallVector<Instruction *, 8> CycleNodes;
        do {
          CycleNodes.push_back(CyclePad);
          Instruction *CycleTerminator = SiblingFuncletInfo[CyclePad];
          if (CycleTerminator != CyclePad)
            CycleNodes.push_back(CycleTerminator);
          CyclePad = getSuccPad(CycleTerminator);
        } while (CyclePad != SuccPad);
        Assert(false, "EH pads can't handle each other's exceptions",
               ArrayRef<Instruction *>(CycleNodes));
      }
      // Don't re-walk a node we've already checked
      if (!Visited.insert(SuccPad).second)
        break;
      // Walk to this successor if it has a map entry.
      PredPad = SuccPad;
      auto TermI = SiblingFuncletInfo.find(PredPad);
      if (TermI == SiblingFuncletInfo.end())
        break;
      Terminator = TermI->second;
      Active.insert(PredPad);
    } while (true);
    // Each node only has one successor, so we've walked all the active
    // nodes' successors.
    Active.clear();
  }
}

// visitFunction - Verify that a function is ok.
//
void Verifier::visitFunction(const Function &F) {
  visitGlobalValue(F);

  // Check function arguments.
  FunctionType *FT = F.getFunctionType();
  unsigned NumArgs = F.arg_size();

  Assert(&Context == &F.getContext(),
         "Function context does not match Module context!", &F);

  Assert(!F.hasCommonLinkage(), "Functions may not have common linkage", &F);
  Assert(FT->getNumParams() == NumArgs,
         "# formal arguments must match # of arguments for function type!", &F,
         FT);
  Assert(F.getReturnType()->isFirstClassType() ||
             F.getReturnType()->isVoidTy() || F.getReturnType()->isStructTy(),
         "Functions cannot return aggregate values!", &F);

  Assert(!F.hasStructRetAttr() || F.getReturnType()->isVoidTy(),
         "Invalid struct return type!", &F);

  AttributeList Attrs = F.getAttributes();

  Assert(verifyAttributeCount(Attrs, FT->getNumParams()),
         "Attribute after last parameter!", &F);

  // Check function attributes.
  verifyFunctionAttrs(FT, Attrs, &F);

  // On function declarations/definitions, we do not support the builtin
  // attribute. We do not check this in VerifyFunctionAttrs since that is
  // checking for Attributes that can/can not ever be on functions.
  Assert(!Attrs.hasFnAttribute(Attribute::Builtin),
         "Attribute 'builtin' can only be applied to a callsite.", &F);

  // Check that this function meets the restrictions on this calling convention.
  // Sometimes varargs is used for perfectly forwarding thunks, so some of these
  // restrictions can be lifted.
  switch (F.getCallingConv()) {
  default:
  case CallingConv::C:
    break;
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    Assert(F.getReturnType()->isVoidTy(),
           "Calling convention requires void return type", &F);
    LLVM_FALLTHROUGH;
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
    Assert(!F.hasStructRetAttr(),
           "Calling convention does not allow sret", &F);
    LLVM_FALLTHROUGH;
  case CallingConv::Fast:
  case CallingConv::Cold:
  case CallingConv::Intel_OCL_BI:
  case CallingConv::PTX_Kernel:
  case CallingConv::PTX_Device:
    Assert(!F.isVarArg(), "Calling convention does not support varargs or "
                          "perfect forwarding!",
           &F);
    break;
  }

  bool isLLVMdotName = F.getName().size() >= 5 &&
                       F.getName().substr(0, 5) == "llvm.";

  // Check that the argument values match the function type for this function...
  unsigned i = 0;
  for (const Argument &Arg : F.args()) {
    Assert(Arg.getType() == FT->getParamType(i),
           "Argument value does not match function argument type!", &Arg,
           FT->getParamType(i));
    Assert(Arg.getType()->isFirstClassType(),
           "Function arguments must have first-class types!", &Arg);
    if (!isLLVMdotName) {
      Assert(!Arg.getType()->isMetadataTy(),
             "Function takes metadata but isn't an intrinsic", &Arg, &F);
      Assert(!Arg.getType()->isTokenTy(),
             "Function takes token but isn't an intrinsic", &Arg, &F);
    }

    // Check that swifterror argument is only used by loads and stores.
    if (Attrs.hasParamAttribute(i, Attribute::SwiftError)) {
      verifySwiftErrorValue(&Arg);
    }
    ++i;
  }

  if (!isLLVMdotName)
    Assert(!F.getReturnType()->isTokenTy(),
           "Functions returns a token but isn't an intrinsic", &F);

  // Get the function metadata attachments.
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  assert(F.hasMetadata() != MDs.empty() && "Bit out-of-sync");
  verifyFunctionMetadata(MDs);

  // Check validity of the personality function
  if (F.hasPersonalityFn()) {
    auto *Per = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
    if (Per)
      Assert(Per->getParent() == F.getParent(),
             "Referencing personality function in another module!",
             &F, F.getParent(), Per, Per->getParent());
  }

  if (F.isMaterializable()) {
    // Function has a body somewhere we can't see.
    Assert(MDs.empty(), "unmaterialized function cannot have metadata", &F,
           MDs.empty() ? nullptr : MDs.front().second);
  } else if (F.isDeclaration()) {
    for (const auto &I : MDs) {
      AssertDI(I.first != LLVMContext::MD_dbg,
               "function declaration may not have a !dbg attachment", &F);
      Assert(I.first != LLVMContext::MD_prof,
             "function declaration may not have a !prof attachment", &F);

      // Verify the metadata itself.
      visitMDNode(*I.second);
    }
    Assert(!F.hasPersonalityFn(),
           "Function declaration shouldn't have a personality routine", &F);
  } else {
    // Verify that this function (which has a body) is not named "llvm.*".  It
    // is not legal to define intrinsics.
    Assert(!isLLVMdotName, "llvm intrinsics cannot be defined!", &F);

    // Check the entry node
    const BasicBlock *Entry = &F.getEntryBlock();
    Assert(pred_empty(Entry),
           "Entry block to function must not have predecessors!", Entry);

    // The address of the entry block cannot be taken, unless it is dead.
    if (Entry->hasAddressTaken()) {
      Assert(!BlockAddress::lookup(Entry)->isConstantUsed(),
             "blockaddress may not be used with the entry block!", Entry);
    }

    unsigned NumDebugAttachments = 0, NumProfAttachments = 0;
    // Visit metadata attachments.
    for (const auto &I : MDs) {
      // Verify that the attachment is legal.
      switch (I.first) {
      default:
        break;
      case LLVMContext::MD_dbg: {
        ++NumDebugAttachments;
        AssertDI(NumDebugAttachments == 1,
                 "function must have a single !dbg attachment", &F, I.second);
        AssertDI(isa<DISubprogram>(I.second),
                 "function !dbg attachment must be a subprogram", &F, I.second);
        auto *SP = cast<DISubprogram>(I.second);
        const Function *&AttachedTo = DISubprogramAttachments[SP];
        AssertDI(!AttachedTo || AttachedTo == &F,
                 "DISubprogram attached to more than one function", SP, &F);
        AttachedTo = &F;
        break;
      }
      case LLVMContext::MD_prof:
        ++NumProfAttachments;
        Assert(NumProfAttachments == 1,
               "function must have a single !prof attachment", &F, I.second);
        break;
      }

      // Verify the metadata itself.
      visitMDNode(*I.second);
    }
  }

  // If this function is actually an intrinsic, verify that it is only used in
  // direct call/invokes, never having its "address taken".
  // Only do this if the module is materialized, otherwise we don't have all the
  // uses.
  if (F.getIntrinsicID() && F.getParent()->isMaterialized()) {
    const User *U;
    if (F.hasAddressTaken(&U))
      Assert(false, "Invalid user of intrinsic instruction!", U);
  }

  auto *N = F.getSubprogram();
  HasDebugInfo = (N != nullptr);
  if (!HasDebugInfo)
    return;

  // Check that all !dbg attachments lead to back to N (or, at least, another
  // subprogram that describes the same function).
  //
  // FIXME: Check this incrementally while visiting !dbg attachments.
  // FIXME: Only check when N is the canonical subprogram for F.
  SmallPtrSet<const MDNode *, 32> Seen;
  for (auto &BB : F)
    for (auto &I : BB) {
      // Be careful about using DILocation here since we might be dealing with
      // broken code (this is the Verifier after all).
      DILocation *DL =
          dyn_cast_or_null<DILocation>(I.getDebugLoc().getAsMDNode());
      if (!DL)
        continue;
      if (!Seen.insert(DL).second)
        continue;

      Metadata *Parent = DL->getRawScope();
      AssertDI(Parent && isa<DILocalScope>(Parent),
               "DILocation's scope must be a DILocalScope", N, &F, &I, DL,
               Parent);
      DILocalScope *Scope = DL->getInlinedAtScope();
      if (Scope && !Seen.insert(Scope).second)
        continue;

      DISubprogram *SP = Scope ? Scope->getSubprogram() : nullptr;

      // Scope and SP could be the same MDNode and we don't want to skip
      // validation in that case
      if (SP && ((Scope != SP) && !Seen.insert(SP).second))
        continue;

      // FIXME: Once N is canonical, check "SP == &N".
      AssertDI(SP->describes(&F),
               "!dbg attachment points at wrong subprogram for function", N, &F,
               &I, DL, Scope, SP);
    }
}

// verifyBasicBlock - Verify that a basic block is well formed...
//
void Verifier::visitBasicBlock(BasicBlock &BB) {
  InstsInThisBlock.clear();

  // Ensure that basic blocks have terminators!
  Assert(BB.getTerminator(), "Basic Block does not have terminator!", &BB);

  // Check constraints that this basic block imposes on all of the PHI nodes in
  // it.
  if (isa<PHINode>(BB.front())) {
    SmallVector<BasicBlock*, 8> Preds(pred_begin(&BB), pred_end(&BB));
    SmallVector<std::pair<BasicBlock*, Value*>, 8> Values;
    llvm::sort(Preds);
    for (const PHINode &PN : BB.phis()) {
      // Ensure that PHI nodes have at least one entry!
      Assert(PN.getNumIncomingValues() != 0,
             "PHI nodes must have at least one entry.  If the block is dead, "
             "the PHI should be removed!",
             &PN);
      Assert(PN.getNumIncomingValues() == Preds.size(),
             "PHINode should have one entry for each predecessor of its "
             "parent basic block!",
             &PN);

      // Get and sort all incoming values in the PHI node...
      Values.clear();
      Values.reserve(PN.getNumIncomingValues());
      for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i)
        Values.push_back(
            std::make_pair(PN.getIncomingBlock(i), PN.getIncomingValue(i)));
      llvm::sort(Values);

      for (unsigned i = 0, e = Values.size(); i != e; ++i) {
        // Check to make sure that if there is more than one entry for a
        // particular basic block in this PHI node, that the incoming values are
        // all identical.
        //
        Assert(i == 0 || Values[i].first != Values[i - 1].first ||
                   Values[i].second == Values[i - 1].second,
               "PHI node has multiple entries for the same basic block with "
               "different incoming values!",
               &PN, Values[i].first, Values[i].second, Values[i - 1].second);

        // Check to make sure that the predecessors and PHI node entries are
        // matched up.
        Assert(Values[i].first == Preds[i],
               "PHI node entries do not match predecessors!", &PN,
               Values[i].first, Preds[i]);
      }
    }
  }

  // Check that all instructions have their parent pointers set up correctly.
  for (auto &I : BB)
  {
    Assert(I.getParent() == &BB, "Instruction has bogus parent pointer!");
  }
}

void Verifier::visitTerminator(Instruction &I) {
  // Ensure that terminators only exist at the end of the basic block.
  Assert(&I == I.getParent()->getTerminator(),
         "Terminator found in the middle of a basic block!", I.getParent());
  visitInstruction(I);
}

void Verifier::visitBranchInst(BranchInst &BI) {
  if (BI.isConditional()) {
    Assert(BI.getCondition()->getType()->isIntegerTy(1),
           "Branch condition is not 'i1' type!", &BI, BI.getCondition());
  }
  visitTerminator(BI);
}

void Verifier::visitReturnInst(ReturnInst &RI) {
  Function *F = RI.getParent()->getParent();
  unsigned N = RI.getNumOperands();
  if (F->getReturnType()->isVoidTy())
    Assert(N == 0,
           "Found return instr that returns non-void in Function of void "
           "return type!",
           &RI, F->getReturnType());
  else
    Assert(N == 1 && F->getReturnType() == RI.getOperand(0)->getType(),
           "Function return type does not match operand "
           "type of return inst!",
           &RI, F->getReturnType());

  // Check to make sure that the return value has necessary properties for
  // terminators...
  visitTerminator(RI);
}

void Verifier::visitSwitchInst(SwitchInst &SI) {
  // Check to make sure that all of the constants in the switch instruction
  // have the same type as the switched-on value.
  Type *SwitchTy = SI.getCondition()->getType();
  SmallPtrSet<ConstantInt*, 32> Constants;
  for (auto &Case : SI.cases()) {
    Assert(Case.getCaseValue()->getType() == SwitchTy,
           "Switch constants must all be same type as switch value!", &SI);
    Assert(Constants.insert(Case.getCaseValue()).second,
           "Duplicate integer as switch case", &SI, Case.getCaseValue());
  }

  visitTerminator(SI);
}

void Verifier::visitIndirectBrInst(IndirectBrInst &BI) {
  Assert(BI.getAddress()->getType()->isPointerTy(),
         "Indirectbr operand must have pointer type!", &BI);
  for (unsigned i = 0, e = BI.getNumDestinations(); i != e; ++i)
    Assert(BI.getDestination(i)->getType()->isLabelTy(),
           "Indirectbr destinations must all have pointer type!", &BI);

  visitTerminator(BI);
}

void Verifier::visitSelectInst(SelectInst &SI) {
  Assert(!SelectInst::areInvalidOperands(SI.getOperand(0), SI.getOperand(1),
                                         SI.getOperand(2)),
         "Invalid operands for select instruction!", &SI);

  Assert(SI.getTrueValue()->getType() == SI.getType(),
         "Select values must have same type as select instruction!", &SI);
  visitInstruction(SI);
}

/// visitUserOp1 - User defined operators shouldn't live beyond the lifetime of
/// a pass, if any exist, it's an error.
///
void Verifier::visitUserOp1(Instruction &I) {
  Assert(false, "User-defined operators should not live outside of a pass!", &I);
}

void Verifier::visitTruncInst(TruncInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Assert(SrcTy->isIntOrIntVectorTy(), "Trunc only operates on integer", &I);
  Assert(DestTy->isIntOrIntVectorTy(), "Trunc only produces integer", &I);
  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(),
         "trunc source and destination must both be a vector or neither", &I);
  Assert(SrcBitSize > DestBitSize, "DestTy too big for Trunc", &I);

  visitInstruction(I);
}

void Verifier::visitZExtInst(ZExtInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  Assert(SrcTy->isIntOrIntVectorTy(), "ZExt only operates on integer", &I);
  Assert(DestTy->isIntOrIntVectorTy(), "ZExt only produces an integer", &I);
  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(),
         "zext source and destination must both be a vector or neither", &I);
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Assert(SrcBitSize < DestBitSize, "Type too small for ZExt", &I);

  visitInstruction(I);
}

void Verifier::visitSExtInst(SExtInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Assert(SrcTy->isIntOrIntVectorTy(), "SExt only operates on integer", &I);
  Assert(DestTy->isIntOrIntVectorTy(), "SExt only produces an integer", &I);
  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(),
         "sext source and destination must both be a vector or neither", &I);
  Assert(SrcBitSize < DestBitSize, "Type too small for SExt", &I);

  visitInstruction(I);
}

void Verifier::visitFPTruncInst(FPTruncInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();
  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Assert(SrcTy->isFPOrFPVectorTy(), "FPTrunc only operates on FP", &I);
  Assert(DestTy->isFPOrFPVectorTy(), "FPTrunc only produces an FP", &I);
  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(),
         "fptrunc source and destination must both be a vector or neither", &I);
  Assert(SrcBitSize > DestBitSize, "DestTy too big for FPTrunc", &I);

  visitInstruction(I);
}

void Verifier::visitFPExtInst(FPExtInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Assert(SrcTy->isFPOrFPVectorTy(), "FPExt only operates on FP", &I);
  Assert(DestTy->isFPOrFPVectorTy(), "FPExt only produces an FP", &I);
  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(),
         "fpext source and destination must both be a vector or neither", &I);
  Assert(SrcBitSize < DestBitSize, "DestTy too small for FPExt", &I);

  visitInstruction(I);
}

void Verifier::visitUIToFPInst(UIToFPInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Assert(SrcVec == DstVec,
         "UIToFP source and dest must both be vector or scalar", &I);
  Assert(SrcTy->isIntOrIntVectorTy(),
         "UIToFP source must be integer or integer vector", &I);
  Assert(DestTy->isFPOrFPVectorTy(), "UIToFP result must be FP or FP vector",
         &I);

  if (SrcVec && DstVec)
    Assert(cast<VectorType>(SrcTy)->getNumElements() ==
               cast<VectorType>(DestTy)->getNumElements(),
           "UIToFP source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitSIToFPInst(SIToFPInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Assert(SrcVec == DstVec,
         "SIToFP source and dest must both be vector or scalar", &I);
  Assert(SrcTy->isIntOrIntVectorTy(),
         "SIToFP source must be integer or integer vector", &I);
  Assert(DestTy->isFPOrFPVectorTy(), "SIToFP result must be FP or FP vector",
         &I);

  if (SrcVec && DstVec)
    Assert(cast<VectorType>(SrcTy)->getNumElements() ==
               cast<VectorType>(DestTy)->getNumElements(),
           "SIToFP source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitFPToUIInst(FPToUIInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Assert(SrcVec == DstVec,
         "FPToUI source and dest must both be vector or scalar", &I);
  Assert(SrcTy->isFPOrFPVectorTy(), "FPToUI source must be FP or FP vector",
         &I);
  Assert(DestTy->isIntOrIntVectorTy(),
         "FPToUI result must be integer or integer vector", &I);

  if (SrcVec && DstVec)
    Assert(cast<VectorType>(SrcTy)->getNumElements() ==
               cast<VectorType>(DestTy)->getNumElements(),
           "FPToUI source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitFPToSIInst(FPToSIInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Assert(SrcVec == DstVec,
         "FPToSI source and dest must both be vector or scalar", &I);
  Assert(SrcTy->isFPOrFPVectorTy(), "FPToSI source must be FP or FP vector",
         &I);
  Assert(DestTy->isIntOrIntVectorTy(),
         "FPToSI result must be integer or integer vector", &I);

  if (SrcVec && DstVec)
    Assert(cast<VectorType>(SrcTy)->getNumElements() ==
               cast<VectorType>(DestTy)->getNumElements(),
           "FPToSI source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitPtrToIntInst(PtrToIntInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  Assert(SrcTy->isPtrOrPtrVectorTy(), "PtrToInt source must be pointer", &I);

  if (auto *PTy = dyn_cast<PointerType>(SrcTy->getScalarType()))
    Assert(!DL.isNonIntegralPointerType(PTy),
           "ptrtoint not supported for non-integral pointers");

  Assert(DestTy->isIntOrIntVectorTy(), "PtrToInt result must be integral", &I);
  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(), "PtrToInt type mismatch",
         &I);

  if (SrcTy->isVectorTy()) {
    VectorType *VSrc = dyn_cast<VectorType>(SrcTy);
    VectorType *VDest = dyn_cast<VectorType>(DestTy);
    Assert(VSrc->getNumElements() == VDest->getNumElements(),
           "PtrToInt Vector width mismatch", &I);
  }

  visitInstruction(I);
}

void Verifier::visitIntToPtrInst(IntToPtrInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  Assert(SrcTy->isIntOrIntVectorTy(),
         "IntToPtr source must be an integral", &I);
  Assert(DestTy->isPtrOrPtrVectorTy(), "IntToPtr result must be a pointer", &I);

  if (auto *PTy = dyn_cast<PointerType>(DestTy->getScalarType()))
    Assert(!DL.isNonIntegralPointerType(PTy),
           "inttoptr not supported for non-integral pointers");

  Assert(SrcTy->isVectorTy() == DestTy->isVectorTy(), "IntToPtr type mismatch",
         &I);
  if (SrcTy->isVectorTy()) {
    VectorType *VSrc = dyn_cast<VectorType>(SrcTy);
    VectorType *VDest = dyn_cast<VectorType>(DestTy);
    Assert(VSrc->getNumElements() == VDest->getNumElements(),
           "IntToPtr Vector width mismatch", &I);
  }
  visitInstruction(I);
}

void Verifier::visitBitCastInst(BitCastInst &I) {
  Assert(
      CastInst::castIsValid(Instruction::BitCast, I.getOperand(0), I.getType()),
      "Invalid bitcast", &I);
  visitInstruction(I);
}

void Verifier::visitAddrSpaceCastInst(AddrSpaceCastInst &I) {
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  Assert(SrcTy->isPtrOrPtrVectorTy(), "AddrSpaceCast source must be a pointer",
         &I);
  Assert(DestTy->isPtrOrPtrVectorTy(), "AddrSpaceCast result must be a pointer",
         &I);
  Assert(SrcTy->getPointerAddressSpace() != DestTy->getPointerAddressSpace(),
         "AddrSpaceCast must be between different address spaces", &I);
  if (SrcTy->isVectorTy())
    Assert(SrcTy->getVectorNumElements() == DestTy->getVectorNumElements(),
           "AddrSpaceCast vector pointer number of elements mismatch", &I);
  visitInstruction(I);
}

/// visitPHINode - Ensure that a PHI node is well formed.
///
void Verifier::visitPHINode(PHINode &PN) {
  // Ensure that the PHI nodes are all grouped together at the top of the block.
  // This can be tested by checking whether the instruction before this is
  // either nonexistent (because this is begin()) or is a PHI node.  If not,
  // then there is some other instruction before a PHI.
  Assert(&PN == &PN.getParent()->front() ||
             isa<PHINode>(--BasicBlock::iterator(&PN)),
         "PHI nodes not grouped at top of basic block!", &PN, PN.getParent());

  // Check that a PHI doesn't yield a Token.
  Assert(!PN.getType()->isTokenTy(), "PHI nodes cannot have token type!");

  // Check that all of the values of the PHI node have the same type as the
  // result, and that the incoming blocks are really basic blocks.
  for (Value *IncValue : PN.incoming_values()) {
    Assert(PN.getType() == IncValue->getType(),
           "PHI node operands are not the same type as the result!", &PN);
  }

  // All other PHI node constraints are checked in the visitBasicBlock method.

  visitInstruction(PN);
}

void Verifier::visitCallBase(CallBase &Call) {
  Assert(Call.getCalledValue()->getType()->isPointerTy(),
         "Called function must be a pointer!", Call);
  PointerType *FPTy = cast<PointerType>(Call.getCalledValue()->getType());

  Assert(FPTy->getElementType()->isFunctionTy(),
         "Called function is not pointer to function type!", Call);

  Assert(FPTy->getElementType() == Call.getFunctionType(),
         "Called function is not the same type as the call!", Call);

  FunctionType *FTy = Call.getFunctionType();

  // Verify that the correct number of arguments are being passed
  if (FTy->isVarArg())
    Assert(Call.arg_size() >= FTy->getNumParams(),
           "Called function requires more parameters than were provided!",
           Call);
  else
    Assert(Call.arg_size() == FTy->getNumParams(),
           "Incorrect number of arguments passed to called function!", Call);

  // Verify that all arguments to the call match the function type.
  for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
    Assert(Call.getArgOperand(i)->getType() == FTy->getParamType(i),
           "Call parameter type does not match function signature!",
           Call.getArgOperand(i), FTy->getParamType(i), Call);

  AttributeList Attrs = Call.getAttributes();

  Assert(verifyAttributeCount(Attrs, Call.arg_size()),
         "Attribute after last parameter!", Call);

  if (Attrs.hasAttribute(AttributeList::FunctionIndex, Attribute::Speculatable)) {
    // Don't allow speculatable on call sites, unless the underlying function
    // declaration is also speculatable.
    Function *Callee =
        dyn_cast<Function>(Call.getCalledValue()->stripPointerCasts());
    Assert(Callee && Callee->isSpeculatable(),
           "speculatable attribute may not apply to call sites", Call);
  }

  // Verify call attributes.
  verifyFunctionAttrs(FTy, Attrs, &Call);

  // Conservatively check the inalloca argument.
  // We have a bug if we can find that there is an underlying alloca without
  // inalloca.
  if (Call.hasInAllocaArgument()) {
    Value *InAllocaArg = Call.getArgOperand(FTy->getNumParams() - 1);
    if (auto AI = dyn_cast<AllocaInst>(InAllocaArg->stripInBoundsOffsets()))
      Assert(AI->isUsedWithInAlloca(),
             "inalloca argument for call has mismatched alloca", AI, Call);
  }

  // For each argument of the callsite, if it has the swifterror argument,
  // make sure the underlying alloca/parameter it comes from has a swifterror as
  // well.
  for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
    if (Call.paramHasAttr(i, Attribute::SwiftError)) {
      Value *SwiftErrorArg = Call.getArgOperand(i);
      if (auto AI = dyn_cast<AllocaInst>(SwiftErrorArg->stripInBoundsOffsets())) {
        Assert(AI->isSwiftError(),
               "swifterror argument for call has mismatched alloca", AI, Call);
        continue;
      }
      auto ArgI = dyn_cast<Argument>(SwiftErrorArg);
      Assert(ArgI,
             "swifterror argument should come from an alloca or parameter",
             SwiftErrorArg, Call);
      Assert(ArgI->hasSwiftErrorAttr(),
             "swifterror argument for call has mismatched parameter", ArgI,
             Call);
    }

  if (FTy->isVarArg()) {
    // FIXME? is 'nest' even legal here?
    bool SawNest = false;
    bool SawReturned = false;

    for (unsigned Idx = 0; Idx < FTy->getNumParams(); ++Idx) {
      if (Attrs.hasParamAttribute(Idx, Attribute::Nest))
        SawNest = true;
      if (Attrs.hasParamAttribute(Idx, Attribute::Returned))
        SawReturned = true;
    }

    // Check attributes on the varargs part.
    for (unsigned Idx = FTy->getNumParams(); Idx < Call.arg_size(); ++Idx) {
      Type *Ty = Call.getArgOperand(Idx)->getType();
      AttributeSet ArgAttrs = Attrs.getParamAttributes(Idx);
      verifyParameterAttrs(ArgAttrs, Ty, &Call);

      if (ArgAttrs.hasAttribute(Attribute::Nest)) {
        Assert(!SawNest, "More than one parameter has attribute nest!", Call);
        SawNest = true;
      }

      if (ArgAttrs.hasAttribute(Attribute::Returned)) {
        Assert(!SawReturned, "More than one parameter has attribute returned!",
               Call);
        Assert(Ty->canLosslesslyBitCastTo(FTy->getReturnType()),
               "Incompatible argument and return types for 'returned' "
               "attribute",
               Call);
        SawReturned = true;
      }

      // Statepoint intrinsic is vararg but the wrapped function may be not.
      // Allow sret here and check the wrapped function in verifyStatepoint.
      if (!Call.getCalledFunction() ||
          Call.getCalledFunction()->getIntrinsicID() !=
              Intrinsic::experimental_gc_statepoint)
        Assert(!ArgAttrs.hasAttribute(Attribute::StructRet),
               "Attribute 'sret' cannot be used for vararg call arguments!",
               Call);

      if (ArgAttrs.hasAttribute(Attribute::InAlloca))
        Assert(Idx == Call.arg_size() - 1,
               "inalloca isn't on the last argument!", Call);
    }
  }

  // Verify that there's no metadata unless it's a direct call to an intrinsic.
  if (!Call.getCalledFunction() ||
      !Call.getCalledFunction()->getName().startswith("llvm.")) {
    for (Type *ParamTy : FTy->params()) {
      Assert(!ParamTy->isMetadataTy(),
             "Function has metadata parameter but isn't an intrinsic", Call);
      Assert(!ParamTy->isTokenTy(),
             "Function has token parameter but isn't an intrinsic", Call);
    }
  }

  // Verify that indirect calls don't return tokens.
  if (!Call.getCalledFunction())
    Assert(!FTy->getReturnType()->isTokenTy(),
           "Return type cannot be token for indirect call!");

  if (Function *F = Call.getCalledFunction())
    if (Intrinsic::ID ID = (Intrinsic::ID)F->getIntrinsicID())
      visitIntrinsicCall(ID, Call);

  // Verify that a callsite has at most one "deopt", at most one "funclet" and
  // at most one "gc-transition" operand bundle.
  bool FoundDeoptBundle = false, FoundFuncletBundle = false,
       FoundGCTransitionBundle = false;
  for (unsigned i = 0, e = Call.getNumOperandBundles(); i < e; ++i) {
    OperandBundleUse BU = Call.getOperandBundleAt(i);
    uint32_t Tag = BU.getTagID();
    if (Tag == LLVMContext::OB_deopt) {
      Assert(!FoundDeoptBundle, "Multiple deopt operand bundles", Call);
      FoundDeoptBundle = true;
    } else if (Tag == LLVMContext::OB_gc_transition) {
      Assert(!FoundGCTransitionBundle, "Multiple gc-transition operand bundles",
             Call);
      FoundGCTransitionBundle = true;
    } else if (Tag == LLVMContext::OB_funclet) {
      Assert(!FoundFuncletBundle, "Multiple funclet operand bundles", Call);
      FoundFuncletBundle = true;
      Assert(BU.Inputs.size() == 1,
             "Expected exactly one funclet bundle operand", Call);
      Assert(isa<FuncletPadInst>(BU.Inputs.front()),
             "Funclet bundle operands should correspond to a FuncletPadInst",
             Call);
    }
  }

  // Verify that each inlinable callsite of a debug-info-bearing function in a
  // debug-info-bearing function has a debug location attached to it. Failure to
  // do so causes assertion failures when the inliner sets up inline scope info.
  if (Call.getFunction()->getSubprogram() && Call.getCalledFunction() &&
      Call.getCalledFunction()->getSubprogram())
    AssertDI(Call.getDebugLoc(),
             "inlinable function call in a function with "
             "debug info must have a !dbg location",
             Call);

  visitInstruction(Call);
}

/// Two types are "congruent" if they are identical, or if they are both pointer
/// types with different pointee types and the same address space.
static bool isTypeCongruent(Type *L, Type *R) {
  if (L == R)
    return true;
  PointerType *PL = dyn_cast<PointerType>(L);
  PointerType *PR = dyn_cast<PointerType>(R);
  if (!PL || !PR)
    return false;
  return PL->getAddressSpace() == PR->getAddressSpace();
}

static AttrBuilder getParameterABIAttributes(int I, AttributeList Attrs) {
  static const Attribute::AttrKind ABIAttrs[] = {
      Attribute::StructRet, Attribute::ByVal, Attribute::InAlloca,
      Attribute::InReg, Attribute::Returned, Attribute::SwiftSelf,
      Attribute::SwiftError};
  AttrBuilder Copy;
  for (auto AK : ABIAttrs) {
    if (Attrs.hasParamAttribute(I, AK))
      Copy.addAttribute(AK);
  }
  if (Attrs.hasParamAttribute(I, Attribute::Alignment))
    Copy.addAlignmentAttr(Attrs.getParamAlignment(I));
  return Copy;
}

void Verifier::verifyMustTailCall(CallInst &CI) {
  Assert(!CI.isInlineAsm(), "cannot use musttail call with inline asm", &CI);

  // - The caller and callee prototypes must match.  Pointer types of
  //   parameters or return types may differ in pointee type, but not
  //   address space.
  Function *F = CI.getParent()->getParent();
  FunctionType *CallerTy = F->getFunctionType();
  FunctionType *CalleeTy = CI.getFunctionType();
  if (!CI.getCalledFunction() || !CI.getCalledFunction()->isIntrinsic()) {
    Assert(CallerTy->getNumParams() == CalleeTy->getNumParams(),
           "cannot guarantee tail call due to mismatched parameter counts",
           &CI);
    for (int I = 0, E = CallerTy->getNumParams(); I != E; ++I) {
      Assert(
          isTypeCongruent(CallerTy->getParamType(I), CalleeTy->getParamType(I)),
          "cannot guarantee tail call due to mismatched parameter types", &CI);
    }
  }
  Assert(CallerTy->isVarArg() == CalleeTy->isVarArg(),
         "cannot guarantee tail call due to mismatched varargs", &CI);
  Assert(isTypeCongruent(CallerTy->getReturnType(), CalleeTy->getReturnType()),
         "cannot guarantee tail call due to mismatched return types", &CI);

  // - The calling conventions of the caller and callee must match.
  Assert(F->getCallingConv() == CI.getCallingConv(),
         "cannot guarantee tail call due to mismatched calling conv", &CI);

  // - All ABI-impacting function attributes, such as sret, byval, inreg,
  //   returned, and inalloca, must match.
  AttributeList CallerAttrs = F->getAttributes();
  AttributeList CalleeAttrs = CI.getAttributes();
  for (int I = 0, E = CallerTy->getNumParams(); I != E; ++I) {
    AttrBuilder CallerABIAttrs = getParameterABIAttributes(I, CallerAttrs);
    AttrBuilder CalleeABIAttrs = getParameterABIAttributes(I, CalleeAttrs);
    Assert(CallerABIAttrs == CalleeABIAttrs,
           "cannot guarantee tail call due to mismatched ABI impacting "
           "function attributes",
           &CI, CI.getOperand(I));
  }

  // - The call must immediately precede a :ref:`ret <i_ret>` instruction,
  //   or a pointer bitcast followed by a ret instruction.
  // - The ret instruction must return the (possibly bitcasted) value
  //   produced by the call or void.
  Value *RetVal = &CI;
  Instruction *Next = CI.getNextNode();

  // Handle the optional bitcast.
  if (BitCastInst *BI = dyn_cast_or_null<BitCastInst>(Next)) {
    Assert(BI->getOperand(0) == RetVal,
           "bitcast following musttail call must use the call", BI);
    RetVal = BI;
    Next = BI->getNextNode();
  }

  // Check the return.
  ReturnInst *Ret = dyn_cast_or_null<ReturnInst>(Next);
  Assert(Ret, "musttail call must precede a ret with an optional bitcast",
         &CI);
  Assert(!Ret->getReturnValue() || Ret->getReturnValue() == RetVal,
         "musttail call result must be returned", Ret);
}

void Verifier::visitCallInst(CallInst &CI) {
  visitCallBase(CI);

  if (CI.isMustTailCall())
    verifyMustTailCall(CI);
}

void Verifier::visitInvokeInst(InvokeInst &II) {
  visitCallBase(II);

  // Verify that the first non-PHI instruction of the unwind destination is an
  // exception handling instruction.
  Assert(
      II.getUnwindDest()->isEHPad(),
      "The unwind destination does not have an exception handling instruction!",
      &II);

  visitTerminator(II);
}

/// visitUnaryOperator - Check the argument to the unary operator.
///
void Verifier::visitUnaryOperator(UnaryOperator &U) {
  Assert(U.getType() == U.getOperand(0)->getType(), 
         "Unary operators must have same type for"
         "operands and result!",
         &U);

  switch (U.getOpcode()) {
  // Check that floating-point arithmetic operators are only used with
  // floating-point operands.
  case Instruction::FNeg:
    Assert(U.getType()->isFPOrFPVectorTy(),
           "FNeg operator only works with float types!", &U);
    break;
  default:
    llvm_unreachable("Unknown UnaryOperator opcode!");
  }

  visitInstruction(U);
}

/// visitBinaryOperator - Check that both arguments to the binary operator are
/// of the same type!
///
void Verifier::visitBinaryOperator(BinaryOperator &B) {
  Assert(B.getOperand(0)->getType() == B.getOperand(1)->getType(),
         "Both operands to a binary operator are not of the same type!", &B);

  switch (B.getOpcode()) {
  // Check that integer arithmetic operators are only used with
  // integral operands.
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::SDiv:
  case Instruction::UDiv:
  case Instruction::SRem:
  case Instruction::URem:
    Assert(B.getType()->isIntOrIntVectorTy(),
           "Integer arithmetic operators only work with integral types!", &B);
    Assert(B.getType() == B.getOperand(0)->getType(),
           "Integer arithmetic operators must have same type "
           "for operands and result!",
           &B);
    break;
  // Check that floating-point arithmetic operators are only used with
  // floating-point operands.
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
    Assert(B.getType()->isFPOrFPVectorTy(),
           "Floating-point arithmetic operators only work with "
           "floating-point types!",
           &B);
    Assert(B.getType() == B.getOperand(0)->getType(),
           "Floating-point arithmetic operators must have same type "
           "for operands and result!",
           &B);
    break;
  // Check that logical operators are only used with integral operands.
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    Assert(B.getType()->isIntOrIntVectorTy(),
           "Logical operators only work with integral types!", &B);
    Assert(B.getType() == B.getOperand(0)->getType(),
           "Logical operators must have same type for operands and result!",
           &B);
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    Assert(B.getType()->isIntOrIntVectorTy(),
           "Shifts only work with integral types!", &B);
    Assert(B.getType() == B.getOperand(0)->getType(),
           "Shift return type must be same as operands!", &B);
    break;
  default:
    llvm_unreachable("Unknown BinaryOperator opcode!");
  }

  visitInstruction(B);
}

void Verifier::visitICmpInst(ICmpInst &IC) {
  // Check that the operands are the same type
  Type *Op0Ty = IC.getOperand(0)->getType();
  Type *Op1Ty = IC.getOperand(1)->getType();
  Assert(Op0Ty == Op1Ty,
         "Both operands to ICmp instruction are not of the same type!", &IC);
  // Check that the operands are the right type
  Assert(Op0Ty->isIntOrIntVectorTy() || Op0Ty->isPtrOrPtrVectorTy(),
         "Invalid operand types for ICmp instruction", &IC);
  // Check that the predicate is valid.
  Assert(IC.isIntPredicate(),
         "Invalid predicate in ICmp instruction!", &IC);

  visitInstruction(IC);
}

void Verifier::visitFCmpInst(FCmpInst &FC) {
  // Check that the operands are the same type
  Type *Op0Ty = FC.getOperand(0)->getType();
  Type *Op1Ty = FC.getOperand(1)->getType();
  Assert(Op0Ty == Op1Ty,
         "Both operands to FCmp instruction are not of the same type!", &FC);
  // Check that the operands are the right type
  Assert(Op0Ty->isFPOrFPVectorTy(),
         "Invalid operand types for FCmp instruction", &FC);
  // Check that the predicate is valid.
  Assert(FC.isFPPredicate(),
         "Invalid predicate in FCmp instruction!", &FC);

  visitInstruction(FC);
}

void Verifier::visitExtractElementInst(ExtractElementInst &EI) {
  Assert(
      ExtractElementInst::isValidOperands(EI.getOperand(0), EI.getOperand(1)),
      "Invalid extractelement operands!", &EI);
  visitInstruction(EI);
}

void Verifier::visitInsertElementInst(InsertElementInst &IE) {
  Assert(InsertElementInst::isValidOperands(IE.getOperand(0), IE.getOperand(1),
                                            IE.getOperand(2)),
         "Invalid insertelement operands!", &IE);
  visitInstruction(IE);
}

void Verifier::visitShuffleVectorInst(ShuffleVectorInst &SV) {
  Assert(ShuffleVectorInst::isValidOperands(SV.getOperand(0), SV.getOperand(1),
                                            SV.getOperand(2)),
         "Invalid shufflevector operands!", &SV);
  visitInstruction(SV);
}

void Verifier::visitGetElementPtrInst(GetElementPtrInst &GEP) {
  Type *TargetTy = GEP.getPointerOperandType()->getScalarType();

  Assert(isa<PointerType>(TargetTy),
         "GEP base pointer is not a vector or a vector of pointers", &GEP);
  Assert(GEP.getSourceElementType()->isSized(), "GEP into unsized type!", &GEP);

  SmallVector<Value*, 16> Idxs(GEP.idx_begin(), GEP.idx_end());
  Assert(all_of(
      Idxs, [](Value* V) { return V->getType()->isIntOrIntVectorTy(); }),
      "GEP indexes must be integers", &GEP);
  Type *ElTy =
      GetElementPtrInst::getIndexedType(GEP.getSourceElementType(), Idxs);
  Assert(ElTy, "Invalid indices for GEP pointer type!", &GEP);

  Assert(GEP.getType()->isPtrOrPtrVectorTy() &&
             GEP.getResultElementType() == ElTy,
         "GEP is not of right type for indices!", &GEP, ElTy);

  if (GEP.getType()->isVectorTy()) {
    // Additional checks for vector GEPs.
    unsigned GEPWidth = GEP.getType()->getVectorNumElements();
    if (GEP.getPointerOperandType()->isVectorTy())
      Assert(GEPWidth == GEP.getPointerOperandType()->getVectorNumElements(),
             "Vector GEP result width doesn't match operand's", &GEP);
    for (Value *Idx : Idxs) {
      Type *IndexTy = Idx->getType();
      if (IndexTy->isVectorTy()) {
        unsigned IndexWidth = IndexTy->getVectorNumElements();
        Assert(IndexWidth == GEPWidth, "Invalid GEP index vector width", &GEP);
      }
      Assert(IndexTy->isIntOrIntVectorTy(),
             "All GEP indices should be of integer type");
    }
  }

  if (auto *PTy = dyn_cast<PointerType>(GEP.getType())) {
    Assert(GEP.getAddressSpace() == PTy->getAddressSpace(),
           "GEP address space doesn't match type", &GEP);
  }

  visitInstruction(GEP);
}

static bool isContiguous(const ConstantRange &A, const ConstantRange &B) {
  return A.getUpper() == B.getLower() || A.getLower() == B.getUpper();
}

void Verifier::visitRangeMetadata(Instruction &I, MDNode *Range, Type *Ty) {
  assert(Range && Range == I.getMetadata(LLVMContext::MD_range) &&
         "precondition violation");

  unsigned NumOperands = Range->getNumOperands();
  Assert(NumOperands % 2 == 0, "Unfinished range!", Range);
  unsigned NumRanges = NumOperands / 2;
  Assert(NumRanges >= 1, "It should have at least one range!", Range);

  ConstantRange LastRange(1); // Dummy initial value
  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Low =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(2 * i));
    Assert(Low, "The lower limit must be an integer!", Low);
    ConstantInt *High =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(2 * i + 1));
    Assert(High, "The upper limit must be an integer!", High);
    Assert(High->getType() == Low->getType() && High->getType() == Ty,
           "Range types must match instruction type!", &I);

    APInt HighV = High->getValue();
    APInt LowV = Low->getValue();
    ConstantRange CurRange(LowV, HighV);
    Assert(!CurRange.isEmptySet() && !CurRange.isFullSet(),
           "Range must not be empty!", Range);
    if (i != 0) {
      Assert(CurRange.intersectWith(LastRange).isEmptySet(),
             "Intervals are overlapping", Range);
      Assert(LowV.sgt(LastRange.getLower()), "Intervals are not in order",
             Range);
      Assert(!isContiguous(CurRange, LastRange), "Intervals are contiguous",
             Range);
    }
    LastRange = ConstantRange(LowV, HighV);
  }
  if (NumRanges > 2) {
    APInt FirstLow =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(0))->getValue();
    APInt FirstHigh =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(1))->getValue();
    ConstantRange FirstRange(FirstLow, FirstHigh);
    Assert(FirstRange.intersectWith(LastRange).isEmptySet(),
           "Intervals are overlapping", Range);
    Assert(!isContiguous(FirstRange, LastRange), "Intervals are contiguous",
           Range);
  }
}

void Verifier::checkAtomicMemAccessSize(Type *Ty, const Instruction *I) {
  unsigned Size = DL.getTypeSizeInBits(Ty);
  Assert(Size >= 8, "atomic memory access' size must be byte-sized", Ty, I);
  Assert(!(Size & (Size - 1)),
         "atomic memory access' operand must have a power-of-two size", Ty, I);
}

void Verifier::visitLoadInst(LoadInst &LI) {
  PointerType *PTy = dyn_cast<PointerType>(LI.getOperand(0)->getType());
  Assert(PTy, "Load operand must be a pointer.", &LI);
  Type *ElTy = LI.getType();
  Assert(LI.getAlignment() <= Value::MaximumAlignment,
         "huge alignment values are unsupported", &LI);
  Assert(ElTy->isSized(), "loading unsized types is not allowed", &LI);
  if (LI.isAtomic()) {
    Assert(LI.getOrdering() != AtomicOrdering::Release &&
               LI.getOrdering() != AtomicOrdering::AcquireRelease,
           "Load cannot have Release ordering", &LI);
    Assert(LI.getAlignment() != 0,
           "Atomic load must specify explicit alignment", &LI);
    Assert(ElTy->isIntOrPtrTy() || ElTy->isFloatingPointTy(),
           "atomic load operand must have integer, pointer, or floating point "
           "type!",
           ElTy, &LI);
    checkAtomicMemAccessSize(ElTy, &LI);
  } else {
    Assert(LI.getSyncScopeID() == SyncScope::System,
           "Non-atomic load cannot have SynchronizationScope specified", &LI);
  }

  visitInstruction(LI);
}

void Verifier::visitStoreInst(StoreInst &SI) {
  PointerType *PTy = dyn_cast<PointerType>(SI.getOperand(1)->getType());
  Assert(PTy, "Store operand must be a pointer.", &SI);
  Type *ElTy = PTy->getElementType();
  Assert(ElTy == SI.getOperand(0)->getType(),
         "Stored value type does not match pointer operand type!", &SI, ElTy);
  Assert(SI.getAlignment() <= Value::MaximumAlignment,
         "huge alignment values are unsupported", &SI);
  Assert(ElTy->isSized(), "storing unsized types is not allowed", &SI);
  if (SI.isAtomic()) {
    Assert(SI.getOrdering() != AtomicOrdering::Acquire &&
               SI.getOrdering() != AtomicOrdering::AcquireRelease,
           "Store cannot have Acquire ordering", &SI);
    Assert(SI.getAlignment() != 0,
           "Atomic store must specify explicit alignment", &SI);
    Assert(ElTy->isIntOrPtrTy() || ElTy->isFloatingPointTy(),
           "atomic store operand must have integer, pointer, or floating point "
           "type!",
           ElTy, &SI);
    checkAtomicMemAccessSize(ElTy, &SI);
  } else {
    Assert(SI.getSyncScopeID() == SyncScope::System,
           "Non-atomic store cannot have SynchronizationScope specified", &SI);
  }
  visitInstruction(SI);
}

/// Check that SwiftErrorVal is used as a swifterror argument in CS.
void Verifier::verifySwiftErrorCall(CallBase &Call,
                                    const Value *SwiftErrorVal) {
  unsigned Idx = 0;
  for (auto I = Call.arg_begin(), E = Call.arg_end(); I != E; ++I, ++Idx) {
    if (*I == SwiftErrorVal) {
      Assert(Call.paramHasAttr(Idx, Attribute::SwiftError),
             "swifterror value when used in a callsite should be marked "
             "with swifterror attribute",
             SwiftErrorVal, Call);
    }
  }
}

void Verifier::verifySwiftErrorValue(const Value *SwiftErrorVal) {
  // Check that swifterror value is only used by loads, stores, or as
  // a swifterror argument.
  for (const User *U : SwiftErrorVal->users()) {
    Assert(isa<LoadInst>(U) || isa<StoreInst>(U) || isa<CallInst>(U) ||
           isa<InvokeInst>(U),
           "swifterror value can only be loaded and stored from, or "
           "as a swifterror argument!",
           SwiftErrorVal, U);
    // If it is used by a store, check it is the second operand.
    if (auto StoreI = dyn_cast<StoreInst>(U))
      Assert(StoreI->getOperand(1) == SwiftErrorVal,
             "swifterror value should be the second operand when used "
             "by stores", SwiftErrorVal, U);
    if (auto *Call = dyn_cast<CallBase>(U))
      verifySwiftErrorCall(*const_cast<CallBase *>(Call), SwiftErrorVal);
  }
}

void Verifier::visitAllocaInst(AllocaInst &AI) {
  SmallPtrSet<Type*, 4> Visited;
  PointerType *PTy = AI.getType();
  // TODO: Relax this restriction?
  Assert(PTy->getAddressSpace() == DL.getAllocaAddrSpace(),
         "Allocation instruction pointer not in the stack address space!",
         &AI);
  Assert(AI.getAllocatedType()->isSized(&Visited),
         "Cannot allocate unsized type", &AI);
  Assert(AI.getArraySize()->getType()->isIntegerTy(),
         "Alloca array size must have integer type", &AI);
  Assert(AI.getAlignment() <= Value::MaximumAlignment,
         "huge alignment values are unsupported", &AI);

  if (AI.isSwiftError()) {
    verifySwiftErrorValue(&AI);
  }

  visitInstruction(AI);
}

void Verifier::visitAtomicCmpXchgInst(AtomicCmpXchgInst &CXI) {

  // FIXME: more conditions???
  Assert(CXI.getSuccessOrdering() != AtomicOrdering::NotAtomic,
         "cmpxchg instructions must be atomic.", &CXI);
  Assert(CXI.getFailureOrdering() != AtomicOrdering::NotAtomic,
         "cmpxchg instructions must be atomic.", &CXI);
  Assert(CXI.getSuccessOrdering() != AtomicOrdering::Unordered,
         "cmpxchg instructions cannot be unordered.", &CXI);
  Assert(CXI.getFailureOrdering() != AtomicOrdering::Unordered,
         "cmpxchg instructions cannot be unordered.", &CXI);
  Assert(!isStrongerThan(CXI.getFailureOrdering(), CXI.getSuccessOrdering()),
         "cmpxchg instructions failure argument shall be no stronger than the "
         "success argument",
         &CXI);
  Assert(CXI.getFailureOrdering() != AtomicOrdering::Release &&
             CXI.getFailureOrdering() != AtomicOrdering::AcquireRelease,
         "cmpxchg failure ordering cannot include release semantics", &CXI);

  PointerType *PTy = dyn_cast<PointerType>(CXI.getOperand(0)->getType());
  Assert(PTy, "First cmpxchg operand must be a pointer.", &CXI);
  Type *ElTy = PTy->getElementType();
  Assert(ElTy->isIntOrPtrTy(),
         "cmpxchg operand must have integer or pointer type", ElTy, &CXI);
  checkAtomicMemAccessSize(ElTy, &CXI);
  Assert(ElTy == CXI.getOperand(1)->getType(),
         "Expected value type does not match pointer operand type!", &CXI,
         ElTy);
  Assert(ElTy == CXI.getOperand(2)->getType(),
         "Stored value type does not match pointer operand type!", &CXI, ElTy);
  visitInstruction(CXI);
}

void Verifier::visitAtomicRMWInst(AtomicRMWInst &RMWI) {
  Assert(RMWI.getOrdering() != AtomicOrdering::NotAtomic,
         "atomicrmw instructions must be atomic.", &RMWI);
  Assert(RMWI.getOrdering() != AtomicOrdering::Unordered,
         "atomicrmw instructions cannot be unordered.", &RMWI);
  auto Op = RMWI.getOperation();
  PointerType *PTy = dyn_cast<PointerType>(RMWI.getOperand(0)->getType());
  Assert(PTy, "First atomicrmw operand must be a pointer.", &RMWI);
  Type *ElTy = PTy->getElementType();
  Assert(ElTy->isIntegerTy(), "atomicrmw " +
         AtomicRMWInst::getOperationName(Op) +
         " operand must have integer type!",
         &RMWI, ElTy);
  checkAtomicMemAccessSize(ElTy, &RMWI);
  Assert(ElTy == RMWI.getOperand(1)->getType(),
         "Argument value type does not match pointer operand type!", &RMWI,
         ElTy);
  Assert(AtomicRMWInst::FIRST_BINOP <= Op && Op <= AtomicRMWInst::LAST_BINOP,
         "Invalid binary operation!", &RMWI);
  visitInstruction(RMWI);
}

void Verifier::visitFenceInst(FenceInst &FI) {
  const AtomicOrdering Ordering = FI.getOrdering();
  Assert(Ordering == AtomicOrdering::Acquire ||
             Ordering == AtomicOrdering::Release ||
             Ordering == AtomicOrdering::AcquireRelease ||
             Ordering == AtomicOrdering::SequentiallyConsistent,
         "fence instructions may only have acquire, release, acq_rel, or "
         "seq_cst ordering.",
         &FI);
  visitInstruction(FI);
}

void Verifier::visitExtractValueInst(ExtractValueInst &EVI) {
  Assert(ExtractValueInst::getIndexedType(EVI.getAggregateOperand()->getType(),
                                          EVI.getIndices()) == EVI.getType(),
         "Invalid ExtractValueInst operands!", &EVI);

  visitInstruction(EVI);
}

void Verifier::visitInsertValueInst(InsertValueInst &IVI) {
  Assert(ExtractValueInst::getIndexedType(IVI.getAggregateOperand()->getType(),
                                          IVI.getIndices()) ==
             IVI.getOperand(1)->getType(),
         "Invalid InsertValueInst operands!", &IVI);

  visitInstruction(IVI);
}

static Value *getParentPad(Value *EHPad) {
  if (auto *FPI = dyn_cast<FuncletPadInst>(EHPad))
    return FPI->getParentPad();

  return cast<CatchSwitchInst>(EHPad)->getParentPad();
}

void Verifier::visitEHPadPredecessors(Instruction &I) {
  assert(I.isEHPad());

  BasicBlock *BB = I.getParent();
  Function *F = BB->getParent();

  Assert(BB != &F->getEntryBlock(), "EH pad cannot be in entry block.", &I);

  if (auto *LPI = dyn_cast<LandingPadInst>(&I)) {
    // The landingpad instruction defines its parent as a landing pad block. The
    // landing pad block may be branched to only by the unwind edge of an
    // invoke.
    for (BasicBlock *PredBB : predecessors(BB)) {
      const auto *II = dyn_cast<InvokeInst>(PredBB->getTerminator());
      Assert(II && II->getUnwindDest() == BB && II->getNormalDest() != BB,
             "Block containing LandingPadInst must be jumped to "
             "only by the unwind edge of an invoke.",
             LPI);
    }
    return;
  }
  if (auto *CPI = dyn_cast<CatchPadInst>(&I)) {
    if (!pred_empty(BB))
      Assert(BB->getUniquePredecessor() == CPI->getCatchSwitch()->getParent(),
             "Block containg CatchPadInst must be jumped to "
             "only by its catchswitch.",
             CPI);
    Assert(BB != CPI->getCatchSwitch()->getUnwindDest(),
           "Catchswitch cannot unwind to one of its catchpads",
           CPI->getCatchSwitch(), CPI);
    return;
  }

  // Verify that each pred has a legal terminator with a legal to/from EH
  // pad relationship.
  Instruction *ToPad = &I;
  Value *ToPadParent = getParentPad(ToPad);
  for (BasicBlock *PredBB : predecessors(BB)) {
    Instruction *TI = PredBB->getTerminator();
    Value *FromPad;
    if (auto *II = dyn_cast<InvokeInst>(TI)) {
      Assert(II->getUnwindDest() == BB && II->getNormalDest() != BB,
             "EH pad must be jumped to via an unwind edge", ToPad, II);
      if (auto Bundle = II->getOperandBundle(LLVMContext::OB_funclet))
        FromPad = Bundle->Inputs[0];
      else
        FromPad = ConstantTokenNone::get(II->getContext());
    } else if (auto *CRI = dyn_cast<CleanupReturnInst>(TI)) {
      FromPad = CRI->getOperand(0);
      Assert(FromPad != ToPadParent, "A cleanupret must exit its cleanup", CRI);
    } else if (auto *CSI = dyn_cast<CatchSwitchInst>(TI)) {
      FromPad = CSI;
    } else {
      Assert(false, "EH pad must be jumped to via an unwind edge", ToPad, TI);
    }

    // The edge may exit from zero or more nested pads.
    SmallSet<Value *, 8> Seen;
    for (;; FromPad = getParentPad(FromPad)) {
      Assert(FromPad != ToPad,
             "EH pad cannot handle exceptions raised within it", FromPad, TI);
      if (FromPad == ToPadParent) {
        // This is a legal unwind edge.
        break;
      }
      Assert(!isa<ConstantTokenNone>(FromPad),
             "A single unwind edge may only enter one EH pad", TI);
      Assert(Seen.insert(FromPad).second,
             "EH pad jumps through a cycle of pads", FromPad);
    }
  }
}

void Verifier::visitLandingPadInst(LandingPadInst &LPI) {
  // The landingpad instruction is ill-formed if it doesn't have any clauses and
  // isn't a cleanup.
  Assert(LPI.getNumClauses() > 0 || LPI.isCleanup(),
         "LandingPadInst needs at least one clause or to be a cleanup.", &LPI);

  visitEHPadPredecessors(LPI);

  if (!LandingPadResultTy)
    LandingPadResultTy = LPI.getType();
  else
    Assert(LandingPadResultTy == LPI.getType(),
           "The landingpad instruction should have a consistent result type "
           "inside a function.",
           &LPI);

  Function *F = LPI.getParent()->getParent();
  Assert(F->hasPersonalityFn(),
         "LandingPadInst needs to be in a function with a personality.", &LPI);

  // The landingpad instruction must be the first non-PHI instruction in the
  // block.
  Assert(LPI.getParent()->getLandingPadInst() == &LPI,
         "LandingPadInst not the first non-PHI instruction in the block.",
         &LPI);

  for (unsigned i = 0, e = LPI.getNumClauses(); i < e; ++i) {
    Constant *Clause = LPI.getClause(i);
    if (LPI.isCatch(i)) {
      Assert(isa<PointerType>(Clause->getType()),
             "Catch operand does not have pointer type!", &LPI);
    } else {
      Assert(LPI.isFilter(i), "Clause is neither catch nor filter!", &LPI);
      Assert(isa<ConstantArray>(Clause) || isa<ConstantAggregateZero>(Clause),
             "Filter operand is not an array of constants!", &LPI);
    }
  }

  visitInstruction(LPI);
}

void Verifier::visitResumeInst(ResumeInst &RI) {
  Assert(RI.getFunction()->hasPersonalityFn(),
         "ResumeInst needs to be in a function with a personality.", &RI);

  if (!LandingPadResultTy)
    LandingPadResultTy = RI.getValue()->getType();
  else
    Assert(LandingPadResultTy == RI.getValue()->getType(),
           "The resume instruction should have a consistent result type "
           "inside a function.",
           &RI);

  visitTerminator(RI);
}

void Verifier::visitCatchPadInst(CatchPadInst &CPI) {
  BasicBlock *BB = CPI.getParent();

  Function *F = BB->getParent();
  Assert(F->hasPersonalityFn(),
         "CatchPadInst needs to be in a function with a personality.", &CPI);

  Assert(isa<CatchSwitchInst>(CPI.getParentPad()),
         "CatchPadInst needs to be directly nested in a CatchSwitchInst.",
         CPI.getParentPad());

  // The catchpad instruction must be the first non-PHI instruction in the
  // block.
  Assert(BB->getFirstNonPHI() == &CPI,
         "CatchPadInst not the first non-PHI instruction in the block.", &CPI);

  visitEHPadPredecessors(CPI);
  visitFuncletPadInst(CPI);
}

void Verifier::visitCatchReturnInst(CatchReturnInst &CatchReturn) {
  Assert(isa<CatchPadInst>(CatchReturn.getOperand(0)),
         "CatchReturnInst needs to be provided a CatchPad", &CatchReturn,
         CatchReturn.getOperand(0));

  visitTerminator(CatchReturn);
}

void Verifier::visitCleanupPadInst(CleanupPadInst &CPI) {
  BasicBlock *BB = CPI.getParent();

  Function *F = BB->getParent();
  Assert(F->hasPersonalityFn(),
         "CleanupPadInst needs to be in a function with a personality.", &CPI);

  // The cleanuppad instruction must be the first non-PHI instruction in the
  // block.
  Assert(BB->getFirstNonPHI() == &CPI,
         "CleanupPadInst not the first non-PHI instruction in the block.",
         &CPI);

  auto *ParentPad = CPI.getParentPad();
  Assert(isa<ConstantTokenNone>(ParentPad) || isa<FuncletPadInst>(ParentPad),
         "CleanupPadInst has an invalid parent.", &CPI);

  visitEHPadPredecessors(CPI);
  visitFuncletPadInst(CPI);
}

void Verifier::visitFuncletPadInst(FuncletPadInst &FPI) {
  User *FirstUser = nullptr;
  Value *FirstUnwindPad = nullptr;
  SmallVector<FuncletPadInst *, 8> Worklist({&FPI});
  SmallSet<FuncletPadInst *, 8> Seen;

  while (!Worklist.empty()) {
    FuncletPadInst *CurrentPad = Worklist.pop_back_val();
    Assert(Seen.insert(CurrentPad).second,
           "FuncletPadInst must not be nested within itself", CurrentPad);
    Value *UnresolvedAncestorPad = nullptr;
    for (User *U : CurrentPad->users()) {
      BasicBlock *UnwindDest;
      if (auto *CRI = dyn_cast<CleanupReturnInst>(U)) {
        UnwindDest = CRI->getUnwindDest();
      } else if (auto *CSI = dyn_cast<CatchSwitchInst>(U)) {
        // We allow catchswitch unwind to caller to nest
        // within an outer pad that unwinds somewhere else,
        // because catchswitch doesn't have a nounwind variant.
        // See e.g. SimplifyCFGOpt::SimplifyUnreachable.
        if (CSI->unwindsToCaller())
          continue;
        UnwindDest = CSI->getUnwindDest();
      } else if (auto *II = dyn_cast<InvokeInst>(U)) {
        UnwindDest = II->getUnwindDest();
      } else if (isa<CallInst>(U)) {
        // Calls which don't unwind may be found inside funclet
        // pads that unwind somewhere else.  We don't *require*
        // such calls to be annotated nounwind.
        continue;
      } else if (auto *CPI = dyn_cast<CleanupPadInst>(U)) {
        // The unwind dest for a cleanup can only be found by
        // recursive search.  Add it to the worklist, and we'll
        // search for its first use that determines where it unwinds.
        Worklist.push_back(CPI);
        continue;
      } else {
        Assert(isa<CatchReturnInst>(U), "Bogus funclet pad use", U);
        continue;
      }

      Value *UnwindPad;
      bool ExitsFPI;
      if (UnwindDest) {
        UnwindPad = UnwindDest->getFirstNonPHI();
        if (!cast<Instruction>(UnwindPad)->isEHPad())
          continue;
        Value *UnwindParent = getParentPad(UnwindPad);
        // Ignore unwind edges that don't exit CurrentPad.
        if (UnwindParent == CurrentPad)
          continue;
        // Determine whether the original funclet pad is exited,
        // and if we are scanning nested pads determine how many
        // of them are exited so we can stop searching their
        // children.
        Value *ExitedPad = CurrentPad;
        ExitsFPI = false;
        do {
          if (ExitedPad == &FPI) {
            ExitsFPI = true;
            // Now we can resolve any ancestors of CurrentPad up to
            // FPI, but not including FPI since we need to make sure
            // to check all direct users of FPI for consistency.
            UnresolvedAncestorPad = &FPI;
            break;
          }
          Value *ExitedParent = getParentPad(ExitedPad);
          if (ExitedParent == UnwindParent) {
            // ExitedPad is the ancestor-most pad which this unwind
            // edge exits, so we can resolve up to it, meaning that
            // ExitedParent is the first ancestor still unresolved.
            UnresolvedAncestorPad = ExitedParent;
            break;
          }
          ExitedPad = ExitedParent;
        } while (!isa<ConstantTokenNone>(ExitedPad));
      } else {
        // Unwinding to caller exits all pads.
        UnwindPad = ConstantTokenNone::get(FPI.getContext());
        ExitsFPI = true;
        UnresolvedAncestorPad = &FPI;
      }

      if (ExitsFPI) {
        // This unwind edge exits FPI.  Make sure it agrees with other
        // such edges.
        if (FirstUser) {
          Assert(UnwindPad == FirstUnwindPad, "Unwind edges out of a funclet "
                                              "pad must have the same unwind "
                                              "dest",
                 &FPI, U, FirstUser);
        } else {
          FirstUser = U;
          FirstUnwindPad = UnwindPad;
          // Record cleanup sibling unwinds for verifySiblingFuncletUnwinds
          if (isa<CleanupPadInst>(&FPI) && !isa<ConstantTokenNone>(UnwindPad) &&
              getParentPad(UnwindPad) == getParentPad(&FPI))
            SiblingFuncletInfo[&FPI] = cast<Instruction>(U);
        }
      }
      // Make sure we visit all uses of FPI, but for nested pads stop as
      // soon as we know where they unwind to.
      if (CurrentPad != &FPI)
        break;
    }
    if (UnresolvedAncestorPad) {
      if (CurrentPad == UnresolvedAncestorPad) {
        // When CurrentPad is FPI itself, we don't mark it as resolved even if
        // we've found an unwind edge that exits it, because we need to verify
        // all direct uses of FPI.
        assert(CurrentPad == &FPI);
        continue;
      }
      // Pop off the worklist any nested pads that we've found an unwind
      // destination for.  The pads on the worklist are the uncles,
      // great-uncles, etc. of CurrentPad.  We've found an unwind destination
      // for all ancestors of CurrentPad up to but not including
      // UnresolvedAncestorPad.
      Value *ResolvedPad = CurrentPad;
      while (!Worklist.empty()) {
        Value *UnclePad = Worklist.back();
        Value *AncestorPad = getParentPad(UnclePad);
        // Walk ResolvedPad up the ancestor list until we either find the
        // uncle's parent or the last resolved ancestor.
        while (ResolvedPad != AncestorPad) {
          Value *ResolvedParent = getParentPad(ResolvedPad);
          if (ResolvedParent == UnresolvedAncestorPad) {
            break;
          }
          ResolvedPad = ResolvedParent;
        }
        // If the resolved ancestor search didn't find the uncle's parent,
        // then the uncle is not yet resolved.
        if (ResolvedPad != AncestorPad)
          break;
        // This uncle is resolved, so pop it from the worklist.
        Worklist.pop_back();
      }
    }
  }

  if (FirstUnwindPad) {
    if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(FPI.getParentPad())) {
      BasicBlock *SwitchUnwindDest = CatchSwitch->getUnwindDest();
      Value *SwitchUnwindPad;
      if (SwitchUnwindDest)
        SwitchUnwindPad = SwitchUnwindDest->getFirstNonPHI();
      else
        SwitchUnwindPad = ConstantTokenNone::get(FPI.getContext());
      Assert(SwitchUnwindPad == FirstUnwindPad,
             "Unwind edges out of a catch must have the same unwind dest as "
             "the parent catchswitch",
             &FPI, FirstUser, CatchSwitch);
    }
  }

  visitInstruction(FPI);
}

void Verifier::visitCatchSwitchInst(CatchSwitchInst &CatchSwitch) {
  BasicBlock *BB = CatchSwitch.getParent();

  Function *F = BB->getParent();
  Assert(F->hasPersonalityFn(),
         "CatchSwitchInst needs to be in a function with a personality.",
         &CatchSwitch);

  // The catchswitch instruction must be the first non-PHI instruction in the
  // block.
  Assert(BB->getFirstNonPHI() == &CatchSwitch,
         "CatchSwitchInst not the first non-PHI instruction in the block.",
         &CatchSwitch);

  auto *ParentPad = CatchSwitch.getParentPad();
  Assert(isa<ConstantTokenNone>(ParentPad) || isa<FuncletPadInst>(ParentPad),
         "CatchSwitchInst has an invalid parent.", ParentPad);

  if (BasicBlock *UnwindDest = CatchSwitch.getUnwindDest()) {
    Instruction *I = UnwindDest->getFirstNonPHI();
    Assert(I->isEHPad() && !isa<LandingPadInst>(I),
           "CatchSwitchInst must unwind to an EH block which is not a "
           "landingpad.",
           &CatchSwitch);

    // Record catchswitch sibling unwinds for verifySiblingFuncletUnwinds
    if (getParentPad(I) == ParentPad)
      SiblingFuncletInfo[&CatchSwitch] = &CatchSwitch;
  }

  Assert(CatchSwitch.getNumHandlers() != 0,
         "CatchSwitchInst cannot have empty handler list", &CatchSwitch);

  for (BasicBlock *Handler : CatchSwitch.handlers()) {
    Assert(isa<CatchPadInst>(Handler->getFirstNonPHI()),
           "CatchSwitchInst handlers must be catchpads", &CatchSwitch, Handler);
  }

  visitEHPadPredecessors(CatchSwitch);
  visitTerminator(CatchSwitch);
}

void Verifier::visitCleanupReturnInst(CleanupReturnInst &CRI) {
  Assert(isa<CleanupPadInst>(CRI.getOperand(0)),
         "CleanupReturnInst needs to be provided a CleanupPad", &CRI,
         CRI.getOperand(0));

  if (BasicBlock *UnwindDest = CRI.getUnwindDest()) {
    Instruction *I = UnwindDest->getFirstNonPHI();
    Assert(I->isEHPad() && !isa<LandingPadInst>(I),
           "CleanupReturnInst must unwind to an EH block which is not a "
           "landingpad.",
           &CRI);
  }

  visitTerminator(CRI);
}

void Verifier::verifyDominatesUse(Instruction &I, unsigned i) {
  Instruction *Op = cast<Instruction>(I.getOperand(i));
  // If the we have an invalid invoke, don't try to compute the dominance.
  // We already reject it in the invoke specific checks and the dominance
  // computation doesn't handle multiple edges.
  if (InvokeInst *II = dyn_cast<InvokeInst>(Op)) {
    if (II->getNormalDest() == II->getUnwindDest())
      return;
  }

  // Quick check whether the def has already been encountered in the same block.
  // PHI nodes are not checked to prevent accepting preceeding PHIs, because PHI
  // uses are defined to happen on the incoming edge, not at the instruction.
  //
  // FIXME: If this operand is a MetadataAsValue (wrapping a LocalAsMetadata)
  // wrapping an SSA value, assert that we've already encountered it.  See
  // related FIXME in Mapper::mapLocalAsMetadata in ValueMapper.cpp.
  if (!isa<PHINode>(I) && InstsInThisBlock.count(Op))
    return;

  const Use &U = I.getOperandUse(i);
  Assert(DT.dominates(Op, U),
         "Instruction does not dominate all uses!", Op, &I);
}

void Verifier::visitDereferenceableMetadata(Instruction& I, MDNode* MD) {
  Assert(I.getType()->isPointerTy(), "dereferenceable, dereferenceable_or_null "
         "apply only to pointer types", &I);
  Assert(isa<LoadInst>(I),
         "dereferenceable, dereferenceable_or_null apply only to load"
         " instructions, use attributes for calls or invokes", &I);
  Assert(MD->getNumOperands() == 1, "dereferenceable, dereferenceable_or_null "
         "take one operand!", &I);
  ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(MD->getOperand(0));
  Assert(CI && CI->getType()->isIntegerTy(64), "dereferenceable, "
         "dereferenceable_or_null metadata value must be an i64!", &I);
}

/// verifyInstruction - Verify that an instruction is well formed.
///
void Verifier::visitInstruction(Instruction &I) {
  BasicBlock *BB = I.getParent();
  Assert(BB, "Instruction not embedded in basic block!", &I);

  if (!isa<PHINode>(I)) {   // Check that non-phi nodes are not self referential
    for (User *U : I.users()) {
      Assert(U != (User *)&I || !DT.isReachableFromEntry(BB),
             "Only PHI nodes may reference their own value!", &I);
    }
  }

  // Check that void typed values don't have names
  Assert(!I.getType()->isVoidTy() || !I.hasName(),
         "Instruction has a name, but provides a void value!", &I);

  // Check that the return value of the instruction is either void or a legal
  // value type.
  Assert(I.getType()->isVoidTy() || I.getType()->isFirstClassType(),
         "Instruction returns a non-scalar type!", &I);

  // Check that the instruction doesn't produce metadata. Calls are already
  // checked against the callee type.
  Assert(!I.getType()->isMetadataTy() || isa<CallInst>(I) || isa<InvokeInst>(I),
         "Invalid use of metadata!", &I);

  // Check that all uses of the instruction, if they are instructions
  // themselves, actually have parent basic blocks.  If the use is not an
  // instruction, it is an error!
  for (Use &U : I.uses()) {
    if (Instruction *Used = dyn_cast<Instruction>(U.getUser()))
      Assert(Used->getParent() != nullptr,
             "Instruction referencing"
             " instruction not embedded in a basic block!",
             &I, Used);
    else {
      CheckFailed("Use of instruction is not an instruction!", U);
      return;
    }
  }

  // Get a pointer to the call base of the instruction if it is some form of
  // call.
  const CallBase *CBI = dyn_cast<CallBase>(&I);

  for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i) {
    Assert(I.getOperand(i) != nullptr, "Instruction has null operand!", &I);

    // Check to make sure that only first-class-values are operands to
    // instructions.
    if (!I.getOperand(i)->getType()->isFirstClassType()) {
      Assert(false, "Instruction operands must be first-class values!", &I);
    }

    if (Function *F = dyn_cast<Function>(I.getOperand(i))) {
      // Check to make sure that the "address of" an intrinsic function is never
      // taken.
      Assert(!F->isIntrinsic() ||
                 (CBI && &CBI->getCalledOperandUse() == &I.getOperandUse(i)),
             "Cannot take the address of an intrinsic!", &I);
      Assert(
          !F->isIntrinsic() || isa<CallInst>(I) ||
              F->getIntrinsicID() == Intrinsic::donothing ||
              F->getIntrinsicID() == Intrinsic::coro_resume ||
              F->getIntrinsicID() == Intrinsic::coro_destroy ||
              F->getIntrinsicID() == Intrinsic::experimental_patchpoint_void ||
              F->getIntrinsicID() == Intrinsic::experimental_patchpoint_i64 ||
              F->getIntrinsicID() == Intrinsic::experimental_gc_statepoint,
          "Cannot invoke an intrinsic other than donothing, patchpoint, "
          "statepoint, coro_resume or coro_destroy",
          &I);
      Assert(F->getParent() == &M, "Referencing function in another module!",
             &I, &M, F, F->getParent());
    } else if (BasicBlock *OpBB = dyn_cast<BasicBlock>(I.getOperand(i))) {
      Assert(OpBB->getParent() == BB->getParent(),
             "Referring to a basic block in another function!", &I);
    } else if (Argument *OpArg = dyn_cast<Argument>(I.getOperand(i))) {
      Assert(OpArg->getParent() == BB->getParent(),
             "Referring to an argument in another function!", &I);
    } else if (GlobalValue *GV = dyn_cast<GlobalValue>(I.getOperand(i))) {
      Assert(GV->getParent() == &M, "Referencing global in another module!", &I,
             &M, GV, GV->getParent());
    } else if (isa<Instruction>(I.getOperand(i))) {
      verifyDominatesUse(I, i);
    } else if (isa<InlineAsm>(I.getOperand(i))) {
      Assert(CBI && &CBI->getCalledOperandUse() == &I.getOperandUse(i),
             "Cannot take the address of an inline asm!", &I);
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(I.getOperand(i))) {
      if (CE->getType()->isPtrOrPtrVectorTy() ||
          !DL.getNonIntegralAddressSpaces().empty()) {
        // If we have a ConstantExpr pointer, we need to see if it came from an
        // illegal bitcast.  If the datalayout string specifies non-integral
        // address spaces then we also need to check for illegal ptrtoint and
        // inttoptr expressions.
        visitConstantExprsRecursively(CE);
      }
    }
  }

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_fpmath)) {
    Assert(I.getType()->isFPOrFPVectorTy(),
           "fpmath requires a floating point result!", &I);
    Assert(MD->getNumOperands() == 1, "fpmath takes one operand!", &I);
    if (ConstantFP *CFP0 =
            mdconst::dyn_extract_or_null<ConstantFP>(MD->getOperand(0))) {
      const APFloat &Accuracy = CFP0->getValueAPF();
      Assert(&Accuracy.getSemantics() == &APFloat::IEEEsingle(),
             "fpmath accuracy must have float type", &I);
      Assert(Accuracy.isFiniteNonZero() && !Accuracy.isNegative(),
             "fpmath accuracy not a positive number!", &I);
    } else {
      Assert(false, "invalid fpmath accuracy!", &I);
    }
  }

  if (MDNode *Range = I.getMetadata(LLVMContext::MD_range)) {
    Assert(isa<LoadInst>(I) || isa<CallInst>(I) || isa<InvokeInst>(I),
           "Ranges are only for loads, calls and invokes!", &I);
    visitRangeMetadata(I, Range, I.getType());
  }

  if (I.getMetadata(LLVMContext::MD_nonnull)) {
    Assert(I.getType()->isPointerTy(), "nonnull applies only to pointer types",
           &I);
    Assert(isa<LoadInst>(I),
           "nonnull applies only to load instructions, use attributes"
           " for calls or invokes",
           &I);
  }

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_dereferenceable))
    visitDereferenceableMetadata(I, MD);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_dereferenceable_or_null))
    visitDereferenceableMetadata(I, MD);

  if (MDNode *TBAA = I.getMetadata(LLVMContext::MD_tbaa))
    TBAAVerifyHelper.visitTBAAMetadata(I, TBAA);

  if (MDNode *AlignMD = I.getMetadata(LLVMContext::MD_align)) {
    Assert(I.getType()->isPointerTy(), "align applies only to pointer types",
           &I);
    Assert(isa<LoadInst>(I), "align applies only to load instructions, "
           "use attributes for calls or invokes", &I);
    Assert(AlignMD->getNumOperands() == 1, "align takes one operand!", &I);
    ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(AlignMD->getOperand(0));
    Assert(CI && CI->getType()->isIntegerTy(64),
           "align metadata value must be an i64!", &I);
    uint64_t Align = CI->getZExtValue();
    Assert(isPowerOf2_64(Align),
           "align metadata value must be a power of 2!", &I);
    Assert(Align <= Value::MaximumAlignment,
           "alignment is larger that implementation defined limit", &I);
  }

  if (MDNode *N = I.getDebugLoc().getAsMDNode()) {
    AssertDI(isa<DILocation>(N), "invalid !dbg metadata attachment", &I, N);
    visitMDNode(*N);
  }

  if (auto *DII = dyn_cast<DbgVariableIntrinsic>(&I))
    verifyFragmentExpression(*DII);

  InstsInThisBlock.insert(&I);
}

/// Allow intrinsics to be verified in different ways.
void Verifier::visitIntrinsicCall(Intrinsic::ID ID, CallBase &Call) {
  Function *IF = Call.getCalledFunction();
  Assert(IF->isDeclaration(), "Intrinsic functions should never be defined!",
         IF);

  // Verify that the intrinsic prototype lines up with what the .td files
  // describe.
  FunctionType *IFTy = IF->getFunctionType();
  bool IsVarArg = IFTy->isVarArg();

  SmallVector<Intrinsic::IITDescriptor, 8> Table;
  getIntrinsicInfoTableEntries(ID, Table);
  ArrayRef<Intrinsic::IITDescriptor> TableRef = Table;

  SmallVector<Type *, 4> ArgTys;
  Assert(!Intrinsic::matchIntrinsicType(IFTy->getReturnType(),
                                        TableRef, ArgTys),
         "Intrinsic has incorrect return type!", IF);
  for (unsigned i = 0, e = IFTy->getNumParams(); i != e; ++i)
    Assert(!Intrinsic::matchIntrinsicType(IFTy->getParamType(i),
                                          TableRef, ArgTys),
           "Intrinsic has incorrect argument type!", IF);

  // Verify if the intrinsic call matches the vararg property.
  if (IsVarArg)
    Assert(!Intrinsic::matchIntrinsicVarArg(IsVarArg, TableRef),
           "Intrinsic was not defined with variable arguments!", IF);
  else
    Assert(!Intrinsic::matchIntrinsicVarArg(IsVarArg, TableRef),
           "Callsite was not defined with variable arguments!", IF);

  // All descriptors should be absorbed by now.
  Assert(TableRef.empty(), "Intrinsic has too few arguments!", IF);

  // Now that we have the intrinsic ID and the actual argument types (and we
  // know they are legal for the intrinsic!) get the intrinsic name through the
  // usual means.  This allows us to verify the mangling of argument types into
  // the name.
  const std::string ExpectedName = Intrinsic::getName(ID, ArgTys);
  Assert(ExpectedName == IF->getName(),
         "Intrinsic name not mangled correctly for type arguments! "
         "Should be: " +
             ExpectedName,
         IF);

  // If the intrinsic takes MDNode arguments, verify that they are either global
  // or are local to *this* function.
  for (Value *V : Call.args())
    if (auto *MD = dyn_cast<MetadataAsValue>(V))
      visitMetadataAsValue(*MD, Call.getCaller());

  switch (ID) {
  default:
    break;
  case Intrinsic::coro_id: {
    auto *InfoArg = Call.getArgOperand(3)->stripPointerCasts();
    if (isa<ConstantPointerNull>(InfoArg))
      break;
    auto *GV = dyn_cast<GlobalVariable>(InfoArg);
    Assert(GV && GV->isConstant() && GV->hasDefinitiveInitializer(),
      "info argument of llvm.coro.begin must refer to an initialized "
      "constant");
    Constant *Init = GV->getInitializer();
    Assert(isa<ConstantStruct>(Init) || isa<ConstantArray>(Init),
      "info argument of llvm.coro.begin must refer to either a struct or "
      "an array");
    break;
  }
  case Intrinsic::ctlz:  // llvm.ctlz
  case Intrinsic::cttz:  // llvm.cttz
    Assert(isa<ConstantInt>(Call.getArgOperand(1)),
           "is_zero_undef argument of bit counting intrinsics must be a "
           "constant int",
           Call);
    break;
  case Intrinsic::experimental_constrained_fadd:
  case Intrinsic::experimental_constrained_fsub:
  case Intrinsic::experimental_constrained_fmul:
  case Intrinsic::experimental_constrained_fdiv:
  case Intrinsic::experimental_constrained_frem:
  case Intrinsic::experimental_constrained_fma:
  case Intrinsic::experimental_constrained_sqrt:
  case Intrinsic::experimental_constrained_pow:
  case Intrinsic::experimental_constrained_powi:
  case Intrinsic::experimental_constrained_sin:
  case Intrinsic::experimental_constrained_cos:
  case Intrinsic::experimental_constrained_exp:
  case Intrinsic::experimental_constrained_exp2:
  case Intrinsic::experimental_constrained_log:
  case Intrinsic::experimental_constrained_log10:
  case Intrinsic::experimental_constrained_log2:
  case Intrinsic::experimental_constrained_rint:
  case Intrinsic::experimental_constrained_nearbyint:
  case Intrinsic::experimental_constrained_maxnum:
  case Intrinsic::experimental_constrained_minnum:
  case Intrinsic::experimental_constrained_ceil:
  case Intrinsic::experimental_constrained_floor:
  case Intrinsic::experimental_constrained_round:
  case Intrinsic::experimental_constrained_trunc:
    visitConstrainedFPIntrinsic(cast<ConstrainedFPIntrinsic>(Call));
    break;
  case Intrinsic::dbg_declare: // llvm.dbg.declare
    Assert(isa<MetadataAsValue>(Call.getArgOperand(0)),
           "invalid llvm.dbg.declare intrinsic call 1", Call);
    visitDbgIntrinsic("declare", cast<DbgVariableIntrinsic>(Call));
    break;
  case Intrinsic::dbg_addr: // llvm.dbg.addr
    visitDbgIntrinsic("addr", cast<DbgVariableIntrinsic>(Call));
    break;
  case Intrinsic::dbg_value: // llvm.dbg.value
    visitDbgIntrinsic("value", cast<DbgVariableIntrinsic>(Call));
    break;
  case Intrinsic::dbg_label: // llvm.dbg.label
    visitDbgLabelIntrinsic("label", cast<DbgLabelInst>(Call));
    break;
  case Intrinsic::memcpy:
  case Intrinsic::memmove:
  case Intrinsic::memset: {
    const auto *MI = cast<MemIntrinsic>(&Call);
    auto IsValidAlignment = [&](unsigned Alignment) -> bool {
      return Alignment == 0 || isPowerOf2_32(Alignment);
    };
    Assert(IsValidAlignment(MI->getDestAlignment()),
           "alignment of arg 0 of memory intrinsic must be 0 or a power of 2",
           Call);
    if (const auto *MTI = dyn_cast<MemTransferInst>(MI)) {
      Assert(IsValidAlignment(MTI->getSourceAlignment()),
             "alignment of arg 1 of memory intrinsic must be 0 or a power of 2",
             Call);
    }
    Assert(isa<ConstantInt>(Call.getArgOperand(3)),
           "isvolatile argument of memory intrinsics must be a constant int",
           Call);
    break;
  }
  case Intrinsic::memcpy_element_unordered_atomic:
  case Intrinsic::memmove_element_unordered_atomic:
  case Intrinsic::memset_element_unordered_atomic: {
    const auto *AMI = cast<AtomicMemIntrinsic>(&Call);

    ConstantInt *ElementSizeCI =
        dyn_cast<ConstantInt>(AMI->getRawElementSizeInBytes());
    Assert(ElementSizeCI,
           "element size of the element-wise unordered atomic memory "
           "intrinsic must be a constant int",
           Call);
    const APInt &ElementSizeVal = ElementSizeCI->getValue();
    Assert(ElementSizeVal.isPowerOf2(),
           "element size of the element-wise atomic memory intrinsic "
           "must be a power of 2",
           Call);

    if (auto *LengthCI = dyn_cast<ConstantInt>(AMI->getLength())) {
      uint64_t Length = LengthCI->getZExtValue();
      uint64_t ElementSize = AMI->getElementSizeInBytes();
      Assert((Length % ElementSize) == 0,
             "constant length must be a multiple of the element size in the "
             "element-wise atomic memory intrinsic",
             Call);
    }

    auto IsValidAlignment = [&](uint64_t Alignment) {
      return isPowerOf2_64(Alignment) && ElementSizeVal.ule(Alignment);
    };
    uint64_t DstAlignment = AMI->getDestAlignment();
    Assert(IsValidAlignment(DstAlignment),
           "incorrect alignment of the destination argument", Call);
    if (const auto *AMT = dyn_cast<AtomicMemTransferInst>(AMI)) {
      uint64_t SrcAlignment = AMT->getSourceAlignment();
      Assert(IsValidAlignment(SrcAlignment),
             "incorrect alignment of the source argument", Call);
    }
    break;
  }
  case Intrinsic::gcroot:
  case Intrinsic::gcwrite:
  case Intrinsic::gcread:
    if (ID == Intrinsic::gcroot) {
      AllocaInst *AI =
          dyn_cast<AllocaInst>(Call.getArgOperand(0)->stripPointerCasts());
      Assert(AI, "llvm.gcroot parameter #1 must be an alloca.", Call);
      Assert(isa<Constant>(Call.getArgOperand(1)),
             "llvm.gcroot parameter #2 must be a constant.", Call);
      if (!AI->getAllocatedType()->isPointerTy()) {
        Assert(!isa<ConstantPointerNull>(Call.getArgOperand(1)),
               "llvm.gcroot parameter #1 must either be a pointer alloca, "
               "or argument #2 must be a non-null constant.",
               Call);
      }
    }

    Assert(Call.getParent()->getParent()->hasGC(),
           "Enclosing function does not use GC.", Call);
    break;
  case Intrinsic::init_trampoline:
    Assert(isa<Function>(Call.getArgOperand(1)->stripPointerCasts()),
           "llvm.init_trampoline parameter #2 must resolve to a function.",
           Call);
    break;
  case Intrinsic::prefetch:
    Assert(isa<ConstantInt>(Call.getArgOperand(1)) &&
               isa<ConstantInt>(Call.getArgOperand(2)) &&
               cast<ConstantInt>(Call.getArgOperand(1))->getZExtValue() < 2 &&
               cast<ConstantInt>(Call.getArgOperand(2))->getZExtValue() < 4,
           "invalid arguments to llvm.prefetch", Call);
    break;
  case Intrinsic::stackprotector:
    Assert(isa<AllocaInst>(Call.getArgOperand(1)->stripPointerCasts()),
           "llvm.stackprotector parameter #2 must resolve to an alloca.", Call);
    break;
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  case Intrinsic::invariant_start:
    Assert(isa<ConstantInt>(Call.getArgOperand(0)),
           "size argument of memory use markers must be a constant integer",
           Call);
    break;
  case Intrinsic::invariant_end:
    Assert(isa<ConstantInt>(Call.getArgOperand(1)),
           "llvm.invariant.end parameter #2 must be a constant integer", Call);
    break;

  case Intrinsic::localescape: {
    BasicBlock *BB = Call.getParent();
    Assert(BB == &BB->getParent()->front(),
           "llvm.localescape used outside of entry block", Call);
    Assert(!SawFrameEscape,
           "multiple calls to llvm.localescape in one function", Call);
    for (Value *Arg : Call.args()) {
      if (isa<ConstantPointerNull>(Arg))
        continue; // Null values are allowed as placeholders.
      auto *AI = dyn_cast<AllocaInst>(Arg->stripPointerCasts());
      Assert(AI && AI->isStaticAlloca(),
             "llvm.localescape only accepts static allocas", Call);
    }
    FrameEscapeInfo[BB->getParent()].first = Call.getNumArgOperands();
    SawFrameEscape = true;
    break;
  }
  case Intrinsic::localrecover: {
    Value *FnArg = Call.getArgOperand(0)->stripPointerCasts();
    Function *Fn = dyn_cast<Function>(FnArg);
    Assert(Fn && !Fn->isDeclaration(),
           "llvm.localrecover first "
           "argument must be function defined in this module",
           Call);
    auto *IdxArg = dyn_cast<ConstantInt>(Call.getArgOperand(2));
    Assert(IdxArg, "idx argument of llvm.localrecover must be a constant int",
           Call);
    auto &Entry = FrameEscapeInfo[Fn];
    Entry.second = unsigned(
        std::max(uint64_t(Entry.second), IdxArg->getLimitedValue(~0U) + 1));
    break;
  }

  case Intrinsic::experimental_gc_statepoint:
    if (auto *CI = dyn_cast<CallInst>(&Call))
      Assert(!CI->isInlineAsm(),
             "gc.statepoint support for inline assembly unimplemented", CI);
    Assert(Call.getParent()->getParent()->hasGC(),
           "Enclosing function does not use GC.", Call);

    verifyStatepoint(Call);
    break;
  case Intrinsic::experimental_gc_result: {
    Assert(Call.getParent()->getParent()->hasGC(),
           "Enclosing function does not use GC.", Call);
    // Are we tied to a statepoint properly?
    const auto *StatepointCall = dyn_cast<CallBase>(Call.getArgOperand(0));
    const Function *StatepointFn =
        StatepointCall ? StatepointCall->getCalledFunction() : nullptr;
    Assert(StatepointFn && StatepointFn->isDeclaration() &&
               StatepointFn->getIntrinsicID() ==
                   Intrinsic::experimental_gc_statepoint,
           "gc.result operand #1 must be from a statepoint", Call,
           Call.getArgOperand(0));

    // Assert that result type matches wrapped callee.
    const Value *Target = StatepointCall->getArgOperand(2);
    auto *PT = cast<PointerType>(Target->getType());
    auto *TargetFuncType = cast<FunctionType>(PT->getElementType());
    Assert(Call.getType() == TargetFuncType->getReturnType(),
           "gc.result result type does not match wrapped callee", Call);
    break;
  }
  case Intrinsic::experimental_gc_relocate: {
    Assert(Call.getNumArgOperands() == 3, "wrong number of arguments", Call);

    Assert(isa<PointerType>(Call.getType()->getScalarType()),
           "gc.relocate must return a pointer or a vector of pointers", Call);

    // Check that this relocate is correctly tied to the statepoint

    // This is case for relocate on the unwinding path of an invoke statepoint
    if (LandingPadInst *LandingPad =
            dyn_cast<LandingPadInst>(Call.getArgOperand(0))) {

      const BasicBlock *InvokeBB =
          LandingPad->getParent()->getUniquePredecessor();

      // Landingpad relocates should have only one predecessor with invoke
      // statepoint terminator
      Assert(InvokeBB, "safepoints should have unique landingpads",
             LandingPad->getParent());
      Assert(InvokeBB->getTerminator(), "safepoint block should be well formed",
             InvokeBB);
      Assert(isStatepoint(InvokeBB->getTerminator()),
             "gc relocate should be linked to a statepoint", InvokeBB);
    } else {
      // In all other cases relocate should be tied to the statepoint directly.
      // This covers relocates on a normal return path of invoke statepoint and
      // relocates of a call statepoint.
      auto Token = Call.getArgOperand(0);
      Assert(isa<Instruction>(Token) && isStatepoint(cast<Instruction>(Token)),
             "gc relocate is incorrectly tied to the statepoint", Call, Token);
    }

    // Verify rest of the relocate arguments.
    const CallBase &StatepointCall =
        *cast<CallBase>(cast<GCRelocateInst>(Call).getStatepoint());

    // Both the base and derived must be piped through the safepoint.
    Value *Base = Call.getArgOperand(1);
    Assert(isa<ConstantInt>(Base),
           "gc.relocate operand #2 must be integer offset", Call);

    Value *Derived = Call.getArgOperand(2);
    Assert(isa<ConstantInt>(Derived),
           "gc.relocate operand #3 must be integer offset", Call);

    const int BaseIndex = cast<ConstantInt>(Base)->getZExtValue();
    const int DerivedIndex = cast<ConstantInt>(Derived)->getZExtValue();
    // Check the bounds
    Assert(0 <= BaseIndex && BaseIndex < (int)StatepointCall.arg_size(),
           "gc.relocate: statepoint base index out of bounds", Call);
    Assert(0 <= DerivedIndex && DerivedIndex < (int)StatepointCall.arg_size(),
           "gc.relocate: statepoint derived index out of bounds", Call);

    // Check that BaseIndex and DerivedIndex fall within the 'gc parameters'
    // section of the statepoint's argument.
    Assert(StatepointCall.arg_size() > 0,
           "gc.statepoint: insufficient arguments");
    Assert(isa<ConstantInt>(StatepointCall.getArgOperand(3)),
           "gc.statement: number of call arguments must be constant integer");
    const unsigned NumCallArgs =
        cast<ConstantInt>(StatepointCall.getArgOperand(3))->getZExtValue();
    Assert(StatepointCall.arg_size() > NumCallArgs + 5,
           "gc.statepoint: mismatch in number of call arguments");
    Assert(isa<ConstantInt>(StatepointCall.getArgOperand(NumCallArgs + 5)),
           "gc.statepoint: number of transition arguments must be "
           "a constant integer");
    const int NumTransitionArgs =
        cast<ConstantInt>(StatepointCall.getArgOperand(NumCallArgs + 5))
            ->getZExtValue();
    const int DeoptArgsStart = 4 + NumCallArgs + 1 + NumTransitionArgs + 1;
    Assert(isa<ConstantInt>(StatepointCall.getArgOperand(DeoptArgsStart)),
           "gc.statepoint: number of deoptimization arguments must be "
           "a constant integer");
    const int NumDeoptArgs =
        cast<ConstantInt>(StatepointCall.getArgOperand(DeoptArgsStart))
            ->getZExtValue();
    const int GCParamArgsStart = DeoptArgsStart + 1 + NumDeoptArgs;
    const int GCParamArgsEnd = StatepointCall.arg_size();
    Assert(GCParamArgsStart <= BaseIndex && BaseIndex < GCParamArgsEnd,
           "gc.relocate: statepoint base index doesn't fall within the "
           "'gc parameters' section of the statepoint call",
           Call);
    Assert(GCParamArgsStart <= DerivedIndex && DerivedIndex < GCParamArgsEnd,
           "gc.relocate: statepoint derived index doesn't fall within the "
           "'gc parameters' section of the statepoint call",
           Call);

    // Relocated value must be either a pointer type or vector-of-pointer type,
    // but gc_relocate does not need to return the same pointer type as the
    // relocated pointer. It can be casted to the correct type later if it's
    // desired. However, they must have the same address space and 'vectorness'
    GCRelocateInst &Relocate = cast<GCRelocateInst>(Call);
    Assert(Relocate.getDerivedPtr()->getType()->isPtrOrPtrVectorTy(),
           "gc.relocate: relocated value must be a gc pointer", Call);

    auto ResultType = Call.getType();
    auto DerivedType = Relocate.getDerivedPtr()->getType();
    Assert(ResultType->isVectorTy() == DerivedType->isVectorTy(),
           "gc.relocate: vector relocates to vector and pointer to pointer",
           Call);
    Assert(
        ResultType->getPointerAddressSpace() ==
            DerivedType->getPointerAddressSpace(),
        "gc.relocate: relocating a pointer shouldn't change its address space",
        Call);
    break;
  }
  case Intrinsic::eh_exceptioncode:
  case Intrinsic::eh_exceptionpointer: {
    Assert(isa<CatchPadInst>(Call.getArgOperand(0)),
           "eh.exceptionpointer argument must be a catchpad", Call);
    break;
  }
  case Intrinsic::masked_load: {
    Assert(Call.getType()->isVectorTy(), "masked_load: must return a vector",
           Call);

    Value *Ptr = Call.getArgOperand(0);
    // Value *Alignment = Call.getArgOperand(1);
    Value *Mask = Call.getArgOperand(2);
    Value *PassThru = Call.getArgOperand(3);
    Assert(Mask->getType()->isVectorTy(), "masked_load: mask must be vector",
           Call);

    // DataTy is the overloaded type
    Type *DataTy = cast<PointerType>(Ptr->getType())->getElementType();
    Assert(DataTy == Call.getType(),
           "masked_load: return must match pointer type", Call);
    Assert(PassThru->getType() == DataTy,
           "masked_load: pass through and data type must match", Call);
    Assert(Mask->getType()->getVectorNumElements() ==
               DataTy->getVectorNumElements(),
           "masked_load: vector mask must be same length as data", Call);
    break;
  }
  case Intrinsic::masked_store: {
    Value *Val = Call.getArgOperand(0);
    Value *Ptr = Call.getArgOperand(1);
    // Value *Alignment = Call.getArgOperand(2);
    Value *Mask = Call.getArgOperand(3);
    Assert(Mask->getType()->isVectorTy(), "masked_store: mask must be vector",
           Call);

    // DataTy is the overloaded type
    Type *DataTy = cast<PointerType>(Ptr->getType())->getElementType();
    Assert(DataTy == Val->getType(),
           "masked_store: storee must match pointer type", Call);
    Assert(Mask->getType()->getVectorNumElements() ==
               DataTy->getVectorNumElements(),
           "masked_store: vector mask must be same length as data", Call);
    break;
  }

  case Intrinsic::experimental_guard: {
    Assert(isa<CallInst>(Call), "experimental_guard cannot be invoked", Call);
    Assert(Call.countOperandBundlesOfType(LLVMContext::OB_deopt) == 1,
           "experimental_guard must have exactly one "
           "\"deopt\" operand bundle");
    break;
  }

  case Intrinsic::experimental_deoptimize: {
    Assert(isa<CallInst>(Call), "experimental_deoptimize cannot be invoked",
           Call);
    Assert(Call.countOperandBundlesOfType(LLVMContext::OB_deopt) == 1,
           "experimental_deoptimize must have exactly one "
           "\"deopt\" operand bundle");
    Assert(Call.getType() == Call.getFunction()->getReturnType(),
           "experimental_deoptimize return type must match caller return type");

    if (isa<CallInst>(Call)) {
      auto *RI = dyn_cast<ReturnInst>(Call.getNextNode());
      Assert(RI,
             "calls to experimental_deoptimize must be followed by a return");

      if (!Call.getType()->isVoidTy() && RI)
        Assert(RI->getReturnValue() == &Call,
               "calls to experimental_deoptimize must be followed by a return "
               "of the value computed by experimental_deoptimize");
    }

    break;
  }
  case Intrinsic::sadd_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::usub_sat: {
    Value *Op1 = Call.getArgOperand(0);
    Value *Op2 = Call.getArgOperand(1);
    Assert(Op1->getType()->isIntOrIntVectorTy(),
           "first operand of [us][add|sub]_sat must be an int type or vector "
           "of ints");
    Assert(Op2->getType()->isIntOrIntVectorTy(),
           "second operand of [us][add|sub]_sat must be an int type or vector "
           "of ints");
    break;
  }
  case Intrinsic::smul_fix: {
    Value *Op1 = Call.getArgOperand(0);
    Value *Op2 = Call.getArgOperand(1);
    Assert(Op1->getType()->isIntOrIntVectorTy(),
           "first operand of smul_fix must be an int type or vector "
           "of ints");
    Assert(Op2->getType()->isIntOrIntVectorTy(),
           "second operand of smul_fix must be an int type or vector "
           "of ints");

    auto *Op3 = dyn_cast<ConstantInt>(Call.getArgOperand(2));
    Assert(Op3, "third argument of smul_fix must be a constant integer");
    Assert(Op3->getType()->getBitWidth() <= 32,
           "third argument of smul_fix must fit within 32 bits");
    Assert(Op3->getZExtValue() < Op1->getType()->getScalarSizeInBits(),
           "the scale of smul_fix must be less than the width of the operands");
    break;
  }
  };
}

/// Carefully grab the subprogram from a local scope.
///
/// This carefully grabs the subprogram from a local scope, avoiding the
/// built-in assertions that would typically fire.
static DISubprogram *getSubprogram(Metadata *LocalScope) {
  if (!LocalScope)
    return nullptr;

  if (auto *SP = dyn_cast<DISubprogram>(LocalScope))
    return SP;

  if (auto *LB = dyn_cast<DILexicalBlockBase>(LocalScope))
    return getSubprogram(LB->getRawScope());

  // Just return null; broken scope chains are checked elsewhere.
  assert(!isa<DILocalScope>(LocalScope) && "Unknown type of local scope");
  return nullptr;
}

void Verifier::visitConstrainedFPIntrinsic(ConstrainedFPIntrinsic &FPI) {
  unsigned NumOperands = FPI.getNumArgOperands();
  Assert(((NumOperands == 5 && FPI.isTernaryOp()) ||
          (NumOperands == 3 && FPI.isUnaryOp()) || (NumOperands == 4)),
           "invalid arguments for constrained FP intrinsic", &FPI);
  Assert(isa<MetadataAsValue>(FPI.getArgOperand(NumOperands-1)),
         "invalid exception behavior argument", &FPI);
  Assert(isa<MetadataAsValue>(FPI.getArgOperand(NumOperands-2)),
         "invalid rounding mode argument", &FPI);
  Assert(FPI.getRoundingMode() != ConstrainedFPIntrinsic::rmInvalid,
         "invalid rounding mode argument", &FPI);
  Assert(FPI.getExceptionBehavior() != ConstrainedFPIntrinsic::ebInvalid,
         "invalid exception behavior argument", &FPI);
}

void Verifier::visitDbgIntrinsic(StringRef Kind, DbgVariableIntrinsic &DII) {
  auto *MD = cast<MetadataAsValue>(DII.getArgOperand(0))->getMetadata();
  AssertDI(isa<ValueAsMetadata>(MD) ||
             (isa<MDNode>(MD) && !cast<MDNode>(MD)->getNumOperands()),
         "invalid llvm.dbg." + Kind + " intrinsic address/value", &DII, MD);
  AssertDI(isa<DILocalVariable>(DII.getRawVariable()),
         "invalid llvm.dbg." + Kind + " intrinsic variable", &DII,
         DII.getRawVariable());
  AssertDI(isa<DIExpression>(DII.getRawExpression()),
         "invalid llvm.dbg." + Kind + " intrinsic expression", &DII,
         DII.getRawExpression());

  // Ignore broken !dbg attachments; they're checked elsewhere.
  if (MDNode *N = DII.getDebugLoc().getAsMDNode())
    if (!isa<DILocation>(N))
      return;

  BasicBlock *BB = DII.getParent();
  Function *F = BB ? BB->getParent() : nullptr;

  // The scopes for variables and !dbg attachments must agree.
  DILocalVariable *Var = DII.getVariable();
  DILocation *Loc = DII.getDebugLoc();
  AssertDI(Loc, "llvm.dbg." + Kind + " intrinsic requires a !dbg attachment",
           &DII, BB, F);

  DISubprogram *VarSP = getSubprogram(Var->getRawScope());
  DISubprogram *LocSP = getSubprogram(Loc->getRawScope());
  if (!VarSP || !LocSP)
    return; // Broken scope chains are checked elsewhere.

  AssertDI(VarSP == LocSP, "mismatched subprogram between llvm.dbg." + Kind +
                               " variable and !dbg attachment",
           &DII, BB, F, Var, Var->getScope()->getSubprogram(), Loc,
           Loc->getScope()->getSubprogram());

  // This check is redundant with one in visitLocalVariable().
  AssertDI(isType(Var->getRawType()), "invalid type ref", Var,
           Var->getRawType());
  if (auto *Type = dyn_cast_or_null<DIType>(Var->getRawType()))
    if (Type->isBlockByrefStruct())
      AssertDI(DII.getExpression() && DII.getExpression()->getNumElements(),
               "BlockByRef variable without complex expression", Var, &DII);

  verifyFnArgs(DII);
}

void Verifier::visitDbgLabelIntrinsic(StringRef Kind, DbgLabelInst &DLI) {
  AssertDI(isa<DILabel>(DLI.getRawLabel()),
         "invalid llvm.dbg." + Kind + " intrinsic variable", &DLI,
         DLI.getRawLabel());

  // Ignore broken !dbg attachments; they're checked elsewhere.
  if (MDNode *N = DLI.getDebugLoc().getAsMDNode())
    if (!isa<DILocation>(N))
      return;

  BasicBlock *BB = DLI.getParent();
  Function *F = BB ? BB->getParent() : nullptr;

  // The scopes for variables and !dbg attachments must agree.
  DILabel *Label = DLI.getLabel();
  DILocation *Loc = DLI.getDebugLoc();
  Assert(Loc, "llvm.dbg." + Kind + " intrinsic requires a !dbg attachment",
         &DLI, BB, F);

  DISubprogram *LabelSP = getSubprogram(Label->getRawScope());
  DISubprogram *LocSP = getSubprogram(Loc->getRawScope());
  if (!LabelSP || !LocSP)
    return;

  AssertDI(LabelSP == LocSP, "mismatched subprogram between llvm.dbg." + Kind +
                             " label and !dbg attachment",
           &DLI, BB, F, Label, Label->getScope()->getSubprogram(), Loc,
           Loc->getScope()->getSubprogram());
}

void Verifier::verifyFragmentExpression(const DbgVariableIntrinsic &I) {
  DILocalVariable *V = dyn_cast_or_null<DILocalVariable>(I.getRawVariable());
  DIExpression *E = dyn_cast_or_null<DIExpression>(I.getRawExpression());

  // We don't know whether this intrinsic verified correctly.
  if (!V || !E || !E->isValid())
    return;

  // Nothing to do if this isn't a DW_OP_LLVM_fragment expression.
  auto Fragment = E->getFragmentInfo();
  if (!Fragment)
    return;

  // The frontend helps out GDB by emitting the members of local anonymous
  // unions as artificial local variables with shared storage. When SROA splits
  // the storage for artificial local variables that are smaller than the entire
  // union, the overhang piece will be outside of the allotted space for the
  // variable and this check fails.
  // FIXME: Remove this check as soon as clang stops doing this; it hides bugs.
  if (V->isArtificial())
    return;

  verifyFragmentExpression(*V, *Fragment, &I);
}

template <typename ValueOrMetadata>
void Verifier::verifyFragmentExpression(const DIVariable &V,
                                        DIExpression::FragmentInfo Fragment,
                                        ValueOrMetadata *Desc) {
  // If there's no size, the type is broken, but that should be checked
  // elsewhere.
  auto VarSize = V.getSizeInBits();
  if (!VarSize)
    return;

  unsigned FragSize = Fragment.SizeInBits;
  unsigned FragOffset = Fragment.OffsetInBits;
  AssertDI(FragSize + FragOffset <= *VarSize,
         "fragment is larger than or outside of variable", Desc, &V);
  AssertDI(FragSize != *VarSize, "fragment covers entire variable", Desc, &V);
}

void Verifier::verifyFnArgs(const DbgVariableIntrinsic &I) {
  // This function does not take the scope of noninlined function arguments into
  // account. Don't run it if current function is nodebug, because it may
  // contain inlined debug intrinsics.
  if (!HasDebugInfo)
    return;

  // For performance reasons only check non-inlined ones.
  if (I.getDebugLoc()->getInlinedAt())
    return;

  DILocalVariable *Var = I.getVariable();
  AssertDI(Var, "dbg intrinsic without variable");

  unsigned ArgNo = Var->getArg();
  if (!ArgNo)
    return;

  // Verify there are no duplicate function argument debug info entries.
  // These will cause hard-to-debug assertions in the DWARF backend.
  if (DebugFnArgs.size() < ArgNo)
    DebugFnArgs.resize(ArgNo, nullptr);

  auto *Prev = DebugFnArgs[ArgNo - 1];
  DebugFnArgs[ArgNo - 1] = Var;
  AssertDI(!Prev || (Prev == Var), "conflicting debug info for argument", &I,
           Prev, Var);
}

void Verifier::verifyCompileUnits() {
  // When more than one Module is imported into the same context, such as during
  // an LTO build before linking the modules, ODR type uniquing may cause types
  // to point to a different CU. This check does not make sense in this case.
  if (M.getContext().isODRUniquingDebugTypes())
    return;
  auto *CUs = M.getNamedMetadata("llvm.dbg.cu");
  SmallPtrSet<const Metadata *, 2> Listed;
  if (CUs)
    Listed.insert(CUs->op_begin(), CUs->op_end());
  for (auto *CU : CUVisited)
    AssertDI(Listed.count(CU), "DICompileUnit not listed in llvm.dbg.cu", CU);
  CUVisited.clear();
}

void Verifier::verifyDeoptimizeCallingConvs() {
  if (DeoptimizeDeclarations.empty())
    return;

  const Function *First = DeoptimizeDeclarations[0];
  for (auto *F : makeArrayRef(DeoptimizeDeclarations).slice(1)) {
    Assert(First->getCallingConv() == F->getCallingConv(),
           "All llvm.experimental.deoptimize declarations must have the same "
           "calling convention",
           First, F);
  }
}

void Verifier::verifySourceDebugInfo(const DICompileUnit &U, const DIFile &F) {
  bool HasSource = F.getSource().hasValue();
  if (!HasSourceDebugInfo.count(&U))
    HasSourceDebugInfo[&U] = HasSource;
  AssertDI(HasSource == HasSourceDebugInfo[&U],
           "inconsistent use of embedded source");
}

//===----------------------------------------------------------------------===//
//  Implement the public interfaces to this file...
//===----------------------------------------------------------------------===//

bool llvm::verifyFunction(const Function &f, raw_ostream *OS) {
  Function &F = const_cast<Function &>(f);

  // Don't use a raw_null_ostream.  Printing IR is expensive.
  Verifier V(OS, /*ShouldTreatBrokenDebugInfoAsError=*/true, *f.getParent());

  // Note that this function's return value is inverted from what you would
  // expect of a function called "verify".
  return !V.verify(F);
}

bool llvm::verifyModule(const Module &M, raw_ostream *OS,
                        bool *BrokenDebugInfo) {
  // Don't use a raw_null_ostream.  Printing IR is expensive.
  Verifier V(OS, /*ShouldTreatBrokenDebugInfoAsError=*/!BrokenDebugInfo, M);

  bool Broken = false;
  for (const Function &F : M)
    Broken |= !V.verify(F);

  Broken |= !V.verify();
  if (BrokenDebugInfo)
    *BrokenDebugInfo = V.hasBrokenDebugInfo();
  // Note that this function's return value is inverted from what you would
  // expect of a function called "verify".
  return Broken;
}

namespace {

struct VerifierLegacyPass : public FunctionPass {
  static char ID;

  std::unique_ptr<Verifier> V;
  bool FatalErrors = true;

  VerifierLegacyPass() : FunctionPass(ID) {
    initializeVerifierLegacyPassPass(*PassRegistry::getPassRegistry());
  }
  explicit VerifierLegacyPass(bool FatalErrors)
      : FunctionPass(ID),
        FatalErrors(FatalErrors) {
    initializeVerifierLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool doInitialization(Module &M) override {
    V = llvm::make_unique<Verifier>(
        &dbgs(), /*ShouldTreatBrokenDebugInfoAsError=*/false, M);
    return false;
  }

  bool runOnFunction(Function &F) override {
    if (!V->verify(F) && FatalErrors) {
      errs() << "in function " << F.getName() << '\n'; 
      report_fatal_error("Broken function found, compilation aborted!");
    }
    return false;
  }

  bool doFinalization(Module &M) override {
    bool HasErrors = false;
    for (Function &F : M)
      if (F.isDeclaration())
        HasErrors |= !V->verify(F);

    HasErrors |= !V->verify();
    if (FatalErrors && (HasErrors || V->hasBrokenDebugInfo()))
      report_fatal_error("Broken module found, compilation aborted!");
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // end anonymous namespace

/// Helper to issue failure from the TBAA verification
template <typename... Tys> void TBAAVerifier::CheckFailed(Tys &&... Args) {
  if (Diagnostic)
    return Diagnostic->CheckFailed(Args...);
}

#define AssertTBAA(C, ...)                                                     \
  do {                                                                         \
    if (!(C)) {                                                                \
      CheckFailed(__VA_ARGS__);                                                \
      return false;                                                            \
    }                                                                          \
  } while (false)

/// Verify that \p BaseNode can be used as the "base type" in the struct-path
/// TBAA scheme.  This means \p BaseNode is either a scalar node, or a
/// struct-type node describing an aggregate data structure (like a struct).
TBAAVerifier::TBAABaseNodeSummary
TBAAVerifier::verifyTBAABaseNode(Instruction &I, const MDNode *BaseNode,
                                 bool IsNewFormat) {
  if (BaseNode->getNumOperands() < 2) {
    CheckFailed("Base nodes must have at least two operands", &I, BaseNode);
    return {true, ~0u};
  }

  auto Itr = TBAABaseNodes.find(BaseNode);
  if (Itr != TBAABaseNodes.end())
    return Itr->second;

  auto Result = verifyTBAABaseNodeImpl(I, BaseNode, IsNewFormat);
  auto InsertResult = TBAABaseNodes.insert({BaseNode, Result});
  (void)InsertResult;
  assert(InsertResult.second && "We just checked!");
  return Result;
}

TBAAVerifier::TBAABaseNodeSummary
TBAAVerifier::verifyTBAABaseNodeImpl(Instruction &I, const MDNode *BaseNode,
                                     bool IsNewFormat) {
  const TBAAVerifier::TBAABaseNodeSummary InvalidNode = {true, ~0u};

  if (BaseNode->getNumOperands() == 2) {
    // Scalar nodes can only be accessed at offset 0.
    return isValidScalarTBAANode(BaseNode)
               ? TBAAVerifier::TBAABaseNodeSummary({false, 0})
               : InvalidNode;
  }

  if (IsNewFormat) {
    if (BaseNode->getNumOperands() % 3 != 0) {
      CheckFailed("Access tag nodes must have the number of operands that is a "
                  "multiple of 3!", BaseNode);
      return InvalidNode;
    }
  } else {
    if (BaseNode->getNumOperands() % 2 != 1) {
      CheckFailed("Struct tag nodes must have an odd number of operands!",
                  BaseNode);
      return InvalidNode;
    }
  }

  // Check the type size field.
  if (IsNewFormat) {
    auto *TypeSizeNode = mdconst::dyn_extract_or_null<ConstantInt>(
        BaseNode->getOperand(1));
    if (!TypeSizeNode) {
      CheckFailed("Type size nodes must be constants!", &I, BaseNode);
      return InvalidNode;
    }
  }

  // Check the type name field. In the new format it can be anything.
  if (!IsNewFormat && !isa<MDString>(BaseNode->getOperand(0))) {
    CheckFailed("Struct tag nodes have a string as their first operand",
                BaseNode);
    return InvalidNode;
  }

  bool Failed = false;

  Optional<APInt> PrevOffset;
  unsigned BitWidth = ~0u;

  // We've already checked that BaseNode is not a degenerate root node with one
  // operand in \c verifyTBAABaseNode, so this loop should run at least once.
  unsigned FirstFieldOpNo = IsNewFormat ? 3 : 1;
  unsigned NumOpsPerField = IsNewFormat ? 3 : 2;
  for (unsigned Idx = FirstFieldOpNo; Idx < BaseNode->getNumOperands();
           Idx += NumOpsPerField) {
    const MDOperand &FieldTy = BaseNode->getOperand(Idx);
    const MDOperand &FieldOffset = BaseNode->getOperand(Idx + 1);
    if (!isa<MDNode>(FieldTy)) {
      CheckFailed("Incorrect field entry in struct type node!", &I, BaseNode);
      Failed = true;
      continue;
    }

    auto *OffsetEntryCI =
        mdconst::dyn_extract_or_null<ConstantInt>(FieldOffset);
    if (!OffsetEntryCI) {
      CheckFailed("Offset entries must be constants!", &I, BaseNode);
      Failed = true;
      continue;
    }

    if (BitWidth == ~0u)
      BitWidth = OffsetEntryCI->getBitWidth();

    if (OffsetEntryCI->getBitWidth() != BitWidth) {
      CheckFailed(
          "Bitwidth between the offsets and struct type entries must match", &I,
          BaseNode);
      Failed = true;
      continue;
    }

    // NB! As far as I can tell, we generate a non-strictly increasing offset
    // sequence only from structs that have zero size bit fields.  When
    // recursing into a contained struct in \c getFieldNodeFromTBAABaseNode we
    // pick the field lexically the latest in struct type metadata node.  This
    // mirrors the actual behavior of the alias analysis implementation.
    bool IsAscending =
        !PrevOffset || PrevOffset->ule(OffsetEntryCI->getValue());

    if (!IsAscending) {
      CheckFailed("Offsets must be increasing!", &I, BaseNode);
      Failed = true;
    }

    PrevOffset = OffsetEntryCI->getValue();

    if (IsNewFormat) {
      auto *MemberSizeNode = mdconst::dyn_extract_or_null<ConstantInt>(
          BaseNode->getOperand(Idx + 2));
      if (!MemberSizeNode) {
        CheckFailed("Member size entries must be constants!", &I, BaseNode);
        Failed = true;
        continue;
      }
    }
  }

  return Failed ? InvalidNode
                : TBAAVerifier::TBAABaseNodeSummary(false, BitWidth);
}

static bool IsRootTBAANode(const MDNode *MD) {
  return MD->getNumOperands() < 2;
}

static bool IsScalarTBAANodeImpl(const MDNode *MD,
                                 SmallPtrSetImpl<const MDNode *> &Visited) {
  if (MD->getNumOperands() != 2 && MD->getNumOperands() != 3)
    return false;

  if (!isa<MDString>(MD->getOperand(0)))
    return false;

  if (MD->getNumOperands() == 3) {
    auto *Offset = mdconst::dyn_extract<ConstantInt>(MD->getOperand(2));
    if (!(Offset && Offset->isZero() && isa<MDString>(MD->getOperand(0))))
      return false;
  }

  auto *Parent = dyn_cast_or_null<MDNode>(MD->getOperand(1));
  return Parent && Visited.insert(Parent).second &&
         (IsRootTBAANode(Parent) || IsScalarTBAANodeImpl(Parent, Visited));
}

bool TBAAVerifier::isValidScalarTBAANode(const MDNode *MD) {
  auto ResultIt = TBAAScalarNodes.find(MD);
  if (ResultIt != TBAAScalarNodes.end())
    return ResultIt->second;

  SmallPtrSet<const MDNode *, 4> Visited;
  bool Result = IsScalarTBAANodeImpl(MD, Visited);
  auto InsertResult = TBAAScalarNodes.insert({MD, Result});
  (void)InsertResult;
  assert(InsertResult.second && "Just checked!");

  return Result;
}

/// Returns the field node at the offset \p Offset in \p BaseNode.  Update \p
/// Offset in place to be the offset within the field node returned.
///
/// We assume we've okayed \p BaseNode via \c verifyTBAABaseNode.
MDNode *TBAAVerifier::getFieldNodeFromTBAABaseNode(Instruction &I,
                                                   const MDNode *BaseNode,
                                                   APInt &Offset,
                                                   bool IsNewFormat) {
  assert(BaseNode->getNumOperands() >= 2 && "Invalid base node!");

  // Scalar nodes have only one possible "field" -- their parent in the access
  // hierarchy.  Offset must be zero at this point, but our caller is supposed
  // to Assert that.
  if (BaseNode->getNumOperands() == 2)
    return cast<MDNode>(BaseNode->getOperand(1));

  unsigned FirstFieldOpNo = IsNewFormat ? 3 : 1;
  unsigned NumOpsPerField = IsNewFormat ? 3 : 2;
  for (unsigned Idx = FirstFieldOpNo; Idx < BaseNode->getNumOperands();
           Idx += NumOpsPerField) {
    auto *OffsetEntryCI =
        mdconst::extract<ConstantInt>(BaseNode->getOperand(Idx + 1));
    if (OffsetEntryCI->getValue().ugt(Offset)) {
      if (Idx == FirstFieldOpNo) {
        CheckFailed("Could not find TBAA parent in struct type node", &I,
                    BaseNode, &Offset);
        return nullptr;
      }

      unsigned PrevIdx = Idx - NumOpsPerField;
      auto *PrevOffsetEntryCI =
          mdconst::extract<ConstantInt>(BaseNode->getOperand(PrevIdx + 1));
      Offset -= PrevOffsetEntryCI->getValue();
      return cast<MDNode>(BaseNode->getOperand(PrevIdx));
    }
  }

  unsigned LastIdx = BaseNode->getNumOperands() - NumOpsPerField;
  auto *LastOffsetEntryCI = mdconst::extract<ConstantInt>(
      BaseNode->getOperand(LastIdx + 1));
  Offset -= LastOffsetEntryCI->getValue();
  return cast<MDNode>(BaseNode->getOperand(LastIdx));
}

static bool isNewFormatTBAATypeNode(llvm::MDNode *Type) {
  if (!Type || Type->getNumOperands() < 3)
    return false;

  // In the new format type nodes shall have a reference to the parent type as
  // its first operand.
  MDNode *Parent = dyn_cast_or_null<MDNode>(Type->getOperand(0));
  if (!Parent)
    return false;

  return true;
}

bool TBAAVerifier::visitTBAAMetadata(Instruction &I, const MDNode *MD) {
  AssertTBAA(isa<LoadInst>(I) || isa<StoreInst>(I) || isa<CallInst>(I) ||
                 isa<VAArgInst>(I) || isa<AtomicRMWInst>(I) ||
                 isa<AtomicCmpXchgInst>(I),
             "This instruction shall not have a TBAA access tag!", &I);

  bool IsStructPathTBAA =
      isa<MDNode>(MD->getOperand(0)) && MD->getNumOperands() >= 3;

  AssertTBAA(
      IsStructPathTBAA,
      "Old-style TBAA is no longer allowed, use struct-path TBAA instead", &I);

  MDNode *BaseNode = dyn_cast_or_null<MDNode>(MD->getOperand(0));
  MDNode *AccessType = dyn_cast_or_null<MDNode>(MD->getOperand(1));

  bool IsNewFormat = isNewFormatTBAATypeNode(AccessType);

  if (IsNewFormat) {
    AssertTBAA(MD->getNumOperands() == 4 || MD->getNumOperands() == 5,
               "Access tag metadata must have either 4 or 5 operands", &I, MD);
  } else {
    AssertTBAA(MD->getNumOperands() < 5,
               "Struct tag metadata must have either 3 or 4 operands", &I, MD);
  }

  // Check the access size field.
  if (IsNewFormat) {
    auto *AccessSizeNode = mdconst::dyn_extract_or_null<ConstantInt>(
        MD->getOperand(3));
    AssertTBAA(AccessSizeNode, "Access size field must be a constant", &I, MD);
  }

  // Check the immutability flag.
  unsigned ImmutabilityFlagOpNo = IsNewFormat ? 4 : 3;
  if (MD->getNumOperands() == ImmutabilityFlagOpNo + 1) {
    auto *IsImmutableCI = mdconst::dyn_extract_or_null<ConstantInt>(
        MD->getOperand(ImmutabilityFlagOpNo));
    AssertTBAA(IsImmutableCI,
               "Immutability tag on struct tag metadata must be a constant",
               &I, MD);
    AssertTBAA(
        IsImmutableCI->isZero() || IsImmutableCI->isOne(),
        "Immutability part of the struct tag metadata must be either 0 or 1",
        &I, MD);
  }

  AssertTBAA(BaseNode && AccessType,
             "Malformed struct tag metadata: base and access-type "
             "should be non-null and point to Metadata nodes",
             &I, MD, BaseNode, AccessType);

  if (!IsNewFormat) {
    AssertTBAA(isValidScalarTBAANode(AccessType),
               "Access type node must be a valid scalar type", &I, MD,
               AccessType);
  }

  auto *OffsetCI = mdconst::dyn_extract_or_null<ConstantInt>(MD->getOperand(2));
  AssertTBAA(OffsetCI, "Offset must be constant integer", &I, MD);

  APInt Offset = OffsetCI->getValue();
  bool SeenAccessTypeInPath = false;

  SmallPtrSet<MDNode *, 4> StructPath;

  for (/* empty */; BaseNode && !IsRootTBAANode(BaseNode);
       BaseNode = getFieldNodeFromTBAABaseNode(I, BaseNode, Offset,
                                               IsNewFormat)) {
    if (!StructPath.insert(BaseNode).second) {
      CheckFailed("Cycle detected in struct path", &I, MD);
      return false;
    }

    bool Invalid;
    unsigned BaseNodeBitWidth;
    std::tie(Invalid, BaseNodeBitWidth) = verifyTBAABaseNode(I, BaseNode,
                                                             IsNewFormat);

    // If the base node is invalid in itself, then we've already printed all the
    // errors we wanted to print.
    if (Invalid)
      return false;

    SeenAccessTypeInPath |= BaseNode == AccessType;

    if (isValidScalarTBAANode(BaseNode) || BaseNode == AccessType)
      AssertTBAA(Offset == 0, "Offset not zero at the point of scalar access",
                 &I, MD, &Offset);

    AssertTBAA(BaseNodeBitWidth == Offset.getBitWidth() ||
                   (BaseNodeBitWidth == 0 && Offset == 0) ||
                   (IsNewFormat && BaseNodeBitWidth == ~0u),
               "Access bit-width not the same as description bit-width", &I, MD,
               BaseNodeBitWidth, Offset.getBitWidth());

    if (IsNewFormat && SeenAccessTypeInPath)
      break;
  }

  AssertTBAA(SeenAccessTypeInPath, "Did not see access type in access path!",
             &I, MD);
  return true;
}

char VerifierLegacyPass::ID = 0;
INITIALIZE_PASS(VerifierLegacyPass, "verify", "Module Verifier", false, false)

FunctionPass *llvm::createVerifierPass(bool FatalErrors) {
  return new VerifierLegacyPass(FatalErrors);
}

AnalysisKey VerifierAnalysis::Key;
VerifierAnalysis::Result VerifierAnalysis::run(Module &M,
                                               ModuleAnalysisManager &) {
  Result Res;
  Res.IRBroken = llvm::verifyModule(M, &dbgs(), &Res.DebugInfoBroken);
  return Res;
}

VerifierAnalysis::Result VerifierAnalysis::run(Function &F,
                                               FunctionAnalysisManager &) {
  return { llvm::verifyFunction(F, &dbgs()), false };
}

PreservedAnalyses VerifierPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto Res = AM.getResult<VerifierAnalysis>(M);
  if (FatalErrors && (Res.IRBroken || Res.DebugInfoBroken))
    report_fatal_error("Broken module found, compilation aborted!");

  return PreservedAnalyses::all();
}

PreservedAnalyses VerifierPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto res = AM.getResult<VerifierAnalysis>(F);
  if (res.IRBroken && FatalErrors)
    report_fatal_error("Broken function found, compilation aborted!");

  return PreservedAnalyses::all();
}
