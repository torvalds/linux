/*
 * native hashtable management.
 *
 * SMP scalability work:
 *    Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG_LOW

#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/smp.h>

#include <asm/abs_addr.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/cputable.h>
#include <asm/udbg.h>

#ifdef DEBUG_LOW
#define DBG_LOW(fmt...) udbg_printf(fmt)
#else
#define DBG_LOW(fmt...)
#endif

#define HPTE_LOCK_BIT 3

static DEFINE_SPINLOCK(native_tlbie_lock);

static inline void __tlbie(unsigned long va, unsigned int psize)
{
	unsigned int penc;

	/* clear top 16 bits, non SLS segment */
	va &= ~(0xffffULL << 48);

	switch (psize) {
	case MMU_PAGE_4K:
		va &= ~0xffful;
		asm volatile("tlbie %0,0" : : "r" (va) : "memory");
		break;
	default:
		penc = mmu_psize_defs[psize].penc;
		va &= ~((1ul << mmu_psize_defs[psize].shift) - 1);
		va |= penc << 12;
		asm volatile("tlbie %0,1" : : "r" (va) : "memory");
		break;
	}
}

static inline void __tlbiel(unsigned long va, unsigned int psize)
{
	unsigned int penc;

	/* clear top 16 bits, non SLS segment */
	va &= ~(0xffffULL << 48);

	switch (psize) {
	case MMU_PAGE_4K:
		va &= ~0xffful;
		asm volatile(".long 0x7c000224 | (%0 << 11) | (0 << 21)"
			     : : "r"(va) : "memory");
		break;
	default:
		penc = mmu_psize_defs[psize].penc;
		va &= ~((1ul << mmu_psize_defs[psize].shift) - 1);
		va |= penc << 12;
		asm volatile(".long 0x7c000224 | (%0 << 11) | (1 << 21)"
			     : : "r"(va) : "memory");
		break;
	}

}

static inline void tlbie(unsigned long va, int psize, int local)
{
	unsigned int use_local = local && cpu_has_feature(CPU_FTR_TLBIEL);
	int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);

	if (use_local)
		use_local = mmu_psize_defs[psize].tlbiel;
	if (lock_tlbie && !use_local)
		spin_lock(&native_tlbie_lock);
	asm volatile("ptesync": : :"memory");
	if (use_local) {
		__tlbiel(va, psize);
		asm volatile("ptesync": : :"memory");
	} else {
		__tlbie(va, psize);
		asm volatile("eieio; tlbsync; ptesync": : :"memory");
	}
	if (lock_tlbie && !use_local)
		spin_unlock(&native_tlbie_lock);
}

static inline void native_lock_hpte(hpte_t *hptep)
{
	unsigned long *word = &hptep->v;

	while (1) {
		if (!test_and_set_bit(HPTE_LOCK_BIT, word))
			break;
		while(test_bit(HPTE_LOCK_BIT, word))
			cpu_relax();
	}
}

static inline void native_unlock_hpte(hpte_t *hptep)
{
	unsigned long *word = &hptep->v;

	asm volatile("lwsync":::"memory");
	clear_bit(HPTE_LOCK_BIT, word);
}

