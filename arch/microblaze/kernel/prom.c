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
#include <linux/pci.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>

static int __initdata dt_root_addr_cells;
static int __initdata dt_root_size_cells;

typedef u32 cell_t;

static struct boot_param_header *initial_boot_params;

/* export that to outside world */
struct device_node *of_chosen;

static inline char *find_flat_dt_string(u32 offset)
{
	return ((char *)initial_boot_params) +
		initial_boot_params->off_dt_strings + offset;
}

/**
 * This function is used to scan the flattened device-tree, it is
 * used to extract the memory informations at boot before we can
 * unflatten the tree
 */
int __init of_scan_flat_dt(int (*it)(unsigned long node,
				     const char *uname, int depth,
				     void *data),
			   void *data)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	int rc = 0;
	int depth = -1;

	do {
		u32 tag = *((u32 *)p);
		char *pathp;

		p += 4;
		if (tag == OF_DT_END_NODE) {
			depth--;
			continue;
		}
		if (tag == OF_DT_NOP)
			continue;
		if (tag == OF_DT_END)
			break;
		if (tag == OF_DT_PROP) {
			u32 sz = *((u32 *)p);
			p += 8;
			if (initial_boot_params->version < 0x10)
				p = _ALIGN(p, sz >= 8 ? 8 : 4);
			p += sz;
			p = _ALIGN(p, 4);
			continue;
		}
		if (tag != OF_DT_BEGIN_NODE) {
			printk(KERN_WARNING "Invalid tag %x scanning flattened"
				" device tree !\n", tag);
			return -EINVAL;
		}
		depth++;
		pathp = (char *)p;
		p = _ALIGN(p + strlen(pathp) + 1, 4);
		if ((*pathp) == '/') {
			char *lp, *np;
			for (lp = NULL, np = pathp; *np; np++)
				if ((*np) == '/')
					lp = np+1;
			if (lp != NULL)
				pathp = lp;
		}
		rc = it(p, pathp, depth, data);
		if (rc != 0)
			break;
	} while (1);

	return rc;
}

unsigned long __init of_get_flat_dt_root(void)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;

	while (*((u32 *)p) == OF_DT_NOP)
		p += 4;
	BUG_ON(*((u32 *)p) != OF_DT_BEGIN_NODE);
	p += 4;
	return _ALIGN(p + strlen((char *)p) + 1, 4);
}

/**
 * This function can be used within scan_flattened_dt callback to get
 * access to properties
 */
void *__init of_get_flat_dt_prop(unsigned long node, const char *name,
				unsigned long *size)
{
	unsigned long p = node;

	do {
		u32 tag = *((u32 *)p);
		u32 sz, noff;
		const char *nstr;

		p += 4;
		if (tag == OF_DT_NOP)
			continue;
		if (tag != OF_DT_PROP)
			return NULL;

		sz = *((u32 *)p);
		noff = *((u32 *)(p + 4));
		p += 8;
		if (initial_boot_params->version < 0x10)
			p = _ALIGN(p, sz >= 8 ? 8 : 4);

		nstr = find_flat_dt_string(noff);
		if (nstr == NULL) {
			printk(KERN_WARNING "Can't find property index"
				" name !\n");
			return NULL;
		}
		if (strcmp(name, nstr) == 0) {
			if (size)
				*size = sz;
			return (void *)p;
		}
		p += sz;
		p = _ALIGN(p, 4);
	} while (1);
}

