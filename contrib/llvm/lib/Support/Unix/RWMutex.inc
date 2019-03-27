//= llvm/Support/Unix/RWMutex.inc - Unix Reader/Writer Mutual Exclusion Lock  =//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific (non-pthread) RWMutex class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "llvm/Support/Mutex.h"

namespace llvm {

using namespace sys;

// This naive implementation treats readers the same as writers.  This
// will therefore deadlock if a thread tries to acquire a read lock
// multiple times.

RWMutexImpl::RWMutexImpl() : data_(new MutexImpl(false)) { }

RWMutexImpl::~RWMutexImpl() {
  delete static_cast<MutexImpl *>(data_);
}

bool RWMutexImpl::reader_acquire() {
  return static_cast<MutexImpl *>(data_)->acquire();
}

bool RWMutexImpl::reader_release() {
  return static_cast<MutexImpl *>(data_)->release();
}

bool RWMutexImpl::writer_acquire() {
  return static_cast<MutexImpl *>(data_)->acquire();
}

bool RWMutexImpl::writer_release() {
  return static_cast<MutexImpl *>(data_)->release();
}

}
