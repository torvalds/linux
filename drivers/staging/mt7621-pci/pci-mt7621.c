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
#include <linux/gpio/consumer.h>
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
#include <linux/sys_soc.h>
#include <mt7621.h>
#include <ralink_regs.h>

#include "../../pci/pci.h"

/* sysctl */
#define MT7621_GPIO_MODE		0x60

/* MediaTek specific configuration registers */
#define PCIE_FTS_NUM			0x70c
#define PCIE_FTS_NUM_MASK		GENMASK(15, 8)
#define PCIE_FTS_NUM_L0(x)		(((x) & 0xff) << 8)

/* rt_sysc_membase relative registers */
#define RALINK_CLKCFG1			0x30

/* Host-PCI bridge registers */
#define RALINK_PCI_PCICFG_ADDR		0x0000
#define RALINK_PCI_PCIMSK_ADDR		0x000C
#define RALINK_PCI_CONFIG_ADDR		0x0020
#define RALINK_PCI_CONFIG_DATA		0x0024
#define RALINK_PCI_MEMBASE		0x0028
#define RALINK_PCI_IOBASE		0x002C

/* PCICFG virtual bridges */
#define PCIE_P2P_CNT			3
#define PCIE_P2P_BR_DEVNUM_SHIFT(p)	(16 + (p) * 4)
#define PCIE_P2P_BR_DEVNUM0_SHIFT	PCIE_P2P_BR_DEVNUM_SHIFT(0)
#define PCIE_P2P_BR_DEVNUM1_SHIFT	PCIE_P2P_BR_DEVNUM_SHIFT(1)
#define PCIE_P2P_BR_DEVNUM2_SHIFT	PCIE_P2P_BR_DEVNUM_SHIFT(2)
#define PCIE_P2P_BR_DEVNUM_MASK		0xf
#define PCIE_P2P_BR_DEVNUM_MASK_FULL	(0xfff << PCIE_P2P_BR_DEVNUM0_SHIFT)

/* PCIe RC control registers */
#define MT7621_PCIE_OFFSET		0x2000
#define MT7621_NEXT_PORT		0x1000

#define RALINK_PCI_BAR0SETUP_ADDR	0x0010
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
#define PCIE_PORT_LINKUP		BIT(0)

#define PERST_MODE_MASK			GENMASK(11, 10)
#define PERST_MODE_GPIO			BIT(10)
#define PERST_DELAY_MS			100

/**
 * struct mt7621_pcie_port - PCIe port information
 * @base: I/O mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @phy: pointer to PHY control block
 * @pcie_rst: pointer to port reset control
 * @gpio_rst: gpio reset
 * @slot: port slot
 * @irq: GIC irq
 * @enabled: indicates if port is enabled
 */
struct mt7621_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct mt7621_pcie *pcie;
	struct phy *phy;
	struct reset_control *pcie_rst;
	struct gpio_desc *gpio_rst;
	u32 slot;
	int irq;
	bool enabled;
};

/**
 * struct mt7621_pcie - PCIe host information
 * @base: IO Mapped Register Base
 * @io: IO resource
 * @mem: pointer to non-prefetchable memory resource
 * @dev: Pointer to PCIe device
 * @io_map_base: virtual memory base address for io
 * @ports: pointer to PCIe port information
 * @irq_map: irq mapping info according pcie link status
 * @resets_inverted: depends on chip revision
 * reset lines are inverted.
 */
struct mt7621_pcie {
	void __iomem *base;
	struct device *dev;
	struct resource io;
	struct resource *mem;
	unsigned long io_map_base;
	struct list_head ports;
	int irq_map[PCIE_P2P_CNT];
	bool resets_inverted;
};

static inline u32 pcie_read(struct mt7621_pcie *pcie, u32 reg)
{
	return readl(pcie->base + reg);
}

static inline void pcie_write(struct mt7621_pcie *pcie, u32 val, u32 reg)
{
	writel(val, pcie->base + reg);
}

