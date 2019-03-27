//===- Arg.h - Parsed Argument Classes --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the llvm::Arg class for parsed arguments.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_ARG_H
#define LLVM_OPTION_ARG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <string>

namespace llvm {

class raw_ostream;

namespace opt {

class ArgList;

/// A concrete instance of a particular driver option.
///
/// The Arg class encodes just enough information to be able to
/// derive the argument values efficiently.
class Arg {
private:
  /// The option this argument is an instance of.
  const Option Opt;

  /// The argument this argument was derived from (during tool chain
  /// argument translation), if any.
  const Arg *BaseArg;

  /// How this instance of the option was spelled.
  StringRef Spelling;

  /// The index at which this argument appears in the containing
  /// ArgList.
  unsigned Index;

  /// Was this argument used to effect compilation?
  ///
  /// This is used for generating "argument unused" diagnostics.
  mutable unsigned Claimed : 1;

  /// Does this argument own its values?
  mutable unsigned OwnsValues : 1;

  /// The argument values, as C strings.
  SmallVector<const char *, 2> Values;

public:
  Arg(const Option Opt, StringRef Spelling, unsigned Index,
      const Arg *BaseArg = nullptr);
  Arg(const Option Opt, StringRef Spelling, unsigned Index,
      const char *Value0, const Arg *BaseArg = nullptr);
  Arg(const Option Opt, StringRef Spelling, unsigned Index,
      const char *Value0, const char *Value1, const Arg *BaseArg = nullptr);
  Arg(const Arg &) = delete;
  Arg &operator=(const Arg &) = delete;
  ~Arg();

  const Option &getOption() const { return Opt; }
  StringRef getSpelling() const { return Spelling; }
  unsigned getIndex() const { return Index; }

  /// Return the base argument which generated this arg.
  ///
  /// This is either the argument itself or the argument it was
  /// derived from during tool chain specific argument translation.
  const Arg &getBaseArg() const {
    return BaseArg ? *BaseArg : *this;
  }
  void setBaseArg(const Arg *BaseArg) { this->BaseArg = BaseArg; }

  bool getOwnsValues() const { return OwnsValues; }
  void setOwnsValues(bool Value) const { OwnsValues = Value; }

  bool isClaimed() const { return getBaseArg().Claimed; }

  /// Set the Arg claimed bit.
  void claim() const { getBaseArg().Claimed = true; }

  unsigned getNumValues() const { return Values.size(); }

  const char *getValue(unsigned N = 0) const {
    return Values[N];
  }

  SmallVectorImpl<const char *> &getValues() { return Values; }
  const SmallVectorImpl<const char *> &getValues() const { return Values; }

  bool containsValue(StringRef Value) const {
    for (unsigned i = 0, e = getNumValues(); i != e; ++i)
      if (Values[i] == Value)
        return true;
    return false;
  }

  /// Append the argument onto the given array as strings.
  void render(const ArgList &Args, ArgStringList &Output) const;

  /// Append the argument, render as an input, onto the given
  /// array as strings.
  ///
  /// The distinction is that some options only render their values
  /// when rendered as a input (e.g., Xlinker).
  void renderAsInput(const ArgList &Args, ArgStringList &Output) const;

  void print(raw_ostream &O) const;
  void dump() const;

  /// Return a formatted version of the argument and
  /// its values, for debugging and diagnostics.
  std::string getAsString(const ArgList &Args) const;
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_ARG_H
