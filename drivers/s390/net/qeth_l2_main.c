// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2009
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#define KMSG_COMPONENT "qeth"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/hashtable.h>
#include <asm/setup.h>
#include "qeth_core.h"
#include "qeth_l2.h"

static void qeth_bridgeport_query_support(struct qeth_card *card);
static void qeth_bridge_state_change(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd);
static void qeth_bridge_host_event(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd);
static void qeth_l2_vnicc_set_defaults(struct qeth_card *card);
static void qeth_l2_vnicc_init(struct qeth_card *card);
static bool qeth_l2_vnicc_recover_timeout(struct qeth_card *card, u32 vnicc,
					  u32 *timeout);

static int qeth_l2_setdelmac_makerc(struct qeth_card *card, u16 retcode)
{
	int rc;

	if (retcode)
		QETH_CARD_TEXT_(card, 2, "err%04x", retcode);
	switch (retcode) {
	case IPA_RC_SUCCESS:
		rc = 0;
		break;
	case IPA_RC_L2_UNSUPPORTED_CMD:
		rc = -EOPNOTSUPP;
		break;
	case IPA_RC_L2_ADDR_TABLE_FULL:
		rc = -ENOSPC;
		break;
	case IPA_RC_L2_DUP_MAC:
	case IPA_RC_L2_DUP_LAYER3_MAC:
		rc = -EEXIST;
		break;
	case IPA_RC_L2_MAC_NOT_AUTH_BY_HYP:
	case IPA_RC_L2_MAC_NOT_AUTH_BY_ADP:
		rc = -EPERM;
		break;
	case IPA_RC_L2_MAC_NOT_FOUND:
		rc = -ENOENT;
		break;
	default:
		rc = -EIO;
		break;
	}
	return rc;
}

static int qeth_l2_send_setdelmac_cb(struct qeth_card *card,
				     struct qeth_reply *reply,
				     unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;

	return qeth_l2_setdelmac_makerc(card, cmd->hdr.return_code);
}

static int qeth_l2_send_setdelmac(struct qeth_card *card, __u8 *mac,
			   enum qeth_ipa_cmds ipacmd)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "L2sdmac");
	iob = qeth_ipa_alloc_cmd(card, ipacmd, QETH_PROT_IPV4,
				 IPA_DATA_SIZEOF(setdelmac));
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setdelmac.mac_length = ETH_ALEN;
	ether_addr_copy(cmd->data.setdelmac.mac, mac);
	return qeth_send_ipa_cmd(card, iob, qeth_l2_send_setdelmac_cb, NULL);
}

static int qeth_l2_send_setmac(struct qeth_card *card, __u8 *mac)
{
	int rc;

	QETH_CARD_TEXT(card, 2, "L2Setmac");
	rc = qeth_l2_send_setdelmac(card, mac, IPA_CMD_SETVMAC);
	if (rc == 0) {
		dev_info(&card->gdev->dev,
			 "MAC address %pM successfully registered\n", mac);
	} else {
		switch (rc) {
		case -EEXIST:
			dev_warn(&card->gdev->dev,
				"MAC address %pM already exists\n", mac);
			break;
		case -EPERM:
			dev_warn(&card->gdev->dev,
				"MAC address %pM is not authorized\n", mac);
			break;
		}
	}
	return rc;
}

static int qeth_l2_write_mac(struct qeth_card *card, u8 *mac)
{
	enum qeth_ipa_cmds cmd = is_multicast_ether_addr(mac) ?
					IPA_CMD_SETGMAC : IPA_CMD_SETVMAC;
	int rc;

	QETH_CARD_TEXT(card, 2, "L2Wmac");
	rc = qeth_l2_send_setdelmac(card, mac, cmd);
	if (rc == -EEXIST)
		QETH_DBF_MESSAGE(2, "MAC already registered on device %x\n",
				 CARD_DEVID(card));
	else if (rc)
		QETH_DBF_MESSAGE(2, "Failed to register MAC on device %x: %d\n",
				 CARD_DEVID(card), rc);
	return rc;
}

static int qeth_l2_remove_mac(struct qeth_card *card, u8 *mac)
{
	enum qeth_ipa_cmds cmd = is_multicast_ether_addr(mac) ?
					IPA_CMD_DELGMAC : IPA_CMD_DELVMAC;
	int rc;

	QETH_CARD_TEXT(card, 2, "L2Rmac");
	rc = qeth_l2_send_setdelmac(card, mac, cmd);
	if (rc)
		QETH_DBF_MESSAGE(2, "Failed to delete MAC on device %u: %d\n",
				 CARD_DEVID(card), rc);
	return rc;
}

static void qeth_l2_drain_rx_mode_cache(struct qeth_card *card)
{
	struct qeth_mac *mac;
	struct hlist_node *tmp;
	int i;

	hash_for_each_safe(card->mac_htable, i, tmp, mac, hnode) {
		hash_del(&mac->hnode);
		kfree(mac);
	}
}

static void qeth_l2_fill_header(struct qeth_qdio_out_q *queue,
				struct qeth_hdr *hdr, struct sk_buff *skb,
				int ipv, unsigned int data_len)
{
	int cast_type = qeth_get_ether_cast_type(skb);
	struct vlan_ethhdr *veth = vlan_eth_hdr(skb);

	hdr->hdr.l2.pkt_length = data_len;

	if (skb_is_gso(skb)) {
		hdr->hdr.l2.id = QETH_HEADER_TYPE_L2_TSO;
	} else {
		hdr->hdr.l2.id = QETH_HEADER_TYPE_LAYER2;
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			qeth_tx_csum(skb, &hdr->hdr.l2.flags[1], ipv);
	}

	/* set byte byte 3 to casting flags */
	if (cast_type == RTN_MULTICAST)
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_MULTICAST;
	else if (cast_type == RTN_BROADCAST)
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_BROADCAST;
	else
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_UNICAST;

	/* VSWITCH relies on the VLAN
	 * information to be present in
	 * the QDIO header */
	if (veth->h_vlan_proto == __constant_htons(ETH_P_8021Q)) {
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_VLAN;
		hdr->hdr.l2.vlan_id = ntohs(veth->h_vlan_TCI);
	}
}

static int qeth_l2_setdelvlan_makerc(struct qeth_card *card, u16 retcode)
{
	if (retcode)
		QETH_CARD_TEXT_(card, 2, "err%04x", retcode);

	switch (retcode) {
	case IPA_RC_SUCCESS:
		return 0;
	case IPA_RC_L2_INVALID_VLAN_ID:
		return -EINVAL;
	case IPA_RC_L2_DUP_VLAN_ID:
		return -EEXIST;
	case IPA_RC_L2_VLAN_ID_NOT_FOUND:
		return -ENOENT;
	case IPA_RC_L2_VLAN_ID_NOT_ALLOWED:
		return -EPERM;
	default:
		return -EIO;
	}
}

static int qeth_l2_send_setdelvlan_cb(struct qeth_card *card,
				      struct qeth_reply *reply,
				      unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;

	QETH_CARD_TEXT(card, 2, "L2sdvcb");
	if (cmd->hdr.return_code) {
		QETH_DBF_MESSAGE(2, "Error in processing VLAN %u on device %x: %#x.\n",
				 cmd->data.setdelvlan.vlan_id,
				 CARD_DEVID(card), cmd->hdr.return_code);
		QETH_CARD_TEXT_(card, 2, "L2VL%4x", cmd->hdr.command);
	}
	return qeth_l2_setdelvlan_makerc(card, cmd->hdr.return_code);
}

static int qeth_l2_send_setdelvlan(struct qeth_card *card, __u16 i,
				   enum qeth_ipa_cmds ipacmd)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT_(card, 4, "L2sdv%x", ipacmd);
	iob = qeth_ipa_alloc_cmd(card, ipacmd, QETH_PROT_IPV4,
				 IPA_DATA_SIZEOF(setdelvlan));
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setdelvlan.vlan_id = i;
	return qeth_send_ipa_cmd(card, iob, qeth_l2_send_setdelvlan_cb, NULL);
}

static int qeth_l2_vlan_rx_add_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT_(card, 4, "aid:%d", vid);
	if (!vid)
		return 0;

	return qeth_l2_send_setdelvlan(card, vid, IPA_CMD_SETVLAN);
}

static int qeth_l2_vlan_rx_kill_vid(struct net_device *dev,
				    __be16 proto, u16 vid)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT_(card, 4, "kid:%d", vid);
	if (!vid)
		return 0;

	return qeth_l2_send_setdelvlan(card, vid, IPA_CMD_DELVLAN);
}

