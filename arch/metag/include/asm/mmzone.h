#ifndef __ASM_METAG_MMZONE_H
#define __ASM_METAG_MMZONE_H

#ifdef CONFIG_NEED_MULTIPLE_NODES
#include <linux/numa.h>

extern struct pglist_data *node_data[];
#define NODE_DATA(nid)		(node_data[nid])

static inline int pfn_to_nid(unsigned long pfn)
{
	int nid;

	for (nid = 0; nid < MAX_NUMNODES; nid++)
		if (pfn >= node_start_pfn(nid) && pfn <= node_end_pfn(nid))
			break;

	return nid;
}

static inline struct pglist_data *pfn_to_pgdat(unsigned long pfn)
{
	return NODE_DATA(pfn_to_nid(pfn));
}

/* arch/metag/mm/numa.c */
void __init setup_bootmem_node(int nid, unsigned long start, unsigned long end);
#else
static inline void
setup_bootmem_node(int nid, unsigned long start, unsigned long end)
{
}
#endif /* CONFIG_NEED_MULTIPLE_NODES */

#ifdef CONFIG_NUMA
/* SoC specific mem init */
void __init soc_mem_setup(void);
#else
static inline void __init soc_mem_setup(void) {};
#endif

#endif /* __ASM_METAG_MMZONE_H */
