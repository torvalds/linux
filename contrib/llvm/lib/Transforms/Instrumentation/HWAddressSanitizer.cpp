//===- HWAddressSanitizer.cpp - detector of uninitialized reads -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file is a part of HWAddressSanitizer, an address sanity checker
/// based on tagged addressing.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <sstream>

using namespace llvm;

#define DEBUG_TYPE "hwasan"

static const char *const kHwasanModuleCtorName = "hwasan.module_ctor";
static const char *const kHwasanInitName = "__hwasan_init";

static const char *const kHwasanShadowMemoryDynamicAddress =
    "__hwasan_shadow_memory_dynamic_address";

// Accesses sizes are powers of two: 1, 2, 4, 8, 16.
static const size_t kNumberOfAccessSizes = 5;

static const size_t kDefaultShadowScale = 4;
static const uint64_t kDynamicShadowSentinel =
    std::numeric_limits<uint64_t>::max();
static const unsigned kPointerTagShift = 56;

static const unsigned kShadowBaseAlignment = 32;

static cl::opt<std::string> ClMemoryAccessCallbackPrefix(
    "hwasan-memory-access-callback-prefix",
    cl::desc("Prefix for memory access callbacks"), cl::Hidden,
    cl::init("__hwasan_"));

static cl::opt<bool>
    ClInstrumentWithCalls("hwasan-instrument-with-calls",
                cl::desc("instrument reads and writes with callbacks"),
                cl::Hidden, cl::init(false));

static cl::opt<bool> ClInstrumentReads("hwasan-instrument-reads",
                                       cl::desc("instrument read instructions"),
                                       cl::Hidden, cl::init(true));

static cl::opt<bool> ClInstrumentWrites(
    "hwasan-instrument-writes", cl::desc("instrument write instructions"),
    cl::Hidden, cl::init(true));

static cl::opt<bool> ClInstrumentAtomics(
    "hwasan-instrument-atomics",
    cl::desc("instrument atomic instructions (rmw, cmpxchg)"), cl::Hidden,
    cl::init(true));

