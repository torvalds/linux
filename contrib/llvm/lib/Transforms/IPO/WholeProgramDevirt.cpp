//===- WholeProgramDevirt.cpp - Whole program virtual call optimization ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements whole program optimization of virtual calls in cases
// where we know (via !type metadata) that the list of callees is fixed. This
// includes the following:
// - Single implementation devirtualization: if a virtual call has a single
//   possible callee, replace all calls with a direct call to that callee.
// - Virtual constant propagation: if the virtual function's return type is an
//   integer <=64 bits and all possible callees are readnone, for each class and
//   each list of constant arguments: evaluate the function, store the return
//   value alongside the virtual table, and rewrite each virtual call as a load
//   from the virtual table.
// - Uniform return value optimization: if the conditions for virtual constant
//   propagation hold and each function returns the same constant value, replace
//   each virtual call with that constant.
// - Unique return value optimization for i1 return values: if the conditions
//   for virtual constant propagation hold and a single vtable's function
//   returns 0, or a single vtable's function returns 1, replace each virtual
//   call with a comparison of the vptr against that vtable's address.
//
// This pass is intended to be used during the regular and thin LTO pipelines.
// During regular LTO, the pass determines the best optimization for each
// virtual call and applies the resolutions directly to virtual calls that are
// eligible for virtual call optimization (i.e. calls that use either of the
// llvm.assume(llvm.type.test) or llvm.type.checked.load intrinsics). During
// ThinLTO, the pass operates in two phases:
// - Export phase: this is run during the thin link over a single merged module
//   that contains all vtables with !type metadata that participate in the link.
//   The pass computes a resolution for each virtual call and stores it in the
//   type identifier summary.
// - Import phase: this is run during the thin backends over the individual
//   modules. The pass applies the resolutions previously computed during the
//   import phase to each eligible virtual call.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndexYAML.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/Utils/Evaluator.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>

using namespace llvm;
using namespace wholeprogramdevirt;

#define DEBUG_TYPE "wholeprogramdevirt"

static cl::opt<PassSummaryAction> ClSummaryAction(
    "wholeprogramdevirt-summary-action",
    cl::desc("What to do with the summary when running this pass"),
    cl::values(clEnumValN(PassSummaryAction::None, "none", "Do nothing"),
               clEnumValN(PassSummaryAction::Import, "import",
                          "Import typeid resolutions from summary and globals"),
               clEnumValN(PassSummaryAction::Export, "export",
                          "Export typeid resolutions to summary and globals")),
    cl::Hidden);

static cl::opt<std::string> ClReadSummary(
    "wholeprogramdevirt-read-summary",
    cl::desc("Read summary from given YAML file before running pass"),
    cl::Hidden);

static cl::opt<std::string> ClWriteSummary(
    "wholeprogramdevirt-write-summary",
    cl::desc("Write summary to given YAML file after running pass"),
    cl::Hidden);

static cl::opt<unsigned>
    ClThreshold("wholeprogramdevirt-branch-funnel-threshold", cl::Hidden,
                cl::init(10), cl::ZeroOrMore,
                cl::desc("Maximum number of call targets per "
                         "call site to enable branch funnels"));

// Find the minimum offset that we may store a value of size Size bits at. If
// IsAfter is set, look for an offset before the object, otherwise look for an
// offset after the object.
uint64_t
wholeprogramdevirt::findLowestOffset(ArrayRef<VirtualCallTarget> Targets,
                                     bool IsAfter, uint64_t Size) {
  // Find a minimum offset taking into account only vtable sizes.
  uint64_t MinByte = 0;
  for (const VirtualCallTarget &Target : Targets) {
    if (IsAfter)
      MinByte = std::max(MinByte, Target.minAfterBytes());
    else
      MinByte = std::max(MinByte, Target.minBeforeBytes());
  }

  // Build a vector of arrays of bytes covering, for each target, a slice of the
  // used region (see AccumBitVector::BytesUsed in
  // llvm/Transforms/IPO/WholeProgramDevirt.h) starting at MinByte. Effectively,
  // this aligns the used regions to start at MinByte.
  //
  // In this example, A, B and C are vtables, # is a byte already allocated for
  // a virtual function pointer, AAAA... (etc.) are the used regions for the
  // vtables and Offset(X) is the value computed for the Offset variable below
  // for X.
  //
  //                    Offset(A)
  //                    |       |
  //                            |MinByte
  // A: ################AAAAAAAA|AAAAAAAA
  // B: ########BBBBBBBBBBBBBBBB|BBBB
  // C: ########################|CCCCCCCCCCCCCCCC
  //            |   Offset(B)   |
  //
  // This code produces the slices of A, B and C that appear after the divider
  // at MinByte.
  std::vector<ArrayRef<uint8_t>> Used;
  for (const VirtualCallTarget &Target : Targets) {
    ArrayRef<uint8_t> VTUsed = IsAfter ? Target.TM->Bits->After.BytesUsed
                                       : Target.TM->Bits->Before.BytesUsed;
    uint64_t Offset = IsAfter ? MinByte - Target.minAfterBytes()
                              : MinByte - Target.minBeforeBytes();

    // Disregard used regions that are smaller than Offset. These are
    // effectively all-free regions that do not need to be checked.
    if (VTUsed.size() > Offset)
      Used.push_back(VTUsed.slice(Offset));
  }

  if (Size == 1) {
    // Find a free bit in each member of Used.
    for (unsigned I = 0;; ++I) {
      uint8_t BitsUsed = 0;
      for (auto &&B : Used)
        if (I < B.size())
          BitsUsed |= B[I];
      if (BitsUsed != 0xff)
        return (MinByte + I) * 8 +
               countTrailingZeros(uint8_t(~BitsUsed), ZB_Undefined);
    }
  } else {
    // Find a free (Size/8) byte region in each member of Used.
    // FIXME: see if alignment helps.
    for (unsigned I = 0;; ++I) {
      for (auto &&B : Used) {
        unsigned Byte = 0;
        while ((I + Byte) < B.size() && Byte < (Size / 8)) {
          if (B[I + Byte])
            goto NextI;
          ++Byte;
        }
      }
      return (MinByte + I) * 8;
    NextI:;
    }
  }
}

void wholeprogramdevirt::setBeforeReturnValues(
    MutableArrayRef<VirtualCallTarget> Targets, uint64_t AllocBefore,
    unsigned BitWidth, int64_t &OffsetByte, uint64_t &OffsetBit) {
  if (BitWidth == 1)
    OffsetByte = -(AllocBefore / 8 + 1);
  else
    OffsetByte = -((AllocBefore + 7) / 8 + (BitWidth + 7) / 8);
  OffsetBit = AllocBefore % 8;

  for (VirtualCallTarget &Target : Targets) {
    if (BitWidth == 1)
      Target.setBeforeBit(AllocBefore);
    else
      Target.setBeforeBytes(AllocBefore, (BitWidth + 7) / 8);
  }
}

void wholeprogramdevirt::setAfterReturnValues(
    MutableArrayRef<VirtualCallTarget> Targets, uint64_t AllocAfter,
    unsigned BitWidth, int64_t &OffsetByte, uint64_t &OffsetBit) {
  if (BitWidth == 1)
    OffsetByte = AllocAfter / 8;
  else
    OffsetByte = (AllocAfter + 7) / 8;
  OffsetBit = AllocAfter % 8;

  for (VirtualCallTarget &Target : Targets) {
    if (BitWidth == 1)
      Target.setAfterBit(AllocAfter);
    else
      Target.setAfterBytes(AllocAfter, (BitWidth + 7) / 8);
  }
}

VirtualCallTarget::VirtualCallTarget(Function *Fn, const TypeMemberInfo *TM)
    : Fn(Fn), TM(TM),
      IsBigEndian(Fn->getParent()->getDataLayout().isBigEndian()), WasDevirt(false) {}

namespace {

// A slot in a set of virtual tables. The TypeID identifies the set of virtual
// tables, and the ByteOffset is the offset in bytes from the address point to
// the virtual function pointer.
struct VTableSlot {
  Metadata *TypeID;
  uint64_t ByteOffset;
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<VTableSlot> {
  static VTableSlot getEmptyKey() {
    return {DenseMapInfo<Metadata *>::getEmptyKey(),
            DenseMapInfo<uint64_t>::getEmptyKey()};
  }
  static VTableSlot getTombstoneKey() {
    return {DenseMapInfo<Metadata *>::getTombstoneKey(),
            DenseMapInfo<uint64_t>::getTombstoneKey()};
  }
  static unsigned getHashValue(const VTableSlot &I) {
    return DenseMapInfo<Metadata *>::getHashValue(I.TypeID) ^
           DenseMapInfo<uint64_t>::getHashValue(I.ByteOffset);
  }
  static bool isEqual(const VTableSlot &LHS,
                      const VTableSlot &RHS) {
    return LHS.TypeID == RHS.TypeID && LHS.ByteOffset == RHS.ByteOffset;
  }
};

} // end namespace llvm

namespace {

// A virtual call site. VTable is the loaded virtual table pointer, and CS is
// the indirect virtual call.
struct VirtualCallSite {
  Value *VTable;
  CallSite CS;

