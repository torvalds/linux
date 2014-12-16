/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _ASM_NIOS2_CPUINFO_H
#define _ASM_NIOS2_CPUINFO_H

#include <linux/types.h>

struct cpuinfo {
	/* Core CPU configuration */
	char cpu_impl[12];
	u32 cpu_clock_freq;
	u32 mmu;
	u32 has_div;
	u32 has_mul;
	u32 has_mulx;

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
