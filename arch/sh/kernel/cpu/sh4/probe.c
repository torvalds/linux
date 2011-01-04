/*
 * arch/sh/kernel/cpu/sh4/probe.c
 *
 * CPU Subtype Probing for SH-4.
 *
 * Copyright (C) 2001 - 2007  Paul Mundt
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

void __cpuinit cpu_probe(void)
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

	pvr = (__raw_readl(CCN_PVR) >> 8) & 0xffffff;
	prr = (__raw_readl(CCN_PRR) >> 4) & 0xff;
	cvr = (__raw_readl(CCN_CVR));

	/*
	 * Setup some sane SH-4 defaults for the icache
	 */
	boot_cpu_data.icache.way_incr		= (1 << 13);
	boot_cpu_data.icache.entry_shift	= 5;
	boot_cpu_data.icache.sets		= 256;
	boot_cpu_data.icache.ways		= 1;
	boot_cpu_data.icache.linesz		= L1_CACHE_BYTES;

	/*
	 * And again for the dcache ..
	 */
	boot_cpu_data.dcache.way_incr		= (1 << 14);
	boot_cpu_data.dcache.entry_shift	= 5;
	boot_cpu_data.dcache.sets		= 512;
	boot_cpu_data.dcache.ways		= 1;
	boot_cpu_data.dcache.linesz		= L1_CACHE_BYTES;

	/* We don't know the chip cut */
	boot_cpu_data.cut_major = boot_cpu_data.cut_minor = -1;

	/*
	 * Setup some generic flags we can probe on SH-4A parts
	 */
	if (((pvr >> 16) & 0xff) == 0x10) {
		boot_cpu_data.family = CPU_FAMILY_SH4A;

		if ((cvr & 0x10000000) == 0) {
			boot_cpu_data.flags |= CPU_HAS_DSP;
			boot_cpu_data.family = CPU_FAMILY_SH4AL_DSP;
		}

		boot_cpu_data.flags |= CPU_HAS_LLSC | CPU_HAS_PERF_COUNTER;
		boot_cpu_data.cut_major = pvr & 0x7f;

		boot_cpu_data.icache.ways = 4;
		boot_cpu_data.dcache.ways = 4;
	} else {
		/* And some SH-4 defaults.. */
		boot_cpu_data.flags |= CPU_HAS_PTEA | CPU_HAS_FPU;
		boot_cpu_data.family = CPU_FAMILY_SH4;
	}

	/* FPU detection works for almost everyone */
	if ((cvr & 0x20000000))
		boot_cpu_data.flags |= CPU_HAS_FPU;

	/* Mask off the upper chip ID */
	pvr &= 0xffff;

	/*
	 * Probe the underlying processor version/revision and
	 * adjust cpu_data setup accordingly.
	 */
	switch (pvr) {
	case 0x205:
		boot_cpu_data.type = CPU_SH7750;
		boot_cpu_data.flags |= CPU_HAS_P2_FLUSH_BUG |
				       CPU_HAS_PERF_COUNTER;
		break;
	case 0x206:
		boot_cpu_data.type = CPU_SH7750S;
		boot_cpu_data.flags |= CPU_HAS_P2_FLUSH_BUG |
				       CPU_HAS_PERF_COUNTER;
		break;
	case 0x1100:
		boot_cpu_data.type = CPU_SH7751;
		break;
	case 0x2001:
	case 0x2004:
		boot_cpu_data.type = CPU_SH7770;
		break;
	case 0x2006:
	case 0x200A:
		if (prr == 0x61)
			boot_cpu_data.type = CPU_SH7781;
		else if (prr == 0xa1)
			boot_cpu_data.type = CPU_SH7763;
		else
			boot_cpu_data.type = CPU_SH7780;

		break;
	case 0x3000:
	case 0x3003:
	case 0x3009:
		boot_cpu_data.type = CPU_SH7343;
		break;
	case 0x3004:
	case 0x3007:
		boot_cpu_data.type = CPU_SH7785;
		break;
	case 0x4004:
	case 0x4005:
		boot_cpu_data.type = CPU_SH7786;
		boot_cpu_data.flags |= CPU_HAS_PTEAEX | CPU_HAS_L2_CACHE;
		break;
	case 0x3008:
		switch (prr) {
		case 0x50:
		case 0x51:
			boot_cpu_data.type = CPU_SH7723;
			boot_cpu_data.flags |= CPU_HAS_L2_CACHE;
			break;
		case 0x70:
			boot_cpu_data.type = CPU_SH7366;
			break;
		case 0xa0:
		case 0xa1:
			boot_cpu_data.type = CPU_SH7722;
			break;
		}
		break;
	case 0x300b:
		switch (prr) {
		case 0x20:
			boot_cpu_data.type = CPU_SH7724;
			boot_cpu_data.flags |= CPU_HAS_L2_CACHE;
			break;
		case 0x10:
			boot_cpu_data.type = CPU_SH7757;
			break;
		}
		break;
	case 0x4000:	/* 1st cut */
	case 0x4001:	/* 2nd cut */
		boot_cpu_data.type = CPU_SHX3;
		break;
	case 0x700:
		boot_cpu_data.type = CPU_SH4_501;
		boot_cpu_data.flags &= ~CPU_HAS_FPU;
		boot_cpu_data.icache.ways = 2;
		boot_cpu_data.dcache.ways = 2;
		break;
	case 0x600:
		boot_cpu_data.type = CPU_SH4_202;
		boot_cpu_data.icache.ways = 2;
		boot_cpu_data.dcache.ways = 2;
		break;
	case 0x500 ... 0x501:
		switch (prr) {
		case 0x10:
			boot_cpu_data.type = CPU_SH7750R;
			break;
		case 0x11:
			boot_cpu_data.type = CPU_SH7751R;
			break;
		case 0x50 ... 0x5f:
			boot_cpu_data.type = CPU_SH7760;
			break;
		}

		boot_cpu_data.icache.ways = 2;
		boot_cpu_data.dcache.ways = 2;

		break;
	}

	/*
	 * On anything that's not a direct-mapped cache, look to the CVR
	 * for I/D-cache specifics.
	 */
	if (boot_cpu_data.icache.ways > 1) {
		size = sizes[(cvr >> 20) & 0xf];
		boot_cpu_data.icache.way_incr	= (size >> 1);
		boot_cpu_data.icache.sets	= (size >> 6);

	}

	/* And the rest of the D-cache */
	if (boot_cpu_data.dcache.ways > 1) {
		size = sizes[(cvr >> 16) & 0xf];
		boot_cpu_data.dcache.way_incr	= (size >> 1);
		boot_cpu_data.dcache.sets	= (size >> 6);
	}

	/*
	 * SH-4A's have an optional PIPT L2.
	 */
	if (boot_cpu_data.flags & CPU_HAS_L2_CACHE) {
		/*
		 * Verify that it really has something hooked up, this
		 * is the safety net for CPUs that have optional L2
		 * support yet do not implement it.
		 */
		if ((cvr & 0xf) == 0)
			boot_cpu_data.flags &= ~CPU_HAS_L2_CACHE;
		else {
			/*
			 * Silicon and specifications have clearly never
			 * met..
			 */
			cvr ^= 0xf;

			/*
			 * Size calculation is much more sensible
			 * than it is for the L1.
			 *
			 * Sizes are 128KB, 256KB, 512KB, and 1MB.
			 */
			size = (cvr & 0xf) << 17;

			boot_cpu_data.scache.way_incr		= (1 << 16);
			boot_cpu_data.scache.entry_shift	= 5;
			boot_cpu_data.scache.ways		= 4;
			boot_cpu_data.scache.linesz		= L1_CACHE_BYTES;

			boot_cpu_data.scache.entry_mask	=
				(boot_cpu_data.scache.way_incr -
				 boot_cpu_data.scache.linesz);

			boot_cpu_data.scache.sets	= size /
				(boot_cpu_data.scache.linesz *
				 boot_cpu_data.scache.ways);

			boot_cpu_data.scache.way_size	=
				(boot_cpu_data.scache.sets *
				 boot_cpu_data.scache.linesz);
		}
	}
}
