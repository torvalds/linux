// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation
 */

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include "stmmac.h"

/* This struct is used to associate PCI Function of MAC controller on a board,
 * discovered via DMI, with the address of PHY connected to the MAC. The
 * negative value of the address means that MAC controller is not connected
 * with PHY.
 */
struct stmmac_pci_func_data {
	unsigned int func;
	int phy_addr;
};

struct stmmac_pci_dmi_data {
	const struct stmmac_pci_func_data *func;
	size_t nfuncs;
};

struct stmmac_pci_info {
	int (*setup)(struct pci_dev *pdev, struct plat_stmmacenet_data *plat);
};

static int stmmac_pci_find_phy_addr(struct pci_dev *pdev,
				    const struct dmi_system_id *dmi_list)
{
	const struct stmmac_pci_func_data *func_data;
	const struct stmmac_pci_dmi_data *dmi_data;
	const struct dmi_system_id *dmi_id;
	int func = PCI_FUNC(pdev->devfn);
	size_t n;

	dmi_id = dmi_first_match(dmi_list);
	if (!dmi_id)
		return -ENODEV;

	dmi_data = dmi_id->driver_data;
	func_data = dmi_data->func;

	for (n = 0; n < dmi_data->nfuncs; n++, func_data++)
		if (func_data->func == func)
			return func_data->phy_addr;

	return -ENODEV;
}

static void common_default_data(struct plat_stmmacenet_data *plat)
{
	plat->clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

	plat->mdio_bus_data->needs_reset = true;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 1;
	plat->rx_queues_to_use = 1;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].use_prio = false;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;
}

static int intel_mgbe_common_data(struct pci_dev *pdev,
				  struct plat_stmmacenet_data *plat)
{
	int i;

	plat->clk_csr = 5;
	plat->has_gmac = 0;
	plat->has_gmac4 = 1;
	plat->force_sf_dma_mode = 0;
	plat->tso_en = 1;

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;

	for (i = 0; i < plat->rx_queues_to_use; i++) {
		plat->rx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;
		plat->rx_queues_cfg[i].chan = i;

		/* Disable Priority config by default */
		plat->rx_queues_cfg[i].use_prio = false;

		/* Disable RX queues routing by default */
		plat->rx_queues_cfg[i].pkt_route = 0x0;
	}

	for (i = 0; i < plat->tx_queues_to_use; i++) {
		plat->tx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;

		/* Disable Priority config by default */
		plat->tx_queues_cfg[i].use_prio = false;
	}

	/* FIFO size is 4096 bytes for 1 tx/rx queue */
	plat->tx_fifo_size = plat->tx_queues_to_use * 4096;
	plat->rx_fifo_size = plat->rx_queues_to_use * 4096;

	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	plat->tx_queues_cfg[0].weight = 0x09;
	plat->tx_queues_cfg[1].weight = 0x0A;
	plat->tx_queues_cfg[2].weight = 0x0B;
	plat->tx_queues_cfg[3].weight = 0x0C;
	plat->tx_queues_cfg[4].weight = 0x0D;
	plat->tx_queues_cfg[5].weight = 0x0E;
	plat->tx_queues_cfg[6].weight = 0x0F;
	plat->tx_queues_cfg[7].weight = 0x10;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;
	plat->dma_cfg->fixed_burst = 0;
	plat->dma_cfg->mixed_burst = 0;
	plat->dma_cfg->aal = 0;

	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi),
				 GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

	plat->axi->axi_lpi_en = 0;
	plat->axi->axi_xit_frm = 0;
	plat->axi->axi_wr_osr_lmt = 1;
	plat->axi->axi_rd_osr_lmt = 1;
	plat->axi->axi_blen[0] = 4;
	plat->axi->axi_blen[1] = 8;
	plat->axi->axi_blen[2] = 16;

	plat->ptp_max_adj = plat->clk_ptp_rate;

	/* Set system clock */
	plat->stmmac_clk = clk_register_fixed_rate(&pdev->dev,
						   "stmmac-clk", NULL, 0,
						   plat->clk_ptp_rate);

	if (IS_ERR(plat->stmmac_clk)) {
		dev_warn(&pdev->dev, "Fail to register stmmac-clk\n");
		plat->stmmac_clk = NULL;
	}
	clk_prepare_enable(plat->stmmac_clk);

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	return 0;
}

