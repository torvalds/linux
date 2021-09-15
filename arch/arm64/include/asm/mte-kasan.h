/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_KASAN_H
#define __ASM_MTE_KASAN_H

#include <asm/mte-def.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>

#ifdef CONFIG_ARM64_MTE

/*
 * These functions are meant to be only used from KASAN runtime through
 * the arch_*() interface defined in asm/memory.h.
 * These functions don't include system_supports_mte() checks,
 * as KASAN only calls them when MTE is supported and enabled.
 */

static inline u8 mte_get_ptr_tag(void *ptr)
{
	/* Note: The format of KASAN tags is 0xF<x> */
	u8 tag = 0xF0 | (u8)(((u64)(ptr)) >> MTE_TAG_SHIFT);

	return tag;
}

/* Get allocation tag for the address. */
static inline u8 mte_get_mem_tag(void *addr)
{
	asm(__MTE_PREAMBLE "ldg %0, [%0]"
		: "+r" (addr));

	return mte_get_ptr_tag(addr);
}

/* Generate a random tag. */
static inline u8 mte_get_random_tag(void)
{
	void *addr;

	asm(__MTE_PREAMBLE "irg %0, %0"
		: "=r" (addr));

	return mte_get_ptr_tag(addr);
}

static inline u64 __stg_post(u64 p)
{
	asm volatile(__MTE_PREAMBLE "stg %0, [%0], #16"
		     : "+r"(p)
		     :
		     : "memory");
	return p;
}

static inline u64 __stzg_post(u64 p)
{
	asm volatile(__MTE_PREAMBLE "stzg %0, [%0], #16"
		     : "+r"(p)
		     :
		     : "memory");
	return p;
}

static inline void __dc_gva(u64 p)
{
	asm volatile(__MTE_PREAMBLE "dc gva, %0" : : "r"(p) : "memory");
}

static inline void __dc_gzva(u64 p)
{
	asm volatile(__MTE_PREAMBLE "dc gzva, %0" : : "r"(p) : "memory");
}

/*
 * Assign allocation tags for a region of memory based on the pointer tag.
 * Note: The address must be non-NULL and MTE_GRANULE_SIZE aligned and
 * size must be MTE_GRANULE_SIZE aligned.
 */
static inline void mte_set_mem_tag_range(void *addr, size_t size, u8 tag,
					 bool init)
{
	u64 curr, mask, dczid_bs, end1, end2, end3;

	/* Read DC G(Z)VA block size from the system register. */
	dczid_bs = 4ul << (read_cpuid(DCZID_EL0) & 0xf);

	curr = (u64)__tag_set(addr, tag);
	mask = dczid_bs - 1;
	/* STG/STZG up to the end of the first block. */
	end1 = curr | mask;
	end3 = curr + size;
	/* DC GVA / GZVA in [end1, end2) */
	end2 = end3 & ~mask;

	/*
	 * The following code uses STG on the first DC GVA block even if the
	 * start address is aligned - it appears to be faster than an alignment
	 * check + conditional branch. Also, if the range size is at least 2 DC
	 * GVA blocks, the first two loops can use post-condition to save one
	 * branch each.
	 */
#define SET_MEMTAG_RANGE(stg_post, dc_gva)		\
	do {						\
		if (size >= 2 * dczid_bs) {		\
			do {				\
				curr = stg_post(curr);	\
			} while (curr < end1);		\
							\
			do {				\
				dc_gva(curr);		\
				curr += dczid_bs;	\
			} while (curr < end2);		\
		}					\
							\
		while (curr < end3)			\
			curr = stg_post(curr);		\
	} while (0)

	if (init)
		SET_MEMTAG_RANGE(__stzg_post, __dc_gzva);
	else
		SET_MEMTAG_RANGE(__stg_post, __dc_gva);
#undef SET_MEMTAG_RANGE
}

void mte_enable_kernel_sync(void);
void mte_enable_kernel_async(void);

#else /* CONFIG_ARM64_MTE */

static inline u8 mte_get_ptr_tag(void *ptr)
{
	return 0xFF;
}

static inline u8 mte_get_mem_tag(void *addr)
{
	return 0xFF;
}

static inline u8 mte_get_random_tag(void)
{
	return 0xFF;
}

static inline void mte_set_mem_tag_range(void *addr, size_t size,
						u8 tag, bool init)
{
}

static inline void mte_enable_kernel_sync(void)
{
}

static inline void mte_enable_kernel_async(void)
{
}

#endif /* CONFIG_ARM64_MTE */

#endif /* __ASSEMBLY__ */

#endif /* __ASM_MTE_KASAN_H  */
