/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_CACHE_H
#define __ASM_CACHE_H

#define L1_CACHE_SHIFT		(6)
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define CLIDR_LOUU_SHIFT	27
#define CLIDR_LOC_SHIFT		24
#define CLIDR_LOUIS_SHIFT	21

#define CLIDR_LOUU(clidr)	(((clidr) >> CLIDR_LOUU_SHIFT) & 0x7)
#define CLIDR_LOC(clidr)	(((clidr) >> CLIDR_LOC_SHIFT) & 0x7)
#define CLIDR_LOUIS(clidr)	(((clidr) >> CLIDR_LOUIS_SHIFT) & 0x7)

/* Ctypen, bits[3(n - 1) + 2 : 3(n - 1)], for n = 1 to 7 */
#define CLIDR_CTYPE_SHIFT(level)	(3 * (level - 1))
#define CLIDR_CTYPE_MASK(level)		(7 << CLIDR_CTYPE_SHIFT(level))
#define CLIDR_CTYPE(clidr, level)	\
	(((clidr) & CLIDR_CTYPE_MASK(level)) >> CLIDR_CTYPE_SHIFT(level))

/* Ttypen, bits [2(n - 1) + 34 : 2(n - 1) + 33], for n = 1 to 7 */
#define CLIDR_TTYPE_SHIFT(level)	(2 * ((level) - 1) + CLIDR_EL1_Ttypen_SHIFT)

/*
 * Memory returned by kmalloc() may be used for DMA, so we must make
 * sure that all such allocations are cache aligned. Otherwise,
 * unrelated code may cause parts of the buffer to be read into the
 * cache before the transfer is done, causing old data to be seen by
 * the CPU.
 */
#define ARCH_DMA_MINALIGN	(128)
#define ARCH_KMALLOC_MINALIGN	(8)

#if !defined(__ASSEMBLY__) && !defined(BUILD_VDSO)

#include <linux/bitops.h>
#include <linux/kasan-enabled.h>

#include <asm/cputype.h>
#include <asm/mte-def.h>
#include <asm/sysreg.h>

#ifdef CONFIG_KASAN_SW_TAGS
#define ARCH_SLAB_MINALIGN	(1ULL << KASAN_SHADOW_SCALE_SHIFT)
#elif defined(CONFIG_KASAN_HW_TAGS)
static inline unsigned int arch_slab_minalign(void)
{
	return kasan_hw_tags_enabled() ? MTE_GRANULE_SIZE :
					 __alignof__(unsigned long long);
}
#define arch_slab_minalign() arch_slab_minalign()
#endif

#define CTR_L1IP(ctr)		SYS_FIELD_GET(CTR_EL0, L1Ip, ctr)

#define ICACHEF_ALIASING	0
extern unsigned long __icache_flags;

/*
 * Whilst the D-side always behaves as PIPT on AArch64, aliasing is
 * permitted in the I-cache.
 */
static inline int icache_is_aliasing(void)
{
	return test_bit(ICACHEF_ALIASING, &__icache_flags);
}

static inline u32 cache_type_cwg(void)
{
	return SYS_FIELD_GET(CTR_EL0, CWG, read_cpuid_cachetype());
}

#define __read_mostly __section(".data..read_mostly")

static inline int cache_line_size_of_cpu(void)
{
	u32 cwg = cache_type_cwg();

	return cwg ? 4 << cwg : ARCH_DMA_MINALIGN;
}

int cache_line_size(void);

#define dma_get_cache_alignment	cache_line_size

/*
 * Read the effective value of CTR_EL0.
 *
 * According to ARM ARM for ARMv8-A (ARM DDI 0487C.a),
 * section D10.2.33 "CTR_EL0, Cache Type Register" :
 *
 * CTR_EL0.IDC reports the data cache clean requirements for
 * instruction to data coherence.
 *
 *  0 - dcache clean to PoU is required unless :
 *     (CLIDR_EL1.LoC == 0) || (CLIDR_EL1.LoUIS == 0 && CLIDR_EL1.LoUU == 0)
 *  1 - dcache clean to PoU is not required for i-to-d coherence.
 *
 * This routine provides the CTR_EL0 with the IDC field updated to the
 * effective state.
 */
static inline u32 __attribute_const__ read_cpuid_effective_cachetype(void)
{
	u32 ctr = read_cpuid_cachetype();

	if (!(ctr & BIT(CTR_EL0_IDC_SHIFT))) {
		u64 clidr = read_sysreg(clidr_el1);

		if (CLIDR_LOC(clidr) == 0 ||
		    (CLIDR_LOUIS(clidr) == 0 && CLIDR_LOUU(clidr) == 0))
			ctr |= BIT(CTR_EL0_IDC_SHIFT);
	}

	return ctr;
}

#endif /* !defined(__ASSEMBLY__) && !defined(BUILD_VDSO) */

#endif
