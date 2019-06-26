// SPDX-License-Identifier: GPL-2.0+
/*
 * BRIEF MODULE DESCRIPTION
 *     PCI init for Ralink RT2880 solution
 *
 * Copyright 2007 Ralink Inc. (bruce_chang@ralinktech.com.tw)
 *
 * May 2007 Bruce Chang
 * Initial Release
 *
 * May 2009 Bruce Chang
 * support RT2880/RT3883 PCIe
 *
 * May 2011 Bruce Chang
 * support RT6855/MT7620 PCIe
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <mt7621.h>
#include <ralink_regs.h>

#include "../../pci/pci.h"

/* sysctl */
#define MT7621_CHIP_REV_ID		0x0c
#define CHIP_REV_MT7621_E2		0x0101

/* MediaTek specific configuration registers */
#define PCIE_FTS_NUM			0x70c
#define PCIE_FTS_NUM_MASK		GENMASK(15, 8)
#define PCIE_FTS_NUM_L0(x)		(((x) & 0xff) << 8)

/* rt_sysc_membase relative registers */
#define RALINK_PCIE_CLK_GEN		0x7c
#define RALINK_PCIE_CLK_GEN1		0x80

/* Host-PCI bridge registers */
#define RALINK_PCI_PCICFG_ADDR		0x0000
#define RALINK_PCI_PCIMSK_ADDR		0x000C
#define RALINK_PCI_CONFIG_ADDR		0x0020
#define RALINK_PCI_CONFIG_DATA		0x0024
#define RALINK_PCI_MEMBASE		0x0028
#define RALINK_PCI_IOBASE		0x002C

/* PCICFG virtual bridges */
#define MT7621_BR0_MASK			GENMASK(19, 16)
#define MT7621_BR1_MASK			GENMASK(23, 20)
#define MT7621_BR2_MASK			GENMASK(27, 24)
#define MT7621_BR_ALL_MASK		GENMASK(27, 16)
#define MT7621_BR0_SHIFT		16
#define MT7621_BR1_SHIFT		20
#define MT7621_BR2_SHIFT		24

/* PCIe RC control registers */
#define MT7621_PCIE_OFFSET		0x2000
#define MT7621_NEXT_PORT		0x1000

#define RALINK_PCI_BAR0SETUP_ADDR	0x0010
#define RALINK_PCI_IMBASEBAR0_ADDR	0x0018
#define RALINK_PCI_ID			0x0030
#define RALINK_PCI_CLASS		0x0034
#define RALINK_PCI_SUBID		0x0038
#define RALINK_PCI_STATUS		0x0050

/* Some definition values */
#define PCIE_REVISION_ID		BIT(0)
#define PCIE_CLASS_CODE			(0x60400 << 8)
#define PCIE_BAR_MAP_MAX		GENMASK(30, 16)
#define PCIE_BAR_ENABLE			BIT(0)
#define PCIE_PORT_INT_EN(x)		BIT(20 + (x))
#define PCIE_PORT_CLK_EN(x)		BIT(24 + (x))
#define PCIE_PORT_PERST(x)		BIT(1 + (x))
#define PCIE_PORT_LINKUP		BIT(0)

#define PCIE_CLK_GEN_EN			BIT(31)
#define PCIE_CLK_GEN_DIS		0
#define PCIE_CLK_GEN1_DIS		GENMASK(30, 24)
#define PCIE_CLK_GEN1_EN		(BIT(27) | BIT(25))
#define MEMORY_BASE			0x0

/**
 * struct mt7621_pcie_port - PCIe port information
 * @base: I/O mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @phy: pointer to PHY control block
 * @pcie_rst: pointer to port reset control
 * @slot: port slot
 * @enabled: indicates if port is enabled
 */
struct mt7621_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct mt7621_pcie *pcie;
	struct phy *phy;
	struct reset_control *pcie_rst;
	u32 slot;
	bool enabled;
};

/**
 * struct mt7621_pcie - PCIe host information
 * @base: IO Mapped Register Base
 * @io: IO resource
 * @mem: non-prefetchable memory resource
 * @busn: bus range
 * @offset: IO / Memory offset
 * @dev: Pointer to PCIe device
 * @ports: pointer to PCIe port information
 * @rst: pointer to pcie reset
 */
