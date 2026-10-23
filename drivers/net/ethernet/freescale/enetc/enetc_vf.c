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

/*
 * Returns: 0 on success, a negative errno on failure, or the positive IP
 * minor revision for an ENETC_MSG_GET_IP_MN request.
 */
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
		case ENETC_MSG_CLASS_ID_MAC_FILTER:
			if (FIELD_GET(ENETC_PF_MSG_CLASS_CODE, pf_msg) ==
			    ENETC_MF_CLASS_CODE_UCF_DENY)
				return -EACCES;
			fallthrough;
		case ENETC_MSG_CLASS_ID_INVALID_MSG_LEN:
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

static int enetc_msg_link_status_notifier(struct enetc_si *si, bool reg)
{
	struct device *dev = &si->pdev->dev;
	struct enetc_msg_swbd msg_swbd;
	u8 cmd_id;

	msg_swbd.size = ALIGN(sizeof(struct enetc_msg_generic),
			      ENETC_MSG_ALIGN);
	msg_swbd.vaddr = dma_alloc_coherent(dev, msg_swbd.size,
					    &msg_swbd.dma, GFP_KERNEL);
	if (!msg_swbd.vaddr)
		return -ENOMEM;

	cmd_id = reg ? ENETC_MSG_REGISTER_LINK_CHANGE_NOTIFIER :
		       ENETC_MSG_UNREGISTER_LINK_CHANGE_NOTIFIER;
	enetc_msg_fill_common_hdr(&msg_swbd, ENETC_MSG_CLASS_ID_LINK_STATUS,
				  cmd_id, 0, 0);

	return enetc_msg_vsi_send(si, &msg_swbd);
}

static int enetc_vf_reg_link_status_notifier(struct enetc_si *si)
{
	return enetc_msg_link_status_notifier(si, true);
}

