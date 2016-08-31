/*
 * Rockchip AXI PCIe host controller driver
 *
 * Copyright (c) 2016 Rockchip, Inc.
 *
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *         Wenrui Li <wenrui.li@rock-chips.com>
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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/regmap.h>

#define PCIE_CLIENT_BASE			0x0
#define PCIE_RC_CONFIG_NORMAL_BASE		0x800000
#define PCIE_RC_CONFIG_BASE			0xa00000
#define PCIE_RC_CONFIG_LCSR			0xd0
#define  PCIE_RC_CONFIG_LCSR_LBMIE		BIT(10)
#define  PCIE_RC_CONFIG_LCSR_LABIE		BIT(11)
#define  PCIE_RC_CONFIG_LCSR_LBMS		BIT(30)
#define  PCIE_RC_CONFIG_LCSR_LAMS		BIT(31)
#define PCIE_CORE_CTRL_MGMT_BASE		0x900000
#define PCIE_CORE_AXI_CONF_BASE			0xc00000
#define PCIE_CORE_AXI_INBOUND_BASE		0xc00800
#define PCIE_CLIENT_BASIC_STATUS1		0x48
#define PCIE_CLIENT_INT_MASK			0x4c
#define PCIE_CLIENT_INT_STATUS			0x50
#define  PCIE_CLIENT_INT_LEGACY_DONE		BIT(15)
#define  PCIE_CLIENT_INT_MSG			BIT(14)
#define  PCIE_CLIENT_INT_HOT_RST		BIT(13)
#define  PCIE_CLIENT_INT_DPA			BIT(12)
#define  PCIE_CLIENT_INT_FATAL_ERR		BIT(11)
#define  PCIE_CLIENT_INT_NFATAL_ERR		BIT(10)
#define  PCIE_CLIENT_INT_CORR_ERR		BIT(9)
#define  PCIE_CLIENT_INT_INTD			BIT(8)
#define  PCIE_CLIENT_INT_INTC			BIT(7)
#define  PCIE_CLIENT_INT_INTB			BIT(6)
#define  PCIE_CLIENT_INT_INTA			BIT(5)
#define  PCIE_CLIENT_INT_LOCAL			BIT(4)
#define  PCIE_CLIENT_INT_UDMA			BIT(3)
#define  PCIE_CLIENT_INT_PHY			BIT(2)
#define  PCIE_CLIENT_INT_HOT_PLUG		BIT(1)
#define  PCIE_CLIENT_INT_PWR_STCG		BIT(0)
#define PCIE_RC_CONFIG_RID_CCR			0x8
#define PCIE_RC_CONFIG_LCS			0xd0
#define PCIE_RC_BAR_CONF			0x300
#define PCIE_CORE_OB_REGION_ADDR1		0x4
#define PCIE_CORE_OB_REGION_DESC0		0x8
#define PCIE_CORE_OB_REGION_DESC1		0xc
#define PCIE_CORE_OB_REGION_ADDR0_NUM_BITS	0x3f
#define PCIE_CORE_OB_REGION_ADDR0_LO_ADDR	0xffffff00
#define PCIE_CORE_IB_REGION_ADDR0_NUM_BITS	0x3f
#define PCIE_CORE_IB_REGION_ADDR0_LO_ADDR	0xffffff00
#define PCIE_RP_IB_ADDR_TRANS			0x4
#define PCIE_CORE_INT_MASK			0x900210
#define PCIE_CORE_INT_STATUS			0x90020c
#define  PCIE_CORE_INT_PRFPE			BIT(0)
#define  PCIE_CORE_INT_CRFPE			BIT(1)
#define  PCIE_CORE_INT_RRPE			BIT(2)
#define  PCIE_CORE_INT_PRFO			BIT(3)
#define  PCIE_CORE_INT_CRFO			BIT(4)
#define  PCIE_CORE_INT_RT			BIT(5)
#define  PCIE_CORE_INT_RTR			BIT(6)
#define  PCIE_CORE_INT_PE			BIT(7)
#define  PCIE_CORE_INT_MTR			BIT(8)
#define  PCIE_CORE_INT_UCR			BIT(9)
#define  PCIE_CORE_INT_FCE			BIT(10)
#define  PCIE_CORE_INT_CT			BIT(11)
#define  PCIE_CORE_INT_UTC			BIT(18)
#define  PCIE_CORE_INT_MMVC			BIT(19)

/* Size of one AXI Region (not Region 0) */
#define AXI_REGION_SIZE				BIT(20)
/* Size of Region 0, equal to sum of sizes of other regions */
#define AXI_REGION_0_SIZE			(32 * (0x1 << 20))
#define OB_REG_SIZE_SHIFT			5
#define IB_ROOT_PORT_REG_SIZE_SHIFT		3
#define AXI_WRAPPER_IO_WRITE			0x6
#define AXI_WRAPPER_MEM_WRITE			0x2

#define MAX_AXI_IB_ROOTPORT_REGION_NUM		3
#define MIN_AXI_ADDR_BITS_PASSED		8
#define ROCKCHIP_VENDOR_ID			0x1d87
#define PCIE_ECAM_BUS(x)			(((x) & 0xff) << 20)
#define PCIE_ECAM_DEV(x)			(((x) & 0x1f) << 15)
#define PCIE_ECAM_FUNC(x)			(((x) & 0x7) << 12)
#define PCIE_ECAM_REG(x)			(((x) & 0xfff) << 0)
#define PCIE_ECAM_ADDR(bus, dev, func, reg) \
	  (PCIE_ECAM_BUS(bus) | PCIE_ECAM_DEV(dev) | \
	   PCIE_ECAM_FUNC(func) | PCIE_ECAM_REG(reg))

