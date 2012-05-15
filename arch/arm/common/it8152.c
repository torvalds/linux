/*
 * linux/arch/arm/common/it8152.c
 *
 * Copyright Compulab Ltd, 2002-2007
 * Mike Rapoport <mike@compulab.co.il>
 *
 * The DMA bouncing part is taken from arch/arm/mach-ixp4xx/common-pci.c
 * (see this file for respective copyrights)
 *
 * Thanks to Guennadi Liakhovetski <gl@dsa-ac.de> for IRQ enumberation
 * and demux code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/export.h>

#include <asm/mach/pci.h>
#include <asm/hardware/it8152.h>

#define MAX_SLOTS		21

static void it8152_mask_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

       if (irq >= IT8152_LD_IRQ(0)) {
	       __raw_writel((__raw_readl(IT8152_INTC_LDCNIMR) |
			    (1 << (irq - IT8152_LD_IRQ(0)))),
			    IT8152_INTC_LDCNIMR);
       } else if (irq >= IT8152_LP_IRQ(0)) {
	       __raw_writel((__raw_readl(IT8152_INTC_LPCNIMR) |
			    (1 << (irq - IT8152_LP_IRQ(0)))),
			    IT8152_INTC_LPCNIMR);
       } else if (irq >= IT8152_PD_IRQ(0)) {
	       __raw_writel((__raw_readl(IT8152_INTC_PDCNIMR) |
			    (1 << (irq - IT8152_PD_IRQ(0)))),
			    IT8152_INTC_PDCNIMR);
       }
}

static void it8152_unmask_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

       if (irq >= IT8152_LD_IRQ(0)) {
	       __raw_writel((__raw_readl(IT8152_INTC_LDCNIMR) &
			     ~(1 << (irq - IT8152_LD_IRQ(0)))),
			    IT8152_INTC_LDCNIMR);
       } else if (irq >= IT8152_LP_IRQ(0)) {
	       __raw_writel((__raw_readl(IT8152_INTC_LPCNIMR) &
			     ~(1 << (irq - IT8152_LP_IRQ(0)))),
			    IT8152_INTC_LPCNIMR);
       } else if (irq >= IT8152_PD_IRQ(0)) {
	       __raw_writel((__raw_readl(IT8152_INTC_PDCNIMR) &
			     ~(1 << (irq - IT8152_PD_IRQ(0)))),
			    IT8152_INTC_PDCNIMR);
       }
}

static struct irq_chip it8152_irq_chip = {
	.name		= "it8152",
	.irq_ack	= it8152_mask_irq,
	.irq_mask	= it8152_mask_irq,
	.irq_unmask	= it8152_unmask_irq,
};

void it8152_init_irq(void)
{
	int irq;

	__raw_writel((0xffff), IT8152_INTC_PDCNIMR);
	__raw_writel((0), IT8152_INTC_PDCNIRR);
	__raw_writel((0xffff), IT8152_INTC_LPCNIMR);
	__raw_writel((0), IT8152_INTC_LPCNIRR);
	__raw_writel((0xffff), IT8152_INTC_LDCNIMR);
	__raw_writel((0), IT8152_INTC_LDCNIRR);

	for (irq = IT8152_IRQ(0); irq <= IT8152_LAST_IRQ; irq++) {
		irq_set_chip_and_handler(irq, &it8152_irq_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
}

void it8152_irq_demux(unsigned int irq, struct irq_desc *desc)
{
       int bits_pd, bits_lp, bits_ld;
       int i;

       while (1) {
	       /* Read all */
	       bits_pd = __raw_readl(IT8152_INTC_PDCNIRR);
	       bits_lp = __raw_readl(IT8152_INTC_LPCNIRR);
	       bits_ld = __raw_readl(IT8152_INTC_LDCNIRR);

	       /* Ack */
	       __raw_writel((~bits_pd), IT8152_INTC_PDCNIRR);
	       __raw_writel((~bits_lp), IT8152_INTC_LPCNIRR);
	       __raw_writel((~bits_ld), IT8152_INTC_LDCNIRR);

	       if (!(bits_ld | bits_lp | bits_pd)) {
		       /* Re-read to guarantee, that there was a moment of
			  time, when they all three were 0. */
		       bits_pd = __raw_readl(IT8152_INTC_PDCNIRR);
		       bits_lp = __raw_readl(IT8152_INTC_LPCNIRR);
		       bits_ld = __raw_readl(IT8152_INTC_LDCNIRR);
		       if (!(bits_ld | bits_lp | bits_pd))
			       return;
	       }

	       bits_pd &= ((1 << IT8152_PD_IRQ_COUNT) - 1);
	       while (bits_pd) {
		       i = __ffs(bits_pd);
		       generic_handle_irq(IT8152_PD_IRQ(i));
		       bits_pd &= ~(1 << i);
	       }

	       bits_lp &= ((1 << IT8152_LP_IRQ_COUNT) - 1);
	       while (bits_lp) {
		       i = __ffs(bits_lp);
		       generic_handle_irq(IT8152_LP_IRQ(i));
		       bits_lp &= ~(1 << i);
	       }

	       bits_ld &= ((1 << IT8152_LD_IRQ_COUNT) - 1);
	       while (bits_ld) {
		       i = __ffs(bits_ld);
		       generic_handle_irq(IT8152_LD_IRQ(i));
		       bits_ld &= ~(1 << i);
	       }
       }
}

