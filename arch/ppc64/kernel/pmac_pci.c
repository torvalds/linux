/*
 * Support for PCI bridges found on Power Macintoshes.
 * At present the "bandit" and "chaos" bridges are supported.
 * Fortunately you access configuration space in the same
 * way with either bridge.
 *
 * Copyright (C) 2003 Benjamin Herrenschmuidt (benh@kernel.crashing.org)
 * Copyright (C) 1997 Paul Mackerras (paulus@samba.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/iommu.h>

#include "pci.h"
#include "pmac.h"

#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/* XXX Could be per-controller, but I don't think we risk anything by
 * assuming we won't have both UniNorth and Bandit */
static int has_uninorth;
static struct pci_controller *u3_agp;
struct device_node *k2_skiplist[2];

static int __init fixup_one_level_bus_range(struct device_node *node, int higher)
{
	for (; node != 0;node = node->sibling) {
		int * bus_range;
		unsigned int *class_code;
		int len;

		/* For PCI<->PCI bridges or CardBus bridges, we go down */
		class_code = (unsigned int *) get_property(node, "class-code", NULL);
		if (!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		bus_range = (int *) get_property(node, "bus-range", &len);
		if (bus_range != NULL && len > 2 * sizeof(int)) {
			if (bus_range[1] > higher)
				higher = bus_range[1];
		}
		higher = fixup_one_level_bus_range(node->child, higher);
	}
	return higher;
}

/* This routine fixes the "bus-range" property of all bridges in the
 * system since they tend to have their "last" member wrong on macs
 *
 * Note that the bus numbers manipulated here are OF bus numbers, they
 * are not Linux bus numbers.
 */
static void __init fixup_bus_range(struct device_node *bridge)
{
	int * bus_range;
	int len;

	/* Lookup the "bus-range" property for the hose */
	bus_range = (int *) get_property(bridge, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s\n",
			       bridge->full_name);
		return;
	}
	bus_range[1] = fixup_one_level_bus_range(bridge->child, bus_range[1]);
}

/*
 * Apple MacRISC (U3, UniNorth, Bandit, Chaos) PCI controllers.
 *
 * The "Bandit" version is present in all early PCI PowerMacs,
 * and up to the first ones using Grackle. Some machines may
 * have 2 bandit controllers (2 PCI busses).
 *
 * "Chaos" is used in some "Bandit"-type machines as a bridge
 * for the separate display bus. It is accessed the same
 * way as bandit, but cannot be probed for devices. It therefore
 * has its own config access functions.
 *
 * The "UniNorth" version is present in all Core99 machines
 * (iBook, G4, new IMacs, and all the recent Apple machines).
 * It contains 3 controllers in one ASIC.
 *
 * The U3 is the bridge used on G5 machines. It contains on
 * AGP bus which is dealt with the old UniNorth access routines
 * and an HyperTransport bus which uses its own set of access
 * functions.
 */

#define MACRISC_CFA0(devfn, off)	\
	((1 << (unsigned long)PCI_SLOT(dev_fn)) \
	| (((unsigned long)PCI_FUNC(dev_fn)) << 8) \
	| (((unsigned long)(off)) & 0xFCUL))

#define MACRISC_CFA1(bus, devfn, off)	\
	((((unsigned long)(bus)) << 16) \
	|(((unsigned long)(devfn)) << 8) \
	|(((unsigned long)(off)) & 0xFCUL) \
	|1UL)

static unsigned long __pmac macrisc_cfg_access(struct pci_controller* hose,
					       u8 bus, u8 dev_fn, u8 offset)
{
	unsigned int caddr;

	if (bus == hose->first_busno) {
		if (dev_fn < (11 << 3))
			return 0;
		caddr = MACRISC_CFA0(dev_fn, offset);
	} else
		caddr = MACRISC_CFA1(bus, dev_fn, offset);

	/* Uninorth will return garbage if we don't read back the value ! */
	do {
		out_le32(hose->cfg_addr, caddr);
	} while (in_le32(hose->cfg_addr) != caddr);

	offset &= has_uninorth ? 0x07 : 0x03;
	return ((unsigned long)hose->cfg_data) + offset;
}

