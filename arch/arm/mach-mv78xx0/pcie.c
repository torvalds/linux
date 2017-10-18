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
#include <linux/mbus.h>
#include <video/vga.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <plat/pcie.h>
#include "mv78xx0.h"
#include "common.h"

#define MV78XX0_MBUS_PCIE_MEM_TARGET(port, lane) ((port) ? 8 : 4)
#define MV78XX0_MBUS_PCIE_MEM_ATTR(port, lane)   (0xf8 & ~(0x10 << (lane)))
#define MV78XX0_MBUS_PCIE_IO_TARGET(port, lane)  ((port) ? 8 : 4)
#define MV78XX0_MBUS_PCIE_IO_ATTR(port, lane)    (0xf0 & ~(0x10 << (lane)))

struct pcie_port {
	u8			maj;
	u8			min;
	u8			root_bus_nr;
	void __iomem		*base;
	spinlock_t		conf_lock;
	char			mem_space_name[20];
	struct resource		res;
};

static struct pcie_port pcie_port[8];
static int num_pcie_ports;
static struct resource pcie_io_space;

void __init mv78xx0_pcie_id(u32 *dev, u32 *rev)
{
	*dev = orion_pcie_dev_id(PCIE00_VIRT_BASE);
	*rev = orion_pcie_rev(PCIE00_VIRT_BASE);
}

u32 pcie_port_size[8] = {
	0,
	0x30000000,
	0x10000000,
	0x10000000,
	0x08000000,
	0x08000000,
	0x08000000,
	0x04000000,
};

static void __init mv78xx0_pcie_preinit(void)
{
	int i;
	u32 size_each;
	u32 start;

	pcie_io_space.name = "PCIe I/O Space";
	pcie_io_space.start = MV78XX0_PCIE_IO_PHYS_BASE(0);
	pcie_io_space.end =
		MV78XX0_PCIE_IO_PHYS_BASE(0) + MV78XX0_PCIE_IO_SIZE * 8 - 1;
	pcie_io_space.flags = IORESOURCE_MEM;
	if (request_resource(&iomem_resource, &pcie_io_space))
		panic("can't allocate PCIe I/O space");

	if (num_pcie_ports > 7)
		panic("invalid number of PCIe ports");

	size_each = pcie_port_size[num_pcie_ports];

	start = MV78XX0_PCIE_MEM_PHYS_BASE;
	for (i = 0; i < num_pcie_ports; i++) {
		struct pcie_port *pp = pcie_port + i;

		snprintf(pp->mem_space_name, sizeof(pp->mem_space_name),
			"PCIe %d.%d MEM", pp->maj, pp->min);
		pp->mem_space_name[sizeof(pp->mem_space_name) - 1] = 0;
		pp->res.name = pp->mem_space_name;
		pp->res.flags = IORESOURCE_MEM;
		pp->res.start = start;
		pp->res.end = start + size_each - 1;
		start += size_each;

		if (request_resource(&iomem_resource, &pp->res))
			panic("can't allocate PCIe MEM sub-space");

		mvebu_mbus_add_window_by_id(MV78XX0_MBUS_PCIE_MEM_TARGET(pp->maj, pp->min),
					    MV78XX0_MBUS_PCIE_MEM_ATTR(pp->maj, pp->min),
					    pp->res.start, resource_size(&pp->res));
		mvebu_mbus_add_window_remap_by_id(MV78XX0_MBUS_PCIE_IO_TARGET(pp->maj, pp->min),
						  MV78XX0_MBUS_PCIE_IO_ATTR(pp->maj, pp->min),
						  i * SZ_64K, SZ_64K, 0);
	}
}

static int __init mv78xx0_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct pcie_port *pp;

	if (nr >= num_pcie_ports)
		return 0;

	pp = &pcie_port[nr];
	sys->private_data = pp;
	pp->root_bus_nr = sys->busnr;

	/*
	 * Generic PCIe unit setup.
	 */
	orion_pcie_set_local_bus_nr(pp->base, sys->busnr);
	orion_pcie_setup(pp->base);

	pci_ioremap_io(nr * SZ_64K, MV78XX0_PCIE_IO_PHYS_BASE(nr));

	pci_add_resource_offset(&sys->resources, &pp->res, sys->mem_offset);

	return 1;
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
	struct pci_sys_data *sys = bus->sysdata;
	struct pcie_port *pp = sys->private_data;
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
	struct pci_sys_data *sys = bus->sysdata;
	struct pcie_port *pp = sys->private_data;
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

static void rc_pci_fixup(struct pci_dev *dev)
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

static int __init mv78xx0_pcie_scan_bus(int nr, struct pci_host_bridge *bridge)
{
	struct pci_sys_data *sys = pci_host_bridge_priv(bridge);

	if (nr >= num_pcie_ports) {
		BUG();
		return -EINVAL;
	}

	list_splice_init(&sys->resources, &bridge->windows);
	bridge->dev.parent = NULL;
	bridge->sysdata = sys;
	bridge->busnr = sys->busnr;
	bridge->ops = &pcie_ops;

	return pci_scan_root_bus_bridge(bridge);
}

static int __init mv78xx0_pcie_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	struct pci_sys_data *sys = dev->bus->sysdata;
	struct pcie_port *pp = sys->private_data;

	return IRQ_MV78XX0_PCIE_00 + (pp->maj << 2) + pp->min;
}

static struct hw_pci mv78xx0_pci __initdata = {
	.nr_controllers	= 8,
	.preinit	= mv78xx0_pcie_preinit,
	.setup		= mv78xx0_pcie_setup,
	.scan		= mv78xx0_pcie_scan_bus,
	.map_irq	= mv78xx0_pcie_map_irq,
};

static void __init add_pcie_port(int maj, int min, void __iomem *base)
{
	printk(KERN_INFO "MV78xx0 PCIe port %d.%d: ", maj, min);

	if (orion_pcie_link_up(base)) {
		struct pcie_port *pp = &pcie_port[num_pcie_ports++];

		printk("link up\n");

		pp->maj = maj;
		pp->min = min;
		pp->root_bus_nr = -1;
		pp->base = base;
		spin_lock_init(&pp->conf_lock);
		memset(&pp->res, 0, sizeof(pp->res));
	} else {
		printk("link down, ignoring\n");
	}
}

void __init mv78xx0_pcie_init(int init_port0, int init_port1)
{
	vga_base = MV78XX0_PCIE_MEM_PHYS_BASE;

	if (init_port0) {
		add_pcie_port(0, 0, PCIE00_VIRT_BASE);
		if (!orion_pcie_x4_mode(PCIE00_VIRT_BASE)) {
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
