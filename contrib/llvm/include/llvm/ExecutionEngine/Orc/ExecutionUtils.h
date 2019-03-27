//===- ExecutionUtils.h - Utilities for executing code in Orc ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for executing code in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/OrcError.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Support/DynamicLibrary.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class ConstantArray;
class GlobalVariable;
class Function;
class Module;
class TargetMachine;
class Value;

namespace orc {

/// This iterator provides a convenient way to iterate over the elements
///        of an llvm.global_ctors/llvm.global_dtors instance.
///
///   The easiest way to get hold of instances of this class is to use the
/// getConstructors/getDestructors functions.
class CtorDtorIterator {
public:
  /// Accessor for an element of the global_ctors/global_dtors array.
  ///
  ///   This class provides a read-only view of the element with any casts on
  /// the function stripped away.
  struct Element {
    Element(unsigned Priority, Function *Func, Value *Data)
      : Priority(Priority), Func(Func), Data(Data) {}

    unsigned Priority;
    Function *Func;
    Value *Data;
  };

  /// Construct an iterator instance. If End is true then this iterator
  ///        acts as the end of the range, otherwise it is the beginning.
  CtorDtorIterator(const GlobalVariable *GV, bool End);

  /// Test iterators for equality.
  bool operator==(const CtorDtorIterator &Other) const;

  /// Test iterators for inequality.
  bool operator!=(const CtorDtorIterator &Other) const;

  /// Pre-increment iterator.
  CtorDtorIterator& operator++();

  /// Post-increment iterator.
  CtorDtorIterator operator++(int);

  /// Dereference iterator. The resulting value provides a read-only view
  ///        of this element of the global_ctors/global_dtors list.
  Element operator*() const;

private:
  const ConstantArray *InitList;
  unsigned I;
};

/// Create an iterator range over the entries of the llvm.global_ctors
///        array.
iterator_range<CtorDtorIterator> getConstructors(const Module &M);

/// Create an iterator range over the entries of the llvm.global_ctors
///        array.
iterator_range<CtorDtorIterator> getDestructors(const Module &M);

/// Convenience class for recording constructor/destructor names for
///        later execution.
template <typename JITLayerT>
class LegacyCtorDtorRunner {
public:
  /// Construct a CtorDtorRunner for the given range using the given
  ///        name mangling function.
  LegacyCtorDtorRunner(std::vector<std::string> CtorDtorNames, VModuleKey K)
      : CtorDtorNames(std::move(CtorDtorNames)), K(K) {}

  /// Run the recorded constructors/destructors through the given JIT
  ///        layer.
  Error runViaLayer(JITLayerT &JITLayer) const {
    using CtorDtorTy = void (*)();

    for (const auto &CtorDtorName : CtorDtorNames) {
      if (auto CtorDtorSym = JITLayer.findSymbolIn(K, CtorDtorName, false)) {
        if (auto AddrOrErr = CtorDtorSym.getAddress()) {
          CtorDtorTy CtorDtor =
            reinterpret_cast<CtorDtorTy>(static_cast<uintptr_t>(*AddrOrErr));
          CtorDtor();
        } else
          return AddrOrErr.takeError();
      } else {
        if (auto Err = CtorDtorSym.takeError())
          return Err;
        else
          return make_error<JITSymbolNotFound>(CtorDtorName);
      }
    }
    return Error::success();
  }

private:
  std::vector<std::string> CtorDtorNames;
  orc::VModuleKey K;
};

class CtorDtorRunner {
public:
  CtorDtorRunner(JITDylib &JD) : JD(JD) {}
  void add(iterator_range<CtorDtorIterator> CtorDtors);
  Error run();

private:
  using CtorDtorList = std::vector<SymbolStringPtr>;
  using CtorDtorPriorityMap = std::map<unsigned, CtorDtorList>;