struct mt7621_pcie {
	void __iomem *base;
	struct device *dev;
	struct resource io;
	struct resource mem;
	struct resource busn;
	struct {
		resource_size_t mem;
		resource_size_t io;
	} offset;
	struct list_head ports;
	struct reset_control *rst;
};

static inline u32 pcie_read(struct mt7621_pcie *pcie, u32 reg)
{
	return readl(pcie->base + reg);
}

static inline void pcie_write(struct mt7621_pcie *pcie, u32 val, u32 reg)
{
	writel(val, pcie->base + reg);
}

static inline u32 pcie_port_read(struct mt7621_pcie_port *port, u32 reg)
{
	return readl(port->base + reg);
}

static inline void pcie_port_write(struct mt7621_pcie_port *port,
				   u32 val, u32 reg)
{
	writel(val, port->base + reg);
}

static inline u32 mt7621_pci_get_cfgaddr(unsigned int bus, unsigned int slot,
					 unsigned int func, unsigned int where)
{
	return (((where & 0xF00) >> 8) << 24) | (bus << 16) | (slot << 11) |
		(func << 8) | (where & 0xfc) | 0x80000000;
}

static void __iomem *mt7621_pcie_map_bus(struct pci_bus *bus,
					 unsigned int devfn, int where)
{
	struct mt7621_pcie *pcie = bus->sysdata;
	u32 address = mt7621_pci_get_cfgaddr(bus->number, PCI_SLOT(devfn),
					     PCI_FUNC(devfn), where);

	writel(address, pcie->base + RALINK_PCI_CONFIG_ADDR);

	return pcie->base + RALINK_PCI_CONFIG_DATA + (where & 3);
}

struct pci_ops mt7621_pci_ops = {
	.map_bus	= mt7621_pcie_map_bus,
	.read		= pci_generic_config_read,
	.write		= pci_generic_config_write,
};

static u32 read_config(struct mt7621_pcie *pcie, unsigned int dev, u32 reg)
{
	u32 address = mt7621_pci_get_cfgaddr(0, dev, 0, reg);

	pcie_write(pcie, address, RALINK_PCI_CONFIG_ADDR);
	return pcie_read(pcie, RALINK_PCI_CONFIG_DATA);
}

static void write_config(struct mt7621_pcie *pcie, unsigned int dev,
			 u32 reg, u32 val)
{
	u32 address = mt7621_pci_get_cfgaddr(0, dev, 0, reg);

	pcie_write(pcie, address, RALINK_PCI_CONFIG_ADDR);
	pcie_write(pcie, val, RALINK_PCI_CONFIG_DATA);
}

static inline void mt7621_control_assert(struct mt7621_pcie_port *port)
{
	u32 chip_rev_id = rt_sysc_r32(MT7621_CHIP_REV_ID);

	if ((chip_rev_id & 0xFFFF) == CHIP_REV_MT7621_E2)
		reset_control_assert(port->pcie_rst);
	else
		reset_control_deassert(port->pcie_rst);
}

static inline void mt7621_control_deassert(struct mt7621_pcie_port *port)
{
	u32 chip_rev_id = rt_sysc_r32(MT7621_CHIP_REV_ID);

	if ((chip_rev_id & 0xFFFF) == CHIP_REV_MT7621_E2)
		reset_control_deassert(port->pcie_rst);
	else
		reset_control_assert(port->pcie_rst);
}

static void mt7621_reset_port(struct mt7621_pcie_port *port)
{
	mt7621_control_assert(port);
	msleep(100);
	mt7621_control_deassert(port);
}

static void setup_cm_memory_region(struct mt7621_pcie *pcie)
{
	struct resource *mem_resource = &pcie->mem;
	struct device *dev = pcie->dev;
	resource_size_t mask;

	if (mips_cps_numiocu(0)) {
		/*
		 * FIXME: hardware doesn't accept mask values with 1s after
		 * 0s (e.g. 0xffef), so it would be great to warn if that's
		 * about to happen
		 */
		mask = ~(mem_resource->end - mem_resource->start);

		write_gcr_reg1_base(mem_resource->start);
		write_gcr_reg1_mask(mask | CM_GCR_REGn_MASK_CMTGT_IOCU0);
		dev_info(dev, "PCI coherence region base: 0x%08llx, mask/settings: 0x%08llx\n",
			(unsigned long long)read_gcr_reg1_base(),
			(unsigned long long)read_gcr_reg1_mask());
	}
}

