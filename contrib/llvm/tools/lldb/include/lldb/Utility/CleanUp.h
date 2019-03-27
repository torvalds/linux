//===-- CleanUp.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CleanUp_h_
#define liblldb_CleanUp_h_

#include "lldb/lldb-public.h"
#include <functional>

namespace lldb_private {

/// Run a cleanup function on scope exit unless it's explicitly disabled.
class CleanUp {
  std::function<void()> Clean;

public:
  /// Register a cleanup function which applies \p Func to a list of arguments.
  /// Use caution with arguments which are references: they will be copied.
  template <typename F, typename... Args>
  CleanUp(F &&Func, Args &&... args)
      : Clean(std::bind(std::forward<F>(Func), std::forward<Args>(args)...)) {}

  ~CleanUp() {
    if (Clean)
      Clean();
  }

  /// Disable the cleanup.
  void disable() { Clean = nullptr; }

  // Prevent cleanups from being run more than once.
  DISALLOW_COPY_AND_ASSIGN(CleanUp);
};

} // namespace lldb_private

#endif // #ifndef liblldb_CleanUp_h_
