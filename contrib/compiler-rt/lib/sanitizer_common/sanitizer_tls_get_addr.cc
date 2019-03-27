//===-- sanitizer_tls_get_addr.cc -----------------------------------------===//
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
//===----------------------------------------------------------------------===//

#include "sanitizer_tls_get_addr.h"

#include "sanitizer_flags.h"
#include "sanitizer_platform_interceptors.h"

namespace __sanitizer {
#if SANITIZER_INTERCEPT_TLS_GET_ADDR

// The actual parameter that comes to __tls_get_addr
// is a pointer to a struct with two words in it:
struct TlsGetAddrParam {
  uptr dso_id;
  uptr offset;
};

// Glibc starting from 2.19 allocates tls using __signal_safe_memalign,
// which has such header.
struct Glibc_2_19_tls_header {
  uptr size;
  uptr start;
};

// This must be static TLS
__attribute__((tls_model("initial-exec")))
static __thread DTLS dtls;

// Make sure we properly destroy the DTLS objects:
// this counter should never get too large.
static atomic_uintptr_t number_of_live_dtls;

static const uptr kDestroyedThread = -1;

static inline void DTLS_Deallocate(DTLS::DTV *dtv, uptr size) {
  if (!size) return;
  VReport(2, "__tls_get_addr: DTLS_Deallocate %p %zd\n", dtv, size);
  UnmapOrDie(dtv, size * sizeof(DTLS::DTV));
  atomic_fetch_sub(&number_of_live_dtls, 1, memory_order_relaxed);
}

static inline void DTLS_Resize(uptr new_size) {
  if (dtls.dtv_size >= new_size) return;
  new_size = RoundUpToPowerOfTwo(new_size);
  new_size = Max(new_size, 4096UL / sizeof(DTLS::DTV));
  DTLS::DTV *new_dtv =
      (DTLS::DTV *)MmapOrDie(new_size * sizeof(DTLS::DTV), "DTLS_Resize");
  uptr num_live_dtls =
      atomic_fetch_add(&number_of_live_dtls, 1, memory_order_relaxed);
  VReport(2, "__tls_get_addr: DTLS_Resize %p %zd\n", &dtls, num_live_dtls);
  CHECK_LT(num_live_dtls, 1 << 20);
  uptr old_dtv_size = dtls.dtv_size;
  DTLS::DTV *old_dtv = dtls.dtv;
  if (old_dtv_size)
    internal_memcpy(new_dtv, dtls.dtv, dtls.dtv_size * sizeof(DTLS::DTV));
  dtls.dtv = new_dtv;
  dtls.dtv_size = new_size;
  if (old_dtv_size)
    DTLS_Deallocate(old_dtv, old_dtv_size);
}

void DTLS_Destroy() {
  if (!common_flags()->intercept_tls_get_addr) return;
  VReport(2, "__tls_get_addr: DTLS_Destroy %p %zd\n", &dtls, dtls.dtv_size);
  uptr s = dtls.dtv_size;
  dtls.dtv_size = kDestroyedThread;  // Do this before unmap for AS-safety.
  DTLS_Deallocate(dtls.dtv, s);
}

#if defined(__powerpc64__) || defined(__mips__)
// This is glibc's TLS_DTV_OFFSET:
// "Dynamic thread vector pointers point 0x8000 past the start of each
//  TLS block."
static const uptr kDtvOffset = 0x8000;
#else
static const uptr kDtvOffset = 0;
#endif

DTLS::DTV *DTLS_on_tls_get_addr(void *arg_void, void *res,
                                uptr static_tls_begin, uptr static_tls_end) {
  if (!common_flags()->intercept_tls_get_addr) return 0;
  TlsGetAddrParam *arg = reinterpret_cast<TlsGetAddrParam *>(arg_void);
  uptr dso_id = arg->dso_id;
  if (dtls.dtv_size == kDestroyedThread) return 0;
  DTLS_Resize(dso_id + 1);
  if (dtls.dtv[dso_id].beg) return 0;
  uptr tls_size = 0;
  uptr tls_beg = reinterpret_cast<uptr>(res) - arg->offset - kDtvOffset;
  VReport(2, "__tls_get_addr: %p {%p,%p} => %p; tls_beg: %p; sp: %p "
             "num_live_dtls %zd\n",
          arg, arg->dso_id, arg->offset, res, tls_beg, &tls_beg,
          atomic_load(&number_of_live_dtls, memory_order_relaxed));
  if (dtls.last_memalign_ptr == tls_beg) {
    tls_size = dtls.last_memalign_size;
    VReport(2, "__tls_get_addr: glibc <=2.18 suspected; tls={%p,%p}\n",
        tls_beg, tls_size);
  } else if (tls_beg >= static_tls_begin && tls_beg < static_tls_end) {
    // This is the static TLS block which was initialized / unpoisoned at thread
    // creation.
    VReport(2, "__tls_get_addr: static tls: %p\n", tls_beg);
    tls_size = 0;
  } else if ((tls_beg % 4096) == sizeof(Glibc_2_19_tls_header)) {
    // We may want to check gnu_get_libc_version().
    Glibc_2_19_tls_header *header = (Glibc_2_19_tls_header *)tls_beg - 1;
    tls_size = header->size;
    tls_beg = header->start;
    VReport(2, "__tls_get_addr: glibc >=2.19 suspected; tls={%p %p}\n",
        tls_beg, tls_size);
  } else {
    VReport(2, "__tls_get_addr: Can't guess glibc version\n");
    // This may happen inside the DTOR of main thread, so just ignore it.
    tls_size = 0;
  }
  dtls.dtv[dso_id].beg = tls_beg;
  dtls.dtv[dso_id].size = tls_size;
  return dtls.dtv + dso_id;
}

void DTLS_on_libc_memalign(void *ptr, uptr size) {
  if (!common_flags()->intercept_tls_get_addr) return;
  VReport(2, "DTLS_on_libc_memalign: %p %p\n", ptr, size);
  dtls.last_memalign_ptr = reinterpret_cast<uptr>(ptr);
  dtls.last_memalign_size = size;
}

DTLS *DTLS_Get() { return &dtls; }

bool DTLSInDestruction(DTLS *dtls) {
  return dtls->dtv_size == kDestroyedThread;
}

#else
void DTLS_on_libc_memalign(void *ptr, uptr size) {}
DTLS::DTV *DTLS_on_tls_get_addr(void *arg, void *res,
  unsigned long, unsigned long) { return 0; }
DTLS *DTLS_Get() { return 0; }
void DTLS_Destroy() {}
bool DTLSInDestruction(DTLS *dtls) {
  UNREACHABLE("dtls is unsupported on this platform!");
}

#endif  // SANITIZER_INTERCEPT_TLS_GET_ADDR

}  // namespace __sanitizer
