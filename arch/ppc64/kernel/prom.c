/*
 * 
 *
 * Procedures for interfacing to Open Firmware.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <stdarg.h>
#include <linux/config.h>
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

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/lmb.h>
#include <asm/abs_addr.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/ppcdebug.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>
#include <asm/pSeries_reconfig.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

struct pci_reg_property {
	struct pci_address addr;
	u32 size_hi;
	u32 size_lo;
};

struct isa_reg_property {
	u32 space;
	u32 address;
	u32 size;
};


typedef int interpret_func(struct device_node *, unsigned long *,
			   int, int, int);

extern struct rtas_t rtas;
extern struct lmb lmb;
extern unsigned long klimit;
extern unsigned long memory_limit;

static int __initdata dt_root_addr_cells;
static int __initdata dt_root_size_cells;
static int __initdata iommu_is_off;
int __initdata iommu_force_on;
unsigned long tce_alloc_start, tce_alloc_end;

typedef u32 cell_t;

#if 0
static struct boot_param_header *initial_boot_params __initdata;
#else
struct boot_param_header *initial_boot_params;
#endif

static struct device_node *allnodes = NULL;

/* use when traversing tree through the allnext, child, sibling,
 * or parent members of struct device_node.
 */
static DEFINE_RWLOCK(devtree_lock);

/* export that to outside world */
struct device_node *of_chosen;

/*
 * Wrapper for allocating memory for various data that needs to be
 * attached to device nodes as they are processed at boot or when
 * added to the device tree later (e.g. DLPAR).  At boot there is
 * already a region reserved so we just increment *mem_start by size;
 * otherwise we call kmalloc.
 */
static void * prom_alloc(unsigned long size, unsigned long *mem_start)
{
	unsigned long tmp;

	if (!mem_start)
		return kmalloc(size, GFP_KERNEL);

	tmp = *mem_start;
	*mem_start += size;
	return (void *)tmp;
}

/*
 * Find the device_node with a given phandle.
 */
static struct device_node * find_phandle(phandle ph)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->linux_phandle == ph)
			return np;
	return NULL;
}

/*
 * Find the interrupt parent of a node.
 */
static struct device_node * __devinit intr_parent(struct device_node *p)
{
	phandle *parp;

	parp = (phandle *) get_property(p, "interrupt-parent", NULL);
	if (parp == NULL)
		return p->parent;
	return find_phandle(*parp);
}

/*
 * Find out the size of each entry of the interrupts property
 * for a node.
 */
int __devinit prom_n_intr_cells(struct device_node *np)
{
	struct device_node *p;
	unsigned int *icp;

	for (p = np; (p = intr_parent(p)) != NULL; ) {
		icp = (unsigned int *)
			get_property(p, "#interrupt-cells", NULL);
		if (icp != NULL)
			return *icp;
		if (get_property(p, "interrupt-controller", NULL) != NULL
		    || get_property(p, "interrupt-map", NULL) != NULL) {
			printk("oops, node %s doesn't have #interrupt-cells\n",
			       p->full_name);
			return 1;
		}
	}
#ifdef DEBUG_IRQ
	printk("prom_n_intr_cells failed for %s\n", np->full_name);
#endif
	return 1;
}

/*
 * Map an interrupt from a device up to the platform interrupt
 * descriptor.
 */
static int __devinit map_interrupt(unsigned int **irq, struct device_node **ictrler,
				   struct device_node *np, unsigned int *ints,
				   int nintrc)
{
	struct device_node *p, *ipar;
	unsigned int *imap, *imask, *ip;
	int i, imaplen, match;
	int newintrc = 0, newaddrc = 0;
	unsigned int *reg;
	int naddrc;

	reg = (unsigned int *) get_property(np, "reg", NULL);
	naddrc = prom_n_addr_cells(np);
	p = intr_parent(np);
	while (p != NULL) {
		if (get_property(p, "interrupt-controller", NULL) != NULL)
			/* this node is an interrupt controller, stop here */
			break;
		imap = (unsigned int *)
			get_property(p, "interrupt-map", &imaplen);
		if (imap == NULL) {
			p = intr_parent(p);
			continue;
		}
		imask = (unsigned int *)
			get_property(p, "interrupt-map-mask", NULL);
		if (imask == NULL) {
			printk("oops, %s has interrupt-map but no mask\n",
			       p->full_name);
			return 0;
		}
		imaplen /= sizeof(unsigned int);
		match = 0;
		ipar = NULL;
		while (imaplen > 0 && !match) {
			/* check the child-interrupt field */
			match = 1;
			for (i = 0; i < naddrc && match; ++i)
				match = ((reg[i] ^ imap[i]) & imask[i]) == 0;
			for (; i < naddrc + nintrc && match; ++i)
				match = ((ints[i-naddrc] ^ imap[i]) & imask[i]) == 0;
			imap += naddrc + nintrc;
			imaplen -= naddrc + nintrc;
			/* grab the interrupt parent */
			ipar = find_phandle((phandle) *imap++);
			--imaplen;
			if (ipar == NULL) {
				printk("oops, no int parent %x in map of %s\n",
				       imap[-1], p->full_name);
				return 0;
			}
			/* find the parent's # addr and intr cells */
			ip = (unsigned int *)
				get_property(ipar, "#interrupt-cells", NULL);
			if (ip == NULL) {
				printk("oops, no #interrupt-cells on %s\n",
				       ipar->full_name);
				return 0;
			}
			newintrc = *ip;
			ip = (unsigned int *)
				get_property(ipar, "#address-cells", NULL);
			newaddrc = (ip == NULL)? 0: *ip;
			imap += newaddrc + newintrc;
			imaplen -= newaddrc + newintrc;
		}
		if (imaplen < 0) {
			printk("oops, error decoding int-map on %s, len=%d\n",
			       p->full_name, imaplen);
			return 0;
		}
		if (!match) {
#ifdef DEBUG_IRQ
			printk("oops, no match in %s int-map for %s\n",
			       p->full_name, np->full_name);
#endif
			return 0;
		}
		p = ipar;
		naddrc = newaddrc;
		nintrc = newintrc;
		ints = imap - nintrc;
		reg = ints - naddrc;
	}
	if (p == NULL) {
#ifdef DEBUG_IRQ
		printk("hmmm, int tree for %s doesn't have ctrler\n",
		       np->full_name);
#endif
		return 0;
	}
	*irq = ints;
	*ictrler = p;
	return nintrc;
}

