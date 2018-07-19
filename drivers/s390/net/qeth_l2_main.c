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

static int qeth_l2_set_offline(struct ccwgroup_device *);
static int qeth_l2_stop(struct net_device *);
static void qeth_l2_set_rx_mode(struct net_device *);
static void qeth_bridgeport_query_support(struct qeth_card *card);
static void qeth_bridge_state_change(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd);
static void qeth_bridge_host_event(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd);
static void qeth_l2_vnicc_set_defaults(struct qeth_card *card);
static void qeth_l2_vnicc_init(struct qeth_card *card);
static bool qeth_l2_vnicc_recover_timeout(struct qeth_card *card, u32 vnicc,
					  u32 *timeout);

static struct net_device *qeth_l2_netdev_by_devno(unsigned char *read_dev_no)
{
	struct qeth_card *card;
	struct net_device *ndev;
	__u16 temp_dev_no;
	unsigned long flags;
	struct ccw_dev_id read_devid;

	ndev = NULL;
	memcpy(&temp_dev_no, read_dev_no, 2);
	read_lock_irqsave(&qeth_core_card_list.rwlock, flags);
	list_for_each_entry(card, &qeth_core_card_list.list, list) {
		ccw_device_get_id(CARD_RDEV(card), &read_devid);
		if (read_devid.devno == temp_dev_no) {
			ndev = card->dev;
			break;
		}
	}
	read_unlock_irqrestore(&qeth_core_card_list.rwlock, flags);
	return ndev;
}

static int qeth_setdelmac_makerc(struct qeth_card *card, int retcode)
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
	case -ENOMEM:
		rc = -ENOMEM;
		break;
	default:
		rc = -EIO;
		break;
	}
	return rc;
}

static int qeth_l2_send_setdelmac(struct qeth_card *card, __u8 *mac,
			   enum qeth_ipa_cmds ipacmd)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "L2sdmac");
	iob = qeth_get_ipacmd_buffer(card, ipacmd, QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setdelmac.mac_length = ETH_ALEN;
	ether_addr_copy(cmd->data.setdelmac.mac, mac);
	return qeth_setdelmac_makerc(card, qeth_send_ipa_cmd(card, iob,
					   NULL, NULL));
}

static int qeth_l2_send_setmac(struct qeth_card *card, __u8 *mac)
{
	int rc;

	QETH_CARD_TEXT(card, 2, "L2Setmac");
	rc = qeth_l2_send_setdelmac(card, mac, IPA_CMD_SETVMAC);
	if (rc == 0) {
		dev_info(&card->gdev->dev,
			 "MAC address %pM successfully registered on device %s\n",
			 mac, card->dev->name);
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
		QETH_DBF_MESSAGE(2, "MAC %pM already registered on %s\n",
				 mac, QETH_CARD_IFNAME(card));
	else if (rc)
		QETH_DBF_MESSAGE(2, "Failed to register MAC %pM on %s: %d\n",
				 mac, QETH_CARD_IFNAME(card), rc);
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
		QETH_DBF_MESSAGE(2, "Failed to delete MAC %pM on %s: %d\n",
				 mac, QETH_CARD_IFNAME(card), rc);
	return rc;
}

static void qeth_l2_del_all_macs(struct qeth_card *card)
{
	struct qeth_mac *mac;
	struct hlist_node *tmp;
	int i;

	spin_lock_bh(&card->mclock);
	hash_for_each_safe(card->mac_htable, i, tmp, mac, hnode) {
		hash_del(&mac->hnode);
		kfree(mac);
	}
	spin_unlock_bh(&card->mclock);
}

static int qeth_l2_get_cast_type(struct qeth_card *card, struct sk_buff *skb)
{
	if (card->info.type == QETH_CARD_TYPE_OSN)
		return RTN_UNSPEC;
	if (is_broadcast_ether_addr(skb->data))
		return RTN_BROADCAST;
	if (is_multicast_ether_addr(skb->data))
		return RTN_MULTICAST;
	return RTN_UNSPEC;
}

static void qeth_l2_fill_header(struct qeth_hdr *hdr, struct sk_buff *skb,
				int cast_type, unsigned int data_len)
{
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)skb_mac_header(skb);

	memset(hdr, 0, sizeof(struct qeth_hdr));
	hdr->hdr.l2.id = QETH_HEADER_TYPE_LAYER2;
	hdr->hdr.l2.pkt_length = data_len;

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

static int qeth_setdelvlan_makerc(struct qeth_card *card, int retcode)
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
	case -ENOMEM:
		return -ENOMEM;
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
		QETH_DBF_MESSAGE(2, "Error in processing VLAN %i on %s: 0x%x.\n",
				 cmd->data.setdelvlan.vlan_id,
				 QETH_CARD_IFNAME(card), cmd->hdr.return_code);
		QETH_CARD_TEXT_(card, 2, "L2VL%4x", cmd->hdr.command);
		QETH_CARD_TEXT_(card, 2, "err%d", cmd->hdr.return_code);
	}
	return 0;
}

static int qeth_l2_send_setdelvlan(struct qeth_card *card, __u16 i,
				   enum qeth_ipa_cmds ipacmd)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT_(card, 4, "L2sdv%x", ipacmd);
	iob = qeth_get_ipacmd_buffer(card, ipacmd, QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setdelvlan.vlan_id = i;
	return qeth_setdelvlan_makerc(card, qeth_send_ipa_cmd(card, iob,
					    qeth_l2_send_setdelvlan_cb, NULL));
}

static void qeth_l2_process_vlans(struct qeth_card *card)
{
	struct qeth_vlan_vid *id;

	QETH_CARD_TEXT(card, 3, "L2prcvln");
	mutex_lock(&card->vid_list_mutex);
	list_for_each_entry(id, &card->vid_list, list) {
		qeth_l2_send_setdelvlan(card, id->vid, IPA_CMD_SETVLAN);
	}
	mutex_unlock(&card->vid_list_mutex);
}

static int qeth_l2_vlan_rx_add_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct qeth_card *card = dev->ml_priv;
	struct qeth_vlan_vid *id;
	int rc;

	QETH_CARD_TEXT_(card, 4, "aid:%d", vid);
	if (!vid)
		return 0;
	if (qeth_wait_for_threads(card, QETH_RECOVER_THREAD)) {
		QETH_CARD_TEXT(card, 3, "aidREC");
		return 0;
	}
	id = kmalloc(sizeof(*id), GFP_KERNEL);
	if (id) {
		id->vid = vid;
		rc = qeth_l2_send_setdelvlan(card, vid, IPA_CMD_SETVLAN);
		if (rc) {
			kfree(id);
			return rc;
		}
		mutex_lock(&card->vid_list_mutex);
		list_add_tail(&id->list, &card->vid_list);
		mutex_unlock(&card->vid_list_mutex);
	} else {
		return -ENOMEM;
	}
	return 0;
}

