//===-- WebAssemblySelectionDAGInfo.cpp - WebAssembly SelectionDAG Info ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the WebAssemblySelectionDAGInfo class.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-selectiondag-info"

WebAssemblySelectionDAGInfo::~WebAssemblySelectionDAGInfo() {}
