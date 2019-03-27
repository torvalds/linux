//===-- hwasan_poisoning.cc -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#include "hwasan_poisoning.h"

#include "hwasan_mapping.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"

namespace __hwasan {

uptr TagMemoryAligned(uptr p, uptr size, tag_t tag) {
  CHECK(IsAligned(p, kShadowAlignment));
  CHECK(IsAligned(size, kShadowAlignment));
  uptr shadow_start = MemToShadow(p);
  uptr shadow_size = MemToShadowSize(size);
  internal_memset((void *)shadow_start, tag, shadow_size);
  return AddTagToPointer(p, tag);
}

uptr TagMemory(uptr p, uptr size, tag_t tag) {
  uptr start = RoundDownTo(p, kShadowAlignment);
  uptr end = RoundUpTo(p + size, kShadowAlignment);
  return TagMemoryAligned(start, end - start, tag);
}

}  // namespace __hwasan
