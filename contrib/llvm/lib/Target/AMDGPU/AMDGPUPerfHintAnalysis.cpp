//===- AMDGPUPerfHintAnalysis.cpp - analysis of functions memory traffic --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Analyzes if a function potentially memory bound and if a kernel
/// kernel may benefit from limiting number of waves to reduce cache thrashing.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-perf-hint"

static cl::opt<unsigned>
    MemBoundThresh("amdgpu-membound-threshold", cl::init(50), cl::Hidden,
                   cl::desc("Function mem bound threshold in %"));

static cl::opt<unsigned>
    LimitWaveThresh("amdgpu-limit-wave-threshold", cl::init(50), cl::Hidden,
                    cl::desc("Kernel limit wave threshold in %"));

static cl::opt<unsigned>
    IAWeight("amdgpu-indirect-access-weight", cl::init(1000), cl::Hidden,
             cl::desc("Indirect access memory instruction weight"));

static cl::opt<unsigned>
    LSWeight("amdgpu-large-stride-weight", cl::init(1000), cl::Hidden,
             cl::desc("Large stride memory access weight"));

static cl::opt<unsigned>
    LargeStrideThresh("amdgpu-large-stride-threshold", cl::init(64), cl::Hidden,
                      cl::desc("Large stride memory access threshold"));

STATISTIC(NumMemBound, "Number of functions marked as memory bound");
STATISTIC(NumLimitWave, "Number of functions marked as needing limit wave");

char llvm::AMDGPUPerfHintAnalysis::ID = 0;
char &llvm::AMDGPUPerfHintAnalysisID = AMDGPUPerfHintAnalysis::ID;

INITIALIZE_PASS(AMDGPUPerfHintAnalysis, DEBUG_TYPE,
                "Analysis if a function is memory bound", true, true)

namespace {

struct AMDGPUPerfHint {
  friend AMDGPUPerfHintAnalysis;

public:
  AMDGPUPerfHint(AMDGPUPerfHintAnalysis::FuncInfoMap &FIM_,
                 const TargetLowering *TLI_)
      : FIM(FIM_), DL(nullptr), TLI(TLI_) {}

  void runOnFunction(Function &F);

private:
  struct MemAccessInfo {
    const Value *V;
    const Value *Base;
    int64_t Offset;
    MemAccessInfo() : V(nullptr), Base(nullptr), Offset(0) {}
    bool isLargeStride(MemAccessInfo &Reference) const;
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    Printable print() const {
      return Printable([this](raw_ostream &OS) {
        OS << "Value: " << *V << '\n'
           << "Base: " << *Base << " Offset: " << Offset << '\n';
      });
    }
#endif
  };

  MemAccessInfo makeMemAccessInfo(Instruction *) const;

  MemAccessInfo LastAccess; // Last memory access info

  AMDGPUPerfHintAnalysis::FuncInfoMap &FIM;

  const DataLayout *DL;

  const TargetLowering *TLI;

  void visit(const Function &F);
  static bool isMemBound(const AMDGPUPerfHintAnalysis::FuncInfo &F);
  static bool needLimitWave(const AMDGPUPerfHintAnalysis::FuncInfo &F);

  bool isIndirectAccess(const Instruction *Inst) const;

  /// Check if the instruction is large stride.
  /// The purpose is to identify memory access pattern like:
  /// x = a[i];
  /// y = a[i+1000];
  /// z = a[i+2000];
  /// In the above example, the second and third memory access will be marked
  /// large stride memory access.
  bool isLargeStride(const Instruction *Inst);

