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
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#ifdef CONFIG_OF

/**
 * dwmac1000_validate_mcast_bins - validates the number of Multicast filter bins
 * @mcast_bins: Multicast filtering bins
 * Description:
 * this function validates the number of Multicast filtering bins specified
 * by the configuration through the device tree. The Synopsys GMAC supports
 * 64 bins, 128 bins, or 256 bins. "bins" refer to the division of CRC
 * number space. 64 bins correspond to 6 bits of the CRC, 128 corresponds
 * to 7 bits, and 256 refers to 8 bits of the CRC. Any other setting is
 * invalid and will cause the filtering algorithm to use Multicast
 * promiscuous mode.
 */
static int dwmac1000_validate_mcast_bins(int mcast_bins)
{
	int x = mcast_bins;

	switch (x) {
	case HASH_TABLE_SIZE:
	case 128:
	case 256:
		break;
	default:
		x = 0;
		pr_info("Hash table entries set to unexpected value %d",
			mcast_bins);
		break;
	}
	return x;
}

/**
 * dwmac1000_validate_ucast_entries - validate the Unicast address entries
 * @ucast_entries: number of Unicast address entries
 * Description:
 * This function validates the number of Unicast address entries supported
 * by a particular Synopsys 10/100/1000 controller. The Synopsys controller
 * supports 1, 32, 64, or 128 Unicast filter entries for it's Unicast filter
 * logic. This function validates a valid, supported configuration is
 * selected, and defaults to 1 Unicast address if an unsupported
 * configuration is selected.
 */
static int dwmac1000_validate_ucast_entries(int ucast_entries)
{
	int x = ucast_entries;

	switch (x) {
	case 1:
	case 32:
	case 64:
	case 128:
		break;
	default:
		x = 1;
		pr_info("Unicast table entries set to unexpected value %d\n",
			ucast_entries);
		break;
	}
	return x;
}

/**
 * stmmac_probe_config_dt - parse device-tree driver parameters
 * @pdev: platform_device structure
 * @plat: driver data platform structure
 * @mac: MAC address to use
 * Description:
 * this function is to read the driver parameters from device-tree and
 * set some private fields that will be used by the main at runtime.
 */
