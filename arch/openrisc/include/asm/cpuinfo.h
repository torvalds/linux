/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef __ASM_OPENRISC_CPUINFO_H
#define __ASM_OPENRISC_CPUINFO_H

#include <asm/spr.h>
#include <asm/spr_defs.h>

struct cache_desc {
	u32 size;
	u32 sets;
	u32 block_size;
	u32 ways;
};

struct cpuinfo_or1k {
	u32 clock_frequency;

	struct cache_desc icache;
	struct cache_desc dcache;

	u16 coreid;
};

extern struct cpuinfo_or1k cpuinfo_or1k[NR_CPUS];
extern void setup_cpuinfo(void);

/*
 * Check if the cache component exists.
 */
extern bool cpu_cache_is_present(const unsigned int cache_type);

#endif /* __ASM_OPENRISC_CPUINFO_H */
