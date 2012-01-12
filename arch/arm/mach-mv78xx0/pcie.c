/*
 * arch/arm/mach-mv78xx0/pcie.c
 *
 * PCIe functions for Marvell MV78xx0 SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <video/vga.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <plat/pcie.h>
#include <plat/addr-map.h>
#include "common.h"

struct pcie_port {
	u8			maj;
	u8			min;
	u8			root_bus_nr;
	void __iomem		*base;
	spinlock_t		conf_lock;
	char			io_space_name[16];
	char			mem_space_name[16];
	struct resource		res[2];
};

static struct pcie_port pcie_port[8];
static int num_pcie_ports;
static struct resource pcie_io_space;
static struct resource pcie_mem_space;


void __init mv78xx0_pcie_id(u32 *dev, u32 *rev)
{
	*dev = orion_pcie_dev_id((void __iomem *)PCIE00_VIRT_BASE);
	*rev = orion_pcie_rev((void __iomem *)PCIE00_VIRT_BASE);
}

static void __init mv78xx0_pcie_preinit(void)
{
	int i;
	u32 size_each;
	u32 start;
	int win;

	pcie_io_space.name = "PCIe I/O Space";
	pcie_io_space.start = MV78XX0_PCIE_IO_PHYS_BASE(0);
	pcie_io_space.end =
		MV78XX0_PCIE_IO_PHYS_BASE(0) + MV78XX0_PCIE_IO_SIZE * 8 - 1;
	pcie_io_space.flags = IORESOURCE_IO;
	if (request_resource(&iomem_resource, &pcie_io_space))
		panic("can't allocate PCIe I/O space");

	pcie_mem_space.name = "PCIe MEM Space";
	pcie_mem_space.start = MV78XX0_PCIE_MEM_PHYS_BASE;
	pcie_mem_space.end =
		MV78XX0_PCIE_MEM_PHYS_BASE + MV78XX0_PCIE_MEM_SIZE - 1;
	pcie_mem_space.flags = IORESOURCE_MEM;
	if (request_resource(&iomem_resource, &pcie_mem_space))
		panic("can't allocate PCIe MEM space");

	for (i = 0; i < num_pcie_ports; i++) {
		struct pcie_port *pp = pcie_port + i;

		snprintf(pp->io_space_name, sizeof(pp->io_space_name),
			"PCIe %d.%d I/O", pp->maj, pp->min);
		pp->io_space_name[sizeof(pp->io_space_name) - 1] = 0;
		pp->res[0].name = pp->io_space_name;
		pp->res[0].start = MV78XX0_PCIE_IO_PHYS_BASE(i);
		pp->res[0].end = pp->res[0].start + MV78XX0_PCIE_IO_SIZE - 1;
		pp->res[0].flags = IORESOURCE_IO;

		snprintf(pp->mem_space_name, sizeof(pp->mem_space_name),
			"PCIe %d.%d MEM", pp->maj, pp->min);
		pp->mem_space_name[sizeof(pp->mem_space_name) - 1] = 0;
		pp->res[1].name = pp->mem_space_name;
		pp->res[1].flags = IORESOURCE_MEM;
	}

	switch (num_pcie_ports) {
	case 0:
		size_each = 0;
		break;

	case 1:
		size_each = 0x30000000;
		break;

	case 2 ... 3:
		size_each = 0x10000000;
		break;

	case 4 ... 6:
		size_each = 0x08000000;
		break;

	case 7:
		size_each = 0x04000000;
		break;

	default:
		panic("invalid number of PCIe ports");
	}

	start = MV78XX0_PCIE_MEM_PHYS_BASE;
	for (i = 0; i < num_pcie_ports; i++) {
		struct pcie_port *pp = pcie_port + i;

		pp->res[1].start = start;
		pp->res[1].end = start + size_each - 1;
		start += size_each;
	}

	for (i = 0; i < num_pcie_ports; i++) {
		struct pcie_port *pp = pcie_port + i;

		if (request_resource(&pcie_io_space, &pp->res[0]))
			panic("can't allocate PCIe I/O sub-space");

		if (request_resource(&pcie_mem_space, &pp->res[1]))
			panic("can't allocate PCIe MEM sub-space");
	}

	win = 0;
	for (i = 0; i < num_pcie_ports; i++) {
		struct pcie_port *pp = pcie_port + i;

		mv78xx0_setup_pcie_io_win(win++, pp->res[0].start,
					  resource_size(&pp->res[0]),
					  pp->maj, pp->min);

		mv78xx0_setup_pcie_mem_win(win++, pp->res[1].start,
					   resource_size(&pp->res[1]),
					   pp->maj, pp->min);
	}
}

static int __init mv78xx0_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct pcie_port *pp;

	if (nr >= num_pcie_ports)
		return 0;

	pp = &pcie_port[nr];
	pp->root_bus_nr = sys->busnr;

	/*
	 * Generic PCIe unit setup.
	 */
	orion_pcie_set_local_bus_nr(pp->base, sys->busnr);
	orion_pcie_setup(pp->base);

	pci_add_resource(&sys->resources, &pp->res[0]);
	pci_add_resource(&sys->resources, &pp->res[1]);

	return 1;
}

