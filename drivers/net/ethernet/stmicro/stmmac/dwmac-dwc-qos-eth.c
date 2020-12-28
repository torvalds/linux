// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys DWC Ethernet Quality-of-Service v4.10a linux driver
 *
 * Copyright (C) 2016 Joao Pinto <jpinto@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"
#include "dwmac4.h"

struct tegra_eqos {
	struct device *dev;
	void __iomem *regs;

	struct reset_control *rst;
	struct clk *clk_master;
	struct clk *clk_slave;
	struct clk *clk_tx;
	struct clk *clk_rx;

	struct gpio_desc *reset;
};

static int dwc_eth_dwmac_config_dt(struct platform_device *pdev,
				   struct plat_stmmacenet_data *plat_dat)
{
	struct device *dev = &pdev->dev;
	u32 burst_map = 0;
	u32 bit_index = 0;
	u32 a_index = 0;

	if (!plat_dat->axi) {
		plat_dat->axi = kzalloc(sizeof(struct stmmac_axi), GFP_KERNEL);

		if (!plat_dat->axi)
			return -ENOMEM;
	}

	plat_dat->axi->axi_lpi_en = device_property_read_bool(dev,
							      "snps,en-lpi");
	if (device_property_read_u32(dev, "snps,write-requests",
				     &plat_dat->axi->axi_wr_osr_lmt)) {
		/**
		 * Since the register has a reset value of 1, if property
		 * is missing, default to 1.
		 */
		plat_dat->axi->axi_wr_osr_lmt = 1;
	} else {
		/**
		 * If property exists, to keep the behavior from dwc_eth_qos,
		 * subtract one after parsing.
		 */
		plat_dat->axi->axi_wr_osr_lmt--;
	}

	if (device_property_read_u32(dev, "snps,read-requests",
				     &plat_dat->axi->axi_rd_osr_lmt)) {
		/**
		 * Since the register has a reset value of 1, if property
		 * is missing, default to 1.
		 */
		plat_dat->axi->axi_rd_osr_lmt = 1;
	} else {
		/**
		 * If property exists, to keep the behavior from dwc_eth_qos,
		 * subtract one after parsing.
		 */
		plat_dat->axi->axi_rd_osr_lmt--;
	}
	device_property_read_u32(dev, "snps,burst-map", &burst_map);

	/* converts burst-map bitmask to burst array */
	for (bit_index = 0; bit_index < 7; bit_index++) {
		if (burst_map & (1 << bit_index)) {
			switch (bit_index) {
			case 0:
			plat_dat->axi->axi_blen[a_index] = 4; break;
			case 1:
			plat_dat->axi->axi_blen[a_index] = 8; break;
			case 2:
			plat_dat->axi->axi_blen[a_index] = 16; break;
			case 3:
			plat_dat->axi->axi_blen[a_index] = 32; break;
			case 4:
			plat_dat->axi->axi_blen[a_index] = 64; break;
			case 5:
			plat_dat->axi->axi_blen[a_index] = 128; break;
			case 6:
			plat_dat->axi->axi_blen[a_index] = 256; break;
			default:
			break;
			}
			a_index++;
		}
	}

	/* dwc-qos needs GMAC4, AAL, TSO and PMT */
	plat_dat->has_gmac4 = 1;
	plat_dat->dma_cfg->aal = 1;
	plat_dat->tso_en = 1;
	plat_dat->pmt = 1;

	return 0;
}

static int dwc_qos_probe(struct platform_device *pdev,
			 struct plat_stmmacenet_data *plat_dat,
			 struct stmmac_resources *stmmac_res)
{
	int err;

	plat_dat->stmmac_clk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(plat_dat->stmmac_clk)) {
		dev_err(&pdev->dev, "apb_pclk clock not found.\n");
		return PTR_ERR(plat_dat->stmmac_clk);
	}

	err = clk_prepare_enable(plat_dat->stmmac_clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable apb_pclk clock: %d\n",
			err);
		return err;
	}

	plat_dat->pclk = devm_clk_get(&pdev->dev, "phy_ref_clk");
	if (IS_ERR(plat_dat->pclk)) {
		dev_err(&pdev->dev, "phy_ref_clk clock not found.\n");
		err = PTR_ERR(plat_dat->pclk);
		goto disable;
	}

	err = clk_prepare_enable(plat_dat->pclk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable phy_ref clock: %d\n",
			err);
		goto disable;
	}

	return 0;

disable:
	clk_disable_unprepare(plat_dat->stmmac_clk);
	return err;
}

static int dwc_qos_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	clk_disable_unprepare(priv->plat->pclk);
	clk_disable_unprepare(priv->plat->stmmac_clk);

	return 0;
}

#define SDMEMCOMPPADCTRL 0x8800
#define  SDMEMCOMPPADCTRL_PAD_E_INPUT_OR_E_PWRD BIT(31)

#define AUTO_CAL_CONFIG 0x8804
#define  AUTO_CAL_CONFIG_START BIT(31)
#define  AUTO_CAL_CONFIG_ENABLE BIT(29)

