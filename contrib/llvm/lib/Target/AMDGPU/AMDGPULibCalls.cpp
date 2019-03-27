//===- AMDGPULibCalls.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file does AMD library function optimizations.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "amdgpu-simplifylib"

#include "AMDGPU.h"
#include "AMDGPULibFunc.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include <vector>
#include <cmath>

using namespace llvm;

static cl::opt<bool> EnablePreLink("amdgpu-prelink",
  cl::desc("Enable pre-link mode optimizations"),
  cl::init(false),
  cl::Hidden);

static cl::list<std::string> UseNative("amdgpu-use-native",
  cl::desc("Comma separated list of functions to replace with native, or all"),
  cl::CommaSeparated, cl::ValueOptional,
  cl::Hidden);

#define MATH_PI     3.14159265358979323846264338327950288419716939937511
#define MATH_E      2.71828182845904523536028747135266249775724709369996
#define MATH_SQRT2  1.41421356237309504880168872420969807856967187537695

#define MATH_LOG2E     1.4426950408889634073599246810018921374266459541529859
#define MATH_LOG10E    0.4342944819032518276511289189166050822943970058036665
// Value of log2(10)
#define MATH_LOG2_10   3.3219280948873623478703194294893901758648313930245806
// Value of 1 / log2(10)
#define MATH_RLOG2_10  0.3010299956639811952137388947244930267681898814621085
// Value of 1 / M_LOG2E_F = 1 / log2(e)
#define MATH_RLOG2_E   0.6931471805599453094172321214581765680755001343602552

namespace llvm {

class AMDGPULibCalls {
private:

  typedef llvm::AMDGPULibFunc FuncInfo;

  // -fuse-native.
  bool AllNative = false;

  bool useNativeFunc(const StringRef F) const;

  // Return a pointer (pointer expr) to the function if function defintion with
  // "FuncName" exists. It may create a new function prototype in pre-link mode.
  Constant *getFunction(Module *M, const FuncInfo& fInfo);

  // Replace a normal function with its native version.
  bool replaceWithNative(CallInst *CI, const FuncInfo &FInfo);

  bool parseFunctionName(const StringRef& FMangledName,
                         FuncInfo *FInfo=nullptr /*out*/);

  bool TDOFold(CallInst *CI, const FuncInfo &FInfo);

  /* Specialized optimizations */

