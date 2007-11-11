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
#include <asm/mach/pci.h>
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
#define PCIE_CTRL		ORION_PCIE_REG(0x1a00)
#define PCIE_STAT		ORION_PCIE_REG(0x1a04)
#define PCIE_DEV_ID		ORION_PCIE_REG(0x0000)
#define PCIE_CMD_STAT		ORION_PCIE_REG(0x0004)
#define PCIE_DEV_REV		ORION_PCIE_REG(0x0008)
#define PCIE_MASK		ORION_PCIE_REG(0x1910)
#define PCIE_CONF_ADDR		ORION_PCIE_REG(0x18f8)
#define PCIE_CONF_DATA		ORION_PCIE_REG(0x18fc)

/*
 * PCIE_STAT bits
 */
#define PCIE_STAT_LINK_DOWN		1
#define PCIE_STAT_BUS_OFFS		8
#define PCIE_STAT_BUS_MASK		(0xff << PCIE_STAT_BUS_OFFS)
#define PCIE_STAT_DEV_OFFS		20
#define PCIE_STAT_DEV_MASK		(0x1f << PCIE_STAT_DEV_OFFS)

/*
 * PCIE_CONF_ADDR bits
 */
#define PCIE_CONF_REG(r)		((((r) & 0xf00) << 24) | ((r) & 0xfc))
#define PCIE_CONF_FUNC(f)		(((f) & 0x3) << 8)
#define PCIE_CONF_DEV(d)		(((d) & 0x1f) << 11)
#define PCIE_CONF_BUS(b)		(((b) & 0xff) << 16)
#define PCIE_CONF_ADDR_EN		(1 << 31)

/*
 * PCIE config cycles are done by programming the PCIE_CONF_ADDR register
 * and then reading the PCIE_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */
static DEFINE_SPINLOCK(orion_pcie_lock);

void orion_pcie_id(u32 *dev, u32 *rev)
{
	*dev = orion_read(PCIE_DEV_ID) >> 16;
	*rev = orion_read(PCIE_DEV_REV) & 0xff;
}

u32 orion_pcie_local_bus_nr(void)
{
	u32 stat = orion_read(PCIE_STAT);
	return((stat & PCIE_STAT_BUS_MASK) >> PCIE_STAT_BUS_OFFS);
}

static u32 orion_pcie_local_dev_nr(void)
{
	u32 stat = orion_read(PCIE_STAT);
	return((stat & PCIE_STAT_DEV_MASK) >> PCIE_STAT_DEV_OFFS);
}

static u32 orion_pcie_no_link(void)
{
	u32 stat = orion_read(PCIE_STAT);
	return(stat & PCIE_STAT_LINK_DOWN);
}

static void orion_pcie_set_bus_nr(int nr)
{
	orion_clrbits(PCIE_STAT, PCIE_STAT_BUS_MASK);
	orion_setbits(PCIE_STAT, nr << PCIE_STAT_BUS_OFFS);
}

static void orion_pcie_master_slave_enable(void)
{
	orion_setbits(PCIE_CMD_STAT, PCI_COMMAND_MASTER |
					  PCI_COMMAND_IO |
					  PCI_COMMAND_MEMORY);
}

static void orion_pcie_enable_interrupts(void)
{
	/*
	 * Enable interrupts lines
	 * INTA[24] INTB[25] INTC[26] INTD[27]
	 */
	orion_setbits(PCIE_MASK, 0xf<<24);
}

static int orion_pcie_valid_config(u32 bus, u32 dev)
{
	/*
	 * Don't go out when trying to access --
	 * 1. our own device
	 * 2. where there's no device connected (no link)
	 * 3. nonexisting devices on local bus
	 */

	if ((orion_pcie_local_bus_nr() == bus) &&
	   (orion_pcie_local_dev_nr() == dev))
		return 0;

	if (orion_pcie_no_link())
		return 0;

	if (bus == orion_pcie_local_bus_nr())
		if (((orion_pcie_local_dev_nr() == 0) && (dev != 1)) ||
		   ((orion_pcie_local_dev_nr() != 0) && (dev != 0)))
		return 0;

	return 1;
}

