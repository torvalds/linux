// SPDX-License-Identifier: GPL-2.0+
/*
 * Rockchip AXI PCIe host controller driver
 *
 * Copyright (c) 2016 Rockchip, Inc.
 *
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *         Wenrui Li <wenrui.li@rock-chips.com>
 *
 * Bits taken from Synopsys DesignWare Host controller driver and
 * ARM PCI Host generic driver.
 */

#include <linux/bitrev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
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

#include "pcie-rockchip.h"

static void rockchip_pcie_enable_bw_int(struct rockchip_pcie *rockchip)
{
	u32 status;

	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_LCS);
	status |= (PCI_EXP_LNKCTL_LBMIE | PCI_EXP_LNKCTL_LABIE);
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_LCS);
}

static void rockchip_pcie_clr_bw_int(struct rockchip_pcie *rockchip)
{
	u32 status;

	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_LCS);
	status |= (PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_LABS) << 16;
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_LCS);
}

static void rockchip_pcie_update_txcredit_mui(struct rockchip_pcie *rockchip)
{
	u32 val;

	/* Update Tx credit maximum update interval */
	val = rockchip_pcie_read(rockchip, PCIE_CORE_TXCREDIT_CFG1);
	val &= ~PCIE_CORE_TXCREDIT_CFG1_MUI_MASK;
	val |= PCIE_CORE_TXCREDIT_CFG1_MUI_ENCODE(24000);	/* ns */
	rockchip_pcie_write(rockchip, val, PCIE_CORE_TXCREDIT_CFG1);
}

static int rockchip_pcie_valid_device(struct rockchip_pcie *rockchip,
				      struct pci_bus *bus, int dev)
{
	/* access only one slot on each root port */
	if (bus->number == rockchip->root_bus_nr && dev > 0)
		return 0;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's downstream side.
	 */
	if (bus->primary == rockchip->root_bus_nr && dev > 0)
		return 0;

	return 1;
}

static u8 rockchip_pcie_lane_map(struct rockchip_pcie *rockchip)
{
	u32 val;
	u8 map;

	if (rockchip->legacy_phy)
		return GENMASK(MAX_LANE_NUM - 1, 0);

	val = rockchip_pcie_read(rockchip, PCIE_CORE_LANE_MAP);
	map = val & PCIE_CORE_LANE_MAP_MASK;

	/* The link may be using a reverse-indexed mapping. */
	if (val & PCIE_CORE_LANE_MAP_REVERSE)
		map = bitrev8(map) >> 4;

	return map;
}

static int rockchip_pcie_rd_own_conf(struct rockchip_pcie *rockchip,
				     int where, int size, u32 *val)
{
	void __iomem *addr;

	addr = rockchip->apb_base + PCIE_RC_CONFIG_NORMAL_BASE + where;

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

static int rockchip_pcie_wr_own_conf(struct rockchip_pcie *rockchip,
				     int where, int size, u32 val)
{
	u32 mask, tmp, offset;
	void __iomem *addr;

	offset = where & ~0x3;
	addr = rockchip->apb_base + PCIE_RC_CONFIG_NORMAL_BASE + offset;

	if (size == 4) {
		writel(val, addr);
		return PCIBIOS_SUCCESSFUL;
	}

	mask = ~(((1 << (size * 8)) - 1) << ((where & 0x3) * 8));

	/*
	 * N.B. This read/modify/write isn't safe in general because it can
	 * corrupt RW1C bits in adjacent registers.  But the hardware
	 * doesn't support smaller writes.
	 */
	tmp = readl(addr) & mask;
	tmp |= val << ((where & 0x3) * 8);
	writel(tmp, addr);

	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_rd_other_conf(struct rockchip_pcie *rockchip,
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

	if (bus->parent->number == rockchip->root_bus_nr)
		rockchip_pcie_cfg_configuration_accesses(rockchip,
						AXI_WRAPPER_TYPE0_CFG);
	else
		rockchip_pcie_cfg_configuration_accesses(rockchip,
						AXI_WRAPPER_TYPE1_CFG);

	if (size == 4) {
		*val = readl(rockchip->reg_base + busdev);
	} else if (size == 2) {
		*val = readw(rockchip->reg_base + busdev);
	} else if (size == 1) {
		*val = readb(rockchip->reg_base + busdev);
	} else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_wr_other_conf(struct rockchip_pcie *rockchip,
				       struct pci_bus *bus, u32 devfn,
				       int where, int size, u32 val)
{
	u32 busdev;

	busdev = PCIE_ECAM_ADDR(bus->number, PCI_SLOT(devfn),
				PCI_FUNC(devfn), where);
	if (!IS_ALIGNED(busdev, size))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (bus->parent->number == rockchip->root_bus_nr)
		rockchip_pcie_cfg_configuration_accesses(rockchip,
						AXI_WRAPPER_TYPE0_CFG);
	else
		rockchip_pcie_cfg_configuration_accesses(rockchip,
						AXI_WRAPPER_TYPE1_CFG);

	if (size == 4)
		writel(val, rockchip->reg_base + busdev);
	else if (size == 2)
		writew(val, rockchip->reg_base + busdev);
	else if (size == 1)
		writeb(val, rockchip->reg_base + busdev);
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static int rockchip_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
				 int size, u32 *val)
{
	struct rockchip_pcie *rockchip = bus->sysdata;

	if (!rockchip_pcie_valid_device(rockchip, bus, PCI_SLOT(devfn))) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number == rockchip->root_bus_nr)
		return rockchip_pcie_rd_own_conf(rockchip, where, size, val);

	return rockchip_pcie_rd_other_conf(rockchip, bus, devfn, where, size,
					   val);
}

static int rockchip_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
				 int where, int size, u32 val)
{
	struct rockchip_pcie *rockchip = bus->sysdata;

	if (!rockchip_pcie_valid_device(rockchip, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == rockchip->root_bus_nr)
		return rockchip_pcie_wr_own_conf(rockchip, where, size, val);

	return rockchip_pcie_wr_other_conf(rockchip, bus, devfn, where, size,
					   val);
}

static struct pci_ops rockchip_pcie_ops = {
	.read = rockchip_pcie_rd_conf,
	.write = rockchip_pcie_wr_conf,
};

static void rockchip_pcie_set_power_limit(struct rockchip_pcie *rockchip)
{
	int curr;
	u32 status, scale, power;

	if (IS_ERR(rockchip->vpcie3v3))
		return;

	/*
	 * Set RC's captured slot power limit and scale if
	 * vpcie3v3 available. The default values are both zero
	 * which means the software should set these two according
	 * to the actual power supply.
	 */
	curr = regulator_get_current_limit(rockchip->vpcie3v3);
	if (curr <= 0)
		return;

	scale = 3; /* 0.001x */
	curr = curr / 1000; /* convert to mA */
	power = (curr * 3300) / 1000; /* milliwatt */
	while (power > PCIE_RC_CONFIG_DCR_CSPL_LIMIT) {
		if (!scale) {
			dev_warn(rockchip->dev, "invalid power supply\n");
			return;
		}
		scale--;
		power = power / 10;
	}

	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_DCR);
	status |= (power << PCIE_RC_CONFIG_DCR_CSPL_SHIFT) |
		  (scale << PCIE_RC_CONFIG_DCR_CPLS_SHIFT);
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_DCR);
}

