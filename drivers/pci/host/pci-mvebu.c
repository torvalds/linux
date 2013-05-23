/*
 * PCIe driver for Marvell Armada 370 and Armada XP SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mbus.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

/*
 * PCIe unit register offsets.
 */
#define PCIE_DEV_ID_OFF		0x0000
#define PCIE_CMD_OFF		0x0004
#define PCIE_DEV_REV_OFF	0x0008
#define PCIE_BAR_LO_OFF(n)	(0x0010 + ((n) << 3))
#define PCIE_BAR_HI_OFF(n)	(0x0014 + ((n) << 3))
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
#define PCIE_DEBUG_CTRL         0x1a60
#define  PCIE_DEBUG_SOFT_RESET		BIT(20)

/*
 * This product ID is registered by Marvell, and used when the Marvell
 * SoC is not the root complex, but an endpoint on the PCIe bus. It is
 * therefore safe to re-use this PCI ID for our emulated PCI-to-PCI
 * bridge.
 */
#define MARVELL_EMULATED_PCI_PCI_BRIDGE_ID 0x7846

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
	u16 prefmembase;
	u16 prefmemlimit;
	u32 prefbaseupper;
	u32 preflimitupper;
	u16 iobaseupper;
	u16 iolimitupper;
	u8 cappointer;
	u8 reserved1;
	u16 reserved2;
	u32 romaddr;
	u8 intline;
	u8 intpin;
	u16 bridgectrl;
};

struct mvebu_pcie_port;

/* Structure representing all PCIe interfaces */
struct mvebu_pcie {
	struct platform_device *pdev;
	struct mvebu_pcie_port *ports;
	struct resource io;
	struct resource realio;
	struct resource mem;
	struct resource busn;
	int nports;
};

/* Structure representing one PCIe interface */
struct mvebu_pcie_port {
	char *name;
	void __iomem *base;
	spinlock_t conf_lock;
	int haslink;
	u32 port;
	u32 lane;
	int devfn;
	struct clk *clk;
	struct mvebu_sw_pci_bridge bridge;
	struct device_node *dn;
	struct mvebu_pcie *pcie;
	phys_addr_t memwin_base;
	size_t memwin_size;
	phys_addr_t iowin_base;
	size_t iowin_size;
};

static bool mvebu_pcie_link_up(struct mvebu_pcie_port *port)
{
	return !(readl(port->base + PCIE_STAT_OFF) & PCIE_STAT_LINK_DOWN);
}

static void mvebu_pcie_set_local_bus_nr(struct mvebu_pcie_port *port, int nr)
{
	u32 stat;

	stat = readl(port->base + PCIE_STAT_OFF);
	stat &= ~PCIE_STAT_BUS;
	stat |= nr << 8;
	writel(stat, port->base + PCIE_STAT_OFF);
}

static void mvebu_pcie_set_local_dev_nr(struct mvebu_pcie_port *port, int nr)
{
	u32 stat;

	stat = readl(port->base + PCIE_STAT_OFF);
	stat &= ~PCIE_STAT_DEV;
	stat |= nr << 16;
	writel(stat, port->base + PCIE_STAT_OFF);
}

/*
 * Setup PCIE BARs and Address Decode Wins:
 * BAR[0,2] -> disabled, BAR[1] -> covers all DRAM banks
 * WIN[0-3] -> DRAM bank[0-3]
 */
static void __init mvebu_pcie_setup_wins(struct mvebu_pcie_port *port)
{
	const struct mbus_dram_target_info *dram;
	u32 size;
	int i;

	dram = mv_mbus_dram_info();

	/* First, disable and clear BARs and windows. */
	for (i = 1; i < 3; i++) {
		writel(0, port->base + PCIE_BAR_CTRL_OFF(i));
		writel(0, port->base + PCIE_BAR_LO_OFF(i));
		writel(0, port->base + PCIE_BAR_HI_OFF(i));
	}

	for (i = 0; i < 5; i++) {
		writel(0, port->base + PCIE_WIN04_CTRL_OFF(i));
		writel(0, port->base + PCIE_WIN04_BASE_OFF(i));
		writel(0, port->base + PCIE_WIN04_REMAP_OFF(i));
	}

	writel(0, port->base + PCIE_WIN5_CTRL_OFF);
	writel(0, port->base + PCIE_WIN5_BASE_OFF);
	writel(0, port->base + PCIE_WIN5_REMAP_OFF);

	/* Setup windows for DDR banks.  Count total DDR size on the fly. */
	size = 0;
	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		writel(cs->base & 0xffff0000,
		       port->base + PCIE_WIN04_BASE_OFF(i));
		writel(0, port->base + PCIE_WIN04_REMAP_OFF(i));
		writel(((cs->size - 1) & 0xffff0000) |
			(cs->mbus_attr << 8) |
			(dram->mbus_dram_target_id << 4) | 1,
		       port->base + PCIE_WIN04_CTRL_OFF(i));

		size += cs->size;
	}

	/* Round up 'size' to the nearest power of two. */
	if ((size & (size - 1)) != 0)
		size = 1 << fls(size);

	/* Setup BAR[1] to all DRAM banks. */
	writel(dram->cs[0].base, port->base + PCIE_BAR_LO_OFF(1));
	writel(0, port->base + PCIE_BAR_HI_OFF(1));
	writel(((size - 1) & 0xffff0000) | 1,
	       port->base + PCIE_BAR_CTRL_OFF(1));
}