  // If non-null, this field points to the associated unsafe use count stored in
  // the DevirtModule::NumUnsafeUsesForTypeTest map below. See the description
  // of that field for details.
  unsigned *NumUnsafeUses;

  void
  emitRemark(const StringRef OptName, const StringRef TargetName,
             function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter) {
    Function *F = CS.getCaller();
    DebugLoc DLoc = CS->getDebugLoc();
    BasicBlock *Block = CS.getParent();

    using namespace ore;
    OREGetter(F).emit(OptimizationRemark(DEBUG_TYPE, OptName, DLoc, Block)
                      << NV("Optimization", OptName)
                      << ": devirtualized a call to "
                      << NV("FunctionName", TargetName));
  }

  void replaceAndErase(
      const StringRef OptName, const StringRef TargetName, bool RemarksEnabled,
      function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
      Value *New) {
    if (RemarksEnabled)
      emitRemark(OptName, TargetName, OREGetter);
    CS->replaceAllUsesWith(New);
    if (auto II = dyn_cast<InvokeInst>(CS.getInstruction())) {
      BranchInst::Create(II->getNormalDest(), CS.getInstruction());
      II->getUnwindDest()->removePredecessor(II->getParent());
    }
    CS->eraseFromParent();
    // This use is no longer unsafe.
    if (NumUnsafeUses)
      --*NumUnsafeUses;
  }
};

// Call site information collected for a specific VTableSlot and possibly a list
// of constant integer arguments. The grouping by arguments is handled by the
// VTableSlotInfo class.
struct CallSiteInfo {
  /// The set of call sites for this slot. Used during regular LTO and the
  /// import phase of ThinLTO (as well as the export phase of ThinLTO for any
  /// call sites that appear in the merged module itself); in each of these
  /// cases we are directly operating on the call sites at the IR level.
  std::vector<VirtualCallSite> CallSites;

  /// Whether all call sites represented by this CallSiteInfo, including those
  /// in summaries, have been devirtualized. This starts off as true because a
  /// default constructed CallSiteInfo represents no call sites.
  bool AllCallSitesDevirted = true;

  // These fields are used during the export phase of ThinLTO and reflect
  // information collected from function summaries.

  /// Whether any function summary contains an llvm.assume(llvm.type.test) for
  /// this slot.
  bool SummaryHasTypeTestAssumeUsers = false;

  /// CFI-specific: a vector containing the list of function summaries that use
  /// the llvm.type.checked.load intrinsic and therefore will require
  /// resolutions for llvm.type.test in order to implement CFI checks if
  /// devirtualization was unsuccessful. If devirtualization was successful, the
  /// pass will clear this vector by calling markDevirt(). If at the end of the
  /// pass the vector is non-empty, we will need to add a use of llvm.type.test
  /// to each of the function summaries in the vector.
  std::vector<FunctionSummary *> SummaryTypeCheckedLoadUsers;

  bool isExported() const {
    return SummaryHasTypeTestAssumeUsers ||
           !SummaryTypeCheckedLoadUsers.empty();
  }

  void markSummaryHasTypeTestAssumeUsers() {
    SummaryHasTypeTestAssumeUsers = true;
    AllCallSitesDevirted = false;
  }

  void addSummaryTypeCheckedLoadUser(FunctionSummary *FS) {
    SummaryTypeCheckedLoadUsers.push_back(FS);
    AllCallSitesDevirted = false;
  }

  void markDevirt() {
    AllCallSitesDevirted = true;

    // As explained in the comment for SummaryTypeCheckedLoadUsers.
    SummaryTypeCheckedLoadUsers.clear();
  }
};

// Call site information collected for a specific VTableSlot.
struct VTableSlotInfo {
  // The set of call sites which do not have all constant integer arguments
  // (excluding "this").
  CallSiteInfo CSInfo;

  // The set of call sites with all constant integer arguments (excluding
  // "this"), grouped by argument list.
  std::map<std::vector<uint64_t>, CallSiteInfo> ConstCSInfo;

  void addCallSite(Value *VTable, CallSite CS, unsigned *NumUnsafeUses);

private:
  CallSiteInfo &findCallSiteInfo(CallSite CS);
};

CallSiteInfo &VTableSlotInfo::findCallSiteInfo(CallSite CS) {
  std::vector<uint64_t> Args;
  auto *CI = dyn_cast<IntegerType>(CS.getType());
  if (!CI || CI->getBitWidth() > 64 || CS.arg_empty())
    return CSInfo;
  for (auto &&Arg : make_range(CS.arg_begin() + 1, CS.arg_end())) {
    auto *CI = dyn_cast<ConstantInt>(Arg);
    if (!CI || CI->getBitWidth() > 64)
      return CSInfo;
    Args.push_back(CI->getZExtValue());
  }
  return ConstCSInfo[Args];
}

void VTableSlotInfo::addCallSite(Value *VTable, CallSite CS,
                                 unsigned *NumUnsafeUses) {
  auto &CSI = findCallSiteInfo(CS);
  CSI.AllCallSitesDevirted = false;
  CSI.CallSites.push_back({VTable, CS, NumUnsafeUses});
}

struct DevirtModule {
  Module &M;
  function_ref<AAResults &(Function &)> AARGetter;
  function_ref<DominatorTree &(Function &)> LookupDomTree;

  ModuleSummaryIndex *ExportSummary;
  const ModuleSummaryIndex *ImportSummary;

  IntegerType *Int8Ty;
  PointerType *Int8PtrTy;
  IntegerType *Int32Ty;
  IntegerType *Int64Ty;
  IntegerType *IntPtrTy;

  bool RemarksEnabled;
  function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter;

  MapVector<VTableSlot, VTableSlotInfo> CallSlots;

  // This map keeps track of the number of "unsafe" uses of a loaded function
  // pointer. The key is the associated llvm.type.test intrinsic call generated
  // by this pass. An unsafe use is one that calls the loaded function pointer
  // directly. Every time we eliminate an unsafe use (for example, by
  // devirtualizing it or by applying virtual constant propagation), we
  // decrement the value stored in this map. If a value reaches zero, we can
  // eliminate the type check by RAUWing the associated llvm.type.test call with
  // true.
  std::map<CallInst *, unsigned> NumUnsafeUsesForTypeTest;

  DevirtModule(Module &M, function_ref<AAResults &(Function &)> AARGetter,
               function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
               function_ref<DominatorTree &(Function &)> LookupDomTree,
               ModuleSummaryIndex *ExportSummary,
               const ModuleSummaryIndex *ImportSummary)
      : M(M), AARGetter(AARGetter), LookupDomTree(LookupDomTree),
        ExportSummary(ExportSummary), ImportSummary(ImportSummary),
        Int8Ty(Type::getInt8Ty(M.getContext())),
        Int8PtrTy(Type::getInt8PtrTy(M.getContext())),
        Int32Ty(Type::getInt32Ty(M.getContext())),
        Int64Ty(Type::getInt64Ty(M.getContext())),
        IntPtrTy(M.getDataLayout().getIntPtrType(M.getContext(), 0)),
        RemarksEnabled(areRemarksEnabled()), OREGetter(OREGetter) {
    assert(!(ExportSummary && ImportSummary));
  }

  bool areRemarksEnabled();

  void scanTypeTestUsers(Function *TypeTestFunc, Function *AssumeFunc);
  void scanTypeCheckedLoadUsers(Function *TypeCheckedLoadFunc);

  void buildTypeIdentifierMap(
      std::vector<VTableBits> &Bits,
      DenseMap<Metadata *, std::set<TypeMemberInfo>> &TypeIdMap);
  Constant *getPointerAtOffset(Constant *I, uint64_t Offset);
  bool
  tryFindVirtualCallTargets(std::vector<VirtualCallTarget> &TargetsForSlot,
                            const std::set<TypeMemberInfo> &TypeMemberInfos,
                            uint64_t ByteOffset);

  void applySingleImplDevirt(VTableSlotInfo &SlotInfo, Constant *TheFn,
                             bool &IsExported);
  bool trySingleImplDevirt(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                           VTableSlotInfo &SlotInfo,
                           WholeProgramDevirtResolution *Res);

