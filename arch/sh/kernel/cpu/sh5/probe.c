/*
 * arch/sh/kernel/cpu/sh5/probe.c
 *
 * CPU Subtype Probing for SH-5.
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <asm/processor.h>
#include <asm/cache.h>

int __init detect_cpu_and_cache_system(void)
{
	unsigned long long cir;

	/* Do peeks in real mode to avoid having to set up a mapping for the
	   WPC registers. On SH5-101 cut2, such a mapping would be exposed to
	   an address translation erratum which would make it hard to set up
	   correctly. */
	cir = peek_real_address_q(0x0d000008);
	if ((cir & 0xffff) == 0x5103) {
		boot_cpu_data.type = CPU_SH5_103;
	} else if (((cir >> 32) & 0xffff) == 0x51e2) {
		/* CPU.VCR aliased at CIR address on SH5-101 */
		boot_cpu_data.type = CPU_SH5_101;
	} else {
		boot_cpu_data.type = CPU_SH_NONE;
	}

	/*
	 * First, setup some sane values for the I-cache.
	 */
	boot_cpu_data.icache.ways		= 4;
	boot_cpu_data.icache.sets		= 256;
	boot_cpu_data.icache.linesz		= L1_CACHE_BYTES;

#if 0
	/*
	 * FIXME: This can probably be cleaned up a bit as well.. for example,
	 * do we really need the way shift _and_ the way_step_shift ?? Judging
	 * by the existing code, I would guess no.. is there any valid reason
	 * why we need to be tracking this around?
	 */
	boot_cpu_data.icache.way_shift		= 13;
	boot_cpu_data.icache.entry_shift	= 5;
	boot_cpu_data.icache.set_shift		= 4;
	boot_cpu_data.icache.way_step_shift	= 16;
	boot_cpu_data.icache.asid_shift		= 2;

	/*
	 * way offset = cache size / associativity, so just don't factor in
	 * associativity in the first place..
	 */
	boot_cpu_data.icache.way_ofs	= boot_cpu_data.icache.sets *
					  boot_cpu_data.icache.linesz;

	boot_cpu_data.icache.asid_mask		= 0x3fc;
	boot_cpu_data.icache.idx_mask		= 0x1fe0;
	boot_cpu_data.icache.epn_mask		= 0xffffe000;
#endif

	boot_cpu_data.icache.flags		= 0;

	/* A trivial starting point.. */
	memcpy(&boot_cpu_data.dcache,
	       &boot_cpu_data.icache, sizeof(struct cache_info));

	return 0;
}
