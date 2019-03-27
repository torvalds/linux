//===-- ExecutionEngine.cpp - Common Implementation shared by EEs ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the common interface used by the various execution engine
// subclasses.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MutexGuard.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cmath>
#include <cstring>
using namespace llvm;

#define DEBUG_TYPE "jit"

STATISTIC(NumInitBytes, "Number of bytes of global vars initialized");
STATISTIC(NumGlobals  , "Number of global vars initialized");

ExecutionEngine *(*ExecutionEngine::MCJITCtor)(
    std::unique_ptr<Module> M, std::string *ErrorStr,
    std::shared_ptr<MCJITMemoryManager> MemMgr,
    std::shared_ptr<LegacyJITSymbolResolver> Resolver,
    std::unique_ptr<TargetMachine> TM) = nullptr;

ExecutionEngine *(*ExecutionEngine::OrcMCJITReplacementCtor)(
    std::string *ErrorStr, std::shared_ptr<MCJITMemoryManager> MemMgr,
    std::shared_ptr<LegacyJITSymbolResolver> Resolver,
    std::unique_ptr<TargetMachine> TM) = nullptr;

ExecutionEngine *(*ExecutionEngine::InterpCtor)(std::unique_ptr<Module> M,
                                                std::string *ErrorStr) =nullptr;

void JITEventListener::anchor() {}

void ObjectCache::anchor() {}

void ExecutionEngine::Init(std::unique_ptr<Module> M) {
  CompilingLazily         = false;
  GVCompilationDisabled   = false;
  SymbolSearchingDisabled = false;

  // IR module verification is enabled by default in debug builds, and disabled
  // by default in release builds.
#ifndef NDEBUG
  VerifyModules = true;
#else
  VerifyModules = false;
#endif

  assert(M && "Module is null?");
  Modules.push_back(std::move(M));
}

ExecutionEngine::ExecutionEngine(std::unique_ptr<Module> M)
    : DL(M->getDataLayout()), LazyFunctionCreator(nullptr) {
  Init(std::move(M));
}

ExecutionEngine::ExecutionEngine(DataLayout DL, std::unique_ptr<Module> M)
    : DL(std::move(DL)), LazyFunctionCreator(nullptr) {
  Init(std::move(M));
}

ExecutionEngine::~ExecutionEngine() {
  clearAllGlobalMappings();
}

namespace {
/// Helper class which uses a value handler to automatically deletes the
/// memory block when the GlobalVariable is destroyed.
class GVMemoryBlock final : public CallbackVH {
  GVMemoryBlock(const GlobalVariable *GV)
    : CallbackVH(const_cast<GlobalVariable*>(GV)) {}

public:
  /// Returns the address the GlobalVariable should be written into.  The
  /// GVMemoryBlock object prefixes that.
  static char *Create(const GlobalVariable *GV, const DataLayout& TD) {
    Type *ElTy = GV->getValueType();
    size_t GVSize = (size_t)TD.getTypeAllocSize(ElTy);
    void *RawMemory = ::operator new(
        alignTo(sizeof(GVMemoryBlock), TD.getPreferredAlignment(GV)) + GVSize);
    new(RawMemory) GVMemoryBlock(GV);
    return static_cast<char*>(RawMemory) + sizeof(GVMemoryBlock);
  }

  void deleted() override {
    // We allocated with operator new and with some extra memory hanging off the
    // end, so don't just delete this.  I'm not sure if this is actually
    // required.
    this->~GVMemoryBlock();
    ::operator delete(this);
  }
};
}  // anonymous namespace

char *ExecutionEngine::getMemoryForGV(const GlobalVariable *GV) {
  return GVMemoryBlock::Create(GV, getDataLayout());
}

void ExecutionEngine::addObjectFile(std::unique_ptr<object::ObjectFile> O) {
  llvm_unreachable("ExecutionEngine subclass doesn't implement addObjectFile.");
}

void
ExecutionEngine::addObjectFile(object::OwningBinary<object::ObjectFile> O) {
  llvm_unreachable("ExecutionEngine subclass doesn't implement addObjectFile.");
}

void ExecutionEngine::addArchive(object::OwningBinary<object::Archive> A) {
  llvm_unreachable("ExecutionEngine subclass doesn't implement addArchive.");
}

bool ExecutionEngine::removeModule(Module *M) {
  for (auto I = Modules.begin(), E = Modules.end(); I != E; ++I) {
    Module *Found = I->get();
    if (Found == M) {
      I->release();
      Modules.erase(I);
      clearGlobalMappingsFromModule(M);
      return true;
    }
  }
  return false;
}

Function *ExecutionEngine::FindFunctionNamed(StringRef FnName) {
  for (unsigned i = 0, e = Modules.size(); i != e; ++i) {
    Function *F = Modules[i]->getFunction(FnName);
    if (F && !F->isDeclaration())
      return F;
  }
  return nullptr;
}

GlobalVariable *ExecutionEngine::FindGlobalVariableNamed(StringRef Name, bool AllowInternal) {
  for (unsigned i = 0, e = Modules.size(); i != e; ++i) {
    GlobalVariable *GV = Modules[i]->getGlobalVariable(Name,AllowInternal);
    if (GV && !GV->isDeclaration())
      return GV;
  }
  return nullptr;
}

uint64_t ExecutionEngineState::RemoveMapping(StringRef Name) {
  GlobalAddressMapTy::iterator I = GlobalAddressMap.find(Name);
  uint64_t OldVal;

  // FIXME: This is silly, we shouldn't end up with a mapping -> 0 in the
  // GlobalAddressMap.
  if (I == GlobalAddressMap.end())
    OldVal = 0;
  else {
    GlobalAddressReverseMap.erase(I->second);
    OldVal = I->second;
    GlobalAddressMap.erase(I);
  }

  return OldVal;
}

std::string ExecutionEngine::getMangledName(const GlobalValue *GV) {
  assert(GV->hasName() && "Global must have name.");

  MutexGuard locked(lock);
  SmallString<128> FullName;

  const DataLayout &DL =
    GV->getParent()->getDataLayout().isDefault()
      ? getDataLayout()
      : GV->getParent()->getDataLayout();

  Mangler::getNameWithPrefix(FullName, GV->getName(), DL);
  return FullName.str();
}

void ExecutionEngine::addGlobalMapping(const GlobalValue *GV, void *Addr) {
  MutexGuard locked(lock);
  addGlobalMapping(getMangledName(GV), (uint64_t) Addr);
}

