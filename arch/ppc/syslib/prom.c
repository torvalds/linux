/*
 * Procedures for interfacing to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * In particular, we are interested in the device tree
 * and in using some of its services (exit, write to stdout).
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <stdarg.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/bitops.h>

#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/bootx.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/btext.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>


struct pci_address {
	unsigned a_hi;
	unsigned a_mid;
	unsigned a_lo;
};

struct pci_reg_property {
	struct pci_address addr;
	unsigned size_hi;
	unsigned size_lo;
};

struct isa_reg_property {
	unsigned space;
	unsigned address;
	unsigned size;
};

typedef unsigned long interpret_func(struct device_node *, unsigned long,
				     int, int);
static interpret_func interpret_pci_props;
static interpret_func interpret_dbdma_props;
static interpret_func interpret_isa_props;
static interpret_func interpret_macio_props;
static interpret_func interpret_root_props;

extern char *klimit;

/* Set for a newworld or CHRP machine */
int use_of_interrupt_tree;
struct device_node *dflt_interrupt_controller;
int num_interrupt_controllers;

int pmac_newworld;

extern unsigned int rtas_entry;  /* physical pointer */

extern struct device_node *allnodes;

static unsigned long finish_node(struct device_node *, unsigned long,
				 interpret_func *, int, int);
static unsigned long finish_node_interrupts(struct device_node *, unsigned long);
static struct device_node *find_phandle(phandle);

extern void enter_rtas(void *);
void phys_call_rtas(int, int, int, ...);

extern char cmd_line[512];	/* XXX */
extern boot_infos_t *boot_infos;
unsigned long dev_tree_size;

void
phys_call_rtas(int service, int nargs, int nret, ...)
{
	va_list list;
	union {
		unsigned long words[16];
		double align;
	} u;
	void (*rtas)(void *, unsigned long);
	int i;

	u.words[0] = service;
	u.words[1] = nargs;
	u.words[2] = nret;
	va_start(list, nret);
	for (i = 0; i < nargs; ++i)
		u.words[i+3] = va_arg(list, unsigned long);
	va_end(list);

	rtas = (void (*)(void *, unsigned long)) rtas_entry;
	rtas(&u, rtas_data);
}

/*
 * finish_device_tree is called once things are running normally
 * (i.e. with text and data mapped to the address they were linked at).
 * It traverses the device tree and fills in the name, type,
 * {n_}addrs and {n_}intrs fields of each node.
 */
void __init
finish_device_tree(void)
{
	unsigned long mem = (unsigned long) klimit;
	struct device_node *np;

	/* All newworld pmac machines and CHRPs now use the interrupt tree */
	for (np = allnodes; np != NULL; np = np->allnext) {
		if (get_property(np, "interrupt-parent", NULL)) {
			use_of_interrupt_tree = 1;
			break;
		}
	}
	if (_machine == _MACH_Pmac && use_of_interrupt_tree)
		pmac_newworld = 1;

#ifdef CONFIG_BOOTX_TEXT
	if (boot_infos && pmac_newworld) {
		prom_print("WARNING ! BootX/miBoot booting is not supported on this machine\n");
		prom_print("          You should use an Open Firmware bootloader\n");
	}
#endif /* CONFIG_BOOTX_TEXT */

	if (use_of_interrupt_tree) {
		/*
		 * We want to find out here how many interrupt-controller
		 * nodes there are, and if we are booted from BootX,
		 * we need a pointer to the first (and hopefully only)
		 * such node.  But we can't use find_devices here since
		 * np->name has not been set yet.  -- paulus
		 */
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
			    || (ic != NULL && iclen == 0 && strcmp(name, "AppleKiwi"))) {
				if (n == 0)
					dflt_interrupt_controller = np;
				++n;
			}
		}
		num_interrupt_controllers = n;
	}

	mem = finish_node(allnodes, mem, NULL, 1, 1);
	dev_tree_size = mem - (unsigned long) allnodes;
	klimit = (char *) mem;
}

