//===--- Lookup.h - Framework for clang refactoring tools --*- C++ -*------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines helper methods for clang tools performing name lookup.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_CORE_LOOKUP_H
#define LLVM_CLANG_TOOLING_CORE_LOOKUP_H

#include "clang/Basic/LLVM.h"
#include <string>

namespace clang {

class DeclContext;
class NamedDecl;
class NestedNameSpecifier;

namespace tooling {

/// Emulate a lookup to replace one nested name specifier with another using as
/// few additional namespace qualifications as possible.
///
/// This does not perform a full C++ lookup so ADL will not work.
///
/// \param Use The nested name to be replaced.
/// \param UseContext The context in which the nested name is contained. This
///                   will be used to minimize namespace qualifications.
/// \param FromDecl The declaration to which the nested name points.
/// \param ReplacementString The replacement nested name. Must be fully
///                          qualified including a leading "::".
/// \returns The new name to be inserted in place of the current nested name.
std::string replaceNestedName(const NestedNameSpecifier *Use,
                              const DeclContext *UseContext,
                              const NamedDecl *FromDecl,
                              StringRef ReplacementString);

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_CORE_LOOKUP_H
