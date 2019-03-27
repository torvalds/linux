//===-- MCJIT.cpp - MC-based Just-in-Time Compiler ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCJIT.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MutexGuard.h"

using namespace llvm;

namespace {

static struct RegisterJIT {
  RegisterJIT() { MCJIT::Register(); }
} JITRegistrator;

}

extern "C" void LLVMLinkInMCJIT() {
}

ExecutionEngine *
MCJIT::createJIT(std::unique_ptr<Module> M, std::string *ErrorStr,
                 std::shared_ptr<MCJITMemoryManager> MemMgr,
                 std::shared_ptr<LegacyJITSymbolResolver> Resolver,
                 std::unique_ptr<TargetMachine> TM) {
  // Try to register the program as a source of symbols to resolve against.
  //
  // FIXME: Don't do this here.
  sys::DynamicLibrary::LoadLibraryPermanently(nullptr, nullptr);

  if (!MemMgr || !Resolver) {
    auto RTDyldMM = std::make_shared<SectionMemoryManager>();
    if (!MemMgr)
      MemMgr = RTDyldMM;
    if (!Resolver)
      Resolver = RTDyldMM;
  }

  return new MCJIT(std::move(M), std::move(TM), std::move(MemMgr),
                   std::move(Resolver));
}

MCJIT::MCJIT(std::unique_ptr<Module> M, std::unique_ptr<TargetMachine> TM,
             std::shared_ptr<MCJITMemoryManager> MemMgr,
             std::shared_ptr<LegacyJITSymbolResolver> Resolver)
    : ExecutionEngine(TM->createDataLayout(), std::move(M)), TM(std::move(TM)),
      Ctx(nullptr), MemMgr(std::move(MemMgr)),
      Resolver(*this, std::move(Resolver)), Dyld(*this->MemMgr, this->Resolver),
      ObjCache(nullptr) {
  // FIXME: We are managing our modules, so we do not want the base class
  // ExecutionEngine to manage them as well. To avoid double destruction
  // of the first (and only) module added in ExecutionEngine constructor
  // we remove it from EE and will destruct it ourselves.
  //
  // It may make sense to move our module manager (based on SmallStPtr) back
  // into EE if the JIT and Interpreter can live with it.
  // If so, additional functions: addModule, removeModule, FindFunctionNamed,
  // runStaticConstructorsDestructors could be moved back to EE as well.
  //
  std::unique_ptr<Module> First = std::move(Modules[0]);
  Modules.clear();

  if (First->getDataLayout().isDefault())
    First->setDataLayout(getDataLayout());

  OwnedModules.addModule(std::move(First));
  RegisterJITEventListener(JITEventListener::createGDBRegistrationListener());
}

MCJIT::~MCJIT() {
  MutexGuard locked(lock);

  Dyld.deregisterEHFrames();

  for (auto &Obj : LoadedObjects)
    if (Obj)
      notifyFreeingObject(*Obj);

  Archives.clear();
}

void MCJIT::addModule(std::unique_ptr<Module> M) {
  MutexGuard locked(lock);

  if (M->getDataLayout().isDefault())
    M->setDataLayout(getDataLayout());

  OwnedModules.addModule(std::move(M));
}

bool MCJIT::removeModule(Module *M) {
  MutexGuard locked(lock);
  return OwnedModules.removeModule(M);
}

void MCJIT::addObjectFile(std::unique_ptr<object::ObjectFile> Obj) {
  std::unique_ptr<RuntimeDyld::LoadedObjectInfo> L = Dyld.loadObject(*Obj);
  if (Dyld.hasError())
    report_fatal_error(Dyld.getErrorString());

  notifyObjectLoaded(*Obj, *L);

  LoadedObjects.push_back(std::move(Obj));
}

void MCJIT::addObjectFile(object::OwningBinary<object::ObjectFile> Obj) {
  std::unique_ptr<object::ObjectFile> ObjFile;
  std::unique_ptr<MemoryBuffer> MemBuf;
  std::tie(ObjFile, MemBuf) = Obj.takeBinary();
  addObjectFile(std::move(ObjFile));
  Buffers.push_back(std::move(MemBuf));
}

