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
#include <linux/kexec.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/lmb.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/kdump.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>
#include <asm/pSeries_reconfig.h>
#include <asm/pci-bridge.h>
#include <asm/kexec.h>

#ifdef DEBUG
#define DBG(fmt...) printk(KERN_ERR fmt)
#else
#define DBG(fmt...)
#endif


static int __initdata dt_root_addr_cells;
static int __initdata dt_root_size_cells;

#ifdef CONFIG_PPC64
int __initdata iommu_is_off;
int __initdata iommu_force_on;
unsigned long tce_alloc_start, tce_alloc_end;
#endif

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

struct device_node *dflt_interrupt_controller;
int num_interrupt_controllers;

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
	p = find_phandle(*parp);
	if (p != NULL)
		return p;
	/*
	 * On a powermac booted with BootX, we don't get to know the
	 * phandles for any nodes, so find_phandle will return NULL.
	 * Fortunately these machines only have one interrupt controller
	 * so there isn't in fact any ambiguity.  -- paulus
	 */
	if (num_interrupt_controllers == 1)
		p = dflt_interrupt_controller;
	return p;
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
			if (ipar == NULL && num_interrupt_controllers == 1)
				/* cope with BootX not giving us phandles */
				ipar = dflt_interrupt_controller;
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

static unsigned char map_isa_senses[4] = {
	IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE,
	IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE,
	IRQ_SENSE_EDGE  | IRQ_POLARITY_NEGATIVE,
	IRQ_SENSE_EDGE  | IRQ_POLARITY_POSITIVE
};

static unsigned char map_mpic_senses[4] = {
	IRQ_SENSE_EDGE  | IRQ_POLARITY_POSITIVE,
	IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE,
	/* 2 seems to be used for the 8259 cascade... */
	IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE,
	IRQ_SENSE_EDGE  | IRQ_POLARITY_NEGATIVE,
};

