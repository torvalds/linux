/*
 *  drivers/s390/net/qeth_l2_main.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/ip.h>

#include <asm/s390_rdev.h>

#include "qeth_core.h"
#include "qeth_core_offl.h"

#define QETH_DBF_TXT_BUF qeth_l2_dbf_txt_buf
static DEFINE_PER_CPU(char[256], qeth_l2_dbf_txt_buf);

static int qeth_l2_set_offline(struct ccwgroup_device *);
static int qeth_l2_stop(struct net_device *);
static int qeth_l2_send_delmac(struct qeth_card *, __u8 *);
static int qeth_l2_send_setdelmac(struct qeth_card *, __u8 *,
			   enum qeth_ipa_cmds,
			   int (*reply_cb) (struct qeth_card *,
					    struct qeth_reply*,
					    unsigned long));
static void qeth_l2_set_multicast_list(struct net_device *);
static int qeth_l2_recover(void *);

static int qeth_l2_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct qeth_card *card = netdev_priv(dev);
	struct mii_ioctl_data *mii_data;
	int rc = 0;

	if (!card)
		return -ENODEV;

	if ((card->state != CARD_STATE_UP) &&
		(card->state != CARD_STATE_SOFTSETUP))
		return -ENODEV;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return -EPERM;

	switch (cmd) {
	case SIOC_QETH_ADP_SET_SNMP_CONTROL:
		rc = qeth_snmp_command(card, rq->ifr_ifru.ifru_data);
		break;
	case SIOC_QETH_GET_CARD_TYPE:
		if ((card->info.type == QETH_CARD_TYPE_OSAE) &&
		    !card->info.guestlan)
			return 1;
		return 0;
		break;
	case SIOCGMIIPHY:
		mii_data = if_mii(rq);
		mii_data->phy_id = 0;
		break;
	case SIOCGMIIREG:
		mii_data = if_mii(rq);
		if (mii_data->phy_id != 0)
			rc = -EINVAL;
		else
			mii_data->val_out = qeth_mdio_read(dev,
				mii_data->phy_id, mii_data->reg_num);
		break;
	default:
		rc = -EOPNOTSUPP;
	}
	if (rc)
		QETH_DBF_TEXT_(TRACE, 2, "ioce%d", rc);
	return rc;
}

static int qeth_l2_verify_dev(struct net_device *dev)
{
	struct qeth_card *card;
	unsigned long flags;
	int rc = 0;

	read_lock_irqsave(&qeth_core_card_list.rwlock, flags);
	list_for_each_entry(card, &qeth_core_card_list.list, list) {
		if (card->dev == dev) {
			rc = QETH_REAL_CARD;
			break;
		}
	}
	read_unlock_irqrestore(&qeth_core_card_list.rwlock, flags);

	return rc;
}

static struct net_device *qeth_l2_netdev_by_devno(unsigned char *read_dev_no)
{
	struct qeth_card *card;
	struct net_device *ndev;
	unsigned char *readno;
	__u16 temp_dev_no, card_dev_no;
	char *endp;
	unsigned long flags;

	ndev = NULL;
	memcpy(&temp_dev_no, read_dev_no, 2);
	read_lock_irqsave(&qeth_core_card_list.rwlock, flags);
	list_for_each_entry(card, &qeth_core_card_list.list, list) {
		readno = CARD_RDEV_ID(card);
		readno += (strlen(readno) - 4);
		card_dev_no = simple_strtoul(readno, &endp, 16);
		if (card_dev_no == temp_dev_no) {
			ndev = card->dev;
			break;
		}
	}
	read_unlock_irqrestore(&qeth_core_card_list.rwlock, flags);
	return ndev;
}

static int qeth_l2_send_setgroupmac_cb(struct qeth_card *card,
				struct qeth_reply *reply,
				unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	__u8 *mac;

	QETH_DBF_TEXT(TRACE, 2, "L2Sgmacb");
	cmd = (struct qeth_ipa_cmd *) data;
	mac = &cmd->data.setdelmac.mac[0];
	/* MAC already registered, needed in couple/uncouple case */
	if (cmd->hdr.return_code == 0x2005) {
		PRINT_WARN("Group MAC %02x:%02x:%02x:%02x:%02x:%02x " \
			  "already existing on %s \n",
			  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			  QETH_CARD_IFNAME(card));
		cmd->hdr.return_code = 0;
	}
	if (cmd->hdr.return_code)
		PRINT_ERR("Could not set group MAC " \
			  "%02x:%02x:%02x:%02x:%02x:%02x on %s: %x\n",
			  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			  QETH_CARD_IFNAME(card), cmd->hdr.return_code);
	return 0;
}

static int qeth_l2_send_setgroupmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(TRACE, 2, "L2Sgmac");
	return qeth_l2_send_setdelmac(card, mac, IPA_CMD_SETGMAC,
					  qeth_l2_send_setgroupmac_cb);
}