static int qeth_l2_vlan_rx_kill_vid(struct net_device *dev,
				    __be16 proto, u16 vid)
{
	struct qeth_vlan_vid *id, *tmpid = NULL;
	struct qeth_card *card = dev->ml_priv;
	int rc = 0;

	QETH_CARD_TEXT_(card, 4, "kid:%d", vid);
	if (qeth_wait_for_threads(card, QETH_RECOVER_THREAD)) {
		QETH_CARD_TEXT(card, 3, "kidREC");
		return 0;
	}
	mutex_lock(&card->vid_list_mutex);
	list_for_each_entry(id, &card->vid_list, list) {
		if (id->vid == vid) {
			list_del(&id->list);
			tmpid = id;
			break;
		}
	}
	mutex_unlock(&card->vid_list_mutex);
	if (tmpid) {
		rc = qeth_l2_send_setdelvlan(card, vid, IPA_CMD_DELVLAN);
		kfree(tmpid);
	}
	qeth_l2_set_rx_mode(card->dev);
	return rc;
}

static void qeth_l2_stop_card(struct qeth_card *card, int recovery_mode)
{
	QETH_DBF_TEXT(SETUP , 2, "stopcard");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	qeth_set_allowed_threads(card, 0, 1);
	if (card->read.state == CH_STATE_UP &&
	    card->write.state == CH_STATE_UP &&
	    (card->state == CARD_STATE_UP)) {
		if (recovery_mode &&
		    card->info.type != QETH_CARD_TYPE_OSN) {
			qeth_l2_stop(card->dev);
		} else {
			rtnl_lock();
			dev_close(card->dev);
			rtnl_unlock();
		}
		card->info.mac_bits &= ~QETH_LAYER2_MAC_REGISTERED;
		card->state = CARD_STATE_SOFTSETUP;
	}
	if (card->state == CARD_STATE_SOFTSETUP) {
		qeth_l2_del_all_macs(card);
		qeth_clear_ipacmd_list(card);
		card->state = CARD_STATE_HARDSETUP;
	}
	if (card->state == CARD_STATE_HARDSETUP) {
		qeth_qdio_clear_card(card, 0);
		qeth_clear_qdio_buffers(card);
		qeth_clear_working_pool_list(card);
		card->state = CARD_STATE_DOWN;
	}
	if (card->state == CARD_STATE_DOWN) {
		qeth_clear_cmd_buffers(&card->read);
		qeth_clear_cmd_buffers(&card->write);
	}
}

static int qeth_l2_process_inbound_buffer(struct qeth_card *card,
				int budget, int *done)
{
	int work_done = 0;
	struct sk_buff *skb;
	struct qeth_hdr *hdr;
	unsigned int len;

	*done = 0;
	WARN_ON_ONCE(!budget);
	while (budget) {
		skb = qeth_core_get_next_skb(card,
			&card->qdio.in_q->bufs[card->rx.b_index],
			&card->rx.b_element, &card->rx.e_offset, &hdr);
		if (!skb) {
			*done = 1;
			break;
		}
		switch (hdr->hdr.l2.id) {
		case QETH_HEADER_TYPE_LAYER2:
			skb->protocol = eth_type_trans(skb, skb->dev);
			qeth_rx_csum(card, skb, hdr->hdr.l2.flags[1]);
			if (skb->protocol == htons(ETH_P_802_2))
				*((__u32 *)skb->cb) = ++card->seqno.pkt_seqno;
			len = skb->len;
			napi_gro_receive(&card->napi, skb);
			break;
		case QETH_HEADER_TYPE_OSN:
			if (card->info.type == QETH_CARD_TYPE_OSN) {
				skb_push(skb, sizeof(struct qeth_hdr));
				skb_copy_to_linear_data(skb, hdr,
						sizeof(struct qeth_hdr));
				len = skb->len;
				card->osn_info.data_cb(skb);
				break;
			}
			/* else unknown */
		default:
			dev_kfree_skb_any(skb);
			QETH_CARD_TEXT(card, 3, "inbunkno");
			QETH_DBF_HEX(CTRL, 3, hdr, QETH_DBF_CTRL_LEN);
			continue;
		}
		work_done++;
		budget--;
		card->stats.rx_packets++;
		card->stats.rx_bytes += len;
	}
	return work_done;
}

static int qeth_l2_request_initial_mac(struct qeth_card *card)
{
	int rc = 0;

	QETH_DBF_TEXT(SETUP, 2, "l2reqmac");
	QETH_DBF_TEXT_(SETUP, 2, "doL2%s", CARD_BUS_ID(card));

	if (MACHINE_IS_VM) {
		rc = qeth_vm_request_mac(card);
		if (!rc)
			goto out;
		QETH_DBF_MESSAGE(2, "z/VM MAC Service failed on device %s: x%x\n",
				 CARD_BUS_ID(card), rc);
		QETH_DBF_TEXT_(SETUP, 2, "err%04x", rc);
		/* fall back to alternative mechanism: */
	}

	if (card->info.type == QETH_CARD_TYPE_IQD ||
	    card->info.type == QETH_CARD_TYPE_OSM ||
	    card->info.type == QETH_CARD_TYPE_OSX ||
	    card->info.guestlan) {
		rc = qeth_setadpparms_change_macaddr(card);
		if (!rc)
			goto out;
		QETH_DBF_MESSAGE(2, "READ_MAC Assist failed on device %s: x%x\n",
				 CARD_BUS_ID(card), rc);
		QETH_DBF_TEXT_(SETUP, 2, "1err%04x", rc);
		/* fall back once more: */
	}

	/* some devices don't support a custom MAC address: */
	if (card->info.type == QETH_CARD_TYPE_OSM ||
	    card->info.type == QETH_CARD_TYPE_OSX)
		return (rc) ? rc : -EADDRNOTAVAIL;
	eth_hw_addr_random(card->dev);

out:
	QETH_DBF_HEX(SETUP, 2, card->dev->dev_addr, card->dev->addr_len);
	return 0;
}

