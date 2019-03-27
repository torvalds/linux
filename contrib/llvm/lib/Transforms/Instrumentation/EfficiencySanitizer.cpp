//===-- EfficiencySanitizer.cpp - performance tuner -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners
// that detects multiple performance issues via separate sub-tools.
//
// The instrumentation phase is straightforward:
//   - Take action on every memory access: either inlined instrumentation,
//     or Inserted calls to our run-time library.
//   - Optimizations may apply to avoid instrumenting some of the accesses.
//   - Turn mem{set,cpy,move} instrinsics into library calls.
// The rest is handled by the run-time library.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "esan"

// The tool type must be just one of these ClTool* options, as the tools
// cannot be combined due to shadow memory constraints.
static cl::opt<bool>
    ClToolCacheFrag("esan-cache-frag", cl::init(false),
                    cl::desc("Detect data cache fragmentation"), cl::Hidden);
static cl::opt<bool>
    ClToolWorkingSet("esan-working-set", cl::init(false),
                    cl::desc("Measure the working set size"), cl::Hidden);
// Each new tool will get its own opt flag here.
// These are converted to EfficiencySanitizerOptions for use
// in the code.

static cl::opt<bool> ClInstrumentLoadsAndStores(
    "esan-instrument-loads-and-stores", cl::init(true),
    cl::desc("Instrument loads and stores"), cl::Hidden);
static cl::opt<bool> ClInstrumentMemIntrinsics(
    "esan-instrument-memintrinsics", cl::init(true),
    cl::desc("Instrument memintrinsics (memset/memcpy/memmove)"), cl::Hidden);
static cl::opt<bool> ClInstrumentFastpath(
    "esan-instrument-fastpath", cl::init(true),
    cl::desc("Instrument fastpath"), cl::Hidden);
static cl::opt<bool> ClAuxFieldInfo(
    "esan-aux-field-info", cl::init(true),
    cl::desc("Generate binary with auxiliary struct field information"),
    cl::Hidden);

// Experiments show that the performance difference can be 2x or more,
// and accuracy loss is typically negligible, so we turn this on by default.
static cl::opt<bool> ClAssumeIntraCacheLine(
    "esan-assume-intra-cache-line", cl::init(true),
    cl::desc("Assume each memory access touches just one cache line, for "
             "better performance but with a potential loss of accuracy."),
    cl::Hidden);

STATISTIC(NumInstrumentedLoads, "Number of instrumented loads");
STATISTIC(NumInstrumentedStores, "Number of instrumented stores");
STATISTIC(NumFastpaths, "Number of instrumented fastpaths");
STATISTIC(NumAccessesWithIrregularSize,
          "Number of accesses with a size outside our targeted callout sizes");
STATISTIC(NumIgnoredStructs, "Number of ignored structs");
STATISTIC(NumIgnoredGEPs, "Number of ignored GEP instructions");
STATISTIC(NumInstrumentedGEPs, "Number of instrumented GEP instructions");
STATISTIC(NumAssumedIntraCacheLine,
          "Number of accesses assumed to be intra-cache-line");

static const uint64_t EsanCtorAndDtorPriority = 0;
static const char *const EsanModuleCtorName = "esan.module_ctor";
static const char *const EsanModuleDtorName = "esan.module_dtor";
static const char *const EsanInitName = "__esan_init";
static const char *const EsanExitName = "__esan_exit";

// We need to specify the tool to the runtime earlier than
// the ctor is called in some cases, so we set a global variable.
static const char *const EsanWhichToolName = "__esan_which_tool";

// We must keep these Shadow* constants consistent with the esan runtime.
// FIXME: Try to place these shadow constants, the names of the __esan_*
// interface functions, and the ToolType enum into a header shared between
// llvm and compiler-rt.
struct ShadowMemoryParams {
  uint64_t ShadowMask;
  uint64_t ShadowOffs[3];
};

static const ShadowMemoryParams ShadowParams47 = {
    0x00000fffffffffffull,
    {
        0x0000130000000000ull, 0x0000220000000000ull, 0x0000440000000000ull,
    }};

static const ShadowMemoryParams ShadowParams40 = {
    0x0fffffffffull,
    {
        0x1300000000ull, 0x2200000000ull, 0x4400000000ull,
    }};

// This array is indexed by the ToolType enum.
static const int ShadowScale[] = {
  0, // ESAN_None.
  2, // ESAN_CacheFrag: 4B:1B, so 4 to 1 == >>2.
  6, // ESAN_WorkingSet: 64B:1B, so 64 to 1 == >>6.
};

// MaxStructCounterNameSize is a soft size limit to avoid insanely long
// names for those extremely large structs.
static const unsigned MaxStructCounterNameSize = 512;

