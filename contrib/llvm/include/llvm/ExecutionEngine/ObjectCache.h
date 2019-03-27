//===-- ObjectCache.h - Class definition for the ObjectCache ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_OBJECTCACHE_H
#define LLVM_EXECUTIONENGINE_OBJECTCACHE_H

#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {

class Module;

/// This is the base ObjectCache type which can be provided to an
/// ExecutionEngine for the purpose of avoiding compilation for Modules that
/// have already been compiled and an object file is available.
class ObjectCache {
  virtual void anchor();

public:
  ObjectCache() = default;

  virtual ~ObjectCache() = default;

  /// notifyObjectCompiled - Provides a pointer to compiled code for Module M.
  virtual void notifyObjectCompiled(const Module *M, MemoryBufferRef Obj) = 0;

  /// Returns a pointer to a newly allocated MemoryBuffer that contains the
  /// object which corresponds with Module M, or 0 if an object is not
  /// available.
  virtual std::unique_ptr<MemoryBuffer> getObject(const Module* M) = 0;
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_OBJECTCACHE_H