int __init of_flat_dt_is_compatible(unsigned long node, const char *compat)
{
	const char *cp;
	unsigned long cplen, l;

	cp = of_get_flat_dt_prop(node, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (strncasecmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}

static void *__init unflatten_dt_alloc(unsigned long *mem, unsigned long size,
					unsigned long align)
{
	void *res;

	*mem = _ALIGN(*mem, align);
	res = (void *)*mem;
	*mem += size;

	return res;
}

static unsigned long __init unflatten_dt_node(unsigned long mem,
					unsigned long *p,
					struct device_node *dad,
					struct device_node ***allnextpp,
					unsigned long fpsize)
{
	struct device_node *np;
	struct property *pp, **prev_pp = NULL;
	char *pathp;
	u32 tag;
	unsigned int l, allocl;
	int has_name = 0;
	int new_format = 0;

	tag = *((u32 *)(*p));
	if (tag != OF_DT_BEGIN_NODE) {
		printk("Weird tag at start of node: %x\n", tag);
		return mem;
	}
	*p += 4;
	pathp = (char *)*p;
	l = allocl = strlen(pathp) + 1;
	*p = _ALIGN(*p + l, 4);

	/* version 0x10 has a more compact unit name here instead of the full
	 * path. we accumulate the full path size using "fpsize", we'll rebuild
	 * it later. We detect this because the first character of the name is
	 * not '/'.
	 */
	if ((*pathp) != '/') {
		new_format = 1;
		if (fpsize == 0) {
			/* root node: special case. fpsize accounts for path
			 * plus terminating zero. root node only has '/', so
			 * fpsize should be 2, but we want to avoid the first
			 * level nodes to have two '/' so we use fpsize 1 here
			 */
			fpsize = 1;
			allocl = 2;
		} else {
			/* account for '/' and path size minus terminal 0
			 * already in 'l'
			 */
			fpsize += l;
			allocl = fpsize;
		}
	}

	np = unflatten_dt_alloc(&mem, sizeof(struct device_node) + allocl,
				__alignof__(struct device_node));
	if (allnextpp) {
		memset(np, 0, sizeof(*np));
		np->full_name = ((char *)np) + sizeof(struct device_node);
		if (new_format) {
			char *p2 = np->full_name;
			/* rebuild full path for new format */
			if (dad && dad->parent) {
				strcpy(p2, dad->full_name);
#ifdef DEBUG
				if ((strlen(p2) + l + 1) != allocl) {
					pr_debug("%s: p: %d, l: %d, a: %d\n",
						pathp, (int)strlen(p2),
						l, allocl);
				}
#endif
				p2 += strlen(p2);
			}
			*(p2++) = '/';
			memcpy(p2, pathp, l);
		} else
			memcpy(np->full_name, pathp, l);
		prev_pp = &np->properties;
		**allnextpp = np;
		*allnextpp = &np->allnext;
		if (dad != NULL) {
			np->parent = dad;
			/* we temporarily use the next field as `last_child'*/
			if (dad->next == NULL)
				dad->child = np;
			else
				dad->next->sibling = np;
			dad->next = np;
		}
		kref_init(&np->kref);
	}
	while (1) {
		u32 sz, noff;
		char *pname;

		tag = *((u32 *)(*p));
		if (tag == OF_DT_NOP) {
			*p += 4;
			continue;
		}
		if (tag != OF_DT_PROP)
			break;
		*p += 4;
		sz = *((u32 *)(*p));
		noff = *((u32 *)((*p) + 4));
		*p += 8;
		if (initial_boot_params->version < 0x10)
			*p = _ALIGN(*p, sz >= 8 ? 8 : 4);

		pname = find_flat_dt_string(noff);
		if (pname == NULL) {
			printk(KERN_INFO
				"Can't find property name in list !\n");
			break;
		}
		if (strcmp(pname, "name") == 0)
			has_name = 1;
		l = strlen(pname) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property),
					__alignof__(struct property));
		if (allnextpp) {
			if (strcmp(pname, "linux,phandle") == 0) {
				np->node = *((u32 *)*p);
				if (np->linux_phandle == 0)
					np->linux_phandle = np->node;
			}
			if (strcmp(pname, "ibm,phandle") == 0)
				np->linux_phandle = *((u32 *)*p);
			pp->name = pname;
			pp->length = sz;
			pp->value = (void *)*p;
			*prev_pp = pp;
			prev_pp = &pp->next;
		}
		*p = _ALIGN((*p) + sz, 4);
	}
	/* with version 0x10 we may not have the name property, recreate
	 * it here from the unit name if absent
	 */
	if (!has_name) {
		char *p1 = pathp, *ps = pathp, *pa = NULL;
		int sz;

		while (*p1) {
			if ((*p1) == '@')
				pa = p1;
			if ((*p1) == '/')
				ps = p1 + 1;
			p1++;
		}
		if (pa < ps)
			pa = p1;
		sz = (pa - ps) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property) + sz,
					__alignof__(struct property));
		if (allnextpp) {
			pp->name = "name";
			pp->length = sz;
			pp->value = pp + 1;
			*prev_pp = pp;
			prev_pp = &pp->next;
			memcpy(pp->value, ps, sz - 1);
			((char *)pp->value)[sz - 1] = 0;
			pr_debug("fixed up name for %s -> %s\n", pathp,
				(char *)pp->value);
		}
	}
	if (allnextpp) {
		*prev_pp = NULL;
		np->name = of_get_property(np, "name", NULL);
		np->type = of_get_property(np, "device_type", NULL);

		if (!np->name)
			np->name = "<NULL>";
		if (!np->type)
			np->type = "<NULL>";
	}
	while (tag == OF_DT_BEGIN_NODE) {
		mem = unflatten_dt_node(mem, p, np, allnextpp, fpsize);
		tag = *((u32 *)(*p));
	}
	if (tag != OF_DT_END_NODE) {
		printk(KERN_INFO "Weird tag at end of node: %x\n", tag);
		return mem;
	}
	*p += 4;
	return mem;
}

