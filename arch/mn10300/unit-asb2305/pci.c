/* ASB2305 PCI support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Derived from arch/i386/kernel/pci-pc.c
 *	(c) 1999--2000 Martin Mares <mj@suse.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "pci-asb2305.h"

unsigned int pci_probe = 1;

int pcibios_last_bus = -1;
struct pci_bus *pci_root_bus;
struct pci_ops *pci_root_ops;

/*
 * Functions for accessing PCI configuration space
 */

#define CONFIG_CMD(bus, devfn, where) \
	(0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

#define MEM_PAGING_REG	(*(volatile __u32 *) 0xBFFFFFF4)
#define CONFIG_ADDRESS	(*(volatile __u32 *) 0xBFFFFFF8)
#define CONFIG_DATAL(X)	(*(volatile __u32 *) 0xBFFFFFFC)
#define CONFIG_DATAW(X)	(*(volatile __u16 *) (0xBFFFFFFC + ((X) & 2)))
#define CONFIG_DATAB(X)	(*(volatile __u8  *) (0xBFFFFFFC + ((X) & 3)))

#define BRIDGEREGB(X)	(*(volatile __u8  *) (0xBE040000 + (X)))
#define BRIDGEREGW(X)	(*(volatile __u16 *) (0xBE040000 + (X)))
#define BRIDGEREGL(X)	(*(volatile __u32 *) (0xBE040000 + (X)))

static inline int __query(const struct pci_bus *bus, unsigned int devfn)
{
#if 0
	return bus->number == 0 && (devfn == PCI_DEVFN(0, 0));
	return bus->number == 1;
	return bus->number == 0 &&
		(devfn == PCI_DEVFN(2, 0) || devfn == PCI_DEVFN(3, 0));
#endif
	return 1;
}

/*
 * translate Linuxcentric addresses to PCI bus addresses
 */
void pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			     struct resource *res)
{
	if (res->flags & IORESOURCE_IO) {
		region->start = (res->start & 0x00ffffff);
		region->end   = (res->end   & 0x00ffffff);
	}

	if (res->flags & IORESOURCE_MEM) {
		region->start = (res->start & 0x03ffffff) | MEM_PAGING_REG;
		region->end   = (res->end   & 0x03ffffff) | MEM_PAGING_REG;
	}

#if 0
	printk(KERN_DEBUG "RES->BUS: %lx-%lx => %lx-%lx\n",
	       res->start, res->end, region->start, region->end);
#endif
}
EXPORT_SYMBOL(pcibios_resource_to_bus);

/*
 * translate PCI bus addresses to Linuxcentric addresses
 */
void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			     struct pci_bus_region *region)
{
	if (res->flags & IORESOURCE_IO) {
		res->start = (region->start & 0x00ffffff) | 0xbe000000;
		res->end   = (region->end   & 0x00ffffff) | 0xbe000000;
	}

	if (res->flags & IORESOURCE_MEM) {
		res->start = (region->start & 0x03ffffff) | 0xb8000000;
		res->end   = (region->end   & 0x03ffffff) | 0xb8000000;
	}

#if 0
	printk(KERN_INFO "BUS->RES: %lx-%lx => %lx-%lx\n",
	       region->start, region->end, res->start, res->end);
#endif
}
EXPORT_SYMBOL(pcibios_bus_to_resource);

/*
 *
 */
static int pci_ampci_read_config_byte(struct pci_bus *bus, unsigned int devfn,
				      int where, u32 *_value)
{
	u32 rawval, value;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		value = BRIDGEREGB(where);
		__pcbdebug("=> %02hx", &BRIDGEREGL(where), value);
	} else {
		CONFIG_ADDRESS = CONFIG_CMD(bus, devfn, where);
		rawval = CONFIG_ADDRESS;
		value = CONFIG_DATAB(where);
		if (__query(bus, devfn))
			__pcidebug("=> %02hx", bus, devfn, where, value);
	}

	*_value = value;
	return PCIBIOS_SUCCESSFUL;
}

static int pci_ampci_read_config_word(struct pci_bus *bus, unsigned int devfn,
				      int where, u32 *_value)
{
	u32 rawval, value;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		value = BRIDGEREGW(where);
		__pcbdebug("=> %04hx", &BRIDGEREGL(where), value);
	} else {
		CONFIG_ADDRESS = CONFIG_CMD(bus, devfn, where);
		rawval = CONFIG_ADDRESS;
		value = CONFIG_DATAW(where);
		if (__query(bus, devfn))
			__pcidebug("=> %04hx", bus, devfn, where, value);
	}

	*_value = value;
	return PCIBIOS_SUCCESSFUL;
}

