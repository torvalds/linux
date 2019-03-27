//===- IndirectionUtils.h - Utilities for adding indirections ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for adding indirections and breaking up modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_INDIRECTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_INDIRECTIONUTILS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/Process.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {

class Constant;
class Function;
class FunctionType;
class GlobalAlias;
class GlobalVariable;
class Module;
class PointerType;
class Triple;
class Value;

namespace orc {

/// Base class for pools of compiler re-entry trampolines.
/// These trampolines are callable addresses that save all register state
/// before calling a supplied function to return the trampoline landing
/// address, then restore all state before jumping to that address. They
/// are used by various ORC APIs to support lazy compilation
class TrampolinePool {
public:
  virtual ~TrampolinePool() {}

  /// Get an available trampoline address.
  /// Returns an error if no trampoline can be created.
  virtual Expected<JITTargetAddress> getTrampoline() = 0;

private:
  virtual void anchor();
};

/// A trampoline pool for trampolines within the current process.
template <typename ORCABI> class LocalTrampolinePool : public TrampolinePool {
public:
  using GetTrampolineLandingFunction =
      std::function<JITTargetAddress(JITTargetAddress TrampolineAddr)>;

  /// Creates a LocalTrampolinePool with the given RunCallback function.
  /// Returns an error if this function is unable to correctly allocate, write
  /// and protect the resolver code block.
  static Expected<std::unique_ptr<LocalTrampolinePool>>
  Create(GetTrampolineLandingFunction GetTrampolineLanding) {
    Error Err = Error::success();

    auto LTP = std::unique_ptr<LocalTrampolinePool>(
        new LocalTrampolinePool(std::move(GetTrampolineLanding), Err));

    if (Err)
      return std::move(Err);
    return std::move(LTP);
  }

  /// Get a free trampoline. Returns an error if one can not be provide (e.g.
  /// because the pool is empty and can not be grown).
  Expected<JITTargetAddress> getTrampoline() override {
    std::lock_guard<std::mutex> Lock(LTPMutex);
    if (AvailableTrampolines.empty()) {
      if (auto Err = grow())
        return std::move(Err);
    }
    assert(!AvailableTrampolines.empty() && "Failed to grow trampoline pool");
    auto TrampolineAddr = AvailableTrampolines.back();
    AvailableTrampolines.pop_back();
    return TrampolineAddr;
  }

  /// Returns the given trampoline to the pool for re-use.
  void releaseTrampoline(JITTargetAddress TrampolineAddr) {
    std::lock_guard<std::mutex> Lock(LTPMutex);
    AvailableTrampolines.push_back(TrampolineAddr);
  }

private:
  static JITTargetAddress reenter(void *TrampolinePoolPtr, void *TrampolineId) {
    LocalTrampolinePool<ORCABI> *TrampolinePool =
        static_cast<LocalTrampolinePool *>(TrampolinePoolPtr);
    return TrampolinePool->GetTrampolineLanding(static_cast<JITTargetAddress>(
        reinterpret_cast<uintptr_t>(TrampolineId)));
  }

  LocalTrampolinePool(GetTrampolineLandingFunction GetTrampolineLanding,
                      Error &Err)
      : GetTrampolineLanding(std::move(GetTrampolineLanding)) {

    ErrorAsOutParameter _(&Err);

    /// Try to set up the resolver block.
    std::error_code EC;
    ResolverBlock = sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
        ORCABI::ResolverCodeSize, nullptr,
        sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC) {
      Err = errorCodeToError(EC);
      return;
    }

    ORCABI::writeResolverCode(static_cast<uint8_t *>(ResolverBlock.base()),
                              &reenter, this);

    EC = sys::Memory::protectMappedMemory(ResolverBlock.getMemoryBlock(),
                                          sys::Memory::MF_READ |
                                              sys::Memory::MF_EXEC);
    if (EC) {
      Err = errorCodeToError(EC);
      return;
    }
  }

  Error grow() {
    assert(this->AvailableTrampolines.empty() && "Growing prematurely?");

    std::error_code EC;
    auto TrampolineBlock =
        sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
            sys::Process::getPageSize(), nullptr,
            sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC)
      return errorCodeToError(EC);

    unsigned NumTrampolines =
        (sys::Process::getPageSize() - ORCABI::PointerSize) /
        ORCABI::TrampolineSize;

