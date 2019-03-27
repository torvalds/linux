//===----- Attr.h - Enum values for C Attribute Kinds ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::attr::Kind enum.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ATTRKINDS_H
#define LLVM_CLANG_BASIC_ATTRKINDS_H

namespace clang {

namespace attr {

// A list of all the recognized kinds of attributes.
enum Kind {
#define ATTR(X) X,
#define ATTR_RANGE(CLASS, FIRST_NAME, LAST_NAME) \
  First##CLASS = FIRST_NAME,                    \
  Last##CLASS = LAST_NAME,
#include "clang/Basic/AttrList.inc"
};

} // end namespace attr
} // end namespace clang

#endif
