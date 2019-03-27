//===-- WebAssemblyTargetObjectFile.h - WebAssembly Object Info -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the WebAssembly-specific subclass of
/// TargetLoweringObjectFile.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

class WebAssemblyTargetObjectFile final : public TargetLoweringObjectFileWasm {
public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;
};

} // end namespace llvm

#endif
