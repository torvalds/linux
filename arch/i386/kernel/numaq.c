/*
 * Written by: Patricia Gaughen, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <gone@us.ibm.com>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <asm/numaq.h>
#include <asm/topology.h>
#include <asm/processor.h>

#define	MB_TO_PAGES(addr) ((addr) << (20 - PAGE_SHIFT))

/*
 * Function: smp_dump_qct()
 *
 * Description: gets memory layout from the quad config table.  This
 * function also updates node_online_map with the nodes (quads) present.
 */
static void __init smp_dump_qct(void)
{
	int node;
	struct eachquadmem *eq;
	struct sys_cfg_data *scd =
		(struct sys_cfg_data *)__va(SYS_CFG_DATA_PRIV_ADDR);

	nodes_clear(node_online_map);
	for_each_node(node) {
		if (scd->quads_present31_0 & (1 << node)) {
			node_set_online(node);
			eq = &scd->eq[node];
			/* Convert to pages */
			node_start_pfn[node] = MB_TO_PAGES(
				eq->hi_shrd_mem_start - eq->priv_mem_size);
			node_end_pfn[node] = MB_TO_PAGES(
				eq->hi_shrd_mem_start + eq->hi_shrd_mem_size);

			memory_present(node,
				node_start_pfn[node], node_end_pfn[node]);
			node_remap_size[node] = node_memmap_size_bytes(node,
							node_start_pfn[node],
							node_end_pfn[node]);
		}
	}
}

/*
 * Unlike Summit, we don't really care to let the NUMA-Q
 * fall back to flat mode.  Don't compile for NUMA-Q
 * unless you really need it!
 */
int __init get_memcfg_numaq(void)
{
	smp_dump_qct();
	return 1;
}

static int __init numaq_dsc_disable(void)
{
	printk(KERN_DEBUG "NUMAQ: disabling TSC\n");
	tsc_disable = 1;
	return 0;
}
core_initcall(numaq_dsc_disable);
