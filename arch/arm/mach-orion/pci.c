/*
 * arch/arm/mach-orion/pci.c
 *
 * PCI and PCIE functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/mbus.h>
#include <asm/mach/pci.h>
#include <asm/plat-orion/pcie.h>
#include "common.h"

/*****************************************************************************
 * Orion has one PCIE controller and one PCI controller.
 *
 * Note1: The local PCIE bus number is '0'. The local PCI bus number
 * follows the scanned PCIE bridged busses, if any.
 *
 * Note2: It is possible for PCI/PCIE agents to access many subsystem's
 * space, by configuring BARs and Address Decode Windows, e.g. flashes on
 * device bus, Orion registers, etc. However this code only enable the
 * access to DDR banks.
 ****************************************************************************/


/*****************************************************************************
 * PCIE controller
 ****************************************************************************/
#define PCIE_BASE	((void __iomem *)ORION_PCIE_VIRT_BASE)

void __init orion_pcie_id(u32 *dev, u32 *rev)
{
	*dev = orion_pcie_dev_id(PCIE_BASE);
	*rev = orion_pcie_rev(PCIE_BASE);
}

int orion_pcie_local_bus_nr(void)
{
	return orion_pcie_get_local_bus_nr(PCIE_BASE);
}

static int pcie_valid_config(int bus, int dev)
{
	/*
	 * Don't go out when trying to access --
	 * 1. our own device / nonexisting device on local bus
	 * 2. where there's no device connected (no link)
	 */
	if (bus == 0 && dev != 1)
		return 0;

	if (!orion_pcie_link_up(PCIE_BASE))
		return 0;

	return 1;
}


/*
 * PCIE config cycles are done by programming the PCIE_CONF_ADDR register
 * and then reading the PCIE_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */
static DEFINE_SPINLOCK(orion_pcie_lock);

static int pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	unsigned long flags;
	int ret;

	if (pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	spin_lock_irqsave(&orion_pcie_lock, flags);
	ret = orion_pcie_rd_conf(PCIE_BASE, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&orion_pcie_lock, flags);

	return ret;
}

static int pcie_rd_conf_wa(struct pci_bus *bus, u32 devfn,
			   int where, int size, u32 *val)
{
	int ret;

	if (pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/*
	 * We only support access to the non-extended configuration
	 * space when using the WA access method (or we would have to
	 * sacrifice 256M of CPU virtual address space.)
	 */
	if (where >= 0x100) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	ret = orion_pcie_rd_conf_wa((void __iomem *)ORION_PCIE_WA_VIRT_BASE,
				    bus, devfn, where, size, val);

	return ret;
}

static int pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	unsigned long flags;
	int ret;

	if (pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&orion_pcie_lock, flags);
	ret = orion_pcie_wr_conf(PCIE_BASE, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&orion_pcie_lock, flags);

	return ret;
}

struct pci_ops pcie_ops = {
	.read = pcie_rd_conf,
	.write = pcie_wr_conf,
};


static int __init pcie_setup(struct pci_sys_data *sys)
{
	struct resource *res;
	int dev;

	/*
	 * Generic PCIe unit setup.
	 */
	orion_pcie_setup(PCIE_BASE, &orion_mbus_dram_info);

	/*
	 * Check whether to apply Orion-1/Orion-NAS PCIe config
	 * read transaction workaround.
	 */
	dev = orion_pcie_dev_id(PCIE_BASE);
	if (dev == MV88F5181_DEV_ID || dev == MV88F5182_DEV_ID) {
		printk(KERN_NOTICE "Applying Orion-1/Orion-NAS PCIe config "
				   "read transaction workaround\n");
		pcie_ops.read = pcie_rd_conf_wa;
	}

	/*
	 * Request resources.
	 */
	res = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("pcie_setup unable to alloc resources");

	/*
	 * IORESOURCE_IO
	 */
	res[0].name = "PCI-EX I/O Space";
	res[0].flags = IORESOURCE_IO;
	res[0].start = ORION_PCIE_IO_BUS_BASE;
	res[0].end = res[0].start + ORION_PCIE_IO_SIZE - 1;
	if (request_resource(&ioport_resource, &res[0]))
		panic("Request PCIE IO resource failed\n");
	sys->resource[0] = &res[0];

	/*
	 * IORESOURCE_MEM
	 */
	res[1].name = "PCI-EX Memory Space";
	res[1].flags = IORESOURCE_MEM;
	res[1].start = ORION_PCIE_MEM_PHYS_BASE;
	res[1].end = res[1].start + ORION_PCIE_MEM_SIZE - 1;
	if (request_resource(&iomem_resource, &res[1]))
		panic("Request PCIE Memory resource failed\n");
	sys->resource[1] = &res[1];

	sys->resource[2] = NULL;
	sys->io_offset = 0;

	return 1;
}

