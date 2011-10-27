/*
 * linux/arch/unicore32/kernel/pci.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  PCI bios-type initialisation for PCI machines
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/io.h>

static int debug_pci;
static int use_firmware;

#define CONFIG_CMD(bus, devfn, where)	\
	(0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

static int
puv3_read_config(struct pci_bus *bus, unsigned int devfn, int where,
			int size, u32 *value)
{
	writel(CONFIG_CMD(bus, devfn, where), PCICFG_ADDR);
	switch (size) {
	case 1:
		*value = (readl(PCICFG_DATA) >> ((where & 3) * 8)) & 0xFF;
		break;
	case 2:
		*value = (readl(PCICFG_DATA) >> ((where & 2) * 8)) & 0xFFFF;
		break;
	case 4:
		*value = readl(PCICFG_DATA);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
puv3_write_config(struct pci_bus *bus, unsigned int devfn, int where,
			int size, u32 value)
{
	writel(CONFIG_CMD(bus, devfn, where), PCICFG_ADDR);
	switch (size) {
	case 1:
		writel((readl(PCICFG_DATA) & ~FMASK(8, (where&3)*8))
			| FIELD(value, 8, (where&3)*8), PCICFG_DATA);
		break;
	case 2:
		writel((readl(PCICFG_DATA) & ~FMASK(16, (where&2)*8))
			| FIELD(value, 16, (where&2)*8), PCICFG_DATA);
		break;
	case 4:
		writel(value, PCICFG_DATA);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops pci_puv3_ops = {
	.read  = puv3_read_config,
	.write = puv3_write_config,
};

void pci_puv3_preinit(void)
{
	printk(KERN_DEBUG "PCI: PKUnity PCI Controller Initializing ...\n");
	/* config PCI bridge base */
	writel(io_v2p(PKUNITY_PCIBRI_BASE), PCICFG_BRIBASE);

	writel(0, PCIBRI_AHBCTL0);
	writel(io_v2p(PKUNITY_PCIBRI_BASE) | PCIBRI_BARx_MEM, PCIBRI_AHBBAR0);
	writel(0xFFFF0000, PCIBRI_AHBAMR0);
	writel(0, PCIBRI_AHBTAR0);

	writel(PCIBRI_CTLx_AT, PCIBRI_AHBCTL1);
	writel(io_v2p(PKUNITY_PCILIO_BASE) | PCIBRI_BARx_IO, PCIBRI_AHBBAR1);
	writel(0xFFFF0000, PCIBRI_AHBAMR1);
	writel(0x00000000, PCIBRI_AHBTAR1);

	writel(PCIBRI_CTLx_PREF, PCIBRI_AHBCTL2);
	writel(io_v2p(PKUNITY_PCIMEM_BASE) | PCIBRI_BARx_MEM, PCIBRI_AHBBAR2);
	writel(0xF8000000, PCIBRI_AHBAMR2);
	writel(0, PCIBRI_AHBTAR2);

	writel(io_v2p(PKUNITY_PCIAHB_BASE) | PCIBRI_BARx_MEM, PCIBRI_BAR1);

	writel(PCIBRI_CTLx_AT | PCIBRI_CTLx_PREF, PCIBRI_PCICTL0);
	writel(io_v2p(PKUNITY_PCIAHB_BASE) | PCIBRI_BARx_MEM, PCIBRI_PCIBAR0);
	writel(0xF8000000, PCIBRI_PCIAMR0);
	writel(PKUNITY_SDRAM_BASE, PCIBRI_PCITAR0);

	writel(readl(PCIBRI_CMD) | PCIBRI_CMD_IO | PCIBRI_CMD_MEM, PCIBRI_CMD);
}

static int __init pci_puv3_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->bus->number == 0) {
#ifdef CONFIG_ARCH_FPGA /* 4 pci slots */
		if      (dev->devfn == 0x00)
			return IRQ_PCIINTA;
		else if (dev->devfn == 0x08)
			return IRQ_PCIINTB;
		else if (dev->devfn == 0x10)
			return IRQ_PCIINTC;
		else if (dev->devfn == 0x18)
			return IRQ_PCIINTD;
#endif
#ifdef CONFIG_PUV3_DB0913 /* 3 pci slots */
		if      (dev->devfn == 0x30)
			return IRQ_PCIINTB;
		else if (dev->devfn == 0x60)
			return IRQ_PCIINTC;
		else if (dev->devfn == 0x58)
			return IRQ_PCIINTD;
#endif
#if	defined(CONFIG_PUV3_NB0916) || defined(CONFIG_PUV3_SMW0919)
		/* only support 2 pci devices */
		if      (dev->devfn == 0x00)
			return IRQ_PCIINTC; /* sata */
#endif
	}
	return -1;
}

/*
 * Only first 128MB of memory can be accessed via PCI.
 * We use GFP_DMA to allocate safe buffers to do map/unmap.
 * This is really ugly and we need a better way of specifying
 * DMA-capable regions of memory.
 */
void __init puv3_pci_adjust_zones(unsigned long *zone_size,
	unsigned long *zhole_size)
{
	unsigned int sz = SZ_128M >> PAGE_SHIFT;

	/*
	 * Only adjust if > 128M on current system
	 */
	if (zone_size[0] <= sz)
		return;

	zone_size[1] = zone_size[0] - sz;
	zone_size[0] = sz;
	zhole_size[1] = zhole_size[0];
	zhole_size[0] = 0;
}

