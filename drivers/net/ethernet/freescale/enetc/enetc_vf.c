// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include <linux/iopoll.h>
#include <linux/module.h>
#include "enetc.h"

#define ENETC_DRV_NAME_STR "ENETC VF driver"

/* Note: This function should be called after filling the message body,
 * because the CRC16 needs to be calculated after all the data has been
 * filled.
 */
static void enetc_msg_fill_common_hdr(struct enetc_msg_swbd *msg_swbd,
				      u8 class_id, u8 cmd_id, u8 proto_ver,
				      u8 cookie)
{
	struct enetc_msg_header *hdr = msg_swbd->vaddr;
	u8 *data_buf = ((u8 *)msg_swbd->vaddr) + 2; /* skip crc16 field */
	u32 data_size = msg_swbd->size - 2;
	u16 crc16;

	hdr->class_id = class_id;
	hdr->cmd_id = cmd_id;
	hdr->len = ENETC_MSG_EXT_BODY_LEN(msg_swbd->size);
	hdr->proto_ver = proto_ver;
	hdr->cookie = FIELD_PREP(ENETC_VF_MSG_COOKIE, cookie);

	crc16 = crc_itu_t(ENETC_CRC_INIT, data_buf, data_size);
	hdr->crc16 = htons(crc16);
}

static void enetc_msg_vsi_write_msg(struct enetc_hw *hw,
				    struct enetc_msg_swbd *msg)
{
	u32 val;

	val = enetc_vsi_set_msize(msg->size) | lower_32_bits(msg->dma);
	enetc_wr(hw, ENETC_VSIMSGSNDAR1, upper_32_bits(msg->dma));
	enetc_wr(hw, ENETC_VSIMSGSNDAR0, val);
}

static void enetc_msg_dma_free(struct device *dev, struct enetc_msg_swbd *msg)
{
	if (msg->vaddr) {
		dma_free_coherent(dev, msg->size, msg->vaddr, msg->dma);
		msg->vaddr = NULL;
	}
}

static int enetc_msg_vsi_send(struct enetc_si *si, struct enetc_msg_swbd *msg)
{
	struct device *dev = &si->pdev->dev;
	u32 vsimsgsr;
	u16 pf_msg;
	int err;

	/* The VSI mailbox may be busy if last message was not yet processed
	 * by PSI. So need to check the mailbox status before sending.
	 */
	vsimsgsr = enetc_rd(&si->hw, ENETC_VSIMSGSR);
	if (vsimsgsr & ENETC_VSIMSGSR_MB) {
		/* It is safe to free the DMA buffer here, the caller does
		 * not access the DMA buffer if enetc_msg_vsi_send() fails.
		 */
		enetc_msg_dma_free(dev, msg);
		dev_err(dev, "VSI mailbox is busy\n");
		return -EIO;
	}

	/* Free the DMA buffer of the last message */
	enetc_msg_dma_free(dev, &si->msg);
	si->msg = *msg;
	enetc_msg_vsi_write_msg(&si->hw, msg);
	err = read_poll_timeout(enetc_rd, vsimsgsr,
				!(vsimsgsr & ENETC_VSIMSGSR_MB),
				1000, 200000, false, &si->hw, ENETC_VSIMSGSR);
	if (err) {
		dev_err(dev, "VSI mailbox timeout\n");

		return err;
	}

	/* check for message delivery error */
	if (vsimsgsr & ENETC_VSIMSGSR_MS) {
		dev_err(dev, "Transfer error when copying the data\n");
		return -EIO;
	}

	pf_msg = ENETC_SIMSGSR_GET_MC(vsimsgsr);
	/* Check the user-defined completion status. */
	if (FIELD_GET(ENETC_PF_MSG_CLASS_ID, pf_msg) !=
	    ENETC_MSG_CLASS_ID_CMD_SUCCESS) {
		switch (FIELD_GET(ENETC_PF_MSG_CLASS_ID, pf_msg)) {
		case ENETC_MSG_CLASS_ID_PERMISSION_DENY:
			/* Intentionally returning early to prevent excessive
			 * error logs due to permission issues.
			 */
			return -EACCES;
		case ENETC_MSG_CLASS_ID_CMD_NOT_SUPPORT:
		case ENETC_MSG_CLASS_ID_PROTO_NOT_SUPPORT:
			err = -EOPNOTSUPP;
			break;
		case ENETC_MSG_CLASS_ID_PSI_BUSY:
			err = -EBUSY;
			break;
		case ENETC_MSG_CLASS_ID_CMD_TIMEOUT:
			err = -ETIME;
			break;
		case ENETC_MSG_CLASS_ID_INVALID_MSG_LEN:
		case ENETC_MSG_CLASS_ID_MAC_FILTER:
			err = -EINVAL;
			break;
		case ENETC_MSG_CLASS_ID_CMD_NOT_PERMITTED:
			err = -EPERM;
			break;
		case ENETC_MSG_CLASS_ID_IP_REVISION:
			err = FIELD_GET(ENETC_PF_MSG_CLASS_CODE_U8, pf_msg);
			break;
		case ENETC_MSG_CLASS_ID_CMD_FAIL:
		case ENETC_MSG_CLASS_ID_CRC_ERROR:
		case ENETC_MSG_CLASS_ID_CMD_DEFERRED:
		default:
			err = -EIO;
		}
	}

	if (err < 0)
		dev_err(dev, "Return error code from PSI: 0x%04x\n", pf_msg);

	return err;
}

