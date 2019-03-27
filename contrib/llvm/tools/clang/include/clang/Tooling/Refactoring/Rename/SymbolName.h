//===--- SymbolName.h - Clang refactoring library -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_RENAME_SYMBOL_NAME_H
#define LLVM_CLANG_TOOLING_REFACTOR_RENAME_SYMBOL_NAME_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace tooling {

/// A name of a symbol.
///
/// Symbol's name can be composed of multiple strings. For example, Objective-C
/// methods can contain multiple argument labels:
///
/// \code
/// - (void) myMethodNamePiece: (int)x anotherNamePieces:(int)y;
/// //       ^~ string 0 ~~~~~         ^~ string 1 ~~~~~
/// \endcode
class SymbolName {
public:
  explicit SymbolName(StringRef Name) {
    // While empty symbol names are valid (Objective-C selectors can have empty
    // name pieces), occurrences Objective-C selectors are created using an
    // array of strings instead of just one string.
    assert(!Name.empty() && "Invalid symbol name!");
    this->Name.push_back(Name.str());
  }

  ArrayRef<std::string> getNamePieces() const { return Name; }

private:
  llvm::SmallVector<std::string, 1> Name;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_RENAME_SYMBOL_NAME_H