static unsigned long __init
finish_node(struct device_node *np, unsigned long mem_start,
	    interpret_func *ifunc, int naddrc, int nsizec)
{
	struct device_node *child;
	int *ip;

	np->name = get_property(np, "name", NULL);
	np->type = get_property(np, "device_type", NULL);

	if (!np->name)
		np->name = "<NULL>";
	if (!np->type)
		np->type = "<NULL>";

	/* get the device addresses and interrupts */
	if (ifunc != NULL)
		mem_start = ifunc(np, mem_start, naddrc, nsizec);

	if (use_of_interrupt_tree)
		mem_start = finish_node_interrupts(np, mem_start);

	/* Look for #address-cells and #size-cells properties. */
	ip = (int *) get_property(np, "#address-cells", NULL);
	if (ip != NULL)
		naddrc = *ip;
	ip = (int *) get_property(np, "#size-cells", NULL);
	if (ip != NULL)
		nsizec = *ip;

	if (np->parent == NULL)
		ifunc = interpret_root_props;
	else if (np->type == 0)
		ifunc = NULL;
	else if (!strcmp(np->type, "pci") || !strcmp(np->type, "vci"))
		ifunc = interpret_pci_props;
	else if (!strcmp(np->type, "dbdma"))
		ifunc = interpret_dbdma_props;
	else if (!strcmp(np->type, "mac-io")
		 || ifunc == interpret_macio_props)
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

	/* if we were booted from BootX, convert the full name */
	if (boot_infos
	    && strncmp(np->full_name, "Devices:device-tree", 19) == 0) {
		if (np->full_name[19] == 0) {
			strcpy(np->full_name, "/");
		} else if (np->full_name[19] == ':') {
			char *p = np->full_name + 19;
			np->full_name = p;
			for (; *p; ++p)
				if (*p == ':')
					*p = '/';
		}
	}

	for (child = np->child; child != NULL; child = child->sibling)
		mem_start = finish_node(child, mem_start, ifunc,
					naddrc, nsizec);

	return mem_start;
}

/*
 * Find the interrupt parent of a node.
 */
static struct device_node * __init
intr_parent(struct device_node *p)
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
static int __init
prom_n_intr_cells(struct device_node *np)
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
	printk("prom_n_intr_cells failed for %s\n", np->full_name);
	return 1;
}

/*
 * Map an interrupt from a device up to the platform interrupt
 * descriptor.
 */
static int __init
map_interrupt(unsigned int **irq, struct device_node **ictrler,
	      struct device_node *np, unsigned int *ints, int nintrc)
{
	struct device_node *p, *ipar;
	unsigned int *imap, *imask, *ip;
	int i, imaplen, match;
	int newintrc = 1, newaddrc = 1;
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
			printk("oops, no match in %s int-map for %s\n",
			       p->full_name, np->full_name);
			return 0;
		}
		p = ipar;
		naddrc = newaddrc;
		nintrc = newintrc;
		ints = imap - nintrc;
		reg = ints - naddrc;
	}
	if (p == NULL)
		printk("hmmm, int tree for %s doesn't have ctrler\n",
		       np->full_name);
	*irq = ints;
	*ictrler = p;
	return nintrc;
}

/*
 * New version of finish_node_interrupts.
 */
static unsigned long __init
finish_node_interrupts(struct device_node *np, unsigned long mem_start)
{
	unsigned int *ints;
	int intlen, intrcells;
	int i, j, n, offset;
	unsigned int *irq;
	struct device_node *ic;

	ints = (unsigned int *) get_property(np, "interrupts", &intlen);
	if (ints == NULL)
		return mem_start;
	intrcells = prom_n_intr_cells(np);
	intlen /= intrcells * sizeof(unsigned int);
	np->n_intrs = intlen;
	np->intrs = (struct interrupt_info *) mem_start;
	mem_start += intlen * sizeof(struct interrupt_info);

	for (i = 0; i < intlen; ++i) {
		np->intrs[i].line = 0;
		np->intrs[i].sense = 1;
		n = map_interrupt(&irq, &ic, np, ints, intrcells);
		if (n <= 0)
			continue;
		offset = 0;
		/*
		 * On a CHRP we have an 8259 which is subordinate to
		 * the openpic in the interrupt tree, but we want the
		 * openpic's interrupt numbers offsetted, not the 8259's.
		 * So we apply the offset if the controller is at the
		 * root of the interrupt tree, i.e. has no interrupt-parent.
		 * This doesn't cope with the general case of multiple
		 * cascaded interrupt controllers, but then neither will
		 * irq.c at the moment either.  -- paulus
		 * The G5 triggers that code, I add a machine test. On
		 * those machines, we want to offset interrupts from the
		 * second openpic by 128 -- BenH
		 */
		if (_machine != _MACH_Pmac && num_interrupt_controllers > 1
		    && ic != NULL
		    && get_property(ic, "interrupt-parent", NULL) == NULL)
			offset = 16;
		else if (_machine == _MACH_Pmac && num_interrupt_controllers > 1
			 && ic != NULL && ic->parent != NULL) {
			char *name = get_property(ic->parent, "name", NULL);
			if (name && !strcmp(name, "u3"))
				offset = 128;
		}

		np->intrs[i].line = irq[0] + offset;
		if (n > 1)
			np->intrs[i].sense = irq[1];
		if (n > 2) {
			printk("hmmm, got %d intr cells for %s:", n,
			       np->full_name);
			for (j = 0; j < n; ++j)
				printk(" %d", irq[j]);
			printk("\n");
		}
		ints += intrcells;
	}

	return mem_start;
}