  bool isGlobalAddr(const Value *V) const;
  bool isLocalAddr(const Value *V) const;
  bool isConstantAddr(const Value *V) const;
};

static const Value *getMemoryInstrPtr(const Instruction *Inst) {
  if (auto LI = dyn_cast<LoadInst>(Inst)) {
    return LI->getPointerOperand();
  }
  if (auto SI = dyn_cast<StoreInst>(Inst)) {
    return SI->getPointerOperand();
  }
  if (auto AI = dyn_cast<AtomicCmpXchgInst>(Inst)) {
    return AI->getPointerOperand();
  }
  if (auto AI = dyn_cast<AtomicRMWInst>(Inst)) {
    return AI->getPointerOperand();
  }
  if (auto MI = dyn_cast<AnyMemIntrinsic>(Inst)) {
    return MI->getRawDest();
  }

  return nullptr;
}

bool AMDGPUPerfHint::isIndirectAccess(const Instruction *Inst) const {
  LLVM_DEBUG(dbgs() << "[isIndirectAccess] " << *Inst << '\n');
  SmallSet<const Value *, 32> WorkSet;
  SmallSet<const Value *, 32> Visited;
  if (const Value *MO = getMemoryInstrPtr(Inst)) {
    if (isGlobalAddr(MO))
      WorkSet.insert(MO);
  }

  while (!WorkSet.empty()) {
    const Value *V = *WorkSet.begin();
    WorkSet.erase(*WorkSet.begin());
    if (!Visited.insert(V).second)
      continue;
    LLVM_DEBUG(dbgs() << "  check: " << *V << '\n');

    if (auto LD = dyn_cast<LoadInst>(V)) {
      auto M = LD->getPointerOperand();
      if (isGlobalAddr(M) || isLocalAddr(M) || isConstantAddr(M)) {
        LLVM_DEBUG(dbgs() << "    is IA\n");
        return true;
      }
      continue;
    }

    if (auto GEP = dyn_cast<GetElementPtrInst>(V)) {
      auto P = GEP->getPointerOperand();
      WorkSet.insert(P);
      for (unsigned I = 1, E = GEP->getNumIndices() + 1; I != E; ++I)
        WorkSet.insert(GEP->getOperand(I));
      continue;
    }

    if (auto U = dyn_cast<UnaryInstruction>(V)) {
      WorkSet.insert(U->getOperand(0));
      continue;
    }

    if (auto BO = dyn_cast<BinaryOperator>(V)) {
      WorkSet.insert(BO->getOperand(0));
      WorkSet.insert(BO->getOperand(1));
      continue;
    }

    if (auto S = dyn_cast<SelectInst>(V)) {
      WorkSet.insert(S->getFalseValue());
      WorkSet.insert(S->getTrueValue());
      continue;
    }

    if (auto E = dyn_cast<ExtractElementInst>(V)) {
      WorkSet.insert(E->getVectorOperand());
      continue;
    }

    LLVM_DEBUG(dbgs() << "    dropped\n");
  }

  LLVM_DEBUG(dbgs() << "  is not IA\n");
  return false;
}

void AMDGPUPerfHint::visit(const Function &F) {
  auto FIP = FIM.insert(std::make_pair(&F, AMDGPUPerfHintAnalysis::FuncInfo()));
  if (!FIP.second)
    return;

  AMDGPUPerfHintAnalysis::FuncInfo &FI = FIP.first->second;

  LLVM_DEBUG(dbgs() << "[AMDGPUPerfHint] process " << F.getName() << '\n');

  for (auto &B : F) {
    LastAccess = MemAccessInfo();
    for (auto &I : B) {
      if (getMemoryInstrPtr(&I)) {
        if (isIndirectAccess(&I))
          ++FI.IAMInstCount;
        if (isLargeStride(&I))
          ++FI.LSMInstCount;
        ++FI.MemInstCount;
        ++FI.InstCount;
        continue;
      }
      CallSite CS(const_cast<Instruction *>(&I));
      if (CS) {
        Function *Callee = CS.getCalledFunction();
        if (!Callee || Callee->isDeclaration()) {
          ++FI.InstCount;
          continue;
        }
        if (&F == Callee) // Handle immediate recursion
          continue;

        visit(*Callee);
        auto Loc = FIM.find(Callee);

        assert(Loc != FIM.end() && "No func info");
        FI.MemInstCount += Loc->second.MemInstCount;
        FI.InstCount += Loc->second.InstCount;
        FI.IAMInstCount += Loc->second.IAMInstCount;
        FI.LSMInstCount += Loc->second.LSMInstCount;
      } else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
        TargetLoweringBase::AddrMode AM;
        auto *Ptr = GetPointerBaseWithConstantOffset(GEP, AM.BaseOffs, *DL);
        AM.BaseGV = dyn_cast_or_null<GlobalValue>(const_cast<Value *>(Ptr));
        AM.HasBaseReg = !AM.BaseGV;
        if (TLI->isLegalAddressingMode(*DL, AM, GEP->getResultElementType(),
                                       GEP->getPointerAddressSpace()))
          // Offset will likely be folded into load or store
          continue;
        ++FI.InstCount;
      } else {
        ++FI.InstCount;
      }
    }
  }
}