#define AUTO_CAL_STATUS 0x880c
#define  AUTO_CAL_STATUS_ACTIVE BIT(31)

static void tegra_eqos_fix_speed(void *priv, unsigned int speed)
{
	struct tegra_eqos *eqos = priv;
	unsigned long rate = 125000000;
	bool needs_calibration = false;
	u32 value;
	int err;

	switch (speed) {
	case SPEED_1000:
		needs_calibration = true;
		rate = 125000000;
		break;

	case SPEED_100:
		needs_calibration = true;
		rate = 25000000;
		break;

	case SPEED_10:
		rate = 2500000;
		break;

	default:
		dev_err(eqos->dev, "invalid speed %u\n", speed);
		break;
	}

	if (needs_calibration) {
		/* calibrate */
		value = readl(eqos->regs + SDMEMCOMPPADCTRL);
		value |= SDMEMCOMPPADCTRL_PAD_E_INPUT_OR_E_PWRD;
		writel(value, eqos->regs + SDMEMCOMPPADCTRL);

		udelay(1);

		value = readl(eqos->regs + AUTO_CAL_CONFIG);
		value |= AUTO_CAL_CONFIG_START | AUTO_CAL_CONFIG_ENABLE;
		writel(value, eqos->regs + AUTO_CAL_CONFIG);

		err = readl_poll_timeout_atomic(eqos->regs + AUTO_CAL_STATUS,
						value,
						value & AUTO_CAL_STATUS_ACTIVE,
						1, 10);
		if (err < 0) {
			dev_err(eqos->dev, "calibration did not start\n");
			goto failed;
		}

		err = readl_poll_timeout_atomic(eqos->regs + AUTO_CAL_STATUS,
						value,
						(value & AUTO_CAL_STATUS_ACTIVE) == 0,
						20, 200);
		if (err < 0) {
			dev_err(eqos->dev, "calibration didn't finish\n");
			goto failed;
		}

	failed:
		value = readl(eqos->regs + SDMEMCOMPPADCTRL);
		value &= ~SDMEMCOMPPADCTRL_PAD_E_INPUT_OR_E_PWRD;
		writel(value, eqos->regs + SDMEMCOMPPADCTRL);
	} else {
		value = readl(eqos->regs + AUTO_CAL_CONFIG);
		value &= ~AUTO_CAL_CONFIG_ENABLE;
		writel(value, eqos->regs + AUTO_CAL_CONFIG);
	}

	err = clk_set_rate(eqos->clk_tx, rate);
	if (err < 0)
		dev_err(eqos->dev, "failed to set TX rate: %d\n", err);
}

static int tegra_eqos_init(struct platform_device *pdev, void *priv)
{
	struct tegra_eqos *eqos = priv;
	unsigned long rate;
	u32 value;

	rate = clk_get_rate(eqos->clk_slave);

	value = (rate / 1000000) - 1;
	writel(value, eqos->regs + GMAC_1US_TIC_COUNTER);

	return 0;
}

static int tegra_eqos_probe(struct platform_device *pdev,
			    struct plat_stmmacenet_data *data,
			    struct stmmac_resources *res)
{
	struct device *dev = &pdev->dev;
	struct tegra_eqos *eqos;
	int err;

	eqos = devm_kzalloc(&pdev->dev, sizeof(*eqos), GFP_KERNEL);
	if (!eqos)
		return -ENOMEM;

	eqos->dev = &pdev->dev;
	eqos->regs = res->addr;

	if (!is_of_node(dev->fwnode))
		goto bypass_clk_reset_gpio;

	eqos->clk_master = devm_clk_get(&pdev->dev, "master_bus");
	if (IS_ERR(eqos->clk_master)) {
		err = PTR_ERR(eqos->clk_master);
		goto error;
	}

	err = clk_prepare_enable(eqos->clk_master);
	if (err < 0)
		goto error;

	eqos->clk_slave = devm_clk_get(&pdev->dev, "slave_bus");
	if (IS_ERR(eqos->clk_slave)) {
		err = PTR_ERR(eqos->clk_slave);
		goto disable_master;
	}

	data->stmmac_clk = eqos->clk_slave;

	err = clk_prepare_enable(eqos->clk_slave);
	if (err < 0)
		goto disable_master;

	eqos->clk_rx = devm_clk_get(&pdev->dev, "rx");
	if (IS_ERR(eqos->clk_rx)) {
		err = PTR_ERR(eqos->clk_rx);
		goto disable_slave;
	}

	err = clk_prepare_enable(eqos->clk_rx);
	if (err < 0)
		goto disable_slave;

	eqos->clk_tx = devm_clk_get(&pdev->dev, "tx");
	if (IS_ERR(eqos->clk_tx)) {
		err = PTR_ERR(eqos->clk_tx);
		goto disable_rx;
	}

	err = clk_prepare_enable(eqos->clk_tx);
	if (err < 0)
		goto disable_rx;

	eqos->reset = devm_gpiod_get(&pdev->dev, "phy-reset", GPIOD_OUT_HIGH);
	if (IS_ERR(eqos->reset)) {
		err = PTR_ERR(eqos->reset);
		goto disable_tx;
	}