static void qeth_l2_stop_card(struct qeth_card *card)
{
	QETH_CARD_TEXT(card, 2, "stopcard");

	qeth_set_allowed_threads(card, 0, 1);

	cancel_work_sync(&card->rx_mode_work);
	qeth_l2_drain_rx_mode_cache(card);

	if (card->state == CARD_STATE_SOFTSETUP) {
		qeth_clear_ipacmd_list(card);
		qeth_drain_output_queues(card);
		card->state = CARD_STATE_DOWN;
	}

	qeth_qdio_clear_card(card, 0);
	qeth_clear_working_pool_list(card);
	flush_workqueue(card->event_wq);
	card->info.mac_bits &= ~QETH_LAYER2_MAC_REGISTERED;
	card->info.promisc_mode = 0;
}

static int qeth_l2_request_initial_mac(struct qeth_card *card)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 2, "l2reqmac");

	if (MACHINE_IS_VM) {
		rc = qeth_vm_request_mac(card);
		if (!rc)
			goto out;
		QETH_DBF_MESSAGE(2, "z/VM MAC Service failed on device %x: %#x\n",
				 CARD_DEVID(card), rc);
		QETH_CARD_TEXT_(card, 2, "err%04x", rc);
		/* fall back to alternative mechanism: */
	}

	if (!IS_OSN(card)) {
		rc = qeth_setadpparms_change_macaddr(card);
		if (!rc)
			goto out;
		QETH_DBF_MESSAGE(2, "READ_MAC Assist failed on device %x: %#x\n",
				 CARD_DEVID(card), rc);
		QETH_CARD_TEXT_(card, 2, "1err%04x", rc);
		/* fall back once more: */
	}

	/* some devices don't support a custom MAC address: */
	if (IS_OSM(card) || IS_OSX(card))
		return (rc) ? rc : -EADDRNOTAVAIL;
	eth_hw_addr_random(card->dev);

out:
	QETH_CARD_HEX(card, 2, card->dev->dev_addr, card->dev->addr_len);
	return 0;
}

static void qeth_l2_register_dev_addr(struct qeth_card *card)
{
	if (!is_valid_ether_addr(card->dev->dev_addr))
		qeth_l2_request_initial_mac(card);

	if (!IS_OSN(card) && !qeth_l2_send_setmac(card, card->dev->dev_addr))
		card->info.mac_bits |= QETH_LAYER2_MAC_REGISTERED;
}

static int qeth_l2_validate_addr(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	if (card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED)
		return eth_validate_addr(dev);

	QETH_CARD_TEXT(card, 4, "nomacadr");
	return -EPERM;
}

static int qeth_l2_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct qeth_card *card = dev->ml_priv;
	u8 old_addr[ETH_ALEN];
	int rc = 0;

	QETH_CARD_TEXT(card, 3, "setmac");

	if (IS_OSM(card) || IS_OSX(card)) {
		QETH_CARD_TEXT(card, 3, "setmcTYP");
		return -EOPNOTSUPP;
	}
	QETH_CARD_HEX(card, 3, addr->sa_data, ETH_ALEN);
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* don't register the same address twice */
	if (ether_addr_equal_64bits(dev->dev_addr, addr->sa_data) &&
	    (card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))
		return 0;

	/* add the new address, switch over, drop the old */
	rc = qeth_l2_send_setmac(card, addr->sa_data);
	if (rc)
		return rc;
	ether_addr_copy(old_addr, dev->dev_addr);
	ether_addr_copy(dev->dev_addr, addr->sa_data);

	if (card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED)
		qeth_l2_remove_mac(card, old_addr);
	card->info.mac_bits |= QETH_LAYER2_MAC_REGISTERED;
	return 0;
}

static void qeth_l2_promisc_to_bridge(struct qeth_card *card, bool enable)
{
	int role;
	int rc;

	QETH_CARD_TEXT(card, 3, "pmisc2br");

	if (enable) {
		if (card->options.sbp.reflect_promisc_primary)
			role = QETH_SBP_ROLE_PRIMARY;
		else
			role = QETH_SBP_ROLE_SECONDARY;
	} else
		role = QETH_SBP_ROLE_NONE;

	rc = qeth_bridgeport_setrole(card, role);
	QETH_CARD_TEXT_(card, 2, "bpm%c%04x", enable ? '+' : '-', rc);
	if (!rc) {
		card->options.sbp.role = role;
		card->info.promisc_mode = enable;
	}
}

static void qeth_l2_set_promisc_mode(struct qeth_card *card)
{
	bool enable = card->dev->flags & IFF_PROMISC;

	if (card->info.promisc_mode == enable)
		return;

	if (qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE)) {
		qeth_setadp_promisc_mode(card, enable);
	} else {
		mutex_lock(&card->sbp_lock);
		if (card->options.sbp.reflect_promisc)
			qeth_l2_promisc_to_bridge(card, enable);
		mutex_unlock(&card->sbp_lock);
	}
}

/* New MAC address is added to the hash table and marked to be written on card
 * only if there is not in the hash table storage already
 *
*/
static void qeth_l2_add_mac(struct qeth_card *card, struct netdev_hw_addr *ha)
{
	u32 mac_hash = get_unaligned((u32 *)(&ha->addr[2]));
	struct qeth_mac *mac;

	hash_for_each_possible(card->mac_htable, mac, hnode, mac_hash) {
		if (ether_addr_equal_64bits(ha->addr, mac->mac_addr)) {
			mac->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
			return;
		}
	}

	mac = kzalloc(sizeof(struct qeth_mac), GFP_ATOMIC);
	if (!mac)
		return;

	ether_addr_copy(mac->mac_addr, ha->addr);
	mac->disp_flag = QETH_DISP_ADDR_ADD;

	hash_add(card->mac_htable, &mac->hnode, mac_hash);
}

static void qeth_l2_rx_mode_work(struct work_struct *work)
{
	struct qeth_card *card = container_of(work, struct qeth_card,
					      rx_mode_work);
	struct net_device *dev = card->dev;
	struct netdev_hw_addr *ha;
	struct qeth_mac *mac;
	struct hlist_node *tmp;
	int i;
	int rc;

	QETH_CARD_TEXT(card, 3, "setmulti");

	netif_addr_lock_bh(dev);
	netdev_for_each_mc_addr(ha, dev)
		qeth_l2_add_mac(card, ha);
	netdev_for_each_uc_addr(ha, dev)
		qeth_l2_add_mac(card, ha);
	netif_addr_unlock_bh(dev);

	hash_for_each_safe(card->mac_htable, i, tmp, mac, hnode) {
		switch (mac->disp_flag) {
		case QETH_DISP_ADDR_DELETE:
			qeth_l2_remove_mac(card, mac->mac_addr);
			hash_del(&mac->hnode);
			kfree(mac);
			break;
		case QETH_DISP_ADDR_ADD:
			rc = qeth_l2_write_mac(card, mac->mac_addr);
			if (rc) {
				hash_del(&mac->hnode);
				kfree(mac);
				break;
			}
			/* fall through */
		default:
			/* for next call to set_rx_mode(): */
			mac->disp_flag = QETH_DISP_ADDR_DELETE;
		}
	}

	qeth_l2_set_promisc_mode(card);
}

static int qeth_l2_xmit_osn(struct qeth_card *card, struct sk_buff *skb,
			    struct qeth_qdio_out_q *queue)
{
	struct qeth_hdr *hdr = (struct qeth_hdr *)skb->data;
	addr_t end = (addr_t)(skb->data + sizeof(*hdr));
	addr_t start = (addr_t)skb->data;
	unsigned int elements = 0;
	unsigned int hd_len = 0;
	int rc;

	if (skb->protocol == htons(ETH_P_IPV6))
		return -EPROTONOSUPPORT;

	if (qeth_get_elements_for_range(start, end) > 1) {
		/* Misaligned HW header, move it to its own buffer element. */
		hdr = kmem_cache_alloc(qeth_core_header_cache, GFP_ATOMIC);
		if (!hdr)
			return -ENOMEM;
		hd_len = sizeof(*hdr);
		skb_copy_from_linear_data(skb, (char *)hdr, hd_len);
		elements++;
	}

	elements += qeth_count_elements(skb, hd_len);
	if (elements > queue->max_elements) {
		rc = -E2BIG;
		goto out;
	}

	rc = qeth_do_send_packet(card, queue, skb, hdr, hd_len, hd_len,
				 elements);
out:
	if (rc && hd_len)
		kmem_cache_free(qeth_core_header_cache, hdr);
	return rc;
}