/*
 * When BootX makes a copy of the device tree from the MacOS
 * Name Registry, it is in the format we use but all of the pointers
 * are offsets from the start of the tree.
 * This procedure updates the pointers.
 */
void __init
relocate_nodes(void)
{
	unsigned long base;
	struct device_node *np;
	struct property *pp;

#define ADDBASE(x)	(x = (typeof (x))((x)? ((unsigned long)(x) + base): 0))

	base = (unsigned long) boot_infos + boot_infos->deviceTreeOffset;
	allnodes = (struct device_node *)(base + 4);
	for (np = allnodes; np != 0; np = np->allnext) {
		ADDBASE(np->full_name);
		ADDBASE(np->properties);
		ADDBASE(np->parent);
		ADDBASE(np->child);
		ADDBASE(np->sibling);
		ADDBASE(np->allnext);
		for (pp = np->properties; pp != 0; pp = pp->next) {
			ADDBASE(pp->name);
			ADDBASE(pp->value);
			ADDBASE(pp->next);
		}
	}
}

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

static unsigned long __init
map_addr(struct device_node *np, unsigned long space, unsigned long addr)
{
	int na;
	unsigned int *ranges;
	int rlen = 0;
	unsigned int type;

	type = (space >> 24) & 3;
	if (type == 0)
		return addr;

	while ((np = np->parent) != NULL) {
		if (strcmp(np->type, "pci") != 0)
			continue;
		/* PCI bridge: map the address through the ranges property */
		na = prom_n_addr_cells(np);
		ranges = (unsigned int *) get_property(np, "ranges", &rlen);
		while ((rlen -= (na + 5) * sizeof(unsigned int)) >= 0) {
			if (((ranges[0] >> 24) & 3) == type
			    && ranges[2] <= addr
			    && addr - ranges[2] < ranges[na+4]) {
				/* ok, this matches, translate it */
				addr += ranges[na+2] - ranges[2];
				break;
			}
			ranges += na + 5;
		}
	}
	return addr;
}

static unsigned long __init
interpret_pci_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct address_range *adr;
	struct pci_reg_property *pci_addrs;
	int i, l, *ip;

	pci_addrs = (struct pci_reg_property *)
		get_property(np, "assigned-addresses", &l);
	if (pci_addrs != 0 && l >= sizeof(struct pci_reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct pci_reg_property)) >= 0) {
			adr[i].space = pci_addrs[i].addr.a_hi;
			adr[i].address = map_addr(np, pci_addrs[i].addr.a_hi,
						  pci_addrs[i].addr.a_lo);
			adr[i].size = pci_addrs[i].size_lo;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0 && np->parent)
		ip = (int *) get_property(np->parent, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 1;
		}
	}

	return mem_start;
}

static unsigned long __init
interpret_dbdma_props(struct device_node *np, unsigned long mem_start,
		      int naddrc, int nsizec)
{
	struct reg_property *rp;
	struct address_range *adr;
	unsigned long base_address;
	int i, l, *ip;
	struct device_node *db;

	base_address = 0;
	for (db = np->parent; db != NULL; db = db->parent) {
		if (!strcmp(db->type, "dbdma") && db->n_addrs != 0) {
			base_address = db->addrs[0].address;
			break;
		}
	}

	rp = (struct reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = 2;
			adr[i].address = rp[i].address + base_address;
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 1;
		}
	}

	return mem_start;
}

