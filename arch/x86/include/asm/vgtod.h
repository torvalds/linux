/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VGTOD_H
#define _ASM_X86_VGTOD_H

/*
 * This check is required to prevent ARCH=um to include
 * unwanted headers.
 */
#ifdef CONFIG_GENERIC_GETTIMEOFDAY
#include <linux/compiler.h>
#include <asm/clocksource.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

#include <uapi/linux/time.h>

#ifdef BUILD_VDSO32_64
typedef u64 gtod_long_t;
#else
typedef unsigned long gtod_long_t;
#endif
#endif /* CONFIG_GENERIC_GETTIMEOFDAY */

#endif /* _ASM_X86_VGTOD_H */
