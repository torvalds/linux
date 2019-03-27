//===- ArgList.h - Argument List Management ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_ARGLIST_H
#define LLVM_OPTION_ARGLIST_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/Option.h"
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;

namespace opt {

/// arg_iterator - Iterates through arguments stored inside an ArgList.
template<typename BaseIter, unsigned NumOptSpecifiers = 0>
class arg_iterator {
  /// The current argument and the end of the sequence we're iterating.
  BaseIter Current, End;

  /// Optional filters on the arguments which will be match. To avoid a
  /// zero-sized array, we store one specifier even if we're asked for none.
  OptSpecifier Ids[NumOptSpecifiers ? NumOptSpecifiers : 1];

  void SkipToNextArg() {
    for (; Current != End; ++Current) {
      // Skip erased elements.
      if (!*Current)
        continue;

      // Done if there are no filters.
      if (!NumOptSpecifiers)
        return;

      // Otherwise require a match.
      const Option &O = (*Current)->getOption();
      for (auto Id : Ids) {
        if (!Id.isValid())
          break;
        if (O.matches(Id))
          return;
      }
    }
  }

  using Traits = std::iterator_traits<BaseIter>;

public:
  using value_type = typename Traits::value_type;
  using reference = typename Traits::reference;
  using pointer = typename Traits::pointer;
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;

  arg_iterator(
      BaseIter Current, BaseIter End,
      const OptSpecifier (&Ids)[NumOptSpecifiers ? NumOptSpecifiers : 1] = {})
      : Current(Current), End(End) {
    for (unsigned I = 0; I != NumOptSpecifiers; ++I)
      this->Ids[I] = Ids[I];
    SkipToNextArg();
  }

  reference operator*() const { return *Current; }
  pointer operator->() const { return Current; }

  arg_iterator &operator++() {
    ++Current;
    SkipToNextArg();
    return *this;
  }

  arg_iterator operator++(int) {
    arg_iterator tmp(*this);
    ++(*this);
    return tmp;
  }

  friend bool operator==(arg_iterator LHS, arg_iterator RHS) {
    return LHS.Current == RHS.Current;
  }
  friend bool operator!=(arg_iterator LHS, arg_iterator RHS) {
    return !(LHS == RHS);
  }
};

/// ArgList - Ordered collection of driver arguments.
///
/// The ArgList class manages a list of Arg instances as well as
/// auxiliary data and convenience methods to allow Tools to quickly
/// check for the presence of Arg instances for a particular Option
/// and to iterate over groups of arguments.
class ArgList {
public:
  using arglist_type = SmallVector<Arg *, 16>;
  using iterator = arg_iterator<arglist_type::iterator>;
  using const_iterator = arg_iterator<arglist_type::const_iterator>;
  using reverse_iterator = arg_iterator<arglist_type::reverse_iterator>;
  using const_reverse_iterator =
      arg_iterator<arglist_type::const_reverse_iterator>;

  template<unsigned N> using filtered_iterator =
      arg_iterator<arglist_type::const_iterator, N>;
  template<unsigned N> using filtered_reverse_iterator =
      arg_iterator<arglist_type::const_reverse_iterator, N>;

private:
  /// The internal list of arguments.
  arglist_type Args;

  using OptRange = std::pair<unsigned, unsigned>;
  static OptRange emptyRange() { return {-1u, 0u}; }

  /// The first and last index of each different OptSpecifier ID.
  DenseMap<unsigned, OptRange> OptRanges;

  /// Get the range of indexes in which options with the specified IDs might
  /// reside, or (0, 0) if there are no such options.
  OptRange getRange(std::initializer_list<OptSpecifier> Ids) const;

protected:
  // Make the default special members protected so they won't be used to slice
  // derived objects, but can still be used by derived objects to implement
  // their own special members.
  ArgList() = default;

  // Explicit move operations to ensure the container is cleared post-move
  // otherwise it could lead to a double-delete in the case of moving of an
  // InputArgList which deletes the contents of the container. If we could fix
  // up the ownership here (delegate storage/ownership to the derived class so
  // it can be a container of unique_ptr) this would be simpler.
  ArgList(ArgList &&RHS)
      : Args(std::move(RHS.Args)), OptRanges(std::move(RHS.OptRanges)) {
    RHS.Args.clear();
    RHS.OptRanges.clear();
  }

