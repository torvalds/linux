// SPDX-License-Identifier: GPL-2.0-only
/*
 * dwmac-stm32.c - DWMAC Specific Glue layer for STM32 MCU
 *
 * Copyright (C) STMicroelectronics SA 2017
 * Author:  Alexandre Torgue <alexandre.torgue@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define SYSCFG_MCU_ETH_MASK		BIT(23)
#define SYSCFG_MP1_ETH_MASK		GENMASK(23, 16)
#define SYSCFG_PMCCLRR_OFFSET		0x40

#define SYSCFG_PMCR_ETH_CLK_SEL		BIT(16)
#define SYSCFG_PMCR_ETH_REF_CLK_SEL	BIT(17)

/* CLOCK feed to PHY*/
#define ETH_CK_F_25M	25000000
#define ETH_CK_F_50M	50000000
#define ETH_CK_F_125M	125000000

/*  Ethernet PHY interface selection in register SYSCFG Configuration
 *------------------------------------------
 * src	 |BIT(23)| BIT(22)| BIT(21)|BIT(20)|
 *------------------------------------------
 * MII   |   0	 |   0	  |   0    |   1   |
 *------------------------------------------
 * GMII  |   0	 |   0	  |   0    |   0   |
 *------------------------------------------
 * RGMII |   0	 |   0	  |   1	   |  n/a  |
 *------------------------------------------
 * RMII  |   1	 |   0	  |   0	   |  n/a  |
 *------------------------------------------
 */
#define SYSCFG_PMCR_ETH_SEL_MII		BIT(20)
#define SYSCFG_PMCR_ETH_SEL_RGMII	BIT(21)
#define SYSCFG_PMCR_ETH_SEL_RMII	BIT(23)
#define SYSCFG_PMCR_ETH_SEL_GMII	0
#define SYSCFG_MCU_ETH_SEL_MII		0
#define SYSCFG_MCU_ETH_SEL_RMII		1

/* STM32MP2 register definitions */
#define SYSCFG_MP2_ETH_MASK		GENMASK(31, 0)

#define SYSCFG_ETHCR_ETH_PTP_CLK_SEL	BIT(2)
#define SYSCFG_ETHCR_ETH_CLK_SEL	BIT(1)
#define SYSCFG_ETHCR_ETH_REF_CLK_SEL	BIT(0)

#define SYSCFG_ETHCR_ETH_SEL_MII	0
#define SYSCFG_ETHCR_ETH_SEL_RGMII	BIT(4)
#define SYSCFG_ETHCR_ETH_SEL_RMII	BIT(6)

/* STM32MPx register definitions
 *
 * Below table summarizes the clock requirement and clock sources for
 * supported phy interface modes.
 * __________________________________________________________________________
 *|PHY_MODE | Normal | PHY wo crystal|   PHY wo crystal   |No 125MHz from PHY|
 *|         |        |      25MHz    |        50MHz       |                  |
 * ---------------------------------------------------------------------------
 *|  MII    |	 -   |     eth-ck    |	      n/a	  |	  n/a        |
 *|         |        | st,ext-phyclk |                    |		     |
 * ---------------------------------------------------------------------------
 *|  GMII   |	 -   |     eth-ck    |	      n/a	  |	  n/a        |
 *|         |        | st,ext-phyclk |                    |		     |
 * ---------------------------------------------------------------------------
 *| RGMII   |	 -   |     eth-ck    |	      n/a	  |      eth-ck      |
 *|         |        | st,ext-phyclk |                    | st,eth-clk-sel or|
 *|         |        |               |                    | st,ext-phyclk    |
 * ---------------------------------------------------------------------------
 *| RMII    |	 -   |     eth-ck    |	    eth-ck        |	  n/a        |
 *|         |        | st,ext-phyclk | st,eth-ref-clk-sel |		     |
 *|         |        |               | or st,ext-phyclk   |		     |
 * ---------------------------------------------------------------------------
 *
 */

struct stm32_dwmac {
	struct clk *clk_tx;
	struct clk *clk_rx;
	struct clk *clk_eth_ck;
	struct clk *clk_ethstp;
	struct clk *syscfg_clk;
	int ext_phyclk;
	int enable_eth_ck;
	int eth_clk_sel_reg;
	int eth_ref_clk_sel_reg;
	int irq_pwr_wakeup;
	u32 mode_reg;		 /* MAC glue-logic mode register */
	u32 mode_mask;
	struct regmap *regmap;
	u32 speed;
	const struct stm32_ops *ops;
	struct device *dev;
};

