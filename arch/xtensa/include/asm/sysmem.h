/*
 * sysmem-related prototypes.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_SYSMEM_H
#define _XTENSA_SYSMEM_H

#define SYSMEM_BANKS_MAX 31

struct meminfo {
	unsigned long start;
	unsigned long end;
};

/*
 * Bank array is sorted by .start.
 * Banks don't overlap and there's at least one page gap
 * between adjacent bank entries.
 */
struct sysmem_info {
	int nr_banks;
	struct meminfo bank[SYSMEM_BANKS_MAX];
};

extern struct sysmem_info sysmem;

int add_sysmem_bank(unsigned long start, unsigned long end);
int mem_reserve(unsigned long, unsigned long, int);
void bootmem_init(void);
void zones_init(void);

#endif /* _XTENSA_SYSMEM_H */
