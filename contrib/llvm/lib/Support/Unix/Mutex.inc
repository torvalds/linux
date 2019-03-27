//===- llvm/Support/Unix/Mutex.inc - Unix Mutex Implementation ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific (non-pthread) Mutex class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

namespace llvm
{
using namespace sys;

MutexImpl::MutexImpl( bool recursive)
{
}

MutexImpl::~MutexImpl()
{
}

bool
MutexImpl::release()
{
  return true;
}

bool
MutexImpl::tryacquire( void )
{
  return true;
}

}