/*
  * The higher 16-bit of this register is used for write protection
  * only if BIT(x + 16) set to 1 the BIT(x) can be written.
  */
#define HIWORD_UPDATE(val, mask, shift) \
	((val) << (shift) | (mask) << ((shift) + 16))

#define RC_REGION_0_ADDR_TRANS_H		0x00000000
#define RC_REGION_0_ADDR_TRANS_L		0x00000000
#define RC_REGION_0_PASS_BITS			(25 - 1)
#define RC_REGION_1_ADDR_TRANS_H		0x00000000
#define RC_REGION_1_ADDR_TRANS_L		0x00400000
#define RC_REGION_1_PASS_BITS			(20 - 1)
#define MAX_AXI_WRAPPER_REGION_NUM		33
#define PCIE_CORE_LCSR_RETRAIN_LINK		BIT(5)
#define PCIE_CLIENT_CONF_ENABLE			BIT(0)
#define PCIE_CLIENT_CONF_ENABLE_SHIFT		0
#define PCIE_CLIENT_CONF_ENABLE_MASK		0x1
#define PCIE_CLIENT_LINK_TRAIN_ENABLE		1
#define PCIE_CLIENT_LINK_TRAIN_SHIFT		1
#define PCIE_CLIENT_LINK_TRAIN_MASK		0x1
#define PCIE_CLIENT_ARI_ENABLE			BIT(0)
#define PCIE_CLIENT_ARI_ENABLE_SHIFT		3
#define PCIE_CLIENT_ARI_ENABLE_MASK		0x1
#define PCIE_CLIENT_CONF_LANE_NUM(x)		(x / 2)
#define PCIE_CLIENT_CONF_LANE_NUM_SHIFT		4
#define PCIE_CLIENT_CONF_LANE_NUM_MASK		0x3
#define PCIE_CLIENT_MODE_RC			BIT(0)
#define PCIE_CLIENT_MODE_SHIFT			6
#define PCIE_CLIENT_MODE_MASK			0x1
#define PCIE_CLIENT_GEN_SEL_2			1
#define PCIE_CLIENT_GEN_SEL_1			0
#define PCIE_CLIENT_GEN_SEL_SHIFT		7
#define PCIE_CLIENT_GEN_SEL_MASK		0x1
#define PCIE_CLIENT_LINK_STATUS_UP		0x3
#define PCIE_CLIENT_LINK_STATUS_SHIFT		20
#define PCIE_CLIENT_LINK_STATUS_MASK		0x3
#define PCIE_CORE_PL_CONF_SPEED_2_5G		0x0
#define PCIE_CORE_PL_CONF_SPEED_5G		0x1
#define PCIE_CORE_PL_CONF_SPEED_8G		0x2
#define PCIE_CORE_PL_CONF_SPEED_SHIFT		3
#define PCIE_CORE_PL_CONF_SPEED_MASK		0x3
#define PCIE_CORE_PL_CONF_LANE_SHIFT		1
#define PCIE_CORE_PL_CONF_LANE_MASK		0x3
#define PCIE_CORE_RC_CONF_SCC_SHIFT		16

#define ROCKCHIP_PCIE_RPIFR1_INTR_MASK		GENMASK(8, 5)
#define ROCKCHIP_PCIE_RPIFR1_INTR_SHIFT		5

#define PCIE_CORE_INT \
		(PCIE_CORE_INT_PRFPE | PCIE_CORE_INT_CRFPE | \
		 PCIE_CORE_INT_RRPE | PCIE_CORE_INT_CRFO | \
		 PCIE_CORE_INT_RT | PCIE_CORE_INT_RTR | \
		 PCIE_CORE_INT_PE | PCIE_CORE_INT_MTR | \
		 PCIE_CORE_INT_UCR | PCIE_CORE_INT_FCE | \
		 PCIE_CORE_INT_CT | PCIE_CORE_INT_UTC | \
		 PCIE_CORE_INT_MMVC)

#define PCIE_CLIENT_INT_SUBSYSTEM \
	(PCIE_CLIENT_INT_PWR_STCG | PCIE_CLIENT_INT_HOT_PLUG | \
	PCIE_CLIENT_INT_PHY | PCIE_CLIENT_INT_UDMA | \
	PCIE_CLIENT_INT_LOCAL)

#define PCIE_CLIENT_INT_LEGACY \
	(PCIE_CLIENT_INT_INTA | PCIE_CLIENT_INT_INTB | \
	PCIE_CLIENT_INT_INTC | PCIE_CLIENT_INT_INTD)

#define PCIE_CLIENT_INT_CLI \
	(PCIE_CLIENT_INT_CORR_ERR | PCIE_CLIENT_INT_NFATAL_ERR | \
	PCIE_CLIENT_INT_FATAL_ERR | PCIE_CLIENT_INT_DPA | \
	PCIE_CLIENT_INT_HOT_RST | PCIE_CLIENT_INT_MSG | \
	PCIE_CLIENT_INT_LEGACY_DONE | PCIE_CLIENT_INT_LEGACY | \
	PCIE_CLIENT_INT_PHY)

