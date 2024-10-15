// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include "hbg_common.h"
#include "hbg_hw.h"
#include "hbg_irq.h"
#include "hbg_mdio.h"

static void hbg_all_irq_enable(struct hbg_priv *priv, bool enabled)
{
	struct hbg_irq_info *info;
	u32 i;

	for (i = 0; i < priv->vectors.info_array_len; i++) {
		info = &priv->vectors.info_array[i];
		hbg_hw_irq_enable(priv, info->mask, enabled);
	}
}

static int hbg_net_open(struct net_device *netdev)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	hbg_all_irq_enable(priv, true);
	hbg_hw_mac_enable(priv, HBG_STATUS_ENABLE);
	netif_start_queue(netdev);
	hbg_phy_start(priv);

	return 0;
}

static int hbg_net_stop(struct net_device *netdev)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	hbg_phy_stop(priv);
	netif_stop_queue(netdev);
	hbg_hw_mac_enable(priv, HBG_STATUS_DISABLE);
	hbg_all_irq_enable(priv, false);

	return 0;
}

static int hbg_net_set_mac_address(struct net_device *netdev, void *addr)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	u8 *mac_addr;

	mac_addr = ((struct sockaddr *)addr)->sa_data;

	if (!is_valid_ether_addr(mac_addr))
		return -EADDRNOTAVAIL;

	hbg_hw_set_uc_addr(priv, ether_addr_to_u64(mac_addr));
	dev_addr_set(netdev, mac_addr);

	return 0;
}

static void hbg_change_mtu(struct hbg_priv *priv, int new_mtu)
{
	u32 frame_len;

	frame_len = new_mtu + VLAN_HLEN * priv->dev_specs.vlan_layers +
		    ETH_HLEN + ETH_FCS_LEN;
	hbg_hw_set_mtu(priv, frame_len);
}

static int hbg_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	if (netif_running(netdev))
		return -EBUSY;

	hbg_change_mtu(priv, new_mtu);
	WRITE_ONCE(netdev->mtu, new_mtu);

	dev_dbg(&priv->pdev->dev,
		"change mtu from %u to %u\n", netdev->mtu, new_mtu);

	return 0;
}

static const struct net_device_ops hbg_netdev_ops = {
	.ndo_open		= hbg_net_open,
	.ndo_stop		= hbg_net_stop,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= hbg_net_set_mac_address,
	.ndo_change_mtu		= hbg_net_change_mtu,
};

static int hbg_init(struct hbg_priv *priv)
{
	int ret;

	ret = hbg_hw_event_notify(priv, HBG_HW_EVENT_INIT);
	if (ret)
		return ret;

	ret = hbg_hw_init(priv);
	if (ret)
		return ret;

	ret = hbg_irq_init(priv);
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
	netdev->max_mtu = priv->dev_specs.max_mtu;
	netdev->min_mtu = priv->dev_specs.min_mtu;
	netdev->netdev_ops = &hbg_netdev_ops;

	hbg_change_mtu(priv, ETH_DATA_LEN);
	hbg_net_set_mac_address(priv->netdev, &priv->dev_specs.mac_addr);

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
