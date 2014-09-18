/*
 * PCI-E support for CNS3xxx
 *
 * Copyright 2008 Cavium Networks
 *		  Richard Liu <richard.liu@caviumnetworks.com>
 * Copyright 2010 MontaVista Software, LLC.
 *		  Anton Vorontsov <avorontsov@mvista.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <asm/mach/map.h>
#include "cns3xxx.h"
#include "core.h"

struct cns3xxx_pcie {
	void __iomem *host_regs; /* PCI config registers for host bridge */
	void __iomem *cfg0_regs; /* PCI Type 0 config registers */
	void __iomem *cfg1_regs; /* PCI Type 1 config registers */
	unsigned int irqs[2];
	struct resource res_io;
	struct resource res_mem;
	struct hw_pci hw_pci;

	bool linked;
};

static struct cns3xxx_pcie cns3xxx_pcie[]; /* forward decl. */

static struct cns3xxx_pcie *sysdata_to_cnspci(void *sysdata)
{
	struct pci_sys_data *root = sysdata;

	return &cns3xxx_pcie[root->domain];
}

static struct cns3xxx_pcie *pdev_to_cnspci(const struct pci_dev *dev)
{
	return sysdata_to_cnspci(dev->sysdata);
}

static struct cns3xxx_pcie *pbus_to_cnspci(struct pci_bus *bus)
{
	return sysdata_to_cnspci(bus->sysdata);
}

static void __iomem *cns3xxx_pci_cfg_base(struct pci_bus *bus,
				  unsigned int devfn, int where)
{
	struct cns3xxx_pcie *cnspci = pbus_to_cnspci(bus);
	int busno = bus->number;
	int slot = PCI_SLOT(devfn);
	int offset;
	void __iomem *base;

	/* If there is no link, just show the CNS PCI bridge. */
	if (!cnspci->linked && (busno > 0 || slot > 0))
		return NULL;

	/*
	 * The CNS PCI bridge doesn't fit into the PCI hierarchy, though
	 * we still want to access it. For this to work, we must place
	 * the first device on the same bus as the CNS PCI bridge.
	 */
	if (busno == 0) { /* directly connected PCIe bus */
		switch (slot) {
		case 0: /* host bridge device, function 0 only */
			base = cnspci->host_regs;
			break;
		case 1: /* directly connected device */
			base = cnspci->cfg0_regs;
			break;
		default:
			return NULL; /* no such device */
		}
	} else /* remote PCI bus */
		base = cnspci->cfg1_regs;

	offset = ((busno & 0xf) << 20) | (devfn << 12) | (where & 0xffc);
	return base + offset;
}

static int cns3xxx_pci_read_config(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	u32 v;
	void __iomem *base;
	u32 mask = (0x1ull << (size * 8)) - 1;
	int shift = (where % 4) * 8;

	base = cns3xxx_pci_cfg_base(bus, devfn, where);
	if (!base) {
		*val = 0xffffffff;
		return PCIBIOS_SUCCESSFUL;
	}

	v = __raw_readl(base);

	if (bus->number == 0 && devfn == 0 &&
			(where & 0xffc) == PCI_CLASS_REVISION) {
		/*
		 * RC's class is 0xb, but Linux PCI driver needs 0x604
		 * for a PCIe bridge. So we must fixup the class code
		 * to 0x604 here.
		 */
		v &= 0xff;
		v |= 0x604 << 16;
	}

	*val = (v >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int cns3xxx_pci_write_config(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 val)
{
	u32 v;
	void __iomem *base;
	u32 mask = (0x1ull << (size * 8)) - 1;
	int shift = (where % 4) * 8;

	base = cns3xxx_pci_cfg_base(bus, devfn, where);
	if (!base)
		return PCIBIOS_SUCCESSFUL;

	v = __raw_readl(base);

	v &= ~(mask << shift);
	v |= (val & mask) << shift;

	__raw_writel(v, base);

	return PCIBIOS_SUCCESSFUL;
}

static int cns3xxx_pci_setup(int nr, struct pci_sys_data *sys)
{
	struct cns3xxx_pcie *cnspci = sysdata_to_cnspci(sys);
	struct resource *res_io = &cnspci->res_io;
	struct resource *res_mem = &cnspci->res_mem;

	BUG_ON(request_resource(&iomem_resource, res_io) ||
	       request_resource(&iomem_resource, res_mem));

	pci_add_resource_offset(&sys->resources, res_io, sys->io_offset);
	pci_add_resource_offset(&sys->resources, res_mem, sys->mem_offset);

	return 1;
}

static struct pci_ops cns3xxx_pcie_ops = {
	.read = cns3xxx_pci_read_config,
	.write = cns3xxx_pci_write_config,
};

static int cns3xxx_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct cns3xxx_pcie *cnspci = pdev_to_cnspci(dev);
	int irq = cnspci->irqs[slot];

	pr_info("PCIe map irq: %04d:%02x:%02x.%02x slot %d, pin %d, irq: %d\n",
		pci_domain_nr(dev->bus), dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), slot, pin, irq);

	return irq;
}

