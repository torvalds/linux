//===----------- JITSymbol.cpp - JITSymbol class implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// JITSymbol class implementation plus helper functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Object/ObjectFile.h"

using namespace llvm;

JITSymbolFlags llvm::JITSymbolFlags::fromGlobalValue(const GlobalValue &GV) {
  JITSymbolFlags Flags = JITSymbolFlags::None;
  if (GV.hasWeakLinkage() || GV.hasLinkOnceLinkage())
    Flags |= JITSymbolFlags::Weak;
  if (GV.hasCommonLinkage())
    Flags |= JITSymbolFlags::Common;
  if (!GV.hasLocalLinkage() && !GV.hasHiddenVisibility())
    Flags |= JITSymbolFlags::Exported;

  if (isa<Function>(GV))
    Flags |= JITSymbolFlags::Callable;
  else if (isa<GlobalAlias>(GV) &&
           isa<Function>(cast<GlobalAlias>(GV).getAliasee()))
    Flags |= JITSymbolFlags::Callable;

  return Flags;
}

Expected<JITSymbolFlags>
llvm::JITSymbolFlags::fromObjectSymbol(const object::SymbolRef &Symbol) {
  JITSymbolFlags Flags = JITSymbolFlags::None;
  if (Symbol.getFlags() & object::BasicSymbolRef::SF_Weak)
    Flags |= JITSymbolFlags::Weak;
  if (Symbol.getFlags() & object::BasicSymbolRef::SF_Common)
    Flags |= JITSymbolFlags::Common;
  if (Symbol.getFlags() & object::BasicSymbolRef::SF_Exported)
    Flags |= JITSymbolFlags::Exported;

  auto SymbolType = Symbol.getType();
  if (!SymbolType)
    return SymbolType.takeError();

  if (*SymbolType & object::SymbolRef::ST_Function)
    Flags |= JITSymbolFlags::Callable;

  return Flags;
}

ARMJITSymbolFlags
llvm::ARMJITSymbolFlags::fromObjectSymbol(const object::SymbolRef &Symbol) {
  ARMJITSymbolFlags Flags;
  if (Symbol.getFlags() & object::BasicSymbolRef::SF_Thumb)
    Flags |= ARMJITSymbolFlags::Thumb;
  return Flags;
}

/// Performs lookup by, for each symbol, first calling
///        findSymbolInLogicalDylib and if that fails calling
///        findSymbol.
void LegacyJITSymbolResolver::lookup(const LookupSet &Symbols,
                                     OnResolvedFunction OnResolved) {
  JITSymbolResolver::LookupResult Result;
  for (auto &Symbol : Symbols) {
    std::string SymName = Symbol.str();
    if (auto Sym = findSymbolInLogicalDylib(SymName)) {
      if (auto AddrOrErr = Sym.getAddress())
        Result[Symbol] = JITEvaluatedSymbol(*AddrOrErr, Sym.getFlags());
      else {
        OnResolved(AddrOrErr.takeError());
        return;
      }
    } else if (auto Err = Sym.takeError()) {
      OnResolved(std::move(Err));
      return;
    } else {
      // findSymbolInLogicalDylib failed. Lets try findSymbol.
      if (auto Sym = findSymbol(SymName)) {
        if (auto AddrOrErr = Sym.getAddress())
          Result[Symbol] = JITEvaluatedSymbol(*AddrOrErr, Sym.getFlags());
        else {
          OnResolved(AddrOrErr.takeError());
          return;
        }
      } else if (auto Err = Sym.takeError()) {
        OnResolved(std::move(Err));
        return;
      } else {
        OnResolved(make_error<StringError>("Symbol not found: " + Symbol,
                                           inconvertibleErrorCode()));
        return;
      }
    }
  }

  OnResolved(std::move(Result));
}

/// Performs flags lookup by calling findSymbolInLogicalDylib and
///        returning the flags value for that symbol.
Expected<JITSymbolResolver::LookupSet>
LegacyJITSymbolResolver::getResponsibilitySet(const LookupSet &Symbols) {
  JITSymbolResolver::LookupSet Result;

  for (auto &Symbol : Symbols) {
    std::string SymName = Symbol.str();
    if (auto Sym = findSymbolInLogicalDylib(SymName)) {
      // If there's an existing def but it is not strong, then the caller is
      // responsible for it.
      if (!Sym.getFlags().isStrong())
        Result.insert(Symbol);
    } else if (auto Err = Sym.takeError())
      return std::move(Err);
    else {
      // If there is no existing definition then the caller is responsible for
      // it.
      Result.insert(Symbol);
    }
  }

  return std::move(Result);
}