static int enetc_msg_vsi_set_primary_mac_addr(struct enetc_ndev_priv *priv,
					      struct sockaddr *saddr)
{
	struct enetc_msg_mac_exact_filter *msg;
	struct enetc_msg_swbd msg_swbd;
	u32 msg_size;

	msg_size = struct_size(msg, mac, 1);
	msg_swbd.size = ALIGN(msg_size, ENETC_MSG_ALIGN);
	msg_swbd.vaddr = dma_alloc_coherent(priv->dev, msg_swbd.size,
					    &msg_swbd.dma, GFP_KERNEL);
	if (!msg_swbd.vaddr)
		return -ENOMEM;

	msg = (struct enetc_msg_mac_exact_filter *)msg_swbd.vaddr;
	memcpy(&msg->mac[0].addr, saddr->sa_data, ETH_ALEN);
	enetc_msg_fill_common_hdr(&msg_swbd, ENETC_MSG_CLASS_ID_MAC_FILTER,
				  ENETC_MSG_SET_PRIMARY_MAC, 0, 0);

	/* send the command and wait */
	return enetc_msg_vsi_send(priv->si, &msg_swbd);
}

static int enetc_vf_get_ip_minor_revision(struct enetc_si *si)
{
	struct device *dev = &si->pdev->dev;
	struct enetc_msg_swbd msg_swbd;

	msg_swbd.size = ALIGN(sizeof(struct enetc_msg_generic),
			      ENETC_MSG_ALIGN);
	msg_swbd.vaddr = dma_alloc_coherent(dev, msg_swbd.size,
					    &msg_swbd.dma, GFP_KERNEL);
	if (!msg_swbd.vaddr)
		return -ENOMEM;

	enetc_msg_fill_common_hdr(&msg_swbd, ENETC_MSG_CLASS_ID_IP_REVISION,
				  ENETC_MSG_GET_IP_MN, 0, 0);

	return enetc_msg_vsi_send(si, &msg_swbd);
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

	eth_hw_addr_set(ndev, saddr->sa_data);

	return 0;
}

static int enetc_vf_set_features(struct net_device *ndev,
				 netdev_features_t features)
{
	enetc_set_features(ndev, features);

	return 0;
}