struct stm32_ops {
	int (*set_mode)(struct plat_stmmacenet_data *plat_dat);
	int (*suspend)(struct stm32_dwmac *dwmac);
	void (*resume)(struct stm32_dwmac *dwmac);
	int (*parse_data)(struct stm32_dwmac *dwmac,
			  struct device *dev);
	bool clk_rx_enable_in_suspend;
	bool is_mp13, is_mp2;
	u32 syscfg_clr_off;
};

static int stm32_dwmac_clk_enable(struct stm32_dwmac *dwmac, bool resume)
{
	int ret;

	ret = clk_prepare_enable(dwmac->clk_tx);
	if (ret)
		goto err_clk_tx;

	if (!dwmac->ops->clk_rx_enable_in_suspend || !resume) {
		ret = clk_prepare_enable(dwmac->clk_rx);
		if (ret)
			goto err_clk_rx;
	}

	ret = clk_prepare_enable(dwmac->syscfg_clk);
	if (ret)
		goto err_syscfg_clk;

	if (dwmac->enable_eth_ck) {
		ret = clk_prepare_enable(dwmac->clk_eth_ck);
		if (ret)
			goto err_clk_eth_ck;
	}

	return ret;

err_clk_eth_ck:
	clk_disable_unprepare(dwmac->syscfg_clk);
err_syscfg_clk:
	if (!dwmac->ops->clk_rx_enable_in_suspend || !resume)
		clk_disable_unprepare(dwmac->clk_rx);
err_clk_rx:
	clk_disable_unprepare(dwmac->clk_tx);
err_clk_tx:
	return ret;
}

static int stm32_dwmac_init(struct plat_stmmacenet_data *plat_dat, bool resume)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	int ret;

	if (dwmac->ops->set_mode) {
		ret = dwmac->ops->set_mode(plat_dat);
		if (ret)
			return ret;
	}

	return stm32_dwmac_clk_enable(dwmac, resume);
}

static int stm32mp1_select_ethck_external(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		dwmac->enable_eth_ck = dwmac->ext_phyclk;
		return 0;
	case PHY_INTERFACE_MODE_GMII:
		dwmac->enable_eth_ck = dwmac->eth_clk_sel_reg ||
				       dwmac->ext_phyclk;
		return 0;
	case PHY_INTERFACE_MODE_RMII:
		dwmac->enable_eth_ck = dwmac->eth_ref_clk_sel_reg ||
				       dwmac->ext_phyclk;
		return 0;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		dwmac->enable_eth_ck = dwmac->eth_clk_sel_reg ||
				       dwmac->ext_phyclk;
		return 0;
	default:
		dwmac->enable_eth_ck = false;
		dev_err(dwmac->dev, "Mode %s not supported",
			phy_modes(plat_dat->mac_interface));
		return -EINVAL;
	}
}

static int stm32mp1_validate_ethck_rate(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	const u32 clk_rate = clk_get_rate(dwmac->clk_eth_ck);

	if (!dwmac->enable_eth_ck)
		return 0;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		if (clk_rate == ETH_CK_F_25M)
			return 0;
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (clk_rate == ETH_CK_F_25M || clk_rate == ETH_CK_F_50M)
			return 0;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (clk_rate == ETH_CK_F_25M || clk_rate == ETH_CK_F_125M)
			return 0;
		break;
	default:
		break;
	}

	dev_err(dwmac->dev, "Mode %s does not match eth-ck frequency %d Hz",
		phy_modes(plat_dat->mac_interface), clk_rate);
	return -EINVAL;
}

