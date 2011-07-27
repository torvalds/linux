/*
 * Generic support for queying CPU info
 *
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <jwilliams@itee.uq.edu.au>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_CPUINFO_H
#define _ASM_MICROBLAZE_CPUINFO_H

#include <asm/prom.h>

/* CPU Version and FPGA Family code conversion table type */
struct cpu_ver_key {
	const char *s;
	const unsigned k;
};

extern const struct cpu_ver_key cpu_ver_lookup[];

struct family_string_key {
	const char *s;
	const unsigned k;
};

extern const struct family_string_key family_string_lookup[];

struct cpuinfo {
	/* Core CPU configuration */
	u32 use_instr;
	u32 use_mult;
	u32 use_fpu;
	u32 use_exc;
	u32 ver_code;
	u32 mmu;
	u32 mmu_privins;
	u32 endian;

	/* CPU caches */
	u32 use_icache;
	u32 icache_tagbits;
	u32 icache_write;
	u32 icache_line_length;
	u32 icache_size;
	unsigned long icache_base;
	unsigned long icache_high;

	u32 use_dcache;
	u32 dcache_tagbits;
	u32 dcache_write;
	u32 dcache_line_length;
	u32 dcache_size;
	u32 dcache_wb;
	unsigned long dcache_base;
	unsigned long dcache_high;

	/* Bus connections */
	u32 use_dopb;
	u32 use_iopb;
	u32 use_dlmb;
	u32 use_ilmb;
	u32 num_fsl;

	/* CPU interrupt line info */
	u32 irq_edge;
	u32 irq_positive;

	u32 area_optimised;

	/* HW debug support */
	u32 hw_debug;
	u32 num_pc_brk;
	u32 num_rd_brk;
	u32 num_wr_brk;
	u32 cpu_clock_freq; /* store real freq of cpu */

	/* FPGA family */
	u32 fpga_family_code;

	/* User define */
	u32 pvr_user1;
	u32 pvr_user2;
};

extern struct cpuinfo cpuinfo;

/* fwd declarations of the various CPUinfo populators */
void setup_cpuinfo(void);

void set_cpuinfo_static(struct cpuinfo *ci, struct device_node *cpu);
void set_cpuinfo_pvr_full(struct cpuinfo *ci, struct device_node *cpu);

static inline unsigned int fcpu(struct device_node *cpu, char *n)
{
	const __be32 *val;
	return (val = of_get_property(cpu, n, NULL)) ?
							be32_to_cpup(val) : 0;
}

#endif /* _ASM_MICROBLAZE_CPUINFO_H */
