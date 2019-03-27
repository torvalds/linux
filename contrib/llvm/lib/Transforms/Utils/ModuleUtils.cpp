//===-- ModuleUtils.cpp - Functions to manipulate Modules -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on Modules.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static void appendToGlobalArray(const char *Array, Module &M, Function *F,
                                int Priority, Constant *Data) {
  IRBuilder<> IRB(M.getContext());
  FunctionType *FnTy = FunctionType::get(IRB.getVoidTy(), false);

  // Get the current set of static global constructors and add the new ctor
  // to the list.
  SmallVector<Constant *, 16> CurrentCtors;
  StructType *EltTy;
  if (GlobalVariable *GVCtor = M.getNamedGlobal(Array)) {
    ArrayType *ATy = cast<ArrayType>(GVCtor->getValueType());
    StructType *OldEltTy = cast<StructType>(ATy->getElementType());
    // Upgrade a 2-field global array type to the new 3-field format if needed.
    if (Data && OldEltTy->getNumElements() < 3)
      EltTy = StructType::get(IRB.getInt32Ty(), PointerType::getUnqual(FnTy),
                              IRB.getInt8PtrTy());
    else
      EltTy = OldEltTy;
    if (Constant *Init = GVCtor->getInitializer()) {
      unsigned n = Init->getNumOperands();
      CurrentCtors.reserve(n + 1);
      for (unsigned i = 0; i != n; ++i) {
        auto Ctor = cast<Constant>(Init->getOperand(i));
        if (EltTy != OldEltTy)
          Ctor =
              ConstantStruct::get(EltTy, Ctor->getAggregateElement((unsigned)0),
                                  Ctor->getAggregateElement(1),
                                  Constant::getNullValue(IRB.getInt8PtrTy()));
        CurrentCtors.push_back(Ctor);
      }
    }
    GVCtor->eraseFromParent();
  } else {
    // Use the new three-field struct if there isn't one already.
    EltTy = StructType::get(IRB.getInt32Ty(), PointerType::getUnqual(FnTy),
                            IRB.getInt8PtrTy());
  }

  // Build a 2 or 3 field global_ctor entry.  We don't take a comdat key.
  Constant *CSVals[3];
  CSVals[0] = IRB.getInt32(Priority);
  CSVals[1] = F;
  // FIXME: Drop support for the two element form in LLVM 4.0.
  if (EltTy->getNumElements() >= 3)
    CSVals[2] = Data ? ConstantExpr::getPointerCast(Data, IRB.getInt8PtrTy())
                     : Constant::getNullValue(IRB.getInt8PtrTy());
  Constant *RuntimeCtorInit =
      ConstantStruct::get(EltTy, makeArrayRef(CSVals, EltTy->getNumElements()));

  CurrentCtors.push_back(RuntimeCtorInit);

  // Create a new initializer.
  ArrayType *AT = ArrayType::get(EltTy, CurrentCtors.size());
  Constant *NewInit = ConstantArray::get(AT, CurrentCtors);

  // Create the new global variable and replace all uses of
  // the old global variable with the new one.
  (void)new GlobalVariable(M, NewInit->getType(), false,
                           GlobalValue::AppendingLinkage, NewInit, Array);
}

void llvm::appendToGlobalCtors(Module &M, Function *F, int Priority, Constant *Data) {
  appendToGlobalArray("llvm.global_ctors", M, F, Priority, Data);
}

void llvm::appendToGlobalDtors(Module &M, Function *F, int Priority, Constant *Data) {
  appendToGlobalArray("llvm.global_dtors", M, F, Priority, Data);
}

