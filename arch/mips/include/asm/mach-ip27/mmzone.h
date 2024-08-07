/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_MMZONE_H
#define _ASM_MACH_MMZONE_H

#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/agent.h>
#include <asm/sn/klkernvars.h>

#define pa_to_nid(addr)		NASID_GET(addr)

struct hub_data {
	kern_vars_t	kern_vars;
	DECLARE_BITMAP(h_bigwin_used, HUB_NUM_BIG_WINDOW);
	cpumask_t	h_cpus;
};

struct node_data {
	struct pglist_data pglist;
	struct hub_data hub;
};

extern struct node_data *__node_data[];

#define hub_data(n)		(&__node_data[(n)]->hub)

extern struct pglist_data *node_data[];

#define NODE_DATA(nid)		(node_data[nid])

#endif /* _ASM_MACH_MMZONE_H */
