//===-- lldb-defines.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_lldb_defines_h_
#define LLDB_lldb_defines_h_

#include "lldb/lldb-types.h"

#if defined(_WIN32)
#if defined(EXPORT_LIBLLDB)
#define LLDB_API __declspec(dllexport)
#elif defined(IMPORT_LIBLLDB)
#define LLDB_API __declspec(dllimport)
#else
#define LLDB_API
#endif
#else // defined (_WIN32)
#define LLDB_API
#endif

#if !defined(INT32_MAX)
#define INT32_MAX 2147483647
#endif

#if !defined(UINT32_MAX)
#define UINT32_MAX 4294967295U
#endif

#if !defined(UINT64_MAX)
#define UINT64_MAX 18446744073709551615ULL
#endif

//----------------------------------------------------------------------
// LLDB version
//
// A build script phase can modify this version number if needed.
//----------------------------------------------------------------------
//#define LLDB_VERSION
//#define LLDB_REVISION
//#define LLDB_VERSION_STRING

//----------------------------------------------------------------------
// LLDB defines
//----------------------------------------------------------------------
#define LLDB_GENERIC_ERROR UINT32_MAX

//----------------------------------------------------------------------
// Breakpoints
//----------------------------------------------------------------------
#define LLDB_INVALID_BREAK_ID 0
#define LLDB_DEFAULT_BREAK_SIZE 0
#define LLDB_BREAK_ID_IS_VALID(bid) ((bid) != (LLDB_INVALID_BREAK_ID))
#define LLDB_BREAK_ID_IS_INTERNAL(bid) ((bid) < 0)

//----------------------------------------------------------------------
// Watchpoints
//----------------------------------------------------------------------
#define LLDB_INVALID_WATCH_ID 0
#define LLDB_WATCH_ID_IS_VALID(uid) ((uid) != (LLDB_INVALID_WATCH_ID))
#define LLDB_WATCH_TYPE_READ (1u << 0)
#define LLDB_WATCH_TYPE_WRITE (1u << 1)
#define LLDB_WATCH_TYPE_IS_VALID(type)                                         \
  ((type | LLDB_WATCH_TYPE_READ) || (type | LLDB_WATCH_TYPE_WRITE))

//----------------------------------------------------------------------
// Generic Register Numbers
//----------------------------------------------------------------------
#define LLDB_REGNUM_GENERIC_PC 0    // Program Counter
#define LLDB_REGNUM_GENERIC_SP 1    // Stack Pointer
#define LLDB_REGNUM_GENERIC_FP 2    // Frame Pointer
#define LLDB_REGNUM_GENERIC_RA 3    // Return Address
#define LLDB_REGNUM_GENERIC_FLAGS 4 // Processor flags register
#define LLDB_REGNUM_GENERIC_ARG1                                               \
  5 // The register that would contain pointer size or less argument 1 (if any)
#define LLDB_REGNUM_GENERIC_ARG2                                               \
  6 // The register that would contain pointer size or less argument 2 (if any)
#define LLDB_REGNUM_GENERIC_ARG3                                               \
  7 // The register that would contain pointer size or less argument 3 (if any)
#define LLDB_REGNUM_GENERIC_ARG4                                               \
  8 // The register that would contain pointer size or less argument 4 (if any)
#define LLDB_REGNUM_GENERIC_ARG5                                               \
  9 // The register that would contain pointer size or less argument 5 (if any)
#define LLDB_REGNUM_GENERIC_ARG6                                               \
  10 // The register that would contain pointer size or less argument 6 (if any)
#define LLDB_REGNUM_GENERIC_ARG7                                               \
  11 // The register that would contain pointer size or less argument 7 (if any)
#define LLDB_REGNUM_GENERIC_ARG8                                               \
  12 // The register that would contain pointer size or less argument 8 (if any)
//---------------------------------------------------------------------
/// Invalid value definitions
//----------------------------------------------------------------------
#define LLDB_INVALID_ADDRESS UINT64_MAX
#define LLDB_INVALID_INDEX32 UINT32_MAX
#define LLDB_INVALID_IVAR_OFFSET UINT32_MAX
#define LLDB_INVALID_IMAGE_TOKEN UINT32_MAX
#define LLDB_INVALID_MODULE_VERSION UINT32_MAX
#define LLDB_INVALID_REGNUM UINT32_MAX
#define LLDB_INVALID_UID UINT64_MAX
#define LLDB_INVALID_PROCESS_ID 0
#define LLDB_INVALID_THREAD_ID 0
#define LLDB_INVALID_FRAME_ID UINT32_MAX
#define LLDB_INVALID_SIGNAL_NUMBER INT32_MAX
#define LLDB_INVALID_OFFSET UINT64_MAX // Must match max of lldb::offset_t
#define LLDB_INVALID_LINE_NUMBER UINT32_MAX
#define LLDB_INVALID_QUEUE_ID 0

//----------------------------------------------------------------------
/// CPU Type definitions
//----------------------------------------------------------------------
#define LLDB_ARCH_DEFAULT "systemArch"
#define LLDB_ARCH_DEFAULT_32BIT "systemArch32"
#define LLDB_ARCH_DEFAULT_64BIT "systemArch64"
#define LLDB_INVALID_CPUTYPE (0xFFFFFFFEu)

//----------------------------------------------------------------------
/// Option Set definitions
//----------------------------------------------------------------------
// FIXME: I'm sure there's some #define magic that can create all 32 sets on the
// fly.  That would have the added benefit of making this unreadable.
#define LLDB_MAX_NUM_OPTION_SETS 32
#define LLDB_OPT_SET_ALL 0xFFFFFFFFU
#define LLDB_OPT_SET_1 (1U << 0)
#define LLDB_OPT_SET_2 (1U << 1)
#define LLDB_OPT_SET_3 (1U << 2)
#define LLDB_OPT_SET_4 (1U << 3)
#define LLDB_OPT_SET_5 (1U << 4)
#define LLDB_OPT_SET_6 (1U << 5)
#define LLDB_OPT_SET_7 (1U << 6)
#define LLDB_OPT_SET_8 (1U << 7)
#define LLDB_OPT_SET_9 (1U << 8)
#define LLDB_OPT_SET_10 (1U << 9)
#define LLDB_OPT_SET_11 (1U << 10)
#define LLDB_OPT_SET_FROM_TO(A, B)                                             \
  (((1U << (B)) - 1) ^ (((1U << (A)) - 1) >> 1))

#if defined(_WIN32) && !defined(MAX_PATH)
#define MAX_PATH 260
#endif

// ignore GCC function attributes
#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(X)
#endif

#define UNUSED_IF_ASSERT_DISABLED(x) ((void)(x))

#if defined(__cplusplus)

//----------------------------------------------------------------------
/// @def DISALLOW_COPY_AND_ASSIGN(TypeName)
///     Macro definition for easily disallowing copy constructor and
///     assignment operators in C++ classes.
//----------------------------------------------------------------------
#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
  TypeName(const TypeName &) = delete;                                         \
  const TypeName &operator=(const TypeName &) = delete

#endif // #if defined(__cplusplus)

#endif // LLDB_lldb_defines_h_