static cl::opt<bool> ClRecover(
    "hwasan-recover",
    cl::desc("Enable recovery mode (continue-after-error)."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> ClInstrumentStack("hwasan-instrument-stack",
                                       cl::desc("instrument stack (allocas)"),
                                       cl::Hidden, cl::init(true));

static cl::opt<bool> ClUARRetagToZero(
    "hwasan-uar-retag-to-zero",
    cl::desc("Clear alloca tags before returning from the function to allow "
             "non-instrumented and instrumented function calls mix. When set "
             "to false, allocas are retagged before returning from the "
             "function to detect use after return."),
    cl::Hidden, cl::init(true));

static cl::opt<bool> ClGenerateTagsWithCalls(
    "hwasan-generate-tags-with-calls",
    cl::desc("generate new tags with runtime library calls"), cl::Hidden,
    cl::init(false));

static cl::opt<int> ClMatchAllTag(
    "hwasan-match-all-tag",
    cl::desc("don't report bad accesses via pointers with this tag"),
    cl::Hidden, cl::init(-1));

static cl::opt<bool> ClEnableKhwasan(
    "hwasan-kernel",
    cl::desc("Enable KernelHWAddressSanitizer instrumentation"),
    cl::Hidden, cl::init(false));

// These flags allow to change the shadow mapping and control how shadow memory
// is accessed. The shadow mapping looks like:
//    Shadow = (Mem >> scale) + offset

static cl::opt<unsigned long long> ClMappingOffset(
    "hwasan-mapping-offset",
    cl::desc("HWASan shadow mapping offset [EXPERIMENTAL]"), cl::Hidden,
    cl::init(0));

static cl::opt<bool>
    ClWithIfunc("hwasan-with-ifunc",
                cl::desc("Access dynamic shadow through an ifunc global on "
                         "platforms that support this"),
                cl::Hidden, cl::init(false));

static cl::opt<bool> ClWithTls(
    "hwasan-with-tls",
    cl::desc("Access dynamic shadow through an thread-local pointer on "
             "platforms that support this"),
    cl::Hidden, cl::init(true));

static cl::opt<bool>
    ClRecordStackHistory("hwasan-record-stack-history",
                         cl::desc("Record stack frames with tagged allocations "
                                  "in a thread-local ring buffer"),
                         cl::Hidden, cl::init(true));
static cl::opt<bool>
    ClCreateFrameDescriptions("hwasan-create-frame-descriptions",
                              cl::desc("create static frame descriptions"),
                              cl::Hidden, cl::init(true));

static cl::opt<bool>
    ClInstrumentMemIntrinsics("hwasan-instrument-mem-intrinsics",
                              cl::desc("instrument memory intrinsics"),
                              cl::Hidden, cl::init(true));
namespace {

/// An instrumentation pass implementing detection of addressability bugs
/// using tagged pointers.
class HWAddressSanitizer : public FunctionPass {
public:
  // Pass identification, replacement for typeid.
  static char ID;

  explicit HWAddressSanitizer(bool CompileKernel = false, bool Recover = false)
      : FunctionPass(ID) {
    this->Recover = ClRecover.getNumOccurrences() > 0 ? ClRecover : Recover;
    this->CompileKernel = ClEnableKhwasan.getNumOccurrences() > 0 ?
        ClEnableKhwasan : CompileKernel;
  }

  StringRef getPassName() const override { return "HWAddressSanitizer"; }

  bool runOnFunction(Function &F) override;
  bool doInitialization(Module &M) override;

  void initializeCallbacks(Module &M);

  Value *getDynamicShadowNonTls(IRBuilder<> &IRB);

  void untagPointerOperand(Instruction *I, Value *Addr);
  Value *memToShadow(Value *Shadow, Type *Ty, IRBuilder<> &IRB);
  void instrumentMemAccessInline(Value *PtrLong, bool IsWrite,
                                 unsigned AccessSizeIndex,
                                 Instruction *InsertBefore);
  void instrumentMemIntrinsic(MemIntrinsic *MI);
  bool instrumentMemAccess(Instruction *I);
  Value *isInterestingMemoryAccess(Instruction *I, bool *IsWrite,
                                   uint64_t *TypeSize, unsigned *Alignment,
                                   Value **MaybeMask);

  bool isInterestingAlloca(const AllocaInst &AI);
  bool tagAlloca(IRBuilder<> &IRB, AllocaInst *AI, Value *Tag);
  Value *tagPointer(IRBuilder<> &IRB, Type *Ty, Value *PtrLong, Value *Tag);
  Value *untagPointer(IRBuilder<> &IRB, Value *PtrLong);
  bool instrumentStack(SmallVectorImpl<AllocaInst *> &Allocas,
                       SmallVectorImpl<Instruction *> &RetVec, Value *StackTag);
  Value *getNextTagWithCall(IRBuilder<> &IRB);
  Value *getStackBaseTag(IRBuilder<> &IRB);
  Value *getAllocaTag(IRBuilder<> &IRB, Value *StackTag, AllocaInst *AI,
                     unsigned AllocaNo);
  Value *getUARTag(IRBuilder<> &IRB, Value *StackTag);

  Value *getHwasanThreadSlotPtr(IRBuilder<> &IRB, Type *Ty);
  Value *emitPrologue(IRBuilder<> &IRB, bool WithFrameRecord);

private:
  LLVMContext *C;
  std::string CurModuleUniqueId;
  Triple TargetTriple;
  Function *HWAsanMemmove, *HWAsanMemcpy, *HWAsanMemset;

  // Frame description is a way to pass names/sizes of local variables
  // to the run-time w/o adding extra executable code in every function.
  // We do this by creating a separate section with {PC,Descr} pairs and passing
  // the section beg/end to __hwasan_init_frames() at module init time.
  std::string createFrameString(ArrayRef<AllocaInst*> Allocas);
  void createFrameGlobal(Function &F, const std::string &FrameString);
  // Get the section name for frame descriptions. Currently ELF-only.
  const char *getFrameSection() { return "__hwasan_frames"; }
  const char *getFrameSectionBeg() { return  "__start___hwasan_frames"; }
  const char *getFrameSectionEnd() { return  "__stop___hwasan_frames"; }
  GlobalVariable *createFrameSectionBound(Module &M, Type *Ty,
                                          const char *Name) {
    auto GV = new GlobalVariable(M, Ty, false, GlobalVariable::ExternalLinkage,
                                 nullptr, Name);
    GV->setVisibility(GlobalValue::HiddenVisibility);
    return GV;
  }

  /// This struct defines the shadow mapping using the rule:
  ///   shadow = (mem >> Scale) + Offset.
  /// If InGlobal is true, then
  ///   extern char __hwasan_shadow[];
  ///   shadow = (mem >> Scale) + &__hwasan_shadow
  /// If InTls is true, then
  ///   extern char *__hwasan_tls;
  ///   shadow = (mem>>Scale) + align_up(__hwasan_shadow, kShadowBaseAlignment)
  struct ShadowMapping {
    int Scale;
    uint64_t Offset;
    bool InGlobal;
    bool InTls;

    void init(Triple &TargetTriple);
    unsigned getAllocaAlignment() const { return 1U << Scale; }
  };
  ShadowMapping Mapping;

  Type *IntptrTy;
  Type *Int8PtrTy;
  Type *Int8Ty;

  bool CompileKernel;
  bool Recover;

  Function *HwasanCtorFunction;

  Function *HwasanMemoryAccessCallback[2][kNumberOfAccessSizes];
  Function *HwasanMemoryAccessCallbackSized[2];

  Function *HwasanTagMemoryFunc;
  Function *HwasanGenerateTagFunc;
  Function *HwasanThreadEnterFunc;

  Constant *ShadowGlobal;

  Value *LocalDynamicShadow = nullptr;
  GlobalValue *ThreadPtrGlobal = nullptr;
};

} // end anonymous namespace

char HWAddressSanitizer::ID = 0;

INITIALIZE_PASS_BEGIN(
    HWAddressSanitizer, "hwasan",
    "HWAddressSanitizer: detect memory bugs using tagged addressing.", false,
    false)
INITIALIZE_PASS_END(
    HWAddressSanitizer, "hwasan",
    "HWAddressSanitizer: detect memory bugs using tagged addressing.", false,
    false)

FunctionPass *llvm::createHWAddressSanitizerPass(bool CompileKernel,
                                                 bool Recover) {
  assert(!CompileKernel || Recover);
  return new HWAddressSanitizer(CompileKernel, Recover);
}

/// Module-level initialization.
///
/// inserts a call to __hwasan_init to the module's constructor list.
bool HWAddressSanitizer::doInitialization(Module &M) {
  LLVM_DEBUG(dbgs() << "Init " << M.getName() << "\n");
  auto &DL = M.getDataLayout();

  TargetTriple = Triple(M.getTargetTriple());

  Mapping.init(TargetTriple);

  C = &(M.getContext());
  CurModuleUniqueId = getUniqueModuleId(&M);
  IRBuilder<> IRB(*C);
  IntptrTy = IRB.getIntPtrTy(DL);
  Int8PtrTy = IRB.getInt8PtrTy();
  Int8Ty = IRB.getInt8Ty();

  HwasanCtorFunction = nullptr;
  if (!CompileKernel) {
    std::tie(HwasanCtorFunction, std::ignore) =
        createSanitizerCtorAndInitFunctions(M, kHwasanModuleCtorName,
                                            kHwasanInitName,
                                            /*InitArgTypes=*/{},
                                            /*InitArgs=*/{});
    Comdat *CtorComdat = M.getOrInsertComdat(kHwasanModuleCtorName);
    HwasanCtorFunction->setComdat(CtorComdat);
    appendToGlobalCtors(M, HwasanCtorFunction, 0, HwasanCtorFunction);

    // Create a zero-length global in __hwasan_frame so that the linker will
    // always create start and stop symbols.
    //
    // N.B. If we ever start creating associated metadata in this pass this
    // global will need to be associated with the ctor.
    Type *Int8Arr0Ty = ArrayType::get(Int8Ty, 0);
    auto GV =
        new GlobalVariable(M, Int8Arr0Ty, /*isConstantGlobal*/ true,
                           GlobalVariable::PrivateLinkage,
                           Constant::getNullValue(Int8Arr0Ty), "__hwasan");
    GV->setSection(getFrameSection());
    GV->setComdat(CtorComdat);
    appendToCompilerUsed(M, GV);

    IRBuilder<> IRBCtor(HwasanCtorFunction->getEntryBlock().getTerminator());
    IRBCtor.CreateCall(
        declareSanitizerInitFunction(M, "__hwasan_init_frames",
                                     {Int8PtrTy, Int8PtrTy}),
        {createFrameSectionBound(M, Int8Ty, getFrameSectionBeg()),
         createFrameSectionBound(M, Int8Ty, getFrameSectionEnd())});
  }

  if (!TargetTriple.isAndroid())
    appendToCompilerUsed(
        M, ThreadPtrGlobal = new GlobalVariable(
               M, IntptrTy, false, GlobalVariable::ExternalLinkage, nullptr,
               "__hwasan_tls", nullptr, GlobalVariable::InitialExecTLSModel));

  return true;
}

void HWAddressSanitizer::initializeCallbacks(Module &M) {
  IRBuilder<> IRB(*C);
  for (size_t AccessIsWrite = 0; AccessIsWrite <= 1; AccessIsWrite++) {
    const std::string TypeStr = AccessIsWrite ? "store" : "load";
    const std::string EndingStr = Recover ? "_noabort" : "";

    HwasanMemoryAccessCallbackSized[AccessIsWrite] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            ClMemoryAccessCallbackPrefix + TypeStr + "N" + EndingStr,
            FunctionType::get(IRB.getVoidTy(), {IntptrTy, IntptrTy}, false)));

    for (size_t AccessSizeIndex = 0; AccessSizeIndex < kNumberOfAccessSizes;
         AccessSizeIndex++) {
      HwasanMemoryAccessCallback[AccessIsWrite][AccessSizeIndex] =
          checkSanitizerInterfaceFunction(M.getOrInsertFunction(
              ClMemoryAccessCallbackPrefix + TypeStr +
                  itostr(1ULL << AccessSizeIndex) + EndingStr,
              FunctionType::get(IRB.getVoidTy(), {IntptrTy}, false)));
    }
  }

  HwasanTagMemoryFunc = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__hwasan_tag_memory", IRB.getVoidTy(), Int8PtrTy, Int8Ty, IntptrTy));
  HwasanGenerateTagFunc = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__hwasan_generate_tag", Int8Ty));

  if (Mapping.InGlobal)
    ShadowGlobal = M.getOrInsertGlobal("__hwasan_shadow",
                                       ArrayType::get(IRB.getInt8Ty(), 0));

  const std::string MemIntrinCallbackPrefix =
      CompileKernel ? std::string("") : ClMemoryAccessCallbackPrefix;
  HWAsanMemmove = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      MemIntrinCallbackPrefix + "memmove", IRB.getInt8PtrTy(),
      IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IntptrTy));
  HWAsanMemcpy = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      MemIntrinCallbackPrefix + "memcpy", IRB.getInt8PtrTy(),
      IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IntptrTy));
  HWAsanMemset = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      MemIntrinCallbackPrefix + "memset", IRB.getInt8PtrTy(),
      IRB.getInt8PtrTy(), IRB.getInt32Ty(), IntptrTy));

  HwasanThreadEnterFunc = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__hwasan_thread_enter", IRB.getVoidTy()));
}