namespace {

static EfficiencySanitizerOptions
OverrideOptionsFromCL(EfficiencySanitizerOptions Options) {
  if (ClToolCacheFrag)
    Options.ToolType = EfficiencySanitizerOptions::ESAN_CacheFrag;
  else if (ClToolWorkingSet)
    Options.ToolType = EfficiencySanitizerOptions::ESAN_WorkingSet;

  // Direct opt invocation with no params will have the default ESAN_None.
  // We run the default tool in that case.
  if (Options.ToolType == EfficiencySanitizerOptions::ESAN_None)
    Options.ToolType = EfficiencySanitizerOptions::ESAN_CacheFrag;

  return Options;
}

/// EfficiencySanitizer: instrument each module to find performance issues.
class EfficiencySanitizer : public ModulePass {
public:
  EfficiencySanitizer(
      const EfficiencySanitizerOptions &Opts = EfficiencySanitizerOptions())
      : ModulePass(ID), Options(OverrideOptionsFromCL(Opts)) {}
  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
  static char ID;

private:
  bool initOnModule(Module &M);
  void initializeCallbacks(Module &M);
  bool shouldIgnoreStructType(StructType *StructTy);
  void createStructCounterName(
      StructType *StructTy, SmallString<MaxStructCounterNameSize> &NameStr);
  void createCacheFragAuxGV(
    Module &M, const DataLayout &DL, StructType *StructTy,
    GlobalVariable *&TypeNames, GlobalVariable *&Offsets, GlobalVariable *&Size);
  GlobalVariable *createCacheFragInfoGV(Module &M, const DataLayout &DL,
                                        Constant *UnitName);
  Constant *createEsanInitToolInfoArg(Module &M, const DataLayout &DL);
  void createDestructor(Module &M, Constant *ToolInfoArg);
  bool runOnFunction(Function &F, Module &M);
  bool instrumentLoadOrStore(Instruction *I, const DataLayout &DL);
  bool instrumentMemIntrinsic(MemIntrinsic *MI);
  bool instrumentGetElementPtr(Instruction *I, Module &M);
  bool insertCounterUpdate(Instruction *I, StructType *StructTy,
                           unsigned CounterIdx);
  unsigned getFieldCounterIdx(StructType *StructTy) {
    return 0;
  }
  unsigned getArrayCounterIdx(StructType *StructTy) {
    return StructTy->getNumElements();
  }
  unsigned getStructCounterSize(StructType *StructTy) {
    // The struct counter array includes:
    // - one counter for each struct field,
    // - one counter for the struct access within an array.
    return (StructTy->getNumElements()/*field*/ + 1/*array*/);
  }
  bool shouldIgnoreMemoryAccess(Instruction *I);
  int getMemoryAccessFuncIndex(Value *Addr, const DataLayout &DL);
  Value *appToShadow(Value *Shadow, IRBuilder<> &IRB);
  bool instrumentFastpath(Instruction *I, const DataLayout &DL, bool IsStore,
                          Value *Addr, unsigned Alignment);
  // Each tool has its own fastpath routine:
  bool instrumentFastpathCacheFrag(Instruction *I, const DataLayout &DL,
                                   Value *Addr, unsigned Alignment);
  bool instrumentFastpathWorkingSet(Instruction *I, const DataLayout &DL,
                                    Value *Addr, unsigned Alignment);

  EfficiencySanitizerOptions Options;
  LLVMContext *Ctx;
  Type *IntptrTy;
  // Our slowpath involves callouts to the runtime library.
  // Access sizes are powers of two: 1, 2, 4, 8, 16.
  static const size_t NumberOfAccessSizes = 5;
  Function *EsanAlignedLoad[NumberOfAccessSizes];
  Function *EsanAlignedStore[NumberOfAccessSizes];
  Function *EsanUnalignedLoad[NumberOfAccessSizes];
  Function *EsanUnalignedStore[NumberOfAccessSizes];
  // For irregular sizes of any alignment:
  Function *EsanUnalignedLoadN, *EsanUnalignedStoreN;
  Function *MemmoveFn, *MemcpyFn, *MemsetFn;
  Function *EsanCtorFunction;
  Function *EsanDtorFunction;
  // Remember the counter variable for each struct type to avoid
  // recomputing the variable name later during instrumentation.
  std::map<Type *, GlobalVariable *> StructTyMap;
  ShadowMemoryParams ShadowParams;
};
} // namespace

