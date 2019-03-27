//===-- tsan_platform.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Platform-specific code.
//===----------------------------------------------------------------------===//

#ifndef TSAN_PLATFORM_H
#define TSAN_PLATFORM_H

#if !defined(__LP64__) && !defined(_WIN64)
# error "Only 64-bit is supported"
#endif

#include "tsan_defs.h"
#include "tsan_trace.h"

namespace __tsan {

#if !SANITIZER_GO

#if defined(__x86_64__)
/*
C/C++ on linux/x86_64 and freebsd/x86_64
0000 0000 1000 - 0080 0000 0000: main binary and/or MAP_32BIT mappings (512GB)
0040 0000 0000 - 0100 0000 0000: -
0100 0000 0000 - 2000 0000 0000: shadow
2000 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 4000 0000 0000: metainfo (memory blocks and sync objects)
4000 0000 0000 - 5500 0000 0000: -
5500 0000 0000 - 5680 0000 0000: pie binaries without ASLR or on 4.1+ kernels
5680 0000 0000 - 6000 0000 0000: -
6000 0000 0000 - 6200 0000 0000: traces
6200 0000 0000 - 7d00 0000 0000: -
7b00 0000 0000 - 7c00 0000 0000: heap
7c00 0000 0000 - 7e80 0000 0000: -
7e80 0000 0000 - 8000 0000 0000: modules and main thread stack

C/C++ on netbsd/amd64 can reuse the same mapping:
 * The address space starts from 0x1000 (option with 0x0) and ends with
   0x7f7ffffff000.
 * LoAppMem-kHeapMemEnd can be reused as it is.
 * No VDSO support.
 * No MidAppMem region.
 * No additional HeapMem region.
 * HiAppMem contains the stack, loader, shared libraries and heap.
 * Stack on NetBSD/amd64 has prereserved 128MB.
 * Heap grows downwards (top-down).
 * ASLR must be disabled per-process or globally.

*/
struct Mapping {
  static const uptr kMetaShadowBeg = 0x300000000000ull;
  static const uptr kMetaShadowEnd = 0x340000000000ull;
  static const uptr kTraceMemBeg   = 0x600000000000ull;
  static const uptr kTraceMemEnd   = 0x620000000000ull;
  static const uptr kShadowBeg     = 0x010000000000ull;
  static const uptr kShadowEnd     = 0x200000000000ull;
  static const uptr kHeapMemBeg    = 0x7b0000000000ull;
  static const uptr kHeapMemEnd    = 0x7c0000000000ull;
  static const uptr kLoAppMemBeg   = 0x000000001000ull;
  static const uptr kLoAppMemEnd   = 0x008000000000ull;
  static const uptr kMidAppMemBeg  = 0x550000000000ull;
  static const uptr kMidAppMemEnd  = 0x568000000000ull;
  static const uptr kHiAppMemBeg   = 0x7e8000000000ull;
  static const uptr kHiAppMemEnd   = 0x800000000000ull;
  static const uptr kAppMemMsk     = 0x780000000000ull;
  static const uptr kAppMemXor     = 0x040000000000ull;
  static const uptr kVdsoBeg       = 0xf000000000000000ull;
};

#define TSAN_MID_APP_RANGE 1
#elif defined(__mips64)
/*
C/C++ on linux/mips64 (40-bit VMA)
0000 0000 00 - 0100 0000 00: -                                           (4 GB)
0100 0000 00 - 0200 0000 00: main binary                                 (4 GB)
0200 0000 00 - 2000 0000 00: -                                         (120 GB)
2000 0000 00 - 4000 0000 00: shadow                                    (128 GB)
4000 0000 00 - 5000 0000 00: metainfo (memory blocks and sync objects)  (64 GB)
5000 0000 00 - aa00 0000 00: -                                         (360 GB)
aa00 0000 00 - ab00 0000 00: main binary (PIE)                           (4 GB)
ab00 0000 00 - b000 0000 00: -                                          (20 GB)
b000 0000 00 - b200 0000 00: traces                                      (8 GB)
b200 0000 00 - fe00 0000 00: -                                         (304 GB)
fe00 0000 00 - ff00 0000 00: heap                                        (4 GB)
ff00 0000 00 - ff80 0000 00: -                                           (2 GB)
ff80 0000 00 - ffff ffff ff: modules and main thread stack              (<2 GB)
*/
struct Mapping {
  static const uptr kMetaShadowBeg = 0x4000000000ull;
  static const uptr kMetaShadowEnd = 0x5000000000ull;
  static const uptr kTraceMemBeg   = 0xb000000000ull;
  static const uptr kTraceMemEnd   = 0xb200000000ull;
  static const uptr kShadowBeg     = 0x2000000000ull;
  static const uptr kShadowEnd     = 0x4000000000ull;
  static const uptr kHeapMemBeg    = 0xfe00000000ull;
  static const uptr kHeapMemEnd    = 0xff00000000ull;
  static const uptr kLoAppMemBeg   = 0x0100000000ull;
  static const uptr kLoAppMemEnd   = 0x0200000000ull;
  static const uptr kMidAppMemBeg  = 0xaa00000000ull;
  static const uptr kMidAppMemEnd  = 0xab00000000ull;
  static const uptr kHiAppMemBeg   = 0xff80000000ull;
  static const uptr kHiAppMemEnd   = 0xffffffffffull;
  static const uptr kAppMemMsk     = 0xf800000000ull;
  static const uptr kAppMemXor     = 0x0800000000ull;
  static const uptr kVdsoBeg       = 0xfffff00000ull;
};

#define TSAN_MID_APP_RANGE 1
#elif defined(__aarch64__) && defined(__APPLE__)
/*
C/C++ on Darwin/iOS/ARM64 (36-bit VMA, 64 GB VM)
0000 0000 00 - 0100 0000 00: -                                    (4 GB)
0100 0000 00 - 0200 0000 00: main binary, modules, thread stacks  (4 GB)
0200 0000 00 - 0300 0000 00: heap                                 (4 GB)
0300 0000 00 - 0400 0000 00: -                                    (4 GB)
0400 0000 00 - 0c00 0000 00: shadow memory                       (32 GB)
0c00 0000 00 - 0d00 0000 00: -                                    (4 GB)
0d00 0000 00 - 0e00 0000 00: metainfo                             (4 GB)
0e00 0000 00 - 0f00 0000 00: -                                    (4 GB)
0f00 0000 00 - 0fc0 0000 00: traces                               (3 GB)
0fc0 0000 00 - 1000 0000 00: -
*/
struct Mapping {
  static const uptr kLoAppMemBeg   = 0x0100000000ull;
  static const uptr kLoAppMemEnd   = 0x0200000000ull;
  static const uptr kHeapMemBeg    = 0x0200000000ull;
  static const uptr kHeapMemEnd    = 0x0300000000ull;
  static const uptr kShadowBeg     = 0x0400000000ull;
  static const uptr kShadowEnd     = 0x0c00000000ull;
  static const uptr kMetaShadowBeg = 0x0d00000000ull;
  static const uptr kMetaShadowEnd = 0x0e00000000ull;
  static const uptr kTraceMemBeg   = 0x0f00000000ull;
  static const uptr kTraceMemEnd   = 0x0fc0000000ull;
  static const uptr kHiAppMemBeg   = 0x0fc0000000ull;
  static const uptr kHiAppMemEnd   = 0x0fc0000000ull;
  static const uptr kAppMemMsk     =          0x0ull;
  static const uptr kAppMemXor     =          0x0ull;
  static const uptr kVdsoBeg       = 0x7000000000000000ull;
};

#elif defined(__aarch64__)
// AArch64 supports multiple VMA which leads to multiple address transformation
// functions.  To support these multiple VMAS transformations and mappings TSAN
// runtime for AArch64 uses an external memory read (vmaSize) to select which
// mapping to use.  Although slower, it make a same instrumented binary run on
// multiple kernels.

/*
C/C++ on linux/aarch64 (39-bit VMA)
0000 0010 00 - 0100 0000 00: main binary
0100 0000 00 - 0800 0000 00: -
0800 0000 00 - 2000 0000 00: shadow memory
2000 0000 00 - 3100 0000 00: -
3100 0000 00 - 3400 0000 00: metainfo
3400 0000 00 - 5500 0000 00: -
5500 0000 00 - 5600 0000 00: main binary (PIE)
5600 0000 00 - 6000 0000 00: -
6000 0000 00 - 6200 0000 00: traces
6200 0000 00 - 7d00 0000 00: -
7c00 0000 00 - 7d00 0000 00: heap
7d00 0000 00 - 7fff ffff ff: modules and main thread stack
*/
struct Mapping39 {
  static const uptr kLoAppMemBeg   = 0x0000001000ull;
  static const uptr kLoAppMemEnd   = 0x0100000000ull;
  static const uptr kShadowBeg     = 0x0800000000ull;
  static const uptr kShadowEnd     = 0x2000000000ull;
  static const uptr kMetaShadowBeg = 0x3100000000ull;
  static const uptr kMetaShadowEnd = 0x3400000000ull;
  static const uptr kMidAppMemBeg  = 0x5500000000ull;
  static const uptr kMidAppMemEnd  = 0x5600000000ull;
  static const uptr kTraceMemBeg   = 0x6000000000ull;
  static const uptr kTraceMemEnd   = 0x6200000000ull;
  static const uptr kHeapMemBeg    = 0x7c00000000ull;
  static const uptr kHeapMemEnd    = 0x7d00000000ull;
  static const uptr kHiAppMemBeg   = 0x7e00000000ull;
  static const uptr kHiAppMemEnd   = 0x7fffffffffull;
  static const uptr kAppMemMsk     = 0x7800000000ull;
  static const uptr kAppMemXor     = 0x0200000000ull;
  static const uptr kVdsoBeg       = 0x7f00000000ull;
};

/*
C/C++ on linux/aarch64 (42-bit VMA)
00000 0010 00 - 01000 0000 00: main binary
01000 0000 00 - 10000 0000 00: -
10000 0000 00 - 20000 0000 00: shadow memory
20000 0000 00 - 26000 0000 00: -
26000 0000 00 - 28000 0000 00: metainfo
28000 0000 00 - 2aa00 0000 00: -
2aa00 0000 00 - 2ab00 0000 00: main binary (PIE)
2ab00 0000 00 - 36200 0000 00: -
36200 0000 00 - 36240 0000 00: traces
36240 0000 00 - 3e000 0000 00: -
3e000 0000 00 - 3f000 0000 00: heap
3f000 0000 00 - 3ffff ffff ff: modules and main thread stack
*/
struct Mapping42 {
  static const uptr kLoAppMemBeg   = 0x00000001000ull;
  static const uptr kLoAppMemEnd   = 0x01000000000ull;
  static const uptr kShadowBeg     = 0x10000000000ull;
  static const uptr kShadowEnd     = 0x20000000000ull;
  static const uptr kMetaShadowBeg = 0x26000000000ull;
  static const uptr kMetaShadowEnd = 0x28000000000ull;
  static const uptr kMidAppMemBeg  = 0x2aa00000000ull;
  static const uptr kMidAppMemEnd  = 0x2ab00000000ull;
  static const uptr kTraceMemBeg   = 0x36200000000ull;
  static const uptr kTraceMemEnd   = 0x36400000000ull;
  static const uptr kHeapMemBeg    = 0x3e000000000ull;
  static const uptr kHeapMemEnd    = 0x3f000000000ull;
  static const uptr kHiAppMemBeg   = 0x3f000000000ull;
  static const uptr kHiAppMemEnd   = 0x3ffffffffffull;
  static const uptr kAppMemMsk     = 0x3c000000000ull;
  static const uptr kAppMemXor     = 0x04000000000ull;
  static const uptr kVdsoBeg       = 0x37f00000000ull;
};

struct Mapping48 {
  static const uptr kLoAppMemBeg   = 0x0000000001000ull;
  static const uptr kLoAppMemEnd   = 0x0000200000000ull;
  static const uptr kShadowBeg     = 0x0002000000000ull;
  static const uptr kShadowEnd     = 0x0004000000000ull;
  static const uptr kMetaShadowBeg = 0x0005000000000ull;
  static const uptr kMetaShadowEnd = 0x0006000000000ull;
  static const uptr kMidAppMemBeg  = 0x0aaaa00000000ull;
  static const uptr kMidAppMemEnd  = 0x0aaaf00000000ull;
  static const uptr kTraceMemBeg   = 0x0f06000000000ull;
  static const uptr kTraceMemEnd   = 0x0f06200000000ull;
  static const uptr kHeapMemBeg    = 0x0ffff00000000ull;
  static const uptr kHeapMemEnd    = 0x0ffff00000000ull;
  static const uptr kHiAppMemBeg   = 0x0ffff00000000ull;
  static const uptr kHiAppMemEnd   = 0x1000000000000ull;
  static const uptr kAppMemMsk     = 0x0fff800000000ull;
  static const uptr kAppMemXor     = 0x0000800000000ull;
  static const uptr kVdsoBeg       = 0xffff000000000ull;
};

// Indicates the runtime will define the memory regions at runtime.
#define TSAN_RUNTIME_VMA 1
// Indicates that mapping defines a mid range memory segment.
#define TSAN_MID_APP_RANGE 1
#elif defined(__powerpc64__)
// PPC64 supports multiple VMA which leads to multiple address transformation
// functions.  To support these multiple VMAS transformations and mappings TSAN
// runtime for PPC64 uses an external memory read (vmaSize) to select which
// mapping to use.  Although slower, it make a same instrumented binary run on
// multiple kernels.

/*
C/C++ on linux/powerpc64 (44-bit VMA)
0000 0000 0100 - 0001 0000 0000: main binary
0001 0000 0000 - 0001 0000 0000: -
0001 0000 0000 - 0b00 0000 0000: shadow
0b00 0000 0000 - 0b00 0000 0000: -
0b00 0000 0000 - 0d00 0000 0000: metainfo (memory blocks and sync objects)
0d00 0000 0000 - 0d00 0000 0000: -
0d00 0000 0000 - 0f00 0000 0000: traces
0f00 0000 0000 - 0f00 0000 0000: -
0f00 0000 0000 - 0f50 0000 0000: heap
0f50 0000 0000 - 0f60 0000 0000: -
0f60 0000 0000 - 1000 0000 0000: modules and main thread stack
*/
struct Mapping44 {
  static const uptr kMetaShadowBeg = 0x0b0000000000ull;
  static const uptr kMetaShadowEnd = 0x0d0000000000ull;
  static const uptr kTraceMemBeg   = 0x0d0000000000ull;
  static const uptr kTraceMemEnd   = 0x0f0000000000ull;
  static const uptr kShadowBeg     = 0x000100000000ull;
  static const uptr kShadowEnd     = 0x0b0000000000ull;
  static const uptr kLoAppMemBeg   = 0x000000000100ull;
  static const uptr kLoAppMemEnd   = 0x000100000000ull;
  static const uptr kHeapMemBeg    = 0x0f0000000000ull;
  static const uptr kHeapMemEnd    = 0x0f5000000000ull;
  static const uptr kHiAppMemBeg   = 0x0f6000000000ull;
  static const uptr kHiAppMemEnd   = 0x100000000000ull; // 44 bits
  static const uptr kAppMemMsk     = 0x0f0000000000ull;
  static const uptr kAppMemXor     = 0x002100000000ull;
  static const uptr kVdsoBeg       = 0x3c0000000000000ull;
};

/*
C/C++ on linux/powerpc64 (46-bit VMA)
0000 0000 1000 - 0100 0000 0000: main binary
0100 0000 0000 - 0200 0000 0000: -
0100 0000 0000 - 1000 0000 0000: shadow
1000 0000 0000 - 1000 0000 0000: -
1000 0000 0000 - 2000 0000 0000: metainfo (memory blocks and sync objects)
2000 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2200 0000 0000: traces
2200 0000 0000 - 3d00 0000 0000: -
3d00 0000 0000 - 3e00 0000 0000: heap
3e00 0000 0000 - 3e80 0000 0000: -
3e80 0000 0000 - 4000 0000 0000: modules and main thread stack
*/
struct Mapping46 {
  static const uptr kMetaShadowBeg = 0x100000000000ull;
  static const uptr kMetaShadowEnd = 0x200000000000ull;
  static const uptr kTraceMemBeg   = 0x200000000000ull;
  static const uptr kTraceMemEnd   = 0x220000000000ull;
  static const uptr kShadowBeg     = 0x010000000000ull;
  static const uptr kShadowEnd     = 0x100000000000ull;
  static const uptr kHeapMemBeg    = 0x3d0000000000ull;
  static const uptr kHeapMemEnd    = 0x3e0000000000ull;
  static const uptr kLoAppMemBeg   = 0x000000001000ull;
  static const uptr kLoAppMemEnd   = 0x010000000000ull;
  static const uptr kHiAppMemBeg   = 0x3e8000000000ull;
  static const uptr kHiAppMemEnd   = 0x400000000000ull; // 46 bits
  static const uptr kAppMemMsk     = 0x3c0000000000ull;
  static const uptr kAppMemXor     = 0x020000000000ull;
  static const uptr kVdsoBeg       = 0x7800000000000000ull;
};

/*
C/C++ on linux/powerpc64 (47-bit VMA)
0000 0000 1000 - 0100 0000 0000: main binary
0100 0000 0000 - 0200 0000 0000: -
0100 0000 0000 - 1000 0000 0000: shadow
1000 0000 0000 - 1000 0000 0000: -
1000 0000 0000 - 2000 0000 0000: metainfo (memory blocks and sync objects)
2000 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2200 0000 0000: traces
2200 0000 0000 - 7d00 0000 0000: -
7d00 0000 0000 - 7e00 0000 0000: heap
7e00 0000 0000 - 7e80 0000 0000: -
7e80 0000 0000 - 8000 0000 0000: modules and main thread stack
*/
struct Mapping47 {
  static const uptr kMetaShadowBeg = 0x100000000000ull;
  static const uptr kMetaShadowEnd = 0x200000000000ull;
  static const uptr kTraceMemBeg   = 0x200000000000ull;
  static const uptr kTraceMemEnd   = 0x220000000000ull;
  static const uptr kShadowBeg     = 0x010000000000ull;
  static const uptr kShadowEnd     = 0x100000000000ull;
  static const uptr kHeapMemBeg    = 0x7d0000000000ull;
  static const uptr kHeapMemEnd    = 0x7e0000000000ull;
  static const uptr kLoAppMemBeg   = 0x000000001000ull;
  static const uptr kLoAppMemEnd   = 0x010000000000ull;
  static const uptr kHiAppMemBeg   = 0x7e8000000000ull;
  static const uptr kHiAppMemEnd   = 0x800000000000ull; // 47 bits
  static const uptr kAppMemMsk     = 0x7c0000000000ull;
  static const uptr kAppMemXor     = 0x020000000000ull;
  static const uptr kVdsoBeg       = 0x7800000000000000ull;
};

// Indicates the runtime will define the memory regions at runtime.
#define TSAN_RUNTIME_VMA 1
#endif

#elif SANITIZER_GO && !SANITIZER_WINDOWS && defined(__x86_64__)

/* Go on linux, darwin and freebsd on x86_64
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2380 0000 0000: shadow
2380 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 4000 0000 0000: metainfo (memory blocks and sync objects)
4000 0000 0000 - 6000 0000 0000: -
6000 0000 0000 - 6200 0000 0000: traces
6200 0000 0000 - 8000 0000 0000: -
*/

struct Mapping {
  static const uptr kMetaShadowBeg = 0x300000000000ull;
  static const uptr kMetaShadowEnd = 0x400000000000ull;
  static const uptr kTraceMemBeg   = 0x600000000000ull;
  static const uptr kTraceMemEnd   = 0x620000000000ull;
  static const uptr kShadowBeg     = 0x200000000000ull;
  static const uptr kShadowEnd     = 0x238000000000ull;
  static const uptr kAppMemBeg     = 0x000000001000ull;
  static const uptr kAppMemEnd     = 0x00e000000000ull;
};

#elif SANITIZER_GO && SANITIZER_WINDOWS

/* Go on windows
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00f8 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 0100 0000 0000: -
0100 0000 0000 - 0500 0000 0000: shadow
0500 0000 0000 - 0560 0000 0000: -
0560 0000 0000 - 0760 0000 0000: traces
0760 0000 0000 - 07d0 0000 0000: metainfo (memory blocks and sync objects)
07d0 0000 0000 - 8000 0000 0000: -
*/

struct Mapping {
  static const uptr kMetaShadowBeg = 0x076000000000ull;
  static const uptr kMetaShadowEnd = 0x07d000000000ull;
  static const uptr kTraceMemBeg   = 0x056000000000ull;
  static const uptr kTraceMemEnd   = 0x076000000000ull;
  static const uptr kShadowBeg     = 0x010000000000ull;
  static const uptr kShadowEnd     = 0x050000000000ull;
  static const uptr kAppMemBeg     = 0x000000001000ull;
  static const uptr kAppMemEnd     = 0x00e000000000ull;
};

#elif SANITIZER_GO && defined(__powerpc64__)

/* Only Mapping46 and Mapping47 are currently supported for powercp64 on Go. */

/* Go on linux/powerpc64 (46-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2380 0000 0000: shadow
2380 0000 0000 - 2400 0000 0000: -
2400 0000 0000 - 3400 0000 0000: metainfo (memory blocks and sync objects)
3400 0000 0000 - 3600 0000 0000: -
3600 0000 0000 - 3800 0000 0000: traces
3800 0000 0000 - 4000 0000 0000: -
*/

struct Mapping46 {
  static const uptr kMetaShadowBeg = 0x240000000000ull;
  static const uptr kMetaShadowEnd = 0x340000000000ull;
  static const uptr kTraceMemBeg   = 0x360000000000ull;
  static const uptr kTraceMemEnd   = 0x380000000000ull;
  static const uptr kShadowBeg     = 0x200000000000ull;
  static const uptr kShadowEnd     = 0x238000000000ull;
  static const uptr kAppMemBeg     = 0x000000001000ull;
  static const uptr kAppMemEnd     = 0x00e000000000ull;
};

/* Go on linux/powerpc64 (47-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 3000 0000 0000: shadow
3000 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 4000 0000 0000: metainfo (memory blocks and sync objects)
4000 0000 0000 - 6000 0000 0000: -
6000 0000 0000 - 6200 0000 0000: traces
6200 0000 0000 - 8000 0000 0000: -
*/

struct Mapping47 {
  static const uptr kMetaShadowBeg = 0x300000000000ull;
  static const uptr kMetaShadowEnd = 0x400000000000ull;
  static const uptr kTraceMemBeg   = 0x600000000000ull;
  static const uptr kTraceMemEnd   = 0x620000000000ull;
  static const uptr kShadowBeg     = 0x200000000000ull;
  static const uptr kShadowEnd     = 0x300000000000ull;
  static const uptr kAppMemBeg     = 0x000000001000ull;
  static const uptr kAppMemEnd     = 0x00e000000000ull;
};

#elif SANITIZER_GO && defined(__aarch64__)

/* Go on linux/aarch64 (48-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 3000 0000 0000: shadow
3000 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 4000 0000 0000: metainfo (memory blocks and sync objects)
4000 0000 0000 - 6000 0000 0000: -
6000 0000 0000 - 6200 0000 0000: traces
6200 0000 0000 - 8000 0000 0000: -
*/

struct Mapping {
  static const uptr kMetaShadowBeg = 0x300000000000ull;
  static const uptr kMetaShadowEnd = 0x400000000000ull;
  static const uptr kTraceMemBeg   = 0x600000000000ull;
  static const uptr kTraceMemEnd   = 0x620000000000ull;
  static const uptr kShadowBeg     = 0x200000000000ull;
  static const uptr kShadowEnd     = 0x300000000000ull;
  static const uptr kAppMemBeg     = 0x000000001000ull;
  static const uptr kAppMemEnd     = 0x00e000000000ull;
};

// Indicates the runtime will define the memory regions at runtime.
#define TSAN_RUNTIME_VMA 1

#else
# error "Unknown platform"
#endif


#ifdef TSAN_RUNTIME_VMA
extern uptr vmaSize;
#endif


enum MappingType {
  MAPPING_LO_APP_BEG,
  MAPPING_LO_APP_END,
  MAPPING_HI_APP_BEG,
  MAPPING_HI_APP_END,
#ifdef TSAN_MID_APP_RANGE
  MAPPING_MID_APP_BEG,
  MAPPING_MID_APP_END,
#endif
  MAPPING_HEAP_BEG,
  MAPPING_HEAP_END,
  MAPPING_APP_BEG,
  MAPPING_APP_END,
  MAPPING_SHADOW_BEG,
  MAPPING_SHADOW_END,
  MAPPING_META_SHADOW_BEG,
  MAPPING_META_SHADOW_END,
  MAPPING_TRACE_BEG,
  MAPPING_TRACE_END,
  MAPPING_VDSO_BEG,
};

template<typename Mapping, int Type>
uptr MappingImpl(void) {
  switch (Type) {
#if !SANITIZER_GO
    case MAPPING_LO_APP_BEG: return Mapping::kLoAppMemBeg;
    case MAPPING_LO_APP_END: return Mapping::kLoAppMemEnd;
# ifdef TSAN_MID_APP_RANGE
    case MAPPING_MID_APP_BEG: return Mapping::kMidAppMemBeg;
    case MAPPING_MID_APP_END: return Mapping::kMidAppMemEnd;
# endif
    case MAPPING_HI_APP_BEG: return Mapping::kHiAppMemBeg;
    case MAPPING_HI_APP_END: return Mapping::kHiAppMemEnd;
    case MAPPING_HEAP_BEG: return Mapping::kHeapMemBeg;
    case MAPPING_HEAP_END: return Mapping::kHeapMemEnd;
    case MAPPING_VDSO_BEG: return Mapping::kVdsoBeg;
#else
    case MAPPING_APP_BEG: return Mapping::kAppMemBeg;
    case MAPPING_APP_END: return Mapping::kAppMemEnd;
#endif
    case MAPPING_SHADOW_BEG: return Mapping::kShadowBeg;
    case MAPPING_SHADOW_END: return Mapping::kShadowEnd;
    case MAPPING_META_SHADOW_BEG: return Mapping::kMetaShadowBeg;
    case MAPPING_META_SHADOW_END: return Mapping::kMetaShadowEnd;
    case MAPPING_TRACE_BEG: return Mapping::kTraceMemBeg;
    case MAPPING_TRACE_END: return Mapping::kTraceMemEnd;
  }
}

template<int Type>
uptr MappingArchImpl(void) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return MappingImpl<Mapping39, Type>();
    case 42: return MappingImpl<Mapping42, Type>();
    case 48: return MappingImpl<Mapping48, Type>();
  }
  DCHECK(0);
  return 0;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return MappingImpl<Mapping44, Type>();