    uint8_t *TrampolineMem = static_cast<uint8_t *>(TrampolineBlock.base());
    ORCABI::writeTrampolines(TrampolineMem, ResolverBlock.base(),
                             NumTrampolines);

    for (unsigned I = 0; I < NumTrampolines; ++I)
      this->AvailableTrampolines.push_back(
          static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(
              TrampolineMem + (I * ORCABI::TrampolineSize))));

    if (auto EC = sys::Memory::protectMappedMemory(
                    TrampolineBlock.getMemoryBlock(),
                    sys::Memory::MF_READ | sys::Memory::MF_EXEC))
      return errorCodeToError(EC);

    TrampolineBlocks.push_back(std::move(TrampolineBlock));
    return Error::success();
  }

  GetTrampolineLandingFunction GetTrampolineLanding;

  std::mutex LTPMutex;
  sys::OwningMemoryBlock ResolverBlock;
  std::vector<sys::OwningMemoryBlock> TrampolineBlocks;
  std::vector<JITTargetAddress> AvailableTrampolines;
};

/// Target-independent base class for compile callback management.
class JITCompileCallbackManager {
public:
  using CompileFunction = std::function<JITTargetAddress()>;

  virtual ~JITCompileCallbackManager() = default;

  /// Reserve a compile callback.
  Expected<JITTargetAddress> getCompileCallback(CompileFunction Compile);

  /// Execute the callback for the given trampoline id. Called by the JIT
  ///        to compile functions on demand.
  JITTargetAddress executeCompileCallback(JITTargetAddress TrampolineAddr);

protected:
  /// Construct a JITCompileCallbackManager.
  JITCompileCallbackManager(std::unique_ptr<TrampolinePool> TP,
                            ExecutionSession &ES,
                            JITTargetAddress ErrorHandlerAddress)
      : TP(std::move(TP)), ES(ES),
        CallbacksJD(ES.createJITDylib("<Callbacks>")),
        ErrorHandlerAddress(ErrorHandlerAddress) {}

  void setTrampolinePool(std::unique_ptr<TrampolinePool> TP) {
    this->TP = std::move(TP);
  }

private:
  std::mutex CCMgrMutex;
  std::unique_ptr<TrampolinePool> TP;
  ExecutionSession &ES;
  JITDylib &CallbacksJD;
  JITTargetAddress ErrorHandlerAddress;
  std::map<JITTargetAddress, SymbolStringPtr> AddrToSymbol;
  size_t NextCallbackId = 0;
};

/// Manage compile callbacks for in-process JITs.
template <typename ORCABI>
class LocalJITCompileCallbackManager : public JITCompileCallbackManager {
public:
  /// Create a new LocalJITCompileCallbackManager.
  static Expected<std::unique_ptr<LocalJITCompileCallbackManager>>
  Create(ExecutionSession &ES, JITTargetAddress ErrorHandlerAddress) {
    Error Err = Error::success();
    auto CCMgr = std::unique_ptr<LocalJITCompileCallbackManager>(
        new LocalJITCompileCallbackManager(ES, ErrorHandlerAddress, Err));
    if (Err)
      return std::move(Err);
    return std::move(CCMgr);
  }

private:
  /// Construct a InProcessJITCompileCallbackManager.
  /// @param ErrorHandlerAddress The address of an error handler in the target
  ///                            process to be used if a compile callback fails.
  LocalJITCompileCallbackManager(ExecutionSession &ES,
                                 JITTargetAddress ErrorHandlerAddress,
                                 Error &Err)
      : JITCompileCallbackManager(nullptr, ES, ErrorHandlerAddress) {
    ErrorAsOutParameter _(&Err);
    auto TP = LocalTrampolinePool<ORCABI>::Create(
        [this](JITTargetAddress TrampolineAddr) {
          return executeCompileCallback(TrampolineAddr);
        });

    if (!TP) {
      Err = TP.takeError();
      return;
    }

    setTrampolinePool(std::move(*TP));
  }
};

/// Base class for managing collections of named indirect stubs.
class IndirectStubsManager {
public:
  /// Map type for initializing the manager. See init.
  using StubInitsMap = StringMap<std::pair<JITTargetAddress, JITSymbolFlags>>;

  virtual ~IndirectStubsManager() = default;

  /// Create a single stub with the given name, target address and flags.
  virtual Error createStub(StringRef StubName, JITTargetAddress StubAddr,
                           JITSymbolFlags StubFlags) = 0;

