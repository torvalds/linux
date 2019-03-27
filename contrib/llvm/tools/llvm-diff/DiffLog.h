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

#ifndef LLVM_TOOLS_LLVM_DIFF_DIFFLOG_H
#define LLVM_TOOLS_LLVM_DIFF_DIFFLOG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
  class Instruction;
  class Value;
  class Consumer;

  /// Trichotomy assumption
  enum DiffChange { DC_match, DC_left, DC_right };

  /// A temporary-object class for building up log messages.
  class LogBuilder {
    Consumer *consumer;

    /// The use of a stored StringRef here is okay because
    /// LogBuilder should be used only as a temporary, and as a
    /// temporary it will be destructed before whatever temporary
    /// might be initializing this format.
    StringRef Format;

    SmallVector<Value*, 4> Arguments;

  public:
    LogBuilder(Consumer &c, StringRef Format) : consumer(&c), Format(Format) {}
    LogBuilder(LogBuilder &&L)
        : consumer(L.consumer), Format(L.Format),
          Arguments(std::move(L.Arguments)) {
      L.consumer = nullptr;
    }

    LogBuilder &operator<<(Value *V) {
      Arguments.push_back(V);
      return *this;
    }

    ~LogBuilder();

    StringRef getFormat() const;
    unsigned getNumArguments() const;
    Value *getArgument(unsigned I) const;
  };

  /// A temporary-object class for building up diff messages.
  class DiffLogBuilder {
    typedef std::pair<Instruction*,Instruction*> DiffRecord;
    SmallVector<DiffRecord, 20> Diff;

    Consumer &consumer;

  public:
    DiffLogBuilder(Consumer &c) : consumer(c) {}
    ~DiffLogBuilder();

    void addMatch(Instruction *L, Instruction *R);
    // HACK: VS 2010 has a bug in the stdlib that requires this.
    void addLeft(Instruction *L);
    void addRight(Instruction *R);

    unsigned getNumLines() const;
    DiffChange getLineKind(unsigned I) const;
    Instruction *getLeft(unsigned I) const;
    Instruction *getRight(unsigned I) const;
  };

}

#endif