static int qeth_l2_send_delgroupmac_cb(struct qeth_card *card,
				struct qeth_reply *reply,
				unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	__u8 *mac;

	QETH_DBF_TEXT(TRACE, 2, "L2Dgmacb");
	cmd = (struct qeth_ipa_cmd *) data;
	mac = &cmd->data.setdelmac.mac[0];
	if (cmd->hdr.return_code)
		PRINT_ERR("Could not delete group MAC " \
			  "%02x:%02x:%02x:%02x:%02x:%02x on %s: %x\n",
			  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			  QETH_CARD_IFNAME(card), cmd->hdr.return_code);
	return 0;
}

static int qeth_l2_send_delgroupmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(TRACE, 2, "L2Dgmac");
	return qeth_l2_send_setdelmac(card, mac, IPA_CMD_DELGMAC,
					  qeth_l2_send_delgroupmac_cb);
}

static void qeth_l2_add_mc(struct qeth_card *card, __u8 *mac)
{
	struct qeth_mc_mac *mc;

	mc = kmalloc(sizeof(struct qeth_mc_mac), GFP_ATOMIC);

	if (!mc) {
		PRINT_ERR("no mem vor mc mac address\n");
		return;
	}

	memcpy(mc->mc_addr, mac, OSA_ADDR_LEN);
	mc->mc_addrlen = OSA_ADDR_LEN;

	if (!qeth_l2_send_setgroupmac(card, mac))
		list_add_tail(&mc->list, &card->mc_list);
	else
		kfree(mc);
}

static void qeth_l2_del_all_mc(struct qeth_card *card)
{
	struct qeth_mc_mac *mc, *tmp;

	spin_lock_bh(&card->mclock);
	list_for_each_entry_safe(mc, tmp, &card->mc_list, list) {
		qeth_l2_send_delgroupmac(card, mc->mc_addr);
		list_del(&mc->list);
		kfree(mc);
	}
	spin_unlock_bh(&card->mclock);
}

static void qeth_l2_get_packet_type(struct qeth_card *card,
			struct qeth_hdr *hdr, struct sk_buff *skb)
{
	__u16 hdr_mac;

	if (!memcmp(skb->data + QETH_HEADER_SIZE,
		    skb->dev->broadcast, 6)) {
		/* broadcast? */
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_BROADCAST;
		return;
	}
	hdr_mac = *((__u16 *)skb->data);
	/* tr multicast? */
	switch (card->info.link_type) {
	case QETH_LINK_TYPE_HSTR:
	case QETH_LINK_TYPE_LANE_TR:
		if ((hdr_mac == QETH_TR_MAC_NC) ||
		    (hdr_mac == QETH_TR_MAC_C))
			hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_MULTICAST;
		else
			hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_UNICAST;
		break;
		/* eth or so multicast? */
	default:
		if ((hdr_mac == QETH_ETH_MAC_V4) ||
		     (hdr_mac == QETH_ETH_MAC_V6))
			hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_MULTICAST;
		else
			hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_UNICAST;
	}
}

static void qeth_l2_fill_header(struct qeth_card *card, struct qeth_hdr *hdr,
			struct sk_buff *skb, int ipv, int cast_type)
{
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)((skb->data) +
					QETH_HEADER_SIZE);

	memset(hdr, 0, sizeof(struct qeth_hdr));
	hdr->hdr.l2.id = QETH_HEADER_TYPE_LAYER2;

	/* set byte byte 3 to casting flags */
	if (cast_type == RTN_MULTICAST)
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_MULTICAST;
	else if (cast_type == RTN_BROADCAST)
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_BROADCAST;
	else
		qeth_l2_get_packet_type(card, hdr, skb);

	hdr->hdr.l2.pkt_length = skb->len-QETH_HEADER_SIZE;
	/* VSWITCH relies on the VLAN
	 * information to be present in
	 * the QDIO header */
	if (veth->h_vlan_proto == __constant_htons(ETH_P_8021Q)) {
		hdr->hdr.l2.flags[2] |= QETH_LAYER2_FLAG_VLAN;
		hdr->hdr.l2.vlan_id = ntohs(veth->h_vlan_TCI);
	}
}

static int qeth_l2_send_setdelvlan_cb(struct qeth_card *card,
			struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 2, "L2sdvcb");
	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code) {
		PRINT_ERR("Error in processing VLAN %i on %s: 0x%x. "
			  "Continuing\n", cmd->data.setdelvlan.vlan_id,
			  QETH_CARD_IFNAME(card), cmd->hdr.return_code);
		QETH_DBF_TEXT_(TRACE, 2, "L2VL%4x", cmd->hdr.command);
		QETH_DBF_TEXT_(TRACE, 2, "L2%s", CARD_BUS_ID(card));
		QETH_DBF_TEXT_(TRACE, 2, "err%d", cmd->hdr.return_code);
	}
	return 0;
}

static int qeth_l2_send_setdelvlan(struct qeth_card *card, __u16 i,
				enum qeth_ipa_cmds ipacmd)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT_(TRACE, 4, "L2sdv%x", ipacmd);
	iob = qeth_get_ipacmd_buffer(card, ipacmd, QETH_PROT_IPV4);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setdelvlan.vlan_id = i;
	return qeth_send_ipa_cmd(card, iob,
				 qeth_l2_send_setdelvlan_cb, NULL);
}

