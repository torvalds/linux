/*
 * arch/sh/kernel/cpu/sh4/probe.c
 *
 * CPU Subtype Probing for SH-4.
 *
 * Copyright (C) 2001 - 2006  Paul Mundt
 * Copyright (C) 2003  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <asm/processor.h>
#include <asm/cache.h>

int __init detect_cpu_and_cache_system(void)
{
	unsigned long pvr, prr, cvr;
	unsigned long size;

	static unsigned long sizes[16] = {
		[1] = (1 << 12),
		[2] = (1 << 13),
		[4] = (1 << 14),
		[8] = (1 << 15),
		[9] = (1 << 16)
	};

	pvr = (ctrl_inl(CCN_PVR) >> 8) & 0xffffff;
	prr = (ctrl_inl(CCN_PRR) >> 4) & 0xff;
	cvr = (ctrl_inl(CCN_CVR));

	/*
	 * Setup some sane SH-4 defaults for the icache
	 */
	current_cpu_data.icache.way_incr	= (1 << 13);
	current_cpu_data.icache.entry_shift	= 5;
	current_cpu_data.icache.sets		= 256;
	current_cpu_data.icache.ways		= 1;
	current_cpu_data.icache.linesz		= L1_CACHE_BYTES;

	/*
	 * And again for the dcache ..
	 */
	current_cpu_data.dcache.way_incr	= (1 << 14);
	current_cpu_data.dcache.entry_shift	= 5;
	current_cpu_data.dcache.sets		= 512;
	current_cpu_data.dcache.ways		= 1;
	current_cpu_data.dcache.linesz		= L1_CACHE_BYTES;

	/*
	 * Setup some generic flags we can probe
	 * (L2 and DSP detection only work on SH-4A)
	 */
	if (((pvr >> 16) & 0xff) == 0x10) {
		if ((cvr & 0x02000000) == 0)
			current_cpu_data.flags |= CPU_HAS_L2_CACHE;
		if ((cvr & 0x10000000) == 0)
			current_cpu_data.flags |= CPU_HAS_DSP;

		current_cpu_data.flags |= CPU_HAS_LLSC;
	}

	/* FPU detection works for everyone */
	if ((cvr & 0x20000000) == 1)
		current_cpu_data.flags |= CPU_HAS_FPU;

	/* Mask off the upper chip ID */
	pvr &= 0xffff;

	/*
	 * Probe the underlying processor version/revision and
	 * adjust cpu_data setup accordingly.
	 */
	switch (pvr) {
	case 0x205:
		current_cpu_data.type = CPU_SH7750;
		current_cpu_data.flags |= CPU_HAS_P2_FLUSH_BUG | CPU_HAS_FPU |
				   CPU_HAS_PERF_COUNTER;
		break;
	case 0x206:
		current_cpu_data.type = CPU_SH7750S;
		current_cpu_data.flags |= CPU_HAS_P2_FLUSH_BUG | CPU_HAS_FPU |
				   CPU_HAS_PERF_COUNTER;
		break;
	case 0x1100:
		current_cpu_data.type = CPU_SH7751;
		current_cpu_data.flags |= CPU_HAS_FPU;
		break;
	case 0x2000:
		current_cpu_data.type = CPU_SH73180;
		current_cpu_data.icache.ways = 4;
		current_cpu_data.dcache.ways = 4;
		current_cpu_data.flags |= CPU_HAS_LLSC;
		break;
	case 0x2001:
	case 0x2004:
		current_cpu_data.type = CPU_SH7770;
		current_cpu_data.icache.ways = 4;
		current_cpu_data.dcache.ways = 4;

		current_cpu_data.flags |= CPU_HAS_FPU | CPU_HAS_LLSC;
		break;
	case 0x2006:
	case 0x200A:
		if (prr == 0x61)
			current_cpu_data.type = CPU_SH7781;
		else
			current_cpu_data.type = CPU_SH7780;

		current_cpu_data.icache.ways = 4;
		current_cpu_data.dcache.ways = 4;

		current_cpu_data.flags |= CPU_HAS_FPU | CPU_HAS_PERF_COUNTER |
				   CPU_HAS_LLSC;
		break;
	case 0x3000:
	case 0x3003:
	case 0x3009:
		current_cpu_data.type = CPU_SH7343;
		current_cpu_data.icache.ways = 4;
		current_cpu_data.dcache.ways = 4;
		current_cpu_data.flags |= CPU_HAS_LLSC;
		break;
	case 0x3004:
	case 0x3007:
		current_cpu_data.type = CPU_SH7785;
		current_cpu_data.icache.ways = 4;
		current_cpu_data.dcache.ways = 4;
		current_cpu_data.flags |= CPU_HAS_FPU | CPU_HAS_PERF_COUNTER |
					  CPU_HAS_LLSC;
		break;
	case 0x3008:
		if (prr == 0xa0) {
			current_cpu_data.type = CPU_SH7722;
			current_cpu_data.icache.ways = 4;
			current_cpu_data.dcache.ways = 4;
			current_cpu_data.flags |= CPU_HAS_LLSC;
		}
		break;
	case 0x8000:
		current_cpu_data.type = CPU_ST40RA;
		current_cpu_data.flags |= CPU_HAS_FPU;
		break;
	case 0x8100:
		current_cpu_data.type = CPU_ST40GX1;
		current_cpu_data.flags |= CPU_HAS_FPU;
		break;
	case 0x700:
		current_cpu_data.type = CPU_SH4_501;
		current_cpu_data.icache.ways = 2;
		current_cpu_data.dcache.ways = 2;
		break;
	case 0x600:
		current_cpu_data.type = CPU_SH4_202;
		current_cpu_data.icache.ways = 2;
		current_cpu_data.dcache.ways = 2;
		current_cpu_data.flags |= CPU_HAS_FPU;
		break;
	case 0x500 ... 0x501:
		switch (prr) {
		case 0x10:
			current_cpu_data.type = CPU_SH7750R;
			break;
		case 0x11:
			current_cpu_data.type = CPU_SH7751R;
			break;
		case 0x50 ... 0x5f:
			current_cpu_data.type = CPU_SH7760;
			break;
		}

		current_cpu_data.icache.ways = 2;
		current_cpu_data.dcache.ways = 2;

		current_cpu_data.flags |= CPU_HAS_FPU;

		break;
	default:
		current_cpu_data.type = CPU_SH_NONE;
		break;
	}