/**
 * rockchip_pcie_init_port - Initialize hardware
 * @rockchip: PCIe port information
 */
static int rockchip_pcie_init_port(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	int err, i;
	u32 status;

	gpiod_set_value_cansleep(rockchip->ep_gpio, 0);

	err = reset_control_assert(rockchip->aclk_rst);
	if (err) {
		dev_err(dev, "assert aclk_rst err %d\n", err);
		return err;
	}

	err = reset_control_assert(rockchip->pclk_rst);
	if (err) {
		dev_err(dev, "assert pclk_rst err %d\n", err);
		return err;
	}

	err = reset_control_assert(rockchip->pm_rst);
	if (err) {
		dev_err(dev, "assert pm_rst err %d\n", err);
		return err;
	}

	for (i = 0; i < MAX_LANE_NUM; i++) {
		err = phy_init(rockchip->phys[i]);
		if (err) {
			dev_err(dev, "init phy%d err %d\n", i, err);
			goto err_exit_phy;
		}
	}

	err = reset_control_assert(rockchip->core_rst);
	if (err) {
		dev_err(dev, "assert core_rst err %d\n", err);
		goto err_exit_phy;
	}

	err = reset_control_assert(rockchip->mgmt_rst);
	if (err) {
		dev_err(dev, "assert mgmt_rst err %d\n", err);
		goto err_exit_phy;
	}

	err = reset_control_assert(rockchip->mgmt_sticky_rst);
	if (err) {
		dev_err(dev, "assert mgmt_sticky_rst err %d\n", err);
		goto err_exit_phy;
	}

	err = reset_control_assert(rockchip->pipe_rst);
	if (err) {
		dev_err(dev, "assert pipe_rst err %d\n", err);
		goto err_exit_phy;
	}

	udelay(10);

	err = reset_control_deassert(rockchip->pm_rst);
	if (err) {
		dev_err(dev, "deassert pm_rst err %d\n", err);
		goto err_exit_phy;
	}

	err = reset_control_deassert(rockchip->aclk_rst);
	if (err) {
		dev_err(dev, "deassert aclk_rst err %d\n", err);
		goto err_exit_phy;
	}

	err = reset_control_deassert(rockchip->pclk_rst);
	if (err) {
		dev_err(dev, "deassert pclk_rst err %d\n", err);
		goto err_exit_phy;
	}

	if (rockchip->link_gen == 2)
		rockchip_pcie_write(rockchip, PCIE_CLIENT_GEN_SEL_2,
				    PCIE_CLIENT_CONFIG);
	else
		rockchip_pcie_write(rockchip, PCIE_CLIENT_GEN_SEL_1,
				    PCIE_CLIENT_CONFIG);

	rockchip_pcie_write(rockchip,
			    PCIE_CLIENT_CONF_ENABLE |
			    PCIE_CLIENT_LINK_TRAIN_ENABLE |
			    PCIE_CLIENT_ARI_ENABLE |
			    PCIE_CLIENT_CONF_LANE_NUM(rockchip->lanes) |
			    PCIE_CLIENT_MODE_RC,
			    PCIE_CLIENT_CONFIG);

	for (i = 0; i < MAX_LANE_NUM; i++) {
		err = phy_power_on(rockchip->phys[i]);
		if (err) {
			dev_err(dev, "power on phy%d err %d\n", i, err);
			goto err_power_off_phy;
		}
	}

	/*
	 * Please don't reorder the deassert sequence of the following
	 * four reset pins.
	 */
	err = reset_control_deassert(rockchip->mgmt_sticky_rst);
	if (err) {
		dev_err(dev, "deassert mgmt_sticky_rst err %d\n", err);
		goto err_power_off_phy;
	}

	err = reset_control_deassert(rockchip->core_rst);
	if (err) {
		dev_err(dev, "deassert core_rst err %d\n", err);
		goto err_power_off_phy;
	}

	err = reset_control_deassert(rockchip->mgmt_rst);
	if (err) {
		dev_err(dev, "deassert mgmt_rst err %d\n", err);
		goto err_power_off_phy;
	}

	err = reset_control_deassert(rockchip->pipe_rst);
	if (err) {
		dev_err(dev, "deassert pipe_rst err %d\n", err);
		goto err_power_off_phy;
	}

	/* Fix the transmitted FTS count desired to exit from L0s. */
	status = rockchip_pcie_read(rockchip, PCIE_CORE_CTRL_PLC1);
	status = (status & ~PCIE_CORE_CTRL_PLC1_FTS_MASK) |
		 (PCIE_CORE_CTRL_PLC1_FTS_CNT << PCIE_CORE_CTRL_PLC1_FTS_SHIFT);
	rockchip_pcie_write(rockchip, status, PCIE_CORE_CTRL_PLC1);

	rockchip_pcie_set_power_limit(rockchip);

	/* Set RC's clock architecture as common clock */
	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_LCS);
	status |= PCI_EXP_LNKSTA_SLC << 16;
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_LCS);

	/* Set RC's RCB to 128 */
	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_LCS);
	status |= PCI_EXP_LNKCTL_RCB;
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_LCS);

	/* Enable Gen1 training */
	rockchip_pcie_write(rockchip, PCIE_CLIENT_LINK_TRAIN_ENABLE,
			    PCIE_CLIENT_CONFIG);

	gpiod_set_value_cansleep(rockchip->ep_gpio, 1);

	/* 500ms timeout value should be enough for Gen1/2 training */
	err = readl_poll_timeout(rockchip->apb_base + PCIE_CLIENT_BASIC_STATUS1,
				 status, PCIE_LINK_UP(status), 20,
				 500 * USEC_PER_MSEC);
	if (err) {
		dev_err(dev, "PCIe link training gen1 timeout!\n");
		goto err_power_off_phy;
	}

	if (rockchip->link_gen == 2) {
		/*
		 * Enable retrain for gen2. This should be configured only after
		 * gen1 finished.
		 */
		status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_LCS);
		status |= PCI_EXP_LNKCTL_RL;
		rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_LCS);

		err = readl_poll_timeout(rockchip->apb_base + PCIE_CORE_CTRL,
					 status, PCIE_LINK_IS_GEN2(status), 20,
					 500 * USEC_PER_MSEC);
		if (err)
			dev_dbg(dev, "PCIe link training gen2 timeout, fall back to gen1!\n");
	}

	/* Check the final link width from negotiated lane counter from MGMT */
	status = rockchip_pcie_read(rockchip, PCIE_CORE_CTRL);
	status = 0x1 << ((status & PCIE_CORE_PL_CONF_LANE_MASK) >>
			  PCIE_CORE_PL_CONF_LANE_SHIFT);
	dev_dbg(dev, "current link width is x%d\n", status);

	/* Power off unused lane(s) */
	rockchip->lanes_map = rockchip_pcie_lane_map(rockchip);
	for (i = 0; i < MAX_LANE_NUM; i++) {
		if (!(rockchip->lanes_map & BIT(i))) {
			dev_dbg(dev, "idling lane %d\n", i);
			phy_power_off(rockchip->phys[i]);
		}
	}

	rockchip_pcie_write(rockchip, ROCKCHIP_VENDOR_ID,
			    PCIE_CORE_CONFIG_VENDOR);
	rockchip_pcie_write(rockchip,
			    PCI_CLASS_BRIDGE_PCI << PCIE_RC_CONFIG_SCC_SHIFT,
			    PCIE_RC_CONFIG_RID_CCR);

	/* Clear THP cap's next cap pointer to remove L1 substate cap */
	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_THP_CAP);
	status &= ~PCIE_RC_CONFIG_THP_CAP_NEXT_MASK;
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_THP_CAP);

	/* Clear L0s from RC's link cap */
	if (of_property_read_bool(dev->of_node, "aspm-no-l0s")) {
		status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_LINK_CAP);
		status &= ~PCIE_RC_CONFIG_LINK_CAP_L0S;
		rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_LINK_CAP);
	}

	status = rockchip_pcie_read(rockchip, PCIE_RC_CONFIG_DCSR);
	status &= ~PCIE_RC_CONFIG_DCSR_MPS_MASK;
	status |= PCIE_RC_CONFIG_DCSR_MPS_256;
	rockchip_pcie_write(rockchip, status, PCIE_RC_CONFIG_DCSR);

	return 0;
