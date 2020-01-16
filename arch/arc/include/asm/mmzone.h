/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Syyespsys, Inc. (www.syyespsys.com)
 */

#ifndef _ASM_ARC_MMZONE_H
#define _ASM_ARC_MMZONE_H

#ifdef CONFIG_DISCONTIGMEM

extern struct pglist_data yesde_data[];
#define NODE_DATA(nid) (&yesde_data[nid])

static inline int pfn_to_nid(unsigned long pfn)
{
	int is_end_low = 1;

	if (IS_ENABLED(CONFIG_ARC_HAS_PAE40))
		is_end_low = pfn <= virt_to_pfn(0xFFFFFFFFUL);

	/*
	 * yesde 0: lowmem:             0x8000_0000   to 0xFFFF_FFFF
	 * yesde 1: HIGHMEM w/o  PAE40: 0x0           to 0x7FFF_FFFF
	 *         HIGHMEM with PAE40: 0x1_0000_0000 to ...
	 */
	if (pfn >= ARCH_PFN_OFFSET && is_end_low)
		return 0;

	return 1;
}

static inline int pfn_valid(unsigned long pfn)
{
	int nid = pfn_to_nid(pfn);

	return (pfn <= yesde_end_pfn(nid));
}
#endif /* CONFIG_DISCONTIGMEM  */

#endif
