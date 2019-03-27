//===-- WebAssemblyTargetObjectFile.cpp - WebAssembly Object Info ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the functions of the WebAssembly-specific subclass
/// of TargetLoweringObjectFile.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetObjectFile.h"
#include "WebAssemblyTargetMachine.h"

using namespace llvm;

void WebAssemblyTargetObjectFile::Initialize(MCContext &Ctx,
                                             const TargetMachine &TM) {
  TargetLoweringObjectFileWasm::Initialize(Ctx, TM);
  InitializeWasm();
}