static int enetc_vf_unreg_link_status_notifier(struct enetc_si *si)
{
	return enetc_msg_link_status_notifier(si, false);
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

static int enetc_vf_set_mac_promisc(struct enetc_si *si, int type,
				    bool promisc, bool flush_macs)
{
	struct enetc_msg_mac_promisc_mode *msg;
	struct device *dev = &si->pdev->dev;
	struct enetc_msg_swbd msg_swbd;

	if (!(type & ENETC_MAC_FILTER_TYPE_ALL))
		return -EINVAL;

	msg_swbd.size = ALIGN(sizeof(*msg), ENETC_MSG_ALIGN);
	msg_swbd.vaddr = dma_alloc_coherent(dev, msg_swbd.size,
					    &msg_swbd.dma, GFP_KERNEL);
	if (!msg_swbd.vaddr)
		return -ENOMEM;

	msg = (struct enetc_msg_mac_promisc_mode *)msg_swbd.vaddr;
	msg->config = FIELD_PREP(ENETC_MSG_MAC_TYPE,
				 type & ENETC_MAC_FILTER_TYPE_ALL);
	msg->config |= FIELD_PREP(ENETC_MSG_MAC_PROMISC_MODE, promisc);
	msg->config |= FIELD_PREP(ENETC_MSG_MAC_FLUSH_MACS, flush_macs);
	enetc_msg_fill_common_hdr(&msg_swbd, ENETC_MSG_CLASS_ID_MAC_FILTER,
				  ENETC_MSG_SET_MAC_PROMISC_MODE, 0, 0);

	return enetc_msg_vsi_send(si, &msg_swbd);
}

static int enetc_vf_set_mac_hash_filter(struct enetc_si *si,
					struct netdev_hw_addr_list *uc,
					struct netdev_hw_addr_list *mc)
{
	struct enetc_msg_mac_hash_filter *msg;
	struct enetc_mac_filter *mac_filter;
	struct device *dev = &si->pdev->dev;
	struct enetc_msg_swbd msg_swbd;
	struct netdev_hw_addr *ha;
	int mac_filter_type = 0;
	u32 tbl_cnt = 0;
	u32 msg_size;
	int i = 0;

	if (!uc && !mc)
		return 0;

	if (uc) {
		tbl_cnt += 2;
		mac_filter_type |= ENETC_MAC_FILTER_TYPE_UC;
	}

	if (mc) {
		tbl_cnt += 2;
		mac_filter_type |= ENETC_MAC_FILTER_TYPE_MC;
	}

	msg_size = struct_size(msg, hash_tbl, tbl_cnt);
	msg_swbd.size = ALIGN(msg_size, ENETC_MSG_ALIGN);
	msg_swbd.vaddr = dma_alloc_coherent(dev, msg_swbd.size,
					    &msg_swbd.dma, GFP_KERNEL);
	if (!msg_swbd.vaddr)
		return -ENOMEM;

	msg = (struct enetc_msg_mac_hash_filter *)msg_swbd.vaddr;
	msg->sz_type = FIELD_PREP(ENETC_MSG_MAC_TYPE, mac_filter_type);
	msg->sz_type |= FIELD_PREP(ENETC_MSG_MAC_HASH_SIZE,
				   ENETC_MAC_HASH_TABLE_SIZE_64);

	if (uc) {
		mac_filter = &si->mac_filter[UC];
		enetc_reset_mac_addr_filter(mac_filter);
		netdev_hw_addr_list_for_each(ha, uc)
			enetc_add_mac_addr_ht_filter(mac_filter, ha->addr);

		bitmap_to_arr32(&msg->hash_tbl[i], mac_filter->mac_hash_table,
				ENETC_MADDR_HASH_TBL_SZ);
		i += 2;
	}

	if (mc) {
		mac_filter = &si->mac_filter[MC];
		enetc_reset_mac_addr_filter(mac_filter);
		netdev_hw_addr_list_for_each(ha, mc)
			enetc_add_mac_addr_ht_filter(mac_filter, ha->addr);

		bitmap_to_arr32(&msg->hash_tbl[i], mac_filter->mac_hash_table,
				ENETC_MADDR_HASH_TBL_SZ);
	}

	enetc_msg_fill_common_hdr(&msg_swbd, ENETC_MSG_CLASS_ID_MAC_FILTER,
				  ENETC_MSG_SET_MAC_HASH_TABLE, 0, 0);

	return enetc_msg_vsi_send(si, &msg_swbd);
}

static int enetc_vf_enable_iff_promisc(struct enetc_si *si)
{
	int err;

	err = enetc_vf_set_mac_promisc(si, ENETC_MAC_FILTER_TYPE_ALL,
				       true, true);
	if (err)
		dev_err(&si->pdev->dev,
			"Failed to enable promiscuous mode, err: %pe\n",
			ERR_PTR(err));

	return err;
}

static int enetc_vf_disable_iff_promisc(struct enetc_si *si,
					struct netdev_hw_addr_list *uc,
					struct netdev_hw_addr_list *mc)
{
	int err;

	err = enetc_vf_set_mac_hash_filter(si, uc, mc);
	if (err) {
		dev_err_once(&si->pdev->dev,
			     "Failed to set MAC hash filters, err: %pe\n",
			     ERR_PTR(err));
		return err;
	}

	err = enetc_vf_set_mac_promisc(si, ENETC_MAC_FILTER_TYPE_ALL,
				       false, false);
	if (err)
		dev_err_once(&si->pdev->dev,
			     "Failed to disable promiscuous mode, err: %pe\n",
			     ERR_PTR(err));

	return err;
}

static int enetc_vf_enable_iff_allmulti(struct enetc_si *si,
					struct netdev_hw_addr_list *uc)
{
	int err;

	err = enetc_vf_set_mac_promisc(si, ENETC_MAC_FILTER_TYPE_MC,
				       true, true);
	if (err) {
		dev_err(&si->pdev->dev,
			"Failed to enable multicast promiscuous mode, err: %pe\n",
			ERR_PTR(err));
		return err;
	}

	err = enetc_vf_set_mac_hash_filter(si, uc, NULL);
	if (err) {
		dev_err(&si->pdev->dev,
			"Failed to set unicast filter, err: %pe\n",
		ERR_PTR(err));
		return err;
	}

	err = enetc_vf_set_mac_promisc(si, ENETC_MAC_FILTER_TYPE_UC,
				       false, false);
	if (err)
		dev_err(&si->pdev->dev,
			"Failed to disable unicast promiscuous mode, err: %pe\n",
		ERR_PTR(err));

	return err;
}

static int enetc_vf_set_rx_mode(struct net_device *ndev,
				struct netdev_hw_addr_list *uc,
				struct netdev_hw_addr_list *mc)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	int err;

	if (ndev->flags & IFF_PROMISC)
		err = enetc_vf_enable_iff_promisc(si);
	else if (ndev->flags & IFF_ALLMULTI)
		err = enetc_vf_enable_iff_allmulti(si, uc);
	else
		err = enetc_vf_disable_iff_promisc(si, uc, mc);

	/* If the error code is -EOPNOTSUPP or -EACCES or -EPERM, return 0
	 * directly to avoid meaningless retries.
	 */
	if (err == -EOPNOTSUPP || err == -EACCES || err == -EPERM)
		return 0;

	return err;
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

static const struct net_device_ops enetc4_ndev_ops = {
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
	.ndo_set_rx_mode_async	= enetc_vf_set_rx_mode,
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

	if (!is_enetc_rev1(si))
		ndev->priv_flags |= IFF_UNICAST_FLT;

	if (si->num_rss) {
		ndev->hw_features |= NETIF_F_RXHASH;
		ndev->features |= NETIF_F_RXHASH;
	}

	if (si->drvdata->tx_csum)
		priv->active_offloads |= ENETC_F_TXCSUM;

	if (si->hw_features & ENETC_SI_F_LSO)
		priv->active_offloads |= ENETC_F_LSO;

	/* pick up primary MAC address from SI */
	enetc_load_primary_mac_addr(&si->hw, ndev);
}

static void enetc_vf_enable_mr_int(struct enetc_si *si)
{
	if (is_enetc_rev1(si))
		return;

	enetc_wr(&si->hw, ENETC_VSIIER, VSIIER_MRIE);
}

static void enetc_vf_disable_mr_int(struct enetc_si *si)
{
	if (is_enetc_rev1(si))
		return;

	enetc_wr(&si->hw, ENETC_VSIIER, 0);
}

static void enetc_vf_msg_handle_link_status(struct enetc_si *si, u8 status)
{
	bool tx_pause = !!(status & ENETC_CLASS_CODE_TX_PAUSE_EN);
	bool link_down = !!(status & ENETC_CLASS_CODE_LINK_DOWN);
	struct enetc_ndev_priv *priv = netdev_priv(si->ndev);
	struct net_device *ndev = si->ndev;

	rtnl_lock();
	if (!netif_running(ndev))
		goto unlock_rtnl;

	if (link_down) {
		if (netif_carrier_ok(ndev)) {
			netif_carrier_off(ndev);
			netdev_info(ndev, "Link is Down\n");
		}

		goto unlock_rtnl;
	}

	/* Link is up */
	enetc_set_congestion_mode(priv, tx_pause);

	if (!netif_carrier_ok(ndev)) {
		netif_carrier_on(ndev);
		netdev_info(ndev, "Link is Up, tx pause %s\n",
			    tx_pause ? "on" : "off");
	}

unlock_rtnl:
	rtnl_unlock();
}

static void enetc_vf_msg_task(struct work_struct *work)
{
	struct enetc_si *si = container_of(work, struct enetc_si, msg_task);
	struct enetc_hw *hw = &si->hw;
	u8 class_id, class_code;
	u16 pf_msg;

	/* W1C to clear the message received interrupt event */
	enetc_wr(hw, ENETC_VSIIDR, VSIIDR_MR);

	/* Reading VSIMSGRR retrieves the message data and acknowledges to
	 * the PF that the message was received and another message can be
	 * sent.
	 */
	pf_msg = FIELD_GET(VSIMSGRR_MC, enetc_rd(hw, ENETC_VSIMSGRR));
	class_id = FIELD_GET(ENETC_PF_MSG_CLASS_ID, pf_msg);

	switch (class_id) {
	case ENETC_MSG_CLASS_ID_LINK_STATUS:
		class_code = FIELD_GET(ENETC_PF_MSG_CLASS_CODE_U8, pf_msg);
		enetc_vf_msg_handle_link_status(si, class_code);
		break;
	default:
		dev_err(&si->pdev->dev,
			"Unsupported Message Class ID (0x%02x) from PF\n",
			class_id);
	}

	enetc_vf_enable_mr_int(si);
}

static irqreturn_t enetc_vf_msg_msix_handler(int irq, void *data)
{
	struct enetc_si *si = (struct enetc_si *)data;

	enetc_vf_disable_mr_int(si);
	queue_work(si->workqueue, &si->msg_task);

	return IRQ_HANDLED;
}

static int enetc_vf_register_msg_msix(struct enetc_si *si)
{
	int irq, err;

	if (is_enetc_rev1(si))
		return 0;

	snprintf(si->msg_int_name, sizeof(si->msg_int_name), "%s-pfmsg",
		 pci_name(si->pdev));
	irq = pci_irq_vector(si->pdev, ENETC_SI_INT_IDX);
	err = request_irq(irq, enetc_vf_msg_msix_handler, 0,
			  si->msg_int_name, si);
	if (err) {
		dev_err(&si->pdev->dev,
			"VF messaging: request_irq() failed!\n");
		return err;
	}

	/* set one IRQ entry for PSI-to-VSI messaging */
	enetc_wr(&si->hw, ENETC_SIMSIVR, ENETC_SI_INT_IDX);

	return 0;
}

static void enetc_vf_free_msg_msix(struct enetc_si *si)
{
	if (is_enetc_rev1(si))
		return;

	free_irq(pci_irq_vector(si->pdev, ENETC_SI_INT_IDX), si);
}

static const struct enetc_si_ops enetc_vsi_ops = {
	.get_rss_table = enetc_get_rss_table,
	.set_rss_table = enetc_set_rss_table,
	.setup_cbdr = enetc_setup_cbdr,
	.teardown_cbdr = enetc_teardown_cbdr,
};

static const struct enetc_si_ops enetc4_vsi_ops = {
	.get_rss_table = enetc4_get_rss_table,
	.set_rss_table = enetc4_set_rss_table,
	.setup_cbdr = enetc4_setup_cbdr,
	.teardown_cbdr = enetc4_teardown_cbdr,
	.vf_reg_link_status_notifier = enetc_vf_reg_link_status_notifier,
	.vf_unreg_link_status_notifier = enetc_vf_unreg_link_status_notifier,
};

static int enetc_vf_wq_task_init(struct enetc_si *si)
{
	if (is_enetc_rev1(si))
		return 0;

	si->workqueue = alloc_ordered_workqueue("enetc-%s-wq", WQ_MEM_RECLAIM,
						pci_name(si->pdev));
	if (!si->workqueue)
		return -ENOMEM;

	INIT_WORK(&si->msg_task, enetc_vf_msg_task);

	return 0;
}

static void enetc_vf_wq_task_destroy(struct enetc_si *si)
{
	if (!si->workqueue)
		return;

	disable_work_sync(&si->msg_task);

	/* Disable the MR interrupt */
	enetc_vf_disable_mr_int(si);
	enetc_wr(&si->hw, ENETC_VSIIDR, VSIIDR_MR);
	/* Reading VSIMSGRR to clear the MS bit on the PF's side */
	enetc_rd(&si->hw, ENETC_VSIMSGRR);

	destroy_workqueue(si->workqueue);
}

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
	if (is_enetc_rev1(si))
		si->ops = &enetc_vsi_ops;
	else
		si->ops = &enetc4_vsi_ops;

	err = enetc_get_driver_data(si);
	if (err) {
		dev_err_probe(&pdev->dev, err,
			      "Could not get VF driver data\n");
		goto err_get_driver_data;
	}

	enetc_get_si_caps(si);

	/* Currently, the supported SoCs have a max of 6 CPUs and the VFs
	 * have less than 6 RX/TX rings. So no issues for these supported
	 * SoCs, but for future SoCs which have more CPUs or more TX/RX
	 * rings, all the related logic needs to be improved.
	 */
	ndev = alloc_etherdev_mqs(sizeof(*priv),
				  min(si->num_tx_rings, ENETC_MAX_NUM_TXQS),
				  min(si->num_rx_rings, ENETC_MAX_NUM_TXQS));
	if (!ndev) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "netdev creation failed\n");
		goto err_alloc_netdev;
	}

	if (is_enetc_rev1(si))
		enetc_vf_netdev_setup(si, ndev, &enetc_ndev_ops);
	else
		enetc_vf_netdev_setup(si, ndev, &enetc4_ndev_ops);

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

	err = enetc_vf_wq_task_init(si);
	if (err) {
		dev_err(&pdev->dev, "Failed to init workqueue\n");
		goto err_wq_init;
	}

	err = enetc_vf_register_msg_msix(si);
	if (err) {
		dev_err(&pdev->dev, "Failed to register msg irq\n");
		goto err_register_msg_msix;
	}

	netif_carrier_off(ndev);
	/* Enable message received interrupt */
	enetc_vf_enable_mr_int(si);
	err = register_netdev(ndev);
	if (err)
		goto err_reg_netdev;

	return 0;

err_reg_netdev:
	enetc_vf_free_msg_msix(si);
err_register_msg_msix:
	enetc_vf_wq_task_destroy(si);
err_wq_init:
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

	/* Disable promiscuous mode and clear MAC filters */
	if (!is_enetc_rev1(si))
		enetc_vf_set_mac_promisc(si, ENETC_MAC_FILTER_TYPE_ALL,
					 false, true);

	enetc_vf_free_msg_msix(si);
	enetc_vf_wq_task_destroy(si);
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
	{ PCI_DEVICE(NXP_ENETC_VENDOR_ID, NXP_ENETC_VF_DEV_ID) },
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