  // recip (half or native)
  bool fold_recip(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // divide (half or native)
  bool fold_divide(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // pow/powr/pown
  bool fold_pow(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // rootn
  bool fold_rootn(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // fma/mad
  bool fold_fma_mad(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // -fuse-native for sincos
  bool sincosUseNative(CallInst *aCI, const FuncInfo &FInfo);

  // evaluate calls if calls' arguments are constants.
  bool evaluateScalarMathFunc(FuncInfo &FInfo, double& Res0,
    double& Res1, Constant *copr0, Constant *copr1, Constant *copr2);
  bool evaluateCall(CallInst *aCI, FuncInfo &FInfo);

  // exp
  bool fold_exp(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // exp2
  bool fold_exp2(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // exp10
  bool fold_exp10(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // log
  bool fold_log(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // log2
  bool fold_log2(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // log10
  bool fold_log10(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // sqrt
  bool fold_sqrt(CallInst *CI, IRBuilder<> &B, const FuncInfo &FInfo);

  // sin/cos
  bool fold_sincos(CallInst * CI, IRBuilder<> &B, AliasAnalysis * AA);

  // __read_pipe/__write_pipe
  bool fold_read_write_pipe(CallInst *CI, IRBuilder<> &B, FuncInfo &FInfo);

  // Get insertion point at entry.
  BasicBlock::iterator getEntryIns(CallInst * UI);
  // Insert an Alloc instruction.
  AllocaInst* insertAlloca(CallInst * UI, IRBuilder<> &B, const char *prefix);
  // Get a scalar native builtin signle argument FP function
  Constant* getNativeFunction(Module* M, const FuncInfo &FInfo);

protected:
  CallInst *CI;

  bool isUnsafeMath(const CallInst *CI) const;

  void replaceCall(Value *With) {
    CI->replaceAllUsesWith(With);
    CI->eraseFromParent();
  }

public:
  bool fold(CallInst *CI, AliasAnalysis *AA = nullptr);

  void initNativeFuncs();

  // Replace a normal math function call with that native version
  bool useNative(CallInst *CI);
};

} // end llvm namespace

namespace {

  class AMDGPUSimplifyLibCalls : public FunctionPass {

  AMDGPULibCalls Simplifier;

  const TargetOptions Options;

  public:
    static char ID; // Pass identification

    AMDGPUSimplifyLibCalls(const TargetOptions &Opt = TargetOptions())
      : FunctionPass(ID), Options(Opt) {
      initializeAMDGPUSimplifyLibCallsPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<AAResultsWrapperPass>();
    }

    bool runOnFunction(Function &M) override;
  };

  class AMDGPUUseNativeCalls : public FunctionPass {

  AMDGPULibCalls Simplifier;

  public:
    static char ID; // Pass identification

    AMDGPUUseNativeCalls() : FunctionPass(ID) {
      initializeAMDGPUUseNativeCallsPass(*PassRegistry::getPassRegistry());
      Simplifier.initNativeFuncs();
    }

    bool runOnFunction(Function &F) override;
  };

} // end anonymous namespace.

char AMDGPUSimplifyLibCalls::ID = 0;
char AMDGPUUseNativeCalls::ID = 0;

INITIALIZE_PASS_BEGIN(AMDGPUSimplifyLibCalls, "amdgpu-simplifylib",
                      "Simplify well-known AMD library calls", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(AMDGPUSimplifyLibCalls, "amdgpu-simplifylib",
                    "Simplify well-known AMD library calls", false, false)

INITIALIZE_PASS(AMDGPUUseNativeCalls, "amdgpu-usenative",
                "Replace builtin math calls with that native versions.",
                false, false)

template <typename IRB>
static CallInst *CreateCallEx(IRB &B, Value *Callee, Value *Arg,
                              const Twine &Name = "") {
  CallInst *R = B.CreateCall(Callee, Arg, Name);
  if (Function* F = dyn_cast<Function>(Callee))
    R->setCallingConv(F->getCallingConv());
  return R;
}

template <typename IRB>
static CallInst *CreateCallEx2(IRB &B, Value *Callee, Value *Arg1, Value *Arg2,
                               const Twine &Name = "") {
  CallInst *R = B.CreateCall(Callee, {Arg1, Arg2}, Name);
  if (Function* F = dyn_cast<Function>(Callee))
    R->setCallingConv(F->getCallingConv());
  return R;
}

//  Data structures for table-driven optimizations.
//  FuncTbl works for both f32 and f64 functions with 1 input argument

struct TableEntry {
  double   result;
  double   input;
};

/* a list of {result, input} */
static const TableEntry tbl_acos[] = {
  {MATH_PI/2.0, 0.0},
  {MATH_PI/2.0, -0.0},
  {0.0, 1.0},
  {MATH_PI, -1.0}
};
static const TableEntry tbl_acosh[] = {
  {0.0, 1.0}
};
static const TableEntry tbl_acospi[] = {
  {0.5, 0.0},
  {0.5, -0.0},
  {0.0, 1.0},
  {1.0, -1.0}
};
static const TableEntry tbl_asin[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {MATH_PI/2.0, 1.0},
  {-MATH_PI/2.0, -1.0}
};
static const TableEntry tbl_asinh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_asinpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {0.5, 1.0},
  {-0.5, -1.0}
};
static const TableEntry tbl_atan[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {MATH_PI/4.0, 1.0},
  {-MATH_PI/4.0, -1.0}
};
static const TableEntry tbl_atanh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_atanpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {0.25, 1.0},
  {-0.25, -1.0}
};
static const TableEntry tbl_cbrt[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {1.0, 1.0},
  {-1.0, -1.0},
};
static const TableEntry tbl_cos[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_cosh[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_cospi[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_erfc[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_erf[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_exp[] = {
  {1.0, 0.0},
  {1.0, -0.0},
  {MATH_E, 1.0}
};
static const TableEntry tbl_exp2[] = {
  {1.0, 0.0},
  {1.0, -0.0},
  {2.0, 1.0}
};
static const TableEntry tbl_exp10[] = {
  {1.0, 0.0},
  {1.0, -0.0},
  {10.0, 1.0}
};
static const TableEntry tbl_expm1[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_log[] = {
  {0.0, 1.0},
  {1.0, MATH_E}
};
static const TableEntry tbl_log2[] = {
  {0.0, 1.0},
  {1.0, 2.0}
};
static const TableEntry tbl_log10[] = {
  {0.0, 1.0},
  {1.0, 10.0}
};
static const TableEntry tbl_rsqrt[] = {
  {1.0, 1.0},
  {1.0/MATH_SQRT2, 2.0}
};
static const TableEntry tbl_sin[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_sinh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_sinpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_sqrt[] = {
  {0.0, 0.0},
  {1.0, 1.0},
  {MATH_SQRT2, 2.0}
};
static const TableEntry tbl_tan[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_tanh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_tanpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_tgamma[] = {
  {1.0, 1.0},
  {1.0, 2.0},
  {2.0, 3.0},
  {6.0, 4.0}
};

static bool HasNative(AMDGPULibFunc::EFuncId id) {
  switch(id) {
  case AMDGPULibFunc::EI_DIVIDE:
  case AMDGPULibFunc::EI_COS:
  case AMDGPULibFunc::EI_EXP:
  case AMDGPULibFunc::EI_EXP2:
  case AMDGPULibFunc::EI_EXP10:
  case AMDGPULibFunc::EI_LOG:
  case AMDGPULibFunc::EI_LOG2:
  case AMDGPULibFunc::EI_LOG10:
  case AMDGPULibFunc::EI_POWR:
  case AMDGPULibFunc::EI_RECIP:
  case AMDGPULibFunc::EI_RSQRT:
  case AMDGPULibFunc::EI_SIN:
  case AMDGPULibFunc::EI_SINCOS:
  case AMDGPULibFunc::EI_SQRT:
  case AMDGPULibFunc::EI_TAN:
    return true;
  default:;
  }
  return false;
}

struct TableRef {
  size_t size;
  const TableEntry *table; // variable size: from 0 to (size - 1)

  TableRef() : size(0), table(nullptr) {}

  template <size_t N>
  TableRef(const TableEntry (&tbl)[N]) : size(N), table(&tbl[0]) {}
};

static TableRef getOptTable(AMDGPULibFunc::EFuncId id) {
  switch(id) {
  case AMDGPULibFunc::EI_ACOS:    return TableRef(tbl_acos);
  case AMDGPULibFunc::EI_ACOSH:   return TableRef(tbl_acosh);
  case AMDGPULibFunc::EI_ACOSPI:  return TableRef(tbl_acospi);
  case AMDGPULibFunc::EI_ASIN:    return TableRef(tbl_asin);
  case AMDGPULibFunc::EI_ASINH:   return TableRef(tbl_asinh);
  case AMDGPULibFunc::EI_ASINPI:  return TableRef(tbl_asinpi);
  case AMDGPULibFunc::EI_ATAN:    return TableRef(tbl_atan);
  case AMDGPULibFunc::EI_ATANH:   return TableRef(tbl_atanh);
  case AMDGPULibFunc::EI_ATANPI:  return TableRef(tbl_atanpi);
  case AMDGPULibFunc::EI_CBRT:    return TableRef(tbl_cbrt);
  case AMDGPULibFunc::EI_NCOS:
  case AMDGPULibFunc::EI_COS:     return TableRef(tbl_cos);
  case AMDGPULibFunc::EI_COSH:    return TableRef(tbl_cosh);
  case AMDGPULibFunc::EI_COSPI:   return TableRef(tbl_cospi);
  case AMDGPULibFunc::EI_ERFC:    return TableRef(tbl_erfc);
  case AMDGPULibFunc::EI_ERF:     return TableRef(tbl_erf);
  case AMDGPULibFunc::EI_EXP:     return TableRef(tbl_exp);
  case AMDGPULibFunc::EI_NEXP2:
  case AMDGPULibFunc::EI_EXP2:    return TableRef(tbl_exp2);
  case AMDGPULibFunc::EI_EXP10:   return TableRef(tbl_exp10);
  case AMDGPULibFunc::EI_EXPM1:   return TableRef(tbl_expm1);
  case AMDGPULibFunc::EI_LOG:     return TableRef(tbl_log);
  case AMDGPULibFunc::EI_NLOG2:
  case AMDGPULibFunc::EI_LOG2:    return TableRef(tbl_log2);
  case AMDGPULibFunc::EI_LOG10:   return TableRef(tbl_log10);
  case AMDGPULibFunc::EI_NRSQRT:
  case AMDGPULibFunc::EI_RSQRT:   return TableRef(tbl_rsqrt);
  case AMDGPULibFunc::EI_NSIN:
  case AMDGPULibFunc::EI_SIN:     return TableRef(tbl_sin);
  case AMDGPULibFunc::EI_SINH:    return TableRef(tbl_sinh);
  case AMDGPULibFunc::EI_SINPI:   return TableRef(tbl_sinpi);
  case AMDGPULibFunc::EI_NSQRT:
  case AMDGPULibFunc::EI_SQRT:    return TableRef(tbl_sqrt);
  case AMDGPULibFunc::EI_TAN:     return TableRef(tbl_tan);
  case AMDGPULibFunc::EI_TANH:    return TableRef(tbl_tanh);
  case AMDGPULibFunc::EI_TANPI:   return TableRef(tbl_tanpi);
  case AMDGPULibFunc::EI_TGAMMA:  return TableRef(tbl_tgamma);
  default:;
  }
  return TableRef();
}

static inline int getVecSize(const AMDGPULibFunc& FInfo) {
  return FInfo.getLeads()[0].VectorSize;
}

static inline AMDGPULibFunc::EType getArgType(const AMDGPULibFunc& FInfo) {
  return (AMDGPULibFunc::EType)FInfo.getLeads()[0].ArgType;
}

Constant *AMDGPULibCalls::getFunction(Module *M, const FuncInfo& fInfo) {
  // If we are doing PreLinkOpt, the function is external. So it is safe to
  // use getOrInsertFunction() at this stage.

  return EnablePreLink ? AMDGPULibFunc::getOrInsertFunction(M, fInfo)
                       : AMDGPULibFunc::getFunction(M, fInfo);
}

bool AMDGPULibCalls::parseFunctionName(const StringRef& FMangledName,
                                    FuncInfo *FInfo) {
  return AMDGPULibFunc::parse(FMangledName, *FInfo);
}

bool AMDGPULibCalls::isUnsafeMath(const CallInst *CI) const {
  if (auto Op = dyn_cast<FPMathOperator>(CI))
    if (Op->isFast())
      return true;
  const Function *F = CI->getParent()->getParent();
  Attribute Attr = F->getFnAttribute("unsafe-fp-math");
  return Attr.getValueAsString() == "true";
}

bool AMDGPULibCalls::useNativeFunc(const StringRef F) const {
  return AllNative ||
         std::find(UseNative.begin(), UseNative.end(), F) != UseNative.end();
}

void AMDGPULibCalls::initNativeFuncs() {
  AllNative = useNativeFunc("all") ||
              (UseNative.getNumOccurrences() && UseNative.size() == 1 &&
               UseNative.begin()->empty());
}

bool AMDGPULibCalls::sincosUseNative(CallInst *aCI, const FuncInfo &FInfo) {
  bool native_sin = useNativeFunc("sin");
  bool native_cos = useNativeFunc("cos");

  if (native_sin && native_cos) {
    Module *M = aCI->getModule();
    Value *opr0 = aCI->getArgOperand(0);

    AMDGPULibFunc nf;
    nf.getLeads()[0].ArgType = FInfo.getLeads()[0].ArgType;
    nf.getLeads()[0].VectorSize = FInfo.getLeads()[0].VectorSize;

    nf.setPrefix(AMDGPULibFunc::NATIVE);
    nf.setId(AMDGPULibFunc::EI_SIN);
    Constant *sinExpr = getFunction(M, nf);

    nf.setPrefix(AMDGPULibFunc::NATIVE);
    nf.setId(AMDGPULibFunc::EI_COS);
    Constant *cosExpr = getFunction(M, nf);
    if (sinExpr && cosExpr) {
      Value *sinval = CallInst::Create(sinExpr, opr0, "splitsin", aCI);
      Value *cosval = CallInst::Create(cosExpr, opr0, "splitcos", aCI);
      new StoreInst(cosval, aCI->getArgOperand(1), aCI);

      DEBUG_WITH_TYPE("usenative", dbgs() << "<useNative> replace " << *aCI
                                          << " with native version of sin/cos");

      replaceCall(sinval);
      return true;
    }
  }
  return false;
}

bool AMDGPULibCalls::useNative(CallInst *aCI) {
  CI = aCI;
  Function *Callee = aCI->getCalledFunction();

  FuncInfo FInfo;
  if (!parseFunctionName(Callee->getName(), &FInfo) || !FInfo.isMangled() ||
      FInfo.getPrefix() != AMDGPULibFunc::NOPFX ||
      getArgType(FInfo) == AMDGPULibFunc::F64 || !HasNative(FInfo.getId()) ||
      !(AllNative || useNativeFunc(FInfo.getName()))) {
    return false;
  }

  if (FInfo.getId() == AMDGPULibFunc::EI_SINCOS)
    return sincosUseNative(aCI, FInfo);

  FInfo.setPrefix(AMDGPULibFunc::NATIVE);
  Constant *F = getFunction(aCI->getModule(), FInfo);
  if (!F)
    return false;

  aCI->setCalledFunction(F);
  DEBUG_WITH_TYPE("usenative", dbgs() << "<useNative> replace " << *aCI
                                      << " with native version");
  return true;
}

// Clang emits call of __read_pipe_2 or __read_pipe_4 for OpenCL read_pipe
// builtin, with appended type size and alignment arguments, where 2 or 4
// indicates the original number of arguments. The library has optimized version
// of __read_pipe_2/__read_pipe_4 when the type size and alignment has the same
// power of 2 value. This function transforms __read_pipe_2 to __read_pipe_2_N
// for such cases where N is the size in bytes of the type (N = 1, 2, 4, 8, ...,
// 128). The same for __read_pipe_4, write_pipe_2, and write_pipe_4.
bool AMDGPULibCalls::fold_read_write_pipe(CallInst *CI, IRBuilder<> &B,
                                          FuncInfo &FInfo) {
  auto *Callee = CI->getCalledFunction();
  if (!Callee->isDeclaration())
    return false;

  assert(Callee->hasName() && "Invalid read_pipe/write_pipe function");
  auto *M = Callee->getParent();
  auto &Ctx = M->getContext();
  std::string Name = Callee->getName();
  auto NumArg = CI->getNumArgOperands();
  if (NumArg != 4 && NumArg != 6)
    return false;
  auto *PacketSize = CI->getArgOperand(NumArg - 2);
  auto *PacketAlign = CI->getArgOperand(NumArg - 1);
  if (!isa<ConstantInt>(PacketSize) || !isa<ConstantInt>(PacketAlign))
    return false;
  unsigned Size = cast<ConstantInt>(PacketSize)->getZExtValue();
  unsigned Align = cast<ConstantInt>(PacketAlign)->getZExtValue();
  if (Size != Align || !isPowerOf2_32(Size))
    return false;

  Type *PtrElemTy;
  if (Size <= 8)
    PtrElemTy = Type::getIntNTy(Ctx, Size * 8);
  else
    PtrElemTy = VectorType::get(Type::getInt64Ty(Ctx), Size / 8);
  unsigned PtrArgLoc = CI->getNumArgOperands() - 3;
  auto PtrArg = CI->getArgOperand(PtrArgLoc);
  unsigned PtrArgAS = PtrArg->getType()->getPointerAddressSpace();
  auto *PtrTy = llvm::PointerType::get(PtrElemTy, PtrArgAS);

  SmallVector<llvm::Type *, 6> ArgTys;
  for (unsigned I = 0; I != PtrArgLoc; ++I)
    ArgTys.push_back(CI->getArgOperand(I)->getType());
  ArgTys.push_back(PtrTy);

  Name = Name + "_" + std::to_string(Size);
  auto *FTy = FunctionType::get(Callee->getReturnType(),
                                ArrayRef<Type *>(ArgTys), false);
  AMDGPULibFunc NewLibFunc(Name, FTy);
  auto *F = AMDGPULibFunc::getOrInsertFunction(M, NewLibFunc);
  if (!F)
    return false;

  auto *BCast = B.CreatePointerCast(PtrArg, PtrTy);
  SmallVector<Value *, 6> Args;
  for (unsigned I = 0; I != PtrArgLoc; ++I)
    Args.push_back(CI->getArgOperand(I));
  Args.push_back(BCast);

  auto *NCI = B.CreateCall(F, Args);
  NCI->setAttributes(CI->getAttributes());
  CI->replaceAllUsesWith(NCI);
  CI->dropAllReferences();
  CI->eraseFromParent();

  return true;
}

// This function returns false if no change; return true otherwise.
bool AMDGPULibCalls::fold(CallInst *CI, AliasAnalysis *AA) {
  this->CI = CI;
  Function *Callee = CI->getCalledFunction();

  // Ignore indirect calls.
  if (Callee == 0) return false;

  FuncInfo FInfo;
  if (!parseFunctionName(Callee->getName(), &FInfo))
    return false;

  // Further check the number of arguments to see if they match.
  if (CI->getNumArgOperands() != FInfo.getNumArgs())
    return false;

  BasicBlock *BB = CI->getParent();
  LLVMContext &Context = CI->getParent()->getContext();
  IRBuilder<> B(Context);

  // Set the builder to the instruction after the call.
  B.SetInsertPoint(BB, CI->getIterator());

  // Copy fast flags from the original call.
  if (const FPMathOperator *FPOp = dyn_cast<const FPMathOperator>(CI))
    B.setFastMathFlags(FPOp->getFastMathFlags());

  if (TDOFold(CI, FInfo))
    return true;

  // Under unsafe-math, evaluate calls if possible.
  // According to Brian Sumner, we can do this for all f32 function calls
  // using host's double function calls.
  if (isUnsafeMath(CI) && evaluateCall(CI, FInfo))
    return true;

  // Specilized optimizations for each function call
  switch (FInfo.getId()) {
  case AMDGPULibFunc::EI_RECIP:
    // skip vector function
    assert ((FInfo.getPrefix() == AMDGPULibFunc::NATIVE ||
             FInfo.getPrefix() == AMDGPULibFunc::HALF) &&
            "recip must be an either native or half function");
    return (getVecSize(FInfo) != 1) ? false : fold_recip(CI, B, FInfo);

  case AMDGPULibFunc::EI_DIVIDE:
    // skip vector function
    assert ((FInfo.getPrefix() == AMDGPULibFunc::NATIVE ||
             FInfo.getPrefix() == AMDGPULibFunc::HALF) &&
            "divide must be an either native or half function");
    return (getVecSize(FInfo) != 1) ? false : fold_divide(CI, B, FInfo);

  case AMDGPULibFunc::EI_POW:
  case AMDGPULibFunc::EI_POWR:
  case AMDGPULibFunc::EI_POWN:
    return fold_pow(CI, B, FInfo);

  case AMDGPULibFunc::EI_ROOTN:
    // skip vector function
    return (getVecSize(FInfo) != 1) ? false : fold_rootn(CI, B, FInfo);

  case AMDGPULibFunc::EI_FMA:
  case AMDGPULibFunc::EI_MAD:
  case AMDGPULibFunc::EI_NFMA:
    // skip vector function
    return (getVecSize(FInfo) != 1) ? false : fold_fma_mad(CI, B, FInfo);

  case AMDGPULibFunc::EI_SQRT:
    return isUnsafeMath(CI) && fold_sqrt(CI, B, FInfo);
  case AMDGPULibFunc::EI_COS:
  case AMDGPULibFunc::EI_SIN:
    if ((getArgType(FInfo) == AMDGPULibFunc::F32 ||
         getArgType(FInfo) == AMDGPULibFunc::F64)
        && (FInfo.getPrefix() == AMDGPULibFunc::NOPFX))
      return fold_sincos(CI, B, AA);

    break;
  case AMDGPULibFunc::EI_READ_PIPE_2:
  case AMDGPULibFunc::EI_READ_PIPE_4:
  case AMDGPULibFunc::EI_WRITE_PIPE_2:
  case AMDGPULibFunc::EI_WRITE_PIPE_4:
    return fold_read_write_pipe(CI, B, FInfo);

  default:
    break;
  }

  return false;
}

bool AMDGPULibCalls::TDOFold(CallInst *CI, const FuncInfo &FInfo) {
  // Table-Driven optimization
  const TableRef tr = getOptTable(FInfo.getId());
  if (tr.size==0)
    return false;

  int const sz = (int)tr.size;
  const TableEntry * const ftbl = tr.table;
  Value *opr0 = CI->getArgOperand(0);

  if (getVecSize(FInfo) > 1) {
    if (ConstantDataVector *CV = dyn_cast<ConstantDataVector>(opr0)) {
      SmallVector<double, 0> DVal;
      for (int eltNo = 0; eltNo < getVecSize(FInfo); ++eltNo) {
        ConstantFP *eltval = dyn_cast<ConstantFP>(
                               CV->getElementAsConstant((unsigned)eltNo));
        assert(eltval && "Non-FP arguments in math function!");
        bool found = false;
        for (int i=0; i < sz; ++i) {
          if (eltval->isExactlyValue(ftbl[i].input)) {
            DVal.push_back(ftbl[i].result);
            found = true;
            break;
          }
        }
        if (!found) {
          // This vector constants not handled yet.
          return false;
        }
      }
      LLVMContext &context = CI->getParent()->getParent()->getContext();
      Constant *nval;
      if (getArgType(FInfo) == AMDGPULibFunc::F32) {
        SmallVector<float, 0> FVal;
        for (unsigned i = 0; i < DVal.size(); ++i) {
          FVal.push_back((float)DVal[i]);
        }
        ArrayRef<float> tmp(FVal);
        nval = ConstantDataVector::get(context, tmp);
      } else { // F64
        ArrayRef<double> tmp(DVal);
        nval = ConstantDataVector::get(context, tmp);
      }
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *nval << "\n");
      replaceCall(nval);
      return true;
    }
  } else {
    // Scalar version
    if (ConstantFP *CF = dyn_cast<ConstantFP>(opr0)) {
      for (int i = 0; i < sz; ++i) {
        if (CF->isExactlyValue(ftbl[i].input)) {
          Value *nval = ConstantFP::get(CF->getType(), ftbl[i].result);
          LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *nval << "\n");
          replaceCall(nval);
          return true;
        }
      }
    }
  }

  return false;
}

bool AMDGPULibCalls::replaceWithNative(CallInst *CI, const FuncInfo &FInfo) {
  Module *M = CI->getModule();
  if (getArgType(FInfo) != AMDGPULibFunc::F32 ||
      FInfo.getPrefix() != AMDGPULibFunc::NOPFX ||
      !HasNative(FInfo.getId()))
    return false;

  AMDGPULibFunc nf = FInfo;
  nf.setPrefix(AMDGPULibFunc::NATIVE);
  if (Constant *FPExpr = getFunction(M, nf)) {
    LLVM_DEBUG(dbgs() << "AMDIC: " << *CI << " ---> ");

    CI->setCalledFunction(FPExpr);

    LLVM_DEBUG(dbgs() << *CI << '\n');

    return true;
  }
  return false;
}

//  [native_]half_recip(c) ==> 1.0/c
bool AMDGPULibCalls::fold_recip(CallInst *CI, IRBuilder<> &B,
                                const FuncInfo &FInfo) {
  Value *opr0 = CI->getArgOperand(0);
  if (ConstantFP *CF = dyn_cast<ConstantFP>(opr0)) {
    // Just create a normal div. Later, InstCombine will be able
    // to compute the divide into a constant (avoid check float infinity
    // or subnormal at this point).
    Value *nval = B.CreateFDiv(ConstantFP::get(CF->getType(), 1.0),
                               opr0,
                               "recip2div");
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *nval << "\n");
    replaceCall(nval);
    return true;
  }
  return false;
}

//  [native_]half_divide(x, c) ==> x/c
bool AMDGPULibCalls::fold_divide(CallInst *CI, IRBuilder<> &B,
                                 const FuncInfo &FInfo) {
  Value *opr0 = CI->getArgOperand(0);
  Value *opr1 = CI->getArgOperand(1);
  ConstantFP *CF0 = dyn_cast<ConstantFP>(opr0);
  ConstantFP *CF1 = dyn_cast<ConstantFP>(opr1);

  if ((CF0 && CF1) ||  // both are constants
      (CF1 && (getArgType(FInfo) == AMDGPULibFunc::F32)))
      // CF1 is constant && f32 divide
  {
    Value *nval1 = B.CreateFDiv(ConstantFP::get(opr1->getType(), 1.0),
                                opr1, "__div2recip");
    Value *nval  = B.CreateFMul(opr0, nval1, "__div2mul");
    replaceCall(nval);
    return true;
  }
  return false;
}

namespace llvm {
static double log2(double V) {
#if _XOPEN_SOURCE >= 600 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
  return ::log2(V);
#else
  return log(V) / 0.693147180559945309417;
#endif
}
}

bool AMDGPULibCalls::fold_pow(CallInst *CI, IRBuilder<> &B,
                              const FuncInfo &FInfo) {
  assert((FInfo.getId() == AMDGPULibFunc::EI_POW ||
          FInfo.getId() == AMDGPULibFunc::EI_POWR ||
          FInfo.getId() == AMDGPULibFunc::EI_POWN) &&
         "fold_pow: encounter a wrong function call");

  Value *opr0, *opr1;
  ConstantFP *CF;
  ConstantInt *CINT;
  ConstantAggregateZero *CZero;
  Type *eltType;

  opr0 = CI->getArgOperand(0);
  opr1 = CI->getArgOperand(1);
  CZero = dyn_cast<ConstantAggregateZero>(opr1);
  if (getVecSize(FInfo) == 1) {
    eltType = opr0->getType();
    CF = dyn_cast<ConstantFP>(opr1);
    CINT = dyn_cast<ConstantInt>(opr1);
  } else {
    VectorType *VTy = dyn_cast<VectorType>(opr0->getType());
    assert(VTy && "Oprand of vector function should be of vectortype");
    eltType = VTy->getElementType();
    ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(opr1);

    // Now, only Handle vector const whose elements have the same value.
    CF = CDV ? dyn_cast_or_null<ConstantFP>(CDV->getSplatValue()) : nullptr;
    CINT = CDV ? dyn_cast_or_null<ConstantInt>(CDV->getSplatValue()) : nullptr;
  }

  // No unsafe math , no constant argument, do nothing
  if (!isUnsafeMath(CI) && !CF && !CINT && !CZero)
    return false;

  // 0x1111111 means that we don't do anything for this call.
  int ci_opr1 = (CINT ? (int)CINT->getSExtValue() : 0x1111111);

  if ((CF && CF->isZero()) || (CINT && ci_opr1 == 0) || CZero) {
    //  pow/powr/pown(x, 0) == 1
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> 1\n");
    Constant *cnval = ConstantFP::get(eltType, 1.0);
    if (getVecSize(FInfo) > 1) {
      cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
    }
    replaceCall(cnval);
    return true;
  }
  if ((CF && CF->isExactlyValue(1.0)) || (CINT && ci_opr1 == 1)) {
    // pow/powr/pown(x, 1.0) = x
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr0 << "\n");
    replaceCall(opr0);
    return true;
  }
  if ((CF && CF->isExactlyValue(2.0)) || (CINT && ci_opr1 == 2)) {
    // pow/powr/pown(x, 2.0) = x*x
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr0 << " * " << *opr0
                      << "\n");
    Value *nval = B.CreateFMul(opr0, opr0, "__pow2");
    replaceCall(nval);
    return true;
  }
  if ((CF && CF->isExactlyValue(-1.0)) || (CINT && ci_opr1 == -1)) {
    // pow/powr/pown(x, -1.0) = 1.0/x
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> 1 / " << *opr0 << "\n");
    Constant *cnval = ConstantFP::get(eltType, 1.0);
    if (getVecSize(FInfo) > 1) {
      cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
    }
    Value *nval = B.CreateFDiv(cnval, opr0, "__powrecip");
    replaceCall(nval);
    return true;
  }

  Module *M = CI->getModule();
  if (CF && (CF->isExactlyValue(0.5) || CF->isExactlyValue(-0.5))) {
    // pow[r](x, [-]0.5) = sqrt(x)
    bool issqrt = CF->isExactlyValue(0.5);
    if (Constant *FPExpr = getFunction(M,
        AMDGPULibFunc(issqrt ? AMDGPULibFunc::EI_SQRT
                             : AMDGPULibFunc::EI_RSQRT, FInfo))) {
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> "
                        << FInfo.getName().c_str() << "(" << *opr0 << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, issqrt ? "__pow2sqrt"
                                                        : "__pow2rsqrt");
      replaceCall(nval);
      return true;
    }
  }

  if (!isUnsafeMath(CI))
    return false;

  // Unsafe Math optimization

  // Remember that ci_opr1 is set if opr1 is integral
  if (CF) {
    double dval = (getArgType(FInfo) == AMDGPULibFunc::F32)
                    ? (double)CF->getValueAPF().convertToFloat()
                    : CF->getValueAPF().convertToDouble();
    int ival = (int)dval;
    if ((double)ival == dval) {
      ci_opr1 = ival;
    } else
      ci_opr1 = 0x11111111;
  }

  // pow/powr/pown(x, c) = [1/](x*x*..x); where
  //   trunc(c) == c && the number of x == c && |c| <= 12
  unsigned abs_opr1 = (ci_opr1 < 0) ? -ci_opr1 : ci_opr1;
  if (abs_opr1 <= 12) {
    Constant *cnval;
    Value *nval;
    if (abs_opr1 == 0) {
      cnval = ConstantFP::get(eltType, 1.0);
      if (getVecSize(FInfo) > 1) {
        cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
      }
      nval = cnval;
    } else {
      Value *valx2 = nullptr;
      nval = nullptr;
      while (abs_opr1 > 0) {
        valx2 = valx2 ? B.CreateFMul(valx2, valx2, "__powx2") : opr0;
        if (abs_opr1 & 1) {
          nval = nval ? B.CreateFMul(nval, valx2, "__powprod") : valx2;
        }
        abs_opr1 >>= 1;
      }
    }

    if (ci_opr1 < 0) {
      cnval = ConstantFP::get(eltType, 1.0);
      if (getVecSize(FInfo) > 1) {
        cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
      }
      nval = B.CreateFDiv(cnval, nval, "__1powprod");
    }
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> "
                      << ((ci_opr1 < 0) ? "1/prod(" : "prod(") << *opr0
                      << ")\n");
    replaceCall(nval);
    return true;
  }

  // powr ---> exp2(y * log2(x))
  // pown/pow ---> powr(fabs(x), y) | (x & ((int)y << 31))
  Constant *ExpExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_EXP2,
                                                   FInfo));
  if (!ExpExpr)
    return false;

  bool needlog = false;
  bool needabs = false;
  bool needcopysign = false;
  Constant *cnval = nullptr;
  if (getVecSize(FInfo) == 1) {
    CF = dyn_cast<ConstantFP>(opr0);

    if (CF) {
      double V = (getArgType(FInfo) == AMDGPULibFunc::F32)
                   ? (double)CF->getValueAPF().convertToFloat()
                   : CF->getValueAPF().convertToDouble();

      V = log2(std::abs(V));
      cnval = ConstantFP::get(eltType, V);
      needcopysign = (FInfo.getId() != AMDGPULibFunc::EI_POWR) &&
                     CF->isNegative();
    } else {
      needlog = true;
      needcopysign = needabs = FInfo.getId() != AMDGPULibFunc::EI_POWR &&
                               (!CF || CF->isNegative());
    }
  } else {
    ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(opr0);

    if (!CDV) {
      needlog = true;
      needcopysign = needabs = FInfo.getId() != AMDGPULibFunc::EI_POWR;
    } else {
      assert ((int)CDV->getNumElements() == getVecSize(FInfo) &&
              "Wrong vector size detected");

      SmallVector<double, 0> DVal;
      for (int i=0; i < getVecSize(FInfo); ++i) {
        double V = (getArgType(FInfo) == AMDGPULibFunc::F32)
                     ? (double)CDV->getElementAsFloat(i)
                     : CDV->getElementAsDouble(i);
        if (V < 0.0) needcopysign = true;
        V = log2(std::abs(V));
        DVal.push_back(V);
      }
      if (getArgType(FInfo) == AMDGPULibFunc::F32) {
        SmallVector<float, 0> FVal;
        for (unsigned i=0; i < DVal.size(); ++i) {
          FVal.push_back((float)DVal[i]);
        }
        ArrayRef<float> tmp(FVal);
        cnval = ConstantDataVector::get(M->getContext(), tmp);
      } else {
        ArrayRef<double> tmp(DVal);
        cnval = ConstantDataVector::get(M->getContext(), tmp);
      }
    }
  }

  if (needcopysign && (FInfo.getId() == AMDGPULibFunc::EI_POW)) {
    // We cannot handle corner cases for a general pow() function, give up
    // unless y is a constant integral value. Then proceed as if it were pown.
    if (getVecSize(FInfo) == 1) {
      if (const ConstantFP *CF = dyn_cast<ConstantFP>(opr1)) {
        double y = (getArgType(FInfo) == AMDGPULibFunc::F32)
                   ? (double)CF->getValueAPF().convertToFloat()
                   : CF->getValueAPF().convertToDouble();
        if (y != (double)(int64_t)y)
          return false;
      } else
        return false;
    } else {
      if (const ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(opr1)) {
        for (int i=0; i < getVecSize(FInfo); ++i) {
          double y = (getArgType(FInfo) == AMDGPULibFunc::F32)
                     ? (double)CDV->getElementAsFloat(i)
                     : CDV->getElementAsDouble(i);
          if (y != (double)(int64_t)y)
            return false;
        }
      } else
        return false;
    }
  }

  Value *nval;
  if (needabs) {
    Constant *AbsExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_FABS,
                                                     FInfo));
    if (!AbsExpr)
      return false;
    nval = CreateCallEx(B, AbsExpr, opr0, "__fabs");
  } else {
    nval = cnval ? cnval : opr0;
  }
  if (needlog) {
    Constant *LogExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_LOG2,
                                                     FInfo));
    if (!LogExpr)
      return false;
    nval = CreateCallEx(B,LogExpr, nval, "__log2");
  }

  if (FInfo.getId() == AMDGPULibFunc::EI_POWN) {
    // convert int(32) to fp(f32 or f64)
    opr1 = B.CreateSIToFP(opr1, nval->getType(), "pownI2F");
  }
  nval = B.CreateFMul(opr1, nval, "__ylogx");
  nval = CreateCallEx(B,ExpExpr, nval, "__exp2");

  if (needcopysign) {
    Value *opr_n;
    Type* rTy = opr0->getType();
    Type* nTyS = eltType->isDoubleTy() ? B.getInt64Ty() : B.getInt32Ty();
    Type *nTy = nTyS;
    if (const VectorType *vTy = dyn_cast<VectorType>(rTy))
      nTy = VectorType::get(nTyS, vTy->getNumElements());
    unsigned size = nTy->getScalarSizeInBits();
    opr_n = CI->getArgOperand(1);
    if (opr_n->getType()->isIntegerTy())
      opr_n = B.CreateZExtOrBitCast(opr_n, nTy, "__ytou");
    else
      opr_n = B.CreateFPToSI(opr1, nTy, "__ytou");

    Value *sign = B.CreateShl(opr_n, size-1, "__yeven");
    sign = B.CreateAnd(B.CreateBitCast(opr0, nTy), sign, "__pow_sign");
    nval = B.CreateOr(B.CreateBitCast(nval, nTy), sign);
    nval = B.CreateBitCast(nval, opr0->getType());
  }

  LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> "
                    << "exp2(" << *opr1 << " * log2(" << *opr0 << "))\n");
  replaceCall(nval);

  return true;
}

bool AMDGPULibCalls::fold_rootn(CallInst *CI, IRBuilder<> &B,
                                const FuncInfo &FInfo) {
  Value *opr0 = CI->getArgOperand(0);
  Value *opr1 = CI->getArgOperand(1);

  ConstantInt *CINT = dyn_cast<ConstantInt>(opr1);
  if (!CINT) {
    return false;
  }
  int ci_opr1 = (int)CINT->getSExtValue();
  if (ci_opr1 == 1) {  // rootn(x, 1) = x
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr0 << "\n");
    replaceCall(opr0);
    return true;
  }
  if (ci_opr1 == 2) {  // rootn(x, 2) = sqrt(x)
    std::vector<const Type*> ParamsTys;
    ParamsTys.push_back(opr0->getType());
    Module *M = CI->getModule();
    if (Constant *FPExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_SQRT,
                                                        FInfo))) {
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> sqrt(" << *opr0 << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, "__rootn2sqrt");
      replaceCall(nval);
      return true;
    }
  } else if (ci_opr1 == 3) { // rootn(x, 3) = cbrt(x)
    Module *M = CI->getModule();
    if (Constant *FPExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_CBRT,
                                                        FInfo))) {
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> cbrt(" << *opr0 << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, "__rootn2cbrt");
      replaceCall(nval);
      return true;
    }
  } else if (ci_opr1 == -1) { // rootn(x, -1) = 1.0/x
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> 1.0 / " << *opr0 << "\n");
    Value *nval = B.CreateFDiv(ConstantFP::get(opr0->getType(), 1.0),
                               opr0,
                               "__rootn2div");
    replaceCall(nval);
    return true;
  } else if (ci_opr1 == -2) {  // rootn(x, -2) = rsqrt(x)
    std::vector<const Type*> ParamsTys;
    ParamsTys.push_back(opr0->getType());
    Module *M = CI->getModule();
    if (Constant *FPExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_RSQRT,
                                                        FInfo))) {
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> rsqrt(" << *opr0
                        << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, "__rootn2rsqrt");
      replaceCall(nval);
      return true;
    }
  }
  return false;
}