static int __devinit finish_node_interrupts(struct device_node *np,
					    unsigned long *mem_start,
					    int measure_only)
{
	unsigned int *ints;
	int intlen, intrcells, intrcount;
	int i, j, n, sense;
	unsigned int *irq, virq;
	struct device_node *ic;
	int trace = 0;

	//#define TRACE(fmt...) do { if (trace) { printk(fmt); mdelay(1000); } } while(0)
#define TRACE(fmt...)

	if (!strcmp(np->name, "smu-doorbell"))
		trace = 1;

	TRACE("Finishing SMU doorbell ! num_interrupt_controllers = %d\n",
	      num_interrupt_controllers);

	if (num_interrupt_controllers == 0) {
		/*
		 * Old machines just have a list of interrupt numbers
		 * and no interrupt-controller nodes.
		 */
		ints = (unsigned int *) get_property(np, "AAPL,interrupts",
						     &intlen);
		/* XXX old interpret_pci_props looked in parent too */
		/* XXX old interpret_macio_props looked for interrupts
		   before AAPL,interrupts */
		if (ints == NULL)
			ints = (unsigned int *) get_property(np, "interrupts",
							     &intlen);
		if (ints == NULL)
			return 0;

		np->n_intrs = intlen / sizeof(unsigned int);
		np->intrs = prom_alloc(np->n_intrs * sizeof(np->intrs[0]),
				       mem_start);
		if (!np->intrs)
			return -ENOMEM;
		if (measure_only)
			return 0;

		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ints++;
			np->intrs[i].sense = IRQ_SENSE_LEVEL
				| IRQ_POLARITY_NEGATIVE;
		}
		return 0;
	}

	ints = (unsigned int *) get_property(np, "interrupts", &intlen);
	TRACE("ints=%p, intlen=%d\n", ints, intlen);
	if (ints == NULL)
		return 0;
	intrcells = prom_n_intr_cells(np);
	intlen /= intrcells * sizeof(unsigned int);
	TRACE("intrcells=%d, new intlen=%d\n", intrcells, intlen);
	np->intrs = prom_alloc(intlen * sizeof(*(np->intrs)), mem_start);
	if (!np->intrs)
		return -ENOMEM;

	if (measure_only)
		return 0;

	intrcount = 0;
	for (i = 0; i < intlen; ++i, ints += intrcells) {
		n = map_interrupt(&irq, &ic, np, ints, intrcells);
		TRACE("map, irq=%d, ic=%p, n=%d\n", irq, ic, n);
		if (n <= 0)
			continue;

		/* don't map IRQ numbers under a cascaded 8259 controller */
		if (ic && device_is_compatible(ic, "chrp,iic")) {
			np->intrs[intrcount].line = irq[0];
			sense = (n > 1)? (irq[1] & 3): 3;
			np->intrs[intrcount].sense = map_isa_senses[sense];
		} else {
			virq = virt_irq_create_mapping(irq[0]);
			TRACE("virq=%d\n", virq);
#ifdef CONFIG_PPC64
			if (virq == NO_IRQ) {
				printk(KERN_CRIT "Could not allocate interrupt"
				       " number for %s\n", np->full_name);
				continue;
			}
#endif
			np->intrs[intrcount].line = irq_offset_up(virq);
			sense = (n > 1)? (irq[1] & 3): 1;

			/* Apple uses bits in there in a different way, let's
			 * only keep the real sense bit on macs
			 */
			if (machine_is(powermac))
				sense &= 0x1;
			np->intrs[intrcount].sense = map_mpic_senses[sense];
		}

#ifdef CONFIG_PPC64
		/* We offset irq numbers for the u3 MPIC by 128 in PowerMac */
		if (machine_is(powermac) && ic && ic->parent) {
			char *name = get_property(ic->parent, "name", NULL);
			if (name && !strcmp(name, "u3"))
				np->intrs[intrcount].line += 128;
			else if (!(name && (!strcmp(name, "mac-io") ||
					    !strcmp(name, "u4"))))
				/* ignore other cascaded controllers, such as
				   the k2-sata-root */
				break;
		}
#endif /* CONFIG_PPC64 */
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

static int __devinit finish_node(struct device_node *np,
				 unsigned long *mem_start,
				 int measure_only)
{
	struct device_node *child;
	int rc = 0;

	rc = finish_node_interrupts(np, mem_start, measure_only);
	if (rc)
		goto out;

	for (child = np->child; child != NULL; child = child->sibling) {
		rc = finish_node(child, mem_start, measure_only);
		if (rc)
			goto out;
	}
out:
	return rc;
}

static void __init scan_interrupt_controllers(void)
{
	struct device_node *np;
	int n = 0;
	char *name, *ic;
	int iclen;

	for (np = allnodes; np != NULL; np = np->allnext) {
		ic = get_property(np, "interrupt-controller", &iclen);
		name = get_property(np, "name", NULL);
		/* checking iclen makes sure we don't get a false
		   match on /chosen.interrupt_controller */
		if ((name != NULL
		     && strcmp(name, "interrupt-controller") == 0)
		    || (ic != NULL && iclen == 0
			&& strcmp(name, "AppleKiwi"))) {
			if (n == 0)
				dflt_interrupt_controller = np;
			++n;
		}
	}
	num_interrupt_controllers = n;
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

#ifdef CONFIG_PPC64
	/* Initialize virtual IRQ map */
	virt_irq_init();
#endif
	scan_interrupt_controllers();

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
	finish_node(allnodes, &size, 1);
	size -= 16;

	if (0 == size)
		end = start = 0;
	else
		end = start = (unsigned long)__va(lmb_alloc(size, 128));

	finish_node(allnodes, &end, 0);
	BUG_ON(end != start + size);

	DBG(" <- finish_device_tree\n");
}

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

unsigned long __init of_get_flat_dt_root(void)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;

	while(*((u32 *)p) == OF_DT_NOP)
		p += 4;
	BUG_ON (*((u32 *)p) != OF_DT_BEGIN_NODE);
	p += 4;
	return _ALIGN(p + strlen((char *)p) + 1, 4);
}

/**
 * This  function can be used within scan_flattened_dt callback to get
 * access to properties
 */
