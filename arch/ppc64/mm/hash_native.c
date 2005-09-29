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

#define HPTE_LOCK_BIT 3

static DEFINE_SPINLOCK(native_tlbie_lock);

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
			unsigned long prpn, unsigned long vflags,
			unsigned long rflags)
{
	hpte_t *hptep = htab_address + hpte_group;
	unsigned long hpte_v, hpte_r;
	int i;

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

	hpte_v = (va >> 23) << HPTE_V_AVPN_SHIFT | vflags | HPTE_V_VALID;
	if (vflags & HPTE_V_LARGE)
		va &= ~(1UL << HPTE_V_AVPN_SHIFT);
	hpte_r = (prpn << HPTE_R_RPN_SHIFT) | rflags;

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

static inline void set_pp_bit(unsigned long pp, hpte_t *addr)
{
	unsigned long old;
	unsigned long *p = &addr->r;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3\n\
		rldimi	%0,%2,0,61\n\
		stdcx.	%0,0,%3\n\
		bne	1b"
	: "=&r" (old), "=m" (*p)
	: "r" (pp), "r" (p), "m" (*p)
	: "cc");
}

/*
 * Only works on small pages. Yes its ugly to have to check each slot in
 * the group but we only use this during bootup.
 */
static long native_hpte_find(unsigned long vpn)
{
	hpte_t *hptep;
	unsigned long hash;
	unsigned long i, j;
	long slot;
	unsigned long hpte_v;

	hash = hpt_hash(vpn, 0);

	for (j = 0; j < 2; j++) {
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		for (i = 0; i < HPTES_PER_GROUP; i++) {
			hptep = htab_address + slot;
			hpte_v = hptep->v;

			if ((HPTE_V_AVPN_VAL(hpte_v) == (vpn >> 11))
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

static long native_hpte_updatepp(unsigned long slot, unsigned long newpp,
				 unsigned long va, int large, int local)
{
	hpte_t *hptep = htab_address + slot;
	unsigned long hpte_v;
	unsigned long avpn = va >> 23;
	int ret = 0;

	if (large)
		avpn &= ~1;

	native_lock_hpte(hptep);

	hpte_v = hptep->v;

	/* Even if we miss, we need to invalidate the TLB */
	if ((HPTE_V_AVPN_VAL(hpte_v) != avpn)
	    || !(hpte_v & HPTE_V_VALID)) {
		native_unlock_hpte(hptep);
		ret = -1;
	} else {
		set_pp_bit(newpp, hptep);
		native_unlock_hpte(hptep);
	}

	/* Ensure it is out of the tlb too */
	if (cpu_has_feature(CPU_FTR_TLBIEL) && !large && local) {
		tlbiel(va);
	} else {
		int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);

		if (lock_tlbie)
			spin_lock(&native_tlbie_lock);
		tlbie(va, large);
		if (lock_tlbie)
			spin_unlock(&native_tlbie_lock);
	}

	return ret;
}

/*
 * Update the page protection bits. Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT. Assumes pages being operated on will not be stolen.
 * Does not work on large pages.
 *
 * No need to lock here because we should be the only user.
 */
static void native_hpte_updateboltedpp(unsigned long newpp, unsigned long ea)
{
	unsigned long vsid, va, vpn, flags = 0;
	long slot;
	hpte_t *hptep;
	int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;

	slot = native_hpte_find(vpn);
	if (slot == -1)
		panic("could not find page to bolt\n");
	hptep = htab_address + slot;

	set_pp_bit(newpp, hptep);

	/* Ensure it is out of the tlb too */
	if (lock_tlbie)
		spin_lock_irqsave(&native_tlbie_lock, flags);
	tlbie(va, 0);
	if (lock_tlbie)
		spin_unlock_irqrestore(&native_tlbie_lock, flags);
}

static void native_hpte_invalidate(unsigned long slot, unsigned long va,
				    int large, int local)
{
	hpte_t *hptep = htab_address + slot;
	unsigned long hpte_v;
	unsigned long avpn = va >> 23;
	unsigned long flags;
	int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);

	if (large)
		avpn &= ~1;

	local_irq_save(flags);
	native_lock_hpte(hptep);

	hpte_v = hptep->v;

	/* Even if we miss, we need to invalidate the TLB */
	if ((HPTE_V_AVPN_VAL(hpte_v) != avpn)
	    || !(hpte_v & HPTE_V_VALID)) {
		native_unlock_hpte(hptep);
	} else {
		/* Invalidate the hpte. NOTE: this also unlocks it */
		hptep->v = 0;
	}

	/* Invalidate the tlb */
	if (cpu_has_feature(CPU_FTR_TLBIEL) && !large && local) {
		tlbiel(va);
	} else {
		if (lock_tlbie)
			spin_lock(&native_tlbie_lock);
		tlbie(va, large);
		if (lock_tlbie)
			spin_unlock(&native_tlbie_lock);
	}
	local_irq_restore(flags);
}

/*
 * clear all mappings on kexec.  All cpus are in real mode (or they will
 * be when they isi), and we are the only one left.  We rely on our kernel
 * mapping being 0xC0's and the hardware ignoring those two real bits.
 *
 * TODO: add batching support when enabled.  remember, no dynamic memory here,
 * athough there is the control page available...
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

		if (hpte_v & HPTE_V_VALID) {
			hptep->v = 0;
			tlbie(slot2va(hpte_v, slot), hpte_v & HPTE_V_LARGE);
		}
	}

	spin_unlock(&native_tlbie_lock);
	local_irq_restore(flags);
}

static void native_flush_hash_range(unsigned long number, int local)
{
	unsigned long va, vpn, hash, secondary, slot, flags, avpn;
	int i, j;
	hpte_t *hptep;
	unsigned long hpte_v;
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);
	unsigned long large = batch->large;

	local_irq_save(flags);

	j = 0;
	for (i = 0; i < number; i++) {
		va = batch->vaddr[j];
		if (large)
			vpn = va >> HPAGE_SHIFT;
		else
			vpn = va >> PAGE_SHIFT;
		hash = hpt_hash(vpn, large);
		secondary = (pte_val(batch->pte[i]) & _PAGE_SECONDARY) >> 15;
		if (secondary)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += (pte_val(batch->pte[i]) & _PAGE_GROUP_IX) >> 12;

		hptep = htab_address + slot;

		avpn = va >> 23;
		if (large)
			avpn &= ~0x1UL;

		native_lock_hpte(hptep);

		hpte_v = hptep->v;

		/* Even if we miss, we need to invalidate the TLB */
		if ((HPTE_V_AVPN_VAL(hpte_v) != avpn)
		    || !(hpte_v & HPTE_V_VALID)) {
			native_unlock_hpte(hptep);
		} else {
			/* Invalidate the hpte. NOTE: this also unlocks it */
			hptep->v = 0;
		}

		j++;
	}

	if (cpu_has_feature(CPU_FTR_TLBIEL) && !large && local) {
		asm volatile("ptesync":::"memory");

		for (i = 0; i < j; i++)
			__tlbiel(batch->vaddr[i]);

		asm volatile("ptesync":::"memory");
	} else {
		int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);

		if (lock_tlbie)
			spin_lock(&native_tlbie_lock);

		asm volatile("ptesync":::"memory");

		for (i = 0; i < j; i++)
			__tlbie(batch->vaddr[i], large);

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