bool AMDGPULibCalls::fold_fma_mad(CallInst *CI, IRBuilder<> &B,
                                  const FuncInfo &FInfo) {
  Value *opr0 = CI->getArgOperand(0);
  Value *opr1 = CI->getArgOperand(1);
  Value *opr2 = CI->getArgOperand(2);

  ConstantFP *CF0 = dyn_cast<ConstantFP>(opr0);
  ConstantFP *CF1 = dyn_cast<ConstantFP>(opr1);
  if ((CF0 && CF0->isZero()) || (CF1 && CF1->isZero())) {
    // fma/mad(a, b, c) = c if a=0 || b=0
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr2 << "\n");
    replaceCall(opr2);
    return true;
  }
  if (CF0 && CF0->isExactlyValue(1.0f)) {
    // fma/mad(a, b, c) = b+c if a=1
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr1 << " + " << *opr2
                      << "\n");
    Value *nval = B.CreateFAdd(opr1, opr2, "fmaadd");
    replaceCall(nval);
    return true;
  }
  if (CF1 && CF1->isExactlyValue(1.0f)) {
    // fma/mad(a, b, c) = a+c if b=1
    LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr0 << " + " << *opr2
                      << "\n");
    Value *nval = B.CreateFAdd(opr0, opr2, "fmaadd");
    replaceCall(nval);
    return true;
  }
  if (ConstantFP *CF = dyn_cast<ConstantFP>(opr2)) {
    if (CF->isZero()) {
      // fma/mad(a, b, c) = a*b if c=0
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *opr0 << " * "
                        << *opr1 << "\n");
      Value *nval = B.CreateFMul(opr0, opr1, "fmamul");
      replaceCall(nval);
      return true;
    }
  }

  return false;
}

