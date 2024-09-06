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

#endif /* CONFIG_GENERIC_GETTIMEOFDAY */

#endif /* _ASM_X86_VGTOD_H */
