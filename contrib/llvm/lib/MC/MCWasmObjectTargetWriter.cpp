//===-- MCWasmObjectTargetWriter.cpp - Wasm Target Writer Subclass --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCWasmObjectWriter.h"

using namespace llvm;

MCWasmObjectTargetWriter::MCWasmObjectTargetWriter(bool Is64Bit_)
    : Is64Bit(Is64Bit_) {}

// Pin the vtable to this object file
MCWasmObjectTargetWriter::~MCWasmObjectTargetWriter() = default;