static void qeth_l2_process_vlans(struct qeth_card *card, int clear)
{
	struct qeth_vlan_vid *id;
	QETH_DBF_TEXT(TRACE, 3, "L2prcvln");
	spin_lock_bh(&card->vlanlock);
	list_for_each_entry(id, &card->vid_list, list) {
		if (clear)
			qeth_l2_send_setdelvlan(card, id->vid,
				IPA_CMD_DELVLAN);
		else
			qeth_l2_send_setdelvlan(card, id->vid,
				IPA_CMD_SETVLAN);
	}
	spin_unlock_bh(&card->vlanlock);
}

static void qeth_l2_vlan_rx_add_vid(struct net_device *dev, unsigned short vid)
{
	struct qeth_card *card = netdev_priv(dev);
	struct qeth_vlan_vid *id;

	QETH_DBF_TEXT_(TRACE, 4, "aid:%d", vid);
	id = kmalloc(sizeof(struct qeth_vlan_vid), GFP_ATOMIC);
	if (id) {
		id->vid = vid;
		qeth_l2_send_setdelvlan(card, vid, IPA_CMD_SETVLAN);
		spin_lock_bh(&card->vlanlock);
		list_add_tail(&id->list, &card->vid_list);
		spin_unlock_bh(&card->vlanlock);
	} else {
		PRINT_ERR("no memory for vid\n");
	}
}

static void qeth_l2_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct qeth_vlan_vid *id, *tmpid = NULL;
	struct qeth_card *card = netdev_priv(dev);

	QETH_DBF_TEXT_(TRACE, 4, "kid:%d", vid);
	spin_lock_bh(&card->vlanlock);
	list_for_each_entry(id, &card->vid_list, list) {
		if (id->vid == vid) {
			list_del(&id->list);
			tmpid = id;
			break;
		}
	}
	spin_unlock_bh(&card->vlanlock);
	if (tmpid) {
		qeth_l2_send_setdelvlan(card, vid, IPA_CMD_DELVLAN);
		kfree(tmpid);
	}
	qeth_l2_set_multicast_list(card->dev);
}

static int qeth_l2_stop_card(struct qeth_card *card, int recovery_mode)
{
	int rc = 0;

	QETH_DBF_TEXT(SETUP , 2, "stopcard");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	qeth_set_allowed_threads(card, 0, 1);
	if (qeth_wait_for_threads(card, ~QETH_RECOVER_THREAD))
		return -ERESTARTSYS;
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
		if (!card->use_hard_stop) {
			__u8 *mac = &card->dev->dev_addr[0];
			rc = qeth_l2_send_delmac(card, mac);
			QETH_DBF_TEXT_(SETUP, 2, "Lerr%d", rc);
		}
		card->state = CARD_STATE_SOFTSETUP;
	}
	if (card->state == CARD_STATE_SOFTSETUP) {
		qeth_l2_process_vlans(card, 1);
		qeth_l2_del_all_mc(card);
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
	card->use_hard_stop = 0;
	return rc;
}

static void qeth_l2_process_inbound_buffer(struct qeth_card *card,
			    struct qeth_qdio_buffer *buf, int index)
{
	struct qdio_buffer_element *element;
	struct sk_buff *skb;
	struct qeth_hdr *hdr;
	int offset;
	unsigned int len;

	/* get first element of current buffer */
	element = (struct qdio_buffer_element *)&buf->buffer->element[0];
	offset = 0;
	if (card->options.performance_stats)
		card->perf_stats.bufs_rec++;
	while ((skb = qeth_core_get_next_skb(card, buf->buffer, &element,
				       &offset, &hdr))) {
		skb->dev = card->dev;
		/* is device UP ? */
		if (!(card->dev->flags & IFF_UP)) {
			dev_kfree_skb_any(skb);
			continue;
		}

		switch (hdr->hdr.l2.id) {
		case QETH_HEADER_TYPE_LAYER2:
			skb->pkt_type = PACKET_HOST;
			skb->protocol = eth_type_trans(skb, skb->dev);
			if (card->options.checksum_type == NO_CHECKSUMMING)
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			else
				skb->ip_summed = CHECKSUM_NONE;
			if (skb->protocol == htons(ETH_P_802_2))
				*((__u32 *)skb->cb) = ++card->seqno.pkt_seqno;
			len = skb->len;
			netif_rx(skb);
			break;
		case QETH_HEADER_TYPE_OSN:
			skb_push(skb, sizeof(struct qeth_hdr));
			skb_copy_to_linear_data(skb, hdr,
						sizeof(struct qeth_hdr));
			len = skb->len;
			card->osn_info.data_cb(skb);
			break;
		default:
			dev_kfree_skb_any(skb);
			QETH_DBF_TEXT(TRACE, 3, "inbunkno");
			QETH_DBF_HEX(CTRL, 3, hdr, QETH_DBF_CTRL_LEN);
			continue;
		}
		card->dev->last_rx = jiffies;
		card->stats.rx_packets++;
		card->stats.rx_bytes += len;
	}
}

