/*
 *  linux/arch/arm/mach-versatile/pci.c
 *
 * (C) Copyright Koninklijke Philips Electronics NV 2004. All rights reserved.
 * You can redistribute and/or modify this software under the terms of version 2
 * of the GNU General Public License as published by the Free Software Foundation.
 * THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * Koninklijke Philips Electronics nor its subsidiaries is obligated to provide any support for this software.
 *
 * ARM Versatile PCI driver.
 *
 * 14/04/2005 Initial version, colin.king@philips.com
 *
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/mach/pci.h>

/*
 * these spaces are mapped using the following base registers:
 *
 * Usage Local Bus Memory         Base/Map registers used
 *
 * Mem   50000000 - 5FFFFFFF      LB_BASE0/LB_MAP0,  non prefetch
 * Mem   60000000 - 6FFFFFFF      LB_BASE1/LB_MAP1,  prefetch
 * IO    44000000 - 4FFFFFFF      LB_BASE2/LB_MAP2,  IO
 * Cfg   42000000 - 42FFFFFF	  PCI config
 *
 */
#define __IO_ADDRESS(n) ((void __iomem *)(unsigned long)IO_ADDRESS(n))
#define SYS_PCICTL		__IO_ADDRESS(VERSATILE_SYS_PCICTL)
#define PCI_IMAP0		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0x0)
#define PCI_IMAP1		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0x4)
#define PCI_IMAP2		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0x8)
#define PCI_SMAP0		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0x10)
#define PCI_SMAP1		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0x14)
#define PCI_SMAP2		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0x18)
#define PCI_SELFID		__IO_ADDRESS(VERSATILE_PCI_CORE_BASE+0xc)

#define DEVICE_ID_OFFSET		0x00
#define CSR_OFFSET			0x04
#define CLASS_ID_OFFSET			0x08

#define VP_PCI_DEVICE_ID		0x030010ee
#define VP_PCI_CLASS_ID			0x0b400000

static unsigned long pci_slot_ignore = 0;

static int __init versatile_pci_slot_ignore(char *str)
{
	int retval;
	int slot;

	while ((retval = get_option(&str,&slot))) {
		if ((slot < 0) || (slot > 31)) {
			printk("Illegal slot value: %d\n",slot);
		} else {
			pci_slot_ignore |= (1 << slot);
		}
	}
	return 1;
}

__setup("pci_slot_ignore=", versatile_pci_slot_ignore);


static void __iomem *__pci_addr(struct pci_bus *bus,
				unsigned int devfn, int offset)
{
	unsigned int busnr = bus->number;

	/*
	 * Trap out illegal values
	 */
	if (offset > 255)
		BUG();
	if (busnr > 255)
		BUG();
	if (devfn > 255)
		BUG();

	return VERSATILE_PCI_CFG_VIRT_BASE + ((busnr << 16) |
		(PCI_SLOT(devfn) << 11) | (PCI_FUNC(devfn) << 8) | offset);
}

