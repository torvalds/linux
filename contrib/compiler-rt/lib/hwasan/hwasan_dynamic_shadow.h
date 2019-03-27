//===-- hwasan_dynamic_shadow.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer. It reserves dynamic shadow memory
/// region.
///
//===----------------------------------------------------------------------===//

#ifndef HWASAN_PREMAP_SHADOW_H
#define HWASAN_PREMAP_SHADOW_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __hwasan {

uptr FindDynamicShadowStart(uptr shadow_size_bytes);

}  // namespace __hwasan

#endif  // HWASAN_PREMAP_SHADOW_H