static int qeth_l2_send_setdelmac(struct qeth_card *card, __u8 *mac,
			   enum qeth_ipa_cmds ipacmd,
			   int (*reply_cb) (struct qeth_card *,
					    struct qeth_reply*,
					    unsigned long))
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(TRACE, 2, "L2sdmac");
	iob = qeth_get_ipacmd_buffer(card, ipacmd, QETH_PROT_IPV4);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setdelmac.mac_length = OSA_ADDR_LEN;
	memcpy(&cmd->data.setdelmac.mac, mac, OSA_ADDR_LEN);
	return qeth_send_ipa_cmd(card, iob, reply_cb, NULL);
}

static int qeth_l2_send_setmac_cb(struct qeth_card *card,
			   struct qeth_reply *reply,
			   unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 2, "L2Smaccb");
	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(TRACE, 2, "L2er%x", cmd->hdr.return_code);
		card->info.mac_bits &= ~QETH_LAYER2_MAC_REGISTERED;
		cmd->hdr.return_code = -EIO;
	} else {
		card->info.mac_bits |= QETH_LAYER2_MAC_REGISTERED;
		memcpy(card->dev->dev_addr, cmd->data.setdelmac.mac,
		       OSA_ADDR_LEN);
		PRINT_INFO("MAC address %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
			   "successfully registered on device %s\n",
			   card->dev->dev_addr[0], card->dev->dev_addr[1],
			   card->dev->dev_addr[2], card->dev->dev_addr[3],
			   card->dev->dev_addr[4], card->dev->dev_addr[5],
			   card->dev->name);
	}
	return 0;
}

static int qeth_l2_send_setmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(TRACE, 2, "L2Setmac");
	return qeth_l2_send_setdelmac(card, mac, IPA_CMD_SETVMAC,
					  qeth_l2_send_setmac_cb);
}

static int qeth_l2_send_delmac_cb(struct qeth_card *card,
			   struct qeth_reply *reply,
			   unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 2, "L2Dmaccb");
	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(TRACE, 2, "err%d", cmd->hdr.return_code);
		cmd->hdr.return_code = -EIO;
		return 0;
	}
	card->info.mac_bits &= ~QETH_LAYER2_MAC_REGISTERED;

	return 0;
}

static int qeth_l2_send_delmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(TRACE, 2, "L2Delmac");
	if (!(card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))
		return 0;
	return qeth_l2_send_setdelmac(card, mac, IPA_CMD_DELVMAC,
					  qeth_l2_send_delmac_cb);
}

static int qeth_l2_request_initial_mac(struct qeth_card *card)
{
	int rc = 0;
	char vendor_pre[] = {0x02, 0x00, 0x00};

	QETH_DBF_TEXT(SETUP, 2, "doL2init");
	QETH_DBF_TEXT_(SETUP, 2, "doL2%s", CARD_BUS_ID(card));

	rc = qeth_query_setadapterparms(card);
	if (rc) {
		PRINT_WARN("could not query adapter parameters on device %s: "
			   "x%x\n", CARD_BUS_ID(card), rc);
	}

	if (card->info.guestlan) {
		rc = qeth_setadpparms_change_macaddr(card);
		if (rc) {
			PRINT_WARN("couldn't get MAC address on "
			   "device %s: x%x\n",
			   CARD_BUS_ID(card), rc);
			QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
			return rc;
		}
		QETH_DBF_HEX(SETUP, 2, card->dev->dev_addr, OSA_ADDR_LEN);
	} else {
		random_ether_addr(card->dev->dev_addr);
		memcpy(card->dev->dev_addr, vendor_pre, 3);
	}
	return 0;
}

static int qeth_l2_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct qeth_card *card = netdev_priv(dev);
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 3, "setmac");

	if (qeth_l2_verify_dev(dev) != QETH_REAL_CARD) {
		QETH_DBF_TEXT(TRACE, 3, "setmcINV");
		return -EOPNOTSUPP;
	}

	if (card->info.type == QETH_CARD_TYPE_OSN) {
		PRINT_WARN("Setting MAC address on %s is not supported.\n",
			   dev->name);
		QETH_DBF_TEXT(TRACE, 3, "setmcOSN");
		return -EOPNOTSUPP;
	}
	QETH_DBF_TEXT_(TRACE, 3, "%s", CARD_BUS_ID(card));
	QETH_DBF_HEX(TRACE, 3, addr->sa_data, OSA_ADDR_LEN);
	rc = qeth_l2_send_delmac(card, &card->dev->dev_addr[0]);
	if (!rc)
		rc = qeth_l2_send_setmac(card, addr->sa_data);
	return rc;
}

