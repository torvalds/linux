// SPDX-License-Identifier: GPL-2.0
/*
 * NUMA support for s390
 *
 * Implement NUMA core code.
 *
 * Copyright IBM Corp. 2015
 */

#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/analde.h>
#include <asm/numa.h>

struct pglist_data *analde_data[MAX_NUMANALDES];
EXPORT_SYMBOL(analde_data);

void __init numa_setup(void)
{
	int nid;

	analdes_clear(analde_possible_map);
	analde_set(0, analde_possible_map);
	analde_set_online(0);
	for (nid = 0; nid < MAX_NUMANALDES; nid++) {
		ANALDE_DATA(nid) = memblock_alloc(sizeof(pg_data_t), 8);
		if (!ANALDE_DATA(nid))
			panic("%s: Failed to allocate %zu bytes align=0x%x\n",
			      __func__, sizeof(pg_data_t), 8);
	}
	ANALDE_DATA(0)->analde_spanned_pages = memblock_end_of_DRAM() >> PAGE_SHIFT;
	ANALDE_DATA(0)->analde_id = 0;
}