static int ehl_common_data(struct pci_dev *pdev,
			   struct plat_stmmacenet_data *plat)
{
	int ret;

	plat->rx_queues_to_use = 8;
	plat->tx_queues_to_use = 8;
	plat->clk_ptp_rate = 200000000;
	ret = intel_mgbe_common_data(pdev, plat);
	if (ret)
		return ret;

	return 0;
}

static int ehl_sgmii_data(struct pci_dev *pdev,
			  struct plat_stmmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->phy_interface = PHY_INTERFACE_MODE_SGMII;

	return ehl_common_data(pdev, plat);
}

static struct stmmac_pci_info ehl_sgmii1g_pci_info = {
	.setup = ehl_sgmii_data,
};

static int ehl_rgmii_data(struct pci_dev *pdev,
			  struct plat_stmmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->phy_interface = PHY_INTERFACE_MODE_RGMII;

	return ehl_common_data(pdev, plat);
}

static struct stmmac_pci_info ehl_rgmii1g_pci_info = {
	.setup = ehl_rgmii_data,
};

static int ehl_pse0_common_data(struct pci_dev *pdev,
				struct plat_stmmacenet_data *plat)
{
	plat->bus_id = 2;
	plat->phy_addr = 1;
	return ehl_common_data(pdev, plat);
}

static int ehl_pse0_rgmii1g_data(struct pci_dev *pdev,
				 struct plat_stmmacenet_data *plat)
{
	plat->phy_interface = PHY_INTERFACE_MODE_RGMII_ID;
	return ehl_pse0_common_data(pdev, plat);
}

static struct stmmac_pci_info ehl_pse0_rgmii1g_pci_info = {
	.setup = ehl_pse0_rgmii1g_data,
};

static int ehl_pse0_sgmii1g_data(struct pci_dev *pdev,
				 struct plat_stmmacenet_data *plat)
{
	plat->phy_interface = PHY_INTERFACE_MODE_SGMII;
	return ehl_pse0_common_data(pdev, plat);
}

static struct stmmac_pci_info ehl_pse0_sgmii1g_pci_info = {
	.setup = ehl_pse0_sgmii1g_data,
};

static int ehl_pse1_common_data(struct pci_dev *pdev,
				struct plat_stmmacenet_data *plat)
{
	plat->bus_id = 3;
	plat->phy_addr = 1;
	return ehl_common_data(pdev, plat);
}

static int ehl_pse1_rgmii1g_data(struct pci_dev *pdev,
				 struct plat_stmmacenet_data *plat)
{
	plat->phy_interface = PHY_INTERFACE_MODE_RGMII_ID;
	return ehl_pse1_common_data(pdev, plat);
}

static struct stmmac_pci_info ehl_pse1_rgmii1g_pci_info = {
	.setup = ehl_pse1_rgmii1g_data,
};

static int ehl_pse1_sgmii1g_data(struct pci_dev *pdev,
				 struct plat_stmmacenet_data *plat)
{
	plat->phy_interface = PHY_INTERFACE_MODE_SGMII;
	return ehl_pse1_common_data(pdev, plat);
}

static struct stmmac_pci_info ehl_pse1_sgmii1g_pci_info = {
	.setup = ehl_pse1_sgmii1g_data,
};

static int tgl_common_data(struct pci_dev *pdev,
			   struct plat_stmmacenet_data *plat)
{
	int ret;

	plat->rx_queues_to_use = 6;
	plat->tx_queues_to_use = 4;
	plat->clk_ptp_rate = 200000000;
	ret = intel_mgbe_common_data(pdev, plat);
	if (ret)
		return ret;

