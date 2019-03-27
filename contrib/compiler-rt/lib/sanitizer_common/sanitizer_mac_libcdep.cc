//===-- sanitizer_mac_libcdep.cc ------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements OSX-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_MAC
#include "sanitizer_mac.h"

#include <sys/mman.h>

namespace __sanitizer {

void RestrictMemoryToMaxAddress(uptr max_address) {
  uptr size_to_mmap = GetMaxUserVirtualAddress() + 1 - max_address;
  void *res = MmapFixedNoAccess(max_address, size_to_mmap, "high gap");
  CHECK(res != MAP_FAILED);
}

}  // namespace __sanitizer

#endif  // SANITIZER_MAC