static void qeth_l2_set_multicast_list(struct net_device *dev)
{
	struct qeth_card *card = netdev_priv(dev);
	struct dev_mc_list *dm;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return ;

	QETH_DBF_TEXT(TRACE, 3, "setmulti");
	qeth_l2_del_all_mc(card);
	spin_lock_bh(&card->mclock);
	for (dm = dev->mc_list; dm; dm = dm->next)
		qeth_l2_add_mc(card, dm->dmi_addr);
	spin_unlock_bh(&card->mclock);
	if (!qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE))
		return;
	qeth_setadp_promisc_mode(card);
}

static int qeth_l2_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int rc;
	struct qeth_hdr *hdr = NULL;
	int elements = 0;
	struct qeth_card *card = netdev_priv(dev);
	struct sk_buff *new_skb = skb;
	int ipv = qeth_get_ip_version(skb);
	int cast_type = qeth_get_cast_type(card, skb);
	struct qeth_qdio_out_q *queue = card->qdio.out_qs
		[qeth_get_priority_queue(card, skb, ipv, cast_type)];
	int tx_bytes = skb->len;
	enum qeth_large_send_types large_send = QETH_LARGE_SEND_NO;
	struct qeth_eddp_context *ctx = NULL;

	QETH_DBF_TEXT(TRACE, 6, "l2xmit");

	if ((card->state != CARD_STATE_UP) || !card->lan_online) {
		card->stats.tx_carrier_errors++;
		goto tx_drop;
	}

	if ((card->info.type == QETH_CARD_TYPE_OSN) &&
	    (skb->protocol == htons(ETH_P_IPV6)))
		goto tx_drop;

	if (card->options.performance_stats) {
		card->perf_stats.outbound_cnt++;
		card->perf_stats.outbound_start_time = qeth_get_micros();
	}
	netif_stop_queue(dev);

	if (skb_is_gso(skb))
		large_send = QETH_LARGE_SEND_EDDP;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		hdr = (struct qeth_hdr *)skb->data;
	else {
		new_skb = qeth_prepare_skb(card, skb, &hdr);
		if (!new_skb)
			goto tx_drop;
		qeth_l2_fill_header(card, hdr, new_skb, ipv, cast_type);
	}

	if (large_send == QETH_LARGE_SEND_EDDP) {
		ctx = qeth_eddp_create_context(card, new_skb, hdr,
						skb->sk->sk_protocol);
		if (ctx == NULL) {
			PRINT_WARN("could not create eddp context\n");
			goto tx_drop;
		}
	} else {
		elements = qeth_get_elements_no(card, (void *)hdr, new_skb, 0);
		if (!elements)
			goto tx_drop;
	}

	if ((large_send == QETH_LARGE_SEND_NO) &&
	    (skb->ip_summed == CHECKSUM_PARTIAL))
		qeth_tx_csum(new_skb);

	if (card->info.type != QETH_CARD_TYPE_IQD)
		rc = qeth_do_send_packet(card, queue, new_skb, hdr,
					 elements, ctx);
	else
		rc = qeth_do_send_packet_fast(card, queue, new_skb, hdr,
					      elements, ctx);
	if (!rc) {
		card->stats.tx_packets++;
		card->stats.tx_bytes += tx_bytes;
		if (new_skb != skb)
			dev_kfree_skb_any(skb);
		if (card->options.performance_stats) {
			if (large_send != QETH_LARGE_SEND_NO) {
				card->perf_stats.large_send_bytes += tx_bytes;
				card->perf_stats.large_send_cnt++;
			}
			if (skb_shinfo(new_skb)->nr_frags > 0) {
				card->perf_stats.sg_skbs_sent++;
				/* nr_frags + skb->data */
				card->perf_stats.sg_frags_sent +=
					skb_shinfo(new_skb)->nr_frags + 1;
			}
		}

		if (ctx != NULL) {
			qeth_eddp_put_context(ctx);
			dev_kfree_skb_any(new_skb);
		}
	} else {
		if (ctx != NULL)
			qeth_eddp_put_context(ctx);

		if (rc == -EBUSY) {
			if (new_skb != skb)
				dev_kfree_skb_any(new_skb);
			return NETDEV_TX_BUSY;
		} else
			goto tx_drop;
	}

	netif_wake_queue(dev);
	if (card->options.performance_stats)
		card->perf_stats.outbound_time += qeth_get_micros() -
			card->perf_stats.outbound_start_time;
	return rc;