// Get a scalar native builtin signle argument FP function
Constant* AMDGPULibCalls::getNativeFunction(Module* M, const FuncInfo& FInfo) {
  if (getArgType(FInfo) == AMDGPULibFunc::F64 || !HasNative(FInfo.getId()))
    return nullptr;
  FuncInfo nf = FInfo;
  nf.setPrefix(AMDGPULibFunc::NATIVE);
  return getFunction(M, nf);
}

// fold sqrt -> native_sqrt (x)
bool AMDGPULibCalls::fold_sqrt(CallInst *CI, IRBuilder<> &B,
                               const FuncInfo &FInfo) {
  if (getArgType(FInfo) == AMDGPULibFunc::F32 && (getVecSize(FInfo) == 1) &&
      (FInfo.getPrefix() != AMDGPULibFunc::NATIVE)) {
    if (Constant *FPExpr = getNativeFunction(
        CI->getModule(), AMDGPULibFunc(AMDGPULibFunc::EI_SQRT, FInfo))) {
      Value *opr0 = CI->getArgOperand(0);
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> "
                        << "sqrt(" << *opr0 << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, "__sqrt");
      replaceCall(nval);
      return true;
    }
  }
  return false;
}

// fold sin, cos -> sincos.
bool AMDGPULibCalls::fold_sincos(CallInst *CI, IRBuilder<> &B,
                                 AliasAnalysis *AA) {
  AMDGPULibFunc fInfo;
  if (!AMDGPULibFunc::parse(CI->getCalledFunction()->getName(), fInfo))
    return false;

  assert(fInfo.getId() == AMDGPULibFunc::EI_SIN ||
         fInfo.getId() == AMDGPULibFunc::EI_COS);
  bool const isSin = fInfo.getId() == AMDGPULibFunc::EI_SIN;

  Value *CArgVal = CI->getArgOperand(0);
  BasicBlock * const CBB = CI->getParent();

  int const MaxScan = 30;

  { // fold in load value.
    LoadInst *LI = dyn_cast<LoadInst>(CArgVal);
    if (LI && LI->getParent() == CBB) {
      BasicBlock::iterator BBI = LI->getIterator();
      Value *AvailableVal = FindAvailableLoadedValue(LI, CBB, BBI, MaxScan, AA);
      if (AvailableVal) {
        CArgVal->replaceAllUsesWith(AvailableVal);
        if (CArgVal->getNumUses() == 0)
          LI->eraseFromParent();
        CArgVal = CI->getArgOperand(0);
      }
    }
  }

  Module *M = CI->getModule();
  fInfo.setId(isSin ? AMDGPULibFunc::EI_COS : AMDGPULibFunc::EI_SIN);
  std::string const PairName = fInfo.mangle();

  CallInst *UI = nullptr;
  for (User* U : CArgVal->users()) {
    CallInst *XI = dyn_cast_or_null<CallInst>(U);
    if (!XI || XI == CI || XI->getParent() != CBB)
      continue;

    Function *UCallee = XI->getCalledFunction();
    if (!UCallee || !UCallee->getName().equals(PairName))
      continue;

    BasicBlock::iterator BBI = CI->getIterator();
    if (BBI == CI->getParent()->begin())
      break;
    --BBI;
    for (int I = MaxScan; I > 0 && BBI != CBB->begin(); --BBI, --I) {
      if (cast<Instruction>(BBI) == XI) {
        UI = XI;
        break;
      }
    }
    if (UI) break;
  }

  if (!UI) return false;

  // Merge the sin and cos.

  // for OpenCL 2.0 we have only generic implementation of sincos
  // function.
  AMDGPULibFunc nf(AMDGPULibFunc::EI_SINCOS, fInfo);
  nf.getLeads()[0].PtrKind = AMDGPULibFunc::getEPtrKindFromAddrSpace(AMDGPUAS::FLAT_ADDRESS);
  Function *Fsincos = dyn_cast_or_null<Function>(getFunction(M, nf));
  if (!Fsincos) return false;

  BasicBlock::iterator ItOld = B.GetInsertPoint();
  AllocaInst *Alloc = insertAlloca(UI, B, "__sincos_");
  B.SetInsertPoint(UI);

  Value *P = Alloc;
  Type *PTy = Fsincos->getFunctionType()->getParamType(1);
  // The allocaInst allocates the memory in private address space. This need
  // to be bitcasted to point to the address space of cos pointer type.
  // In OpenCL 2.0 this is generic, while in 1.2 that is private.
  if (PTy->getPointerAddressSpace() != AMDGPUAS::PRIVATE_ADDRESS)
    P = B.CreateAddrSpaceCast(Alloc, PTy);
  CallInst *Call = CreateCallEx2(B, Fsincos, UI->getArgOperand(0), P);

  LLVM_DEBUG(errs() << "AMDIC: fold_sincos (" << *CI << ", " << *UI << ") with "
                    << *Call << "\n");

  if (!isSin) { // CI->cos, UI->sin
    B.SetInsertPoint(&*ItOld);
    UI->replaceAllUsesWith(&*Call);
    Instruction *Reload = B.CreateLoad(Alloc);
    CI->replaceAllUsesWith(Reload);
    UI->eraseFromParent();
    CI->eraseFromParent();
  } else { // CI->sin, UI->cos
    Instruction *Reload = B.CreateLoad(Alloc);
    UI->replaceAllUsesWith(Reload);
    CI->replaceAllUsesWith(Call);
    UI->eraseFromParent();
    CI->eraseFromParent();
  }
  return true;
}

