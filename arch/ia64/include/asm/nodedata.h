/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2002 NEC Corp.
 * Copyright (c) 2002 Erich Focht <efocht@ess.nec.de>
 * Copyright (c) 2002 Kimio Suganuma <k-suganuma@da.jp.nec.com>
 */
#ifndef _ASM_IA64_NODEDATA_H
#define _ASM_IA64_NODEDATA_H

#include <linux/numa.h>

#include <asm/percpu.h>
#include <asm/mmzone.h>

#ifdef CONFIG_NUMA

/*
 * Node Data. One of these structures is located on each node of a NUMA system.
 */

struct pglist_data;
struct ia64_node_data {
	short			active_cpu_count;
	short			node;
	struct pglist_data	*pg_data_ptrs[MAX_NUMNODES];
};


/*
 * Return a pointer to the node_data structure for the executing cpu.
 */
#define local_node_data		(local_cpu_data->node_data)

/*
 * Given a node id, return a pointer to the pg_data_t for the node.
 *
 * NODE_DATA 	- should be used in all code not related to system
 *		  initialization. It uses pernode data structures to minimize
 *		  offnode memory references. However, these structure are not 
 *		  present during boot. This macro can be used once cpu_init
 *		  completes.
 */
#define NODE_DATA(nid)		(local_node_data->pg_data_ptrs[nid])

/*
 * LOCAL_DATA_ADDR - This is to calculate the address of other node's
 *		     "local_node_data" at hot-plug phase. The local_node_data
 *		     is pointed by per_cpu_page. Kernel usually use it for
 *		     just executing cpu. However, when new node is hot-added,
 *		     the addresses of local data for other nodes are necessary
 *		     to update all of them.
 */
#define LOCAL_DATA_ADDR(pgdat)  			\
	((struct ia64_node_data *)((u64)(pgdat) + 	\
				   L1_CACHE_ALIGN(sizeof(struct pglist_data))))

#endif /* CONFIG_NUMA */

#endif /* _ASM_IA64_NODEDATA_H */