  JITDylib &JD;
  CtorDtorPriorityMap CtorDtorsByPriority;
};

/// Support class for static dtor execution. For hosted (in-process) JITs
///        only!
///
///   If a __cxa_atexit function isn't found C++ programs that use static
/// destructors will fail to link. However, we don't want to use the host
/// process's __cxa_atexit, because it will schedule JIT'd destructors to run
/// after the JIT has been torn down, which is no good. This class makes it easy
/// to override __cxa_atexit (and the related __dso_handle).
///
///   To use, clients should manually call searchOverrides from their symbol
/// resolver. This should generally be done after attempting symbol resolution
/// inside the JIT, but before searching the host process's symbol table. When
/// the client determines that destructors should be run (generally at JIT
/// teardown or after a return from main), the runDestructors method should be
/// called.
class LocalCXXRuntimeOverridesBase {
public:
  /// Run any destructors recorded by the overriden __cxa_atexit function
  /// (CXAAtExitOverride).
  void runDestructors();

protected:
  template <typename PtrTy> JITTargetAddress toTargetAddress(PtrTy *P) {
    return static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(P));
  }

  using DestructorPtr = void (*)(void *);
  using CXXDestructorDataPair = std::pair<DestructorPtr, void *>;
  using CXXDestructorDataPairList = std::vector<CXXDestructorDataPair>;
  CXXDestructorDataPairList DSOHandleOverride;
  static int CXAAtExitOverride(DestructorPtr Destructor, void *Arg,
                               void *DSOHandle);
};

class LegacyLocalCXXRuntimeOverrides : public LocalCXXRuntimeOverridesBase {
public:
  /// Create a runtime-overrides class.
  template <typename MangleFtorT>
  LegacyLocalCXXRuntimeOverrides(const MangleFtorT &Mangle) {
    addOverride(Mangle("__dso_handle"), toTargetAddress(&DSOHandleOverride));
    addOverride(Mangle("__cxa_atexit"), toTargetAddress(&CXAAtExitOverride));
  }

  /// Search overrided symbols.
  JITEvaluatedSymbol searchOverrides(const std::string &Name) {
    auto I = CXXRuntimeOverrides.find(Name);
    if (I != CXXRuntimeOverrides.end())
      return JITEvaluatedSymbol(I->second, JITSymbolFlags::Exported);
    return nullptr;
  }

private:
  void addOverride(const std::string &Name, JITTargetAddress Addr) {
    CXXRuntimeOverrides.insert(std::make_pair(Name, Addr));
  }

  StringMap<JITTargetAddress> CXXRuntimeOverrides;
};

class LocalCXXRuntimeOverrides : public LocalCXXRuntimeOverridesBase {
public:
  Error enable(JITDylib &JD, MangleAndInterner &Mangler);
};

/// A utility class to expose symbols found via dlsym to the JIT.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found in the given DynamicLibrary that
/// passes the 'Allow' predicate will be added to the JITDylib.
class DynamicLibrarySearchGenerator {
public:
  using SymbolPredicate = std::function<bool(SymbolStringPtr)>;

  /// Create a DynamicLibrarySearchGenerator that searches for symbols in the
  /// given sys::DynamicLibrary.
  /// If the Allow predicate is given then only symbols matching the predicate
  /// will be searched for in the DynamicLibrary. If the predicate is not given
  /// then all symbols will be searched for.
  DynamicLibrarySearchGenerator(sys::DynamicLibrary Dylib, const DataLayout &DL,
                                SymbolPredicate Allow = SymbolPredicate());

  /// Permanently loads the library at the given path and, on success, returns
  /// a DynamicLibrarySearchGenerator that will search it for symbol definitions
  /// in the library. On failure returns the reason the library failed to load.
  static Expected<DynamicLibrarySearchGenerator>
  Load(const char *FileName, const DataLayout &DL,
       SymbolPredicate Allow = SymbolPredicate());

  /// Creates a DynamicLibrarySearchGenerator that searches for symbols in
  /// the current process.
  static Expected<DynamicLibrarySearchGenerator>
  GetForCurrentProcess(const DataLayout &DL,
                       SymbolPredicate Allow = SymbolPredicate()) {
    return Load(nullptr, DL, std::move(Allow));
  }

  SymbolNameSet operator()(JITDylib &JD, const SymbolNameSet &Names);

private:
  sys::DynamicLibrary Dylib;
  SymbolPredicate Allow;
  char GlobalPrefix;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H
