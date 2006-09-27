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
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>

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

	pvr = (ctrl_inl(CCN_PVR) >> 8) & 0xffff;
	prr = (ctrl_inl(CCN_PRR) >> 4) & 0xff;
	cvr = (ctrl_inl(CCN_CVR));

	/*
	 * Setup some sane SH-4 defaults for the icache
	 */
	cpu_data->icache.way_incr	= (1 << 13);
	cpu_data->icache.entry_shift	= 5;
	cpu_data->icache.entry_mask	= 0x1fe0;
	cpu_data->icache.sets		= 256;
	cpu_data->icache.ways		= 1;
	cpu_data->icache.linesz		= L1_CACHE_BYTES;

	/*
	 * And again for the dcache ..
	 */
	cpu_data->dcache.way_incr	= (1 << 14);
	cpu_data->dcache.entry_shift	= 5;
	cpu_data->dcache.entry_mask	= 0x3fe0;
	cpu_data->dcache.sets		= 512;
	cpu_data->dcache.ways		= 1;
	cpu_data->dcache.linesz		= L1_CACHE_BYTES;

	/*
	 * Probe the underlying processor version/revision and
	 * adjust cpu_data setup accordingly.
	 */
	switch (pvr) {
	case 0x205:
		cpu_data->type = CPU_SH7750;
		cpu_data->flags |= CPU_HAS_P2_FLUSH_BUG | CPU_HAS_FPU |
				   CPU_HAS_PERF_COUNTER | CPU_HAS_PTEA;
		break;
	case 0x206:
		cpu_data->type = CPU_SH7750S;
		cpu_data->flags |= CPU_HAS_P2_FLUSH_BUG | CPU_HAS_FPU |
				   CPU_HAS_PERF_COUNTER | CPU_HAS_PTEA;
		break;
	case 0x1100:
		cpu_data->type = CPU_SH7751;
		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_PTEA;
		break;
	case 0x2000:
		cpu_data->type = CPU_SH73180;
		cpu_data->icache.ways = 4;
		cpu_data->dcache.ways = 4;
		cpu_data->flags |= CPU_HAS_LLSC;
		break;
	case 0x2001:
	case 0x2004:
		cpu_data->type = CPU_SH7770;
		cpu_data->icache.ways = 4;
		cpu_data->dcache.ways = 4;

		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_LLSC;
		break;
	case 0x2006:
	case 0x200A:
		if (prr == 0x61)
			cpu_data->type = CPU_SH7781;
		else
			cpu_data->type = CPU_SH7780;

		cpu_data->icache.ways = 4;
		cpu_data->dcache.ways = 4;

		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_PERF_COUNTER |
				   CPU_HAS_LLSC;
		break;
	case 0x3000:
	case 0x3003:
		cpu_data->type = CPU_SH7343;
		cpu_data->icache.ways = 4;
		cpu_data->dcache.ways = 4;
		cpu_data->flags |= CPU_HAS_LLSC;
		break;
	case 0x8000:
		cpu_data->type = CPU_ST40RA;
		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_PTEA;
		break;
	case 0x8100:
		cpu_data->type = CPU_ST40GX1;
		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_PTEA;
		break;
	case 0x700:
		cpu_data->type = CPU_SH4_501;
		cpu_data->icache.ways = 2;
		cpu_data->dcache.ways = 2;
		cpu_data->flags |= CPU_HAS_PTEA;
		break;
	case 0x600:
		cpu_data->type = CPU_SH4_202;
		cpu_data->icache.ways = 2;
		cpu_data->dcache.ways = 2;
		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_PTEA;
		break;
	case 0x500 ... 0x501:
		switch (prr) {
		case 0x10:
			cpu_data->type = CPU_SH7750R;
			break;
		case 0x11:
			cpu_data->type = CPU_SH7751R;
			break;
		case 0x50 ... 0x5f:
			cpu_data->type = CPU_SH7760;
			break;
		}

		cpu_data->icache.ways = 2;
		cpu_data->dcache.ways = 2;

		cpu_data->flags |= CPU_HAS_FPU | CPU_HAS_PTEA;

		break;
	default:
		cpu_data->type = CPU_SH_NONE;
		break;
	}

#ifdef CONFIG_SH_DIRECT_MAPPED
	cpu_data->icache.ways = 1;
	cpu_data->dcache.ways = 1;
#endif

	/*
	 * On anything that's not a direct-mapped cache, look to the CVR
	 * for I/D-cache specifics.
	 */
	if (cpu_data->icache.ways > 1) {
		size = sizes[(cvr >> 20) & 0xf];
		cpu_data->icache.way_incr	= (size >> 1);
		cpu_data->icache.sets		= (size >> 6);
		cpu_data->icache.entry_mask	=
			(cpu_data->icache.way_incr - (1 << 5));
	}

	cpu_data->icache.way_size = cpu_data->icache.sets *
				    cpu_data->icache.linesz;

	if (cpu_data->dcache.ways > 1) {
		size = sizes[(cvr >> 16) & 0xf];
		cpu_data->dcache.way_incr	= (size >> 1);
		cpu_data->dcache.sets		= (size >> 6);
		cpu_data->dcache.entry_mask	=
			(cpu_data->dcache.way_incr - (1 << 5));
	}

	cpu_data->dcache.way_size = cpu_data->dcache.sets *
				    cpu_data->dcache.linesz;

	return 0;
}