static int enetc_vf_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			     void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return enetc_setup_tc_mqprio(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

/* Probing/ Init */
static const struct net_device_ops enetc_ndev_ops = {
	.ndo_open		= enetc_open,
	.ndo_stop		= enetc_close,
	.ndo_start_xmit		= enetc_xmit,
	.ndo_get_stats		= enetc_get_stats,
	.ndo_set_mac_address	= enetc_vf_set_mac_addr,
	.ndo_set_features	= enetc_vf_set_features,
	.ndo_eth_ioctl		= enetc_ioctl,
	.ndo_setup_tc		= enetc_vf_setup_tc,
	.ndo_hwtstamp_get	= enetc_hwtstamp_get,
	.ndo_hwtstamp_set	= enetc_hwtstamp_set,
};

static void enetc_vf_get_revision(struct enetc_si *si)
{
	int ip_mn;

	if (is_enetc_rev1(si)) {
		si->revision = ENETC_REV_1_0;
		return;
	}

	ip_mn = enetc_vf_get_ip_minor_revision(si);
	if (ip_mn >= 0) {
		si->revision = (si->pdev->revision << 8) | ip_mn;
		return;
	}

	si->revision = ENETC_REV_4_1;
	dev_info(&si->pdev->dev,
		 "Failed to get revision, use compatible revision: 0x%04x\n",
		 si->revision);
}

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
	priv->sysclk_freq = si->drvdata->sysclk_freq;
	priv->max_frags = si->drvdata->max_frags;
	ndev->netdev_ops = ndev_ops;
	enetc_set_ethtool_ops(ndev);
	ndev->watchdog_timeo = 5 * HZ;
	ndev->max_mtu = ENETC_MAX_MTU;

	ndev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM |
			    NETIF_F_HW_VLAN_CTAG_TX |
			    NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_CSUM | NETIF_F_TSO | NETIF_F_TSO6 |
			    NETIF_F_GSO_UDP_L4;
	ndev->features = NETIF_F_HIGHDMA | NETIF_F_SG | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX |
			 NETIF_F_HW_CSUM | NETIF_F_TSO | NETIF_F_TSO6 |
			 NETIF_F_GSO_UDP_L4;
	ndev->vlan_features = NETIF_F_SG | NETIF_F_HW_CSUM |
			      NETIF_F_TSO | NETIF_F_TSO6;

	if (si->num_rss) {
		ndev->hw_features |= NETIF_F_RXHASH;
		ndev->features |= NETIF_F_RXHASH;
	}

	/* pick up primary MAC address from SI */
	enetc_load_primary_mac_addr(&si->hw, ndev);
}

static const struct enetc_si_ops enetc_vsi_ops = {
	.get_rss_table = enetc_get_rss_table,
	.set_rss_table = enetc_set_rss_table,
	.setup_cbdr = enetc_setup_cbdr,
	.teardown_cbdr = enetc_teardown_cbdr,
};

static int enetc_vf_probe(struct pci_dev *pdev,
			  const struct pci_device_id *ent)
{
	struct enetc_ndev_priv *priv;
	struct enetc_msg_swbd msg;
	struct net_device *ndev;
	struct enetc_si *si;
	int err;

	err = enetc_pci_probe(pdev, KBUILD_MODNAME, 0);
	if (err)
		return dev_err_probe(&pdev->dev, err, "PCI probing failed\n");

	si = pci_get_drvdata(pdev);
	enetc_vf_get_revision(si);
	si->ops = &enetc_vsi_ops;
	err = enetc_get_driver_data(si);
	if (err) {
		dev_err_probe(&pdev->dev, err,
			      "Could not get VF driver data\n");
		goto err_get_driver_data;
	}

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

	err = si->ops->setup_cbdr(si);
	if (err)
		goto err_setup_cbdr;

	err = enetc_alloc_si_resources(priv);
	if (err) {
		dev_err(&pdev->dev, "SI resource alloc failed\n");
		goto err_alloc_si_res;
	}

	err = enetc_configure_si(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to configure SI\n");
		goto err_config_si;
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

	return 0;

err_reg_netdev:
	enetc_free_msix(priv);
err_config_si:
err_alloc_msix:
	enetc_free_si_resources(priv);
err_alloc_si_res:
	si->ops->teardown_cbdr(si);
err_setup_cbdr:
	si->ndev = NULL;
	free_netdev(ndev);
err_alloc_netdev:
err_get_driver_data:
	msg = si->msg;
	enetc_pci_remove(pdev);
	enetc_msg_dma_free(&pdev->dev, &msg);

	return err;
}

static void enetc_vf_remove(struct pci_dev *pdev)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_ndev_priv *priv;
	struct enetc_msg_swbd msg;

	priv = netdev_priv(si->ndev);
	unregister_netdev(si->ndev);

	enetc_free_msix(priv);

	enetc_free_si_resources(priv);
	si->ops->teardown_cbdr(si);

	free_netdev(si->ndev);

	msg = si->msg;
	enetc_pci_remove(pdev);
	enetc_msg_dma_free(&pdev->dev, &msg);
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