  void applyICallBranchFunnel(VTableSlotInfo &SlotInfo, Constant *JT,
                              bool &IsExported);
  void tryICallBranchFunnel(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                            VTableSlotInfo &SlotInfo,
                            WholeProgramDevirtResolution *Res, VTableSlot Slot);

  bool tryEvaluateFunctionsWithArgs(
      MutableArrayRef<VirtualCallTarget> TargetsForSlot,
      ArrayRef<uint64_t> Args);

  void applyUniformRetValOpt(CallSiteInfo &CSInfo, StringRef FnName,
                             uint64_t TheRetVal);
  bool tryUniformRetValOpt(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                           CallSiteInfo &CSInfo,
                           WholeProgramDevirtResolution::ByArg *Res);

  // Returns the global symbol name that is used to export information about the
  // given vtable slot and list of arguments.
  std::string getGlobalName(VTableSlot Slot, ArrayRef<uint64_t> Args,
                            StringRef Name);

  bool shouldExportConstantsAsAbsoluteSymbols();

  // This function is called during the export phase to create a symbol
  // definition containing information about the given vtable slot and list of
  // arguments.
  void exportGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args, StringRef Name,
                    Constant *C);
  void exportConstant(VTableSlot Slot, ArrayRef<uint64_t> Args, StringRef Name,
                      uint32_t Const, uint32_t &Storage);

  // This function is called during the import phase to create a reference to
  // the symbol definition created during the export phase.
  Constant *importGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args,
                         StringRef Name);
  Constant *importConstant(VTableSlot Slot, ArrayRef<uint64_t> Args,
                           StringRef Name, IntegerType *IntTy,
                           uint32_t Storage);

  Constant *getMemberAddr(const TypeMemberInfo *M);

  void applyUniqueRetValOpt(CallSiteInfo &CSInfo, StringRef FnName, bool IsOne,
                            Constant *UniqueMemberAddr);
  bool tryUniqueRetValOpt(unsigned BitWidth,
                          MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                          CallSiteInfo &CSInfo,
                          WholeProgramDevirtResolution::ByArg *Res,
                          VTableSlot Slot, ArrayRef<uint64_t> Args);

  void applyVirtualConstProp(CallSiteInfo &CSInfo, StringRef FnName,
                             Constant *Byte, Constant *Bit);
  bool tryVirtualConstProp(MutableArrayRef<VirtualCallTarget> TargetsForSlot,
                           VTableSlotInfo &SlotInfo,
                           WholeProgramDevirtResolution *Res, VTableSlot Slot);

  void rebuildGlobal(VTableBits &B);

  // Apply the summary resolution for Slot to all virtual calls in SlotInfo.
  void importResolution(VTableSlot Slot, VTableSlotInfo &SlotInfo);

  // If we were able to eliminate all unsafe uses for a type checked load,
  // eliminate the associated type tests by replacing them with true.
  void removeRedundantTypeTests();

  bool run();

  // Lower the module using the action and summary passed as command line
  // arguments. For testing purposes only.
  static bool
  runForTesting(Module &M, function_ref<AAResults &(Function &)> AARGetter,
                function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
                function_ref<DominatorTree &(Function &)> LookupDomTree);
};

struct WholeProgramDevirt : public ModulePass {
  static char ID;

  bool UseCommandLine = false;

  ModuleSummaryIndex *ExportSummary;
  const ModuleSummaryIndex *ImportSummary;

  WholeProgramDevirt() : ModulePass(ID), UseCommandLine(true) {
    initializeWholeProgramDevirtPass(*PassRegistry::getPassRegistry());
  }

  WholeProgramDevirt(ModuleSummaryIndex *ExportSummary,
                     const ModuleSummaryIndex *ImportSummary)
      : ModulePass(ID), ExportSummary(ExportSummary),
        ImportSummary(ImportSummary) {
    initializeWholeProgramDevirtPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;

    // In the new pass manager, we can request the optimization
    // remark emitter pass on a per-function-basis, which the
    // OREGetter will do for us.
    // In the old pass manager, this is harder, so we just build
    // an optimization remark emitter on the fly, when we need it.
    std::unique_ptr<OptimizationRemarkEmitter> ORE;
    auto OREGetter = [&](Function *F) -> OptimizationRemarkEmitter & {
      ORE = make_unique<OptimizationRemarkEmitter>(F);
      return *ORE;
    };

    auto LookupDomTree = [this](Function &F) -> DominatorTree & {
      return this->getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    };

    if (UseCommandLine)
      return DevirtModule::runForTesting(M, LegacyAARGetter(*this), OREGetter,
                                         LookupDomTree);

    return DevirtModule(M, LegacyAARGetter(*this), OREGetter, LookupDomTree,
                        ExportSummary, ImportSummary)
        .run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
  }
};

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(WholeProgramDevirt, "wholeprogramdevirt",
                      "Whole program devirtualization", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(WholeProgramDevirt, "wholeprogramdevirt",
                    "Whole program devirtualization", false, false)
char WholeProgramDevirt::ID = 0;

ModulePass *
llvm::createWholeProgramDevirtPass(ModuleSummaryIndex *ExportSummary,
                                   const ModuleSummaryIndex *ImportSummary) {
  return new WholeProgramDevirt(ExportSummary, ImportSummary);
}

PreservedAnalyses WholeProgramDevirtPass::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto AARGetter = [&](Function &F) -> AAResults & {
    return FAM.getResult<AAManager>(F);
  };
  auto OREGetter = [&](Function *F) -> OptimizationRemarkEmitter & {
    return FAM.getResult<OptimizationRemarkEmitterAnalysis>(*F);
  };
  auto LookupDomTree = [&FAM](Function &F) -> DominatorTree & {
    return FAM.getResult<DominatorTreeAnalysis>(F);
  };
  if (!DevirtModule(M, AARGetter, OREGetter, LookupDomTree, ExportSummary,
                    ImportSummary)
           .run())
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

bool DevirtModule::runForTesting(
    Module &M, function_ref<AAResults &(Function &)> AARGetter,
    function_ref<OptimizationRemarkEmitter &(Function *)> OREGetter,
    function_ref<DominatorTree &(Function &)> LookupDomTree) {
  ModuleSummaryIndex Summary(/*HaveGVs=*/false);

  // Handle the command-line summary arguments. This code is for testing
  // purposes only, so we handle errors directly.
  if (!ClReadSummary.empty()) {
    ExitOnError ExitOnErr("-wholeprogramdevirt-read-summary: " + ClReadSummary +
                          ": ");
    auto ReadSummaryFile =
        ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(ClReadSummary)));

    yaml::Input In(ReadSummaryFile->getBuffer());
    In >> Summary;
    ExitOnErr(errorCodeToError(In.error()));
  }

  bool Changed =
      DevirtModule(
          M, AARGetter, OREGetter, LookupDomTree,
          ClSummaryAction == PassSummaryAction::Export ? &Summary : nullptr,
          ClSummaryAction == PassSummaryAction::Import ? &Summary : nullptr)
          .run();

  if (!ClWriteSummary.empty()) {
    ExitOnError ExitOnErr(
        "-wholeprogramdevirt-write-summary: " + ClWriteSummary + ": ");
    std::error_code EC;
    raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::F_Text);
    ExitOnErr(errorCodeToError(EC));

    yaml::Output Out(OS);
    Out << Summary;
  }

  return Changed;
}

