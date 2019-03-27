//===-- llvm/BinaryFormat/Wasm.cpp -------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Wasm.h"

std::string llvm::wasm::toString(wasm::WasmSymbolType type) {
  switch (type) {
  case wasm::WASM_SYMBOL_TYPE_FUNCTION:
    return "WASM_SYMBOL_TYPE_FUNCTION";
  case wasm::WASM_SYMBOL_TYPE_GLOBAL:
    return "WASM_SYMBOL_TYPE_GLOBAL";
  case wasm::WASM_SYMBOL_TYPE_DATA:
    return "WASM_SYMBOL_TYPE_DATA";
  case wasm::WASM_SYMBOL_TYPE_SECTION:
    return "WASM_SYMBOL_TYPE_SECTION";
  case wasm::WASM_SYMBOL_TYPE_EVENT:
    return "WASM_SYMBOL_TYPE_EVENT";
  }
  llvm_unreachable("unknown symbol type");
}

std::string llvm::wasm::relocTypetoString(uint32_t type) {
  switch (type) {
#define WASM_RELOC(NAME, VALUE)                                                \
  case VALUE:                                                                  \
    return #NAME;
#include "llvm/BinaryFormat/WasmRelocs.def"
#undef WASM_RELOC
  default:
    llvm_unreachable("unknown reloc type");
  }
}
