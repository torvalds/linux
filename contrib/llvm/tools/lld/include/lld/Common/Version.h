//===- lld/Common/Version.h - LLD Version Number ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines a version-related utility function.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_VERSION_H
#define LLD_VERSION_H

#include "lld/Common/Version.inc"
#include "llvm/ADT/StringRef.h"

namespace lld {
/// Retrieves a string representing the complete lld version.
std::string getLLDVersion();
}

#endif // LLD_VERSION_H
