/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_KASAN_H
#define __ASM_MTE_KASAN_H

#include <asm/compiler.h>
#include <asm/cputype.h>
#include <asm/mte-def.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>

#ifdef CONFIG_KASAN_HW_TAGS

/* Whether the MTE asynchronous mode is enabled. */
DECLARE_STATIC_KEY_FALSE(mte_async_or_asymm_mode);

static inline bool system_uses_mte_async_or_asymm_mode(void)
{
	return static_branch_unlikely(&mte_async_or_asymm_mode);
}

#else /* CONFIG_KASAN_HW_TAGS */

static inline bool system_uses_mte_async_or_asymm_mode(void)
{
	return false;
}

#endif /* CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_ARM64_MTE

/*
 * The Tag Check Flag (TCF) mode for MTE is per EL, hence TCF0
 * affects EL0 and TCF affects EL1 irrespective of which TTBR is
 * used.
 * The kernel accesses TTBR0 usually with LDTR/STTR instructions
 * when UAO is available, so these would act as EL0 accesses using
 * TCF0.
 * However futex.h code uses exclusives which would be executed as
 * EL1, this can potentially cause a tag check fault even if the
 * user disables TCF0.
 *
 * To address the problem we set the PSTATE.TCO bit in uaccess_enable()
 * and reset it in uaccess_disable().
 *
 * The Tag check override (TCO) bit disables temporarily the tag checking
 * preventing the issue.
 */
static inline void mte_disable_tco(void)
{
	asm volatile(ALTERNATIVE("nop", SET_PSTATE_TCO(0),
				 ARM64_MTE, CONFIG_KASAN_HW_TAGS));
}

static inline void mte_enable_tco(void)
{
	asm volatile(ALTERNATIVE("nop", SET_PSTATE_TCO(1),
				 ARM64_MTE, CONFIG_KASAN_HW_TAGS));
}

/*
 * These functions disable tag checking only if in MTE async mode
 * since the sync mode generates exceptions synchronously and the
 * nofault or load_unaligned_zeropad can handle them.
 */
static inline void __mte_disable_tco_async(void)
{
	if (system_uses_mte_async_or_asymm_mode())
		mte_disable_tco();
}

static inline void __mte_enable_tco_async(void)
{
	if (system_uses_mte_async_or_asymm_mode())
		mte_enable_tco();
}

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
	u64 curr, mask, dczid, dczid_bs, dczid_dzp, end1, end2, end3;

	/* Read DC G(Z)VA block size from the system register. */
	dczid = read_cpuid(DCZID_EL0);
	dczid_bs = 4ul << (dczid & 0xf);
	dczid_dzp = (dczid >> 4) & 1;

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
		if (!dczid_dzp && size >= 2 * dczid_bs) {\
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
void mte_enable_kernel_asymm(void);
int mte_enable_kernel_store_only(void);

#else /* CONFIG_ARM64_MTE */

static inline void mte_disable_tco(void)
{
}

static inline void mte_enable_tco(void)
{
}

static inline void __mte_disable_tco_async(void)
{
}

static inline void __mte_enable_tco_async(void)
{
}

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

static inline void mte_enable_kernel_asymm(void)
{
}

static inline int mte_enable_kernel_store_only(void)
{
	return -EINVAL;
}

#endif /* CONFIG_ARM64_MTE */

#endif /* __ASSEMBLY__ */

#endif /* __ASM_MTE_KASAN_H  */