void ExecutionEngine::addGlobalMapping(StringRef Name, uint64_t Addr) {
  MutexGuard locked(lock);

  assert(!Name.empty() && "Empty GlobalMapping symbol name!");

  LLVM_DEBUG(dbgs() << "JIT: Map \'" << Name << "\' to [" << Addr << "]\n";);
  uint64_t &CurVal = EEState.getGlobalAddressMap()[Name];
  assert((!CurVal || !Addr) && "GlobalMapping already established!");
  CurVal = Addr;

  // If we are using the reverse mapping, add it too.
  if (!EEState.getGlobalAddressReverseMap().empty()) {
    std::string &V = EEState.getGlobalAddressReverseMap()[CurVal];
    assert((!V.empty() || !Name.empty()) &&
           "GlobalMapping already established!");
    V = Name;
  }
}

void ExecutionEngine::clearAllGlobalMappings() {
  MutexGuard locked(lock);

  EEState.getGlobalAddressMap().clear();
  EEState.getGlobalAddressReverseMap().clear();
}

void ExecutionEngine::clearGlobalMappingsFromModule(Module *M) {
  MutexGuard locked(lock);

  for (GlobalObject &GO : M->global_objects())
    EEState.RemoveMapping(getMangledName(&GO));
}

uint64_t ExecutionEngine::updateGlobalMapping(const GlobalValue *GV,
                                              void *Addr) {
  MutexGuard locked(lock);
  return updateGlobalMapping(getMangledName(GV), (uint64_t) Addr);
}

uint64_t ExecutionEngine::updateGlobalMapping(StringRef Name, uint64_t Addr) {
  MutexGuard locked(lock);

  ExecutionEngineState::GlobalAddressMapTy &Map =
    EEState.getGlobalAddressMap();

  // Deleting from the mapping?
  if (!Addr)
    return EEState.RemoveMapping(Name);

  uint64_t &CurVal = Map[Name];
  uint64_t OldVal = CurVal;

  if (CurVal && !EEState.getGlobalAddressReverseMap().empty())
    EEState.getGlobalAddressReverseMap().erase(CurVal);
  CurVal = Addr;

  // If we are using the reverse mapping, add it too.
  if (!EEState.getGlobalAddressReverseMap().empty()) {
    std::string &V = EEState.getGlobalAddressReverseMap()[CurVal];
    assert((!V.empty() || !Name.empty()) &&
           "GlobalMapping already established!");
    V = Name;
  }
  return OldVal;
}

uint64_t ExecutionEngine::getAddressToGlobalIfAvailable(StringRef S) {
  MutexGuard locked(lock);
  uint64_t Address = 0;
  ExecutionEngineState::GlobalAddressMapTy::iterator I =
    EEState.getGlobalAddressMap().find(S);
  if (I != EEState.getGlobalAddressMap().end())
    Address = I->second;
  return Address;
}


void *ExecutionEngine::getPointerToGlobalIfAvailable(StringRef S) {
  MutexGuard locked(lock);
  if (void* Address = (void *) getAddressToGlobalIfAvailable(S))
    return Address;
  return nullptr;
}

void *ExecutionEngine::getPointerToGlobalIfAvailable(const GlobalValue *GV) {
  MutexGuard locked(lock);
  return getPointerToGlobalIfAvailable(getMangledName(GV));
}

const GlobalValue *ExecutionEngine::getGlobalValueAtAddress(void *Addr) {
  MutexGuard locked(lock);

  // If we haven't computed the reverse mapping yet, do so first.
  if (EEState.getGlobalAddressReverseMap().empty()) {
    for (ExecutionEngineState::GlobalAddressMapTy::iterator
           I = EEState.getGlobalAddressMap().begin(),
           E = EEState.getGlobalAddressMap().end(); I != E; ++I) {
      StringRef Name = I->first();
      uint64_t Addr = I->second;
      EEState.getGlobalAddressReverseMap().insert(std::make_pair(
                                                          Addr, Name));
    }
  }

  std::map<uint64_t, std::string>::iterator I =
    EEState.getGlobalAddressReverseMap().find((uint64_t) Addr);

  if (I != EEState.getGlobalAddressReverseMap().end()) {
    StringRef Name = I->second;
    for (unsigned i = 0, e = Modules.size(); i != e; ++i)
      if (GlobalValue *GV = Modules[i]->getNamedValue(Name))
        return GV;
  }
  return nullptr;
}

namespace {
class ArgvArray {
  std::unique_ptr<char[]> Array;
  std::vector<std::unique_ptr<char[]>> Values;
public:
  /// Turn a vector of strings into a nice argv style array of pointers to null
  /// terminated strings.
  void *reset(LLVMContext &C, ExecutionEngine *EE,
              const std::vector<std::string> &InputArgv);
};
}  // anonymous namespace
void *ArgvArray::reset(LLVMContext &C, ExecutionEngine *EE,
                       const std::vector<std::string> &InputArgv) {
  Values.clear();  // Free the old contents.
  Values.reserve(InputArgv.size());
  unsigned PtrSize = EE->getDataLayout().getPointerSize();
  Array = make_unique<char[]>((InputArgv.size()+1)*PtrSize);

  LLVM_DEBUG(dbgs() << "JIT: ARGV = " << (void *)Array.get() << "\n");
  Type *SBytePtr = Type::getInt8PtrTy(C);

  for (unsigned i = 0; i != InputArgv.size(); ++i) {
    unsigned Size = InputArgv[i].size()+1;
    auto Dest = make_unique<char[]>(Size);
    LLVM_DEBUG(dbgs() << "JIT: ARGV[" << i << "] = " << (void *)Dest.get()
                      << "\n");

    std::copy(InputArgv[i].begin(), InputArgv[i].end(), Dest.get());
    Dest[Size-1] = 0;

    // Endian safe: Array[i] = (PointerTy)Dest;
    EE->StoreValueToMemory(PTOGV(Dest.get()),
                           (GenericValue*)(&Array[i*PtrSize]), SBytePtr);
    Values.push_back(std::move(Dest));
  }

  // Null terminate it
  EE->StoreValueToMemory(PTOGV(nullptr),
                         (GenericValue*)(&Array[InputArgv.size()*PtrSize]),
                         SBytePtr);
  return Array.get();
}

void ExecutionEngine::runStaticConstructorsDestructors(Module &module,
                                                       bool isDtors) {
  StringRef Name(isDtors ? "llvm.global_dtors" : "llvm.global_ctors");
  GlobalVariable *GV = module.getNamedGlobal(Name);

  // If this global has internal linkage, or if it has a use, then it must be
  // an old-style (llvmgcc3) static ctor with __main linked in and in use.  If
  // this is the case, don't execute any of the global ctors, __main will do
  // it.
  if (!GV || GV->isDeclaration() || GV->hasLocalLinkage()) return;

  // Should be an array of '{ i32, void ()* }' structs.  The first value is
  // the init priority, which we ignore.
  ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList)
    return;
  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i) {
    ConstantStruct *CS = dyn_cast<ConstantStruct>(InitList->getOperand(i));
    if (!CS) continue;

    Constant *FP = CS->getOperand(1);
    if (FP->isNullValue())
      continue;  // Found a sentinal value, ignore.

    // Strip off constant expression casts.
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(FP))
      if (CE->isCast())
        FP = CE->getOperand(0);

    // Execute the ctor/dtor function!
    if (Function *F = dyn_cast<Function>(FP))
      runFunction(F, None);

    // FIXME: It is marginally lame that we just do nothing here if we see an
    // entry we don't recognize. It might not be unreasonable for the verifier
    // to not even allow this and just assert here.
  }
}

