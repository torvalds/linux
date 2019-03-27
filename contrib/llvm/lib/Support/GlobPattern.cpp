//===-- GlobPattern.cpp - Glob pattern matcher implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a glob pattern matcher.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/GlobPattern.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

static bool hasWildcard(StringRef S) {
  return S.find_first_of("?*[") != StringRef::npos;
}

// Expands character ranges and returns a bitmap.
// For example, "a-cf-hz" is expanded to "abcfghz".
static Expected<BitVector> expand(StringRef S, StringRef Original) {
  BitVector BV(256, false);

  // Expand X-Y.
  for (;;) {
    if (S.size() < 3)
      break;

    uint8_t Start = S[0];
    uint8_t End = S[2];

    // If it doesn't start with something like X-Y,
    // consume the first character and proceed.
    if (S[1] != '-') {
      BV[Start] = true;
      S = S.substr(1);
      continue;
    }

    // It must be in the form of X-Y.
    // Validate it and then interpret the range.
    if (Start > End)
      return make_error<StringError>("invalid glob pattern: " + Original,
                                     errc::invalid_argument);

    for (int C = Start; C <= End; ++C)
      BV[(uint8_t)C] = true;
    S = S.substr(3);
  }

  for (char C : S)
    BV[(uint8_t)C] = true;
  return BV;
}

// This is a scanner for the glob pattern.
// A glob pattern token is one of "*", "?", "[<chars>]", "[^<chars>]"
// (which is a negative form of "[<chars>]"), or a non-meta character.
// This function returns the first token in S.
static Expected<BitVector> scan(StringRef &S, StringRef Original) {
  switch (S[0]) {
  case '*':
    S = S.substr(1);
    // '*' is represented by an empty bitvector.
    // All other bitvectors are 256-bit long.
    return BitVector();
  case '?':
    S = S.substr(1);
    return BitVector(256, true);
  case '[': {
    size_t End = S.find(']', 1);
    if (End == StringRef::npos)
      return make_error<StringError>("invalid glob pattern: " + Original,
                                     errc::invalid_argument);

    StringRef Chars = S.substr(1, End - 1);
    S = S.substr(End + 1);
    if (Chars.startswith("^")) {
      Expected<BitVector> BV = expand(Chars.substr(1), Original);
      if (!BV)
        return BV.takeError();
      return BV->flip();
    }
    return expand(Chars, Original);
  }
  default:
    BitVector BV(256, false);
    BV[(uint8_t)S[0]] = true;
    S = S.substr(1);
    return BV;
  }
}

Expected<GlobPattern> GlobPattern::create(StringRef S) {
  GlobPattern Pat;

  // S doesn't contain any metacharacter,
  // so the regular string comparison should work.
  if (!hasWildcard(S)) {
    Pat.Exact = S;
    return Pat;
  }

  // S is something like "foo*". We can use startswith().
  if (S.endswith("*") && !hasWildcard(S.drop_back())) {
    Pat.Prefix = S.drop_back();
    return Pat;
  }

  // S is something like "*foo". We can use endswith().
  if (S.startswith("*") && !hasWildcard(S.drop_front())) {
    Pat.Suffix = S.drop_front();
    return Pat;
  }

  // Otherwise, we need to do real glob pattern matching.
  // Parse the pattern now.
  StringRef Original = S;
  while (!S.empty()) {
    Expected<BitVector> BV = scan(S, Original);
    if (!BV)
      return BV.takeError();
    Pat.Tokens.push_back(*BV);
  }
  return Pat;
}

bool GlobPattern::match(StringRef S) const {
  if (Exact)
    return S == *Exact;
  if (Prefix)
    return S.startswith(*Prefix);
  if (Suffix)
    return S.endswith(*Suffix);
  return matchOne(Tokens, S);
}

// Runs glob pattern Pats against string S.
bool GlobPattern::matchOne(ArrayRef<BitVector> Pats, StringRef S) const {
  for (;;) {
    if (Pats.empty())
      return S.empty();

    // If Pats[0] is '*', try to match Pats[1..] against all possible
    // tail strings of S to see at least one pattern succeeds.
    if (Pats[0].size() == 0) {
      Pats = Pats.slice(1);
      if (Pats.empty())
        // Fast path. If a pattern is '*', it matches anything.
        return true;
      for (size_t I = 0, E = S.size(); I < E; ++I)
        if (matchOne(Pats, S.substr(I)))
          return true;
      return false;
    }

    // If Pats[0] is not '*', it must consume one character.
    if (S.empty() || !Pats[0][(uint8_t)S[0]])
      return false;
    Pats = Pats.slice(1);
    S = S.substr(1);
  }
}
