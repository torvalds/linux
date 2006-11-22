/*
 *  PS3 pagetable management routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>

#include <asm/machdep.h>
#include <asm/lmb.h>
#include <asm/udbg.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...) do{if(0)printk(fmt);}while(0)
#endif

static hpte_t *htab;
static unsigned long htab_addr;
static unsigned char *bolttab;
static unsigned char *inusetab;

static spinlock_t ps3_bolttab_lock = SPIN_LOCK_UNLOCKED;

#define debug_dump_hpte(_a, _b, _c, _d, _e, _f, _g) \
	_debug_dump_hpte(_a, _b, _c, _d, _e, _f, _g, __func__, __LINE__)
static void _debug_dump_hpte(unsigned long pa, unsigned long va,
	unsigned long group, unsigned long bitmap, hpte_t lhpte, int psize,
	unsigned long slot, const char* func, int line)
{
	DBG("%s:%d: pa     = %lxh\n", func, line, pa);
	DBG("%s:%d: lpar   = %lxh\n", func, line,
		ps3_mm_phys_to_lpar(pa));
	DBG("%s:%d: va     = %lxh\n", func, line, va);
	DBG("%s:%d: group  = %lxh\n", func, line, group);
	DBG("%s:%d: bitmap = %lxh\n", func, line, bitmap);
	DBG("%s:%d: hpte.v = %lxh\n", func, line, lhpte.v);
	DBG("%s:%d: hpte.r = %lxh\n", func, line, lhpte.r);
	DBG("%s:%d: psize  = %xh\n", func, line, psize);
	DBG("%s:%d: slot   = %lxh\n", func, line, slot);
}

static long ps3_hpte_insert(unsigned long hpte_group, unsigned long va,
	unsigned long pa, unsigned long rflags, unsigned long vflags, int psize)
{
	unsigned long slot;
	hpte_t lhpte;
	int secondary = 0;
	unsigned long result;
	unsigned long bitmap;
	unsigned long flags;
	unsigned long p_pteg, s_pteg, b_index, b_mask, cb, ci;

	vflags &= ~HPTE_V_SECONDARY; /* this bit is ignored */

	lhpte.v = hpte_encode_v(va, psize) | vflags | HPTE_V_VALID;
	lhpte.r = hpte_encode_r(ps3_mm_phys_to_lpar(pa), psize) | rflags;

	p_pteg = hpte_group / HPTES_PER_GROUP;
	s_pteg = ~p_pteg & htab_hash_mask;

	spin_lock_irqsave(&ps3_bolttab_lock, flags);

	BUG_ON(bolttab[p_pteg] == 0xff && bolttab[s_pteg] == 0xff);

	bitmap = (inusetab[p_pteg] << 8) | inusetab[s_pteg];

	if (bitmap == 0xffff) {
		/*
		 * PTEG is full. Search for victim.
		 */
		bitmap &= ~((bolttab[p_pteg] << 8) | bolttab[s_pteg]);
		do {
			ci = mftb() & 15;
			cb = 0x8000UL >> ci;
		} while ((cb & bitmap) == 0);
	} else {
		/*
		 * search free slot in hardware order
		 *	[primary]	0, 2, 4, 6, 1, 3, 5, 7
		 *	[secondary]	0, 2, 4, 6, 1, 3, 5, 7
		 */
		for (ci = 0; ci < HPTES_PER_GROUP; ci += 2) {
			cb = 0x8000UL >> ci;
			if ((cb & bitmap) == 0)
				goto found;
		}
		for (ci = 1; ci < HPTES_PER_GROUP; ci += 2) {
			cb = 0x8000UL >> ci;
			if ((cb & bitmap) == 0)
				goto found;
		}
		for (ci = HPTES_PER_GROUP; ci < HPTES_PER_GROUP*2; ci += 2) {
			cb = 0x8000UL >> ci;
			if ((cb & bitmap) == 0)
				goto found;
		}
		for (ci = HPTES_PER_GROUP+1; ci < HPTES_PER_GROUP*2; ci += 2) {
			cb = 0x8000UL >> ci;
			if ((cb & bitmap) == 0)
				goto found;
		}
	}