#endif
    case 46: return MappingImpl<Mapping46, Type>();
    case 47: return MappingImpl<Mapping47, Type>();
  }
  DCHECK(0);
  return 0;
#else
  return MappingImpl<Mapping, Type>();
#endif
}

#if !SANITIZER_GO
ALWAYS_INLINE
uptr LoAppMemBeg(void) {
  return MappingArchImpl<MAPPING_LO_APP_BEG>();
}
ALWAYS_INLINE
uptr LoAppMemEnd(void) {
  return MappingArchImpl<MAPPING_LO_APP_END>();
}

#ifdef TSAN_MID_APP_RANGE
ALWAYS_INLINE
uptr MidAppMemBeg(void) {
  return MappingArchImpl<MAPPING_MID_APP_BEG>();
}
ALWAYS_INLINE
uptr MidAppMemEnd(void) {
  return MappingArchImpl<MAPPING_MID_APP_END>();
}
#endif

ALWAYS_INLINE
uptr HeapMemBeg(void) {
  return MappingArchImpl<MAPPING_HEAP_BEG>();
}
ALWAYS_INLINE
uptr HeapMemEnd(void) {
  return MappingArchImpl<MAPPING_HEAP_END>();
}

ALWAYS_INLINE
uptr HiAppMemBeg(void) {
  return MappingArchImpl<MAPPING_HI_APP_BEG>();
}
ALWAYS_INLINE
uptr HiAppMemEnd(void) {
  return MappingArchImpl<MAPPING_HI_APP_END>();
}