static netdev_tx_t qeth_l2_hard_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	u16 txq = skb_get_queue_mapping(skb);
	struct qeth_qdio_out_q *queue;
	int rc;

	if (!skb_is_gso(skb))
		qdisc_skb_cb(skb)->pkt_len = skb->len;
	if (IS_IQD(card))
		txq = qeth_iqd_translate_txq(dev, txq);
	queue = card->qdio.out_qs[txq];

	if (IS_OSN(card))
		rc = qeth_l2_xmit_osn(card, skb, queue);
	else
		rc = qeth_xmit(card, skb, queue, qeth_get_ip_version(skb),
			       qeth_l2_fill_header);

	if (!rc)
		return NETDEV_TX_OK;

	QETH_TXQ_STAT_INC(queue, tx_dropped);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static u16 qeth_l2_select_queue(struct net_device *dev, struct sk_buff *skb,
				struct net_device *sb_dev)
{
	struct qeth_card *card = dev->ml_priv;

	if (IS_IQD(card))
		return qeth_iqd_select_queue(dev, skb,
					     qeth_get_ether_cast_type(skb),
					     sb_dev);
	return qeth_get_priority_queue(card, skb);
}

static const struct device_type qeth_l2_devtype = {
	.name = "qeth_layer2",
	.groups = qeth_l2_attr_groups,
};

static int qeth_l2_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc;

	qeth_l2_vnicc_set_defaults(card);
	mutex_init(&card->sbp_lock);

	if (gdev->dev.type == &qeth_generic_devtype) {
		rc = qeth_l2_create_device_attributes(&gdev->dev);
		if (rc)
			return rc;
	}

	hash_init(card->mac_htable);
	INIT_WORK(&card->rx_mode_work, qeth_l2_rx_mode_work);
	return 0;
}

static void qeth_l2_remove_device(struct ccwgroup_device *cgdev)
{
	struct qeth_card *card = dev_get_drvdata(&cgdev->dev);

	if (cgdev->dev.type == &qeth_generic_devtype)
		qeth_l2_remove_device_attributes(&cgdev->dev);
	qeth_set_allowed_threads(card, 0, 1);
	wait_event(card->wait_q, qeth_threads_running(card, 0xffffffff) == 0);

	if (cgdev->state == CCWGROUP_ONLINE)
		qeth_set_offline(card, false);

	cancel_work_sync(&card->close_dev_work);
	if (qeth_netdev_is_registered(card->dev))
		unregister_netdev(card->dev);
}

static void qeth_l2_set_rx_mode(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	schedule_work(&card->rx_mode_work);
}

static const struct net_device_ops qeth_l2_netdev_ops = {
	.ndo_open		= qeth_open,
	.ndo_stop		= qeth_stop,
	.ndo_get_stats64	= qeth_get_stats64,
	.ndo_start_xmit		= qeth_l2_hard_start_xmit,
	.ndo_features_check	= qeth_features_check,
	.ndo_select_queue	= qeth_l2_select_queue,
	.ndo_validate_addr	= qeth_l2_validate_addr,
	.ndo_set_rx_mode	= qeth_l2_set_rx_mode,
	.ndo_do_ioctl		= qeth_do_ioctl,
	.ndo_set_mac_address    = qeth_l2_set_mac_address,
	.ndo_vlan_rx_add_vid	= qeth_l2_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = qeth_l2_vlan_rx_kill_vid,
	.ndo_tx_timeout	   	= qeth_tx_timeout,
	.ndo_fix_features	= qeth_fix_features,
	.ndo_set_features	= qeth_set_features
};

static const struct net_device_ops qeth_osn_netdev_ops = {
	.ndo_open		= qeth_open,
	.ndo_stop		= qeth_stop,
	.ndo_get_stats64	= qeth_get_stats64,
	.ndo_start_xmit		= qeth_l2_hard_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= qeth_tx_timeout,
};

static int qeth_l2_setup_netdev(struct qeth_card *card, bool carrier_ok)
{
	int rc;

	if (IS_OSN(card)) {
		card->dev->netdev_ops = &qeth_osn_netdev_ops;
		card->dev->flags |= IFF_NOARP;
		goto add_napi;
	}

	card->dev->needed_headroom = sizeof(struct qeth_hdr);
	card->dev->netdev_ops = &qeth_l2_netdev_ops;
	card->dev->priv_flags |= IFF_UNICAST_FLT;

	if (IS_OSM(card)) {
		card->dev->features |= NETIF_F_VLAN_CHALLENGED;
	} else {
		if (!IS_VM_NIC(card))
			card->dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;
		card->dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	if (IS_OSD(card) && !IS_VM_NIC(card)) {
		card->dev->features |= NETIF_F_SG;
		/* OSA 3S and earlier has no RX/TX support */
		if (qeth_is_supported(card, IPA_OUTBOUND_CHECKSUM)) {
			card->dev->hw_features |= NETIF_F_IP_CSUM;
			card->dev->vlan_features |= NETIF_F_IP_CSUM;
		}
	}
	if (qeth_is_supported6(card, IPA_OUTBOUND_CHECKSUM_V6)) {
		card->dev->hw_features |= NETIF_F_IPV6_CSUM;
		card->dev->vlan_features |= NETIF_F_IPV6_CSUM;
	}
	if (qeth_is_supported(card, IPA_INBOUND_CHECKSUM) ||
	    qeth_is_supported6(card, IPA_INBOUND_CHECKSUM_V6)) {
		card->dev->hw_features |= NETIF_F_RXCSUM;
		card->dev->vlan_features |= NETIF_F_RXCSUM;
	}
	if (qeth_is_supported(card, IPA_OUTBOUND_TSO)) {
		card->dev->hw_features |= NETIF_F_TSO;
		card->dev->vlan_features |= NETIF_F_TSO;
	}
	if (qeth_is_supported6(card, IPA_OUTBOUND_TSO)) {
		card->dev->hw_features |= NETIF_F_TSO6;
		card->dev->vlan_features |= NETIF_F_TSO6;
	}

	if (card->dev->hw_features & (NETIF_F_TSO | NETIF_F_TSO6)) {
		card->dev->needed_headroom = sizeof(struct qeth_hdr_tso);
		netif_set_gso_max_size(card->dev,
				       PAGE_SIZE * (QDIO_MAX_ELEMENTS_PER_BUFFER - 1));
	}

add_napi:
	netif_napi_add(card->dev, &card->napi, qeth_poll, QETH_NAPI_WEIGHT);
	rc = register_netdev(card->dev);
	if (!rc && carrier_ok)
		netif_carrier_on(card->dev);

	if (rc)
		card->dev->netdev_ops = NULL;
	return rc;
}

static void qeth_l2_trace_features(struct qeth_card *card)
{
	/* Set BridgePort features */
	QETH_CARD_TEXT(card, 2, "featuSBP");
	QETH_CARD_HEX(card, 2, &card->options.sbp.supported_funcs,
		      sizeof(card->options.sbp.supported_funcs));
	/* VNIC Characteristics features */
	QETH_CARD_TEXT(card, 2, "feaVNICC");
	QETH_CARD_HEX(card, 2, &card->options.vnicc.sup_chars,
		      sizeof(card->options.vnicc.sup_chars));
}

static void qeth_l2_setup_bridgeport_attrs(struct qeth_card *card)
{
	if (!card->options.sbp.reflect_promisc &&
	    card->options.sbp.role != QETH_SBP_ROLE_NONE) {
		/* Conditional to avoid spurious error messages */
		qeth_bridgeport_setrole(card, card->options.sbp.role);
		/* Let the callback function refresh the stored role value. */
		qeth_bridgeport_query_ports(card, &card->options.sbp.role,
					    NULL);
	}
	if (card->options.sbp.hostnotification) {
		if (qeth_bridgeport_an_set(card, 1))
			card->options.sbp.hostnotification = 0;
	} else {
		qeth_bridgeport_an_set(card, 0);
	}
}

static int qeth_l2_set_online(struct qeth_card *card)
{
	struct ccwgroup_device *gdev = card->gdev;
	struct net_device *dev = card->dev;
	int rc = 0;
	bool carrier_ok;

	rc = qeth_core_hardsetup_card(card, &carrier_ok);
	if (rc) {
		QETH_CARD_TEXT_(card, 2, "2err%04x", rc);
		rc = -ENODEV;
		goto out_remove;
	}

	mutex_lock(&card->sbp_lock);
	qeth_bridgeport_query_support(card);
	if (card->options.sbp.supported_funcs) {
		qeth_l2_setup_bridgeport_attrs(card);
		dev_info(&card->gdev->dev,
			 "The device represents a Bridge Capable Port\n");
	}
	mutex_unlock(&card->sbp_lock);

	qeth_l2_register_dev_addr(card);

	/* for the rx_bcast characteristic, init VNICC after setmac */
	qeth_l2_vnicc_init(card);

	qeth_trace_features(card);
	qeth_l2_trace_features(card);

	qeth_print_status_message(card);

	/* softsetup */
	QETH_CARD_TEXT(card, 2, "softsetp");

	card->state = CARD_STATE_SOFTSETUP;

	qeth_set_allowed_threads(card, 0xffffffff, 0);

	if (!qeth_netdev_is_registered(dev)) {
		rc = qeth_l2_setup_netdev(card, carrier_ok);
		if (rc)
			goto out_remove;
	} else {
		rtnl_lock();
		if (carrier_ok)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);

		netif_device_attach(dev);
		qeth_enable_hw_features(dev);

		if (card->info.open_when_online) {
			card->info.open_when_online = 0;
			dev_open(dev, NULL);
		}
		rtnl_unlock();
	}
	/* let user_space know that device is online */
	kobject_uevent(&gdev->dev.kobj, KOBJ_CHANGE);
	return 0;

out_remove:
	qeth_l2_stop_card(card);
	qeth_stop_channel(&card->data);
	qeth_stop_channel(&card->write);
	qeth_stop_channel(&card->read);
	qdio_free(CARD_DDEV(card));
	return rc;
}

