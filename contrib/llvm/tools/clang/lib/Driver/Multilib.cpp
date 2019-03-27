//===- Multilib.cpp - Multilib Implementation -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Multilib.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <string>

using namespace clang;
using namespace driver;
using namespace llvm::sys;

/// normalize Segment to "/foo/bar" or "".
static void normalizePathSegment(std::string &Segment) {
  StringRef seg = Segment;

  // Prune trailing "/" or "./"
  while (true) {
    StringRef last = path::filename(seg);
    if (last != ".")
      break;
    seg = path::parent_path(seg);
  }

  if (seg.empty() || seg == "/") {
    Segment.clear();
    return;
  }

  // Add leading '/'
  if (seg.front() != '/') {
    Segment = "/" + seg.str();
  } else {
    Segment = seg;
  }
}

Multilib::Multilib(StringRef GCCSuffix, StringRef OSSuffix,
                   StringRef IncludeSuffix)
    : GCCSuffix(GCCSuffix), OSSuffix(OSSuffix), IncludeSuffix(IncludeSuffix) {
  normalizePathSegment(this->GCCSuffix);
  normalizePathSegment(this->OSSuffix);
  normalizePathSegment(this->IncludeSuffix);
}

Multilib &Multilib::gccSuffix(StringRef S) {
  GCCSuffix = S;
  normalizePathSegment(GCCSuffix);
  return *this;
}

Multilib &Multilib::osSuffix(StringRef S) {
  OSSuffix = S;
  normalizePathSegment(OSSuffix);
  return *this;
}

Multilib &Multilib::includeSuffix(StringRef S) {
  IncludeSuffix = S;
  normalizePathSegment(IncludeSuffix);
  return *this;
}

LLVM_DUMP_METHOD void Multilib::dump() const {
  print(llvm::errs());
}

void Multilib::print(raw_ostream &OS) const {
  assert(GCCSuffix.empty() || (StringRef(GCCSuffix).front() == '/'));
  if (GCCSuffix.empty())
    OS << ".";
  else {
    OS << StringRef(GCCSuffix).drop_front();
  }
  OS << ";";
  for (StringRef Flag : Flags) {
    if (Flag.front() == '+')
      OS << "@" << Flag.substr(1);
  }
}

bool Multilib::isValid() const {
  llvm::StringMap<int> FlagSet;
  for (unsigned I = 0, N = Flags.size(); I != N; ++I) {
    StringRef Flag(Flags[I]);
    llvm::StringMap<int>::iterator SI = FlagSet.find(Flag.substr(1));

    assert(StringRef(Flag).front() == '+' || StringRef(Flag).front() == '-');

    if (SI == FlagSet.end())
      FlagSet[Flag.substr(1)] = I;
    else if (Flags[I] != Flags[SI->getValue()])
      return false;
  }
  return true;
}

bool Multilib::operator==(const Multilib &Other) const {
  // Check whether the flags sets match
  // allowing for the match to be order invariant
  llvm::StringSet<> MyFlags;
  for (const auto &Flag : Flags)
    MyFlags.insert(Flag);

  for (const auto &Flag : Other.Flags)
    if (MyFlags.find(Flag) == MyFlags.end())
      return false;

  if (osSuffix() != Other.osSuffix())
    return false;

  if (gccSuffix() != Other.gccSuffix())
    return false;

  if (includeSuffix() != Other.includeSuffix())
    return false;

  return true;
}

raw_ostream &clang::driver::operator<<(raw_ostream &OS, const Multilib &M) {
  M.print(OS);
  return OS;
}

MultilibSet &MultilibSet::Maybe(const Multilib &M) {
  Multilib Opposite;
  // Negate any '+' flags
  for (StringRef Flag : M.flags()) {
    if (Flag.front() == '+')
      Opposite.flags().push_back(("-" + Flag.substr(1)).str());
  }
  return Either(M, Opposite);
}

MultilibSet &MultilibSet::Either(const Multilib &M1, const Multilib &M2) {
  return Either({M1, M2});
}

MultilibSet &MultilibSet::Either(const Multilib &M1, const Multilib &M2,
                                 const Multilib &M3) {
  return Either({M1, M2, M3});
}

MultilibSet &MultilibSet::Either(const Multilib &M1, const Multilib &M2,
                                 const Multilib &M3, const Multilib &M4) {
  return Either({M1, M2, M3, M4});
}