  /// Create StubInits.size() stubs with the given names, target
  ///        addresses, and flags.
  virtual Error createStubs(const StubInitsMap &StubInits) = 0;

  /// Find the stub with the given name. If ExportedStubsOnly is true,
  ///        this will only return a result if the stub's flags indicate that it
  ///        is exported.
  virtual JITEvaluatedSymbol findStub(StringRef Name, bool ExportedStubsOnly) = 0;

  /// Find the implementation-pointer for the stub.
  virtual JITEvaluatedSymbol findPointer(StringRef Name) = 0;

  /// Change the value of the implementation pointer for the stub.
  virtual Error updatePointer(StringRef Name, JITTargetAddress NewAddr) = 0;

private:
  virtual void anchor();
};

/// IndirectStubsManager implementation for the host architecture, e.g.
///        OrcX86_64. (See OrcArchitectureSupport.h).
template <typename TargetT>
class LocalIndirectStubsManager : public IndirectStubsManager {
public:
  Error createStub(StringRef StubName, JITTargetAddress StubAddr,
                   JITSymbolFlags StubFlags) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    if (auto Err = reserveStubs(1))
      return Err;

    createStubInternal(StubName, StubAddr, StubFlags);

    return Error::success();
  }

  Error createStubs(const StubInitsMap &StubInits) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    if (auto Err = reserveStubs(StubInits.size()))
      return Err;

    for (auto &Entry : StubInits)
      createStubInternal(Entry.first(), Entry.second.first,
                         Entry.second.second);

    return Error::success();
  }

  JITEvaluatedSymbol findStub(StringRef Name, bool ExportedStubsOnly) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    auto I = StubIndexes.find(Name);
    if (I == StubIndexes.end())
      return nullptr;
    auto Key = I->second.first;
    void *StubAddr = IndirectStubsInfos[Key.first].getStub(Key.second);
    assert(StubAddr && "Missing stub address");
    auto StubTargetAddr =
        static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(StubAddr));
    auto StubSymbol = JITEvaluatedSymbol(StubTargetAddr, I->second.second);
    if (ExportedStubsOnly && !StubSymbol.getFlags().isExported())
      return nullptr;
    return StubSymbol;
  }

  JITEvaluatedSymbol findPointer(StringRef Name) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    auto I = StubIndexes.find(Name);
    if (I == StubIndexes.end())
      return nullptr;
    auto Key = I->second.first;
    void *PtrAddr = IndirectStubsInfos[Key.first].getPtr(Key.second);
    assert(PtrAddr && "Missing pointer address");
    auto PtrTargetAddr =
        static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(PtrAddr));
    return JITEvaluatedSymbol(PtrTargetAddr, I->second.second);
  }

  Error updatePointer(StringRef Name, JITTargetAddress NewAddr) override {
    using AtomicIntPtr = std::atomic<uintptr_t>;

    std::lock_guard<std::mutex> Lock(StubsMutex);
    auto I = StubIndexes.find(Name);
    assert(I != StubIndexes.end() && "No stub pointer for symbol");
    auto Key = I->second.first;
    AtomicIntPtr *AtomicStubPtr = reinterpret_cast<AtomicIntPtr *>(
        IndirectStubsInfos[Key.first].getPtr(Key.second));
    *AtomicStubPtr = static_cast<uintptr_t>(NewAddr);
    return Error::success();
  }

private:
  Error reserveStubs(unsigned NumStubs) {
    if (NumStubs <= FreeStubs.size())
      return Error::success();

    unsigned NewStubsRequired = NumStubs - FreeStubs.size();
    unsigned NewBlockId = IndirectStubsInfos.size();
    typename TargetT::IndirectStubsInfo ISI;
    if (auto Err =
            TargetT::emitIndirectStubsBlock(ISI, NewStubsRequired, nullptr))
      return Err;
    for (unsigned I = 0; I < ISI.getNumStubs(); ++I)
      FreeStubs.push_back(std::make_pair(NewBlockId, I));
    IndirectStubsInfos.push_back(std::move(ISI));
    return Error::success();
  }

  void createStubInternal(StringRef StubName, JITTargetAddress InitAddr,
                          JITSymbolFlags StubFlags) {
    auto Key = FreeStubs.back();
    FreeStubs.pop_back();
    *IndirectStubsInfos[Key.first].getPtr(Key.second) =
        reinterpret_cast<void *>(static_cast<uintptr_t>(InitAddr));
    StubIndexes[StubName] = std::make_pair(Key, StubFlags);
  }

  std::mutex StubsMutex;
  std::vector<typename TargetT::IndirectStubsInfo> IndirectStubsInfos;
  using StubKey = std::pair<uint16_t, uint16_t>;
  std::vector<StubKey> FreeStubs;
  StringMap<std::pair<StubKey, JITSymbolFlags>> StubIndexes;
};

