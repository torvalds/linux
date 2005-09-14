/*
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *		      IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define DEBUG

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
#include <asm/iommu.h>

#include "pci.h"

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

static struct pci_controller *u3_agp, *u3_ht;

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


#define U3_AGP_CFA0(devfn, off)	\
	((1 << (unsigned long)PCI_SLOT(dev_fn)) \
	| (((unsigned long)PCI_FUNC(dev_fn)) << 8) \
	| (((unsigned long)(off)) & 0xFCUL))

#define U3_AGP_CFA1(bus, devfn, off)	\
	((((unsigned long)(bus)) << 16) \
	|(((unsigned long)(devfn)) << 8) \
	|(((unsigned long)(off)) & 0xFCUL) \
	|1UL)

static unsigned long u3_agp_cfg_access(struct pci_controller* hose,
				       u8 bus, u8 dev_fn, u8 offset)
{
	unsigned int caddr;

	if (bus == hose->first_busno) {
		if (dev_fn < (11 << 3))
			return 0;
		caddr = U3_AGP_CFA0(dev_fn, offset);
	} else
		caddr = U3_AGP_CFA1(bus, dev_fn, offset);

	/* Uninorth will return garbage if we don't read back the value ! */
	do {
		out_le32(hose->cfg_addr, caddr);
	} while (in_le32(hose->cfg_addr) != caddr);

	offset &= 0x07;
	return ((unsigned long)hose->cfg_data) + offset;
}

static int u3_agp_read_config(struct pci_bus *bus, unsigned int devfn,
			      int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	unsigned long addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = u3_agp_cfg_access(hose, bus->number, devfn, offset);
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

static int u3_agp_write_config(struct pci_bus *bus, unsigned int devfn,
			       int offset, int len, u32 val)
{
	struct pci_controller *hose;
	unsigned long addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = u3_agp_cfg_access(hose, bus->number, devfn, offset);
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

static struct pci_ops u3_agp_pci_ops =
{
	u3_agp_read_config,
	u3_agp_write_config
};


#define U3_HT_CFA0(devfn, off)		\
		((((unsigned long)devfn) << 8) | offset)
#define U3_HT_CFA1(bus, devfn, off)	\
		(U3_HT_CFA0(devfn, off) \
		+ (((unsigned long)bus) << 16) \
		+ 0x01000000UL)

static unsigned long u3_ht_cfg_access(struct pci_controller* hose,
				      u8 bus, u8 devfn, u8 offset)
{
	if (bus == hose->first_busno) {
		if (PCI_SLOT(devfn) == 0)
			return 0;
		return ((unsigned long)hose->cfg_data) + U3_HT_CFA0(devfn, offset);
	} else
		return ((unsigned long)hose->cfg_data) + U3_HT_CFA1(bus, devfn, offset);
}

static int u3_ht_read_config(struct pci_bus *bus, unsigned int devfn,
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

static int u3_ht_write_config(struct pci_bus *bus, unsigned int devfn,
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
	hose->ops = &u3_agp_pci_ops;
	hose->cfg_addr = ioremap(0xf0000000 + 0x800000, 0x1000);
	hose->cfg_data = ioremap(0xf0000000 + 0xc00000, 0x1000);

	u3_agp = hose;
}

static void __init setup_u3_ht(struct pci_controller* hose)
{
	hose->ops = &u3_ht_pci_ops;

	/* We hard code the address because of the different size of
	 * the reg address cell, we shall fix that by killing struct
	 * reg_property and using some accessor functions instead
	 */
	hose->cfg_data = (volatile unsigned char *)ioremap(0xf2000000, 0x02000000);

	hose->first_busno = 0;
	hose->last_busno = 0xef;

	u3_ht = hose;
}

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
	pci_process_bridge_OF_ranges(hose, dev);
	pci_setup_phb_io(hose, primary);

	/* Fixup "bus-range" OF property */
	fixup_bus_range(dev);

	return 0;
}


void __init maple_pcibios_fixup(void)
{
	struct pci_dev *dev = NULL;

	DBG(" -> maple_pcibios_fixup\n");

	for_each_pci_dev(dev)
		pci_read_irq_line(dev);

	/* Do the mapping of the IO space */
	phbs_remap_io();

	DBG(" <- maple_pcibios_fixup\n");
}

static void __init maple_fixup_phb_resources(void)
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

void __init maple_pci_init(void)
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
		printk(KERN_CRIT "maple_find_bridges: can't find root of device tree\n");
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
	maple_fixup_phb_resources();

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

	/* Tell pci.c to use the common resource allocation mecanism */
	pci_probe_only = 0;
	
	/* Allow all IO */
	io_page_mask = -1;
}

int maple_pci_get_legacy_ide_irq(struct pci_dev *pdev, int channel)
{
	struct device_node *np;
	int irq = channel ? 15 : 14;

	if (pdev->vendor != PCI_VENDOR_ID_AMD ||
	    pdev->device != PCI_DEVICE_ID_AMD_8111_IDE)
		return irq;

	np = pci_device_to_OF_node(pdev);
	if (np == NULL)
		return irq;
	if (np->n_intrs < 2)
		return irq;
	return np->intrs[channel & 0x1].line;
}

/* XXX: To remove once all firmwares are ok */
static void fixup_maple_ide(struct pci_dev* dev)
{
#if 0 /* Enable this to enable IDE port 0 */
	{
		u8 v;

		pci_read_config_byte(dev, 0x40, &v);
		v |= 2;
		pci_write_config_byte(dev, 0x40, v);
	}
#endif
#if 0 /* fix bus master base */
	pci_write_config_dword(dev, 0x20, 0xcc01);
	printk("old ide resource: %lx -> %lx \n",
	       dev->resource[4].start, dev->resource[4].end);
	dev->resource[4].start = 0xcc00;
	dev->resource[4].end = 0xcc10;
#endif
#if 1 /* Enable this to fixup IDE sense/polarity of irqs in IO-APICs */
	{
		struct pci_dev *apicdev;
		u32 v;

		apicdev = pci_get_slot (dev->bus, PCI_DEVFN(5,0));
		if (apicdev == NULL)
			printk("IDE Fixup IRQ: Can't find IO-APIC !\n");
		else {
			pci_write_config_byte(apicdev, 0xf2, 0x10 + 2*14);
			pci_read_config_dword(apicdev, 0xf4, &v);
			v &= ~0x00000022;
			pci_write_config_dword(apicdev, 0xf4, v);
			pci_write_config_byte(apicdev, 0xf2, 0x10 + 2*15);
			pci_read_config_dword(apicdev, 0xf4, &v);
			v &= ~0x00000022;
			pci_write_config_dword(apicdev, 0xf4, v);
			pci_dev_put(apicdev);
		}
	}
#endif
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8111_IDE,
			 fixup_maple_ide);