void MCJIT::addArchive(object::OwningBinary<object::Archive> A) {
  Archives.push_back(std::move(A));
}

void MCJIT::setObjectCache(ObjectCache* NewCache) {
  MutexGuard locked(lock);
  ObjCache = NewCache;
}

std::unique_ptr<MemoryBuffer> MCJIT::emitObject(Module *M) {
  assert(M && "Can not emit a null module");

  MutexGuard locked(lock);

  // Materialize all globals in the module if they have not been
  // materialized already.
  cantFail(M->materializeAll());

  // This must be a module which has already been added but not loaded to this
  // MCJIT instance, since these conditions are tested by our caller,
  // generateCodeForModule.

  legacy::PassManager PM;

  // The RuntimeDyld will take ownership of this shortly
  SmallVector<char, 4096> ObjBufferSV;
  raw_svector_ostream ObjStream(ObjBufferSV);

  // Turn the machine code intermediate representation into bytes in memory
  // that may be executed.
  if (TM->addPassesToEmitMC(PM, Ctx, ObjStream, !getVerifyModules()))
    report_fatal_error("Target does not support MC emission!");

  // Initialize passes.
  PM.run(*M);
  // Flush the output buffer to get the generated code into memory

  std::unique_ptr<MemoryBuffer> CompiledObjBuffer(
      new SmallVectorMemoryBuffer(std::move(ObjBufferSV)));

  // If we have an object cache, tell it about the new object.
  // Note that we're using the compiled image, not the loaded image (as below).
  if (ObjCache) {
    // MemoryBuffer is a thin wrapper around the actual memory, so it's OK
    // to create a temporary object here and delete it after the call.
    MemoryBufferRef MB = CompiledObjBuffer->getMemBufferRef();
    ObjCache->notifyObjectCompiled(M, MB);
  }

  return CompiledObjBuffer;
}

void MCJIT::generateCodeForModule(Module *M) {
  // Get a thread lock to make sure we aren't trying to load multiple times
  MutexGuard locked(lock);

  // This must be a module which has already been added to this MCJIT instance.
  assert(OwnedModules.ownsModule(M) &&
         "MCJIT::generateCodeForModule: Unknown module.");

  // Re-compilation is not supported
  if (OwnedModules.hasModuleBeenLoaded(M))
    return;

  std::unique_ptr<MemoryBuffer> ObjectToLoad;
  // Try to load the pre-compiled object from cache if possible
  if (ObjCache)
    ObjectToLoad = ObjCache->getObject(M);

  assert(M->getDataLayout() == getDataLayout() && "DataLayout Mismatch");

  // If the cache did not contain a suitable object, compile the object
  if (!ObjectToLoad) {
    ObjectToLoad = emitObject(M);
    assert(ObjectToLoad && "Compilation did not produce an object.");
  }

  // Load the object into the dynamic linker.
  // MCJIT now owns the ObjectImage pointer (via its LoadedObjects list).
  Expected<std::unique_ptr<object::ObjectFile>> LoadedObject =
    object::ObjectFile::createObjectFile(ObjectToLoad->getMemBufferRef());
  if (!LoadedObject) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    logAllUnhandledErrors(LoadedObject.takeError(), OS);
    OS.flush();
    report_fatal_error(Buf);
  }
  std::unique_ptr<RuntimeDyld::LoadedObjectInfo> L =
    Dyld.loadObject(*LoadedObject.get());

  if (Dyld.hasError())
    report_fatal_error(Dyld.getErrorString());

  notifyObjectLoaded(*LoadedObject.get(), *L);

  Buffers.push_back(std::move(ObjectToLoad));
  LoadedObjects.push_back(std::move(*LoadedObject));

  OwnedModules.markModuleAsLoaded(M);
}

void MCJIT::finalizeLoadedModules() {
  MutexGuard locked(lock);

  // Resolve any outstanding relocations.
  Dyld.resolveRelocations();

  OwnedModules.markAllLoadedModulesAsFinalized();

  // Register EH frame data for any module we own which has been loaded
  Dyld.registerEHFrames();

  // Set page permissions.
  MemMgr->finalizeMemory();
}