MultilibSet &MultilibSet::Either(const Multilib &M1, const Multilib &M2,
                                 const Multilib &M3, const Multilib &M4,
                                 const Multilib &M5) {
  return Either({M1, M2, M3, M4, M5});
}

static Multilib compose(const Multilib &Base, const Multilib &New) {
  SmallString<128> GCCSuffix;
  llvm::sys::path::append(GCCSuffix, "/", Base.gccSuffix(), New.gccSuffix());
  SmallString<128> OSSuffix;
  llvm::sys::path::append(OSSuffix, "/", Base.osSuffix(), New.osSuffix());
  SmallString<128> IncludeSuffix;
  llvm::sys::path::append(IncludeSuffix, "/", Base.includeSuffix(),
                          New.includeSuffix());

  Multilib Composed(GCCSuffix, OSSuffix, IncludeSuffix);

  Multilib::flags_list &Flags = Composed.flags();

  Flags.insert(Flags.end(), Base.flags().begin(), Base.flags().end());
  Flags.insert(Flags.end(), New.flags().begin(), New.flags().end());

  return Composed;
}

MultilibSet &MultilibSet::Either(ArrayRef<Multilib> MultilibSegments) {
  multilib_list Composed;

  if (Multilibs.empty())
    Multilibs.insert(Multilibs.end(), MultilibSegments.begin(),
                     MultilibSegments.end());
  else {
    for (const auto &New : MultilibSegments) {
      for (const auto &Base : *this) {
        Multilib MO = compose(Base, New);
        if (MO.isValid())
          Composed.push_back(MO);
      }
    }

    Multilibs = Composed;
  }

  return *this;
}

MultilibSet &MultilibSet::FilterOut(FilterCallback F) {
  filterInPlace(F, Multilibs);
  return *this;
}

MultilibSet &MultilibSet::FilterOut(const char *Regex) {
  llvm::Regex R(Regex);
#ifndef NDEBUG
  std::string Error;
  if (!R.isValid(Error)) {
    llvm::errs() << Error;
    llvm_unreachable("Invalid regex!");
  }
#endif

  filterInPlace([&R](const Multilib &M) { return R.match(M.gccSuffix()); },
                Multilibs);
  return *this;
}

void MultilibSet::push_back(const Multilib &M) { Multilibs.push_back(M); }

void MultilibSet::combineWith(const MultilibSet &Other) {
  Multilibs.insert(Multilibs.end(), Other.begin(), Other.end());
}

static bool isFlagEnabled(StringRef Flag) {
  char Indicator = Flag.front();
  assert(Indicator == '+' || Indicator == '-');
  return Indicator == '+';
}

bool MultilibSet::select(const Multilib::flags_list &Flags, Multilib &M) const {
  llvm::StringMap<bool> FlagSet;

  // Stuff all of the flags into the FlagSet such that a true mappend indicates
  // the flag was enabled, and a false mappend indicates the flag was disabled.
  for (StringRef Flag : Flags)
    FlagSet[Flag.substr(1)] = isFlagEnabled(Flag);

  multilib_list Filtered = filterCopy([&FlagSet](const Multilib &M) {
    for (StringRef Flag : M.flags()) {
      llvm::StringMap<bool>::const_iterator SI = FlagSet.find(Flag.substr(1));
      if (SI != FlagSet.end())
        if (SI->getValue() != isFlagEnabled(Flag))
          return true;
    }
    return false;
  }, Multilibs);

  if (Filtered.empty())
    return false;
  if (Filtered.size() == 1) {
    M = Filtered[0];
    return true;
  }

  // TODO: pick the "best" multlib when more than one is suitable
  assert(false);
  return false;
}

LLVM_DUMP_METHOD void MultilibSet::dump() const {
  print(llvm::errs());
}

void MultilibSet::print(raw_ostream &OS) const {
  for (const auto &M : *this)
    OS << M << "\n";
}

MultilibSet::multilib_list MultilibSet::filterCopy(FilterCallback F,
                                                   const multilib_list &Ms) {
  multilib_list Copy(Ms);
  filterInPlace(F, Copy);
  return Copy;
}

void MultilibSet::filterInPlace(FilterCallback F, multilib_list &Ms) {
  Ms.erase(std::remove_if(Ms.begin(), Ms.end(), F), Ms.end());
}

raw_ostream &clang::driver::operator<<(raw_ostream &OS, const MultilibSet &MS) {
  MS.print(OS);
  return OS;
}