err_power_off_phy:
	while (i--)
		phy_power_off(rockchip->phys[i]);
	i = MAX_LANE_NUM;
err_exit_phy:
	while (i--)
		phy_exit(rockchip->phys[i]);
	return err;
}

static irqreturn_t rockchip_pcie_subsys_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie *rockchip = arg;
	struct device *dev = rockchip->dev;
	u32 reg;
	u32 sub_reg;

	reg = rockchip_pcie_read(rockchip, PCIE_CLIENT_INT_STATUS);
	if (reg & PCIE_CLIENT_INT_LOCAL) {
		dev_dbg(dev, "local interrupt received\n");
		sub_reg = rockchip_pcie_read(rockchip, PCIE_CORE_INT_STATUS);
		if (sub_reg & PCIE_CORE_INT_PRFPE)
			dev_dbg(dev, "parity error detected while reading from the PNP receive FIFO RAM\n");

		if (sub_reg & PCIE_CORE_INT_CRFPE)
			dev_dbg(dev, "parity error detected while reading from the Completion Receive FIFO RAM\n");

		if (sub_reg & PCIE_CORE_INT_RRPE)
			dev_dbg(dev, "parity error detected while reading from replay buffer RAM\n");

		if (sub_reg & PCIE_CORE_INT_PRFO)
			dev_dbg(dev, "overflow occurred in the PNP receive FIFO\n");

		if (sub_reg & PCIE_CORE_INT_CRFO)
			dev_dbg(dev, "overflow occurred in the completion receive FIFO\n");

		if (sub_reg & PCIE_CORE_INT_RT)
			dev_dbg(dev, "replay timer timed out\n");

		if (sub_reg & PCIE_CORE_INT_RTR)
			dev_dbg(dev, "replay timer rolled over after 4 transmissions of the same TLP\n");

		if (sub_reg & PCIE_CORE_INT_PE)
			dev_dbg(dev, "phy error detected on receive side\n");

		if (sub_reg & PCIE_CORE_INT_MTR)
			dev_dbg(dev, "malformed TLP received from the link\n");

		if (sub_reg & PCIE_CORE_INT_UCR)
			dev_dbg(dev, "malformed TLP received from the link\n");

		if (sub_reg & PCIE_CORE_INT_FCE)
			dev_dbg(dev, "an error was observed in the flow control advertisements from the other side\n");

		if (sub_reg & PCIE_CORE_INT_CT)
			dev_dbg(dev, "a request timed out waiting for completion\n");

		if (sub_reg & PCIE_CORE_INT_UTC)
			dev_dbg(dev, "unmapped TC error\n");

		if (sub_reg & PCIE_CORE_INT_MMVC)
			dev_dbg(dev, "MSI mask register changes\n");

		rockchip_pcie_write(rockchip, sub_reg, PCIE_CORE_INT_STATUS);
	} else if (reg & PCIE_CLIENT_INT_PHY) {
		dev_dbg(dev, "phy link changes\n");
		rockchip_pcie_update_txcredit_mui(rockchip);
		rockchip_pcie_clr_bw_int(rockchip);
	}

	rockchip_pcie_write(rockchip, reg & PCIE_CLIENT_INT_LOCAL,
			    PCIE_CLIENT_INT_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_pcie_client_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie *rockchip = arg;
	struct device *dev = rockchip->dev;
	u32 reg;

	reg = rockchip_pcie_read(rockchip, PCIE_CLIENT_INT_STATUS);
	if (reg & PCIE_CLIENT_INT_LEGACY_DONE)
		dev_dbg(dev, "legacy done interrupt received\n");

	if (reg & PCIE_CLIENT_INT_MSG)
		dev_dbg(dev, "message done interrupt received\n");

	if (reg & PCIE_CLIENT_INT_HOT_RST)
		dev_dbg(dev, "hot reset interrupt received\n");

	if (reg & PCIE_CLIENT_INT_DPA)
		dev_dbg(dev, "dpa interrupt received\n");

	if (reg & PCIE_CLIENT_INT_FATAL_ERR)
		dev_dbg(dev, "fatal error interrupt received\n");

	if (reg & PCIE_CLIENT_INT_NFATAL_ERR)
		dev_dbg(dev, "no fatal error interrupt received\n");

	if (reg & PCIE_CLIENT_INT_CORR_ERR)
		dev_dbg(dev, "correctable error interrupt received\n");

	if (reg & PCIE_CLIENT_INT_PHY)
		dev_dbg(dev, "phy interrupt received\n");

	rockchip_pcie_write(rockchip, reg & (PCIE_CLIENT_INT_LEGACY_DONE |
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
	struct rockchip_pcie *rockchip = irq_desc_get_handler_data(desc);
	struct device *dev = rockchip->dev;
	u32 reg;
	u32 hwirq;
	u32 virq;

	chained_irq_enter(chip, desc);

	reg = rockchip_pcie_read(rockchip, PCIE_CLIENT_INT_STATUS);
	reg = (reg & PCIE_CLIENT_INTR_MASK) >> PCIE_CLIENT_INTR_SHIFT;

	while (reg) {
		hwirq = ffs(reg) - 1;
		reg &= ~BIT(hwirq);

		virq = irq_find_mapping(rockchip->irq_domain, hwirq);
		if (virq)
			generic_handle_irq(virq);
		else
			dev_err(dev, "unexpected IRQ, INT%d\n", hwirq);
	}

	chained_irq_exit(chip, desc);
}

static int rockchip_pcie_setup_irq(struct rockchip_pcie *rockchip)
{
	int irq, err;
	struct device *dev = rockchip->dev;
	struct platform_device *pdev = to_platform_device(dev);

	irq = platform_get_irq_byname(pdev, "sys");
	if (irq < 0) {
		dev_err(dev, "missing sys IRQ resource\n");
		return irq;
	}

	err = devm_request_irq(dev, irq, rockchip_pcie_subsys_irq_handler,
			       IRQF_SHARED, "pcie-sys", rockchip);
	if (err) {
		dev_err(dev, "failed to request PCIe subsystem IRQ\n");
		return err;
	}

	irq = platform_get_irq_byname(pdev, "legacy");
	if (irq < 0) {
		dev_err(dev, "missing legacy IRQ resource\n");
		return irq;
	}

	irq_set_chained_handler_and_data(irq,
					 rockchip_pcie_legacy_int_handler,
					 rockchip);

	irq = platform_get_irq_byname(pdev, "client");
	if (irq < 0) {
		dev_err(dev, "missing client IRQ resource\n");
		return irq;
	}

	err = devm_request_irq(dev, irq, rockchip_pcie_client_irq_handler,
			       IRQF_SHARED, "pcie-client", rockchip);
	if (err) {
		dev_err(dev, "failed to request PCIe client IRQ\n");
		return err;
	}

	return 0;
}

/**
 * rockchip_pcie_parse_host_dt - Parse Device Tree
 * @rockchip: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int rockchip_pcie_parse_host_dt(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	int err;

	err = rockchip_pcie_parse_dt(rockchip);
	if (err)
		return err;

	err = rockchip_pcie_setup_irq(rockchip);
	if (err)
		return err;

	rockchip->vpcie12v = devm_regulator_get_optional(dev, "vpcie12v");
	if (IS_ERR(rockchip->vpcie12v)) {
		if (PTR_ERR(rockchip->vpcie12v) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie12v regulator found\n");
	}

	rockchip->vpcie3v3 = devm_regulator_get_optional(dev, "vpcie3v3");
	if (IS_ERR(rockchip->vpcie3v3)) {
		if (PTR_ERR(rockchip->vpcie3v3) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie3v3 regulator found\n");
	}

	rockchip->vpcie1v8 = devm_regulator_get_optional(dev, "vpcie1v8");
	if (IS_ERR(rockchip->vpcie1v8)) {
		if (PTR_ERR(rockchip->vpcie1v8) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie1v8 regulator found\n");
	}

	rockchip->vpcie0v9 = devm_regulator_get_optional(dev, "vpcie0v9");
	if (IS_ERR(rockchip->vpcie0v9)) {
		if (PTR_ERR(rockchip->vpcie0v9) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vpcie0v9 regulator found\n");
	}

	return 0;
}

static int rockchip_pcie_set_vpcie(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	int err;

	if (!IS_ERR(rockchip->vpcie12v)) {
		err = regulator_enable(rockchip->vpcie12v);
		if (err) {
			dev_err(dev, "fail to enable vpcie12v regulator\n");
			goto err_out;
		}
	}

	if (!IS_ERR(rockchip->vpcie3v3)) {
		err = regulator_enable(rockchip->vpcie3v3);
		if (err) {
			dev_err(dev, "fail to enable vpcie3v3 regulator\n");
			goto err_disable_12v;
		}
	}

	if (!IS_ERR(rockchip->vpcie1v8)) {
		err = regulator_enable(rockchip->vpcie1v8);
		if (err) {
			dev_err(dev, "fail to enable vpcie1v8 regulator\n");
			goto err_disable_3v3;
		}
	}

	if (!IS_ERR(rockchip->vpcie0v9)) {
		err = regulator_enable(rockchip->vpcie0v9);
		if (err) {
			dev_err(dev, "fail to enable vpcie0v9 regulator\n");
			goto err_disable_1v8;
		}
	}

	return 0;

err_disable_1v8:
	if (!IS_ERR(rockchip->vpcie1v8))
		regulator_disable(rockchip->vpcie1v8);
err_disable_3v3:
	if (!IS_ERR(rockchip->vpcie3v3))
		regulator_disable(rockchip->vpcie3v3);
err_disable_12v:
	if (!IS_ERR(rockchip->vpcie12v))
		regulator_disable(rockchip->vpcie12v);
err_out:
	return err;
}

static void rockchip_pcie_enable_interrupts(struct rockchip_pcie *rockchip)
{
	rockchip_pcie_write(rockchip, (PCIE_CLIENT_INT_CLI << 16) &
			    (~PCIE_CLIENT_INT_CLI), PCIE_CLIENT_INT_MASK);
	rockchip_pcie_write(rockchip, (u32)(~PCIE_CORE_INT),
			    PCIE_CORE_INT_MASK);

	rockchip_pcie_enable_bw_int(rockchip);
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

static int rockchip_pcie_init_irq_domain(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	struct device_node *intc = of_get_next_child(dev->of_node, NULL);

	if (!intc) {
		dev_err(dev, "missing child interrupt-controller node\n");
		return -EINVAL;
	}

	rockchip->irq_domain = irq_domain_add_linear(intc, PCI_NUM_INTX,
						    &intx_domain_ops, rockchip);
	if (!rockchip->irq_domain) {
		dev_err(dev, "failed to get a INTx IRQ domain\n");
		return -EINVAL;
	}

	return 0;
}

static int rockchip_pcie_prog_ob_atu(struct rockchip_pcie *rockchip,
				     int region_no, int type, u8 num_pass_bits,
				     u32 lower_addr, u32 upper_addr)
{
	u32 ob_addr_0;
	u32 ob_addr_1;
	u32 ob_desc_0;
	u32 aw_offset;

	if (region_no >= MAX_AXI_WRAPPER_REGION_NUM)
		return -EINVAL;
	if (num_pass_bits + 1 < 8)
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

	aw_offset = (region_no << OB_REG_SIZE_SHIFT);

	ob_addr_0 = num_pass_bits & PCIE_CORE_OB_REGION_ADDR0_NUM_BITS;
	ob_addr_0 |= lower_addr & PCIE_CORE_OB_REGION_ADDR0_LO_ADDR;
	ob_addr_1 = upper_addr;
	ob_desc_0 = (1 << 23 | type);

	rockchip_pcie_write(rockchip, ob_addr_0,
			    PCIE_CORE_OB_REGION_ADDR0 + aw_offset);
	rockchip_pcie_write(rockchip, ob_addr_1,
			    PCIE_CORE_OB_REGION_ADDR1 + aw_offset);
	rockchip_pcie_write(rockchip, ob_desc_0,
			    PCIE_CORE_OB_REGION_DESC0 + aw_offset);
	rockchip_pcie_write(rockchip, 0,
			    PCIE_CORE_OB_REGION_DESC1 + aw_offset);

	return 0;
}

static int rockchip_pcie_prog_ib_atu(struct rockchip_pcie *rockchip,
				     int region_no, u8 num_pass_bits,
				     u32 lower_addr, u32 upper_addr)
{
	u32 ib_addr_0;
	u32 ib_addr_1;
	u32 aw_offset;

	if (region_no > MAX_AXI_IB_ROOTPORT_REGION_NUM)
		return -EINVAL;
	if (num_pass_bits + 1 < MIN_AXI_ADDR_BITS_PASSED)
		return -EINVAL;
	if (num_pass_bits > 63)
		return -EINVAL;

	aw_offset = (region_no << IB_ROOT_PORT_REG_SIZE_SHIFT);

	ib_addr_0 = num_pass_bits & PCIE_CORE_IB_REGION_ADDR0_NUM_BITS;
	ib_addr_0 |= (lower_addr << 8) & PCIE_CORE_IB_REGION_ADDR0_LO_ADDR;
	ib_addr_1 = upper_addr;

	rockchip_pcie_write(rockchip, ib_addr_0, PCIE_RP_IB_ADDR0 + aw_offset);
	rockchip_pcie_write(rockchip, ib_addr_1, PCIE_RP_IB_ADDR1 + aw_offset);

	return 0;
}

static int rockchip_pcie_cfg_atu(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	int offset;
	int err;
	int reg_no;

	rockchip_pcie_cfg_configuration_accesses(rockchip,
						 AXI_WRAPPER_TYPE0_CFG);

	for (reg_no = 0; reg_no < (rockchip->mem_size >> 20); reg_no++) {
		err = rockchip_pcie_prog_ob_atu(rockchip, reg_no + 1,
						AXI_WRAPPER_MEM_WRITE,
						20 - 1,
						rockchip->mem_bus_addr +
						(reg_no << 20),
						0);
		if (err) {
			dev_err(dev, "program RC mem outbound ATU failed\n");
			return err;
		}
	}

	err = rockchip_pcie_prog_ib_atu(rockchip, 2, 32 - 1, 0x0, 0);
	if (err) {
		dev_err(dev, "program RC mem inbound ATU failed\n");
		return err;
	}

	offset = rockchip->mem_size >> 20;
	for (reg_no = 0; reg_no < (rockchip->io_size >> 20); reg_no++) {
		err = rockchip_pcie_prog_ob_atu(rockchip,
						reg_no + 1 + offset,
						AXI_WRAPPER_IO_WRITE,
						20 - 1,
						rockchip->io_bus_addr +
						(reg_no << 20),
						0);
		if (err) {
			dev_err(dev, "program RC io outbound ATU failed\n");
			return err;
		}
	}

	/* assign message regions */
	rockchip_pcie_prog_ob_atu(rockchip, reg_no + 1 + offset,
				  AXI_WRAPPER_NOR_MSG,
				  20 - 1, 0, 0);

	rockchip->msg_bus_addr = rockchip->mem_bus_addr +
					((reg_no + offset) << 20);
	return err;
}

static int rockchip_pcie_wait_l2(struct rockchip_pcie *rockchip)
{
	u32 value;
	int err;

	/* send PME_TURN_OFF message */
	writel(0x0, rockchip->msg_region + PCIE_RC_SEND_PME_OFF);

	/* read LTSSM and wait for falling into L2 link state */
	err = readl_poll_timeout(rockchip->apb_base + PCIE_CLIENT_DEBUG_OUT_0,
				 value, PCIE_LINK_IS_L2(value), 20,
				 jiffies_to_usecs(5 * HZ));
	if (err) {
		dev_err(rockchip->dev, "PCIe link enter L2 timeout!\n");
		return err;
	}

	return 0;
}

static int __maybe_unused rockchip_pcie_suspend_noirq(struct device *dev)
{
	struct rockchip_pcie *rockchip = dev_get_drvdata(dev);
	int ret;

	/* disable core and cli int since we don't need to ack PME_ACK */
	rockchip_pcie_write(rockchip, (PCIE_CLIENT_INT_CLI << 16) |
			    PCIE_CLIENT_INT_CLI, PCIE_CLIENT_INT_MASK);
	rockchip_pcie_write(rockchip, (u32)PCIE_CORE_INT, PCIE_CORE_INT_MASK);

	ret = rockchip_pcie_wait_l2(rockchip);
	if (ret) {
		rockchip_pcie_enable_interrupts(rockchip);
		return ret;
	}

	rockchip_pcie_deinit_phys(rockchip);

	rockchip_pcie_disable_clocks(rockchip);

	if (!IS_ERR(rockchip->vpcie0v9))
		regulator_disable(rockchip->vpcie0v9);

	return ret;
}

static int __maybe_unused rockchip_pcie_resume_noirq(struct device *dev)
{
	struct rockchip_pcie *rockchip = dev_get_drvdata(dev);
	int err;

	if (!IS_ERR(rockchip->vpcie0v9)) {
		err = regulator_enable(rockchip->vpcie0v9);
		if (err) {
			dev_err(dev, "fail to enable vpcie0v9 regulator\n");
			return err;
		}
	}

	err = rockchip_pcie_enable_clocks(rockchip);
	if (err)
		goto err_disable_0v9;

	err = rockchip_pcie_init_port(rockchip);
	if (err)
		goto err_pcie_resume;

	err = rockchip_pcie_cfg_atu(rockchip);
	if (err)
		goto err_err_deinit_port;

	/* Need this to enter L1 again */
	rockchip_pcie_update_txcredit_mui(rockchip);
	rockchip_pcie_enable_interrupts(rockchip);

	return 0;

err_err_deinit_port:
	rockchip_pcie_deinit_phys(rockchip);
err_pcie_resume:
	rockchip_pcie_disable_clocks(rockchip);
err_disable_0v9:
	if (!IS_ERR(rockchip->vpcie0v9))
		regulator_disable(rockchip->vpcie0v9);
	return err;
}

static int rockchip_pcie_probe(struct platform_device *pdev)
{
	struct rockchip_pcie *rockchip;
	struct device *dev = &pdev->dev;
	struct pci_bus *bus, *child;
	struct pci_host_bridge *bridge;
	struct resource_entry *win;
	resource_size_t io_base;
	struct resource	*mem;
	struct resource	*io;
	int err;

	LIST_HEAD(res);

	if (!dev->of_node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rockchip));
	if (!bridge)
		return -ENOMEM;

	rockchip = pci_host_bridge_priv(bridge);

	platform_set_drvdata(pdev, rockchip);
	rockchip->dev = dev;
	rockchip->is_rc = true;

	err = rockchip_pcie_parse_host_dt(rockchip);
	if (err)
		return err;

	err = rockchip_pcie_enable_clocks(rockchip);
	if (err)
		return err;

	err = rockchip_pcie_set_vpcie(rockchip);
	if (err) {
		dev_err(dev, "failed to set vpcie regulator\n");
		goto err_set_vpcie;
	}

	err = rockchip_pcie_init_port(rockchip);
	if (err)
		goto err_vpcie;

	rockchip_pcie_enable_interrupts(rockchip);

	err = rockchip_pcie_init_irq_domain(rockchip);
	if (err < 0)
		goto err_deinit_port;

	err = of_pci_get_host_bridge_resources(dev->of_node, 0, 0xff,
					       &res, &io_base);
	if (err)
		goto err_remove_irq_domain;

	err = devm_request_pci_bus_resources(dev, &res);
	if (err)
		goto err_free_res;

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &res) {
		switch (resource_type(win->res)) {
		case IORESOURCE_IO:
			io = win->res;
			io->name = "I/O";
			rockchip->io_size = resource_size(io);
			rockchip->io_bus_addr = io->start - win->offset;
			err = pci_remap_iospace(io, io_base);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, io);
				continue;
			}
			rockchip->io = io;
			break;
		case IORESOURCE_MEM:
			mem = win->res;
			mem->name = "MEM";
			rockchip->mem_size = resource_size(mem);
			rockchip->mem_bus_addr = mem->start - win->offset;
			break;
		case IORESOURCE_BUS:
			rockchip->root_bus_nr = win->res->start;
			break;
		default:
			continue;
		}
	}

	err = rockchip_pcie_cfg_atu(rockchip);
	if (err)
		goto err_unmap_iospace;

	rockchip->msg_region = devm_ioremap(dev, rockchip->msg_bus_addr, SZ_1M);
	if (!rockchip->msg_region) {
		err = -ENOMEM;
		goto err_unmap_iospace;
	}

	list_splice_init(&res, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = rockchip;
	bridge->busnr = 0;
	bridge->ops = &rockchip_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	err = pci_scan_root_bus_bridge(bridge);
	if (err < 0)
		goto err_unmap_iospace;

	bus = bridge->bus;

	rockchip->root_bus = bus;

	pci_bus_size_bridges(bus);
	pci_bus_assign_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(bus);
	return 0;

err_unmap_iospace:
	pci_unmap_iospace(rockchip->io);
err_free_res:
	pci_free_resource_list(&res);
err_remove_irq_domain:
	irq_domain_remove(rockchip->irq_domain);
err_deinit_port:
	rockchip_pcie_deinit_phys(rockchip);
err_vpcie:
	if (!IS_ERR(rockchip->vpcie12v))
		regulator_disable(rockchip->vpcie12v);
	if (!IS_ERR(rockchip->vpcie3v3))
		regulator_disable(rockchip->vpcie3v3);
	if (!IS_ERR(rockchip->vpcie1v8))
		regulator_disable(rockchip->vpcie1v8);
	if (!IS_ERR(rockchip->vpcie0v9))
		regulator_disable(rockchip->vpcie0v9);
err_set_vpcie:
	rockchip_pcie_disable_clocks(rockchip);
	return err;
}

