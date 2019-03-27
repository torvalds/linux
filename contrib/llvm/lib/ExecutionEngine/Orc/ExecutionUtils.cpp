//===---- ExecutionUtils.cpp - Utilities for executing functions in Orc ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
namespace orc {

CtorDtorIterator::CtorDtorIterator(const GlobalVariable *GV, bool End)
  : InitList(
      GV ? dyn_cast_or_null<ConstantArray>(GV->getInitializer()) : nullptr),
    I((InitList && End) ? InitList->getNumOperands() : 0) {
}

bool CtorDtorIterator::operator==(const CtorDtorIterator &Other) const {
  assert(InitList == Other.InitList && "Incomparable iterators.");
  return I == Other.I;
}

bool CtorDtorIterator::operator!=(const CtorDtorIterator &Other) const {
  return !(*this == Other);
}

CtorDtorIterator& CtorDtorIterator::operator++() {
  ++I;
  return *this;
}

CtorDtorIterator CtorDtorIterator::operator++(int) {
  CtorDtorIterator Temp = *this;
  ++I;
  return Temp;
}

CtorDtorIterator::Element CtorDtorIterator::operator*() const {
  ConstantStruct *CS = dyn_cast<ConstantStruct>(InitList->getOperand(I));
  assert(CS && "Unrecognized type in llvm.global_ctors/llvm.global_dtors");

  Constant *FuncC = CS->getOperand(1);
  Function *Func = nullptr;

  // Extract function pointer, pulling off any casts.
  while (FuncC) {
    if (Function *F = dyn_cast_or_null<Function>(FuncC)) {
      Func = F;
      break;
    } else if (ConstantExpr *CE = dyn_cast_or_null<ConstantExpr>(FuncC)) {
      if (CE->isCast())
        FuncC = dyn_cast_or_null<ConstantExpr>(CE->getOperand(0));
      else
        break;
    } else {
      // This isn't anything we recognize. Bail out with Func left set to null.
      break;
    }
  }

  ConstantInt *Priority = dyn_cast<ConstantInt>(CS->getOperand(0));
  Value *Data = CS->getNumOperands() == 3 ? CS->getOperand(2) : nullptr;
  if (Data && !isa<GlobalValue>(Data))
    Data = nullptr;
  return Element(Priority->getZExtValue(), Func, Data);
}

iterator_range<CtorDtorIterator> getConstructors(const Module &M) {
  const GlobalVariable *CtorsList = M.getNamedGlobal("llvm.global_ctors");
  return make_range(CtorDtorIterator(CtorsList, false),
                    CtorDtorIterator(CtorsList, true));
}

iterator_range<CtorDtorIterator> getDestructors(const Module &M) {
  const GlobalVariable *DtorsList = M.getNamedGlobal("llvm.global_dtors");
  return make_range(CtorDtorIterator(DtorsList, false),
                    CtorDtorIterator(DtorsList, true));
}

void CtorDtorRunner::add(iterator_range<CtorDtorIterator> CtorDtors) {
  if (empty(CtorDtors))
    return;

  MangleAndInterner Mangle(
      JD.getExecutionSession(),
      (*CtorDtors.begin()).Func->getParent()->getDataLayout());

  for (const auto &CtorDtor : CtorDtors) {
    assert(CtorDtor.Func && CtorDtor.Func->hasName() &&
           "Ctor/Dtor function must be named to be runnable under the JIT");

    // FIXME: Maybe use a symbol promoter here instead.
    if (CtorDtor.Func->hasLocalLinkage()) {
      CtorDtor.Func->setLinkage(GlobalValue::ExternalLinkage);
      CtorDtor.Func->setVisibility(GlobalValue::HiddenVisibility);
    }

    if (CtorDtor.Data && cast<GlobalValue>(CtorDtor.Data)->isDeclaration()) {
      dbgs() << "  Skipping because why now?\n";
      continue;
    }

    CtorDtorsByPriority[CtorDtor.Priority].push_back(
        Mangle(CtorDtor.Func->getName()));
  }
}

Error CtorDtorRunner::run() {
  using CtorDtorTy = void (*)();

  SymbolNameSet Names;

  for (auto &KV : CtorDtorsByPriority) {
    for (auto &Name : KV.second) {
      auto Added = Names.insert(Name).second;
      (void)Added;
      assert(Added && "Ctor/Dtor names clashed");
    }
  }

  auto &ES = JD.getExecutionSession();
  if (auto CtorDtorMap =
          ES.lookup(JITDylibSearchList({{&JD, true}}), std::move(Names),
                    NoDependenciesToRegister, true)) {
    for (auto &KV : CtorDtorsByPriority) {
      for (auto &Name : KV.second) {
        assert(CtorDtorMap->count(Name) && "No entry for Name");
        auto CtorDtor = reinterpret_cast<CtorDtorTy>(
            static_cast<uintptr_t>((*CtorDtorMap)[Name].getAddress()));
        CtorDtor();
      }
    }
    return Error::success();
  } else
    return CtorDtorMap.takeError();

  CtorDtorsByPriority.clear();

  return Error::success();
}

void LocalCXXRuntimeOverridesBase::runDestructors() {
  auto& CXXDestructorDataPairs = DSOHandleOverride;
  for (auto &P : CXXDestructorDataPairs)
    P.first(P.second);
  CXXDestructorDataPairs.clear();
}

int LocalCXXRuntimeOverridesBase::CXAAtExitOverride(DestructorPtr Destructor,
                                                    void *Arg,
                                                    void *DSOHandle) {
  auto& CXXDestructorDataPairs =
    *reinterpret_cast<CXXDestructorDataPairList*>(DSOHandle);
  CXXDestructorDataPairs.push_back(std::make_pair(Destructor, Arg));
  return 0;
}

Error LocalCXXRuntimeOverrides::enable(JITDylib &JD,
                                        MangleAndInterner &Mangle) {
  SymbolMap RuntimeInterposes;
  RuntimeInterposes[Mangle("__dso_handle")] =
    JITEvaluatedSymbol(toTargetAddress(&DSOHandleOverride),
                       JITSymbolFlags::Exported);
  RuntimeInterposes[Mangle("__cxa_atexit")] =
    JITEvaluatedSymbol(toTargetAddress(&CXAAtExitOverride),
                       JITSymbolFlags::Exported);

  return JD.define(absoluteSymbols(std::move(RuntimeInterposes)));
}

DynamicLibrarySearchGenerator::DynamicLibrarySearchGenerator(
    sys::DynamicLibrary Dylib, const DataLayout &DL, SymbolPredicate Allow)
    : Dylib(std::move(Dylib)), Allow(std::move(Allow)),
      GlobalPrefix(DL.getGlobalPrefix()) {}

Expected<DynamicLibrarySearchGenerator>
DynamicLibrarySearchGenerator::Load(const char *FileName, const DataLayout &DL,
                                    SymbolPredicate Allow) {
  std::string ErrMsg;
  auto Lib = sys::DynamicLibrary::getPermanentLibrary(FileName, &ErrMsg);
  if (!Lib.isValid())
    return make_error<StringError>(std::move(ErrMsg), inconvertibleErrorCode());
  return DynamicLibrarySearchGenerator(std::move(Lib), DL, std::move(Allow));
}

SymbolNameSet DynamicLibrarySearchGenerator::
operator()(JITDylib &JD, const SymbolNameSet &Names) {
  orc::SymbolNameSet Added;
  orc::SymbolMap NewSymbols;

  bool HasGlobalPrefix = (GlobalPrefix != '\0');

  for (auto &Name : Names) {
    if ((*Name).empty())
      continue;

    if (Allow && !Allow(Name))
      continue;

    if (HasGlobalPrefix && (*Name).front() != GlobalPrefix)
      continue;

    std::string Tmp((*Name).data() + (HasGlobalPrefix ? 1 : 0), (*Name).size());
    if (void *Addr = Dylib.getAddressOfSymbol(Tmp.c_str())) {
      Added.insert(Name);
      NewSymbols[Name] = JITEvaluatedSymbol(
          static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(Addr)),
          JITSymbolFlags::Exported);
    }
  }

  // Add any new symbols to JD. Since the generator is only called for symbols
  // that are not already defined, this will never trigger a duplicate
  // definition error, so we can wrap this call in a 'cantFail'.
  if (!NewSymbols.empty())
    cantFail(JD.define(absoluteSymbols(std::move(NewSymbols))));

  return Added;
}

} // End namespace orc.
} // End namespace llvm.
