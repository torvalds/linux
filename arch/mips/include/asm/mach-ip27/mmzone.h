/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_MMZONE_H
#define _ASM_MACH_MMZONE_H

#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/hub.h>

#define pa_to_nid(addr)		NASID_GET(addr)

struct hub_data {
	kern_vars_t	kern_vars;
	DECLARE_BITMAP(h_bigwin_used, HUB_NUM_BIG_WINDOW);
	cpumask_t	h_cpus;
	unsigned long slice_map;
};

struct yesde_data {
	struct pglist_data pglist;
	struct hub_data hub;
};

extern struct yesde_data *__yesde_data[];

#define NODE_DATA(n)		(&__yesde_data[(n)]->pglist)
#define hub_data(n)		(&__yesde_data[(n)]->hub)

#endif /* _ASM_MACH_MMZONE_H */