void DevirtModule::buildTypeIdentifierMap(
    std::vector<VTableBits> &Bits,
    DenseMap<Metadata *, std::set<TypeMemberInfo>> &TypeIdMap) {
  DenseMap<GlobalVariable *, VTableBits *> GVToBits;
  Bits.reserve(M.getGlobalList().size());
  SmallVector<MDNode *, 2> Types;
  for (GlobalVariable &GV : M.globals()) {
    Types.clear();
    GV.getMetadata(LLVMContext::MD_type, Types);
    if (GV.isDeclaration() || Types.empty())
      continue;

    VTableBits *&BitsPtr = GVToBits[&GV];
    if (!BitsPtr) {
      Bits.emplace_back();
      Bits.back().GV = &GV;
      Bits.back().ObjectSize =
          M.getDataLayout().getTypeAllocSize(GV.getInitializer()->getType());
      BitsPtr = &Bits.back();
    }

    for (MDNode *Type : Types) {
      auto TypeID = Type->getOperand(1).get();

      uint64_t Offset =
          cast<ConstantInt>(
              cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
              ->getZExtValue();

      TypeIdMap[TypeID].insert({BitsPtr, Offset});
    }
  }
}

Constant *DevirtModule::getPointerAtOffset(Constant *I, uint64_t Offset) {
  if (I->getType()->isPointerTy()) {
    if (Offset == 0)
      return I;
    return nullptr;
  }

  const DataLayout &DL = M.getDataLayout();

  if (auto *C = dyn_cast<ConstantStruct>(I)) {
    const StructLayout *SL = DL.getStructLayout(C->getType());
    if (Offset >= SL->getSizeInBytes())
      return nullptr;

    unsigned Op = SL->getElementContainingOffset(Offset);
    return getPointerAtOffset(cast<Constant>(I->getOperand(Op)),
                              Offset - SL->getElementOffset(Op));
  }
  if (auto *C = dyn_cast<ConstantArray>(I)) {
    ArrayType *VTableTy = C->getType();
    uint64_t ElemSize = DL.getTypeAllocSize(VTableTy->getElementType());

    unsigned Op = Offset / ElemSize;
    if (Op >= C->getNumOperands())
      return nullptr;

    return getPointerAtOffset(cast<Constant>(I->getOperand(Op)),
                              Offset % ElemSize);
  }
  return nullptr;
}

bool DevirtModule::tryFindVirtualCallTargets(
    std::vector<VirtualCallTarget> &TargetsForSlot,
    const std::set<TypeMemberInfo> &TypeMemberInfos, uint64_t ByteOffset) {
  for (const TypeMemberInfo &TM : TypeMemberInfos) {
    if (!TM.Bits->GV->isConstant())
      return false;

    Constant *Ptr = getPointerAtOffset(TM.Bits->GV->getInitializer(),
                                       TM.Offset + ByteOffset);
    if (!Ptr)
      return false;

    auto Fn = dyn_cast<Function>(Ptr->stripPointerCasts());
    if (!Fn)
      return false;

    // We can disregard __cxa_pure_virtual as a possible call target, as
    // calls to pure virtuals are UB.
    if (Fn->getName() == "__cxa_pure_virtual")
      continue;

    TargetsForSlot.push_back({Fn, &TM});
  }

  // Give up if we couldn't find any targets.
  return !TargetsForSlot.empty();
}

void DevirtModule::applySingleImplDevirt(VTableSlotInfo &SlotInfo,
                                         Constant *TheFn, bool &IsExported) {
  auto Apply = [&](CallSiteInfo &CSInfo) {
    for (auto &&VCallSite : CSInfo.CallSites) {
      if (RemarksEnabled)
        VCallSite.emitRemark("single-impl",
                             TheFn->stripPointerCasts()->getName(), OREGetter);
      VCallSite.CS.setCalledFunction(ConstantExpr::getBitCast(
          TheFn, VCallSite.CS.getCalledValue()->getType()));
      // This use is no longer unsafe.
      if (VCallSite.NumUnsafeUses)
        --*VCallSite.NumUnsafeUses;
    }
    if (CSInfo.isExported())
      IsExported = true;
    CSInfo.markDevirt();
  };
  Apply(SlotInfo.CSInfo);
  for (auto &P : SlotInfo.ConstCSInfo)
    Apply(P.second);
}

bool DevirtModule::trySingleImplDevirt(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot,
    VTableSlotInfo &SlotInfo, WholeProgramDevirtResolution *Res) {
  // See if the program contains a single implementation of this virtual
  // function.
  Function *TheFn = TargetsForSlot[0].Fn;
  for (auto &&Target : TargetsForSlot)
    if (TheFn != Target.Fn)
      return false;

  // If so, update each call site to call that implementation directly.
  if (RemarksEnabled)
    TargetsForSlot[0].WasDevirt = true;

  bool IsExported = false;
  applySingleImplDevirt(SlotInfo, TheFn, IsExported);
  if (!IsExported)
    return false;

  // If the only implementation has local linkage, we must promote to external
  // to make it visible to thin LTO objects. We can only get here during the
  // ThinLTO export phase.
  if (TheFn->hasLocalLinkage()) {
    std::string NewName = (TheFn->getName() + "$merged").str();

    // Since we are renaming the function, any comdats with the same name must
    // also be renamed. This is required when targeting COFF, as the comdat name
    // must match one of the names of the symbols in the comdat.
    if (Comdat *C = TheFn->getComdat()) {
      if (C->getName() == TheFn->getName()) {
        Comdat *NewC = M.getOrInsertComdat(NewName);
        NewC->setSelectionKind(C->getSelectionKind());
        for (GlobalObject &GO : M.global_objects())
          if (GO.getComdat() == C)
            GO.setComdat(NewC);
      }
    }

    TheFn->setLinkage(GlobalValue::ExternalLinkage);
    TheFn->setVisibility(GlobalValue::HiddenVisibility);
    TheFn->setName(NewName);
  }

  Res->TheKind = WholeProgramDevirtResolution::SingleImpl;
  Res->SingleImplName = TheFn->getName();

  return true;
}

void DevirtModule::tryICallBranchFunnel(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, VTableSlotInfo &SlotInfo,
    WholeProgramDevirtResolution *Res, VTableSlot Slot) {
  Triple T(M.getTargetTriple());
  if (T.getArch() != Triple::x86_64)
    return;

  if (TargetsForSlot.size() > ClThreshold)
    return;

  bool HasNonDevirt = !SlotInfo.CSInfo.AllCallSitesDevirted;
  if (!HasNonDevirt)
    for (auto &P : SlotInfo.ConstCSInfo)
      if (!P.second.AllCallSitesDevirted) {
        HasNonDevirt = true;
        break;
      }

  if (!HasNonDevirt)
    return;

  FunctionType *FT =
      FunctionType::get(Type::getVoidTy(M.getContext()), {Int8PtrTy}, true);
  Function *JT;
  if (isa<MDString>(Slot.TypeID)) {
    JT = Function::Create(FT, Function::ExternalLinkage,
                          M.getDataLayout().getProgramAddressSpace(),
                          getGlobalName(Slot, {}, "branch_funnel"), &M);
    JT->setVisibility(GlobalValue::HiddenVisibility);
  } else {
    JT = Function::Create(FT, Function::InternalLinkage,
                          M.getDataLayout().getProgramAddressSpace(),
                          "branch_funnel", &M);
  }
  JT->addAttribute(1, Attribute::Nest);

  std::vector<Value *> JTArgs;
  JTArgs.push_back(JT->arg_begin());
  for (auto &T : TargetsForSlot) {
    JTArgs.push_back(getMemberAddr(T.TM));
    JTArgs.push_back(T.Fn);
  }

  BasicBlock *BB = BasicBlock::Create(M.getContext(), "", JT, nullptr);
  Constant *Intr =
      Intrinsic::getDeclaration(&M, llvm::Intrinsic::icall_branch_funnel, {});

  auto *CI = CallInst::Create(Intr, JTArgs, "", BB);
  CI->setTailCallKind(CallInst::TCK_MustTail);
  ReturnInst::Create(M.getContext(), nullptr, BB);

  bool IsExported = false;
  applyICallBranchFunnel(SlotInfo, JT, IsExported);
  if (IsExported)
    Res->TheKind = WholeProgramDevirtResolution::BranchFunnel;
}

void DevirtModule::applyICallBranchFunnel(VTableSlotInfo &SlotInfo,
                                          Constant *JT, bool &IsExported) {
  auto Apply = [&](CallSiteInfo &CSInfo) {
    if (CSInfo.isExported())
      IsExported = true;
    if (CSInfo.AllCallSitesDevirted)
      return;
    for (auto &&VCallSite : CSInfo.CallSites) {
      CallSite CS = VCallSite.CS;

      // Jump tables are only profitable if the retpoline mitigation is enabled.
      Attribute FSAttr = CS.getCaller()->getFnAttribute("target-features");
      if (FSAttr.hasAttribute(Attribute::None) ||
          !FSAttr.getValueAsString().contains("+retpoline"))
        continue;

      if (RemarksEnabled)
        VCallSite.emitRemark("branch-funnel",
                             JT->stripPointerCasts()->getName(), OREGetter);

      // Pass the address of the vtable in the nest register, which is r10 on
      // x86_64.
      std::vector<Type *> NewArgs;
      NewArgs.push_back(Int8PtrTy);
      for (Type *T : CS.getFunctionType()->params())
        NewArgs.push_back(T);
      PointerType *NewFT = PointerType::getUnqual(
          FunctionType::get(CS.getFunctionType()->getReturnType(), NewArgs,
                            CS.getFunctionType()->isVarArg()));

      IRBuilder<> IRB(CS.getInstruction());
      std::vector<Value *> Args;
      Args.push_back(IRB.CreateBitCast(VCallSite.VTable, Int8PtrTy));
      for (unsigned I = 0; I != CS.getNumArgOperands(); ++I)
        Args.push_back(CS.getArgOperand(I));

      CallSite NewCS;
      if (CS.isCall())
        NewCS = IRB.CreateCall(IRB.CreateBitCast(JT, NewFT), Args);
      else
        NewCS = IRB.CreateInvoke(
            IRB.CreateBitCast(JT, NewFT),
            cast<InvokeInst>(CS.getInstruction())->getNormalDest(),
            cast<InvokeInst>(CS.getInstruction())->getUnwindDest(), Args);
      NewCS.setCallingConv(CS.getCallingConv());

      AttributeList Attrs = CS.getAttributes();
      std::vector<AttributeSet> NewArgAttrs;
      NewArgAttrs.push_back(AttributeSet::get(
          M.getContext(), ArrayRef<Attribute>{Attribute::get(
                              M.getContext(), Attribute::Nest)}));
      for (unsigned I = 0; I + 2 <  Attrs.getNumAttrSets(); ++I)
        NewArgAttrs.push_back(Attrs.getParamAttributes(I));
      NewCS.setAttributes(
          AttributeList::get(M.getContext(), Attrs.getFnAttributes(),
                             Attrs.getRetAttributes(), NewArgAttrs));

      CS->replaceAllUsesWith(NewCS.getInstruction());
      CS->eraseFromParent();

      // This use is no longer unsafe.
      if (VCallSite.NumUnsafeUses)
        --*VCallSite.NumUnsafeUses;
    }
    // Don't mark as devirtualized because there may be callers compiled without
    // retpoline mitigation, which would mean that they are lowered to
    // llvm.type.test and therefore require an llvm.type.test resolution for the
    // type identifier.
  };
  Apply(SlotInfo.CSInfo);
  for (auto &P : SlotInfo.ConstCSInfo)
    Apply(P.second);
}

bool DevirtModule::tryEvaluateFunctionsWithArgs(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot,
    ArrayRef<uint64_t> Args) {
  // Evaluate each function and store the result in each target's RetVal
  // field.
  for (VirtualCallTarget &Target : TargetsForSlot) {
    if (Target.Fn->arg_size() != Args.size() + 1)
      return false;

    Evaluator Eval(M.getDataLayout(), nullptr);
    SmallVector<Constant *, 2> EvalArgs;
    EvalArgs.push_back(
        Constant::getNullValue(Target.Fn->getFunctionType()->getParamType(0)));
    for (unsigned I = 0; I != Args.size(); ++I) {
      auto *ArgTy = dyn_cast<IntegerType>(
          Target.Fn->getFunctionType()->getParamType(I + 1));
      if (!ArgTy)
        return false;
      EvalArgs.push_back(ConstantInt::get(ArgTy, Args[I]));
    }

    Constant *RetVal;
    if (!Eval.EvaluateFunction(Target.Fn, RetVal, EvalArgs) ||
        !isa<ConstantInt>(RetVal))
      return false;
    Target.RetVal = cast<ConstantInt>(RetVal)->getZExtValue();
  }
  return true;
}

void DevirtModule::applyUniformRetValOpt(CallSiteInfo &CSInfo, StringRef FnName,
                                         uint64_t TheRetVal) {
  for (auto Call : CSInfo.CallSites)
    Call.replaceAndErase(
        "uniform-ret-val", FnName, RemarksEnabled, OREGetter,
        ConstantInt::get(cast<IntegerType>(Call.CS.getType()), TheRetVal));
  CSInfo.markDevirt();
}

bool DevirtModule::tryUniformRetValOpt(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, CallSiteInfo &CSInfo,
    WholeProgramDevirtResolution::ByArg *Res) {
  // Uniform return value optimization. If all functions return the same
  // constant, replace all calls with that constant.
  uint64_t TheRetVal = TargetsForSlot[0].RetVal;
  for (const VirtualCallTarget &Target : TargetsForSlot)
    if (Target.RetVal != TheRetVal)
      return false;

  if (CSInfo.isExported()) {
    Res->TheKind = WholeProgramDevirtResolution::ByArg::UniformRetVal;
    Res->Info = TheRetVal;
  }

  applyUniformRetValOpt(CSInfo, TargetsForSlot[0].Fn->getName(), TheRetVal);
  if (RemarksEnabled)
    for (auto &&Target : TargetsForSlot)
      Target.WasDevirt = true;
  return true;
}

std::string DevirtModule::getGlobalName(VTableSlot Slot,
                                        ArrayRef<uint64_t> Args,
                                        StringRef Name) {
  std::string FullName = "__typeid_";
  raw_string_ostream OS(FullName);
  OS << cast<MDString>(Slot.TypeID)->getString() << '_' << Slot.ByteOffset;
  for (uint64_t Arg : Args)
    OS << '_' << Arg;
  OS << '_' << Name;
  return OS.str();
}

bool DevirtModule::shouldExportConstantsAsAbsoluteSymbols() {
  Triple T(M.getTargetTriple());
  return (T.getArch() == Triple::x86 || T.getArch() == Triple::x86_64) &&
         T.getObjectFormat() == Triple::ELF;
}

void DevirtModule::exportGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                StringRef Name, Constant *C) {
  GlobalAlias *GA = GlobalAlias::create(Int8Ty, 0, GlobalValue::ExternalLinkage,
                                        getGlobalName(Slot, Args, Name), C, &M);
  GA->setVisibility(GlobalValue::HiddenVisibility);
}