static int mt7621_pci_parse_request_of_pci_ranges(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	int err;

	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	for_each_of_pci_range(&parser, &range) {
		struct resource *res = NULL;

		switch (range.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			ioremap(range.cpu_addr, range.size);
			res = &pcie->io;
			pcie->offset.io = 0x00000000UL;
			break;
		case IORESOURCE_MEM:
			res = &pcie->mem;
			pcie->offset.mem = 0x00000000UL;
			break;
		}

		if (res)
			of_pci_range_to_resource(&range, node, res);
	}

	err = of_pci_parse_bus_range(node, &pcie->busn);
	if (err < 0) {
		dev_err(dev, "failed to parse bus ranges property: %d\n", err);
		pcie->busn.name = node->name;
		pcie->busn.start = 0;
		pcie->busn.end = 0xff;
		pcie->busn.flags = IORESOURCE_BUS;
	}

	return 0;
}

static int mt7621_pcie_parse_port(struct mt7621_pcie *pcie,
				  struct device_node *node,
				  int slot)
{
	struct mt7621_pcie_port *port;
	struct device *dev = pcie->dev;
	struct device_node *pnode = dev->of_node;
	struct resource regs;
	char name[10];
	int err;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	err = of_address_to_resource(pnode, slot + 1, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	port->base = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	snprintf(name, sizeof(name), "pcie%d", slot);
	port->pcie_rst = devm_reset_control_get_exclusive(dev, name);
	if (PTR_ERR(port->pcie_rst) == -EPROBE_DEFER) {
		dev_err(dev, "failed to get pcie%d reset control\n", slot);
		return PTR_ERR(port->pcie_rst);
	}

	snprintf(name, sizeof(name), "pcie-phy%d", slot);
	port->phy = devm_phy_get(dev, name);
	if (IS_ERR(port->phy))
		return PTR_ERR(port->phy);

	port->slot = slot;
	port->pcie = pcie;

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int mt7621_pcie_parse_dt(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node, *child;
	struct resource regs;
	int err;

	err = of_address_to_resource(node, 0, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	pcie->base = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	pcie->rst = devm_reset_control_get_exclusive(dev, "pcie");
	if (PTR_ERR(pcie->rst) == -EPROBE_DEFER) {
		dev_err(dev, "failed to get pcie reset control\n");
		return PTR_ERR(pcie->rst);
	}

	for_each_available_child_of_node(node, child) {
		int slot;

		err = of_pci_get_devfn(child);
		if (err < 0) {
			dev_err(dev, "failed to parse devfn: %d\n", err);
			return err;
		}

		slot = PCI_SLOT(err);

		err = mt7621_pcie_parse_port(pcie, child, slot);
		if (err)
			return err;
	}

	return 0;
}

static int mt7621_pcie_init_port(struct mt7621_pcie_port *port)
{
	struct mt7621_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	u32 slot = port->slot;
	u32 val = 0;
	int err;

	/*
	 * Any MT7621 Ralink pcie controller that doesn't have 0x0101 at
	 * the end of the chip_id has inverted PCI resets.
	 */
	mt7621_reset_port(port);

	val = read_config(pcie, slot, PCIE_FTS_NUM);
	dev_info(dev, "Port %d N_FTS = %x\n", (unsigned int)val, slot);

	err = phy_init(port->phy);
	if (err) {
		dev_err(dev, "failed to initialize port%d phy\n", slot);
		goto err_phy_init;
	}

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(dev, "failed to power on port%d phy\n", slot);
		goto err_phy_on;
	}

	if ((pcie_port_read(port, RALINK_PCI_STATUS) & PCIE_PORT_LINKUP) == 0) {
		dev_err(dev, "pcie%d no card, disable it (RST & CLK)\n", slot);
		mt7621_control_assert(port);
		port->enabled = false;
		err = -ENODEV;
		goto err_no_link_up;
	}

	port->enabled = true;

	return 0;

err_no_link_up:
	phy_power_off(port->phy);
err_phy_on:
	phy_exit(port->phy);
err_phy_init:
	return err;
}

static void mt7621_pcie_init_ports(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct mt7621_pcie_port *port, *tmp;
	int err;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		u32 slot = port->slot;

		err = mt7621_pcie_init_port(port);
		if (err) {
			dev_err(dev, "Initiating port %d failed\n", slot);
			list_del(&port->list);
		}
	}

	reset_control_assert(pcie->rst);
	rt_sysc_m32(0x30, 2 << 4, SYSC_REG_SYSTEM_CONFIG1);
	rt_sysc_m32(PCIE_CLK_GEN_EN, PCIE_CLK_GEN_DIS, RALINK_PCIE_CLK_GEN);
	rt_sysc_m32(PCIE_CLK_GEN1_DIS, PCIE_CLK_GEN1_EN, RALINK_PCIE_CLK_GEN1);
	rt_sysc_m32(PCIE_CLK_GEN_DIS, PCIE_CLK_GEN_EN, RALINK_PCIE_CLK_GEN);
	msleep(50);
	reset_control_deassert(pcie->rst);
}

static int mt7621_pcie_enable_port(struct mt7621_pcie_port *port)
{
	struct mt7621_pcie *pcie = port->pcie;
	u32 slot = port->slot;
	u32 offset = MT7621_PCIE_OFFSET + (slot * MT7621_NEXT_PORT);
	u32 val;
	int err;

	/* assert port PERST_N */
	val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
	val |= PCIE_PORT_PERST(slot);
	pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);

	/* de-assert port PERST_N */
	val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
	val &= ~PCIE_PORT_PERST(slot);
	pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);

	/* 100ms timeout value should be enough for Gen1 training */
	err = readl_poll_timeout(port->base + RALINK_PCI_STATUS,
				 val, !!(val & PCIE_PORT_LINKUP),
				 20, 100 * USEC_PER_MSEC);
	if (err)
		return -ETIMEDOUT;

	/* enable pcie interrupt */
	val = pcie_read(pcie, RALINK_PCI_PCIMSK_ADDR);
	val |= PCIE_PORT_INT_EN(slot);
	pcie_write(pcie, val, RALINK_PCI_PCIMSK_ADDR);

	/* map 2G DDR region */
	pcie_write(pcie, PCIE_BAR_MAP_MAX | PCIE_BAR_ENABLE,
		   offset + RALINK_PCI_BAR0SETUP_ADDR);
	pcie_write(pcie, MEMORY_BASE,
		   offset + RALINK_PCI_IMBASEBAR0_ADDR);

	/* configure class code and revision ID */
	pcie_write(pcie, PCIE_CLASS_CODE | PCIE_REVISION_ID,
		   offset + RALINK_PCI_CLASS);

	return 0;
}

