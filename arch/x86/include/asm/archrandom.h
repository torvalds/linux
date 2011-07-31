/*
 * This file is part of the Linux kernel.
 *
 * Copyright (c) 2011, Intel Corporation
 * Authors: Fenghua Yu <fenghua.yu@intel.com>,
 *          H. Peter Anvin <hpa@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef ASM_X86_ARCHRANDOM_H
#define ASM_X86_ARCHRANDOM_H

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/nops.h>

#define RDRAND_RETRY_LOOPS	10

#define RDRAND_INT	".byte 0x0f,0xc7,0xf0"
#ifdef CONFIG_X86_64
# define RDRAND_LONG	".byte 0x48,0x0f,0xc7,0xf0"
#else
# define RDRAND_LONG	RDRAND_INT
#endif

#ifdef CONFIG_ARCH_RANDOM

#define GET_RANDOM(name, type, rdrand, nop)			\
static inline int name(type *v)					\
{								\
	int ok;							\
	alternative_io("movl $0, %0\n\t"			\
		       nop,					\
		       "\n1: " rdrand "\n\t"			\
		       "jc 2f\n\t"				\
		       "decl %0\n\t"                            \
		       "jnz 1b\n\t"                             \
		       "2:",                                    \
		       X86_FEATURE_RDRAND,                      \
		       ASM_OUTPUT2("=r" (ok), "=a" (*v)),       \
		       "0" (RDRAND_RETRY_LOOPS));		\
	return ok;						\
}

#ifdef CONFIG_X86_64

GET_RANDOM(arch_get_random_long, unsigned long, RDRAND_LONG, ASM_NOP5);
GET_RANDOM(arch_get_random_int, unsigned int, RDRAND_INT, ASM_NOP4);

#else

GET_RANDOM(arch_get_random_long, unsigned long, RDRAND_LONG, ASM_NOP3);
GET_RANDOM(arch_get_random_int, unsigned int, RDRAND_INT, ASM_NOP3);

#endif /* CONFIG_X86_64 */

#endif  /* CONFIG_ARCH_RANDOM */

#endif /* ASM_X86_ARCHRANDOM_H */