static int __devinit finish_node_interrupts(struct device_node *np,
					    unsigned long *mem_start,
					    int measure_only)
{
	unsigned int *ints;
	int intlen, intrcells, intrcount;
	int i, j, n;
	unsigned int *irq, virq;
	struct device_node *ic;

	ints = (unsigned int *) get_property(np, "interrupts", &intlen);
	if (ints == NULL)
		return 0;
	intrcells = prom_n_intr_cells(np);
	intlen /= intrcells * sizeof(unsigned int);

	np->intrs = prom_alloc(intlen * sizeof(*(np->intrs)), mem_start);
	if (!np->intrs)
		return -ENOMEM;

	if (measure_only)
		return 0;

	intrcount = 0;
	for (i = 0; i < intlen; ++i, ints += intrcells) {
		n = map_interrupt(&irq, &ic, np, ints, intrcells);
		if (n <= 0)
			continue;

		/* don't map IRQ numbers under a cascaded 8259 controller */
		if (ic && device_is_compatible(ic, "chrp,iic")) {
			np->intrs[intrcount].line = irq[0];
		} else {
			virq = virt_irq_create_mapping(irq[0]);
			if (virq == NO_IRQ) {
				printk(KERN_CRIT "Could not allocate interrupt"
				       " number for %s\n", np->full_name);
				continue;
			}
			np->intrs[intrcount].line = irq_offset_up(virq);
		}

		/* We offset irq numbers for the u3 MPIC by 128 in PowerMac */
		if (systemcfg->platform == PLATFORM_POWERMAC && ic && ic->parent) {
			char *name = get_property(ic->parent, "name", NULL);
			if (name && !strcmp(name, "u3"))
				np->intrs[intrcount].line += 128;
			else if (!(name && !strcmp(name, "mac-io")))
				/* ignore other cascaded controllers, such as
				   the k2-sata-root */
				break;
		}
		np->intrs[intrcount].sense = 1;
		if (n > 1)
			np->intrs[intrcount].sense = irq[1];
		if (n > 2) {
			printk("hmmm, got %d intr cells for %s:", n,
			       np->full_name);
			for (j = 0; j < n; ++j)
				printk(" %d", irq[j]);
			printk("\n");
		}
		++intrcount;
	}
	np->n_intrs = intrcount;

	return 0;
}

static int __devinit interpret_pci_props(struct device_node *np,
					 unsigned long *mem_start,
					 int naddrc, int nsizec,
					 int measure_only)
{
	struct address_range *adr;
	struct pci_reg_property *pci_addrs;
	int i, l, n_addrs;

	pci_addrs = (struct pci_reg_property *)
		get_property(np, "assigned-addresses", &l);
	if (!pci_addrs)
		return 0;

	n_addrs = l / sizeof(*pci_addrs);

	adr = prom_alloc(n_addrs * sizeof(*adr), mem_start);
	if (!adr)
		return -ENOMEM;

 	if (measure_only)
 		return 0;

 	np->addrs = adr;
 	np->n_addrs = n_addrs;

 	for (i = 0; i < n_addrs; i++) {
 		adr[i].space = pci_addrs[i].addr.a_hi;
 		adr[i].address = pci_addrs[i].addr.a_lo |
			((u64)pci_addrs[i].addr.a_mid << 32);
 		adr[i].size = pci_addrs[i].size_lo;
	}

	return 0;
}

