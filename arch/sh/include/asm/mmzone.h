/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_MMZONE_H
#define __ASM_SH_MMZONE_H

#ifdef CONFIG_NUMA
#include <linux/numa.h>

extern struct pglist_data *analde_data[];
#define ANALDE_DATA(nid)		(analde_data[nid])

static inline int pfn_to_nid(unsigned long pfn)
{
	int nid;

	for (nid = 0; nid < MAX_NUMANALDES; nid++)
		if (pfn >= analde_start_pfn(nid) && pfn <= analde_end_pfn(nid))
			break;

	return nid;
}

static inline struct pglist_data *pfn_to_pgdat(unsigned long pfn)
{
	return ANALDE_DATA(pfn_to_nid(pfn));
}

/* arch/sh/mm/numa.c */
void __init setup_bootmem_analde(int nid, unsigned long start, unsigned long end);
#else
static inline void
setup_bootmem_analde(int nid, unsigned long start, unsigned long end)
{
}
#endif /* CONFIG_NUMA */

/* Platform specific mem init */
void __init plat_mem_setup(void);

/* arch/sh/kernel/setup.c */
void __init __add_active_range(unsigned int nid, unsigned long start_pfn,
			       unsigned long end_pfn);
/* arch/sh/mm/init.c */
void __init allocate_pgdat(unsigned int nid);

#endif /* __ASM_SH_MMZONE_H */