/* mapping for on-chip devices */
int __init it8152_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if ((dev->vendor == PCI_VENDOR_ID_ITE) &&
	    (dev->device == PCI_DEVICE_ID_ITE_8152)) {
		if ((dev->class >> 8) == PCI_CLASS_MULTIMEDIA_AUDIO)
			return IT8152_AUDIO_INT;
		if ((dev->class >> 8) == PCI_CLASS_SERIAL_USB)
			return IT8152_USB_INT;
		if ((dev->class >> 8) == PCI_CLASS_SYSTEM_DMA)
			return IT8152_CDMA_INT;
	}

	return 0;
}

static unsigned long it8152_pci_dev_base_address(struct pci_bus *bus,
						 unsigned int devfn)
{
	unsigned long addr = 0;

	if (bus->number == 0) {
			if (devfn < PCI_DEVFN(MAX_SLOTS, 0))
				addr = (devfn << 8);
	} else
		addr = (bus->number << 16) | (devfn << 8);

	return addr;
}

static int it8152_pci_read_config(struct pci_bus *bus,
				  unsigned int devfn, int where,
				  int size, u32 *value)
{
	unsigned long addr = it8152_pci_dev_base_address(bus, devfn);
	u32 v;
	int shift;

	shift = (where & 3);

	__raw_writel((addr + where), IT8152_PCI_CFG_ADDR);
	v = (__raw_readl(IT8152_PCI_CFG_DATA)  >> (8 * (shift)));

	*value = v;

	return PCIBIOS_SUCCESSFUL;
}