static void mt7621_pcie_enable_ports(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct mt7621_pcie_port *port;
	u8 num_slots_enabled = 0;
	u32 slot;
	u32 val;

	list_for_each_entry(port, &pcie->ports, list) {
		if (port->enabled) {
			if (mt7621_pcie_enable_port(port)) {
				dev_err(dev, "de-assert port %d PERST_N\n",
					port->slot);
				continue;
			}
			dev_info(dev, "PCIE%d enabled\n", slot);
			num_slots_enabled++;
		}
	}

	for (slot = 0; slot < num_slots_enabled; slot++) {
		val = read_config(pcie, slot, PCI_COMMAND);
		val |= PCI_COMMAND_MASTER;
		write_config(pcie, slot, PCI_COMMAND, val);
		/* configure RC FTS number to 250 when it leaves L0s */
		val = read_config(pcie, slot, PCIE_FTS_NUM);
		val &= ~PCIE_FTS_NUM_MASK;
		val |= PCIE_FTS_NUM_L0(0x50);
		write_config(pcie, slot, PCIE_FTS_NUM, val);
	}
}

static int mt7621_pcie_init_virtual_bridges(struct mt7621_pcie *pcie)
{
	u32 pcie_link_status = 0;
	u32 val = 0;
	struct mt7621_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list) {
		u32 slot = port->slot;

		if (port->enabled)
			pcie_link_status |= BIT(slot);
	}

	if (pcie_link_status == 0)
		return -1;

	/*
	 * pcie(2/1/0) link status	pcie2_num	pcie1_num	pcie0_num
	 * 3'b000			x		x		x
	 * 3'b001			x		x		0
	 * 3'b010			x		0		x
	 * 3'b011			x		1		0
	 * 3'b100			0		x		x
	 * 3'b101			1		x		0
	 * 3'b110			1		0		x
	 * 3'b111			2		1		0
	 */
	switch (pcie_link_status) {
	case 2:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~(MT7621_BR0_MASK | MT7621_BR1_MASK);
		val |= 0x1 << MT7621_BR0_SHIFT;
		val |= 0x0 << MT7621_BR1_SHIFT;
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	case 4:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~MT7621_BR_ALL_MASK;
		val |= 0x1 << MT7621_BR0_SHIFT;
		val |= 0x2 << MT7621_BR1_SHIFT;
		val |= 0x0 << MT7621_BR2_SHIFT;
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	case 5:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~MT7621_BR_ALL_MASK;
		val |= 0x0 << MT7621_BR0_SHIFT;
		val |= 0x2 << MT7621_BR1_SHIFT;
		val |= 0x1 << MT7621_BR2_SHIFT;
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	case 6:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~MT7621_BR_ALL_MASK;
		val |= 0x2 << MT7621_BR0_SHIFT;
		val |= 0x0 << MT7621_BR1_SHIFT;
		val |= 0x1 << MT7621_BR2_SHIFT;
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	}

	return 0;
}