ALWAYS_INLINE
uptr VdsoBeg(void) {
  return MappingArchImpl<MAPPING_VDSO_BEG>();
}

#else

ALWAYS_INLINE
uptr AppMemBeg(void) {
  return MappingArchImpl<MAPPING_APP_BEG>();
}
ALWAYS_INLINE
uptr AppMemEnd(void) {
  return MappingArchImpl<MAPPING_APP_END>();
}

#endif

static inline
bool GetUserRegion(int i, uptr *start, uptr *end) {
  switch (i) {
  default:
    return false;
#if !SANITIZER_GO
  case 0:
    *start = LoAppMemBeg();
    *end = LoAppMemEnd();
    return true;
  case 1:
    *start = HiAppMemBeg();
    *end = HiAppMemEnd();
    return true;
  case 2:
    *start = HeapMemBeg();
    *end = HeapMemEnd();
    return true;
# ifdef TSAN_MID_APP_RANGE
  case 3:
    *start = MidAppMemBeg();
    *end = MidAppMemEnd();
    return true;
# endif
#else
  case 0:
    *start = AppMemBeg();
    *end = AppMemEnd();
    return true;
#endif
  }
}

ALWAYS_INLINE
uptr ShadowBeg(void) {
  return MappingArchImpl<MAPPING_SHADOW_BEG>();
}
ALWAYS_INLINE
uptr ShadowEnd(void) {
  return MappingArchImpl<MAPPING_SHADOW_END>();
}

