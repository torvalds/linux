/*
 * Copyright (C) 2018 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_NEON_INTRINSICS_H
#define __ASM_NEON_INTRINSICS_H

#include <asm-generic/int-ll64.h>

/*
 * In the kernel, u64/s64 are [un]signed long long, not [un]signed long.
 * So by redefining these macros to the former, we can force gcc-stdint.h
 * to define uint64_t / in64_t in a compatible manner.
 */

#ifdef __INT64_TYPE__
#undef __INT64_TYPE__
#define __INT64_TYPE__		long long
#endif

#ifdef __UINT64_TYPE__
#undef __UINT64_TYPE__
#define __UINT64_TYPE__		unsigned long long
#endif

/*
 * genksyms chokes on the ARM NEON instrinsics system header, but we
 * don't export anything it defines anyway, so just disregard when
 * genksyms execute.
 */
#ifndef __GENKSYMS__
#include <arm_neon.h>
#endif

#ifdef CONFIG_CC_IS_CLANG
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif

#endif /* __ASM_NEON_INTRINSICS_H */