	return 0;
}

static int tgl_sgmii_data(struct pci_dev *pdev,
			  struct plat_stmmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->phy_interface = PHY_INTERFACE_MODE_SGMII;
	return tgl_common_data(pdev, plat);
}

static struct stmmac_pci_info tgl_sgmii1g_pci_info = {
	.setup = tgl_sgmii_data,
};

static const struct stmmac_pci_func_data galileo_stmmac_func_data[] = {
	{
		.func = 6,
		.phy_addr = 1,
	},
};

static const struct stmmac_pci_dmi_data galileo_stmmac_dmi_data = {
	.func = galileo_stmmac_func_data,
	.nfuncs = ARRAY_SIZE(galileo_stmmac_func_data),
};

static const struct stmmac_pci_func_data iot2040_stmmac_func_data[] = {
	{
		.func = 6,
		.phy_addr = 1,
	},
	{
		.func = 7,
		.phy_addr = 1,
	},
};

static const struct stmmac_pci_dmi_data iot2040_stmmac_dmi_data = {
	.func = iot2040_stmmac_func_data,
	.nfuncs = ARRAY_SIZE(iot2040_stmmac_func_data),
};

static const struct dmi_system_id quark_pci_dmi[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "Galileo"),
		},
		.driver_data = (void *)&galileo_stmmac_dmi_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GalileoGen2"),
		},
		.driver_data = (void *)&galileo_stmmac_dmi_data,
	},
	/* There are 2 types of SIMATIC IOT2000: IOT2020 and IOT2040.
	 * The asset tag "6ES7647-0AA00-0YA2" is only for IOT2020 which
	 * has only one pci network device while other asset tags are
	 * for IOT2040 which has two.
	 */
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
			DMI_EXACT_MATCH(DMI_BOARD_ASSET_TAG,
					"6ES7647-0AA00-0YA2"),
		},
		.driver_data = (void *)&galileo_stmmac_dmi_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
		},
		.driver_data = (void *)&iot2040_stmmac_dmi_data,
	},
	{}
};

static int quark_default_data(struct pci_dev *pdev,
			      struct plat_stmmacenet_data *plat)
{
	int ret;

	/* Set common default data first */
	common_default_data(plat);

	/* Refuse to load the driver and register net device if MAC controller
	 * does not connect to any PHY interface.
	 */
	ret = stmmac_pci_find_phy_addr(pdev, quark_pci_dmi);
	if (ret < 0) {
		/* Return error to the caller on DMI enabled boards. */
		if (dmi_get_system_info(DMI_BOARD_NAME))
			return ret;

		/* Galileo boards with old firmware don't support DMI. We always
		 * use 1 here as PHY address, so at least the first found MAC
		 * controller would be probed.
		 */
		ret = 1;
	}

	plat->bus_id = pci_dev_id(pdev);
	plat->phy_addr = ret;
	plat->phy_interface = PHY_INTERFACE_MODE_RMII;

	plat->dma_cfg->pbl = 16;
	plat->dma_cfg->pblx8 = true;
	plat->dma_cfg->fixed_burst = 1;
	/* AXI (TODO) */

	return 0;
}

static const struct stmmac_pci_info quark_pci_info = {
	.setup = quark_default_data,
};

/**
 * intel_eth_pci_probe
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
static int intel_eth_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct stmmac_pci_info *info = (struct stmmac_pci_info *)id->driver_data;
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res;
	int i;
	int ret;

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
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

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n",
			__func__);
		return ret;
	}

	/* Get the base address of device */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(i), pci_name(pdev));
		if (ret)
			return ret;
		break;
	}

	pci_set_master(pdev);

	ret = info->setup(pdev, plat);
	if (ret)
		return ret;

	pci_enable_msi(pdev);

	memset(&res, 0, sizeof(res));
	res.addr = pcim_iomap_table(pdev)[i];
	res.wol_irq = pdev->irq;
	res.irq = pdev->irq;

	return stmmac_dvr_probe(&pdev->dev, plat, &res);
}

