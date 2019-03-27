//===- StoreRef.h - Smart pointer for store objects -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defined the type StoreRef.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STOREREF_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STOREREF_H

#include <cassert>

namespace clang {
namespace ento {

class StoreManager;

/// Store - This opaque type encapsulates an immutable mapping from
///  locations to values.  At a high-level, it represents the symbolic
///  memory model.  Different subclasses of StoreManager may choose
///  different types to represent the locations and values.
using Store = const void *;

class StoreRef {
  Store store;
  StoreManager &mgr;

public:
  StoreRef(Store store, StoreManager &smgr);
  StoreRef(const StoreRef &sr);
  StoreRef &operator=(StoreRef const &newStore);
  ~StoreRef();

  bool operator==(const StoreRef &x) const {
    assert(&mgr == &x.mgr);
    return x.store == store;
  }

  bool operator!=(const StoreRef &x) const { return !operator==(x); }

  Store getStore() const { return store; }
  const StoreManager &getStoreManager() const { return mgr; }
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STOREREF_H