long native_hpte_insert(unsigned long hpte_group, unsigned long va,
			unsigned long pa, unsigned long rflags,
			unsigned long vflags, int psize)
{
	hpte_t *hptep = htab_address + hpte_group;
	unsigned long hpte_v, hpte_r;
	int i;

	if (!(vflags & HPTE_V_BOLTED)) {
		DBG_LOW("    insert(group=%lx, va=%016lx, pa=%016lx,"
			" rflags=%lx, vflags=%lx, psize=%d)\n",
			hpte_group, va, pa, rflags, vflags, psize);
	}

	for (i = 0; i < HPTES_PER_GROUP; i++) {
		if (! (hptep->v & HPTE_V_VALID)) {
			/* retry with lock held */
			native_lock_hpte(hptep);
			if (! (hptep->v & HPTE_V_VALID))
				break;
			native_unlock_hpte(hptep);
		}

		hptep++;
	}

	if (i == HPTES_PER_GROUP)
		return -1;

	hpte_v = hpte_encode_v(va, psize) | vflags | HPTE_V_VALID;
	hpte_r = hpte_encode_r(pa, psize) | rflags;

	if (!(vflags & HPTE_V_BOLTED)) {
		DBG_LOW(" i=%x hpte_v=%016lx, hpte_r=%016lx\n",
			i, hpte_v, hpte_r);
	}

	hptep->r = hpte_r;
	/* Guarantee the second dword is visible before the valid bit */
	__asm__ __volatile__ ("eieio" : : : "memory");
	/*
	 * Now set the first dword including the valid bit
	 * NOTE: this also unlocks the hpte
	 */
	hptep->v = hpte_v;

	__asm__ __volatile__ ("ptesync" : : : "memory");

	return i | (!!(vflags & HPTE_V_SECONDARY) << 3);
}

static long native_hpte_remove(unsigned long hpte_group)
{
	hpte_t *hptep;
	int i;
	int slot_offset;
	unsigned long hpte_v;

	DBG_LOW("    remove(group=%lx)\n", hpte_group);

	/* pick a random entry to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {
		hptep = htab_address + hpte_group + slot_offset;
		hpte_v = hptep->v;

		if ((hpte_v & HPTE_V_VALID) && !(hpte_v & HPTE_V_BOLTED)) {
			/* retry with lock held */
			native_lock_hpte(hptep);
			hpte_v = hptep->v;
			if ((hpte_v & HPTE_V_VALID)
			    && !(hpte_v & HPTE_V_BOLTED))
				break;
			native_unlock_hpte(hptep);
		}

		slot_offset++;
		slot_offset &= 0x7;
	}

	if (i == HPTES_PER_GROUP)
		return -1;

	/* Invalidate the hpte. NOTE: this also unlocks it */
	hptep->v = 0;

	return i;
}

static long native_hpte_updatepp(unsigned long slot, unsigned long newpp,
				 unsigned long va, int psize, int local)
{
	hpte_t *hptep = htab_address + slot;
	unsigned long hpte_v, want_v;
	int ret = 0;

	want_v = hpte_encode_v(va, psize);

	DBG_LOW("    update(va=%016lx, avpnv=%016lx, hash=%016lx, newpp=%x)",
		va, want_v & HPTE_V_AVPN, slot, newpp);

	native_lock_hpte(hptep);

	hpte_v = hptep->v;

	/* Even if we miss, we need to invalidate the TLB */
	if (!HPTE_V_COMPARE(hpte_v, want_v) || !(hpte_v & HPTE_V_VALID)) {
		DBG_LOW(" -> miss\n");
		native_unlock_hpte(hptep);
		ret = -1;
	} else {
		DBG_LOW(" -> hit\n");
		/* Update the HPTE */
		hptep->r = (hptep->r & ~(HPTE_R_PP | HPTE_R_N)) |
			(newpp & (HPTE_R_PP | HPTE_R_N | HPTE_R_C));
		native_unlock_hpte(hptep);
	}

	/* Ensure it is out of the tlb too. */
	tlbie(va, psize, local);

	return ret;
}

static long native_hpte_find(unsigned long va, int psize)
{
	hpte_t *hptep;
	unsigned long hash;
	unsigned long i, j;
	long slot;
	unsigned long want_v, hpte_v;

	hash = hpt_hash(va, mmu_psize_defs[psize].shift);
	want_v = hpte_encode_v(va, psize);

	for (j = 0; j < 2; j++) {
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		for (i = 0; i < HPTES_PER_GROUP; i++) {
			hptep = htab_address + slot;
			hpte_v = hptep->v;

			if (HPTE_V_COMPARE(hpte_v, want_v)
			    && (hpte_v & HPTE_V_VALID)
			    && ( !!(hpte_v & HPTE_V_SECONDARY) == j)) {
				/* HPTE matches */
				if (j)
					slot = -slot;
				return slot;
			}
			++slot;
		}
		hash = ~hash;
	}

	return -1;
}