static void qeth_l2_set_offline(struct qeth_card *card)
{
	qeth_l2_stop_card(card);
}

static int __init qeth_l2_init(void)
{
	pr_info("register layer 2 discipline\n");
	return 0;
}

static void __exit qeth_l2_exit(void)
{
	pr_info("unregister layer 2 discipline\n");
}

/* Returns zero if the command is successfully "consumed" */
static int qeth_l2_control_event(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd)
{
	switch (cmd->hdr.command) {
	case IPA_CMD_SETBRIDGEPORT_OSA:
	case IPA_CMD_SETBRIDGEPORT_IQD:
		if (cmd->data.sbp.hdr.command_code ==
				IPA_SBP_BRIDGE_PORT_STATE_CHANGE) {
			qeth_bridge_state_change(card, cmd);
			return 0;
		} else
			return 1;
	case IPA_CMD_ADDRESS_CHANGE_NOTIF:
		qeth_bridge_host_event(card, cmd);
		return 0;
	default:
		return 1;
	}
}

struct qeth_discipline qeth_l2_discipline = {
	.devtype = &qeth_l2_devtype,
	.setup = qeth_l2_probe_device,
	.remove = qeth_l2_remove_device,
	.set_online = qeth_l2_set_online,
	.set_offline = qeth_l2_set_offline,
	.do_ioctl = NULL,
	.control_event_handler = qeth_l2_control_event,
};
EXPORT_SYMBOL_GPL(qeth_l2_discipline);

static void qeth_osn_assist_cb(struct qeth_card *card,
			       struct qeth_cmd_buffer *iob,
			       unsigned int data_length)
{
	qeth_notify_cmd(iob, 0);
	qeth_put_cmd(iob);
}

int qeth_osn_assist(struct net_device *dev, void *data, int data_len)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_card *card;

	if (data_len < 0)
		return -EINVAL;
	if (!dev)
		return -ENODEV;
	card = dev->ml_priv;
	if (!card)
		return -ENODEV;
	QETH_CARD_TEXT(card, 2, "osnsdmc");
	if (!qeth_card_hw_is_reachable(card))
		return -ENODEV;

	iob = qeth_alloc_cmd(&card->write, IPA_PDU_HEADER_SIZE + data_len, 1,
			     QETH_IPA_TIMEOUT);
	if (!iob)
		return -ENOMEM;

	qeth_prepare_ipa_cmd(card, iob, (u16) data_len, NULL);

	memcpy(__ipa_cmd(iob), data, data_len);
	iob->callback = qeth_osn_assist_cb;
	return qeth_send_ipa_cmd(card, iob, NULL, NULL);
}
EXPORT_SYMBOL(qeth_osn_assist);

int qeth_osn_register(unsigned char *read_dev_no, struct net_device **dev,
		  int (*assist_cb)(struct net_device *, void *),
		  int (*data_cb)(struct sk_buff *))
{
	struct qeth_card *card;
	char bus_id[16];
	u16 devno;

	memcpy(&devno, read_dev_no, 2);
	sprintf(bus_id, "0.0.%04x", devno);
	card = qeth_get_card_by_busid(bus_id);
	if (!card || !IS_OSN(card))
		return -ENODEV;
	*dev = card->dev;

	QETH_CARD_TEXT(card, 2, "osnreg");
	if ((assist_cb == NULL) || (data_cb == NULL))
		return -EINVAL;
	card->osn_info.assist_cb = assist_cb;
	card->osn_info.data_cb = data_cb;
	return 0;
}
EXPORT_SYMBOL(qeth_osn_register);

void qeth_osn_deregister(struct net_device *dev)
{
	struct qeth_card *card;

	if (!dev)
		return;
	card = dev->ml_priv;
	if (!card)
		return;
	QETH_CARD_TEXT(card, 2, "osndereg");
	card->osn_info.assist_cb = NULL;
	card->osn_info.data_cb = NULL;
	return;
}
EXPORT_SYMBOL(qeth_osn_deregister);

/* SETBRIDGEPORT support, async notifications */

enum qeth_an_event_type {anev_reg_unreg, anev_abort, anev_reset};

/**
 * qeth_bridge_emit_host_event() - bridgeport address change notification
 * @card:  qeth_card structure pointer, for udev events.
 * @evtype:  "normal" register/unregister, or abort, or reset. For abort
 *	      and reset token and addr_lnid are unused and may be NULL.
 * @code:  event bitmask: high order bit 0x80 value 1 means removal of an
 *			  object, 0 - addition of an object.
 *			  0x01 - VLAN, 0x02 - MAC, 0x03 - VLAN and MAC.
 * @token: "network token" structure identifying physical address of the port.
 * @addr_lnid: pointer to structure with MAC address and VLAN ID.
 *
 * This function is called when registrations and deregistrations are
 * reported by the hardware, and also when notifications are enabled -
 * for all currently registered addresses.
 */
static void qeth_bridge_emit_host_event(struct qeth_card *card,
	enum qeth_an_event_type evtype,
	u8 code, struct net_if_token *token, struct mac_addr_lnid *addr_lnid)
{
	char str[7][32];
	char *env[8];
	int i = 0;

	switch (evtype) {
	case anev_reg_unreg:
		snprintf(str[i], sizeof(str[i]), "BRIDGEDHOST=%s",
				(code & IPA_ADDR_CHANGE_CODE_REMOVAL)
				? "deregister" : "register");
		env[i] = str[i]; i++;
		if (code & IPA_ADDR_CHANGE_CODE_VLANID) {
			snprintf(str[i], sizeof(str[i]), "VLAN=%d",
				addr_lnid->lnid);
			env[i] = str[i]; i++;
		}
		if (code & IPA_ADDR_CHANGE_CODE_MACADDR) {
			snprintf(str[i], sizeof(str[i]), "MAC=%pM",
				addr_lnid->mac);
			env[i] = str[i]; i++;
		}
		snprintf(str[i], sizeof(str[i]), "NTOK_BUSID=%x.%x.%04x",
			token->cssid, token->ssid, token->devnum);
		env[i] = str[i]; i++;
		snprintf(str[i], sizeof(str[i]), "NTOK_IID=%02x", token->iid);
		env[i] = str[i]; i++;
		snprintf(str[i], sizeof(str[i]), "NTOK_CHPID=%02x",
				token->chpid);
		env[i] = str[i]; i++;
		snprintf(str[i], sizeof(str[i]), "NTOK_CHID=%04x", token->chid);
		env[i] = str[i]; i++;
		break;
	case anev_abort:
		snprintf(str[i], sizeof(str[i]), "BRIDGEDHOST=abort");
		env[i] = str[i]; i++;
		break;
	case anev_reset:
		snprintf(str[i], sizeof(str[i]), "BRIDGEDHOST=reset");
		env[i] = str[i]; i++;
		break;
	}
	env[i] = NULL;
	kobject_uevent_env(&card->gdev->dev.kobj, KOBJ_CHANGE, env);
}

struct qeth_bridge_state_data {
	struct work_struct worker;
	struct qeth_card *card;
	struct qeth_sbp_state_change qports;
};

static void qeth_bridge_state_change_worker(struct work_struct *work)
{
	struct qeth_bridge_state_data *data =
		container_of(work, struct qeth_bridge_state_data, worker);
	/* We are only interested in the first entry - local port */
	struct qeth_sbp_port_entry *entry = &data->qports.entry[0];
	char env_locrem[32];
	char env_role[32];
	char env_state[32];
	char *env[] = {
		env_locrem,
		env_role,
		env_state,
		NULL
	};

	/* Role should not change by itself, but if it did, */
	/* information from the hardware is authoritative.  */
	mutex_lock(&data->card->sbp_lock);
	data->card->options.sbp.role = entry->role;
	mutex_unlock(&data->card->sbp_lock);

	snprintf(env_locrem, sizeof(env_locrem), "BRIDGEPORT=statechange");
	snprintf(env_role, sizeof(env_role), "ROLE=%s",
		(entry->role == QETH_SBP_ROLE_NONE) ? "none" :
		(entry->role == QETH_SBP_ROLE_PRIMARY) ? "primary" :
		(entry->role == QETH_SBP_ROLE_SECONDARY) ? "secondary" :
		"<INVALID>");
	snprintf(env_state, sizeof(env_state), "STATE=%s",
		(entry->state == QETH_SBP_STATE_INACTIVE) ? "inactive" :
		(entry->state == QETH_SBP_STATE_STANDBY) ? "standby" :
		(entry->state == QETH_SBP_STATE_ACTIVE) ? "active" :
		"<INVALID>");
	kobject_uevent_env(&data->card->gdev->dev.kobj,
				KOBJ_CHANGE, env);
	kfree(data);
}

