//===-- dfsan_interceptors.cc ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// Interceptors for standard library functions.
//===----------------------------------------------------------------------===//

#include "dfsan/dfsan.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"

using namespace __sanitizer;

INTERCEPTOR(void *, mmap, void *addr, SIZE_T length, int prot, int flags,
            int fd, OFF_T offset) {
  void *res = REAL(mmap)(addr, length, prot, flags, fd, offset);
  if (res != (void*)-1)
    dfsan_set_label(0, res, RoundUpTo(length, GetPageSize()));
  return res;
}

INTERCEPTOR(void *, mmap64, void *addr, SIZE_T length, int prot, int flags,
            int fd, OFF64_T offset) {
  void *res = REAL(mmap64)(addr, length, prot, flags, fd, offset);
  if (res != (void*)-1)
    dfsan_set_label(0, res, RoundUpTo(length, GetPageSize()));
  return res;
}

namespace __dfsan {
void InitializeInterceptors() {
  static int inited = 0;
  CHECK_EQ(inited, 0);

  INTERCEPT_FUNCTION(mmap);
  INTERCEPT_FUNCTION(mmap64);
  inited = 1;
}
}  // namespace __dfsan