void DevirtModule::exportConstant(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                  StringRef Name, uint32_t Const,
                                  uint32_t &Storage) {
  if (shouldExportConstantsAsAbsoluteSymbols()) {
    exportGlobal(
        Slot, Args, Name,
        ConstantExpr::getIntToPtr(ConstantInt::get(Int32Ty, Const), Int8PtrTy));
    return;
  }

  Storage = Const;
}

Constant *DevirtModule::importGlobal(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                     StringRef Name) {
  Constant *C = M.getOrInsertGlobal(getGlobalName(Slot, Args, Name), Int8Ty);
  auto *GV = dyn_cast<GlobalVariable>(C);
  if (GV)
    GV->setVisibility(GlobalValue::HiddenVisibility);
  return C;
}

Constant *DevirtModule::importConstant(VTableSlot Slot, ArrayRef<uint64_t> Args,
                                       StringRef Name, IntegerType *IntTy,
                                       uint32_t Storage) {
  if (!shouldExportConstantsAsAbsoluteSymbols())
    return ConstantInt::get(IntTy, Storage);

  Constant *C = importGlobal(Slot, Args, Name);
  auto *GV = cast<GlobalVariable>(C->stripPointerCasts());
  C = ConstantExpr::getPtrToInt(C, IntTy);

  // We only need to set metadata if the global is newly created, in which
  // case it would not have hidden visibility.
  if (GV->hasMetadata(LLVMContext::MD_absolute_symbol))
    return C;

  auto SetAbsRange = [&](uint64_t Min, uint64_t Max) {
    auto *MinC = ConstantAsMetadata::get(ConstantInt::get(IntPtrTy, Min));
    auto *MaxC = ConstantAsMetadata::get(ConstantInt::get(IntPtrTy, Max));
    GV->setMetadata(LLVMContext::MD_absolute_symbol,
                    MDNode::get(M.getContext(), {MinC, MaxC}));
  };
  unsigned AbsWidth = IntTy->getBitWidth();
  if (AbsWidth == IntPtrTy->getBitWidth())
    SetAbsRange(~0ull, ~0ull); // Full set.
  else
    SetAbsRange(0, 1ull << AbsWidth);
  return C;
}

void DevirtModule::applyUniqueRetValOpt(CallSiteInfo &CSInfo, StringRef FnName,
                                        bool IsOne,
                                        Constant *UniqueMemberAddr) {
  for (auto &&Call : CSInfo.CallSites) {
    IRBuilder<> B(Call.CS.getInstruction());
    Value *Cmp =
        B.CreateICmp(IsOne ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE,
                     B.CreateBitCast(Call.VTable, Int8PtrTy), UniqueMemberAddr);
    Cmp = B.CreateZExt(Cmp, Call.CS->getType());
    Call.replaceAndErase("unique-ret-val", FnName, RemarksEnabled, OREGetter,
                         Cmp);
  }
  CSInfo.markDevirt();
}

Constant *DevirtModule::getMemberAddr(const TypeMemberInfo *M) {
  Constant *C = ConstantExpr::getBitCast(M->Bits->GV, Int8PtrTy);
  return ConstantExpr::getGetElementPtr(Int8Ty, C,
                                        ConstantInt::get(Int64Ty, M->Offset));
}