static void qeth_bridge_state_change(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd)
{
	struct qeth_sbp_state_change *qports =
		 &cmd->data.sbp.data.state_change;
	struct qeth_bridge_state_data *data;
	int extrasize;

	QETH_CARD_TEXT(card, 2, "brstchng");
	if (qports->entry_length != sizeof(struct qeth_sbp_port_entry)) {
		QETH_CARD_TEXT_(card, 2, "BPsz%04x", qports->entry_length);
		return;
	}
	extrasize = sizeof(struct qeth_sbp_port_entry) * qports->num_entries;
	data = kzalloc(sizeof(struct qeth_bridge_state_data) + extrasize,
		GFP_ATOMIC);
	if (!data) {
		QETH_CARD_TEXT(card, 2, "BPSalloc");
		return;
	}
	INIT_WORK(&data->worker, qeth_bridge_state_change_worker);
	data->card = card;
	memcpy(&data->qports, qports,
			sizeof(struct qeth_sbp_state_change) + extrasize);
	queue_work(card->event_wq, &data->worker);
}

struct qeth_bridge_host_data {
	struct work_struct worker;
	struct qeth_card *card;
	struct qeth_ipacmd_addr_change hostevs;
};

static void qeth_bridge_host_event_worker(struct work_struct *work)
{
	struct qeth_bridge_host_data *data =
		container_of(work, struct qeth_bridge_host_data, worker);
	int i;

	if (data->hostevs.lost_event_mask) {
		dev_info(&data->card->gdev->dev,
"Address notification from the Bridge Port stopped %s (%s)\n",
			data->card->dev->name,
			(data->hostevs.lost_event_mask == 0x01)
			? "Overflow"
			: (data->hostevs.lost_event_mask == 0x02)
			? "Bridge port state change"
			: "Unknown reason");
		mutex_lock(&data->card->sbp_lock);
		data->card->options.sbp.hostnotification = 0;
		mutex_unlock(&data->card->sbp_lock);
		qeth_bridge_emit_host_event(data->card, anev_abort,
			0, NULL, NULL);
	} else
		for (i = 0; i < data->hostevs.num_entries; i++) {
			struct qeth_ipacmd_addr_change_entry *entry =
					&data->hostevs.entry[i];
			qeth_bridge_emit_host_event(data->card,
					anev_reg_unreg,
					entry->change_code,
					&entry->token, &entry->addr_lnid);
		}
	kfree(data);
}

static void qeth_bridge_host_event(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd)
{
	struct qeth_ipacmd_addr_change *hostevs =
		 &cmd->data.addrchange;
	struct qeth_bridge_host_data *data;
	int extrasize;

	QETH_CARD_TEXT(card, 2, "brhostev");
	if (cmd->hdr.return_code != 0x0000) {
		if (cmd->hdr.return_code == 0x0010) {
			if (hostevs->lost_event_mask == 0x00)
				hostevs->lost_event_mask = 0xff;
		} else {
			QETH_CARD_TEXT_(card, 2, "BPHe%04x",
				cmd->hdr.return_code);
			return;
		}
	}
	extrasize = sizeof(struct qeth_ipacmd_addr_change_entry) *
						hostevs->num_entries;
	data = kzalloc(sizeof(struct qeth_bridge_host_data) + extrasize,
		GFP_ATOMIC);
	if (!data) {
		QETH_CARD_TEXT(card, 2, "BPHalloc");
		return;
	}
	INIT_WORK(&data->worker, qeth_bridge_host_event_worker);
	data->card = card;
	memcpy(&data->hostevs, hostevs,
			sizeof(struct qeth_ipacmd_addr_change) + extrasize);
	queue_work(card->event_wq, &data->worker);
}

/* SETBRIDGEPORT support; sending commands */

struct _qeth_sbp_cbctl {
	union {
		u32 supported;
		struct {
			enum qeth_sbp_roles *role;
			enum qeth_sbp_states *state;
		} qports;
	} data;
};

static int qeth_bridgeport_makerc(struct qeth_card *card,
				  struct qeth_ipa_cmd *cmd)
{
	struct qeth_ipacmd_setbridgeport *sbp = &cmd->data.sbp;
	enum qeth_ipa_sbp_cmd setcmd = sbp->hdr.command_code;
	u16 ipa_rc = cmd->hdr.return_code;
	u16 sbp_rc = sbp->hdr.return_code;
	int rc;

	if (ipa_rc == IPA_RC_SUCCESS && sbp_rc == IPA_RC_SUCCESS)
		return 0;

	if ((IS_IQD(card) && ipa_rc == IPA_RC_SUCCESS) ||
	    (!IS_IQD(card) && ipa_rc == sbp_rc)) {
		switch (sbp_rc) {
		case IPA_RC_SUCCESS:
			rc = 0;
			break;
		case IPA_RC_L2_UNSUPPORTED_CMD:
		case IPA_RC_UNSUPPORTED_COMMAND:
			rc = -EOPNOTSUPP;
			break;
		case IPA_RC_SBP_OSA_NOT_CONFIGURED:
		case IPA_RC_SBP_IQD_NOT_CONFIGURED:
			rc = -ENODEV; /* maybe not the best code here? */
			dev_err(&card->gdev->dev,
	"The device is not configured as a Bridge Port\n");
			break;
		case IPA_RC_SBP_OSA_OS_MISMATCH:
		case IPA_RC_SBP_IQD_OS_MISMATCH:
			rc = -EPERM;
			dev_err(&card->gdev->dev,
	"A Bridge Port is already configured by a different operating system\n");
			break;
		case IPA_RC_SBP_OSA_ANO_DEV_PRIMARY:
		case IPA_RC_SBP_IQD_ANO_DEV_PRIMARY:
			switch (setcmd) {
			case IPA_SBP_SET_PRIMARY_BRIDGE_PORT:
				rc = -EEXIST;
				dev_err(&card->gdev->dev,
	"The LAN already has a primary Bridge Port\n");
				break;
			case IPA_SBP_SET_SECONDARY_BRIDGE_PORT:
				rc = -EBUSY;
				dev_err(&card->gdev->dev,
	"The device is already a primary Bridge Port\n");
				break;
			default:
				rc = -EIO;
			}
			break;
		case IPA_RC_SBP_OSA_CURRENT_SECOND:
		case IPA_RC_SBP_IQD_CURRENT_SECOND:
			rc = -EBUSY;
			dev_err(&card->gdev->dev,
	"The device is already a secondary Bridge Port\n");
			break;
		case IPA_RC_SBP_OSA_LIMIT_SECOND:
		case IPA_RC_SBP_IQD_LIMIT_SECOND:
			rc = -EEXIST;
			dev_err(&card->gdev->dev,
	"The LAN cannot have more secondary Bridge Ports\n");
			break;
		case IPA_RC_SBP_OSA_CURRENT_PRIMARY:
		case IPA_RC_SBP_IQD_CURRENT_PRIMARY:
			rc = -EBUSY;
			dev_err(&card->gdev->dev,
	"The device is already a primary Bridge Port\n");
			break;
		case IPA_RC_SBP_OSA_NOT_AUTHD_BY_ZMAN:
		case IPA_RC_SBP_IQD_NOT_AUTHD_BY_ZMAN:
			rc = -EACCES;
			dev_err(&card->gdev->dev,
	"The device is not authorized to be a Bridge Port\n");
			break;
		default:
			rc = -EIO;
		}
	} else {
		switch (ipa_rc) {
		case IPA_RC_NOTSUPP:
			rc = -EOPNOTSUPP;
			break;
		case IPA_RC_UNSUPPORTED_COMMAND:
			rc = -EOPNOTSUPP;
			break;
		default:
			rc = -EIO;
		}
	}

	if (rc) {
		QETH_CARD_TEXT_(card, 2, "SBPi%04x", ipa_rc);
		QETH_CARD_TEXT_(card, 2, "SBPc%04x", sbp_rc);
	}
	return rc;
}

