/*
 * linux/arch/unicore32/include/asm/bitops.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_BITOPS_H__
#define __UNICORE_BITOPS_H__

#define _ASM_GENERIC_BITOPS_FLS_H_
#define _ASM_GENERIC_BITOPS___FLS_H_
#define _ASM_GENERIC_BITOPS_FFS_H_
#define _ASM_GENERIC_BITOPS___FFS_H_
/*
 * On UNICORE, those functions can be implemented around
 * the cntlz instruction for much better code efficiency.
 */

static inline int fls(int x)
{
	int ret;

	asm("cntlz\t%0, %1" : "=r" (ret) : "r" (x) : "cc");
	ret = 32 - ret;

	return ret;
}

#define __fls(x) (fls(x) - 1)
#define ffs(x) ({ unsigned long __t = (x); fls(__t & -__t); })
#define __ffs(x) (ffs(x) - 1)

#include <asm-generic/bitops.h>

/* following definitions: to avoid using codes in lib/find_*.c */
#define find_next_bit		find_next_bit
#define find_next_zero_bit	find_next_zero_bit
#define find_first_bit		find_first_bit
#define find_first_zero_bit	find_first_zero_bit

#endif /* __UNICORE_BITOPS_H__ */