static int stm32mp1_configure_pmcr(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	u32 reg = dwmac->mode_reg;
	int val = 0;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		/*
		 * STM32MP15xx supports both MII and GMII, STM32MP13xx MII only.
		 * SYSCFG_PMCSETR ETH_SELMII is present only on STM32MP15xx and
		 * acts as a selector between 0:GMII and 1:MII. As STM32MP13xx
		 * supports only MII, ETH_SELMII is not present.
		 */
		if (!dwmac->ops->is_mp13)  /* Select MII mode on STM32MP15xx */
			val |= SYSCFG_PMCR_ETH_SEL_MII;
		break;
	case PHY_INTERFACE_MODE_GMII:
		val = SYSCFG_PMCR_ETH_SEL_GMII;
		if (dwmac->enable_eth_ck)
			val |= SYSCFG_PMCR_ETH_CLK_SEL;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = SYSCFG_PMCR_ETH_SEL_RMII;
		if (dwmac->enable_eth_ck)
			val |= SYSCFG_PMCR_ETH_REF_CLK_SEL;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = SYSCFG_PMCR_ETH_SEL_RGMII;
		if (dwmac->enable_eth_ck)
			val |= SYSCFG_PMCR_ETH_CLK_SEL;
		break;
	default:
		dev_err(dwmac->dev, "Mode %s not supported",
			phy_modes(plat_dat->mac_interface));
		/* Do not manage others interfaces */
		return -EINVAL;
	}

	dev_dbg(dwmac->dev, "Mode %s", phy_modes(plat_dat->mac_interface));

	/* Shift value at correct ethernet MAC offset in SYSCFG_PMCSETR */
	val <<= ffs(dwmac->mode_mask) - ffs(SYSCFG_MP1_ETH_MASK);

	/* Need to update PMCCLRR (clear register) */
	regmap_write(dwmac->regmap, dwmac->ops->syscfg_clr_off,
		     dwmac->mode_mask);

	/* Update PMCSETR (set register) */
	return regmap_update_bits(dwmac->regmap, reg,
				 dwmac->mode_mask, val);
}

static int stm32mp2_configure_syscfg(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	u32 reg = dwmac->mode_reg;
	int val = 0;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		/* ETH_REF_CLK_SEL bit in SYSCFG register is not applicable in MII mode */
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = SYSCFG_ETHCR_ETH_SEL_RMII;
		if (dwmac->enable_eth_ck) {
			/* Internal clock ETH_CLK of 50MHz from RCC is used */
			val |= SYSCFG_ETHCR_ETH_REF_CLK_SEL;
		}
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = SYSCFG_ETHCR_ETH_SEL_RGMII;
		fallthrough;
	case PHY_INTERFACE_MODE_GMII:
		if (dwmac->enable_eth_ck) {
			/* Internal clock ETH_CLK of 125MHz from RCC is used */
			val |= SYSCFG_ETHCR_ETH_CLK_SEL;
		}
		break;
	default:
		dev_err(dwmac->dev, "Mode %s not supported",
			phy_modes(plat_dat->mac_interface));
		/* Do not manage others interfaces */
		return -EINVAL;
	}

	dev_dbg(dwmac->dev, "Mode %s", phy_modes(plat_dat->mac_interface));

	/* Select PTP (IEEE1588) clock selection from RCC (ck_ker_ethxptp) */
	val |= SYSCFG_ETHCR_ETH_PTP_CLK_SEL;

	/* Update ETHCR (set register) */
	return regmap_update_bits(dwmac->regmap, reg,
				 SYSCFG_MP2_ETH_MASK, val);
}

static int stm32mp1_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	int ret;

	ret = stm32mp1_select_ethck_external(plat_dat);
	if (ret)
		return ret;

	ret = stm32mp1_validate_ethck_rate(plat_dat);
	if (ret)
		return ret;

	if (!dwmac->ops->is_mp2)
		return stm32mp1_configure_pmcr(plat_dat);
	else
		return stm32mp2_configure_syscfg(plat_dat);
}

static int stm32mcu_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	u32 reg = dwmac->mode_reg;
	int val;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		val = SYSCFG_MCU_ETH_SEL_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = SYSCFG_MCU_ETH_SEL_RMII;
		break;
	default:
		dev_err(dwmac->dev, "Mode %s not supported",
			phy_modes(plat_dat->mac_interface));
		/* Do not manage others interfaces */
		return -EINVAL;
	}

	dev_dbg(dwmac->dev, "Mode %s", phy_modes(plat_dat->mac_interface));

	return regmap_update_bits(dwmac->regmap, reg,
				 SYSCFG_MCU_ETH_MASK, val << 23);
}

static void stm32_dwmac_clk_disable(struct stm32_dwmac *dwmac, bool suspend)
{
	clk_disable_unprepare(dwmac->clk_tx);
	if (!dwmac->ops->clk_rx_enable_in_suspend || !suspend)
		clk_disable_unprepare(dwmac->clk_rx);

	clk_disable_unprepare(dwmac->syscfg_clk);
	if (dwmac->enable_eth_ck)
		clk_disable_unprepare(dwmac->clk_eth_ck);
}