ALWAYS_INLINE
uptr MetaShadowBeg(void) {
  return MappingArchImpl<MAPPING_META_SHADOW_BEG>();
}
ALWAYS_INLINE
uptr MetaShadowEnd(void) {
  return MappingArchImpl<MAPPING_META_SHADOW_END>();
}

ALWAYS_INLINE
uptr TraceMemBeg(void) {
  return MappingArchImpl<MAPPING_TRACE_BEG>();
}
ALWAYS_INLINE
uptr TraceMemEnd(void) {
  return MappingArchImpl<MAPPING_TRACE_END>();
}


template<typename Mapping>
bool IsAppMemImpl(uptr mem) {
#if !SANITIZER_GO
  return (mem >= Mapping::kHeapMemBeg && mem < Mapping::kHeapMemEnd) ||
# ifdef TSAN_MID_APP_RANGE
         (mem >= Mapping::kMidAppMemBeg && mem < Mapping::kMidAppMemEnd) ||
# endif
         (mem >= Mapping::kLoAppMemBeg && mem < Mapping::kLoAppMemEnd) ||
         (mem >= Mapping::kHiAppMemBeg && mem < Mapping::kHiAppMemEnd);
#else
  return mem >= Mapping::kAppMemBeg && mem < Mapping::kAppMemEnd;
#endif
}