static struct cns3xxx_pcie cns3xxx_pcie[] = {
	[0] = {
		.host_regs = (void __iomem *)CNS3XXX_PCIE0_HOST_BASE_VIRT,
		.cfg0_regs = (void __iomem *)CNS3XXX_PCIE0_CFG0_BASE_VIRT,
		.cfg1_regs = (void __iomem *)CNS3XXX_PCIE0_CFG1_BASE_VIRT,
		.res_io = {
			.name = "PCIe0 I/O space",
			.start = CNS3XXX_PCIE0_IO_BASE,
			.end = CNS3XXX_PCIE0_CFG0_BASE - 1, /* 16 MiB */
			.flags = IORESOURCE_IO,
		},
		.res_mem = {
			.name = "PCIe0 non-prefetchable",
			.start = CNS3XXX_PCIE0_MEM_BASE,
			.end = CNS3XXX_PCIE0_HOST_BASE - 1, /* 176 MiB */
			.flags = IORESOURCE_MEM,
		},
		.irqs = { IRQ_CNS3XXX_PCIE0_RC, IRQ_CNS3XXX_PCIE0_DEVICE, },
		.hw_pci = {
			.domain = 0,
			.nr_controllers = 1,
			.ops = &cns3xxx_pcie_ops,
			.setup = cns3xxx_pci_setup,
			.map_irq = cns3xxx_pcie_map_irq,
		},
	},
	[1] = {
		.host_regs = (void __iomem *)CNS3XXX_PCIE1_HOST_BASE_VIRT,
		.cfg0_regs = (void __iomem *)CNS3XXX_PCIE1_CFG0_BASE_VIRT,
		.cfg1_regs = (void __iomem *)CNS3XXX_PCIE1_CFG1_BASE_VIRT,
		.res_io = {
			.name = "PCIe1 I/O space",
			.start = CNS3XXX_PCIE1_IO_BASE,
			.end = CNS3XXX_PCIE1_CFG0_BASE - 1, /* 16 MiB */
			.flags = IORESOURCE_IO,
		},
		.res_mem = {
			.name = "PCIe1 non-prefetchable",
			.start = CNS3XXX_PCIE1_MEM_BASE,
			.end = CNS3XXX_PCIE1_HOST_BASE - 1, /* 176 MiB */
			.flags = IORESOURCE_MEM,
		},
		.irqs = { IRQ_CNS3XXX_PCIE1_RC, IRQ_CNS3XXX_PCIE1_DEVICE, },
		.hw_pci = {
			.domain = 1,
			.nr_controllers = 1,
			.ops = &cns3xxx_pcie_ops,
			.setup = cns3xxx_pci_setup,
			.map_irq = cns3xxx_pcie_map_irq,
		},
	},
};

static void __init cns3xxx_pcie_check_link(struct cns3xxx_pcie *cnspci)
{
	int port = cnspci->hw_pci.domain;
	u32 reg;
	unsigned long time;

	reg = __raw_readl(MISC_PCIE_CTRL(port));
	/*
	 * Enable Application Request to 1, it will exit L1 automatically,
	 * but when chip back, it will use another clock, still can use 0x1.
	 */
	reg |= 0x3;
	__raw_writel(reg, MISC_PCIE_CTRL(port));

	pr_info("PCIe: Port[%d] Enable PCIe LTSSM\n", port);
	pr_info("PCIe: Port[%d] Check data link layer...", port);

	time = jiffies;
	while (1) {
		reg = __raw_readl(MISC_PCIE_PM_DEBUG(port));
		if (reg & 0x1) {
			pr_info("Link up.\n");
			cnspci->linked = 1;
			break;
		} else if (time_after(jiffies, time + 50)) {
			pr_info("Device not found.\n");
			break;
		}
	}
}