static int orion_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
						int size, u32 *val)
{
	unsigned long flags;
	unsigned int dev, rev, pcie_addr;

	if (orion_pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	spin_lock_irqsave(&orion_pcie_lock, flags);

	orion_write(PCIE_CONF_ADDR, PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_DEV(PCI_SLOT(devfn)) |
			PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
			PCIE_CONF_REG(where) | PCIE_CONF_ADDR_EN);

	orion_pcie_id(&dev, &rev);
	if (dev == MV88F5181_DEV_ID || dev == MV88F5182_DEV_ID) {
		/* extended register space */
		pcie_addr = ORION_PCIE_WA_BASE;
		pcie_addr |= PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_DEV(PCI_SLOT(devfn)) |
			PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
			PCIE_CONF_REG(where);
		*val = orion_read(pcie_addr);
	} else
		*val = orion_read(PCIE_CONF_DATA);

	if (size == 1)
		*val = (*val >> (8*(where & 0x3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8*(where & 0x3))) & 0xffff;

	spin_unlock_irqrestore(&orion_pcie_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}


static int orion_pcie_wr_conf(struct pci_bus *bus, u32 devfn, int where,
						int size, u32 val)
{
	unsigned long flags;
	int ret;

	if (orion_pcie_valid_config(bus->number, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&orion_pcie_lock, flags);

	ret = PCIBIOS_SUCCESSFUL;

	orion_write(PCIE_CONF_ADDR, PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_DEV(PCI_SLOT(devfn)) |
			PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
			PCIE_CONF_REG(where) | PCIE_CONF_ADDR_EN);

	if (size == 4) {
		__raw_writel(val, PCIE_CONF_DATA);
	} else if (size == 2) {
		__raw_writew(val, PCIE_CONF_DATA + (where & 0x3));
	} else if (size == 1) {
		__raw_writeb(val, PCIE_CONF_DATA + (where & 0x3));
	} else {
		ret = PCIBIOS_BAD_REGISTER_NUMBER;
	}

	spin_unlock_irqrestore(&orion_pcie_lock, flags);

	return ret;
}

struct pci_ops orion_pcie_ops = {
	.read = orion_pcie_rd_conf,
	.write = orion_pcie_wr_conf,
};


static int orion_pcie_setup(struct pci_sys_data *sys)
{
	struct resource *res;

	/*
	 * Master + Slave enable
	 */
	orion_pcie_master_slave_enable();

	/*
	 * Enable interrupts lines A-D
	 */
	orion_pcie_enable_interrupts();

	/*
	 * Request resource
	 */
	res = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("orion_pci_setup unable to alloc resources");

	/*
	 * IORESOURCE_IO
	 */
	res[0].name = "PCI-EX I/O Space";
	res[0].flags = IORESOURCE_IO;
	res[0].start = ORION_PCIE_IO_REMAP;
	res[0].end = res[0].start + ORION_PCIE_IO_SIZE - 1;
	if (request_resource(&ioport_resource, &res[0]))
		panic("Request PCIE IO resource failed\n");
	sys->resource[0] = &res[0];

	/*
	 * IORESOURCE_MEM
	 */
	res[1].name = "PCI-EX Memory Space";
	res[1].flags = IORESOURCE_MEM;
	res[1].start = ORION_PCIE_MEM_BASE;
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
 * PCI config cycles are done by programming the PCI_CONF_ADDR register
 * and then reading the PCI_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */
static DEFINE_SPINLOCK(orion_pci_lock);

u32 orion_pci_local_bus_nr(void)
{
	u32 conf = orion_read(PCI_P2P_CONF);
	return((conf & PCI_P2P_BUS_MASK) >> PCI_P2P_BUS_OFFS);
}

u32 orion_pci_local_dev_nr(void)
{
	u32 conf = orion_read(PCI_P2P_CONF);
	return((conf & PCI_P2P_DEV_MASK) >> PCI_P2P_DEV_OFFS);
}

int orion_pci_hw_rd_conf(u32 bus, u32 dev, u32 func,
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

int orion_pci_hw_wr_conf(u32 bus, u32 dev, u32 func,
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

struct pci_ops orion_pci_ops = {
	.read = orion_pci_rd_conf,
	.write = orion_pci_wr_conf,
};

static void orion_pci_set_bus_nr(int nr)
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

static void orion_pci_master_slave_enable(void)
{
	u32 bus_nr, dev_nr, func, reg, val;

	bus_nr = orion_pci_local_bus_nr();
	dev_nr = orion_pci_local_dev_nr();
	func = PCI_CONF_FUNC_STAT_CMD;
	reg = PCI_CONF_REG_STAT_CMD;
	orion_pci_hw_rd_conf(bus_nr, dev_nr, func, reg, 4, &val);
	val |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	orion_pci_hw_wr_conf(bus_nr, dev_nr, func, reg, 4, val | 0x7);
}

static int orion_pci_setup(struct pci_sys_data *sys)
{
	struct resource *res;

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
		panic("orion_pci_setup unable to alloc resources");

	/*
	 * IORESOURCE_IO
	 */
	res[0].name = "PCI I/O Space";
	res[0].flags = IORESOURCE_IO;
	res[0].start = ORION_PCI_IO_REMAP;
	res[0].end = res[0].start + ORION_PCI_IO_SIZE - 1;
	if (request_resource(&ioport_resource, &res[0]))
		panic("Request PCI IO resource failed\n");
	sys->resource[0] = &res[0];

	/*
	 * IORESOURCE_MEM
	 */
	res[1].name = "PCI Memory Space";
	res[1].flags = IORESOURCE_MEM;
	res[1].start = ORION_PCI_MEM_BASE;
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
int orion_pci_sys_setup(int nr, struct pci_sys_data *sys)
{
	int ret = 0;

	if (nr == 0) {
		/*
		 * PCIE setup
		 */
		orion_pcie_set_bus_nr(0);
		ret = orion_pcie_setup(sys);
	} else if (nr == 1) {
		/*
		 * PCI setup
		 */
		ret = orion_pci_setup(sys);
	}

	return ret;
}

struct pci_bus *orion_pci_sys_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_ops *ops;
	struct pci_bus *bus;


	if (nr == 0) {
		u32 pci_bus;
		/*
		 * PCIE scan
		 */
		ops = &orion_pcie_ops;
		bus = pci_scan_bus(sys->busnr, ops, sys);
		/*
		 * Set local PCI bus number to follow PCIE bridges (if any)
		 */
		pci_bus	= bus->number + bus->subordinate - bus->secondary + 1;
		orion_pci_set_bus_nr(pci_bus);
	} else if (nr == 1) {
		/*
		 * PCI scan
		 */
		ops = &orion_pci_ops;
		bus = pci_scan_bus(sys->busnr, ops, sys);
	} else {
		BUG();
		bus = NULL;
	}

	return bus;
}
