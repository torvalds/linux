//===-- sanitizer_libignore.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// LibIgnore allows to ignore all interceptors called from a particular set
// of dynamic libraries. LibIgnore can be initialized with several templates
// of names of libraries to be ignored. It finds code ranges for the libraries;
// and checks whether the provided PC value belongs to the code ranges.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_LIBIGNORE_H
#define SANITIZER_LIBIGNORE_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_common.h"
#include "sanitizer_atomic.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

class LibIgnore {
 public:
  explicit LibIgnore(LinkerInitialized);

  // Must be called during initialization.
  void AddIgnoredLibrary(const char *name_templ);
  void IgnoreNoninstrumentedModules(bool enable) {
    track_instrumented_libs_ = enable;
  }

  // Must be called after a new dynamic library is loaded.
  void OnLibraryLoaded(const char *name);

  // Must be called after a dynamic library is unloaded.
  void OnLibraryUnloaded();

  // Checks whether the provided PC belongs to one of the ignored libraries or
  // the PC should be ignored because it belongs to an non-instrumented module
  // (when ignore_noninstrumented_modules=1). Also returns true via
  // "pc_in_ignored_lib" if the PC is in an ignored library, false otherwise.
  bool IsIgnored(uptr pc, bool *pc_in_ignored_lib) const;

  // Checks whether the provided PC belongs to an instrumented module.
  bool IsPcInstrumented(uptr pc) const;

 private:
  struct Lib {
    char *templ;
    char *name;
    char *real_name;  // target of symlink
    bool loaded;
  };

  struct LibCodeRange {
    uptr begin;
    uptr end;
  };

  inline bool IsInRange(uptr pc, const LibCodeRange &range) const {
    return (pc >= range.begin && pc < range.end);
  }

  static const uptr kMaxIgnoredRanges = 128;
  static const uptr kMaxInstrumentedRanges = 1024;
  static const uptr kMaxLibs = 1024;

  // Hot part:
  atomic_uintptr_t ignored_ranges_count_;
  LibCodeRange ignored_code_ranges_[kMaxIgnoredRanges];

  atomic_uintptr_t instrumented_ranges_count_;
  LibCodeRange instrumented_code_ranges_[kMaxInstrumentedRanges];

  // Cold part:
  BlockingMutex mutex_;
  uptr count_;
  Lib libs_[kMaxLibs];
  bool track_instrumented_libs_;

  // Disallow copying of LibIgnore objects.
  LibIgnore(const LibIgnore&);  // not implemented
  void operator = (const LibIgnore&);  // not implemented
};

inline bool LibIgnore::IsIgnored(uptr pc, bool *pc_in_ignored_lib) const {
  const uptr n = atomic_load(&ignored_ranges_count_, memory_order_acquire);
  for (uptr i = 0; i < n; i++) {
    if (IsInRange(pc, ignored_code_ranges_[i])) {
      *pc_in_ignored_lib = true;
      return true;
    }
  }
  *pc_in_ignored_lib = false;
  if (track_instrumented_libs_ && !IsPcInstrumented(pc))
    return true;
  return false;
}

inline bool LibIgnore::IsPcInstrumented(uptr pc) const {
  const uptr n = atomic_load(&instrumented_ranges_count_, memory_order_acquire);
  for (uptr i = 0; i < n; i++) {
    if (IsInRange(pc, instrumented_code_ranges_[i]))
      return true;
  }
  return false;
}

}  // namespace __sanitizer

#endif  // SANITIZER_LIBIGNORE_H
