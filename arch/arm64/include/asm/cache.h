/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_CACHE_H
#define __ASM_CACHE_H

#include <asm/cputype.h>

#define CTR_L1IP_SHIFT		14
#define CTR_L1IP_MASK		3
#define CTR_DMINLINE_SHIFT	16
#define CTR_ERG_SHIFT		20
#define CTR_CWG_SHIFT		24
#define CTR_CWG_MASK		15
#define CTR_IDC_SHIFT		28
#define CTR_DIC_SHIFT		29

#define CTR_L1IP(ctr)		(((ctr) >> CTR_L1IP_SHIFT) & CTR_L1IP_MASK)

#define ICACHE_POLICY_VPIPT	0
#define ICACHE_POLICY_VIPT	2
#define ICACHE_POLICY_PIPT	3

#define L1_CACHE_SHIFT		(6)
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

/*
 * Memory returned by kmalloc() may be used for DMA, so we must make
 * sure that all such allocations are cache aligned. Otherwise,
 * unrelated code may cause parts of the buffer to be read into the
 * cache before the transfer is done, causing old data to be seen by
 * the CPU.
 */
#define ARCH_DMA_MINALIGN	(128)

#ifndef __ASSEMBLY__

#include <linux/bitops.h>

#define ICACHEF_ALIASING	0
#define ICACHEF_VPIPT		1
extern unsigned long __icache_flags;

/*
 * Whilst the D-side always behaves as PIPT on AArch64, aliasing is
 * permitted in the I-cache.
 */
static inline int icache_is_aliasing(void)
{
	return test_bit(ICACHEF_ALIASING, &__icache_flags);
}

static inline int icache_is_vpipt(void)
{
	return test_bit(ICACHEF_VPIPT, &__icache_flags);
}

static inline u32 cache_type_cwg(void)
{
	return (read_cpuid_cachetype() >> CTR_CWG_SHIFT) & CTR_CWG_MASK;
}

#define __read_mostly __attribute__((__section__(".data..read_mostly")))

static inline int cache_line_size(void)
{
	u32 cwg = cache_type_cwg();
	return cwg ? 4 << cwg : ARCH_DMA_MINALIGN;
}

#endif	/* __ASSEMBLY__ */

#endif