#ifdef CONFIG_SH_DIRECT_MAPPED
	current_cpu_data.icache.ways = 1;
	current_cpu_data.dcache.ways = 1;
#endif

#ifdef CONFIG_CPU_HAS_PTEA
	current_cpu_data.flags |= CPU_HAS_PTEA;
#endif

	/*
	 * On anything that's not a direct-mapped cache, look to the CVR
	 * for I/D-cache specifics.
	 */
	if (current_cpu_data.icache.ways > 1) {
		size = sizes[(cvr >> 20) & 0xf];
		current_cpu_data.icache.way_incr	= (size >> 1);
		current_cpu_data.icache.sets		= (size >> 6);

	}

	/* And the rest of the D-cache */
	if (current_cpu_data.dcache.ways > 1) {
		size = sizes[(cvr >> 16) & 0xf];
		current_cpu_data.dcache.way_incr	= (size >> 1);
		current_cpu_data.dcache.sets		= (size >> 6);
	}

	/*
	 * Setup the L2 cache desc
	 *
	 * SH-4A's have an optional PIPT L2.
	 */
	if (current_cpu_data.flags & CPU_HAS_L2_CACHE) {
		/*
		 * Size calculation is much more sensible
		 * than it is for the L1.
		 *
		 * Sizes are 128KB, 258KB, 512KB, and 1MB.
		 */
		size = (cvr & 0xf) << 17;

		BUG_ON(!size);

		current_cpu_data.scache.way_incr	= (1 << 16);
		current_cpu_data.scache.entry_shift	= 5;
		current_cpu_data.scache.ways		= 4;
		current_cpu_data.scache.linesz		= L1_CACHE_BYTES;

		current_cpu_data.scache.entry_mask	=
			(current_cpu_data.scache.way_incr -
			 current_cpu_data.scache.linesz);

		current_cpu_data.scache.sets		= size /
			(current_cpu_data.scache.linesz *
			 current_cpu_data.scache.ways);

		current_cpu_data.scache.way_size	=
			(current_cpu_data.scache.sets *
			 current_cpu_data.scache.linesz);
	}

	return 0;
}