static int __pmac macrisc_read_config(struct pci_bus *bus, unsigned int devfn,
				      int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	unsigned long addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = macrisc_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8((u8 *)addr);
		break;
	case 2:
		*val = in_le16((u16 *)addr);
		break;
	default:
		*val = in_le32((u32 *)addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int __pmac macrisc_write_config(struct pci_bus *bus, unsigned int devfn,
				       int offset, int len, u32 val)
{
	struct pci_controller *hose;
	unsigned long addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = macrisc_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8((u8 *)addr, val);
		(void) in_8((u8 *)addr);
		break;
	case 2:
		out_le16((u16 *)addr, val);
		(void) in_le16((u16 *)addr);
		break;
	default:
		out_le32((u32 *)addr, val);
		(void) in_le32((u32 *)addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops macrisc_pci_ops =
{
	macrisc_read_config,
	macrisc_write_config
};

/*
 * These versions of U3 HyperTransport config space access ops do not
 * implement self-view of the HT host yet
 */

/*
 * This function deals with some "special cases" devices.
 *
 *  0 -> No special case
 *  1 -> Skip the device but act as if the access was successfull
 *       (return 0xff's on reads, eventually, cache config space
 *       accesses in a later version)
 * -1 -> Hide the device (unsuccessful acess)
 */
static int u3_ht_skip_device(struct pci_controller *hose,
			     struct pci_bus *bus, unsigned int devfn)
{
	struct device_node *busdn, *dn;
	int i;

	/* We only allow config cycles to devices that are in OF device-tree
	 * as we are apparently having some weird things going on with some
	 * revs of K2 on recent G5s
	 */
	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = hose->arch_data;
	for (dn = busdn->child; dn; dn = dn->sibling)
		if (dn->data && PCI_DN(dn)->devfn == devfn)
			break;
	if (dn == NULL)
		return -1;

	/*
	 * When a device in K2 is powered down, we die on config
	 * cycle accesses. Fix that here.
	 */
	for (i=0; i<2; i++)
		if (k2_skiplist[i] == dn)
			return 1;

	return 0;
}

#define U3_HT_CFA0(devfn, off)		\
		((((unsigned long)devfn) << 8) | offset)
#define U3_HT_CFA1(bus, devfn, off)	\
		(U3_HT_CFA0(devfn, off) \
		+ (((unsigned long)bus) << 16) \
		+ 0x01000000UL)

static unsigned long __pmac u3_ht_cfg_access(struct pci_controller* hose,
					     u8 bus, u8 devfn, u8 offset)
{
	if (bus == hose->first_busno) {
		/* For now, we don't self probe U3 HT bridge */
		if (PCI_SLOT(devfn) == 0)
			return 0;
		return ((unsigned long)hose->cfg_data) + U3_HT_CFA0(devfn, offset);
	} else
		return ((unsigned long)hose->cfg_data) + U3_HT_CFA1(bus, devfn, offset);
}

static int __pmac u3_ht_read_config(struct pci_bus *bus, unsigned int devfn,
				    int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	unsigned long addr;


	hose = pci_bus_to_host(bus);      
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (u3_ht_skip_device(hose, bus, devfn)) {
	case 0:
		break;
	case 1:
		switch (len) {
		case 1:
			*val = 0xff; break;
		case 2:
			*val = 0xffff; break;
		default:
			*val = 0xfffffffful; break;
		}
		return PCIBIOS_SUCCESSFUL;
	default:
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8((u8 *)addr);
		break;
	case 2:
		*val = in_le16((u16 *)addr);
		break;
	default:
		*val = in_le32((u32 *)addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int __pmac u3_ht_write_config(struct pci_bus *bus, unsigned int devfn,
				     int offset, int len, u32 val)
{
	struct pci_controller *hose;
	unsigned long addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (u3_ht_skip_device(hose, bus, devfn)) {
	case 0:
		break;
	case 1:
		return PCIBIOS_SUCCESSFUL;
	default:
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8((u8 *)addr, val);
		(void) in_8((u8 *)addr);
		break;
	case 2:
		out_le16((u16 *)addr, val);
		(void) in_le16((u16 *)addr);
		break;
	default:
		out_le32((u32 *)addr, val);
		(void) in_le32((u32 *)addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u3_ht_pci_ops =
{
	u3_ht_read_config,
	u3_ht_write_config
};

static void __init setup_u3_agp(struct pci_controller* hose)
{
	/* On G5, we move AGP up to high bus number so we don't need
	 * to reassign bus numbers for HT. If we ever have P2P bridges
	 * on AGP, we'll have to move pci_assign_all_busses to the
	 * pci_controller structure so we enable it for AGP and not for
	 * HT childs.
	 * We hard code the address because of the different size of
	 * the reg address cell, we shall fix that by killing struct
	 * reg_property and using some accessor functions instead
	 */
	hose->first_busno = 0xf0;
	hose->last_busno = 0xff;
	has_uninorth = 1;
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(0xf0000000 + 0x800000, 0x1000);
	hose->cfg_data = ioremap(0xf0000000 + 0xc00000, 0x1000);

	u3_agp = hose;
}

static void __init setup_u3_ht(struct pci_controller* hose)
{
	struct device_node *np = (struct device_node *)hose->arch_data;
	int i, cur;

	hose->ops = &u3_ht_pci_ops;

	/* We hard code the address because of the different size of
	 * the reg address cell, we shall fix that by killing struct
	 * reg_property and using some accessor functions instead
	 */
	hose->cfg_data = (volatile unsigned char *)ioremap(0xf2000000, 0x02000000);

	/*
	 * /ht node doesn't expose a "ranges" property, so we "remove" regions that
	 * have been allocated to AGP. So far, this version of the code doesn't assign
	 * any of the 0xfxxxxxxx "fine" memory regions to /ht.
	 * We need to fix that sooner or later by either parsing all child "ranges"
	 * properties or figuring out the U3 address space decoding logic and
	 * then read it's configuration register (if any).
	 */
	hose->io_base_phys = 0xf4000000;
	hose->io_base_virt = ioremap(hose->io_base_phys, 0x00400000);
	isa_io_base = pci_io_base = (unsigned long) hose->io_base_virt;
	hose->io_resource.name = np->full_name;
	hose->io_resource.start = 0;
	hose->io_resource.end = 0x003fffff;
	hose->io_resource.flags = IORESOURCE_IO;
	hose->pci_mem_offset = 0;
	hose->first_busno = 0;
	hose->last_busno = 0xef;
	hose->mem_resources[0].name = np->full_name;
	hose->mem_resources[0].start = 0x80000000;
	hose->mem_resources[0].end = 0xefffffff;
	hose->mem_resources[0].flags = IORESOURCE_MEM;

	if (u3_agp == NULL) {
		DBG("U3 has no AGP, using full resource range\n");
		return;
	}

	/* We "remove" the AGP resources from the resources allocated to HT, that
	 * is we create "holes". However, that code does assumptions that so far
	 * happen to be true (cross fingers...), typically that resources in the
	 * AGP node are properly ordered
	 */
	cur = 0;
	for (i=0; i<3; i++) {
		struct resource *res = &u3_agp->mem_resources[i];
		if (res->flags != IORESOURCE_MEM)
			continue;
		/* We don't care about "fine" resources */
		if (res->start >= 0xf0000000)
			continue;
		/* Check if it's just a matter of "shrinking" us in one direction */
		if (hose->mem_resources[cur].start == res->start) {
			DBG("U3/HT: shrink start of %d, %08lx -> %08lx\n",
			    cur, hose->mem_resources[cur].start, res->end + 1);
			hose->mem_resources[cur].start = res->end + 1;
			continue;
		}
		if (hose->mem_resources[cur].end == res->end) {
			DBG("U3/HT: shrink end of %d, %08lx -> %08lx\n",
			    cur, hose->mem_resources[cur].end, res->start - 1);
			hose->mem_resources[cur].end = res->start - 1;
			continue;
		}
		/* No, it's not the case, we need a hole */
		if (cur == 2) {
			/* not enough resources for a hole, we drop part of the range */
			printk(KERN_WARNING "Running out of resources for /ht host !\n");
			hose->mem_resources[cur].end = res->start - 1;
			continue;
		}		
		cur++;
		DBG("U3/HT: hole, %d end at %08lx, %d start at %08lx\n",
		    cur-1, res->start - 1, cur, res->end + 1);
		hose->mem_resources[cur].name = np->full_name;
		hose->mem_resources[cur].flags = IORESOURCE_MEM;
		hose->mem_resources[cur].start = res->end + 1;
		hose->mem_resources[cur].end = hose->mem_resources[cur-1].end;
		hose->mem_resources[cur-1].end = res->start - 1;
	}
}

static void __init pmac_process_bridge_OF_ranges(struct pci_controller *hose,
			   struct device_node *dev, int primary)
{
	static unsigned int static_lc_ranges[2024];
	unsigned int *dt_ranges, *lc_ranges, *ranges, *prev;
	unsigned int size;
	int rlen = 0, orig_rlen;
	int memno = 0;
	struct resource *res;
	int np, na = prom_n_addr_cells(dev);

	np = na + 5;

	/* First we try to merge ranges to fix a problem with some pmacs
	 * that can have more than 3 ranges, fortunately using contiguous
	 * addresses -- BenH
	 */
	dt_ranges = (unsigned int *) get_property(dev, "ranges", &rlen);
	if (!dt_ranges)
		return;
	/*	lc_ranges = alloc_bootmem(rlen);*/
	lc_ranges = static_lc_ranges;
	if (!lc_ranges)
		return; /* what can we do here ? */
	memcpy(lc_ranges, dt_ranges, rlen);
	orig_rlen = rlen;

	/* Let's work on a copy of the "ranges" property instead of damaging
	 * the device-tree image in memory
	 */
	ranges = lc_ranges;
	prev = NULL;
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		if (prev) {
			if (prev[0] == ranges[0] && prev[1] == ranges[1] &&
				(prev[2] + prev[na+4]) == ranges[2] &&
				(prev[na+2] + prev[na+4]) == ranges[na+2]) {
				prev[na+4] += ranges[na+4];
				ranges[0] = 0;
				ranges += np;
				continue;
			}
		}
		prev = ranges;
		ranges += np;
	}

	/*
	 * The ranges property is laid out as an array of elements,
	 * each of which comprises:
	 *   cells 0 - 2:	a PCI address
	 *   cells 3 or 3+4:	a CPU physical address
	 *			(size depending on dev->n_addr_cells)
	 *   cells 4+5 or 5+6:	the size of the range
	 */
	ranges = lc_ranges;
	rlen = orig_rlen;
	while (ranges && (rlen -= np * sizeof(unsigned int)) >= 0) {
		res = NULL;
		size = ranges[na+4];
		switch (ranges[0] >> 24) {
		case 1:		/* I/O space */
			if (ranges[2] != 0)
				break;
			hose->io_base_phys = ranges[na+2];
			/* limit I/O space to 16MB */
			if (size > 0x01000000)
				size = 0x01000000;
			hose->io_base_virt = ioremap(ranges[na+2], size);
			if (primary)
				isa_io_base = (unsigned long) hose->io_base_virt;
			res = &hose->io_resource;
			res->flags = IORESOURCE_IO;
			res->start = ranges[2];
			break;
		case 2:		/* memory space */
			memno = 0;
			if (ranges[1] == 0 && ranges[2] == 0
			    && ranges[na+4] <= (16 << 20)) {
				/* 1st 16MB, i.e. ISA memory area */
#if 0
				if (primary)
					isa_mem_base = ranges[na+2];
#endif
				memno = 1;
			}
			while (memno < 3 && hose->mem_resources[memno].flags)
				++memno;
			if (memno == 0)
				hose->pci_mem_offset = ranges[na+2] - ranges[2];
			if (memno < 3) {
				res = &hose->mem_resources[memno];
				res->flags = IORESOURCE_MEM;
				res->start = ranges[na+2];
			}
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->end = res->start + size - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
		ranges += np;
	}
}

/*
 * We assume that if we have a G3 powermac, we have one bridge called
 * "pci" (a MPC106) and no bandit or chaos bridges, and contrariwise,
 * if we have one or more bandit or chaos bridges, we don't have a MPC106.
 */
static int __init add_bridge(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	char* disp_name;
	int *bus_range;
	int primary = 1;
	struct property *of_prop;

	DBG("Adding PCI host bridge %s\n", dev->full_name);

	bus_range = (int *) get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, assume bus 0\n",
			dev->full_name);
	}

	hose = alloc_bootmem(sizeof(struct pci_controller));
	if (hose == NULL)
		return -ENOMEM;
	pci_setup_pci_controller(hose);

	hose->arch_data = dev;
	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	of_prop = alloc_bootmem(sizeof(struct property) +
				sizeof(hose->global_number));
	if (of_prop) {
		memset(of_prop, 0, sizeof(struct property));
		of_prop->name = "linux,pci-domain";
		of_prop->length = sizeof(hose->global_number);
		of_prop->value = (unsigned char *)&of_prop[1];
		memcpy(of_prop->value, &hose->global_number, sizeof(hose->global_number));
		prom_add_property(dev, of_prop);
	}

	disp_name = NULL;
	if (device_is_compatible(dev, "u3-agp")) {
		setup_u3_agp(hose);
		disp_name = "U3-AGP";
		primary = 0;
	} else if (device_is_compatible(dev, "u3-ht")) {
		setup_u3_ht(hose);
		disp_name = "U3-HT";
		primary = 1;
	}
	printk(KERN_INFO "Found %s PCI host bridge. Firmware bus number: %d->%d\n",
		disp_name, hose->first_busno, hose->last_busno);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pmac_process_bridge_OF_ranges(hose, dev, primary);

	/* Fixup "bus-range" OF property */
	fixup_bus_range(dev);

	return 0;
}

/*
 * We use our own read_irq_line here because PCI_INTERRUPT_PIN is
 * crap on some of Apple ASICs. We unconditionally use the Open Firmware
 * interrupt number as this is always right.
 */
static int pmac_pci_read_irq_line(struct pci_dev *pci_dev)
{
	struct device_node *node;

	node = pci_device_to_OF_node(pci_dev);
	if (node == NULL)
		return -1;
	if (node->n_intrs == 0)
		return -1;
	pci_dev->irq = node->intrs[0].line;
	pci_write_config_byte(pci_dev, PCI_INTERRUPT_LINE, pci_dev->irq);

	return 0;
}

void __init pmac_pcibios_fixup(void)
{
	struct pci_dev *dev = NULL;

	for_each_pci_dev(dev)
		pmac_pci_read_irq_line(dev);
}

static void __init pmac_fixup_phb_resources(void)
{
	struct pci_controller *hose, *tmp;
	
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		unsigned long offset = (unsigned long)hose->io_base_virt - pci_io_base;
		hose->io_resource.start += offset;
		hose->io_resource.end += offset;
		printk(KERN_INFO "PCI Host %d, io start: %lx; io end: %lx\n",
		       hose->global_number,
		       hose->io_resource.start, hose->io_resource.end);
	}
}

void __init pmac_pci_init(void)
{
	struct device_node *np, *root;
	struct device_node *ht = NULL;

	/* Probe root PCI hosts, that is on U3 the AGP host and the
	 * HyperTransport host. That one is actually "kept" around
	 * and actually added last as it's resource management relies
	 * on the AGP resources to have been setup first
	 */
	root = of_find_node_by_path("/");
	if (root == NULL) {
		printk(KERN_CRIT "pmac_find_bridges: can't find root of device tree\n");
		return;
	}
	for (np = NULL; (np = of_get_next_child(root, np)) != NULL;) {
		if (np->name == NULL)
			continue;
		if (strcmp(np->name, "pci") == 0) {
			if (add_bridge(np) == 0)
				of_node_get(np);
		}
		if (strcmp(np->name, "ht") == 0) {
			of_node_get(np);
			ht = np;
		}
	}
	of_node_put(root);

	/* Now setup the HyperTransport host if we found any
	 */
	if (ht && add_bridge(ht) != 0)
		of_node_put(ht);

	/* Fixup the IO resources on our host bridges as the common code
	 * does it only for childs of the host bridges
	 */
	pmac_fixup_phb_resources();

	/* Setup the linkage between OF nodes and PHBs */ 
	pci_devs_phb_init();

	/* Fixup the PCI<->OF mapping for U3 AGP due to bus renumbering. We
	 * assume there is no P2P bridge on the AGP bus, which should be a
	 * safe assumptions hopefully.
	 */
	if (u3_agp) {
		struct device_node *np = u3_agp->arch_data;
		PCI_DN(np)->busno = 0xf0;
		for (np = np->child; np; np = np->sibling)
			PCI_DN(np)->busno = 0xf0;
	}

	pmac_check_ht_link();

	/* Tell pci.c to not use the common resource allocation mecanism */
	pci_probe_only = 1;
	
	/* Allow all IO */
	io_page_mask = -1;
}

/*
 * Disable second function on K2-SATA, it's broken
 * and disable IO BARs on first one
 */
static void fixup_k2_sata(struct pci_dev* dev)
{
	int i;
	u16 cmd;

	if (PCI_FUNC(dev->devfn) > 0) {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		for (i = 0; i < 6; i++) {
			dev->resource[i].start = dev->resource[i].end = 0;
			dev->resource[i].flags = 0;
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4 * i, 0);
		}
	} else {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd &= ~PCI_COMMAND_IO;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		for (i = 0; i < 5; i++) {
			dev->resource[i].start = dev->resource[i].end = 0;
			dev->resource[i].flags = 0;
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4 * i, 0);
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SERVERWORKS, 0x0240, fixup_k2_sata);

