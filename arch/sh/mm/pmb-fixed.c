/*
 * arch/sh/mm/fixed_pmb.c
 *
 * Copyright (C) 2009  Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>

static int __uses_jump_to_uncached fixed_pmb_init(void)
{
	int i;
	unsigned long addr, data;

	jump_to_uncached();

	for (i = 0; i < PMB_ENTRY_MAX; i++) {
		addr = PMB_DATA + (i << PMB_E_SHIFT);
		data = ctrl_inl(addr);
		if (!(data & PMB_V))
			continue;

		if (data & PMB_C) {
#if defined(CONFIG_CACHE_WRITETHROUGH)
			data |= PMB_WT;
#elif defined(CONFIG_CACHE_WRITEBACK)
			data &= ~PMB_WT;
#else
			data &= ~(PMB_C | PMB_WT);
#endif
		}
		ctrl_outl(data, addr);
	}

	back_to_cached();

	return 0;
}
arch_initcall(fixed_pmb_init);