struct rockchip_pcie_port {
	void	__iomem *reg_base;
	void	__iomem *apb_base;
	struct	phy *phy;
	struct	reset_control *core_rst;
	struct	reset_control *mgmt_rst;
	struct	reset_control *mgmt_sticky_rst;
	struct	reset_control *pipe_rst;
	struct	clk *aclk_pcie;
	struct	clk *aclk_perf_pcie;
	struct	clk *hclk_pcie;
	struct	clk *clk_pcie_pm;
	struct	regulator *vpcie3v3; /* 3.3V power supply */
	struct	regulator *vpcie1v8; /* 1.8V power supply */
	struct	regulator *vpcie0v9; /* 0.9V power supply */
	struct	gpio_desc *ep_gpio;
	u32	lanes;
	u8	root_bus_nr;
	struct	device *dev;
	struct	irq_domain *irq_domain;
};

static inline u32 pcie_read(struct rockchip_pcie_port *port, u32 reg)
{
	return readl(port->apb_base + reg);
}

static inline void pcie_write(struct rockchip_pcie_port *port, u32 val, u32 reg)
{
	writel(val, port->apb_base + reg);
}

static void rockchip_pcie_enable_bw_int(struct rockchip_pcie_port *port)
{
	u32 status;

	status = pcie_read(port, PCIE_RC_CONFIG_BASE + PCIE_RC_CONFIG_LCSR);
	status |= (PCIE_RC_CONFIG_LCSR_LBMIE | PCIE_RC_CONFIG_LCSR_LABIE);
	pcie_write(port, status, PCIE_RC_CONFIG_BASE + PCIE_RC_CONFIG_LCSR);
}

static void rockchip_pcie_clr_bw_int(struct rockchip_pcie_port *port)
{
	u32 status;

	status = pcie_read(port, PCIE_RC_CONFIG_BASE + PCIE_RC_CONFIG_LCSR);
	status |= (PCIE_RC_CONFIG_LCSR_LBMS | PCIE_RC_CONFIG_LCSR_LAMS);
	pcie_write(port, status, PCIE_RC_CONFIG_BASE + PCIE_RC_CONFIG_LCSR);
}

static int rockchip_pcie_valid_config(struct rockchip_pcie_port *pp,
				      struct pci_bus *bus, int dev)
{
	/* access only one slot on each root port */
	if (bus->number == pp->root_bus_nr && dev > 0)
		return 0;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's downstream side.
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

