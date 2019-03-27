//===- X86ModRMFilters.cpp - Disassembler ModR/M filterss -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "X86ModRMFilters.h"

using namespace llvm::X86Disassembler;

void ModRMFilter::anchor() { }

void DumbFilter::anchor() { }

void ModFilter::anchor() { }

void ExtendedFilter::anchor() { }

void ExactFilter::anchor() { }