/*****************************************************************************
 * PCI controller
 ****************************************************************************/
#define PCI_MODE		ORION_PCI_REG(0xd00)
#define PCI_CMD			ORION_PCI_REG(0xc00)
#define PCI_P2P_CONF		ORION_PCI_REG(0x1d14)
#define PCI_CONF_ADDR		ORION_PCI_REG(0xc78)
#define PCI_CONF_DATA		ORION_PCI_REG(0xc7c)

/*
 * PCI_MODE bits
 */
#define PCI_MODE_64BIT			(1 << 2)
#define PCI_MODE_PCIX			((1 << 4) | (1 << 5))

/*
 * PCI_CMD bits
 */
#define PCI_CMD_HOST_REORDER		(1 << 29)

/*
 * PCI_P2P_CONF bits
 */
#define PCI_P2P_BUS_OFFS		16
#define PCI_P2P_BUS_MASK		(0xff << PCI_P2P_BUS_OFFS)
#define PCI_P2P_DEV_OFFS		24
#define PCI_P2P_DEV_MASK		(0x1f << PCI_P2P_DEV_OFFS)

/*
 * PCI_CONF_ADDR bits
 */
#define PCI_CONF_REG(reg)		((reg) & 0xfc)
#define PCI_CONF_FUNC(func)		(((func) & 0x3) << 8)
#define PCI_CONF_DEV(dev)		(((dev) & 0x1f) << 11)
#define PCI_CONF_BUS(bus)		(((bus) & 0xff) << 16)
#define PCI_CONF_ADDR_EN		(1 << 31)

/*
 * Internal configuration space
 */
#define PCI_CONF_FUNC_STAT_CMD		0
#define PCI_CONF_REG_STAT_CMD		4
#define PCIX_STAT			0x64
#define PCIX_STAT_BUS_OFFS		8
#define PCIX_STAT_BUS_MASK		(0xff << PCIX_STAT_BUS_OFFS)

/*
 * PCI Address Decode Windows registers
 */
#define PCI_BAR_SIZE_DDR_CS(n)	(((n) == 0) ? ORION_PCI_REG(0xc08) : \
				((n) == 1) ? ORION_PCI_REG(0xd08) :  \
				((n) == 2) ? ORION_PCI_REG(0xc0c) :  \
				((n) == 3) ? ORION_PCI_REG(0xd0c) : 0)
#define PCI_BAR_REMAP_DDR_CS(n)	(((n) ==0) ? ORION_PCI_REG(0xc48) :  \
				((n) == 1) ? ORION_PCI_REG(0xd48) :  \
				((n) == 2) ? ORION_PCI_REG(0xc4c) :  \
				((n) == 3) ? ORION_PCI_REG(0xd4c) : 0)
#define PCI_BAR_ENABLE		ORION_PCI_REG(0xc3c)
#define PCI_ADDR_DECODE_CTRL	ORION_PCI_REG(0xd3c)

/*
 * PCI configuration helpers for BAR settings
 */
#define PCI_CONF_FUNC_BAR_CS(n)		((n) >> 1)
#define PCI_CONF_REG_BAR_LO_CS(n)	(((n) & 1) ? 0x18 : 0x10)
#define PCI_CONF_REG_BAR_HI_CS(n)	(((n) & 1) ? 0x1c : 0x14)

/*
 * PCI config cycles are done by programming the PCI_CONF_ADDR register
 * and then reading the PCI_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */
static DEFINE_SPINLOCK(orion_pci_lock);

int orion_pci_local_bus_nr(void)
{
	u32 conf = orion_read(PCI_P2P_CONF);
	return((conf & PCI_P2P_BUS_MASK) >> PCI_P2P_BUS_OFFS);
}

static int orion_pci_local_dev_nr(void)
{
	u32 conf = orion_read(PCI_P2P_CONF);
	return((conf & PCI_P2P_DEV_MASK) >> PCI_P2P_DEV_OFFS);
}