/*
 * Update the page protection bits. Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT. Assumes pages being operated on will not be stolen.
 *
 * No need to lock here because we should be the only user.
 */
static void native_hpte_updateboltedpp(unsigned long newpp, unsigned long ea,
				       int psize)
{
	unsigned long vsid, va;
	long slot;
	hpte_t *hptep;

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);

	slot = native_hpte_find(va, psize);
	if (slot == -1)
		panic("could not find page to bolt\n");
	hptep = htab_address + slot;

	/* Update the HPTE */
	hptep->r = (hptep->r & ~(HPTE_R_PP | HPTE_R_N)) |
		(newpp & (HPTE_R_PP | HPTE_R_N));

	/* Ensure it is out of the tlb too. */
	tlbie(va, psize, 0);
}

static void native_hpte_invalidate(unsigned long slot, unsigned long va,
				   int psize, int local)
{
	hpte_t *hptep = htab_address + slot;
	unsigned long hpte_v;
	unsigned long want_v;
	unsigned long flags;

	local_irq_save(flags);

	DBG_LOW("    invalidate(va=%016lx, hash: %x)\n", va, slot);

	want_v = hpte_encode_v(va, psize);
	native_lock_hpte(hptep);
	hpte_v = hptep->v;

	/* Even if we miss, we need to invalidate the TLB */
	if (!HPTE_V_COMPARE(hpte_v, want_v) || !(hpte_v & HPTE_V_VALID))
		native_unlock_hpte(hptep);
	else
		/* Invalidate the hpte. NOTE: this also unlocks it */
		hptep->v = 0;

	/* Invalidate the TLB */
	tlbie(va, psize, local);

	local_irq_restore(flags);
}

/*
 * XXX This need fixing based on page size. It's only used by
 * native_hpte_clear() for now which needs fixing too so they
 * make a good pair...
 */
static unsigned long slot2va(unsigned long hpte_v, unsigned long slot)
{
	unsigned long avpn = HPTE_V_AVPN_VAL(hpte_v);
	unsigned long va;

	va = avpn << 23;

	if (! (hpte_v & HPTE_V_LARGE)) {
		unsigned long vpi, pteg;

		pteg = slot / HPTES_PER_GROUP;
		if (hpte_v & HPTE_V_SECONDARY)
			pteg = ~pteg;

		vpi = ((va >> 28) ^ pteg) & htab_hash_mask;

		va |= vpi << PAGE_SHIFT;
	}

	return va;
}

/*
 * clear all mappings on kexec.  All cpus are in real mode (or they will
 * be when they isi), and we are the only one left.  We rely on our kernel
 * mapping being 0xC0's and the hardware ignoring those two real bits.
 *
 * TODO: add batching support when enabled.  remember, no dynamic memory here,
 * athough there is the control page available...
 *
 * XXX FIXME: 4k only for now !
 */
static void native_hpte_clear(void)
{
	unsigned long slot, slots, flags;
	hpte_t *hptep = htab_address;
	unsigned long hpte_v;
	unsigned long pteg_count;

	pteg_count = htab_hash_mask + 1;

	local_irq_save(flags);

	/* we take the tlbie lock and hold it.  Some hardware will
	 * deadlock if we try to tlbie from two processors at once.
	 */
	spin_lock(&native_tlbie_lock);

	slots = pteg_count * HPTES_PER_GROUP;

	for (slot = 0; slot < slots; slot++, hptep++) {
		/*
		 * we could lock the pte here, but we are the only cpu
		 * running,  right?  and for crash dump, we probably
		 * don't want to wait for a maybe bad cpu.
		 */
		hpte_v = hptep->v;

		/*
		 * Call __tlbie() here rather than tlbie() since we
		 * already hold the native_tlbie_lock.
		 */
		if (hpte_v & HPTE_V_VALID) {
			hptep->v = 0;
			__tlbie(slot2va(hpte_v, slot), MMU_PAGE_4K);
		}
	}

	asm volatile("eieio; tlbsync; ptesync":::"memory");
	spin_unlock(&native_tlbie_lock);
	local_irq_restore(flags);
}