static int __init interpret_dbdma_props(struct device_node *np,
					unsigned long *mem_start,
					int naddrc, int nsizec,
					int measure_only)
{
	struct reg_property32 *rp;
	struct address_range *adr;
	unsigned long base_address;
	int i, l;
	struct device_node *db;

	base_address = 0;
	if (!measure_only) {
		for (db = np->parent; db != NULL; db = db->parent) {
			if (!strcmp(db->type, "dbdma") && db->n_addrs != 0) {
				base_address = db->addrs[0].address;
				break;
			}
		}
	}

	rp = (struct reg_property32 *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct reg_property32)) {
		i = 0;
		adr = (struct address_range *) (*mem_start);
		while ((l -= sizeof(struct reg_property32)) >= 0) {
			if (!measure_only) {
				adr[i].space = 2;
				adr[i].address = rp[i].address + base_address;
				adr[i].size = rp[i].size;
			}
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		(*mem_start) += i * sizeof(struct address_range);
	}

	return 0;
}

static int __init interpret_macio_props(struct device_node *np,
					unsigned long *mem_start,
					int naddrc, int nsizec,
					int measure_only)
{
	struct reg_property32 *rp;
	struct address_range *adr;
	unsigned long base_address;
	int i, l;
	struct device_node *db;

	base_address = 0;
	if (!measure_only) {
		for (db = np->parent; db != NULL; db = db->parent) {
			if (!strcmp(db->type, "mac-io") && db->n_addrs != 0) {
				base_address = db->addrs[0].address;
				break;
			}
		}
	}

	rp = (struct reg_property32 *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct reg_property32)) {
		i = 0;
		adr = (struct address_range *) (*mem_start);
		while ((l -= sizeof(struct reg_property32)) >= 0) {
			if (!measure_only) {
				adr[i].space = 2;
				adr[i].address = rp[i].address + base_address;
				adr[i].size = rp[i].size;
			}
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		(*mem_start) += i * sizeof(struct address_range);
	}

	return 0;
}

static int __init interpret_isa_props(struct device_node *np,
				      unsigned long *mem_start,
				      int naddrc, int nsizec,
				      int measure_only)
{
	struct isa_reg_property *rp;
	struct address_range *adr;
	int i, l;

	rp = (struct isa_reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct isa_reg_property)) {
		i = 0;
		adr = (struct address_range *) (*mem_start);
		while ((l -= sizeof(struct isa_reg_property)) >= 0) {
			if (!measure_only) {
				adr[i].space = rp[i].space;
				adr[i].address = rp[i].address;
				adr[i].size = rp[i].size;
			}
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		(*mem_start) += i * sizeof(struct address_range);
	}

	return 0;
}

static int __init interpret_root_props(struct device_node *np,
				       unsigned long *mem_start,
				       int naddrc, int nsizec,
				       int measure_only)
{
	struct address_range *adr;
	int i, l;
	unsigned int *rp;
	int rpsize = (naddrc + nsizec) * sizeof(unsigned int);

	rp = (unsigned int *) get_property(np, "reg", &l);
	if (rp != 0 && l >= rpsize) {
		i = 0;
		adr = (struct address_range *) (*mem_start);
		while ((l -= rpsize) >= 0) {
			if (!measure_only) {
				adr[i].space = 0;
				adr[i].address = rp[naddrc - 1];
				adr[i].size = rp[naddrc + nsizec - 1];
			}
			++i;
			rp += naddrc + nsizec;
		}
		np->addrs = adr;
		np->n_addrs = i;
		(*mem_start) += i * sizeof(struct address_range);
	}

	return 0;
}

static int __devinit finish_node(struct device_node *np,
				 unsigned long *mem_start,
				 interpret_func *ifunc,
				 int naddrc, int nsizec,
				 int measure_only)
{
	struct device_node *child;
	int *ip, rc = 0;

	/* get the device addresses and interrupts */
	if (ifunc != NULL)
		rc = ifunc(np, mem_start, naddrc, nsizec, measure_only);
	if (rc)
		goto out;

	rc = finish_node_interrupts(np, mem_start, measure_only);
	if (rc)
		goto out;

	/* Look for #address-cells and #size-cells properties. */
	ip = (int *) get_property(np, "#address-cells", NULL);
	if (ip != NULL)
		naddrc = *ip;
	ip = (int *) get_property(np, "#size-cells", NULL);
	if (ip != NULL)
		nsizec = *ip;

	if (!strcmp(np->name, "device-tree") || np->parent == NULL)
		ifunc = interpret_root_props;
	else if (np->type == 0)
		ifunc = NULL;
	else if (!strcmp(np->type, "pci") || !strcmp(np->type, "vci"))
		ifunc = interpret_pci_props;
	else if (!strcmp(np->type, "dbdma"))
		ifunc = interpret_dbdma_props;
	else if (!strcmp(np->type, "mac-io") || ifunc == interpret_macio_props)
		ifunc = interpret_macio_props;
	else if (!strcmp(np->type, "isa"))
		ifunc = interpret_isa_props;
	else if (!strcmp(np->name, "uni-n") || !strcmp(np->name, "u3"))
		ifunc = interpret_root_props;
	else if (!((ifunc == interpret_dbdma_props
		    || ifunc == interpret_macio_props)
		   && (!strcmp(np->type, "escc")
		       || !strcmp(np->type, "media-bay"))))
		ifunc = NULL;

	for (child = np->child; child != NULL; child = child->sibling) {
		rc = finish_node(child, mem_start, ifunc,
				 naddrc, nsizec, measure_only);
		if (rc)
			goto out;
	}
out:
	return rc;
}

