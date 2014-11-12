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
#ifndef __ASM_CACHETYPE_H
#define __ASM_CACHETYPE_H

#include <asm/cputype.h>

#define CTR_L1IP_SHIFT		14
#define CTR_L1IP_MASK		3
#define CTR_CWG_SHIFT		24
#define CTR_CWG_MASK		15

#define ICACHE_POLICY_RESERVED	0
#define ICACHE_POLICY_AIVIVT	1
#define ICACHE_POLICY_VIPT	2
#define ICACHE_POLICY_PIPT	3

#ifndef __ASSEMBLY__

#include <linux/bitops.h>

#define CTR_L1IP(ctr)	(((ctr) >> CTR_L1IP_SHIFT) & CTR_L1IP_MASK)

#define ICACHEF_ALIASING	BIT(0)
#define ICACHEF_AIVIVT		BIT(1)

extern unsigned long __icache_flags;

#define CCSIDR_EL1_LINESIZE_MASK	0x7
#define CCSIDR_EL1_LINESIZE(x)		((x) & CCSIDR_EL1_LINESIZE_MASK)

#define CCSIDR_EL1_NUMSETS_SHIFT	13
#define CCSIDR_EL1_NUMSETS_MASK		(0x7fff << CCSIDR_EL1_NUMSETS_SHIFT)
#define CCSIDR_EL1_NUMSETS(x) \
	(((x) & CCSIDR_EL1_NUMSETS_MASK) >> CCSIDR_EL1_NUMSETS_SHIFT)

extern u64 __attribute_const__ icache_get_ccsidr(void);

static inline int icache_get_linesize(void)
{
	return 16 << CCSIDR_EL1_LINESIZE(icache_get_ccsidr());
}

static inline int icache_get_numsets(void)
{
	return 1 + CCSIDR_EL1_NUMSETS(icache_get_ccsidr());
}

/*
 * Whilst the D-side always behaves as PIPT on AArch64, aliasing is
 * permitted in the I-cache.
 */
static inline int icache_is_aliasing(void)
{
	return test_bit(ICACHEF_ALIASING, &__icache_flags);
}

static inline int icache_is_aivivt(void)
{
	return test_bit(ICACHEF_AIVIVT, &__icache_flags);
}

static inline u32 cache_type_cwg(void)
{
	return (read_cpuid_cachetype() >> CTR_CWG_SHIFT) & CTR_CWG_MASK;
}

#endif	/* __ASSEMBLY__ */

#endif	/* __ASM_CACHETYPE_H */