/*
 * Batched hash table flush, we batch the tlbie's to avoid taking/releasing
 * the lock all the time
 */
static void native_flush_hash_range(unsigned long number, int local)
{
	unsigned long va, hash, index, hidx, shift, slot;
	hpte_t *hptep;
	unsigned long hpte_v;
	unsigned long want_v;
	unsigned long flags;
	real_pte_t pte;
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);
	unsigned long psize = batch->psize;
	int i;

	local_irq_save(flags);

	for (i = 0; i < number; i++) {
		va = batch->vaddr[i];
		pte = batch->pte[i];

		pte_iterate_hashed_subpages(pte, psize, va, index, shift) {
			hash = hpt_hash(va, shift);
			hidx = __rpte_to_hidx(pte, index);
			if (hidx & _PTEIDX_SECONDARY)
				hash = ~hash;
			slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
			slot += hidx & _PTEIDX_GROUP_IX;
			hptep = htab_address + slot;
			want_v = hpte_encode_v(va, psize);
			native_lock_hpte(hptep);
			hpte_v = hptep->v;
			if (!HPTE_V_COMPARE(hpte_v, want_v) ||
			    !(hpte_v & HPTE_V_VALID))
				native_unlock_hpte(hptep);
			else
				hptep->v = 0;
		} pte_iterate_hashed_end();
	}

	if (cpu_has_feature(CPU_FTR_TLBIEL) &&
	    mmu_psize_defs[psize].tlbiel && local) {
		asm volatile("ptesync":::"memory");
		for (i = 0; i < number; i++) {
			va = batch->vaddr[i];
			pte = batch->pte[i];

			pte_iterate_hashed_subpages(pte, psize, va, index,
						    shift) {
				__tlbiel(va, psize);
			} pte_iterate_hashed_end();
		}
		asm volatile("ptesync":::"memory");
	} else {
		int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);

		if (lock_tlbie)
			spin_lock(&native_tlbie_lock);

		asm volatile("ptesync":::"memory");
		for (i = 0; i < number; i++) {
			va = batch->vaddr[i];
			pte = batch->pte[i];

			pte_iterate_hashed_subpages(pte, psize, va, index,
						    shift) {
				__tlbie(va, psize);
			} pte_iterate_hashed_end();
		}
		asm volatile("eieio; tlbsync; ptesync":::"memory");

		if (lock_tlbie)
			spin_unlock(&native_tlbie_lock);
	}

	local_irq_restore(flags);
}

#ifdef CONFIG_PPC_PSERIES
/* Disable TLB batching on nighthawk */
static inline int tlb_batching_enabled(void)
{
	struct device_node *root = of_find_node_by_path("/");
	int enabled = 1;

	if (root) {
		const char *model = get_property(root, "model", NULL);
		if (model && !strcmp(model, "IBM,9076-N81"))
			enabled = 0;
		of_node_put(root);
	}

	return enabled;
}
#else
static inline int tlb_batching_enabled(void)
{
	return 1;
}
#endif

void hpte_init_native(void)
{
	ppc_md.hpte_invalidate	= native_hpte_invalidate;
	ppc_md.hpte_updatepp	= native_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = native_hpte_updateboltedpp;
	ppc_md.hpte_insert	= native_hpte_insert;
	ppc_md.hpte_remove	= native_hpte_remove;
	ppc_md.hpte_clear_all	= native_hpte_clear;
	if (tlb_batching_enabled())
		ppc_md.flush_hash_range = native_flush_hash_range;
	htab_finish_init();
}
