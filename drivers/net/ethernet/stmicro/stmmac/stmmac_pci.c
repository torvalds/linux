/*******************************************************************************
  This contains the functions to handle the pci driver.

  Copyright (C) 2011-2012  Vayavya Labs Pvt Ltd

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

  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/pci.h>
#include "stmmac.h"

static struct plat_stmmacenet_data plat_dat;
static struct stmmac_mdio_bus_data mdio_data;
static struct stmmac_dma_cfg dma_cfg;

static void stmmac_default_data(void)
{
	memset(&plat_dat, 0, sizeof(struct plat_stmmacenet_data));

	plat_dat.bus_id = 1;
	plat_dat.phy_addr = 0;
	plat_dat.interface = PHY_INTERFACE_MODE_GMII;
	plat_dat.clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat_dat.has_gmac = 1;
	plat_dat.force_sf_dma_mode = 1;

	mdio_data.phy_reset = NULL;
	mdio_data.phy_mask = 0;
	plat_dat.mdio_bus_data = &mdio_data;

	dma_cfg.pbl = 32;
	dma_cfg.burst_len = DMA_AXI_BLEN_256;
	plat_dat.dma_cfg = &dma_cfg;

	/* Set default value for multicast hash bins */
	plat_dat.multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat_dat.unicast_filter_entries = 1;
}

/**
 * stmmac_pci_probe
 *
 * @pdev: pci device pointer
 * @id: pointer to table of device id/id's.
 *
 * Description: This probing function gets called for all PCI devices which
 * match the ID table and are not "owned" by other driver yet. This function
 * gets passed a "struct pci_dev *" for each device whose entry in the ID table
 * matches the device. The probe functions returns zero when the driver choose
 * to take "ownership" of the device or an error code(-ve no) otherwise.
 */
static int stmmac_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct stmmac_priv *priv;
	int i;
	int ret;

	/* Enable pci device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		pr_err("%s : ERROR: failed to enable %s device\n", __func__,
		       pci_name(pdev));
		return ret;
	}

	/* Get the base address of device */
	for (i = 0; i <= PCI_STD_RESOURCE_END; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(i), pci_name(pdev));
		if (ret)
			return ret;
		break;
	}

	pci_set_master(pdev);

	stmmac_default_data();

	priv = stmmac_dvr_probe(&pdev->dev, &plat_dat,
				pcim_iomap_table(pdev)[i]);
	if (IS_ERR(priv)) {
		pr_err("%s: main driver probe failed", __func__);
		return PTR_ERR(priv);
	}
	priv->dev->irq = pdev->irq;
	priv->wol_irq = pdev->irq;

	pci_set_drvdata(pdev, priv->dev);

	pr_debug("STMMAC platform driver registration completed");

	return 0;
}

/**
 * stmmac_pci_remove
 *
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and releases the PCI resources.
 */
static void stmmac_pci_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);

	stmmac_dvr_remove(ndev);
}

#ifdef CONFIG_PM_SLEEP
static int stmmac_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *ndev = pci_get_drvdata(pdev);

	return stmmac_suspend(ndev);
}

static int stmmac_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *ndev = pci_get_drvdata(pdev);

	return stmmac_resume(ndev);
}
#endif

static SIMPLE_DEV_PM_OPS(stmmac_pm_ops, stmmac_pci_suspend, stmmac_pci_resume);

#define STMMAC_VENDOR_ID 0x700
#define STMMAC_DEVICE_ID 0x1108

static const struct pci_device_id stmmac_id_table[] = {
	{PCI_DEVICE(STMMAC_VENDOR_ID, STMMAC_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_MAC)},
	{}
};

MODULE_DEVICE_TABLE(pci, stmmac_id_table);

struct pci_driver stmmac_pci_driver = {
	.name = STMMAC_RESOURCE_NAME,
	.id_table = stmmac_id_table,
	.probe = stmmac_pci_probe,
	.remove = stmmac_pci_remove,
	.driver         = {
		.pm     = &stmmac_pm_ops,
	},
};

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet PCI driver");
MODULE_AUTHOR("Rayagond Kokatanur <rayagond.kokatanur@vayavyalabs.com>");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
