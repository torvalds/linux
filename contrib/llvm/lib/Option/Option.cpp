//===- Option.cpp - Abstract Driver Options -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstring>

using namespace llvm;
using namespace llvm::opt;

Option::Option(const OptTable::Info *info, const OptTable *owner)
  : Info(info), Owner(owner) {
  // Multi-level aliases are not supported. This just simplifies option
  // tracking, it is not an inherent limitation.
  assert((!Info || !getAlias().isValid() || !getAlias().getAlias().isValid()) &&
         "Multi-level aliases are not supported.");

  if (Info && getAliasArgs()) {
    assert(getAlias().isValid() && "Only alias options can have alias args.");
    assert(getKind() == FlagClass && "Only Flag aliases can have alias args.");
    assert(getAlias().getKind() != FlagClass &&
           "Cannot provide alias args to a flag option.");
  }
}

void Option::print(raw_ostream &O) const {
  O << "<";
  switch (getKind()) {
#define P(N) case N: O << #N; break
    P(GroupClass);
    P(InputClass);
    P(UnknownClass);
    P(FlagClass);
    P(JoinedClass);
    P(ValuesClass);
    P(SeparateClass);
    P(CommaJoinedClass);
    P(MultiArgClass);
    P(JoinedOrSeparateClass);
    P(JoinedAndSeparateClass);
    P(RemainingArgsClass);
    P(RemainingArgsJoinedClass);
#undef P
  }

  if (Info->Prefixes) {
    O << " Prefixes:[";
    for (const char *const *Pre = Info->Prefixes; *Pre != nullptr; ++Pre) {
      O << '"' << *Pre << (*(Pre + 1) == nullptr ? "\"" : "\", ");
    }
    O << ']';
  }

  O << " Name:\"" << getName() << '"';

  const Option Group = getGroup();
  if (Group.isValid()) {
    O << " Group:";
    Group.print(O);
  }

  const Option Alias = getAlias();
  if (Alias.isValid()) {
    O << " Alias:";
    Alias.print(O);
  }

  if (getKind() == MultiArgClass)
    O << " NumArgs:" << getNumArgs();

  O << ">\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Option::dump() const { print(dbgs()); }
#endif

bool Option::matches(OptSpecifier Opt) const {
  // Aliases are never considered in matching, look through them.
  const Option Alias = getAlias();
  if (Alias.isValid())
    return Alias.matches(Opt);

  // Check exact match.
  if (getID() == Opt.getID())
    return true;

  const Option Group = getGroup();
  if (Group.isValid())
    return Group.matches(Opt);
  return false;
}

Arg *Option::accept(const ArgList &Args,
                    unsigned &Index,
                    unsigned ArgSize) const {
  const Option &UnaliasedOption = getUnaliasedOption();
  StringRef Spelling;
  // If the option was an alias, get the spelling from the unaliased one.
  if (getID() == UnaliasedOption.getID()) {
    Spelling = StringRef(Args.getArgString(Index), ArgSize);
  } else {
    Spelling = Args.MakeArgString(Twine(UnaliasedOption.getPrefix()) +
                                  Twine(UnaliasedOption.getName()));
  }

  switch (getKind()) {
  case FlagClass: {
    if (ArgSize != strlen(Args.getArgString(Index)))
      return nullptr;

    Arg *A = new Arg(UnaliasedOption, Spelling, Index++);
    if (getAliasArgs()) {
      const char *Val = getAliasArgs();
      while (*Val != '\0') {
        A->getValues().push_back(Val);

        // Move past the '\0' to the next argument.
        Val += strlen(Val) + 1;
      }
    }

    if (UnaliasedOption.getKind() == JoinedClass && !getAliasArgs())
      // A Flag alias for a Joined option must provide an argument.
      A->getValues().push_back("");

    return A;
  }
  case JoinedClass: {
    const char *Value = Args.getArgString(Index) + ArgSize;
    return new Arg(UnaliasedOption, Spelling, Index++, Value);
  }
  case CommaJoinedClass: {
    // Always matches.
    const char *Str = Args.getArgString(Index) + ArgSize;
    Arg *A = new Arg(UnaliasedOption, Spelling, Index++);

    // Parse out the comma separated values.
    const char *Prev = Str;
    for (;; ++Str) {
      char c = *Str;

      if (!c || c == ',') {
        if (Prev != Str) {
          char *Value = new char[Str - Prev + 1];
          memcpy(Value, Prev, Str - Prev);
          Value[Str - Prev] = '\0';
          A->getValues().push_back(Value);
        }

        if (!c)
          break;

        Prev = Str + 1;
      }
    }
    A->setOwnsValues(true);

    return A;
  }
  case SeparateClass:
    // Matches iff this is an exact match.
    // FIXME: Avoid strlen.
    if (ArgSize != strlen(Args.getArgString(Index)))
      return nullptr;

    Index += 2;
    if (Index > Args.getNumInputArgStrings() ||
        Args.getArgString(Index - 1) == nullptr)
      return nullptr;

    return new Arg(UnaliasedOption, Spelling,
                   Index - 2, Args.getArgString(Index - 1));
  case MultiArgClass: {
    // Matches iff this is an exact match.
    // FIXME: Avoid strlen.
    if (ArgSize != strlen(Args.getArgString(Index)))
      return nullptr;

    Index += 1 + getNumArgs();
    if (Index > Args.getNumInputArgStrings())
      return nullptr;

    Arg *A = new Arg(UnaliasedOption, Spelling, Index - 1 - getNumArgs(),
                      Args.getArgString(Index - getNumArgs()));
    for (unsigned i = 1; i != getNumArgs(); ++i)
      A->getValues().push_back(Args.getArgString(Index - getNumArgs() + i));
    return A;
  }
  case JoinedOrSeparateClass: {
    // If this is not an exact match, it is a joined arg.
    // FIXME: Avoid strlen.
    if (ArgSize != strlen(Args.getArgString(Index))) {
      const char *Value = Args.getArgString(Index) + ArgSize;
      return new Arg(*this, Spelling, Index++, Value);
    }

    // Otherwise it must be separate.
    Index += 2;
    if (Index > Args.getNumInputArgStrings() ||
        Args.getArgString(Index - 1) == nullptr)
      return nullptr;

    return new Arg(UnaliasedOption, Spelling,
                   Index - 2, Args.getArgString(Index - 1));
  }
  case JoinedAndSeparateClass:
    // Always matches.
    Index += 2;
    if (Index > Args.getNumInputArgStrings() ||
        Args.getArgString(Index - 1) == nullptr)
      return nullptr;

    return new Arg(UnaliasedOption, Spelling, Index - 2,
                   Args.getArgString(Index - 2) + ArgSize,
                   Args.getArgString(Index - 1));
  case RemainingArgsClass: {
    // Matches iff this is an exact match.
    // FIXME: Avoid strlen.
    if (ArgSize != strlen(Args.getArgString(Index)))
      return nullptr;
    Arg *A = new Arg(UnaliasedOption, Spelling, Index++);
    while (Index < Args.getNumInputArgStrings() &&
           Args.getArgString(Index) != nullptr)
      A->getValues().push_back(Args.getArgString(Index++));
    return A;
  }
  case RemainingArgsJoinedClass: {
    Arg *A = new Arg(UnaliasedOption, Spelling, Index);
    if (ArgSize != strlen(Args.getArgString(Index))) {
      // An inexact match means there is a joined arg.
      A->getValues().push_back(Args.getArgString(Index) + ArgSize);
    }
    Index++;
    while (Index < Args.getNumInputArgStrings() &&
           Args.getArgString(Index) != nullptr)
      A->getValues().push_back(Args.getArgString(Index++));
    return A;
  }

  default:
    llvm_unreachable("Invalid option kind!");
  }
}
