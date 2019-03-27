//===-- asan_activation.h ---------------------------------------*- C++ -*-===//
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
// ASan activation/deactivation logic.
//===----------------------------------------------------------------------===//

#ifndef ASAN_ACTIVATION_H
#define ASAN_ACTIVATION_H

namespace __asan {
void AsanDeactivate();
void AsanActivate();
}  // namespace __asan

#endif  // ASAN_ACTIVATION_H