void ExecutionEngine::runStaticConstructorsDestructors(bool isDtors) {
  // Execute global ctors/dtors for each module in the program.
  for (std::unique_ptr<Module> &M : Modules)
    runStaticConstructorsDestructors(*M, isDtors);
}

#ifndef NDEBUG
/// isTargetNullPtr - Return whether the target pointer stored at Loc is null.
static bool isTargetNullPtr(ExecutionEngine *EE, void *Loc) {
  unsigned PtrSize = EE->getDataLayout().getPointerSize();
  for (unsigned i = 0; i < PtrSize; ++i)
    if (*(i + (uint8_t*)Loc))
      return false;
  return true;
}
#endif

int ExecutionEngine::runFunctionAsMain(Function *Fn,
                                       const std::vector<std::string> &argv,
                                       const char * const * envp) {
  std::vector<GenericValue> GVArgs;
  GenericValue GVArgc;
  GVArgc.IntVal = APInt(32, argv.size());

  // Check main() type
  unsigned NumArgs = Fn->getFunctionType()->getNumParams();
  FunctionType *FTy = Fn->getFunctionType();
  Type* PPInt8Ty = Type::getInt8PtrTy(Fn->getContext())->getPointerTo();

  // Check the argument types.
  if (NumArgs > 3)
    report_fatal_error("Invalid number of arguments of main() supplied");
  if (NumArgs >= 3 && FTy->getParamType(2) != PPInt8Ty)
    report_fatal_error("Invalid type for third argument of main() supplied");
  if (NumArgs >= 2 && FTy->getParamType(1) != PPInt8Ty)
    report_fatal_error("Invalid type for second argument of main() supplied");
  if (NumArgs >= 1 && !FTy->getParamType(0)->isIntegerTy(32))
    report_fatal_error("Invalid type for first argument of main() supplied");
  if (!FTy->getReturnType()->isIntegerTy() &&
      !FTy->getReturnType()->isVoidTy())
    report_fatal_error("Invalid return type of main() supplied");

  ArgvArray CArgv;
  ArgvArray CEnv;
  if (NumArgs) {
    GVArgs.push_back(GVArgc); // Arg #0 = argc.
    if (NumArgs > 1) {
      // Arg #1 = argv.
      GVArgs.push_back(PTOGV(CArgv.reset(Fn->getContext(), this, argv)));
      assert(!isTargetNullPtr(this, GVTOP(GVArgs[1])) &&
             "argv[0] was null after CreateArgv");
      if (NumArgs > 2) {
        std::vector<std::string> EnvVars;
        for (unsigned i = 0; envp[i]; ++i)
          EnvVars.emplace_back(envp[i]);
        // Arg #2 = envp.
        GVArgs.push_back(PTOGV(CEnv.reset(Fn->getContext(), this, EnvVars)));
      }
    }
  }

  return runFunction(Fn, GVArgs).IntVal.getZExtValue();
}

EngineBuilder::EngineBuilder() : EngineBuilder(nullptr) {}

EngineBuilder::EngineBuilder(std::unique_ptr<Module> M)
    : M(std::move(M)), WhichEngine(EngineKind::Either), ErrorStr(nullptr),
      OptLevel(CodeGenOpt::Default), MemMgr(nullptr), Resolver(nullptr),
      UseOrcMCJITReplacement(false) {
// IR module verification is enabled by default in debug builds, and disabled
// by default in release builds.
#ifndef NDEBUG
  VerifyModules = true;
#else
  VerifyModules = false;
#endif
}

EngineBuilder::~EngineBuilder() = default;

EngineBuilder &EngineBuilder::setMCJITMemoryManager(
                                   std::unique_ptr<RTDyldMemoryManager> mcjmm) {
  auto SharedMM = std::shared_ptr<RTDyldMemoryManager>(std::move(mcjmm));
  MemMgr = SharedMM;
  Resolver = SharedMM;
  return *this;
}

EngineBuilder&
EngineBuilder::setMemoryManager(std::unique_ptr<MCJITMemoryManager> MM) {
  MemMgr = std::shared_ptr<MCJITMemoryManager>(std::move(MM));
  return *this;
}

EngineBuilder &
EngineBuilder::setSymbolResolver(std::unique_ptr<LegacyJITSymbolResolver> SR) {
  Resolver = std::shared_ptr<LegacyJITSymbolResolver>(std::move(SR));
  return *this;
}

ExecutionEngine *EngineBuilder::create(TargetMachine *TM) {
  std::unique_ptr<TargetMachine> TheTM(TM); // Take ownership.

  // Make sure we can resolve symbols in the program as well. The zero arg
  // to the function tells DynamicLibrary to load the program, not a library.
  if (sys::DynamicLibrary::LoadLibraryPermanently(nullptr, ErrorStr))
    return nullptr;

  // If the user specified a memory manager but didn't specify which engine to
  // create, we assume they only want the JIT, and we fail if they only want
  // the interpreter.
  if (MemMgr) {
    if (WhichEngine & EngineKind::JIT)
      WhichEngine = EngineKind::JIT;
    else {
      if (ErrorStr)
        *ErrorStr = "Cannot create an interpreter with a memory manager.";
      return nullptr;
    }
  }

  // Unless the interpreter was explicitly selected or the JIT is not linked,
  // try making a JIT.
  if ((WhichEngine & EngineKind::JIT) && TheTM) {
    if (!TM->getTarget().hasJIT()) {
      errs() << "WARNING: This target JIT is not designed for the host"
             << " you are running.  If bad things happen, please choose"
             << " a different -march switch.\n";
    }

    ExecutionEngine *EE = nullptr;
    if (ExecutionEngine::OrcMCJITReplacementCtor && UseOrcMCJITReplacement) {
      EE = ExecutionEngine::OrcMCJITReplacementCtor(ErrorStr, std::move(MemMgr),
                                                    std::move(Resolver),
                                                    std::move(TheTM));
      EE->addModule(std::move(M));
    } else if (ExecutionEngine::MCJITCtor)
      EE = ExecutionEngine::MCJITCtor(std::move(M), ErrorStr, std::move(MemMgr),
                                      std::move(Resolver), std::move(TheTM));

    if (EE) {
      EE->setVerifyModules(VerifyModules);
      return EE;
    }
  }

  // If we can't make a JIT and we didn't request one specifically, try making
  // an interpreter instead.
  if (WhichEngine & EngineKind::Interpreter) {
    if (ExecutionEngine::InterpCtor)
      return ExecutionEngine::InterpCtor(std::move(M), ErrorStr);
    if (ErrorStr)
      *ErrorStr = "Interpreter has not been linked in.";
    return nullptr;
  }

  if ((WhichEngine & EngineKind::JIT) && !ExecutionEngine::MCJITCtor) {
    if (ErrorStr)
      *ErrorStr = "JIT has not been linked in.";
  }

  return nullptr;
}

