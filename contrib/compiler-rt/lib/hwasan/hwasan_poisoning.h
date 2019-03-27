//===-- hwasan_poisoning.h --------------------------------------*- C++ -*-===//
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

#ifndef HWASAN_POISONING_H
#define HWASAN_POISONING_H

#include "hwasan.h"

namespace __hwasan {
uptr TagMemory(uptr p, uptr size, tag_t tag);
uptr TagMemoryAligned(uptr p, uptr size, tag_t tag);

}  // namespace __hwasan

#endif  // HWASAN_POISONING_H
