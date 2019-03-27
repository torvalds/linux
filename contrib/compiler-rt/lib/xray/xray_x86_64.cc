#include "cpuid.h"
#include "sanitizer_common/sanitizer_common.h"
#if !SANITIZER_FUCHSIA
#include "sanitizer_common/sanitizer_posix.h"
#endif
#include "xray_defs.h"
#include "xray_interface_internal.h"

#if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_OPENBSD || SANITIZER_MAC
#include <sys/types.h>
#if SANITIZER_OPENBSD
#include <sys/time.h>
#include <machine/cpu.h>
#endif
#include <sys/sysctl.h>
#elif SANITIZER_FUCHSIA
#include <zircon/syscalls.h>
#endif

#include <atomic>
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <iterator>
#include <limits>
#include <tuple>
#include <unistd.h>

namespace __xray {

#if SANITIZER_LINUX
static std::pair<ssize_t, bool>
retryingReadSome(int Fd, char *Begin, char *End) XRAY_NEVER_INSTRUMENT {
  auto BytesToRead = std::distance(Begin, End);
  ssize_t BytesRead;
  ssize_t TotalBytesRead = 0;
  while (BytesToRead && (BytesRead = read(Fd, Begin, BytesToRead))) {
    if (BytesRead == -1) {
      if (errno == EINTR)
        continue;
      Report("Read error; errno = %d\n", errno);
      return std::make_pair(TotalBytesRead, false);
    }

    TotalBytesRead += BytesRead;
    BytesToRead -= BytesRead;
    Begin += BytesRead;
  }
  return std::make_pair(TotalBytesRead, true);
}

static bool readValueFromFile(const char *Filename,
                              long long *Value) XRAY_NEVER_INSTRUMENT {
  int Fd = open(Filename, O_RDONLY | O_CLOEXEC);
  if (Fd == -1)
    return false;
  static constexpr size_t BufSize = 256;
  char Line[BufSize] = {};
  ssize_t BytesRead;
  bool Success;
  std::tie(BytesRead, Success) = retryingReadSome(Fd, Line, Line + BufSize);
  close(Fd);
  if (!Success)
    return false;
  const char *End = nullptr;
  long long Tmp = internal_simple_strtoll(Line, &End, 10);
  bool Result = false;
  if (Line[0] != '\0' && (*End == '\n' || *End == '\0')) {
    *Value = Tmp;
    Result = true;
  }
  return Result;
}

uint64_t getTSCFrequency() XRAY_NEVER_INSTRUMENT {
  long long TSCFrequency = -1;
  if (readValueFromFile("/sys/devices/system/cpu/cpu0/tsc_freq_khz",
                        &TSCFrequency)) {
    TSCFrequency *= 1000;
  } else if (readValueFromFile(
                 "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
                 &TSCFrequency)) {
    TSCFrequency *= 1000;
  } else {
    Report("Unable to determine CPU frequency for TSC accounting.\n");
  }
  return TSCFrequency == -1 ? 0 : static_cast<uint64_t>(TSCFrequency);
}
#elif SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_OPENBSD || SANITIZER_MAC
uint64_t getTSCFrequency() XRAY_NEVER_INSTRUMENT {
    long long TSCFrequency = -1;
    size_t tscfreqsz = sizeof(TSCFrequency);
#if SANITIZER_OPENBSD
    int Mib[2] = { CTL_MACHDEP, CPU_TSCFREQ };
    if (internal_sysctl(Mib, 2, &TSCFrequency, &tscfreqsz, NULL, 0) != -1) {
#elif SANITIZER_MAC
    if (internal_sysctlbyname("machdep.tsc.frequency", &TSCFrequency,
                              &tscfreqsz, NULL, 0) != -1) {

#else
    if (internal_sysctlbyname("machdep.tsc_freq", &TSCFrequency, &tscfreqsz,
                              NULL, 0) != -1) {
#endif
        return static_cast<uint64_t>(TSCFrequency);
    } else {
      Report("Unable to determine CPU frequency for TSC accounting.\n");
    }

    return 0;
}
#elif !SANITIZER_FUCHSIA
uint64_t getTSCFrequency() XRAY_NEVER_INSTRUMENT {
    /* Not supported */
    return 0;
}
#endif

static constexpr uint8_t CallOpCode = 0xe8;
static constexpr uint16_t MovR10Seq = 0xba41;
static constexpr uint16_t Jmp9Seq = 0x09eb;
static constexpr uint16_t Jmp20Seq = 0x14eb;
static constexpr uint16_t Jmp15Seq = 0x0feb;
static constexpr uint8_t JmpOpCode = 0xe9;
static constexpr uint8_t RetOpCode = 0xc3;
static constexpr uint16_t NopwSeq = 0x9066;

static constexpr int64_t MinOffset{std::numeric_limits<int32_t>::min()};
static constexpr int64_t MaxOffset{std::numeric_limits<int32_t>::max()};

bool patchFunctionEntry(const bool Enable, const uint32_t FuncId,
                        const XRaySledEntry &Sled,
                        void (*Trampoline)()) XRAY_NEVER_INSTRUMENT {
  // Here we do the dance of replacing the following sled:
  //
  // xray_sled_n:
  //   jmp +9
  //   <9 byte nop>
  //
  // With the following:
  //
  //   mov r10d, <function id>
  //   call <relative 32bit offset to entry trampoline>
  //
  // We need to do this in the following order:
  //
  // 1. Put the function id first, 2 bytes from the start of the sled (just
  // after the 2-byte jmp instruction).
  // 2. Put the call opcode 6 bytes from the start of the sled.
  // 3. Put the relative offset 7 bytes from the start of the sled.
  // 4. Do an atomic write over the jmp instruction for the "mov r10d"
  // opcode and first operand.
  //
  // Prerequisite is to compute the relative offset to the trampoline's address.
  int64_t TrampolineOffset = reinterpret_cast<int64_t>(Trampoline) -
                             (static_cast<int64_t>(Sled.Address) + 11);
  if (TrampolineOffset < MinOffset || TrampolineOffset > MaxOffset) {
    Report("XRay Entry trampoline (%p) too far from sled (%p)\n",
           Trampoline, reinterpret_cast<void *>(Sled.Address));
    return false;
  }
  if (Enable) {
    *reinterpret_cast<uint32_t *>(Sled.Address + 2) = FuncId;
    *reinterpret_cast<uint8_t *>(Sled.Address + 6) = CallOpCode;
    *reinterpret_cast<uint32_t *>(Sled.Address + 7) = TrampolineOffset;
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), MovR10Seq,
        std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), Jmp9Seq,
        std::memory_order_release);
    // FIXME: Write out the nops still?
  }
  return true;
}

