/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/debugfs.h>
#include <linux/irq.h>
#include <linux/lmb.h>

#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>

/* export that to outside world */
struct device_node *of_chosen;

#define early_init_dt_scan_drconf_memory(node) 0

static int __init early_init_dt_scan_cpus(unsigned long node,
					  const char *uname, int depth,
					  void *data)
{
	static int logical_cpuid;
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const u32 *intserv;
	int i, nthreads;
	int found = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	/* Get physical cpuid */
	intserv = of_get_flat_dt_prop(node, "reg", NULL);
	nthreads = 1;

	/*
	 * Now see if any of these threads match our boot cpu.
	 * NOTE: This must match the parsing done in smp_setup_cpu_maps.
	 */
	for (i = 0; i < nthreads; i++) {
		/*
		 * version 2 of the kexec param format adds the phys cpuid of
		 * booted proc.
		 */
		if (initial_boot_params && initial_boot_params->version >= 2) {
			if (intserv[i] ==
					initial_boot_params->boot_cpuid_phys) {
				found = 1;
				break;
			}
		} else {
			/*
			 * Check if it's the boot-cpu, set it's hw index now,
			 * unfortunately this format did not support booting
			 * off secondary threads.
			 */
			if (of_get_flat_dt_prop(node,
					"linux,boot-cpu", NULL) != NULL) {
				found = 1;
				break;
			}
		}

#ifdef CONFIG_SMP
		/* logical cpu id is always 0 on UP kernels */
		logical_cpuid++;
#endif
	}

	if (found) {
		pr_debug("boot cpu: logical %d physical %d\n", logical_cpuid,
			intserv[i]);
		boot_cpuid = logical_cpuid;
	}

	return 0;
}

void __init early_init_dt_scan_chosen_arch(unsigned long node)
{
	/* No Microblaze specific code here */
}

static int __init early_init_dt_scan_memory(unsigned long node,
				const char *uname, int depth, void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	__be32 *reg, *endp;
	unsigned long l;

	/* Look for the ibm,dynamic-reconfiguration-memory node */
/*	if (depth == 1 &&
		strcmp(uname, "ibm,dynamic-reconfiguration-memory") == 0)
		return early_init_dt_scan_drconf_memory(node);
*/
	/* We are scanning "memory" nodes only */
	if (type == NULL) {
		/*
		 * The longtrail doesn't have a device_type on the
		 * /memory node, so look for the node called /memory@0.
		 */
		if (depth != 1 || strcmp(uname, "memory@0") != 0)
			return 0;
	} else if (strcmp(type, "memory") != 0)
		return 0;

	reg = (__be32 *)of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = (__be32 *)of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));

	pr_debug("memory scan node %s, reg size %ld, data: %x %x %x %x,\n",
		uname, l, reg[0], reg[1], reg[2], reg[3]);

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (size == 0)
			continue;
		pr_debug(" - %llx ,  %llx\n", (unsigned long long)base,
			(unsigned long long)size);

		lmb_add(base, size);
	}
	return 0;
}

#ifdef CONFIG_PHYP_DUMP
/**
 * phyp_dump_calculate_reserve_size() - reserve variable boot area 5% or arg
 *
 * Function to find the largest size we need to reserve
 * during early boot process.
 *
 * It either looks for boot param and returns that OR
 * returns larger of 256 or 5% rounded down to multiples of 256MB.
 *
 */
static inline unsigned long phyp_dump_calculate_reserve_size(void)
{
	unsigned long tmp;

	if (phyp_dump_info->reserve_bootvar)
		return phyp_dump_info->reserve_bootvar;

	/* divide by 20 to get 5% of value */
	tmp = lmb_end_of_DRAM();
	do_div(tmp, 20);

	/* round it down in multiples of 256 */
	tmp = tmp & ~0x0FFFFFFFUL;

	return (tmp > PHYP_DUMP_RMR_END ? tmp : PHYP_DUMP_RMR_END);
}

/**
 * phyp_dump_reserve_mem() - reserve all not-yet-dumped mmemory
 *
 * This routine may reserve memory regions in the kernel only
 * if the system is supported and a dump was taken in last
 * boot instance or if the hardware is supported and the
 * scratch area needs to be setup. In other instances it returns
 * without reserving anything. The memory in case of dump being
 * active is freed when the dump is collected (by userland tools).
 */
static void __init phyp_dump_reserve_mem(void)
{
	unsigned long base, size;
	unsigned long variable_reserve_size;

	if (!phyp_dump_info->phyp_dump_configured) {
		printk(KERN_ERR "Phyp-dump not supported on this hardware\n");
		return;
	}

	if (!phyp_dump_info->phyp_dump_at_boot) {
		printk(KERN_INFO "Phyp-dump disabled at boot time\n");
		return;
	}

	variable_reserve_size = phyp_dump_calculate_reserve_size();

	if (phyp_dump_info->phyp_dump_is_active) {
		/* Reserve *everything* above RMR.Area freed by userland tools*/
		base = variable_reserve_size;
		size = lmb_end_of_DRAM() - base;

		/* XXX crashed_ram_end is wrong, since it may be beyond
		 * the memory_limit, it will need to be adjusted. */
		lmb_reserve(base, size);

		phyp_dump_info->init_reserve_start = base;
		phyp_dump_info->init_reserve_size = size;
	} else {
		size = phyp_dump_info->cpu_state_size +
			phyp_dump_info->hpte_region_size +
			variable_reserve_size;
		base = lmb_end_of_DRAM() - size;
		lmb_reserve(base, size);
		phyp_dump_info->init_reserve_start = base;
		phyp_dump_info->init_reserve_size = size;
	}
}
#else
static inline void __init phyp_dump_reserve_mem(void) {}
#endif /* CONFIG_PHYP_DUMP  && CONFIG_PPC_RTAS */