/**
 * finish_device_tree is called once things are running normally
 * (i.e. with text and data mapped to the address they were linked at).
 * It traverses the device tree and fills in some of the additional,
 * fields in each node like {n_}addrs and {n_}intrs, the virt interrupt
 * mapping is also initialized at this point.
 */
void __init finish_device_tree(void)
{
	unsigned long start, end, size = 0;

	DBG(" -> finish_device_tree\n");

	if (ppc64_interrupt_controller == IC_INVALID) {
		DBG("failed to configure interrupt controller type\n");
		panic("failed to configure interrupt controller type\n");
	}
	
	/* Initialize virtual IRQ map */
	virt_irq_init();

	/*
	 * Finish device-tree (pre-parsing some properties etc...)
	 * We do this in 2 passes. One with "measure_only" set, which
	 * will only measure the amount of memory needed, then we can
	 * allocate that memory, and call finish_node again. However,
	 * we must be careful as most routines will fail nowadays when
	 * prom_alloc() returns 0, so we must make sure our first pass
	 * doesn't start at 0. We pre-initialize size to 16 for that
	 * reason and then remove those additional 16 bytes
	 */
	size = 16;
	finish_node(allnodes, &size, NULL, 0, 0, 1);
	size -= 16;
	end = start = (unsigned long)abs_to_virt(lmb_alloc(size, 128));
	finish_node(allnodes, &end, NULL, 0, 0, 0);
	BUG_ON(end != start + size);

	DBG(" <- finish_device_tree\n");
}

#ifdef DEBUG
#define printk udbg_printf
#endif

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
static int __init scan_flat_dt(int (*it)(unsigned long node,
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
			depth --;
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
	} while(1);

	return rc;
}

/**
 * This  function can be used within scan_flattened_dt callback to get
 * access to properties
 */
