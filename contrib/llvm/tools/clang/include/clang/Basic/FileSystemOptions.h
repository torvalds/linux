//===--- FileSystemOptions.h - File System Options --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::FileSystemOptions interface.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_FILESYSTEMOPTIONS_H
#define LLVM_CLANG_BASIC_FILESYSTEMOPTIONS_H

#include <string>

namespace clang {

/// Keeps track of options that affect how file operations are performed.
class FileSystemOptions {
public:
  /// If set, paths are resolved as if the working directory was
  /// set to the value of WorkingDir.
  std::string WorkingDir;
};

} // end namespace clang

#endif
