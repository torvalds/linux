//===-- scudo_utils.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Platform specific utility functions.
///
//===----------------------------------------------------------------------===//

#include "scudo_utils.h"

#if defined(__x86_64__) || defined(__i386__)
# include <cpuid.h>
#elif defined(__arm__) || defined(__aarch64__)
# include "sanitizer_common/sanitizer_getauxval.h"
# if SANITIZER_FUCHSIA
#  include <zircon/syscalls.h>
#  include <zircon/features.h>
# elif SANITIZER_POSIX
#  include "sanitizer_common/sanitizer_posix.h"
#  include <fcntl.h>
# endif
#endif

#include <stdarg.h>

// TODO(kostyak): remove __sanitizer *Printf uses in favor for our own less
//                complicated string formatting code. The following is a
//                temporary workaround to be able to use __sanitizer::VSNPrintf.
namespace __sanitizer {

extern int VSNPrintf(char *buff, int buff_length, const char *format,
                     va_list args);

}  // namespace __sanitizer

namespace __scudo {

FORMAT(1, 2) void NORETURN dieWithMessage(const char *Format, ...) {
  static const char ScudoError[] = "Scudo ERROR: ";
  static constexpr uptr PrefixSize = sizeof(ScudoError) - 1;
  // Our messages are tiny, 256 characters is more than enough.
  char Message[256];
  va_list Args;
  va_start(Args, Format);
  internal_memcpy(Message, ScudoError, PrefixSize);
  VSNPrintf(Message + PrefixSize, sizeof(Message) - PrefixSize, Format, Args);
  va_end(Args);
  LogMessageOnPrintf(Message);
  if (common_flags()->abort_on_error)
    SetAbortMessage(Message);
  RawWrite(Message);
  Die();
}

#if defined(__x86_64__) || defined(__i386__)
// i386 and x86_64 specific code to detect CRC32 hardware support via CPUID.
// CRC32 requires the SSE 4.2 instruction set.
# ifndef bit_SSE4_2
#  define bit_SSE4_2 bit_SSE42  // clang and gcc have different defines.
# endif
bool hasHardwareCRC32() {
  u32 Eax, Ebx, Ecx, Edx;
  __get_cpuid(0, &Eax, &Ebx, &Ecx, &Edx);
  const bool IsIntel = (Ebx == signature_INTEL_ebx) &&
                       (Edx == signature_INTEL_edx) &&
                       (Ecx == signature_INTEL_ecx);
  const bool IsAMD = (Ebx == signature_AMD_ebx) &&
                     (Edx == signature_AMD_edx) &&
                     (Ecx == signature_AMD_ecx);
  if (!IsIntel && !IsAMD)
    return false;
  __get_cpuid(1, &Eax, &Ebx, &Ecx, &Edx);
  return !!(Ecx & bit_SSE4_2);
}
#elif defined(__arm__) || defined(__aarch64__)
// For ARM and AArch64, hardware CRC32 support is indicated in the AT_HWCAP
// auxiliary vector.
# ifndef AT_HWCAP
#  define AT_HWCAP 16
# endif
# ifndef HWCAP_CRC32
#  define HWCAP_CRC32 (1 << 7)  // HWCAP_CRC32 is missing on older platforms.
# endif
# if SANITIZER_POSIX
bool hasHardwareCRC32ARMPosix() {
  uptr F = internal_open("/proc/self/auxv", O_RDONLY);
  if (internal_iserror(F))
    return false;
  struct { uptr Tag; uptr Value; } Entry = { 0, 0 };
  for (;;) {
    uptr N = internal_read(F, &Entry, sizeof(Entry));
    if (internal_iserror(N) || N != sizeof(Entry) ||
        (Entry.Tag == 0 && Entry.Value == 0) || Entry.Tag == AT_HWCAP)
      break;
  }
  internal_close(F);
  return (Entry.Tag == AT_HWCAP && (Entry.Value & HWCAP_CRC32) != 0);
}
# else
bool hasHardwareCRC32ARMPosix() { return false; }
# endif  // SANITIZER_POSIX

// Bionic doesn't initialize its globals early enough. This causes issues when
// trying to access them from a preinit_array (b/25751302) or from another
// constructor called before the libc one (b/68046352). __progname is
// initialized after the other globals, so we can check its value to know if
// calling getauxval is safe.
extern "C" SANITIZER_WEAK_ATTRIBUTE char *__progname;
INLINE bool areBionicGlobalsInitialized() {
  return !SANITIZER_ANDROID || (&__progname && __progname);
}

bool hasHardwareCRC32() {
#if SANITIZER_FUCHSIA
  u32 HWCap;
  zx_status_t Status = zx_system_get_features(ZX_FEATURE_KIND_CPU, &HWCap);
  if (Status != ZX_OK || (HWCap & ZX_ARM64_FEATURE_ISA_CRC32) == 0)
    return false;
  return true;
#else
  if (&getauxval && areBionicGlobalsInitialized())
    return !!(getauxval(AT_HWCAP) & HWCAP_CRC32);
  return hasHardwareCRC32ARMPosix();
#endif  // SANITIZER_FUCHSIA
}
#else
bool hasHardwareCRC32() { return false; }
#endif  // defined(__x86_64__) || defined(__i386__)

}  // namespace __scudo