static void __init cns3xxx_pcie_hw_init(struct cns3xxx_pcie *cnspci)
{
	int port = cnspci->hw_pci.domain;
	struct pci_sys_data sd = {
		.domain = port,
	};
	struct pci_bus bus = {
		.number = 0,
		.ops = &cns3xxx_pcie_ops,
		.sysdata = &sd,
	};
	u16 mem_base  = cnspci->res_mem.start >> 16;
	u16 mem_limit = cnspci->res_mem.end   >> 16;
	u16 io_base   = cnspci->res_io.start  >> 16;
	u16 io_limit  = cnspci->res_io.end    >> 16;
	u32 devfn = 0;
	u8 tmp8;
	u16 pos;
	u16 dc;

	pci_bus_write_config_byte(&bus, devfn, PCI_PRIMARY_BUS, 0);
	pci_bus_write_config_byte(&bus, devfn, PCI_SECONDARY_BUS, 1);
	pci_bus_write_config_byte(&bus, devfn, PCI_SUBORDINATE_BUS, 1);

	pci_bus_read_config_byte(&bus, devfn, PCI_PRIMARY_BUS, &tmp8);
	pci_bus_read_config_byte(&bus, devfn, PCI_SECONDARY_BUS, &tmp8);
	pci_bus_read_config_byte(&bus, devfn, PCI_SUBORDINATE_BUS, &tmp8);

	pci_bus_write_config_word(&bus, devfn, PCI_MEMORY_BASE, mem_base);
	pci_bus_write_config_word(&bus, devfn, PCI_MEMORY_LIMIT, mem_limit);
	pci_bus_write_config_word(&bus, devfn, PCI_IO_BASE_UPPER16, io_base);
	pci_bus_write_config_word(&bus, devfn, PCI_IO_LIMIT_UPPER16, io_limit);

	if (!cnspci->linked)
		return;

	/* Set Device Max_Read_Request_Size to 128 byte */
	devfn = PCI_DEVFN(1, 0);
	pos = pci_bus_find_capability(&bus, devfn, PCI_CAP_ID_EXP);
	pci_bus_read_config_word(&bus, devfn, pos + PCI_EXP_DEVCTL, &dc);
	dc &= ~(0x3 << 12);	/* Clear Device Control Register [14:12] */
	pci_bus_write_config_word(&bus, devfn, pos + PCI_EXP_DEVCTL, dc);
	pci_bus_read_config_word(&bus, devfn, pos + PCI_EXP_DEVCTL, &dc);
	if (!(dc & (0x3 << 12)))
		pr_info("PCIe: Set Device Max_Read_Request_Size to 128 byte\n");

	/* Disable PCIe0 Interrupt Mask INTA to INTD */
	__raw_writel(~0x3FFF, MISC_PCIE_INT_MASK(port));
}

static int cns3xxx_pcie_abort_handler(unsigned long addr, unsigned int fsr,
				      struct pt_regs *regs)
{
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;
	return 0;
}

static int __init cns3xxx_pcie_init(void)
{
	int i;

	pcibios_min_io = 0;
	pcibios_min_mem = 0;

	hook_fault_code(16 + 6, cns3xxx_pcie_abort_handler, SIGBUS, 0,
			"imprecise external abort");

	for (i = 0; i < ARRAY_SIZE(cns3xxx_pcie); i++) {
		cns3xxx_pwr_clk_en(0x1 << PM_CLK_GATE_REG_OFFSET_PCIE(i));
		cns3xxx_pwr_soft_rst(0x1 << PM_SOFT_RST_REG_OFFST_PCIE(i));
		cns3xxx_pcie_check_link(&cns3xxx_pcie[i]);
		cns3xxx_pcie_hw_init(&cns3xxx_pcie[i]);
		pci_common_init(&cns3xxx_pcie[i].hw_pci);
	}

	pci_assign_unassigned_resources();

	return 0;
}
device_initcall(cns3xxx_pcie_init);