void *ExecutionEngine::getPointerToGlobal(const GlobalValue *GV) {
  if (Function *F = const_cast<Function*>(dyn_cast<Function>(GV)))
    return getPointerToFunction(F);

  MutexGuard locked(lock);
  if (void* P = getPointerToGlobalIfAvailable(GV))
    return P;

  // Global variable might have been added since interpreter started.
  if (GlobalVariable *GVar =
          const_cast<GlobalVariable *>(dyn_cast<GlobalVariable>(GV)))
    EmitGlobalVariable(GVar);
  else
    llvm_unreachable("Global hasn't had an address allocated yet!");

  return getPointerToGlobalIfAvailable(GV);
}

/// Converts a Constant* into a GenericValue, including handling of
/// ConstantExpr values.
GenericValue ExecutionEngine::getConstantValue(const Constant *C) {
  // If its undefined, return the garbage.
  if (isa<UndefValue>(C)) {
    GenericValue Result;
    switch (C->getType()->getTypeID()) {
    default:
      break;
    case Type::IntegerTyID:
    case Type::X86_FP80TyID:
    case Type::FP128TyID:
    case Type::PPC_FP128TyID:
      // Although the value is undefined, we still have to construct an APInt
      // with the correct bit width.
      Result.IntVal = APInt(C->getType()->getPrimitiveSizeInBits(), 0);
      break;
    case Type::StructTyID: {
      // if the whole struct is 'undef' just reserve memory for the value.
      if(StructType *STy = dyn_cast<StructType>(C->getType())) {
        unsigned int elemNum = STy->getNumElements();
        Result.AggregateVal.resize(elemNum);
        for (unsigned int i = 0; i < elemNum; ++i) {
          Type *ElemTy = STy->getElementType(i);
          if (ElemTy->isIntegerTy())
            Result.AggregateVal[i].IntVal =
              APInt(ElemTy->getPrimitiveSizeInBits(), 0);
          else if (ElemTy->isAggregateType()) {
              const Constant *ElemUndef = UndefValue::get(ElemTy);
              Result.AggregateVal[i] = getConstantValue(ElemUndef);
            }
          }
        }
      }
      break;
    case Type::VectorTyID:
      // if the whole vector is 'undef' just reserve memory for the value.
      auto* VTy = dyn_cast<VectorType>(C->getType());
      Type *ElemTy = VTy->getElementType();
      unsigned int elemNum = VTy->getNumElements();
      Result.AggregateVal.resize(elemNum);
      if (ElemTy->isIntegerTy())
        for (unsigned int i = 0; i < elemNum; ++i)
          Result.AggregateVal[i].IntVal =
            APInt(ElemTy->getPrimitiveSizeInBits(), 0);
      break;
    }
    return Result;
  }

  // Otherwise, if the value is a ConstantExpr...
  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    Constant *Op0 = CE->getOperand(0);
    switch (CE->getOpcode()) {
    case Instruction::GetElementPtr: {
      // Compute the index
      GenericValue Result = getConstantValue(Op0);
      APInt Offset(DL.getPointerSizeInBits(), 0);
      cast<GEPOperator>(CE)->accumulateConstantOffset(DL, Offset);

      char* tmp = (char*) Result.PointerVal;
      Result = PTOGV(tmp + Offset.getSExtValue());
      return Result;
    }
    case Instruction::Trunc: {
      GenericValue GV = getConstantValue(Op0);
      uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
      GV.IntVal = GV.IntVal.trunc(BitWidth);
      return GV;
    }
    case Instruction::ZExt: {
      GenericValue GV = getConstantValue(Op0);
      uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
      GV.IntVal = GV.IntVal.zext(BitWidth);
      return GV;
    }
    case Instruction::SExt: {
      GenericValue GV = getConstantValue(Op0);
      uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
      GV.IntVal = GV.IntVal.sext(BitWidth);
      return GV;
    }
    case Instruction::FPTrunc: {
      // FIXME long double
      GenericValue GV = getConstantValue(Op0);
      GV.FloatVal = float(GV.DoubleVal);
      return GV;
    }
    case Instruction::FPExt:{
      // FIXME long double
      GenericValue GV = getConstantValue(Op0);
      GV.DoubleVal = double(GV.FloatVal);
      return GV;
    }
    case Instruction::UIToFP: {
      GenericValue GV = getConstantValue(Op0);
      if (CE->getType()->isFloatTy())
        GV.FloatVal = float(GV.IntVal.roundToDouble());
      else if (CE->getType()->isDoubleTy())
        GV.DoubleVal = GV.IntVal.roundToDouble();
      else if (CE->getType()->isX86_FP80Ty()) {
        APFloat apf = APFloat::getZero(APFloat::x87DoubleExtended());
        (void)apf.convertFromAPInt(GV.IntVal,
                                   false,
                                   APFloat::rmNearestTiesToEven);
        GV.IntVal = apf.bitcastToAPInt();
      }
      return GV;
    }
    case Instruction::SIToFP: {
      GenericValue GV = getConstantValue(Op0);
      if (CE->getType()->isFloatTy())
        GV.FloatVal = float(GV.IntVal.signedRoundToDouble());
      else if (CE->getType()->isDoubleTy())
        GV.DoubleVal = GV.IntVal.signedRoundToDouble();
      else if (CE->getType()->isX86_FP80Ty()) {
        APFloat apf = APFloat::getZero(APFloat::x87DoubleExtended());
        (void)apf.convertFromAPInt(GV.IntVal,
                                   true,
                                   APFloat::rmNearestTiesToEven);
        GV.IntVal = apf.bitcastToAPInt();
      }
      return GV;
    }
    case Instruction::FPToUI: // double->APInt conversion handles sign
    case Instruction::FPToSI: {
      GenericValue GV = getConstantValue(Op0);
      uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
      if (Op0->getType()->isFloatTy())
        GV.IntVal = APIntOps::RoundFloatToAPInt(GV.FloatVal, BitWidth);
      else if (Op0->getType()->isDoubleTy())
        GV.IntVal = APIntOps::RoundDoubleToAPInt(GV.DoubleVal, BitWidth);
      else if (Op0->getType()->isX86_FP80Ty()) {
        APFloat apf = APFloat(APFloat::x87DoubleExtended(), GV.IntVal);
        uint64_t v;
        bool ignored;
        (void)apf.convertToInteger(makeMutableArrayRef(v), BitWidth,
                                   CE->getOpcode()==Instruction::FPToSI,
                                   APFloat::rmTowardZero, &ignored);
        GV.IntVal = v; // endian?
      }
      return GV;
    }
    case Instruction::PtrToInt: {
      GenericValue GV = getConstantValue(Op0);
      uint32_t PtrWidth = DL.getTypeSizeInBits(Op0->getType());
      assert(PtrWidth <= 64 && "Bad pointer width");
      GV.IntVal = APInt(PtrWidth, uintptr_t(GV.PointerVal));
      uint32_t IntWidth = DL.getTypeSizeInBits(CE->getType());
      GV.IntVal = GV.IntVal.zextOrTrunc(IntWidth);
      return GV;
    }
    case Instruction::IntToPtr: {
      GenericValue GV = getConstantValue(Op0);
      uint32_t PtrWidth = DL.getTypeSizeInBits(CE->getType());
      GV.IntVal = GV.IntVal.zextOrTrunc(PtrWidth);
      assert(GV.IntVal.getBitWidth() <= 64 && "Bad pointer width");
      GV.PointerVal = PointerTy(uintptr_t(GV.IntVal.getZExtValue()));
      return GV;
    }
    case Instruction::BitCast: {
      GenericValue GV = getConstantValue(Op0);
      Type* DestTy = CE->getType();
      switch (Op0->getType()->getTypeID()) {
        default: llvm_unreachable("Invalid bitcast operand");
        case Type::IntegerTyID:
          assert(DestTy->isFloatingPointTy() && "invalid bitcast");
          if (DestTy->isFloatTy())
            GV.FloatVal = GV.IntVal.bitsToFloat();
          else if (DestTy->isDoubleTy())
            GV.DoubleVal = GV.IntVal.bitsToDouble();
          break;
        case Type::FloatTyID:
          assert(DestTy->isIntegerTy(32) && "Invalid bitcast");
          GV.IntVal = APInt::floatToBits(GV.FloatVal);
          break;
        case Type::DoubleTyID:
          assert(DestTy->isIntegerTy(64) && "Invalid bitcast");
          GV.IntVal = APInt::doubleToBits(GV.DoubleVal);
          break;
        case Type::PointerTyID:
          assert(DestTy->isPointerTy() && "Invalid bitcast");
          break; // getConstantValue(Op0)  above already converted it
      }
      return GV;
    }
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
      GenericValue LHS = getConstantValue(Op0);
      GenericValue RHS = getConstantValue(CE->getOperand(1));
      GenericValue GV;
      switch (CE->getOperand(0)->getType()->getTypeID()) {
      default: llvm_unreachable("Bad add type!");
      case Type::IntegerTyID:
        switch (CE->getOpcode()) {
          default: llvm_unreachable("Invalid integer opcode");
          case Instruction::Add: GV.IntVal = LHS.IntVal + RHS.IntVal; break;
          case Instruction::Sub: GV.IntVal = LHS.IntVal - RHS.IntVal; break;
          case Instruction::Mul: GV.IntVal = LHS.IntVal * RHS.IntVal; break;
          case Instruction::UDiv:GV.IntVal = LHS.IntVal.udiv(RHS.IntVal); break;
          case Instruction::SDiv:GV.IntVal = LHS.IntVal.sdiv(RHS.IntVal); break;
          case Instruction::URem:GV.IntVal = LHS.IntVal.urem(RHS.IntVal); break;
          case Instruction::SRem:GV.IntVal = LHS.IntVal.srem(RHS.IntVal); break;
          case Instruction::And: GV.IntVal = LHS.IntVal & RHS.IntVal; break;
          case Instruction::Or:  GV.IntVal = LHS.IntVal | RHS.IntVal; break;
          case Instruction::Xor: GV.IntVal = LHS.IntVal ^ RHS.IntVal; break;
        }
        break;
      case Type::FloatTyID:
        switch (CE->getOpcode()) {
          default: llvm_unreachable("Invalid float opcode");
          case Instruction::FAdd:
            GV.FloatVal = LHS.FloatVal + RHS.FloatVal; break;
          case Instruction::FSub:
            GV.FloatVal = LHS.FloatVal - RHS.FloatVal; break;
          case Instruction::FMul:
            GV.FloatVal = LHS.FloatVal * RHS.FloatVal; break;
          case Instruction::FDiv:
            GV.FloatVal = LHS.FloatVal / RHS.FloatVal; break;
          case Instruction::FRem:
            GV.FloatVal = std::fmod(LHS.FloatVal,RHS.FloatVal); break;
        }
        break;
      case Type::DoubleTyID:
        switch (CE->getOpcode()) {
          default: llvm_unreachable("Invalid double opcode");
          case Instruction::FAdd:
            GV.DoubleVal = LHS.DoubleVal + RHS.DoubleVal; break;
          case Instruction::FSub:
            GV.DoubleVal = LHS.DoubleVal - RHS.DoubleVal; break;
          case Instruction::FMul:
            GV.DoubleVal = LHS.DoubleVal * RHS.DoubleVal; break;
          case Instruction::FDiv:
            GV.DoubleVal = LHS.DoubleVal / RHS.DoubleVal; break;
          case Instruction::FRem:
            GV.DoubleVal = std::fmod(LHS.DoubleVal,RHS.DoubleVal); break;
        }
        break;
      case Type::X86_FP80TyID:
      case Type::PPC_FP128TyID:
      case Type::FP128TyID: {
        const fltSemantics &Sem = CE->getOperand(0)->getType()->getFltSemantics();
        APFloat apfLHS = APFloat(Sem, LHS.IntVal);
        switch (CE->getOpcode()) {
          default: llvm_unreachable("Invalid long double opcode");
          case Instruction::FAdd:
            apfLHS.add(APFloat(Sem, RHS.IntVal), APFloat::rmNearestTiesToEven);
            GV.IntVal = apfLHS.bitcastToAPInt();
            break;
          case Instruction::FSub:
            apfLHS.subtract(APFloat(Sem, RHS.IntVal),
                            APFloat::rmNearestTiesToEven);
            GV.IntVal = apfLHS.bitcastToAPInt();
            break;
          case Instruction::FMul:
            apfLHS.multiply(APFloat(Sem, RHS.IntVal),
                            APFloat::rmNearestTiesToEven);
            GV.IntVal = apfLHS.bitcastToAPInt();
            break;
          case Instruction::FDiv:
            apfLHS.divide(APFloat(Sem, RHS.IntVal),
                          APFloat::rmNearestTiesToEven);
            GV.IntVal = apfLHS.bitcastToAPInt();
            break;
          case Instruction::FRem:
            apfLHS.mod(APFloat(Sem, RHS.IntVal));
            GV.IntVal = apfLHS.bitcastToAPInt();
            break;
          }
        }
        break;
      }
      return GV;
    }
    default:
      break;
    }

    SmallString<256> Msg;
    raw_svector_ostream OS(Msg);
    OS << "ConstantExpr not handled: " << *CE;
    report_fatal_error(OS.str());
  }

  // Otherwise, we have a simple constant.
  GenericValue Result;
  switch (C->getType()->getTypeID()) {
  case Type::FloatTyID:
    Result.FloatVal = cast<ConstantFP>(C)->getValueAPF().convertToFloat();
    break;
  case Type::DoubleTyID:
    Result.DoubleVal = cast<ConstantFP>(C)->getValueAPF().convertToDouble();
    break;
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
    Result.IntVal = cast <ConstantFP>(C)->getValueAPF().bitcastToAPInt();
    break;
  case Type::IntegerTyID:
    Result.IntVal = cast<ConstantInt>(C)->getValue();
    break;
  case Type::PointerTyID:
    while (auto *A = dyn_cast<GlobalAlias>(C)) {
      C = A->getAliasee();
    }
    if (isa<ConstantPointerNull>(C))
      Result.PointerVal = nullptr;
    else if (const Function *F = dyn_cast<Function>(C))
      Result = PTOGV(getPointerToFunctionOrStub(const_cast<Function*>(F)));
    else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(C))
      Result = PTOGV(getOrEmitGlobalVariable(const_cast<GlobalVariable*>(GV)));
    else
      llvm_unreachable("Unknown constant pointer type!");
    break;
  case Type::VectorTyID: {
    unsigned elemNum;
    Type* ElemTy;
    const ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(C);
    const ConstantVector *CV = dyn_cast<ConstantVector>(C);
    const ConstantAggregateZero *CAZ = dyn_cast<ConstantAggregateZero>(C);

    if (CDV) {
        elemNum = CDV->getNumElements();
        ElemTy = CDV->getElementType();
    } else if (CV || CAZ) {
        VectorType* VTy = dyn_cast<VectorType>(C->getType());
        elemNum = VTy->getNumElements();
        ElemTy = VTy->getElementType();
    } else {
        llvm_unreachable("Unknown constant vector type!");
    }

    Result.AggregateVal.resize(elemNum);
    // Check if vector holds floats.
    if(ElemTy->isFloatTy()) {
      if (CAZ) {
        GenericValue floatZero;
        floatZero.FloatVal = 0.f;
        std::fill(Result.AggregateVal.begin(), Result.AggregateVal.end(),
                  floatZero);
        break;
      }
      if(CV) {
        for (unsigned i = 0; i < elemNum; ++i)
          if (!isa<UndefValue>(CV->getOperand(i)))
            Result.AggregateVal[i].FloatVal = cast<ConstantFP>(
              CV->getOperand(i))->getValueAPF().convertToFloat();
        break;
      }
      if(CDV)
        for (unsigned i = 0; i < elemNum; ++i)
          Result.AggregateVal[i].FloatVal = CDV->getElementAsFloat(i);

      break;
    }
    // Check if vector holds doubles.
    if (ElemTy->isDoubleTy()) {
      if (CAZ) {
        GenericValue doubleZero;
        doubleZero.DoubleVal = 0.0;
        std::fill(Result.AggregateVal.begin(), Result.AggregateVal.end(),
                  doubleZero);
        break;
      }
      if(CV) {
        for (unsigned i = 0; i < elemNum; ++i)
          if (!isa<UndefValue>(CV->getOperand(i)))
            Result.AggregateVal[i].DoubleVal = cast<ConstantFP>(
              CV->getOperand(i))->getValueAPF().convertToDouble();
        break;
      }
      if(CDV)
        for (unsigned i = 0; i < elemNum; ++i)
          Result.AggregateVal[i].DoubleVal = CDV->getElementAsDouble(i);

      break;
    }
    // Check if vector holds integers.
    if (ElemTy->isIntegerTy()) {
      if (CAZ) {
        GenericValue intZero;
        intZero.IntVal = APInt(ElemTy->getScalarSizeInBits(), 0ull);
        std::fill(Result.AggregateVal.begin(), Result.AggregateVal.end(),
                  intZero);
        break;
      }
      if(CV) {
        for (unsigned i = 0; i < elemNum; ++i)
          if (!isa<UndefValue>(CV->getOperand(i)))
            Result.AggregateVal[i].IntVal = cast<ConstantInt>(
                                            CV->getOperand(i))->getValue();
          else {
            Result.AggregateVal[i].IntVal =
              APInt(CV->getOperand(i)->getType()->getPrimitiveSizeInBits(), 0);
          }
        break;
      }
      if(CDV)
        for (unsigned i = 0; i < elemNum; ++i)
          Result.AggregateVal[i].IntVal = APInt(
            CDV->getElementType()->getPrimitiveSizeInBits(),
            CDV->getElementAsInteger(i));

      break;
    }
    llvm_unreachable("Unknown constant pointer type!");
  }
  break;

  default:
    SmallString<256> Msg;
    raw_svector_ostream OS(Msg);
    OS << "ERROR: Constant unimplemented for type: " << *C->getType();
    report_fatal_error(OS.str());
  }

  return Result;
}