static void __init mvebu_pcie_setup_hw(struct mvebu_pcie_port *port)
{
	u16 cmd;
	u32 mask;

	/* Point PCIe unit MBUS decode windows to DRAM space. */
	mvebu_pcie_setup_wins(port);

	/* Master + slave enable. */
	cmd = readw(port->base + PCIE_CMD_OFF);
	cmd |= PCI_COMMAND_IO;
	cmd |= PCI_COMMAND_MEMORY;
	cmd |= PCI_COMMAND_MASTER;
	writew(cmd, port->base + PCIE_CMD_OFF);

	/* Enable interrupt lines A-D. */
	mask = readl(port->base + PCIE_MASK_OFF);
	mask |= PCIE_MASK_ENABLE_INTS;
	writel(mask, port->base + PCIE_MASK_OFF);
}

static int mvebu_pcie_hw_rd_conf(struct mvebu_pcie_port *port,
				 struct pci_bus *bus,
				 u32 devfn, int where, int size, u32 *val)
{
	writel(PCIE_CONF_ADDR(bus->number, devfn, where),
	       port->base + PCIE_CONF_ADDR_OFF);

	*val = readl(port->base + PCIE_CONF_DATA_OFF);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

static int mvebu_pcie_hw_wr_conf(struct mvebu_pcie_port *port,
				 struct pci_bus *bus,
				 u32 devfn, int where, int size, u32 val)
{
	int ret = PCIBIOS_SUCCESSFUL;

	writel(PCIE_CONF_ADDR(bus->number, devfn, where),
	       port->base + PCIE_CONF_ADDR_OFF);

	if (size == 4)
		writel(val, port->base + PCIE_CONF_DATA_OFF);
	else if (size == 2)
		writew(val, port->base + PCIE_CONF_DATA_OFF + (where & 3));
	else if (size == 1)
		writeb(val, port->base + PCIE_CONF_DATA_OFF + (where & 3));
	else
		ret = PCIBIOS_BAD_REGISTER_NUMBER;

	return ret;
}

static void mvebu_pcie_handle_iobase_change(struct mvebu_pcie_port *port)
{
	phys_addr_t iobase;

	/* Are the new iobase/iolimit values invalid? */
	if (port->bridge.iolimit < port->bridge.iobase ||
	    port->bridge.iolimitupper < port->bridge.iobaseupper) {

		/* If a window was configured, remove it */
		if (port->iowin_base) {
			mvebu_mbus_del_window(port->iowin_base,
					      port->iowin_size);
			port->iowin_base = 0;
			port->iowin_size = 0;
		}

		return;
	}

	/*
	 * We read the PCI-to-PCI bridge emulated registers, and
	 * calculate the base address and size of the address decoding
	 * window to setup, according to the PCI-to-PCI bridge
	 * specifications. iobase is the bus address, port->iowin_base
	 * is the CPU address.
	 */
	iobase = ((port->bridge.iobase & 0xF0) << 8) |
		(port->bridge.iobaseupper << 16);
	port->iowin_base = port->pcie->io.start + iobase;
	port->iowin_size = ((0xFFF | ((port->bridge.iolimit & 0xF0) << 8) |
			    (port->bridge.iolimitupper << 16)) -
			    iobase);

	mvebu_mbus_add_window_remap_flags(port->name, port->iowin_base,
					  port->iowin_size,
					  iobase,
					  MVEBU_MBUS_PCI_IO);

	pci_ioremap_io(iobase, port->iowin_base);
}

static void mvebu_pcie_handle_membase_change(struct mvebu_pcie_port *port)
{
	/* Are the new membase/memlimit values invalid? */
	if (port->bridge.memlimit < port->bridge.membase) {

		/* If a window was configured, remove it */
		if (port->memwin_base) {
			mvebu_mbus_del_window(port->memwin_base,
					      port->memwin_size);
			port->memwin_base = 0;
			port->memwin_size = 0;
		}

		return;
	}

	/*
	 * We read the PCI-to-PCI bridge emulated registers, and
	 * calculate the base address and size of the address decoding
	 * window to setup, according to the PCI-to-PCI bridge
	 * specifications.
	 */
	port->memwin_base  = ((port->bridge.membase & 0xFFF0) << 16);
	port->memwin_size  =
		(((port->bridge.memlimit & 0xFFF0) << 16) | 0xFFFFF) -
		port->memwin_base;

	mvebu_mbus_add_window_remap_flags(port->name, port->memwin_base,
					  port->memwin_size,
					  MVEBU_MBUS_NO_REMAP,
					  MVEBU_MBUS_PCI_MEM);
}

/*
 * Initialize the configuration space of the PCI-to-PCI bridge
 * associated with the given PCIe interface.
 */
static void mvebu_sw_pci_bridge_init(struct mvebu_pcie_port *port)
{
	struct mvebu_sw_pci_bridge *bridge = &port->bridge;

	memset(bridge, 0, sizeof(struct mvebu_sw_pci_bridge));

	bridge->status = PCI_STATUS_CAP_LIST;
	bridge->class = PCI_CLASS_BRIDGE_PCI;
	bridge->vendor = PCI_VENDOR_ID_MARVELL;
	bridge->device = MARVELL_EMULATED_PCI_PCI_BRIDGE_ID;
	bridge->header_type = PCI_HEADER_TYPE_BRIDGE;
	bridge->cache_line_size = 0x10;

	/* We support 32 bits I/O addressing */
	bridge->iobase = PCI_IO_RANGE_TYPE_32;
	bridge->iolimit = PCI_IO_RANGE_TYPE_32;
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
		*value = bridge->status << 16 | bridge->command;
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
		*value = (bridge->secondary_status << 16 |
			  bridge->iolimit          <<  8 |
			  bridge->iobase);
		break;

	case PCI_MEMORY_BASE:
		*value = (bridge->memlimit << 16 | bridge->membase);
		break;

	case PCI_PREF_MEMORY_BASE:
		*value = (bridge->prefmemlimit << 16 | bridge->prefmembase);
		break;

	case PCI_PREF_BASE_UPPER32:
		*value = bridge->prefbaseupper;
		break;

	case PCI_PREF_LIMIT_UPPER32:
		*value = bridge->preflimitupper;
		break;

	case PCI_IO_BASE_UPPER16:
		*value = (bridge->iolimitupper << 16 | bridge->iobaseupper);
		break;

	case PCI_ROM_ADDRESS1:
		*value = 0;
		break;

	default:
		*value = 0xffffffff;
		return PCIBIOS_BAD_REGISTER_NUMBER;
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
		bridge->command = value & 0xffff;
		bridge->status = value >> 16;
		break;

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
		bridge->secondary_status = value >> 16;
		mvebu_pcie_handle_iobase_change(port);
		break;

	case PCI_MEMORY_BASE:
		bridge->membase = value & 0xffff;
		bridge->memlimit = value >> 16;
		mvebu_pcie_handle_membase_change(port);
		break;

	case PCI_PREF_MEMORY_BASE:
		bridge->prefmembase = value & 0xffff;
		bridge->prefmemlimit = value >> 16;
		break;

	case PCI_PREF_BASE_UPPER32:
		bridge->prefbaseupper = value;
		break;

	case PCI_PREF_LIMIT_UPPER32:
		bridge->preflimitupper = value;
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

	default:
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static inline struct mvebu_pcie *sys_to_pcie(struct pci_sys_data *sys)
{
	return sys->private_data;
}

static struct mvebu_pcie_port *
mvebu_pcie_find_port(struct mvebu_pcie *pcie, struct pci_bus *bus,
		     int devfn)
{
	int i;

	for (i = 0; i < pcie->nports; i++) {
		struct mvebu_pcie_port *port = &pcie->ports[i];
		if (bus->number == 0 && port->devfn == devfn)
			return port;
		if (bus->number != 0 &&
		    port->bridge.secondary_bus == bus->number)
			return port;
	}

	return NULL;
}

/* PCI configuration space write function */
static int mvebu_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			      int where, int size, u32 val)
{
	struct mvebu_pcie *pcie = sys_to_pcie(bus->sysdata);
	struct mvebu_pcie_port *port;
	unsigned long flags;
	int ret;

	port = mvebu_pcie_find_port(pcie, bus, devfn);
	if (!port)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Access the emulated PCI-to-PCI bridge */
	if (bus->number == 0)
		return mvebu_sw_pci_bridge_write(port, where, size, val);

	if (!port->haslink || PCI_SLOT(devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Access the real PCIe interface */
	spin_lock_irqsave(&port->conf_lock, flags);
	ret = mvebu_pcie_hw_wr_conf(port, bus, devfn,
				    where, size, val);
	spin_unlock_irqrestore(&port->conf_lock, flags);

	return ret;
}

/* PCI configuration space read function */
static int mvebu_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			      int size, u32 *val)
{
	struct mvebu_pcie *pcie = sys_to_pcie(bus->sysdata);
	struct mvebu_pcie_port *port;
	unsigned long flags;
	int ret;

	port = mvebu_pcie_find_port(pcie, bus, devfn);
	if (!port) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* Access the emulated PCI-to-PCI bridge */
	if (bus->number == 0)
		return mvebu_sw_pci_bridge_read(port, where, size, val);

	if (!port->haslink || PCI_SLOT(devfn) != 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* Access the real PCIe interface */
	spin_lock_irqsave(&port->conf_lock, flags);
	ret = mvebu_pcie_hw_rd_conf(port, bus, devfn,
				    where, size, val);
	spin_unlock_irqrestore(&port->conf_lock, flags);

	return ret;
}

static struct pci_ops mvebu_pcie_ops = {
	.read = mvebu_pcie_rd_conf,
	.write = mvebu_pcie_wr_conf,
};

static int __init mvebu_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct mvebu_pcie *pcie = sys_to_pcie(sys);
	int i;

	pci_add_resource_offset(&sys->resources, &pcie->realio, sys->io_offset);
	pci_add_resource_offset(&sys->resources, &pcie->mem, sys->mem_offset);
	pci_add_resource(&sys->resources, &pcie->busn);

	for (i = 0; i < pcie->nports; i++) {
		struct mvebu_pcie_port *port = &pcie->ports[i];
		mvebu_pcie_setup_hw(port);
	}

	return 1;
}

static int __init mvebu_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct of_irq oirq;
	int ret;

	ret = of_irq_map_pci(dev, &oirq);
	if (ret)
		return ret;

	return irq_create_of_mapping(oirq.controller, oirq.specifier,
				     oirq.size);
}

static struct pci_bus *mvebu_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct mvebu_pcie *pcie = sys_to_pcie(sys);
	struct pci_bus *bus;

	bus = pci_create_root_bus(&pcie->pdev->dev, sys->busnr,
				  &mvebu_pcie_ops, sys, &sys->resources);
	if (!bus)
		return NULL;

	pci_scan_child_bus(bus);

	return bus;
}