Value *HWAddressSanitizer::getDynamicShadowNonTls(IRBuilder<> &IRB) {
  // Generate code only when dynamic addressing is needed.
  if (Mapping.Offset != kDynamicShadowSentinel)
    return nullptr;

  if (Mapping.InGlobal) {
    // An empty inline asm with input reg == output reg.
    // An opaque pointer-to-int cast, basically.
    InlineAsm *Asm = InlineAsm::get(
        FunctionType::get(IntptrTy, {ShadowGlobal->getType()}, false),
        StringRef(""), StringRef("=r,0"),
        /*hasSideEffects=*/false);
    return IRB.CreateCall(Asm, {ShadowGlobal}, ".hwasan.shadow");
  } else {
    Value *GlobalDynamicAddress =
        IRB.GetInsertBlock()->getParent()->getParent()->getOrInsertGlobal(
            kHwasanShadowMemoryDynamicAddress, IntptrTy);
    return IRB.CreateLoad(GlobalDynamicAddress);
  }
}

Value *HWAddressSanitizer::isInterestingMemoryAccess(Instruction *I,
                                                     bool *IsWrite,
                                                     uint64_t *TypeSize,
                                                     unsigned *Alignment,
                                                     Value **MaybeMask) {
  // Skip memory accesses inserted by another instrumentation.
  if (I->getMetadata("nosanitize")) return nullptr;

  // Do not instrument the load fetching the dynamic shadow address.
  if (LocalDynamicShadow == I)
    return nullptr;

  Value *PtrOperand = nullptr;
  const DataLayout &DL = I->getModule()->getDataLayout();
  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    if (!ClInstrumentReads) return nullptr;
    *IsWrite = false;
    *TypeSize = DL.getTypeStoreSizeInBits(LI->getType());
    *Alignment = LI->getAlignment();
    PtrOperand = LI->getPointerOperand();
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    if (!ClInstrumentWrites) return nullptr;
    *IsWrite = true;
    *TypeSize = DL.getTypeStoreSizeInBits(SI->getValueOperand()->getType());
    *Alignment = SI->getAlignment();
    PtrOperand = SI->getPointerOperand();
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    if (!ClInstrumentAtomics) return nullptr;
    *IsWrite = true;
    *TypeSize = DL.getTypeStoreSizeInBits(RMW->getValOperand()->getType());
    *Alignment = 0;
    PtrOperand = RMW->getPointerOperand();
  } else if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
    if (!ClInstrumentAtomics) return nullptr;
    *IsWrite = true;
    *TypeSize = DL.getTypeStoreSizeInBits(XCHG->getCompareOperand()->getType());
    *Alignment = 0;
    PtrOperand = XCHG->getPointerOperand();
  }

  if (PtrOperand) {
    // Do not instrument accesses from different address spaces; we cannot deal
    // with them.
    Type *PtrTy = cast<PointerType>(PtrOperand->getType()->getScalarType());
    if (PtrTy->getPointerAddressSpace() != 0)
      return nullptr;

    // Ignore swifterror addresses.
    // swifterror memory addresses are mem2reg promoted by instruction
    // selection. As such they cannot have regular uses like an instrumentation
    // function and it makes no sense to track them as memory.
    if (PtrOperand->isSwiftError())
      return nullptr;
  }

  return PtrOperand;
}