// FIXME: Rename this.
void MCJIT::finalizeObject() {
  MutexGuard locked(lock);

  // Generate code for module is going to move objects out of the 'added' list,
  // so we need to copy that out before using it:
  SmallVector<Module*, 16> ModsToAdd;
  for (auto M : OwnedModules.added())
    ModsToAdd.push_back(M);

  for (auto M : ModsToAdd)
    generateCodeForModule(M);

  finalizeLoadedModules();
}

void MCJIT::finalizeModule(Module *M) {
  MutexGuard locked(lock);

  // This must be a module which has already been added to this MCJIT instance.
  assert(OwnedModules.ownsModule(M) && "MCJIT::finalizeModule: Unknown module.");

  // If the module hasn't been compiled, just do that.
  if (!OwnedModules.hasModuleBeenLoaded(M))
    generateCodeForModule(M);

  finalizeLoadedModules();
}

JITSymbol MCJIT::findExistingSymbol(const std::string &Name) {
  if (void *Addr = getPointerToGlobalIfAvailable(Name))
    return JITSymbol(static_cast<uint64_t>(
                         reinterpret_cast<uintptr_t>(Addr)),
                     JITSymbolFlags::Exported);

  return Dyld.getSymbol(Name);
}

Module *MCJIT::findModuleForSymbol(const std::string &Name,
                                   bool CheckFunctionsOnly) {
  StringRef DemangledName = Name;
  if (DemangledName[0] == getDataLayout().getGlobalPrefix())
    DemangledName = DemangledName.substr(1);

  MutexGuard locked(lock);

  // If it hasn't already been generated, see if it's in one of our modules.
  for (ModulePtrSet::iterator I = OwnedModules.begin_added(),
                              E = OwnedModules.end_added();
       I != E; ++I) {
    Module *M = *I;
    Function *F = M->getFunction(DemangledName);
    if (F && !F->isDeclaration())
      return M;
    if (!CheckFunctionsOnly) {
      GlobalVariable *G = M->getGlobalVariable(DemangledName);
      if (G && !G->isDeclaration())
        return M;
      // FIXME: Do we need to worry about global aliases?
    }
  }
  // We didn't find the symbol in any of our modules.
  return nullptr;
}

uint64_t MCJIT::getSymbolAddress(const std::string &Name,
                                 bool CheckFunctionsOnly) {
  std::string MangledName;
  {
    raw_string_ostream MangledNameStream(MangledName);
    Mangler::getNameWithPrefix(MangledNameStream, Name, getDataLayout());
  }
  if (auto Sym = findSymbol(MangledName, CheckFunctionsOnly)) {
    if (auto AddrOrErr = Sym.getAddress())
      return *AddrOrErr;
    else
      report_fatal_error(AddrOrErr.takeError());
  } else if (auto Err = Sym.takeError())
    report_fatal_error(Sym.takeError());
  return 0;
}

JITSymbol MCJIT::findSymbol(const std::string &Name,
                            bool CheckFunctionsOnly) {
  MutexGuard locked(lock);

  // First, check to see if we already have this symbol.
  if (auto Sym = findExistingSymbol(Name))
    return Sym;

  for (object::OwningBinary<object::Archive> &OB : Archives) {
    object::Archive *A = OB.getBinary();
    // Look for our symbols in each Archive
    auto OptionalChildOrErr = A->findSym(Name);
    if (!OptionalChildOrErr)
      report_fatal_error(OptionalChildOrErr.takeError());
    auto &OptionalChild = *OptionalChildOrErr;
    if (OptionalChild) {
      // FIXME: Support nested archives?
      Expected<std::unique_ptr<object::Binary>> ChildBinOrErr =
          OptionalChild->getAsBinary();
      if (!ChildBinOrErr) {
        // TODO: Actually report errors helpfully.
        consumeError(ChildBinOrErr.takeError());
        continue;
      }
      std::unique_ptr<object::Binary> &ChildBin = ChildBinOrErr.get();
      if (ChildBin->isObject()) {
        std::unique_ptr<object::ObjectFile> OF(
            static_cast<object::ObjectFile *>(ChildBin.release()));
        // This causes the object file to be loaded.
        addObjectFile(std::move(OF));
        // The address should be here now.
        if (auto Sym = findExistingSymbol(Name))
          return Sym;
      }
    }
  }

  // If it hasn't already been generated, see if it's in one of our modules.
  Module *M = findModuleForSymbol(Name, CheckFunctionsOnly);
  if (M) {
    generateCodeForModule(M);

    // Check the RuntimeDyld table again, it should be there now.
    return findExistingSymbol(Name);
  }

  // If a LazyFunctionCreator is installed, use it to get/create the function.
  // FIXME: Should we instead have a LazySymbolCreator callback?
  if (LazyFunctionCreator) {
    auto Addr = static_cast<uint64_t>(
                  reinterpret_cast<uintptr_t>(LazyFunctionCreator(Name)));
    return JITSymbol(Addr, JITSymbolFlags::Exported);
  }

  return nullptr;
}

