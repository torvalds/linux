/*
 * Rockchip AXI PCIe host controller driver
 *
 * Copyright (c) 2016 Rockchip, Inc.
 *
 * Based on the xilinx PCIe driver
 *
 * Bits taken from Synopsys Designware Host controller driver and
 * ARM PCI Host generic driver.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/regmap.h>

#define REF_CLK_100MHZ			(100 * 1000 * 1000)
#define PCIE_CLIENT_BASE		0x0
#define PCIE_RC_CONFIG_BASE		0xa00000
#define PCIE_CORE_CTRL_MGMT_BASE	0x900000
#define PCIE_CORE_AXI_CONF_BASE		0xc00000
#define PCIE_CORE_AXI_INBOUND_BASE	0xc00800

#define PCIE_CLIENT_BASIC_STATUS0	0x44
#define PCIE_CLIENT_BASIC_STATUS1	0x48
#define PCIE_CLIENT_INT_MASK		0x4c
#define PCIE_CLIENT_INT_STATUS		0x50
#define PCIE_CORE_INT_MASK		0x900210
#define PCIE_CORE_INT_STATUS		0x90020c

/** Size of one AXI Region (not Region 0) */
#define	AXI_REGION_SIZE			(0x1 << 20)
/** Overall size of AXI area */
#define	AXI_OVERALL_SIZE		(64 * (0x1 << 20))
/** Size of Region 0, equal to sum of sizes of other regions */
#define	AXI_REGION_0_SIZE		(32 * (0x1 << 20))
#define OB_REG_SIZE_SHIFT		5
#define IB_ROOT_PORT_REG_SIZE_SHIFT	3

#define AXI_WRAPPER_IO_WRITE		0x6
#define AXI_WRAPPER_MEM_WRITE		0x2
#define MAX_AXI_IB_ROOTPORT_REGION_NUM	3
#define	MIN_AXI_ADDR_BITS_PASSED	8

#define ROCKCHIP_PCIE_RPIFR1_INTR_MASK	GENMASK(8, 5)
#define ROCKCHIP_PCIE_RPIFR1_INTR_SHIFT	5
#define CLIENT_INTERRUPTS \
		(LOC_INT | INTA | INTB | INTC | INTD |\
		 CORR_ERR | NFATAL_ERR | FATAL_ERR | DPA_INT | \
		 HOT_RESET | MSG_DONE | LEGACY_DONE)
#define CORE_INTERRUPTS	\
		(PRFPE | CRFPE | RRPE | CRFO | RT | RTR | \
		 PE | MTR | UCR | FCE | CT | UTC | MMVC)
#define PWR_STCG			BIT(0)
#define HOT_PLUG			BIT(1)
#define PHY_INT				BIT(2)
#define UDMA_INT			BIT(3)
#define LOC_INT				BIT(4)
#define INTA				BIT(5)
#define INTB				BIT(6)
#define INTC				BIT(7)
#define INTD				BIT(8)
#define CORR_ERR			BIT(9)
#define NFATAL_ERR			BIT(10)
#define FATAL_ERR			BIT(11)
#define DPA_INT				BIT(12)
#define HOT_RESET			BIT(13)
#define MSG_DONE			BIT(14)
#define LEGACY_DONE			BIT(15)
#define PRFPE				BIT(0)
#define CRFPE				BIT(1)
#define RRPE				BIT(2)
#define PRFO				BIT(3)
#define CRFO				BIT(4)
#define RT				BIT(5)
#define RTR				BIT(6)
#define PE				BIT(7)
#define MTR				BIT(8)
#define UCR				BIT(9)
#define FCE				BIT(10)
#define CT				BIT(11)
#define UTC				BIT(18)
#define MMVC				BIT(19)

#define PCIE_ECAM_BUS(x)		(((x) & 0xFF) << 20)
#define PCIE_ECAM_DEV(x)		(((x) & 0x1F) << 15)
#define PCIE_ECAM_FUNC(x)		(((x) & 0x7) << 12)
#define PCIE_ECAM_REG(x)		(((x) & 0xFFF) << 0)
#define PCIE_ECAM_ADDR(bus, dev, func, reg) \
	  (PCIE_ECAM_BUS(bus) | PCIE_ECAM_DEV(dev) | \
	   PCIE_ECAM_FUNC(func) | PCIE_ECAM_REG(reg))

#define RC_REGION_0_ADDR_TRANS_H	0x00000000
#define RC_REGION_0_ADDR_TRANS_L	0x00000000
#define RC_REGION_0_PASS_BITS		(25 - 1)
#define RC_REGION_1_ADDR_TRANS_H	0x00000000
#define RC_REGION_1_ADDR_TRANS_L	0x00400000
#define RC_REGION_1_PASS_BITS		(20 - 1)
#define MAX_AXI_WRAPPER_REGION_NUM	33
#define PCIE_CLIENT_CONF_ENABLE		BIT(0)
#define PCIE_CLIENT_CONF_LANE_NUM(x)	((x / 2) << 4)
#define PCIE_CLIENT_MODE_RC		BIT(6)
#define PCIE_CLIENT_GEN_SEL_2		BIT(7)
#define PCIE_CLIENT_GEN_SEL_1		0x0