bool patchFunctionExit(const bool Enable, const uint32_t FuncId,
                       const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // Here we do the dance of replacing the following sled:
  //
  // xray_sled_n:
  //   ret
  //   <10 byte nop>
  //
  // With the following:
  //
  //   mov r10d, <function id>
  //   jmp <relative 32bit offset to exit trampoline>
  //
  // 1. Put the function id first, 2 bytes from the start of the sled (just
  // after the 1-byte ret instruction).
  // 2. Put the jmp opcode 6 bytes from the start of the sled.
  // 3. Put the relative offset 7 bytes from the start of the sled.
  // 4. Do an atomic write over the jmp instruction for the "mov r10d"
  // opcode and first operand.
  //
  // Prerequisite is to compute the relative offset fo the
  // __xray_FunctionExit function's address.
  int64_t TrampolineOffset = reinterpret_cast<int64_t>(__xray_FunctionExit) -
                             (static_cast<int64_t>(Sled.Address) + 11);
  if (TrampolineOffset < MinOffset || TrampolineOffset > MaxOffset) {
    Report("XRay Exit trampoline (%p) too far from sled (%p)\n",
           __xray_FunctionExit, reinterpret_cast<void *>(Sled.Address));
    return false;
  }
  if (Enable) {
    *reinterpret_cast<uint32_t *>(Sled.Address + 2) = FuncId;
    *reinterpret_cast<uint8_t *>(Sled.Address + 6) = JmpOpCode;
    *reinterpret_cast<uint32_t *>(Sled.Address + 7) = TrampolineOffset;
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), MovR10Seq,
        std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint8_t> *>(Sled.Address), RetOpCode,
        std::memory_order_release);
    // FIXME: Write out the nops still?
  }
  return true;
}