static void* __init get_flat_dt_prop(unsigned long node, const char *name,
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
	} while(1);
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
		np->full_name = ((char*)np) + sizeof(struct device_node);
		if (new_format) {
			char *p = np->full_name;
			/* rebuild full path for new format */
			if (dad && dad->parent) {
				strcpy(p, dad->full_name);
#ifdef DEBUG
				if ((strlen(p) + l + 1) != allocl) {
					DBG("%s: p: %d, l: %d, a: %d\n",
					    pathp, strlen(p), l, allocl);
				}
#endif
				p += strlen(p);
			}
			*(p++) = '/';
			memcpy(p, pathp, l);
		} else
			memcpy(np->full_name, pathp, l);
		prev_pp = &np->properties;
		**allnextpp = np;
		*allnextpp = &np->allnext;
		if (dad != NULL) {
			np->parent = dad;
			/* we temporarily use the next field as `last_child'*/
			if (dad->next == 0)
				dad->child = np;
			else
				dad->next->sibling = np;
			dad->next = np;
		}
		kref_init(&np->kref);
	}
	while(1) {
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
			printk("Can't find property name in list !\n");
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
		char *p = pathp, *ps = pathp, *pa = NULL;
		int sz;

		while (*p) {
			if ((*p) == '@')
				pa = p;
			if ((*p) == '/')
				ps = p + 1;
			p++;
		}
		if (pa < ps)
			pa = p;
		sz = (pa - ps) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property) + sz,
					__alignof__(struct property));
		if (allnextpp) {
			pp->name = "name";
			pp->length = sz;
			pp->value = (unsigned char *)(pp + 1);
			*prev_pp = pp;
			prev_pp = &pp->next;
			memcpy(pp->value, ps, sz - 1);
			((char *)pp->value)[sz - 1] = 0;
			DBG("fixed up name for %s -> %s\n", pathp, pp->value);
		}
	}
	if (allnextpp) {
		*prev_pp = NULL;
		np->name = get_property(np, "name", NULL);
		np->type = get_property(np, "device_type", NULL);

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
		printk("Weird tag at end of node: %x\n", tag);
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
	char *p = NULL;
	int l = 0;

	DBG(" -> unflatten_device_tree()\n");

	/* First pass, scan for size */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	size = unflatten_dt_node(0, &start, NULL, NULL, 0);
	size = (size | 3) + 1;

	DBG("  size is %lx, allocating...\n", size);

	/* Allocate memory for the expanded device tree */
	mem = lmb_alloc(size + 4, __alignof__(struct device_node));
	if (!mem) {
		DBG("Couldn't allocate memory with lmb_alloc()!\n");
		panic("Couldn't allocate memory with lmb_alloc()!\n");
	}
	mem = (unsigned long)abs_to_virt(mem);

	((u32 *)mem)[size / 4] = 0xdeadbeef;

	DBG("  unflattening...\n", mem);

	/* Second pass, do actual unflattening */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	unflatten_dt_node(mem, &start, NULL, &allnextp, 0);
	if (*((u32 *)start) != OF_DT_END)
		printk(KERN_WARNING "Weird tag at end of tree: %08x\n", *((u32 *)start));
	if (((u32 *)mem)[size / 4] != 0xdeadbeef)
		printk(KERN_WARNING "End of tree marker overwritten: %08x\n",
		       ((u32 *)mem)[size / 4] );
	*allnextp = NULL;

	/* Get pointer to OF "/chosen" node for use everywhere */
	of_chosen = of_find_node_by_path("/chosen");

	/* Retreive command line */
	if (of_chosen != NULL) {
		p = (char *)get_property(of_chosen, "bootargs", &l);
		if (p != NULL && l > 0)
			strlcpy(cmd_line, p, min(l, COMMAND_LINE_SIZE));
	}
#ifdef CONFIG_CMDLINE
	if (l == 0 || (l == 1 && (*p) == 0))
		strlcpy(cmd_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif /* CONFIG_CMDLINE */

	DBG("Command line is: %s\n", cmd_line);

	DBG(" <- unflatten_device_tree()\n");
}


static int __init early_init_dt_scan_cpus(unsigned long node,
					  const char *uname, int depth, void *data)
{
	char *type = get_flat_dt_prop(node, "device_type", NULL);
	u32 *prop;
	unsigned long size;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	/* On LPAR, look for the first ibm,pft-size property for the  hash table size
	 */
	if (systemcfg->platform == PLATFORM_PSERIES_LPAR && ppc64_pft_size == 0) {
		u32 *pft_size;
		pft_size = (u32 *)get_flat_dt_prop(node, "ibm,pft-size", NULL);
		if (pft_size != NULL) {
			/* pft_size[0] is the NUMA CEC cookie */
			ppc64_pft_size = pft_size[1];
		}
	}

	if (initial_boot_params && initial_boot_params->version >= 2) {
		/* version 2 of the kexec param format adds the phys cpuid
		 * of booted proc.
		 */
		boot_cpuid_phys = initial_boot_params->boot_cpuid_phys;
		boot_cpuid = 0;
	} else {
		/* Check if it's the boot-cpu, set it's hw index in paca now */
		if (get_flat_dt_prop(node, "linux,boot-cpu", NULL) != NULL) {
			u32 *prop = get_flat_dt_prop(node, "reg", NULL);
			set_hard_smp_processor_id(0, prop == NULL ? 0 : *prop);
			boot_cpuid_phys = get_hard_smp_processor_id(0);
		}
	}

#ifdef CONFIG_ALTIVEC
	/* Check if we have a VMX and eventually update CPU features */
	prop = (u32 *)get_flat_dt_prop(node, "ibm,vmx", NULL);
	if (prop && (*prop) > 0) {
		cur_cpu_spec->cpu_features |= CPU_FTR_ALTIVEC;
		cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_ALTIVEC;
	}

	/* Same goes for Apple's "altivec" property */
	prop = (u32 *)get_flat_dt_prop(node, "altivec", NULL);
	if (prop) {
		cur_cpu_spec->cpu_features |= CPU_FTR_ALTIVEC;
		cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_ALTIVEC;
	}
#endif /* CONFIG_ALTIVEC */

	/*
	 * Check for an SMT capable CPU and set the CPU feature. We do
	 * this by looking at the size of the ibm,ppc-interrupt-server#s
	 * property
	 */
	prop = (u32 *)get_flat_dt_prop(node, "ibm,ppc-interrupt-server#s",
				       &size);
	cur_cpu_spec->cpu_features &= ~CPU_FTR_SMT;
	if (prop && ((size / sizeof(u32)) > 1))
		cur_cpu_spec->cpu_features |= CPU_FTR_SMT;

	return 0;
}

static int __init early_init_dt_scan_chosen(unsigned long node,
					    const char *uname, int depth, void *data)
{
	u32 *prop;
	u64 *prop64;

	DBG("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

	if (depth != 1 || strcmp(uname, "chosen") != 0)
		return 0;

	/* get platform type */
	prop = (u32 *)get_flat_dt_prop(node, "linux,platform", NULL);
	if (prop == NULL)
		return 0;
	systemcfg->platform = *prop;

	/* check if iommu is forced on or off */
	if (get_flat_dt_prop(node, "linux,iommu-off", NULL) != NULL)
		iommu_is_off = 1;
	if (get_flat_dt_prop(node, "linux,iommu-force-on", NULL) != NULL)
		iommu_force_on = 1;

 	prop64 = (u64*)get_flat_dt_prop(node, "linux,memory-limit", NULL);
 	if (prop64)
 		memory_limit = *prop64;

 	prop64 = (u64*)get_flat_dt_prop(node, "linux,tce-alloc-start", NULL);
 	if (prop64)
 		tce_alloc_start = *prop64;

 	prop64 = (u64*)get_flat_dt_prop(node, "linux,tce-alloc-end", NULL);
 	if (prop64)
 		tce_alloc_end = *prop64;

#ifdef CONFIG_PPC_RTAS
	/* To help early debugging via the front panel, we retreive a minimal
	 * set of RTAS infos now if available
	 */
	{
		u64 *basep, *entryp;

		basep = (u64*)get_flat_dt_prop(node, "linux,rtas-base", NULL);
		entryp = (u64*)get_flat_dt_prop(node, "linux,rtas-entry", NULL);
		prop = (u32*)get_flat_dt_prop(node, "linux,rtas-size", NULL);
		if (basep && entryp && prop) {
			rtas.base = *basep;
			rtas.entry = *entryp;
			rtas.size = *prop;
		}
	}
#endif /* CONFIG_PPC_RTAS */

	/* break now */
	return 1;
}

static int __init early_init_dt_scan_root(unsigned long node,
					  const char *uname, int depth, void *data)
{
	u32 *prop;

	if (depth != 0)
		return 0;

	prop = (u32 *)get_flat_dt_prop(node, "#size-cells", NULL);
	dt_root_size_cells = (prop == NULL) ? 1 : *prop;
	DBG("dt_root_size_cells = %x\n", dt_root_size_cells);

	prop = (u32 *)get_flat_dt_prop(node, "#address-cells", NULL);
	dt_root_addr_cells = (prop == NULL) ? 2 : *prop;
	DBG("dt_root_addr_cells = %x\n", dt_root_addr_cells);
	
	/* break now */
	return 1;
}

static unsigned long __init dt_mem_next_cell(int s, cell_t **cellp)
{
	cell_t *p = *cellp;
	unsigned long r = 0;

	/* Ignore more than 2 cells */
	while (s > 2) {
		p++;
		s--;
	}
	while (s) {
		r <<= 32;
		r |= *(p++);
		s--;
	}

	*cellp = p;
	return r;
}


static int __init early_init_dt_scan_memory(unsigned long node,
					    const char *uname, int depth, void *data)
{
	char *type = get_flat_dt_prop(node, "device_type", NULL);
	cell_t *reg, *endp;
	unsigned long l;

	/* We are scanning "memory" nodes only */
	if (type == NULL || strcmp(type, "memory") != 0)
		return 0;

	reg = (cell_t *)get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(cell_t));

	DBG("memory scan node %s ..., reg size %ld, data: %x %x %x %x, ...\n",
	    uname, l, reg[0], reg[1], reg[2], reg[3]);

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		unsigned long base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (size == 0)
			continue;
		DBG(" - %lx ,  %lx\n", base, size);
		if (iommu_is_off) {
			if (base >= 0x80000000ul)
				continue;
			if ((base + size) > 0x80000000ul)
				size = 0x80000000ul - base;
		}
		lmb_add(base, size);
	}
	return 0;
}