  ArgList &operator=(ArgList &&RHS) {
    Args = std::move(RHS.Args);
    RHS.Args.clear();
    OptRanges = std::move(RHS.OptRanges);
    RHS.OptRanges.clear();
    return *this;
  }

  // Protect the dtor to ensure this type is never destroyed polymorphically.
  ~ArgList() = default;

  // Implicitly convert a value to an OptSpecifier. Used to work around a bug
  // in MSVC's implementation of narrowing conversion checking.
  static OptSpecifier toOptSpecifier(OptSpecifier S) { return S; }

public:
  /// @name Arg Access
  /// @{

  /// append - Append \p A to the arg list.
  void append(Arg *A);

  const arglist_type &getArgs() const { return Args; }

  unsigned size() const { return Args.size(); }

  /// @}
  /// @name Arg Iteration
  /// @{

  iterator begin() { return {Args.begin(), Args.end()}; }
  iterator end() { return {Args.end(), Args.end()}; }

  reverse_iterator rbegin() { return {Args.rbegin(), Args.rend()}; }
  reverse_iterator rend() { return {Args.rend(), Args.rend()}; }

  const_iterator begin() const { return {Args.begin(), Args.end()}; }
  const_iterator end() const { return {Args.end(), Args.end()}; }

  const_reverse_iterator rbegin() const { return {Args.rbegin(), Args.rend()}; }
  const_reverse_iterator rend() const { return {Args.rend(), Args.rend()}; }

  template<typename ...OptSpecifiers>
  iterator_range<filtered_iterator<sizeof...(OptSpecifiers)>>
  filtered(OptSpecifiers ...Ids) const {
    OptRange Range = getRange({toOptSpecifier(Ids)...});
    auto B = Args.begin() + Range.first;
    auto E = Args.begin() + Range.second;
    using Iterator = filtered_iterator<sizeof...(OptSpecifiers)>;
    return make_range(Iterator(B, E, {toOptSpecifier(Ids)...}),
                      Iterator(E, E, {toOptSpecifier(Ids)...}));
  }

  template<typename ...OptSpecifiers>
  iterator_range<filtered_reverse_iterator<sizeof...(OptSpecifiers)>>
  filtered_reverse(OptSpecifiers ...Ids) const {
    OptRange Range = getRange({toOptSpecifier(Ids)...});
    auto B = Args.rend() - Range.second;
    auto E = Args.rend() - Range.first;
    using Iterator = filtered_reverse_iterator<sizeof...(OptSpecifiers)>;
    return make_range(Iterator(B, E, {toOptSpecifier(Ids)...}),
                      Iterator(E, E, {toOptSpecifier(Ids)...}));
  }

  /// @}
  /// @name Arg Removal
  /// @{

  /// eraseArg - Remove any option matching \p Id.
  void eraseArg(OptSpecifier Id);

  /// @}
  /// @name Arg Access
  /// @{

  /// hasArg - Does the arg list contain any option matching \p Id.
  ///
  /// \p Claim Whether the argument should be claimed, if it exists.
  template<typename ...OptSpecifiers>
  bool hasArgNoClaim(OptSpecifiers ...Ids) const {
    return getLastArgNoClaim(Ids...) != nullptr;
  }
  template<typename ...OptSpecifiers>
  bool hasArg(OptSpecifiers ...Ids) const {
    return getLastArg(Ids...) != nullptr;
  }

  /// Return the last argument matching \p Id, or null.
  template<typename ...OptSpecifiers>
  Arg *getLastArg(OptSpecifiers ...Ids) const {
    Arg *Res = nullptr;
    for (Arg *A : filtered(Ids...)) {
      Res = A;
      Res->claim();
    }
    return Res;
  }

  /// Return the last argument matching \p Id, or null. Do not "claim" the
  /// option (don't mark it as having been used).
  template<typename ...OptSpecifiers>
  Arg *getLastArgNoClaim(OptSpecifiers ...Ids) const {
    for (Arg *A : filtered_reverse(Ids...))
      return A;
    return nullptr;
  }

  /// getArgString - Return the input argument string at \p Index.
  virtual const char *getArgString(unsigned Index) const = 0;

  /// getNumInputArgStrings - Return the number of original argument strings,
  /// which are guaranteed to be the first strings in the argument string
  /// list.
  virtual unsigned getNumInputArgStrings() const = 0;

  /// @}
  /// @name Argument Lookup Utilities
  /// @{

  /// getLastArgValue - Return the value of the last argument, or a default.
  StringRef getLastArgValue(OptSpecifier Id, StringRef Default = "") const;

