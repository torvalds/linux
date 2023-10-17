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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "../pci.h"
#include "pcie-rockchip.h"

int rockchip_pcie_parse_dt(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *node = dev->of_node;
	struct resource *regs;
	int err;

	if (rockchip->is_rc) {
		regs = platform_get_resource_byname(pdev,
						    IORESOURCE_MEM,
						    "axi-base");
		rockchip->reg_base = devm_pci_remap_cfg_resource(dev, regs);
		if (IS_ERR(rockchip->reg_base))
			return PTR_ERR(rockchip->reg_base);
	} else {
		rockchip->mem_res =
			platform_get_resource_byname(pdev, IORESOURCE_MEM,
						     "mem-base");
		if (!rockchip->mem_res)
			return -EINVAL;
	}

	rockchip->apb_base =
		devm_platform_ioremap_resource_byname(pdev, "apb-base");
	if (IS_ERR(rockchip->apb_base))
		return PTR_ERR(rockchip->apb_base);

	err = rockchip_pcie_get_phys(rockchip);
	if (err)
		return err;

	rockchip->lanes = 1;
	err = of_property_read_u32(node, "num-lanes", &rockchip->lanes);
	if (!err && (rockchip->lanes == 0 ||
		     rockchip->lanes == 3 ||
		     rockchip->lanes > 4)) {
		dev_warn(dev, "invalid num-lanes, default to use one lane\n");
		rockchip->lanes = 1;
	}

	rockchip->link_gen = of_pci_get_max_link_speed(node);
	if (rockchip->link_gen < 0 || rockchip->link_gen > 2)
		rockchip->link_gen = 2;

	rockchip->core_rst = devm_reset_control_get_exclusive(dev, "core");
	if (IS_ERR(rockchip->core_rst)) {
		if (PTR_ERR(rockchip->core_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing core reset property in node\n");
		return PTR_ERR(rockchip->core_rst);
	}

	rockchip->mgmt_rst = devm_reset_control_get_exclusive(dev, "mgmt");
	if (IS_ERR(rockchip->mgmt_rst)) {
		if (PTR_ERR(rockchip->mgmt_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing mgmt reset property in node\n");
		return PTR_ERR(rockchip->mgmt_rst);
	}

	rockchip->mgmt_sticky_rst = devm_reset_control_get_exclusive(dev,
								"mgmt-sticky");
	if (IS_ERR(rockchip->mgmt_sticky_rst)) {
		if (PTR_ERR(rockchip->mgmt_sticky_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing mgmt-sticky reset property in node\n");
		return PTR_ERR(rockchip->mgmt_sticky_rst);
	}

	rockchip->pipe_rst = devm_reset_control_get_exclusive(dev, "pipe");
	if (IS_ERR(rockchip->pipe_rst)) {
		if (PTR_ERR(rockchip->pipe_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing pipe reset property in node\n");
		return PTR_ERR(rockchip->pipe_rst);
	}

	rockchip->pm_rst = devm_reset_control_get_exclusive(dev, "pm");
	if (IS_ERR(rockchip->pm_rst)) {
		if (PTR_ERR(rockchip->pm_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing pm reset property in node\n");
		return PTR_ERR(rockchip->pm_rst);
	}

	rockchip->pclk_rst = devm_reset_control_get_exclusive(dev, "pclk");
	if (IS_ERR(rockchip->pclk_rst)) {
		if (PTR_ERR(rockchip->pclk_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing pclk reset property in node\n");
		return PTR_ERR(rockchip->pclk_rst);
	}

	rockchip->aclk_rst = devm_reset_control_get_exclusive(dev, "aclk");
	if (IS_ERR(rockchip->aclk_rst)) {
		if (PTR_ERR(rockchip->aclk_rst) != -EPROBE_DEFER)
			dev_err(dev, "missing aclk reset property in node\n");
		return PTR_ERR(rockchip->aclk_rst);
	}

	if (rockchip->is_rc) {
		rockchip->ep_gpio = devm_gpiod_get_optional(dev, "ep",
							    GPIOD_OUT_HIGH);
		if (IS_ERR(rockchip->ep_gpio))
			return dev_err_probe(dev, PTR_ERR(rockchip->ep_gpio),
					     "failed to get ep GPIO\n");
	}

	rockchip->aclk_pcie = devm_clk_get(dev, "aclk");
	if (IS_ERR(rockchip->aclk_pcie)) {
		dev_err(dev, "aclk clock not found\n");
		return PTR_ERR(rockchip->aclk_pcie);
	}

	rockchip->aclk_perf_pcie = devm_clk_get(dev, "aclk-perf");
	if (IS_ERR(rockchip->aclk_perf_pcie)) {
		dev_err(dev, "aclk_perf clock not found\n");
		return PTR_ERR(rockchip->aclk_perf_pcie);
	}

	rockchip->hclk_pcie = devm_clk_get(dev, "hclk");
	if (IS_ERR(rockchip->hclk_pcie)) {
		dev_err(dev, "hclk clock not found\n");
		return PTR_ERR(rockchip->hclk_pcie);
	}

	rockchip->clk_pcie_pm = devm_clk_get(dev, "pm");
	if (IS_ERR(rockchip->clk_pcie_pm)) {
		dev_err(dev, "pm clock not found\n");
		return PTR_ERR(rockchip->clk_pcie_pm);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_pcie_parse_dt);

#define rockchip_pcie_read_addr(addr) rockchip_pcie_read(rockchip, addr)
/* 100 ms max wait time for PHY PLLs to lock */
#define RK_PHY_PLL_LOCK_TIMEOUT_US 100000
/* Sleep should be less than 20ms */
#define RK_PHY_PLL_LOCK_SLEEP_US 1000

int rockchip_pcie_init_port(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	int err, i;
	u32 regs;

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

	regs = PCIE_CLIENT_LINK_TRAIN_ENABLE | PCIE_CLIENT_ARI_ENABLE |
	       PCIE_CLIENT_CONF_LANE_NUM(rockchip->lanes);

	if (rockchip->is_rc)
		regs |= PCIE_CLIENT_CONF_ENABLE | PCIE_CLIENT_MODE_RC;
	else
		regs |= PCIE_CLIENT_CONF_DISABLE | PCIE_CLIENT_MODE_EP;

	rockchip_pcie_write(rockchip, regs, PCIE_CLIENT_CONFIG);

	for (i = 0; i < MAX_LANE_NUM; i++) {
		err = phy_power_on(rockchip->phys[i]);
		if (err) {
			dev_err(dev, "power on phy%d err %d\n", i, err);
			goto err_power_off_phy;
		}
	}

	err = readx_poll_timeout(rockchip_pcie_read_addr,
				 PCIE_CLIENT_SIDE_BAND_STATUS,
				 regs, !(regs & PCIE_CLIENT_PHY_ST),
				 RK_PHY_PLL_LOCK_SLEEP_US,
				 RK_PHY_PLL_LOCK_TIMEOUT_US);
	if (err) {
		dev_err(dev, "PHY PLLs could not lock, %d\n", err);
		goto err_power_off_phy;
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
EXPORT_SYMBOL_GPL(rockchip_pcie_init_port);

int rockchip_pcie_get_phys(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	struct phy *phy;
	char *name;
	u32 i;

	phy = devm_phy_get(dev, "pcie-phy");
	if (!IS_ERR(phy)) {
		rockchip->legacy_phy = true;
		rockchip->phys[0] = phy;
		dev_warn(dev, "legacy phy model is deprecated!\n");
		return 0;
	}

	if (PTR_ERR(phy) == -EPROBE_DEFER)
		return PTR_ERR(phy);

	dev_dbg(dev, "missing legacy phy; search for per-lane PHY\n");

	for (i = 0; i < MAX_LANE_NUM; i++) {
		name = kasprintf(GFP_KERNEL, "pcie-phy-%u", i);
		if (!name)
			return -ENOMEM;

		phy = devm_of_phy_get(dev, dev->of_node, name);
		kfree(name);

		if (IS_ERR(phy)) {
			if (PTR_ERR(phy) != -EPROBE_DEFER)
				dev_err(dev, "missing phy for lane %d: %ld\n",
					i, PTR_ERR(phy));
			return PTR_ERR(phy);
		}

		rockchip->phys[i] = phy;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_pcie_get_phys);

void rockchip_pcie_deinit_phys(struct rockchip_pcie *rockchip)
{
	int i;

	for (i = 0; i < MAX_LANE_NUM; i++) {
		/* inactive lanes are already powered off */
		if (rockchip->lanes_map & BIT(i))
			phy_power_off(rockchip->phys[i]);
		phy_exit(rockchip->phys[i]);
	}
}
EXPORT_SYMBOL_GPL(rockchip_pcie_deinit_phys);

int rockchip_pcie_enable_clocks(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->dev;
	int err;

	err = clk_prepare_enable(rockchip->aclk_pcie);
	if (err) {
		dev_err(dev, "unable to enable aclk_pcie clock\n");
		return err;
	}

	err = clk_prepare_enable(rockchip->aclk_perf_pcie);
	if (err) {
		dev_err(dev, "unable to enable aclk_perf_pcie clock\n");
		goto err_aclk_perf_pcie;
	}

	err = clk_prepare_enable(rockchip->hclk_pcie);
	if (err) {
		dev_err(dev, "unable to enable hclk_pcie clock\n");
		goto err_hclk_pcie;
	}

	err = clk_prepare_enable(rockchip->clk_pcie_pm);
	if (err) {
		dev_err(dev, "unable to enable clk_pcie_pm clock\n");
		goto err_clk_pcie_pm;
	}

	return 0;

err_clk_pcie_pm:
	clk_disable_unprepare(rockchip->hclk_pcie);
err_hclk_pcie:
	clk_disable_unprepare(rockchip->aclk_perf_pcie);
err_aclk_perf_pcie:
	clk_disable_unprepare(rockchip->aclk_pcie);
	return err;
}
EXPORT_SYMBOL_GPL(rockchip_pcie_enable_clocks);

void rockchip_pcie_disable_clocks(void *data)
{
	struct rockchip_pcie *rockchip = data;

	clk_disable_unprepare(rockchip->clk_pcie_pm);
	clk_disable_unprepare(rockchip->hclk_pcie);
	clk_disable_unprepare(rockchip->aclk_perf_pcie);
	clk_disable_unprepare(rockchip->aclk_pcie);
}
EXPORT_SYMBOL_GPL(rockchip_pcie_disable_clocks);

void rockchip_pcie_cfg_configuration_accesses(
		struct rockchip_pcie *rockchip, u32 type)
{
	u32 ob_desc_0;

	/* Configuration Accesses for region 0 */
	rockchip_pcie_write(rockchip, 0x0, PCIE_RC_BAR_CONF);

	rockchip_pcie_write(rockchip,
			    (RC_REGION_0_ADDR_TRANS_L + RC_REGION_0_PASS_BITS),
			    PCIE_CORE_OB_REGION_ADDR0);
	rockchip_pcie_write(rockchip, RC_REGION_0_ADDR_TRANS_H,
			    PCIE_CORE_OB_REGION_ADDR1);
	ob_desc_0 = rockchip_pcie_read(rockchip, PCIE_CORE_OB_REGION_DESC0);
	ob_desc_0 &= ~(RC_REGION_0_TYPE_MASK);
	ob_desc_0 |= (type | (0x1 << 23));
	rockchip_pcie_write(rockchip, ob_desc_0, PCIE_CORE_OB_REGION_DESC0);
	rockchip_pcie_write(rockchip, 0x0, PCIE_CORE_OB_REGION_DESC1);
}
EXPORT_SYMBOL_GPL(rockchip_pcie_cfg_configuration_accesses);
