//===-- AMDGPUCodeEmitter.cpp - AMDGPU Code Emitter interface -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// CodeEmitter interface for R600 and SI codegen.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMCCodeEmitter.h"

using namespace llvm;

// pin vtable to this file
void AMDGPUMCCodeEmitter::anchor() {}

