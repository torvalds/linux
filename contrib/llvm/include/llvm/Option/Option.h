//===- Option.h - Abstract Driver Options -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTION_H
#define LLVM_OPTION_OPTION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <string>

namespace llvm {

class raw_ostream;

namespace opt {

class Arg;
class ArgList;

/// ArgStringList - Type used for constructing argv lists for subprocesses.
using ArgStringList = SmallVector<const char *, 16>;

/// Base flags for all options. Custom flags may be added after.
enum DriverFlag {
  HelpHidden       = (1 << 0),
  RenderAsInput    = (1 << 1),
  RenderJoined     = (1 << 2),
  RenderSeparate   = (1 << 3)
};

/// Option - Abstract representation for a single form of driver
/// argument.
///
/// An Option class represents a form of option that the driver
/// takes, for example how many arguments the option has and how
/// they can be provided. Individual option instances store
/// additional information about what group the option is a member
/// of (if any), if the option is an alias, and a number of
/// flags. At runtime the driver parses the command line into
/// concrete Arg instances, each of which corresponds to a
/// particular Option instance.
class Option {
public:
  enum OptionClass {
    GroupClass = 0,
    InputClass,
    UnknownClass,
    FlagClass,
    JoinedClass,
    ValuesClass,
    SeparateClass,
    RemainingArgsClass,
    RemainingArgsJoinedClass,
    CommaJoinedClass,
    MultiArgClass,
    JoinedOrSeparateClass,
    JoinedAndSeparateClass
  };

  enum RenderStyleKind {
    RenderCommaJoinedStyle,
    RenderJoinedStyle,
    RenderSeparateStyle,
    RenderValuesStyle
  };

protected:
  const OptTable::Info *Info;
  const OptTable *Owner;

public:
  Option(const OptTable::Info *Info, const OptTable *Owner);

  bool isValid() const {
    return Info != nullptr;
  }

  unsigned getID() const {
    assert(Info && "Must have a valid info!");
    return Info->ID;
  }

  OptionClass getKind() const {
    assert(Info && "Must have a valid info!");
    return OptionClass(Info->Kind);
  }

  /// Get the name of this option without any prefix.
  StringRef getName() const {
    assert(Info && "Must have a valid info!");
    return Info->Name;
  }

  const Option getGroup() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOption(Info->GroupID);
  }

  const Option getAlias() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOption(Info->AliasID);
  }

  /// Get the alias arguments as a \0 separated list.
  /// E.g. ["foo", "bar"] would be returned as "foo\0bar\0".
  const char *getAliasArgs() const {
    assert(Info && "Must have a valid info!");
    assert((!Info->AliasArgs || Info->AliasArgs[0] != 0) &&
           "AliasArgs should be either 0 or non-empty.");

    return Info->AliasArgs;
  }

  /// Get the default prefix for this option.
  StringRef getPrefix() const {
    const char *Prefix = *Info->Prefixes;
    return Prefix ? Prefix : StringRef();
  }

  /// Get the name of this option with the default prefix.
  std::string getPrefixedName() const {
    std::string Ret = getPrefix();
    Ret += getName();
    return Ret;
  }

  unsigned getNumArgs() const { return Info->Param; }

  bool hasNoOptAsInput() const { return Info->Flags & RenderAsInput;}

  RenderStyleKind getRenderStyle() const {
    if (Info->Flags & RenderJoined)
      return RenderJoinedStyle;
    if (Info->Flags & RenderSeparate)
      return RenderSeparateStyle;
    switch (getKind()) {
    case GroupClass:
    case InputClass:
    case UnknownClass:
      return RenderValuesStyle;
    case JoinedClass:
    case JoinedAndSeparateClass:
      return RenderJoinedStyle;
    case CommaJoinedClass:
      return RenderCommaJoinedStyle;
    case FlagClass:
    case ValuesClass:
    case SeparateClass:
    case MultiArgClass:
    case JoinedOrSeparateClass:
    case RemainingArgsClass:
    case RemainingArgsJoinedClass:
      return RenderSeparateStyle;
    }
    llvm_unreachable("Unexpected kind!");
  }

  /// Test if this option has the flag \a Val.
  bool hasFlag(unsigned Val) const {
    return Info->Flags & Val;
  }

  /// getUnaliasedOption - Return the final option this option
  /// aliases (itself, if the option has no alias).
  const Option getUnaliasedOption() const {
    const Option Alias = getAlias();
    if (Alias.isValid()) return Alias.getUnaliasedOption();
    return *this;
  }

  /// getRenderName - Return the name to use when rendering this
  /// option.
  StringRef getRenderName() const {
    return getUnaliasedOption().getName();
  }

  /// matches - Predicate for whether this option is part of the
  /// given option (which may be a group).
  ///
  /// Note that matches against options which are an alias should never be
  /// done -- aliases do not participate in matching and so such a query will
  /// always be false.
  bool matches(OptSpecifier ID) const;

  /// accept - Potentially accept the current argument, returning a
  /// new Arg instance, or 0 if the option does not accept this
  /// argument (or the argument is missing values).
  ///
  /// If the option accepts the current argument, accept() sets
  /// Index to the position where argument parsing should resume
  /// (even if the argument is missing values).
  ///
  /// \param ArgSize The number of bytes taken up by the matched Option prefix
  ///                and name. This is used to determine where joined values
  ///                start.
  Arg *accept(const ArgList &Args, unsigned &Index, unsigned ArgSize) const;

  void print(raw_ostream &O) const;
  void dump() const;
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_OPTION_H