tx_drop:
	card->stats.tx_dropped++;
	card->stats.tx_errors++;
	if ((new_skb != skb) && new_skb)
		dev_kfree_skb_any(new_skb);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void qeth_l2_qdio_input_handler(struct ccw_device *ccwdev,
			unsigned int status, unsigned int qdio_err,
			unsigned int siga_err, unsigned int queue,
			int first_element, int count, unsigned long card_ptr)
{
	struct net_device *net_dev;
	struct qeth_card *card;
	struct qeth_qdio_buffer *buffer;
	int index;
	int i;

	QETH_DBF_TEXT(TRACE, 6, "qdinput");
	card = (struct qeth_card *) card_ptr;
	net_dev = card->dev;
	if (card->options.performance_stats) {
		card->perf_stats.inbound_cnt++;
		card->perf_stats.inbound_start_time = qeth_get_micros();
	}
	if (status & QDIO_STATUS_LOOK_FOR_ERROR) {
		if (status & QDIO_STATUS_ACTIVATE_CHECK_CONDITION) {
			QETH_DBF_TEXT(TRACE, 1, "qdinchk");
			QETH_DBF_TEXT_(TRACE, 1, "%s", CARD_BUS_ID(card));
			QETH_DBF_TEXT_(TRACE, 1, "%04X%04X", first_element,
					count);
			QETH_DBF_TEXT_(TRACE, 1, "%04X%04X", queue, status);
			qeth_schedule_recovery(card);
			return;
		}
	}
	for (i = first_element; i < (first_element + count); ++i) {
		index = i % QDIO_MAX_BUFFERS_PER_Q;
		buffer = &card->qdio.in_q->bufs[index];
		if (!((status & QDIO_STATUS_LOOK_FOR_ERROR) &&
		      qeth_check_qdio_errors(buffer->buffer,
					     qdio_err, siga_err, "qinerr")))
			qeth_l2_process_inbound_buffer(card, buffer, index);
		/* clear buffer and give back to hardware */
		qeth_put_buffer_pool_entry(card, buffer->pool_entry);
		qeth_queue_input_buffer(card, index);
	}
	if (card->options.performance_stats)
		card->perf_stats.inbound_time += qeth_get_micros() -
			card->perf_stats.inbound_start_time;
}

static int qeth_l2_open(struct net_device *dev)
{
	struct qeth_card *card = netdev_priv(dev);

	QETH_DBF_TEXT(TRACE, 4, "qethopen");
	if (card->state != CARD_STATE_SOFTSETUP)
		return -ENODEV;

	if ((card->info.type != QETH_CARD_TYPE_OSN) &&
	     (!(card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))) {
		QETH_DBF_TEXT(TRACE, 4, "nomacadr");
		return -EPERM;
	}
	card->data.state = CH_STATE_UP;
	card->state = CARD_STATE_UP;
	card->dev->flags |= IFF_UP;
	netif_start_queue(dev);

	if (!card->lan_online && netif_carrier_ok(dev))
		netif_carrier_off(dev);
	return 0;
}


static int qeth_l2_stop(struct net_device *dev)
{
	struct qeth_card *card = netdev_priv(dev);

	QETH_DBF_TEXT(TRACE, 4, "qethstop");
	netif_tx_disable(dev);
	card->dev->flags &= ~IFF_UP;
	if (card->state == CARD_STATE_UP)
		card->state = CARD_STATE_SOFTSETUP;
	return 0;
}

static int qeth_l2_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);

	INIT_LIST_HEAD(&card->vid_list);
	INIT_LIST_HEAD(&card->mc_list);
	card->options.layer2 = 1;
	card->discipline.input_handler = (qdio_handler_t *)
		qeth_l2_qdio_input_handler;
	card->discipline.output_handler = (qdio_handler_t *)
		qeth_qdio_output_handler;
	card->discipline.recover = qeth_l2_recover;
	return 0;
}

static void qeth_l2_remove_device(struct ccwgroup_device *cgdev)
{
	struct qeth_card *card = dev_get_drvdata(&cgdev->dev);

	wait_event(card->wait_q, qeth_threads_running(card, 0xffffffff) == 0);

	if (cgdev->state == CCWGROUP_ONLINE) {
		card->use_hard_stop = 1;
		qeth_l2_set_offline(cgdev);
	}

	if (card->dev) {
		unregister_netdev(card->dev);
		card->dev = NULL;
	}

	qeth_l2_del_all_mc(card);
	return;
}

static struct ethtool_ops qeth_l2_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_hw_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_strings = qeth_core_get_strings,
	.get_ethtool_stats = qeth_core_get_ethtool_stats,
	.get_stats_count = qeth_core_get_stats_count,
	.get_drvinfo = qeth_core_get_drvinfo,
};

static struct ethtool_ops qeth_l2_osn_ops = {
	.get_strings = qeth_core_get_strings,
	.get_ethtool_stats = qeth_core_get_ethtool_stats,
	.get_stats_count = qeth_core_get_stats_count,
	.get_drvinfo = qeth_core_get_drvinfo,
};