bool DevirtModule::tryUniqueRetValOpt(
    unsigned BitWidth, MutableArrayRef<VirtualCallTarget> TargetsForSlot,
    CallSiteInfo &CSInfo, WholeProgramDevirtResolution::ByArg *Res,
    VTableSlot Slot, ArrayRef<uint64_t> Args) {
  // IsOne controls whether we look for a 0 or a 1.
  auto tryUniqueRetValOptFor = [&](bool IsOne) {
    const TypeMemberInfo *UniqueMember = nullptr;
    for (const VirtualCallTarget &Target : TargetsForSlot) {
      if (Target.RetVal == (IsOne ? 1 : 0)) {
        if (UniqueMember)
          return false;
        UniqueMember = Target.TM;
      }
    }

    // We should have found a unique member or bailed out by now. We already
    // checked for a uniform return value in tryUniformRetValOpt.
    assert(UniqueMember);

    Constant *UniqueMemberAddr = getMemberAddr(UniqueMember);
    if (CSInfo.isExported()) {
      Res->TheKind = WholeProgramDevirtResolution::ByArg::UniqueRetVal;
      Res->Info = IsOne;

      exportGlobal(Slot, Args, "unique_member", UniqueMemberAddr);
    }

    // Replace each call with the comparison.
    applyUniqueRetValOpt(CSInfo, TargetsForSlot[0].Fn->getName(), IsOne,
                         UniqueMemberAddr);

    // Update devirtualization statistics for targets.
    if (RemarksEnabled)
      for (auto &&Target : TargetsForSlot)
        Target.WasDevirt = true;

    return true;
  };

  if (BitWidth == 1) {
    if (tryUniqueRetValOptFor(true))
      return true;
    if (tryUniqueRetValOptFor(false))
      return true;
  }
  return false;
}

void DevirtModule::applyVirtualConstProp(CallSiteInfo &CSInfo, StringRef FnName,
                                         Constant *Byte, Constant *Bit) {
  for (auto Call : CSInfo.CallSites) {
    auto *RetType = cast<IntegerType>(Call.CS.getType());
    IRBuilder<> B(Call.CS.getInstruction());
    Value *Addr =
        B.CreateGEP(Int8Ty, B.CreateBitCast(Call.VTable, Int8PtrTy), Byte);
    if (RetType->getBitWidth() == 1) {
      Value *Bits = B.CreateLoad(Addr);
      Value *BitsAndBit = B.CreateAnd(Bits, Bit);
      auto IsBitSet = B.CreateICmpNE(BitsAndBit, ConstantInt::get(Int8Ty, 0));
      Call.replaceAndErase("virtual-const-prop-1-bit", FnName, RemarksEnabled,
                           OREGetter, IsBitSet);
    } else {
      Value *ValAddr = B.CreateBitCast(Addr, RetType->getPointerTo());
      Value *Val = B.CreateLoad(RetType, ValAddr);
      Call.replaceAndErase("virtual-const-prop", FnName, RemarksEnabled,
                           OREGetter, Val);
    }
  }
  CSInfo.markDevirt();
}

bool DevirtModule::tryVirtualConstProp(
    MutableArrayRef<VirtualCallTarget> TargetsForSlot, VTableSlotInfo &SlotInfo,
    WholeProgramDevirtResolution *Res, VTableSlot Slot) {
  // This only works if the function returns an integer.
  auto RetType = dyn_cast<IntegerType>(TargetsForSlot[0].Fn->getReturnType());
  if (!RetType)
    return false;
  unsigned BitWidth = RetType->getBitWidth();
  if (BitWidth > 64)
    return false;

  // Make sure that each function is defined, does not access memory, takes at
  // least one argument, does not use its first argument (which we assume is
  // 'this'), and has the same return type.
  //
  // Note that we test whether this copy of the function is readnone, rather
  // than testing function attributes, which must hold for any copy of the
  // function, even a less optimized version substituted at link time. This is
  // sound because the virtual constant propagation optimizations effectively
  // inline all implementations of the virtual function into each call site,
  // rather than using function attributes to perform local optimization.
  for (VirtualCallTarget &Target : TargetsForSlot) {
    if (Target.Fn->isDeclaration() ||
        computeFunctionBodyMemoryAccess(*Target.Fn, AARGetter(*Target.Fn)) !=
            MAK_ReadNone ||
        Target.Fn->arg_empty() || !Target.Fn->arg_begin()->use_empty() ||
        Target.Fn->getReturnType() != RetType)
      return false;
  }

  for (auto &&CSByConstantArg : SlotInfo.ConstCSInfo) {
    if (!tryEvaluateFunctionsWithArgs(TargetsForSlot, CSByConstantArg.first))
      continue;

    WholeProgramDevirtResolution::ByArg *ResByArg = nullptr;
    if (Res)
      ResByArg = &Res->ResByArg[CSByConstantArg.first];

    if (tryUniformRetValOpt(TargetsForSlot, CSByConstantArg.second, ResByArg))
      continue;

    if (tryUniqueRetValOpt(BitWidth, TargetsForSlot, CSByConstantArg.second,
                           ResByArg, Slot, CSByConstantArg.first))
      continue;

    // Find an allocation offset in bits in all vtables associated with the
    // type.
    uint64_t AllocBefore =
        findLowestOffset(TargetsForSlot, /*IsAfter=*/false, BitWidth);
    uint64_t AllocAfter =
        findLowestOffset(TargetsForSlot, /*IsAfter=*/true, BitWidth);

    // Calculate the total amount of padding needed to store a value at both
    // ends of the object.
    uint64_t TotalPaddingBefore = 0, TotalPaddingAfter = 0;
    for (auto &&Target : TargetsForSlot) {
      TotalPaddingBefore += std::max<int64_t>(
          (AllocBefore + 7) / 8 - Target.allocatedBeforeBytes() - 1, 0);
      TotalPaddingAfter += std::max<int64_t>(
          (AllocAfter + 7) / 8 - Target.allocatedAfterBytes() - 1, 0);
    }

    // If the amount of padding is too large, give up.
    // FIXME: do something smarter here.
    if (std::min(TotalPaddingBefore, TotalPaddingAfter) > 128)
      continue;

    // Calculate the offset to the value as a (possibly negative) byte offset
    // and (if applicable) a bit offset, and store the values in the targets.
    int64_t OffsetByte;
    uint64_t OffsetBit;
    if (TotalPaddingBefore <= TotalPaddingAfter)
      setBeforeReturnValues(TargetsForSlot, AllocBefore, BitWidth, OffsetByte,
                            OffsetBit);
    else
      setAfterReturnValues(TargetsForSlot, AllocAfter, BitWidth, OffsetByte,
                           OffsetBit);

    if (RemarksEnabled)
      for (auto &&Target : TargetsForSlot)
        Target.WasDevirt = true;


    if (CSByConstantArg.second.isExported()) {
      ResByArg->TheKind = WholeProgramDevirtResolution::ByArg::VirtualConstProp;
      exportConstant(Slot, CSByConstantArg.first, "byte", OffsetByte,
                     ResByArg->Byte);
      exportConstant(Slot, CSByConstantArg.first, "bit", 1ULL << OffsetBit,
                     ResByArg->Bit);
    }

    // Rewrite each call to a load from OffsetByte/OffsetBit.
    Constant *ByteConst = ConstantInt::get(Int32Ty, OffsetByte);
    Constant *BitConst = ConstantInt::get(Int8Ty, 1ULL << OffsetBit);
    applyVirtualConstProp(CSByConstantArg.second,
                          TargetsForSlot[0].Fn->getName(), ByteConst, BitConst);
  }
  return true;
}