/// StoreIntToMemory - Fills the StoreBytes bytes of memory starting from Dst
/// with the integer held in IntVal.
static void StoreIntToMemory(const APInt &IntVal, uint8_t *Dst,
                             unsigned StoreBytes) {
  assert((IntVal.getBitWidth()+7)/8 >= StoreBytes && "Integer too small!");
  const uint8_t *Src = (const uint8_t *)IntVal.getRawData();

  if (sys::IsLittleEndianHost) {
    // Little-endian host - the source is ordered from LSB to MSB.  Order the
    // destination from LSB to MSB: Do a straight copy.
    memcpy(Dst, Src, StoreBytes);
  } else {
    // Big-endian host - the source is an array of 64 bit words ordered from
    // LSW to MSW.  Each word is ordered from MSB to LSB.  Order the destination
    // from MSB to LSB: Reverse the word order, but not the bytes in a word.
    while (StoreBytes > sizeof(uint64_t)) {
      StoreBytes -= sizeof(uint64_t);
      // May not be aligned so use memcpy.
      memcpy(Dst + StoreBytes, Src, sizeof(uint64_t));
      Src += sizeof(uint64_t);
    }

    memcpy(Dst, Src + sizeof(uint64_t) - StoreBytes, StoreBytes);
  }
}

void ExecutionEngine::StoreValueToMemory(const GenericValue &Val,
                                         GenericValue *Ptr, Type *Ty) {
  const unsigned StoreBytes = getDataLayout().getTypeStoreSize(Ty);

  switch (Ty->getTypeID()) {
  default:
    dbgs() << "Cannot store value of type " << *Ty << "!\n";
    break;
  case Type::IntegerTyID:
    StoreIntToMemory(Val.IntVal, (uint8_t*)Ptr, StoreBytes);
    break;
  case Type::FloatTyID:
    *((float*)Ptr) = Val.FloatVal;
    break;
  case Type::DoubleTyID:
    *((double*)Ptr) = Val.DoubleVal;
    break;
  case Type::X86_FP80TyID:
    memcpy(Ptr, Val.IntVal.getRawData(), 10);
    break;
  case Type::PointerTyID:
    // Ensure 64 bit target pointers are fully initialized on 32 bit hosts.
    if (StoreBytes != sizeof(PointerTy))
      memset(&(Ptr->PointerVal), 0, StoreBytes);

    *((PointerTy*)Ptr) = Val.PointerVal;
    break;
  case Type::VectorTyID:
    for (unsigned i = 0; i < Val.AggregateVal.size(); ++i) {
      if (cast<VectorType>(Ty)->getElementType()->isDoubleTy())
        *(((double*)Ptr)+i) = Val.AggregateVal[i].DoubleVal;
      if (cast<VectorType>(Ty)->getElementType()->isFloatTy())
        *(((float*)Ptr)+i) = Val.AggregateVal[i].FloatVal;
      if (cast<VectorType>(Ty)->getElementType()->isIntegerTy()) {
        unsigned numOfBytes =(Val.AggregateVal[i].IntVal.getBitWidth()+7)/8;
        StoreIntToMemory(Val.AggregateVal[i].IntVal,
          (uint8_t*)Ptr + numOfBytes*i, numOfBytes);
      }
    }
    break;
  }

  if (sys::IsLittleEndianHost != getDataLayout().isLittleEndian())
    // Host and target are different endian - reverse the stored bytes.
    std::reverse((uint8_t*)Ptr, StoreBytes + (uint8_t*)Ptr);
}