static int rockchip_pcie_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie *rockchip = dev_get_drvdata(dev);

	pci_stop_root_bus(rockchip->root_bus);
	pci_remove_root_bus(rockchip->root_bus);
	pci_unmap_iospace(rockchip->io);
	irq_domain_remove(rockchip->irq_domain);

	rockchip_pcie_deinit_phys(rockchip);

	rockchip_pcie_disable_clocks(rockchip);

	if (!IS_ERR(rockchip->vpcie12v))
		regulator_disable(rockchip->vpcie12v);
	if (!IS_ERR(rockchip->vpcie3v3))
		regulator_disable(rockchip->vpcie3v3);
	if (!IS_ERR(rockchip->vpcie1v8))
		regulator_disable(rockchip->vpcie1v8);
	if (!IS_ERR(rockchip->vpcie0v9))
		regulator_disable(rockchip->vpcie0v9);

	return 0;
}

static const struct dev_pm_ops rockchip_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rockchip_pcie_suspend_noirq,
				      rockchip_pcie_resume_noirq)
};

static const struct of_device_id rockchip_pcie_of_match[] = {
	{ .compatible = "rockchip,rk3399-pcie", },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_pcie_of_match);

static struct platform_driver rockchip_pcie_driver = {
	.driver = {
		.name = "rockchip-pcie",
		.of_match_table = rockchip_pcie_of_match,
		.pm = &rockchip_pcie_pm_ops,
	},
	.probe = rockchip_pcie_probe,
	.remove = rockchip_pcie_remove,
};
module_platform_driver(rockchip_pcie_driver);

MODULE_AUTHOR("Rockchip Inc");
MODULE_DESCRIPTION("Rockchip AXI PCIe driver");
MODULE_LICENSE("GPL v2");
