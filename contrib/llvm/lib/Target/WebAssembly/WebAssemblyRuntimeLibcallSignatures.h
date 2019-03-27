// CodeGen/RuntimeLibcallSignatures.h - R.T. Lib. Call Signatures -*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides signature information for runtime libcalls.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_RUNTIME_LIBCALL_SIGNATURES_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_RUNTIME_LIBCALL_SIGNATURES_H

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"

namespace llvm {

class WebAssemblySubtarget;

extern void GetLibcallSignature(const WebAssemblySubtarget &Subtarget,
                                RTLIB::Libcall LC,
                                SmallVectorImpl<wasm::ValType> &Rets,
                                SmallVectorImpl<wasm::ValType> &Params);

extern void GetLibcallSignature(const WebAssemblySubtarget &Subtarget,
                                const char *Name,
                                SmallVectorImpl<wasm::ValType> &Rets,
                                SmallVectorImpl<wasm::ValType> &Params);

} // end namespace llvm

#endif
