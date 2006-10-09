/*
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *		      IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/iommu.h>
#include <asm/ppc-pci.h>

#include "maple.h"

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

static struct pci_controller *u3_agp, *u3_ht, *u4_pcie;

static int __init fixup_one_level_bus_range(struct device_node *node, int higher)
{
	for (; node != 0;node = node->sibling) {
		const int *bus_range;
		const unsigned int *class_code;
		int len;

		/* For PCI<->PCI bridges or CardBus bridges, we go down */
		class_code = get_property(node, "class-code", NULL);
		if (!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		bus_range = get_property(node, "bus-range", &len);
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
	int *bus_range;
	struct property *prop;
	int len;

	/* Lookup the "bus-range" property for the hose */
	prop = of_find_property(bridge, "bus-range", &len);
	if (prop == NULL  || prop->value == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s\n",
			       bridge->full_name);
		return;
	}
	bus_range = (int *)prop->value;
	bus_range[1] = fixup_one_level_bus_range(bridge->child, bus_range[1]);
}


static unsigned long u3_agp_cfa0(u8 devfn, u8 off)
{
	return (1 << (unsigned long)PCI_SLOT(devfn)) |
		((unsigned long)PCI_FUNC(devfn) << 8) |
		((unsigned long)off & 0xFCUL);
}

static unsigned long u3_agp_cfa1(u8 bus, u8 devfn, u8 off)
{
	return ((unsigned long)bus << 16) |
		((unsigned long)devfn << 8) |
		((unsigned long)off & 0xFCUL) |
		1UL;
}

static volatile void __iomem *u3_agp_cfg_access(struct pci_controller* hose,
				       u8 bus, u8 dev_fn, u8 offset)
{
	unsigned int caddr;

	if (bus == hose->first_busno) {
		if (dev_fn < (11 << 3))
			return NULL;
		caddr = u3_agp_cfa0(dev_fn, offset);
	} else
		caddr = u3_agp_cfa1(bus, dev_fn, offset);

	/* Uninorth will return garbage if we don't read back the value ! */
	do {
		out_le32(hose->cfg_addr, caddr);
	} while (in_le32(hose->cfg_addr) != caddr);

	offset &= 0x07;
	return hose->cfg_data + offset;
}