static int qeth_l2_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct qeth_card *card = dev->ml_priv;
	u8 old_addr[ETH_ALEN];
	int rc = 0;

	QETH_CARD_TEXT(card, 3, "setmac");

	if (card->info.type == QETH_CARD_TYPE_OSN ||
	    card->info.type == QETH_CARD_TYPE_OSM ||
	    card->info.type == QETH_CARD_TYPE_OSX) {
		QETH_CARD_TEXT(card, 3, "setmcTYP");
		return -EOPNOTSUPP;
	}
	QETH_CARD_HEX(card, 3, addr->sa_data, ETH_ALEN);
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (qeth_wait_for_threads(card, QETH_RECOVER_THREAD)) {
		QETH_CARD_TEXT(card, 3, "setmcREC");
		return -ERESTARTSYS;
	}

	/* avoid racing against concurrent state change: */
	if (!mutex_trylock(&card->conf_mutex))
		return -EAGAIN;

	if (!qeth_card_hw_is_reachable(card)) {
		ether_addr_copy(dev->dev_addr, addr->sa_data);
		goto out_unlock;
	}

	/* don't register the same address twice */
	if (ether_addr_equal_64bits(dev->dev_addr, addr->sa_data) &&
	    (card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))
		goto out_unlock;

	/* add the new address, switch over, drop the old */
	rc = qeth_l2_send_setmac(card, addr->sa_data);
	if (rc)
		goto out_unlock;
	ether_addr_copy(old_addr, dev->dev_addr);
	ether_addr_copy(dev->dev_addr, addr->sa_data);

	if (card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED)
		qeth_l2_remove_mac(card, old_addr);
	card->info.mac_bits |= QETH_LAYER2_MAC_REGISTERED;

out_unlock:
	mutex_unlock(&card->conf_mutex);
	return rc;
}

static void qeth_promisc_to_bridge(struct qeth_card *card)
{
	struct net_device *dev = card->dev;
	enum qeth_ipa_promisc_modes promisc_mode;
	int role;
	int rc;

	QETH_CARD_TEXT(card, 3, "pmisc2br");

	if (!card->options.sbp.reflect_promisc)
		return;
	promisc_mode = (dev->flags & IFF_PROMISC) ? SET_PROMISC_MODE_ON
						: SET_PROMISC_MODE_OFF;
	if (promisc_mode == card->info.promisc_mode)
		return;

	if (promisc_mode == SET_PROMISC_MODE_ON) {
		if (card->options.sbp.reflect_promisc_primary)
			role = QETH_SBP_ROLE_PRIMARY;
		else
			role = QETH_SBP_ROLE_SECONDARY;
	} else
		role = QETH_SBP_ROLE_NONE;

	rc = qeth_bridgeport_setrole(card, role);
	QETH_DBF_TEXT_(SETUP, 2, "bpm%c%04x",
			(promisc_mode == SET_PROMISC_MODE_ON) ? '+' : '-', rc);
	if (!rc) {
		card->options.sbp.role = role;
		card->info.promisc_mode = promisc_mode;
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

static void qeth_l2_set_rx_mode(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	struct netdev_hw_addr *ha;
	struct qeth_mac *mac;
	struct hlist_node *tmp;
	int i;
	int rc;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return;

	QETH_CARD_TEXT(card, 3, "setmulti");
	if (qeth_threads_running(card, QETH_RECOVER_THREAD) &&
	    (card->state != CARD_STATE_UP))
		return;

	spin_lock_bh(&card->mclock);

	netdev_for_each_mc_addr(ha, dev)
		qeth_l2_add_mac(card, ha);
	netdev_for_each_uc_addr(ha, dev)
		qeth_l2_add_mac(card, ha);

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

	spin_unlock_bh(&card->mclock);

	if (qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE))
		qeth_setadp_promisc_mode(card);
	else
		qeth_promisc_to_bridge(card);
}

static int qeth_l2_xmit_iqd(struct qeth_card *card, struct sk_buff *skb,
			    struct qeth_qdio_out_q *queue, int cast_type)
{
	unsigned int data_offset = ETH_HLEN;
	struct qeth_hdr *hdr;
	int rc;

	hdr = kmem_cache_alloc(qeth_core_header_cache, GFP_ATOMIC);
	if (!hdr)
		return -ENOMEM;
	qeth_l2_fill_header(hdr, skb, cast_type, skb->len);
	skb_copy_from_linear_data(skb, ((char *)hdr) + sizeof(*hdr),
				  data_offset);

	if (!qeth_get_elements_no(card, skb, 1, data_offset)) {
		rc = -E2BIG;
		goto out;
	}
	rc = qeth_do_send_packet_fast(queue, skb, hdr, data_offset,
				      sizeof(*hdr) + data_offset);
out:
	if (rc)
		kmem_cache_free(qeth_core_header_cache, hdr);
	return rc;
}

static int qeth_l2_xmit_osa(struct qeth_card *card, struct sk_buff *skb,
			    struct qeth_qdio_out_q *queue, int cast_type,
			    int ipv)
{
	int push_len = sizeof(struct qeth_hdr);
	unsigned int elements, nr_frags;
	unsigned int hdr_elements = 0;
	struct qeth_hdr *hdr = NULL;
	unsigned int hd_len = 0;
	int rc;

	/* fix hardware limitation: as long as we do not have sbal
	 * chaining we can not send long frag lists
	 */
	if (!qeth_get_elements_no(card, skb, 0, 0)) {
		rc = skb_linearize(skb);

		if (card->options.performance_stats) {
			if (rc)
				card->perf_stats.tx_linfail++;
			else
				card->perf_stats.tx_lin++;
		}
		if (rc)
			return rc;
	}
	nr_frags = skb_shinfo(skb)->nr_frags;

	rc = skb_cow_head(skb, push_len);
	if (rc)
		return rc;
	push_len = qeth_push_hdr(skb, &hdr, push_len);
	if (push_len < 0)
		return push_len;
	if (!push_len) {
		/* hdr was allocated from cache */
		hd_len = sizeof(*hdr);
		hdr_elements = 1;
	}
	qeth_l2_fill_header(hdr, skb, cast_type, skb->len - push_len);
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		qeth_tx_csum(skb, &hdr->hdr.l2.flags[1], ipv);
		if (card->options.performance_stats)
			card->perf_stats.tx_csum++;
	}

	elements = qeth_get_elements_no(card, skb, hdr_elements, 0);
	if (!elements) {
		rc = -E2BIG;
		goto out;
	}
	elements += hdr_elements;

	/* TODO: remove the skb_orphan() once TX completion is fast enough */
	skb_orphan(skb);
	rc = qeth_do_send_packet(card, queue, skb, hdr, 0, hd_len, elements);
out:
	if (!rc) {
		if (card->options.performance_stats && nr_frags) {
			card->perf_stats.sg_skbs_sent++;
			/* nr_frags + skb->data */
			card->perf_stats.sg_frags_sent += nr_frags + 1;
		}
	} else {
		if (hd_len)
			kmem_cache_free(qeth_core_header_cache, hdr);
		if (rc == -EBUSY)
			/* roll back to ETH header */
			skb_pull(skb, push_len);
	}
	return rc;
}

static int qeth_l2_xmit_osn(struct qeth_card *card, struct sk_buff *skb,
			    struct qeth_qdio_out_q *queue)
{
	unsigned int elements;
	struct qeth_hdr *hdr;

	if (skb->protocol == htons(ETH_P_IPV6))
		return -EPROTONOSUPPORT;

