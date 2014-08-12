/*******************************************************************************
  This contains the functions to handle the platform driver.

  Copyright (C) 2007-2011  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <dt-bindings/gpio/gpio.h>
#include "stmmac.h"
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include <linux/regulator/consumer.h>

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

// RK3288_GRF_SOC_CON1
#define GMAC_PHY_INTF_SEL_RGMII ((0x01C0 << 16) | (0x0040))
#define GMAC_PHY_INTF_SEL_RMII  ((0x01C0 << 16) | (0x0100))
#define GMAC_FLOW_CTRL          ((0x0200 << 16) | (0x0200))
#define GMAC_FLOW_CTRL_CLR      ((0x0200 << 16) | (0x0000))
#define GMAC_SPEED_10M          ((0x0400 << 16) | (0x0000))
#define GMAC_SPEED_100M         ((0x0400 << 16) | (0x0400))
#define GMAC_RMII_CLK_25M       ((0x0800 << 16) | (0x0800))
#define GMAC_RMII_CLK_2_5M      ((0x0800 << 16) | (0x0000))
#define GMAC_CLK_125M           ((0x3000 << 16) | (0x0000))
#define GMAC_CLK_25M            ((0x3000 << 16) | (0x3000))
#define GMAC_CLK_2_5M           ((0x3000 << 16) | (0x2000))
#define GMAC_RMII_MODE          ((0x4000 << 16) | (0x4000))
#define GMAC_RMII_MODE_CLR      ((0x4000 << 16) | (0x0000))

// RK3288_GRF_SOC_CON3
#define GMAC_TXCLK_DLY_ENABLE   ((0x4000 << 16) | (0x4000))
#define GMAC_TXCLK_DLY_DISABLE  ((0x4000 << 16) | (0x0000))
#define GMAC_RXCLK_DLY_ENABLE   ((0x8000 << 16) | (0x8000))
#define GMAC_RXCLK_DLY_DISABLE  ((0x8000 << 16) | (0x0000))
#if 0
#define GMAC_CLK_RX_DL_CFG	((0x3F80 << 16) | (0x0800))
#define GMAC_CLK_TX_DL_CFG	((0x007F << 16) | (0x0040))
#else
#define GMAC_CLK_RX_DL_CFG(val) ((0x3F80 << 16) | (val<<7))        // 7bit
#define GMAC_CLK_TX_DL_CFG(val) ((0x007F << 16) | (val))           // 7bit
#endif
struct bsp_priv g_bsp_priv;

static int phy_power_on(struct plat_stmmacenet_data *plat, int enable)
{
	struct bsp_priv * bsp_priv;
	//int ret;

	printk("%s: enable = %d \n", __func__, enable);

	if ((plat) && (plat->bsp_priv)) {
		bsp_priv = plat->bsp_priv;
	} else {
		pr_err("%s: ERROR: platform data or private data is NULL.\n", __FUNCTION__);
		return -1;
	}

	if (enable) {
		//power on
		if (gpio_is_valid(bsp_priv->power_io)) {
			gpio_direction_output(bsp_priv->power_io, !bsp_priv->power_io_level);
			msleep(10);
			gpio_direction_output(bsp_priv->power_io, bsp_priv->power_io_level);
			//gpio_set_value(bsp_priv->power_io, 1);
		}

		//reset
		if (gpio_is_valid(bsp_priv->reset_io)) {
			gpio_direction_output(bsp_priv->reset_io, bsp_priv->reset_io_level);
			//gpio_set_value(bsp_priv->reset_io, 0);
			msleep(10);
			gpio_direction_output(bsp_priv->reset_io, !bsp_priv->reset_io_level);
		}
		msleep(100);
	} else {
		//power off
		if (gpio_is_valid(bsp_priv->power_io)) {
			gpio_direction_output(bsp_priv->power_io, !bsp_priv->power_io_level);
			//gpio_set_value(bsp_priv->power_io, 0);
		}
	}

	return 0;
}

int stmmc_pltfr_init(struct platform_device *pdev) {
	int phy_iface;
	int err;
	struct bsp_priv *bsp_priv;

	pr_info("%s: \n", __func__);

//iomux
#if 0
	if ((pdev->dev.pins) && (pdev->dev.pins->p)) {
		gmac_state = pinctrl_lookup_state(pdev->dev.pins->p, "default");
		if (IS_ERR(gmac_state)) {
				dev_err(&pdev->dev, "no gmc pinctrl state\n");
				return -1;
		}

		pinctrl_select_state(pdev->dev.pins->p, gmac_state);
	}
#endif

	bsp_priv = &g_bsp_priv;
	phy_iface = bsp_priv->phy_iface;
//power
	if (!gpio_is_valid(bsp_priv->power_io)) {
		pr_err("%s: ERROR: Get power-gpio failed.\n", __func__);
		//return -EINVAL;
	} else {
		err = gpio_request(bsp_priv->power_io, "gmac_phy_power");
		if (err) {
			pr_err("%s: ERROR: Request gmac phy power pin failed.\n", __func__);
			//return -EINVAL;
		}
	}

	if (!gpio_is_valid(bsp_priv->reset_io)) {
		pr_err("%s: ERROR: Get reset-gpio failed.\n", __func__);
		//return -EINVAL;
	} else {
		err = gpio_request(bsp_priv->reset_io, "gmac_phy_reset");
		if (err) {
			pr_err("%s: ERROR: Request gmac phy reset pin failed.\n", __func__);
			//return -EINVAL;
		}
	}
//rmii or rgmii
	if (phy_iface == PHY_INTERFACE_MODE_RGMII) {
		pr_info("%s: init for RGMII\n", __func__);
		grf_writel(GMAC_PHY_INTF_SEL_RGMII, RK3288_GRF_SOC_CON1);
		grf_writel(GMAC_RMII_MODE_CLR, RK3288_GRF_SOC_CON1);
		grf_writel(GMAC_RXCLK_DLY_ENABLE, RK3288_GRF_SOC_CON3);
		grf_writel(GMAC_TXCLK_DLY_ENABLE, RK3288_GRF_SOC_CON3);
		grf_writel(GMAC_CLK_RX_DL_CFG(0x10), RK3288_GRF_SOC_CON3);
		grf_writel(GMAC_CLK_TX_DL_CFG(0x30), RK3288_GRF_SOC_CON3);

	} else if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		pr_info("%s: init for RMII\n", __func__);
		grf_writel(GMAC_PHY_INTF_SEL_RMII, RK3288_GRF_SOC_CON1);
		grf_writel(GMAC_RMII_MODE, RK3288_GRF_SOC_CON1);
	} else {
		pr_err("%s: ERROR: NO interface defined!\n", __func__);
	}

	return 0;
}

void stmmc_pltfr_fix_mac_speed(void *priv, unsigned int speed){
	struct bsp_priv * bsp_priv = priv;
	int interface;

	pr_info("%s: fix speed to %d\n", __func__, speed);

	if (bsp_priv) {
		interface = bsp_priv->phy_iface;
	}

	if (interface == PHY_INTERFACE_MODE_RGMII) {
		pr_info("%s: fix speed for RGMII\n", __func__);

		switch (speed) {
			case 10: {
				grf_writel(GMAC_CLK_2_5M, RK3288_GRF_SOC_CON1);
				break;
			}
			case 100: {
				grf_writel(GMAC_CLK_25M, RK3288_GRF_SOC_CON1);
				break;
			}
			case 1000: {
				grf_writel(GMAC_CLK_125M, RK3288_GRF_SOC_CON1);
				break;
			}
			default: {
				pr_err("%s: ERROR: speed %d is not defined!\n", __func__, speed);
			}
		}

	} else if (interface == PHY_INTERFACE_MODE_RMII) {
		pr_info("%s: fix speed for RMII\n", __func__);
		switch (speed) {
			case 10: {
				grf_writel(GMAC_RMII_CLK_2_5M, RK3288_GRF_SOC_CON1);
				grf_writel(GMAC_SPEED_10M, RK3288_GRF_SOC_CON1);
				break;
			}
			case 100: {
				grf_writel(GMAC_RMII_CLK_25M, RK3288_GRF_SOC_CON1);
				grf_writel(GMAC_SPEED_100M, RK3288_GRF_SOC_CON1);
				break;
			}
			default: {
				pr_err("%s: ERROR: speed %d is not defined!\n", __func__, speed);
			}
		}
	} else {
		pr_err("%s: ERROR: NO interface defined!\n", __func__);
	}
}


#ifdef CONFIG_OF
static int stmmac_probe_config_dt(struct platform_device *pdev,
				  struct plat_stmmacenet_data *plat,
				  const char **mac)
{
	struct device_node *np = pdev->dev.of_node;
	enum of_gpio_flags flags;
	int ret;
	const char * strings = NULL;
	int value;

	if (!np)
		return -ENODEV;

	*mac = of_get_mac_address(np);
	plat->interface = of_get_phy_mode(np);
	//don't care about the return value of of_get_phy_mode(np)
#ifdef CONFIG_GMAC_PHY_RMII
	plat->interface = PHY_INTERFACE_MODE_RMII;
#else
	plat->interface = PHY_INTERFACE_MODE_RGMII;
#endif

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(struct stmmac_mdio_bus_data),
					   GFP_KERNEL);

	plat->init = stmmc_pltfr_init;
	plat->fix_mac_speed = stmmc_pltfr_fix_mac_speed;

	ret = of_property_read_string(np, "pmu_regulator", &strings);
	if (ret) {
		pr_err("%s: Can not read property: pmu_regulator.\n", __func__);
		g_bsp_priv.power_ctrl_by_pmu = false;
	} else {
		pr_info("%s: ethernet phy power controled by pmu(%s).\n", __func__, strings);
		g_bsp_priv.power_ctrl_by_pmu = true;
		strcpy(g_bsp_priv.pmu_regulator, strings);
	}
	ret = of_property_read_u32(np, "pmu_enable_level", &value);
	if (ret) {
		pr_err("%s: Can not read property: pmu_enable_level.\n", __func__);
		g_bsp_priv.power_ctrl_by_pmu = false;
	} else {
		pr_info("%s: ethernet phy power controled by pmu(level = %s).\n", __func__, (value == 1)?"HIGH":"LOW");
		g_bsp_priv.power_ctrl_by_pmu = true;
		g_bsp_priv.pmu_enable_level = value;
	}

	g_bsp_priv.reset_io = 
			of_get_named_gpio_flags(np, "reset-gpio", 0, &flags);
	g_bsp_priv.reset_io_level = (flags == GPIO_ACTIVE_HIGH) ? 1 : 0;
	g_bsp_priv.power_io = 
			of_get_named_gpio_flags(np, "power-gpio", 0, &flags);
	g_bsp_priv.power_io_level = (flags == GPIO_ACTIVE_HIGH) ? 1 : 0;

	g_bsp_priv.phy_iface = plat->interface;
	g_bsp_priv.phy_power_on = phy_power_on;

	plat->bsp_priv = &g_bsp_priv;

	/*
	 * Currently only the properties needed on SPEAr600
	 * are provided. All other properties should be added
	 * once needed on other platforms.
	 */
	if (of_device_is_compatible(np, "rockchip,gmac")) {
		plat->has_gmac = 1;
		plat->pmt = 1;
	}

	return 0;
}
#else
static int stmmac_probe_config_dt(struct platform_device *pdev,
				  struct plat_stmmacenet_data *plat,
				  const char **mac)
{
	return -ENOSYS;
}
#endif /* CONFIG_OF */