static struct qeth_cmd_buffer *qeth_sbp_build_cmd(struct qeth_card *card,
						  enum qeth_ipa_sbp_cmd sbp_cmd,
						  unsigned int data_length)
{
	enum qeth_ipa_cmds ipa_cmd = IS_IQD(card) ? IPA_CMD_SETBRIDGEPORT_IQD :
						    IPA_CMD_SETBRIDGEPORT_OSA;
	struct qeth_ipacmd_sbp_hdr *hdr;
	struct qeth_cmd_buffer *iob;

	iob = qeth_ipa_alloc_cmd(card, ipa_cmd, QETH_PROT_NONE,
				 data_length +
				 offsetof(struct qeth_ipacmd_setbridgeport,
					  data));
	if (!iob)
		return iob;

	hdr = &__ipa_cmd(iob)->data.sbp.hdr;
	hdr->cmdlength = sizeof(*hdr) + data_length;
	hdr->command_code = sbp_cmd;
	hdr->used_total = 1;
	hdr->seq_no = 1;
	return iob;
}

static int qeth_bridgeport_query_support_cb(struct qeth_card *card,
	struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;
	struct _qeth_sbp_cbctl *cbctl = (struct _qeth_sbp_cbctl *)reply->param;
	int rc;

	QETH_CARD_TEXT(card, 2, "brqsupcb");
	rc = qeth_bridgeport_makerc(card, cmd);
	if (rc)
		return rc;

	cbctl->data.supported =
		cmd->data.sbp.data.query_cmds_supp.supported_cmds;
	return 0;
}

/**
 * qeth_bridgeport_query_support() - store bitmask of supported subfunctions.
 * @card:			     qeth_card structure pointer.
 *
 * Sets bitmask of supported setbridgeport subfunctions in the qeth_card
 * strucutre: card->options.sbp.supported_funcs.
 */
static void qeth_bridgeport_query_support(struct qeth_card *card)
{
	struct qeth_cmd_buffer *iob;
	struct _qeth_sbp_cbctl cbctl;

	QETH_CARD_TEXT(card, 2, "brqsuppo");
	iob = qeth_sbp_build_cmd(card, IPA_SBP_QUERY_COMMANDS_SUPPORTED,
				 SBP_DATA_SIZEOF(query_cmds_supp));
	if (!iob)
		return;

	if (qeth_send_ipa_cmd(card, iob, qeth_bridgeport_query_support_cb,
			      &cbctl)) {
		card->options.sbp.role = QETH_SBP_ROLE_NONE;
		card->options.sbp.supported_funcs = 0;
		return;
	}
	card->options.sbp.supported_funcs = cbctl.data.supported;
}

static int qeth_bridgeport_query_ports_cb(struct qeth_card *card,
	struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;
	struct qeth_sbp_query_ports *qports = &cmd->data.sbp.data.query_ports;
	struct _qeth_sbp_cbctl *cbctl = (struct _qeth_sbp_cbctl *)reply->param;
	int rc;

	QETH_CARD_TEXT(card, 2, "brqprtcb");
	rc = qeth_bridgeport_makerc(card, cmd);
	if (rc)
		return rc;

	if (qports->entry_length != sizeof(struct qeth_sbp_port_entry)) {
		QETH_CARD_TEXT_(card, 2, "SBPs%04x", qports->entry_length);
		return -EINVAL;
	}
	/* first entry contains the state of the local port */
	if (qports->num_entries > 0) {
		if (cbctl->data.qports.role)
			*cbctl->data.qports.role = qports->entry[0].role;
		if (cbctl->data.qports.state)
			*cbctl->data.qports.state = qports->entry[0].state;
	}
	return 0;
}

/**
 * qeth_bridgeport_query_ports() - query local bridgeport status.
 * @card:			   qeth_card structure pointer.
 * @role:   Role of the port: 0-none, 1-primary, 2-secondary.
 * @state:  State of the port: 0-inactive, 1-standby, 2-active.
 *
 * Returns negative errno-compatible error indication or 0 on success.
 *
 * 'role' and 'state' are not updated in case of hardware operation failure.
 */
int qeth_bridgeport_query_ports(struct qeth_card *card,
	enum qeth_sbp_roles *role, enum qeth_sbp_states *state)
{
	struct qeth_cmd_buffer *iob;
	struct _qeth_sbp_cbctl cbctl = {
		.data = {
			.qports = {
				.role = role,
				.state = state,
			},
		},
	};

	QETH_CARD_TEXT(card, 2, "brqports");
	if (!(card->options.sbp.supported_funcs & IPA_SBP_QUERY_BRIDGE_PORTS))
		return -EOPNOTSUPP;
	iob = qeth_sbp_build_cmd(card, IPA_SBP_QUERY_BRIDGE_PORTS, 0);
	if (!iob)
		return -ENOMEM;

	return qeth_send_ipa_cmd(card, iob, qeth_bridgeport_query_ports_cb,
				 &cbctl);
}

static int qeth_bridgeport_set_cb(struct qeth_card *card,
	struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *)data;

	QETH_CARD_TEXT(card, 2, "brsetrcb");
	return qeth_bridgeport_makerc(card, cmd);
}

/**
 * qeth_bridgeport_setrole() - Assign primary role to the port.
 * @card:		       qeth_card structure pointer.
 * @role:		       Role to assign.
 *
 * Returns negative errno-compatible error indication or 0 on success.
 */
int qeth_bridgeport_setrole(struct qeth_card *card, enum qeth_sbp_roles role)
{
	struct qeth_cmd_buffer *iob;
	enum qeth_ipa_sbp_cmd setcmd;
	unsigned int cmdlength = 0;

	QETH_CARD_TEXT(card, 2, "brsetrol");
	switch (role) {
	case QETH_SBP_ROLE_NONE:
		setcmd = IPA_SBP_RESET_BRIDGE_PORT_ROLE;
		break;
	case QETH_SBP_ROLE_PRIMARY:
		setcmd = IPA_SBP_SET_PRIMARY_BRIDGE_PORT;
		cmdlength = SBP_DATA_SIZEOF(set_primary);
		break;
	case QETH_SBP_ROLE_SECONDARY:
		setcmd = IPA_SBP_SET_SECONDARY_BRIDGE_PORT;
		break;
	default:
		return -EINVAL;
	}
	if (!(card->options.sbp.supported_funcs & setcmd))
		return -EOPNOTSUPP;
	iob = qeth_sbp_build_cmd(card, setcmd, cmdlength);
	if (!iob)
		return -ENOMEM;

	return qeth_send_ipa_cmd(card, iob, qeth_bridgeport_set_cb, NULL);
}

/**
 * qeth_anset_makerc() - derive "traditional" error from hardware codes.
 * @card:		      qeth_card structure pointer, for debug messages.
 *
 * Returns negative errno-compatible error indication or 0 on success.
 */
static int qeth_anset_makerc(struct qeth_card *card, int pnso_rc, u16 response)
{
	int rc;

	if (pnso_rc == 0)
		switch (response) {
		case 0x0001:
			rc = 0;
			break;
		case 0x0004:
		case 0x0100:
		case 0x0106:
			rc = -EOPNOTSUPP;
			dev_err(&card->gdev->dev,
				"Setting address notification failed\n");
			break;
		case 0x0107:
			rc = -EAGAIN;
			break;
		default:
			rc = -EIO;
		}
	else
		rc = -EIO;

	if (rc) {
		QETH_CARD_TEXT_(card, 2, "SBPp%04x", pnso_rc);
		QETH_CARD_TEXT_(card, 2, "SBPr%04x", response);
	}
	return rc;
}

static void qeth_bridgeport_an_set_cb(void *priv,
		enum qdio_brinfo_entry_type type, void *entry)
{
	struct qeth_card *card = (struct qeth_card *)priv;
	struct qdio_brinfo_entry_l2 *l2entry;
	u8 code;

	if (type != l2_addr_lnid) {
		WARN_ON_ONCE(1);
		return;
	}

	l2entry = (struct qdio_brinfo_entry_l2 *)entry;
	code = IPA_ADDR_CHANGE_CODE_MACADDR;
	if (l2entry->addr_lnid.lnid < VLAN_N_VID)
		code |= IPA_ADDR_CHANGE_CODE_VLANID;
	qeth_bridge_emit_host_event(card, anev_reg_unreg, code,
		(struct net_if_token *)&l2entry->nit,
		(struct mac_addr_lnid *)&l2entry->addr_lnid);
}

/**
 * qeth_bridgeport_an_set() - Enable or disable bridgeport address notification
 * @card:		      qeth_card structure pointer.
 * @enable:		      0 - disable, non-zero - enable notifications
 *
 * Returns negative errno-compatible error indication or 0 on success.
 *
 * On enable, emits a series of address notifications udev events for all
 * currently registered hosts.
 */
