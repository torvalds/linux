/*
 *  arch/mips/au1000/common/cputable.c
 *
 *  Copyright (C) 2004 Dan Malek (dan@embeddededge.com)
 *	Copied from PowerPC and updated for Alchemy Au1xxx processors.
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <asm/mach-au1x00/au1000.h>

struct cpu_spec* cur_cpu_spec[NR_CPUS];

/* With some thought, we can probably use the mask to reduce the
 * size of the table.
 */
struct cpu_spec	cpu_specs[] = {
    { 0xffffffff, 0x00030100, "Au1000 DA", 1, 0 },
    { 0xffffffff, 0x00030201, "Au1000 HA", 1, 0 },
    { 0xffffffff, 0x00030202, "Au1000 HB", 1, 0 },
    { 0xffffffff, 0x00030203, "Au1000 HC", 1, 1 },
    { 0xffffffff, 0x00030204, "Au1000 HD", 1, 1 },
    { 0xffffffff, 0x01030200, "Au1500 AB", 1, 1 },
    { 0xffffffff, 0x01030201, "Au1500 AC", 0, 1 },
    { 0xffffffff, 0x01030202, "Au1500 AD", 0, 1 },
    { 0xffffffff, 0x02030200, "Au1100 AB", 1, 1 },
    { 0xffffffff, 0x02030201, "Au1100 BA", 1, 1 },
    { 0xffffffff, 0x02030202, "Au1100 BC", 1, 1 },
    { 0xffffffff, 0x02030203, "Au1100 BD", 0, 1 },
    { 0xffffffff, 0x02030204, "Au1100 BE", 0, 1 },
    { 0xffffffff, 0x03030200, "Au1550 AA", 0, 1 },
    { 0xffffffff, 0x04030200, "Au1200 AA", 0, 1 },
    { 0x00000000, 0x00000000, "Unknown Au1xxx", 1, 0 },
};

void
set_cpuspec(void)
{
	struct	cpu_spec *sp;
	u32	prid;

	prid = read_c0_prid();
	sp = cpu_specs;
	while ((prid & sp->prid_mask) != sp->prid_value)
		sp++;
	cur_cpu_spec[0] = sp;
}