bool patchFunctionTailExit(const bool Enable, const uint32_t FuncId,
                           const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // Here we do the dance of replacing the tail call sled with a similar
  // sequence as the entry sled, but calls the tail exit sled instead.
  int64_t TrampolineOffset =
      reinterpret_cast<int64_t>(__xray_FunctionTailExit) -
      (static_cast<int64_t>(Sled.Address) + 11);
  if (TrampolineOffset < MinOffset || TrampolineOffset > MaxOffset) {
    Report("XRay Tail Exit trampoline (%p) too far from sled (%p)\n",
           __xray_FunctionTailExit, reinterpret_cast<void *>(Sled.Address));
    return false;
  }
  if (Enable) {
    *reinterpret_cast<uint32_t *>(Sled.Address + 2) = FuncId;
    *reinterpret_cast<uint8_t *>(Sled.Address + 6) = CallOpCode;
    *reinterpret_cast<uint32_t *>(Sled.Address + 7) = TrampolineOffset;
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), MovR10Seq,
        std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), Jmp9Seq,
        std::memory_order_release);
    // FIXME: Write out the nops still?
  }
  return true;
}

bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // Here we do the dance of replacing the following sled:
  //
  // In Version 0:
  //
  // xray_sled_n:
  //   jmp +20          // 2 bytes
  //   ...
  //
  // With the following:
  //
  //   nopw             // 2 bytes*
  //   ...
  //
  //
  // The "unpatch" should just turn the 'nopw' back to a 'jmp +20'.
  //
  // ---
  //
  // In Version 1:
  //
  //   The jump offset is now 15 bytes (0x0f), so when restoring the nopw back
  //   to a jmp, use 15 bytes instead.
  //
  if (Enable) {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), NopwSeq,
        std::memory_order_release);
  } else {
    switch (Sled.Version) {
    case 1:
      std::atomic_store_explicit(
          reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), Jmp15Seq,
          std::memory_order_release);
      break;
    case 0:
    default:
      std::atomic_store_explicit(
          reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), Jmp20Seq,
          std::memory_order_release);
      break;
    }
    }
  return false;
}

bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // Here we do the dance of replacing the following sled:
  //
  // xray_sled_n:
  //   jmp +20          // 2 byte instruction
  //   ...
  //
  // With the following:
  //
  //   nopw             // 2 bytes
  //   ...
  //
  //
  // The "unpatch" should just turn the 'nopw' back to a 'jmp +20'.
  // The 20 byte sled stashes three argument registers, calls the trampoline,
  // unstashes the registers and returns. If the arguments are already in
  // the correct registers, the stashing and unstashing become equivalently
  // sized nops.
  if (Enable) {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), NopwSeq,
        std::memory_order_release);
  } else {
      std::atomic_store_explicit(
          reinterpret_cast<std::atomic<uint16_t> *>(Sled.Address), Jmp20Seq,
          std::memory_order_release);
  }
  return false;
}

#if !SANITIZER_FUCHSIA
// We determine whether the CPU we're running on has the correct features we
// need. In x86_64 this will be rdtscp support.
bool probeRequiredCPUFeatures() XRAY_NEVER_INSTRUMENT {
  unsigned int EAX, EBX, ECX, EDX;

  // We check whether rdtscp support is enabled. According to the x86_64 manual,
  // level should be set at 0x80000001, and we should have a look at bit 27 in
  // EDX. That's 0x8000000 (or 1u << 27).
  __asm__ __volatile__("cpuid" : "=a"(EAX), "=b"(EBX), "=c"(ECX), "=d"(EDX)
    : "0"(0x80000001));
  if (!(EDX & (1u << 27))) {
    Report("Missing rdtscp support.\n");
    return false;
  }
  // Also check whether we can determine the CPU frequency, since if we cannot,
  // we should use the emulated TSC instead.
  if (!getTSCFrequency()) {
    Report("Unable to determine CPU frequency.\n");
    return false;
  }
  return true;
}
#endif

} // namespace __xray