static int stm32_dwmac_parse_data(struct stm32_dwmac *dwmac,
				  struct device *dev)
{
	struct device_node *np = dev->of_node;
	int err;

	/*  Get TX/RX clocks */
	dwmac->clk_tx = devm_clk_get(dev, "mac-clk-tx");
	if (IS_ERR(dwmac->clk_tx)) {
		dev_err(dev, "No ETH Tx clock provided...\n");
		return PTR_ERR(dwmac->clk_tx);
	}

	dwmac->clk_rx = devm_clk_get(dev, "mac-clk-rx");
	if (IS_ERR(dwmac->clk_rx)) {
		dev_err(dev, "No ETH Rx clock provided...\n");
		return PTR_ERR(dwmac->clk_rx);
	}

	if (dwmac->ops->parse_data) {
		err = dwmac->ops->parse_data(dwmac, dev);
		if (err)
			return err;
	}

	/* Get mode register */
	dwmac->regmap = syscon_regmap_lookup_by_phandle_args(np, "st,syscon",
							     1, &dwmac->mode_reg);
	if (IS_ERR(dwmac->regmap))
		return PTR_ERR(dwmac->regmap);

	if (dwmac->ops->is_mp2)
		return 0;

	dwmac->mode_mask = SYSCFG_MP1_ETH_MASK;
	err = of_property_read_u32_index(np, "st,syscon", 2, &dwmac->mode_mask);
	if (err) {
		if (dwmac->ops->is_mp13) {
			dev_err(dev, "Sysconfig register mask must be set (%d)\n", err);
		} else {
			dev_dbg(dev, "Warning sysconfig register mask not set\n");
			err = 0;
		}
	}

	return err;
}

static int stm32mp1_parse_data(struct stm32_dwmac *dwmac,
			       struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;
	int err = 0;

	/* Ethernet PHY have no crystal */
	dwmac->ext_phyclk = of_property_read_bool(np, "st,ext-phyclk");

	/* Gigabit Ethernet 125MHz clock selection. */
	dwmac->eth_clk_sel_reg = of_property_read_bool(np, "st,eth-clk-sel");

	/* Ethernet 50MHz RMII clock selection */
	dwmac->eth_ref_clk_sel_reg =
		of_property_read_bool(np, "st,eth-ref-clk-sel");

	/*  Get ETH_CLK clocks */
	dwmac->clk_eth_ck = devm_clk_get(dev, "eth-ck");
	if (IS_ERR(dwmac->clk_eth_ck)) {
		dev_info(dev, "No phy clock provided...\n");
		dwmac->clk_eth_ck = NULL;
	}

	/*  Clock used for low power mode */
	dwmac->clk_ethstp = devm_clk_get(dev, "ethstp");
	if (IS_ERR(dwmac->clk_ethstp)) {
		dev_err(dev,
			"No ETH peripheral clock provided for CStop mode ...\n");
		return PTR_ERR(dwmac->clk_ethstp);
	}

	/*  Optional Clock for sysconfig */
	dwmac->syscfg_clk = devm_clk_get(dev, "syscfg-clk");
	if (IS_ERR(dwmac->syscfg_clk))
		dwmac->syscfg_clk = NULL;

	/* Get IRQ information early to have an ability to ask for deferred
	 * probe if needed before we went too far with resource allocation.
	 */
	dwmac->irq_pwr_wakeup = platform_get_irq_byname_optional(pdev,
							"stm32_pwr_wakeup");
	if (dwmac->irq_pwr_wakeup == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (!dwmac->clk_eth_ck && dwmac->irq_pwr_wakeup >= 0) {
		err = device_init_wakeup(&pdev->dev, true);
		if (err) {
			dev_err(&pdev->dev, "Failed to init wake up irq\n");
			return err;
		}
		err = dev_pm_set_dedicated_wake_irq(&pdev->dev,
						    dwmac->irq_pwr_wakeup);
		if (err) {
			dev_err(&pdev->dev, "Failed to set wake up irq\n");
			device_init_wakeup(&pdev->dev, false);
		}
		device_set_wakeup_enable(&pdev->dev, false);
	}
	return err;
}

static int stm32_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct stm32_dwmac *dwmac;
	const struct stm32_ops *data;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no of match data provided\n");
		return -EINVAL;
	}

	dwmac->ops = data;
	dwmac->dev = &pdev->dev;

	ret = stm32_dwmac_parse_data(dwmac, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to parse OF data\n");
		return ret;
	}

	plat_dat->bsp_priv = dwmac;

	ret = stm32_dwmac_init(plat_dat, false);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_clk_disable;

	return 0;

