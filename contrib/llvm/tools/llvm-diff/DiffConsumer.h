//===-- DiffConsumer.h - Difference Consumer --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines the interface to the LLVM difference Consumer
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_DIFF_DIFFCONSUMER_H
#define LLVM_TOOLS_LLVM_DIFF_DIFFCONSUMER_H

#include "DiffLog.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class StringRef;
  class Module;
  class Value;
  class Function;

  /// The interface for consumers of difference data.
  class Consumer {
    virtual void anchor();
  public:
    /// Record that a local context has been entered.  Left and
    /// Right are IR "containers" of some sort which are being
    /// considered for structural equivalence: global variables,
    /// functions, blocks, instructions, etc.
    virtual void enterContext(Value *Left, Value *Right) = 0;

    /// Record that a local context has been exited.
    virtual void exitContext() = 0;

    /// Record a difference within the current context.
    virtual void log(StringRef Text) = 0;

    /// Record a formatted difference within the current context.
    virtual void logf(const LogBuilder &Log) = 0;

    /// Record a line-by-line instruction diff.
    virtual void logd(const DiffLogBuilder &Log) = 0;

  protected:
    virtual ~Consumer() {}
  };

  class DiffConsumer : public Consumer {
  private:
    struct DiffContext {
      DiffContext(Value *L, Value *R)
        : L(L), R(R), Differences(false), IsFunction(isa<Function>(L)) {}
      Value *L;
      Value *R;
      bool Differences;
      bool IsFunction;
      DenseMap<Value*,unsigned> LNumbering;
      DenseMap<Value*,unsigned> RNumbering;
    };

    raw_ostream &out;
    SmallVector<DiffContext, 5> contexts;
    bool Differences;
    unsigned Indent;

    void printValue(Value *V, bool isL);
    void header();
    void indent();

  public:
    DiffConsumer()
      : out(errs()), Differences(false), Indent(0) {}

    bool hadDifferences() const;
    void enterContext(Value *L, Value *R) override;
    void exitContext() override;
    void log(StringRef text) override;
    void logf(const LogBuilder &Log) override;
    void logd(const DiffLogBuilder &Log) override;
  };
}

#endif