/// LoadIntFromMemory - Loads the integer stored in the LoadBytes bytes starting
/// from Src into IntVal, which is assumed to be wide enough and to hold zero.
static void LoadIntFromMemory(APInt &IntVal, uint8_t *Src, unsigned LoadBytes) {
  assert((IntVal.getBitWidth()+7)/8 >= LoadBytes && "Integer too small!");
  uint8_t *Dst = reinterpret_cast<uint8_t *>(
                   const_cast<uint64_t *>(IntVal.getRawData()));

  if (sys::IsLittleEndianHost)
    // Little-endian host - the destination must be ordered from LSB to MSB.
    // The source is ordered from LSB to MSB: Do a straight copy.
    memcpy(Dst, Src, LoadBytes);
  else {
    // Big-endian - the destination is an array of 64 bit words ordered from
    // LSW to MSW.  Each word must be ordered from MSB to LSB.  The source is
    // ordered from MSB to LSB: Reverse the word order, but not the bytes in
    // a word.
    while (LoadBytes > sizeof(uint64_t)) {
      LoadBytes -= sizeof(uint64_t);
      // May not be aligned so use memcpy.
      memcpy(Dst, Src + LoadBytes, sizeof(uint64_t));
      Dst += sizeof(uint64_t);
    }

    memcpy(Dst + sizeof(uint64_t) - LoadBytes, Src, LoadBytes);
  }
}