void DevirtModule::rebuildGlobal(VTableBits &B) {
  if (B.Before.Bytes.empty() && B.After.Bytes.empty())
    return;

  // Align each byte array to pointer width.
  unsigned PointerSize = M.getDataLayout().getPointerSize();
  B.Before.Bytes.resize(alignTo(B.Before.Bytes.size(), PointerSize));
  B.After.Bytes.resize(alignTo(B.After.Bytes.size(), PointerSize));

  // Before was stored in reverse order; flip it now.
  for (size_t I = 0, Size = B.Before.Bytes.size(); I != Size / 2; ++I)
    std::swap(B.Before.Bytes[I], B.Before.Bytes[Size - 1 - I]);

  // Build an anonymous global containing the before bytes, followed by the
  // original initializer, followed by the after bytes.
  auto NewInit = ConstantStruct::getAnon(
      {ConstantDataArray::get(M.getContext(), B.Before.Bytes),
       B.GV->getInitializer(),
       ConstantDataArray::get(M.getContext(), B.After.Bytes)});
  auto NewGV =
      new GlobalVariable(M, NewInit->getType(), B.GV->isConstant(),
                         GlobalVariable::PrivateLinkage, NewInit, "", B.GV);
  NewGV->setSection(B.GV->getSection());
  NewGV->setComdat(B.GV->getComdat());

  // Copy the original vtable's metadata to the anonymous global, adjusting
  // offsets as required.
  NewGV->copyMetadata(B.GV, B.Before.Bytes.size());

  // Build an alias named after the original global, pointing at the second
  // element (the original initializer).
  auto Alias = GlobalAlias::create(
      B.GV->getInitializer()->getType(), 0, B.GV->getLinkage(), "",
      ConstantExpr::getGetElementPtr(
          NewInit->getType(), NewGV,
          ArrayRef<Constant *>{ConstantInt::get(Int32Ty, 0),
                               ConstantInt::get(Int32Ty, 1)}),
      &M);
  Alias->setVisibility(B.GV->getVisibility());
  Alias->takeName(B.GV);

  B.GV->replaceAllUsesWith(Alias);
  B.GV->eraseFromParent();
}

bool DevirtModule::areRemarksEnabled() {
  const auto &FL = M.getFunctionList();
  for (const Function &Fn : FL) {
    const auto &BBL = Fn.getBasicBlockList();
    if (BBL.empty())
      continue;
    auto DI = OptimizationRemark(DEBUG_TYPE, "", DebugLoc(), &BBL.front());
    return DI.isEnabled();
  }
  return false;
}

void DevirtModule::scanTypeTestUsers(Function *TypeTestFunc,
                                     Function *AssumeFunc) {
  // Find all virtual calls via a virtual table pointer %p under an assumption
  // of the form llvm.assume(llvm.type.test(%p, %md)). This indicates that %p
  // points to a member of the type identifier %md. Group calls by (type ID,
  // offset) pair (effectively the identity of the virtual function) and store
  // to CallSlots.
  DenseSet<CallSite> SeenCallSites;
  for (auto I = TypeTestFunc->use_begin(), E = TypeTestFunc->use_end();
       I != E;) {
    auto CI = dyn_cast<CallInst>(I->getUser());
    ++I;
    if (!CI)
      continue;

    // Search for virtual calls based on %p and add them to DevirtCalls.
    SmallVector<DevirtCallSite, 1> DevirtCalls;
    SmallVector<CallInst *, 1> Assumes;
    auto &DT = LookupDomTree(*CI->getFunction());
    findDevirtualizableCallsForTypeTest(DevirtCalls, Assumes, CI, DT);

    // If we found any, add them to CallSlots.
    if (!Assumes.empty()) {
      Metadata *TypeId =
          cast<MetadataAsValue>(CI->getArgOperand(1))->getMetadata();
      Value *Ptr = CI->getArgOperand(0)->stripPointerCasts();
      for (DevirtCallSite Call : DevirtCalls) {
        // Only add this CallSite if we haven't seen it before. The vtable
        // pointer may have been CSE'd with pointers from other call sites,
        // and we don't want to process call sites multiple times. We can't
        // just skip the vtable Ptr if it has been seen before, however, since
        // it may be shared by type tests that dominate different calls.
        if (SeenCallSites.insert(Call.CS).second)
          CallSlots[{TypeId, Call.Offset}].addCallSite(Ptr, Call.CS, nullptr);
      }
    }

    // We no longer need the assumes or the type test.
    for (auto Assume : Assumes)
      Assume->eraseFromParent();
    // We can't use RecursivelyDeleteTriviallyDeadInstructions here because we
    // may use the vtable argument later.
    if (CI->use_empty())
      CI->eraseFromParent();
  }
}

void DevirtModule::scanTypeCheckedLoadUsers(Function *TypeCheckedLoadFunc) {
  Function *TypeTestFunc = Intrinsic::getDeclaration(&M, Intrinsic::type_test);

  for (auto I = TypeCheckedLoadFunc->use_begin(),
            E = TypeCheckedLoadFunc->use_end();
       I != E;) {
    auto CI = dyn_cast<CallInst>(I->getUser());
    ++I;
    if (!CI)
      continue;

    Value *Ptr = CI->getArgOperand(0);
    Value *Offset = CI->getArgOperand(1);
    Value *TypeIdValue = CI->getArgOperand(2);
    Metadata *TypeId = cast<MetadataAsValue>(TypeIdValue)->getMetadata();

    SmallVector<DevirtCallSite, 1> DevirtCalls;
    SmallVector<Instruction *, 1> LoadedPtrs;
    SmallVector<Instruction *, 1> Preds;
    bool HasNonCallUses = false;
    auto &DT = LookupDomTree(*CI->getFunction());
    findDevirtualizableCallsForTypeCheckedLoad(DevirtCalls, LoadedPtrs, Preds,
                                               HasNonCallUses, CI, DT);

    // Start by generating "pessimistic" code that explicitly loads the function
    // pointer from the vtable and performs the type check. If possible, we will
    // eliminate the load and the type check later.

    // If possible, only generate the load at the point where it is used.
    // This helps avoid unnecessary spills.
    IRBuilder<> LoadB(
        (LoadedPtrs.size() == 1 && !HasNonCallUses) ? LoadedPtrs[0] : CI);
    Value *GEP = LoadB.CreateGEP(Int8Ty, Ptr, Offset);
    Value *GEPPtr = LoadB.CreateBitCast(GEP, PointerType::getUnqual(Int8PtrTy));
    Value *LoadedValue = LoadB.CreateLoad(Int8PtrTy, GEPPtr);

    for (Instruction *LoadedPtr : LoadedPtrs) {
      LoadedPtr->replaceAllUsesWith(LoadedValue);
      LoadedPtr->eraseFromParent();
    }

    // Likewise for the type test.
    IRBuilder<> CallB((Preds.size() == 1 && !HasNonCallUses) ? Preds[0] : CI);
    CallInst *TypeTestCall = CallB.CreateCall(TypeTestFunc, {Ptr, TypeIdValue});

    for (Instruction *Pred : Preds) {
      Pred->replaceAllUsesWith(TypeTestCall);
      Pred->eraseFromParent();
    }

    // We have already erased any extractvalue instructions that refer to the
    // intrinsic call, but the intrinsic may have other non-extractvalue uses
    // (although this is unlikely). In that case, explicitly build a pair and
    // RAUW it.
    if (!CI->use_empty()) {
      Value *Pair = UndefValue::get(CI->getType());
      IRBuilder<> B(CI);
      Pair = B.CreateInsertValue(Pair, LoadedValue, {0});
      Pair = B.CreateInsertValue(Pair, TypeTestCall, {1});
      CI->replaceAllUsesWith(Pair);
    }

    // The number of unsafe uses is initially the number of uses.
    auto &NumUnsafeUses = NumUnsafeUsesForTypeTest[TypeTestCall];
    NumUnsafeUses = DevirtCalls.size();

    // If the function pointer has a non-call user, we cannot eliminate the type
    // check, as one of those users may eventually call the pointer. Increment
    // the unsafe use count to make sure it cannot reach zero.
    if (HasNonCallUses)
      ++NumUnsafeUses;
    for (DevirtCallSite Call : DevirtCalls) {
      CallSlots[{TypeId, Call.Offset}].addCallSite(Ptr, Call.CS,
                                                   &NumUnsafeUses);
    }

    CI->eraseFromParent();
  }
}