/// Create a local compile callback manager.
///
/// The given target triple will determine the ABI, and the given
/// ErrorHandlerAddress will be used by the resulting compile callback
/// manager if a compile callback fails.
Expected<std::unique_ptr<JITCompileCallbackManager>>
createLocalCompileCallbackManager(const Triple &T, ExecutionSession &ES,
                                  JITTargetAddress ErrorHandlerAddress);

/// Create a local indriect stubs manager builder.
///
/// The given target triple will determine the ABI.
std::function<std::unique_ptr<IndirectStubsManager>()>
createLocalIndirectStubsManagerBuilder(const Triple &T);

/// Build a function pointer of FunctionType with the given constant
///        address.
///
///   Usage example: Turn a trampoline address into a function pointer constant
/// for use in a stub.
Constant *createIRTypedAddress(FunctionType &FT, JITTargetAddress Addr);

/// Create a function pointer with the given type, name, and initializer
///        in the given Module.
GlobalVariable *createImplPointer(PointerType &PT, Module &M, const Twine &Name,
                                  Constant *Initializer);

/// Turn a function declaration into a stub function that makes an
///        indirect call using the given function pointer.
void makeStub(Function &F, Value &ImplPointer);

/// Promotes private symbols to global hidden, and renames to prevent clashes
/// with other promoted symbols. The same SymbolPromoter instance should be
/// used for all symbols to be added to a single JITDylib.
class SymbolLinkagePromoter {
public:
  /// Promote symbols in the given module. Returns the set of global values
  /// that have been renamed/promoted.
  std::vector<GlobalValue *> operator()(Module &M);

private:
  unsigned NextId = 0;
};

/// Clone a function declaration into a new module.
///
///   This function can be used as the first step towards creating a callback
/// stub (see makeStub), or moving a function body (see moveFunctionBody).
///
///   If the VMap argument is non-null, a mapping will be added between F and
/// the new declaration, and between each of F's arguments and the new
/// declaration's arguments. This map can then be passed in to moveFunction to
/// move the function body if required. Note: When moving functions between
/// modules with these utilities, all decls should be cloned (and added to a
/// single VMap) before any bodies are moved. This will ensure that references
/// between functions all refer to the versions in the new module.
Function *cloneFunctionDecl(Module &Dst, const Function &F,
                            ValueToValueMapTy *VMap = nullptr);

/// Move the body of function 'F' to a cloned function declaration in a
///        different module (See related cloneFunctionDecl).
///
///   If the target function declaration is not supplied via the NewF parameter
/// then it will be looked up via the VMap.
///
///   This will delete the body of function 'F' from its original parent module,
/// but leave its declaration.
void moveFunctionBody(Function &OrigF, ValueToValueMapTy &VMap,
                      ValueMaterializer *Materializer = nullptr,
                      Function *NewF = nullptr);

/// Clone a global variable declaration into a new module.
GlobalVariable *cloneGlobalVariableDecl(Module &Dst, const GlobalVariable &GV,
                                        ValueToValueMapTy *VMap = nullptr);

/// Move global variable GV from its parent module to cloned global
///        declaration in a different module.
///
///   If the target global declaration is not supplied via the NewGV parameter
/// then it will be looked up via the VMap.
///
///   This will delete the initializer of GV from its original parent module,
/// but leave its declaration.
void moveGlobalVariableInitializer(GlobalVariable &OrigGV,
                                   ValueToValueMapTy &VMap,
                                   ValueMaterializer *Materializer = nullptr,
                                   GlobalVariable *NewGV = nullptr);

/// Clone a global alias declaration into a new module.
GlobalAlias *cloneGlobalAliasDecl(Module &Dst, const GlobalAlias &OrigA,
                                  ValueToValueMapTy &VMap);

/// Clone module flags metadata into the destination module.
void cloneModuleFlagsMetadata(Module &Dst, const Module &Src,
                              ValueToValueMapTy &VMap);

} // end namespace orc

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_INDIRECTIONUTILS_H
