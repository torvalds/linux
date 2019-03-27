//===-- sanitizer_platform_limits_linux.cc --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer common code.
//
// Sizes and layouts of linux kernel data structures.
//===----------------------------------------------------------------------===//

// This is a separate compilation unit for linux headers that conflict with
// userspace headers.
// Most "normal" includes go in sanitizer_platform_limits_posix.cc

#include "sanitizer_platform.h"
#if SANITIZER_LINUX

#include "sanitizer_internal_defs.h"
#include "sanitizer_platform_limits_posix.h"

// For offsetof -> __builtin_offsetof definition.
#include <stddef.h>

// With old kernels (and even new kernels on powerpc) asm/stat.h uses types that
// are not defined anywhere in userspace headers. Fake them. This seems to work
// fine with newer headers, too.
#include <linux/posix_types.h>
#if defined(__x86_64__) ||  defined(__mips__)
#include <sys/stat.h>
#else
#define ino_t __kernel_ino_t
#define mode_t __kernel_mode_t
#define nlink_t __kernel_nlink_t
#define uid_t __kernel_uid_t
#define gid_t __kernel_gid_t
#define off_t __kernel_off_t
#define time_t __kernel_time_t
// This header seems to contain the definitions of _kernel_ stat* structs.
#include <asm/stat.h>
#undef ino_t
#undef mode_t
#undef nlink_t
#undef uid_t
#undef gid_t
#undef off_t
#endif

#include <linux/aio_abi.h>

#if !SANITIZER_ANDROID
#include <sys/statfs.h>
#include <linux/perf_event.h>
#endif

using namespace __sanitizer;

namespace __sanitizer {
#if !SANITIZER_ANDROID
  unsigned struct_statfs64_sz = sizeof(struct statfs64);
#endif
}  // namespace __sanitizer

#if !defined(__powerpc64__) && !defined(__x86_64__) && !defined(__aarch64__)\
                            && !defined(__mips__) && !defined(__s390__)\
                            && !defined(__sparc__)
COMPILER_CHECK(struct___old_kernel_stat_sz == sizeof(struct __old_kernel_stat));
#endif

COMPILER_CHECK(struct_kernel_stat_sz == sizeof(struct stat));

#if defined(__i386__)
COMPILER_CHECK(struct_kernel_stat64_sz == sizeof(struct stat64));
#endif

CHECK_TYPE_SIZE(io_event);
CHECK_SIZE_AND_OFFSET(io_event, data);
CHECK_SIZE_AND_OFFSET(io_event, obj);
CHECK_SIZE_AND_OFFSET(io_event, res);
CHECK_SIZE_AND_OFFSET(io_event, res2);

#if !SANITIZER_ANDROID
COMPILER_CHECK(sizeof(struct __sanitizer_perf_event_attr) <=
               sizeof(struct perf_event_attr));
CHECK_SIZE_AND_OFFSET(perf_event_attr, type);
CHECK_SIZE_AND_OFFSET(perf_event_attr, size);
#endif

COMPILER_CHECK(iocb_cmd_pread == IOCB_CMD_PREAD);
COMPILER_CHECK(iocb_cmd_pwrite == IOCB_CMD_PWRITE);
#if !SANITIZER_ANDROID
COMPILER_CHECK(iocb_cmd_preadv == IOCB_CMD_PREADV);
COMPILER_CHECK(iocb_cmd_pwritev == IOCB_CMD_PWRITEV);
#endif

CHECK_TYPE_SIZE(iocb);
CHECK_SIZE_AND_OFFSET(iocb, aio_data);
// Skip aio_key, it's weird.
CHECK_SIZE_AND_OFFSET(iocb, aio_lio_opcode);
CHECK_SIZE_AND_OFFSET(iocb, aio_reqprio);
CHECK_SIZE_AND_OFFSET(iocb, aio_fildes);
CHECK_SIZE_AND_OFFSET(iocb, aio_buf);
CHECK_SIZE_AND_OFFSET(iocb, aio_nbytes);
CHECK_SIZE_AND_OFFSET(iocb, aio_offset);

#endif  // SANITIZER_LINUX
