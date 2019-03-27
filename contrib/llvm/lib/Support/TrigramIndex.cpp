//===-- TrigramIndex.cpp - a heuristic for SpecialCaseList ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// TrigramIndex implements a heuristic for SpecialCaseList that allows to
// filter out ~99% incoming queries when all regular expressions in the
// SpecialCaseList are simple wildcards with '*' and '.'. If rules are more
// complicated, the check is defeated and it will always pass the queries to a
// full regex.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/TrigramIndex.h"
#include "llvm/ADT/SmallVector.h"

#include <set>
#include <string>
#include <unordered_map>

using namespace llvm;

static const char RegexAdvancedMetachars[] = "()^$|+?[]\\{}";

static bool isAdvancedMetachar(unsigned Char) {
  return strchr(RegexAdvancedMetachars, Char) != nullptr;
}

void TrigramIndex::insert(std::string Regex) {
  if (Defeated) return;
  std::set<unsigned> Was;
  unsigned Cnt = 0;
  unsigned Tri = 0;
  unsigned Len = 0;
  bool Escaped = false;
  for (unsigned Char : Regex) {
    if (!Escaped) {
      // Regular expressions allow escaping symbols by preceding it with '\'.
      if (Char == '\\') {
        Escaped = true;
        continue;
      }
      if (isAdvancedMetachar(Char)) {
        // This is a more complicated regex than we can handle here.
        Defeated = true;
        return;
      }
      if (Char == '.' || Char == '*') {
        Tri = 0;
        Len = 0;
        continue;
      }
    }
    if (Escaped && Char >= '1' && Char <= '9') {
      Defeated = true;
      return;
    }
    // We have already handled escaping and can reset the flag.
    Escaped = false;
    Tri = ((Tri << 8) + Char) & 0xFFFFFF;
    Len++;
    if (Len < 3)
      continue;
    // We don't want the index to grow too much for the popular trigrams,
    // as they are weak signals. It's ok to still require them for the
    // rules we have already processed. It's just a small additional
    // computational cost.
    if (Index[Tri].size() >= 4)
      continue;
    Cnt++;
    if (!Was.count(Tri)) {
      // Adding the current rule to the index.
      Index[Tri].push_back(Counts.size());
      Was.insert(Tri);
    }
  }
  if (!Cnt) {
    // This rule does not have remarkable trigrams to rely on.
    // We have to always call the full regex chain.
    Defeated = true;
    return;
  }
  Counts.push_back(Cnt);
}

bool TrigramIndex::isDefinitelyOut(StringRef Query) const {
  if (Defeated)
    return false;
  std::vector<unsigned> CurCounts(Counts.size());
  unsigned Tri = 0;
  for (size_t I = 0; I < Query.size(); I++) {
    Tri = ((Tri << 8) + Query[I]) & 0xFFFFFF;
    if (I < 2)
      continue;
    const auto &II = Index.find(Tri);
    if (II == Index.end())
      continue;
    for (size_t J : II->second) {
      CurCounts[J]++;
      // If we have reached a desired limit, we have to look at the query
      // more closely by running a full regex.
      if (CurCounts[J] >= Counts[J])
        return false;
    }
  }
  return true;
}