/**
 * unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_node. It also fills the "name" and "type"
 * pointers of the nodes so the normal device-tree walking functions
 * can be used (this used to be done by finish_device_tree)
 */
void __init unflatten_device_tree(void)
{
	unsigned long start, mem, size;
	struct device_node **allnextp = &allnodes;

	pr_debug(" -> unflatten_device_tree()\n");

	/* First pass, scan for size */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	size = unflatten_dt_node(0, &start, NULL, NULL, 0);
	size = (size | 3) + 1;

	pr_debug("  size is %lx, allocating...\n", size);

	/* Allocate memory for the expanded device tree */
	mem = lmb_alloc(size + 4, __alignof__(struct device_node));
	mem = (unsigned long) __va(mem);

	((u32 *)mem)[size / 4] = 0xdeadbeef;

	pr_debug("  unflattening %lx...\n", mem);

	/* Second pass, do actual unflattening */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	unflatten_dt_node(mem, &start, NULL, &allnextp, 0);
	if (*((u32 *)start) != OF_DT_END)
		printk(KERN_WARNING "Weird tag at end of tree: %08x\n",
			*((u32 *)start));
	if (((u32 *)mem)[size / 4] != 0xdeadbeef)
		printk(KERN_WARNING "End of tree marker overwritten: %08x\n",
			((u32 *)mem)[size / 4]);
	*allnextp = NULL;

	/* Get pointer to OF "/chosen" node for use everywhere */
	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen == NULL)
		of_chosen = of_find_node_by_path("/chosen@0");

	pr_debug(" <- unflatten_device_tree()\n");
}

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

#ifdef CONFIG_BLK_DEV_INITRD
static void __init early_init_dt_check_for_initrd(unsigned long node)
{
	unsigned long l;
	u32 *prop;

	pr_debug("Looking for initrd properties... ");

	prop = of_get_flat_dt_prop(node, "linux,initrd-start", &l);
	if (prop) {
		initrd_start = (unsigned long)__va(of_read_ulong(prop, l/4));

		prop = of_get_flat_dt_prop(node, "linux,initrd-end", &l);
		if (prop) {
			initrd_end = (unsigned long)
					__va(of_read_ulong(prop, l/4));
			initrd_below_start_ok = 1;
		} else {
			initrd_start = 0;
		}
	}

	pr_debug("initrd_start=0x%lx  initrd_end=0x%lx\n",
					initrd_start, initrd_end);
}
#else
static inline void early_init_dt_check_for_initrd(unsigned long node)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