/// FIXME: document
///
void ExecutionEngine::LoadValueFromMemory(GenericValue &Result,
                                          GenericValue *Ptr,
                                          Type *Ty) {
  const unsigned LoadBytes = getDataLayout().getTypeStoreSize(Ty);

  switch (Ty->getTypeID()) {
  case Type::IntegerTyID:
    // An APInt with all words initially zero.
    Result.IntVal = APInt(cast<IntegerType>(Ty)->getBitWidth(), 0);
    LoadIntFromMemory(Result.IntVal, (uint8_t*)Ptr, LoadBytes);
    break;
  case Type::FloatTyID:
    Result.FloatVal = *((float*)Ptr);
    break;
  case Type::DoubleTyID:
    Result.DoubleVal = *((double*)Ptr);
    break;
  case Type::PointerTyID:
    Result.PointerVal = *((PointerTy*)Ptr);
    break;
  case Type::X86_FP80TyID: {
    // This is endian dependent, but it will only work on x86 anyway.
    // FIXME: Will not trap if loading a signaling NaN.
    uint64_t y[2];
    memcpy(y, Ptr, 10);
    Result.IntVal = APInt(80, y);
    break;
  }
  case Type::VectorTyID: {
    auto *VT = cast<VectorType>(Ty);
    Type *ElemT = VT->getElementType();
    const unsigned numElems = VT->getNumElements();
    if (ElemT->isFloatTy()) {
      Result.AggregateVal.resize(numElems);
      for (unsigned i = 0; i < numElems; ++i)
        Result.AggregateVal[i].FloatVal = *((float*)Ptr+i);
    }
    if (ElemT->isDoubleTy()) {
      Result.AggregateVal.resize(numElems);
      for (unsigned i = 0; i < numElems; ++i)
        Result.AggregateVal[i].DoubleVal = *((double*)Ptr+i);
    }
    if (ElemT->isIntegerTy()) {
      GenericValue intZero;
      const unsigned elemBitWidth = cast<IntegerType>(ElemT)->getBitWidth();
      intZero.IntVal = APInt(elemBitWidth, 0);
      Result.AggregateVal.resize(numElems, intZero);
      for (unsigned i = 0; i < numElems; ++i)
        LoadIntFromMemory(Result.AggregateVal[i].IntVal,
          (uint8_t*)Ptr+((elemBitWidth+7)/8)*i, (elemBitWidth+7)/8);
    }
  break;
  }
  default:
    SmallString<256> Msg;
    raw_svector_ostream OS(Msg);
    OS << "Cannot load value of type " << *Ty << "!";
    report_fatal_error(OS.str());
  }
}