uint64_t MCJIT::getGlobalValueAddress(const std::string &Name) {
  MutexGuard locked(lock);
  uint64_t Result = getSymbolAddress(Name, false);
  if (Result != 0)
    finalizeLoadedModules();
  return Result;
}

uint64_t MCJIT::getFunctionAddress(const std::string &Name) {
  MutexGuard locked(lock);
  uint64_t Result = getSymbolAddress(Name, true);
  if (Result != 0)
    finalizeLoadedModules();
  return Result;
}

// Deprecated.  Use getFunctionAddress instead.
void *MCJIT::getPointerToFunction(Function *F) {
  MutexGuard locked(lock);

  Mangler Mang;
  SmallString<128> Name;
  TM->getNameWithPrefix(Name, F, Mang);

  if (F->isDeclaration() || F->hasAvailableExternallyLinkage()) {
    bool AbortOnFailure = !F->hasExternalWeakLinkage();
    void *Addr = getPointerToNamedFunction(Name, AbortOnFailure);
    updateGlobalMapping(F, Addr);
    return Addr;
  }

  Module *M = F->getParent();
  bool HasBeenAddedButNotLoaded = OwnedModules.hasModuleBeenAddedButNotLoaded(M);

  // Make sure the relevant module has been compiled and loaded.
  if (HasBeenAddedButNotLoaded)
    generateCodeForModule(M);
  else if (!OwnedModules.hasModuleBeenLoaded(M)) {
    // If this function doesn't belong to one of our modules, we're done.
    // FIXME: Asking for the pointer to a function that hasn't been registered,
    //        and isn't a declaration (which is handled above) should probably
    //        be an assertion.
    return nullptr;
  }

  // FIXME: Should the Dyld be retaining module information? Probably not.
  //
  // This is the accessor for the target address, so make sure to check the
  // load address of the symbol, not the local address.
  return (void*)Dyld.getSymbol(Name).getAddress();
}

void MCJIT::runStaticConstructorsDestructorsInModulePtrSet(
    bool isDtors, ModulePtrSet::iterator I, ModulePtrSet::iterator E) {
  for (; I != E; ++I) {
    ExecutionEngine::runStaticConstructorsDestructors(**I, isDtors);
  }
}

void MCJIT::runStaticConstructorsDestructors(bool isDtors) {
  // Execute global ctors/dtors for each module in the program.
  runStaticConstructorsDestructorsInModulePtrSet(
      isDtors, OwnedModules.begin_added(), OwnedModules.end_added());
  runStaticConstructorsDestructorsInModulePtrSet(
      isDtors, OwnedModules.begin_loaded(), OwnedModules.end_loaded());
  runStaticConstructorsDestructorsInModulePtrSet(
      isDtors, OwnedModules.begin_finalized(), OwnedModules.end_finalized());
}

Function *MCJIT::FindFunctionNamedInModulePtrSet(StringRef FnName,
                                                 ModulePtrSet::iterator I,
                                                 ModulePtrSet::iterator E) {
  for (; I != E; ++I) {
    Function *F = (*I)->getFunction(FnName);
    if (F && !F->isDeclaration())
      return F;
  }
  return nullptr;
}

GlobalVariable *MCJIT::FindGlobalVariableNamedInModulePtrSet(StringRef Name,
                                                             bool AllowInternal,
                                                             ModulePtrSet::iterator I,
                                                             ModulePtrSet::iterator E) {
  for (; I != E; ++I) {
    GlobalVariable *GV = (*I)->getGlobalVariable(Name, AllowInternal);
    if (GV && !GV->isDeclaration())
      return GV;
  }
  return nullptr;
}