static struct pcie_port *bus_to_port(int bus)
{
	int i;

	for (i = num_pcie_ports - 1; i >= 0; i--) {
		int rbus = pcie_port[i].root_bus_nr;
		if (rbus != -1 && rbus <= bus)
			break;
	}

	return i >= 0 ? pcie_port + i : NULL;
}

static int pcie_valid_config(struct pcie_port *pp, int bus, int dev)
{
	/*
	 * Don't go out when trying to access nonexisting devices
	 * on the local bus.
	 */
	if (bus == pp->root_bus_nr && dev > 1)
		return 0;

	return 1;
}

static int pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	struct pcie_port *pp = bus_to_port(bus->number);
	unsigned long flags;
	int ret;

	if (pcie_valid_config(pp, bus->number, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	spin_lock_irqsave(&pp->conf_lock, flags);
	ret = orion_pcie_rd_conf(pp->base, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&pp->conf_lock, flags);

	return ret;
}

static int pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	struct pcie_port *pp = bus_to_port(bus->number);
	unsigned long flags;
	int ret;

	if (pcie_valid_config(pp, bus->number, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&pp->conf_lock, flags);
	ret = orion_pcie_wr_conf(pp->base, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&pp->conf_lock, flags);

	return ret;
}

static struct pci_ops pcie_ops = {
	.read = pcie_rd_conf,
	.write = pcie_wr_conf,
};

static void __devinit rc_pci_fixup(struct pci_dev *dev)
{
	/*
	 * Prevent enumeration of root complex.
	 */
	if (dev->bus->parent == NULL && dev->devfn == 0) {
		int i;

		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			dev->resource[i].start = 0;
			dev->resource[i].end   = 0;
			dev->resource[i].flags = 0;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL, PCI_ANY_ID, rc_pci_fixup);

static struct pci_bus __init *
mv78xx0_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_bus *bus;

	if (nr < num_pcie_ports) {
		bus = pci_scan_root_bus(NULL, sys->busnr, &pcie_ops, sys,
					&sys->resources);
	} else {
		bus = NULL;
		BUG();
	}

	return bus;
}

static int __init mv78xx0_pcie_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	struct pcie_port *pp = bus_to_port(dev->bus->number);

	return IRQ_MV78XX0_PCIE_00 + (pp->maj << 2) + pp->min;
}

static struct hw_pci mv78xx0_pci __initdata = {
	.nr_controllers	= 8,
	.preinit	= mv78xx0_pcie_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= mv78xx0_pcie_setup,
	.scan		= mv78xx0_pcie_scan_bus,
	.map_irq	= mv78xx0_pcie_map_irq,
};

static void __init add_pcie_port(int maj, int min, unsigned long base)
{
	printk(KERN_INFO "MV78xx0 PCIe port %d.%d: ", maj, min);

	if (orion_pcie_link_up((void __iomem *)base)) {
		struct pcie_port *pp = &pcie_port[num_pcie_ports++];

		printk("link up\n");

		pp->maj = maj;
		pp->min = min;
		pp->root_bus_nr = -1;
		pp->base = (void __iomem *)base;
		spin_lock_init(&pp->conf_lock);
		memset(pp->res, 0, sizeof(pp->res));
	} else {
		printk("link down, ignoring\n");
	}
}

void __init mv78xx0_pcie_init(int init_port0, int init_port1)
{
	vga_base = MV78XX0_PCIE_MEM_PHYS_BASE;

	if (init_port0) {
		add_pcie_port(0, 0, PCIE00_VIRT_BASE);
		if (!orion_pcie_x4_mode((void __iomem *)PCIE00_VIRT_BASE)) {
			add_pcie_port(0, 1, PCIE01_VIRT_BASE);
			add_pcie_port(0, 2, PCIE02_VIRT_BASE);
			add_pcie_port(0, 3, PCIE03_VIRT_BASE);
		}
	}

	if (init_port1) {
		add_pcie_port(1, 0, PCIE10_VIRT_BASE);
		if (!orion_pcie_x4_mode((void __iomem *)PCIE10_VIRT_BASE)) {
			add_pcie_port(1, 1, PCIE11_VIRT_BASE);
			add_pcie_port(1, 2, PCIE12_VIRT_BASE);
			add_pcie_port(1, 3, PCIE13_VIRT_BASE);
		}
	}

	pci_common_init(&mv78xx0_pci);
}