#ifdef CONFIG_EARLY_PRINTK
/* MS this is Microblaze specifig function */
static int __init early_init_dt_scan_serial(unsigned long node,
				const char *uname, int depth, void *data)
{
	unsigned long l;
	char *p;
	int *addr;

	pr_debug("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

/* find all serial nodes */
	if (strncmp(uname, "serial", 6) != 0)
		return 0;

	early_init_dt_check_for_initrd(node);

/* find compatible node with uartlite */
	p = of_get_flat_dt_prop(node, "compatible", &l);
	if ((strncmp(p, "xlnx,xps-uartlite", 17) != 0) &&
			(strncmp(p, "xlnx,opb-uartlite", 17) != 0))
		return 0;

	addr = of_get_flat_dt_prop(node, "reg", &l);
	return *addr; /* return address */
}

/* this function is looking for early uartlite console - Microblaze specific */
int __init early_uartlite_console(void)
{
	return of_scan_flat_dt(early_init_dt_scan_serial, NULL);
}
#endif

void __init early_init_devtree(void *params)
{
	pr_debug(" -> early_init_devtree(%p)\n", params);

	/* Setup flat device-tree pointer */
	initial_boot_params = params;

#ifdef CONFIG_PHYP_DUMP
	/* scan tree to see if dump occured during last boot */
	of_scan_flat_dt(early_init_dt_scan_phyp_dump, NULL);
#endif

	/* Retrieve various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, TCE reserve, and more ...
	 */
	of_scan_flat_dt(early_init_dt_scan_chosen, NULL);

	/* Scan memory nodes and rebuild LMBs */
	lmb_init();
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	/* Save command line for /proc/cmdline and then parse parameters */
	strlcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);
	parse_early_param();

	lmb_analyze();

	pr_debug("Phys. mem: %lx\n", (unsigned long) lmb_phys_mem_size());

	pr_debug("Scanning CPUs ...\n");

	/* Retreive CPU related informations from the flat tree
	 * (altivec support, boot CPU ID, ...)
	 */
	of_scan_flat_dt(early_init_dt_scan_cpus, NULL);

	pr_debug(" <- early_init_devtree()\n");
}

/*******
 *
 * New implementation of the OF "find" APIs, return a refcounted
 * object, call of_node_put() when done.  The device tree and list
 * are protected by a rw_lock.
 *
 * Note that property management will need some locking as well,
 * this isn't dealt with yet.
 *
 *******/

/**
 *	of_find_node_by_phandle - Find a node given a phandle
 *	@handle:	phandle of the node to find
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_phandle(phandle handle)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	for (np = allnodes; np != NULL; np = np->allnext)
		if (np->linux_phandle == handle)
			break;
	of_node_get(np);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_phandle);

/*
 * Plug a device node into the tree and global list.
 */
void of_attach_node(struct device_node *np)
{
	unsigned long flags;

	write_lock_irqsave(&devtree_lock, flags);
	np->sibling = np->parent->child;
	np->allnext = allnodes;
	np->parent->child = np;
	allnodes = np;
	write_unlock_irqrestore(&devtree_lock, flags);
}

/*
 * "Unplug" a node from the device tree.  The caller must hold
 * a reference to the node.  The memory associated with the node
 * is not freed until its refcount goes to zero.
 */
void of_detach_node(struct device_node *np)
{
	struct device_node *parent;
	unsigned long flags;

	write_lock_irqsave(&devtree_lock, flags);

	parent = np->parent;
	if (!parent)
		goto out_unlock;

	if (allnodes == np)
		allnodes = np->allnext;
	else {
		struct device_node *prev;
		for (prev = allnodes;
		     prev->allnext != np;
		     prev = prev->allnext)
			;
		prev->allnext = np->allnext;
	}

	if (parent->child == np)
		parent->child = np->sibling;
	else {
		struct device_node *prevsib;
		for (prevsib = np->parent->child;
		     prevsib->sibling != np;
		     prevsib = prevsib->sibling)
			;
		prevsib->sibling = np->sibling;
	}

	of_node_set_flag(np, OF_DETACHED);

out_unlock:
	write_unlock_irqrestore(&devtree_lock, flags);
}

#if defined(CONFIG_DEBUG_FS) && defined(DEBUG)
static struct debugfs_blob_wrapper flat_dt_blob;

static int __init export_flat_device_tree(void)
{
	struct dentry *d;

	flat_dt_blob.data = initial_boot_params;
	flat_dt_blob.size = initial_boot_params->totalsize;

	d = debugfs_create_blob("flat-device-tree", S_IFREG | S_IRUSR,
				of_debugfs_root, &flat_dt_blob);
	if (!d)
		return 1;

	return 0;
}
device_initcall(export_flat_device_tree);
#endif