// Get insertion point at entry.
BasicBlock::iterator AMDGPULibCalls::getEntryIns(CallInst * UI) {
  Function * Func = UI->getParent()->getParent();
  BasicBlock * BB = &Func->getEntryBlock();
  assert(BB && "Entry block not found!");
  BasicBlock::iterator ItNew = BB->begin();
  return ItNew;
}

// Insert a AllocsInst at the beginning of function entry block.
AllocaInst* AMDGPULibCalls::insertAlloca(CallInst *UI, IRBuilder<> &B,
                                         const char *prefix) {
  BasicBlock::iterator ItNew = getEntryIns(UI);
  Function *UCallee = UI->getCalledFunction();
  Type *RetType = UCallee->getReturnType();
  B.SetInsertPoint(&*ItNew);
  AllocaInst *Alloc = B.CreateAlloca(RetType, 0,
    std::string(prefix) + UI->getName());
  Alloc->setAlignment(UCallee->getParent()->getDataLayout()
                       .getTypeAllocSize(RetType));
  return Alloc;
}

bool AMDGPULibCalls::evaluateScalarMathFunc(FuncInfo &FInfo,
                                            double& Res0, double& Res1,
                                            Constant *copr0, Constant *copr1,
                                            Constant *copr2) {
  // By default, opr0/opr1/opr3 holds values of float/double type.
  // If they are not float/double, each function has to its
  // operand separately.
  double opr0=0.0, opr1=0.0, opr2=0.0;
  ConstantFP *fpopr0 = dyn_cast_or_null<ConstantFP>(copr0);
  ConstantFP *fpopr1 = dyn_cast_or_null<ConstantFP>(copr1);
  ConstantFP *fpopr2 = dyn_cast_or_null<ConstantFP>(copr2);
  if (fpopr0) {
    opr0 = (getArgType(FInfo) == AMDGPULibFunc::F64)
             ? fpopr0->getValueAPF().convertToDouble()
             : (double)fpopr0->getValueAPF().convertToFloat();
  }

  if (fpopr1) {
    opr1 = (getArgType(FInfo) == AMDGPULibFunc::F64)
             ? fpopr1->getValueAPF().convertToDouble()
             : (double)fpopr1->getValueAPF().convertToFloat();
  }

  if (fpopr2) {
    opr2 = (getArgType(FInfo) == AMDGPULibFunc::F64)
             ? fpopr2->getValueAPF().convertToDouble()
             : (double)fpopr2->getValueAPF().convertToFloat();
  }

  switch (FInfo.getId()) {
  default : return false;

  case AMDGPULibFunc::EI_ACOS:
    Res0 = acos(opr0);
    return true;

  case AMDGPULibFunc::EI_ACOSH:
    // acosh(x) == log(x + sqrt(x*x - 1))
    Res0 = log(opr0 + sqrt(opr0*opr0 - 1.0));
    return true;

  case AMDGPULibFunc::EI_ACOSPI:
    Res0 = acos(opr0) / MATH_PI;
    return true;

  case AMDGPULibFunc::EI_ASIN:
    Res0 = asin(opr0);
    return true;

  case AMDGPULibFunc::EI_ASINH:
    // asinh(x) == log(x + sqrt(x*x + 1))
    Res0 = log(opr0 + sqrt(opr0*opr0 + 1.0));
    return true;

  case AMDGPULibFunc::EI_ASINPI:
    Res0 = asin(opr0) / MATH_PI;
    return true;

  case AMDGPULibFunc::EI_ATAN:
    Res0 = atan(opr0);
    return true;

  case AMDGPULibFunc::EI_ATANH:
    // atanh(x) == (log(x+1) - log(x-1))/2;
    Res0 = (log(opr0 + 1.0) - log(opr0 - 1.0))/2.0;
    return true;

  case AMDGPULibFunc::EI_ATANPI:
    Res0 = atan(opr0) / MATH_PI;
    return true;

  case AMDGPULibFunc::EI_CBRT:
    Res0 = (opr0 < 0.0) ? -pow(-opr0, 1.0/3.0) : pow(opr0, 1.0/3.0);
    return true;

  case AMDGPULibFunc::EI_COS:
    Res0 = cos(opr0);
    return true;

  case AMDGPULibFunc::EI_COSH:
    Res0 = cosh(opr0);
    return true;

  case AMDGPULibFunc::EI_COSPI:
    Res0 = cos(MATH_PI * opr0);
    return true;

  case AMDGPULibFunc::EI_EXP:
    Res0 = exp(opr0);
    return true;

  case AMDGPULibFunc::EI_EXP2:
    Res0 = pow(2.0, opr0);
    return true;

  case AMDGPULibFunc::EI_EXP10:
    Res0 = pow(10.0, opr0);
    return true;

  case AMDGPULibFunc::EI_EXPM1:
    Res0 = exp(opr0) - 1.0;
    return true;

  case AMDGPULibFunc::EI_LOG:
    Res0 = log(opr0);
    return true;

  case AMDGPULibFunc::EI_LOG2:
    Res0 = log(opr0) / log(2.0);
    return true;

  case AMDGPULibFunc::EI_LOG10:
    Res0 = log(opr0) / log(10.0);
    return true;

  case AMDGPULibFunc::EI_RSQRT:
    Res0 = 1.0 / sqrt(opr0);
    return true;

  case AMDGPULibFunc::EI_SIN:
    Res0 = sin(opr0);
    return true;

  case AMDGPULibFunc::EI_SINH:
    Res0 = sinh(opr0);
    return true;

  case AMDGPULibFunc::EI_SINPI:
    Res0 = sin(MATH_PI * opr0);
    return true;

  case AMDGPULibFunc::EI_SQRT:
    Res0 = sqrt(opr0);
    return true;

  case AMDGPULibFunc::EI_TAN:
    Res0 = tan(opr0);
    return true;

  case AMDGPULibFunc::EI_TANH:
    Res0 = tanh(opr0);
    return true;

  case AMDGPULibFunc::EI_TANPI:
    Res0 = tan(MATH_PI * opr0);
    return true;

  case AMDGPULibFunc::EI_RECIP:
    Res0 = 1.0 / opr0;
    return true;

  // two-arg functions
  case AMDGPULibFunc::EI_DIVIDE:
    Res0 = opr0 / opr1;
    return true;

  case AMDGPULibFunc::EI_POW:
  case AMDGPULibFunc::EI_POWR:
    Res0 = pow(opr0, opr1);
    return true;

  case AMDGPULibFunc::EI_POWN: {
    if (ConstantInt *iopr1 = dyn_cast_or_null<ConstantInt>(copr1)) {
      double val = (double)iopr1->getSExtValue();
      Res0 = pow(opr0, val);
      return true;
    }
    return false;
  }

  case AMDGPULibFunc::EI_ROOTN: {
    if (ConstantInt *iopr1 = dyn_cast_or_null<ConstantInt>(copr1)) {
      double val = (double)iopr1->getSExtValue();
      Res0 = pow(opr0, 1.0 / val);
      return true;
    }
    return false;
  }

  // with ptr arg
  case AMDGPULibFunc::EI_SINCOS:
    Res0 = sin(opr0);
    Res1 = cos(opr0);
    return true;

  // three-arg functions
  case AMDGPULibFunc::EI_FMA:
  case AMDGPULibFunc::EI_MAD:
    Res0 = opr0 * opr1 + opr2;
    return true;
  }

  return false;
}