static void appendToUsedList(Module &M, StringRef Name, ArrayRef<GlobalValue *> Values) {
  GlobalVariable *GV = M.getGlobalVariable(Name);
  SmallPtrSet<Constant *, 16> InitAsSet;
  SmallVector<Constant *, 16> Init;
  if (GV) {
    ConstantArray *CA = dyn_cast<ConstantArray>(GV->getInitializer());
    for (auto &Op : CA->operands()) {
      Constant *C = cast_or_null<Constant>(Op);
      if (InitAsSet.insert(C).second)
        Init.push_back(C);
    }
    GV->eraseFromParent();
  }

  Type *Int8PtrTy = llvm::Type::getInt8PtrTy(M.getContext());
  for (auto *V : Values) {
    Constant *C = ConstantExpr::getBitCast(V, Int8PtrTy);
    if (InitAsSet.insert(C).second)
      Init.push_back(C);
  }

  if (Init.empty())
    return;

  ArrayType *ATy = ArrayType::get(Int8PtrTy, Init.size());
  GV = new llvm::GlobalVariable(M, ATy, false, GlobalValue::AppendingLinkage,
                                ConstantArray::get(ATy, Init), Name);
  GV->setSection("llvm.metadata");
}

void llvm::appendToUsed(Module &M, ArrayRef<GlobalValue *> Values) {
  appendToUsedList(M, "llvm.used", Values);
}

void llvm::appendToCompilerUsed(Module &M, ArrayRef<GlobalValue *> Values) {
  appendToUsedList(M, "llvm.compiler.used", Values);
}

Function *llvm::checkSanitizerInterfaceFunction(Constant *FuncOrBitcast) {
  if (isa<Function>(FuncOrBitcast))
    return cast<Function>(FuncOrBitcast);
  FuncOrBitcast->print(errs());
  errs() << '\n';
  std::string Err;
  raw_string_ostream Stream(Err);
  Stream << "Sanitizer interface function redefined: " << *FuncOrBitcast;
  report_fatal_error(Err);
}

Function *llvm::declareSanitizerInitFunction(Module &M, StringRef InitName,
                                             ArrayRef<Type *> InitArgTypes) {
  assert(!InitName.empty() && "Expected init function name");
  Function *F = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      InitName,
      FunctionType::get(Type::getVoidTy(M.getContext()), InitArgTypes, false),
      AttributeList()));
  F->setLinkage(Function::ExternalLinkage);
  return F;
}

std::pair<Function *, Function *> llvm::createSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    StringRef VersionCheckName) {
  assert(!InitName.empty() && "Expected init function name");
  assert(InitArgs.size() == InitArgTypes.size() &&
         "Sanitizer's init function expects different number of arguments");
  Function *InitFunction =
      declareSanitizerInitFunction(M, InitName, InitArgTypes);
  Function *Ctor = Function::Create(
      FunctionType::get(Type::getVoidTy(M.getContext()), false),
      GlobalValue::InternalLinkage, CtorName, &M);
  BasicBlock *CtorBB = BasicBlock::Create(M.getContext(), "", Ctor);
  IRBuilder<> IRB(ReturnInst::Create(M.getContext(), CtorBB));
  IRB.CreateCall(InitFunction, InitArgs);
  if (!VersionCheckName.empty()) {
    Function *VersionCheckFunction =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            VersionCheckName, FunctionType::get(IRB.getVoidTy(), {}, false),
            AttributeList()));
    IRB.CreateCall(VersionCheckFunction, {});
  }
  return std::make_pair(Ctor, InitFunction);
}

std::pair<Function *, Function *>
llvm::getOrCreateSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    function_ref<void(Function *, Function *)> FunctionsCreatedCallback,
    StringRef VersionCheckName) {
  assert(!CtorName.empty() && "Expected ctor function name");

  if (Function *Ctor = M.getFunction(CtorName))
    // FIXME: Sink this logic into the module, similar to the handling of
    // globals. This will make moving to a concurrent model much easier.
    if (Ctor->arg_size() == 0 ||
        Ctor->getReturnType() == Type::getVoidTy(M.getContext()))
      return {Ctor, declareSanitizerInitFunction(M, InitName, InitArgTypes)};

  Function *Ctor, *InitFunction;
  std::tie(Ctor, InitFunction) = llvm::createSanitizerCtorAndInitFunctions(
      M, CtorName, InitName, InitArgTypes, InitArgs, VersionCheckName);
  FunctionsCreatedCallback(Ctor, InitFunction);
  return std::make_pair(Ctor, InitFunction);
}

