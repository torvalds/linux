//===- ChainedDiagnosticConsumer.cpp - Chain Diagnostic Clients -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/ChainedDiagnosticConsumer.h"

using namespace clang;

void ChainedDiagnosticConsumer::anchor() { }
