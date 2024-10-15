// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include "hbg_common.h"
#include "hbg_hw.h"
#include "hbg_mdio.h"

static int hbg_init(struct hbg_priv *priv)
{
	int ret;

	ret = hbg_hw_event_notify(priv, HBG_HW_EVENT_INIT);
	if (ret)
		return ret;

	ret = hbg_hw_init(priv);
	if (ret)
		return ret;

	return hbg_mdio_init(priv);
}

static int hbg_pci_init(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hbg_priv *priv = netdev_priv(netdev);
	struct device *dev = &pdev->dev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable PCI device\n");

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "failed to set PCI DMA mask\n");

	ret = pcim_iomap_regions(pdev, BIT(0), dev_driver_string(dev));
	if (ret)
		return dev_err_probe(dev, ret, "failed to map PCI bar space\n");

	priv->io_base = pcim_iomap_table(pdev)[0];
	if (!priv->io_base)
		return dev_err_probe(dev, -ENOMEM, "failed to get io base\n");

	pci_set_master(pdev);
	return 0;
}

static int hbg_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct hbg_priv *priv;
	int ret;

	netdev = devm_alloc_etherdev(dev, sizeof(struct hbg_priv));
	if (!netdev)
		return -ENOMEM;

	pci_set_drvdata(pdev, netdev);
	SET_NETDEV_DEV(netdev, dev);

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->pdev = pdev;

	ret = hbg_pci_init(pdev);
	if (ret)
		return ret;

	ret = hbg_init(priv);
	if (ret)
		return ret;

	netdev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;

	ret = devm_register_netdev(dev, netdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register netdev\n");

	netif_carrier_off(netdev);
	return 0;
}

static const struct pci_device_id hbg_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, 0x3730), 0},
	{ }
};
MODULE_DEVICE_TABLE(pci, hbg_pci_tbl);

static struct pci_driver hbg_driver = {
	.name		= "hibmcge",
	.id_table	= hbg_pci_tbl,
	.probe		= hbg_probe,
};
module_pci_driver(hbg_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("hibmcge driver");
MODULE_VERSION("1.0");