static inline void pcie_rmw(struct mt7621_pcie *pcie, u32 reg, u32 clr, u32 set)
{
	u32 val = readl(pcie->base + reg);

	val &= ~clr;
	val |= set;
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

static inline void mt7621_rst_gpio_pcie_assert(struct mt7621_pcie_port *port)
{
	if (port->gpio_rst)
		gpiod_set_value(port->gpio_rst, 1);
}

static inline void mt7621_rst_gpio_pcie_deassert(struct mt7621_pcie_port *port)
{
	if (port->gpio_rst)
		gpiod_set_value(port->gpio_rst, 0);
}

static inline bool mt7621_pcie_port_is_linkup(struct mt7621_pcie_port *port)
{
	return (pcie_port_read(port, RALINK_PCI_STATUS) & PCIE_PORT_LINKUP) != 0;
}

static inline void mt7621_pcie_port_clk_enable(struct mt7621_pcie_port *port)
{
	rt_sysc_m32(0, PCIE_PORT_CLK_EN(port->slot), RALINK_CLKCFG1);
}

static inline void mt7621_pcie_port_clk_disable(struct mt7621_pcie_port *port)
{
	rt_sysc_m32(PCIE_PORT_CLK_EN(port->slot), 0, RALINK_CLKCFG1);
}

static inline void mt7621_control_assert(struct mt7621_pcie_port *port)
{
	struct mt7621_pcie *pcie = port->pcie;

	if (pcie->resets_inverted)
		reset_control_assert(port->pcie_rst);
	else
		reset_control_deassert(port->pcie_rst);
}

static inline void mt7621_control_deassert(struct mt7621_pcie_port *port)
{
	struct mt7621_pcie *pcie = port->pcie;

	if (pcie->resets_inverted)
		reset_control_deassert(port->pcie_rst);
	else
		reset_control_assert(port->pcie_rst);
}

static void setup_cm_memory_region(struct mt7621_pcie *pcie)
{
	struct resource *mem_resource = pcie->mem;
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

static int mt7621_map_irq(const struct pci_dev *pdev, u8 slot, u8 pin)
{
	struct mt7621_pcie *pcie = pdev->bus->sysdata;
	struct device *dev = pcie->dev;
	int irq = pcie->irq_map[slot];

	dev_info(dev, "bus=%d slot=%d irq=%d\n", pdev->bus->number, slot, irq);
	return irq;
}

static int mt7621_pci_parse_request_of_pci_ranges(struct pci_host_bridge *host)
{
	struct mt7621_pcie *pcie = pci_host_bridge_priv(host);
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct of_pci_range_parser parser;
	struct resource_entry *entry;
	struct of_pci_range range;
	LIST_HEAD(res);

	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	/*
	 * IO_SPACE_LIMIT for MIPS is 0xffff but this platform uses IO at
	 * upper address 0x001e160000 so we have to get the resource from
	 * the DT because when it has been requested it failed and has been
	 * removed from bridge->dma_ranges and bridge->windows. So parse it
	 * and remap it manually to make things work.
	 */
	for_each_of_pci_range(&parser, &range) {
		switch (range.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			pcie->io_map_base =
				(unsigned long)ioremap(range.cpu_addr,
						       range.size);
			of_pci_range_to_resource(&range, node, &pcie->io);
			pcie->io.start = range.cpu_addr;
			pcie->io.end = range.cpu_addr + range.size - 1;
			set_io_port_base(pcie->io_map_base);
			break;
		}
	}

	entry = resource_list_first_type(&host->windows, IORESOURCE_MEM);
	if (!entry) {
		dev_err(dev, "Cannot get memory resource");
		return -EINVAL;
	}

	pcie->mem = entry->res;
	pci_add_resource(&res, &pcie->io);
	pci_add_resource(&res, entry->res);
	list_splice_init(&res, &host->windows);

	return 0;
}

static int mt7621_pcie_parse_port(struct mt7621_pcie *pcie,
				  int slot)
{
	struct mt7621_pcie_port *port;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	char name[10];

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->base = devm_platform_ioremap_resource(pdev, slot + 1);
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
	if (IS_ERR(port->phy) && slot != 1)
		return PTR_ERR(port->phy);

	port->gpio_rst = devm_gpiod_get_index_optional(dev, "reset", slot,
						       GPIOD_OUT_LOW);
	if (IS_ERR(port->gpio_rst)) {
		dev_err(dev, "Failed to get GPIO for PCIe%d\n", slot);
		return PTR_ERR(port->gpio_rst);
	}

	port->slot = slot;
	port->pcie = pcie;

	port->irq = platform_get_irq(pdev, slot);
	if (port->irq < 0) {
		dev_err(dev, "Failed to get IRQ for PCIe%d\n", slot);
		return -ENXIO;
	}

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int mt7621_pcie_parse_dt(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *node = dev->of_node, *child;
	int err;

	pcie->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	for_each_available_child_of_node(node, child) {
		int slot;

		err = of_pci_get_devfn(child);
		if (err < 0) {
			of_node_put(child);
			dev_err(dev, "failed to parse devfn: %d\n", err);
			return err;
		}

		slot = PCI_SLOT(err);

		err = mt7621_pcie_parse_port(pcie, slot);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	return 0;
}

static int mt7621_pcie_init_port(struct mt7621_pcie_port *port)
{
	struct mt7621_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	u32 slot = port->slot;
	int err;

	err = phy_init(port->phy);
	if (err) {
		dev_err(dev, "failed to initialize port%d phy\n", slot);
		return err;
	}

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(dev, "failed to power on port%d phy\n", slot);
		phy_exit(port->phy);
		return err;
	}

	port->enabled = true;

	return 0;
}

static void mt7621_pcie_reset_assert(struct mt7621_pcie *pcie)
{
	struct mt7621_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list) {
		/* PCIe RC reset assert */
		mt7621_control_assert(port);

		/* PCIe EP reset assert */
		mt7621_rst_gpio_pcie_assert(port);
	}

	mdelay(PERST_DELAY_MS);
}

static void mt7621_pcie_reset_rc_deassert(struct mt7621_pcie *pcie)
{
	struct mt7621_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list)
		mt7621_control_deassert(port);
}

static void mt7621_pcie_reset_ep_deassert(struct mt7621_pcie *pcie)
{
	struct mt7621_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list)
		mt7621_rst_gpio_pcie_deassert(port);

	mdelay(PERST_DELAY_MS);
}