static unsigned long __init
interpret_macio_props(struct device_node *np, unsigned long mem_start,
		      int naddrc, int nsizec)
{
	struct reg_property *rp;
	struct address_range *adr;
	unsigned long base_address;
	int i, l, *ip;
	struct device_node *db;

	base_address = 0;
	for (db = np->parent; db != NULL; db = db->parent) {
		if (!strcmp(db->type, "mac-io") && db->n_addrs != 0) {
			base_address = db->addrs[0].address;
			break;
		}
	}

	rp = (struct reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = 2;
			adr[i].address = rp[i].address + base_address;
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 1;
		}
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
	}

	return mem_start;
}

static unsigned long __init
interpret_isa_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct isa_reg_property *rp;
	struct address_range *adr;
	int i, l, *ip;

	rp = (struct isa_reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct isa_reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = rp[i].space;
			adr[i].address = rp[i].address
				+ (adr[i].space? 0: _ISA_MEM_BASE);
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / (2 * sizeof(int));
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = *ip++;
		}
	}

	return mem_start;
}

static unsigned long __init
interpret_root_props(struct device_node *np, unsigned long mem_start,
		     int naddrc, int nsizec)
{
	struct address_range *adr;
	int i, l, *ip;
	unsigned int *rp;
	int rpsize = (naddrc + nsizec) * sizeof(unsigned int);

	rp = (unsigned int *) get_property(np, "reg", &l);
	if (rp != 0 && l >= rpsize) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= rpsize) >= 0) {
			adr[i].space = (naddrc >= 2? rp[naddrc-2]: 2);
			adr[i].address = rp[naddrc - 1];
			adr[i].size = rp[naddrc + nsizec - 1];
			++i;
			rp += naddrc + nsizec;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 1;
		}
	}

	return mem_start;
}

/*
 * Work out the sense (active-low level / active-high edge)
 * of each interrupt from the device tree.
 */
void __init
prom_get_irq_senses(unsigned char *senses, int off, int max)
{
	struct device_node *np;
	int i, j;

	/* default to level-triggered */
	memset(senses, 1, max - off);
	if (!use_of_interrupt_tree)
		return;

	for (np = allnodes; np != 0; np = np->allnext) {
		for (j = 0; j < np->n_intrs; j++) {
			i = np->intrs[j].line;
			if (i >= off && i < max) {
				if (np->intrs[j].sense == 1)
					senses[i-off] = (IRQ_SENSE_LEVEL
						| IRQ_POLARITY_NEGATIVE);
				else
					senses[i-off] = (IRQ_SENSE_EDGE
						| IRQ_POLARITY_POSITIVE);
			}
		}
	}
}

/*
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

/*
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

/*
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

/* Checks if the given "compat" string matches one of the strings in
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


/*
 * Indicates whether the root node has a given value in its
 * compatible property.
 */
int
machine_is_compatible(const char *compat)
{
	struct device_node *root;

	root = find_path_device("/");
	if (root == 0)
		return 0;
	return device_is_compatible(root, compat);
}

/*
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

/*
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

/*******
 *
 * New implementation of the OF "find" APIs, return a refcounted
 * object, call of_node_put() when done. Currently, still lacks
 * locking as old implementation, this is beeing done for ppc64.
 *
 * Note that property management will need some locking as well,
 * this isn't dealt with yet
 *
 *******/