ALWAYS_INLINE
bool IsAppMem(uptr mem) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return IsAppMemImpl<Mapping39>(mem);
    case 42: return IsAppMemImpl<Mapping42>(mem);
    case 48: return IsAppMemImpl<Mapping48>(mem);
  }
  DCHECK(0);
  return false;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return IsAppMemImpl<Mapping44>(mem);
#endif
    case 46: return IsAppMemImpl<Mapping46>(mem);
    case 47: return IsAppMemImpl<Mapping47>(mem);
  }
  DCHECK(0);
  return false;
#else
  return IsAppMemImpl<Mapping>(mem);
#endif
}


template<typename Mapping>
bool IsShadowMemImpl(uptr mem) {
  return mem >= Mapping::kShadowBeg && mem <= Mapping::kShadowEnd;
}

ALWAYS_INLINE
bool IsShadowMem(uptr mem) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return IsShadowMemImpl<Mapping39>(mem);
    case 42: return IsShadowMemImpl<Mapping42>(mem);
    case 48: return IsShadowMemImpl<Mapping48>(mem);
  }
  DCHECK(0);
  return false;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return IsShadowMemImpl<Mapping44>(mem);
#endif
    case 46: return IsShadowMemImpl<Mapping46>(mem);
    case 47: return IsShadowMemImpl<Mapping47>(mem);
  }
  DCHECK(0);
  return false;
