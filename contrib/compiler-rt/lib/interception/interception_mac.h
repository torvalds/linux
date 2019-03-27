//===-- interception_mac.h --------------------------------------*- C++ -*-===//
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
// Mac-specific interception methods.
//===----------------------------------------------------------------------===//

#if SANITIZER_MAC

#if !defined(INCLUDED_FROM_INTERCEPTION_LIB)
# error "interception_mac.h should be included from interception.h only"
#endif

#ifndef INTERCEPTION_MAC_H
#define INTERCEPTION_MAC_H

#define INTERCEPT_FUNCTION_MAC(func)
#define INTERCEPT_FUNCTION_VER_MAC(func, symver)

#endif  // INTERCEPTION_MAC_H
#endif  // SANITIZER_MAC
