// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Marvell Armada 370 and Armada XP SoCs
 *
 * Author: Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/msi.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>

#include "../pci.h"

/*
 * PCIe unit register offsets.
 */
#define PCIE_DEV_ID_OFF		0x0000
#define PCIE_CMD_OFF		0x0004
#define PCIE_DEV_REV_OFF	0x0008
#define PCIE_BAR_LO_OFF(n)	(0x0010 + ((n) << 3))
#define PCIE_BAR_HI_OFF(n)	(0x0014 + ((n) << 3))
#define PCIE_CAP_PCIEXP		0x0060
#define PCIE_HEADER_LOG_4_OFF	0x0128
#define PCIE_BAR_CTRL_OFF(n)	(0x1804 + (((n) - 1) * 4))
#define PCIE_WIN04_CTRL_OFF(n)	(0x1820 + ((n) << 4))
#define PCIE_WIN04_BASE_OFF(n)	(0x1824 + ((n) << 4))
#define PCIE_WIN04_REMAP_OFF(n)	(0x182c + ((n) << 4))
#define PCIE_WIN5_CTRL_OFF	0x1880
#define PCIE_WIN5_BASE_OFF	0x1884
#define PCIE_WIN5_REMAP_OFF	0x188c
#define PCIE_CONF_ADDR_OFF	0x18f8
#define  PCIE_CONF_ADDR_EN		0x80000000
#define  PCIE_CONF_REG(r)		((((r) & 0xf00) << 16) | ((r) & 0xfc))
#define  PCIE_CONF_BUS(b)		(((b) & 0xff) << 16)
#define  PCIE_CONF_DEV(d)		(((d) & 0x1f) << 11)
#define  PCIE_CONF_FUNC(f)		(((f) & 0x7) << 8)
#define  PCIE_CONF_ADDR(bus, devfn, where) \
	(PCIE_CONF_BUS(bus) | PCIE_CONF_DEV(PCI_SLOT(devfn))    | \
	 PCIE_CONF_FUNC(PCI_FUNC(devfn)) | PCIE_CONF_REG(where) | \
	 PCIE_CONF_ADDR_EN)
#define PCIE_CONF_DATA_OFF	0x18fc
#define PCIE_MASK_OFF		0x1910
#define  PCIE_MASK_ENABLE_INTS          0x0f000000
#define PCIE_CTRL_OFF		0x1a00
#define  PCIE_CTRL_X1_MODE		0x0001
#define PCIE_STAT_OFF		0x1a04
#define  PCIE_STAT_BUS                  0xff00
#define  PCIE_STAT_DEV                  0x1f0000
#define  PCIE_STAT_LINK_DOWN		BIT(0)
#define PCIE_RC_RTSTA		0x1a14
#define PCIE_DEBUG_CTRL         0x1a60
#define  PCIE_DEBUG_SOFT_RESET		BIT(20)

enum {
	PCISWCAP = PCI_BRIDGE_CONTROL + 2,
	PCISWCAP_EXP_LIST_ID	= PCISWCAP + PCI_CAP_LIST_ID,
	PCISWCAP_EXP_DEVCAP	= PCISWCAP + PCI_EXP_DEVCAP,
	PCISWCAP_EXP_DEVCTL	= PCISWCAP + PCI_EXP_DEVCTL,
	PCISWCAP_EXP_LNKCAP	= PCISWCAP + PCI_EXP_LNKCAP,
	PCISWCAP_EXP_LNKCTL	= PCISWCAP + PCI_EXP_LNKCTL,
	PCISWCAP_EXP_SLTCAP	= PCISWCAP + PCI_EXP_SLTCAP,
	PCISWCAP_EXP_SLTCTL	= PCISWCAP + PCI_EXP_SLTCTL,
	PCISWCAP_EXP_RTCTL	= PCISWCAP + PCI_EXP_RTCTL,
	PCISWCAP_EXP_RTSTA	= PCISWCAP + PCI_EXP_RTSTA,
	PCISWCAP_EXP_DEVCAP2	= PCISWCAP + PCI_EXP_DEVCAP2,
	PCISWCAP_EXP_DEVCTL2	= PCISWCAP + PCI_EXP_DEVCTL2,
	PCISWCAP_EXP_LNKCAP2	= PCISWCAP + PCI_EXP_LNKCAP2,
	PCISWCAP_EXP_LNKCTL2	= PCISWCAP + PCI_EXP_LNKCTL2,
	PCISWCAP_EXP_SLTCAP2	= PCISWCAP + PCI_EXP_SLTCAP2,
	PCISWCAP_EXP_SLTCTL2	= PCISWCAP + PCI_EXP_SLTCTL2,
};

/* PCI configuration space of a PCI-to-PCI bridge */
struct mvebu_sw_pci_bridge {
	u16 vendor;
	u16 device;
	u16 command;
	u16 status;
	u16 class;
	u8 interface;
	u8 revision;
	u8 bist;
	u8 header_type;
	u8 latency_timer;
	u8 cache_line_size;
	u32 bar[2];
	u8 primary_bus;
	u8 secondary_bus;
	u8 subordinate_bus;
	u8 secondary_latency_timer;
	u8 iobase;
	u8 iolimit;
	u16 secondary_status;
	u16 membase;
	u16 memlimit;
	u16 iobaseupper;
	u16 iolimitupper;
	u32 romaddr;
	u8 intline;
	u8 intpin;
	u16 bridgectrl;

	/* PCI express capability */
	u32 pcie_sltcap;
	u16 pcie_devctl;
	u16 pcie_rtctl;
};