#else
  return IsShadowMemImpl<Mapping>(mem);
#endif
}


template<typename Mapping>
bool IsMetaMemImpl(uptr mem) {
  return mem >= Mapping::kMetaShadowBeg && mem <= Mapping::kMetaShadowEnd;
}

ALWAYS_INLINE
bool IsMetaMem(uptr mem) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return IsMetaMemImpl<Mapping39>(mem);
    case 42: return IsMetaMemImpl<Mapping42>(mem);
    case 48: return IsMetaMemImpl<Mapping48>(mem);
  }
  DCHECK(0);
  return false;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return IsMetaMemImpl<Mapping44>(mem);
#endif
    case 46: return IsMetaMemImpl<Mapping46>(mem);
    case 47: return IsMetaMemImpl<Mapping47>(mem);
  }
  DCHECK(0);
  return false;
#else
  return IsMetaMemImpl<Mapping>(mem);
#endif
}


template<typename Mapping>
uptr MemToShadowImpl(uptr x) {
  DCHECK(IsAppMem(x));
#if !SANITIZER_GO
  return (((x) & ~(Mapping::kAppMemMsk | (kShadowCell - 1)))
      ^ Mapping::kAppMemXor) * kShadowCnt;
#else
# ifndef SANITIZER_WINDOWS
  return ((x & ~(kShadowCell - 1)) * kShadowCnt) | Mapping::kShadowBeg;
# else
  return ((x & ~(kShadowCell - 1)) * kShadowCnt) + Mapping::kShadowBeg;
# endif
#endif
}