static int u3_agp_read_config(struct pci_bus *bus, unsigned int devfn,
			      int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

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
		*val = in_8(addr);
		break;
	case 2:
		*val = in_le16(addr);
		break;
	default:
		*val = in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int u3_agp_write_config(struct pci_bus *bus, unsigned int devfn,
			       int offset, int len, u32 val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

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
		out_8(addr, val);
		(void) in_8(addr);
		break;
	case 2:
		out_le16(addr, val);
		(void) in_le16(addr);
		break;
	default:
		out_le32(addr, val);
		(void) in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u3_agp_pci_ops =
{
	u3_agp_read_config,
	u3_agp_write_config
};

static unsigned long u3_ht_cfa0(u8 devfn, u8 off)
{
	return (devfn << 8) | off;
}

static unsigned long u3_ht_cfa1(u8 bus, u8 devfn, u8 off)
{
	return u3_ht_cfa0(devfn, off) + (bus << 16) + 0x01000000UL;
}

static volatile void __iomem *u3_ht_cfg_access(struct pci_controller* hose,
				      u8 bus, u8 devfn, u8 offset)
{
	if (bus == hose->first_busno) {
		if (PCI_SLOT(devfn) == 0)
			return NULL;
		return hose->cfg_data + u3_ht_cfa0(devfn, offset);
	} else
		return hose->cfg_data + u3_ht_cfa1(bus, devfn, offset);
}

static int u3_ht_read_config(struct pci_bus *bus, unsigned int devfn,
			     int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (offset > 0xff)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8(addr);
		break;
	case 2:
		*val = in_le16(addr);
		break;
	default:
		*val = in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int u3_ht_write_config(struct pci_bus *bus, unsigned int devfn,
			      int offset, int len, u32 val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (offset > 0xff)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		(void) in_8(addr);
		break;
	case 2:
		out_le16(addr, val);
		(void) in_le16(addr);
		break;
	default:
		out_le32(addr, val);
		(void) in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u3_ht_pci_ops =
{
	u3_ht_read_config,
	u3_ht_write_config
};

static unsigned int u4_pcie_cfa0(unsigned int devfn, unsigned int off)
{
	return (1 << PCI_SLOT(devfn))	|
	       (PCI_FUNC(devfn) << 8)	|
	       ((off >> 8) << 28) 	|
	       (off & 0xfcu);
}

static unsigned int u4_pcie_cfa1(unsigned int bus, unsigned int devfn,
				 unsigned int off)
{
        return (bus << 16)		|
	       (devfn << 8)		|
	       ((off >> 8) << 28)	|
	       (off & 0xfcu)		| 1u;
}

static volatile void __iomem *u4_pcie_cfg_access(struct pci_controller* hose,
                                        u8 bus, u8 dev_fn, int offset)
{
        unsigned int caddr;

        if (bus == hose->first_busno)
                caddr = u4_pcie_cfa0(dev_fn, offset);
        else
                caddr = u4_pcie_cfa1(bus, dev_fn, offset);

        /* Uninorth will return garbage if we don't read back the value ! */
        do {
                out_le32(hose->cfg_addr, caddr);
        } while (in_le32(hose->cfg_addr) != caddr);

        offset &= 0x03;
        return hose->cfg_data + offset;
}

static int u4_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
                               int offset, int len, u32 *val)
{
        struct pci_controller *hose;
        volatile void __iomem *addr;

        hose = pci_bus_to_host(bus);
        if (hose == NULL)
                return PCIBIOS_DEVICE_NOT_FOUND;
        if (offset >= 0x1000)
                return  PCIBIOS_BAD_REGISTER_NUMBER;
        addr = u4_pcie_cfg_access(hose, bus->number, devfn, offset);
        if (!addr)
                return PCIBIOS_DEVICE_NOT_FOUND;
        /*
         * Note: the caller has already checked that offset is
         * suitably aligned and that len is 1, 2 or 4.
         */
        switch (len) {
        case 1:
                *val = in_8(addr);
                break;
        case 2:
                *val = in_le16(addr);
                break;
        default:
                *val = in_le32(addr);
                break;
        }
        return PCIBIOS_SUCCESSFUL;
}
static int u4_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
                                int offset, int len, u32 val)
{
        struct pci_controller *hose;
        volatile void __iomem *addr;

        hose = pci_bus_to_host(bus);
        if (hose == NULL)
                return PCIBIOS_DEVICE_NOT_FOUND;
        if (offset >= 0x1000)
                return  PCIBIOS_BAD_REGISTER_NUMBER;
        addr = u4_pcie_cfg_access(hose, bus->number, devfn, offset);
        if (!addr)
                return PCIBIOS_DEVICE_NOT_FOUND;
        /*
         * Note: the caller has already checked that offset is
         * suitably aligned and that len is 1, 2 or 4.
         */
        switch (len) {
        case 1:
                out_8(addr, val);
                (void) in_8(addr);
                break;
        case 2:
                out_le16(addr, val);
                (void) in_le16(addr);
                break;
        default:
                out_le32(addr, val);
                (void) in_le32(addr);
                break;
        }
        return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u4_pcie_pci_ops =
{
        u4_pcie_read_config,
        u4_pcie_write_config
};

static void __init setup_u3_agp(struct pci_controller* hose)
{
	/* On G5, we move AGP up to high bus number so we don't need
	 * to reassign bus numbers for HT. If we ever have P2P bridges
	 * on AGP, we'll have to move pci_assign_all_buses to the
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

static void __init setup_u4_pcie(struct pci_controller* hose)
{
        /* We currently only implement the "non-atomic" config space, to
         * be optimised later.
         */
        hose->ops = &u4_pcie_pci_ops;
        hose->cfg_addr = ioremap(0xf0000000 + 0x800000, 0x1000);
        hose->cfg_data = ioremap(0xf0000000 + 0xc00000, 0x1000);

        /* The bus contains a bridge from root -> device, we need to
         * make it visible on bus 0 so that we pick the right type
         * of config cycles. If we didn't, we would have to force all
         * config cycles to be type 1. So we override the "bus-range"
         * property here
         */
        hose->first_busno = 0x00;
        hose->last_busno = 0xff;
        u4_pcie = hose;
}

static void __init setup_u3_ht(struct pci_controller* hose)
{
	hose->ops = &u3_ht_pci_ops;

	/* We hard code the address because of the different size of
	 * the reg address cell, we shall fix that by killing struct
	 * reg_property and using some accessor functions instead
	 */
	hose->cfg_data = ioremap(0xf2000000, 0x02000000);

	hose->first_busno = 0;
	hose->last_busno = 0xef;

	u3_ht = hose;
}

static int __init add_bridge(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	char* disp_name;
	const int *bus_range;
	int primary = 1;

	DBG("Adding PCI host bridge %s\n", dev->full_name);

	bus_range = get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, assume bus 0\n",
		dev->full_name);
	}

	hose = pcibios_alloc_controller(dev);
	if (hose == NULL)
		return -ENOMEM;
	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	disp_name = NULL;
	if (device_is_compatible(dev, "u3-agp")) {
		setup_u3_agp(hose);
		disp_name = "U3-AGP";
		primary = 0;
	} else if (device_is_compatible(dev, "u3-ht")) {
		setup_u3_ht(hose);
		disp_name = "U3-HT";
		primary = 1;
        } else if (device_is_compatible(dev, "u4-pcie")) {
                setup_u4_pcie(hose);
                disp_name = "U4-PCIE";
                primary = 0;
	}
	printk(KERN_INFO "Found %s PCI host bridge. Firmware bus number: %d->%d\n",
		disp_name, hose->first_busno, hose->last_busno);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, primary);

	/* Fixup "bus-range" OF property */
	fixup_bus_range(dev);

	return 0;
}