static int versatile_read_config(struct pci_bus *bus, unsigned int devfn, int where,
				 int size, u32 *val)
{
	void __iomem *addr = __pci_addr(bus, devfn, where & ~3);
	u32 v;
	int slot = PCI_SLOT(devfn);

	if (pci_slot_ignore & (1 << slot)) {
		/* Ignore this slot */
		switch (size) {
		case 1:
			v = 0xff;
			break;
		case 2:
			v = 0xffff;
			break;
		default:
			v = 0xffffffff;
		}
	} else {
		switch (size) {
		case 1:
			v = __raw_readl(addr);
			if (where & 2) v >>= 16;
			if (where & 1) v >>= 8;
 			v &= 0xff;
			break;

		case 2:
			v = __raw_readl(addr);
			if (where & 2) v >>= 16;
 			v &= 0xffff;
			break;

		default:
			v = __raw_readl(addr);
			break;
		}
	}

	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

static int versatile_write_config(struct pci_bus *bus, unsigned int devfn, int where,
				  int size, u32 val)
{
	void __iomem *addr = __pci_addr(bus, devfn, where);
	int slot = PCI_SLOT(devfn);

	if (pci_slot_ignore & (1 << slot)) {
		return PCIBIOS_SUCCESSFUL;
	}

	switch (size) {
	case 1:
		__raw_writeb((u8)val, addr);
		break;

	case 2:
		__raw_writew((u16)val, addr);
		break;

	case 4:
		__raw_writel(val, addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_versatile_ops = {
	.read	= versatile_read_config,
	.write	= versatile_write_config,
};

static struct resource io_mem = {
	.name	= "PCI I/O space",
	.start	= VERSATILE_PCI_MEM_BASE0,
	.end	= VERSATILE_PCI_MEM_BASE0+VERSATILE_PCI_MEM_BASE0_SIZE-1,
	.flags	= IORESOURCE_IO,
};

static struct resource non_mem = {
	.name	= "PCI non-prefetchable",
	.start	= VERSATILE_PCI_MEM_BASE1,
	.end	= VERSATILE_PCI_MEM_BASE1+VERSATILE_PCI_MEM_BASE1_SIZE-1,
	.flags	= IORESOURCE_MEM,
};

static struct resource pre_mem = {
	.name	= "PCI prefetchable",
	.start	= VERSATILE_PCI_MEM_BASE2,
	.end	= VERSATILE_PCI_MEM_BASE2+VERSATILE_PCI_MEM_BASE2_SIZE-1,
	.flags	= IORESOURCE_MEM | IORESOURCE_PREFETCH,
};

static int __init pci_versatile_setup_resources(struct resource **resource)
{
	int ret = 0;

	ret = request_resource(&iomem_resource, &io_mem);
	if (ret) {
		printk(KERN_ERR "PCI: unable to allocate I/O "
		       "memory region (%d)\n", ret);
		goto out;
	}
	ret = request_resource(&iomem_resource, &non_mem);
	if (ret) {
		printk(KERN_ERR "PCI: unable to allocate non-prefetchable "
		       "memory region (%d)\n", ret);
		goto release_io_mem;
	}
	ret = request_resource(&iomem_resource, &pre_mem);
	if (ret) {
		printk(KERN_ERR "PCI: unable to allocate prefetchable "
		       "memory region (%d)\n", ret);
		goto release_non_mem;
	}

	/*
	 * bus->resource[0] is the IO resource for this bus
	 * bus->resource[1] is the mem resource for this bus
	 * bus->resource[2] is the prefetch mem resource for this bus
	 */
	resource[0] = &io_mem;
	resource[1] = &non_mem;
	resource[2] = &pre_mem;

	goto out;

 release_non_mem:
	release_resource(&non_mem);
 release_io_mem:
	release_resource(&io_mem);
 out:
	return ret;
}

int __init pci_versatile_setup(int nr, struct pci_sys_data *sys)
{
	int ret = 0;
        int i;
        int myslot = -1;
	unsigned long val;
	void __iomem *local_pci_cfg_base;

	val = __raw_readl(SYS_PCICTL);
	if (!(val & 1)) {
		printk("Not plugged into PCI backplane!\n");
		ret = -EIO;
		goto out;
	}

	if (nr == 0) {
		sys->mem_offset = 0;
		ret = pci_versatile_setup_resources(sys->resource);
		if (ret < 0) {
			printk("pci_versatile_setup: resources... oops?\n");
			goto out;
		}
	} else {
		printk("pci_versatile_setup: resources... nr == 0??\n");
		goto out;
	}

	/*
	 *  We need to discover the PCI core first to configure itself
	 *  before the main PCI probing is performed
	 */
	for (i=0; i<32; i++)
		if ((__raw_readl(VERSATILE_PCI_VIRT_BASE+(i<<11)+DEVICE_ID_OFFSET) == VP_PCI_DEVICE_ID) &&
		    (__raw_readl(VERSATILE_PCI_VIRT_BASE+(i<<11)+CLASS_ID_OFFSET) == VP_PCI_CLASS_ID)) {
			myslot = i;
			break;
		}

	if (myslot == -1) {
		printk("Cannot find PCI core!\n");
		ret = -EIO;
		goto out;
	}

	printk("PCI core found (slot %d)\n",myslot);

	__raw_writel(myslot, PCI_SELFID);
	local_pci_cfg_base = VERSATILE_PCI_CFG_VIRT_BASE + (myslot << 11);

	val = __raw_readl(local_pci_cfg_base + CSR_OFFSET);
	val |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE;
	__raw_writel(val, local_pci_cfg_base + CSR_OFFSET);

	/*
	 * Configure the PCI inbound memory windows to be 1:1 mapped to SDRAM
	 */
	__raw_writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_0);
	__raw_writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_1);
	__raw_writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_2);

	/*
	 * Do not to map Versatile FPGA PCI device into memory space
	 */
	pci_slot_ignore |= (1 << myslot);
	ret = 1;

 out:
	return ret;
}


struct pci_bus *pci_versatile_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_bus(sys->busnr, &pci_versatile_ops, sys);
}

void __init pci_versatile_preinit(void)
{
	__raw_writel(VERSATILE_PCI_MEM_BASE0 >> 28, PCI_IMAP0);
	__raw_writel(VERSATILE_PCI_MEM_BASE1 >> 28, PCI_IMAP1);
	__raw_writel(VERSATILE_PCI_MEM_BASE2 >> 28, PCI_IMAP2);

	__raw_writel(PHYS_OFFSET >> 28, PCI_SMAP0);
	__raw_writel(PHYS_OFFSET >> 28, PCI_SMAP1);
	__raw_writel(PHYS_OFFSET >> 28, PCI_SMAP2);

	__raw_writel(1, SYS_PCICTL);
}

/*
 * map the specified device/slot/pin to an IRQ.   Different backplanes may need to modify this.
 */
static int __init versatile_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;
	int devslot = PCI_SLOT(dev->devfn);

	/* slot,  pin,	irq
	 *  24     1     27
	 *  25     1     28
	 *  26     1     29
	 *  27     1     30
	 */
	irq = 27 + ((slot + pin - 1) & 3);

	printk("PCI map irq: slot %d, pin %d, devslot %d, irq: %d\n",slot,pin,devslot,irq);

	return irq;
}

static struct hw_pci versatile_pci __initdata = {
	.swizzle		= NULL,
	.map_irq		= versatile_map_irq,
	.nr_controllers		= 1,
	.setup			= pci_versatile_setup,
	.scan			= pci_versatile_scan_bus,
	.preinit		= pci_versatile_preinit,
};

static int __init versatile_pci_init(void)
{
	pci_common_init(&versatile_pci);
	return 0;
}

subsys_initcall(versatile_pci_init);
