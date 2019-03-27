//===-- NameMatches.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/RegularExpression.h"

#include "llvm/ADT/StringRef.h"

using namespace lldb_private;

bool lldb_private::NameMatches(llvm::StringRef name, NameMatch match_type,
                               llvm::StringRef match) {
  switch (match_type) {
  case NameMatch::Ignore:
    return true;
  case NameMatch::Equals:
    return name == match;
  case NameMatch::Contains:
    return name.contains(match);
  case NameMatch::StartsWith:
    return name.startswith(match);
  case NameMatch::EndsWith:
    return name.endswith(match);
  case NameMatch::RegularExpression: {
    RegularExpression regex(match);
    return regex.Execute(name);
  }
  }
  return false;
}
