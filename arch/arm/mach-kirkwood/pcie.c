/*
 * arch/arm/mach-kirkwood/pcie.c
 *
 * PCIe functions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mbus.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <plat/pcie.h>
#include <mach/bridge-regs.h>
#include "common.h"

void __init kirkwood_pcie_id(u32 *dev, u32 *rev)
{
	*dev = orion_pcie_dev_id((void __iomem *)PCIE_VIRT_BASE);
	*rev = orion_pcie_rev((void __iomem *)PCIE_VIRT_BASE);
}

struct pcie_port {
	u8			root_bus_nr;
	void __iomem		*base;
	spinlock_t		conf_lock;
	int			irq;
	struct resource		res[2];
};

static int pcie_port_map[2];
static int num_pcie_ports;

static inline struct pcie_port *bus_to_port(struct pci_bus *bus)
{
	struct pci_sys_data *sys = bus->sysdata;
	return sys->private_data;
}

static int pcie_valid_config(struct pcie_port *pp, int bus, int dev)
{
	/*
	 * Don't go out when trying to access --
	 * 1. nonexisting device on local bus
	 * 2. where there's no device connected (no link)
	 */
	if (bus == pp->root_bus_nr && dev == 0)
		return 1;

	if (!orion_pcie_link_up(pp->base))
		return 0;

	if (bus == pp->root_bus_nr && dev != 1)
		return 0;

	return 1;
}


/*
 * PCIe config cycles are done by programming the PCIE_CONF_ADDR register
 * and then reading the PCIE_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */

static int pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	struct pcie_port *pp = bus_to_port(bus);
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
	struct pcie_port *pp = bus_to_port(bus);
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

static void __init pcie0_ioresources_init(struct pcie_port *pp)
{
	pp->base = (void __iomem *)PCIE_VIRT_BASE;
	pp->irq	= IRQ_KIRKWOOD_PCIE;

	/*
	 * IORESOURCE_IO
	 */
	pp->res[0].name = "PCIe 0 I/O Space";
	pp->res[0].start = KIRKWOOD_PCIE_IO_BUS_BASE;
	pp->res[0].end = pp->res[0].start + KIRKWOOD_PCIE_IO_SIZE - 1;
	pp->res[0].flags = IORESOURCE_IO;

	/*
	 * IORESOURCE_MEM
	 */
	pp->res[1].name = "PCIe 0 MEM";
	pp->res[1].start = KIRKWOOD_PCIE_MEM_PHYS_BASE;
	pp->res[1].end = pp->res[1].start + KIRKWOOD_PCIE_MEM_SIZE - 1;
	pp->res[1].flags = IORESOURCE_MEM;
}

static void __init pcie1_ioresources_init(struct pcie_port *pp)
{
	pp->base = (void __iomem *)PCIE1_VIRT_BASE;
	pp->irq	= IRQ_KIRKWOOD_PCIE1;

	/*
	 * IORESOURCE_IO
	 */
	pp->res[0].name = "PCIe 1 I/O Space";
	pp->res[0].start = KIRKWOOD_PCIE1_IO_BUS_BASE;
	pp->res[0].end = pp->res[0].start + KIRKWOOD_PCIE1_IO_SIZE - 1;
	pp->res[0].flags = IORESOURCE_IO;

	/*
	 * IORESOURCE_MEM
	 */
	pp->res[1].name = "PCIe 1 MEM";
	pp->res[1].start = KIRKWOOD_PCIE1_MEM_PHYS_BASE;
	pp->res[1].end = pp->res[1].start + KIRKWOOD_PCIE1_MEM_SIZE - 1;
	pp->res[1].flags = IORESOURCE_MEM;
}

static int __init kirkwood_pcie_setup(int nr, struct pci_sys_data *sys)
{
	extern unsigned int kirkwood_clk_ctrl;
	struct pcie_port *pp;
	int index;

	if (nr >= num_pcie_ports)
		return 0;

	index = pcie_port_map[nr];
	printk(KERN_INFO "PCI: bus%d uses PCIe port %d\n", sys->busnr, index);

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		panic("PCIe: failed to allocate pcie_port data");
	sys->private_data = pp;
	pp->root_bus_nr = sys->busnr;
	spin_lock_init(&pp->conf_lock);

	switch (index) {
	case 0:
		kirkwood_clk_ctrl |= CGC_PEX0;
		pcie0_ioresources_init(pp);
		break;
	case 1:
		kirkwood_clk_ctrl |= CGC_PEX1;
		pcie1_ioresources_init(pp);
		break;
	default:
		panic("PCIe setup: invalid controller %d", index);
	}

	if (request_resource(&ioport_resource, &pp->res[0]))
		panic("Request PCIe%d IO resource failed\n", index);
	if (request_resource(&iomem_resource, &pp->res[1]))
		panic("Request PCIe%d Memory resource failed\n", index);

	sys->resource[0] = &pp->res[0];
	sys->resource[1] = &pp->res[1];
	sys->resource[2] = NULL;
	sys->io_offset = 0;

	/*
	 * Generic PCIe unit setup.
	 */
	orion_pcie_set_local_bus_nr(pp->base, sys->busnr);

	orion_pcie_setup(pp->base, &kirkwood_mbus_dram_info);

	return 1;
}

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
kirkwood_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_bus *bus;

	if (nr < num_pcie_ports) {
		bus = pci_scan_bus(sys->busnr, &pcie_ops, sys);
	} else {
		bus = NULL;
		BUG();
	}

	return bus;
}

static int __init kirkwood_pcie_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pcie_port *pp = bus_to_port(dev->bus);

	return pp->irq;
}

static struct hw_pci kirkwood_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.setup		= kirkwood_pcie_setup,
	.scan		= kirkwood_pcie_scan_bus,
	.map_irq	= kirkwood_pcie_map_irq,
};

static void __init add_pcie_port(int index, unsigned long base)
{
	printk(KERN_INFO "Kirkwood PCIe port %d: ", index);

	if (orion_pcie_link_up((void __iomem *)base)) {
		printk(KERN_INFO "link up\n");
		pcie_port_map[num_pcie_ports++] = index;
	} else
		printk(KERN_INFO "link down, ignoring\n");
}

void __init kirkwood_pcie_init(unsigned int portmask)
{
	if (portmask & KW_PCIE0)
		add_pcie_port(0, PCIE_VIRT_BASE);

	if (portmask & KW_PCIE1)
		add_pcie_port(1, PCIE1_VIRT_BASE);

	kirkwood_pci.nr_controllers = num_pcie_ports;
	pci_common_init(&kirkwood_pci);
}