static unsigned getPointerOperandIndex(Instruction *I) {
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->getPointerOperandIndex();
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->getPointerOperandIndex();
  if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I))
    return RMW->getPointerOperandIndex();
  if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(I))
    return XCHG->getPointerOperandIndex();
  report_fatal_error("Unexpected instruction");
  return -1;
}

static size_t TypeSizeToSizeIndex(uint32_t TypeSize) {
  size_t Res = countTrailingZeros(TypeSize / 8);
  assert(Res < kNumberOfAccessSizes);
  return Res;
}

void HWAddressSanitizer::untagPointerOperand(Instruction *I, Value *Addr) {
  if (TargetTriple.isAArch64())
    return;

  IRBuilder<> IRB(I);
  Value *AddrLong = IRB.CreatePointerCast(Addr, IntptrTy);
  Value *UntaggedPtr =
      IRB.CreateIntToPtr(untagPointer(IRB, AddrLong), Addr->getType());
  I->setOperand(getPointerOperandIndex(I), UntaggedPtr);
}

Value *HWAddressSanitizer::memToShadow(Value *Mem, Type *Ty, IRBuilder<> &IRB) {
  // Mem >> Scale
  Value *Shadow = IRB.CreateLShr(Mem, Mapping.Scale);
  if (Mapping.Offset == 0)
    return Shadow;
  // (Mem >> Scale) + Offset
  Value *ShadowBase;
  if (LocalDynamicShadow)
    ShadowBase = LocalDynamicShadow;
  else
    ShadowBase = ConstantInt::get(Ty, Mapping.Offset);
  return IRB.CreateAdd(Shadow, ShadowBase);
}

void HWAddressSanitizer::instrumentMemAccessInline(Value *PtrLong, bool IsWrite,
                                                   unsigned AccessSizeIndex,
                                                   Instruction *InsertBefore) {
  IRBuilder<> IRB(InsertBefore);
  Value *PtrTag = IRB.CreateTrunc(IRB.CreateLShr(PtrLong, kPointerTagShift),
                                  IRB.getInt8Ty());
  Value *AddrLong = untagPointer(IRB, PtrLong);
  Value *ShadowLong = memToShadow(AddrLong, PtrLong->getType(), IRB);
  Value *MemTag = IRB.CreateLoad(IRB.CreateIntToPtr(ShadowLong, Int8PtrTy));
  Value *TagMismatch = IRB.CreateICmpNE(PtrTag, MemTag);

  int matchAllTag = ClMatchAllTag.getNumOccurrences() > 0 ?
      ClMatchAllTag : (CompileKernel ? 0xFF : -1);
  if (matchAllTag != -1) {
    Value *TagNotIgnored = IRB.CreateICmpNE(PtrTag,
        ConstantInt::get(PtrTag->getType(), matchAllTag));
    TagMismatch = IRB.CreateAnd(TagMismatch, TagNotIgnored);
  }

  Instruction *CheckTerm =
      SplitBlockAndInsertIfThen(TagMismatch, InsertBefore, !Recover,
                                MDBuilder(*C).createBranchWeights(1, 100000));

  IRB.SetInsertPoint(CheckTerm);
  const int64_t AccessInfo = Recover * 0x20 + IsWrite * 0x10 + AccessSizeIndex;
  InlineAsm *Asm;
  switch (TargetTriple.getArch()) {
    case Triple::x86_64:
      // The signal handler will find the data address in rdi.
      Asm = InlineAsm::get(
          FunctionType::get(IRB.getVoidTy(), {PtrLong->getType()}, false),
          "int3\nnopl " + itostr(0x40 + AccessInfo) + "(%rax)",
          "{rdi}",
          /*hasSideEffects=*/true);
      break;
    case Triple::aarch64:
    case Triple::aarch64_be:
      // The signal handler will find the data address in x0.
      Asm = InlineAsm::get(
          FunctionType::get(IRB.getVoidTy(), {PtrLong->getType()}, false),
          "brk #" + itostr(0x900 + AccessInfo),
          "{x0}",
          /*hasSideEffects=*/true);
      break;
    default:
      report_fatal_error("unsupported architecture");
  }
  IRB.CreateCall(Asm, PtrLong);
}

