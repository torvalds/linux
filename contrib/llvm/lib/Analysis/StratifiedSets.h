//===- StratifiedSets.h - Abstract stratified sets implementation. --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRATIFIEDSETS_H
#define LLVM_ADT_STRATIFIEDSETS_H

#include "AliasAnalysisSummary.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include <bitset>
#include <cassert>
#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {
namespace cflaa {
/// An index into Stratified Sets.
typedef unsigned StratifiedIndex;
/// NOTE: ^ This can't be a short -- bootstrapping clang has a case where
/// ~1M sets exist.

// Container of information related to a value in a StratifiedSet.
struct StratifiedInfo {
  StratifiedIndex Index;
  /// For field sensitivity, etc. we can tack fields on here.
};

/// A "link" between two StratifiedSets.
struct StratifiedLink {
  /// This is a value used to signify "does not exist" where the
  /// StratifiedIndex type is used.
  ///
  /// This is used instead of Optional<StratifiedIndex> because
  /// Optional<StratifiedIndex> would eat up a considerable amount of extra
  /// memory, after struct padding/alignment is taken into account.
  static const StratifiedIndex SetSentinel;

  /// The index for the set "above" current
  StratifiedIndex Above;

  /// The link for the set "below" current
  StratifiedIndex Below;

  /// Attributes for these StratifiedSets.
  AliasAttrs Attrs;

  StratifiedLink() : Above(SetSentinel), Below(SetSentinel) {}

  bool hasBelow() const { return Below != SetSentinel; }
  bool hasAbove() const { return Above != SetSentinel; }

