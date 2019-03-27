//===-- DiffLog.h - Difference Log Builder and accessories ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines the interface to the LLVM difference log builder.
//
//===----------------------------------------------------------------------===//

#include "DiffLog.h"
#include "DiffConsumer.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;

LogBuilder::~LogBuilder() {
  if (consumer)
    consumer->logf(*this);
}

StringRef LogBuilder::getFormat() const { return Format; }

unsigned LogBuilder::getNumArguments() const { return Arguments.size(); }
Value *LogBuilder::getArgument(unsigned I) const { return Arguments[I]; }

DiffLogBuilder::~DiffLogBuilder() { consumer.logd(*this); }

void DiffLogBuilder::addMatch(Instruction *L, Instruction *R) {
  Diff.push_back(DiffRecord(L, R));
}
void DiffLogBuilder::addLeft(Instruction *L) {
  // HACK: VS 2010 has a bug in the stdlib that requires this.
  Diff.push_back(DiffRecord(L, DiffRecord::second_type(nullptr)));
}
void DiffLogBuilder::addRight(Instruction *R) {
  // HACK: VS 2010 has a bug in the stdlib that requires this.
  Diff.push_back(DiffRecord(DiffRecord::first_type(nullptr), R));
}

unsigned DiffLogBuilder::getNumLines() const { return Diff.size(); }

DiffChange DiffLogBuilder::getLineKind(unsigned I) const {
  return (Diff[I].first ? (Diff[I].second ? DC_match : DC_left)
                        : DC_right);
}
Instruction *DiffLogBuilder::getLeft(unsigned I) const { return Diff[I].first; }
Instruction *DiffLogBuilder::getRight(unsigned I) const { return Diff[I].second; }
