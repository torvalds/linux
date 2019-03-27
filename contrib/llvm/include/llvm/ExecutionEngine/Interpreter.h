//===-- Interpreter.h - Abstract Execution Engine Interface -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file forces the interpreter to link in on certain operating systems.
// (Windows).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_INTERPRETER_H
#define LLVM_EXECUTIONENGINE_INTERPRETER_H

#include "llvm/ExecutionEngine/ExecutionEngine.h"

extern "C" void LLVMLinkInInterpreter();

namespace {
  struct ForceInterpreterLinking {
    ForceInterpreterLinking() { LLVMLinkInInterpreter(); }
  } ForceInterpreterLinking;
}

#endif
