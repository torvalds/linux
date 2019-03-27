//===- Strings.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_STRINGS_H
#define LLD_STRINGS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/GlobPattern.h"
#include <string>
#include <vector>

namespace lld {
// Returns a demangled C++ symbol name. If Name is not a mangled
// name, it returns Optional::None.
llvm::Optional<std::string> demangleItanium(llvm::StringRef Name);
llvm::Optional<std::string> demangleMSVC(llvm::StringRef S);

std::vector<uint8_t> parseHex(llvm::StringRef S);
bool isValidCIdentifier(llvm::StringRef S);

// Write the contents of the a buffer to a file
void saveBuffer(llvm::StringRef Buffer, const llvm::Twine &Path);

// This class represents multiple glob patterns.
class StringMatcher {
public:
  StringMatcher() = default;
  explicit StringMatcher(llvm::ArrayRef<llvm::StringRef> Pat);

  bool match(llvm::StringRef S) const;

private:
  std::vector<llvm::GlobPattern> Patterns;
};

} // namespace lld

#endif