ALWAYS_INLINE
uptr MemToShadow(uptr x) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return MemToShadowImpl<Mapping39>(x);
    case 42: return MemToShadowImpl<Mapping42>(x);
    case 48: return MemToShadowImpl<Mapping48>(x);
  }
  DCHECK(0);
  return 0;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return MemToShadowImpl<Mapping44>(x);
#endif
    case 46: return MemToShadowImpl<Mapping46>(x);
    case 47: return MemToShadowImpl<Mapping47>(x);
  }
  DCHECK(0);
  return 0;
#else
  return MemToShadowImpl<Mapping>(x);
#endif
}


template<typename Mapping>
u32 *MemToMetaImpl(uptr x) {
  DCHECK(IsAppMem(x));
#if !SANITIZER_GO
  return (u32*)(((((x) & ~(Mapping::kAppMemMsk | (kMetaShadowCell - 1)))) /
      kMetaShadowCell * kMetaShadowSize) | Mapping::kMetaShadowBeg);
#else
# ifndef SANITIZER_WINDOWS
  return (u32*)(((x & ~(kMetaShadowCell - 1)) / \
      kMetaShadowCell * kMetaShadowSize) | Mapping::kMetaShadowBeg);
# else
  return (u32*)(((x & ~(kMetaShadowCell - 1)) / \
      kMetaShadowCell * kMetaShadowSize) + Mapping::kMetaShadowBeg);
# endif
#endif
}

ALWAYS_INLINE
u32 *MemToMeta(uptr x) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return MemToMetaImpl<Mapping39>(x);
    case 42: return MemToMetaImpl<Mapping42>(x);
    case 48: return MemToMetaImpl<Mapping48>(x);
  }
  DCHECK(0);
  return 0;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return MemToMetaImpl<Mapping44>(x);
#endif
    case 46: return MemToMetaImpl<Mapping46>(x);
    case 47: return MemToMetaImpl<Mapping47>(x);
  }
  DCHECK(0);
  return 0;
#else
  return MemToMetaImpl<Mapping>(x);
#endif
}


