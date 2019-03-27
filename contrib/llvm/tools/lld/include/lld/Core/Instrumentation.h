//===- include/Core/Instrumentation.h - Instrumentation API ---------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provide an Instrumentation API that optionally uses VTune interfaces.
///
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_INSTRUMENTATION_H
#define LLD_CORE_INSTRUMENTATION_H

#include "llvm/Support/Compiler.h"
#include <utility>

#ifdef LLD_HAS_VTUNE
# include <ittnotify.h>
#endif

namespace lld {
#ifdef LLD_HAS_VTUNE
/// A unique global scope for instrumentation data.
///
/// Domains last for the lifetime of the application and cannot be destroyed.
/// Multiple Domains created with the same name represent the same domain.
class Domain {
  __itt_domain *_domain;

public:
  explicit Domain(const char *name) : _domain(__itt_domain_createA(name)) {}

  operator __itt_domain *() const { return _domain; }
  __itt_domain *operator->() const { return _domain; }
};

/// A global reference to a string constant.
///
/// These are uniqued by the ITT runtime and cannot be deleted. They are not
/// specific to a domain.
///
/// Prefer reusing a single StringHandle over passing a ntbs when the same
/// string will be used often.
class StringHandle {
  __itt_string_handle *_handle;

public:
  StringHandle(const char *name) : _handle(__itt_string_handle_createA(name)) {}

  operator __itt_string_handle *() const { return _handle; }
};

/// A task on a single thread. Nests within other tasks.
///
/// Each thread has its own task stack and tasks nest recursively on that stack.
/// A task cannot transfer threads.
///
/// SBRM is used to ensure task starts and ends are ballanced. The lifetime of
/// a task is either the lifetime of this object, or until end is called.
class ScopedTask {
  __itt_domain *_domain;

  ScopedTask(const ScopedTask &) = delete;
  ScopedTask &operator=(const ScopedTask &) = delete;

public:
  /// Create a task in Domain \p d named \p s.
  ScopedTask(const Domain &d, const StringHandle &s) : _domain(d) {
    __itt_task_begin(d, __itt_null, __itt_null, s);
  }

  ScopedTask(ScopedTask &&other) {
    *this = std::move(other);
  }

  ScopedTask &operator=(ScopedTask &&other) {
    _domain = other._domain;
    other._domain = nullptr;
    return *this;
  }

  /// Prematurely end this task.
  void end() {
    if (_domain)
      __itt_task_end(_domain);
    _domain = nullptr;
  }

  ~ScopedTask() { end(); }
};

/// A specific point in time. Allows metadata to be associated.
class Marker {
public:
  Marker(const Domain &d, const StringHandle &s) {
    __itt_marker(d, __itt_null, s, __itt_scope_global);
  }
};
#else
class Domain {
public:
  Domain(const char *name) {}
};

class StringHandle {
public:
  StringHandle(const char *name) {}
};

class ScopedTask {
public:
  ScopedTask(const Domain &d, const StringHandle &s) {}
  void end() {}
};

class Marker {
public:
  Marker(const Domain &d, const StringHandle &s) {}
};
#endif

inline const Domain &getDefaultDomain() {
  static Domain domain("org.llvm.lld");
  return domain;
}
} // end namespace lld.

#endif
