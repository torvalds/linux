//===-- ClangHost.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGHOST_H
#define LLDB_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGHOST_H

namespace lldb_private {

class FileSpec;

#if defined(__APPLE__)
bool ComputeClangDirectory(FileSpec &lldb_shlib_spec, FileSpec &file_spec,
                           bool verify);
#endif

FileSpec GetClangResourceDir();

} // namespace lldb_private

#endif
