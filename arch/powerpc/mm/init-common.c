/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#undef DEBUG

#include <linux/string.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

#define CTOR(shift) static void ctor_##shift(void *addr) \
{							\
	memset(addr, 0, sizeof(void *) << (shift));	\
}

CTOR(0); CTOR(1); CTOR(2); CTOR(3); CTOR(4); CTOR(5); CTOR(6); CTOR(7);
CTOR(8); CTOR(9); CTOR(10); CTOR(11); CTOR(12); CTOR(13); CTOR(14); CTOR(15);

static inline void (*ctor(int shift))(void *)
{
	BUILD_BUG_ON(MAX_PGTABLE_INDEX_SIZE != 15);

	switch (shift) {
	case 0: return ctor_0;
	case 1: return ctor_1;
	case 2: return ctor_2;
	case 3: return ctor_3;
	case 4: return ctor_4;
	case 5: return ctor_5;
	case 6: return ctor_6;
	case 7: return ctor_7;
	case 8: return ctor_8;
	case 9: return ctor_9;
	case 10: return ctor_10;
	case 11: return ctor_11;
	case 12: return ctor_12;
	case 13: return ctor_13;
	case 14: return ctor_14;
	case 15: return ctor_15;
	}
	return NULL;
}

struct kmem_cache *pgtable_cache[MAX_PGTABLE_INDEX_SIZE + 1];
EXPORT_SYMBOL_GPL(pgtable_cache);	/* used by kvm_hv module */

/*
 * Create a kmem_cache() for pagetables.  This is not used for PTE
 * pages - they're linked to struct page, come from the normal free
 * pages pool and have a different entry size (see real_pte_t) to
 * everything else.  Caches created by this function are used for all
 * the higher level pagetables, and for hugepage pagetables.
 */
void pgtable_cache_add(unsigned int shift)
{
	char *name;
	unsigned long table_size = sizeof(void *) << shift;
	unsigned long align = table_size;

	/* When batching pgtable pointers for RCU freeing, we store
	 * the index size in the low bits.  Table alignment must be
	 * big enough to fit it.
	 *
	 * Likewise, hugeapge pagetable pointers contain a (different)
	 * shift value in the low bits.  All tables must be aligned so
	 * as to leave enough 0 bits in the address to contain it. */
	unsigned long minalign = max(MAX_PGTABLE_INDEX_SIZE + 1,
				     HUGEPD_SHIFT_MASK + 1);
	struct kmem_cache *new;

	/* It would be nice if this was a BUILD_BUG_ON(), but at the
	 * moment, gcc doesn't seem to recognize is_power_of_2 as a
	 * constant expression, so so much for that. */
	BUG_ON(!is_power_of_2(minalign));
	BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);

	if (PGT_CACHE(shift))
		return; /* Already have a cache of this size */

	align = max_t(unsigned long, align, minalign);
	name = kasprintf(GFP_KERNEL, "pgtable-2^%d", shift);
	new = kmem_cache_create(name, table_size, align, 0, ctor(shift));
	if (!new)
		panic("Could not allocate pgtable cache for order %d", shift);

	kfree(name);
	pgtable_cache[shift] = new;

	pr_debug("Allocated pgtable cache for order %d\n", shift);
}
EXPORT_SYMBOL_GPL(pgtable_cache_add);	/* used by kvm_hv module */

void pgtable_cache_init(void)
{
	pgtable_cache_add(PGD_INDEX_SIZE);

	if (PMD_CACHE_INDEX)
		pgtable_cache_add(PMD_CACHE_INDEX);
	/*
	 * In all current configs, when the PUD index exists it's the
	 * same size as either the pgd or pmd index except with THP enabled
	 * on book3s 64
	 */
	if (PUD_CACHE_INDEX)
		pgtable_cache_add(PUD_CACHE_INDEX);
}