void HWAddressSanitizer::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);
  if (isa<MemTransferInst>(MI)) {
    IRB.CreateCall(
        isa<MemMoveInst>(MI) ? HWAsanMemmove : HWAsanMemcpy,
        {IRB.CreatePointerCast(MI->getOperand(0), IRB.getInt8PtrTy()),
         IRB.CreatePointerCast(MI->getOperand(1), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(MI->getOperand(2), IntptrTy, false)});
  } else if (isa<MemSetInst>(MI)) {
    IRB.CreateCall(
        HWAsanMemset,
        {IRB.CreatePointerCast(MI->getOperand(0), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(MI->getOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(MI->getOperand(2), IntptrTy, false)});
  }
  MI->eraseFromParent();
}

bool HWAddressSanitizer::instrumentMemAccess(Instruction *I) {
  LLVM_DEBUG(dbgs() << "Instrumenting: " << *I << "\n");
  bool IsWrite = false;
  unsigned Alignment = 0;
  uint64_t TypeSize = 0;
  Value *MaybeMask = nullptr;

  if (ClInstrumentMemIntrinsics && isa<MemIntrinsic>(I)) {
    instrumentMemIntrinsic(cast<MemIntrinsic>(I));
    return true;
  }

  Value *Addr =
      isInterestingMemoryAccess(I, &IsWrite, &TypeSize, &Alignment, &MaybeMask);

  if (!Addr)
    return false;

  if (MaybeMask)
    return false; //FIXME

  IRBuilder<> IRB(I);
  Value *AddrLong = IRB.CreatePointerCast(Addr, IntptrTy);
  if (isPowerOf2_64(TypeSize) &&
      (TypeSize / 8 <= (1UL << (kNumberOfAccessSizes - 1))) &&
      (Alignment >= (1UL << Mapping.Scale) || Alignment == 0 ||
       Alignment >= TypeSize / 8)) {
    size_t AccessSizeIndex = TypeSizeToSizeIndex(TypeSize);
    if (ClInstrumentWithCalls) {
      IRB.CreateCall(HwasanMemoryAccessCallback[IsWrite][AccessSizeIndex],
                     AddrLong);
    } else {
      instrumentMemAccessInline(AddrLong, IsWrite, AccessSizeIndex, I);
    }
  } else {
    IRB.CreateCall(HwasanMemoryAccessCallbackSized[IsWrite],
                   {AddrLong, ConstantInt::get(IntptrTy, TypeSize / 8)});
  }
  untagPointerOperand(I, Addr);

  return true;
}

static uint64_t getAllocaSizeInBytes(const AllocaInst &AI) {
  uint64_t ArraySize = 1;
  if (AI.isArrayAllocation()) {
    const ConstantInt *CI = dyn_cast<ConstantInt>(AI.getArraySize());
    assert(CI && "non-constant array size");
    ArraySize = CI->getZExtValue();
  }
  Type *Ty = AI.getAllocatedType();
  uint64_t SizeInBytes = AI.getModule()->getDataLayout().getTypeAllocSize(Ty);
  return SizeInBytes * ArraySize;
}

bool HWAddressSanitizer::tagAlloca(IRBuilder<> &IRB, AllocaInst *AI,
                                   Value *Tag) {
  size_t Size = (getAllocaSizeInBytes(*AI) + Mapping.getAllocaAlignment() - 1) &
                ~(Mapping.getAllocaAlignment() - 1);

  Value *JustTag = IRB.CreateTrunc(Tag, IRB.getInt8Ty());
  if (ClInstrumentWithCalls) {
    IRB.CreateCall(HwasanTagMemoryFunc,
                   {IRB.CreatePointerCast(AI, Int8PtrTy), JustTag,
                    ConstantInt::get(IntptrTy, Size)});
  } else {
    size_t ShadowSize = Size >> Mapping.Scale;
    Value *ShadowPtr = IRB.CreateIntToPtr(
        memToShadow(IRB.CreatePointerCast(AI, IntptrTy), AI->getType(), IRB),
        Int8PtrTy);
    // If this memset is not inlined, it will be intercepted in the hwasan
    // runtime library. That's OK, because the interceptor skips the checks if
    // the address is in the shadow region.
    // FIXME: the interceptor is not as fast as real memset. Consider lowering
    // llvm.memset right here into either a sequence of stores, or a call to
    // hwasan_tag_memory.
    IRB.CreateMemSet(ShadowPtr, JustTag, ShadowSize, /*Align=*/1);
  }
  return true;
}

static unsigned RetagMask(unsigned AllocaNo) {
  // A list of 8-bit numbers that have at most one run of non-zero bits.
  // x = x ^ (mask << 56) can be encoded as a single armv8 instruction for these
  // masks.
  // The list does not include the value 255, which is used for UAR.
  static unsigned FastMasks[] = {
      0,   1,   2,   3,   4,   6,   7,   8,   12,  14,  15, 16,  24,
      28,  30,  31,  32,  48,  56,  60,  62,  63,  64,  96, 112, 120,
      124, 126, 127, 128, 192, 224, 240, 248, 252, 254};
  return FastMasks[AllocaNo % (sizeof(FastMasks) / sizeof(FastMasks[0]))];
}

Value *HWAddressSanitizer::getNextTagWithCall(IRBuilder<> &IRB) {
  return IRB.CreateZExt(IRB.CreateCall(HwasanGenerateTagFunc), IntptrTy);
}

Value *HWAddressSanitizer::getStackBaseTag(IRBuilder<> &IRB) {
  if (ClGenerateTagsWithCalls)
    return getNextTagWithCall(IRB);
  // FIXME: use addressofreturnaddress (but implement it in aarch64 backend
  // first).
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  auto GetStackPointerFn =
      Intrinsic::getDeclaration(M, Intrinsic::frameaddress);
  Value *StackPointer = IRB.CreateCall(
      GetStackPointerFn, {Constant::getNullValue(IRB.getInt32Ty())});

  // Extract some entropy from the stack pointer for the tags.
  // Take bits 20..28 (ASLR entropy) and xor with bits 0..8 (these differ
  // between functions).
  Value *StackPointerLong = IRB.CreatePointerCast(StackPointer, IntptrTy);
  Value *StackTag =
      IRB.CreateXor(StackPointerLong, IRB.CreateLShr(StackPointerLong, 20),
                    "hwasan.stack.base.tag");
  return StackTag;
}

Value *HWAddressSanitizer::getAllocaTag(IRBuilder<> &IRB, Value *StackTag,
                                        AllocaInst *AI, unsigned AllocaNo) {
  if (ClGenerateTagsWithCalls)
    return getNextTagWithCall(IRB);
  return IRB.CreateXor(StackTag,
                       ConstantInt::get(IntptrTy, RetagMask(AllocaNo)));
}

Value *HWAddressSanitizer::getUARTag(IRBuilder<> &IRB, Value *StackTag) {
  if (ClUARRetagToZero)
    return ConstantInt::get(IntptrTy, 0);
  if (ClGenerateTagsWithCalls)
    return getNextTagWithCall(IRB);
  return IRB.CreateXor(StackTag, ConstantInt::get(IntptrTy, 0xFFU));
}

// Add a tag to an address.
Value *HWAddressSanitizer::tagPointer(IRBuilder<> &IRB, Type *Ty,
                                      Value *PtrLong, Value *Tag) {
  Value *TaggedPtrLong;
  if (CompileKernel) {
    // Kernel addresses have 0xFF in the most significant byte.
    Value *ShiftedTag = IRB.CreateOr(
        IRB.CreateShl(Tag, kPointerTagShift),
        ConstantInt::get(IntptrTy, (1ULL << kPointerTagShift) - 1));
    TaggedPtrLong = IRB.CreateAnd(PtrLong, ShiftedTag);
  } else {
    // Userspace can simply do OR (tag << 56);
    Value *ShiftedTag = IRB.CreateShl(Tag, kPointerTagShift);
    TaggedPtrLong = IRB.CreateOr(PtrLong, ShiftedTag);
  }
  return IRB.CreateIntToPtr(TaggedPtrLong, Ty);
}

// Remove tag from an address.
Value *HWAddressSanitizer::untagPointer(IRBuilder<> &IRB, Value *PtrLong) {
  Value *UntaggedPtrLong;
  if (CompileKernel) {
    // Kernel addresses have 0xFF in the most significant byte.
    UntaggedPtrLong = IRB.CreateOr(PtrLong,
        ConstantInt::get(PtrLong->getType(), 0xFFULL << kPointerTagShift));
  } else {
    // Userspace addresses have 0x00.
    UntaggedPtrLong = IRB.CreateAnd(PtrLong,
        ConstantInt::get(PtrLong->getType(), ~(0xFFULL << kPointerTagShift)));
  }
  return UntaggedPtrLong;
}

Value *HWAddressSanitizer::getHwasanThreadSlotPtr(IRBuilder<> &IRB, Type *Ty) {
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  if (TargetTriple.isAArch64() && TargetTriple.isAndroid()) {
    // Android provides a fixed TLS slot for sanitizers. See TLS_SLOT_SANITIZER
    // in Bionic's libc/private/bionic_tls.h.
    Function *ThreadPointerFunc =
        Intrinsic::getDeclaration(M, Intrinsic::thread_pointer);
    Value *SlotPtr = IRB.CreatePointerCast(
        IRB.CreateConstGEP1_32(IRB.CreateCall(ThreadPointerFunc), 0x30),
        Ty->getPointerTo(0));
    return SlotPtr;
  }
  if (ThreadPtrGlobal)
    return ThreadPtrGlobal;


  return nullptr;
}

// Creates a string with a description of the stack frame (set of Allocas).
// The string is intended to be human readable.
// The current form is: Size1 Name1; Size2 Name2; ...
std::string
HWAddressSanitizer::createFrameString(ArrayRef<AllocaInst *> Allocas) {
  std::ostringstream Descr;
  for (auto AI : Allocas)
    Descr << getAllocaSizeInBytes(*AI) << " " <<  AI->getName().str() << "; ";
  return Descr.str();
}

// Creates a global in the frame section which consists of two pointers:
// the function PC and the frame string constant.
void HWAddressSanitizer::createFrameGlobal(Function &F,
                                           const std::string &FrameString) {
  Module &M = *F.getParent();
  auto DescrGV = createPrivateGlobalForString(M, FrameString, true);
  auto PtrPairTy = StructType::get(F.getType(), DescrGV->getType());
  auto GV = new GlobalVariable(
      M, PtrPairTy, /*isConstantGlobal*/ true, GlobalVariable::PrivateLinkage,
      ConstantStruct::get(PtrPairTy, (Constant *)&F, (Constant *)DescrGV),
      "__hwasan");
  GV->setSection(getFrameSection());
  appendToCompilerUsed(M, GV);
  // Put GV into the F's Comadat so that if F is deleted GV can be deleted too.
  if (auto Comdat =
          GetOrCreateFunctionComdat(F, TargetTriple, CurModuleUniqueId))
    GV->setComdat(Comdat);
}

Value *HWAddressSanitizer::emitPrologue(IRBuilder<> &IRB,
                                        bool WithFrameRecord) {
  if (!Mapping.InTls)
    return getDynamicShadowNonTls(IRB);

  Value *SlotPtr = getHwasanThreadSlotPtr(IRB, IntptrTy);
  assert(SlotPtr);

  Instruction *ThreadLong = IRB.CreateLoad(SlotPtr);

  Function *F = IRB.GetInsertBlock()->getParent();
  if (F->getFnAttribute("hwasan-abi").getValueAsString() == "interceptor") {
    Value *ThreadLongEqZero =
        IRB.CreateICmpEQ(ThreadLong, ConstantInt::get(IntptrTy, 0));
    auto *Br = cast<BranchInst>(SplitBlockAndInsertIfThen(
        ThreadLongEqZero, cast<Instruction>(ThreadLongEqZero)->getNextNode(),
        false, MDBuilder(*C).createBranchWeights(1, 100000)));

    IRB.SetInsertPoint(Br);
    // FIXME: This should call a new runtime function with a custom calling
    // convention to avoid needing to spill all arguments here.
    IRB.CreateCall(HwasanThreadEnterFunc);
    LoadInst *ReloadThreadLong = IRB.CreateLoad(SlotPtr);

    IRB.SetInsertPoint(&*Br->getSuccessor(0)->begin());
    PHINode *ThreadLongPhi = IRB.CreatePHI(IntptrTy, 2);
    ThreadLongPhi->addIncoming(ThreadLong, ThreadLong->getParent());
    ThreadLongPhi->addIncoming(ReloadThreadLong, ReloadThreadLong->getParent());
    ThreadLong = ThreadLongPhi;
  }

  // Extract the address field from ThreadLong. Unnecessary on AArch64 with TBI.
  Value *ThreadLongMaybeUntagged =
      TargetTriple.isAArch64() ? ThreadLong : untagPointer(IRB, ThreadLong);

  if (WithFrameRecord) {
    // Prepare ring buffer data.
    auto PC = IRB.CreatePtrToInt(F, IntptrTy);
    auto GetStackPointerFn =
        Intrinsic::getDeclaration(F->getParent(), Intrinsic::frameaddress);
    Value *SP = IRB.CreatePtrToInt(
        IRB.CreateCall(GetStackPointerFn,
                       {Constant::getNullValue(IRB.getInt32Ty())}),
        IntptrTy);
    // Mix SP and PC. TODO: also add the tag to the mix.
    // Assumptions:
    // PC is 0x0000PPPPPPPPPPPP  (48 bits are meaningful, others are zero)
    // SP is 0xsssssssssssSSSS0  (4 lower bits are zero)
    // We only really need ~20 lower non-zero bits (SSSS), so we mix like this:
    //       0xSSSSPPPPPPPPPPPP
    SP = IRB.CreateShl(SP, 44);

    // Store data to ring buffer.
    Value *RecordPtr =
        IRB.CreateIntToPtr(ThreadLongMaybeUntagged, IntptrTy->getPointerTo(0));
    IRB.CreateStore(IRB.CreateOr(PC, SP), RecordPtr);

    // Update the ring buffer. Top byte of ThreadLong defines the size of the
    // buffer in pages, it must be a power of two, and the start of the buffer
    // must be aligned by twice that much. Therefore wrap around of the ring
    // buffer is simply Addr &= ~((ThreadLong >> 56) << 12).
    // The use of AShr instead of LShr is due to
    //   https://bugs.llvm.org/show_bug.cgi?id=39030
    // Runtime library makes sure not to use the highest bit.
    Value *WrapMask = IRB.CreateXor(
        IRB.CreateShl(IRB.CreateAShr(ThreadLong, 56), 12, "", true, true),
        ConstantInt::get(IntptrTy, (uint64_t)-1));
    Value *ThreadLongNew = IRB.CreateAnd(
        IRB.CreateAdd(ThreadLong, ConstantInt::get(IntptrTy, 8)), WrapMask);
    IRB.CreateStore(ThreadLongNew, SlotPtr);
  }

  // Get shadow base address by aligning RecordPtr up.
  // Note: this is not correct if the pointer is already aligned.
  // Runtime library will make sure this never happens.
  Value *ShadowBase = IRB.CreateAdd(
      IRB.CreateOr(
          ThreadLongMaybeUntagged,
          ConstantInt::get(IntptrTy, (1ULL << kShadowBaseAlignment) - 1)),
      ConstantInt::get(IntptrTy, 1), "hwasan.shadow");
  return ShadowBase;
}

bool HWAddressSanitizer::instrumentStack(
    SmallVectorImpl<AllocaInst *> &Allocas,
    SmallVectorImpl<Instruction *> &RetVec, Value *StackTag) {
  // Ideally, we want to calculate tagged stack base pointer, and rewrite all
  // alloca addresses using that. Unfortunately, offsets are not known yet
  // (unless we use ASan-style mega-alloca). Instead we keep the base tag in a
  // temp, shift-OR it into each alloca address and xor with the retag mask.
  // This generates one extra instruction per alloca use.
  for (unsigned N = 0; N < Allocas.size(); ++N) {
    auto *AI = Allocas[N];
    IRBuilder<> IRB(AI->getNextNode());

    // Replace uses of the alloca with tagged address.
    Value *Tag = getAllocaTag(IRB, StackTag, AI, N);
    Value *AILong = IRB.CreatePointerCast(AI, IntptrTy);
    Value *Replacement = tagPointer(IRB, AI->getType(), AILong, Tag);
    std::string Name =
        AI->hasName() ? AI->getName().str() : "alloca." + itostr(N);
    Replacement->setName(Name + ".hwasan");

    for (auto UI = AI->use_begin(), UE = AI->use_end(); UI != UE;) {
      Use &U = *UI++;
      if (U.getUser() != AILong)
        U.set(Replacement);
    }

    tagAlloca(IRB, AI, Tag);

    for (auto RI : RetVec) {
      IRB.SetInsertPoint(RI);

      // Re-tag alloca memory with the special UAR tag.
      Value *Tag = getUARTag(IRB, StackTag);
      tagAlloca(IRB, AI, Tag);
    }
  }

  return true;
}

bool HWAddressSanitizer::isInterestingAlloca(const AllocaInst &AI) {
  return (AI.getAllocatedType()->isSized() &&
          // FIXME: instrument dynamic allocas, too
          AI.isStaticAlloca() &&
          // alloca() may be called with 0 size, ignore it.
          getAllocaSizeInBytes(AI) > 0 &&
          // We are only interested in allocas not promotable to registers.
          // Promotable allocas are common under -O0.
          !isAllocaPromotable(&AI) &&
          // inalloca allocas are not treated as static, and we don't want
          // dynamic alloca instrumentation for them as well.
          !AI.isUsedWithInAlloca() &&
          // swifterror allocas are register promoted by ISel
          !AI.isSwiftError());
}

bool HWAddressSanitizer::runOnFunction(Function &F) {
  if (&F == HwasanCtorFunction)
    return false;

  if (!F.hasFnAttribute(Attribute::SanitizeHWAddress))
    return false;

  LLVM_DEBUG(dbgs() << "Function: " << F.getName() << "\n");

  SmallVector<Instruction*, 16> ToInstrument;
  SmallVector<AllocaInst*, 8> AllocasToInstrument;
  SmallVector<Instruction*, 8> RetVec;
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (ClInstrumentStack)
        if (AllocaInst *AI = dyn_cast<AllocaInst>(&Inst)) {
          // Realign all allocas. We don't want small uninteresting allocas to
          // hide in instrumented alloca's padding.
          if (AI->getAlignment() < Mapping.getAllocaAlignment())
            AI->setAlignment(Mapping.getAllocaAlignment());
          // Instrument some of them.
          if (isInterestingAlloca(*AI))
            AllocasToInstrument.push_back(AI);
          continue;
        }

      if (isa<ReturnInst>(Inst) || isa<ResumeInst>(Inst) ||
          isa<CleanupReturnInst>(Inst))
        RetVec.push_back(&Inst);

      Value *MaybeMask = nullptr;
      bool IsWrite;
      unsigned Alignment;
      uint64_t TypeSize;
      Value *Addr = isInterestingMemoryAccess(&Inst, &IsWrite, &TypeSize,
                                              &Alignment, &MaybeMask);
      if (Addr || isa<MemIntrinsic>(Inst))
        ToInstrument.push_back(&Inst);
    }
  }

  if (AllocasToInstrument.empty() && ToInstrument.empty())
    return false;

  if (ClCreateFrameDescriptions && !AllocasToInstrument.empty())
    createFrameGlobal(F, createFrameString(AllocasToInstrument));

  initializeCallbacks(*F.getParent());

  assert(!LocalDynamicShadow);

  Instruction *InsertPt = &*F.getEntryBlock().begin();
  IRBuilder<> EntryIRB(InsertPt);
  LocalDynamicShadow = emitPrologue(EntryIRB,
                                    /*WithFrameRecord*/ ClRecordStackHistory &&
                                        !AllocasToInstrument.empty());

  bool Changed = false;
  if (!AllocasToInstrument.empty()) {
    Value *StackTag =
        ClGenerateTagsWithCalls ? nullptr : getStackBaseTag(EntryIRB);
    Changed |= instrumentStack(AllocasToInstrument, RetVec, StackTag);
  }

  for (auto Inst : ToInstrument)
    Changed |= instrumentMemAccess(Inst);

  LocalDynamicShadow = nullptr;

  return Changed;
}

void HWAddressSanitizer::ShadowMapping::init(Triple &TargetTriple) {
  Scale = kDefaultShadowScale;
  if (ClMappingOffset.getNumOccurrences() > 0) {
    InGlobal = false;
    InTls = false;
    Offset = ClMappingOffset;
  } else if (ClEnableKhwasan || ClInstrumentWithCalls) {
    InGlobal = false;
    InTls = false;
    Offset = 0;
  } else if (ClWithIfunc) {
    InGlobal = true;
    InTls = false;
    Offset = kDynamicShadowSentinel;
  } else if (ClWithTls) {
    InGlobal = false;
    InTls = true;
    Offset = kDynamicShadowSentinel;
  } else {
    InGlobal = false;
    InTls = false;
    Offset = kDynamicShadowSentinel;
  }
}