struct mvebu_pcie_port;

/* Structure representing all PCIe interfaces */
struct mvebu_pcie {
	struct platform_device *pdev;
	struct mvebu_pcie_port *ports;
	struct msi_controller *msi;
	struct list_head resources;
	struct resource io;
	struct resource realio;
	struct resource mem;
	struct resource busn;
	int nports;
};

struct mvebu_pcie_window {
	phys_addr_t base;
	phys_addr_t remap;
	size_t size;
};

/* Structure representing one PCIe interface */
struct mvebu_pcie_port {
	char *name;
	void __iomem *base;
	u32 port;
	u32 lane;
	int devfn;
	unsigned int mem_target;
	unsigned int mem_attr;
	unsigned int io_target;
	unsigned int io_attr;
	struct clk *clk;
	struct gpio_desc *reset_gpio;
	char *reset_name;
	struct mvebu_sw_pci_bridge bridge;
	struct device_node *dn;
	struct mvebu_pcie *pcie;
	struct mvebu_pcie_window memwin;
	struct mvebu_pcie_window iowin;
	u32 saved_pcie_stat;
};

static inline void mvebu_writel(struct mvebu_pcie_port *port, u32 val, u32 reg)
{
	writel(val, port->base + reg);
}

static inline u32 mvebu_readl(struct mvebu_pcie_port *port, u32 reg)
{
	return readl(port->base + reg);
}

static inline bool mvebu_has_ioport(struct mvebu_pcie_port *port)
{
	return port->io_target != -1 && port->io_attr != -1;
}

static bool mvebu_pcie_link_up(struct mvebu_pcie_port *port)
{
	return !(mvebu_readl(port, PCIE_STAT_OFF) & PCIE_STAT_LINK_DOWN);
}

static void mvebu_pcie_set_local_bus_nr(struct mvebu_pcie_port *port, int nr)
{
	u32 stat;

	stat = mvebu_readl(port, PCIE_STAT_OFF);
	stat &= ~PCIE_STAT_BUS;
	stat |= nr << 8;
	mvebu_writel(port, stat, PCIE_STAT_OFF);
}

static void mvebu_pcie_set_local_dev_nr(struct mvebu_pcie_port *port, int nr)
{
	u32 stat;

	stat = mvebu_readl(port, PCIE_STAT_OFF);
	stat &= ~PCIE_STAT_DEV;
	stat |= nr << 16;
	mvebu_writel(port, stat, PCIE_STAT_OFF);
}

/*
 * Setup PCIE BARs and Address Decode Wins:
 * BAR[0,2] -> disabled, BAR[1] -> covers all DRAM banks
 * WIN[0-3] -> DRAM bank[0-3]
 */
static void mvebu_pcie_setup_wins(struct mvebu_pcie_port *port)
{
	const struct mbus_dram_target_info *dram;
	u32 size;
	int i;

	dram = mv_mbus_dram_info();

	/* First, disable and clear BARs and windows. */
	for (i = 1; i < 3; i++) {
		mvebu_writel(port, 0, PCIE_BAR_CTRL_OFF(i));
		mvebu_writel(port, 0, PCIE_BAR_LO_OFF(i));
		mvebu_writel(port, 0, PCIE_BAR_HI_OFF(i));
	}

	for (i = 0; i < 5; i++) {
		mvebu_writel(port, 0, PCIE_WIN04_CTRL_OFF(i));
		mvebu_writel(port, 0, PCIE_WIN04_BASE_OFF(i));
		mvebu_writel(port, 0, PCIE_WIN04_REMAP_OFF(i));
	}

	mvebu_writel(port, 0, PCIE_WIN5_CTRL_OFF);
	mvebu_writel(port, 0, PCIE_WIN5_BASE_OFF);
	mvebu_writel(port, 0, PCIE_WIN5_REMAP_OFF);

	/* Setup windows for DDR banks.  Count total DDR size on the fly. */
	size = 0;
	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		mvebu_writel(port, cs->base & 0xffff0000,
			     PCIE_WIN04_BASE_OFF(i));
		mvebu_writel(port, 0, PCIE_WIN04_REMAP_OFF(i));
		mvebu_writel(port,
			     ((cs->size - 1) & 0xffff0000) |
			     (cs->mbus_attr << 8) |
			     (dram->mbus_dram_target_id << 4) | 1,
			     PCIE_WIN04_CTRL_OFF(i));

		size += cs->size;
	}

	/* Round up 'size' to the nearest power of two. */
	if ((size & (size - 1)) != 0)
		size = 1 << fls(size);

	/* Setup BAR[1] to all DRAM banks. */
	mvebu_writel(port, dram->cs[0].base, PCIE_BAR_LO_OFF(1));
	mvebu_writel(port, 0, PCIE_BAR_HI_OFF(1));
	mvebu_writel(port, ((size - 1) & 0xffff0000) | 1,
		     PCIE_BAR_CTRL_OFF(1));
}

static void mvebu_pcie_setup_hw(struct mvebu_pcie_port *port)
{
	u32 cmd, mask;

	/* Point PCIe unit MBUS decode windows to DRAM space. */
	mvebu_pcie_setup_wins(port);

	/* Master + slave enable. */
	cmd = mvebu_readl(port, PCIE_CMD_OFF);
	cmd |= PCI_COMMAND_IO;
	cmd |= PCI_COMMAND_MEMORY;
	cmd |= PCI_COMMAND_MASTER;
	mvebu_writel(port, cmd, PCIE_CMD_OFF);

	/* Enable interrupt lines A-D. */
	mask = mvebu_readl(port, PCIE_MASK_OFF);
	mask |= PCIE_MASK_ENABLE_INTS;
	mvebu_writel(port, mask, PCIE_MASK_OFF);
}