	usleep_range(2000, 4000);
	gpiod_set_value(eqos->reset, 0);

	/* MDIO bus was already reset just above */
	data->mdio_bus_data->needs_reset = false;

	eqos->rst = devm_reset_control_get(&pdev->dev, "eqos");
	if (IS_ERR(eqos->rst)) {
		err = PTR_ERR(eqos->rst);
		goto reset_phy;
	}

	err = reset_control_assert(eqos->rst);
	if (err < 0)
		goto reset_phy;

	usleep_range(2000, 4000);

	err = reset_control_deassert(eqos->rst);
	if (err < 0)
		goto reset_phy;

	usleep_range(2000, 4000);

bypass_clk_reset_gpio:
	data->fix_mac_speed = tegra_eqos_fix_speed;
	data->init = tegra_eqos_init;
	data->bsp_priv = eqos;

	err = tegra_eqos_init(pdev, eqos);
	if (err < 0)
		goto reset;

	return 0;
reset:
	reset_control_assert(eqos->rst);
reset_phy:
	gpiod_set_value(eqos->reset, 1);
disable_tx:
	clk_disable_unprepare(eqos->clk_tx);
disable_rx:
	clk_disable_unprepare(eqos->clk_rx);
disable_slave:
	clk_disable_unprepare(eqos->clk_slave);
disable_master:
	clk_disable_unprepare(eqos->clk_master);
error:
	return err;
}

static int tegra_eqos_remove(struct platform_device *pdev)
{
	struct tegra_eqos *eqos = get_stmmac_bsp_priv(&pdev->dev);

	reset_control_assert(eqos->rst);
	gpiod_set_value(eqos->reset, 1);
	clk_disable_unprepare(eqos->clk_tx);
	clk_disable_unprepare(eqos->clk_rx);
	clk_disable_unprepare(eqos->clk_slave);
	clk_disable_unprepare(eqos->clk_master);

	return 0;
}

struct dwc_eth_dwmac_data {
	int (*probe)(struct platform_device *pdev,
		     struct plat_stmmacenet_data *data,
		     struct stmmac_resources *res);
	int (*remove)(struct platform_device *pdev);
};

static const struct dwc_eth_dwmac_data dwc_qos_data = {
	.probe = dwc_qos_probe,
	.remove = dwc_qos_remove,
};

static const struct dwc_eth_dwmac_data tegra_eqos_data = {
	.probe = tegra_eqos_probe,
	.remove = tegra_eqos_remove,
};

static int dwc_eth_dwmac_probe(struct platform_device *pdev)
{
	const struct dwc_eth_dwmac_data *data;
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	int ret;

	data = device_get_match_data(&pdev->dev);

	memset(&stmmac_res, 0, sizeof(struct stmmac_resources));

	/**
	 * Since stmmac_platform supports name IRQ only, basic platform
	 * resource initialization is done in the glue logic.
	 */
	stmmac_res.irq = platform_get_irq(pdev, 0);
	if (stmmac_res.irq < 0)
		return stmmac_res.irq;
	stmmac_res.wol_irq = stmmac_res.irq;

	stmmac_res.addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(stmmac_res.addr))
		return PTR_ERR(stmmac_res.addr);

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	ret = data->probe(pdev, plat_dat, &stmmac_res);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to probe subdriver: %d\n",
				ret);

		goto remove_config;
	}

	ret = dwc_eth_dwmac_config_dt(pdev, plat_dat);
	if (ret)
		goto remove;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto remove;

	return ret;

remove:
	data->remove(pdev);
remove_config:
	stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

static int dwc_eth_dwmac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	const struct dwc_eth_dwmac_data *data;
	int err;

	data = device_get_match_data(&pdev->dev);

	err = stmmac_dvr_remove(&pdev->dev);
	if (err < 0)
		dev_err(&pdev->dev, "failed to remove platform: %d\n", err);

	err = data->remove(pdev);
	if (err < 0)
		dev_err(&pdev->dev, "failed to remove subdriver: %d\n", err);

	stmmac_remove_config_dt(pdev, priv->plat);

	return err;
}

static const struct of_device_id dwc_eth_dwmac_match[] = {
	{ .compatible = "snps,dwc-qos-ethernet-4.10", .data = &dwc_qos_data },
	{ .compatible = "nvidia,tegra186-eqos", .data = &tegra_eqos_data },
	{ }
};
MODULE_DEVICE_TABLE(of, dwc_eth_dwmac_match);

static struct platform_driver dwc_eth_dwmac_driver = {
	.probe  = dwc_eth_dwmac_probe,
	.remove = dwc_eth_dwmac_remove,
	.driver = {
		.name           = "dwc-eth-dwmac",
		.pm             = &stmmac_pltfr_pm_ops,
		.of_match_table = dwc_eth_dwmac_match,
	},
};
module_platform_driver(dwc_eth_dwmac_driver);

MODULE_AUTHOR("Joao Pinto <jpinto@synopsys.com>");
MODULE_DESCRIPTION("Synopsys DWC Ethernet Quality-of-Service v4.10a driver");
MODULE_LICENSE("GPL v2");
