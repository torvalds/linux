/*
 * MediaTek PCIe host controller driver.
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

/* PCIe shared registers */
#define PCIE_SYS_CFG		0x00
#define PCIE_INT_ENABLE		0x0c
#define PCIE_CFG_ADDR		0x20
#define PCIE_CFG_DATA		0x24

/* PCIe per port registers */
#define PCIE_BAR0_SETUP		0x10
#define PCIE_CLASS		0x34
#define PCIE_LINK_STATUS	0x50

#define PCIE_PORT_INT_EN(x)	BIT(20 + (x))
#define PCIE_PORT_PERST(x)	BIT(1 + (x))
#define PCIE_PORT_LINKUP	BIT(0)
#define PCIE_BAR_MAP_MAX	GENMASK(31, 16)

#define PCIE_BAR_ENABLE		BIT(0)
#define PCIE_REVISION_ID	BIT(0)
#define PCIE_CLASS_CODE		(0x60400 << 8)
#define PCIE_CONF_REG(regn)	(((regn) & GENMASK(7, 2)) | \
				((((regn) >> 8) & GENMASK(3, 0)) << 24))
#define PCIE_CONF_FUN(fun)	(((fun) << 8) & GENMASK(10, 8))
#define PCIE_CONF_DEV(dev)	(((dev) << 11) & GENMASK(15, 11))
#define PCIE_CONF_BUS(bus)	(((bus) << 16) & GENMASK(23, 16))
#define PCIE_CONF_ADDR(regn, fun, dev, bus) \
	(PCIE_CONF_REG(regn) | PCIE_CONF_FUN(fun) | \
	 PCIE_CONF_DEV(dev) | PCIE_CONF_BUS(bus))

/* MediaTek specific configuration registers */
#define PCIE_FTS_NUM		0x70c
#define PCIE_FTS_NUM_MASK	GENMASK(15, 8)
#define PCIE_FTS_NUM_L0(x)	((x) & 0xff << 8)

#define PCIE_FC_CREDIT		0x73c
#define PCIE_FC_CREDIT_MASK	(GENMASK(31, 31) | GENMASK(28, 16))
#define PCIE_FC_CREDIT_VAL(x)	((x) << 16)

/**
 * struct mtk_pcie_port - PCIe port information
 * @base: IO mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @reset: pointer to port reset control
 * @sys_ck: pointer to bus clock
 * @phy: pointer to phy control block
 * @lane: lane count
 * @index: port index
 */
struct mtk_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct mtk_pcie *pcie;
	struct reset_control *reset;
	struct clk *sys_ck;
	struct phy *phy;
	u32 lane;
	u32 index;
};

/**
 * struct mtk_pcie - PCIe host information
 * @dev: pointer to PCIe device
 * @base: IO mapped register base
 * @free_ck: free-run reference clock
 * @io: IO resource
 * @pio: PIO resource
 * @mem: non-prefetchable memory resource
 * @busn: bus range
 * @offset: IO / Memory offset
 * @ports: pointer to PCIe port information
 */
struct mtk_pcie {
	struct device *dev;
	void __iomem *base;
	struct clk *free_ck;

	struct resource io;
	struct resource pio;
	struct resource mem;
	struct resource busn;
	struct {
		resource_size_t mem;
		resource_size_t io;
	} offset;
	struct list_head ports;
};

static inline bool mtk_pcie_link_up(struct mtk_pcie_port *port)
{
	return !!(readl(port->base + PCIE_LINK_STATUS) & PCIE_PORT_LINKUP);
}

static void mtk_pcie_subsys_powerdown(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;

	clk_disable_unprepare(pcie->free_ck);

	if (dev->pm_domain) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}
}

static void mtk_pcie_port_free(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;

	devm_iounmap(dev, port->base);
	list_del(&port->list);
	devm_kfree(dev, port);
}

static void mtk_pcie_put_resources(struct mtk_pcie *pcie)
{
	struct mtk_pcie_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		phy_power_off(port->phy);
		clk_disable_unprepare(port->sys_ck);
		mtk_pcie_port_free(port);
	}

	mtk_pcie_subsys_powerdown(pcie);
}

static void __iomem *mtk_pcie_map_bus(struct pci_bus *bus,
				      unsigned int devfn, int where)
{
	struct pci_host_bridge *host = pci_find_host_bridge(bus);
	struct mtk_pcie *pcie = pci_host_bridge_priv(host);

	writel(PCIE_CONF_ADDR(where, PCI_FUNC(devfn), PCI_SLOT(devfn),
			      bus->number), pcie->base + PCIE_CFG_ADDR);

	return pcie->base + PCIE_CFG_DATA + (where & 3);
}

