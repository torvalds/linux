//===----- LLJIT.h -- An ORC-based JIT for compiling LLVM IR ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for 3Bdetails.
//
//===----------------------------------------------------------------------===//
//
// An ORC-based JIT for compiling LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LLJIT_H
#define LLVM_EXECUTIONENGINE_ORC_LLJIT_H

#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/Support/ThreadPool.h"

namespace llvm {
namespace orc {

/// A pre-fabricated ORC JIT stack that can serve as an alternative to MCJIT.
class LLJIT {
public:

  /// Destruct this instance. If a multi-threaded instance, waits for all
  /// compile threads to complete.
  ~LLJIT();

  /// Create an LLJIT instance.
  /// If NumCompileThreads is not equal to zero, creates a multi-threaded
  /// LLJIT with the given number of compile threads.
  static Expected<std::unique_ptr<LLJIT>>
  Create(JITTargetMachineBuilder JTMB, DataLayout DL,
         unsigned NumCompileThreads = 0);

  /// Returns the ExecutionSession for this instance.
  ExecutionSession &getExecutionSession() { return *ES; }

  /// Returns a reference to the JITDylib representing the JIT'd main program.
  JITDylib &getMainJITDylib() { return Main; }

  /// Create a new JITDylib with the given name and return a reference to it.
  JITDylib &createJITDylib(std::string Name) {
    return ES->createJITDylib(std::move(Name));
  }

  /// Convenience method for defining an absolute symbol.
  Error defineAbsolute(StringRef Name, JITEvaluatedSymbol Address);

  /// Convenience method for defining an

  /// Adds an IR module to the given JITDylib.
  Error addIRModule(JITDylib &JD, ThreadSafeModule TSM);

  /// Adds an IR module to the Main JITDylib.
  Error addIRModule(ThreadSafeModule TSM) {
    return addIRModule(Main, std::move(TSM));
  }

  /// Adds an object file to the given JITDylib.
  Error addObjectFile(JITDylib &JD, std::unique_ptr<MemoryBuffer> Obj);

  /// Adds an object file to the given JITDylib.
  Error addObjectFile(std::unique_ptr<MemoryBuffer> Obj) {
    return addObjectFile(Main, std::move(Obj));
  }

  /// Look up a symbol in JITDylib JD by the symbol's linker-mangled name (to
  /// look up symbols based on their IR name use the lookup function instead).
  Expected<JITEvaluatedSymbol> lookupLinkerMangled(JITDylib &JD,
                                                   StringRef Name);

  /// Look up a symbol in the main JITDylib by the symbol's linker-mangled name
  /// (to look up symbols based on their IR name use the lookup function
  /// instead).
  Expected<JITEvaluatedSymbol> lookupLinkerMangled(StringRef Name) {
    return lookupLinkerMangled(Main, Name);
  }

  /// Look up a symbol in JITDylib JD based on its IR symbol name.
  Expected<JITEvaluatedSymbol> lookup(JITDylib &JD, StringRef UnmangledName) {
    return lookupLinkerMangled(JD, mangle(UnmangledName));
  }

  /// Look up a symbol in the main JITDylib based on its IR symbol name.
  Expected<JITEvaluatedSymbol> lookup(StringRef UnmangledName) {
    return lookup(Main, UnmangledName);
  }

  /// Runs all not-yet-run static constructors.
  Error runConstructors() { return CtorRunner.run(); }

  /// Runs all not-yet-run static destructors.
  Error runDestructors() { return DtorRunner.run(); }

  /// Returns a reference to the ObjLinkingLayer
  RTDyldObjectLinkingLayer &getObjLinkingLayer() { return ObjLinkingLayer; }

protected:

  /// Create an LLJIT instance with a single compile thread.
  LLJIT(std::unique_ptr<ExecutionSession> ES, std::unique_ptr<TargetMachine> TM,
        DataLayout DL);

  /// Create an LLJIT instance with multiple compile threads.
  LLJIT(std::unique_ptr<ExecutionSession> ES, JITTargetMachineBuilder JTMB,
        DataLayout DL, unsigned NumCompileThreads);

  std::string mangle(StringRef UnmangledName);

  Error applyDataLayout(Module &M);

  void recordCtorDtors(Module &M);

  std::unique_ptr<ExecutionSession> ES;
  JITDylib &Main;

  DataLayout DL;
  std::unique_ptr<ThreadPool> CompileThreads;

  RTDyldObjectLinkingLayer ObjLinkingLayer;
  IRCompileLayer CompileLayer;

  CtorDtorRunner CtorRunner, DtorRunner;
};

/// An extended version of LLJIT that supports lazy function-at-a-time
/// compilation of LLVM IR.
class LLLazyJIT : public LLJIT {
public:

  /// Create an LLLazyJIT instance.
  /// If NumCompileThreads is not equal to zero, creates a multi-threaded
  /// LLLazyJIT with the given number of compile threads.
  static Expected<std::unique_ptr<LLLazyJIT>>
  Create(JITTargetMachineBuilder JTMB, DataLayout DL,
         JITTargetAddress ErrorAddr, unsigned NumCompileThreads = 0);

  /// Set an IR transform (e.g. pass manager pipeline) to run on each function
  /// when it is compiled.
  void setLazyCompileTransform(IRTransformLayer::TransformFunction Transform) {
    TransformLayer.setTransform(std::move(Transform));
  }

  /// Sets the partition function.
  void
  setPartitionFunction(CompileOnDemandLayer::PartitionFunction Partition) {
    CODLayer.setPartitionFunction(std::move(Partition));
  }

  /// Add a module to be lazily compiled to JITDylib JD.
  Error addLazyIRModule(JITDylib &JD, ThreadSafeModule M);

  /// Add a module to be lazily compiled to the main JITDylib.
  Error addLazyIRModule(ThreadSafeModule M) {
    return addLazyIRModule(Main, std::move(M));
  }

private:

  // Create a single-threaded LLLazyJIT instance.
  LLLazyJIT(std::unique_ptr<ExecutionSession> ES,
            std::unique_ptr<TargetMachine> TM, DataLayout DL,
            std::unique_ptr<LazyCallThroughManager> LCTMgr,
            std::function<std::unique_ptr<IndirectStubsManager>()> ISMBuilder);

  // Create a multi-threaded LLLazyJIT instance.
  LLLazyJIT(std::unique_ptr<ExecutionSession> ES, JITTargetMachineBuilder JTMB,
            DataLayout DL, unsigned NumCompileThreads,
            std::unique_ptr<LazyCallThroughManager> LCTMgr,
            std::function<std::unique_ptr<IndirectStubsManager>()> ISMBuilder);

  std::unique_ptr<LazyCallThroughManager> LCTMgr;
  std::function<std::unique_ptr<IndirectStubsManager>()> ISMBuilder;

  IRTransformLayer TransformLayer;
  CompileOnDemandLayer CODLayer;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LLJIT_H
