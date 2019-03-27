//===-- StringSaver.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/StringSaver.h"

using namespace llvm;

StringRef StringSaver::save(StringRef S) {
  char *P = Alloc.Allocate<char>(S.size() + 1);
  if (!S.empty())
    memcpy(P, S.data(), S.size());
  P[S.size()] = '\0';
  return StringRef(P, S.size());
}

StringRef UniqueStringSaver::save(StringRef S) {
  auto R = Unique.insert(S);
  if (R.second)                 // cache miss, need to actually save the string
    *R.first = Strings.save(S); // safe replacement with equal value
  return *R.first;
}