/**
 *	of_find_node_by_name - Find a node by it's "name" property
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
	struct device_node *np = from ? from->allnext : allnodes;

	for (; np != 0; np = np->allnext)
		if (np->name != 0 && strcasecmp(np->name, name) == 0)
			break;
	if (from)
		of_node_put(from);
	return of_node_get(np);
}

/**
 *	of_find_node_by_type - Find a node by it's "device_type" property
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
	struct device_node *np = from ? from->allnext : allnodes;

	for (; np != 0; np = np->allnext)
		if (np->type != 0 && strcasecmp(np->type, type) == 0)
			break;
	if (from)
		of_node_put(from);
	return of_node_get(np);
}

/**
 *	of_find_compatible_node - Find a node based on type and one of the
 *                                tokens in it's "compatible" property
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
	struct device_node *np = from ? from->allnext : allnodes;

	for (; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcasecmp(np->type, type) == 0))
			continue;
		if (device_is_compatible(np, compatible))
			break;
	}
	if (from)
		of_node_put(from);
	return of_node_get(np);
}

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

	for (; np != 0; np = np->allnext)
		if (np->full_name != 0 && strcasecmp(np->full_name, path) == 0)
			break;
	return of_node_get(np);
}

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
	return of_node_get(prev ? prev->allnext : allnodes);
}

/**
 *	of_get_parent - Get a node's parent if any
 *	@node:	Node to get parent
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_get_parent(const struct device_node *node)
{
	return node ? of_node_get(node->parent) : NULL;
}

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
	struct device_node *next = prev ? prev->sibling : node->child;

	for (; next != 0; next = next->sibling)
		if (of_node_get(next))
			break;
	if (prev)
		of_node_put(prev);
	return next;
}

/**
 *	of_node_get - Increment refcount of a node
 *	@node:	Node to inc refcount, NULL is supported to
 *		simplify writing of callers
 *
 *	Returns the node itself or NULL if gone. Current implementation
 *	does nothing as we don't yet do dynamic node allocation on ppc32
 */
struct device_node *of_node_get(struct device_node *node)
{
	return node;
}

/**
 *	of_node_put - Decrement refcount of a node
 *	@node:	Node to dec refcount, NULL is supported to
 *		simplify writing of callers
 *
 *	Current implementation does nothing as we don't yet do dynamic node
 *	allocation on ppc32
 */
void  of_node_put(struct device_node *node)
{
}

/*
 * Find the device_node with a given phandle.
 */
static struct device_node * __init
find_phandle(phandle ph)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->node == ph)
			return np;
	return NULL;
}

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
unsigned char *
get_property(struct device_node *np, const char *name, int *lenp)
{
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next)
		if (pp->name != NULL && strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			return pp->value;
		}
	return NULL;
}

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

/* I quickly hacked that one, check against spec ! */
static inline unsigned long
bus_space_to_resource_flags(unsigned int bus_space)
{
	u8 space = (bus_space >> 24) & 0xf;
	if (space == 0)
		space = 0x02;
	if (space == 0x02)
		return IORESOURCE_MEM;
	else if (space == 0x01)
		return IORESOURCE_IO;
	else {
		printk(KERN_WARNING "prom.c: bus_space_to_resource_flags(), space: %x\n",
		    	bus_space);
		return 0;
	}
}

static struct resource*
find_parent_pci_resource(struct pci_dev* pdev, struct address_range *range)
{
	unsigned long mask;
	int i;

	/* Check this one */
	mask = bus_space_to_resource_flags(range->space);
	for (i=0; i<DEVICE_COUNT_RESOURCE; i++) {
		if ((pdev->resource[i].flags & mask) == mask &&
			pdev->resource[i].start <= range->address &&
			pdev->resource[i].end > range->address) {
				if ((range->address + range->size - 1) > pdev->resource[i].end) {
					/* Add better message */
					printk(KERN_WARNING "PCI/OF resource overlap !\n");
					return NULL;
				}
				break;
			}
	}
	if (i == DEVICE_COUNT_RESOURCE)
		return NULL;
	return &pdev->resource[i];
}

/*
 * Request an OF device resource. Currently handles child of PCI devices,
 * or other nodes attached to the root node. Ultimately, put some
 * link to resources in the OF node.
 */
