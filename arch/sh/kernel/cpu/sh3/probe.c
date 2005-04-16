/*
 * arch/sh/kernel/cpu/sh3/probe.c
 *
 * CPU Subtype Probing for SH-3.
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2002  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>

int __init detect_cpu_and_cache_system(void)
{
	unsigned long addr0, addr1, data0, data1, data2, data3;

	jump_to_P2();
	/*
	 * Check if the entry shadows or not.
	 * When shadowed, it's 128-entry system.
	 * Otherwise, it's 256-entry system.
	 */
	addr0 = CACHE_OC_ADDRESS_ARRAY + (3 << 12);
	addr1 = CACHE_OC_ADDRESS_ARRAY + (1 << 12);

	/* First, write back & invalidate */
	data0  = ctrl_inl(addr0);
	ctrl_outl(data0&~(SH_CACHE_VALID|SH_CACHE_UPDATED), addr0);
	data1  = ctrl_inl(addr1);
	ctrl_outl(data1&~(SH_CACHE_VALID|SH_CACHE_UPDATED), addr1);

	/* Next, check if there's shadow or not */
	data0 = ctrl_inl(addr0);
	data0 ^= SH_CACHE_VALID;
	ctrl_outl(data0, addr0);
	data1 = ctrl_inl(addr1);
	data2 = data1 ^ SH_CACHE_VALID;
	ctrl_outl(data2, addr1);
	data3 = ctrl_inl(addr0);

	/* Lastly, invaliate them. */
	ctrl_outl(data0&~SH_CACHE_VALID, addr0);
	ctrl_outl(data2&~SH_CACHE_VALID, addr1);

	back_to_P1();

	cpu_data->dcache.ways		= 4;
	cpu_data->dcache.entry_shift	= 4;
	cpu_data->dcache.linesz		= L1_CACHE_BYTES;
	cpu_data->dcache.flags		= 0;

	/*
	 * 7709A/7729 has 16K cache (256-entry), while 7702 has only
	 * 2K(direct) 7702 is not supported (yet)
	 */
	if (data0 == data1 && data2 == data3) {	/* Shadow */
		cpu_data->dcache.way_incr	= (1 << 11);
		cpu_data->dcache.entry_mask	= 0x7f0;
		cpu_data->dcache.sets		= 128;
		cpu_data->type = CPU_SH7708;

		cpu_data->flags |= CPU_HAS_MMU_PAGE_ASSOC;
	} else {				/* 7709A or 7729  */
		cpu_data->dcache.way_incr	= (1 << 12);
		cpu_data->dcache.entry_mask	= 0xff0;
		cpu_data->dcache.sets		= 256;
		cpu_data->type = CPU_SH7729;

#if defined(CONFIG_CPU_SUBTYPE_SH7705)
		cpu_data->type = CPU_SH7705;

#if defined(CONFIG_SH7705_CACHE_32KB)
		cpu_data->dcache.way_incr	= (1 << 13);
		cpu_data->dcache.entry_mask	= 0x1ff0;
		cpu_data->dcache.sets		= 512;
		ctrl_outl(CCR_CACHE_32KB, CCR3);
#else
		ctrl_outl(CCR_CACHE_16KB, CCR3);
#endif
#endif
	}

	/*
	 * SH-3 doesn't have separate caches
	 */
	cpu_data->dcache.flags |= SH_CACHE_COMBINED;
	cpu_data->icache = cpu_data->dcache;

	return 0;
}

