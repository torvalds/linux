//===- llvm/Support/Win32/Mutex.inc - Win32 Mutex Implementation -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Win32 specific (non-pthread) Mutex class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic Win32 code that
//===          is guaranteed to work on *all* Win32 variants.
//===----------------------------------------------------------------------===//

#include "WindowsSupport.h"
#include "llvm/Support/Mutex.h"

namespace llvm {

sys::MutexImpl::MutexImpl(bool /*recursive*/)
{
  data_ = new CRITICAL_SECTION;
  InitializeCriticalSection((LPCRITICAL_SECTION)data_);
}

sys::MutexImpl::~MutexImpl()
{
  DeleteCriticalSection((LPCRITICAL_SECTION)data_);
  delete (LPCRITICAL_SECTION)data_;
  data_ = 0;
}

bool
sys::MutexImpl::acquire()
{
  EnterCriticalSection((LPCRITICAL_SECTION)data_);
  return true;
}

bool
sys::MutexImpl::release()
{
  LeaveCriticalSection((LPCRITICAL_SECTION)data_);
  return true;
}

bool
sys::MutexImpl::tryacquire()
{
  return TryEnterCriticalSection((LPCRITICAL_SECTION)data_);
}

}