static int qeth_l2_setup_netdev(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_OSAE:
		card->dev = alloc_etherdev(0);
		break;
	case QETH_CARD_TYPE_IQD:
		card->dev = alloc_netdev(0, "hsi%d", ether_setup);
		break;
	case QETH_CARD_TYPE_OSN:
		card->dev = alloc_netdev(0, "osn%d", ether_setup);
		card->dev->flags |= IFF_NOARP;
		break;
	default:
		card->dev = alloc_etherdev(0);
	}

	if (!card->dev)
		return -ENODEV;

	card->dev->priv = card;
	card->dev->tx_timeout = &qeth_tx_timeout;
	card->dev->watchdog_timeo = QETH_TX_TIMEOUT;
	card->dev->open = qeth_l2_open;
	card->dev->stop = qeth_l2_stop;
	card->dev->hard_start_xmit = qeth_l2_hard_start_xmit;
	card->dev->do_ioctl = qeth_l2_do_ioctl;
	card->dev->get_stats = qeth_get_stats;
	card->dev->change_mtu = qeth_change_mtu;
	card->dev->set_multicast_list = qeth_l2_set_multicast_list;
	card->dev->vlan_rx_kill_vid = qeth_l2_vlan_rx_kill_vid;
	card->dev->vlan_rx_add_vid = qeth_l2_vlan_rx_add_vid;
	card->dev->set_mac_address = qeth_l2_set_mac_address;
	card->dev->mtu = card->info.initial_mtu;
	if (card->info.type != QETH_CARD_TYPE_OSN)
		SET_ETHTOOL_OPS(card->dev, &qeth_l2_ethtool_ops);
	else
		SET_ETHTOOL_OPS(card->dev, &qeth_l2_osn_ops);
	card->dev->features |= NETIF_F_HW_VLAN_FILTER;
	card->info.broadcast_capable = 1;
	qeth_l2_request_initial_mac(card);
	SET_NETDEV_DEV(card->dev, &card->gdev->dev);
	return register_netdev(card->dev);
}

static int __qeth_l2_set_online(struct ccwgroup_device *gdev, int recovery_mode)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc = 0;
	enum qeth_card_states recover_flag;

	BUG_ON(!card);
	QETH_DBF_TEXT(SETUP, 2, "setonlin");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	qeth_set_allowed_threads(card, QETH_RECOVER_THREAD, 1);
	if (qeth_wait_for_threads(card, ~QETH_RECOVER_THREAD)) {
		PRINT_WARN("set_online of card %s interrupted by user!\n",
			   CARD_BUS_ID(card));
		return -ERESTARTSYS;
	}

	recover_flag = card->state;
	rc = ccw_device_set_online(CARD_RDEV(card));
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		return -EIO;
	}
	rc = ccw_device_set_online(CARD_WDEV(card));
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		return -EIO;
	}
	rc = ccw_device_set_online(CARD_DDEV(card));
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		return -EIO;
	}

	rc = qeth_core_hardsetup_card(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "2err%d", rc);
		goto out_remove;
	}

	if (!card->dev && qeth_l2_setup_netdev(card))
		goto out_remove;

	if (card->info.type != QETH_CARD_TYPE_OSN)
		qeth_l2_send_setmac(card, &card->dev->dev_addr[0]);

	card->state = CARD_STATE_HARDSETUP;
	qeth_print_status_message(card);

	/* softsetup */
	QETH_DBF_TEXT(SETUP, 2, "softsetp");

	rc = qeth_send_startlan(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		if (rc == 0xe080) {
			PRINT_WARN("LAN on card %s if offline! "
				   "Waiting for STARTLAN from card.\n",
				   CARD_BUS_ID(card));
			card->lan_online = 0;
		}
		return rc;
	} else
		card->lan_online = 1;

	if (card->info.type != QETH_CARD_TYPE_OSN) {
		qeth_set_large_send(card, card->options.large_send);
		qeth_l2_process_vlans(card, 0);
	}

	netif_tx_disable(card->dev);

	rc = qeth_init_qdio_queues(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "6err%d", rc);
		goto out_remove;
	}
	card->state = CARD_STATE_SOFTSETUP;
	netif_carrier_on(card->dev);

	qeth_set_allowed_threads(card, 0xffffffff, 0);
	if (recover_flag == CARD_STATE_RECOVER) {
		if (recovery_mode &&
		    card->info.type != QETH_CARD_TYPE_OSN) {
			qeth_l2_open(card->dev);
		} else {
			rtnl_lock();
			dev_open(card->dev);
			rtnl_unlock();
		}
		/* this also sets saved unicast addresses */
		qeth_l2_set_multicast_list(card->dev);
	}
	/* let user_space know that device is online */
	kobject_uevent(&gdev->dev.kobj, KOBJ_CHANGE);
	return 0;