struct rockchip_pcie_port {
	void __iomem *reg_base;
	void __iomem *apb_base;
	struct regmap *grf;
	unsigned int pcie_conf;
	unsigned int pcie_status;
	unsigned int pcie_laneoff;
	struct reset_control *phy_rst;
	struct reset_control *core_rst;
	struct reset_control *mgmt_rst;
	struct reset_control *mgmt_sticky_rst;
	struct reset_control *pipe_rst;
	struct clk *aclk_pcie;
	struct clk *aclk_perf_pcie;
	struct clk *hclk_pcie;
	struct clk *clk_pciephy_ref;
	struct gpio_desc *ep_gpio;
	u32 lanes;
	resource_size_t		io_base;
	struct resource		*cfg;
	struct resource		*io;
	struct resource		*mem;
	struct resource		*busn;
	phys_addr_t		io_bus_addr;
	u32			io_size;
	phys_addr_t		mem_bus_addr;
	u32			mem_size;
	u8	root_bus_nr;
	int irq;
	struct msi_controller *msi;

	struct device *dev;
	struct irq_domain *irq_domain;
};

static inline u32 pcie_read(struct rockchip_pcie_port *port, u32 reg)
{
	return readl(port->apb_base + reg);
}

static inline void pcie_write(struct rockchip_pcie_port *port,
			      u32 val, u32 reg)
{
	writel(val, port->apb_base + reg);
}

static inline void pcie_pb_wr_cfg(struct rockchip_pcie_port *port,
				  u32 addr, u32 data)
{
	regmap_write(port->grf, port->pcie_conf,
		     (0x3ff << 17) | (data << 7) | (addr << 1));
	udelay(1);
	regmap_write(port->grf, port->pcie_conf,
		     (0x1 << 16) | (0x1 << 0));
	udelay(1);
	regmap_write(port->grf, port->pcie_conf,
		     (0x1 << 16) | (0x0 << 0));
}

static inline u32 pcie_pb_rd_cfg(struct rockchip_pcie_port *port,
				 u32 addr)
{
	u32 val;

	regmap_write(port->grf, port->pcie_conf,
		     (0x3ff << 17) | (addr << 1));
	regmap_read(port->grf, port->pcie_status, &val);
	return val;
}

static int rockchip_pcie_valid_config(struct rockchip_pcie_port *pp,
				      struct pci_bus *bus, int dev)
{
	/* access only one slot on each root port */
	if (bus->number == pp->root_bus_nr && dev > 0)
		return 0;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's (Virtual Bridge's) DS side.
	 */
	if (bus->primary == pp->root_bus_nr && dev > 0)
		return 0;

	return 1;
}

