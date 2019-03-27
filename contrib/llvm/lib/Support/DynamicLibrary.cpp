//===-- DynamicLibrary.cpp - Runtime link/load libraries --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system DynamicLibrary concept.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DynamicLibrary.h"
#include "llvm-c/Support.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace llvm;
using namespace llvm::sys;

// All methods for HandleSet should be used holding SymbolsMutex.
class DynamicLibrary::HandleSet {
  typedef std::vector<void *> HandleList;
  HandleList Handles;
  void *Process;

public:
  static void *DLOpen(const char *Filename, std::string *Err);
  static void DLClose(void *Handle);
  static void *DLSym(void *Handle, const char *Symbol);

  HandleSet() : Process(nullptr) {}
  ~HandleSet();

  HandleList::iterator Find(void *Handle) {
    return std::find(Handles.begin(), Handles.end(), Handle);
  }

  bool Contains(void *Handle) {
    return Handle == Process || Find(Handle) != Handles.end();
  }

  bool AddLibrary(void *Handle, bool IsProcess = false, bool CanClose = true) {
#ifdef _WIN32
    assert((Handle == this ? IsProcess : !IsProcess) && "Bad Handle.");
#endif

    if (LLVM_LIKELY(!IsProcess)) {
      if (Find(Handle) != Handles.end()) {
        if (CanClose)
          DLClose(Handle);
        return false;
      }
      Handles.push_back(Handle);
    } else {
#ifndef _WIN32
      if (Process) {
        if (CanClose)
          DLClose(Process);
        if (Process == Handle)
          return false;
      }
#endif
      Process = Handle;
    }
    return true;
  }

  void *LibLookup(const char *Symbol, DynamicLibrary::SearchOrdering Order) {
    if (Order & SO_LoadOrder) {
      for (void *Handle : Handles) {
        if (void *Ptr = DLSym(Handle, Symbol))
          return Ptr;
      }
    } else {
      for (void *Handle : llvm::reverse(Handles)) {
        if (void *Ptr = DLSym(Handle, Symbol))
          return Ptr;
      }
    }
    return nullptr;
  }

  void *Lookup(const char *Symbol, DynamicLibrary::SearchOrdering Order) {
    assert(!((Order & SO_LoadedFirst) && (Order & SO_LoadedLast)) &&
           "Invalid Ordering");

    if (!Process || (Order & SO_LoadedFirst)) {
      if (void *Ptr = LibLookup(Symbol, Order))
        return Ptr;
    }
    if (Process) {
      // Use OS facilities to search the current binary and all loaded libs.
      if (void *Ptr = DLSym(Process, Symbol))
        return Ptr;

      // Search any libs that might have been skipped because of RTLD_LOCAL.
      if (Order & SO_LoadedLast) {
        if (void *Ptr = LibLookup(Symbol, Order))
          return Ptr;
      }
    }
    return nullptr;
  }
};

namespace {
// Collection of symbol name/value pairs to be searched prior to any libraries.
static llvm::ManagedStatic<llvm::StringMap<void *>> ExplicitSymbols;
// Collection of known library handles.
static llvm::ManagedStatic<DynamicLibrary::HandleSet> OpenedHandles;
// Lock for ExplicitSymbols and OpenedHandles.
static llvm::ManagedStatic<llvm::sys::SmartMutex<true>> SymbolsMutex;
}

#ifdef _WIN32

#include "Windows/DynamicLibrary.inc"

#else

#include "Unix/DynamicLibrary.inc"

#endif

char DynamicLibrary::Invalid;
DynamicLibrary::SearchOrdering DynamicLibrary::SearchOrder =
    DynamicLibrary::SO_Linker;

namespace llvm {
void *SearchForAddressOfSpecialSymbol(const char *SymbolName) {
  return DoSearch(SymbolName); // DynamicLibrary.inc
}
}

void DynamicLibrary::AddSymbol(StringRef SymbolName, void *SymbolValue) {
  SmartScopedLock<true> Lock(*SymbolsMutex);
  (*ExplicitSymbols)[SymbolName] = SymbolValue;
}

DynamicLibrary DynamicLibrary::getPermanentLibrary(const char *FileName,
                                                   std::string *Err) {
  // Force OpenedHandles to be added into the ManagedStatic list before any
  // ManagedStatic can be added from static constructors in HandleSet::DLOpen.
  HandleSet& HS = *OpenedHandles;

  void *Handle = HandleSet::DLOpen(FileName, Err);
  if (Handle != &Invalid) {
    SmartScopedLock<true> Lock(*SymbolsMutex);
    HS.AddLibrary(Handle, /*IsProcess*/ FileName == nullptr);
  }

  return DynamicLibrary(Handle);
}

DynamicLibrary DynamicLibrary::addPermanentLibrary(void *Handle,
                                                   std::string *Err) {
  SmartScopedLock<true> Lock(*SymbolsMutex);
  // If we've already loaded this library, tell the caller.
  if (!OpenedHandles->AddLibrary(Handle, /*IsProcess*/false, /*CanClose*/false))
    *Err = "Library already loaded";

  return DynamicLibrary(Handle);
}

void *DynamicLibrary::getAddressOfSymbol(const char *SymbolName) {
  if (!isValid())
    return nullptr;
  return HandleSet::DLSym(Data, SymbolName);
}

void *DynamicLibrary::SearchForAddressOfSymbol(const char *SymbolName) {
  {
    SmartScopedLock<true> Lock(*SymbolsMutex);

    // First check symbols added via AddSymbol().
    if (ExplicitSymbols.isConstructed()) {
      StringMap<void *>::iterator i = ExplicitSymbols->find(SymbolName);

      if (i != ExplicitSymbols->end())
        return i->second;
    }

    // Now search the libraries.
    if (OpenedHandles.isConstructed()) {
      if (void *Ptr = OpenedHandles->Lookup(SymbolName, SearchOrder))
        return Ptr;
    }
  }

  return llvm::SearchForAddressOfSpecialSymbol(SymbolName);
}

//===----------------------------------------------------------------------===//
// C API.
//===----------------------------------------------------------------------===//

LLVMBool LLVMLoadLibraryPermanently(const char *Filename) {
  return llvm::sys::DynamicLibrary::LoadLibraryPermanently(Filename);
}

void *LLVMSearchForAddressOfSymbol(const char *symbolName) {
  return llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(symbolName);
}

void LLVMAddSymbol(const char *symbolName, void *symbolValue) {
  return llvm::sys::DynamicLibrary::AddSymbol(symbolName, symbolValue);
}