static void __init early_reserve_mem(void)
{
	u64 base, size;
	u64 *reserve_map = (u64 *)(((unsigned long)initial_boot_params) +
				   initial_boot_params->off_mem_rsvmap);
	while (1) {
		base = *(reserve_map++);
		size = *(reserve_map++);
		if (size == 0)
			break;
		DBG("reserving: %lx -> %lx\n", base, size);
		lmb_reserve(base, size);
	}

#if 0
	DBG("memory reserved, lmbs :\n");
      	lmb_dump_all();
#endif
}

void __init early_init_devtree(void *params)
{
	DBG(" -> early_init_devtree()\n");

	/* Setup flat device-tree pointer */
	initial_boot_params = params;

	/* By default, hash size is not set */
	ppc64_pft_size = 0;

	/* Retreive various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, TCE reserve, and more ...
	 */
	scan_flat_dt(early_init_dt_scan_chosen, NULL);

	/* Scan memory nodes and rebuild LMBs */
	lmb_init();
	scan_flat_dt(early_init_dt_scan_root, NULL);
	scan_flat_dt(early_init_dt_scan_memory, NULL);
	lmb_enforce_memory_limit(memory_limit);
	lmb_analyze();
	systemcfg->physicalMemorySize = lmb_phys_mem_size();
	lmb_reserve(0, __pa(klimit));

	DBG("Phys. mem: %lx\n", systemcfg->physicalMemorySize);

	/* Reserve LMB regions used by kernel, initrd, dt, etc... */
	early_reserve_mem();

	DBG("Scanning CPUs ...\n");

	/* Retreive hash table size from flattened tree plus other
	 * CPU related informations (altivec support, boot CPU ID, ...)
	 */
	scan_flat_dt(early_init_dt_scan_cpus, NULL);

	/* If hash size wasn't obtained above, we calculate it now based on
	 * the total RAM size
	 */
	if (ppc64_pft_size == 0) {
		unsigned long rnd_mem_size, pteg_count;

		/* round mem_size up to next power of 2 */
		rnd_mem_size = 1UL << __ilog2(systemcfg->physicalMemorySize);
		if (rnd_mem_size < systemcfg->physicalMemorySize)
			rnd_mem_size <<= 1;

		/* # pages / 2 */
		pteg_count = max(rnd_mem_size >> (12 + 1), 1UL << 11);

		ppc64_pft_size = __ilog2(pteg_count << 7);
	}

	DBG("Hash pftSize: %x\n", (int)ppc64_pft_size);
	DBG(" <- early_init_devtree()\n");
}

#undef printk

int
prom_n_addr_cells(struct device_node* np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = (int *) get_property(np, "#address-cells", NULL);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #address-cells property for the root node, default to 1 */
	return 1;
}

int
prom_n_size_cells(struct device_node* np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = (int *) get_property(np, "#size-cells", NULL);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #size-cells property for the root node, default to 1 */
	return 1;
}