/**
 * stmmac_pltfr_probe
 * @pdev: platform device pointer
 * Description: platform_device probe function. It allocates
 * the necessary resources and invokes the main to init
 * the net device, register the mdio bus etc.
 */
static int stmmac_pltfr_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct device *dev = &pdev->dev;
	void __iomem *addr = NULL;
	struct stmmac_priv *priv = NULL;
	struct plat_stmmacenet_data *plat_dat = NULL;
	const char *mac = NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	if (pdev->dev.of_node) {
		plat_dat = devm_kzalloc(&pdev->dev,
					sizeof(struct plat_stmmacenet_data),
					GFP_KERNEL);
		if (!plat_dat) {
			pr_err("%s: ERROR: no memory", __func__);
			return  -ENOMEM;
		}

		ret = stmmac_probe_config_dt(pdev, plat_dat, &mac);
		if (ret) {
			pr_err("%s: ERROR: main dt probe failed", __func__);
			return ret;
		}
	} else {
		plat_dat = pdev->dev.platform_data;
	}

	/* Custom initialisation (if needed)*/
	if (plat_dat->init) {
		ret = plat_dat->init(pdev);
		if (unlikely(ret))
			return ret;
	}

	priv = stmmac_dvr_probe(&(pdev->dev), plat_dat, addr);
	if (!priv) {
		pr_err("%s: ERROR: main driver probe failed", __func__);
		return -ENODEV;
	}

	/* Get MAC address if available (DT) */
	if (mac)
		memcpy(priv->dev->dev_addr, mac, ETH_ALEN);

	/* Get the MAC information */
	priv->dev->irq = platform_get_irq_byname(pdev, "macirq");
	if (priv->dev->irq == -ENXIO) {
		pr_err("%s: ERROR: MAC IRQ configuration "
		       "information not found\n", __func__);
		return -ENXIO;
	}

	/*
	 * On some platforms e.g. SPEAr the wake up irq differs from the mac irq
	 * The external wake up irq can be passed through the platform code
	 * named as "eth_wake_irq"
	 *
	 * In case the wake up interrupt is not passed from the platform
	 * so the driver will continue to use the mac irq (ndev->irq)
	 */
	priv->wol_irq = platform_get_irq_byname(pdev, "eth_wake_irq");
	if (priv->wol_irq == -ENXIO)
		priv->wol_irq = priv->dev->irq;

	priv->lpi_irq = platform_get_irq_byname(pdev, "eth_lpi");

	platform_set_drvdata(pdev, priv->dev);

	pr_debug("STMMAC platform driver registration completed");

	return 0;
}