static int pci_ampci_read_config_dword(struct pci_bus *bus, unsigned int devfn,
				       int where, u32 *_value)
{
	u32 rawval, value;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		value = BRIDGEREGL(where);
		__pcbdebug("=> %08x", &BRIDGEREGL(where), value);
	} else {
		CONFIG_ADDRESS = CONFIG_CMD(bus, devfn, where);
		rawval = CONFIG_ADDRESS;
		value = CONFIG_DATAL(where);
		if (__query(bus, devfn))
			__pcidebug("=> %08x", bus, devfn, where, value);
	}

	*_value = value;
	return PCIBIOS_SUCCESSFUL;
}

static int pci_ampci_write_config_byte(struct pci_bus *bus, unsigned int devfn,
				       int where, u8 value)
{
	u32 rawval;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		__pcbdebug("<= %02x", &BRIDGEREGB(where), value);
		BRIDGEREGB(where) = value;
	} else {
		if (bus->number == 0 &&
		    (devfn == PCI_DEVFN(2, 0) && devfn == PCI_DEVFN(3, 0))
		    )
			__pcidebug("<= %02x", bus, devfn, where, value);
		CONFIG_ADDRESS = CONFIG_CMD(bus, devfn, where);
		rawval = CONFIG_ADDRESS;
		CONFIG_DATAB(where) = value;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int pci_ampci_write_config_word(struct pci_bus *bus, unsigned int devfn,
				       int where, u16 value)
{
	u32 rawval;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		__pcbdebug("<= %04hx", &BRIDGEREGW(where), value);
		BRIDGEREGW(where) = value;
	} else {
		if (__query(bus, devfn))
			__pcidebug("<= %04hx", bus, devfn, where, value);
		CONFIG_ADDRESS = CONFIG_CMD(bus, devfn, where);
		rawval = CONFIG_ADDRESS;
		CONFIG_DATAW(where) = value;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int pci_ampci_write_config_dword(struct pci_bus *bus, unsigned int devfn,
					int where, u32 value)
{
	u32 rawval;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		__pcbdebug("<= %08x", &BRIDGEREGL(where), value);
		BRIDGEREGL(where) = value;
	} else {
		if (__query(bus, devfn))
			__pcidebug("<= %08x", bus, devfn, where, value);
		CONFIG_ADDRESS = CONFIG_CMD(bus, devfn, where);
		rawval = CONFIG_ADDRESS;
		CONFIG_DATAL(where) = value;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int pci_ampci_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	switch (size) {
	case 1:
		return pci_ampci_read_config_byte(bus, devfn, where, val);
	case 2:
		return pci_ampci_read_config_word(bus, devfn, where, val);
	case 4:
		return pci_ampci_read_config_dword(bus, devfn, where, val);
	default:
		BUG();
		return -EOPNOTSUPP;
	}
}

static int pci_ampci_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	switch (size) {
	case 1:
		return pci_ampci_write_config_byte(bus, devfn, where, val);
	case 2:
		return pci_ampci_write_config_word(bus, devfn, where, val);
	case 4:
		return pci_ampci_write_config_dword(bus, devfn, where, val);
	default:
		BUG();
		return -EOPNOTSUPP;
	}
}

static struct pci_ops pci_direct_ampci = {
	pci_ampci_read_config,
	pci_ampci_write_config,
};

/*
 * Before we decide to use direct hardware access mechanisms, we try to do some
 * trivial checks to ensure it at least _seems_ to be working -- we just test
 * whether bus 00 contains a host bridge (this is similar to checking
 * techniques used in XFree86, but ours should be more reliable since we
 * attempt to make use of direct access hints provided by the PCI BIOS).
 *
 * This should be close to trivial, but it isn't, because there are buggy
 * chipsets (yes, you guessed it, by Intel and Compaq) that have no class ID.
 */
static int __init pci_sanity_check(struct pci_ops *o)
{
	struct pci_bus bus;		/* Fake bus and device */
	u32 x;

	bus.number = 0;

	if ((!o->read(&bus, 0, PCI_CLASS_DEVICE, 2, &x) &&
	     (x == PCI_CLASS_BRIDGE_HOST || x == PCI_CLASS_DISPLAY_VGA)) ||
	    (!o->read(&bus, 0, PCI_VENDOR_ID, 2, &x) &&
	     (x == PCI_VENDOR_ID_INTEL || x == PCI_VENDOR_ID_COMPAQ)))
		return 1;

	printk(KERN_ERROR "PCI: Sanity check failed\n");
	return 0;
}

static int __init pci_check_direct(void)
{
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * Check if access works.
	 */
	if (pci_sanity_check(&pci_direct_ampci)) {
		local_irq_restore(flags);
		printk(KERN_INFO "PCI: Using configuration ampci\n");
		request_mem_region(0xBE040000, 256, "AMPCI bridge");
		request_mem_region(0xBFFFFFF4, 12, "PCI ampci");
		return 0;
	}

	local_irq_restore(flags);
	return -ENODEV;
}

static int __devinit is_valid_resource(struct pci_dev *dev, int idx)
{
	unsigned int i, type_mask = IORESOURCE_IO | IORESOURCE_MEM;
	struct resource *devr = &dev->resource[idx];

	if (dev->bus) {
		for (i = 0; i < PCI_BUS_NUM_RESOURCES; i++) {
			struct resource *busr = dev->bus->resource[i];

			if (!busr || (busr->flags ^ devr->flags) & type_mask)
				continue;

			if (devr->start &&
			    devr->start >= busr->start &&
			    devr->end <= busr->end)
				return 1;
		}
	}

	return 0;
}

static void __devinit pcibios_fixup_device_resources(struct pci_dev *dev)
{
	struct pci_bus_region region;
	int i;
	int limit;

	if (dev->bus->number != 0)
		return;

	limit = (dev->hdr_type == PCI_HEADER_TYPE_NORMAL) ?
		PCI_BRIDGE_RESOURCES : PCI_NUM_RESOURCES;

	for (i = 0; i < limit; i++) {
		if (!dev->resource[i].flags)
			continue;

		region.start = dev->resource[i].start;
		region.end = dev->resource[i].end;
		pcibios_bus_to_resource(dev, &dev->resource[i], &region);
		if (is_valid_resource(dev, i))
			pci_claim_resource(dev, i);
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;

	if (bus->self) {
		pci_read_bridge_bases(bus);
		pcibios_fixup_device_resources(bus->self);
	}

	list_for_each_entry(dev, &bus->devices, bus_list)
		pcibios_fixup_device_resources(dev);
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space, but we still keep BIOS order of cards to be
 * compatible with 2.0.X. This should go away some day.
 */
static int __init pcibios_init(void)
{
	ioport_resource.start	= 0xA0000000;
	ioport_resource.end	= 0xDFFFFFFF;
	iomem_resource.start	= 0xA0000000;
	iomem_resource.end	= 0xDFFFFFFF;

	if (!pci_probe)
		return 0;

	if (pci_check_direct() < 0) {
		printk(KERN_WARNING "PCI: No PCI bus detected\n");
		return 0;
	}

	printk(KERN_INFO "PCI: Probing PCI hardware [mempage %08x]\n",
	       MEM_PAGING_REG);

	{
#if 0
		static struct pci_bus am33_root_bus = {
			.children  = LIST_HEAD_INIT(am33_root_bus.children),
			.devices   = LIST_HEAD_INIT(am33_root_bus.devices),
			.number    = 0,
			.secondary = 0,
			.resource = { &ioport_resource, &iomem_resource },
		};

		am33_root_bus.ops = pci_root_ops;
		list_add_tail(&am33_root_bus.node, &pci_root_buses);

		am33_root_bus.subordinate = pci_do_scan_bus(0);

		pci_root_bus = &am33_root_bus;
#else
		pci_root_bus = pci_scan_bus(0, &pci_direct_ampci, NULL);
#endif
	}

	pcibios_irq_init();
	pcibios_fixup_irqs();
#if 0
	pcibios_resource_survey();
#endif
	return 0;
}

arch_initcall(pcibios_init);

char *__init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;

	} else if (!strncmp(str, "lastbus=", 8)) {
		pcibios_last_bus = simple_strtol(str+8, NULL, 0);
		return NULL;
	}

	return str;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	err = pcibios_enable_resources(dev, mask);
	if (err == 0)
		pcibios_enable_irq(dev);
	return err;
}

/*
 * disable the ethernet chipset
 */
static void __init unit_disable_pcnet(struct pci_bus *bus, struct pci_ops *o)
{
	u32 x;

	bus->number = 0;

	o->read (bus, PCI_DEVFN(2, 0), PCI_COMMAND,		2, &x);
	x |= PCI_COMMAND_MASTER |
		PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_SERR | PCI_COMMAND_PARITY;
	o->write(bus, PCI_DEVFN(2, 0), PCI_COMMAND,		2, x);
	o->read (bus, PCI_DEVFN(2, 0), PCI_COMMAND,		2, &x);
	o->write(bus, PCI_DEVFN(2, 0), PCI_BASE_ADDRESS_0,	4, 0x00030001);
	o->read (bus, PCI_DEVFN(2, 0), PCI_BASE_ADDRESS_0,	4, &x);

#define RDP (*(volatile u32 *) 0xBE030010)
#define RAP (*(volatile u32 *) 0xBE030014)
#define __set_RAP(X) do { RAP = (X); x = RAP; } while (0)
#define __set_RDP(X) do { RDP = (X); x = RDP; } while (0)
#define __get_RDP() ({ RDP & 0xffff; })

	__set_RAP(0);
	__set_RDP(0x0004);	/* CSR0 = STOP */

	__set_RAP(88);		/* check CSR88 indicates an Am79C973 */
	BUG_ON(__get_RDP() != 0x5003);

	for (x = 0; x < 100; x++)
		asm volatile("nop");

	__set_RDP(0x0004);	/* CSR0 = STOP */
}

/*
 * initialise the unit hardware
 */
asmlinkage void __init unit_pci_init(void)
{
	struct pci_bus bus;		/* Fake bus and device */
	struct pci_ops *o = &pci_direct_ampci;
	u32 x;

	set_intr_level(XIRQ1, GxICR_LEVEL_3);

	memset(&bus, 0, sizeof(bus));

	MEM_PAGING_REG = 0xE8000000;

	/* we need to set up the bridge _now_ or we won't be able to access the
	 * PCI config registers
	 */
	BRIDGEREGW(PCI_COMMAND) |=
		PCI_COMMAND_SERR | PCI_COMMAND_PARITY |
		PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER;
	BRIDGEREGW(PCI_STATUS)		= 0xF800;
	BRIDGEREGB(PCI_LATENCY_TIMER)	= 0x10;
	BRIDGEREGL(PCI_BASE_ADDRESS_0)	= 0x80000000;
	BRIDGEREGB(PCI_INTERRUPT_LINE)	= 1;
	BRIDGEREGL(0x48)		= 0x98000000;	/* AMPCI base addr */
	BRIDGEREGB(0x41)		= 0x00;		/* secondary bus
							 * number */
	BRIDGEREGB(0x42)		= 0x01;		/* subordinate bus
							 * number */
	BRIDGEREGB(0x44)		= 0x01;
	BRIDGEREGL(0x50)		= 0x00000001;
	BRIDGEREGL(0x58)		= 0x00001002;
	BRIDGEREGL(0x5C)		= 0x00000011;

	/* we also need to set up the PCI-PCI bridge */
	bus.number = 0;

	/* IO: 0x00000000-0x00020000 */
	o->read (&bus, PCI_DEVFN(3, 0), PCI_COMMAND,		2, &x);
	x |= PCI_COMMAND_MASTER |
		PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_SERR | PCI_COMMAND_PARITY;
	o->write(&bus, PCI_DEVFN(3, 0), PCI_COMMAND,		2, x);

	o->read (&bus, PCI_DEVFN(3, 0), PCI_IO_BASE,		1, &x);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_IO_BASE_UPPER16,	4, &x);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_MEMORY_BASE,	4, &x);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_PREF_MEMORY_BASE,	4, &x);

	o->write(&bus, PCI_DEVFN(3, 0), PCI_IO_BASE,		1, 0x01);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_IO_BASE,		1, &x);
	o->write(&bus, PCI_DEVFN(3, 0), PCI_IO_BASE_UPPER16,	4, 0x00020000);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_IO_BASE_UPPER16,	4, &x);
	o->write(&bus, PCI_DEVFN(3, 0), PCI_MEMORY_BASE,	4, 0xEBB0EA00);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_MEMORY_BASE,	4, &x);
	o->write(&bus, PCI_DEVFN(3, 0), PCI_PREF_MEMORY_BASE,	4, 0xE9F0E800);
	o->read (&bus, PCI_DEVFN(3, 0), PCI_PREF_MEMORY_BASE,	4, &x);

	unit_disable_pcnet(&bus, o);
}
