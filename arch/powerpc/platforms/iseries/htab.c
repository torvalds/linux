/*
 * iSeries hashtable management.
 *	Derived from pSeries_htab.c
 *
 * SMP scalability work:
 *    Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/abs_addr.h>
#include <linux/spinlock.h>

#include "call_hpt.h"

static spinlock_t iSeries_hlocks[64] __cacheline_aligned_in_smp =
	{ [0 ... 63] = SPIN_LOCK_UNLOCKED};

/*
 * Very primitive algorithm for picking up a lock
 */
static inline void iSeries_hlock(unsigned long slot)
{
	if (slot & 0x8)
		slot = ~slot;
	spin_lock(&iSeries_hlocks[(slot >> 4) & 0x3f]);
}

static inline void iSeries_hunlock(unsigned long slot)
{
	if (slot & 0x8)
		slot = ~slot;
	spin_unlock(&iSeries_hlocks[(slot >> 4) & 0x3f]);
}

static long iSeries_hpte_insert(unsigned long hpte_group, unsigned long va,
				unsigned long prpn, unsigned long vflags,
				unsigned long rflags)
{
	unsigned long arpn;
	long slot;
	hpte_t lhpte;
	int secondary = 0;

	/*
	 * The hypervisor tries both primary and secondary.
	 * If we are being called to insert in the secondary,
	 * it means we have already tried both primary and secondary,
	 * so we return failure immediately.
	 */
	if (vflags & HPTE_V_SECONDARY)
		return -1;

	iSeries_hlock(hpte_group);

	slot = HvCallHpt_findValid(&lhpte, va >> PAGE_SHIFT);
	BUG_ON(lhpte.v & HPTE_V_VALID);

	if (slot == -1)	{ /* No available entry found in either group */
		iSeries_hunlock(hpte_group);
		return -1;
	}

	if (slot < 0) {		/* MSB set means secondary group */
		vflags |= HPTE_V_SECONDARY;
		secondary = 1;
		slot &= 0x7fffffffffffffff;
	}

	arpn = phys_to_abs(prpn << PAGE_SHIFT) >> PAGE_SHIFT;

	lhpte.v = (va >> 23) << HPTE_V_AVPN_SHIFT | vflags | HPTE_V_VALID;
	lhpte.r = (arpn << HPTE_R_RPN_SHIFT) | rflags;

	/* Now fill in the actual HPTE */
	HvCallHpt_addValidate(slot, secondary, &lhpte);

	iSeries_hunlock(hpte_group);

	return (secondary << 3) | (slot & 7);
}

long iSeries_hpte_bolt_or_insert(unsigned long hpte_group,
		unsigned long va, unsigned long prpn, unsigned long vflags,
		unsigned long rflags)
{
	long slot;
	hpte_t lhpte;

	slot = HvCallHpt_findValid(&lhpte, va >> PAGE_SHIFT);

	if (lhpte.v & HPTE_V_VALID) {
		/* Bolt the existing HPTE */
		HvCallHpt_setSwBits(slot, 0x10, 0);
		HvCallHpt_setPp(slot, PP_RWXX);
		return 0;
	}

	return iSeries_hpte_insert(hpte_group, va, prpn, vflags, rflags);
}

static unsigned long iSeries_hpte_getword0(unsigned long slot)
{
	hpte_t hpte;

	HvCallHpt_get(&hpte, slot);
	return hpte.v;
}

static long iSeries_hpte_remove(unsigned long hpte_group)
{
	unsigned long slot_offset;
	int i;
	unsigned long hpte_v;

	/* Pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	iSeries_hlock(hpte_group);

	for (i = 0; i < HPTES_PER_GROUP; i++) {
		hpte_v = iSeries_hpte_getword0(hpte_group + slot_offset);

		if (! (hpte_v & HPTE_V_BOLTED)) {
			HvCallHpt_invalidateSetSwBitsGet(hpte_group +
							 slot_offset, 0, 0);
			iSeries_hunlock(hpte_group);
			return i;
		}

		slot_offset++;
		slot_offset &= 0x7;
	}

	iSeries_hunlock(hpte_group);

	return -1;
}

/*
 * The HyperVisor expects the "flags" argument in this form:
 *	bits  0..59 : reserved
 *	bit      60 : N
 *	bits 61..63 : PP2,PP1,PP0
 */
static long iSeries_hpte_updatepp(unsigned long slot, unsigned long newpp,
				  unsigned long va, int large, int local)
{
	hpte_t hpte;
	unsigned long avpn = va >> 23;

	iSeries_hlock(slot);

	HvCallHpt_get(&hpte, slot);
	if ((HPTE_V_AVPN_VAL(hpte.v) == avpn) && (hpte.v & HPTE_V_VALID)) {
		/*
		 * Hypervisor expects bits as NPPP, which is
		 * different from how they are mapped in our PP.
		 */
		HvCallHpt_setPp(slot, (newpp & 0x3) | ((newpp & 0x4) << 1));
		iSeries_hunlock(slot);
		return 0;
	}
	iSeries_hunlock(slot);

	return -1;
}

/*
 * Functions used to find the PTE for a particular virtual address.
 * Only used during boot when bolting pages.
 *
 * Input : vpn      : virtual page number
 * Output: PTE index within the page table of the entry
 *         -1 on failure
 */
static long iSeries_hpte_find(unsigned long vpn)
{
	hpte_t hpte;
	long slot;

	/*
	 * The HvCallHpt_findValid interface is as follows:
	 * 0xffffffffffffffff : No entry found.
	 * 0x00000000xxxxxxxx : Entry found in primary group, slot x
	 * 0x80000000xxxxxxxx : Entry found in secondary group, slot x
	 */
	slot = HvCallHpt_findValid(&hpte, vpn);
	if (hpte.v & HPTE_V_VALID) {
		if (slot < 0) {
			slot &= 0x7fffffffffffffff;
			slot = -slot;
		}
	} else
		slot = -1;
	return slot;
}

/*
 * Update the page protection bits. Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT. Assumes pages being operated on will not be stolen.
 * Does not work on large pages.
 *
 * No need to lock here because we should be the only user.
 */
static void iSeries_hpte_updateboltedpp(unsigned long newpp, unsigned long ea)
{
	unsigned long vsid,va,vpn;
	long slot;

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;
	slot = iSeries_hpte_find(vpn);
	if (slot == -1)
		panic("updateboltedpp: Could not find page to bolt\n");
	HvCallHpt_setPp(slot, newpp);
}

static void iSeries_hpte_invalidate(unsigned long slot, unsigned long va,
				    int large, int local)
{
	unsigned long hpte_v;
	unsigned long avpn = va >> 23;
	unsigned long flags;

	local_irq_save(flags);

	iSeries_hlock(slot);

	hpte_v = iSeries_hpte_getword0(slot);

	if ((HPTE_V_AVPN_VAL(hpte_v) == avpn) && (hpte_v & HPTE_V_VALID))
		HvCallHpt_invalidateSetSwBitsGet(slot, 0, 0);

	iSeries_hunlock(slot);

	local_irq_restore(flags);
}

void hpte_init_iSeries(void)
{
	ppc_md.hpte_invalidate	= iSeries_hpte_invalidate;
	ppc_md.hpte_updatepp	= iSeries_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = iSeries_hpte_updateboltedpp;
	ppc_md.hpte_insert	= iSeries_hpte_insert;
	ppc_md.hpte_remove	= iSeries_hpte_remove;

	htab_finish_init();
}
