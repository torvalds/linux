//===-- ubsan_init.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Initialization function for UBSan runtime.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_INIT_H
#define UBSAN_INIT_H

namespace __ubsan {

// Get the full tool name for UBSan.
const char *GetSanititizerToolName();

// Initialize UBSan as a standalone tool. Typically should be called early
// during initialization.
void InitAsStandalone();

// Initialize UBSan as a standalone tool, if it hasn't been initialized before.
void InitAsStandaloneIfNecessary();

// Initializes UBSan as a plugin tool. This function should be called once
// from "parent tool" (e.g. ASan) initialization.
void InitAsPlugin();

}  // namespace __ubsan

#endif  // UBSAN_INIT_H