	hdr = (struct qeth_hdr *)skb->data;
	elements = qeth_get_elements_no(card, skb, 0, 0);
	if (!elements)
		return -E2BIG;
	if (qeth_hdr_chk_and_bounce(skb, &hdr, sizeof(*hdr)))
		return -EINVAL;
	return qeth_do_send_packet(card, queue, skb, hdr, 0, 0, elements);
}

static netdev_tx_t qeth_l2_hard_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	int cast_type = qeth_l2_get_cast_type(card, skb);
	int ipv = qeth_get_ip_version(skb);
	struct qeth_qdio_out_q *queue;
	int tx_bytes = skb->len;
	int rc;

	if (card->qdio.do_prio_queueing || (cast_type &&
					card->info.is_multicast_different))
		queue = card->qdio.out_qs[qeth_get_priority_queue(card, skb,
					ipv, cast_type)];
	else
		queue = card->qdio.out_qs[card->qdio.default_out_queue];

	if ((card->state != CARD_STATE_UP) || !card->lan_online) {
		card->stats.tx_carrier_errors++;
		goto tx_drop;
	}

	if (card->options.performance_stats) {
		card->perf_stats.outbound_cnt++;
		card->perf_stats.outbound_start_time = qeth_get_micros();
	}
	netif_stop_queue(dev);

	switch (card->info.type) {
	case QETH_CARD_TYPE_OSN:
		rc = qeth_l2_xmit_osn(card, skb, queue);
		break;
	case QETH_CARD_TYPE_IQD:
		rc = qeth_l2_xmit_iqd(card, skb, queue, cast_type);
		break;
	default:
		rc = qeth_l2_xmit_osa(card, skb, queue, cast_type, ipv);
	}

	if (!rc) {
		card->stats.tx_packets++;
		card->stats.tx_bytes += tx_bytes;
		if (card->options.performance_stats)
			card->perf_stats.outbound_time += qeth_get_micros() -
				card->perf_stats.outbound_start_time;
		netif_wake_queue(dev);
		return NETDEV_TX_OK;
	} else if (rc == -EBUSY) {
		return NETDEV_TX_BUSY;
	} /* else fall through */

tx_drop:
	card->stats.tx_dropped++;
	card->stats.tx_errors++;
	dev_kfree_skb_any(skb);
	netif_wake_queue(dev);
	return NETDEV_TX_OK;
}

static int __qeth_l2_open(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	int rc = 0;

	QETH_CARD_TEXT(card, 4, "qethopen");
	if (card->state == CARD_STATE_UP)
		return rc;
	if (card->state != CARD_STATE_SOFTSETUP)
		return -ENODEV;

	if ((card->info.type != QETH_CARD_TYPE_OSN) &&
	     (!(card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))) {
		QETH_CARD_TEXT(card, 4, "nomacadr");
		return -EPERM;
	}
	card->data.state = CH_STATE_UP;
	card->state = CARD_STATE_UP;
	netif_start_queue(dev);

	if (qdio_stop_irq(card->data.ccwdev, 0) >= 0) {
		napi_enable(&card->napi);
		napi_schedule(&card->napi);
	} else
		rc = -EIO;
	return rc;
}

static int qeth_l2_open(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT(card, 5, "qethope_");
	if (qeth_wait_for_threads(card, QETH_RECOVER_THREAD)) {
		QETH_CARD_TEXT(card, 3, "openREC");
		return -ERESTARTSYS;
	}
	return __qeth_l2_open(dev);
}

static int qeth_l2_stop(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT(card, 4, "qethstop");
	netif_tx_disable(dev);
	if (card->state == CARD_STATE_UP) {
		card->state = CARD_STATE_SOFTSETUP;
		napi_disable(&card->napi);
	}
	return 0;
}

static const struct device_type qeth_l2_devtype = {
	.name = "qeth_layer2",
	.groups = qeth_l2_attr_groups,
};

static int qeth_l2_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc;

	if (gdev->dev.type == &qeth_generic_devtype) {
		rc = qeth_l2_create_device_attributes(&gdev->dev);
		if (rc)
			return rc;
	}
	INIT_LIST_HEAD(&card->vid_list);
	hash_init(card->mac_htable);
	card->options.layer2 = 1;
	card->info.hwtrap = 0;
	qeth_l2_vnicc_set_defaults(card);
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
		qeth_l2_set_offline(cgdev);

	if (card->dev) {
		unregister_netdev(card->dev);
		free_netdev(card->dev);
		card->dev = NULL;
	}
	return;
}

static const struct ethtool_ops qeth_l2_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_strings = qeth_core_get_strings,
	.get_ethtool_stats = qeth_core_get_ethtool_stats,
	.get_sset_count = qeth_core_get_sset_count,
	.get_drvinfo = qeth_core_get_drvinfo,
	.get_link_ksettings = qeth_core_ethtool_get_link_ksettings,
};

static const struct ethtool_ops qeth_l2_osn_ops = {
	.get_strings = qeth_core_get_strings,
	.get_ethtool_stats = qeth_core_get_ethtool_stats,
	.get_sset_count = qeth_core_get_sset_count,
	.get_drvinfo = qeth_core_get_drvinfo,
};

static const struct net_device_ops qeth_l2_netdev_ops = {
	.ndo_open		= qeth_l2_open,
	.ndo_stop		= qeth_l2_stop,
	.ndo_get_stats		= qeth_get_stats,
	.ndo_start_xmit		= qeth_l2_hard_start_xmit,
	.ndo_features_check	= qeth_features_check,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= qeth_l2_set_rx_mode,
	.ndo_do_ioctl		= qeth_do_ioctl,
	.ndo_set_mac_address    = qeth_l2_set_mac_address,
	.ndo_change_mtu	   	= qeth_change_mtu,
	.ndo_vlan_rx_add_vid	= qeth_l2_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = qeth_l2_vlan_rx_kill_vid,
	.ndo_tx_timeout	   	= qeth_tx_timeout,
	.ndo_fix_features	= qeth_fix_features,
	.ndo_set_features	= qeth_set_features
};