bool AMDGPULibCalls::evaluateCall(CallInst *aCI, FuncInfo &FInfo) {
  int numArgs = (int)aCI->getNumArgOperands();
  if (numArgs > 3)
    return false;

  Constant *copr0 = nullptr;
  Constant *copr1 = nullptr;
  Constant *copr2 = nullptr;
  if (numArgs > 0) {
    if ((copr0 = dyn_cast<Constant>(aCI->getArgOperand(0))) == nullptr)
      return false;
  }

  if (numArgs > 1) {
    if ((copr1 = dyn_cast<Constant>(aCI->getArgOperand(1))) == nullptr) {
      if (FInfo.getId() != AMDGPULibFunc::EI_SINCOS)
        return false;
    }
  }

  if (numArgs > 2) {
    if ((copr2 = dyn_cast<Constant>(aCI->getArgOperand(2))) == nullptr)
      return false;
  }

  // At this point, all arguments to aCI are constants.

  // max vector size is 16, and sincos will generate two results.
  double DVal0[16], DVal1[16];
  bool hasTwoResults = (FInfo.getId() == AMDGPULibFunc::EI_SINCOS);
  if (getVecSize(FInfo) == 1) {
    if (!evaluateScalarMathFunc(FInfo, DVal0[0],
                                DVal1[0], copr0, copr1, copr2)) {
      return false;
    }
  } else {
    ConstantDataVector *CDV0 = dyn_cast_or_null<ConstantDataVector>(copr0);
    ConstantDataVector *CDV1 = dyn_cast_or_null<ConstantDataVector>(copr1);
    ConstantDataVector *CDV2 = dyn_cast_or_null<ConstantDataVector>(copr2);
    for (int i=0; i < getVecSize(FInfo); ++i) {
      Constant *celt0 = CDV0 ? CDV0->getElementAsConstant(i) : nullptr;
      Constant *celt1 = CDV1 ? CDV1->getElementAsConstant(i) : nullptr;
      Constant *celt2 = CDV2 ? CDV2->getElementAsConstant(i) : nullptr;
      if (!evaluateScalarMathFunc(FInfo, DVal0[i],
                                  DVal1[i], celt0, celt1, celt2)) {
        return false;
      }
    }
  }

  LLVMContext &context = CI->getParent()->getParent()->getContext();
  Constant *nval0, *nval1;
  if (getVecSize(FInfo) == 1) {
    nval0 = ConstantFP::get(CI->getType(), DVal0[0]);
    if (hasTwoResults)
      nval1 = ConstantFP::get(CI->getType(), DVal1[0]);
  } else {
    if (getArgType(FInfo) == AMDGPULibFunc::F32) {
      SmallVector <float, 0> FVal0, FVal1;
      for (int i=0; i < getVecSize(FInfo); ++i)
        FVal0.push_back((float)DVal0[i]);
      ArrayRef<float> tmp0(FVal0);
      nval0 = ConstantDataVector::get(context, tmp0);
      if (hasTwoResults) {
        for (int i=0; i < getVecSize(FInfo); ++i)
          FVal1.push_back((float)DVal1[i]);
        ArrayRef<float> tmp1(FVal1);
        nval1 = ConstantDataVector::get(context, tmp1);
      }
    } else {
      ArrayRef<double> tmp0(DVal0);
      nval0 = ConstantDataVector::get(context, tmp0);
      if (hasTwoResults) {
        ArrayRef<double> tmp1(DVal1);
        nval1 = ConstantDataVector::get(context, tmp1);
      }
    }
  }

  if (hasTwoResults) {
    // sincos
    assert(FInfo.getId() == AMDGPULibFunc::EI_SINCOS &&
           "math function with ptr arg not supported yet");
    new StoreInst(nval1, aCI->getArgOperand(1), aCI);
  }

  replaceCall(nval0);
  return true;
}

