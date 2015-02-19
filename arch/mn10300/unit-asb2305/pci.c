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
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include "pci-asb2305.h"

unsigned int pci_probe = 1;

struct pci_ops *pci_root_ops;

/*
 * The accessible PCI window does not cover the entire CPU address space, but
 * there are devices we want to access outside of that window, so we need to
 * insert specific PCI bus resources instead of using the platform-level bus
 * resources directly for the PCI root bus.
 *
 * These are configured and inserted by pcibios_init().
 */
static struct resource pci_ioport_resource = {
	.name	= "PCI IO",
	.start	= 0xbe000000,
	.end	= 0xbe03ffff,
	.flags	= IORESOURCE_IO,
};

static struct resource pci_iomem_resource = {
	.name	= "PCI mem",
	.start	= 0xb8000000,
	.end	= 0xbbffffff,
	.flags	= IORESOURCE_MEM,
};

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
		    (devfn == PCI_DEVFN(2, 0) || devfn == PCI_DEVFN(3, 0))
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
	.read = pci_ampci_read_config,
	.write = pci_ampci_write_config,
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

	printk(KERN_ERR "PCI: Sanity check failed\n");
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
		request_mem_region(0xBC000000, 32 * 1024 * 1024, "PCI SRAM");
		return 0;
	}

	local_irq_restore(flags);
	return -ENODEV;
}

static void pcibios_fixup_device_resources(struct pci_dev *dev)
{
	int idx;

	if (!dev->bus)
		return;

	for (idx = 0; idx < PCI_BRIDGE_RESOURCES; idx++) {
		struct resource *r = &dev->resource[idx];

		if (!r->flags || r->parent || !r->start)
			continue;

		pci_claim_resource(dev, idx);
	}
}

static void pcibios_fixup_bridge_resources(struct pci_dev *dev)
{
	int idx;

	if (!dev->bus)
		return;

	for (idx = PCI_BRIDGE_RESOURCES; idx < PCI_NUM_RESOURCES; idx++) {
		struct resource *r = &dev->resource[idx];

		if (!r->flags || r->parent || !r->start)
			continue;

		pci_claim_bridge_resource(dev, idx);
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;

	if (bus->self) {
		pci_read_bridge_bases(bus);
		pcibios_fixup_bridge_resources(bus->self);
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
	resource_size_t io_offset, mem_offset;
	LIST_HEAD(resources);

	ioport_resource.start	= 0xA0000000;
	ioport_resource.end	= 0xDFFFFFFF;
	iomem_resource.start	= 0xA0000000;
	iomem_resource.end	= 0xDFFFFFFF;

	if (insert_resource(&iomem_resource, &pci_iomem_resource) < 0)
		panic("Unable to insert PCI IOMEM resource\n");
	if (insert_resource(&ioport_resource, &pci_ioport_resource) < 0)
		panic("Unable to insert PCI IOPORT resource\n");

	if (!pci_probe)
		return 0;

	if (pci_check_direct() < 0) {
		printk(KERN_WARNING "PCI: No PCI bus detected\n");
		return 0;
	}

	printk(KERN_INFO "PCI: Probing PCI hardware [mempage %08x]\n",
	       MEM_PAGING_REG);

	io_offset = pci_ioport_resource.start -
	    (pci_ioport_resource.start & 0x00ffffff);
	mem_offset = pci_iomem_resource.start -
	    ((pci_iomem_resource.start & 0x03ffffff) | MEM_PAGING_REG);

	pci_add_resource_offset(&resources, &pci_ioport_resource, io_offset);
	pci_add_resource_offset(&resources, &pci_iomem_resource, mem_offset);
	pci_scan_root_bus(NULL, 0, &pci_direct_ampci, NULL, &resources);

	pcibios_irq_init();
	pcibios_fixup_irqs();
	pcibios_resource_survey();
	return 0;
}

arch_initcall(pcibios_init);

char *__init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}

	return str;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	err = pci_enable_resources(dev, mask);
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

	o->read (bus, PCI_DEVFN(2, 0), PCI_VENDOR_ID,		4, &x);
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

	set_intr_level(XIRQ1, NUM2GxICR_LEVEL(CONFIG_PCI_IRQ_LEVEL));

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
