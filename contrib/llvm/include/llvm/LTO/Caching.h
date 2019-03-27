//===- Caching.h - LLVM Link Time Optimizer Configuration -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the localCache function, which allows clients to add a
// filesystem cache to ThinLTO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_CACHING_H
#define LLVM_LTO_CACHING_H

#include "llvm/LTO/LTO.h"
#include <string>

namespace llvm {
namespace lto {

/// This type defines the callback to add a pre-existing native object file
/// (e.g. in a cache).
///
/// Buffer callbacks must be thread safe.
typedef std::function<void(unsigned Task, std::unique_ptr<MemoryBuffer> MB)>
    AddBufferFn;

/// Create a local file system cache which uses the given cache directory and
/// file callback. This function also creates the cache directory if it does not
/// already exist.
Expected<NativeObjectCache> localCache(StringRef CacheDirectoryPath,
                                       AddBufferFn AddBuffer);

} // namespace lto
} // namespace llvm

#endif