Function *llvm::getOrCreateInitFunction(Module &M, StringRef Name) {
  assert(!Name.empty() && "Expected init function name");
  if (Function *F = M.getFunction(Name)) {
    if (F->arg_size() != 0 ||
        F->getReturnType() != Type::getVoidTy(M.getContext())) {
      std::string Err;
      raw_string_ostream Stream(Err);
      Stream << "Sanitizer interface function defined with wrong type: " << *F;
      report_fatal_error(Err);
    }
    return F;
  }
  Function *F = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      Name, AttributeList(), Type::getVoidTy(M.getContext())));
  F->setLinkage(Function::ExternalLinkage);

  appendToGlobalCtors(M, F, 0);

  return F;
}

void llvm::filterDeadComdatFunctions(
    Module &M, SmallVectorImpl<Function *> &DeadComdatFunctions) {
  // Build a map from the comdat to the number of entries in that comdat we
  // think are dead. If this fully covers the comdat group, then the entire
  // group is dead. If we find another entry in the comdat group though, we'll
  // have to preserve the whole group.
  SmallDenseMap<Comdat *, int, 16> ComdatEntriesCovered;
  for (Function *F : DeadComdatFunctions) {
    Comdat *C = F->getComdat();
    assert(C && "Expected all input GVs to be in a comdat!");
    ComdatEntriesCovered[C] += 1;
  }

  auto CheckComdat = [&](Comdat &C) {
    auto CI = ComdatEntriesCovered.find(&C);
    if (CI == ComdatEntriesCovered.end())
      return;

    // If this could have been covered by a dead entry, just subtract one to
    // account for it.
    if (CI->second > 0) {
      CI->second -= 1;
      return;
    }

    // If we've already accounted for all the entries that were dead, the
    // entire comdat is alive so remove it from the map.
    ComdatEntriesCovered.erase(CI);
  };

  auto CheckAllComdats = [&] {
    for (Function &F : M.functions())
      if (Comdat *C = F.getComdat()) {
        CheckComdat(*C);
        if (ComdatEntriesCovered.empty())
          return;
      }
    for (GlobalVariable &GV : M.globals())
      if (Comdat *C = GV.getComdat()) {
        CheckComdat(*C);
        if (ComdatEntriesCovered.empty())
          return;
      }
    for (GlobalAlias &GA : M.aliases())
      if (Comdat *C = GA.getComdat()) {
        CheckComdat(*C);
        if (ComdatEntriesCovered.empty())
          return;
      }
  };
  CheckAllComdats();

  if (ComdatEntriesCovered.empty()) {
    DeadComdatFunctions.clear();
    return;
  }

  // Remove the entries that were not covering.
  erase_if(DeadComdatFunctions, [&](GlobalValue *GV) {
    return ComdatEntriesCovered.find(GV->getComdat()) ==
           ComdatEntriesCovered.end();
  });
}

std::string llvm::getUniqueModuleId(Module *M) {
  MD5 Md5;
  bool ExportsSymbols = false;
  auto AddGlobal = [&](GlobalValue &GV) {
    if (GV.isDeclaration() || GV.getName().startswith("llvm.") ||
        !GV.hasExternalLinkage() || GV.hasComdat())
      return;
    ExportsSymbols = true;
    Md5.update(GV.getName());
    Md5.update(ArrayRef<uint8_t>{0});
  };

  for (auto &F : *M)
    AddGlobal(F);
  for (auto &GV : M->globals())
    AddGlobal(GV);
  for (auto &GA : M->aliases())
    AddGlobal(GA);
  for (auto &IF : M->ifuncs())
    AddGlobal(IF);

  if (!ExportsSymbols)
    return "";

  MD5::MD5Result R;
  Md5.final(R);

  SmallString<32> Str;
  MD5::stringifyResult(R, Str);
  return ("$" + Str).str();
}
