//===-- sanitizer_tls_get_addr.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Handle the __tls_get_addr call.
//
// All this magic is specific to glibc and is required to workaround
// the lack of interface that would tell us about the Dynamic TLS (DTLS).
// https://sourceware.org/bugzilla/show_bug.cgi?id=16291
//
// The matters get worse because the glibc implementation changed between
// 2.18 and 2.19:
// https://groups.google.com/forum/#!topic/address-sanitizer/BfwYD8HMxTM
//
// Before 2.19, every DTLS chunk is allocated with __libc_memalign,
// which we intercept and thus know where is the DTLS.
// Since 2.19, DTLS chunks are allocated with __signal_safe_memalign,
// which is an internal function that wraps a mmap call, neither of which
// we can intercept. Luckily, __signal_safe_memalign has a simple parseable
// header which we can use.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_TLS_GET_ADDR_H
#define SANITIZER_TLS_GET_ADDR_H

#include "sanitizer_common.h"

namespace __sanitizer {

struct DTLS {
  // Array of DTLS chunks for the current Thread.
  // If beg == 0, the chunk is unused.
  struct DTV {
    uptr beg, size;
  };

  uptr dtv_size;
  DTV *dtv;  // dtv_size elements, allocated by MmapOrDie.

  // Auxiliary fields, don't access them outside sanitizer_tls_get_addr.cc
  uptr last_memalign_size;
  uptr last_memalign_ptr;
};

// Returns pointer and size of a linker-allocated TLS block.
// Each block is returned exactly once.
DTLS::DTV *DTLS_on_tls_get_addr(void *arg, void *res, uptr static_tls_begin,
                                uptr static_tls_end);
void DTLS_on_libc_memalign(void *ptr, uptr size);
DTLS *DTLS_Get();
void DTLS_Destroy();  // Make sure to call this before the thread is destroyed.
// Returns true if DTLS of suspended thread is in destruction process.
bool DTLSInDestruction(DTLS *dtls);

}  // namespace __sanitizer

#endif  // SANITIZER_TLS_GET_ADDR_H