void* __init of_get_flat_dt_prop(unsigned long node, const char *name,
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

int __init of_flat_dt_is_compatible(unsigned long node, const char *compat)
{
	const char* cp;
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
		np->full_name = ((char*)np) + sizeof(struct device_node);
		if (new_format) {
			char *p = np->full_name;
			/* rebuild full path for new format */
			if (dad && dad->parent) {
				strcpy(p, dad->full_name);
#ifdef DEBUG
				if ((strlen(p) + l + 1) != allocl) {
					DBG("%s: p: %d, l: %d, a: %d\n",
					    pathp, (int)strlen(p), l, allocl);
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

static int __init early_parse_mem(char *p)
{
	if (!p)
		return 1;

	memory_limit = PAGE_ALIGN(memparse(p, &p));
	DBG("memory limit = 0x%lx\n", memory_limit);

	return 0;
}
early_param("mem", early_parse_mem);

/*
 * The device tree may be allocated below our memory limit, or inside the
 * crash kernel region for kdump. If so, move it out now.
 */
static void move_device_tree(void)
{
	unsigned long start, size;
	void *p;

	DBG("-> move_device_tree\n");

	start = __pa(initial_boot_params);
	size = initial_boot_params->totalsize;

	if ((memory_limit && (start + size) > memory_limit) ||
			overlaps_crashkernel(start, size)) {
		p = __va(lmb_alloc_base(size, PAGE_SIZE, lmb.rmo_size));
		memcpy(p, initial_boot_params, size);
		initial_boot_params = (struct boot_param_header *)p;
		DBG("Moved device tree to 0x%p\n", p);
	}

	DBG("<- move_device_tree\n");
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

	DBG(" -> unflatten_device_tree()\n");

	/* First pass, scan for size */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	size = unflatten_dt_node(0, &start, NULL, NULL, 0);
	size = (size | 3) + 1;

	DBG("  size is %lx, allocating...\n", size);

	/* Allocate memory for the expanded device tree */
	mem = lmb_alloc(size + 4, __alignof__(struct device_node));
	mem = (unsigned long) __va(mem);

	((u32 *)mem)[size / 4] = 0xdeadbeef;

	DBG("  unflattening %lx...\n", mem);

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
	if (of_chosen == NULL)
		of_chosen = of_find_node_by_path("/chosen@0");

	DBG(" <- unflatten_device_tree()\n");
}

/*
 * ibm,pa-features is a per-cpu property that contains a string of
 * attribute descriptors, each of which has a 2 byte header plus up
 * to 254 bytes worth of processor attribute bits.  First header
 * byte specifies the number of bytes following the header.
 * Second header byte is an "attribute-specifier" type, of which
 * zero is the only currently-defined value.
 * Implementation:  Pass in the byte and bit offset for the feature
 * that we are interested in.  The function will return -1 if the
 * pa-features property is missing, or a 1/0 to indicate if the feature
 * is supported/not supported.  Note that the bit numbers are
 * big-endian to match the definition in PAPR.
 */
static struct ibm_pa_feature {
	unsigned long	cpu_features;	/* CPU_FTR_xxx bit */
	unsigned int	cpu_user_ftrs;	/* PPC_FEATURE_xxx bit */
	unsigned char	pabyte;		/* byte number in ibm,pa-features */
	unsigned char	pabit;		/* bit number (big-endian) */
	unsigned char	invert;		/* if 1, pa bit set => clear feature */
} ibm_pa_features[] __initdata = {
	{0, PPC_FEATURE_HAS_MMU,	0, 0, 0},
	{0, PPC_FEATURE_HAS_FPU,	0, 1, 0},
	{CPU_FTR_SLB, 0,		0, 2, 0},
	{CPU_FTR_CTRL, 0,		0, 3, 0},
	{CPU_FTR_NOEXECUTE, 0,		0, 6, 0},
	{CPU_FTR_NODSISRALIGN, 0,	1, 1, 1},
	{CPU_FTR_CI_LARGE_PAGE, 0,	1, 2, 0},
};

static void __init check_cpu_pa_features(unsigned long node)
{
	unsigned char *pa_ftrs;
	unsigned long len, tablelen, i, bit;

	pa_ftrs = of_get_flat_dt_prop(node, "ibm,pa-features", &tablelen);
	if (pa_ftrs == NULL)
		return;

	/* find descriptor with type == 0 */
	for (;;) {
		if (tablelen < 3)
			return;
		len = 2 + pa_ftrs[0];
		if (tablelen < len)
			return;		/* descriptor 0 not found */
		if (pa_ftrs[1] == 0)
			break;
		tablelen -= len;
		pa_ftrs += len;
	}

	/* loop over bits we know about */
	for (i = 0; i < ARRAY_SIZE(ibm_pa_features); ++i) {
		struct ibm_pa_feature *fp = &ibm_pa_features[i];

		if (fp->pabyte >= pa_ftrs[0])
			continue;
		bit = (pa_ftrs[2 + fp->pabyte] >> (7 - fp->pabit)) & 1;
		if (bit ^ fp->invert) {
			cur_cpu_spec->cpu_features |= fp->cpu_features;
			cur_cpu_spec->cpu_user_features |= fp->cpu_user_ftrs;
		} else {
			cur_cpu_spec->cpu_features &= ~fp->cpu_features;
			cur_cpu_spec->cpu_user_features &= ~fp->cpu_user_ftrs;
		}
	}
}

static int __init early_init_dt_scan_cpus(unsigned long node,
					  const char *uname, int depth,
					  void *data)
{
	static int logical_cpuid = 0;
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
#ifdef CONFIG_ALTIVEC
	u32 *prop;
#endif
	u32 *intserv;
	int i, nthreads;
	unsigned long len;
	int found = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	/* Get physical cpuid */
	intserv = of_get_flat_dt_prop(node, "ibm,ppc-interrupt-server#s", &len);
	if (intserv) {
		nthreads = len / sizeof(int);
	} else {
		intserv = of_get_flat_dt_prop(node, "reg", NULL);
		nthreads = 1;
	}

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
		DBG("boot cpu: logical %d physical %d\n", logical_cpuid,
			intserv[i]);
		boot_cpuid = logical_cpuid;
		set_hard_smp_processor_id(boot_cpuid, intserv[i]);
	}

#ifdef CONFIG_ALTIVEC
	/* Check if we have a VMX and eventually update CPU features */
	prop = (u32 *)of_get_flat_dt_prop(node, "ibm,vmx", NULL);
	if (prop && (*prop) > 0) {
		cur_cpu_spec->cpu_features |= CPU_FTR_ALTIVEC;
		cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_ALTIVEC;
	}

	/* Same goes for Apple's "altivec" property */
	prop = (u32 *)of_get_flat_dt_prop(node, "altivec", NULL);
	if (prop) {
		cur_cpu_spec->cpu_features |= CPU_FTR_ALTIVEC;
		cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_ALTIVEC;
	}
#endif /* CONFIG_ALTIVEC */

	check_cpu_pa_features(node);

#ifdef CONFIG_PPC_PSERIES
	if (nthreads > 1)
		cur_cpu_spec->cpu_features |= CPU_FTR_SMT;
	else
		cur_cpu_spec->cpu_features &= ~CPU_FTR_SMT;
#endif

	return 0;
}

static int __init early_init_dt_scan_chosen(unsigned long node,
					    const char *uname, int depth, void *data)
{
	unsigned long *lprop;
	unsigned long l;
	char *p;

	DBG("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

	if (depth != 1 ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

#ifdef CONFIG_PPC64
	/* check if iommu is forced on or off */
	if (of_get_flat_dt_prop(node, "linux,iommu-off", NULL) != NULL)
		iommu_is_off = 1;
	if (of_get_flat_dt_prop(node, "linux,iommu-force-on", NULL) != NULL)
		iommu_force_on = 1;
#endif

	/* mem=x on the command line is the preferred mechanism */
 	lprop = of_get_flat_dt_prop(node, "linux,memory-limit", NULL);
 	if (lprop)
 		memory_limit = *lprop;

#ifdef CONFIG_PPC64
 	lprop = of_get_flat_dt_prop(node, "linux,tce-alloc-start", NULL);
 	if (lprop)
 		tce_alloc_start = *lprop;
 	lprop = of_get_flat_dt_prop(node, "linux,tce-alloc-end", NULL);
 	if (lprop)
 		tce_alloc_end = *lprop;
#endif

#ifdef CONFIG_PPC_RTAS
	/* To help early debugging via the front panel, we retrieve a minimal
	 * set of RTAS infos now if available
	 */
	{
		u64 *basep, *entryp, *sizep;

		basep = of_get_flat_dt_prop(node, "linux,rtas-base", NULL);
		entryp = of_get_flat_dt_prop(node, "linux,rtas-entry", NULL);
		sizep = of_get_flat_dt_prop(node, "linux,rtas-size", NULL);
		if (basep && entryp && sizep) {
			rtas.base = *basep;
			rtas.entry = *entryp;
			rtas.size = *sizep;
		}
	}
#endif /* CONFIG_PPC_RTAS */

#ifdef CONFIG_KEXEC
       lprop = (u64*)of_get_flat_dt_prop(node, "linux,crashkernel-base", NULL);
       if (lprop)
               crashk_res.start = *lprop;

       lprop = (u64*)of_get_flat_dt_prop(node, "linux,crashkernel-size", NULL);
       if (lprop)
               crashk_res.end = crashk_res.start + *lprop - 1;
#endif

	/* Retreive command line */
 	p = of_get_flat_dt_prop(node, "bootargs", &l);
	if (p != NULL && l > 0)
		strlcpy(cmd_line, p, min((int)l, COMMAND_LINE_SIZE));

#ifdef CONFIG_CMDLINE
	if (l == 0 || (l == 1 && (*p) == 0))
		strlcpy(cmd_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif /* CONFIG_CMDLINE */

	DBG("Command line is: %s\n", cmd_line);

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
	DBG("dt_root_size_cells = %x\n", dt_root_size_cells);

	prop = of_get_flat_dt_prop(node, "#address-cells", NULL);
	dt_root_addr_cells = (prop == NULL) ? 2 : *prop;
	DBG("dt_root_addr_cells = %x\n", dt_root_addr_cells);
	
	/* break now */
	return 1;
}

static unsigned long __init dt_mem_next_cell(int s, cell_t **cellp)
{
	cell_t *p = *cellp;
	unsigned long r;

	/* Ignore more than 2 cells */
	while (s > sizeof(unsigned long) / 4) {
		p++;
		s--;
	}
	r = *p++;
#ifdef CONFIG_PPC64
	if (s > 1) {
		r <<= 32;
		r |= *(p++);
		s--;
	}
#endif

	*cellp = p;
	return r;
}


static int __init early_init_dt_scan_memory(unsigned long node,
					    const char *uname, int depth, void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	cell_t *reg, *endp;
	unsigned long l;

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

	DBG("memory scan node %s, reg size %ld, data: %x %x %x %x,\n",
	    uname, l, reg[0], reg[1], reg[2], reg[3]);

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		unsigned long base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (size == 0)
			continue;
		DBG(" - %lx ,  %lx\n", base, size);
#ifdef CONFIG_PPC64
		if (iommu_is_off) {
			if (base >= 0x80000000ul)
				continue;
			if ((base + size) > 0x80000000ul)
				size = 0x80000000ul - base;
		}
#endif
		lmb_add(base, size);
	}
	return 0;
}

static void __init early_reserve_mem(void)
{
	u64 base, size;
	u64 *reserve_map;

	reserve_map = (u64 *)(((unsigned long)initial_boot_params) +
					initial_boot_params->off_mem_rsvmap);
#ifdef CONFIG_PPC32
	/* 
	 * Handle the case where we might be booting from an old kexec
	 * image that setup the mem_rsvmap as pairs of 32-bit values
	 */
	if (*reserve_map > 0xffffffffull) {
		u32 base_32, size_32;
		u32 *reserve_map_32 = (u32 *)reserve_map;

		while (1) {
			base_32 = *(reserve_map_32++);
			size_32 = *(reserve_map_32++);
			if (size_32 == 0)
				break;
			DBG("reserving: %x -> %x\n", base_32, size_32);
			lmb_reserve(base_32, size_32);
		}
		return;
	}
#endif
	while (1) {
		base = *(reserve_map++);
		size = *(reserve_map++);
		if (size == 0)
			break;
		DBG("reserving: %llx -> %llx\n", base, size);
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
	strlcpy(saved_command_line, cmd_line, COMMAND_LINE_SIZE);
	parse_early_param();

	/* Reserve LMB regions used by kernel, initrd, dt, etc... */
	lmb_reserve(PHYSICAL_START, __pa(klimit) - PHYSICAL_START);
	reserve_kdump_trampoline();
	early_reserve_mem();

	lmb_enforce_memory_limit(memory_limit);
	lmb_analyze();

	DBG("Phys. mem: %lx\n", lmb_phys_mem_size());

	/* We may need to relocate the flat tree, do it now.
	 * FIXME .. and the initrd too? */
	move_device_tree();

	DBG("Scanning CPUs ...\n");

	/* Retreive CPU related informations from the flat tree
	 * (altivec support, boot CPU ID, ...)
	 */
	of_scan_flat_dt(early_init_dt_scan_cpus, NULL);

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
EXPORT_SYMBOL(prom_n_addr_cells);

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
EXPORT_SYMBOL(prom_n_size_cells);

/**
 * Work out the sense (active-low level / active-high edge)
 * of each interrupt from the device tree.
 */
void __init prom_get_irq_senses(unsigned char *senses, int off, int max)
{
	struct device_node *np;
	int i, j;

	/* default to level-triggered */
	memset(senses, IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE, max - off);

	for (np = allnodes; np != 0; np = np->allnext) {
		for (j = 0; j < np->n_intrs; j++) {
			i = np->intrs[j].line;
			if (i >= off && i < max)
				senses[i-off] = np->intrs[j].sense;
		}
	}
}

/**
 * Construct and return a list of the device_nodes with a given name.
 */
struct device_node *find_devices(const char *name)
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
struct device_node *find_type_devices(const char *type)
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
struct device_node *find_all_nodes(void)
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
int device_is_compatible(struct device_node *device, const char *compat)
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
int machine_is_compatible(const char *compat)
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
struct device_node *find_compatible_devices(const char *type,
					    const char *compat)
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
struct device_node *find_path_device(const char *path)
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
	for (; np != NULL; np = np->allnext)
		if (np->name != NULL && strcasecmp(np->name, name) == 0
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

		if (!prop) {
			prop = node->deadprops;
			node->deadprops = NULL;
		}
	}
	kfree(node->intrs);
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

#ifdef CONFIG_PPC_PSERIES
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

static int of_finish_dynamic_node(struct device_node *node)
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
	if (machine_is(powermac))
		return -ENODEV;

	/* fix up new node's linux_phandle field */
	if ((ibm_phandle = (unsigned int *)get_property(node,
							"ibm,phandle", NULL)))
		node->linux_phandle = *ibm_phandle;

out:
	of_node_put(parent);
	return err;
}

static int prom_reconfig_notifier(struct notifier_block *nb,
				  unsigned long action, void *node)
{
	int err;

	switch (action) {
	case PSERIES_RECONFIG_ADD:
		err = of_finish_dynamic_node(node);
		if (!err)
			finish_node(node, NULL, 0);
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
#endif

struct property *of_find_property(struct device_node *np, const char *name,
				  int *lenp)
{
	struct property *pp;

	read_lock(&devtree_lock);
	for (pp = np->properties; pp != 0; pp = pp->next)
		if (strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			break;
		}
	read_unlock(&devtree_lock);

	return pp;
}

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
unsigned char *get_property(struct device_node *np, const char *name,
			    int *lenp)
{
	struct property *pp = of_find_property(np,name,lenp);
	return pp ? pp->value : NULL;
}
EXPORT_SYMBOL(get_property);

/*
 * Add a property to a node
 */
int prom_add_property(struct device_node* np, struct property* prop)
{
	struct property **next;

	prop->next = NULL;	
	write_lock(&devtree_lock);
	next = &np->properties;
	while (*next) {
		if (strcmp(prop->name, (*next)->name) == 0) {
			/* duplicate ! don't insert it */
			write_unlock(&devtree_lock);
			return -1;
		}
		next = &(*next)->next;
	}
	*next = prop;
	write_unlock(&devtree_lock);

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
	int found = 0;

	write_lock(&devtree_lock);
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
	write_unlock(&devtree_lock);

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
	int found = 0;

	write_lock(&devtree_lock);
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
	write_unlock(&devtree_lock);

	if (!found)
		return -ENODEV;

#ifdef CONFIG_PROC_DEVICETREE
	/* try to add to proc as well if it was initialized */
	if (np->pde)
		proc_device_tree_update_prop(np->pde, newprop, oldprop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
}