char EfficiencySanitizer::ID = 0;
INITIALIZE_PASS_BEGIN(
    EfficiencySanitizer, "esan",
    "EfficiencySanitizer: finds performance issues.", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(
    EfficiencySanitizer, "esan",
    "EfficiencySanitizer: finds performance issues.", false, false)

StringRef EfficiencySanitizer::getPassName() const {
  return "EfficiencySanitizer";
}

void EfficiencySanitizer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

ModulePass *
llvm::createEfficiencySanitizerPass(const EfficiencySanitizerOptions &Options) {
  return new EfficiencySanitizer(Options);
}

void EfficiencySanitizer::initializeCallbacks(Module &M) {
  IRBuilder<> IRB(M.getContext());
  // Initialize the callbacks.
  for (size_t Idx = 0; Idx < NumberOfAccessSizes; ++Idx) {
    const unsigned ByteSize = 1U << Idx;
    std::string ByteSizeStr = utostr(ByteSize);
    // We'll inline the most common (i.e., aligned and frequent sizes)
    // load + store instrumentation: these callouts are for the slowpath.
    SmallString<32> AlignedLoadName("__esan_aligned_load" + ByteSizeStr);
    EsanAlignedLoad[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            AlignedLoadName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
    SmallString<32> AlignedStoreName("__esan_aligned_store" + ByteSizeStr);
    EsanAlignedStore[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            AlignedStoreName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
    SmallString<32> UnalignedLoadName("__esan_unaligned_load" + ByteSizeStr);
    EsanUnalignedLoad[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            UnalignedLoadName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
    SmallString<32> UnalignedStoreName("__esan_unaligned_store" + ByteSizeStr);
    EsanUnalignedStore[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            UnalignedStoreName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
  }
  EsanUnalignedLoadN = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__esan_unaligned_loadN", IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  EsanUnalignedStoreN = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__esan_unaligned_storeN", IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemmoveFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memmove", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemcpyFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memcpy", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemsetFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memset", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt32Ty(), IntptrTy));
}

bool EfficiencySanitizer::shouldIgnoreStructType(StructType *StructTy) {
  if (StructTy == nullptr || StructTy->isOpaque() /* no struct body */)
    return true;
  return false;
}

void EfficiencySanitizer::createStructCounterName(
    StructType *StructTy, SmallString<MaxStructCounterNameSize> &NameStr) {
  // Append NumFields and field type ids to avoid struct conflicts
  // with the same name but different fields.
  if (StructTy->hasName())
    NameStr += StructTy->getName();
  else
    NameStr += "struct.anon";
  // We allow the actual size of the StructCounterName to be larger than
  // MaxStructCounterNameSize and append $NumFields and at least one
  // field type id.
  // Append $NumFields.
  NameStr += "$";
  Twine(StructTy->getNumElements()).toVector(NameStr);
  // Append struct field type ids in the reverse order.
  for (int i = StructTy->getNumElements() - 1; i >= 0; --i) {
    NameStr += "$";
    Twine(StructTy->getElementType(i)->getTypeID()).toVector(NameStr);
    if (NameStr.size() >= MaxStructCounterNameSize)
      break;
  }
  if (StructTy->isLiteral()) {
    // End with $ for literal struct.
    NameStr += "$";
  }
}

// Create global variables with auxiliary information (e.g., struct field size,
// offset, and type name) for better user report.
void EfficiencySanitizer::createCacheFragAuxGV(
    Module &M, const DataLayout &DL, StructType *StructTy,
    GlobalVariable *&TypeName, GlobalVariable *&Offset,
    GlobalVariable *&Size) {
  auto *Int8PtrTy = Type::getInt8PtrTy(*Ctx);
  auto *Int32Ty = Type::getInt32Ty(*Ctx);
  // FieldTypeName.
  auto *TypeNameArrayTy = ArrayType::get(Int8PtrTy, StructTy->getNumElements());
  TypeName = new GlobalVariable(M, TypeNameArrayTy, true,
                                 GlobalVariable::InternalLinkage, nullptr);
  SmallVector<Constant *, 16> TypeNameVec;
  // FieldOffset.
  auto *OffsetArrayTy = ArrayType::get(Int32Ty, StructTy->getNumElements());
  Offset = new GlobalVariable(M, OffsetArrayTy, true,
                              GlobalVariable::InternalLinkage, nullptr);
  SmallVector<Constant *, 16> OffsetVec;
  // FieldSize
  auto *SizeArrayTy = ArrayType::get(Int32Ty, StructTy->getNumElements());
  Size = new GlobalVariable(M, SizeArrayTy, true,
                            GlobalVariable::InternalLinkage, nullptr);
  SmallVector<Constant *, 16> SizeVec;
  for (unsigned i = 0; i < StructTy->getNumElements(); ++i) {
    Type *Ty = StructTy->getElementType(i);
    std::string Str;
    raw_string_ostream StrOS(Str);
    Ty->print(StrOS);
    TypeNameVec.push_back(
        ConstantExpr::getPointerCast(
            createPrivateGlobalForString(M, StrOS.str(), true),
            Int8PtrTy));
    OffsetVec.push_back(
        ConstantInt::get(Int32Ty,
                         DL.getStructLayout(StructTy)->getElementOffset(i)));
    SizeVec.push_back(ConstantInt::get(Int32Ty,
                                       DL.getTypeAllocSize(Ty)));
    }
  TypeName->setInitializer(ConstantArray::get(TypeNameArrayTy, TypeNameVec));
  Offset->setInitializer(ConstantArray::get(OffsetArrayTy, OffsetVec));
  Size->setInitializer(ConstantArray::get(SizeArrayTy, SizeVec));
}

// Create the global variable for the cache-fragmentation tool.
GlobalVariable *EfficiencySanitizer::createCacheFragInfoGV(
    Module &M, const DataLayout &DL, Constant *UnitName) {
  assert(Options.ToolType == EfficiencySanitizerOptions::ESAN_CacheFrag);

  auto *Int8PtrTy = Type::getInt8PtrTy(*Ctx);
  auto *Int8PtrPtrTy = Int8PtrTy->getPointerTo();
  auto *Int32Ty = Type::getInt32Ty(*Ctx);
  auto *Int32PtrTy = Type::getInt32PtrTy(*Ctx);
  auto *Int64Ty = Type::getInt64Ty(*Ctx);
  auto *Int64PtrTy = Type::getInt64PtrTy(*Ctx);
  // This structure should be kept consistent with the StructInfo struct
  // in the runtime library.
  // struct StructInfo {
  //   const char *StructName;
  //   u32 Size;
  //   u32 NumFields;
  //   u32 *FieldOffset;           // auxiliary struct field info.
  //   u32 *FieldSize;             // auxiliary struct field info.
  //   const char **FieldTypeName; // auxiliary struct field info.
  //   u64 *FieldCounters;
  //   u64 *ArrayCounter;
  // };
  auto *StructInfoTy =
      StructType::get(Int8PtrTy, Int32Ty, Int32Ty, Int32PtrTy, Int32PtrTy,
                      Int8PtrPtrTy, Int64PtrTy, Int64PtrTy);
  auto *StructInfoPtrTy = StructInfoTy->getPointerTo();
  // This structure should be kept consistent with the CacheFragInfo struct
  // in the runtime library.
  // struct CacheFragInfo {
  //   const char *UnitName;
  //   u32 NumStructs;
  //   StructInfo *Structs;
  // };
  auto *CacheFragInfoTy = StructType::get(Int8PtrTy, Int32Ty, StructInfoPtrTy);

  std::vector<StructType *> Vec = M.getIdentifiedStructTypes();
  unsigned NumStructs = 0;
  SmallVector<Constant *, 16> Initializers;

  for (auto &StructTy : Vec) {
    if (shouldIgnoreStructType(StructTy)) {
      ++NumIgnoredStructs;
      continue;
    }
    ++NumStructs;

    // StructName.
    SmallString<MaxStructCounterNameSize> CounterNameStr;
    createStructCounterName(StructTy, CounterNameStr);
    GlobalVariable *StructCounterName = createPrivateGlobalForString(
        M, CounterNameStr, /*AllowMerging*/true);

    // Counters.
    // We create the counter array with StructCounterName and weak linkage
    // so that the structs with the same name and layout from different
    // compilation units will be merged into one.
    auto *CounterArrayTy = ArrayType::get(Int64Ty,
                                          getStructCounterSize(StructTy));
    GlobalVariable *Counters =
      new GlobalVariable(M, CounterArrayTy, false,
                         GlobalVariable::WeakAnyLinkage,
                         ConstantAggregateZero::get(CounterArrayTy),
                         CounterNameStr);

    // Remember the counter variable for each struct type.
    StructTyMap.insert(std::pair<Type *, GlobalVariable *>(StructTy, Counters));

    // We pass the field type name array, offset array, and size array to
    // the runtime for better reporting.
    GlobalVariable *TypeName = nullptr, *Offset = nullptr, *Size = nullptr;
    if (ClAuxFieldInfo)
      createCacheFragAuxGV(M, DL, StructTy, TypeName, Offset, Size);

    Constant *FieldCounterIdx[2];
    FieldCounterIdx[0] = ConstantInt::get(Int32Ty, 0);
    FieldCounterIdx[1] = ConstantInt::get(Int32Ty,
                                          getFieldCounterIdx(StructTy));
    Constant *ArrayCounterIdx[2];
    ArrayCounterIdx[0] = ConstantInt::get(Int32Ty, 0);
    ArrayCounterIdx[1] = ConstantInt::get(Int32Ty,
                                          getArrayCounterIdx(StructTy));
    Initializers.push_back(ConstantStruct::get(
        StructInfoTy,
        ConstantExpr::getPointerCast(StructCounterName, Int8PtrTy),
        ConstantInt::get(Int32Ty,
                         DL.getStructLayout(StructTy)->getSizeInBytes()),
        ConstantInt::get(Int32Ty, StructTy->getNumElements()),
        Offset == nullptr ? ConstantPointerNull::get(Int32PtrTy)
                          : ConstantExpr::getPointerCast(Offset, Int32PtrTy),
        Size == nullptr ? ConstantPointerNull::get(Int32PtrTy)
                        : ConstantExpr::getPointerCast(Size, Int32PtrTy),
        TypeName == nullptr
            ? ConstantPointerNull::get(Int8PtrPtrTy)
            : ConstantExpr::getPointerCast(TypeName, Int8PtrPtrTy),
        ConstantExpr::getGetElementPtr(CounterArrayTy, Counters,
                                       FieldCounterIdx),
        ConstantExpr::getGetElementPtr(CounterArrayTy, Counters,
                                       ArrayCounterIdx)));
  }
  // Structs.
  Constant *StructInfo;
  if (NumStructs == 0) {
    StructInfo = ConstantPointerNull::get(StructInfoPtrTy);
  } else {
    auto *StructInfoArrayTy = ArrayType::get(StructInfoTy, NumStructs);
    StructInfo = ConstantExpr::getPointerCast(
        new GlobalVariable(M, StructInfoArrayTy, false,
                           GlobalVariable::InternalLinkage,
                           ConstantArray::get(StructInfoArrayTy, Initializers)),
        StructInfoPtrTy);
  }

  auto *CacheFragInfoGV = new GlobalVariable(
      M, CacheFragInfoTy, true, GlobalVariable::InternalLinkage,
      ConstantStruct::get(CacheFragInfoTy, UnitName,
                          ConstantInt::get(Int32Ty, NumStructs), StructInfo));
  return CacheFragInfoGV;
}

// Create the tool-specific argument passed to EsanInit and EsanExit.
Constant *EfficiencySanitizer::createEsanInitToolInfoArg(Module &M,
                                                         const DataLayout &DL) {
  // This structure contains tool-specific information about each compilation
  // unit (module) and is passed to the runtime library.
  GlobalVariable *ToolInfoGV = nullptr;

  auto *Int8PtrTy = Type::getInt8PtrTy(*Ctx);
  // Compilation unit name.
  auto *UnitName = ConstantExpr::getPointerCast(
      createPrivateGlobalForString(M, M.getModuleIdentifier(), true),
      Int8PtrTy);

  // Create the tool-specific variable.
  if (Options.ToolType == EfficiencySanitizerOptions::ESAN_CacheFrag)
    ToolInfoGV = createCacheFragInfoGV(M, DL, UnitName);

  if (ToolInfoGV != nullptr)
    return ConstantExpr::getPointerCast(ToolInfoGV, Int8PtrTy);

  // Create the null pointer if no tool-specific variable created.
  return ConstantPointerNull::get(Int8PtrTy);
}

void EfficiencySanitizer::createDestructor(Module &M, Constant *ToolInfoArg) {
  PointerType *Int8PtrTy = Type::getInt8PtrTy(*Ctx);
  EsanDtorFunction = Function::Create(FunctionType::get(Type::getVoidTy(*Ctx),
                                                        false),
                                      GlobalValue::InternalLinkage,
                                      EsanModuleDtorName, &M);
  ReturnInst::Create(*Ctx, BasicBlock::Create(*Ctx, "", EsanDtorFunction));
  IRBuilder<> IRB_Dtor(EsanDtorFunction->getEntryBlock().getTerminator());
  Function *EsanExit = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction(EsanExitName, IRB_Dtor.getVoidTy(),
                            Int8PtrTy));
  EsanExit->setLinkage(Function::ExternalLinkage);
  IRB_Dtor.CreateCall(EsanExit, {ToolInfoArg});
  appendToGlobalDtors(M, EsanDtorFunction, EsanCtorAndDtorPriority);
}

bool EfficiencySanitizer::initOnModule(Module &M) {

  Triple TargetTriple(M.getTargetTriple());
  if (TargetTriple.isMIPS64())
    ShadowParams = ShadowParams40;
  else
    ShadowParams = ShadowParams47;

  Ctx = &M.getContext();
  const DataLayout &DL = M.getDataLayout();
  IRBuilder<> IRB(M.getContext());
  IntegerType *OrdTy = IRB.getInt32Ty();
  PointerType *Int8PtrTy = Type::getInt8PtrTy(*Ctx);
  IntptrTy = DL.getIntPtrType(M.getContext());
  // Create the variable passed to EsanInit and EsanExit.
  Constant *ToolInfoArg = createEsanInitToolInfoArg(M, DL);
  // Constructor
  // We specify the tool type both in the EsanWhichToolName global
  // and as an arg to the init routine as a sanity check.
  std::tie(EsanCtorFunction, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, EsanModuleCtorName, EsanInitName, /*InitArgTypes=*/{OrdTy, Int8PtrTy},
      /*InitArgs=*/{
        ConstantInt::get(OrdTy, static_cast<int>(Options.ToolType)),
        ToolInfoArg});
  appendToGlobalCtors(M, EsanCtorFunction, EsanCtorAndDtorPriority);

  createDestructor(M, ToolInfoArg);

  new GlobalVariable(M, OrdTy, true,
                     GlobalValue::WeakAnyLinkage,
                     ConstantInt::get(OrdTy,
                                      static_cast<int>(Options.ToolType)),
                     EsanWhichToolName);

  return true;
}

Value *EfficiencySanitizer::appToShadow(Value *Shadow, IRBuilder<> &IRB) {
  // Shadow = ((App & Mask) + Offs) >> Scale
  Shadow = IRB.CreateAnd(Shadow, ConstantInt::get(IntptrTy, ShadowParams.ShadowMask));
  uint64_t Offs;
  int Scale = ShadowScale[Options.ToolType];
  if (Scale <= 2)
    Offs = ShadowParams.ShadowOffs[Scale];
  else
    Offs = ShadowParams.ShadowOffs[0] << Scale;
  Shadow = IRB.CreateAdd(Shadow, ConstantInt::get(IntptrTy, Offs));
  if (Scale > 0)
    Shadow = IRB.CreateLShr(Shadow, Scale);
  return Shadow;
}

bool EfficiencySanitizer::shouldIgnoreMemoryAccess(Instruction *I) {
  if (Options.ToolType == EfficiencySanitizerOptions::ESAN_CacheFrag) {
    // We'd like to know about cache fragmentation in vtable accesses and
    // constant data references, so we do not currently ignore anything.
    return false;
  } else if (Options.ToolType == EfficiencySanitizerOptions::ESAN_WorkingSet) {
    // TODO: the instrumentation disturbs the data layout on the stack, so we
    // may want to add an option to ignore stack references (if we can
    // distinguish them) to reduce overhead.
  }
  // TODO(bruening): future tools will be returning true for some cases.
  return false;
}

bool EfficiencySanitizer::runOnModule(Module &M) {
  bool Res = initOnModule(M);
  initializeCallbacks(M);
  for (auto &F : M) {
    Res |= runOnFunction(F, M);
  }
  return Res;
}

bool EfficiencySanitizer::runOnFunction(Function &F, Module &M) {
  // This is required to prevent instrumenting the call to __esan_init from
  // within the module constructor.
  if (&F == EsanCtorFunction)
    return false;
  SmallVector<Instruction *, 8> LoadsAndStores;
  SmallVector<Instruction *, 8> MemIntrinCalls;
  SmallVector<Instruction *, 8> GetElementPtrs;
  bool Res = false;
  const DataLayout &DL = M.getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if ((isa<LoadInst>(Inst) || isa<StoreInst>(Inst) ||
           isa<AtomicRMWInst>(Inst) || isa<AtomicCmpXchgInst>(Inst)) &&
          !shouldIgnoreMemoryAccess(&Inst))
        LoadsAndStores.push_back(&Inst);
      else if (isa<MemIntrinsic>(Inst))
        MemIntrinCalls.push_back(&Inst);
      else if (isa<GetElementPtrInst>(Inst))
        GetElementPtrs.push_back(&Inst);
      else if (CallInst *CI = dyn_cast<CallInst>(&Inst))
        maybeMarkSanitizerLibraryCallNoBuiltin(CI, TLI);
    }
  }

  if (ClInstrumentLoadsAndStores) {
    for (auto Inst : LoadsAndStores) {
      Res |= instrumentLoadOrStore(Inst, DL);
    }
  }

  if (ClInstrumentMemIntrinsics) {
    for (auto Inst : MemIntrinCalls) {
      Res |= instrumentMemIntrinsic(cast<MemIntrinsic>(Inst));
    }
  }

  if (Options.ToolType == EfficiencySanitizerOptions::ESAN_CacheFrag) {
    for (auto Inst : GetElementPtrs) {
      Res |= instrumentGetElementPtr(Inst, M);
    }
  }

  return Res;
}

bool EfficiencySanitizer::instrumentLoadOrStore(Instruction *I,
                                                const DataLayout &DL) {
  IRBuilder<> IRB(I);
  bool IsStore;
  Value *Addr;
  unsigned Alignment;
  if (LoadInst *Load = dyn_cast<LoadInst>(I)) {
    IsStore = false;
    Alignment = Load->getAlignment();
    Addr = Load->getPointerOperand();
  } else if (StoreInst *Store = dyn_cast<StoreInst>(I)) {
    IsStore = true;
    Alignment = Store->getAlignment();
    Addr = Store->getPointerOperand();
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    IsStore = true;
    Alignment = 0;
    Addr = RMW->getPointerOperand();
  } else if (AtomicCmpXchgInst *Xchg = dyn_cast<AtomicCmpXchgInst>(I)) {
    IsStore = true;
    Alignment = 0;
    Addr = Xchg->getPointerOperand();
  } else
    llvm_unreachable("Unsupported mem access type");

  Type *OrigTy = cast<PointerType>(Addr->getType())->getElementType();
  const uint32_t TypeSizeBytes = DL.getTypeStoreSizeInBits(OrigTy) / 8;
  Value *OnAccessFunc = nullptr;

  // Convert 0 to the default alignment.
  if (Alignment == 0)
    Alignment = DL.getPrefTypeAlignment(OrigTy);

  if (IsStore)
    NumInstrumentedStores++;
  else
    NumInstrumentedLoads++;
  int Idx = getMemoryAccessFuncIndex(Addr, DL);
  if (Idx < 0) {
    OnAccessFunc = IsStore ? EsanUnalignedStoreN : EsanUnalignedLoadN;
    IRB.CreateCall(OnAccessFunc,
                   {IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()),
                    ConstantInt::get(IntptrTy, TypeSizeBytes)});
  } else {
    if (ClInstrumentFastpath &&
        instrumentFastpath(I, DL, IsStore, Addr, Alignment)) {
      NumFastpaths++;
      return true;
    }
    if (Alignment == 0 || (Alignment % TypeSizeBytes) == 0)
      OnAccessFunc = IsStore ? EsanAlignedStore[Idx] : EsanAlignedLoad[Idx];
    else
      OnAccessFunc = IsStore ? EsanUnalignedStore[Idx] : EsanUnalignedLoad[Idx];
    IRB.CreateCall(OnAccessFunc,
                   IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
  }
  return true;
}

// It's simplest to replace the memset/memmove/memcpy intrinsics with
// calls that the runtime library intercepts.
// Our pass is late enough that calls should not turn back into intrinsics.
bool EfficiencySanitizer::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);
  bool Res = false;
  if (isa<MemSetInst>(MI)) {
    IRB.CreateCall(
        MemsetFn,
        {IRB.CreatePointerCast(MI->getArgOperand(0), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(MI->getArgOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(MI->getArgOperand(2), IntptrTy, false)});
    MI->eraseFromParent();
    Res = true;
  } else if (isa<MemTransferInst>(MI)) {
    IRB.CreateCall(
        isa<MemCpyInst>(MI) ? MemcpyFn : MemmoveFn,
        {IRB.CreatePointerCast(MI->getArgOperand(0), IRB.getInt8PtrTy()),
         IRB.CreatePointerCast(MI->getArgOperand(1), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(MI->getArgOperand(2), IntptrTy, false)});
    MI->eraseFromParent();
    Res = true;
  } else
    llvm_unreachable("Unsupported mem intrinsic type");
  return Res;
}

bool EfficiencySanitizer::instrumentGetElementPtr(Instruction *I, Module &M) {
  GetElementPtrInst *GepInst = dyn_cast<GetElementPtrInst>(I);
  bool Res = false;
  if (GepInst == nullptr || GepInst->getNumIndices() == 1) {
    ++NumIgnoredGEPs;
    return false;
  }
  Type *SourceTy = GepInst->getSourceElementType();
  StructType *StructTy = nullptr;
  ConstantInt *Idx;
  // Check if GEP calculates address from a struct array.
  if (isa<StructType>(SourceTy)) {
    StructTy = cast<StructType>(SourceTy);
    Idx = dyn_cast<ConstantInt>(GepInst->getOperand(1));
    if ((Idx == nullptr || Idx->getSExtValue() != 0) &&
        !shouldIgnoreStructType(StructTy) && StructTyMap.count(StructTy) != 0)
      Res |= insertCounterUpdate(I, StructTy, getArrayCounterIdx(StructTy));
  }
  // Iterate all (except the first and the last) idx within each GEP instruction
  // for possible nested struct field address calculation.
  for (unsigned i = 1; i < GepInst->getNumIndices(); ++i) {
    SmallVector<Value *, 8> IdxVec(GepInst->idx_begin(),
                                   GepInst->idx_begin() + i);
    Type *Ty = GetElementPtrInst::getIndexedType(SourceTy, IdxVec);
    unsigned CounterIdx = 0;
    if (isa<ArrayType>(Ty)) {
      ArrayType *ArrayTy = cast<ArrayType>(Ty);
      StructTy = dyn_cast<StructType>(ArrayTy->getElementType());
      if (shouldIgnoreStructType(StructTy) || StructTyMap.count(StructTy) == 0)
        continue;
      // The last counter for struct array access.
      CounterIdx = getArrayCounterIdx(StructTy);
    } else if (isa<StructType>(Ty)) {
      StructTy = cast<StructType>(Ty);
      if (shouldIgnoreStructType(StructTy) || StructTyMap.count(StructTy) == 0)
        continue;
      // Get the StructTy's subfield index.
      Idx = cast<ConstantInt>(GepInst->getOperand(i+1));
      assert(Idx->getSExtValue() >= 0 &&
             Idx->getSExtValue() < StructTy->getNumElements());
      CounterIdx = getFieldCounterIdx(StructTy) + Idx->getSExtValue();
    }
    Res |= insertCounterUpdate(I, StructTy, CounterIdx);
  }
  if (Res)
    ++NumInstrumentedGEPs;
  else
    ++NumIgnoredGEPs;
  return Res;
}

bool EfficiencySanitizer::insertCounterUpdate(Instruction *I,
                                              StructType *StructTy,
                                              unsigned CounterIdx) {
  GlobalVariable *CounterArray = StructTyMap[StructTy];
  if (CounterArray == nullptr)
    return false;
  IRBuilder<> IRB(I);
  Constant *Indices[2];
  // Xref http://llvm.org/docs/LangRef.html#i-getelementptr and
  // http://llvm.org/docs/GetElementPtr.html.
  // The first index of the GEP instruction steps through the first operand,
  // i.e., the array itself.
  Indices[0] = ConstantInt::get(IRB.getInt32Ty(), 0);
  // The second index is the index within the array.
  Indices[1] = ConstantInt::get(IRB.getInt32Ty(), CounterIdx);
  Constant *Counter =
    ConstantExpr::getGetElementPtr(
        ArrayType::get(IRB.getInt64Ty(), getStructCounterSize(StructTy)),
        CounterArray, Indices);
  Value *Load = IRB.CreateLoad(Counter);
  IRB.CreateStore(IRB.CreateAdd(Load, ConstantInt::get(IRB.getInt64Ty(), 1)),
                  Counter);
  return true;
}

int EfficiencySanitizer::getMemoryAccessFuncIndex(Value *Addr,
                                                  const DataLayout &DL) {
  Type *OrigPtrTy = Addr->getType();
  Type *OrigTy = cast<PointerType>(OrigPtrTy)->getElementType();
  assert(OrigTy->isSized());
  // The size is always a multiple of 8.
  uint32_t TypeSizeBytes = DL.getTypeStoreSizeInBits(OrigTy) / 8;
  if (TypeSizeBytes != 1 && TypeSizeBytes != 2 && TypeSizeBytes != 4 &&
      TypeSizeBytes != 8 && TypeSizeBytes != 16) {
    // Irregular sizes do not have per-size call targets.
    NumAccessesWithIrregularSize++;
    return -1;
  }
  size_t Idx = countTrailingZeros(TypeSizeBytes);
  assert(Idx < NumberOfAccessSizes);
  return Idx;
}

bool EfficiencySanitizer::instrumentFastpath(Instruction *I,
                                             const DataLayout &DL, bool IsStore,
                                             Value *Addr, unsigned Alignment) {
  if (Options.ToolType == EfficiencySanitizerOptions::ESAN_CacheFrag) {
    return instrumentFastpathCacheFrag(I, DL, Addr, Alignment);
  } else if (Options.ToolType == EfficiencySanitizerOptions::ESAN_WorkingSet) {
    return instrumentFastpathWorkingSet(I, DL, Addr, Alignment);
  }
  return false;
}

bool EfficiencySanitizer::instrumentFastpathCacheFrag(Instruction *I,
                                                      const DataLayout &DL,
                                                      Value *Addr,
                                                      unsigned Alignment) {
  // Do nothing.
  return true; // Return true to avoid slowpath instrumentation.
}

bool EfficiencySanitizer::instrumentFastpathWorkingSet(
    Instruction *I, const DataLayout &DL, Value *Addr, unsigned Alignment) {
  assert(ShadowScale[Options.ToolType] == 6); // The code below assumes this
  IRBuilder<> IRB(I);
  Type *OrigTy = cast<PointerType>(Addr->getType())->getElementType();
  const uint32_t TypeSize = DL.getTypeStoreSizeInBits(OrigTy);
  // Bail to the slowpath if the access might touch multiple cache lines.
  // An access aligned to its size is guaranteed to be intra-cache-line.
  // getMemoryAccessFuncIndex has already ruled out a size larger than 16
  // and thus larger than a cache line for platforms this tool targets
  // (and our shadow memory setup assumes 64-byte cache lines).
  assert(TypeSize <= 128);
  if (!(TypeSize == 8 ||
        (Alignment % (TypeSize / 8)) == 0)) {
    if (ClAssumeIntraCacheLine)
      ++NumAssumedIntraCacheLine;
    else
      return false;
  }

  // We inline instrumentation to set the corresponding shadow bits for
  // each cache line touched by the application.  Here we handle a single
  // load or store where we've already ruled out the possibility that it
  // might touch more than one cache line and thus we simply update the
  // shadow memory for a single cache line.
  // Our shadow memory model is fine with races when manipulating shadow values.
  // We generate the following code:
  //
  //   const char BitMask = 0x81;
  //   char *ShadowAddr = appToShadow(AppAddr);
  //   if ((*ShadowAddr & BitMask) != BitMask)
  //     *ShadowAddr |= Bitmask;
  //
  Value *AddrPtr = IRB.CreatePointerCast(Addr, IntptrTy);
  Value *ShadowPtr = appToShadow(AddrPtr, IRB);
  Type *ShadowTy = IntegerType::get(*Ctx, 8U);
  Type *ShadowPtrTy = PointerType::get(ShadowTy, 0);
  // The bottom bit is used for the current sampling period's working set.
  // The top bit is used for the total working set.  We set both on each
  // memory access, if they are not already set.
  Value *ValueMask = ConstantInt::get(ShadowTy, 0x81); // 10000001B

  Value *OldValue = IRB.CreateLoad(IRB.CreateIntToPtr(ShadowPtr, ShadowPtrTy));
  // The AND and CMP will be turned into a TEST instruction by the compiler.
  Value *Cmp = IRB.CreateICmpNE(IRB.CreateAnd(OldValue, ValueMask), ValueMask);
  Instruction *CmpTerm = SplitBlockAndInsertIfThen(Cmp, I, false);
  // FIXME: do I need to call SetCurrentDebugLocation?
  IRB.SetInsertPoint(CmpTerm);
  // We use OR to set the shadow bits to avoid corrupting the middle 6 bits,
  // which are used by the runtime library.
  Value *NewVal = IRB.CreateOr(OldValue, ValueMask);
  IRB.CreateStore(NewVal, IRB.CreateIntToPtr(ShadowPtr, ShadowPtrTy));
  IRB.SetInsertPoint(I);

  return true;
}