  /// getAllArgValues - Get the values of all instances of the given argument
  /// as strings.
  std::vector<std::string> getAllArgValues(OptSpecifier Id) const;

  /// @}
  /// @name Translation Utilities
  /// @{

  /// hasFlag - Given an option \p Pos and its negative form \p Neg, return
  /// true if the option is present, false if the negation is present, and
  /// \p Default if neither option is given. If both the option and its
  /// negation are present, the last one wins.
  bool hasFlag(OptSpecifier Pos, OptSpecifier Neg, bool Default=true) const;

  /// hasFlag - Given an option \p Pos, an alias \p PosAlias and its negative
  /// form \p Neg, return true if the option or its alias is present, false if
  /// the negation is present, and \p Default if none of the options are
  /// given. If multiple options are present, the last one wins.
  bool hasFlag(OptSpecifier Pos, OptSpecifier PosAlias, OptSpecifier Neg,
               bool Default = true) const;

  /// AddLastArg - Render only the last argument match \p Id0, if present.
  void AddLastArg(ArgStringList &Output, OptSpecifier Id0) const;
  void AddLastArg(ArgStringList &Output, OptSpecifier Id0,
                  OptSpecifier Id1) const;

  /// AddAllArgsExcept - Render all arguments matching any of the given ids
  /// and not matching any of the excluded ids.
  void AddAllArgsExcept(ArgStringList &Output, ArrayRef<OptSpecifier> Ids,
                        ArrayRef<OptSpecifier> ExcludeIds) const;
  /// AddAllArgs - Render all arguments matching any of the given ids.
  void AddAllArgs(ArgStringList &Output, ArrayRef<OptSpecifier> Ids) const;

  /// AddAllArgs - Render all arguments matching the given ids.
  void AddAllArgs(ArgStringList &Output, OptSpecifier Id0,
                  OptSpecifier Id1 = 0U, OptSpecifier Id2 = 0U) const;

  /// AddAllArgValues - Render the argument values of all arguments
  /// matching the given ids.
  void AddAllArgValues(ArgStringList &Output, OptSpecifier Id0,
                       OptSpecifier Id1 = 0U, OptSpecifier Id2 = 0U) const;

  /// AddAllArgsTranslated - Render all the arguments matching the
  /// given ids, but forced to separate args and using the provided
  /// name instead of the first option value.
  ///
  /// \param Joined - If true, render the argument as joined with
  /// the option specifier.
  void AddAllArgsTranslated(ArgStringList &Output, OptSpecifier Id0,
                            const char *Translation,
                            bool Joined = false) const;

  /// ClaimAllArgs - Claim all arguments which match the given
  /// option id.
  void ClaimAllArgs(OptSpecifier Id0) const;

  /// ClaimAllArgs - Claim all arguments.
  ///
  void ClaimAllArgs() const;

  /// @}
  /// @name Arg Synthesis
  /// @{

  /// Construct a constant string pointer whose
  /// lifetime will match that of the ArgList.
  virtual const char *MakeArgStringRef(StringRef Str) const = 0;
  const char *MakeArgString(const Twine &Str) const {
    SmallString<256> Buf;
    return MakeArgStringRef(Str.toStringRef(Buf));
  }

  /// Create an arg string for (\p LHS + \p RHS), reusing the
  /// string at \p Index if possible.
  const char *GetOrMakeJoinedArgString(unsigned Index, StringRef LHS,
                                        StringRef RHS) const;

  void print(raw_ostream &O) const;
  void dump() const;

  /// @}
};

class InputArgList final : public ArgList {
private:
  /// List of argument strings used by the contained Args.
  ///
  /// This is mutable since we treat the ArgList as being the list
  /// of Args, and allow routines to add new strings (to have a
  /// convenient place to store the memory) via MakeIndex.
  mutable ArgStringList ArgStrings;

  /// Strings for synthesized arguments.
  ///
  /// This is mutable since we treat the ArgList as being the list
  /// of Args, and allow routines to add new strings (to have a
  /// convenient place to store the memory) via MakeIndex.
  mutable std::list<std::string> SynthesizedStrings;

  /// The number of original input argument strings.
  unsigned NumInputArgStrings;

  /// Release allocated arguments.
  void releaseMemory();

public:
  InputArgList() : NumInputArgStrings(0) {}

  InputArgList(const char* const *ArgBegin, const char* const *ArgEnd);