Function *MCJIT::FindFunctionNamed(StringRef FnName) {
  Function *F = FindFunctionNamedInModulePtrSet(
      FnName, OwnedModules.begin_added(), OwnedModules.end_added());
  if (!F)
    F = FindFunctionNamedInModulePtrSet(FnName, OwnedModules.begin_loaded(),
                                        OwnedModules.end_loaded());
  if (!F)
    F = FindFunctionNamedInModulePtrSet(FnName, OwnedModules.begin_finalized(),
                                        OwnedModules.end_finalized());
  return F;
}

GlobalVariable *MCJIT::FindGlobalVariableNamed(StringRef Name, bool AllowInternal) {
  GlobalVariable *GV = FindGlobalVariableNamedInModulePtrSet(
      Name, AllowInternal, OwnedModules.begin_added(), OwnedModules.end_added());
  if (!GV)
    GV = FindGlobalVariableNamedInModulePtrSet(Name, AllowInternal, OwnedModules.begin_loaded(),
                                        OwnedModules.end_loaded());
  if (!GV)
    GV = FindGlobalVariableNamedInModulePtrSet(Name, AllowInternal, OwnedModules.begin_finalized(),
                                        OwnedModules.end_finalized());
  return GV;
}

GenericValue MCJIT::runFunction(Function *F, ArrayRef<GenericValue> ArgValues) {
  assert(F && "Function *F was null at entry to run()");

  void *FPtr = getPointerToFunction(F);
  finalizeModule(F->getParent());
  assert(FPtr && "Pointer to fn's code was null after getPointerToFunction");
  FunctionType *FTy = F->getFunctionType();
  Type *RetTy = FTy->getReturnType();

  assert((FTy->getNumParams() == ArgValues.size() ||
          (FTy->isVarArg() && FTy->getNumParams() <= ArgValues.size())) &&
         "Wrong number of arguments passed into function!");
  assert(FTy->getNumParams() == ArgValues.size() &&
         "This doesn't support passing arguments through varargs (yet)!");

  // Handle some common cases first.  These cases correspond to common `main'
  // prototypes.
  if (RetTy->isIntegerTy(32) || RetTy->isVoidTy()) {
    switch (ArgValues.size()) {
    case 3:
      if (FTy->getParamType(0)->isIntegerTy(32) &&
          FTy->getParamType(1)->isPointerTy() &&
          FTy->getParamType(2)->isPointerTy()) {
        int (*PF)(int, char **, const char **) =
          (int(*)(int, char **, const char **))(intptr_t)FPtr;

        // Call the function.
        GenericValue rv;
        rv.IntVal = APInt(32, PF(ArgValues[0].IntVal.getZExtValue(),
                                 (char **)GVTOP(ArgValues[1]),
                                 (const char **)GVTOP(ArgValues[2])));
        return rv;
      }
      break;
    case 2:
      if (FTy->getParamType(0)->isIntegerTy(32) &&
          FTy->getParamType(1)->isPointerTy()) {
        int (*PF)(int, char **) = (int(*)(int, char **))(intptr_t)FPtr;

        // Call the function.
        GenericValue rv;
        rv.IntVal = APInt(32, PF(ArgValues[0].IntVal.getZExtValue(),
                                 (char **)GVTOP(ArgValues[1])));
        return rv;
      }
      break;
    case 1:
      if (FTy->getNumParams() == 1 &&
          FTy->getParamType(0)->isIntegerTy(32)) {
        GenericValue rv;
        int (*PF)(int) = (int(*)(int))(intptr_t)FPtr;
        rv.IntVal = APInt(32, PF(ArgValues[0].IntVal.getZExtValue()));
        return rv;
      }
      break;
    }
  }

  // Handle cases where no arguments are passed first.
  if (ArgValues.empty()) {
    GenericValue rv;
    switch (RetTy->getTypeID()) {
    default: llvm_unreachable("Unknown return type for function call!");
    case Type::IntegerTyID: {
      unsigned BitWidth = cast<IntegerType>(RetTy)->getBitWidth();
      if (BitWidth == 1)
        rv.IntVal = APInt(BitWidth, ((bool(*)())(intptr_t)FPtr)());
      else if (BitWidth <= 8)
        rv.IntVal = APInt(BitWidth, ((char(*)())(intptr_t)FPtr)());
      else if (BitWidth <= 16)
        rv.IntVal = APInt(BitWidth, ((short(*)())(intptr_t)FPtr)());
      else if (BitWidth <= 32)
        rv.IntVal = APInt(BitWidth, ((int(*)())(intptr_t)FPtr)());
      else if (BitWidth <= 64)
        rv.IntVal = APInt(BitWidth, ((int64_t(*)())(intptr_t)FPtr)());
      else
        llvm_unreachable("Integer types > 64 bits not supported");
      return rv;
    }
    case Type::VoidTyID:
      rv.IntVal = APInt(32, ((int(*)())(intptr_t)FPtr)());
      return rv;
    case Type::FloatTyID:
      rv.FloatVal = ((float(*)())(intptr_t)FPtr)();
      return rv;
    case Type::DoubleTyID:
      rv.DoubleVal = ((double(*)())(intptr_t)FPtr)();
      return rv;
    case Type::X86_FP80TyID:
    case Type::FP128TyID:
    case Type::PPC_FP128TyID:
      llvm_unreachable("long double not supported yet");
    case Type::PointerTyID:
      return PTOGV(((void*(*)())(intptr_t)FPtr)());
    }
  }

  report_fatal_error("MCJIT::runFunction does not support full-featured "
                     "argument passing. Please use "
                     "ExecutionEngine::getFunctionAddress and cast the result "
                     "to the desired function pointer type.");
}

