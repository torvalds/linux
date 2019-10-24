// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include <linux/module.h>
#include "enetc.h"

#define ENETC_DRV_VER_MAJ 1
#define ENETC_DRV_VER_MIN 0

#define ENETC_DRV_VER_STR __stringify(ENETC_DRV_VER_MAJ) "." \
			  __stringify(ENETC_DRV_VER_MIN)
static const char enetc_drv_ver[] = ENETC_DRV_VER_STR;
#define ENETC_DRV_NAME_STR "ENETC VF driver"
static const char enetc_drv_name[] = ENETC_DRV_NAME_STR;

/* Messaging */
static void enetc_msg_vsi_write_msg(struct enetc_hw *hw,
				    struct enetc_msg_swbd *msg)
{
	u32 val;

	val = enetc_vsi_set_msize(msg->size) | lower_32_bits(msg->dma);
	enetc_wr(hw, ENETC_VSIMSGSNDAR1, upper_32_bits(msg->dma));
	enetc_wr(hw, ENETC_VSIMSGSNDAR0, val);
}

static int enetc_msg_vsi_send(struct enetc_si *si, struct enetc_msg_swbd *msg)
{
	int timeout = 100;
	u32 vsimsgsr;

	enetc_msg_vsi_write_msg(&si->hw, msg);

	do {
		vsimsgsr = enetc_rd(&si->hw, ENETC_VSIMSGSR);
		if (!(vsimsgsr & ENETC_VSIMSGSR_MB))
			break;

		usleep_range(1000, 2000);
	} while (--timeout);

	if (!timeout)
		return -ETIMEDOUT;

	/* check for message delivery error */
	if (vsimsgsr & ENETC_VSIMSGSR_MS) {
		dev_err(&si->pdev->dev, "VSI command execute error: %d\n",
			ENETC_SIMSGSR_GET_MC(vsimsgsr));
		return -EIO;
	}

	return 0;
}

static int enetc_msg_vsi_set_primary_mac_addr(struct enetc_ndev_priv *priv,
					      struct sockaddr *saddr)
{
	struct enetc_msg_cmd_set_primary_mac *cmd;
	struct enetc_msg_swbd msg;
	int err;

	msg.size = ALIGN(sizeof(struct enetc_msg_cmd_set_primary_mac), 64);
	msg.vaddr = dma_alloc_coherent(priv->dev, msg.size, &msg.dma,
				       GFP_KERNEL);
	if (!msg.vaddr) {
		dev_err(priv->dev, "Failed to alloc Tx msg (size: %d)\n",
			msg.size);
		return -ENOMEM;
	}

	cmd = (struct enetc_msg_cmd_set_primary_mac *)msg.vaddr;
	cmd->header.type = ENETC_MSG_CMD_MNG_MAC;
	cmd->header.id = ENETC_MSG_CMD_MNG_ADD;
	memcpy(&cmd->mac, saddr, sizeof(struct sockaddr));

	/* send the command and wait */
	err = enetc_msg_vsi_send(priv->si, &msg);

	dma_free_coherent(priv->dev, msg.size, msg.vaddr, msg.dma);

	return err;
}

static int enetc_vf_set_mac_addr(struct net_device *ndev, void *addr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct sockaddr *saddr = addr;
	int err;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	err = enetc_msg_vsi_set_primary_mac_addr(priv, saddr);
	if (err)
		return err;

	return 0;
}

static int enetc_vf_set_features(struct net_device *ndev,
				 netdev_features_t features)
{
	return enetc_set_features(ndev, features);
}

/* Probing/ Init */
static const struct net_device_ops enetc_ndev_ops = {
	.ndo_open		= enetc_open,
	.ndo_stop		= enetc_close,
	.ndo_start_xmit		= enetc_xmit,
	.ndo_get_stats		= enetc_get_stats,
	.ndo_set_mac_address	= enetc_vf_set_mac_addr,
	.ndo_set_features	= enetc_vf_set_features,
	.ndo_do_ioctl		= enetc_ioctl,
	.ndo_setup_tc		= enetc_setup_tc,
};

