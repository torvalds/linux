//===-- RenderScriptx86ABIFixups.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_RENDERSCRIPT_X86_H
#define LLDB_RENDERSCRIPT_X86_H

#include "llvm/IR/Module.h"

namespace lldb_private {
namespace lldb_renderscript {

bool fixupX86FunctionCalls(llvm::Module &module);

bool fixupX86_64FunctionCalls(llvm::Module &module);
}
}
#endif