static int qeth_l2_setup_netdev(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_IQD:
		card->dev = alloc_netdev(0, "hsi%d", NET_NAME_UNKNOWN,
					 ether_setup);
		break;
	case QETH_CARD_TYPE_OSN:
		card->dev = alloc_netdev(0, "osn%d", NET_NAME_UNKNOWN,
					 ether_setup);
		break;
	default:
		card->dev = alloc_etherdev(0);
	}

	if (!card->dev)
		return -ENODEV;

	card->dev->ml_priv = card;
	card->dev->priv_flags |= IFF_UNICAST_FLT;
	card->dev->watchdog_timeo = QETH_TX_TIMEOUT;
	card->dev->mtu = card->info.initial_mtu;
	card->dev->min_mtu = 64;
	card->dev->max_mtu = ETH_MAX_MTU;
	card->dev->dev_port = card->info.portno;
	card->dev->netdev_ops = &qeth_l2_netdev_ops;
	if (card->info.type == QETH_CARD_TYPE_OSN) {
		card->dev->ethtool_ops = &qeth_l2_osn_ops;
		card->dev->flags |= IFF_NOARP;
	} else {
		card->dev->ethtool_ops = &qeth_l2_ethtool_ops;
	}

	if (card->info.type == QETH_CARD_TYPE_OSM)
		card->dev->features |= NETIF_F_VLAN_CHALLENGED;
	else
		card->dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	if (card->info.type != QETH_CARD_TYPE_OSN &&
	    card->info.type != QETH_CARD_TYPE_IQD) {
		card->dev->priv_flags &= ~IFF_TX_SKB_SHARING;
		card->dev->needed_headroom = sizeof(struct qeth_hdr);
		card->dev->hw_features |= NETIF_F_SG;
		card->dev->vlan_features |= NETIF_F_SG;
	}

	if (card->info.type == QETH_CARD_TYPE_OSD && !card->info.guestlan) {
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

	card->info.broadcast_capable = 1;
	qeth_l2_request_initial_mac(card);
	SET_NETDEV_DEV(card->dev, &card->gdev->dev);
	netif_napi_add(card->dev, &card->napi, qeth_poll, QETH_NAPI_WEIGHT);
	netif_carrier_off(card->dev);
	return register_netdev(card->dev);
}

static int qeth_l2_start_ipassists(struct qeth_card *card)
{
	/* configure isolation level */
	if (qeth_set_access_ctrl_online(card, 0))
		return -ENODEV;
	return 0;
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

static int __qeth_l2_set_online(struct ccwgroup_device *gdev, int recovery_mode)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc = 0;
	enum qeth_card_states recover_flag;

	mutex_lock(&card->discipline_mutex);
	mutex_lock(&card->conf_mutex);
	QETH_DBF_TEXT(SETUP, 2, "setonlin");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	recover_flag = card->state;
	rc = qeth_core_hardsetup_card(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "2err%04x", rc);
		rc = -ENODEV;
		goto out_remove;
	}
	qeth_bridgeport_query_support(card);
	if (card->options.sbp.supported_funcs)
		dev_info(&card->gdev->dev,
		"The device represents a Bridge Capable Port\n");

	if (!card->dev && qeth_l2_setup_netdev(card)) {
		rc = -ENODEV;
		goto out_remove;
	}

	if (card->info.type != QETH_CARD_TYPE_OSN &&
	    !qeth_l2_send_setmac(card, card->dev->dev_addr))
		card->info.mac_bits |= QETH_LAYER2_MAC_REGISTERED;

	if (qeth_is_diagass_supported(card, QETH_DIAGS_CMD_TRAP)) {
		if (card->info.hwtrap &&
		    qeth_hw_trap(card, QETH_DIAGS_TRAP_ARM))
			card->info.hwtrap = 0;
	} else
		card->info.hwtrap = 0;

	/* for the rx_bcast characteristic, init VNICC after setmac */
	qeth_l2_vnicc_init(card);

	qeth_trace_features(card);
	qeth_l2_trace_features(card);

	qeth_l2_setup_bridgeport_attrs(card);

	card->state = CARD_STATE_HARDSETUP;
	qeth_print_status_message(card);

	/* softsetup */
	QETH_DBF_TEXT(SETUP, 2, "softsetp");

	if ((card->info.type == QETH_CARD_TYPE_OSD) ||
	    (card->info.type == QETH_CARD_TYPE_OSX)) {
		rc = qeth_l2_start_ipassists(card);
		if (rc)
			goto out_remove;
	}

	if (card->info.type != QETH_CARD_TYPE_OSN)
		qeth_l2_process_vlans(card);

	netif_tx_disable(card->dev);

	rc = qeth_init_qdio_queues(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "6err%d", rc);
		rc = -ENODEV;
		goto out_remove;
	}
	card->state = CARD_STATE_SOFTSETUP;
	if (card->lan_online)
		netif_carrier_on(card->dev);
	else
		netif_carrier_off(card->dev);

	qeth_set_allowed_threads(card, 0xffffffff, 0);

	qeth_enable_hw_features(card->dev);
	if (recover_flag == CARD_STATE_RECOVER) {
		if (recovery_mode &&
		    card->info.type != QETH_CARD_TYPE_OSN) {
			__qeth_l2_open(card->dev);
		} else {
			rtnl_lock();
			dev_open(card->dev);
			rtnl_unlock();
		}
		/* this also sets saved unicast addresses */
		qeth_l2_set_rx_mode(card->dev);
	}
	/* let user_space know that device is online */
	kobject_uevent(&gdev->dev.kobj, KOBJ_CHANGE);
	mutex_unlock(&card->conf_mutex);
	mutex_unlock(&card->discipline_mutex);
	return 0;

out_remove:
	qeth_l2_stop_card(card, 0);
	ccw_device_set_offline(CARD_DDEV(card));
	ccw_device_set_offline(CARD_WDEV(card));
	ccw_device_set_offline(CARD_RDEV(card));
	qdio_free(CARD_DDEV(card));
	if (recover_flag == CARD_STATE_RECOVER)
		card->state = CARD_STATE_RECOVER;
	else
		card->state = CARD_STATE_DOWN;
	mutex_unlock(&card->conf_mutex);
	mutex_unlock(&card->discipline_mutex);
	return rc;
}

static int qeth_l2_set_online(struct ccwgroup_device *gdev)
{
	return __qeth_l2_set_online(gdev, 0);
}

static int __qeth_l2_set_offline(struct ccwgroup_device *cgdev,
					int recovery_mode)
{
	struct qeth_card *card = dev_get_drvdata(&cgdev->dev);
	int rc = 0, rc2 = 0, rc3 = 0;
	enum qeth_card_states recover_flag;

	mutex_lock(&card->discipline_mutex);
	mutex_lock(&card->conf_mutex);
	QETH_DBF_TEXT(SETUP, 3, "setoffl");
	QETH_DBF_HEX(SETUP, 3, &card, sizeof(void *));

	if (card->dev && netif_carrier_ok(card->dev))
		netif_carrier_off(card->dev);
	recover_flag = card->state;
	if ((!recovery_mode && card->info.hwtrap) || card->info.hwtrap == 2) {
		qeth_hw_trap(card, QETH_DIAGS_TRAP_DISARM);
		card->info.hwtrap = 1;
	}
	qeth_l2_stop_card(card, recovery_mode);
	rc  = ccw_device_set_offline(CARD_DDEV(card));
	rc2 = ccw_device_set_offline(CARD_WDEV(card));
	rc3 = ccw_device_set_offline(CARD_RDEV(card));
	if (!rc)
		rc = (rc2) ? rc2 : rc3;
	if (rc)
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
	qdio_free(CARD_DDEV(card));
	if (recover_flag == CARD_STATE_UP)
		card->state = CARD_STATE_RECOVER;
	/* let user_space know that device is offline */
	kobject_uevent(&cgdev->dev.kobj, KOBJ_CHANGE);
	mutex_unlock(&card->conf_mutex);
	mutex_unlock(&card->discipline_mutex);
	return 0;
}

