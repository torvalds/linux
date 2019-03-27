//===-- WasmDump.cpp - wasm-specific dumper ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the wasm-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "llvm-objdump.h"
#include "llvm/Object/Wasm.h"

using namespace llvm;
using namespace object;

void llvm::printWasmFileHeader(const object::ObjectFile *Obj) {
  const WasmObjectFile *File = dyn_cast<const WasmObjectFile>(Obj);

  outs() << "Program Header:\n";
  outs() << "Version: 0x";
  outs().write_hex(File->getHeader().Version);
  outs() << "\n";
}
