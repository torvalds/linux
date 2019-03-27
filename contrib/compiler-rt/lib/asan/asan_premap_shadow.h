//===-- asan_mapping.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Premap shadow range with an ifunc resolver.
//===----------------------------------------------------------------------===//


#ifndef ASAN_PREMAP_SHADOW_H
#define ASAN_PREMAP_SHADOW_H

#if ASAN_PREMAP_SHADOW
namespace __asan {
// Conservative upper limit.
uptr PremapShadowSize();
bool PremapShadowFailed();
}
#endif

extern "C" INTERFACE_ATTRIBUTE void __asan_shadow();
extern "C" decltype(__asan_shadow)* __asan_premap_shadow();

#endif // ASAN_PREMAP_SHADOW_H