struct resource*
request_OF_resource(struct device_node* node, int index, const char* name_postfix)
{
	struct pci_dev* pcidev;
	u8 pci_bus, pci_devfn;
	unsigned long iomask;
	struct device_node* nd;
	struct resource* parent;
	struct resource *res = NULL;
	int nlen, plen;

	if (index >= node->n_addrs)
		goto fail;

	/* Sanity check on bus space */
	iomask = bus_space_to_resource_flags(node->addrs[index].space);
	if (iomask & IORESOURCE_MEM)
		parent = &iomem_resource;
	else if (iomask & IORESOURCE_IO)
		parent = &ioport_resource;
	else
		goto fail;

	/* Find a PCI parent if any */
	nd = node;
	pcidev = NULL;
	while(nd) {
		if (!pci_device_from_OF_node(nd, &pci_bus, &pci_devfn))
			pcidev = pci_find_slot(pci_bus, pci_devfn);
		if (pcidev) break;
		nd = nd->parent;
	}
	if (pcidev)
		parent = find_parent_pci_resource(pcidev, &node->addrs[index]);
	if (!parent) {
		printk(KERN_WARNING "request_OF_resource(%s), parent not found\n",
			node->name);
		goto fail;
	}

	res = __request_region(parent, node->addrs[index].address, node->addrs[index].size, NULL);
	if (!res)
		goto fail;
	nlen = strlen(node->name);
	plen = name_postfix ? strlen(name_postfix) : 0;
	res->name = (const char *)kmalloc(nlen+plen+1, GFP_KERNEL);
	if (res->name) {
		strcpy((char *)res->name, node->name);
		if (plen)
			strcpy((char *)res->name+nlen, name_postfix);
	}
	return res;
fail:
	return NULL;
}

int
release_OF_resource(struct device_node* node, int index)
{
	struct pci_dev* pcidev;
	u8 pci_bus, pci_devfn;
	unsigned long iomask, start, end;
	struct device_node* nd;
	struct resource* parent;
	struct resource *res = NULL;

	if (index >= node->n_addrs)
		return -EINVAL;

	/* Sanity check on bus space */
	iomask = bus_space_to_resource_flags(node->addrs[index].space);
	if (iomask & IORESOURCE_MEM)
		parent = &iomem_resource;
	else if (iomask & IORESOURCE_IO)
		parent = &ioport_resource;
	else
		return -EINVAL;

	/* Find a PCI parent if any */
	nd = node;
	pcidev = NULL;
	while(nd) {
		if (!pci_device_from_OF_node(nd, &pci_bus, &pci_devfn))
			pcidev = pci_find_slot(pci_bus, pci_devfn);
		if (pcidev) break;
		nd = nd->parent;
	}
	if (pcidev)
		parent = find_parent_pci_resource(pcidev, &node->addrs[index]);
	if (!parent) {
		printk(KERN_WARNING "release_OF_resource(%s), parent not found\n",
			node->name);
		return -ENODEV;
	}

	/* Find us in the parent and its childs */
	res = parent->child;
	start = node->addrs[index].address;
	end = start + node->addrs[index].size - 1;
	while (res) {
		if (res->start == start && res->end == end &&
		    (res->flags & IORESOURCE_BUSY))
		    	break;
		if (res->start <= start && res->end >= end)
			res = res->child;
		else
			res = res->sibling;
	}
	if (!res)
		return -ENODEV;

	if (res->name) {
		kfree(res->name);
		res->name = NULL;
	}
	release_resource(res);
	kfree(res);

	return 0;
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

static DEFINE_SPINLOCK(rtas_lock);

/* this can be called after setup -- Cort */
int
call_rtas(const char *service, int nargs, int nret,
	  unsigned long *outputs, ...)
{
	va_list list;
	int i;
	unsigned long s;
	struct device_node *rtas;
	int *tokp;
	union {
		unsigned long words[16];
		double align;
	} u;

	rtas = find_devices("rtas");
	if (rtas == NULL)
		return -1;
	tokp = (int *) get_property(rtas, service, NULL);
	if (tokp == NULL) {
		printk(KERN_ERR "No RTAS service called %s\n", service);
		return -1;
	}
	u.words[0] = *tokp;
	u.words[1] = nargs;
	u.words[2] = nret;
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i)
		u.words[i+3] = va_arg(list, unsigned long);
	va_end(list);

	/*
	 * RTAS doesn't use floating point.
	 * Or at least, according to the CHRP spec we enter RTAS
	 * with FP disabled, and it doesn't change the FP registers.
	 *  -- paulus.
	 */
	spin_lock_irqsave(&rtas_lock, s);
	enter_rtas((void *)__pa(&u));
	spin_unlock_irqrestore(&rtas_lock, s);

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = u.words[i+nargs+4];
	return u.words[nargs+3];
}