	if (!IS_ALIGNED((uintptr_t)addr, size)) {
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
	u32 mask, tmp, offset;

	offset = (where & (~0x3));

	if (size == 4) {
		writel(val, pp->apb_base + PCIE_RC_CONFIG_BASE + offset);
		return PCIBIOS_SUCCESSFUL;
	}

	mask = ~(((1 << (size * 8)) - 1) << ((where & 0x3) * 8));

	tmp = readl(pp->apb_base + PCIE_RC_CONFIG_BASE + offset) & mask;
	tmp |= val << ((where & 0x3) * 8);
	writel(tmp, pp->apb_base + PCIE_RC_CONFIG_BASE + offset);

	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_rd_other_conf(struct rockchip_pcie_port *pp,
				       struct pci_bus *bus, u32 devfn,
				       int where, int size, u32 *val)
{
	u32 busdev;

	busdev = PCIE_ECAM_ADDR(bus->number, PCI_SLOT(devfn),
				PCI_FUNC(devfn), where);

	if (!IS_ALIGNED(busdev, size)) {
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
	if (!IS_ALIGNED(busdev, size))
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

	if (!rockchip_pcie_valid_config(pp, bus, PCI_SLOT(devfn))) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number == pp->root_bus_nr)
		return rockchip_pcie_rd_own_conf(pp, where, size, val);

	return rockchip_pcie_rd_other_conf(pp, bus, devfn, where, size, val);

}

static int rockchip_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
				 int where, int size, u32 val)
{
	struct rockchip_pcie_port *pp = bus->sysdata;

	if (!rockchip_pcie_valid_config(pp, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == pp->root_bus_nr)
		return rockchip_pcie_wr_own_conf(pp, where, size, val);

	return rockchip_pcie_wr_other_conf(pp, bus, devfn, where, size, val);
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
	unsigned long timeout;

	gpiod_set_value(port->ep_gpio, 0);

	err = phy_init(port->phy);
	if (err < 0) {
		dev_err(port->dev, "fail to init phy, err %d\n", err);
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

	pcie_write(port,
		   HIWORD_UPDATE(PCIE_CLIENT_CONF_ENABLE,
				 PCIE_CLIENT_CONF_ENABLE_MASK,
				 PCIE_CLIENT_CONF_ENABLE_SHIFT) |
		   HIWORD_UPDATE(PCIE_CLIENT_CONF_LANE_NUM(port->lanes),
				 PCIE_CLIENT_CONF_LANE_NUM_MASK,
				 PCIE_CLIENT_CONF_LANE_NUM_SHIFT) |
		   HIWORD_UPDATE(PCIE_CLIENT_MODE_RC,
				 PCIE_CLIENT_MODE_MASK,
				 PCIE_CLIENT_MODE_SHIFT) |
		   HIWORD_UPDATE(PCIE_CLIENT_ARI_ENABLE,
				 PCIE_CLIENT_ARI_ENABLE_MASK,
				 PCIE_CLIENT_ARI_ENABLE_SHIFT) |
		   HIWORD_UPDATE(PCIE_CLIENT_GEN_SEL_2,
				 PCIE_CLIENT_GEN_SEL_MASK,
				 PCIE_CLIENT_GEN_SEL_SHIFT),
		   PCIE_CLIENT_BASE);

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(port->dev, "fail to power on phy, err %d\n", err);
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

	/* Enable Gen1 training */
	pcie_write(port,
		   HIWORD_UPDATE(PCIE_CLIENT_LINK_TRAIN_ENABLE,
				 PCIE_CLIENT_LINK_TRAIN_MASK,
				 PCIE_CLIENT_LINK_TRAIN_SHIFT),
		   PCIE_CLIENT_BASE);

	gpiod_set_value(port->ep_gpio, 1);

	/* 500ms timeout value should be enough for Gen1/2 training */
	timeout = jiffies + msecs_to_jiffies(500);

	for (;;) {
		status = pcie_read(port, PCIE_CLIENT_BASIC_STATUS1);
		if (((status >> PCIE_CLIENT_LINK_STATUS_SHIFT) &
		      PCIE_CLIENT_LINK_STATUS_MASK) ==
		      PCIE_CLIENT_LINK_STATUS_UP) {
			dev_dbg(port->dev, "PCIe link training gen1 pass!\n");
			break;
		}

		msleep(20);

		if (!time_before(jiffies, timeout)) {
			err = -ETIMEDOUT;
			break;
		}

	}

	/* Double check gen1 training */
	if (err) {
		status = pcie_read(port, PCIE_CLIENT_BASIC_STATUS1);
		err = (((status >> PCIE_CLIENT_LINK_STATUS_SHIFT) &
			PCIE_CLIENT_LINK_STATUS_MASK) ==
			PCIE_CLIENT_LINK_STATUS_UP) ? 0 : -ETIMEDOUT;
		if (err) {
			dev_err(port->dev, "PCIe link training gen1 timeout!\n");
			return err;
		}
	}

	/*
	 * Enable retrain for gen2. This should be configured only after
	 * gen1 finished.
	 */
	status = pcie_read(port,
			   PCIE_RC_CONFIG_LCS + PCIE_RC_CONFIG_BASE);
	status |= PCIE_CORE_LCSR_RETRAIN_LINK;
	pcie_write(port, status,
		   PCIE_RC_CONFIG_LCS + PCIE_RC_CONFIG_BASE);

	timeout = jiffies + msecs_to_jiffies(500);
	for (;;) {
		status = pcie_read(port, PCIE_CORE_CTRL_MGMT_BASE);
		if (((status >> PCIE_CORE_PL_CONF_SPEED_SHIFT) &
		     PCIE_CORE_PL_CONF_SPEED_MASK) ==
		     PCIE_CORE_PL_CONF_SPEED_5G) {
			dev_dbg(port->dev, "PCIe link training gen2 pass!\n");
			break;
		}

		msleep(20);

		if (!time_before(jiffies, timeout)) {
			err = -ETIMEDOUT;
			break;
		}
	}

	/* Double check gen2 training */
	if (err) {
		status = pcie_read(port, PCIE_CORE_CTRL_MGMT_BASE);
		err = (((status >> PCIE_CORE_PL_CONF_SPEED_SHIFT) &
			PCIE_CORE_PL_CONF_SPEED_MASK) ==
			PCIE_CORE_PL_CONF_SPEED_5G) ? 0 : -ETIMEDOUT;
		if (err)
			dev_dbg(port->dev, "PCIe link training gen2 timeout, fall back to gen1!\n");
	}

	/* Check the final link width from negotiated lane counter from MGMT */
	status = pcie_read(port, PCIE_CORE_CTRL_MGMT_BASE);
	status =  0x1 << ((status >> PCIE_CORE_PL_CONF_LANE_SHIFT) &
			   PCIE_CORE_PL_CONF_LANE_MASK);
	dev_dbg(port->dev, "current link width is x%d\n", status);

	pcie_write(port, ROCKCHIP_VENDOR_ID, PCIE_RC_CONFIG_BASE);
	pcie_write(port, PCI_CLASS_BRIDGE_PCI << PCIE_CORE_RC_CONF_SCC_SHIFT,
		   PCIE_RC_CONFIG_BASE + PCIE_RC_CONFIG_RID_CCR);
	pcie_write(port, 0x0, PCIE_CORE_CTRL_MGMT_BASE + PCIE_RC_BAR_CONF);

	pcie_write(port, (RC_REGION_0_ADDR_TRANS_L + RC_REGION_0_PASS_BITS),
		   PCIE_CORE_AXI_CONF_BASE);
	pcie_write(port, RC_REGION_0_ADDR_TRANS_H,
		   PCIE_CORE_AXI_CONF_BASE + PCIE_CORE_OB_REGION_ADDR1);
	pcie_write(port, 0x0080000a,
		   PCIE_CORE_AXI_CONF_BASE + PCIE_CORE_OB_REGION_DESC0);
	pcie_write(port, 0x0,
		   PCIE_CORE_AXI_CONF_BASE + PCIE_CORE_OB_REGION_DESC1);

	return 0;
}

static irqreturn_t rockchip_pcie_subsys_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie_port *pp = arg;
	u32 reg;
	u32 sub_reg;

	reg = pcie_read(pp, PCIE_CLIENT_INT_STATUS);
	if (reg & PCIE_CLIENT_INT_LOCAL) {
		dev_dbg(pp->dev, "local interrupt received\n");
		sub_reg = pcie_read(pp, PCIE_CORE_INT_STATUS);
		if (sub_reg & PCIE_CORE_INT_PRFPE)
			dev_dbg(pp->dev, "parity error detected while reading from the PNP receive FIFO RAM\n");

		if (sub_reg & PCIE_CORE_INT_CRFPE)
			dev_dbg(pp->dev, "parity error detected while reading from the Completion Receive FIFO RAM\n");

		if (sub_reg & PCIE_CORE_INT_RRPE)
			dev_dbg(pp->dev, "parity error detected while reading from replay buffer RAM\n");

		if (sub_reg & PCIE_CORE_INT_PRFO)
			dev_dbg(pp->dev, "overflow occurred in the PNP receive FIFO\n");

		if (sub_reg & PCIE_CORE_INT_CRFO)
			dev_dbg(pp->dev, "overflow occurred in the completion receive FIFO\n");

		if (sub_reg & PCIE_CORE_INT_RT)
			dev_dbg(pp->dev, "replay timer timed out\n");

		if (sub_reg & PCIE_CORE_INT_RTR)
			dev_dbg(pp->dev, "replay timer rolled over after 4 transmissions of the same TLP\n");

		if (sub_reg & PCIE_CORE_INT_PE)
			dev_dbg(pp->dev, "phy error detected on receive side\n");

		if (sub_reg & PCIE_CORE_INT_MTR)
			dev_dbg(pp->dev, "malformed TLP received from the link\n");

		if (sub_reg & PCIE_CORE_INT_UCR)
			dev_dbg(pp->dev, "malformed TLP received from the link\n");

		if (sub_reg & PCIE_CORE_INT_FCE)
			dev_dbg(pp->dev, "an error was observed in the flow control advertisements from the other side\n");

		if (sub_reg & PCIE_CORE_INT_CT)
			dev_dbg(pp->dev, "a request timed out waiting for completion\n");

		if (sub_reg & PCIE_CORE_INT_UTC)
			dev_dbg(pp->dev, "unmapped TC error\n");

		if (sub_reg & PCIE_CORE_INT_MMVC)
			dev_dbg(pp->dev, "MSI mask register changes\n");

		pcie_write(pp, sub_reg, PCIE_CORE_INT_STATUS);
	} else if (reg & PCIE_CLIENT_INT_PHY) {
		dev_dbg(pp->dev, "phy link changes\n");
		rockchip_pcie_clr_bw_int(pp);
	}

	pcie_write(pp, reg & PCIE_CLIENT_INT_LOCAL, PCIE_CLIENT_INT_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_pcie_client_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie_port *pp = arg;
	u32 reg;

	reg = pcie_read(pp, PCIE_CLIENT_INT_STATUS);
	if (reg & PCIE_CLIENT_INT_LEGACY_DONE)
		dev_dbg(pp->dev, "legacy done interrupt received\n");

	if (reg & PCIE_CLIENT_INT_MSG)
		dev_dbg(pp->dev, "message done interrupt received\n");

	if (reg & PCIE_CLIENT_INT_HOT_RST)
		dev_dbg(pp->dev, "hot reset interrupt received\n");

	if (reg & PCIE_CLIENT_INT_DPA)
		dev_dbg(pp->dev, "dpa interrupt received\n");

	if (reg & PCIE_CLIENT_INT_FATAL_ERR)
		dev_dbg(pp->dev, "fatal error interrupt received\n");

	if (reg & PCIE_CLIENT_INT_NFATAL_ERR)
		dev_dbg(pp->dev, "no fatal error interrupt received\n");

	if (reg & PCIE_CLIENT_INT_CORR_ERR)
		dev_dbg(pp->dev, "correctable error interrupt received\n");

	if (reg & PCIE_CLIENT_INT_PHY)
		dev_dbg(pp->dev, "phy interrupt received\n");

	pcie_write(pp, reg & (PCIE_CLIENT_INT_LEGACY_DONE |
			      PCIE_CLIENT_INT_MSG | PCIE_CLIENT_INT_HOT_RST |
			      PCIE_CLIENT_INT_DPA | PCIE_CLIENT_INT_FATAL_ERR |
			      PCIE_CLIENT_INT_NFATAL_ERR |
			      PCIE_CLIENT_INT_CORR_ERR |
			      PCIE_CLIENT_INT_PHY),
			      PCIE_CLIENT_INT_STATUS);
	return IRQ_HANDLED;
}

static void rockchip_pcie_legacy_int_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct rockchip_pcie_port *port;
	u32 reg;
	u32 hwirq;
	u32 virq;

	chained_irq_enter(chip, desc);
	port = irq_desc_get_handler_data(desc);

	reg = pcie_read(port, PCIE_CLIENT_INT_STATUS);
	reg = (reg & ROCKCHIP_PCIE_RPIFR1_INTR_MASK) >>
	       ROCKCHIP_PCIE_RPIFR1_INTR_SHIFT;

	while (reg) {
		hwirq = ffs(reg) - 1;
		reg &= ~BIT(hwirq);

		virq = irq_find_mapping(port->irq_domain, hwirq);
		if (virq)
			generic_handle_irq(virq);
		else
			dev_err(port->dev, "unexpected IRQ, INT%d\n", hwirq);
	}

	chained_irq_exit(chip, desc);
}


/**
 * rockchip_pcie_parse_dt - Parse Device Tree
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int rockchip_pcie_parse_dt(struct rockchip_pcie_port *port)
{
	struct device *dev = port->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *node = dev->of_node;
	struct resource *regs;
	int irq;
	int err;

	regs = platform_get_resource_byname(pdev,
					    IORESOURCE_MEM,
					    "axi-base");
	if (!regs) {
		dev_err(dev, "missing axi-base property\n");
		return -ENODEV;
	}

	port->reg_base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(port->reg_base))
		return PTR_ERR(port->reg_base);

	regs = platform_get_resource_byname(pdev,
					    IORESOURCE_MEM,
					    "apb-base");
	if (!regs) {
		dev_err(dev, "missing apb-base property\n");
		return -ENODEV;
	}

	port->apb_base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(port->apb_base))
		return PTR_ERR(port->apb_base);

	port->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(port->phy)) {
		if (PTR_ERR(port->phy) != -EPROBE_DEFER)
			dev_err(dev, "missing phy\n");
		return PTR_ERR(port->phy);
	}

	port->lanes = 1;
	err = of_property_read_u32(node, "num-lanes", &port->lanes);
	if (!err && ((port->lanes == 0) ||
		     (port->lanes == 3) ||
		     (port->lanes > 4))) {
		dev_warn(dev, "invalid num-lanes, default use one lane\n");
		port->lanes = 1;
	}

	port->core_rst = devm_reset_control_get(dev, "core");
	if (IS_ERR(port->core_rst)) {
		if (PTR_ERR(port->core_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing core rst property in node\n");
		return PTR_ERR(port->core_rst);
	}

	port->mgmt_rst = devm_reset_control_get(dev, "mgmt");
	if (IS_ERR(port->mgmt_rst)) {
		if (PTR_ERR(port->mgmt_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing mgmt rst property in node\n");
		return PTR_ERR(port->mgmt_rst);
	}

	port->mgmt_sticky_rst = devm_reset_control_get(dev, "mgmt-sticky");
	if (IS_ERR(port->mgmt_sticky_rst)) {
		if (PTR_ERR(port->mgmt_sticky_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing mgmt-sticky rst property in node\n");
		return PTR_ERR(port->mgmt_sticky_rst);
	}

	port->pipe_rst = devm_reset_control_get(dev, "pipe");
	if (IS_ERR(port->pipe_rst)) {
		if (PTR_ERR(port->pipe_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing pipe rst property in node\n");
		return PTR_ERR(port->pipe_rst);
	}

	port->ep_gpio = devm_gpiod_get(dev, "ep", GPIOD_OUT_HIGH);
	if (IS_ERR(port->ep_gpio)) {
		dev_err(dev, "missing ep-gpios property in node\n");
		return PTR_ERR(port->ep_gpio);
	}

	port->aclk_pcie = devm_clk_get(dev, "aclk");
	if (IS_ERR(port->aclk_pcie)) {
		dev_err(dev, "aclk clock not found\n");
		return PTR_ERR(port->aclk_pcie);
	}

	port->aclk_perf_pcie = devm_clk_get(dev, "aclk-perf");
	if (IS_ERR(port->aclk_perf_pcie)) {
		dev_err(dev, "aclk_perf clock not found\n");
		return PTR_ERR(port->aclk_perf_pcie);
	}

	port->hclk_pcie = devm_clk_get(dev, "hclk");
	if (IS_ERR(port->hclk_pcie)) {
		dev_err(dev, "hclk clock not found\n");
		return PTR_ERR(port->hclk_pcie);
	}

	port->clk_pcie_pm = devm_clk_get(dev, "pm");
	if (IS_ERR(port->clk_pcie_pm)) {
		dev_err(dev, "pm clock not found\n");
		return PTR_ERR(port->clk_pcie_pm);
	}

	irq = platform_get_irq_byname(pdev, "sys");
	if (irq < 0) {
		dev_err(dev, "missing sys IRQ resource\n");
		return -EINVAL;
	}

	err = devm_request_irq(dev, irq, rockchip_pcie_subsys_irq_handler,
			       IRQF_SHARED, "pcie-sys", port);
	if (err) {
		dev_err(dev, "failed to request PCIe subsystem IRQ\n");
		return err;
	}

	irq = platform_get_irq_byname(pdev, "legacy");
	if (irq < 0) {
		dev_err(dev, "missing legacy IRQ resource\n");
		return -EINVAL;
	}

	irq_set_chained_handler_and_data(irq,
					 rockchip_pcie_legacy_int_handler,
					 port);

	irq = platform_get_irq_byname(pdev, "client");
	if (irq < 0) {
		dev_err(dev, "missing client IRQ resource\n");
		return -EINVAL;
	}

	err = devm_request_irq(dev, irq, rockchip_pcie_client_irq_handler,
			       IRQF_SHARED, "pcie-client", port);
	if (err) {
		dev_err(dev, "failed to request PCIe client IRQ\n");
		return err;
	}

	port->vpcie3v3 = devm_regulator_get_optional(dev, "vpcie3v3");
	if (IS_ERR(port->vpcie3v3)) {
		if (PTR_ERR(port->vpcie3v3) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie3v3 regulator found\n");
	}

	port->vpcie1v8 = devm_regulator_get_optional(dev, "vpcie1v8");
	if (IS_ERR(port->vpcie1v8)) {
		if (PTR_ERR(port->vpcie1v8) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie1v8 regulator found\n");
	}

	port->vpcie0v9 = devm_regulator_get_optional(dev, "vpcie0v9");
	if (IS_ERR(port->vpcie0v9)) {
		if (PTR_ERR(port->vpcie0v9) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie0v9 regulator found\n");
	}

	return 0;
}

static int rockchip_pcie_set_vpcie(struct rockchip_pcie_port *port)
{
	int err;

	if (!IS_ERR(port->vpcie3v3)) {
		err = regulator_enable(port->vpcie3v3);
		if (err) {
			dev_err(port->dev, "fail to enable vpcie3v3 regulator\n");
			goto err_out;
		}
	}

	if (!IS_ERR(port->vpcie1v8)) {
		err = regulator_enable(port->vpcie1v8);
		if (err) {
			dev_err(port->dev, "fail to enable vpcie1v8 regulator\n");
			goto err_disable_3v3;
		}
	}

	if (!IS_ERR(port->vpcie0v9)) {
		err = regulator_enable(port->vpcie0v9);
		if (err) {
			dev_err(port->dev, "fail to enable vpcie0v9 regulator\n");
			goto err_disable_1v8;
		}
	}

	return 0;

err_disable_1v8:
	if (!IS_ERR(port->vpcie1v8))
		regulator_disable(port->vpcie1v8);
err_disable_3v3:
	if (!IS_ERR(port->vpcie3v3))
		regulator_disable(port->vpcie3v3);
err_out:
	return err;
}

static void rockchip_pcie_enable_interrupts(struct rockchip_pcie_port *port)
{
	pcie_write(port, (PCIE_CLIENT_INT_CLI << 16) &
		   (~PCIE_CLIENT_INT_CLI), PCIE_CLIENT_INT_MASK);
	pcie_write(port, (u32)(~PCIE_CORE_INT), PCIE_CORE_INT_MASK);

	rockchip_pcie_enable_bw_int(port);
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
	struct device_node *intc = of_get_next_child(dev->of_node, NULL);

	if (!intc) {
		dev_err(dev, "missing child interrupt-controller node\n");
		return -EINVAL;
	}

	pp->irq_domain = irq_domain_add_linear(intc, 4, &intx_domain_ops, pp);
	if (!pp->irq_domain) {
		dev_err(dev, "failed to get a INTx IRQ domain\n");
		return -EINVAL;
	}

	return 0;
}

static int rockchip_pcie_prog_ob_atu(struct rockchip_pcie_port *pp,
				     int region_no, int type, u8 num_pass_bits,
				     u32 lower_addr, u32 upper_addr)
{
	u32 ob_addr_0;
	u32 ob_addr_1;
	u32 ob_desc_0;
	void __iomem *aw_base;

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

	ob_addr_0 = num_pass_bits & PCIE_CORE_OB_REGION_ADDR0_NUM_BITS;
	ob_addr_0 |= lower_addr & PCIE_CORE_OB_REGION_ADDR0_LO_ADDR;
	ob_addr_1 = upper_addr;
	ob_desc_0 = (1 << 23 | type);

	writel(ob_addr_0, aw_base);
	writel(ob_addr_1, aw_base + PCIE_CORE_OB_REGION_ADDR1);
	writel(ob_desc_0, aw_base + PCIE_CORE_OB_REGION_DESC0);
	writel(0, aw_base + PCIE_CORE_OB_REGION_DESC1);

	return 0;
}

static int rockchip_pcie_prog_ib_atu(struct rockchip_pcie_port *pp,
				     int region_no, u8 num_pass_bits,
				     u32 lower_addr, u32 upper_addr)
{
	u32 ib_addr_0;
	u32 ib_addr_1;
	void __iomem *aw_base;

	if (region_no > MAX_AXI_IB_ROOTPORT_REGION_NUM)
		return -EINVAL;
	if ((num_pass_bits + 1) < MIN_AXI_ADDR_BITS_PASSED)
		return -EINVAL;
	if (num_pass_bits > 63)
		return -EINVAL;

	aw_base = pp->apb_base + PCIE_CORE_AXI_INBOUND_BASE;
	aw_base += (region_no << IB_ROOT_PORT_REG_SIZE_SHIFT);

	ib_addr_0 = num_pass_bits & PCIE_CORE_IB_REGION_ADDR0_NUM_BITS;
	ib_addr_0 |= (lower_addr << 8) & PCIE_CORE_IB_REGION_ADDR0_LO_ADDR;
	ib_addr_1 = upper_addr;

	writel(ib_addr_0, aw_base);
	writel(ib_addr_1, aw_base + PCIE_RP_IB_ADDR_TRANS);

	return 0;
}

static int rockchip_pcie_probe(struct platform_device *pdev)
{
	struct rockchip_pcie_port *port;
	struct device *dev = &pdev->dev;
	struct pci_bus *bus, *child;
	struct resource_entry *win;
	resource_size_t io_base;
	struct resource	*busn = NULL;
	struct resource	*mem;
	struct resource	*io;
	phys_addr_t io_bus_addr = 0;
	u32 io_size;
	phys_addr_t mem_bus_addr = 0;
	u32 mem_size = 0;
	int reg_no;
	int err;
	int offset;

	LIST_HEAD(res);

	if (!dev->of_node)
		return -ENODEV;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;

	err = rockchip_pcie_parse_dt(port);
	if (err)
		return err;

	err = clk_prepare_enable(port->aclk_pcie);
	if (err) {
		dev_err(dev, "unable to enable aclk_pcie clock\n");
		goto err_aclk_pcie;
	}

	err = clk_prepare_enable(port->aclk_perf_pcie);
	if (err) {
		dev_err(dev, "unable to enable aclk_perf_pcie clock\n");
		goto err_aclk_perf_pcie;
	}

	err = clk_prepare_enable(port->hclk_pcie);
	if (err) {
		dev_err(dev, "unable to enable hclk_pcie clock\n");
		goto err_hclk_pcie;
	}

	err = clk_prepare_enable(port->clk_pcie_pm);
	if (err) {
		dev_err(dev, "unable to enable hclk_pcie clock\n");
		goto err_pcie_pm;
	}

	err = rockchip_pcie_set_vpcie(port);
	if (err) {
		dev_err(port->dev, "failed to set vpcie regulator\n");
		goto err_set_vpcie;
	}

	err = rockchip_pcie_init_port(port);
	if (err)
		goto err_vpcie;

	platform_set_drvdata(pdev, port);

	rockchip_pcie_enable_interrupts(port);

	err = rockchip_pcie_init_irq_domain(port);
	if (err < 0)
		goto err_vpcie;

	err = of_pci_get_host_bridge_resources(dev->of_node, 0, 0xff,
					       &res, &io_base);
	if (err)
		goto err_vpcie;

	err = devm_request_pci_bus_resources(dev, &res);
	if (err)
		goto err_vpcie;

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &res) {
		switch (resource_type(win->res)) {
		case IORESOURCE_IO:
			io = win->res;
			io->name = "I/O";
			io_size = resource_size(io);
			io_bus_addr = io->start - win->offset;
			err = pci_remap_iospace(io, io_base);
			if (err) {
				dev_warn(port->dev, "error %d: failed to map resource %pR\n",
					 err, io);
				continue;
			}
			break;
		case IORESOURCE_MEM:
			mem = win->res;
			mem->name = "MEM";
			mem_size = resource_size(mem);
			mem_bus_addr = mem->start - win->offset;
			break;
		case IORESOURCE_BUS:
			busn = win->res;
			break;
		default:
			continue;
		}
	}

	if (mem_size)
		for (reg_no = 0; reg_no < (mem_size >> 20); reg_no++) {
			err = rockchip_pcie_prog_ob_atu(port, reg_no + 1,
							AXI_WRAPPER_MEM_WRITE,
							20 - 1,
							mem_bus_addr +
							(reg_no << 20),
							0);
			if (err) {
				dev_err(dev, "program RC mem outbound ATU failed\n");
				goto err_vpcie;
			}
		}

	err = rockchip_pcie_prog_ib_atu(port, 2, 32 - 1, 0x0, 0);
	if (err) {
		dev_err(dev, "program RC mem inbound ATU failed\n");
		goto err_vpcie;
	}

	offset = mem_size >> 20;

	if (io_size)
		for (reg_no = 0; reg_no < (io_size >> 20); reg_no++) {
			err = rockchip_pcie_prog_ob_atu(port,
							reg_no + 1 + offset,
							AXI_WRAPPER_IO_WRITE,
							20 - 1,
							io_bus_addr +
							(reg_no << 20),
							0);
			if (err) {
				dev_err(dev, "program RC io outbound ATU failed\n");
				goto err_vpcie;
			}
		}

	if (busn)
		port->root_bus_nr = busn->start;

	bus = pci_scan_root_bus(&pdev->dev, 0, &rockchip_pcie_ops, port, &res);

	if (!bus) {
		err = -ENOMEM;
		goto err_vpcie;
	}

	pci_bus_size_bridges(bus);
	pci_bus_assign_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(bus);

	dev_warn(dev, "only 32-bit config accesses supported; smaller writes may corrupt adjacent RW1C fields\n");

	return err;

err_vpcie:
	if (!IS_ERR(port->vpcie3v3))
		regulator_disable(port->vpcie3v3);
	if (!IS_ERR(port->vpcie1v8))
		regulator_disable(port->vpcie1v8);
	if (!IS_ERR(port->vpcie0v9))
		regulator_disable(port->vpcie0v9);
err_set_vpcie:
	clk_disable_unprepare(port->clk_pcie_pm);
err_pcie_pm:
	clk_disable_unprepare(port->hclk_pcie);
err_hclk_pcie:
	clk_disable_unprepare(port->aclk_perf_pcie);
err_aclk_perf_pcie:
	clk_disable_unprepare(port->aclk_pcie);
err_aclk_pcie:
	return err;
}

static const struct of_device_id rockchip_pcie_of_match[] = {
	{ .compatible = "rockchip,rk3399-pcie", },
	{}
};

static struct platform_driver rockchip_pcie_driver = {
	.driver = {
		.name = "rockchip-pcie",
		.of_match_table = rockchip_pcie_of_match,
	},
	.probe = rockchip_pcie_probe,

};
builtin_platform_driver(rockchip_pcie_driver);