static int mvebu_pcie_hw_rd_conf(struct mvebu_pcie_port *port,
				 struct pci_bus *bus,
				 u32 devfn, int where, int size, u32 *val)
{
	void __iomem *conf_data = port->base + PCIE_CONF_DATA_OFF;

	mvebu_writel(port, PCIE_CONF_ADDR(bus->number, devfn, where),
		     PCIE_CONF_ADDR_OFF);

	switch (size) {
	case 1:
		*val = readb_relaxed(conf_data + (where & 3));
		break;
	case 2:
		*val = readw_relaxed(conf_data + (where & 2));
		break;
	case 4:
		*val = readl_relaxed(conf_data);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int mvebu_pcie_hw_wr_conf(struct mvebu_pcie_port *port,
				 struct pci_bus *bus,
				 u32 devfn, int where, int size, u32 val)
{
	void __iomem *conf_data = port->base + PCIE_CONF_DATA_OFF;

	mvebu_writel(port, PCIE_CONF_ADDR(bus->number, devfn, where),
		     PCIE_CONF_ADDR_OFF);

	switch (size) {
	case 1:
		writeb(val, conf_data + (where & 3));
		break;
	case 2:
		writew(val, conf_data + (where & 2));
		break;
	case 4:
		writel(val, conf_data);
		break;
	default:
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

/*
 * Remove windows, starting from the largest ones to the smallest
 * ones.
 */
static void mvebu_pcie_del_windows(struct mvebu_pcie_port *port,
				   phys_addr_t base, size_t size)
{
	while (size) {
		size_t sz = 1 << (fls(size) - 1);

		mvebu_mbus_del_window(base, sz);
		base += sz;
		size -= sz;
	}
}

/*
 * MBus windows can only have a power of two size, but PCI BARs do not
 * have this constraint. Therefore, we have to split the PCI BAR into
 * areas each having a power of two size. We start from the largest
 * one (i.e highest order bit set in the size).
 */
static void mvebu_pcie_add_windows(struct mvebu_pcie_port *port,
				   unsigned int target, unsigned int attribute,
				   phys_addr_t base, size_t size,
				   phys_addr_t remap)
{
	size_t size_mapped = 0;

	while (size) {
		size_t sz = 1 << (fls(size) - 1);
		int ret;

		ret = mvebu_mbus_add_window_remap_by_id(target, attribute, base,
							sz, remap);
		if (ret) {
			phys_addr_t end = base + sz - 1;

			dev_err(&port->pcie->pdev->dev,
				"Could not create MBus window at [mem %pa-%pa]: %d\n",
				&base, &end, ret);
			mvebu_pcie_del_windows(port, base - size_mapped,
					       size_mapped);
			return;
		}

		size -= sz;
		size_mapped += sz;
		base += sz;
		if (remap != MVEBU_MBUS_NO_REMAP)
			remap += sz;
	}
}

static void mvebu_pcie_set_window(struct mvebu_pcie_port *port,
				  unsigned int target, unsigned int attribute,
				  const struct mvebu_pcie_window *desired,
				  struct mvebu_pcie_window *cur)
{
	if (desired->base == cur->base && desired->remap == cur->remap &&
	    desired->size == cur->size)
		return;

	if (cur->size != 0) {
		mvebu_pcie_del_windows(port, cur->base, cur->size);
		cur->size = 0;
		cur->base = 0;

		/*
		 * If something tries to change the window while it is enabled
		 * the change will not be done atomically. That would be
		 * difficult to do in the general case.
		 */
	}

	if (desired->size == 0)
		return;

	mvebu_pcie_add_windows(port, target, attribute, desired->base,
			       desired->size, desired->remap);
	*cur = *desired;
}

static void mvebu_pcie_handle_iobase_change(struct mvebu_pcie_port *port)
{
	struct mvebu_pcie_window desired = {};

	/* Are the new iobase/iolimit values invalid? */
	if (port->bridge.iolimit < port->bridge.iobase ||
	    port->bridge.iolimitupper < port->bridge.iobaseupper ||
	    !(port->bridge.command & PCI_COMMAND_IO)) {
		mvebu_pcie_set_window(port, port->io_target, port->io_attr,
				      &desired, &port->iowin);
		return;
	}

	if (!mvebu_has_ioport(port)) {
		dev_WARN(&port->pcie->pdev->dev,
			 "Attempt to set IO when IO is disabled\n");
		return;
	}

	/*
	 * We read the PCI-to-PCI bridge emulated registers, and
	 * calculate the base address and size of the address decoding
	 * window to setup, according to the PCI-to-PCI bridge
	 * specifications. iobase is the bus address, port->iowin_base
	 * is the CPU address.
	 */
	desired.remap = ((port->bridge.iobase & 0xF0) << 8) |
			(port->bridge.iobaseupper << 16);
	desired.base = port->pcie->io.start + desired.remap;
	desired.size = ((0xFFF | ((port->bridge.iolimit & 0xF0) << 8) |
			 (port->bridge.iolimitupper << 16)) -
			desired.remap) +
		       1;

	mvebu_pcie_set_window(port, port->io_target, port->io_attr, &desired,
			      &port->iowin);
}

static void mvebu_pcie_handle_membase_change(struct mvebu_pcie_port *port)
{
	struct mvebu_pcie_window desired = {.remap = MVEBU_MBUS_NO_REMAP};

	/* Are the new membase/memlimit values invalid? */
	if (port->bridge.memlimit < port->bridge.membase ||
	    !(port->bridge.command & PCI_COMMAND_MEMORY)) {
		mvebu_pcie_set_window(port, port->mem_target, port->mem_attr,
				      &desired, &port->memwin);
		return;
	}

	/*
	 * We read the PCI-to-PCI bridge emulated registers, and
	 * calculate the base address and size of the address decoding
	 * window to setup, according to the PCI-to-PCI bridge
	 * specifications.
	 */
	desired.base = ((port->bridge.membase & 0xFFF0) << 16);
	desired.size = (((port->bridge.memlimit & 0xFFF0) << 16) | 0xFFFFF) -
		       desired.base + 1;

	mvebu_pcie_set_window(port, port->mem_target, port->mem_attr, &desired,
			      &port->memwin);
}

/*
 * Initialize the configuration space of the PCI-to-PCI bridge
 * associated with the given PCIe interface.
 */
static void mvebu_sw_pci_bridge_init(struct mvebu_pcie_port *port)
{
	struct mvebu_sw_pci_bridge *bridge = &port->bridge;

	memset(bridge, 0, sizeof(struct mvebu_sw_pci_bridge));

	bridge->class = PCI_CLASS_BRIDGE_PCI;
	bridge->vendor = PCI_VENDOR_ID_MARVELL;
	bridge->device = mvebu_readl(port, PCIE_DEV_ID_OFF) >> 16;
	bridge->revision = mvebu_readl(port, PCIE_DEV_REV_OFF) & 0xff;
	bridge->header_type = PCI_HEADER_TYPE_BRIDGE;
	bridge->cache_line_size = 0x10;

	/* We support 32 bits I/O addressing */
	bridge->iobase = PCI_IO_RANGE_TYPE_32;
	bridge->iolimit = PCI_IO_RANGE_TYPE_32;

	/* Add capabilities */
	bridge->status = PCI_STATUS_CAP_LIST;
}

/*
 * Read the configuration space of the PCI-to-PCI bridge associated to
 * the given PCIe interface.
 */
static int mvebu_sw_pci_bridge_read(struct mvebu_pcie_port *port,
				  unsigned int where, int size, u32 *value)
{
	struct mvebu_sw_pci_bridge *bridge = &port->bridge;

	switch (where & ~3) {
	case PCI_VENDOR_ID:
		*value = bridge->device << 16 | bridge->vendor;
		break;

	case PCI_COMMAND:
		*value = bridge->command | bridge->status << 16;
		break;

	case PCI_CLASS_REVISION:
		*value = bridge->class << 16 | bridge->interface << 8 |
			 bridge->revision;
		break;

	case PCI_CACHE_LINE_SIZE:
		*value = bridge->bist << 24 | bridge->header_type << 16 |
			 bridge->latency_timer << 8 | bridge->cache_line_size;
		break;

	case PCI_BASE_ADDRESS_0 ... PCI_BASE_ADDRESS_1:
		*value = bridge->bar[((where & ~3) - PCI_BASE_ADDRESS_0) / 4];
		break;

	case PCI_PRIMARY_BUS:
		*value = (bridge->secondary_latency_timer << 24 |
			  bridge->subordinate_bus         << 16 |
			  bridge->secondary_bus           <<  8 |
			  bridge->primary_bus);
		break;

	case PCI_IO_BASE:
		if (!mvebu_has_ioport(port))
			*value = bridge->secondary_status << 16;
		else
			*value = (bridge->secondary_status << 16 |
				  bridge->iolimit          <<  8 |
				  bridge->iobase);
		break;

	case PCI_MEMORY_BASE:
		*value = (bridge->memlimit << 16 | bridge->membase);
		break;

	case PCI_PREF_MEMORY_BASE:
		*value = 0;
		break;

	case PCI_IO_BASE_UPPER16:
		*value = (bridge->iolimitupper << 16 | bridge->iobaseupper);
		break;

	case PCI_CAPABILITY_LIST:
		*value = PCISWCAP;
		break;

	case PCI_ROM_ADDRESS1:
		*value = 0;
		break;

	case PCI_INTERRUPT_LINE:
		/* LINE PIN MIN_GNT MAX_LAT */
		*value = 0;
		break;

	case PCISWCAP_EXP_LIST_ID:
		/* Set PCIe v2, root port, slot support */
		*value = (PCI_EXP_TYPE_ROOT_PORT << 4 | 2 |
			  PCI_EXP_FLAGS_SLOT) << 16 | PCI_CAP_ID_EXP;
		break;

	case PCISWCAP_EXP_DEVCAP:
		*value = mvebu_readl(port, PCIE_CAP_PCIEXP + PCI_EXP_DEVCAP);
		break;

	case PCISWCAP_EXP_DEVCTL:
		*value = mvebu_readl(port, PCIE_CAP_PCIEXP + PCI_EXP_DEVCTL) &
				 ~(PCI_EXP_DEVCTL_URRE | PCI_EXP_DEVCTL_FERE |
				   PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_CERE);
		*value |= bridge->pcie_devctl;
		break;

	case PCISWCAP_EXP_LNKCAP:
		/*
		 * PCIe requires the clock power management capability to be
		 * hard-wired to zero for downstream ports
		 */
		*value = mvebu_readl(port, PCIE_CAP_PCIEXP + PCI_EXP_LNKCAP) &
			 ~PCI_EXP_LNKCAP_CLKPM;
		break;

	case PCISWCAP_EXP_LNKCTL:
		*value = mvebu_readl(port, PCIE_CAP_PCIEXP + PCI_EXP_LNKCTL);
		break;

	case PCISWCAP_EXP_SLTCAP:
		*value = bridge->pcie_sltcap;
		break;

	case PCISWCAP_EXP_SLTCTL:
		*value = PCI_EXP_SLTSTA_PDS << 16;
		break;

	case PCISWCAP_EXP_RTCTL:
		*value = bridge->pcie_rtctl;
		break;

	case PCISWCAP_EXP_RTSTA:
		*value = mvebu_readl(port, PCIE_RC_RTSTA);
		break;

	/* PCIe requires the v2 fields to be hard-wired to zero */
	case PCISWCAP_EXP_DEVCAP2:
	case PCISWCAP_EXP_DEVCTL2:
	case PCISWCAP_EXP_LNKCAP2:
	case PCISWCAP_EXP_LNKCTL2:
	case PCISWCAP_EXP_SLTCAP2:
	case PCISWCAP_EXP_SLTCTL2:
	default:
		/*
		 * PCI defines configuration read accesses to reserved or
		 * unimplemented registers to read as zero and complete
		 * normally.
		 */
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	if (size == 2)
		*value = (*value >> (8 * (where & 3))) & 0xffff;
	else if (size == 1)
		*value = (*value >> (8 * (where & 3))) & 0xff;

	return PCIBIOS_SUCCESSFUL;
}

/* Write to the PCI-to-PCI bridge configuration space */
static int mvebu_sw_pci_bridge_write(struct mvebu_pcie_port *port,
				     unsigned int where, int size, u32 value)
{
	struct mvebu_sw_pci_bridge *bridge = &port->bridge;
	u32 mask, reg;
	int err;

	if (size == 4)
		mask = 0x0;
	else if (size == 2)
		mask = ~(0xffff << ((where & 3) * 8));
	else if (size == 1)
		mask = ~(0xff << ((where & 3) * 8));
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	err = mvebu_sw_pci_bridge_read(port, where & ~3, 4, &reg);
	if (err)
		return err;

	value = (reg & mask) | value << ((where & 3) * 8);

	switch (where & ~3) {
	case PCI_COMMAND:
	{
		u32 old = bridge->command;

		if (!mvebu_has_ioport(port))
			value &= ~PCI_COMMAND_IO;

		bridge->command = value & 0xffff;
		if ((old ^ bridge->command) & PCI_COMMAND_IO)
			mvebu_pcie_handle_iobase_change(port);
		if ((old ^ bridge->command) & PCI_COMMAND_MEMORY)
			mvebu_pcie_handle_membase_change(port);
		break;
	}

	case PCI_BASE_ADDRESS_0 ... PCI_BASE_ADDRESS_1:
		bridge->bar[((where & ~3) - PCI_BASE_ADDRESS_0) / 4] = value;
		break;

	case PCI_IO_BASE:
		/*
		 * We also keep bit 1 set, it is a read-only bit that
		 * indicates we support 32 bits addressing for the
		 * I/O
		 */
		bridge->iobase = (value & 0xff) | PCI_IO_RANGE_TYPE_32;
		bridge->iolimit = ((value >> 8) & 0xff) | PCI_IO_RANGE_TYPE_32;
		mvebu_pcie_handle_iobase_change(port);
		break;

	case PCI_MEMORY_BASE:
		bridge->membase = value & 0xffff;
		bridge->memlimit = value >> 16;
		mvebu_pcie_handle_membase_change(port);
		break;

	case PCI_IO_BASE_UPPER16:
		bridge->iobaseupper = value & 0xffff;
		bridge->iolimitupper = value >> 16;
		mvebu_pcie_handle_iobase_change(port);
		break;

	case PCI_PRIMARY_BUS:
		bridge->primary_bus             = value & 0xff;
		bridge->secondary_bus           = (value >> 8) & 0xff;
		bridge->subordinate_bus         = (value >> 16) & 0xff;
		bridge->secondary_latency_timer = (value >> 24) & 0xff;
		mvebu_pcie_set_local_bus_nr(port, bridge->secondary_bus);
		break;

	case PCISWCAP_EXP_DEVCTL:
		/*
		 * Armada370 data says these bits must always
		 * be zero when in root complex mode.
		 */
		value &= ~(PCI_EXP_DEVCTL_URRE | PCI_EXP_DEVCTL_FERE |
			   PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_CERE);

		/*
		 * If the mask is 0xffff0000, then we only want to write
		 * the device control register, rather than clearing the
		 * RW1C bits in the device status register.  Mask out the
		 * status register bits.
		 */
		if (mask == 0xffff0000)
			value &= 0xffff;

		mvebu_writel(port, value, PCIE_CAP_PCIEXP + PCI_EXP_DEVCTL);
		break;

	case PCISWCAP_EXP_LNKCTL:
		/*
		 * If we don't support CLKREQ, we must ensure that the
		 * CLKREQ enable bit always reads zero.  Since we haven't
		 * had this capability, and it's dependent on board wiring,
		 * disable it for the time being.
		 */
		value &= ~PCI_EXP_LNKCTL_CLKREQ_EN;

		/*
		 * If the mask is 0xffff0000, then we only want to write
		 * the link control register, rather than clearing the
		 * RW1C bits in the link status register.  Mask out the
		 * RW1C status register bits.
		 */
		if (mask == 0xffff0000)
			value &= ~((PCI_EXP_LNKSTA_LABS |
				    PCI_EXP_LNKSTA_LBMS) << 16);

		mvebu_writel(port, value, PCIE_CAP_PCIEXP + PCI_EXP_LNKCTL);
		break;

	case PCISWCAP_EXP_RTSTA:
		mvebu_writel(port, value, PCIE_RC_RTSTA);
		break;

	default:
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static inline struct mvebu_pcie *sys_to_pcie(struct pci_sys_data *sys)
{
	return sys->private_data;
}

static struct mvebu_pcie_port *mvebu_pcie_find_port(struct mvebu_pcie *pcie,
						    struct pci_bus *bus,
						    int devfn)
{
	int i;

	for (i = 0; i < pcie->nports; i++) {
		struct mvebu_pcie_port *port = &pcie->ports[i];

		if (bus->number == 0 && port->devfn == devfn)
			return port;
		if (bus->number != 0 &&
		    bus->number >= port->bridge.secondary_bus &&
		    bus->number <= port->bridge.subordinate_bus)
			return port;
	}

	return NULL;
}

/* PCI configuration space write function */
static int mvebu_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			      int where, int size, u32 val)
{
	struct mvebu_pcie *pcie = bus->sysdata;
	struct mvebu_pcie_port *port;
	int ret;

	port = mvebu_pcie_find_port(pcie, bus, devfn);
	if (!port)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Access the emulated PCI-to-PCI bridge */
	if (bus->number == 0)
		return mvebu_sw_pci_bridge_write(port, where, size, val);

	if (!mvebu_pcie_link_up(port))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Access the real PCIe interface */
	ret = mvebu_pcie_hw_wr_conf(port, bus, devfn,
				    where, size, val);

	return ret;
}

/* PCI configuration space read function */
static int mvebu_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			      int size, u32 *val)
{
	struct mvebu_pcie *pcie = bus->sysdata;
	struct mvebu_pcie_port *port;
	int ret;

	port = mvebu_pcie_find_port(pcie, bus, devfn);
	if (!port) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* Access the emulated PCI-to-PCI bridge */
	if (bus->number == 0)
		return mvebu_sw_pci_bridge_read(port, where, size, val);

	if (!mvebu_pcie_link_up(port)) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* Access the real PCIe interface */
	ret = mvebu_pcie_hw_rd_conf(port, bus, devfn,
				    where, size, val);

	return ret;
}

static struct pci_ops mvebu_pcie_ops = {
	.read = mvebu_pcie_rd_conf,
	.write = mvebu_pcie_wr_conf,
};

static resource_size_t mvebu_pcie_align_resource(struct pci_dev *dev,
						 const struct resource *res,
						 resource_size_t start,
						 resource_size_t size,
						 resource_size_t align)
{
	if (dev->bus->number != 0)
		return start;

	/*
	 * On the PCI-to-PCI bridge side, the I/O windows must have at
	 * least a 64 KB size and the memory windows must have at
	 * least a 1 MB size. Moreover, MBus windows need to have a
	 * base address aligned on their size, and their size must be
	 * a power of two. This means that if the BAR doesn't have a
	 * power of two size, several MBus windows will actually be
	 * created. We need to ensure that the biggest MBus window
	 * (which will be the first one) is aligned on its size, which
	 * explains the rounddown_pow_of_two() being done here.
	 */
	if (res->flags & IORESOURCE_IO)
		return round_up(start, max_t(resource_size_t, SZ_64K,
					     rounddown_pow_of_two(size)));
	else if (res->flags & IORESOURCE_MEM)
		return round_up(start, max_t(resource_size_t, SZ_1M,
					     rounddown_pow_of_two(size)));
	else
		return start;
}

static void __iomem *mvebu_pcie_map_registers(struct platform_device *pdev,
					      struct device_node *np,
					      struct mvebu_pcie_port *port)
{
	struct resource regs;
	int ret = 0;

	ret = of_address_to_resource(np, 0, &regs);
	if (ret)
		return ERR_PTR(ret);

	return devm_ioremap_resource(&pdev->dev, &regs);
}

#define DT_FLAGS_TO_TYPE(flags)       (((flags) >> 24) & 0x03)
#define    DT_TYPE_IO                 0x1
#define    DT_TYPE_MEM32              0x2
#define DT_CPUADDR_TO_TARGET(cpuaddr) (((cpuaddr) >> 56) & 0xFF)
#define DT_CPUADDR_TO_ATTR(cpuaddr)   (((cpuaddr) >> 48) & 0xFF)

static int mvebu_get_tgt_attr(struct device_node *np, int devfn,
			      unsigned long type,
			      unsigned int *tgt,
			      unsigned int *attr)
{
	const int na = 3, ns = 2;
	const __be32 *range;
	int rlen, nranges, rangesz, pna, i;

	*tgt = -1;
	*attr = -1;

	range = of_get_property(np, "ranges", &rlen);
	if (!range)
		return -EINVAL;

	pna = of_n_addr_cells(np);
	rangesz = pna + na + ns;
	nranges = rlen / sizeof(__be32) / rangesz;

	for (i = 0; i < nranges; i++, range += rangesz) {
		u32 flags = of_read_number(range, 1);
		u32 slot = of_read_number(range + 1, 1);
		u64 cpuaddr = of_read_number(range + na, pna);
		unsigned long rtype;

		if (DT_FLAGS_TO_TYPE(flags) == DT_TYPE_IO)
			rtype = IORESOURCE_IO;
		else if (DT_FLAGS_TO_TYPE(flags) == DT_TYPE_MEM32)
			rtype = IORESOURCE_MEM;
		else
			continue;

		if (slot == PCI_SLOT(devfn) && type == rtype) {
			*tgt = DT_CPUADDR_TO_TARGET(cpuaddr);
			*attr = DT_CPUADDR_TO_ATTR(cpuaddr);
			return 0;
		}
	}

	return -ENOENT;
}

#ifdef CONFIG_PM_SLEEP
static int mvebu_pcie_suspend(struct device *dev)
{
	struct mvebu_pcie *pcie;
	int i;

	pcie = dev_get_drvdata(dev);
	for (i = 0; i < pcie->nports; i++) {
		struct mvebu_pcie_port *port = pcie->ports + i;
		port->saved_pcie_stat = mvebu_readl(port, PCIE_STAT_OFF);
	}

	return 0;
}

static int mvebu_pcie_resume(struct device *dev)
{
	struct mvebu_pcie *pcie;
	int i;

	pcie = dev_get_drvdata(dev);
	for (i = 0; i < pcie->nports; i++) {
		struct mvebu_pcie_port *port = pcie->ports + i;
		mvebu_writel(port, port->saved_pcie_stat, PCIE_STAT_OFF);
		mvebu_pcie_setup_hw(port);
	}

	return 0;
}
#endif

static void mvebu_pcie_port_clk_put(void *data)
{
	struct mvebu_pcie_port *port = data;

	clk_put(port->clk);
}

static int mvebu_pcie_parse_port(struct mvebu_pcie *pcie,
	struct mvebu_pcie_port *port, struct device_node *child)
{
	struct device *dev = &pcie->pdev->dev;
	enum of_gpio_flags flags;
	int reset_gpio, ret;

	port->pcie = pcie;

	if (of_property_read_u32(child, "marvell,pcie-port", &port->port)) {
		dev_warn(dev, "ignoring %pOF, missing pcie-port property\n",
			 child);
		goto skip;
	}

	if (of_property_read_u32(child, "marvell,pcie-lane", &port->lane))
		port->lane = 0;

	port->name = devm_kasprintf(dev, GFP_KERNEL, "pcie%d.%d", port->port,
				    port->lane);
	if (!port->name) {
		ret = -ENOMEM;
		goto err;
	}

	port->devfn = of_pci_get_devfn(child);
	if (port->devfn < 0)
		goto skip;

	ret = mvebu_get_tgt_attr(dev->of_node, port->devfn, IORESOURCE_MEM,
				 &port->mem_target, &port->mem_attr);
	if (ret < 0) {
		dev_err(dev, "%s: cannot get tgt/attr for mem window\n",
			port->name);
		goto skip;
	}

	if (resource_size(&pcie->io) != 0) {
		mvebu_get_tgt_attr(dev->of_node, port->devfn, IORESOURCE_IO,
				   &port->io_target, &port->io_attr);
	} else {
		port->io_target = -1;
		port->io_attr = -1;
	}

	reset_gpio = of_get_named_gpio_flags(child, "reset-gpios", 0, &flags);
	if (reset_gpio == -EPROBE_DEFER) {
		ret = reset_gpio;
		goto err;
	}

	if (gpio_is_valid(reset_gpio)) {
		unsigned long gpio_flags;

		port->reset_name = devm_kasprintf(dev, GFP_KERNEL, "%s-reset",
						  port->name);
		if (!port->reset_name) {
			ret = -ENOMEM;
			goto err;
		}

		if (flags & OF_GPIO_ACTIVE_LOW) {
			dev_info(dev, "%pOF: reset gpio is active low\n",
				 child);
			gpio_flags = GPIOF_ACTIVE_LOW |
				     GPIOF_OUT_INIT_LOW;
		} else {
			gpio_flags = GPIOF_OUT_INIT_HIGH;
		}

		ret = devm_gpio_request_one(dev, reset_gpio, gpio_flags,
					    port->reset_name);
		if (ret) {
			if (ret == -EPROBE_DEFER)
				goto err;
			goto skip;
		}

		port->reset_gpio = gpio_to_desc(reset_gpio);
	}

	port->clk = of_clk_get_by_name(child, NULL);
	if (IS_ERR(port->clk)) {
		dev_err(dev, "%s: cannot get clock\n", port->name);
		goto skip;
	}

	ret = devm_add_action(dev, mvebu_pcie_port_clk_put, port);
	if (ret < 0) {
		clk_put(port->clk);
		goto err;
	}

	return 1;

skip:
	ret = 0;

	/* In the case of skipping, we need to free these */
	devm_kfree(dev, port->reset_name);
	port->reset_name = NULL;
	devm_kfree(dev, port->name);
	port->name = NULL;

err:
	return ret;
}

/*
 * Power up a PCIe port.  PCIe requires the refclk to be stable for 100µs
 * prior to releasing PERST.  See table 2-4 in section 2.6.2 AC Specifications
 * of the PCI Express Card Electromechanical Specification, 1.1.
 */
static int mvebu_pcie_powerup(struct mvebu_pcie_port *port)
{
	int ret;

	ret = clk_prepare_enable(port->clk);
	if (ret < 0)
		return ret;

	if (port->reset_gpio) {
		u32 reset_udelay = PCI_PM_D3COLD_WAIT * 1000;

		of_property_read_u32(port->dn, "reset-delay-us",
				     &reset_udelay);

		udelay(100);

		gpiod_set_value_cansleep(port->reset_gpio, 0);
		msleep(reset_udelay / 1000);
	}

	return 0;
}

/*
 * Power down a PCIe port.  Strictly, PCIe requires us to place the card
 * in D3hot state before asserting PERST#.
 */
static void mvebu_pcie_powerdown(struct mvebu_pcie_port *port)
{
	gpiod_set_value_cansleep(port->reset_gpio, 1);

	clk_disable_unprepare(port->clk);
}

/*
 * We can't use devm_of_pci_get_host_bridge_resources() because we
 * need to parse our special DT properties encoding the MEM and IO
 * apertures.
 */
static int mvebu_pcie_parse_request_resources(struct mvebu_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	INIT_LIST_HEAD(&pcie->resources);

	/* Get the bus range */
	ret = of_pci_parse_bus_range(np, &pcie->busn);
	if (ret) {
		dev_err(dev, "failed to parse bus-range property: %d\n", ret);
		return ret;
	}
	pci_add_resource(&pcie->resources, &pcie->busn);

	/* Get the PCIe memory aperture */
	mvebu_mbus_get_pcie_mem_aperture(&pcie->mem);
	if (resource_size(&pcie->mem) == 0) {
		dev_err(dev, "invalid memory aperture size\n");
		return -EINVAL;
	}

	pcie->mem.name = "PCI MEM";
	pci_add_resource(&pcie->resources, &pcie->mem);

	/* Get the PCIe IO aperture */
	mvebu_mbus_get_pcie_io_aperture(&pcie->io);

	if (resource_size(&pcie->io) != 0) {
		pcie->realio.flags = pcie->io.flags;
		pcie->realio.start = PCIBIOS_MIN_IO;
		pcie->realio.end = min_t(resource_size_t,
					 IO_SPACE_LIMIT - SZ_64K,
					 resource_size(&pcie->io) - 1);
		pcie->realio.name = "PCI I/O";

		pci_add_resource(&pcie->resources, &pcie->realio);
	}

	return devm_request_pci_bus_resources(dev, &pcie->resources);
}

/*
 * This is a copy of pci_host_probe(), except that it does the I/O
 * remap as the last step, once we are sure we won't fail.
 *
 * It should be removed once the I/O remap error handling issue has
 * been sorted out.
 */
static int mvebu_pci_host_probe(struct pci_host_bridge *bridge)
{
	struct mvebu_pcie *pcie;
	struct pci_bus *bus, *child;
	int ret;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret < 0) {
		dev_err(bridge->dev.parent, "Scanning root bridge failed");
		return ret;
	}

	pcie = pci_host_bridge_priv(bridge);
	if (resource_size(&pcie->io) != 0) {
		unsigned int i;

		for (i = 0; i < resource_size(&pcie->realio); i += SZ_64K)
			pci_ioremap_io(i, pcie->io.start + i);
	}

	bus = bridge->bus;

	/*
	 * We insert PCI resources into the iomem_resource and
	 * ioport_resource trees in either pci_bus_claim_resources()
	 * or pci_bus_assign_resources().
	 */
	if (pci_has_flag(PCI_PROBE_ONLY)) {
		pci_bus_claim_resources(bus);
	} else {
		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);

		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}

	pci_bus_add_devices(bus);
	return 0;
}

static int mvebu_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mvebu_pcie *pcie;
	struct pci_host_bridge *bridge;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int num, i, ret;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(struct mvebu_pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->pdev = pdev;
	platform_set_drvdata(pdev, pcie);

	ret = mvebu_pcie_parse_request_resources(pcie);
	if (ret)
		return ret;

	num = of_get_available_child_count(np);

	pcie->ports = devm_kcalloc(dev, num, sizeof(*pcie->ports), GFP_KERNEL);
	if (!pcie->ports)
		return -ENOMEM;

	i = 0;
	for_each_available_child_of_node(np, child) {
		struct mvebu_pcie_port *port = &pcie->ports[i];

		ret = mvebu_pcie_parse_port(pcie, port, child);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		} else if (ret == 0) {
			continue;
		}

		port->dn = child;
		i++;
	}
	pcie->nports = i;

	for (i = 0; i < pcie->nports; i++) {
		struct mvebu_pcie_port *port = &pcie->ports[i];

		child = port->dn;
		if (!child)
			continue;

		ret = mvebu_pcie_powerup(port);
		if (ret < 0)
			continue;

		port->base = mvebu_pcie_map_registers(pdev, child, port);
		if (IS_ERR(port->base)) {
			dev_err(dev, "%s: cannot map registers\n", port->name);
			port->base = NULL;
			mvebu_pcie_powerdown(port);
			continue;
		}

		mvebu_pcie_setup_hw(port);
		mvebu_pcie_set_local_dev_nr(port, 1);
		mvebu_sw_pci_bridge_init(port);
	}

	pcie->nports = i;

	list_splice_init(&pcie->resources, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = pcie;
	bridge->busnr = 0;
	bridge->ops = &mvebu_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;
	bridge->align_resource = mvebu_pcie_align_resource;
	bridge->msi = pcie->msi;

	return mvebu_pci_host_probe(bridge);
}

static const struct of_device_id mvebu_pcie_of_match_table[] = {
	{ .compatible = "marvell,armada-xp-pcie", },
	{ .compatible = "marvell,armada-370-pcie", },
	{ .compatible = "marvell,dove-pcie", },
	{ .compatible = "marvell,kirkwood-pcie", },
	{},
};

static const struct dev_pm_ops mvebu_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(mvebu_pcie_suspend, mvebu_pcie_resume)
};

static struct platform_driver mvebu_pcie_driver = {
	.driver = {
		.name = "mvebu-pcie",
		.of_match_table = mvebu_pcie_of_match_table,
		/* driver unloading/unbinding currently not supported */
		.suppress_bind_attrs = true,
		.pm = &mvebu_pcie_pm_ops,
	},
	.probe = mvebu_pcie_probe,
};
builtin_platform_driver(mvebu_pcie_driver);