/**
 * stmmac_pltfr_remove
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and calls the platforms hook and release the resources (e.g. mem).
 */
static int stmmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = stmmac_dvr_remove(ndev);

	if (priv->plat->exit)
		priv->plat->exit(pdev);

	platform_set_drvdata(pdev, NULL);

	return ret;
}

#ifdef CONFIG_PM
static int stmmac_pltfr_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_suspend(ndev);
}

static int stmmac_pltfr_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_resume(ndev);
}

int stmmac_pltfr_freeze(struct device *dev)
{
	int ret;
	struct plat_stmmacenet_data *plat_dat = dev_get_platdata(dev);
	struct net_device *ndev = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);

	ret = stmmac_freeze(ndev);
	if (plat_dat->exit)
		plat_dat->exit(pdev);

	return ret;
}

int stmmac_pltfr_restore(struct device *dev)
{
	struct plat_stmmacenet_data *plat_dat = dev_get_platdata(dev);
	struct net_device *ndev = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);

	if (plat_dat->init)
		plat_dat->init(pdev);

	return stmmac_restore(ndev);
}

static const struct dev_pm_ops stmmac_pltfr_pm_ops = {
	.suspend = stmmac_pltfr_suspend,
	.resume = stmmac_pltfr_resume,
	.freeze = stmmac_pltfr_freeze,
	.thaw = stmmac_pltfr_restore,
	.restore = stmmac_pltfr_restore,
};
#else
static const struct dev_pm_ops stmmac_pltfr_pm_ops;
#endif /* CONFIG_PM */

static const struct of_device_id stmmac_dt_ids[] = {
	{ .compatible = "rockchip,gmac"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, stmmac_dt_ids);

struct platform_driver stmmac_pltfr_driver = {
	.probe = stmmac_pltfr_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		   .name = STMMAC_RESOURCE_NAME,
		   .owner = THIS_MODULE,
		   .pm = &stmmac_pltfr_pm_ops,
		   .of_match_table = of_match_ptr(stmmac_dt_ids),
		   },
};

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet PLATFORM driver");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