static int qeth_l2_set_offline(struct ccwgroup_device *cgdev)
{
	return __qeth_l2_set_offline(cgdev, 0);
}

static int qeth_l2_recover(void *ptr)
{
	struct qeth_card *card;
	int rc = 0;

	card = (struct qeth_card *) ptr;
	QETH_CARD_TEXT(card, 2, "recover1");
	if (!qeth_do_run_thread(card, QETH_RECOVER_THREAD))
		return 0;
	QETH_CARD_TEXT(card, 2, "recover2");
	dev_warn(&card->gdev->dev,
		"A recovery process has been started for the device\n");
	qeth_set_recovery_task(card);
	__qeth_l2_set_offline(card->gdev, 1);
	rc = __qeth_l2_set_online(card->gdev, 1);
	if (!rc)
		dev_info(&card->gdev->dev,
			"Device successfully recovered!\n");
	else {
		qeth_close_dev(card);
		dev_warn(&card->gdev->dev, "The qeth device driver "
				"failed to recover an error on the device\n");
	}
	qeth_clear_recovery_task(card);
	qeth_clear_thread_start_bit(card, QETH_RECOVER_THREAD);
	qeth_clear_thread_running_bit(card, QETH_RECOVER_THREAD);
	return 0;
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

static int qeth_l2_pm_suspend(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);

	if (card->dev)
		netif_device_detach(card->dev);
	qeth_set_allowed_threads(card, 0, 1);
	wait_event(card->wait_q, qeth_threads_running(card, 0xffffffff) == 0);
	if (gdev->state == CCWGROUP_OFFLINE)
		return 0;
	if (card->state == CARD_STATE_UP) {
		if (card->info.hwtrap)
			qeth_hw_trap(card, QETH_DIAGS_TRAP_DISARM);
		__qeth_l2_set_offline(card->gdev, 1);
	} else
		__qeth_l2_set_offline(card->gdev, 0);
	return 0;
}

static int qeth_l2_pm_resume(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc = 0;

	if (gdev->state == CCWGROUP_OFFLINE)
		goto out;

	if (card->state == CARD_STATE_RECOVER) {
		rc = __qeth_l2_set_online(card->gdev, 1);
		if (rc) {
			rtnl_lock();
			dev_close(card->dev);
			rtnl_unlock();
		}
	} else
		rc = __qeth_l2_set_online(card->gdev, 0);
out:
	qeth_set_allowed_threads(card, 0xffffffff, 0);
	if (card->dev)
		netif_device_attach(card->dev);
	if (rc)
		dev_warn(&card->gdev->dev, "The qeth device driver "
			"failed to recover an error on the device\n");
	return rc;
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
	.process_rx_buffer = qeth_l2_process_inbound_buffer,
	.recover = qeth_l2_recover,
	.setup = qeth_l2_probe_device,
	.remove = qeth_l2_remove_device,
	.set_online = qeth_l2_set_online,
	.set_offline = qeth_l2_set_offline,
	.freeze = qeth_l2_pm_suspend,
	.thaw = qeth_l2_pm_resume,
	.restore = qeth_l2_pm_resume,
	.do_ioctl = NULL,
	.control_event_handler = qeth_l2_control_event,
};
EXPORT_SYMBOL_GPL(qeth_l2_discipline);

static int qeth_osn_send_control_data(struct qeth_card *card, int len,
			   struct qeth_cmd_buffer *iob)
{
	unsigned long flags;
	int rc = 0;

	QETH_CARD_TEXT(card, 5, "osndctrd");

	wait_event(card->wait_q,
		   atomic_cmpxchg(&card->write.irq_pending, 0, 1) == 0);
	qeth_prepare_control_data(card, len, iob);
	QETH_CARD_TEXT(card, 6, "osnoirqp");
	spin_lock_irqsave(get_ccwdev_lock(card->write.ccwdev), flags);
	rc = ccw_device_start_timeout(CARD_WDEV(card), &card->write.ccw,
				      (addr_t) iob, 0, 0, QETH_IPA_TIMEOUT);
	spin_unlock_irqrestore(get_ccwdev_lock(card->write.ccwdev), flags);
	if (rc) {
		QETH_DBF_MESSAGE(2, "qeth_osn_send_control_data: "
			   "ccw_device_start rc = %i\n", rc);
		QETH_CARD_TEXT_(card, 2, " err%d", rc);
		qeth_release_buffer(iob->channel, iob);
		atomic_set(&card->write.irq_pending, 0);
		wake_up(&card->wait_q);
	}
	return rc;
}

static int qeth_osn_send_ipa_cmd(struct qeth_card *card,
			struct qeth_cmd_buffer *iob, int data_len)
{
	u16 s1, s2;

	QETH_CARD_TEXT(card, 4, "osndipa");

	qeth_prepare_ipa_cmd(card, iob, QETH_PROT_OSN2);
	s1 = (u16)(IPA_PDU_HEADER_SIZE + data_len);
	s2 = (u16)data_len;
	memcpy(QETH_IPA_PDU_LEN_TOTAL(iob->data), &s1, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU1(iob->data), &s2, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU2(iob->data), &s2, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU3(iob->data), &s2, 2);
	return qeth_osn_send_control_data(card, s1, iob);
}

int qeth_osn_assist(struct net_device *dev, void *data, int data_len)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_card *card;

	if (!dev)
		return -ENODEV;
	card = dev->ml_priv;
	if (!card)
		return -ENODEV;
	QETH_CARD_TEXT(card, 2, "osnsdmc");
	if (!qeth_card_hw_is_reachable(card))
		return -ENODEV;
	iob = qeth_wait_for_buffer(&card->write);
	memcpy(__ipa_cmd(iob), data, data_len);
	return qeth_osn_send_ipa_cmd(card, iob, data_len);
}
EXPORT_SYMBOL(qeth_osn_assist);

int qeth_osn_register(unsigned char *read_dev_no, struct net_device **dev,
		  int (*assist_cb)(struct net_device *, void *),
		  int (*data_cb)(struct sk_buff *))
{
	struct qeth_card *card;

	*dev = qeth_l2_netdev_by_devno(read_dev_no);
	if (*dev == NULL)
		return -ENODEV;
	card = (*dev)->ml_priv;
	if (!card)
		return -ENODEV;
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
	mutex_lock(&data->card->conf_mutex);
	data->card->options.sbp.role = entry->role;
	mutex_unlock(&data->card->conf_mutex);

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
	queue_work(qeth_wq, &data->worker);
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
		mutex_lock(&data->card->conf_mutex);
		data->card->options.sbp.hostnotification = 0;
		mutex_unlock(&data->card->conf_mutex);
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
	queue_work(qeth_wq, &data->worker);
}