  void clearBelow() { Below = SetSentinel; }
  void clearAbove() { Above = SetSentinel; }
};

/// These are stratified sets, as described in "Fast algorithms for
/// Dyck-CFL-reachability with applications to Alias Analysis" by Zhang Q, Lyu M
/// R, Yuan H, and Su Z. -- in short, this is meant to represent different sets
/// of Value*s. If two Value*s are in the same set, or if both sets have
/// overlapping attributes, then the Value*s are said to alias.
///
/// Sets may be related by position, meaning that one set may be considered as
/// above or below another. In CFL Alias Analysis, this gives us an indication
/// of how two variables are related; if the set of variable A is below a set
/// containing variable B, then at some point, a variable that has interacted
/// with B (or B itself) was either used in order to extract the variable A, or
/// was used as storage of variable A.
///
/// Sets may also have attributes (as noted above). These attributes are
/// generally used for noting whether a variable in the set has interacted with
/// a variable whose origins we don't quite know (i.e. globals/arguments), or if
/// the variable may have had operations performed on it (modified in a function
/// call). All attributes that exist in a set A must exist in all sets marked as
/// below set A.
template <typename T> class StratifiedSets {
public:
  StratifiedSets() = default;
  StratifiedSets(StratifiedSets &&) = default;
  StratifiedSets &operator=(StratifiedSets &&) = default;

  StratifiedSets(DenseMap<T, StratifiedInfo> Map,
                 std::vector<StratifiedLink> Links)
      : Values(std::move(Map)), Links(std::move(Links)) {}

  Optional<StratifiedInfo> find(const T &Elem) const {
    auto Iter = Values.find(Elem);
    if (Iter == Values.end())
      return None;
    return Iter->second;
  }

  const StratifiedLink &getLink(StratifiedIndex Index) const {
    assert(inbounds(Index));
    return Links[Index];
  }

private:
  DenseMap<T, StratifiedInfo> Values;
  std::vector<StratifiedLink> Links;

  bool inbounds(StratifiedIndex Idx) const { return Idx < Links.size(); }
};

/// Generic Builder class that produces StratifiedSets instances.
///
/// The goal of this builder is to efficiently produce correct StratifiedSets
/// instances. To this end, we use a few tricks:
///   > Set chains (A method for linking sets together)
///   > Set remaps (A method for marking a set as an alias [irony?] of another)
///
/// ==== Set chains ====
/// This builder has a notion of some value A being above, below, or with some
/// other value B:
///   > The `A above B` relationship implies that there is a reference edge
///   going from A to B. Namely, it notes that A can store anything in B's set.
///   > The `A below B` relationship is the opposite of `A above B`. It implies
///   that there's a dereference edge going from A to B.
///   > The `A with B` relationship states that there's an assignment edge going
///   from A to B, and that A and B should be treated as equals.
///
/// As an example, take the following code snippet:
///
/// %a = alloca i32, align 4
/// %ap = alloca i32*, align 8
/// %app = alloca i32**, align 8
/// store %a, %ap
/// store %ap, %app
/// %aw = getelementptr %ap, i32 0
///
/// Given this, the following relations exist:
///   - %a below %ap & %ap above %a
///   - %ap below %app & %app above %ap
///   - %aw with %ap & %ap with %aw
///
/// These relations produce the following sets:
///   [{%a}, {%ap, %aw}, {%app}]
///
/// ...Which state that the only MayAlias relationship in the above program is
/// between %ap and %aw.
///
/// Because LLVM allows arbitrary casts, code like the following needs to be
/// supported:
///   %ip = alloca i64, align 8
///   %ipp = alloca i64*, align 8
///   %i = bitcast i64** ipp to i64
///   store i64* %ip, i64** %ipp
///   store i64 %i, i64* %ip
///
/// Which, because %ipp ends up *both* above and below %ip, is fun.
///
/// This is solved by merging %i and %ipp into a single set (...which is the
/// only way to solve this, since their bit patterns are equivalent). Any sets
/// that ended up in between %i and %ipp at the time of merging (in this case,
/// the set containing %ip) also get conservatively merged into the set of %i
/// and %ipp. In short, the resulting StratifiedSet from the above code would be
/// {%ip, %ipp, %i}.
///
/// ==== Set remaps ====
/// More of an implementation detail than anything -- when merging sets, we need
/// to update the numbers of all of the elements mapped to those sets. Rather
/// than doing this at each merge, we note in the BuilderLink structure that a
/// remap has occurred, and use this information so we can defer renumbering set
/// elements until build time.
template <typename T> class StratifiedSetsBuilder {
  /// Represents a Stratified Set, with information about the Stratified
  /// Set above it, the set below it, and whether the current set has been
  /// remapped to another.
  struct BuilderLink {
    const StratifiedIndex Number;

    BuilderLink(StratifiedIndex N) : Number(N) {
      Remap = StratifiedLink::SetSentinel;
    }

    bool hasAbove() const {
      assert(!isRemapped());
      return Link.hasAbove();
    }

    bool hasBelow() const {
      assert(!isRemapped());
      return Link.hasBelow();
    }

    void setBelow(StratifiedIndex I) {
      assert(!isRemapped());
      Link.Below = I;
    }

    void setAbove(StratifiedIndex I) {
      assert(!isRemapped());
      Link.Above = I;
    }

    void clearBelow() {
      assert(!isRemapped());
      Link.clearBelow();
    }

    void clearAbove() {
      assert(!isRemapped());
      Link.clearAbove();
    }

    StratifiedIndex getBelow() const {
      assert(!isRemapped());
      assert(hasBelow());
      return Link.Below;
    }

    StratifiedIndex getAbove() const {
      assert(!isRemapped());
      assert(hasAbove());
      return Link.Above;
    }

    AliasAttrs getAttrs() {
      assert(!isRemapped());
      return Link.Attrs;
    }

    void setAttrs(AliasAttrs Other) {
      assert(!isRemapped());
      Link.Attrs |= Other;
    }

    bool isRemapped() const { return Remap != StratifiedLink::SetSentinel; }

    /// For initial remapping to another set
    void remapTo(StratifiedIndex Other) {
      assert(!isRemapped());
      Remap = Other;
    }

    StratifiedIndex getRemapIndex() const {
      assert(isRemapped());
      return Remap;
    }

    /// Should only be called when we're already remapped.
    void updateRemap(StratifiedIndex Other) {
      assert(isRemapped());
      Remap = Other;
    }

    /// Prefer the above functions to calling things directly on what's returned
    /// from this -- they guard against unexpected calls when the current
    /// BuilderLink is remapped.
    const StratifiedLink &getLink() const { return Link; }

  private:
    StratifiedLink Link;
    StratifiedIndex Remap;
  };

  /// This function performs all of the set unioning/value renumbering
  /// that we've been putting off, and generates a vector<StratifiedLink> that
  /// may be placed in a StratifiedSets instance.
  void finalizeSets(std::vector<StratifiedLink> &StratLinks) {
    DenseMap<StratifiedIndex, StratifiedIndex> Remaps;
    for (auto &Link : Links) {
      if (Link.isRemapped())
        continue;

      StratifiedIndex Number = StratLinks.size();
      Remaps.insert(std::make_pair(Link.Number, Number));
      StratLinks.push_back(Link.getLink());
    }

    for (auto &Link : StratLinks) {
      if (Link.hasAbove()) {
        auto &Above = linksAt(Link.Above);
        auto Iter = Remaps.find(Above.Number);
        assert(Iter != Remaps.end());
        Link.Above = Iter->second;
      }

      if (Link.hasBelow()) {
        auto &Below = linksAt(Link.Below);
        auto Iter = Remaps.find(Below.Number);
        assert(Iter != Remaps.end());
        Link.Below = Iter->second;
      }
    }

    for (auto &Pair : Values) {
      auto &Info = Pair.second;
      auto &Link = linksAt(Info.Index);
      auto Iter = Remaps.find(Link.Number);
      assert(Iter != Remaps.end());
      Info.Index = Iter->second;
    }
  }

  /// There's a guarantee in StratifiedLink where all bits set in a
  /// Link.externals will be set in all Link.externals "below" it.
  static void propagateAttrs(std::vector<StratifiedLink> &Links) {
    const auto getHighestParentAbove = [&Links](StratifiedIndex Idx) {
      const auto *Link = &Links[Idx];
      while (Link->hasAbove()) {
        Idx = Link->Above;
        Link = &Links[Idx];
      }
      return Idx;
    };

    SmallSet<StratifiedIndex, 16> Visited;
    for (unsigned I = 0, E = Links.size(); I < E; ++I) {
      auto CurrentIndex = getHighestParentAbove(I);
      if (!Visited.insert(CurrentIndex).second)
        continue;

      while (Links[CurrentIndex].hasBelow()) {
        auto &CurrentBits = Links[CurrentIndex].Attrs;
        auto NextIndex = Links[CurrentIndex].Below;
        auto &NextBits = Links[NextIndex].Attrs;
        NextBits |= CurrentBits;
        CurrentIndex = NextIndex;
      }
    }
  }

public:
  /// Builds a StratifiedSet from the information we've been given since either
  /// construction or the prior build() call.
  StratifiedSets<T> build() {
    std::vector<StratifiedLink> StratLinks;
    finalizeSets(StratLinks);
    propagateAttrs(StratLinks);
    Links.clear();
    return StratifiedSets<T>(std::move(Values), std::move(StratLinks));
  }

  bool has(const T &Elem) const { return get(Elem).hasValue(); }

  bool add(const T &Main) {
    if (get(Main).hasValue())
      return false;

    auto NewIndex = getNewUnlinkedIndex();
    return addAtMerging(Main, NewIndex);
  }

  /// Restructures the stratified sets as necessary to make "ToAdd" in a
  /// set above "Main". There are some cases where this is not possible (see
  /// above), so we merge them such that ToAdd and Main are in the same set.
  bool addAbove(const T &Main, const T &ToAdd) {
    assert(has(Main));
    auto Index = *indexOf(Main);
    if (!linksAt(Index).hasAbove())
      addLinkAbove(Index);

    auto Above = linksAt(Index).getAbove();
    return addAtMerging(ToAdd, Above);
  }

  /// Restructures the stratified sets as necessary to make "ToAdd" in a
  /// set below "Main". There are some cases where this is not possible (see
  /// above), so we merge them such that ToAdd and Main are in the same set.
  bool addBelow(const T &Main, const T &ToAdd) {
    assert(has(Main));
    auto Index = *indexOf(Main);
    if (!linksAt(Index).hasBelow())
      addLinkBelow(Index);

    auto Below = linksAt(Index).getBelow();
    return addAtMerging(ToAdd, Below);
  }

  bool addWith(const T &Main, const T &ToAdd) {
    assert(has(Main));
    auto MainIndex = *indexOf(Main);
    return addAtMerging(ToAdd, MainIndex);
  }

  void noteAttributes(const T &Main, AliasAttrs NewAttrs) {
    assert(has(Main));
    auto *Info = *get(Main);
    auto &Link = linksAt(Info->Index);
    Link.setAttrs(NewAttrs);
  }

private:
  DenseMap<T, StratifiedInfo> Values;
  std::vector<BuilderLink> Links;

  /// Adds the given element at the given index, merging sets if necessary.
  bool addAtMerging(const T &ToAdd, StratifiedIndex Index) {
    StratifiedInfo Info = {Index};
    auto Pair = Values.insert(std::make_pair(ToAdd, Info));
    if (Pair.second)
      return true;

    auto &Iter = Pair.first;
    auto &IterSet = linksAt(Iter->second.Index);
    auto &ReqSet = linksAt(Index);

    // Failed to add where we wanted to. Merge the sets.
    if (&IterSet != &ReqSet)
      merge(IterSet.Number, ReqSet.Number);

    return false;
  }

  /// Gets the BuilderLink at the given index, taking set remapping into
  /// account.
  BuilderLink &linksAt(StratifiedIndex Index) {
    auto *Start = &Links[Index];
    if (!Start->isRemapped())
      return *Start;

    auto *Current = Start;
    while (Current->isRemapped())
      Current = &Links[Current->getRemapIndex()];

    auto NewRemap = Current->Number;

    // Run through everything that has yet to be updated, and update them to
    // remap to NewRemap
    Current = Start;
    while (Current->isRemapped()) {
      auto *Next = &Links[Current->getRemapIndex()];
      Current->updateRemap(NewRemap);
      Current = Next;
    }

    return *Current;
  }

  /// Merges two sets into one another. Assumes that these sets are not
  /// already one in the same.
  void merge(StratifiedIndex Idx1, StratifiedIndex Idx2) {
    assert(inbounds(Idx1) && inbounds(Idx2));
    assert(&linksAt(Idx1) != &linksAt(Idx2) &&
           "Merging a set into itself is not allowed");

    // CASE 1: If the set at `Idx1` is above or below `Idx2`, we need to merge
    // both the
    // given sets, and all sets between them, into one.
    if (tryMergeUpwards(Idx1, Idx2))
      return;

    if (tryMergeUpwards(Idx2, Idx1))
      return;

    // CASE 2: The set at `Idx1` is not in the same chain as the set at `Idx2`.
    // We therefore need to merge the two chains together.
    mergeDirect(Idx1, Idx2);
  }

  /// Merges two sets assuming that the set at `Idx1` is unreachable from
  /// traversing above or below the set at `Idx2`.
  void mergeDirect(StratifiedIndex Idx1, StratifiedIndex Idx2) {
    assert(inbounds(Idx1) && inbounds(Idx2));

    auto *LinksInto = &linksAt(Idx1);
    auto *LinksFrom = &linksAt(Idx2);
    // Merging everything above LinksInto then proceeding to merge everything
    // below LinksInto becomes problematic, so we go as far "up" as possible!
    while (LinksInto->hasAbove() && LinksFrom->hasAbove()) {
      LinksInto = &linksAt(LinksInto->getAbove());
      LinksFrom = &linksAt(LinksFrom->getAbove());
    }

    if (LinksFrom->hasAbove()) {
      LinksInto->setAbove(LinksFrom->getAbove());
      auto &NewAbove = linksAt(LinksInto->getAbove());
      NewAbove.setBelow(LinksInto->Number);
    }

    // Merging strategy:
    //  > If neither has links below, stop.
    //  > If only `LinksInto` has links below, stop.
    //  > If only `LinksFrom` has links below, reset `LinksInto.Below` to
    //  match `LinksFrom.Below`
    //  > If both have links above, deal with those next.
    while (LinksInto->hasBelow() && LinksFrom->hasBelow()) {
      auto FromAttrs = LinksFrom->getAttrs();
      LinksInto->setAttrs(FromAttrs);

      // Remap needs to happen after getBelow(), but before
      // assignment of LinksFrom
      auto *NewLinksFrom = &linksAt(LinksFrom->getBelow());
      LinksFrom->remapTo(LinksInto->Number);
      LinksFrom = NewLinksFrom;
      LinksInto = &linksAt(LinksInto->getBelow());
    }

    if (LinksFrom->hasBelow()) {
      LinksInto->setBelow(LinksFrom->getBelow());
      auto &NewBelow = linksAt(LinksInto->getBelow());
      NewBelow.setAbove(LinksInto->Number);
    }

    LinksInto->setAttrs(LinksFrom->getAttrs());
    LinksFrom->remapTo(LinksInto->Number);
  }

  /// Checks to see if lowerIndex is at a level lower than upperIndex. If so, it
  /// will merge lowerIndex with upperIndex (and all of the sets between) and
  /// return true. Otherwise, it will return false.
  bool tryMergeUpwards(StratifiedIndex LowerIndex, StratifiedIndex UpperIndex) {
    assert(inbounds(LowerIndex) && inbounds(UpperIndex));
    auto *Lower = &linksAt(LowerIndex);
    auto *Upper = &linksAt(UpperIndex);
    if (Lower == Upper)
      return true;

    SmallVector<BuilderLink *, 8> Found;
    auto *Current = Lower;
    auto Attrs = Current->getAttrs();
    while (Current->hasAbove() && Current != Upper) {
      Found.push_back(Current);
      Attrs |= Current->getAttrs();
      Current = &linksAt(Current->getAbove());
    }

    if (Current != Upper)
      return false;

    Upper->setAttrs(Attrs);

    if (Lower->hasBelow()) {
      auto NewBelowIndex = Lower->getBelow();
      Upper->setBelow(NewBelowIndex);
      auto &NewBelow = linksAt(NewBelowIndex);
      NewBelow.setAbove(UpperIndex);
    } else {
      Upper->clearBelow();
    }

    for (const auto &Ptr : Found)
      Ptr->remapTo(Upper->Number);

    return true;
  }

  Optional<const StratifiedInfo *> get(const T &Val) const {
    auto Result = Values.find(Val);
    if (Result == Values.end())
      return None;
    return &Result->second;
  }

  Optional<StratifiedInfo *> get(const T &Val) {
    auto Result = Values.find(Val);
    if (Result == Values.end())
      return None;
    return &Result->second;
  }

  Optional<StratifiedIndex> indexOf(const T &Val) {
    auto MaybeVal = get(Val);
    if (!MaybeVal.hasValue())
      return None;
    auto *Info = *MaybeVal;
    auto &Link = linksAt(Info->Index);
    return Link.Number;
  }

  StratifiedIndex addLinkBelow(StratifiedIndex Set) {
    auto At = addLinks();
    Links[Set].setBelow(At);
    Links[At].setAbove(Set);
    return At;
  }

  StratifiedIndex addLinkAbove(StratifiedIndex Set) {
    auto At = addLinks();
    Links[At].setBelow(Set);
    Links[Set].setAbove(At);
    return At;
  }

  StratifiedIndex getNewUnlinkedIndex() { return addLinks(); }

  StratifiedIndex addLinks() {
    auto Link = Links.size();
    Links.push_back(BuilderLink(Link));
    return Link;
  }

  bool inbounds(StratifiedIndex N) const { return N < Links.size(); }
};
}
}
#endif // LLVM_ADT_STRATIFIEDSETS_H
