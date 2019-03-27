//===- MCMachObjectTargetWriter.cpp - Mach-O Target Writer Subclass -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCMachObjectWriter.h"

using namespace llvm;

MCMachObjectTargetWriter::MCMachObjectTargetWriter(bool Is64Bit_,
                                                   uint32_t CPUType_,
                                                   uint32_t CPUSubtype_)
    : Is64Bit(Is64Bit_), CPUType(CPUType_), CPUSubtype(CPUSubtype_) {}

MCMachObjectTargetWriter::~MCMachObjectTargetWriter() = default;