static int stmmac_probe_config_dt(struct platform_device *pdev,
				  struct plat_stmmacenet_data *plat,
				  const char **mac)
{
	struct device_node *np = pdev->dev.of_node;
	struct stmmac_dma_cfg *dma_cfg;
	const struct of_device_id *device;
	struct device *dev = &pdev->dev;

	device = of_match_device(dev->driver->of_match_table, dev);
	if (device->data) {
		const struct stmmac_of_data *data = device->data;
		plat->has_gmac = data->has_gmac;
		plat->enh_desc = data->enh_desc;
		plat->tx_coe = data->tx_coe;
		plat->rx_coe = data->rx_coe;
		plat->bugged_jumbo = data->bugged_jumbo;
		plat->pmt = data->pmt;
		plat->riwt_off = data->riwt_off;
		plat->fix_mac_speed = data->fix_mac_speed;
		plat->bus_setup = data->bus_setup;
		plat->setup = data->setup;
		plat->free = data->free;
		plat->init = data->init;
		plat->exit = data->exit;
	}

	*mac = of_get_mac_address(np);
	plat->interface = of_get_phy_mode(np);

	/* Get max speed of operation from device tree */
	if (of_property_read_u32(np, "max-speed", &plat->max_speed))
		plat->max_speed = -1;

	plat->bus_id = of_alias_get_id(np, "ethernet");
	if (plat->bus_id < 0)
		plat->bus_id = 0;

	/* Default to phy auto-detection */
	plat->phy_addr = -1;

	/* If we find a phy-handle property, use it as the PHY */
	plat->phy_node = of_parse_phandle(np, "phy-handle", 0);

	/* If phy-handle is not specified, check if we have a fixed-phy */
	if (!plat->phy_node && of_phy_is_fixed_link(np)) {
		if ((of_phy_register_fixed_link(np) < 0))
			return -ENODEV;

		plat->phy_node = of_node_get(np);
	}

	/* "snps,phy-addr" is not a standard property. Mark it as deprecated
	 * and warn of its use. Remove this when phy node support is added.
	 */
	if (of_property_read_u32(np, "snps,phy-addr", &plat->phy_addr) == 0)
		dev_warn(&pdev->dev, "snps,phy-addr property is deprecated\n");

	if (plat->phy_node || plat->phy_bus_name)
		plat->mdio_bus_data = NULL;
	else
		plat->mdio_bus_data =
			devm_kzalloc(&pdev->dev,
				     sizeof(struct stmmac_mdio_bus_data),
				     GFP_KERNEL);

	of_property_read_u32(np, "tx-fifo-depth", &plat->tx_fifo_size);

	of_property_read_u32(np, "rx-fifo-depth", &plat->rx_fifo_size);

	plat->force_sf_dma_mode =
		of_property_read_bool(np, "snps,force_sf_dma_mode");

	/* Set the maxmtu to a default of JUMBO_LEN in case the
	 * parameter is not present in the device tree.
	 */
	plat->maxmtu = JUMBO_LEN;

	/*
	 * Currently only the properties needed on SPEAr600
	 * are provided. All other properties should be added
	 * once needed on other platforms.
	 */
	if (of_device_is_compatible(np, "st,spear600-gmac") ||
		of_device_is_compatible(np, "snps,dwmac-3.70a") ||
		of_device_is_compatible(np, "snps,dwmac")) {
		/* Note that the max-frame-size parameter as defined in the
		 * ePAPR v1.1 spec is defined as max-frame-size, it's
		 * actually used as the IEEE definition of MAC Client
		 * data, or MTU. The ePAPR specification is confusing as
		 * the definition is max-frame-size, but usage examples
		 * are clearly MTUs
		 */
		of_property_read_u32(np, "max-frame-size", &plat->maxmtu);
		of_property_read_u32(np, "snps,multicast-filter-bins",
				     &plat->multicast_filter_bins);
		of_property_read_u32(np, "snps,perfect-filter-entries",
				     &plat->unicast_filter_entries);
		plat->unicast_filter_entries = dwmac1000_validate_ucast_entries(
					       plat->unicast_filter_entries);
		plat->multicast_filter_bins = dwmac1000_validate_mcast_bins(
					      plat->multicast_filter_bins);
		plat->has_gmac = 1;
		plat->pmt = 1;
	}

	if (of_device_is_compatible(np, "snps,dwmac-3.610") ||
		of_device_is_compatible(np, "snps,dwmac-3.710")) {
		plat->enh_desc = 1;
		plat->bugged_jumbo = 1;
		plat->force_sf_dma_mode = 1;
	}

	if (of_find_property(np, "snps,pbl", NULL)) {
		dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*dma_cfg),
				       GFP_KERNEL);
		if (!dma_cfg) {
			of_node_put(np);
			return -ENOMEM;
		}
		plat->dma_cfg = dma_cfg;
		of_property_read_u32(np, "snps,pbl", &dma_cfg->pbl);
		dma_cfg->fixed_burst =
			of_property_read_bool(np, "snps,fixed-burst");
		dma_cfg->mixed_burst =
			of_property_read_bool(np, "snps,mixed-burst");
		of_property_read_u32(np, "snps,burst_len", &dma_cfg->burst_len);
		if (dma_cfg->burst_len < 0 || dma_cfg->burst_len > 256)
			dma_cfg->burst_len = 0;
	}
	plat->force_thresh_dma_mode = of_property_read_bool(np, "snps,force_thresh_dma_mode");
	if (plat->force_thresh_dma_mode) {
		plat->force_sf_dma_mode = 0;
		pr_warn("force_sf_dma_mode is ignored if force_thresh_dma_mode is set.");
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
 * stmmac_pltfr_probe - platform driver probe.
 * @pdev: platform device pointer
 * Description: platform_device probe function. It is to allocate
 * the necessary platform resources, invoke custom helper (if required) and
 * invoke the main probe function.
 */
int stmmac_pltfr_probe(struct platform_device *pdev)
{
	struct stmmac_resources stmmac_res;
	int ret = 0;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct plat_stmmacenet_data *plat_dat = NULL;

	memset(&stmmac_res, 0, sizeof(stmmac_res));

	/* Get IRQ information early to have an ability to ask for deferred
	 * probe if needed before we went too far with resource allocation.
	 */
	stmmac_res.irq = platform_get_irq_byname(pdev, "macirq");
	if (stmmac_res.irq < 0) {
		if (stmmac_res.irq != -EPROBE_DEFER) {
			dev_err(dev,
				"MAC IRQ configuration information not found\n");
		}
		return stmmac_res.irq;
	}

	/* On some platforms e.g. SPEAr the wake up irq differs from the mac irq
	 * The external wake up irq can be passed through the platform code
	 * named as "eth_wake_irq"
	 *
	 * In case the wake up interrupt is not passed from the platform
	 * so the driver will continue to use the mac irq (ndev->irq)
	 */
	stmmac_res.wol_irq = platform_get_irq_byname(pdev, "eth_wake_irq");
	if (stmmac_res.wol_irq < 0) {
		if (stmmac_res.wol_irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		stmmac_res.wol_irq = stmmac_res.irq;
	}

	stmmac_res.lpi_irq = platform_get_irq_byname(pdev, "eth_lpi");
	if (stmmac_res.lpi_irq == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	stmmac_res.addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(stmmac_res.addr))
		return PTR_ERR(stmmac_res.addr);

	plat_dat = dev_get_platdata(&pdev->dev);

	if (!plat_dat)
		plat_dat = devm_kzalloc(&pdev->dev,
					sizeof(struct plat_stmmacenet_data),
					GFP_KERNEL);
	if (!plat_dat) {
		pr_err("%s: ERROR: no memory", __func__);
		return  -ENOMEM;
	}

	/* Set default value for multicast hash bins */
	plat_dat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat_dat->unicast_filter_entries = 1;

	if (pdev->dev.of_node) {
		ret = stmmac_probe_config_dt(pdev, plat_dat, &stmmac_res.mac);
		if (ret) {
			pr_err("%s: main dt probe failed", __func__);
			return ret;
		}
	}

	/* Custom setup (if needed) */
	if (plat_dat->setup) {
		plat_dat->bsp_priv = plat_dat->setup(pdev);
		if (IS_ERR(plat_dat->bsp_priv))
			return PTR_ERR(plat_dat->bsp_priv);
	}

	/* Custom initialisation (if needed)*/
	if (plat_dat->init) {
		ret = plat_dat->init(pdev, plat_dat->bsp_priv);
		if (unlikely(ret))
			return ret;
	}

	return stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
}
EXPORT_SYMBOL_GPL(stmmac_pltfr_probe);

/**
 * stmmac_pltfr_remove
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and calls the platforms hook and release the resources (e.g. mem).
 */
int stmmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = stmmac_dvr_remove(ndev);

	if (priv->plat->exit)
		priv->plat->exit(pdev, priv->plat->bsp_priv);

	if (priv->plat->free)
		priv->plat->free(pdev, priv->plat->bsp_priv);

	return ret;
}
EXPORT_SYMBOL_GPL(stmmac_pltfr_remove);

#ifdef CONFIG_PM_SLEEP
/**
 * stmmac_pltfr_suspend
 * @dev: device pointer
 * Description: this function is invoked when suspend the driver and it direcly
 * call the main suspend function and then, if required, on some platform, it
 * can call an exit helper.
 */
static int stmmac_pltfr_suspend(struct device *dev)
{
	int ret;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);

	ret = stmmac_suspend(ndev);
	if (priv->plat->exit)
		priv->plat->exit(pdev, priv->plat->bsp_priv);

	return ret;
}

/**
 * stmmac_pltfr_resume
 * @dev: device pointer
 * Description: this function is invoked when resume the driver before calling
 * the main resume function, on some platforms, it can call own init helper
 * if required.
 */
static int stmmac_pltfr_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);

	if (priv->plat->init)
		priv->plat->init(pdev, priv->plat->bsp_priv);

	return stmmac_resume(ndev);
}
#endif /* CONFIG_PM_SLEEP */

SIMPLE_DEV_PM_OPS(stmmac_pltfr_pm_ops, stmmac_pltfr_suspend,
				       stmmac_pltfr_resume);
EXPORT_SYMBOL_GPL(stmmac_pltfr_pm_ops);

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet platform support");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
