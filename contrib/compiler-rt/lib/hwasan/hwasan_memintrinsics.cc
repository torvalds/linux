//===-- hwasan_memintrinsics.cc ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer and contains HWASAN versions of
/// memset, memcpy and memmove
///
//===----------------------------------------------------------------------===//

#include <string.h>
#include "hwasan.h"
#include "hwasan_checks.h"
#include "hwasan_flags.h"
#include "hwasan_interface_internal.h"
#include "sanitizer_common/sanitizer_libc.h"

using namespace __hwasan;

void *__hwasan_memset(void *block, int c, uptr size) {
  CheckAddressSized<ErrorAction::Recover, AccessType::Store>(
      reinterpret_cast<uptr>(block), size);
  return memset(UntagPtr(block), c, size);
}

void *__hwasan_memcpy(void *to, const void *from, uptr size) {
  CheckAddressSized<ErrorAction::Recover, AccessType::Store>(
      reinterpret_cast<uptr>(to), size);
  CheckAddressSized<ErrorAction::Recover, AccessType::Load>(
      reinterpret_cast<uptr>(from), size);
  return memcpy(UntagPtr(to), UntagPtr(from), size);
}

void *__hwasan_memmove(void *to, const void *from, uptr size) {
  CheckAddressSized<ErrorAction::Recover, AccessType::Store>(
      reinterpret_cast<uptr>(to), size);
  CheckAddressSized<ErrorAction::Recover, AccessType::Load>(
      reinterpret_cast<uptr>(from), size);
  return memmove(UntagPtr(to), UntagPtr(from), size);
}
