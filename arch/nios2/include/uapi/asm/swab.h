/*
 * Copyright (C) 2012 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2011 Pyramid Technical Consultants, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_NIOS2_SWAB_H
#define _ASM_NIOS2_SWAB_H

#include <linux/types.h>
#include <asm-generic/swab.h>

#ifdef CONFIG_NIOS2_CI_SWAB_SUPPORT
#ifdef __GNUC__

#define __nios2_swab(x)		\
	__builtin_custom_ini(CONFIG_NIOS2_CI_SWAB_NO, (x))

static inline __attribute__((const)) __u16 __arch_swab16(__u16 x)
{
	return (__u16) __nios2_swab(((__u32) x) << 16);
}
#define __arch_swab16 __arch_swab16

static inline __attribute__((const)) __u32 __arch_swab32(__u32 x)
{
	return (__u32) __nios2_swab(x);
}
#define __arch_swab32 __arch_swab32

#endif /* __GNUC__ */
#endif /* CONFIG_NIOS2_CI_SWAB_SUPPORT */

#endif /* _ASM_NIOS2_SWAB_H */
