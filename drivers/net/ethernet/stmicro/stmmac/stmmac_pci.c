// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  This contains the functions to handle the pci driver.

  Copyright (C) 2011-2012  Vayavya Labs Pvt Ltd


  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>

#include "stmmac.h"
#include "stmmac_libpci.h"

struct stmmac_pci_info {
	int (*setup)(struct pci_dev *pdev, struct plat_stmmacenet_data *plat);
};

static void common_default_data(struct plat_stmmacenet_data *plat)
{
	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->clk_csr = STMMAC_CSR_20_35M;
	plat->core_type = DWMAC_CORE_GMAC;
	plat->force_sf_dma_mode = 1;

	plat->mdio_bus_data->needs_reset = true;
}

static int stmmac_default_data(struct pci_dev *pdev,
			       struct plat_stmmacenet_data *plat)
{
	/* Set common default data first */
	common_default_data(plat);

	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->phy_interface = PHY_INTERFACE_MODE_GMII;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;
	/* TODO: AXI */

	return 0;
}

static const struct stmmac_pci_info stmmac_pci_info = {
	.setup = stmmac_default_data,
};

static int snps_gmac5_default_data(struct pci_dev *pdev,
				   struct plat_stmmacenet_data *plat)
{
	int i;

	plat->clk_csr = STMMAC_CSR_250_300M;
	plat->core_type = DWMAC_CORE_GMAC4;
	plat->force_sf_dma_mode = 1;
	plat->flags |= STMMAC_FLAG_TSO_EN;
	plat->pmt = 1;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 4;
	plat->rx_queues_to_use = 4;

	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	for (i = 0; i < plat->tx_queues_to_use; i++) {
		plat->tx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;
		plat->tx_queues_cfg[i].weight = 25;
		if (i > 0)
			plat->tx_queues_cfg[i].tbs_en = 1;
	}

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	for (i = 0; i < plat->rx_queues_to_use; i++)
		plat->rx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;

	plat->bus_id = 1;
	plat->phy_interface = PHY_INTERFACE_MODE_GMII;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	/* Axi Configuration */
	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi), GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

	plat->axi->axi_wr_osr_lmt = 31;
	plat->axi->axi_rd_osr_lmt = 31;

	plat->axi->axi_fb = false;
	plat->axi->axi_blen_regval = DMA_AXI_BLEN4 | DMA_AXI_BLEN8 |
				     DMA_AXI_BLEN16 | DMA_AXI_BLEN32;

	return 0;
}

static const struct stmmac_pci_info snps_gmac5_pci_info = {
	.setup = snps_gmac5_default_data,
};

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
	struct stmmac_pci_info *info = (struct stmmac_pci_info *)id->driver_data;
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res = {};
	int ret;
	int i;

	plat = stmmac_plat_dat_alloc(&pdev->dev);
	if (!plat)
		return -ENOMEM;

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(*plat->mdio_bus_data),
					   GFP_KERNEL);
	if (!plat->mdio_bus_data)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg),
				     GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	plat->safety_feat_cfg = devm_kzalloc(&pdev->dev,
					     sizeof(*plat->safety_feat_cfg),
					     GFP_KERNEL);
	if (!plat->safety_feat_cfg)
		return -ENOMEM;

	/* Enable pci device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n",
			__func__);
		return ret;
	}

	/* Get the base address of device */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		res.addr = pcim_iomap_region(pdev, i, STMMAC_RESOURCE_NAME);
		if (IS_ERR(res.addr))
			return PTR_ERR(res.addr);
		break;
	}

	pci_set_master(pdev);

	ret = info->setup(pdev, plat);
	if (ret)
		return ret;

	res.wol_irq = pdev->irq;
	res.irq = pdev->irq;

	plat->safety_feat_cfg->tsoee = 1;
	plat->safety_feat_cfg->mrxpee = 1;
	plat->safety_feat_cfg->mestee = 1;
	plat->safety_feat_cfg->mrxee = 1;
	plat->safety_feat_cfg->mtxee = 1;
	plat->safety_feat_cfg->epsi = 1;
	plat->safety_feat_cfg->edpp = 1;
	plat->safety_feat_cfg->prtyen = 1;
	plat->safety_feat_cfg->tmouten = 1;

	plat->suspend = stmmac_pci_plat_suspend;
	plat->resume = stmmac_pci_plat_resume;

	return stmmac_dvr_probe(&pdev->dev, plat, &res);
}

/**
 * stmmac_pci_remove
 *
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources.
 */
static void stmmac_pci_remove(struct pci_dev *pdev)
{
	stmmac_dvr_remove(&pdev->dev);
}

/* synthetic ID, no official vendor */
#define PCI_VENDOR_ID_STMMAC		0x0700

#define PCI_DEVICE_ID_STMMAC_STMMAC		0x1108
#define PCI_DEVICE_ID_SYNOPSYS_GMAC5_ID		0x7102

static const struct pci_device_id stmmac_id_table[] = {
	{ PCI_DEVICE_DATA(STMMAC, STMMAC, &stmmac_pci_info) },
	{ PCI_DEVICE_DATA(STMICRO, MAC, &stmmac_pci_info) },
	{ PCI_DEVICE_DATA(SYNOPSYS, GMAC5_ID, &snps_gmac5_pci_info) },
	{}
};

MODULE_DEVICE_TABLE(pci, stmmac_id_table);

static struct pci_driver stmmac_pci_driver = {
	.name = STMMAC_RESOURCE_NAME,
	.id_table = stmmac_id_table,
	.probe = stmmac_pci_probe,
	.remove = stmmac_pci_remove,
	.driver         = {
		.pm     = &stmmac_simple_pm_ops,
	},
};

module_pci_driver(stmmac_pci_driver);

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet PCI driver");
MODULE_AUTHOR("Rayagond Kokatanur <rayagond.kokatanur@vayavyalabs.com>");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