static int mt7621_pcie_request_resources(struct mt7621_pcie *pcie,
					 struct list_head *res)
{
	struct device *dev = pcie->dev;
	int err;

	pci_add_resource_offset(res, &pcie->io, pcie->offset.io);
	pci_add_resource_offset(res, &pcie->mem, pcie->offset.mem);
	pci_add_resource(res, &pcie->busn);

	err = devm_request_pci_bus_resources(dev, res);
	if (err < 0)
		return err;

	return 0;
}

static int mt7621_pcie_register_host(struct pci_host_bridge *host,
				     struct list_head *res)
{
	struct mt7621_pcie *pcie = pci_host_bridge_priv(host);

	list_splice_init(res, &host->windows);
	host->busnr = pcie->busn.start;
	host->dev.parent = pcie->dev;
	host->ops = &mt7621_pci_ops;
	host->map_irq = of_irq_parse_and_map_pci;
	host->swizzle_irq = pci_common_swizzle;
	host->sysdata = pcie;

	return pci_host_probe(host);
}

static int mt7621_pci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt7621_pcie *pcie;
	struct pci_host_bridge *bridge;
	int err;
	LIST_HEAD(res);

	if (!dev->of_node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->dev = dev;
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	err = mt7621_pcie_parse_dt(pcie);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	/* set resources limits */
	iomem_resource.start = 0;
	iomem_resource.end = ~0UL; /* no limit */
	ioport_resource.start = 0;
	ioport_resource.end = ~0UL; /* no limit */

	mt7621_pcie_init_ports(pcie);

	err = mt7621_pcie_init_virtual_bridges(pcie);
	if (err) {
		dev_err(dev, "Nothing is connected in virtual bridges. Exiting...");
		return 0;
	}

	mt7621_pcie_enable_ports(pcie);

	err = mt7621_pci_parse_request_of_pci_ranges(pcie);
	if (err) {
		dev_err(dev, "Error requesting pci resources from ranges");
		return err;
	}

	setup_cm_memory_region(pcie);

	err = mt7621_pcie_request_resources(pcie, &res);
	if (err) {
		dev_err(dev, "Error requesting resources\n");
		return err;
	}

	err = mt7621_pcie_register_host(bridge, &res);
	if (err) {
		dev_err(dev, "Error registering host\n");
		return err;
	}

	return 0;
}

static const struct of_device_id mt7621_pci_ids[] = {
	{ .compatible = "mediatek,mt7621-pci" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_pci_ids);

static struct platform_driver mt7621_pci_driver = {
	.probe = mt7621_pci_probe,
	.driver = {
		.name = "mt7621-pci",
		.of_match_table = of_match_ptr(mt7621_pci_ids),
	},
};

static int __init mt7621_pci_init(void)
{
	return platform_driver_register(&mt7621_pci_driver);
}

arch_initcall(mt7621_pci_init);
