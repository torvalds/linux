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

#ifdef BUILD_VDSO32_64
typedef u64 gtod_long_t;
#else
typedef unsigned long gtod_long_t;
#endif
#endif /* CONFIG_GENERIC_GETTIMEOFDAY */

extern int vclocks_used;
static inline bool vclock_was_used(int vclock)
{
	return READ_ONCE(vclocks_used) & (1 << vclock);
}

#endif /* _ASM_X86_VGTOD_H */