void ExecutionEngine::InitializeMemory(const Constant *Init, void *Addr) {
  LLVM_DEBUG(dbgs() << "JIT: Initializing " << Addr << " ");
  LLVM_DEBUG(Init->dump());
  if (isa<UndefValue>(Init))
    return;

  if (const ConstantVector *CP = dyn_cast<ConstantVector>(Init)) {
    unsigned ElementSize =
        getDataLayout().getTypeAllocSize(CP->getType()->getElementType());
    for (unsigned i = 0, e = CP->getNumOperands(); i != e; ++i)
      InitializeMemory(CP->getOperand(i), (char*)Addr+i*ElementSize);
    return;
  }

  if (isa<ConstantAggregateZero>(Init)) {
    memset(Addr, 0, (size_t)getDataLayout().getTypeAllocSize(Init->getType()));
    return;
  }

  if (const ConstantArray *CPA = dyn_cast<ConstantArray>(Init)) {
    unsigned ElementSize =
        getDataLayout().getTypeAllocSize(CPA->getType()->getElementType());
    for (unsigned i = 0, e = CPA->getNumOperands(); i != e; ++i)
      InitializeMemory(CPA->getOperand(i), (char*)Addr+i*ElementSize);
    return;
  }

  if (const ConstantStruct *CPS = dyn_cast<ConstantStruct>(Init)) {
    const StructLayout *SL =
        getDataLayout().getStructLayout(cast<StructType>(CPS->getType()));
    for (unsigned i = 0, e = CPS->getNumOperands(); i != e; ++i)
      InitializeMemory(CPS->getOperand(i), (char*)Addr+SL->getElementOffset(i));
    return;
  }

  if (const ConstantDataSequential *CDS =
               dyn_cast<ConstantDataSequential>(Init)) {
    // CDS is already laid out in host memory order.
    StringRef Data = CDS->getRawDataValues();
    memcpy(Addr, Data.data(), Data.size());
    return;
  }

  if (Init->getType()->isFirstClassType()) {
    GenericValue Val = getConstantValue(Init);
    StoreValueToMemory(Val, (GenericValue*)Addr, Init->getType());
    return;
  }

  LLVM_DEBUG(dbgs() << "Bad Type: " << *Init->getType() << "\n");
  llvm_unreachable("Unknown constant type to initialize memory with!");
}

/// EmitGlobals - Emit all of the global variables to memory, storing their
/// addresses into GlobalAddress.  This must make sure to copy the contents of
/// their initializers into the memory.
void ExecutionEngine::emitGlobals() {
  // Loop over all of the global variables in the program, allocating the memory
  // to hold them.  If there is more than one module, do a prepass over globals
  // to figure out how the different modules should link together.
  std::map<std::pair<std::string, Type*>,
           const GlobalValue*> LinkedGlobalsMap;

  if (Modules.size() != 1) {
    for (unsigned m = 0, e = Modules.size(); m != e; ++m) {
      Module &M = *Modules[m];
      for (const auto &GV : M.globals()) {
        if (GV.hasLocalLinkage() || GV.isDeclaration() ||
            GV.hasAppendingLinkage() || !GV.hasName())
          continue;// Ignore external globals and globals with internal linkage.

        const GlobalValue *&GVEntry =
          LinkedGlobalsMap[std::make_pair(GV.getName(), GV.getType())];

        // If this is the first time we've seen this global, it is the canonical
        // version.
        if (!GVEntry) {
          GVEntry = &GV;
          continue;
        }

        // If the existing global is strong, never replace it.
        if (GVEntry->hasExternalLinkage())
          continue;

        // Otherwise, we know it's linkonce/weak, replace it if this is a strong
        // symbol.  FIXME is this right for common?
        if (GV.hasExternalLinkage() || GVEntry->hasExternalWeakLinkage())
          GVEntry = &GV;
      }
    }
  }

  std::vector<const GlobalValue*> NonCanonicalGlobals;
  for (unsigned m = 0, e = Modules.size(); m != e; ++m) {
    Module &M = *Modules[m];
    for (const auto &GV : M.globals()) {
      // In the multi-module case, see what this global maps to.
      if (!LinkedGlobalsMap.empty()) {
        if (const GlobalValue *GVEntry =
              LinkedGlobalsMap[std::make_pair(GV.getName(), GV.getType())]) {
          // If something else is the canonical global, ignore this one.
          if (GVEntry != &GV) {
            NonCanonicalGlobals.push_back(&GV);
            continue;
          }
        }
      }

      if (!GV.isDeclaration()) {
        addGlobalMapping(&GV, getMemoryForGV(&GV));
      } else {
        // External variable reference. Try to use the dynamic loader to
        // get a pointer to it.
        if (void *SymAddr =
            sys::DynamicLibrary::SearchForAddressOfSymbol(GV.getName()))
          addGlobalMapping(&GV, SymAddr);
        else {
          report_fatal_error("Could not resolve external global address: "
                            +GV.getName());
        }
      }
    }

    // If there are multiple modules, map the non-canonical globals to their
    // canonical location.
    if (!NonCanonicalGlobals.empty()) {
      for (unsigned i = 0, e = NonCanonicalGlobals.size(); i != e; ++i) {
        const GlobalValue *GV = NonCanonicalGlobals[i];
        const GlobalValue *CGV =
          LinkedGlobalsMap[std::make_pair(GV->getName(), GV->getType())];
        void *Ptr = getPointerToGlobalIfAvailable(CGV);
        assert(Ptr && "Canonical global wasn't codegen'd!");
        addGlobalMapping(GV, Ptr);
      }
    }

    // Now that all of the globals are set up in memory, loop through them all
    // and initialize their contents.
    for (const auto &GV : M.globals()) {
      if (!GV.isDeclaration()) {
        if (!LinkedGlobalsMap.empty()) {
          if (const GlobalValue *GVEntry =
                LinkedGlobalsMap[std::make_pair(GV.getName(), GV.getType())])
            if (GVEntry != &GV)  // Not the canonical variable.
              continue;
        }
        EmitGlobalVariable(&GV);
      }
    }
  }
}

// EmitGlobalVariable - This method emits the specified global variable to the
// address specified in GlobalAddresses, or allocates new memory if it's not
// already in the map.
void ExecutionEngine::EmitGlobalVariable(const GlobalVariable *GV) {
  void *GA = getPointerToGlobalIfAvailable(GV);

  if (!GA) {
    // If it's not already specified, allocate memory for the global.
    GA = getMemoryForGV(GV);

    // If we failed to allocate memory for this global, return.
    if (!GA) return;

    addGlobalMapping(GV, GA);
  }

  // Don't initialize if it's thread local, let the client do it.
  if (!GV->isThreadLocal())
    InitializeMemory(GV->getInitializer(), GA);

  Type *ElTy = GV->getValueType();
  size_t GVSize = (size_t)getDataLayout().getTypeAllocSize(ElTy);
  NumInitBytes += (unsigned)GVSize;
  ++NumGlobals;
}