err_clk_disable:
	stm32_dwmac_clk_disable(dwmac, false);

	return ret;
}

static void stm32_dwmac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct stm32_dwmac *dwmac = priv->plat->bsp_priv;

	stmmac_dvr_remove(&pdev->dev);

	stm32_dwmac_clk_disable(dwmac, false);

	if (dwmac->irq_pwr_wakeup >= 0) {
		dev_pm_clear_wake_irq(&pdev->dev);
		device_init_wakeup(&pdev->dev, false);
	}
}

static int stm32mp1_suspend(struct stm32_dwmac *dwmac)
{
	return clk_prepare_enable(dwmac->clk_ethstp);
}

static void stm32mp1_resume(struct stm32_dwmac *dwmac)
{
	clk_disable_unprepare(dwmac->clk_ethstp);
}

#ifdef CONFIG_PM_SLEEP
static int stm32_dwmac_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct stm32_dwmac *dwmac = priv->plat->bsp_priv;

	int ret;

	ret = stmmac_suspend(dev);
	if (ret)
		return ret;

	stm32_dwmac_clk_disable(dwmac, true);

	if (dwmac->ops->suspend)
		ret = dwmac->ops->suspend(dwmac);

	return ret;
}

static int stm32_dwmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct stm32_dwmac *dwmac = priv->plat->bsp_priv;
	int ret;

	if (dwmac->ops->resume)
		dwmac->ops->resume(dwmac);

	ret = stm32_dwmac_init(priv->plat, true);
	if (ret)
		return ret;

	ret = stmmac_resume(dev);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(stm32_dwmac_pm_ops,
	stm32_dwmac_suspend, stm32_dwmac_resume);

static struct stm32_ops stm32mcu_dwmac_data = {
	.set_mode = stm32mcu_set_mode
};

static struct stm32_ops stm32mp1_dwmac_data = {
	.set_mode = stm32mp1_set_mode,
	.suspend = stm32mp1_suspend,
	.resume = stm32mp1_resume,
	.parse_data = stm32mp1_parse_data,
	.syscfg_clr_off = 0x44,
	.is_mp13 = false,
	.clk_rx_enable_in_suspend = true
};

static struct stm32_ops stm32mp13_dwmac_data = {
	.set_mode = stm32mp1_set_mode,
	.suspend = stm32mp1_suspend,
	.resume = stm32mp1_resume,
	.parse_data = stm32mp1_parse_data,
	.syscfg_clr_off = 0x08,
	.is_mp13 = true,
	.clk_rx_enable_in_suspend = true
};

static struct stm32_ops stm32mp25_dwmac_data = {
	.set_mode = stm32mp1_set_mode,
	.suspend = stm32mp1_suspend,
	.resume = stm32mp1_resume,
	.parse_data = stm32mp1_parse_data,
	.is_mp2 = true,
	.clk_rx_enable_in_suspend = true
};

static const struct of_device_id stm32_dwmac_match[] = {
	{ .compatible = "st,stm32-dwmac", .data = &stm32mcu_dwmac_data},
	{ .compatible = "st,stm32mp1-dwmac", .data = &stm32mp1_dwmac_data},
	{ .compatible = "st,stm32mp13-dwmac", .data = &stm32mp13_dwmac_data},
	{ .compatible = "st,stm32mp25-dwmac", .data = &stm32mp25_dwmac_data},
	{ }
};
MODULE_DEVICE_TABLE(of, stm32_dwmac_match);

static struct platform_driver stm32_dwmac_driver = {
	.probe  = stm32_dwmac_probe,
	.remove = stm32_dwmac_remove,
	.driver = {
		.name           = "stm32-dwmac",
		.pm		= &stm32_dwmac_pm_ops,
		.of_match_table = stm32_dwmac_match,
	},
};
module_platform_driver(stm32_dwmac_driver);

MODULE_AUTHOR("Alexandre Torgue <alexandre.torgue@gmail.com>");
MODULE_AUTHOR("Christophe Roullier <christophe.roullier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 DWMAC Specific Glue layer");
MODULE_LICENSE("GPL v2");