static void mt7621_pcie_init_ports(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct mt7621_pcie_port *port, *tmp;
	int err;

	rt_sysc_m32(PERST_MODE_MASK, PERST_MODE_GPIO, MT7621_GPIO_MODE);

	mt7621_pcie_reset_assert(pcie);
	mt7621_pcie_reset_rc_deassert(pcie);

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		u32 slot = port->slot;

		if (slot == 1) {
			port->enabled = true;
			continue;
		}

		err = mt7621_pcie_init_port(port);
		if (err) {
			dev_err(dev, "Initiating port %d failed\n", slot);
			list_del(&port->list);
		}
	}

	mt7621_pcie_reset_ep_deassert(pcie);

	tmp = NULL;
	list_for_each_entry(port, &pcie->ports, list) {
		u32 slot = port->slot;

		if (!mt7621_pcie_port_is_linkup(port)) {
			dev_err(dev, "pcie%d no card, disable it (RST & CLK)\n",
				slot);
			mt7621_control_assert(port);
			mt7621_pcie_port_clk_disable(port);
			port->enabled = false;

			if (slot == 0) {
				tmp = port;
				continue;
			}

			if (slot == 1 && tmp && !tmp->enabled)
				phy_power_off(tmp->phy);

		}
	}
}

static void mt7621_pcie_enable_port(struct mt7621_pcie_port *port)
{
	struct mt7621_pcie *pcie = port->pcie;
	u32 slot = port->slot;
	u32 offset = MT7621_PCIE_OFFSET + (slot * MT7621_NEXT_PORT);
	u32 val;

	/* enable pcie interrupt */
	val = pcie_read(pcie, RALINK_PCI_PCIMSK_ADDR);
	val |= PCIE_PORT_INT_EN(slot);
	pcie_write(pcie, val, RALINK_PCI_PCIMSK_ADDR);

	/* map 2G DDR region */
	pcie_write(pcie, PCIE_BAR_MAP_MAX | PCIE_BAR_ENABLE,
		   offset + RALINK_PCI_BAR0SETUP_ADDR);

	/* configure class code and revision ID */
	pcie_write(pcie, PCIE_CLASS_CODE | PCIE_REVISION_ID,
		   offset + RALINK_PCI_CLASS);
}