/**
 * Work out the sense (active-low level / active-high edge)
 * of each interrupt from the device tree.
 */
void __init prom_get_irq_senses(unsigned char *senses, int off, int max)
{
	struct device_node *np;
	int i, j;

	/* default to level-triggered */
	memset(senses, 1, max - off);

	for (np = allnodes; np != 0; np = np->allnext) {
		for (j = 0; j < np->n_intrs; j++) {
			i = np->intrs[j].line;
			if (i >= off && i < max)
				senses[i-off] = np->intrs[j].sense ?
					IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE :
					IRQ_SENSE_EDGE | IRQ_POLARITY_POSITIVE;
		}
	}
}

/**
 * Construct and return a list of the device_nodes with a given name.
 */
struct device_node *
find_devices(const char *name)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (np->name != 0 && strcasecmp(np->name, name) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = NULL;
	return head;
}
EXPORT_SYMBOL(find_devices);

/**
 * Construct and return a list of the device_nodes with a given type.
 */
struct device_node *
find_type_devices(const char *type)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (np->type != 0 && strcasecmp(np->type, type) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = NULL;
	return head;
}
EXPORT_SYMBOL(find_type_devices);

/**
 * Returns all nodes linked together
 */
struct device_node *
find_all_nodes(void)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		*prevp = np;
		prevp = &np->next;
	}
	*prevp = NULL;
	return head;
}
EXPORT_SYMBOL(find_all_nodes);

/** Checks if the given "compat" string matches one of the strings in
 * the device's "compatible" property
 */
int
device_is_compatible(struct device_node *device, const char *compat)
{
	const char* cp;
	int cplen, l;

	cp = (char *) get_property(device, "compatible", &cplen);
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
EXPORT_SYMBOL(device_is_compatible);


/**
 * Indicates whether the root node has a given value in its
 * compatible property.
 */
int
machine_is_compatible(const char *compat)
{
	struct device_node *root;
	int rc = 0;

	root = of_find_node_by_path("/");
	if (root) {
		rc = device_is_compatible(root, compat);
		of_node_put(root);
	}
	return rc;
}
EXPORT_SYMBOL(machine_is_compatible);

/**
 * Construct and return a list of the device_nodes with a given type
 * and compatible property.
 */
struct device_node *
find_compatible_devices(const char *type, const char *compat)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcasecmp(np->type, type) == 0))
			continue;
		if (device_is_compatible(np, compat)) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = NULL;
	return head;
}
EXPORT_SYMBOL(find_compatible_devices);

/**
 * Find the device_node with a given full_name.
 */
struct device_node *
find_path_device(const char *path)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->full_name != 0 && strcasecmp(np->full_name, path) == 0)
			return np;
	return NULL;
}
EXPORT_SYMBOL(find_path_device);

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
 *	of_find_node_by_name - Find a node by its "name" property
 *	@from:	The node to start searching from or NULL, the node
 *		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 *	@name:	The name string to match against
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np != 0; np = np->allnext)
		if (np->name != 0 && strcasecmp(np->name, name) == 0
		    && of_node_get(np))
			break;
	if (from)
		of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_name);

/**
 *	of_find_node_by_type - Find a node by its "device_type" property
 *	@from:	The node to start searching from or NULL, the node
 *		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 *	@name:	The type string to match against
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np != 0; np = np->allnext)
		if (np->type != 0 && strcasecmp(np->type, type) == 0
		    && of_node_get(np))
			break;
	if (from)
		of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_type);

/**
 *	of_find_compatible_node - Find a node based on type and one of the
 *                                tokens in its "compatible" property
 *	@from:		The node to start searching from or NULL, the node
 *			you pass will not be searched, only the next one
 *			will; typically, you pass what the previous call
 *			returned. of_node_put() will be called on it
 *	@type:		The type string to match "device_type" or NULL to ignore
 *	@compatible:	The string to match to one of the tokens in the device
 *			"compatible" list.
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compatible)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcasecmp(np->type, type) == 0))
			continue;
		if (device_is_compatible(np, compatible) && of_node_get(np))
			break;
	}
	if (from)
		of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_compatible_node);

/**
 *	of_find_node_by_path - Find a node matching a full OF path
 *	@path:	The full path to match
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_path(const char *path)
{
	struct device_node *np = allnodes;

	read_lock(&devtree_lock);
	for (; np != 0; np = np->allnext) {
		if (np->full_name != 0 && strcasecmp(np->full_name, path) == 0
		    && of_node_get(np))
			break;
	}
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_path);

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
	for (np = allnodes; np != 0; np = np->allnext)
		if (np->linux_phandle == handle)
			break;
	if (np)
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
	for (; np != 0; np = np->allnext)
		if (of_node_get(np))
			break;
	if (prev)
		of_node_put(prev);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_all_nodes);

/**
 *	of_get_parent - Get a node's parent if any
 *	@node:	Node to get parent
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_get_parent(const struct device_node *node)
{
	struct device_node *np;

	if (!node)
		return NULL;

	read_lock(&devtree_lock);
	np = of_node_get(node->parent);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_get_parent);

/**
 *	of_get_next_child - Iterate a node childs
 *	@node:	parent node
 *	@prev:	previous child of the parent node, or NULL to get first
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_get_next_child(const struct device_node *node,
	struct device_node *prev)
{
	struct device_node *next;

	read_lock(&devtree_lock);
	next = prev ? prev->sibling : node->child;
	for (; next != 0; next = next->sibling)
		if (of_node_get(next))
			break;
	if (prev)
		of_node_put(prev);
	read_unlock(&devtree_lock);
	return next;
}
EXPORT_SYMBOL(of_get_next_child);

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

static inline struct device_node * kref_to_device_node(struct kref *kref)
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

	if (!OF_IS_DYNAMIC(node))
		return;
	while (prop) {
		struct property *next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
		prop = next;
	}
	kfree(node->intrs);
	kfree(node->addrs);
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
 * Fix up the uninitialized fields in a new device node:
 * name, type, n_addrs, addrs, n_intrs, intrs, and pci-specific fields
 *
 * A lot of boot-time code is duplicated here, because functions such
 * as finish_node_interrupts, interpret_pci_props, etc. cannot use the
 * slab allocator.
 *
 * This should probably be split up into smaller chunks.
 */

