/* pci-vdk.c: MB93090-MB00 (VDK) PCI support
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/mb-regs.h>
#include <asm/mb86943a.h>
#include "pci-frv.h"

unsigned int __nongpreldata pci_probe = 1;

int  __nongpreldata pcibios_last_bus = -1;
struct pci_bus *__nongpreldata pci_root_bus;
struct pci_ops *__nongpreldata pci_root_ops;

/*
 * The accessible PCI window does not cover the entire CPU address space, but
 * there are devices we want to access outside of that window, so we need to
 * insert specific PCI bus resources instead of using the platform-level bus
 * resources directly for the PCI root bus.
 *
 * These are configured and inserted by pcibios_init() and are attached to the
 * root bus by pcibios_fixup_bus().
 */
static struct resource pci_ioport_resource = {
	.name	= "PCI IO",
	.start	= 0,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};

static struct resource pci_iomem_resource = {
	.name	= "PCI mem",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};

/*
 * Functions for accessing PCI configuration space
 */

#define CONFIG_CMD(bus, dev, where) \
	(0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

#define __set_PciCfgAddr(A) writel((A), (volatile void __iomem *) __region_CS1 + 0x80)

#define __get_PciCfgDataB(A) readb((volatile void __iomem *) __region_CS1 + 0x88 + ((A) & 3))
#define __get_PciCfgDataW(A) readw((volatile void __iomem *) __region_CS1 + 0x88 + ((A) & 2))
#define __get_PciCfgDataL(A) readl((volatile void __iomem *) __region_CS1 + 0x88)

#define __set_PciCfgDataB(A,V) \
	writeb((V), (volatile void __iomem *) __region_CS1 + 0x88 + (3 - ((A) & 3)))

#define __set_PciCfgDataW(A,V) \
	writew((V), (volatile void __iomem *) __region_CS1 + 0x88 + (2 - ((A) & 2)))

#define __set_PciCfgDataL(A,V) \
	writel((V), (volatile void __iomem *) __region_CS1 + 0x88)

#define __get_PciBridgeDataB(A) readb((volatile void __iomem *) __region_CS1 + 0x800 + (A))
#define __get_PciBridgeDataW(A) readw((volatile void __iomem *) __region_CS1 + 0x800 + (A))
#define __get_PciBridgeDataL(A) readl((volatile void __iomem *) __region_CS1 + 0x800 + (A))

#define __set_PciBridgeDataB(A,V) writeb((V), (volatile void __iomem *) __region_CS1 + 0x800 + (A))
#define __set_PciBridgeDataW(A,V) writew((V), (volatile void __iomem *) __region_CS1 + 0x800 + (A))
#define __set_PciBridgeDataL(A,V) writel((V), (volatile void __iomem *) __region_CS1 + 0x800 + (A))

static inline int __query(const struct pci_dev *dev)
{
//	return dev->bus->number==0 && (dev->devfn==PCI_DEVFN(0,0));
//	return dev->bus->number==1;
//	return dev->bus->number==0 &&
//		(dev->devfn==PCI_DEVFN(2,0) || dev->devfn==PCI_DEVFN(3,0));
	return 0;
}

/*****************************************************************************/
/*
 *
 */
static int pci_frv_read_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
			       u32 *val)
{
	u32 _value;

	if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
		_value = __get_PciBridgeDataL(where & ~3);
	}
	else {
		__set_PciCfgAddr(CONFIG_CMD(bus, devfn, where));
		_value = __get_PciCfgDataL(where & ~3);
	}

	switch (size) {
	case 1:
		_value = _value >> ((where & 3) * 8);
		break;

	case 2:
		_value = _value >> ((where & 2) * 8);
		break;

	case 4:
		break;

	default:
		BUG();
	}

	*val = _value;
	return PCIBIOS_SUCCESSFUL;
}