static struct pci_ops mtk_pcie_ops = {
	.map_bus = mtk_pcie_map_bus,
	.read  = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static void mtk_pcie_configure_rc(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	u32 func = PCI_FUNC(port->index << 3);
	u32 slot = PCI_SLOT(port->index << 3);
	u32 val;

	/* enable interrupt */
	val = readl(pcie->base + PCIE_INT_ENABLE);
	val |= PCIE_PORT_INT_EN(port->index);
	writel(val, pcie->base + PCIE_INT_ENABLE);

	/* map to all DDR region. We need to set it before cfg operation. */
	writel(PCIE_BAR_MAP_MAX | PCIE_BAR_ENABLE,
	       port->base + PCIE_BAR0_SETUP);

	/* configure class code and revision ID */
	writel(PCIE_CLASS_CODE | PCIE_REVISION_ID, port->base + PCIE_CLASS);

	/* configure FC credit */
	writel(PCIE_CONF_ADDR(PCIE_FC_CREDIT, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	val = readl(pcie->base + PCIE_CFG_DATA);
	val &= ~PCIE_FC_CREDIT_MASK;
	val |= PCIE_FC_CREDIT_VAL(0x806c);
	writel(PCIE_CONF_ADDR(PCIE_FC_CREDIT, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	writel(val, pcie->base + PCIE_CFG_DATA);

	/* configure RC FTS number to 250 when it leaves L0s */
	writel(PCIE_CONF_ADDR(PCIE_FTS_NUM, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	val = readl(pcie->base + PCIE_CFG_DATA);
	val &= ~PCIE_FTS_NUM_MASK;
	val |= PCIE_FTS_NUM_L0(0x50);
	writel(PCIE_CONF_ADDR(PCIE_FTS_NUM, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	writel(val, pcie->base + PCIE_CFG_DATA);
}

static void mtk_pcie_assert_ports(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	u32 val;

	/* assert port PERST_N */
	val = readl(pcie->base + PCIE_SYS_CFG);
	val |= PCIE_PORT_PERST(port->index);
	writel(val, pcie->base + PCIE_SYS_CFG);

	/* de-assert port PERST_N */
	val = readl(pcie->base + PCIE_SYS_CFG);
	val &= ~PCIE_PORT_PERST(port->index);
	writel(val, pcie->base + PCIE_SYS_CFG);

	/* PCIe v2.0 need at least 100ms delay to train from Gen1 to Gen2 */
	msleep(100);
}

static void mtk_pcie_enable_ports(struct mtk_pcie_port *port)
{
	struct device *dev = port->pcie->dev;
	int err;

	err = clk_prepare_enable(port->sys_ck);
	if (err) {
		dev_err(dev, "failed to enable port%d clock\n", port->index);
		goto err_sys_clk;
	}

	reset_control_assert(port->reset);
	reset_control_deassert(port->reset);

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(dev, "failed to power on port%d phy\n", port->index);
		goto err_phy_on;
	}

	mtk_pcie_assert_ports(port);

	/* if link up, then setup root port configuration space */
	if (mtk_pcie_link_up(port)) {
		mtk_pcie_configure_rc(port);
		return;
	}

	dev_info(dev, "Port%d link down\n", port->index);

	phy_power_off(port->phy);
err_phy_on:
	clk_disable_unprepare(port->sys_ck);
err_sys_clk:
	mtk_pcie_port_free(port);
}

static int mtk_pcie_parse_ports(struct mtk_pcie *pcie,
				struct device_node *node,
				int index)
{
	struct mtk_pcie_port *port;
	struct resource *regs;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	char name[10];
	int err;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	err = of_property_read_u32(node, "num-lanes", &port->lane);
	if (err) {
		dev_err(dev, "missing num-lanes property\n");
		return err;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, index + 1);
	port->base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(port->base)) {
		dev_err(dev, "failed to map port%d base\n", index);
		return PTR_ERR(port->base);
	}

	snprintf(name, sizeof(name), "sys_ck%d", index);
	port->sys_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->sys_ck)) {
		dev_err(dev, "failed to get port%d clock\n", index);
		return PTR_ERR(port->sys_ck);
	}

	snprintf(name, sizeof(name), "pcie-rst%d", index);
	port->reset = devm_reset_control_get_optional(dev, name);
	if (PTR_ERR(port->reset) == -EPROBE_DEFER)
		return PTR_ERR(port->reset);

	/* some platforms may use default PHY setting */
	snprintf(name, sizeof(name), "pcie-phy%d", index);
	port->phy = devm_phy_optional_get(dev, name);
	if (IS_ERR(port->phy))
		return PTR_ERR(port->phy);

	port->index = index;
	port->pcie = pcie;

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int mtk_pcie_subsys_powerup(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *regs;
	int err;

	/* get shared registers */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pcie->base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(pcie->base)) {
		dev_err(dev, "failed to map shared register\n");
		return PTR_ERR(pcie->base);
	}

	pcie->free_ck = devm_clk_get(dev, "free_ck");
	if (IS_ERR(pcie->free_ck)) {
		if (PTR_ERR(pcie->free_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		pcie->free_ck = NULL;
	}

	if (dev->pm_domain) {
		pm_runtime_enable(dev);
		pm_runtime_get_sync(dev);
	}

	/* enable top level clock */
	err = clk_prepare_enable(pcie->free_ck);
	if (err) {
		dev_err(dev, "failed to enable free_ck\n");
		goto err_free_ck;
	}

	return 0;

err_free_ck:
	if (dev->pm_domain) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}

	return err;
}

static int mtk_pcie_setup(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node, *child;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct resource res;
	struct mtk_pcie_port *port, *tmp;
	int err;

	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	for_each_of_pci_range(&parser, &range) {
		err = of_pci_range_to_resource(&range, node, &res);
		if (err < 0)
			return err;

		switch (res.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			pcie->offset.io = res.start - range.pci_addr;

			memcpy(&pcie->pio, &res, sizeof(res));
			pcie->pio.name = node->full_name;

			pcie->io.start = range.cpu_addr;
			pcie->io.end = range.cpu_addr + range.size - 1;
			pcie->io.flags = IORESOURCE_MEM;
			pcie->io.name = "I/O";

			memcpy(&res, &pcie->io, sizeof(res));
			break;

		case IORESOURCE_MEM:
			pcie->offset.mem = res.start - range.pci_addr;

			memcpy(&pcie->mem, &res, sizeof(res));
			pcie->mem.name = "non-prefetchable";
			break;
		}
	}

	err = of_pci_parse_bus_range(node, &pcie->busn);
	if (err < 0) {
		dev_err(dev, "failed to parse bus ranges property: %d\n", err);
		pcie->busn.name = node->name;
		pcie->busn.start = 0;
		pcie->busn.end = 0xff;
		pcie->busn.flags = IORESOURCE_BUS;
	}

	for_each_available_child_of_node(node, child) {
		int index;

		err = of_pci_get_devfn(child);
		if (err < 0) {
			dev_err(dev, "failed to parse devfn: %d\n", err);
			return err;
		}

		index = PCI_SLOT(err);

		err = mtk_pcie_parse_ports(pcie, child, index);
		if (err)
			return err;
	}

	err = mtk_pcie_subsys_powerup(pcie);
	if (err)
		return err;

	/* enable each port, and then check link status */
	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		mtk_pcie_enable_ports(port);

	/* power down PCIe subsys if slots are all empty (link down) */
	if (list_empty(&pcie->ports))
		mtk_pcie_subsys_powerdown(pcie);

	return 0;
}

static int mtk_pcie_request_resources(struct mtk_pcie *pcie)
{
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct list_head *windows = &host->windows;
	struct device *dev = pcie->dev;
	int err;

	pci_add_resource_offset(windows, &pcie->pio, pcie->offset.io);
	pci_add_resource_offset(windows, &pcie->mem, pcie->offset.mem);
	pci_add_resource(windows, &pcie->busn);

	err = devm_request_pci_bus_resources(dev, windows);
	if (err < 0)
		return err;

	pci_remap_iospace(&pcie->pio, pcie->io.start);

	return 0;
}

static int mtk_pcie_register_host(struct pci_host_bridge *host)
{
	struct mtk_pcie *pcie = pci_host_bridge_priv(host);
	struct pci_bus *child;
	int err;

	host->busnr = pcie->busn.start;
	host->dev.parent = pcie->dev;
	host->ops = &mtk_pcie_ops;
	host->map_irq = of_irq_parse_and_map_pci;
	host->swizzle_irq = pci_common_swizzle;

	err = pci_scan_root_bus_bridge(host);
	if (err < 0)
		return err;

	pci_bus_size_bridges(host->bus);
	pci_bus_assign_resources(host->bus);

	list_for_each_entry(child, &host->bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(host->bus);

	return 0;
}

static int mtk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_pcie *pcie;
	struct pci_host_bridge *host;
	int err;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);

	pcie->dev = dev;
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	err = mtk_pcie_setup(pcie);
	if (err)
		return err;

	err = mtk_pcie_request_resources(pcie);
	if (err)
		goto put_resources;

	err = mtk_pcie_register_host(host);
	if (err)
		goto put_resources;

	return 0;

put_resources:
	if (!list_empty(&pcie->ports))
		mtk_pcie_put_resources(pcie);

	return err;
}

static const struct of_device_id mtk_pcie_ids[] = {
	{ .compatible = "mediatek,mt7623-pcie"},
	{ .compatible = "mediatek,mt2701-pcie"},
	{},
};

static struct platform_driver mtk_pcie_driver = {
	.probe = mtk_pcie_probe,
	.driver = {
		.name = "mtk-pcie",
		.of_match_table = mtk_pcie_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(mtk_pcie_driver);