static int __init early_init_dt_scan_chosen(unsigned long node,
				const char *uname, int depth, void *data)
{
	unsigned long l;
	char *p;

	pr_debug("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

	if (depth != 1 ||
		(strcmp(uname, "chosen") != 0 &&
				strcmp(uname, "chosen@0") != 0))
		return 0;

#ifdef CONFIG_KEXEC
	lprop = (u64 *)of_get_flat_dt_prop(node,
				"linux,crashkernel-base", NULL);
	if (lprop)
		crashk_res.start = *lprop;

	lprop = (u64 *)of_get_flat_dt_prop(node,
				"linux,crashkernel-size", NULL);
	if (lprop)
		crashk_res.end = crashk_res.start + *lprop - 1;
#endif

	early_init_dt_check_for_initrd(node);

	/* Retreive command line */
	p = of_get_flat_dt_prop(node, "bootargs", &l);
	if (p != NULL && l > 0)
		strlcpy(cmd_line, p, min((int)l, COMMAND_LINE_SIZE));

#ifdef CONFIG_CMDLINE
	if (p == NULL || l == 0 || (l == 1 && (*p) == 0))
		strlcpy(cmd_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif /* CONFIG_CMDLINE */

	pr_debug("Command line is: %s\n", cmd_line);

	/* break now */
	return 1;
}

static int __init early_init_dt_scan_root(unsigned long node,
				const char *uname, int depth, void *data)
{
	u32 *prop;

	if (depth != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "#size-cells", NULL);
	dt_root_size_cells = (prop == NULL) ? 1 : *prop;
	pr_debug("dt_root_size_cells = %x\n", dt_root_size_cells);

	prop = of_get_flat_dt_prop(node, "#address-cells", NULL);
	dt_root_addr_cells = (prop == NULL) ? 2 : *prop;
	pr_debug("dt_root_addr_cells = %x\n", dt_root_addr_cells);

	/* break now */
	return 1;
}

static u64 __init dt_mem_next_cell(int s, cell_t **cellp)
{
	cell_t *p = *cellp;

	*cellp = p + s;
	return of_read_number(p, s);
}

static int __init early_init_dt_scan_memory(unsigned long node,
				const char *uname, int depth, void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	cell_t *reg, *endp;
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

	reg = (cell_t *)of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = (cell_t *)of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(cell_t));

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

/**
 * Indicates whether the root node has a given value in its
 * compatible property.
 */
int machine_is_compatible(const char *compat)
{
	struct device_node *root;
	int rc = 0;

	root = of_find_node_by_path("/");
	if (root) {
		rc = of_device_is_compatible(root, compat);
		of_node_put(root);
	}
	return rc;
}
EXPORT_SYMBOL(machine_is_compatible);

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

/**
 *	of_find_all_nodes - Get next node in global list
 *	@prev:	Previous node or NULL to start iteration
 *		of_node_put() will be called on it
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_all_nodes(struct device_node *prev)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = prev ? prev->allnext : allnodes;
	for (; np != NULL; np = np->allnext)
		if (of_node_get(np))
			break;
	of_node_put(prev);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_all_nodes);

/**
 *	of_node_get - Increment refcount of a node
 *	@node:	Node to inc refcount, NULL is supported to
 *		simplify writing of callers
 *
 *	Returns node.
 */
struct device_node *of_node_get(struct device_node *node)
{
	if (node)
		kref_get(&node->kref);
	return node;
}
EXPORT_SYMBOL(of_node_get);

static inline struct device_node *kref_to_device_node(struct kref *kref)
{
	return container_of(kref, struct device_node, kref);
}

/**
 *	of_node_release - release a dynamically allocated node
 *	@kref:  kref element of the node to be released
 *
 *	In of_node_put() this function is passed to kref_put()
 *	as the destructor.
 */
static void of_node_release(struct kref *kref)
{
	struct device_node *node = kref_to_device_node(kref);
	struct property *prop = node->properties;

	/* We should never be releasing nodes that haven't been detached. */
	if (!of_node_check_flag(node, OF_DETACHED)) {
		printk(KERN_INFO "WARNING: Bad of_node_put() on %s\n",
			node->full_name);
		dump_stack();
		kref_init(&node->kref);
		return;
	}

	if (!of_node_check_flag(node, OF_DYNAMIC))
		return;

	while (prop) {
		struct property *next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
		prop = next;

		if (!prop) {
			prop = node->deadprops;
			node->deadprops = NULL;
		}
	}
	kfree(node->full_name);
	kfree(node->data);
	kfree(node);
}

/**
 *	of_node_put - Decrement refcount of a node
 *	@node:	Node to dec refcount, NULL is supported to
 *		simplify writing of callers
 *
 */
void of_node_put(struct device_node *node)
{
	if (node)
		kref_put(&node->kref, of_node_release);
}
EXPORT_SYMBOL(of_node_put);

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

/*
 * Add a property to a node
 */
int prom_add_property(struct device_node *np, struct property *prop)
{
	struct property **next;
	unsigned long flags;

	prop->next = NULL;
	write_lock_irqsave(&devtree_lock, flags);
	next = &np->properties;
	while (*next) {
		if (strcmp(prop->name, (*next)->name) == 0) {
			/* duplicate ! don't insert it */
			write_unlock_irqrestore(&devtree_lock, flags);
			return -1;
		}
		next = &(*next)->next;
	}
	*next = prop;
	write_unlock_irqrestore(&devtree_lock, flags);

#ifdef CONFIG_PROC_DEVICETREE
	/* try to add to proc as well if it was initialized */
	if (np->pde)
		proc_device_tree_add_prop(np->pde, prop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
}

/*
 * Remove a property from a node.  Note that we don't actually
 * remove it, since we have given out who-knows-how-many pointers
 * to the data using get-property.  Instead we just move the property
 * to the "dead properties" list, so it won't be found any more.
 */
int prom_remove_property(struct device_node *np, struct property *prop)
{
	struct property **next;
	unsigned long flags;
	int found = 0;

	write_lock_irqsave(&devtree_lock, flags);
	next = &np->properties;
	while (*next) {
		if (*next == prop) {
			/* found the node */
			*next = prop->next;
			prop->next = np->deadprops;
			np->deadprops = prop;
			found = 1;
			break;
		}
		next = &(*next)->next;
	}
	write_unlock_irqrestore(&devtree_lock, flags);

	if (!found)
		return -ENODEV;

#ifdef CONFIG_PROC_DEVICETREE
	/* try to remove the proc node as well */
	if (np->pde)
		proc_device_tree_remove_prop(np->pde, prop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
}

/*
 * Update a property in a node.  Note that we don't actually
 * remove it, since we have given out who-knows-how-many pointers
 * to the data using get-property.  Instead we just move the property
 * to the "dead properties" list, and add the new property to the
 * property list
 */
int prom_update_property(struct device_node *np,
			 struct property *newprop,
			 struct property *oldprop)
{
	struct property **next;
	unsigned long flags;
	int found = 0;

	write_lock_irqsave(&devtree_lock, flags);
	next = &np->properties;
	while (*next) {
		if (*next == oldprop) {
			/* found the node */
			newprop->next = oldprop->next;
			*next = newprop;
			oldprop->next = np->deadprops;
			np->deadprops = oldprop;
			found = 1;
			break;
		}
		next = &(*next)->next;
	}
	write_unlock_irqrestore(&devtree_lock, flags);

	if (!found)
		return -ENODEV;

#ifdef CONFIG_PROC_DEVICETREE
	/* try to add to proc as well if it was initialized */
	if (np->pde)
		proc_device_tree_update_prop(np->pde, newprop, oldprop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
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
