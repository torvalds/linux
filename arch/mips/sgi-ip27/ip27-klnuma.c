/*
 * Ported from IRIX to Linux by Kanoj Sarcar, 06/08/00.
 * Copyright 2000 - 2001 Silicon Graphics, Inc.
 * Copyright 2000 - 2001 Kanoj Sarcar (kanoj@sgi.com)
 */
#include <linux/init.h>
#include <linux/mmzone.h>
#include <linux/kernel.h>
#include <linux/nodemask.h>
#include <linux/string.h>

#include <asm/page.h>
#include <asm/sections.h>
#include <asm/sn/types.h>
#include <asm/sn/arch.h>
#include <asm/sn/gda.h>
#include <asm/sn/hub.h>
#include <asm/sn/mapped_kernel.h>
#include <asm/sn/sn_private.h>

static cpumask_t ktext_repmask;

/*
 * XXX - This needs to be much smarter about where it puts copies of the
 * kernel.  For example, we should never put a copy on a headless node,
 * and we should respect the topology of the machine.
 */
void __init setup_replication_mask(void)
{
	/* Set only the master cnode's bit.  The master cnode is always 0. */
	cpus_clear(ktext_repmask);
	cpu_set(0, ktext_repmask);

#ifdef CONFIG_REPLICATE_KTEXT
#ifndef CONFIG_MAPPED_KERNEL
#error Kernel replication works with mapped kernel support. No calias support.
#endif
	{
		cnodeid_t	cnode;

		for_each_online_node(cnode) {
			if (cnode == 0)
				continue;
			/* Advertise that we have a copy of the kernel */
			cpu_set(cnode, ktext_repmask);
		}
	}
#endif
	/* Set up a GDA pointer to the replication mask. */
	GDA->g_ktext_repmask = &ktext_repmask;
}


static __init void set_ktext_source(nasid_t client_nasid, nasid_t server_nasid)
{
	cnodeid_t client_cnode;
	kern_vars_t *kvp;

	client_cnode = NASID_TO_COMPACT_NODEID(client_nasid);

	kvp = &hub_data(client_nasid)->kern_vars;

	KERN_VARS_ADDR(client_nasid) = (unsigned long)kvp;

	kvp->kv_magic = KV_MAGIC;
	kvp->kv_ro_nasid = server_nasid;
	kvp->kv_rw_nasid = master_nasid;
	kvp->kv_ro_baseaddr = NODE_CAC_BASE(server_nasid);
	kvp->kv_rw_baseaddr = NODE_CAC_BASE(master_nasid);
	printk("REPLICATION: ON nasid %d, ktext from nasid %d, kdata from nasid %d\n", client_nasid, server_nasid, master_nasid);
}

/* XXX - When the BTE works, we should use it instead of this. */
static __init void copy_kernel(nasid_t dest_nasid)
{
	unsigned long dest_kern_start, source_start, source_end, kern_size;

	source_start = (unsigned long) _stext;
	source_end = (unsigned long) _etext;
	kern_size = source_end - source_start;

	dest_kern_start = CHANGE_ADDR_NASID(MAPPED_KERN_RO_TO_K0(source_start),
					    dest_nasid);
	memcpy((void *)dest_kern_start, (void *)source_start, kern_size);
}

void __init replicate_kernel_text()
{
	cnodeid_t cnode;
	nasid_t client_nasid;
	nasid_t server_nasid;

	server_nasid = master_nasid;

	/* Record where the master node should get its kernel text */
	set_ktext_source(master_nasid, master_nasid);

	for_each_online_node(cnode) {
		if (cnode == 0)
			continue;
		client_nasid = COMPACT_TO_NASID_NODEID(cnode);

		/* Check if this node should get a copy of the kernel */
		if (cpu_isset(cnode, ktext_repmask)) {
			server_nasid = client_nasid;
			copy_kernel(server_nasid);
		}

		/* Record where this node should get its kernel text */
		set_ktext_source(client_nasid, server_nasid);
	}
}

/*
 * Return pfn of first free page of memory on a node. PROM may allocate
 * data structures on the first couple of pages of the first slot of each
 * node. If this is the case, getfirstfree(node) > getslotstart(node, 0).
 */
pfn_t node_getfirstfree(cnodeid_t cnode)
{
	unsigned long loadbase = REP_BASE;
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cnode);
	unsigned long offset;

#ifdef CONFIG_MAPPED_KERNEL
	loadbase += 16777216;
#endif
	offset = PAGE_ALIGN((unsigned long)(&_end)) - loadbase;
	if ((cnode == 0) || (cpu_isset(cnode, ktext_repmask)))
		return (TO_NODE(nasid, offset) >> PAGE_SHIFT);
	else
		return (KDM_TO_PHYS(PAGE_ALIGN(SYMMON_STK_ADDR(nasid, 0))) >>
								PAGE_SHIFT);
}