static int of_finish_dynamic_node(struct device_node *node,
				  unsigned long *unused1, int unused2,
				  int unused3, int unused4)
{
	struct device_node *parent = of_get_parent(node);
	int err = 0;
	phandle *ibm_phandle;

	node->name = get_property(node, "name", NULL);
	node->type = get_property(node, "device_type", NULL);

	if (!parent) {
		err = -ENODEV;
		goto out;
	}

	/* We don't support that function on PowerMac, at least
	 * not yet
	 */
	if (systemcfg->platform == PLATFORM_POWERMAC)
		return -ENODEV;

	/* fix up new node's linux_phandle field */
	if ((ibm_phandle = (unsigned int *)get_property(node, "ibm,phandle", NULL)))
		node->linux_phandle = *ibm_phandle;

out:
	of_node_put(parent);
	return err;
}

/*
 * Plug a device node into the tree and global list.
 */
void of_attach_node(struct device_node *np)
{
	write_lock(&devtree_lock);
	np->sibling = np->parent->child;
	np->allnext = allnodes;
	np->parent->child = np;
	allnodes = np;
	write_unlock(&devtree_lock);
}

/*
 * "Unplug" a node from the device tree.  The caller must hold
 * a reference to the node.  The memory associated with the node
 * is not freed until its refcount goes to zero.
 */
void of_detach_node(const struct device_node *np)
{
	struct device_node *parent;

	write_lock(&devtree_lock);

	parent = np->parent;

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

	write_unlock(&devtree_lock);
}

static int prom_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	int err;

	switch (action) {
	case PSERIES_RECONFIG_ADD:
		err = finish_node(node, NULL, of_finish_dynamic_node, 0, 0, 0);
		if (err < 0) {
			printk(KERN_ERR "finish_node returned %d\n", err);
			err = NOTIFY_BAD;
		}
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block prom_reconfig_nb = {
	.notifier_call = prom_reconfig_notifier,
	.priority = 10, /* This one needs to run first */
};

static int __init prom_reconfig_setup(void)
{
	return pSeries_reconfig_notifier_register(&prom_reconfig_nb);
}
__initcall(prom_reconfig_setup);

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
unsigned char *
get_property(struct device_node *np, const char *name, int *lenp)
{
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next)
		if (strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			return pp->value;
		}
	return NULL;
}
EXPORT_SYMBOL(get_property);

/*
 * Add a property to a node
 */
void
prom_add_property(struct device_node* np, struct property* prop)
{
	struct property **next = &np->properties;

	prop->next = NULL;	
	while (*next)
		next = &(*next)->next;
	*next = prop;
}

#if 0
void
print_properties(struct device_node *np)
{
	struct property *pp;
	char *cp;
	int i, n;

	for (pp = np->properties; pp != 0; pp = pp->next) {
		printk(KERN_INFO "%s", pp->name);
		for (i = strlen(pp->name); i < 16; ++i)
			printk(" ");
		cp = (char *) pp->value;
		for (i = pp->length; i > 0; --i, ++cp)
			if ((i > 1 && (*cp < 0x20 || *cp > 0x7e))
			    || (i == 1 && *cp != 0))
				break;
		if (i == 0 && pp->length > 1) {
			/* looks like a string */
			printk(" %s\n", (char *) pp->value);
		} else {
			/* dump it in hex */
			n = pp->length;
			if (n > 64)
				n = 64;
			if (pp->length % 4 == 0) {
				unsigned int *p = (unsigned int *) pp->value;

				n /= 4;
				for (i = 0; i < n; ++i) {
					if (i != 0 && (i % 4) == 0)
						printk("\n                ");
					printk(" %08x", *p++);
				}
			} else {
				unsigned char *bp = pp->value;

				for (i = 0; i < n; ++i) {
					if (i != 0 && (i % 16) == 0)
						printk("\n                ");
					printk(" %02x", *bp++);
				}
			}
			printk("\n");
			if (pp->length > 64)
				printk("                 ... (length = %d)\n",
				       pp->length);
		}
	}
}
#endif