static void enetc_vf_netdev_setup(struct enetc_si *si, struct net_device *ndev,
				  const struct net_device_ops *ndev_ops)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	SET_NETDEV_DEV(ndev, &si->pdev->dev);
	priv->ndev = ndev;
	priv->si = si;
	priv->dev = &si->pdev->dev;
	si->ndev = ndev;

	priv->msg_enable = (NETIF_MSG_IFUP << 1) - 1;
	ndev->netdev_ops = ndev_ops;
	enetc_set_ethtool_ops(ndev);
	ndev->watchdog_timeo = 5 * HZ;
	ndev->max_mtu = ENETC_MAX_MTU;

	ndev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM | NETIF_F_HW_CSUM |
			    NETIF_F_HW_VLAN_CTAG_TX |
			    NETIF_F_HW_VLAN_CTAG_RX;
	ndev->features = NETIF_F_HIGHDMA | NETIF_F_SG |
			 NETIF_F_RXCSUM | NETIF_F_HW_CSUM |
			 NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX;

	if (si->num_rss)
		ndev->hw_features |= NETIF_F_RXHASH;

	if (si->errata & ENETC_ERR_TXCSUM) {
		ndev->hw_features &= ~NETIF_F_HW_CSUM;
		ndev->features &= ~NETIF_F_HW_CSUM;
	}

	/* pick up primary MAC address from SI */
	enetc_get_primary_mac_addr(&si->hw, ndev->dev_addr);
}

static int enetc_vf_probe(struct pci_dev *pdev,
			  const struct pci_device_id *ent)
{
	struct enetc_ndev_priv *priv;
	struct net_device *ndev;
	struct enetc_si *si;
	int err;

	err = enetc_pci_probe(pdev, KBUILD_MODNAME, 0);
	if (err) {
		dev_err(&pdev->dev, "PCI probing failed\n");
		return err;
	}

	si = pci_get_drvdata(pdev);

	enetc_get_si_caps(si);

	ndev = alloc_etherdev_mq(sizeof(*priv), ENETC_MAX_NUM_TXQS);
	if (!ndev) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "netdev creation failed\n");
		goto err_alloc_netdev;
	}

	enetc_vf_netdev_setup(si, ndev, &enetc_ndev_ops);

	priv = netdev_priv(ndev);

	enetc_init_si_rings_params(priv);

	err = enetc_alloc_si_resources(priv);
	if (err) {
		dev_err(&pdev->dev, "SI resource alloc failed\n");
		goto err_alloc_si_res;
	}

	err = enetc_alloc_msix(priv);
	if (err) {
		dev_err(&pdev->dev, "MSIX alloc failed\n");
		goto err_alloc_msix;
	}

	err = register_netdev(ndev);
	if (err)
		goto err_reg_netdev;

	netif_carrier_off(ndev);

	netif_info(priv, probe, ndev, "%s v%s\n",
		   enetc_drv_name, enetc_drv_ver);

	return 0;

err_reg_netdev:
	enetc_free_msix(priv);
err_alloc_msix:
	enetc_free_si_resources(priv);
err_alloc_si_res:
	si->ndev = NULL;
	free_netdev(ndev);
err_alloc_netdev:
	enetc_pci_remove(pdev);

	return err;
}

static void enetc_vf_remove(struct pci_dev *pdev)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_ndev_priv *priv;

	priv = netdev_priv(si->ndev);
	netif_info(priv, drv, si->ndev, "%s v%s remove\n",
		   enetc_drv_name, enetc_drv_ver);
	unregister_netdev(si->ndev);

	enetc_free_msix(priv);

	enetc_free_si_resources(priv);

	free_netdev(si->ndev);

	enetc_pci_remove(pdev);
}

static const struct pci_device_id enetc_vf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, ENETC_DEV_ID_VF) },
	{ 0, } /* End of table. */
};
MODULE_DEVICE_TABLE(pci, enetc_vf_id_table);

static struct pci_driver enetc_vf_driver = {
	.name = KBUILD_MODNAME,
	.id_table = enetc_vf_id_table,
	.probe = enetc_vf_probe,
	.remove = enetc_vf_remove,
};
module_pci_driver(enetc_vf_driver);

MODULE_DESCRIPTION(ENETC_DRV_NAME_STR);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(ENETC_DRV_VER_STR);