static int pci_frv_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
				u32 value)
{
	switch (size) {
	case 1:
		if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
			__set_PciBridgeDataB(where, value);
		}
		else {
			__set_PciCfgAddr(CONFIG_CMD(bus, devfn, where));
			__set_PciCfgDataB(where, value);
		}
		break;

	case 2:
		if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
			__set_PciBridgeDataW(where, value);
		}
		else {
			__set_PciCfgAddr(CONFIG_CMD(bus, devfn, where));
			__set_PciCfgDataW(where, value);
		}
		break;

	case 4:
		if (bus->number == 0 && devfn == PCI_DEVFN(0, 0)) {
			__set_PciBridgeDataL(where, value);
		}
		else {
			__set_PciCfgAddr(CONFIG_CMD(bus, devfn, where));
			__set_PciCfgDataL(where, value);
		}
		break;

	default:
		BUG();
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_direct_frv = {
	pci_frv_read_config,
	pci_frv_write_config,
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
	u32 id;

	bus.number	= 0;

	if (o->read(&bus, 0, PCI_VENDOR_ID, 4, &id) == PCIBIOS_SUCCESSFUL) {
		printk("PCI: VDK Bridge device:vendor: %08x\n", id);
		if (id == 0x200e10cf)
			return 1;
	}

	printk("PCI: VDK Bridge: Sanity check failed\n");
	return 0;
}

static struct pci_ops * __init pci_check_direct(void)
{
	unsigned long flags;

	local_irq_save(flags);

	/* check if access works */
	if (pci_sanity_check(&pci_direct_frv)) {
		local_irq_restore(flags);
		printk("PCI: Using configuration frv\n");
//		request_mem_region(0xBE040000, 256, "FRV bridge");
//		request_mem_region(0xBFFFFFF4, 12, "PCI frv");
		return &pci_direct_frv;
	}

	local_irq_restore(flags);
	return NULL;
}

/*
 * Discover remaining PCI buses in case there are peer host bridges.
 * We use the number of last PCI bus provided by the PCI BIOS.
 */
static void __init pcibios_fixup_peer_bridges(void)
{
	struct pci_bus bus;
	struct pci_dev dev;
	int n;
	u16 l;

	if (pcibios_last_bus <= 0 || pcibios_last_bus >= 0xff)
		return;
	printk("PCI: Peer bridge fixup\n");
	for (n=0; n <= pcibios_last_bus; n++) {
		if (pci_find_bus(0, n))
			continue;
		bus.number = n;
		bus.ops = pci_root_ops;
		dev.bus = &bus;
		for(dev.devfn=0; dev.devfn<256; dev.devfn += 8)
			if (!pci_read_config_word(&dev, PCI_VENDOR_ID, &l) &&
			    l != 0x0000 && l != 0xffff) {
				printk("Found device at %02x:%02x [%04x]\n", n, dev.devfn, l);
				printk("PCI: Discovered peer bus %02x\n", n);
				pci_scan_bus(n, pci_root_ops, NULL);
				break;
			}
	}
}

/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 */

static void __init pci_fixup_umc_ide(struct pci_dev *d)
{
	/*
	 * UM8886BF IDE controller sets region type bits incorrectly,
	 * therefore they look like memory despite of them being I/O.
	 */
	int i;

	printk("PCI: Fixing base address flags for device %s\n", pci_name(d));
	for(i=0; i<4; i++)
		d->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
}

static void __devinit pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	printk("PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}

static void __devinit pci_fixup_ide_trash(struct pci_dev *d)
{
	int i;

	/*
	 * There exist PCI IDE controllers which have utter garbage
	 * in first four base registers. Ignore that.
	 */
	printk("PCI: IDE base address trash cleared for %s\n", pci_name(d));
	for(i=0; i<4; i++)
		d->resource[i].start = d->resource[i].end = d->resource[i].flags = 0;
}

static void __devinit  pci_fixup_latency(struct pci_dev *d)
{
	/*
	 *  SiS 5597 and 5598 chipsets require latency timer set to
	 *  at most 32 to avoid lockups.
	 */
	DBG("PCI: Setting max latency to 32\n");
	pcibios_max_latency = 32;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_UMC, PCI_DEVICE_ID_UMC_UM8886BF, pci_fixup_umc_ide);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5513, pci_fixup_ide_trash);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5597, pci_fixup_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5598, pci_fixup_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pci_fixup_ide_bases);

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
#if 0
	printk("### PCIBIOS_FIXUP_BUS(%d)\n",bus->number);
#endif

	pci_read_bridge_bases(bus);

	if (bus->number == 0) {
		struct list_head *ln;
		struct pci_dev *dev;
		for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
			dev = pci_dev_b(ln);
			if (dev->devfn == 0) {
				dev->resource[0].start = 0;
				dev->resource[0].end = 0;
			}
		}
	}
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space, but we still keep BIOS order of cards to be
 * compatible with 2.0.X. This should go away some day.
 */