template<typename Mapping>
uptr ShadowToMemImpl(uptr s) {
  DCHECK(IsShadowMem(s));
#if !SANITIZER_GO
  // The shadow mapping is non-linear and we've lost some bits, so we don't have
  // an easy way to restore the original app address. But the mapping is a
  // bijection, so we try to restore the address as belonging to low/mid/high
  // range consecutively and see if shadow->app->shadow mapping gives us the
  // same address.
  uptr p = (s / kShadowCnt) ^ Mapping::kAppMemXor;
  if (p >= Mapping::kLoAppMemBeg && p < Mapping::kLoAppMemEnd &&
      MemToShadow(p) == s)
    return p;
# ifdef TSAN_MID_APP_RANGE
  p = ((s / kShadowCnt) ^ Mapping::kAppMemXor) +
      (Mapping::kMidAppMemBeg & Mapping::kAppMemMsk);
  if (p >= Mapping::kMidAppMemBeg && p < Mapping::kMidAppMemEnd &&
      MemToShadow(p) == s)
    return p;
# endif
  return ((s / kShadowCnt) ^ Mapping::kAppMemXor) | Mapping::kAppMemMsk;
#else  // #if !SANITIZER_GO
# ifndef SANITIZER_WINDOWS
  return (s & ~Mapping::kShadowBeg) / kShadowCnt;
# else
  return (s - Mapping::kShadowBeg) / kShadowCnt;
# endif // SANITIZER_WINDOWS
#endif
}

ALWAYS_INLINE
uptr ShadowToMem(uptr s) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return ShadowToMemImpl<Mapping39>(s);
    case 42: return ShadowToMemImpl<Mapping42>(s);
    case 48: return ShadowToMemImpl<Mapping48>(s);
  }
  DCHECK(0);
  return 0;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return ShadowToMemImpl<Mapping44>(s);
#endif
    case 46: return ShadowToMemImpl<Mapping46>(s);
    case 47: return ShadowToMemImpl<Mapping47>(s);
  }
  DCHECK(0);
  return 0;
#else
  return ShadowToMemImpl<Mapping>(s);
#endif
}



// The additional page is to catch shadow stack overflow as paging fault.
// Windows wants 64K alignment for mmaps.
const uptr kTotalTraceSize = (kTraceSize * sizeof(Event) + sizeof(Trace)
    + (64 << 10) + (64 << 10) - 1) & ~((64 << 10) - 1);

template<typename Mapping>
uptr GetThreadTraceImpl(int tid) {
  uptr p = Mapping::kTraceMemBeg + (uptr)tid * kTotalTraceSize;
  DCHECK_LT(p, Mapping::kTraceMemEnd);
  return p;
}

ALWAYS_INLINE
uptr GetThreadTrace(int tid) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return GetThreadTraceImpl<Mapping39>(tid);
    case 42: return GetThreadTraceImpl<Mapping42>(tid);
    case 48: return GetThreadTraceImpl<Mapping48>(tid);
  }
  DCHECK(0);
  return 0;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return GetThreadTraceImpl<Mapping44>(tid);
#endif
    case 46: return GetThreadTraceImpl<Mapping46>(tid);
    case 47: return GetThreadTraceImpl<Mapping47>(tid);
  }
  DCHECK(0);
  return 0;
#else
  return GetThreadTraceImpl<Mapping>(tid);
#endif
}


template<typename Mapping>
uptr GetThreadTraceHeaderImpl(int tid) {
  uptr p = Mapping::kTraceMemBeg + (uptr)tid * kTotalTraceSize
      + kTraceSize * sizeof(Event);
  DCHECK_LT(p, Mapping::kTraceMemEnd);
  return p;
}

ALWAYS_INLINE
uptr GetThreadTraceHeader(int tid) {
#if defined(__aarch64__) && !defined(__APPLE__) && !SANITIZER_GO
  switch (vmaSize) {
    case 39: return GetThreadTraceHeaderImpl<Mapping39>(tid);
    case 42: return GetThreadTraceHeaderImpl<Mapping42>(tid);
    case 48: return GetThreadTraceHeaderImpl<Mapping48>(tid);
  }
  DCHECK(0);
  return 0;
#elif defined(__powerpc64__)
  switch (vmaSize) {
#if !SANITIZER_GO
    case 44: return GetThreadTraceHeaderImpl<Mapping44>(tid);
#endif
    case 46: return GetThreadTraceHeaderImpl<Mapping46>(tid);
    case 47: return GetThreadTraceHeaderImpl<Mapping47>(tid);
  }
  DCHECK(0);
  return 0;
#else
  return GetThreadTraceHeaderImpl<Mapping>(tid);
#endif
}

void InitializePlatform();
void InitializePlatformEarly();
void CheckAndProtect();
void InitializeShadowMemoryPlatform();
void FlushShadowMemory();
void WriteMemoryProfile(char *buf, uptr buf_size, uptr nthread, uptr nlive);
int ExtractResolvFDs(void *state, int *fds, int nfd);
int ExtractRecvmsgFDs(void *msg, int *fds, int nfd);
void ImitateTlsWrite(ThreadState *thr, uptr tls_addr, uptr tls_size);

int call_pthread_cancel_with_cleanup(int(*fn)(void *c, void *m,
    void *abstime), void *c, void *m, void *abstime,
    void(*cleanup)(void *arg), void *arg);

void DestroyThreadState();

}  // namespace __tsan

#endif  // TSAN_PLATFORM_H