int qeth_bridgeport_an_set(struct qeth_card *card, int enable)
{
	int rc;
	u16 response;
	struct ccw_device *ddev;
	struct subchannel_id schid;

	if (!card)
		return -EINVAL;
	if (!card->options.sbp.supported_funcs)
		return -EOPNOTSUPP;
	ddev = CARD_DDEV(card);
	ccw_device_get_schid(ddev, &schid);

	if (enable) {
		qeth_bridge_emit_host_event(card, anev_reset, 0, NULL, NULL);
		rc = qdio_pnso_brinfo(schid, 1, &response,
			qeth_bridgeport_an_set_cb, card);
	} else
		rc = qdio_pnso_brinfo(schid, 0, &response, NULL, NULL);
	return qeth_anset_makerc(card, rc, response);
}

static bool qeth_bridgeport_is_in_use(struct qeth_card *card)
{
	return (card->options.sbp.role || card->options.sbp.reflect_promisc ||
		card->options.sbp.hostnotification);
}

/* VNIC Characteristics support */

/* handle VNICC IPA command return codes; convert to error codes */
static int qeth_l2_vnicc_makerc(struct qeth_card *card, u16 ipa_rc)
{
	int rc;

	switch (ipa_rc) {
	case IPA_RC_SUCCESS:
		return ipa_rc;
	case IPA_RC_L2_UNSUPPORTED_CMD:
	case IPA_RC_NOTSUPP:
		rc = -EOPNOTSUPP;
		break;
	case IPA_RC_VNICC_OOSEQ:
		rc = -EALREADY;
		break;
	case IPA_RC_VNICC_VNICBP:
		rc = -EBUSY;
		break;
	case IPA_RC_L2_ADDR_TABLE_FULL:
		rc = -ENOSPC;
		break;
	case IPA_RC_L2_MAC_NOT_AUTH_BY_ADP:
		rc = -EACCES;
		break;
	default:
		rc = -EIO;
	}

	QETH_CARD_TEXT_(card, 2, "err%04x", ipa_rc);
	return rc;
}

/* generic VNICC request call back control */
struct _qeth_l2_vnicc_request_cbctl {
	struct {
		union{
			u32 *sup_cmds;
			u32 *timeout;
		};
	} result;
};

/* generic VNICC request call back */
static int qeth_l2_vnicc_request_cb(struct qeth_card *card,
				    struct qeth_reply *reply,
				    unsigned long data)
{
	struct _qeth_l2_vnicc_request_cbctl *cbctl =
		(struct _qeth_l2_vnicc_request_cbctl *) reply->param;
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;
	struct qeth_ipacmd_vnicc *rep = &cmd->data.vnicc;
	u32 sub_cmd = cmd->data.vnicc.hdr.sub_command;

	QETH_CARD_TEXT(card, 2, "vniccrcb");
	if (cmd->hdr.return_code)
		return qeth_l2_vnicc_makerc(card, cmd->hdr.return_code);
	/* return results to caller */
	card->options.vnicc.sup_chars = rep->vnicc_cmds.supported;
	card->options.vnicc.cur_chars = rep->vnicc_cmds.enabled;

	if (sub_cmd == IPA_VNICC_QUERY_CMDS)
		*cbctl->result.sup_cmds = rep->data.query_cmds.sup_cmds;
	else if (sub_cmd == IPA_VNICC_GET_TIMEOUT)
		*cbctl->result.timeout = rep->data.getset_timeout.timeout;

	return 0;
}

static struct qeth_cmd_buffer *qeth_l2_vnicc_build_cmd(struct qeth_card *card,
						       u32 vnicc_cmd,
						       unsigned int data_length)
{
	struct qeth_ipacmd_vnicc_hdr *hdr;
	struct qeth_cmd_buffer *iob;

	iob = qeth_ipa_alloc_cmd(card, IPA_CMD_VNICC, QETH_PROT_NONE,
				 data_length +
				 offsetof(struct qeth_ipacmd_vnicc, data));
	if (!iob)
		return NULL;

	hdr = &__ipa_cmd(iob)->data.vnicc.hdr;
	hdr->data_length = sizeof(*hdr) + data_length;
	hdr->sub_command = vnicc_cmd;
	return iob;
}

/* VNICC query VNIC characteristics request */
static int qeth_l2_vnicc_query_chars(struct qeth_card *card)
{
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "vniccqch");
	iob = qeth_l2_vnicc_build_cmd(card, IPA_VNICC_QUERY_CHARS, 0);
	if (!iob)
		return -ENOMEM;

	return qeth_send_ipa_cmd(card, iob, qeth_l2_vnicc_request_cb, NULL);
}

/* VNICC query sub commands request */
static int qeth_l2_vnicc_query_cmds(struct qeth_card *card, u32 vnic_char,
				    u32 *sup_cmds)
{
	struct _qeth_l2_vnicc_request_cbctl cbctl;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "vniccqcm");
	iob = qeth_l2_vnicc_build_cmd(card, IPA_VNICC_QUERY_CMDS,
				      VNICC_DATA_SIZEOF(query_cmds));
	if (!iob)
		return -ENOMEM;

	__ipa_cmd(iob)->data.vnicc.data.query_cmds.vnic_char = vnic_char;

	/* prepare callback control */
	cbctl.result.sup_cmds = sup_cmds;

	return qeth_send_ipa_cmd(card, iob, qeth_l2_vnicc_request_cb, &cbctl);
}

/* VNICC enable/disable characteristic request */
static int qeth_l2_vnicc_set_char(struct qeth_card *card, u32 vnic_char,
				      u32 cmd)
{
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "vniccedc");
	iob = qeth_l2_vnicc_build_cmd(card, cmd, VNICC_DATA_SIZEOF(set_char));
	if (!iob)
		return -ENOMEM;

	__ipa_cmd(iob)->data.vnicc.data.set_char.vnic_char = vnic_char;

	return qeth_send_ipa_cmd(card, iob, qeth_l2_vnicc_request_cb, NULL);
}

/* VNICC get/set timeout for characteristic request */
static int qeth_l2_vnicc_getset_timeout(struct qeth_card *card, u32 vnicc,
					u32 cmd, u32 *timeout)
{
	struct qeth_vnicc_getset_timeout *getset_timeout;
	struct _qeth_l2_vnicc_request_cbctl cbctl;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "vniccgst");
	iob = qeth_l2_vnicc_build_cmd(card, cmd,
				      VNICC_DATA_SIZEOF(getset_timeout));
	if (!iob)
		return -ENOMEM;

	getset_timeout = &__ipa_cmd(iob)->data.vnicc.data.getset_timeout;
	getset_timeout->vnic_char = vnicc;

	if (cmd == IPA_VNICC_SET_TIMEOUT)
		getset_timeout->timeout = *timeout;

	/* prepare callback control */
	if (cmd == IPA_VNICC_GET_TIMEOUT)
		cbctl.result.timeout = timeout;

	return qeth_send_ipa_cmd(card, iob, qeth_l2_vnicc_request_cb, &cbctl);
}

/* set current VNICC flag state; called from sysfs store function */
int qeth_l2_vnicc_set_state(struct qeth_card *card, u32 vnicc, bool state)
{
	int rc = 0;
	u32 cmd;

	QETH_CARD_TEXT(card, 2, "vniccsch");

	/* do not change anything if BridgePort is enabled */
	if (qeth_bridgeport_is_in_use(card))
		return -EBUSY;

	/* check if characteristic and enable/disable are supported */
	if (!(card->options.vnicc.sup_chars & vnicc) ||
	    !(card->options.vnicc.set_char_sup & vnicc))
		return -EOPNOTSUPP;

	/* set enable/disable command and store wanted characteristic */
	if (state) {
		cmd = IPA_VNICC_ENABLE;
		card->options.vnicc.wanted_chars |= vnicc;
	} else {
		cmd = IPA_VNICC_DISABLE;
		card->options.vnicc.wanted_chars &= ~vnicc;
	}

	/* do we need to do anything? */
	if (card->options.vnicc.cur_chars == card->options.vnicc.wanted_chars)
		return rc;

	/* if card is not ready, simply stop here */
	if (!qeth_card_hw_is_reachable(card)) {
		if (state)
			card->options.vnicc.cur_chars |= vnicc;
		else
			card->options.vnicc.cur_chars &= ~vnicc;
		return rc;
	}

	rc = qeth_l2_vnicc_set_char(card, vnicc, cmd);
	if (rc)
		card->options.vnicc.wanted_chars =
			card->options.vnicc.cur_chars;
	else {
		/* successful online VNICC change; handle special cases */
		if (state && vnicc == QETH_VNICC_RX_BCAST)
			card->options.vnicc.rx_bcast_enabled = true;
		if (!state && vnicc == QETH_VNICC_LEARNING)
			qeth_l2_vnicc_recover_timeout(card, vnicc,
					&card->options.vnicc.learning_timeout);
	}

	return rc;
}