static int orion_pci_hw_rd_conf(int bus, int dev, u32 func,
					u32 where, u32 size, u32 *val)
{
	unsigned long flags;
	spin_lock_irqsave(&orion_pci_lock, flags);

	orion_write(PCI_CONF_ADDR, PCI_CONF_BUS(bus) |
			PCI_CONF_DEV(dev) | PCI_CONF_REG(where) |
			PCI_CONF_FUNC(func) | PCI_CONF_ADDR_EN);

	*val = orion_read(PCI_CONF_DATA);

	if (size == 1)
		*val = (*val >> (8*(where & 0x3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8*(where & 0x3))) & 0xffff;

	spin_unlock_irqrestore(&orion_pci_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int orion_pci_hw_wr_conf(int bus, int dev, u32 func,
					u32 where, u32 size, u32 val)
{
	unsigned long flags;
	int ret = PCIBIOS_SUCCESSFUL;

	spin_lock_irqsave(&orion_pci_lock, flags);

	orion_write(PCI_CONF_ADDR, PCI_CONF_BUS(bus) |
			PCI_CONF_DEV(dev) | PCI_CONF_REG(where) |
			PCI_CONF_FUNC(func) | PCI_CONF_ADDR_EN);

	if (size == 4) {
		__raw_writel(val, PCI_CONF_DATA);
	} else if (size == 2) {
		__raw_writew(val, PCI_CONF_DATA + (where & 0x3));
	} else if (size == 1) {
		__raw_writeb(val, PCI_CONF_DATA + (where & 0x3));
	} else {
		ret = PCIBIOS_BAD_REGISTER_NUMBER;
	}

	spin_unlock_irqrestore(&orion_pci_lock, flags);

	return ret;
}

static int orion_pci_rd_conf(struct pci_bus *bus, u32 devfn,
				int where, int size, u32 *val)
{
	/*
	 * Don't go out for local device
	 */
	if ((orion_pci_local_bus_nr() == bus->number) &&
	   (orion_pci_local_dev_nr() == PCI_SLOT(devfn))) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	return orion_pci_hw_rd_conf(bus->number, PCI_SLOT(devfn),
					PCI_FUNC(devfn), where, size, val);
}

static int orion_pci_wr_conf(struct pci_bus *bus, u32 devfn,
				int where, int size, u32 val)
{
	/*
	 * Don't go out for local device
	 */
	if ((orion_pci_local_bus_nr() == bus->number) &&
	   (orion_pci_local_dev_nr() == PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return orion_pci_hw_wr_conf(bus->number, PCI_SLOT(devfn),
					PCI_FUNC(devfn), where, size, val);
}

struct pci_ops pci_ops = {
	.read = orion_pci_rd_conf,
	.write = orion_pci_wr_conf,
};

static void __init orion_pci_set_bus_nr(int nr)
{
	u32 p2p = orion_read(PCI_P2P_CONF);

	if (orion_read(PCI_MODE) & PCI_MODE_PCIX) {
		/*
		 * PCI-X mode
		 */
		u32 pcix_status, bus, dev;
		bus = (p2p & PCI_P2P_BUS_MASK) >> PCI_P2P_BUS_OFFS;
		dev = (p2p & PCI_P2P_DEV_MASK) >> PCI_P2P_DEV_OFFS;
		orion_pci_hw_rd_conf(bus, dev, 0, PCIX_STAT, 4, &pcix_status);
		pcix_status &= ~PCIX_STAT_BUS_MASK;
		pcix_status |= (nr << PCIX_STAT_BUS_OFFS);
		orion_pci_hw_wr_conf(bus, dev, 0, PCIX_STAT, 4, pcix_status);
	} else {
		/*
		 * PCI Conventional mode
		 */
		p2p &= ~PCI_P2P_BUS_MASK;
		p2p |= (nr << PCI_P2P_BUS_OFFS);
		orion_write(PCI_P2P_CONF, p2p);
	}
}

static void __init orion_pci_master_slave_enable(void)
{
	int bus_nr, dev_nr, func, reg;
	u32 val;

	bus_nr = orion_pci_local_bus_nr();
	dev_nr = orion_pci_local_dev_nr();
	func = PCI_CONF_FUNC_STAT_CMD;
	reg = PCI_CONF_REG_STAT_CMD;
	orion_pci_hw_rd_conf(bus_nr, dev_nr, func, reg, 4, &val);
	val |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	orion_pci_hw_wr_conf(bus_nr, dev_nr, func, reg, 4, val | 0x7);
}

static void __init orion_setup_pci_wins(struct mbus_dram_target_info *dram)
{
	u32 win_enable;
	int bus;
	int dev;
	int i;

	/*
	 * First, disable windows.
	 */
	win_enable = 0xffffffff;
	orion_write(PCI_BAR_ENABLE, win_enable);

	/*
	 * Setup windows for DDR banks.
	 */
	bus = orion_pci_local_bus_nr();
	dev = orion_pci_local_dev_nr();

	for (i = 0; i < dram->num_cs; i++) {
		struct mbus_dram_window *cs = dram->cs + i;
		u32 func = PCI_CONF_FUNC_BAR_CS(cs->cs_index);
		u32 reg;
		u32 val;

		/*
		 * Write DRAM bank base address register.
		 */
		reg = PCI_CONF_REG_BAR_LO_CS(cs->cs_index);
		orion_pci_hw_rd_conf(bus, dev, func, reg, 4, &val);
		val = (cs->base & 0xfffff000) | (val & 0xfff);
		orion_pci_hw_wr_conf(bus, dev, func, reg, 4, val);

		/*
		 * Write DRAM bank size register.
		 */
		reg = PCI_CONF_REG_BAR_HI_CS(cs->cs_index);
		orion_pci_hw_wr_conf(bus, dev, func, reg, 4, 0);
		orion_write(PCI_BAR_SIZE_DDR_CS(cs->cs_index),
				(cs->size - 1) & 0xfffff000);
		orion_write(PCI_BAR_REMAP_DDR_CS(cs->cs_index),
				cs->base & 0xfffff000);

		/*
		 * Enable decode window for this chip select.
		 */
		win_enable &= ~(1 << cs->cs_index);
	}

	/*
	 * Re-enable decode windows.
	 */
	orion_write(PCI_BAR_ENABLE, win_enable);

	/*
	 * Disable automatic update of address remaping when writing to BARs.
	 */
	orion_setbits(PCI_ADDR_DECODE_CTRL, 1);
}

static int __init pci_setup(struct pci_sys_data *sys)
{
	struct resource *res;

	/*
	 * Point PCI unit MBUS decode windows to DRAM space.
	 */
	orion_setup_pci_wins(&orion_mbus_dram_info);

	/*
	 * Master + Slave enable
	 */
	orion_pci_master_slave_enable();

	/*
	 * Force ordering
	 */
	orion_setbits(PCI_CMD, PCI_CMD_HOST_REORDER);

	/*
	 * Request resources
	 */
	res = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("pci_setup unable to alloc resources");

	/*
	 * IORESOURCE_IO
	 */
	res[0].name = "PCI I/O Space";
	res[0].flags = IORESOURCE_IO;
	res[0].start = ORION_PCI_IO_BUS_BASE;
	res[0].end = res[0].start + ORION_PCI_IO_SIZE - 1;
	if (request_resource(&ioport_resource, &res[0]))
		panic("Request PCI IO resource failed\n");
	sys->resource[0] = &res[0];

	/*
	 * IORESOURCE_MEM
	 */
	res[1].name = "PCI Memory Space";
	res[1].flags = IORESOURCE_MEM;
	res[1].start = ORION_PCI_MEM_PHYS_BASE;
	res[1].end = res[1].start + ORION_PCI_MEM_SIZE - 1;
	if (request_resource(&iomem_resource, &res[1]))
		panic("Request PCI Memory resource failed\n");
	sys->resource[1] = &res[1];

	sys->resource[2] = NULL;
	sys->io_offset = 0;

	return 1;
}


/*****************************************************************************
 * General PCIE + PCI
 ****************************************************************************/
int __init orion_pci_sys_setup(int nr, struct pci_sys_data *sys)
{
	int ret = 0;

	if (nr == 0) {
		orion_pcie_set_local_bus_nr(PCIE_BASE, sys->busnr);
		ret = pcie_setup(sys);
	} else if (nr == 1) {
		orion_pci_set_bus_nr(sys->busnr);
		ret = pci_setup(sys);
	}

	return ret;
}

struct pci_bus __init *orion_pci_sys_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_bus *bus;

	if (nr == 0) {
		bus = pci_scan_bus(sys->busnr, &pcie_ops, sys);
	} else if (nr == 1) {
		bus = pci_scan_bus(sys->busnr, &pci_ops, sys);
	} else {
		bus = NULL;
		BUG();
	}

	return bus;
}