resource_size_t mvebu_pcie_align_resource(struct pci_dev *dev,
					  const struct resource *res,
					  resource_size_t start,
					  resource_size_t size,
					  resource_size_t align)
{
	if (dev->bus->number != 0)
		return start;

	/*
	 * On the PCI-to-PCI bridge side, the I/O windows must have at
	 * least a 64 KB size and be aligned on their size, and the
	 * memory windows must have at least a 1 MB size and be
	 * aligned on their size
	 */
	if (res->flags & IORESOURCE_IO)
		return round_up(start, max((resource_size_t)SZ_64K, size));
	else if (res->flags & IORESOURCE_MEM)
		return round_up(start, max((resource_size_t)SZ_1M, size));
	else
		return start;
}

static void __init mvebu_pcie_enable(struct mvebu_pcie *pcie)
{
	struct hw_pci hw;

	memset(&hw, 0, sizeof(hw));

	hw.nr_controllers = 1;
	hw.private_data   = (void **)&pcie;
	hw.setup          = mvebu_pcie_setup;
	hw.scan           = mvebu_pcie_scan_bus;
	hw.map_irq        = mvebu_pcie_map_irq;
	hw.ops            = &mvebu_pcie_ops;
	hw.align_resource = mvebu_pcie_align_resource;

	pci_common_init(&hw);
}