/* SETBRIDGEPORT support; sending commands */

struct _qeth_sbp_cbctl {
	u16 ipa_rc;
	u16 cmd_rc;
	union {
		u32 supported;
		struct {
			enum qeth_sbp_roles *role;
			enum qeth_sbp_states *state;
		} qports;
	} data;
};

/**
 * qeth_bridgeport_makerc() - derive "traditional" error from hardware codes.
 * @card:		      qeth_card structure pointer, for debug messages.
 * @cbctl:		      state structure with hardware return codes.
 * @setcmd:		      IPA command code
 *
 * Returns negative errno-compatible error indication or 0 on success.
 */
static int qeth_bridgeport_makerc(struct qeth_card *card,
	struct _qeth_sbp_cbctl *cbctl, enum qeth_ipa_sbp_cmd setcmd)
{
	int rc;
	int is_iqd = (card->info.type == QETH_CARD_TYPE_IQD);

	if ((is_iqd && (cbctl->ipa_rc == IPA_RC_SUCCESS)) ||
	    (!is_iqd && (cbctl->ipa_rc == cbctl->cmd_rc)))
		switch (cbctl->cmd_rc) {
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
	else
		switch (cbctl->ipa_rc) {
		case IPA_RC_NOTSUPP:
			rc = -EOPNOTSUPP;
			break;
		case IPA_RC_UNSUPPORTED_COMMAND:
			rc = -EOPNOTSUPP;
			break;
		default:
			rc = -EIO;
		}

	if (rc) {
		QETH_CARD_TEXT_(card, 2, "SBPi%04x", cbctl->ipa_rc);
		QETH_CARD_TEXT_(card, 2, "SBPc%04x", cbctl->cmd_rc);
	}
	return rc;
}

static struct qeth_cmd_buffer *qeth_sbp_build_cmd(struct qeth_card *card,
						  enum qeth_ipa_sbp_cmd sbp_cmd,
						  unsigned int cmd_length)
{
	enum qeth_ipa_cmds ipa_cmd = (card->info.type == QETH_CARD_TYPE_IQD) ?
					IPA_CMD_SETBRIDGEPORT_IQD :
					IPA_CMD_SETBRIDGEPORT_OSA;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	iob = qeth_get_ipacmd_buffer(card, ipa_cmd, 0);
	if (!iob)
		return iob;
	cmd = __ipa_cmd(iob);
	cmd->data.sbp.hdr.cmdlength = sizeof(struct qeth_ipacmd_sbp_hdr) +
				      cmd_length;
	cmd->data.sbp.hdr.command_code = sbp_cmd;
	cmd->data.sbp.hdr.used_total = 1;
	cmd->data.sbp.hdr.seq_no = 1;
	return iob;
}

static int qeth_bridgeport_query_support_cb(struct qeth_card *card,
	struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;
	struct _qeth_sbp_cbctl *cbctl = (struct _qeth_sbp_cbctl *)reply->param;
	QETH_CARD_TEXT(card, 2, "brqsupcb");
	cbctl->ipa_rc = cmd->hdr.return_code;
	cbctl->cmd_rc = cmd->data.sbp.hdr.return_code;
	if ((cbctl->ipa_rc == 0) && (cbctl->cmd_rc == 0)) {
		cbctl->data.supported =
			cmd->data.sbp.data.query_cmds_supp.supported_cmds;
	} else {
		cbctl->data.supported = 0;
	}
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
				 sizeof(struct qeth_sbp_query_cmds_supp));
	if (!iob)
		return;
	if (qeth_send_ipa_cmd(card, iob, qeth_bridgeport_query_support_cb,
							(void *)&cbctl) ||
	    qeth_bridgeport_makerc(card, &cbctl,
					IPA_SBP_QUERY_COMMANDS_SUPPORTED)) {
		/* non-zero makerc signifies failure, and produce messages */
		card->options.sbp.role = QETH_SBP_ROLE_NONE;
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

	QETH_CARD_TEXT(card, 2, "brqprtcb");
	cbctl->ipa_rc = cmd->hdr.return_code;
	cbctl->cmd_rc = cmd->data.sbp.hdr.return_code;
	if ((cbctl->ipa_rc != 0) || (cbctl->cmd_rc != 0))
		return 0;
	if (qports->entry_length != sizeof(struct qeth_sbp_port_entry)) {
		cbctl->cmd_rc = 0xffff;
		QETH_CARD_TEXT_(card, 2, "SBPs%04x", qports->entry_length);
		return 0;
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
	int rc = 0;
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
	rc = qeth_send_ipa_cmd(card, iob, qeth_bridgeport_query_ports_cb,
				(void *)&cbctl);
	if (rc < 0)
		return rc;
	return qeth_bridgeport_makerc(card, &cbctl, IPA_SBP_QUERY_BRIDGE_PORTS);
}
EXPORT_SYMBOL_GPL(qeth_bridgeport_query_ports);

static int qeth_bridgeport_set_cb(struct qeth_card *card,
	struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *)data;
	struct _qeth_sbp_cbctl *cbctl = (struct _qeth_sbp_cbctl *)reply->param;
	QETH_CARD_TEXT(card, 2, "brsetrcb");
	cbctl->ipa_rc = cmd->hdr.return_code;
	cbctl->cmd_rc = cmd->data.sbp.hdr.return_code;
	return 0;
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
	int rc = 0;
	int cmdlength;
	struct qeth_cmd_buffer *iob;
	struct _qeth_sbp_cbctl cbctl;
	enum qeth_ipa_sbp_cmd setcmd;

	QETH_CARD_TEXT(card, 2, "brsetrol");
	switch (role) {
	case QETH_SBP_ROLE_NONE:
		setcmd = IPA_SBP_RESET_BRIDGE_PORT_ROLE;
		cmdlength = sizeof(struct qeth_sbp_reset_role);
		break;
	case QETH_SBP_ROLE_PRIMARY:
		setcmd = IPA_SBP_SET_PRIMARY_BRIDGE_PORT;
		cmdlength = sizeof(struct qeth_sbp_set_primary);
		break;
	case QETH_SBP_ROLE_SECONDARY:
		setcmd = IPA_SBP_SET_SECONDARY_BRIDGE_PORT;
		cmdlength = sizeof(struct qeth_sbp_set_secondary);
		break;
	default:
		return -EINVAL;
	}
	if (!(card->options.sbp.supported_funcs & setcmd))
		return -EOPNOTSUPP;
	iob = qeth_sbp_build_cmd(card, setcmd, cmdlength);
	if (!iob)
		return -ENOMEM;
	rc = qeth_send_ipa_cmd(card, iob, qeth_bridgeport_set_cb,
				(void *)&cbctl);
	if (rc < 0)
		return rc;
	return qeth_bridgeport_makerc(card, &cbctl, setcmd);
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
	if (l2entry->addr_lnid.lnid)
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
EXPORT_SYMBOL_GPL(qeth_bridgeport_an_set);

static bool qeth_bridgeport_is_in_use(struct qeth_card *card)
{
	return (card->options.sbp.role || card->options.sbp.reflect_promisc ||
		card->options.sbp.hostnotification);
}

/* VNIC Characteristics support */

/* handle VNICC IPA command return codes; convert to error codes */
static int qeth_l2_vnicc_makerc(struct qeth_card *card, int ipa_rc)
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
	u32 sub_cmd;
	struct {
		u32 vnic_char;
		u32 timeout;
	} param;
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

	QETH_CARD_TEXT(card, 2, "vniccrcb");
	if (cmd->hdr.return_code)
		return 0;
	/* return results to caller */
	card->options.vnicc.sup_chars = rep->hdr.sup;
	card->options.vnicc.cur_chars = rep->hdr.cur;

	if (cbctl->sub_cmd == IPA_VNICC_QUERY_CMDS)
		*cbctl->result.sup_cmds = rep->query_cmds.sup_cmds;

	if (cbctl->sub_cmd == IPA_VNICC_GET_TIMEOUT)
		*cbctl->result.timeout = rep->getset_timeout.timeout;

	return 0;
}

/* generic VNICC request */
static int qeth_l2_vnicc_request(struct qeth_card *card,
				 struct _qeth_l2_vnicc_request_cbctl *cbctl)
{
	struct qeth_ipacmd_vnicc *req;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	int rc;