// Public interface to the Simplify LibCalls pass.
FunctionPass *llvm::createAMDGPUSimplifyLibCallsPass(const TargetOptions &Opt) {
  return new AMDGPUSimplifyLibCalls(Opt);
}

FunctionPass *llvm::createAMDGPUUseNativeCallsPass() {
  return new AMDGPUUseNativeCalls();
}

static bool setFastFlags(Function &F, const TargetOptions &Options) {
  AttrBuilder B;

  if (Options.UnsafeFPMath || Options.NoInfsFPMath)
    B.addAttribute("no-infs-fp-math", "true");
  if (Options.UnsafeFPMath || Options.NoNaNsFPMath)
    B.addAttribute("no-nans-fp-math", "true");
  if (Options.UnsafeFPMath) {
    B.addAttribute("less-precise-fpmad", "true");
    B.addAttribute("unsafe-fp-math", "true");
  }

  if (!B.hasAttributes())
    return false;

  F.addAttributes(AttributeList::FunctionIndex, B);

  return true;
}

bool AMDGPUSimplifyLibCalls::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  bool Changed = false;
  auto AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  LLVM_DEBUG(dbgs() << "AMDIC: process function ";
             F.printAsOperand(dbgs(), false, F.getParent()); dbgs() << '\n';);

  if (!EnablePreLink)
    Changed |= setFastFlags(F, Options);

  for (auto &BB : F) {
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ) {
      // Ignore non-calls.
      CallInst *CI = dyn_cast<CallInst>(I);
      ++I;
      if (!CI) continue;

      // Ignore indirect calls.
      Function *Callee = CI->getCalledFunction();
      if (Callee == 0) continue;

      LLVM_DEBUG(dbgs() << "AMDIC: try folding " << *CI << "\n";
                 dbgs().flush());
      if(Simplifier.fold(CI, AA))
        Changed = true;
    }
  }
  return Changed;
}

bool AMDGPUUseNativeCalls::runOnFunction(Function &F) {
  if (skipFunction(F) || UseNative.empty())
    return false;

  bool Changed = false;
  for (auto &BB : F) {
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ) {
      // Ignore non-calls.
      CallInst *CI = dyn_cast<CallInst>(I);
      ++I;
      if (!CI) continue;

      // Ignore indirect calls.
      Function *Callee = CI->getCalledFunction();
      if (Callee == 0) continue;

      if(Simplifier.useNative(CI))
        Changed = true;
    }
  }
  return Changed;
}