void AMDGPUPerfHint::runOnFunction(Function &F) {
  if (FIM.find(&F) != FIM.end())
    return;

  const Module &M = *F.getParent();
  DL = &M.getDataLayout();

  visit(F);
  auto Loc = FIM.find(&F);

  assert(Loc != FIM.end() && "No func info");
  LLVM_DEBUG(dbgs() << F.getName() << " MemInst: " << Loc->second.MemInstCount
                    << '\n'
                    << " IAMInst: " << Loc->second.IAMInstCount << '\n'
                    << " LSMInst: " << Loc->second.LSMInstCount << '\n'
                    << " TotalInst: " << Loc->second.InstCount << '\n');

  auto &FI = Loc->second;

  if (isMemBound(FI)) {
    LLVM_DEBUG(dbgs() << F.getName() << " is memory bound\n");
    NumMemBound++;
  }

  if (AMDGPU::isEntryFunctionCC(F.getCallingConv()) && needLimitWave(FI)) {
    LLVM_DEBUG(dbgs() << F.getName() << " needs limit wave\n");
    NumLimitWave++;
  }
}

bool AMDGPUPerfHint::isMemBound(const AMDGPUPerfHintAnalysis::FuncInfo &FI) {
  return FI.MemInstCount * 100 / FI.InstCount > MemBoundThresh;
}

bool AMDGPUPerfHint::needLimitWave(const AMDGPUPerfHintAnalysis::FuncInfo &FI) {
  return ((FI.MemInstCount + FI.IAMInstCount * IAWeight +
           FI.LSMInstCount * LSWeight) *
          100 / FI.InstCount) > LimitWaveThresh;
}

bool AMDGPUPerfHint::isGlobalAddr(const Value *V) const {
  if (auto PT = dyn_cast<PointerType>(V->getType())) {
    unsigned As = PT->getAddressSpace();
    // Flat likely points to global too.
    return As == AMDGPUAS::GLOBAL_ADDRESS || As == AMDGPUAS::FLAT_ADDRESS;
  }
  return false;
}

bool AMDGPUPerfHint::isLocalAddr(const Value *V) const {
  if (auto PT = dyn_cast<PointerType>(V->getType()))
    return PT->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS;
  return false;
}

bool AMDGPUPerfHint::isLargeStride(const Instruction *Inst) {
  LLVM_DEBUG(dbgs() << "[isLargeStride] " << *Inst << '\n');

  MemAccessInfo MAI = makeMemAccessInfo(const_cast<Instruction *>(Inst));
  bool IsLargeStride = MAI.isLargeStride(LastAccess);
  if (MAI.Base)
    LastAccess = std::move(MAI);

  return IsLargeStride;
}

AMDGPUPerfHint::MemAccessInfo
AMDGPUPerfHint::makeMemAccessInfo(Instruction *Inst) const {
  MemAccessInfo MAI;
  const Value *MO = getMemoryInstrPtr(Inst);

  LLVM_DEBUG(dbgs() << "[isLargeStride] MO: " << *MO << '\n');
  // Do not treat local-addr memory access as large stride.
  if (isLocalAddr(MO))
    return MAI;

  MAI.V = MO;
  MAI.Base = GetPointerBaseWithConstantOffset(MO, MAI.Offset, *DL);
  return MAI;
}

bool AMDGPUPerfHint::isConstantAddr(const Value *V) const {
  if (auto PT = dyn_cast<PointerType>(V->getType())) {
    unsigned As = PT->getAddressSpace();
    return As == AMDGPUAS::CONSTANT_ADDRESS ||
           As == AMDGPUAS::CONSTANT_ADDRESS_32BIT;
  }
  return false;
}

bool AMDGPUPerfHint::MemAccessInfo::isLargeStride(
    MemAccessInfo &Reference) const {

  if (!Base || !Reference.Base || Base != Reference.Base)
    return false;

  uint64_t Diff = Offset > Reference.Offset ? Offset - Reference.Offset
                                            : Reference.Offset - Offset;
  bool Result = Diff > LargeStrideThresh;
  LLVM_DEBUG(dbgs() << "[isLargeStride compare]\n"
               << print() << "<=>\n"
               << Reference.print() << "Result:" << Result << '\n');
  return Result;
}
} // namespace

bool AMDGPUPerfHintAnalysis::runOnFunction(Function &F) {
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  const TargetMachine &TM = TPC->getTM<TargetMachine>();
  const TargetSubtargetInfo *ST = TM.getSubtargetImpl(F);

  AMDGPUPerfHint Analyzer(FIM, ST->getTargetLowering());
  Analyzer.runOnFunction(F);
  return false;
}

bool AMDGPUPerfHintAnalysis::isMemoryBound(const Function *F) const {
  auto FI = FIM.find(F);
  if (FI == FIM.end())
    return false;

  return AMDGPUPerfHint::isMemBound(FI->second);
}

bool AMDGPUPerfHintAnalysis::needsWaveLimiter(const Function *F) const {
  auto FI = FIM.find(F);
  if (FI == FIM.end())
    return false;

  return AMDGPUPerfHint::needLimitWave(FI->second);
}