static int it8152_pci_write_config(struct pci_bus *bus,
				   unsigned int devfn, int where,
				   int size, u32 value)
{
	unsigned long addr = it8152_pci_dev_base_address(bus, devfn);
	u32 v, vtemp, mask = 0;
	int shift;

	if (size == 1)
		mask = 0xff;
	if (size == 2)
		mask = 0xffff;

	shift = (where & 3);

	__raw_writel((addr + where), IT8152_PCI_CFG_ADDR);
	vtemp = __raw_readl(IT8152_PCI_CFG_DATA);

	if (mask)
		vtemp &= ~(mask << (8 * shift));
	else
		vtemp = 0;

	v = (value << (8 * shift));
	__raw_writel((addr + where), IT8152_PCI_CFG_ADDR);
	__raw_writel((v | vtemp), IT8152_PCI_CFG_DATA);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops it8152_ops = {
	.read = it8152_pci_read_config,
	.write = it8152_pci_write_config,
};

static struct resource it8152_io = {
	.name	= "IT8152 PCI I/O region",
	.flags	= IORESOURCE_IO,
};

static struct resource it8152_mem = {
	.name	= "IT8152 PCI memory region",
	.start	= 0x10000000,
	.end	= 0x13e00000,
	.flags	= IORESOURCE_MEM,
};

/*
 * The following functions are needed for DMA bouncing.
 * ITE8152 chip can address up to 64MByte, so all the devices
 * connected to ITE8152 (PCI and USB) should have limited DMA window
 */
static int it8152_needs_bounce(struct device *dev, dma_addr_t dma_addr, size_t size)
{
	dev_dbg(dev, "%s: dma_addr %08x, size %08x\n",
		__func__, dma_addr, size);
	return (dma_addr + size - PHYS_OFFSET) >= SZ_64M;
}

/*
 * Setup DMA mask to 64MB on devices connected to ITE8152. Ignore all
 * other devices.
 */
static int it8152_pci_platform_notify(struct device *dev)
{
	if (dev->bus == &pci_bus_type) {
		if (dev->dma_mask)
			*dev->dma_mask = (SZ_64M - 1) | PHYS_OFFSET;
		dev->coherent_dma_mask = (SZ_64M - 1) | PHYS_OFFSET;
		dmabounce_register_dev(dev, 2048, 4096, it8152_needs_bounce);
	}
	return 0;
}

static int it8152_pci_platform_notify_remove(struct device *dev)
{
	if (dev->bus == &pci_bus_type)
		dmabounce_unregister_dev(dev);

	return 0;
}

int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	if (mask >= PHYS_OFFSET + SZ_64M - 1)
		return 0;

	return -EIO;
}

int __init it8152_pci_setup(int nr, struct pci_sys_data *sys)
{
	it8152_io.start = IT8152_IO_BASE + 0x12000;
	it8152_io.end	= IT8152_IO_BASE + 0x12000 + 0x100000;

	sys->mem_offset = 0x10000000;
	sys->io_offset  = IT8152_IO_BASE;

	if (request_resource(&ioport_resource, &it8152_io)) {
		printk(KERN_ERR "PCI: unable to allocate IO region\n");
		goto err0;
	}
	if (request_resource(&iomem_resource, &it8152_mem)) {
		printk(KERN_ERR "PCI: unable to allocate memory region\n");
		goto err1;
	}

	pci_add_resource_offset(&sys->resources, &it8152_io, sys->io_offset);
	pci_add_resource_offset(&sys->resources, &it8152_mem, sys->mem_offset);

	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR "PCI: Can't use platform_notify\n");
		goto err2;
	}

	platform_notify = it8152_pci_platform_notify;
	platform_notify_remove = it8152_pci_platform_notify_remove;

	return 1;

err2:
	release_resource(&it8152_io);
err1:
	release_resource(&it8152_mem);
err0:
	return -EBUSY;
}

/* ITE bridge requires setting latency timer to avoid early bus access
   termination by PCI bus master devices
*/
void pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;

	/* no need to update on-chip OHCI controller */
	if ((dev->vendor == PCI_VENDOR_ID_ITE) &&
	    (dev->device == PCI_DEVICE_ID_ITE_8152) &&
	    ((dev->class >> 8) == PCI_CLASS_SERIAL_USB))
		return;

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat < 16)
		lat = (64 <= pcibios_max_latency) ? 64 : pcibios_max_latency;
	else if (lat > pcibios_max_latency)
		lat = pcibios_max_latency;
	else
		return;
	printk(KERN_DEBUG "PCI: Setting latency timer of device %s to %d\n",
	       pci_name(dev), lat);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}


struct pci_bus * __init it8152_pci_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_root_bus(NULL, nr, &it8152_ops, sys, &sys->resources);
}

EXPORT_SYMBOL(dma_set_coherent_mask);
