/*
 *  PS3 pagetable management routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006, 2007 Sony Corporation
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
#include <linux/memblock.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/lv1call.h>
#include <asm/ps3fb.h>

#define PS3_VERBOSE_RESULT
#include "platform.h"

/**
 * enum lpar_vas_id - id of LPAR virtual address space.
 * @lpar_vas_id_current: Current selected virtual address space
 *
 * Identify the target LPAR address space.
 */

enum ps3_lpar_vas_id {
	PS3_LPAR_VAS_ID_CURRENT = 0,
};


static DEFINE_SPINLOCK(ps3_htab_lock);

static long ps3_hpte_insert(unsigned long hpte_group, unsigned long vpn,
	unsigned long pa, unsigned long rflags, unsigned long vflags,
	int psize, int ssize)
{
	int result;
	u64 hpte_v, hpte_r;
	u64 inserted_index;
	u64 evicted_v, evicted_r;
	u64 hpte_v_array[4], hpte_rs;
	unsigned long flags;
	long ret = -1;

	/*
	 * lv1_insert_htab_entry() will search for victim
	 * entry in both primary and secondary pte group
	 */
	vflags &= ~HPTE_V_SECONDARY;

	hpte_v = hpte_encode_v(vpn, psize, ssize) | vflags | HPTE_V_VALID;
	hpte_r = hpte_encode_r(ps3_mm_phys_to_lpar(pa), psize) | rflags;

	spin_lock_irqsave(&ps3_htab_lock, flags);

	/* talk hvc to replace entries BOLTED == 0 */
	result = lv1_insert_htab_entry(PS3_LPAR_VAS_ID_CURRENT, hpte_group,
				       hpte_v, hpte_r,
				       HPTE_V_BOLTED, 0,
				       &inserted_index,
				       &evicted_v, &evicted_r);

	if (result) {
		/* all entries bolted !*/
		pr_info("%s:result=%s vpn=%lx pa=%lx ix=%lx v=%llx r=%llx\n",
			__func__, ps3_result(result), vpn, pa, hpte_group,
			hpte_v, hpte_r);
		BUG();
	}

	/*
	 * see if the entry is inserted into secondary pteg
	 */
	result = lv1_read_htab_entries(PS3_LPAR_VAS_ID_CURRENT,
				       inserted_index & ~0x3UL,
				       &hpte_v_array[0], &hpte_v_array[1],
				       &hpte_v_array[2], &hpte_v_array[3],
				       &hpte_rs);
	BUG_ON(result);

	if (hpte_v_array[inserted_index % 4] & HPTE_V_SECONDARY)
		ret = (inserted_index & 7) | (1 << 3);
	else
		ret = inserted_index & 7;

	spin_unlock_irqrestore(&ps3_htab_lock, flags);

	return ret;
}

static long ps3_hpte_remove(unsigned long hpte_group)
{
	panic("ps3_hpte_remove() not implemented");
	return 0;
}

static long ps3_hpte_updatepp(unsigned long slot, unsigned long newpp,
	unsigned long vpn, int psize, int ssize, int local)
{
	int result;
	u64 hpte_v, want_v, hpte_rs;
	u64 hpte_v_array[4];
	unsigned long flags;
	long ret;

	want_v = hpte_encode_v(vpn, psize, ssize);

	spin_lock_irqsave(&ps3_htab_lock, flags);

	result = lv1_read_htab_entries(PS3_LPAR_VAS_ID_CURRENT, slot & ~0x3UL,
				       &hpte_v_array[0], &hpte_v_array[1],
				       &hpte_v_array[2], &hpte_v_array[3],
				       &hpte_rs);

	if (result) {
		pr_info("%s: result=%s read vpn=%lx slot=%lx psize=%d\n",
			__func__, ps3_result(result), vpn, slot, psize);
		BUG();
	}

	hpte_v = hpte_v_array[slot % 4];

	/*
	 * As lv1_read_htab_entries() does not give us the RPN, we can
	 * not synthesize the new hpte_r value here, and therefore can
	 * not update the hpte with lv1_insert_htab_entry(), so we
	 * instead invalidate it and ask the caller to update it via
	 * ps3_hpte_insert() by returning a -1 value.
	 */
	if (!HPTE_V_COMPARE(hpte_v, want_v) || !(hpte_v & HPTE_V_VALID)) {
		/* not found */
		ret = -1;
	} else {
		/* entry found, just invalidate it */
		result = lv1_write_htab_entry(PS3_LPAR_VAS_ID_CURRENT,
					      slot, 0, 0);
		ret = -1;
	}

	spin_unlock_irqrestore(&ps3_htab_lock, flags);
	return ret;
}

static void ps3_hpte_updateboltedpp(unsigned long newpp, unsigned long ea,
	int psize, int ssize)
{
	panic("ps3_hpte_updateboltedpp() not implemented");
}

static void ps3_hpte_invalidate(unsigned long slot, unsigned long vpn,
	int psize, int ssize, int local)
{
	unsigned long flags;
	int result;

	spin_lock_irqsave(&ps3_htab_lock, flags);

	result = lv1_write_htab_entry(PS3_LPAR_VAS_ID_CURRENT, slot, 0, 0);

	if (result) {
		pr_info("%s: result=%s vpn=%lx slot=%lx psize=%d\n",
			__func__, ps3_result(result), vpn, slot, psize);
		BUG();
	}

	spin_unlock_irqrestore(&ps3_htab_lock, flags);
}

static void ps3_hpte_clear(void)
{
	unsigned long hpte_count = (1UL << ppc64_pft_size) >> 4;
	u64 i;

	for (i = 0; i < hpte_count; i++)
		lv1_write_htab_entry(PS3_LPAR_VAS_ID_CURRENT, i, 0, 0);

	ps3_mm_shutdown();
	ps3_mm_vas_destroy();
}

void __init ps3_hpte_init(unsigned long htab_size)
{
	ppc_md.hpte_invalidate = ps3_hpte_invalidate;
	ppc_md.hpte_updatepp = ps3_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = ps3_hpte_updateboltedpp;
	ppc_md.hpte_insert = ps3_hpte_insert;
	ppc_md.hpte_remove = ps3_hpte_remove;
	ppc_md.hpte_clear_all = ps3_hpte_clear;

	ppc64_pft_size = __ilog2(htab_size);
}