static int rockchip_pcie_rd_own_conf(struct rockchip_pcie_port *pp,
				     int where, int size,
				     u32 *val)
{
	void __iomem *addr = pp->apb_base + PCIE_RC_CONFIG_BASE + where;

	if ((uintptr_t)addr & (size - 1)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (size == 4) {
		*val = readl(addr);
	} else if (size == 2) {
		*val = readw(addr);
	} else if (size == 1) {
		*val = readb(addr);
	} else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_wr_own_conf(struct rockchip_pcie_port *pp,
				     int where, int size, u32 val)
{
	u32 tmp;
	int offset;

	offset = (where & (~0x3));
	tmp = readl(pp->apb_base + PCIE_RC_CONFIG_BASE + offset);
	if (size == 4) {
		writel(val, pp->apb_base + PCIE_RC_CONFIG_BASE + where);
	} else if (size == 2) {
		if (where & 0x2)
			tmp = ((tmp & 0xffff) | (val << 16));
		else
			tmp = ((tmp & 0xffff0000) | val);

		writel(tmp, pp->apb_base + PCIE_RC_CONFIG_BASE + offset);
	} else if (size == 1) {
		if ((where & 0x3) == 0)
			tmp = ((tmp & (~0xff)) | val);
		else if ((where & 0x3) == 1)
			tmp = ((tmp & (~0xff00)) | (val << 8));
		else if ((where & 0x3) == 2)
			tmp = ((tmp & (~0xff0000)) | (val << 16));
		else if ((where & 0x3) == 3)
			tmp = ((tmp & (~0xff000000)) | (val << 24));

		writel(tmp, pp->apb_base + PCIE_RC_CONFIG_BASE + offset);
	} else {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_rd_other_conf(struct rockchip_pcie_port *pp,
				       struct pci_bus *bus, u32 devfn,
				       int where, int size, u32 *val)
{
	u32 busdev;

	busdev = PCIE_ECAM_ADDR(bus->number, PCI_SLOT(devfn),
				PCI_FUNC(devfn), where);

	if (busdev & (size - 1)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (size == 4) {
		*val = readl(pp->reg_base + busdev);
	} else if (size == 2) {
		*val = readw(pp->reg_base + busdev);
	} else if (size == 1) {
		*val = readb(pp->reg_base + busdev);
	} else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_wr_other_conf(struct rockchip_pcie_port *pp,
				       struct pci_bus *bus, u32 devfn,
				       int where, int size, u32 val)
{
	u32 busdev;

	busdev = PCIE_ECAM_ADDR(bus->number, PCI_SLOT(devfn),
				PCI_FUNC(devfn), where);
	if (busdev & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		writel(val, pp->reg_base + busdev);
	else if (size == 2)
		writew(val, pp->reg_base + busdev);
	else if (size == 1)
		writeb(val, pp->reg_base + busdev);
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
				 int size, u32 *val)
{
	struct rockchip_pcie_port *pp = bus->sysdata;
	int ret;

	if (rockchip_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number != pp->root_bus_nr)
		ret = rockchip_pcie_rd_other_conf(pp, bus, devfn,
						  where, size, val);
	else
		ret = rockchip_pcie_rd_own_conf(pp, where, size, val);

	return ret;
}

static int rockchip_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
				 int where, int size, u32 val)
{
	struct rockchip_pcie_port *pp = bus->sysdata;
	int ret;

	if (rockchip_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number != pp->root_bus_nr)
		ret = rockchip_pcie_wr_other_conf(pp, bus, devfn,
						  where, size, val);
	else
		ret = rockchip_pcie_wr_own_conf(pp, where, size, val);

	return ret;
}

static struct pci_ops rockchip_pcie_ops = {
	.read = rockchip_pcie_rd_conf,
	.write = rockchip_pcie_wr_conf,
};

/**
 * rockchip_pcie_init_port - Initialize hardware
 * @port: PCIe port information
 */
static int rockchip_pcie_init_port(struct rockchip_pcie_port *port)
{
	int err;
	u32 status;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	gpiod_set_value(port->ep_gpio, 0);

	/* Make sure PCIe relate block is in reset state */
	err = reset_control_assert(port->phy_rst);
	if (err) {
		dev_err(port->dev, "assert phy_rst err %d\n", err);
		return err;
	}
	err = reset_control_assert(port->core_rst);
	if (err) {
		dev_err(port->dev, "assert core_rst err %d\n", err);
		return err;
	}
	err = reset_control_assert(port->mgmt_rst);
	if (err) {
		dev_err(port->dev, "assert mgmt_rst err %d\n", err);
		return err;
	}
	err = reset_control_assert(port->mgmt_sticky_rst);
	if (err) {
		dev_err(port->dev, "assert mgmt_sticky_rst err %d\n", err);
		return err;
	}
	err = reset_control_assert(port->pipe_rst);
	if (err) {
		dev_err(port->dev, "assert pipe_rst err %d\n", err);
		return err;
	}

	pcie_write(port, (0xf << 20) | (0x1 << 16) | PCIE_CLIENT_GEN_SEL_2 |
			  (0x1 << 19) | (0x1 << 3) |
			  PCIE_CLIENT_MODE_RC |
			  PCIE_CLIENT_CONF_LANE_NUM(port->lanes) |
			  PCIE_CLIENT_CONF_ENABLE, PCIE_CLIENT_BASE);

	err = reset_control_deassert(port->phy_rst);
	if (err) {
		dev_err(port->dev, "deassert phy_rst err %d\n", err);
		return err;
	}
	regmap_write(port->grf, port->pcie_conf,
		     (0x3f << 17) | (0x10 << 1));
	err = -EINVAL;
	while (time_before(jiffies, timeout)) {
		regmap_read(port->grf, port->pcie_status, &status);
		if ((status & (1 << 9))) {
			dev_info(port->dev, "pll locked!\n");
			err = 0;
			break;
		}
	}
	if (err) {
		dev_err(port->dev, "pll lock timeout!\n");
		return err;
	}
	pcie_pb_wr_cfg(port, 0x10, 0x8);
	pcie_pb_wr_cfg(port, 0x12, 0x8);

	err = -ETIMEDOUT;
	while (time_before(jiffies, timeout)) {
		regmap_read(port->grf, port->pcie_status, &status);
		if (!(status & (1 << 10))) {
			dev_info(port->dev, "pll output enable done!\n");
			err = 0;
			break;
		}
	}

	if (err) {
		dev_err(port->dev, "pll output enable timeout!\n");
		return err;
	}

	regmap_write(port->grf, port->pcie_conf,
		     (0x3f << 17) | (0x10 << 1));
	err = -EINVAL;
	while (time_before(jiffies, timeout)) {
		regmap_read(port->grf, port->pcie_status, &status);
		if ((status & (1 << 9))) {
			dev_info(port->dev, "pll relocked!\n");
			err = 0;
			break;
		}
	}
	if (err) {
		dev_err(port->dev, "pll relock timeout!\n");
		return err;
	}

	err = reset_control_deassert(port->core_rst);
	if (err) {
		dev_err(port->dev, "deassert core_rst err %d\n", err);
		return err;
	}
	err = reset_control_deassert(port->mgmt_rst);
	if (err) {
		dev_err(port->dev, "deassert mgmt_rst err %d\n", err);
		return err;
	}
	err = reset_control_deassert(port->mgmt_sticky_rst);
	if (err) {
		dev_err(port->dev, "deassert mgmt_sticky_rst err %d\n", err);
		return err;
	}
	err = reset_control_deassert(port->pipe_rst);
	if (err) {
		dev_err(port->dev, "deassert pipe_rst err %d\n", err);
		return err;
	}

	pcie_write(port, 1 << 17 | 1 << 1, PCIE_CLIENT_BASE);

	gpiod_set_value(port->ep_gpio, 1);
	err = -ETIMEDOUT;
	while (time_before(jiffies, timeout)) {
		status = pcie_read(port, PCIE_CLIENT_BASIC_STATUS1);
		if (((status >> 20) & 0x3) == 0x3) {
			dev_info(port->dev, "pcie link training gen1 pass!\n");
			err = 0;
			break;
		}
	}
	if (err) {
		dev_err(port->dev, "pcie link training gen1 timeout!\n");
		return err;
	}

	status = pcie_read(port, 0x9000d0);
	status |= 0x20;
	pcie_write(port, status, 0x9000d0);
	err = -ETIMEDOUT;
	while (time_before(jiffies, timeout)) {
		status = pcie_read(port, PCIE_CORE_CTRL_MGMT_BASE);
		if (((status >> 3) & 0x3) == 0x1) {
			dev_info(port->dev, "pcie link training gen2 pass!\n");
			err = 0;
			break;
		}
	}
	if (err)
		dev_dbg(port->dev, "pcie link training gen2 timeout, force to gen1!\n");

	if (((status >> 3) & 0x3) == 0x0)
		dev_info(port->dev, "pcie link 2.5!\n");
	if (((status >> 3) & 0x3) == 0x1)
		dev_info(port->dev, "pcie link 5.0!\n");

	status = pcie_read(port, PCIE_CORE_CTRL_MGMT_BASE);
	status =  0x1 << ((status >> 1) & 0x3);
	dev_info(port->dev, "current link width is x%d\n", status);

	status = pcie_pb_rd_cfg(port, 0x30);
	if (!((status >> 11) & 0x1))
		dev_dbg(port->dev, "lane A is used\n");
	else
		regmap_write(port->grf, port->pcie_laneoff,
			     (0x1 << 19) | (0x1 << 3));

	status = pcie_pb_rd_cfg(port, 0x31);
	if (!((status >> 11) & 0x1))
		dev_dbg(port->dev, "lane B is used\n");
	else
		regmap_write(port->grf, port->pcie_laneoff,
			     (0x2 << 19) | (0x2 << 3));

	status = pcie_pb_rd_cfg(port, 0x32);
	if (!((status >> 11) & 0x1))
		dev_dbg(port->dev, "lane C is used\n");
	else
		regmap_write(port->grf, port->pcie_laneoff,
			     (0x4 << 19) | (0x4 << 3));

	status = pcie_pb_rd_cfg(port, 0x33);
	if (!((status >> 11) & 0x1))
		dev_dbg(port->dev, "lane D is used\n");
	else
		regmap_write(port->grf, port->pcie_laneoff,
			     (0x8 << 19) | (0x8 << 3));
	return 0;
}

/**
 * rockchip_pcie_parse_dt - Parse Device tree
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int rockchip_pcie_parse_dt(struct rockchip_pcie_port *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct resource regs;
	unsigned int pcie_conf;
	unsigned int pcie_status;
	unsigned int pcie_laneoff;
	int err;

	err = of_address_to_resource(node, 0, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	port->reg_base = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(port->reg_base))
		return PTR_ERR(port->reg_base);

	err = of_address_to_resource(node, 1, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	port->apb_base = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(port->apb_base))
		return PTR_ERR(port->apb_base);

	port->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(port->grf)) {
		dev_err(dev, "Missing rockchip,grf property\n");
		return PTR_ERR(port->grf);
	}

	if (of_property_read_u32(node, "pcie-conf", &pcie_conf)) {
		dev_err(dev, "missing pcie-conf property in node %s\n",
			node->name);
		return -EINVAL;
	}

	port->pcie_conf = pcie_conf;

	if (of_property_read_u32(node, "pcie-status", &pcie_status)) {
		dev_err(dev, "missing pcie-status property in node %s\n",
			node->name);
		return -EINVAL;
	}

	port->pcie_status = pcie_status;

	if (of_property_read_u32(node, "pcie-laneoff", &pcie_laneoff)) {
		dev_err(dev, "missing pcie-laneoff property in node %s\n",
			node->name);
		return -EINVAL;
	}

	port->pcie_laneoff = pcie_laneoff;

	port->lanes = 1;
	err = of_property_read_u32(node, "num-lanes", &port->lanes);
	if (!err && ((port->lanes == 0) ||
		     (port->lanes == 3) ||
		     (port->lanes > 4))) {
		dev_info(dev, "invalid num-lanes, default use one lane\n");
		port->lanes = 1;
	}

	port->phy_rst = devm_reset_control_get(dev, "phy-rst");
	if (IS_ERR(port->phy_rst)) {
		if (PTR_ERR(port->phy_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing phy-rst property in node %s\n",
				node->name);
		err = PTR_ERR(port->phy_rst);
		goto err_aclk_pcie;
	}

	port->core_rst = devm_reset_control_get(dev, "core-rst");
	if (IS_ERR(port->core_rst)) {
		if (PTR_ERR(port->core_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing core-rst property in node %s\n",
				node->name);
		err = PTR_ERR(port->core_rst);
		goto err_aclk_pcie;
	}

	port->mgmt_rst = devm_reset_control_get(dev, "mgmt-rst");
	if (IS_ERR(port->mgmt_rst)) {
		if (PTR_ERR(port->mgmt_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing mgmt-rst property in node %s\n",
				node->name);
		err = PTR_ERR(port->mgmt_rst);
		goto err_aclk_pcie;
	}

	port->mgmt_sticky_rst = devm_reset_control_get(dev, "mgmt-sticky-rst");
	if (IS_ERR(port->mgmt_sticky_rst)) {
		if (PTR_ERR(port->mgmt_sticky_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing mgmt-sticky-rst property in node %s\n",
				node->name);
		err = PTR_ERR(port->mgmt_sticky_rst);
		goto err_aclk_pcie;
	}

	port->pipe_rst = devm_reset_control_get(dev, "pipe-rst");
	if (IS_ERR(port->pipe_rst)) {
		if (PTR_ERR(port->pipe_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing pipe-rst property in node %s\n",
				node->name);
		err = PTR_ERR(port->pipe_rst);
		goto err_aclk_pcie;
	}

	port->ep_gpio = gpiod_get(dev, "ep", GPIOD_OUT_HIGH);
	if (IS_ERR(port->ep_gpio)) {
		dev_err(dev, "missing ep-gpios property in node %s\n",
			node->name);
		return PTR_ERR(port->ep_gpio);
	}

	port->aclk_pcie = devm_clk_get(dev, "aclk_pcie");
	if (IS_ERR(port->aclk_pcie)) {
		dev_err(dev, "aclk_pcie clock not found.\n");
		return PTR_ERR(port->aclk_pcie);
	}

	port->aclk_perf_pcie = devm_clk_get(dev, "aclk_perf_pcie");
	if (IS_ERR(port->aclk_perf_pcie)) {
		dev_err(dev, "aclk_perf_pcie clock not found.\n");
		return PTR_ERR(port->aclk_perf_pcie);
	}

	port->hclk_pcie = devm_clk_get(dev, "hclk_pcie");
	if (IS_ERR(port->hclk_pcie)) {
		dev_err(dev, "hclk_pcie clock not found.\n");
		return PTR_ERR(port->hclk_pcie);
	}

	port->clk_pciephy_ref = devm_clk_get(dev, "clk_pciephy_ref");
	if (IS_ERR(port->clk_pciephy_ref)) {
		dev_err(dev, "clk_pciephy_ref clock not found.\n");
		return PTR_ERR(port->clk_pciephy_ref);
	}

	err = clk_prepare_enable(port->aclk_pcie);
	if (err) {
		dev_err(dev, "Unable to enable aclk_pcie clock.\n");
		goto err_aclk_pcie;
	}

	err = clk_prepare_enable(port->aclk_perf_pcie);
	if (err) {
		dev_err(dev, "Unable to enable aclk_perf_pcie clock.\n");
		goto err_aclk_perf_pcie;
	}

	err = clk_prepare_enable(port->hclk_pcie);
	if (err) {
		dev_err(dev, "Unable to enable hclk_pcie clock.\n");
		goto err_hclk_pcie;
	}

	err = clk_prepare_enable(port->clk_pciephy_ref);
	if (err) {
		dev_err(dev, "Unable to enable hclk_pcie clock.\n");
		goto err_pciephy_ref;
	}

	return 0;

err_pciephy_ref:
	clk_disable_unprepare(port->hclk_pcie);
err_hclk_pcie:
	clk_disable_unprepare(port->aclk_perf_pcie);
err_aclk_perf_pcie:
	clk_disable_unprepare(port->aclk_pcie);
err_aclk_pcie:
	return err;
}

static void rockchip_pcie_msi_enable(struct rockchip_pcie_port *pp)
{
	struct device_node *msi_node;

	msi_node = of_parse_phandle(pp->dev->of_node,
				    "msi-parent", 0);
	if (!msi_node)
		return;

	pp->msi = of_pci_find_msi_chip_by_node(msi_node);
	of_node_put(msi_node);

	if (pp->msi)
		pp->msi->dev = pp->dev;
}

static void rockchip_pcie_enable_interrupts(struct rockchip_pcie_port *pp)
{
	pcie_write(pp, (CLIENT_INTERRUPTS << 16) &
		   (~CLIENT_INTERRUPTS), PCIE_CLIENT_INT_MASK);
	pcie_write(pp, CORE_INTERRUPTS, PCIE_CORE_INT_MASK);
}

static int rockchip_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = rockchip_pcie_intx_map,
};

static int rockchip_pcie_init_irq_domain(struct rockchip_pcie_port *pp)
{
	struct device *dev = pp->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node =  of_get_next_child(node, NULL);

	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return PTR_ERR(pcie_intc_node);
	}
	pp->irq_domain = irq_domain_add_linear(pcie_intc_node, 4,
					       &intx_domain_ops, pp);
	if (!pp->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return PTR_ERR(pp->irq_domain);
	}

	return 0;
}

static irqreturn_t rockchip_pcie_subsys_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie_port *pp = arg;
	u32 reg;
	u32 sub_reg;

	reg = pcie_read(pp, PCIE_CLIENT_INT_STATUS);
	if (reg & LOC_INT) {
		dev_dbg(pp->dev, "local interrupt recived\n");
		sub_reg = pcie_read(pp, PCIE_CORE_INT_STATUS);
		if (sub_reg & PRFPE)
			dev_dbg(pp->dev, "Parity error detected while reading from the PNP Receive FIFO RAM\n");

		if (sub_reg & CRFPE)
			dev_dbg(pp->dev, "Parity error detected while reading from the Completion Receive FIFO RAM\n");

		if (sub_reg & RRPE)
			dev_dbg(pp->dev, "Parity error detected while reading from Replay Buffer RAM\n");

		if (sub_reg & PRFO)
			dev_dbg(pp->dev, "Overflow occurred in the PNP Receive FIFO\n");

		if (sub_reg & CRFO)
			dev_dbg(pp->dev, "Overflow occurred in the Completion Receive FIFO\n");

		if (sub_reg & RT)
			dev_dbg(pp->dev, "Replay timer timed out\n");

		if (sub_reg & RTR)
			dev_dbg(pp->dev, "Replay timer rolled over after 4 transmissions of the same TLP\n");

		if (sub_reg & PE)
			dev_dbg(pp->dev, "Phy error detected on receive side\n");

		if (sub_reg & MTR)
			dev_dbg(pp->dev, "Malformed TLP received from the link\n");

		if (sub_reg & UCR)
			dev_dbg(pp->dev, "Malformed TLP received from the link\n");

		if (sub_reg & FCE)
			dev_dbg(pp->dev, "An error was observed in the flow control advertisements from the other side\n");

		if (sub_reg & CT)
			dev_dbg(pp->dev, "A request timed out waiting for completion\n");

		if (sub_reg & UTC)
			dev_dbg(pp->dev, "Unmapped TC error\n");

		if (sub_reg & MMVC)
			dev_dbg(pp->dev, "MSI mask register changes\n");

		pcie_write(pp, sub_reg, PCIE_CORE_INT_STATUS);
	}

	pcie_write(pp, reg, PCIE_CLIENT_INT_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_pcie_client_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie_port *pp = arg;
	u32 reg;

	reg = pcie_read(pp, PCIE_CLIENT_INT_STATUS);
	if (reg & LEGACY_DONE)
		dev_dbg(pp->dev, "legacy done interrupt recived\n");

	if (reg & MSG_DONE)
		dev_dbg(pp->dev, "message done interrupt recived\n");

	if (reg & HOT_RESET)
		dev_dbg(pp->dev, "hot reset interrupt recived\n");

	if (reg & DPA_INT)
		dev_dbg(pp->dev, "dpa interrupt recived\n");

	if (reg & FATAL_ERR)
		dev_dbg(pp->dev, "fatal error interrupt recived\n");

	if (reg & DPA_INT)
		dev_dbg(pp->dev, "no fatal error interrupt recived\n");

	if (reg & CORR_ERR)
		dev_dbg(pp->dev, "correctable error interrupt recived\n");

	pcie_write(pp, reg, PCIE_CLIENT_INT_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_pcie_legacy_int_handler(int irq, void *arg)
{
	struct rockchip_pcie_port *pp = arg;
	u32 reg;

	reg = pcie_read(pp, PCIE_CLIENT_INT_STATUS);
	reg = (reg & ROCKCHIP_PCIE_RPIFR1_INTR_MASK) >>
	       ROCKCHIP_PCIE_RPIFR1_INTR_SHIFT;
	generic_handle_irq(irq_find_mapping(pp->irq_domain, ffs(reg)));

	pcie_write(pp, reg, PCIE_CLIENT_INT_STATUS);
	return IRQ_HANDLED;
}

static int rockchip_pcie_prog_ob_atu(struct rockchip_pcie_port *pp,
				     int region_no,
				     int type, u8 num_pass_bits,
				     u32 lower_addr, u32 upper_addr)
{
	u32 ob_addr_0 = 0;
	u32 ob_addr_1 = 0;
	u32 ob_desc_0 = 0;
	u32 ob_desc_1 = 0;
	void __iomem *aw_base;

	if (!pp)
		return -EINVAL;
	if (region_no >= MAX_AXI_WRAPPER_REGION_NUM)
		return -EINVAL;
	if ((num_pass_bits + 1) < 8)
		return -EINVAL;
	if (num_pass_bits > 63)
		return -EINVAL;
	if (region_no == 0) {
		if (AXI_REGION_0_SIZE < (2ULL << num_pass_bits))
		return -EINVAL;
	}
	if (region_no != 0) {
		if (AXI_REGION_SIZE < (2ULL << num_pass_bits))
		return -EINVAL;
	}
	aw_base = pp->apb_base + PCIE_CORE_AXI_CONF_BASE;
	aw_base += (region_no << OB_REG_SIZE_SHIFT);

	ob_addr_0 = (ob_addr_0 &
		     ~0x0000003fU) | (num_pass_bits &
		     0x0000003fU);
	ob_addr_0 = (ob_addr_0 &
		     ~0xffffff00U) | (lower_addr & 0xffffff00U);
	ob_addr_1 = upper_addr;
	ob_desc_0 = (1 << 23 | type);

	writel(ob_addr_0, aw_base);
	writel(ob_addr_1, aw_base + 0x4);
	writel(ob_desc_0, aw_base + 0x8);
	writel(ob_desc_1, aw_base + 0xc);

	return 0;
}

static int rockchip_pcie_prog_ib_atu(struct rockchip_pcie_port *pp,
				     int region_no,
				     u8 num_pass_bits,
				     u32 lower_addr,
				     u32 upper_addr)
{
	u32 ib_addr_0 = 0;
	u32 ib_addr_1 = 0;
	void __iomem *aw_base;

	if (!pp)
		return -EINVAL;
	if (region_no > MAX_AXI_IB_ROOTPORT_REGION_NUM)
		return -EINVAL;
	if ((num_pass_bits + 1) < MIN_AXI_ADDR_BITS_PASSED)
		return -EINVAL;
	if (num_pass_bits > 63)
		return -EINVAL;
	aw_base = pp->apb_base + PCIE_CORE_AXI_INBOUND_BASE;
	aw_base += (region_no << IB_ROOT_PORT_REG_SIZE_SHIFT);
	ib_addr_0 = (ib_addr_0 &
		     ~0x0000003fU) | (num_pass_bits &
		     0x0000003fU);

	ib_addr_0 = (ib_addr_0 & ~0xffffff00U) |
		     ((lower_addr << 8) & 0xffffff00U);
	ib_addr_1 = upper_addr;
	writel(ib_addr_0, aw_base);
	writel(ib_addr_1, aw_base + 0x4);

	return 0;
}

static int rockchip_pcie_probe(struct platform_device *pdev)
{
	struct rockchip_pcie_port *port;
	struct device *dev = &pdev->dev;
	struct pci_bus *bus, *child;
	struct resource_entry *win;
	int reg_no;
	int err = 0;
	int irq;
	LIST_HEAD(res);

	if (!dev->of_node)
		return -ENODEV;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	irq = platform_get_irq_byname(pdev, "pcie-sys");
	if (irq < 0) {
		dev_err(dev, "missing pcie_sys IRQ resource\n");
		return -EINVAL;
	}
	err = devm_request_irq(dev, irq, rockchip_pcie_subsys_irq_handler,
			       IRQF_SHARED, "pcie-sys", port);
	if (err) {
		dev_err(dev, "failed to request pcie subsystem irq\n");
		return err;
	}

	port->irq = platform_get_irq_byname(pdev, "pcie-legacy");
	if (port->irq < 0) {
		dev_err(dev, "missing pcie_legacy IRQ resource\n");
		return -EINVAL;
	}
	err = devm_request_irq(dev, port->irq,
			       rockchip_pcie_legacy_int_handler,
			       IRQF_SHARED,
			       "pcie-legacy",
			       port);
	if (err) {
		dev_err(&pdev->dev, "failed to request pcie-legacy irq\n");
		return err;
	}

	irq = platform_get_irq_byname(pdev, "pcie-client");
	if (irq < 0) {
		dev_err(dev, "missing pcie-client IRQ resource\n");
		return -EINVAL;
	}
	err = devm_request_irq(dev, irq, rockchip_pcie_client_irq_handler,
			       IRQF_SHARED, "pcie-client", port);
	if (err) {
		dev_err(dev, "failed to request pcie client irq\n");
		return err;
	}

	port->dev = dev;
	err = rockchip_pcie_parse_dt(port);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	err = rockchip_pcie_init_port(port);
	if (err)
		return err;

	platform_set_drvdata(pdev, port);

	rockchip_pcie_enable_interrupts(port);
	if (!IS_ENABLED(CONFIG_PCI_MSI)) {
		err = rockchip_pcie_init_irq_domain(port);
		if (err < 0)
			return err;
	}

	err = of_pci_get_host_bridge_resources(dev->of_node, 0, 0xff,
					       &res, &port->io_base);
	if (err)
		return err;
	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &res) {
		switch (resource_type(win->res)) {
		case IORESOURCE_IO:
			port->io = win->res;
			port->io->name = "I/O";
			port->io_size = resource_size(port->io);
			port->io_bus_addr = port->io->start - win->offset;
			err = pci_remap_iospace(port->io, port->io_base);
			if (err) {
				dev_warn(port->dev, "error %d: failed to map resource %pR\n",
					 err, port->io);
				continue;
			}
			break;
		case IORESOURCE_MEM:
			port->mem = win->res;
			port->mem->name = "MEM";
			port->mem_size = resource_size(port->mem);
			port->mem_bus_addr = port->mem->start - win->offset;
			break;
		case 0:
			port->cfg = win->res;
			break;
		case IORESOURCE_BUS:
			port->busn = win->res;
			break;
		default:
			continue;
		}
	}

	pcie_write(port, 0x6040000, PCIE_RC_CONFIG_BASE + 0x8);
	pcie_write(port, 0x0, PCIE_CORE_CTRL_MGMT_BASE + 0x300);

	pcie_write(port, (RC_REGION_0_ADDR_TRANS_L + RC_REGION_0_PASS_BITS),
		   PCIE_CORE_AXI_CONF_BASE);
	pcie_write(port, RC_REGION_0_ADDR_TRANS_H,
		   PCIE_CORE_AXI_CONF_BASE + 0x4);
	pcie_write(port, 0x0080000a, PCIE_CORE_AXI_CONF_BASE + 0x8);
	pcie_write(port, 0x00000000, PCIE_CORE_AXI_CONF_BASE + 0xc);

	for (reg_no = 0; reg_no < (port->mem_size >> 20); reg_no++) {
		err = rockchip_pcie_prog_ob_atu(port, reg_no + 1,
						AXI_WRAPPER_MEM_WRITE,
						20 - 1,
						port->mem_bus_addr +
							(reg_no << 20),
						0);
		if (err) {
			dev_err(dev, "Program RC outbound atu failed\n");
			return err;
		}
	}

	err = rockchip_pcie_prog_ib_atu(port, 2, 32 - 1, 0x0, 0);
	if (err) {
		dev_err(dev, "Program RC inbound atu failed\n");
		return err;
	}

	rockchip_pcie_msi_enable(port);

	port->root_bus_nr = port->busn->start;
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		bus = pci_scan_root_bus_msi(port->dev, port->root_bus_nr,
					    &rockchip_pcie_ops, port, &res,
					    port->msi);
	} else {
		bus = pci_scan_root_bus(&pdev->dev, 0,
					&rockchip_pcie_ops, port, &res);
	}
	if (!bus)
		return -ENOMEM;

	if (!pci_has_flag(PCI_PROBE_ONLY)) {
		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);
		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}

	pci_bus_add_devices(bus);

	return err;
}

static int rockchip_pcie_remove(struct platform_device *pdev)
{
	struct rockchip_pcie_port *port = platform_get_drvdata(pdev);

	clk_disable_unprepare(port->hclk_pcie);
	clk_disable_unprepare(port->aclk_perf_pcie);
	clk_disable_unprepare(port->aclk_pcie);
	clk_disable_unprepare(port->clk_pciephy_ref);

	return 0;
}

static const struct of_device_id rockchip_pcie_of_match[] = {
	{ .compatible = "rockchip,rk3399-pcie", },
	{}
};

static struct platform_driver rockchip_pcie_driver = {
	.driver = {
		.name = "rockchip-pcie",
		.of_match_table = rockchip_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rockchip_pcie_probe,
	.remove = rockchip_pcie_remove,
};
module_platform_driver(rockchip_pcie_driver);

MODULE_AUTHOR("Rockchip Inc");
MODULE_DESCRIPTION("Rockchip AXI PCIe driver");
MODULE_LICENSE("GPL v2");