  InputArgList(InputArgList &&RHS)
      : ArgList(std::move(RHS)), ArgStrings(std::move(RHS.ArgStrings)),
        SynthesizedStrings(std::move(RHS.SynthesizedStrings)),
        NumInputArgStrings(RHS.NumInputArgStrings) {}

  InputArgList &operator=(InputArgList &&RHS) {
    releaseMemory();
    ArgList::operator=(std::move(RHS));
    ArgStrings = std::move(RHS.ArgStrings);
    SynthesizedStrings = std::move(RHS.SynthesizedStrings);
    NumInputArgStrings = RHS.NumInputArgStrings;
    return *this;
  }

  ~InputArgList() { releaseMemory(); }

  const char *getArgString(unsigned Index) const override {
    return ArgStrings[Index];
  }

  unsigned getNumInputArgStrings() const override {
    return NumInputArgStrings;
  }

  /// @name Arg Synthesis
  /// @{

public:
  /// MakeIndex - Get an index for the given string(s).
  unsigned MakeIndex(StringRef String0) const;
  unsigned MakeIndex(StringRef String0, StringRef String1) const;

  using ArgList::MakeArgString;
  const char *MakeArgStringRef(StringRef Str) const override;

  /// @}
};

/// DerivedArgList - An ordered collection of driver arguments,
/// whose storage may be in another argument list.
class DerivedArgList final : public ArgList {
  const InputArgList &BaseArgs;

  /// The list of arguments we synthesized.
  mutable SmallVector<std::unique_ptr<Arg>, 16> SynthesizedArgs;

public:
  /// Construct a new derived arg list from \p BaseArgs.
  DerivedArgList(const InputArgList &BaseArgs);

  const char *getArgString(unsigned Index) const override {
    return BaseArgs.getArgString(Index);
  }

  unsigned getNumInputArgStrings() const override {
    return BaseArgs.getNumInputArgStrings();
  }

  const InputArgList &getBaseArgs() const {
    return BaseArgs;
  }

  /// @name Arg Synthesis
  /// @{

  /// AddSynthesizedArg - Add a argument to the list of synthesized arguments
  /// (to be freed).
  void AddSynthesizedArg(Arg *A);

  using ArgList::MakeArgString;
  const char *MakeArgStringRef(StringRef Str) const override;

  /// AddFlagArg - Construct a new FlagArg for the given option \p Id and
  /// append it to the argument list.
  void AddFlagArg(const Arg *BaseArg, const Option Opt) {
    append(MakeFlagArg(BaseArg, Opt));
  }

  /// AddPositionalArg - Construct a new Positional arg for the given option
  /// \p Id, with the provided \p Value and append it to the argument
  /// list.
  void AddPositionalArg(const Arg *BaseArg, const Option Opt,
                        StringRef Value) {
    append(MakePositionalArg(BaseArg, Opt, Value));
  }

  /// AddSeparateArg - Construct a new Positional arg for the given option
  /// \p Id, with the provided \p Value and append it to the argument
  /// list.
  void AddSeparateArg(const Arg *BaseArg, const Option Opt,
                      StringRef Value) {
    append(MakeSeparateArg(BaseArg, Opt, Value));
  }

  /// AddJoinedArg - Construct a new Positional arg for the given option
  /// \p Id, with the provided \p Value and append it to the argument list.
  void AddJoinedArg(const Arg *BaseArg, const Option Opt,
                    StringRef Value) {
    append(MakeJoinedArg(BaseArg, Opt, Value));
  }

  /// MakeFlagArg - Construct a new FlagArg for the given option \p Id.
  Arg *MakeFlagArg(const Arg *BaseArg, const Option Opt) const;

  /// MakePositionalArg - Construct a new Positional arg for the
  /// given option \p Id, with the provided \p Value.
  Arg *MakePositionalArg(const Arg *BaseArg, const Option Opt,
                          StringRef Value) const;

  /// MakeSeparateArg - Construct a new Positional arg for the
  /// given option \p Id, with the provided \p Value.
  Arg *MakeSeparateArg(const Arg *BaseArg, const Option Opt,
                        StringRef Value) const;

  /// MakeJoinedArg - Construct a new Positional arg for the
  /// given option \p Id, with the provided \p Value.
  Arg *MakeJoinedArg(const Arg *BaseArg, const Option Opt,
                      StringRef Value) const;

  /// @}
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_ARGLIST_H