out_remove:
	card->use_hard_stop = 1;
	qeth_l2_stop_card(card, 0);
	ccw_device_set_offline(CARD_DDEV(card));
	ccw_device_set_offline(CARD_WDEV(card));
	ccw_device_set_offline(CARD_RDEV(card));
	if (recover_flag == CARD_STATE_RECOVER)
		card->state = CARD_STATE_RECOVER;
	else
		card->state = CARD_STATE_DOWN;
	return -ENODEV;
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

	QETH_DBF_TEXT(SETUP, 3, "setoffl");
	QETH_DBF_HEX(SETUP, 3, &card, sizeof(void *));

	if (card->dev && netif_carrier_ok(card->dev))
		netif_carrier_off(card->dev);
	recover_flag = card->state;
	if (qeth_l2_stop_card(card, recovery_mode) == -ERESTARTSYS) {
		PRINT_WARN("Stopping card %s interrupted by user!\n",
			   CARD_BUS_ID(card));
		return -ERESTARTSYS;
	}
	rc  = ccw_device_set_offline(CARD_DDEV(card));
	rc2 = ccw_device_set_offline(CARD_WDEV(card));
	rc3 = ccw_device_set_offline(CARD_RDEV(card));
	if (!rc)
		rc = (rc2) ? rc2 : rc3;
	if (rc)
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
	if (recover_flag == CARD_STATE_UP)
		card->state = CARD_STATE_RECOVER;
	/* let user_space know that device is offline */
	kobject_uevent(&cgdev->dev.kobj, KOBJ_CHANGE);
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
	QETH_DBF_TEXT(TRACE, 2, "recover1");
	QETH_DBF_HEX(TRACE, 2, &card, sizeof(void *));
	if (!qeth_do_run_thread(card, QETH_RECOVER_THREAD))
		return 0;
	QETH_DBF_TEXT(TRACE, 2, "recover2");
	PRINT_WARN("Recovery of device %s started ...\n",
		   CARD_BUS_ID(card));
	card->use_hard_stop = 1;
	__qeth_l2_set_offline(card->gdev, 1);
	rc = __qeth_l2_set_online(card->gdev, 1);
	/* don't run another scheduled recovery */
	qeth_clear_thread_start_bit(card, QETH_RECOVER_THREAD);
	qeth_clear_thread_running_bit(card, QETH_RECOVER_THREAD);
	if (!rc)
		PRINT_INFO("Device %s successfully recovered!\n",
			   CARD_BUS_ID(card));
	else
		PRINT_INFO("Device %s could not be recovered!\n",
			   CARD_BUS_ID(card));
	return 0;
}

static int __init qeth_l2_init(void)
{
	PRINT_INFO("register layer 2 discipline\n");
	return 0;
}

static void __exit qeth_l2_exit(void)
{
	PRINT_INFO("unregister layer 2 discipline\n");
}

static void qeth_l2_shutdown(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	qeth_qdio_clear_card(card, 0);
	qeth_clear_qdio_buffers(card);
}

struct ccwgroup_driver qeth_l2_ccwgroup_driver = {
	.probe = qeth_l2_probe_device,
	.remove = qeth_l2_remove_device,
	.set_online = qeth_l2_set_online,
	.set_offline = qeth_l2_set_offline,
	.shutdown = qeth_l2_shutdown,
};
EXPORT_SYMBOL_GPL(qeth_l2_ccwgroup_driver);

static int qeth_osn_send_control_data(struct qeth_card *card, int len,
			   struct qeth_cmd_buffer *iob)
{
	unsigned long flags;
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 5, "osndctrd");

	wait_event(card->wait_q,
		   atomic_cmpxchg(&card->write.irq_pending, 0, 1) == 0);
	qeth_prepare_control_data(card, len, iob);
	QETH_DBF_TEXT(TRACE, 6, "osnoirqp");
	spin_lock_irqsave(get_ccwdev_lock(card->write.ccwdev), flags);
	rc = ccw_device_start(card->write.ccwdev, &card->write.ccw,
			      (addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(card->write.ccwdev), flags);
	if (rc) {
		PRINT_WARN("qeth_osn_send_control_data: "
			   "ccw_device_start rc = %i\n", rc);
		QETH_DBF_TEXT_(TRACE, 2, " err%d", rc);
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

	QETH_DBF_TEXT(TRACE, 4, "osndipa");

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
	int rc;

	QETH_DBF_TEXT(TRACE, 2, "osnsdmc");
	if (!dev)
		return -ENODEV;
	card = netdev_priv(dev);
	if (!card)
		return -ENODEV;
	if ((card->state != CARD_STATE_UP) &&
	    (card->state != CARD_STATE_SOFTSETUP))
		return -ENODEV;
	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data+IPA_PDU_HEADER_SIZE, data, data_len);
	rc = qeth_osn_send_ipa_cmd(card, iob, data_len);
	return rc;
}
EXPORT_SYMBOL(qeth_osn_assist);

int qeth_osn_register(unsigned char *read_dev_no, struct net_device **dev,
		  int (*assist_cb)(struct net_device *, void *),
		  int (*data_cb)(struct sk_buff *))
{
	struct qeth_card *card;

	QETH_DBF_TEXT(TRACE, 2, "osnreg");
	*dev = qeth_l2_netdev_by_devno(read_dev_no);
	if (*dev == NULL)
		return -ENODEV;
	card = netdev_priv(*dev);
	if (!card)
		return -ENODEV;
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

	QETH_DBF_TEXT(TRACE, 2, "osndereg");
	if (!dev)
		return;
	card = netdev_priv(dev);
	if (!card)
		return;
	card->osn_info.assist_cb = NULL;
	card->osn_info.data_cb = NULL;
	return;
}
EXPORT_SYMBOL(qeth_osn_deregister);

module_init(qeth_l2_init);
module_exit(qeth_l2_exit);
MODULE_AUTHOR("Frank Blaschka <frank.blaschka@de.ibm.com>");
MODULE_DESCRIPTION("qeth layer 2 discipline");
MODULE_LICENSE("GPL");