found:
	if (ci < HPTES_PER_GROUP) {
		slot = p_pteg * HPTES_PER_GROUP + ci;
	} else {
		slot = s_pteg * HPTES_PER_GROUP + (ci & 7);
		/* lhpte.dw0.dw0.h = 1; */
		vflags |= HPTE_V_SECONDARY;
		lhpte.v |= HPTE_V_SECONDARY;
	}

	result = lv1_write_htab_entry(0, slot, lhpte.v, lhpte.r);

	if (result) {
		debug_dump_hpte(pa, va, hpte_group, bitmap, lhpte, psize, slot);
		BUG();
	}

	/*
	 * If used slot is not in primary HPTE group,
	 * the slot should be in secondary HPTE group.
	 */

	if ((hpte_group ^ slot) & ~(HPTES_PER_GROUP - 1)) {
		secondary = 1;
		b_index = s_pteg;
	} else {
		secondary = 0;
		b_index = p_pteg;
	}

	b_mask = (lhpte.v & HPTE_V_BOLTED) ? 1 << 7 : 0 << 7;
	bolttab[b_index] |= b_mask >> (slot & 7);
	b_mask = 1 << 7;
	inusetab[b_index] |= b_mask >> (slot & 7);
	spin_unlock_irqrestore(&ps3_bolttab_lock, flags);

	return (slot & 7) | (secondary << 3);
}

static long ps3_hpte_remove(unsigned long hpte_group)
{
	panic("ps3_hpte_remove() not implemented");
	return 0;
}

static long ps3_hpte_updatepp(unsigned long slot, unsigned long newpp,
	unsigned long va, int psize, int local)
{
	unsigned long flags;
	unsigned long result;
	unsigned long pteg, bit;
	unsigned long hpte_v, want_v;

	want_v = hpte_encode_v(va, psize);

	spin_lock_irqsave(&ps3_bolttab_lock, flags);

	hpte_v = htab[slot].v;
	if (!HPTE_V_COMPARE(hpte_v, want_v) || !(hpte_v & HPTE_V_VALID)) {
		spin_unlock_irqrestore(&ps3_bolttab_lock, flags);

		/* ps3_hpte_insert() will be used to update PTE */
		return -1;
	}

	result = lv1_write_htab_entry(0, slot, 0, 0);

	if (result) {
		DBG("%s: va=%lx slot=%lx psize=%d result = %ld (0x%lx)\n",
		       __func__, va, slot, psize, result, result);
		BUG();
	}

	pteg = slot / HPTES_PER_GROUP;
	bit = slot % HPTES_PER_GROUP;
	inusetab[pteg] &= ~(0x80 >> bit);

	spin_unlock_irqrestore(&ps3_bolttab_lock, flags);

	/* ps3_hpte_insert() will be used to update PTE */
	return -1;
}

static void ps3_hpte_updateboltedpp(unsigned long newpp, unsigned long ea,
	int psize)
{
	panic("ps3_hpte_updateboltedpp() not implemented");
}

static void ps3_hpte_invalidate(unsigned long slot, unsigned long va,
	int psize, int local)
{
	unsigned long flags;
	unsigned long result;
	unsigned long pteg, bit;

	spin_lock_irqsave(&ps3_bolttab_lock, flags);
	result = lv1_write_htab_entry(0, slot, 0, 0);

	if (result) {
		DBG("%s: va=%lx slot=%lx psize=%d result = %ld (0x%lx)\n",
		       __func__, va, slot, psize, result, result);
		BUG();
	}

	pteg = slot / HPTES_PER_GROUP;
	bit = slot % HPTES_PER_GROUP;
	inusetab[pteg] &= ~(0x80 >> bit);
	spin_unlock_irqrestore(&ps3_bolttab_lock, flags);
}

static void ps3_hpte_clear(void)
{
	lv1_unmap_htab(htab_addr);
}

void __init ps3_hpte_init(unsigned long htab_size)
{
	long bitmap_size;

	DBG(" -> %s:%d\n", __func__, __LINE__);

	ppc_md.hpte_invalidate = ps3_hpte_invalidate;
	ppc_md.hpte_updatepp = ps3_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = ps3_hpte_updateboltedpp;
	ppc_md.hpte_insert = ps3_hpte_insert;
	ppc_md.hpte_remove = ps3_hpte_remove;
	ppc_md.hpte_clear_all = ps3_hpte_clear;

	ppc64_pft_size = __ilog2(htab_size);

	bitmap_size = htab_size / sizeof(hpte_t) / 8;

	bolttab = __va(lmb_alloc(bitmap_size, 1));
	inusetab = __va(lmb_alloc(bitmap_size, 1));

	memset(bolttab, 0, bitmap_size);
	memset(inusetab, 0, bitmap_size);

	DBG(" <- %s:%d\n", __func__, __LINE__);
}

void __init ps3_map_htab(void)
{
	long result;
	unsigned long htab_size = (1UL << ppc64_pft_size);

	result = lv1_map_htab(0, &htab_addr);

	htab = (hpte_t *)__ioremap(htab_addr, htab_size, PAGE_READONLY_X);

	DBG("%s:%d: lpar %016lxh, virt %016lxh\n", __func__, __LINE__,
		htab_addr, (unsigned long)htab);
}