int __init pcibios_init(void)
{
	struct pci_ops *dir = NULL;
	LIST_HEAD(resources);

	if (!mb93090_mb00_detected)
		return -ENXIO;

	__reg_MB86943_sl_ctl |= MB86943_SL_CTL_DRCT_MASTER_SWAP | MB86943_SL_CTL_DRCT_SLAVE_SWAP;

	__reg_MB86943_ecs_base(1)	= ((__region_CS2 + 0x01000000) >> 9) | 0x08000000;
	__reg_MB86943_ecs_base(2)	= ((__region_CS2 + 0x00000000) >> 9) | 0x08000000;

	*(volatile uint32_t *) (__region_CS1 + 0x848) = 0xe0000000;
	*(volatile uint32_t *) (__region_CS1 + 0x8b8) = 0x00000000;

	__reg_MB86943_sl_pci_io_base	= (__region_CS2 + 0x04000000) >> 9;
	__reg_MB86943_sl_pci_mem_base	= (__region_CS2 + 0x08000000) >> 9;
	__reg_MB86943_pci_sl_io_base	= __region_CS2 + 0x04000000;
	__reg_MB86943_pci_sl_mem_base	= __region_CS2 + 0x08000000;
	mb();

	/* enable PCI arbitration */
	__reg_MB86943_pci_arbiter	= MB86943_PCIARB_EN;

	pci_ioport_resource.start	= (__reg_MB86943_sl_pci_io_base << 9) & 0xfffffc00;
	pci_ioport_resource.end		= (__reg_MB86943_sl_pci_io_range << 9) | 0x3ff;
	pci_ioport_resource.end		+= pci_ioport_resource.start;

	printk("PCI IO window:  %08llx-%08llx\n",
	       (unsigned long long) pci_ioport_resource.start,
	       (unsigned long long) pci_ioport_resource.end);

	pci_iomem_resource.start	= (__reg_MB86943_sl_pci_mem_base << 9) & 0xfffffc00;
	pci_iomem_resource.end		= (__reg_MB86943_sl_pci_mem_range << 9) | 0x3ff;
	pci_iomem_resource.end		+= pci_iomem_resource.start;

	/* Reserve somewhere to write to flush posted writes.  This is used by
	 * __flush_PCI_writes() from asm/io.h to force the write FIFO in the
	 * CPU-PCI bridge to flush as this doesn't happen automatically when a
	 * read is performed on the MB93090 development kit motherboard.
	 */
	pci_iomem_resource.start	+= 0x400;

	printk("PCI MEM window: %08llx-%08llx\n",
	       (unsigned long long) pci_iomem_resource.start,
	       (unsigned long long) pci_iomem_resource.end);
	printk("PCI DMA memory: %08lx-%08lx\n",
	       dma_coherent_mem_start, dma_coherent_mem_end);

	if (insert_resource(&iomem_resource, &pci_iomem_resource) < 0)
		panic("Unable to insert PCI IOMEM resource\n");
	if (insert_resource(&ioport_resource, &pci_ioport_resource) < 0)
		panic("Unable to insert PCI IOPORT resource\n");

	if (!pci_probe)
		return -ENXIO;

	dir = pci_check_direct();
	if (dir)
		pci_root_ops = dir;
	else {
		printk("PCI: No PCI bus detected\n");
		return -ENXIO;
	}

	printk("PCI: Probing PCI hardware\n");
	pci_add_resource(&resources, &pci_ioport_resource);
	pci_add_resource(&resources, &pci_iomem_resource);
	pci_root_bus = pci_scan_root_bus(NULL, 0, pci_root_ops, NULL,
					 &resources);

	pcibios_irq_init();
	pcibios_fixup_peer_bridges();
	pcibios_fixup_irqs();
	pcibios_resource_survey();

	return 0;
}

arch_initcall(pcibios_init);

char * __init pcibios_setup(char *str)
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

	if ((err = pci_enable_resources(dev, mask)) < 0)
		return err;
	if (!dev->msi_enabled)
		pcibios_enable_irq(dev);
	return 0;
}