void __init maple_pcibios_fixup(void)
{
	struct pci_dev *dev = NULL;

	DBG(" -> maple_pcibios_fixup\n");

	for_each_pci_dev(dev) {
		/* Fixup IRQ for PCIe host */
		if (u4_pcie != NULL && dev->bus->number == 0 &&
		    pci_bus_to_host(dev->bus) == u4_pcie) {
			printk(KERN_DEBUG "Fixup U4 PCIe IRQ\n");
			dev->irq = irq_create_mapping(NULL, 1);
			if (dev->irq != NO_IRQ)
				set_irq_type(dev->irq, IRQ_TYPE_LEVEL_LOW);
			continue;
		}

		/* Hide AMD8111 IDE interrupt when in legacy mode so
		 * the driver calls pci_get_legacy_ide_irq()
		 */
		if (dev->vendor == PCI_VENDOR_ID_AMD &&
		    dev->device == PCI_DEVICE_ID_AMD_8111_IDE &&
		    (dev->class & 5) != 5) {
			dev->irq = NO_IRQ;
			continue;
		}

		/* For all others, map the interrupt from the device-tree */
		pci_read_irq_line(dev);
	}

	DBG(" <- maple_pcibios_fixup\n");
}

static void __init maple_fixup_phb_resources(void)
{
	struct pci_controller *hose, *tmp;
	
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		unsigned long offset = (unsigned long)hose->io_base_virt - pci_io_base;

		hose->io_resource.start += offset;
		hose->io_resource.end += offset;

		printk(KERN_INFO "PCI Host %d, io start: %llx; io end: %llx\n",
		       hose->global_number,
		       (unsigned long long)hose->io_resource.start,
		       (unsigned long long)hose->io_resource.end);
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

        /*
         * We need to call pci_setup_phb_io for the HT bridge first
         * so it gets the I/O port numbers starting at 0, and we
         * need to call it for the AGP bridge after that so it gets
         * small positive I/O port numbers.
         */
        if (u3_ht)
                pci_setup_phb_io(u3_ht, 1);
        if (u3_agp)
                pci_setup_phb_io(u3_agp, 0);
        if (u4_pcie)
                pci_setup_phb_io(u4_pcie, 0);

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

	/* Tell pci.c to not change any resource allocations.  */
	pci_probe_only = 1;
}

int maple_pci_get_legacy_ide_irq(struct pci_dev *pdev, int channel)
{
	struct device_node *np;
	unsigned int defirq = channel ? 15 : 14;
	unsigned int irq;

	if (pdev->vendor != PCI_VENDOR_ID_AMD ||
	    pdev->device != PCI_DEVICE_ID_AMD_8111_IDE)
		return defirq;

	np = pci_device_to_OF_node(pdev);
	if (np == NULL) {
		printk("Failed to locate OF node for IDE %s\n",
		       pci_name(pdev));
		return defirq;
	}
	irq = irq_of_parse_and_map(np, channel & 0x1);
	if (irq == NO_IRQ) {
		printk("Failed to map onboard IDE interrupt for channel %d\n",
		       channel);
		return defirq;
	}
	return irq;
}

/* XXX: To remove once all firmwares are ok */
static void fixup_maple_ide(struct pci_dev* dev)
{
	if (!machine_is(maple))
		return;

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
#if 0 /* Enable this to fixup IDE sense/polarity of irqs in IO-APICs */
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