/**
 * intel_eth_pci_remove
 *
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and releases the PCI resources.
 */
static void intel_eth_pci_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int i;

	stmmac_dvr_remove(&pdev->dev);

	if (priv->plat->stmmac_clk)
		clk_unregister_fixed_rate(priv->plat->stmmac_clk);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		pcim_iounmap_regions(pdev, BIT(i));
		break;
	}

	pci_disable_device(pdev);
}

static int __maybe_unused intel_eth_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = stmmac_suspend(dev);
	if (ret)
		return ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, true);
	return 0;
}

static int __maybe_unused intel_eth_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return stmmac_resume(dev);
}

static SIMPLE_DEV_PM_OPS(intel_eth_pm_ops, intel_eth_pci_suspend,
			 intel_eth_pci_resume);

#define PCI_DEVICE_ID_INTEL_QUARK_ID			0x0937
#define PCI_DEVICE_ID_INTEL_EHL_RGMII1G_ID		0x4b30
#define PCI_DEVICE_ID_INTEL_EHL_SGMII1G_ID		0x4b31
#define PCI_DEVICE_ID_INTEL_EHL_SGMII2G5_ID		0x4b32
/* Intel(R) Programmable Services Engine (Intel(R) PSE) consist of 2 MAC
 * which are named PSE0 and PSE1
 */
#define PCI_DEVICE_ID_INTEL_EHL_PSE0_RGMII1G_ID		0x4ba0
#define PCI_DEVICE_ID_INTEL_EHL_PSE0_SGMII1G_ID		0x4ba1
#define PCI_DEVICE_ID_INTEL_EHL_PSE0_SGMII2G5_ID	0x4ba2
#define PCI_DEVICE_ID_INTEL_EHL_PSE1_RGMII1G_ID		0x4bb0
#define PCI_DEVICE_ID_INTEL_EHL_PSE1_SGMII1G_ID		0x4bb1
#define PCI_DEVICE_ID_INTEL_EHL_PSE1_SGMII2G5_ID	0x4bb2
#define PCI_DEVICE_ID_INTEL_TGL_SGMII1G_ID		0xa0ac

static const struct pci_device_id intel_eth_pci_id_table[] = {
	{ PCI_DEVICE_DATA(INTEL, QUARK_ID, &quark_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_RGMII1G_ID, &ehl_rgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_SGMII1G_ID, &ehl_sgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_SGMII2G5_ID, &ehl_sgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_PSE0_RGMII1G_ID,
			  &ehl_pse0_rgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_PSE0_SGMII1G_ID,
			  &ehl_pse0_sgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_PSE0_SGMII2G5_ID,
			  &ehl_pse0_sgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_PSE1_RGMII1G_ID,
			  &ehl_pse1_rgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_PSE1_SGMII1G_ID,
			  &ehl_pse1_sgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, EHL_PSE1_SGMII2G5_ID,
			  &ehl_pse1_sgmii1g_pci_info) },
	{ PCI_DEVICE_DATA(INTEL, TGL_SGMII1G_ID, &tgl_sgmii1g_pci_info) },
	{}
};

MODULE_DEVICE_TABLE(pci, intel_eth_pci_id_table);

static struct pci_driver intel_eth_pci_driver = {
	.name = "intel-eth-pci",
	.id_table = intel_eth_pci_id_table,
	.probe = intel_eth_pci_probe,
	.remove = intel_eth_pci_remove,
	.driver         = {
		.pm     = &intel_eth_pm_ops,
	},
};

module_pci_driver(intel_eth_pci_driver);

MODULE_DESCRIPTION("INTEL 10/100/1000 Ethernet PCI driver");
MODULE_AUTHOR("Voon Weifeng <weifeng.voon@intel.com>");
MODULE_LICENSE("GPL v2");