/* get current VNICC flag state; called from sysfs show function */
int qeth_l2_vnicc_get_state(struct qeth_card *card, u32 vnicc, bool *state)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 2, "vniccgch");

	/* do not get anything if BridgePort is enabled */
	if (qeth_bridgeport_is_in_use(card))
		return -EBUSY;

	/* check if characteristic is supported */
	if (!(card->options.vnicc.sup_chars & vnicc))
		return -EOPNOTSUPP;

	/* if card is ready, query current VNICC state */
	if (qeth_card_hw_is_reachable(card))
		rc = qeth_l2_vnicc_query_chars(card);

	*state = (card->options.vnicc.cur_chars & vnicc) ? true : false;
	return rc;
}

/* set VNICC timeout; called from sysfs store function. Currently, only learning
 * supports timeout
 */
int qeth_l2_vnicc_set_timeout(struct qeth_card *card, u32 timeout)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 2, "vniccsto");

	/* do not change anything if BridgePort is enabled */
	if (qeth_bridgeport_is_in_use(card))
		return -EBUSY;

	/* check if characteristic and set_timeout are supported */
	if (!(card->options.vnicc.sup_chars & QETH_VNICC_LEARNING) ||
	    !(card->options.vnicc.getset_timeout_sup & QETH_VNICC_LEARNING))
		return -EOPNOTSUPP;

	/* do we need to do anything? */
	if (card->options.vnicc.learning_timeout == timeout)
		return rc;

	/* if card is not ready, simply store the value internally and return */
	if (!qeth_card_hw_is_reachable(card)) {
		card->options.vnicc.learning_timeout = timeout;
		return rc;
	}

	/* send timeout value to card; if successful, store value internally */
	rc = qeth_l2_vnicc_getset_timeout(card, QETH_VNICC_LEARNING,
					  IPA_VNICC_SET_TIMEOUT, &timeout);
	if (!rc)
		card->options.vnicc.learning_timeout = timeout;

	return rc;
}

/* get current VNICC timeout; called from sysfs show function. Currently, only
 * learning supports timeout
 */
int qeth_l2_vnicc_get_timeout(struct qeth_card *card, u32 *timeout)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 2, "vniccgto");

	/* do not get anything if BridgePort is enabled */
	if (qeth_bridgeport_is_in_use(card))
		return -EBUSY;

	/* check if characteristic and get_timeout are supported */
	if (!(card->options.vnicc.sup_chars & QETH_VNICC_LEARNING) ||
	    !(card->options.vnicc.getset_timeout_sup & QETH_VNICC_LEARNING))
		return -EOPNOTSUPP;
	/* if card is ready, get timeout. Otherwise, just return stored value */
	*timeout = card->options.vnicc.learning_timeout;
	if (qeth_card_hw_is_reachable(card))
		rc = qeth_l2_vnicc_getset_timeout(card, QETH_VNICC_LEARNING,
						  IPA_VNICC_GET_TIMEOUT,
						  timeout);

	return rc;
}

/* check if VNICC is currently enabled */
bool qeth_l2_vnicc_is_in_use(struct qeth_card *card)
{
	if (!card->options.vnicc.sup_chars)
		return false;
	/* default values are only OK if rx_bcast was not enabled by user
	 * or the card is offline.
	 */
	if (card->options.vnicc.cur_chars == QETH_VNICC_DEFAULT) {
		if (!card->options.vnicc.rx_bcast_enabled ||
		    !qeth_card_hw_is_reachable(card))
			return false;
	}
	return true;
}

/* recover user timeout setting */
static bool qeth_l2_vnicc_recover_timeout(struct qeth_card *card, u32 vnicc,
					  u32 *timeout)
{
	if (card->options.vnicc.sup_chars & vnicc &&
	    card->options.vnicc.getset_timeout_sup & vnicc &&
	    !qeth_l2_vnicc_getset_timeout(card, vnicc, IPA_VNICC_SET_TIMEOUT,
					  timeout))
		return false;
	*timeout = QETH_VNICC_DEFAULT_TIMEOUT;
	return true;
}

/* recover user characteristic setting */
static bool qeth_l2_vnicc_recover_char(struct qeth_card *card, u32 vnicc,
				       bool enable)
{
	u32 cmd = enable ? IPA_VNICC_ENABLE : IPA_VNICC_DISABLE;

	if (card->options.vnicc.sup_chars & vnicc &&
	    card->options.vnicc.set_char_sup & vnicc &&
	    !qeth_l2_vnicc_set_char(card, vnicc, cmd))
		return false;
	card->options.vnicc.wanted_chars &= ~vnicc;
	card->options.vnicc.wanted_chars |= QETH_VNICC_DEFAULT & vnicc;
	return true;
}

/* (re-)initialize VNICC */
static void qeth_l2_vnicc_init(struct qeth_card *card)
{
	u32 *timeout = &card->options.vnicc.learning_timeout;
	bool enable, error = false;
	unsigned int chars_len, i;
	unsigned long chars_tmp;
	u32 sup_cmds, vnicc;

	QETH_CARD_TEXT(card, 2, "vniccini");
	/* reset rx_bcast */
	card->options.vnicc.rx_bcast_enabled = 0;
	/* initial query and storage of VNIC characteristics */
	if (qeth_l2_vnicc_query_chars(card)) {
		if (card->options.vnicc.wanted_chars != QETH_VNICC_DEFAULT ||
		    *timeout != QETH_VNICC_DEFAULT_TIMEOUT)
			dev_err(&card->gdev->dev, "Configuring the VNIC characteristics failed\n");
		/* fail quietly if user didn't change the default config */
		card->options.vnicc.sup_chars = 0;
		card->options.vnicc.cur_chars = 0;
		card->options.vnicc.wanted_chars = QETH_VNICC_DEFAULT;
		return;
	}
	/* get supported commands for each supported characteristic */
	chars_tmp = card->options.vnicc.sup_chars;
	chars_len = sizeof(card->options.vnicc.sup_chars) * BITS_PER_BYTE;
	for_each_set_bit(i, &chars_tmp, chars_len) {
		vnicc = BIT(i);
		if (qeth_l2_vnicc_query_cmds(card, vnicc, &sup_cmds)) {
			sup_cmds = 0;
			error = true;
		}
		if ((sup_cmds & IPA_VNICC_SET_TIMEOUT) &&
		    (sup_cmds & IPA_VNICC_GET_TIMEOUT))
			card->options.vnicc.getset_timeout_sup |= vnicc;
		else
			card->options.vnicc.getset_timeout_sup &= ~vnicc;
		if ((sup_cmds & IPA_VNICC_ENABLE) &&
		    (sup_cmds & IPA_VNICC_DISABLE))
			card->options.vnicc.set_char_sup |= vnicc;
		else
			card->options.vnicc.set_char_sup &= ~vnicc;
	}
	/* enforce assumed default values and recover settings, if changed  */
	error |= qeth_l2_vnicc_recover_timeout(card, QETH_VNICC_LEARNING,
					       timeout);
	/* Change chars, if necessary  */
	chars_tmp = card->options.vnicc.wanted_chars ^
		    card->options.vnicc.cur_chars;
	chars_len = sizeof(card->options.vnicc.wanted_chars) * BITS_PER_BYTE;
	for_each_set_bit(i, &chars_tmp, chars_len) {
		vnicc = BIT(i);
		enable = card->options.vnicc.wanted_chars & vnicc;
		error |= qeth_l2_vnicc_recover_char(card, vnicc, enable);
	}
	if (error)
		dev_err(&card->gdev->dev, "Configuring the VNIC characteristics failed\n");
}

/* configure default values of VNIC characteristics */
static void qeth_l2_vnicc_set_defaults(struct qeth_card *card)
{
	/* characteristics values */
	card->options.vnicc.sup_chars = QETH_VNICC_ALL;
	card->options.vnicc.cur_chars = QETH_VNICC_DEFAULT;
	card->options.vnicc.learning_timeout = QETH_VNICC_DEFAULT_TIMEOUT;
	/* supported commands */
	card->options.vnicc.set_char_sup = QETH_VNICC_ALL;
	card->options.vnicc.getset_timeout_sup = QETH_VNICC_LEARNING;
	/* settings wanted by users */
	card->options.vnicc.wanted_chars = QETH_VNICC_DEFAULT;
}

module_init(qeth_l2_init);
module_exit(qeth_l2_exit);
MODULE_AUTHOR("Frank Blaschka <frank.blaschka@de.ibm.com>");
MODULE_DESCRIPTION("qeth layer 2 discipline");
MODULE_LICENSE("GPL");
