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
#include <linux/mbus.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <plat/pcie.h>
#include <mach/bridge-regs.h>
#include "common.h"


#define PCIE_BASE	((void __iomem *)PCIE_VIRT_BASE)

void __init kirkwood_pcie_id(u32 *dev, u32 *rev)
{
	*dev = orion_pcie_dev_id(PCIE_BASE);
	*rev = orion_pcie_rev(PCIE_BASE);
}

static int pcie_valid_config(int bus, int dev)
{
	/*
	 * Don't go out when trying to access --
	 * 1. nonexisting device on local bus
	 * 2. where there's no device connected (no link)
	 */
	if (bus == 0 && dev == 0)
		return 1;

	if (!orion_pcie_link_up(PCIE_BASE))
		return 0;

	if (bus == 0 && dev != 1)
		return 0;

	return 1;
}


/*
 * PCIe config cycles are done by programming the PCIE_CONF_ADDR register
 * and then reading the PCIE_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */
static DEFINE_SPINLOCK(kirkwood_pcie_lock);

static int pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	unsigned long flags;
	int ret;

	if (pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	spin_lock_irqsave(&kirkwood_pcie_lock, flags);
	ret = orion_pcie_rd_conf(PCIE_BASE, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&kirkwood_pcie_lock, flags);

	return ret;
}

static int pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	unsigned long flags;
	int ret;

	if (pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&kirkwood_pcie_lock, flags);
	ret = orion_pcie_wr_conf(PCIE_BASE, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&kirkwood_pcie_lock, flags);

	return ret;
}

static struct pci_ops pcie_ops = {
	.read = pcie_rd_conf,
	.write = pcie_wr_conf,
};


static int __init kirkwood_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;
	extern unsigned int kirkwood_clk_ctrl;

	/*
	 * Generic PCIe unit setup.
	 */
	orion_pcie_setup(PCIE_BASE, &kirkwood_mbus_dram_info);

	/*
	 * Request resources.
	 */
	res = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("pcie_setup unable to alloc resources");

	/*
	 * IORESOURCE_IO
	 */
	res[0].name = "PCIe I/O Space";
	res[0].flags = IORESOURCE_IO;
	res[0].start = KIRKWOOD_PCIE_IO_BUS_BASE;
	res[0].end = res[0].start + KIRKWOOD_PCIE_IO_SIZE - 1;
	if (request_resource(&ioport_resource, &res[0]))
		panic("Request PCIe IO resource failed\n");
	sys->resource[0] = &res[0];

	/*
	 * IORESOURCE_MEM
	 */
	res[1].name = "PCIe Memory Space";
	res[1].flags = IORESOURCE_MEM;
	res[1].start = KIRKWOOD_PCIE_MEM_BUS_BASE;
	res[1].end = res[1].start + KIRKWOOD_PCIE_MEM_SIZE - 1;
	if (request_resource(&iomem_resource, &res[1]))
		panic("Request PCIe Memory resource failed\n");
	sys->resource[1] = &res[1];

	sys->resource[2] = NULL;
	sys->io_offset = 0;

	kirkwood_clk_ctrl |= CGC_PEX0;

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

	if (nr == 0) {
		bus = pci_scan_bus(sys->busnr, &pcie_ops, sys);
	} else {
		bus = NULL;
		BUG();
	}

	return bus;
}

static int __init kirkwood_pcie_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return IRQ_KIRKWOOD_PCIE;
}

static struct hw_pci kirkwood_pci __initdata = {
	.nr_controllers	= 1,
	.swizzle	= pci_std_swizzle,
	.setup		= kirkwood_pcie_setup,
	.scan		= kirkwood_pcie_scan_bus,
	.map_irq	= kirkwood_pcie_map_irq,
};

void __init kirkwood_pcie_init(void)
{
	pci_common_init(&kirkwood_pci);
}
