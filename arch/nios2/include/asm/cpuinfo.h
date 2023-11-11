/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 */

#ifndef _ASM_NIOS2_CPUINFO_H
#define _ASM_NIOS2_CPUINFO_H

#include <linux/types.h>

struct cpuinfo {
	/* Core CPU configuration */
	char cpu_impl[12];
	u32 cpu_clock_freq;
	bool mmu;
	bool has_div;
	bool has_mul;
	bool has_mulx;
	bool has_bmx;
	bool has_cdx;

	/* CPU caches */
	u32 icache_line_size;
	u32 icache_size;
	u32 dcache_line_size;
	u32 dcache_size;

	/* TLB */
	u32 tlb_pid_num_bits;	/* number of bits used for the PID in TLBMISC */
	u32 tlb_num_ways;
	u32 tlb_num_ways_log2;
	u32 tlb_num_entries;
	u32 tlb_num_lines;
	u32 tlb_ptr_sz;

	/* Addresses */
	u32 reset_addr;
	u32 exception_addr;
	u32 fast_tlb_miss_exc_addr;
};

extern struct cpuinfo cpuinfo;

extern void setup_cpuinfo(void);

#endif /* _ASM_NIOS2_CPUINFO_H */