void DevirtModule::importResolution(VTableSlot Slot, VTableSlotInfo &SlotInfo) {
  const TypeIdSummary *TidSummary =
      ImportSummary->getTypeIdSummary(cast<MDString>(Slot.TypeID)->getString());
  if (!TidSummary)
    return;
  auto ResI = TidSummary->WPDRes.find(Slot.ByteOffset);
  if (ResI == TidSummary->WPDRes.end())
    return;
  const WholeProgramDevirtResolution &Res = ResI->second;

  if (Res.TheKind == WholeProgramDevirtResolution::SingleImpl) {
    // The type of the function in the declaration is irrelevant because every
    // call site will cast it to the correct type.
    auto *SingleImpl = M.getOrInsertFunction(
        Res.SingleImplName, Type::getVoidTy(M.getContext()));

    // This is the import phase so we should not be exporting anything.
    bool IsExported = false;
    applySingleImplDevirt(SlotInfo, SingleImpl, IsExported);
    assert(!IsExported);
  }

  for (auto &CSByConstantArg : SlotInfo.ConstCSInfo) {
    auto I = Res.ResByArg.find(CSByConstantArg.first);
    if (I == Res.ResByArg.end())
      continue;
    auto &ResByArg = I->second;
    // FIXME: We should figure out what to do about the "function name" argument
    // to the apply* functions, as the function names are unavailable during the
    // importing phase. For now we just pass the empty string. This does not
    // impact correctness because the function names are just used for remarks.
    switch (ResByArg.TheKind) {
    case WholeProgramDevirtResolution::ByArg::UniformRetVal:
      applyUniformRetValOpt(CSByConstantArg.second, "", ResByArg.Info);
      break;
    case WholeProgramDevirtResolution::ByArg::UniqueRetVal: {
      Constant *UniqueMemberAddr =
          importGlobal(Slot, CSByConstantArg.first, "unique_member");
      applyUniqueRetValOpt(CSByConstantArg.second, "", ResByArg.Info,
                           UniqueMemberAddr);
      break;
    }
    case WholeProgramDevirtResolution::ByArg::VirtualConstProp: {
      Constant *Byte = importConstant(Slot, CSByConstantArg.first, "byte",
                                      Int32Ty, ResByArg.Byte);
      Constant *Bit = importConstant(Slot, CSByConstantArg.first, "bit", Int8Ty,
                                     ResByArg.Bit);
      applyVirtualConstProp(CSByConstantArg.second, "", Byte, Bit);
      break;
    }
    default:
      break;
    }
  }

  if (Res.TheKind == WholeProgramDevirtResolution::BranchFunnel) {
    auto *JT = M.getOrInsertFunction(getGlobalName(Slot, {}, "branch_funnel"),
                                     Type::getVoidTy(M.getContext()));
    bool IsExported = false;
    applyICallBranchFunnel(SlotInfo, JT, IsExported);
    assert(!IsExported);
  }
}

void DevirtModule::removeRedundantTypeTests() {
  auto True = ConstantInt::getTrue(M.getContext());
  for (auto &&U : NumUnsafeUsesForTypeTest) {
    if (U.second == 0) {
      U.first->replaceAllUsesWith(True);
      U.first->eraseFromParent();
    }
  }
}

bool DevirtModule::run() {
  Function *TypeTestFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_test));
  Function *TypeCheckedLoadFunc =
      M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load));
  Function *AssumeFunc = M.getFunction(Intrinsic::getName(Intrinsic::assume));

  // If only some of the modules were split, we cannot correctly handle
  // code that contains type tests or type checked loads.
  if ((ExportSummary && ExportSummary->partiallySplitLTOUnits()) ||
      (ImportSummary && ImportSummary->partiallySplitLTOUnits())) {
    if ((TypeTestFunc && !TypeTestFunc->use_empty()) ||
        (TypeCheckedLoadFunc && !TypeCheckedLoadFunc->use_empty()))
      report_fatal_error("inconsistent LTO Unit splitting with llvm.type.test "
                         "or llvm.type.checked.load");
    return false;
  }

  // Normally if there are no users of the devirtualization intrinsics in the
  // module, this pass has nothing to do. But if we are exporting, we also need
  // to handle any users that appear only in the function summaries.
  if (!ExportSummary &&
      (!TypeTestFunc || TypeTestFunc->use_empty() || !AssumeFunc ||
       AssumeFunc->use_empty()) &&
      (!TypeCheckedLoadFunc || TypeCheckedLoadFunc->use_empty()))
    return false;

  if (TypeTestFunc && AssumeFunc)
    scanTypeTestUsers(TypeTestFunc, AssumeFunc);

  if (TypeCheckedLoadFunc)
    scanTypeCheckedLoadUsers(TypeCheckedLoadFunc);

  if (ImportSummary) {
    for (auto &S : CallSlots)
      importResolution(S.first, S.second);

    removeRedundantTypeTests();

    // The rest of the code is only necessary when exporting or during regular
    // LTO, so we are done.
    return true;
  }

  // Rebuild type metadata into a map for easy lookup.
  std::vector<VTableBits> Bits;
  DenseMap<Metadata *, std::set<TypeMemberInfo>> TypeIdMap;
  buildTypeIdentifierMap(Bits, TypeIdMap);
  if (TypeIdMap.empty())
    return true;

  // Collect information from summary about which calls to try to devirtualize.
  if (ExportSummary) {
    DenseMap<GlobalValue::GUID, TinyPtrVector<Metadata *>> MetadataByGUID;
    for (auto &P : TypeIdMap) {
      if (auto *TypeId = dyn_cast<MDString>(P.first))
        MetadataByGUID[GlobalValue::getGUID(TypeId->getString())].push_back(
            TypeId);
    }

    for (auto &P : *ExportSummary) {
      for (auto &S : P.second.SummaryList) {
        auto *FS = dyn_cast<FunctionSummary>(S.get());
        if (!FS)
          continue;
        // FIXME: Only add live functions.
        for (FunctionSummary::VFuncId VF : FS->type_test_assume_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VF.GUID]) {
            CallSlots[{MD, VF.Offset}]
                .CSInfo.markSummaryHasTypeTestAssumeUsers();
          }
        }
        for (FunctionSummary::VFuncId VF : FS->type_checked_load_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VF.GUID]) {
            CallSlots[{MD, VF.Offset}].CSInfo.addSummaryTypeCheckedLoadUser(FS);
          }
        }
        for (const FunctionSummary::ConstVCall &VC :
             FS->type_test_assume_const_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VC.VFunc.GUID]) {
            CallSlots[{MD, VC.VFunc.Offset}]
                .ConstCSInfo[VC.Args]
                .markSummaryHasTypeTestAssumeUsers();
          }
        }
        for (const FunctionSummary::ConstVCall &VC :
             FS->type_checked_load_const_vcalls()) {
          for (Metadata *MD : MetadataByGUID[VC.VFunc.GUID]) {
            CallSlots[{MD, VC.VFunc.Offset}]
                .ConstCSInfo[VC.Args]
                .addSummaryTypeCheckedLoadUser(FS);
          }
        }
      }
    }
  }

  // For each (type, offset) pair:
  bool DidVirtualConstProp = false;
  std::map<std::string, Function*> DevirtTargets;
  for (auto &S : CallSlots) {
    // Search each of the members of the type identifier for the virtual
    // function implementation at offset S.first.ByteOffset, and add to
    // TargetsForSlot.
    std::vector<VirtualCallTarget> TargetsForSlot;
    if (tryFindVirtualCallTargets(TargetsForSlot, TypeIdMap[S.first.TypeID],
                                  S.first.ByteOffset)) {
      WholeProgramDevirtResolution *Res = nullptr;
      if (ExportSummary && isa<MDString>(S.first.TypeID))
        Res = &ExportSummary
                   ->getOrInsertTypeIdSummary(
                       cast<MDString>(S.first.TypeID)->getString())
                   .WPDRes[S.first.ByteOffset];

      if (!trySingleImplDevirt(TargetsForSlot, S.second, Res)) {
        DidVirtualConstProp |=
            tryVirtualConstProp(TargetsForSlot, S.second, Res, S.first);

        tryICallBranchFunnel(TargetsForSlot, S.second, Res, S.first);
      }

      // Collect functions devirtualized at least for one call site for stats.
      if (RemarksEnabled)
        for (const auto &T : TargetsForSlot)
          if (T.WasDevirt)
            DevirtTargets[T.Fn->getName()] = T.Fn;
    }

    // CFI-specific: if we are exporting and any llvm.type.checked.load
    // intrinsics were *not* devirtualized, we need to add the resulting
    // llvm.type.test intrinsics to the function summaries so that the
    // LowerTypeTests pass will export them.
    if (ExportSummary && isa<MDString>(S.first.TypeID)) {
      auto GUID =
          GlobalValue::getGUID(cast<MDString>(S.first.TypeID)->getString());
      for (auto FS : S.second.CSInfo.SummaryTypeCheckedLoadUsers)
        FS->addTypeTest(GUID);
      for (auto &CCS : S.second.ConstCSInfo)
        for (auto FS : CCS.second.SummaryTypeCheckedLoadUsers)
          FS->addTypeTest(GUID);
    }
  }

  if (RemarksEnabled) {
    // Generate remarks for each devirtualized function.
    for (const auto &DT : DevirtTargets) {
      Function *F = DT.second;

      using namespace ore;
      OREGetter(F).emit(OptimizationRemark(DEBUG_TYPE, "Devirtualized", F)
                        << "devirtualized "
                        << NV("FunctionName", F->getName()));
    }
  }

  removeRedundantTypeTests();

  // Rebuild each global we touched as part of virtual constant propagation to
  // include the before and after bytes.
  if (DidVirtualConstProp)
    for (VTableBits &B : Bits)
      rebuildGlobal(B);

  return true;
}
