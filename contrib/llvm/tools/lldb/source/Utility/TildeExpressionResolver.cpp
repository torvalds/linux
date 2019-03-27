//===--------------------- TildeExpressionResolver.cpp ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/TildeExpressionResolver.h"

#include <assert.h>
#include <system_error>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#if !defined(_WIN32)
#include <pwd.h>
#endif

using namespace lldb_private;
using namespace llvm;

namespace fs = llvm::sys::fs;
namespace path = llvm::sys::path;

TildeExpressionResolver::~TildeExpressionResolver() {}

bool StandardTildeExpressionResolver::ResolveExact(
    StringRef Expr, SmallVectorImpl<char> &Output) {
  // We expect the tilde expression to be ONLY the expression itself, and
  // contain no separators.
  assert(!llvm::any_of(Expr, [](char c) { return path::is_separator(c); }));
  assert(Expr.empty() || Expr[0] == '~');

  return !fs::real_path(Expr, Output, true);
}

bool StandardTildeExpressionResolver::ResolvePartial(StringRef Expr,
                                                     StringSet<> &Output) {
  // We expect the tilde expression to be ONLY the expression itself, and
  // contain no separators.
  assert(!llvm::any_of(Expr, [](char c) { return path::is_separator(c); }));
  assert(Expr.empty() || Expr[0] == '~');

  Output.clear();
#if defined(_WIN32) || defined(__ANDROID__)
  return false;
#else
  if (Expr.empty())
    return false;

  SmallString<32> Buffer("~");
  setpwent();
  struct passwd *user_entry;
  Expr = Expr.drop_front();

  while ((user_entry = getpwent()) != nullptr) {
    StringRef ThisName(user_entry->pw_name);
    if (!ThisName.startswith(Expr))
      continue;

    Buffer.resize(1);
    Buffer.append(ThisName);
    Buffer.append(path::get_separator());
    Output.insert(Buffer);
  }

  return true;
#endif
}

bool TildeExpressionResolver::ResolveFullPath(
    StringRef Expr, llvm::SmallVectorImpl<char> &Output) {
  Output.clear();
  if (!Expr.startswith("~")) {
    Output.append(Expr.begin(), Expr.end());
    return false;
  }

  namespace path = llvm::sys::path;
  StringRef Left =
      Expr.take_until([](char c) { return path::is_separator(c); });

  if (!ResolveExact(Left, Output))
    return false;

  Output.append(Expr.begin() + Left.size(), Expr.end());
  return true;
}