/*
 * Looks up the list of register addresses encoded into the reg =
 * <...> property for one that matches the given port/lane. Once
 * found, maps it.
 */
static void __iomem * __init
mvebu_pcie_map_registers(struct platform_device *pdev,
			 struct device_node *np,
			 struct mvebu_pcie_port *port)
{
	struct resource regs;
	int ret = 0;

	ret = of_address_to_resource(np, 0, &regs);
	if (ret)
		return NULL;

	return devm_request_and_ioremap(&pdev->dev, &regs);
}

static int __init mvebu_pcie_probe(struct platform_device *pdev)
{
	struct mvebu_pcie *pcie;
	struct device_node *np = pdev->dev.of_node;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	struct device_node *child;
	int i, ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_pcie),
			    GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->pdev = pdev;

	if (of_pci_range_parser_init(&parser, np))
		return -EINVAL;

	/* Get the I/O and memory ranges from DT */
	for_each_of_pci_range(&parser, &range) {
		unsigned long restype = range.flags & IORESOURCE_TYPE_BITS;
		if (restype == IORESOURCE_IO) {
			of_pci_range_to_resource(&range, np, &pcie->io);
			of_pci_range_to_resource(&range, np, &pcie->realio);
			pcie->io.name = "I/O";
			pcie->realio.start = max_t(resource_size_t,
						   PCIBIOS_MIN_IO,
						   range.pci_addr);
			pcie->realio.end = min_t(resource_size_t,
						 IO_SPACE_LIMIT,
						 range.pci_addr + range.size);
		}
		if (restype == IORESOURCE_MEM) {
			of_pci_range_to_resource(&range, np, &pcie->mem);
			pcie->mem.name = "MEM";
		}
	}

	/* Get the bus range */
	ret = of_pci_parse_bus_range(np, &pcie->busn);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse bus-range property: %d\n",
			ret);
		return ret;
	}

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (!of_device_is_available(child))
			continue;
		pcie->nports++;
	}

	pcie->ports = devm_kzalloc(&pdev->dev, pcie->nports *
				   sizeof(struct mvebu_pcie_port),
				   GFP_KERNEL);
	if (!pcie->ports)
		return -ENOMEM;

	i = 0;
	for_each_child_of_node(pdev->dev.of_node, child) {
		struct mvebu_pcie_port *port = &pcie->ports[i];

		if (!of_device_is_available(child))
			continue;

		port->pcie = pcie;

		if (of_property_read_u32(child, "marvell,pcie-port",
					 &port->port)) {
			dev_warn(&pdev->dev,
				 "ignoring PCIe DT node, missing pcie-port property\n");
			continue;
		}

		if (of_property_read_u32(child, "marvell,pcie-lane",
					 &port->lane))
			port->lane = 0;

		port->name = kasprintf(GFP_KERNEL, "pcie%d.%d",
				       port->port, port->lane);

		port->devfn = of_pci_get_devfn(child);
		if (port->devfn < 0)
			continue;

		port->base = mvebu_pcie_map_registers(pdev, child, port);
		if (!port->base) {
			dev_err(&pdev->dev, "PCIe%d.%d: cannot map registers\n",
				port->port, port->lane);
			continue;
		}

		mvebu_pcie_set_local_dev_nr(port, 1);

		if (mvebu_pcie_link_up(port)) {
			port->haslink = 1;
			dev_info(&pdev->dev, "PCIe%d.%d: link up\n",
				 port->port, port->lane);
		} else {
			port->haslink = 0;
			dev_info(&pdev->dev, "PCIe%d.%d: link down\n",
				 port->port, port->lane);
		}

		port->clk = of_clk_get_by_name(child, NULL);
		if (IS_ERR(port->clk)) {
			dev_err(&pdev->dev, "PCIe%d.%d: cannot get clock\n",
			       port->port, port->lane);
			iounmap(port->base);
			port->haslink = 0;
			continue;
		}

		port->dn = child;

		clk_prepare_enable(port->clk);
		spin_lock_init(&port->conf_lock);

		mvebu_sw_pci_bridge_init(port);

		i++;
	}

	mvebu_pcie_enable(pcie);

	return 0;
}

static const struct of_device_id mvebu_pcie_of_match_table[] = {
	{ .compatible = "marvell,armada-xp-pcie", },
	{ .compatible = "marvell,armada-370-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, mvebu_pcie_of_match_table);

static struct platform_driver mvebu_pcie_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mvebu-pcie",
		.of_match_table =
		   of_match_ptr(mvebu_pcie_of_match_table),
	},
};

static int __init mvebu_pcie_init(void)
{
	return platform_driver_probe(&mvebu_pcie_driver,
				     mvebu_pcie_probe);
}

subsys_initcall(mvebu_pcie_init);

MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_DESCRIPTION("Marvell EBU PCIe driver");
MODULE_LICENSE("GPLv2");