void __devinit pcibios_update_irq(struct pci_dev *dev, int irq)
{
	if (debug_pci)
		printk(KERN_DEBUG "PCI: Assigning IRQ %02d to %s\n",
				irq, pci_name(dev));
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

/*
 * If the bus contains any of these devices, then we must not turn on
 * parity checking of any kind.
 */
static inline int pdev_bad_for_parity(struct pci_dev *dev)
{
	return 0;
}

/*
 * pcibios_fixup_bus - Called after each bus is probed,
 * but before its children are examined.
 */
void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	u16 features = PCI_COMMAND_SERR
		| PCI_COMMAND_PARITY
		| PCI_COMMAND_FAST_BACK;

	bus->resource[0] = &ioport_resource;
	bus->resource[1] = &iomem_resource;

	/*
	 * Walk the devices on this bus, working out what we can
	 * and can't support.
	 */
	list_for_each_entry(dev, &bus->devices, bus_list) {
		u16 status;

		pci_read_config_word(dev, PCI_STATUS, &status);

		/*
		 * If any device on this bus does not support fast back
		 * to back transfers, then the bus as a whole is not able
		 * to support them.  Having fast back to back transfers
		 * on saves us one PCI cycle per transaction.
		 */
		if (!(status & PCI_STATUS_FAST_BACK))
			features &= ~PCI_COMMAND_FAST_BACK;

		if (pdev_bad_for_parity(dev))
			features &= ~(PCI_COMMAND_SERR
					| PCI_COMMAND_PARITY);

		switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_PCI:
			pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &status);
			status |= PCI_BRIDGE_CTL_PARITY
				| PCI_BRIDGE_CTL_MASTER_ABORT;
			status &= ~(PCI_BRIDGE_CTL_BUS_RESET
				| PCI_BRIDGE_CTL_FAST_BACK);
			pci_write_config_word(dev, PCI_BRIDGE_CONTROL, status);
			break;

		case PCI_CLASS_BRIDGE_CARDBUS:
			pci_read_config_word(dev, PCI_CB_BRIDGE_CONTROL,
					&status);
			status |= PCI_CB_BRIDGE_CTL_PARITY
				| PCI_CB_BRIDGE_CTL_MASTER_ABORT;
			pci_write_config_word(dev, PCI_CB_BRIDGE_CONTROL,
					status);
			break;
		}
	}

	/*
	 * Now walk the devices again, this time setting them up.
	 */
	list_for_each_entry(dev, &bus->devices, bus_list) {
		u16 cmd;

		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd |= features;
		pci_write_config_word(dev, PCI_COMMAND, cmd);

		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE,
				      L1_CACHE_BYTES >> 2);
	}

	/*
	 * Propagate the flags to the PCI bridge.
	 */
	if (bus->self && bus->self->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		if (features & PCI_COMMAND_FAST_BACK)
			bus->bridge_ctl |= PCI_BRIDGE_CTL_FAST_BACK;
		if (features & PCI_COMMAND_PARITY)
			bus->bridge_ctl |= PCI_BRIDGE_CTL_PARITY;
	}

	/*
	 * Report what we did for this bus
	 */
	printk(KERN_INFO "PCI: bus%d: Fast back to back transfers %sabled\n",
		bus->number, (features & PCI_COMMAND_FAST_BACK) ? "en" : "dis");
}
#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_fixup_bus);
#endif

static int __init pci_common_init(void)
{
	struct pci_bus *puv3_bus;

	pci_puv3_preinit();

	puv3_bus = pci_scan_bus(0, &pci_puv3_ops, NULL);

	if (!puv3_bus)
		panic("PCI: unable to scan bus!");

	pci_fixup_irqs(pci_common_swizzle, pci_puv3_map_irq);

	if (!use_firmware) {
		/*
		 * Size the bridge windows.
		 */
		pci_bus_size_bridges(puv3_bus);

		/*
		 * Assign resources.
		 */
		pci_bus_assign_resources(puv3_bus);
	}

	/*
	 * Tell drivers about devices found.
	 */
	pci_bus_add_devices(puv3_bus);

	return 0;
}
subsys_initcall(pci_common_init);

char * __devinit pcibios_setup(char *str)
{
	if (!strcmp(str, "debug")) {
		debug_pci = 1;
		return NULL;
	} else if (!strcmp(str, "firmware")) {
		use_firmware = 1;
		return NULL;
	}
	return str;
}

/*
 * From arch/i386/kernel/pci-i386.c:
 *
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might be mirrored at 0x0100-0x03ff..
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO && start & 0x300)
		start = (start + 0x3ff) & ~0x3ff;

	start = (start + align - 1) & ~(align - 1);

	return start;
}

/**
 * pcibios_enable_device - Enable I/O and memory.
 * @dev: PCI device to be enabled
 */
int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		/* Only set up the requested stuff */
		if (!(mask & (1 << idx)))
			continue;

		r = dev->resource + idx;
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because"
			       " of resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	/*
	 * Bridges (eg, cardbus bridges) need to be fully enabled
	 */
	if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE)
		cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

	if (cmd != old_cmd) {
		printk("PCI: enabling device %s (%04x -> %04x)\n",
		       pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long phys;

	if (mmap_state == pci_mmap_io)
		return -EINVAL;

	phys = vma->vm_pgoff;

	/*
	 * Mark this as IO
	 */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, phys,
			     vma->vm_end - vma->vm_start,
			     vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