static void mt7621_pcie_enable_ports(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct mt7621_pcie_port *port;
	u8 num_slots_enabled = 0;
	u32 slot;
	u32 val;

	/* Setup MEMWIN and IOWIN */
	pcie_write(pcie, 0xffffffff, RALINK_PCI_MEMBASE);
	pcie_write(pcie, pcie->io.start, RALINK_PCI_IOBASE);

	list_for_each_entry(port, &pcie->ports, list) {
		if (port->enabled) {
			mt7621_pcie_port_clk_enable(port);
			mt7621_pcie_enable_port(port);
			dev_info(dev, "PCIE%d enabled\n", port->slot);
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
	u32 n = 0;
	int i = 0;
	u32 p2p_br_devnum[PCIE_P2P_CNT];
	int irqs[PCIE_P2P_CNT];
	struct mt7621_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list) {
		u32 slot = port->slot;

		irqs[i++] = port->irq;
		if (port->enabled)
			pcie_link_status |= BIT(slot);
	}

	if (pcie_link_status == 0)
		return -1;

	/*
	 * Assign device numbers from zero to the enabled ports,
	 * then assigning remaining device numbers to any disabled
	 * ports.
	 */
	for (i = 0; i < PCIE_P2P_CNT; i++)
		if (pcie_link_status & BIT(i))
			p2p_br_devnum[i] = n++;

	for (i = 0; i < PCIE_P2P_CNT; i++)
		if ((pcie_link_status & BIT(i)) == 0)
			p2p_br_devnum[i] = n++;

	pcie_rmw(pcie, RALINK_PCI_PCICFG_ADDR,
		 PCIE_P2P_BR_DEVNUM_MASK_FULL,
		 (p2p_br_devnum[0] << PCIE_P2P_BR_DEVNUM0_SHIFT) |
		 (p2p_br_devnum[1] << PCIE_P2P_BR_DEVNUM1_SHIFT) |
		 (p2p_br_devnum[2] << PCIE_P2P_BR_DEVNUM2_SHIFT));

	/* Assign IRQs */
	n = 0;
	for (i = 0; i < PCIE_P2P_CNT; i++)
		if (pcie_link_status & BIT(i))
			pcie->irq_map[n++] = irqs[i];

	for (i = n; i < PCIE_P2P_CNT; i++)
		pcie->irq_map[i] = -1;

	return 0;
}

static int mt7621_pcie_register_host(struct pci_host_bridge *host)
{
	struct mt7621_pcie *pcie = pci_host_bridge_priv(host);

	host->ops = &mt7621_pci_ops;
	host->map_irq = mt7621_map_irq;
	host->sysdata = pcie;

	return pci_host_probe(host);
}

static const struct soc_device_attribute mt7621_pci_quirks_match[] = {
	{ .soc_id = "mt7621", .revision = "E2" }
};

static int mt7621_pci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct soc_device_attribute *attr;
	struct mt7621_pcie *pcie;
	struct pci_host_bridge *bridge;
	int err;

	if (!dev->of_node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->dev = dev;
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	attr = soc_device_match(mt7621_pci_quirks_match);
	if (attr)
		pcie->resets_inverted = true;

	err = mt7621_pcie_parse_dt(pcie);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	err = mt7621_pci_parse_request_of_pci_ranges(bridge);
	if (err) {
		dev_err(dev, "Error requesting pci resources from ranges");
		return err;
	}

	/* set resources limits */
	ioport_resource.start = pcie->io.start;
	ioport_resource.end = pcie->io.end;

	mt7621_pcie_init_ports(pcie);

	err = mt7621_pcie_init_virtual_bridges(pcie);
	if (err) {
		dev_err(dev, "Nothing is connected in virtual bridges. Exiting...");
		return 0;
	}

	mt7621_pcie_enable_ports(pcie);

	setup_cm_memory_region(pcie);

	err = mt7621_pcie_register_host(bridge);
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

builtin_platform_driver(mt7621_pci_driver);
