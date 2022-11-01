// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Loongson Corporation
 */

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/of_irq.h>
#include "stmmac.h"

static int loongson_default_data(struct plat_stmmacenet_data *plat)
{
	plat->clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

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

	/* Default to phy auto-detection */
	plat->phy_addr = -1;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	plat->multicast_filter_bins = 256;
	return 0;
}

static int loongson_dwmac_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res;
	struct device_node *np;
	int ret, i, phy_mode;

	np = dev_of_node(&pdev->dev);

	if (!np) {
		pr_info("dwmac_loongson_pci: No OF node\n");
		return -ENODEV;
	}

	if (!of_device_is_compatible(np, "loongson, pci-gmac")) {
		pr_info("dwmac_loongson_pci: Incompatible OF node\n");
		return -ENODEV;
	}

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->mdio_node = of_get_child_by_name(np, "mdio");
	if (plat->mdio_node) {
		dev_info(&pdev->dev, "Found MDIO subnode\n");

		plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
						   sizeof(*plat->mdio_bus_data),
						   GFP_KERNEL);
		if (!plat->mdio_bus_data)
			return -ENOMEM;
		plat->mdio_bus_data->needs_reset = true;
	}

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg), GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n", __func__);
		return ret;
	}

	/* Get the base address of device */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
		if (ret)
			return ret;
		break;
	}

	plat->bus_id = of_alias_get_id(np, "ethernet");
	if (plat->bus_id < 0)
		plat->bus_id = pci_dev_id(pdev);

	phy_mode = device_get_phy_mode(&pdev->dev);
	if (phy_mode < 0) {
		dev_err(&pdev->dev, "phy_mode not found\n");
		return phy_mode;
	}

	plat->phy_interface = phy_mode;
	plat->interface = PHY_INTERFACE_MODE_GMII;

	pci_set_master(pdev);

	loongson_default_data(plat);
	pci_enable_msi(pdev);
	memset(&res, 0, sizeof(res));
	res.addr = pcim_iomap_table(pdev)[0];

	res.irq = of_irq_get_byname(np, "macirq");
	if (res.irq < 0) {
		dev_err(&pdev->dev, "IRQ macirq not found\n");
		ret = -ENODEV;
	}

	res.wol_irq = of_irq_get_byname(np, "eth_wake_irq");
	if (res.wol_irq < 0) {
		dev_info(&pdev->dev, "IRQ eth_wake_irq not found, using macirq\n");
		res.wol_irq = res.irq;
	}

	res.lpi_irq = of_irq_get_byname(np, "eth_lpi");
	if (res.lpi_irq < 0) {
		dev_err(&pdev->dev, "IRQ eth_lpi not found\n");
		ret = -ENODEV;
	}

	return stmmac_dvr_probe(&pdev->dev, plat, &res);
}

static void loongson_dwmac_remove(struct pci_dev *pdev)
{
	int i;

	stmmac_dvr_remove(&pdev->dev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		pcim_iounmap_regions(pdev, BIT(i));
		break;
	}

	pci_disable_device(pdev);
}

static int __maybe_unused loongson_dwmac_suspend(struct device *dev)
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

static int __maybe_unused loongson_dwmac_resume(struct device *dev)
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

static SIMPLE_DEV_PM_OPS(loongson_dwmac_pm_ops, loongson_dwmac_suspend,
			 loongson_dwmac_resume);

static const struct pci_device_id loongson_dwmac_id_table[] = {
	{ PCI_VDEVICE(LOONGSON, 0x7a03) },
	{}
};
MODULE_DEVICE_TABLE(pci, loongson_dwmac_id_table);

struct pci_driver loongson_dwmac_driver = {
	.name = "dwmac-loongson-pci",
	.id_table = loongson_dwmac_id_table,
	.probe = loongson_dwmac_probe,
	.remove = loongson_dwmac_remove,
	.driver = {
		.pm = &loongson_dwmac_pm_ops,
	},
};

module_pci_driver(loongson_dwmac_driver);

MODULE_DESCRIPTION("Loongson DWMAC PCI driver");
MODULE_AUTHOR("Qing Zhang <zhangqing@loongson.cn>");
MODULE_LICENSE("GPL v2");
