//===- GCMetadataPrinter.cpp - Garbage collection infrastructure ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the abstract base class GCMetadataPrinter.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GCMetadataPrinter.h"

using namespace llvm;

LLVM_INSTANTIATE_REGISTRY(GCMetadataPrinterRegistry)

GCMetadataPrinter::GCMetadataPrinter() = default;

GCMetadataPrinter::~GCMetadataPrinter() = default;