	QETH_CARD_TEXT(card, 2, "vniccreq");

	/* get new buffer for request */
	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_VNICC, 0);
	if (!iob)
		return -ENOMEM;

	/* create header for request */
	cmd = __ipa_cmd(iob);
	req = &cmd->data.vnicc;

	/* create sub command header for request */
	req->sub_hdr.data_length = sizeof(req->sub_hdr);
	req->sub_hdr.sub_command = cbctl->sub_cmd;

	/* create sub command specific request fields */
	switch (cbctl->sub_cmd) {
	case IPA_VNICC_QUERY_CHARS:
		break;
	case IPA_VNICC_QUERY_CMDS:
		req->sub_hdr.data_length += sizeof(req->query_cmds);
		req->query_cmds.vnic_char = cbctl->param.vnic_char;
		break;
	case IPA_VNICC_ENABLE:
	case IPA_VNICC_DISABLE:
		req->sub_hdr.data_length += sizeof(req->set_char);
		req->set_char.vnic_char = cbctl->param.vnic_char;
		break;
	case IPA_VNICC_SET_TIMEOUT:
		req->getset_timeout.timeout = cbctl->param.timeout;
		/* fallthrough */
	case IPA_VNICC_GET_TIMEOUT:
		req->sub_hdr.data_length += sizeof(req->getset_timeout);
		req->getset_timeout.vnic_char = cbctl->param.vnic_char;
		break;
	default:
		qeth_release_buffer(iob->channel, iob);
		return -EOPNOTSUPP;
	}

	/* send request */
	rc = qeth_send_ipa_cmd(card, iob, qeth_l2_vnicc_request_cb,
			       (void *) cbctl);

	return qeth_l2_vnicc_makerc(card, rc);
}

/* VNICC query VNIC characteristics request */
static int qeth_l2_vnicc_query_chars(struct qeth_card *card)
{
	struct _qeth_l2_vnicc_request_cbctl cbctl;

	/* prepare callback control */
	cbctl.sub_cmd = IPA_VNICC_QUERY_CHARS;

	QETH_CARD_TEXT(card, 2, "vniccqch");
	return qeth_l2_vnicc_request(card, &cbctl);
}

/* VNICC query sub commands request */
static int qeth_l2_vnicc_query_cmds(struct qeth_card *card, u32 vnic_char,
				    u32 *sup_cmds)
{
	struct _qeth_l2_vnicc_request_cbctl cbctl;

	/* prepare callback control */
	cbctl.sub_cmd = IPA_VNICC_QUERY_CMDS;
	cbctl.param.vnic_char = vnic_char;
	cbctl.result.sup_cmds = sup_cmds;

	QETH_CARD_TEXT(card, 2, "vniccqcm");
	return qeth_l2_vnicc_request(card, &cbctl);
}

/* VNICC enable/disable characteristic request */
static int qeth_l2_vnicc_set_char(struct qeth_card *card, u32 vnic_char,
				      u32 cmd)
{
	struct _qeth_l2_vnicc_request_cbctl cbctl;

	/* prepare callback control */
	cbctl.sub_cmd = cmd;
	cbctl.param.vnic_char = vnic_char;

	QETH_CARD_TEXT(card, 2, "vniccedc");
	return qeth_l2_vnicc_request(card, &cbctl);
}

/* VNICC get/set timeout for characteristic request */
static int qeth_l2_vnicc_getset_timeout(struct qeth_card *card, u32 vnicc,
					u32 cmd, u32 *timeout)
{
	struct _qeth_l2_vnicc_request_cbctl cbctl;

	/* prepare callback control */
	cbctl.sub_cmd = cmd;
	cbctl.param.vnic_char = vnicc;
	if (cmd == IPA_VNICC_SET_TIMEOUT)
		cbctl.param.timeout = *timeout;
	if (cmd == IPA_VNICC_GET_TIMEOUT)
		cbctl.result.timeout = timeout;

	QETH_CARD_TEXT(card, 2, "vniccgst");
	return qeth_l2_vnicc_request(card, &cbctl);
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
	/* if everything is turned off, VNICC is not active */
	if (!card->options.vnicc.cur_chars)
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
	unsigned int chars_len, i;
	unsigned long chars_tmp;
	u32 sup_cmds, vnicc;
	bool enable, error;

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
		qeth_l2_vnicc_query_cmds(card, vnicc, &sup_cmds);
		if (!(sup_cmds & IPA_VNICC_SET_TIMEOUT) ||
		    !(sup_cmds & IPA_VNICC_GET_TIMEOUT))
			card->options.vnicc.getset_timeout_sup &= ~vnicc;
		if (!(sup_cmds & IPA_VNICC_ENABLE) ||
		    !(sup_cmds & IPA_VNICC_DISABLE))
			card->options.vnicc.set_char_sup &= ~vnicc;
	}
	/* enforce assumed default values and recover settings, if changed  */
	error = qeth_l2_vnicc_recover_timeout(card, QETH_VNICC_LEARNING,
					      timeout);
	chars_tmp = card->options.vnicc.wanted_chars ^ QETH_VNICC_DEFAULT;
	chars_tmp |= QETH_VNICC_BRIDGE_INVISIBLE;
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