void *MCJIT::getPointerToNamedFunction(StringRef Name, bool AbortOnFailure) {
  if (!isSymbolSearchingDisabled()) {
    if (auto Sym = Resolver.findSymbol(Name)) {
      if (auto AddrOrErr = Sym.getAddress())
        return reinterpret_cast<void*>(
                 static_cast<uintptr_t>(*AddrOrErr));
    } else if (auto Err = Sym.takeError())
      report_fatal_error(std::move(Err));
  }

  /// If a LazyFunctionCreator is installed, use it to get/create the function.
  if (LazyFunctionCreator)
    if (void *RP = LazyFunctionCreator(Name))
      return RP;

  if (AbortOnFailure) {
    report_fatal_error("Program used external function '"+Name+
                       "' which could not be resolved!");
  }
  return nullptr;
}

void MCJIT::RegisterJITEventListener(JITEventListener *L) {
  if (!L)
    return;
  MutexGuard locked(lock);
  EventListeners.push_back(L);
}

void MCJIT::UnregisterJITEventListener(JITEventListener *L) {
  if (!L)
    return;
  MutexGuard locked(lock);
  auto I = find(reverse(EventListeners), L);
  if (I != EventListeners.rend()) {
    std::swap(*I, EventListeners.back());
    EventListeners.pop_back();
  }
}

void MCJIT::notifyObjectLoaded(const object::ObjectFile &Obj,
                               const RuntimeDyld::LoadedObjectInfo &L) {
  uint64_t Key =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Obj.getData().data()));
  MutexGuard locked(lock);
  MemMgr->notifyObjectLoaded(this, Obj);
  for (unsigned I = 0, S = EventListeners.size(); I < S; ++I) {
    EventListeners[I]->notifyObjectLoaded(Key, Obj, L);
  }
}

void MCJIT::notifyFreeingObject(const object::ObjectFile &Obj) {
  uint64_t Key =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Obj.getData().data()));
  MutexGuard locked(lock);
  for (JITEventListener *L : EventListeners)
    L->notifyFreeingObject(Key);
}

JITSymbol
LinkingSymbolResolver::findSymbol(const std::string &Name) {
  auto Result = ParentEngine.findSymbol(Name, false);
  if (Result)
    return Result;
  if (ParentEngine.isSymbolSearchingDisabled())
    return nullptr;
  return ClientResolver->findSymbol(Name);
}

void LinkingSymbolResolver::anchor() {}
