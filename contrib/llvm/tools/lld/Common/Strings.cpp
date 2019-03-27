//===- Strings.cpp -------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Strings.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/GlobPattern.h"
#include <algorithm>
#include <mutex>
#include <vector>

using namespace llvm;
using namespace lld;

// Returns the demangled C++ symbol name for Name.
Optional<std::string> lld::demangleItanium(StringRef Name) {
  // itaniumDemangle can be used to demangle strings other than symbol
  // names which do not necessarily start with "_Z". Name can be
  // either a C or C++ symbol. Don't call itaniumDemangle if the name
  // does not look like a C++ symbol name to avoid getting unexpected
  // result for a C symbol that happens to match a mangled type name.
  if (!Name.startswith("_Z"))
    return None;

  char *Buf = itaniumDemangle(Name.str().c_str(), nullptr, nullptr, nullptr);
  if (!Buf)
    return None;
  std::string S(Buf);
  free(Buf);
  return S;
}

Optional<std::string> lld::demangleMSVC(StringRef Name) {
  std::string Prefix;
  if (Name.consume_front("__imp_"))
    Prefix = "__declspec(dllimport) ";

  // Demangle only C++ names.
  if (!Name.startswith("?"))
    return None;

  char *Buf = microsoftDemangle(Name.str().c_str(), nullptr, nullptr, nullptr);
  if (!Buf)
    return None;
  std::string S(Buf);
  free(Buf);
  return Prefix + S;
}

StringMatcher::StringMatcher(ArrayRef<StringRef> Pat) {
  for (StringRef S : Pat) {
    Expected<GlobPattern> Pat = GlobPattern::create(S);
    if (!Pat)
      error(toString(Pat.takeError()));
    else
      Patterns.push_back(*Pat);
  }
}

bool StringMatcher::match(StringRef S) const {
  for (const GlobPattern &Pat : Patterns)
    if (Pat.match(S))
      return true;
  return false;
}

// Converts a hex string (e.g. "deadbeef") to a vector.
std::vector<uint8_t> lld::parseHex(StringRef S) {
  std::vector<uint8_t> Hex;
  while (!S.empty()) {
    StringRef B = S.substr(0, 2);
    S = S.substr(2);
    uint8_t H;
    if (!to_integer(B, H, 16)) {
      error("not a hexadecimal value: " + B);
      return {};
    }
    Hex.push_back(H);
  }
  return Hex;
}

// Returns true if S is valid as a C language identifier.
bool lld::isValidCIdentifier(StringRef S) {
  return !S.empty() && (isAlpha(S[0]) || S[0] == '_') &&
         std::all_of(S.begin() + 1, S.end(),
                     [](char C) { return C == '_' || isAlnum(C); });
}

// Write the contents of the a buffer to a file
void lld::saveBuffer(StringRef Buffer, const Twine &Path) {
  std::error_code EC;
  raw_fd_ostream OS(Path.str(), EC, sys::fs::OpenFlags::F_None);
  if (EC)
    error("cannot create " + Path + ": " + EC.message());
  OS << Buffer;
}
