//===- WasmTraits.h - DenseMap traits for the Wasm structures ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides llvm::DenseMapInfo traits for the Wasm structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WASMTRAITS_H
#define LLVM_OBJECT_WASMTRAITS_H

#include "llvm/ADT/Hashing.h"
#include "llvm/BinaryFormat/Wasm.h"

namespace llvm {

template <typename T> struct DenseMapInfo;

// Traits for using WasmSignature in a DenseMap.
template <> struct DenseMapInfo<wasm::WasmSignature> {
  static wasm::WasmSignature getEmptyKey() {
    wasm::WasmSignature Sig;
    Sig.State = wasm::WasmSignature::Empty;
    return Sig;
  }
  static wasm::WasmSignature getTombstoneKey() {
    wasm::WasmSignature Sig;
    Sig.State = wasm::WasmSignature::Tombstone;
    return Sig;
  }
  static unsigned getHashValue(const wasm::WasmSignature &Sig) {
    uintptr_t H = hash_value(Sig.State);
    for (auto Ret : Sig.Returns)
      H = hash_combine(H, Ret);
    for (auto Param : Sig.Params)
      H = hash_combine(H, Param);
    return H;
  }
  static bool isEqual(const wasm::WasmSignature &LHS,
                      const wasm::WasmSignature &RHS) {
    return LHS == RHS;
  }
};

// Traits for using WasmGlobalType in a DenseMap
template <> struct DenseMapInfo<wasm::WasmGlobalType> {
  static wasm::WasmGlobalType getEmptyKey() {
    return wasm::WasmGlobalType{1, true};
  }
  static wasm::WasmGlobalType getTombstoneKey() {
    return wasm::WasmGlobalType{2, true};
  }
  static unsigned getHashValue(const wasm::WasmGlobalType &GlobalType) {
    return hash_combine(GlobalType.Type, GlobalType.Mutable);
  }
  static bool isEqual(const wasm::WasmGlobalType &LHS,
                      const wasm::WasmGlobalType &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_OBJECT_WASMTRAITS_H
