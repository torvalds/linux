// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2003 - 2004 Intel Corporation. All rights reserved.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * Few modifications for Realtek's Wi-Fi drivers by
 * Andrea Merello <andrea.merello@gmail.com>
 *
 * A special thanks goes to Realtek for their support !
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <linux/uaccess.h>
#include <linux/if_vlan.h>

#include "rtllib.h"

/* 802.11 Data Frame
 *
 *
 * 802.11 frame_control for data frames - 2 bytes
 *      ,--------------------------------------------------------------------.
 * bits | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |  9 |  a |  b  |  c  |  d  | e  |
 *      |---|---|---|---|---|---|---|---|---|----|----|-----|-----|-----|----|
 * val  | 0 | 0 | 0 | 1 | x | 0 | 0 | 0 | 1 |  0 |  x |  x  |  x  |  x  | x  |
 *      |---|---|---|---|---|---|---|---|---|----|----|-----|-----|-----|----|
 * desc |  ver  | type  |  ^-subtype-^  |to |from|more|retry| pwr |more |wep |
 *      |       |       | x=0 data      |DS | DS |frag|     | mgm |data |    |
 *      |       |       | x=1 data+ack  |   |    |    |     |     |     |    |
 *      '--------------------------------------------------------------------'
 *                                           /\
 *                                           |
 * 802.11 Data Frame                         |
 *          ,--------- 'ctrl' expands to >---'
 *          |
 *       ,--'---,-------------------------------------------------------------.
 * Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
 *       |------|------|---------|---------|---------|------|---------|------|
 * Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  Frame  |  fcs |
 *       |      | tion | (BSSID) |         |         | ence |  data   |      |
 *       `--------------------------------------------------|         |------'
 * Total: 28 non-data bytes                                 `----.----'
 *                                                               |
 *        .- 'Frame data' expands to <---------------------------'
 *        |
 *        V
 *       ,---------------------------------------------------.
 * Bytes |  1   |  1   |    1    |    3     |  2   |  0-2304 |
 *       |------|------|---------|----------|------|---------|
 * Desc. | SNAP | SNAP | Control |Eth Tunnel| Type | IP      |
 *       | DSAP | SSAP |         |          |      | Packet  |
 *       | 0xAA | 0xAA |0x03 (UI)|0x00-00-F8|      |         |
 *       `-----------------------------------------|         |
 * Total: 8 non-data bytes                         `----.----'
 *                                                      |
 *        .- 'IP Packet' expands, if WEP enabled, to <--'
 *        |
 *        V
 *       ,-----------------------.
 * Bytes |  4  |   0-2296  |  4  |
 *       |-----|-----------|-----|
 * Desc. | IV  | Encrypted | ICV |
 *       |     | IP Packet |     |
 *       `-----------------------'
 * Total: 8 non-data bytes
 *
 *
 * 802.3 Ethernet Data Frame
 *
 *       ,-----------------------------------------.
 * Bytes |   6   |   6   |  2   |  Variable |   4  |
 *       |-------|-------|------|-----------|------|
 * Desc. | Dest. | Source| Type | IP Packet |  fcs |
 *       |  MAC  |  MAC  |      |	   |      |
 *       `-----------------------------------------'
 * Total: 18 non-data bytes
 *
 * In the event that fragmentation is required, the incoming payload is split
 * into N parts of size ieee->fts.  The first fragment contains the SNAP header
 * and the remaining packets are just data.
 *
 * If encryption is enabled, each fragment payload size is reduced by enough
 * space to add the prefix and postfix (IV and ICV totalling 8 bytes in
 * the case of WEP) So if you have 1500 bytes of payload with ieee->fts set to
 * 500 without encryption it will take 3 frames.  With WEP it will take 4 frames
 * as the payload of each frame is reduced to 492 bytes.
 *
 * SKB visualization
 *
 * ,- skb->data
 * |
 * |    ETHERNET HEADER        ,-<-- PAYLOAD
 * |                           |     14 bytes from skb->data
 * |  2 bytes for Type --> ,T. |     (sizeof ethhdr)
 * |                       | | |
 * |,-Dest.--. ,--Src.---. | | |
 * |  6 bytes| | 6 bytes | | | |
 * v         | |         | | | |
 * 0         | v       1 | v | v           2
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
 *     ^     | ^         | ^ |
 *     |     | |         | | |
 *     |     | |         | `T' <---- 2 bytes for Type
 *     |     | |         |
 *     |     | '---SNAP--' <-------- 6 bytes for SNAP
 *     |     |
 *     `-IV--' <-------------------- 4 bytes for IV (WEP)
 *
 *      SNAP HEADER
 *
 */

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

static int rtllib_put_snap(u8 *data, u16 h_proto)
{
	struct rtllib_snap_hdr *snap;
	u8 *oui;

	snap = (struct rtllib_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;

	if (h_proto == 0x8137 || h_proto == 0x80f3)
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;
	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];

	*(__be16 *)(data + SNAP_SIZE) = htons(h_proto);

	return SNAP_SIZE + sizeof(u16);
}

int rtllib_encrypt_fragment(struct rtllib_device *ieee, struct sk_buff *frag,
			    int hdr_len)
{
	struct lib80211_crypt_data *crypt = NULL;
	int res;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];

	if (!(crypt && crypt->ops)) {
		netdev_info(ieee->dev, "=========>%s(), crypt is null\n",
			    __func__);
		return -1;
	}
	/* To encrypt, frame format is:
	 * IV (4 bytes), clear payload (including SNAP), ICV (4 bytes)
	 */

	/* Host-based IEEE 802.11 fragmentation for TX is not yet supported, so
	 * call both MSDU and MPDU encryption functions from here.
	 */
	atomic_inc(&crypt->refcnt);
	res = 0;
	if (crypt->ops->encrypt_msdu)
		res = crypt->ops->encrypt_msdu(frag, hdr_len, crypt->priv);
	if (res == 0 && crypt->ops->encrypt_mpdu)
		res = crypt->ops->encrypt_mpdu(frag, hdr_len, crypt->priv);

	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		netdev_info(ieee->dev, "%s: Encryption failed: len=%d.\n",
			    ieee->dev->name, frag->len);
		return -1;
	}

	return 0;
}

void rtllib_txb_free(struct rtllib_txb *txb)
{
	if (unlikely(!txb))
		return;
	kfree(txb);
}

static struct rtllib_txb *rtllib_alloc_txb(int nr_frags, int txb_size,
					   gfp_t gfp_mask)
{
	struct rtllib_txb *txb;
	int i;

	txb = kzalloc(struct_size(txb, fragments, nr_frags), gfp_mask);
	if (!txb)
		return NULL;

	txb->nr_frags = nr_frags;
	txb->frag_size = cpu_to_le16(txb_size);

	for (i = 0; i < nr_frags; i++) {
		txb->fragments[i] = dev_alloc_skb(txb_size);
		if (unlikely(!txb->fragments[i]))
			goto err_free;
		memset(txb->fragments[i]->cb, 0, sizeof(txb->fragments[i]->cb));
	}

	return txb;

err_free:
	while (--i >= 0)
		dev_kfree_skb_any(txb->fragments[i]);
	kfree(txb);

	return NULL;
}

static int rtllib_classify(struct sk_buff *skb, u8 bIsAmsdu)
{
	struct ethhdr *eth;
	struct iphdr *ip;

	eth = (struct ethhdr *)skb->data;
	if (eth->h_proto != htons(ETH_P_IP))
		return 0;

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", __func__, DUMP_PREFIX_NONE, skb->data,
			     skb->len);
#endif
	ip = ip_hdr(skb);
	switch (ip->tos & 0xfc) {
	case 0x20:
		return 2;
	case 0x40:
		return 1;
	case 0x60:
		return 3;
	case 0x80:
		return 4;
	case 0xa0:
		return 5;
	case 0xc0:
		return 6;
	case 0xe0:
		return 7;
	default:
		return 0;
	}
}

static void rtllib_tx_query_agg_cap(struct rtllib_device *ieee,
				    struct sk_buff *skb,
				    struct cb_desc *tcb_desc)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	struct tx_ts_record *ts = NULL;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (rtllib_act_scanning(ieee, false))
		return;

	if (!ht_info->current_ht_support || !ht_info->enable_ht)
		return;
	if (!IsQoSDataFrame(skb->data))
		return;
	if (is_multicast_ether_addr(hdr->addr1))
		return;

	if (tcb_desc->bdhcp || ieee->CntAfterLink < 2)
		return;

	if (ht_info->iot_action & HT_IOT_ACT_TX_NO_AGGREGATION)
		return;

	if (!ieee->get_nmode_support_by_sec_cfg(ieee->dev))
		return;
	if (ht_info->current_ampdu_enable) {
		if (!rtllib_get_ts(ieee, (struct ts_common_info **)(&ts), hdr->addr1,
			   skb->priority, TX_DIR, true)) {
			netdev_info(ieee->dev, "%s: can't get TS\n", __func__);
			return;
		}
		if (!ts->tx_admitted_ba_record.b_valid) {
			if (ieee->wpa_ie_len && (ieee->pairwise_key_type ==
			    KEY_TYPE_NA)) {
				;
			} else if (tcb_desc->bdhcp == 1) {
				;
			} else if (!ts->disable_add_ba) {
				TsStartAddBaProcess(ieee, ts);
			}
			return;
		} else if (!ts->using_ba) {
			if (SN_LESS(ts->tx_admitted_ba_record.ba_start_seq_ctrl.field.seq_num,
				    (ts->tx_cur_seq + 1) % 4096))
				ts->using_ba = true;
			else
				return;
		}
		if (ieee->iw_mode == IW_MODE_INFRA) {
			tcb_desc->ampdu_enable = true;
			tcb_desc->ampdu_factor = ht_info->CurrentAMPDUFactor;
			tcb_desc->ampdu_density = ht_info->current_mpdu_density;
		}
	}
}

static void rtllib_query_ShortPreambleMode(struct rtllib_device *ieee,
					   struct cb_desc *tcb_desc)
{
	tcb_desc->bUseShortPreamble = false;
	if (tcb_desc->data_rate == 2)
		return;
	else if (ieee->current_network.capability &
		 WLAN_CAPABILITY_SHORT_PREAMBLE)
		tcb_desc->bUseShortPreamble = true;
}

static void rtllib_query_HTCapShortGI(struct rtllib_device *ieee,
				      struct cb_desc *tcb_desc)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	tcb_desc->bUseShortGI		= false;

	if (!ht_info->current_ht_support || !ht_info->enable_ht)
		return;

	if (ht_info->cur_bw_40mhz && ht_info->cur_short_gi_40mhz)
		tcb_desc->bUseShortGI = true;
	else if (!ht_info->cur_bw_40mhz && ht_info->cur_short_gi_20mhz)
		tcb_desc->bUseShortGI = true;
}

static void rtllib_query_BandwidthMode(struct rtllib_device *ieee,
				       struct cb_desc *tcb_desc)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	tcb_desc->bPacketBW = false;

	if (!ht_info->current_ht_support || !ht_info->enable_ht)
		return;

	if (tcb_desc->multicast || tcb_desc->bBroadcast)
		return;

	if ((tcb_desc->data_rate & 0x80) == 0)
		return;
	if (ht_info->cur_bw_40mhz && ht_info->cur_tx_bw40mhz &&
	    !ieee->bandwidth_auto_switch.bforced_tx20Mhz)
		tcb_desc->bPacketBW = true;
}

static void rtllib_query_protectionmode(struct rtllib_device *ieee,
					struct cb_desc *tcb_desc,
					struct sk_buff *skb)
{
	struct rt_hi_throughput *ht_info;

	tcb_desc->bRTSSTBC			= false;
	tcb_desc->bRTSUseShortGI		= false;
	tcb_desc->bCTSEnable			= false;
	tcb_desc->RTSSC				= 0;
	tcb_desc->bRTSBW			= false;

	if (tcb_desc->bBroadcast || tcb_desc->multicast)
		return;

	if (is_broadcast_ether_addr(skb->data + 16))
		return;

	if (ieee->mode < WIRELESS_MODE_N_24G) {
		if (skb->len > ieee->rts) {
			tcb_desc->bRTSEnable = true;
			tcb_desc->rts_rate = MGN_24M;
		} else if (ieee->current_network.buseprotection) {
			tcb_desc->bRTSEnable = true;
			tcb_desc->bCTSEnable = true;
			tcb_desc->rts_rate = MGN_24M;
		}
		return;
	}

	ht_info = ieee->ht_info;

	while (true) {
		if (ht_info->iot_action & HT_IOT_ACT_FORCED_CTS2SELF) {
			tcb_desc->bCTSEnable	= true;
			tcb_desc->rts_rate  =	MGN_24M;
			tcb_desc->bRTSEnable = true;
			break;
		} else if (ht_info->iot_action & (HT_IOT_ACT_FORCED_RTS |
			   HT_IOT_ACT_PURE_N_MODE)) {
			tcb_desc->bRTSEnable = true;
			tcb_desc->rts_rate  =	MGN_24M;
			break;
		}
		if (ieee->current_network.buseprotection) {
			tcb_desc->bRTSEnable = true;
			tcb_desc->bCTSEnable = true;
			tcb_desc->rts_rate = MGN_24M;
			break;
		}
		if (ht_info->current_ht_support && ht_info->enable_ht) {
			u8 HTOpMode = ht_info->current_op_mode;

			if ((ht_info->cur_bw_40mhz && (HTOpMode == 2 ||
						      HTOpMode == 3)) ||
			     (!ht_info->cur_bw_40mhz && HTOpMode == 3)) {
				tcb_desc->rts_rate = MGN_24M;
				tcb_desc->bRTSEnable = true;
				break;
			}
		}
		if (skb->len > ieee->rts) {
			tcb_desc->rts_rate = MGN_24M;
			tcb_desc->bRTSEnable = true;
			break;
		}
		if (tcb_desc->ampdu_enable) {
			tcb_desc->rts_rate = MGN_24M;
			tcb_desc->bRTSEnable = false;
			break;
		}
		goto NO_PROTECTION;
	}
	if (ieee->current_network.capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		tcb_desc->bUseShortPreamble = true;
	return;
NO_PROTECTION:
	tcb_desc->bRTSEnable	= false;
	tcb_desc->bCTSEnable	= false;
	tcb_desc->rts_rate	= 0;
	tcb_desc->RTSSC		= 0;
	tcb_desc->bRTSBW	= false;
}

static void rtllib_txrate_selectmode(struct rtllib_device *ieee,
				     struct cb_desc *tcb_desc)
{
	if (ieee->tx_dis_rate_fallback)
		tcb_desc->tx_dis_rate_fallback = true;

	if (ieee->tx_use_drv_assinged_rate)
		tcb_desc->tx_use_drv_assinged_rate = true;
	if (!tcb_desc->tx_dis_rate_fallback ||
	    !tcb_desc->tx_use_drv_assinged_rate) {
		if (ieee->iw_mode == IW_MODE_INFRA)
			tcb_desc->ratr_index = 0;
	}
}

static u16 rtllib_query_seqnum(struct rtllib_device *ieee, struct sk_buff *skb,
			       u8 *dst)
{
	u16 seqnum = 0;

	if (is_multicast_ether_addr(dst))
		return 0;
	if (IsQoSDataFrame(skb->data)) {
		struct tx_ts_record *ts = NULL;

		if (!rtllib_get_ts(ieee, (struct ts_common_info **)(&ts), dst,
			   skb->priority, TX_DIR, true))
			return 0;
		seqnum = ts->tx_cur_seq;
		ts->tx_cur_seq = (ts->tx_cur_seq + 1) % 4096;
		return seqnum;
	}
	return 0;
}

static int wme_downgrade_ac(struct sk_buff *skb)
{
	switch (skb->priority) {
	case 6:
	case 7:
		skb->priority = 5; /* VO -> VI */
		return 0;
	case 4:
	case 5:
		skb->priority = 3; /* VI -> BE */
		return 0;
	case 0:
	case 3:
		skb->priority = 1; /* BE -> BK */
		return 0;
	default:
		return -1;
	}
}

static u8 rtllib_current_rate(struct rtllib_device *ieee)
{
	if (ieee->mode & IEEE_MODE_MASK)
		return ieee->rate;

	if (ieee->HTCurrentOperaRate)
		return ieee->HTCurrentOperaRate;
	else
		return ieee->rate & 0x7F;
}

static int rtllib_xmit_inter(struct sk_buff *skb, struct net_device *dev)
{
	struct rtllib_device *ieee = (struct rtllib_device *)
				     netdev_priv_rsl(dev);
	struct rtllib_txb *txb = NULL;
	struct ieee80211_qos_hdr *frag_hdr;
	int i, bytes_per_frag, nr_frags, bytes_last_frag, frag_size;
	unsigned long flags;
	struct net_device_stats *stats = &ieee->stats;
	int ether_type = 0, encrypt;
	int bytes, fc, qos_ctl = 0, hdr_len;
	struct sk_buff *skb_frag;
	struct ieee80211_qos_hdr header = { /* Ensure zero initialized */
		.duration_id = 0,
		.seq_ctrl = 0,
		.qos_ctrl = 0
	};
	int qos_activated = ieee->current_network.qos_data.active;
	u8 dest[ETH_ALEN];
	u8 src[ETH_ALEN];
	struct lib80211_crypt_data *crypt = NULL;
	struct cb_desc *tcb_desc;
	u8 bIsMulticast = false;
	u8 IsAmsdu = false;
	bool	bdhcp = false;

	spin_lock_irqsave(&ieee->lock, flags);

	/* If there is no driver handler to take the TXB, don't bother
	 * creating it...
	 */
	if (!(ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE) ||
	   ((!ieee->softmac_data_hard_start_xmit &&
	   (ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE)))) {
		netdev_warn(ieee->dev, "No xmit handler.\n");
		goto success;
	}

	if (unlikely(skb->len < SNAP_SIZE + sizeof(u16))) {
		netdev_warn(ieee->dev, "skb too small (%d).\n",
			    skb->len);
		goto success;
	}
	/* Save source and destination addresses */
	ether_addr_copy(dest, skb->data);
	ether_addr_copy(src, skb->data + ETH_ALEN);

	memset(skb->cb, 0, sizeof(skb->cb));
	ether_type = ntohs(((struct ethhdr *)skb->data)->h_proto);

	if (ieee->iw_mode == IW_MODE_MONITOR) {
		txb = rtllib_alloc_txb(1, skb->len, GFP_ATOMIC);
		if (unlikely(!txb)) {
			netdev_warn(ieee->dev,
				    "Could not allocate TXB\n");
			goto failed;
		}

		txb->encrypted = 0;
		txb->payload_size = cpu_to_le16(skb->len);
		skb_put_data(txb->fragments[0], skb->data, skb->len);

		goto success;
	}

	if (skb->len > 282) {
		if (ether_type == ETH_P_IP) {
			const struct iphdr *ip = (struct iphdr *)
				((u8 *)skb->data + 14);
			if (ip->protocol == IPPROTO_UDP) {
				struct udphdr *udp;

				udp = (struct udphdr *)((u8 *)ip +
				      (ip->ihl << 2));
				if (((((u8 *)udp)[1] == 68) &&
				     (((u8 *)udp)[3] == 67)) ||
				   ((((u8 *)udp)[1] == 67) &&
				   (((u8 *)udp)[3] == 68))) {
					bdhcp = true;
					ieee->lps_delay_cnt = 200;
				}
			}
		} else if (ether_type == ETH_P_ARP) {
			netdev_info(ieee->dev,
				    "=================>DHCP Protocol start tx ARP pkt!!\n");
			bdhcp = true;
			ieee->lps_delay_cnt =
				 ieee->current_network.tim.tim_count;
		}
	}

	skb->priority = rtllib_classify(skb, IsAmsdu);
	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	encrypt = !(ether_type == ETH_P_PAE && ieee->ieee802_1x) && crypt && crypt->ops;
	if (!encrypt && ieee->ieee802_1x &&
	    ieee->drop_unencrypted && ether_type != ETH_P_PAE) {
		stats->tx_dropped++;
		goto success;
	}
	if (crypt && !encrypt && ether_type == ETH_P_PAE) {
		struct eapol *eap = (struct eapol *)(skb->data +
			sizeof(struct ethhdr) - SNAP_SIZE -
			sizeof(u16));
		netdev_dbg(ieee->dev,
			   "TX: IEEE 802.11 EAPOL frame: %s\n",
			   eap_get_type(eap->type));
	}

	/* Advance the SKB to the start of the payload */
	skb_pull(skb, sizeof(struct ethhdr));

	/* Determine total amount of storage required for TXB packets */
	bytes = skb->len + SNAP_SIZE + sizeof(u16);

	if (encrypt)
		fc = RTLLIB_FTYPE_DATA | IEEE80211_FCTL_PROTECTED;
	else
		fc = RTLLIB_FTYPE_DATA;

	if (qos_activated)
		fc |= IEEE80211_STYPE_QOS_DATA;
	else
		fc |= IEEE80211_STYPE_DATA;

	if (ieee->iw_mode == IW_MODE_INFRA) {
		fc |= IEEE80211_FCTL_TODS;
		/* To DS: Addr1 = BSSID, Addr2 = SA,
		 * Addr3 = DA
		 */
		ether_addr_copy(header.addr1,
				ieee->current_network.bssid);
		ether_addr_copy(header.addr2, src);
		if (IsAmsdu)
			ether_addr_copy(header.addr3,
					ieee->current_network.bssid);
		else
			ether_addr_copy(header.addr3, dest);
	}

	bIsMulticast = is_multicast_ether_addr(header.addr1);

	header.frame_control = cpu_to_le16(fc);

	/* Determine fragmentation size based on destination (multicast
	 * and broadcast are not fragmented)
	 */
	if (bIsMulticast) {
		frag_size = MAX_FRAG_THRESHOLD;
		qos_ctl |= QOS_CTL_NOTCONTAIN_ACK;
	} else {
		frag_size = ieee->fts;
		qos_ctl = 0;
	}

	if (qos_activated) {
		hdr_len = RTLLIB_3ADDR_LEN + 2;

		/* in case we are a client verify acm is not set for this ac */
		while (unlikely(ieee->wmm_acm & (0x01 << skb->priority))) {
			netdev_info(ieee->dev, "skb->priority = %x\n",
				    skb->priority);
			if (wme_downgrade_ac(skb))
				break;
			netdev_info(ieee->dev, "converted skb->priority = %x\n",
				    skb->priority);
		}

		qos_ctl |= skb->priority;
		header.qos_ctrl = cpu_to_le16(qos_ctl & RTLLIB_QOS_TID);

	} else {
		hdr_len = RTLLIB_3ADDR_LEN;
	}
	/* Determine amount of payload per fragment.  Regardless of if
	 * this stack is providing the full 802.11 header, one will
	 * eventually be affixed to this fragment -- so we must account
	 * for it when determining the amount of payload space.
	 */
	bytes_per_frag = frag_size - hdr_len;
	if (ieee->config &
	   (CFG_RTLLIB_COMPUTE_FCS | CFG_RTLLIB_RESERVE_FCS))
		bytes_per_frag -= RTLLIB_FCS_LEN;

	/* Each fragment may need to have room for encrypting
	 * pre/postfix
	 */
	if (encrypt) {
		bytes_per_frag -= crypt->ops->extra_mpdu_prefix_len +
			crypt->ops->extra_mpdu_postfix_len +
			crypt->ops->extra_msdu_prefix_len +
			crypt->ops->extra_msdu_postfix_len;
	}
	/* Number of fragments is the total bytes_per_frag /
	 * payload_per_fragment
	 */
	nr_frags = bytes / bytes_per_frag;
	bytes_last_frag = bytes % bytes_per_frag;
	if (bytes_last_frag)
		nr_frags++;
	else
		bytes_last_frag = bytes_per_frag;

	/* When we allocate the TXB we allocate enough space for the
	 * reserve and full fragment bytes (bytes_per_frag doesn't
	 * include prefix, postfix, header, FCS, etc.)
	 */
	txb = rtllib_alloc_txb(nr_frags, frag_size +
			       ieee->tx_headroom, GFP_ATOMIC);
	if (unlikely(!txb)) {
		netdev_warn(ieee->dev, "Could not allocate TXB\n");
		goto failed;
	}
	txb->encrypted = encrypt;
	txb->payload_size = cpu_to_le16(bytes);

	if (qos_activated)
		txb->queue_index = UP2AC(skb->priority);
	else
		txb->queue_index = WME_AC_BE;

	for (i = 0; i < nr_frags; i++) {
		skb_frag = txb->fragments[i];
		tcb_desc = (struct cb_desc *)(skb_frag->cb +
			    MAX_DEV_ADDR_SIZE);
		if (qos_activated) {
			skb_frag->priority = skb->priority;
			tcb_desc->queue_index =  UP2AC(skb->priority);
		} else {
			skb_frag->priority = WME_AC_BE;
			tcb_desc->queue_index = WME_AC_BE;
		}
		skb_reserve(skb_frag, ieee->tx_headroom);

		if (encrypt) {
			if (ieee->hwsec_active)
				tcb_desc->bHwSec = 1;
			else
				tcb_desc->bHwSec = 0;
			skb_reserve(skb_frag,
				    crypt->ops->extra_mpdu_prefix_len +
				    crypt->ops->extra_msdu_prefix_len);
		} else {
			tcb_desc->bHwSec = 0;
		}
		frag_hdr = skb_put_data(skb_frag, &header, hdr_len);

		/* If this is not the last fragment, then add the
		 * MOREFRAGS bit to the frame control
		 */
		if (i != nr_frags - 1) {
			frag_hdr->frame_control = cpu_to_le16(fc |
							  IEEE80211_FCTL_MOREFRAGS);
			bytes = bytes_per_frag;

		} else {
			/* The last fragment has the remaining length */
			bytes = bytes_last_frag;
		}
		if ((qos_activated) && (!bIsMulticast)) {
			frag_hdr->seq_ctrl =
				 cpu_to_le16(rtllib_query_seqnum(ieee, skb_frag,
								 header.addr1));
			frag_hdr->seq_ctrl =
				 cpu_to_le16(le16_to_cpu(frag_hdr->seq_ctrl) << 4 | i);
		} else {
			frag_hdr->seq_ctrl =
				 cpu_to_le16(ieee->seq_ctrl[0] << 4 | i);
		}
		/* Put a SNAP header on the first fragment */
		if (i == 0) {
			rtllib_put_snap(skb_put(skb_frag,
						SNAP_SIZE +
						sizeof(u16)), ether_type);
			bytes -= SNAP_SIZE + sizeof(u16);
		}

		skb_put_data(skb_frag, skb->data, bytes);

		/* Advance the SKB... */
		skb_pull(skb, bytes);

		/* Encryption routine will move the header forward in
		 * order to insert the IV between the header and the
		 * payload
		 */
		if (encrypt)
			rtllib_encrypt_fragment(ieee, skb_frag,
						hdr_len);
		if (ieee->config &
		   (CFG_RTLLIB_COMPUTE_FCS | CFG_RTLLIB_RESERVE_FCS))
			skb_put(skb_frag, 4);
	}

	if ((qos_activated) && (!bIsMulticast)) {
		if (ieee->seq_ctrl[UP2AC(skb->priority) + 1] == 0xFFF)
			ieee->seq_ctrl[UP2AC(skb->priority) + 1] = 0;
		else
			ieee->seq_ctrl[UP2AC(skb->priority) + 1]++;
	} else {
		if (ieee->seq_ctrl[0] == 0xFFF)
			ieee->seq_ctrl[0] = 0;
		else
			ieee->seq_ctrl[0]++;
	}

 success:
	if (txb) {
		tcb_desc = (struct cb_desc *)
				(txb->fragments[0]->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->tx_enable_fw_calc_dur = 1;
		tcb_desc->priority = skb->priority;

		if (ether_type == ETH_P_PAE) {
			if (ieee->ht_info->iot_action &
			    HT_IOT_ACT_WA_IOT_Broadcom) {
				tcb_desc->data_rate =
					 mgnt_query_tx_rate_exclude_cck_rates(ieee);
				tcb_desc->tx_dis_rate_fallback = false;
			} else {
				tcb_desc->data_rate = ieee->basic_rate;
				tcb_desc->tx_dis_rate_fallback = 1;
			}

			tcb_desc->ratr_index = 7;
			tcb_desc->tx_use_drv_assinged_rate = 1;
		} else {
			if (is_multicast_ether_addr(header.addr1))
				tcb_desc->multicast = 1;
			if (is_broadcast_ether_addr(header.addr1))
				tcb_desc->bBroadcast = 1;
			rtllib_txrate_selectmode(ieee, tcb_desc);
			if (tcb_desc->multicast ||  tcb_desc->bBroadcast)
				tcb_desc->data_rate = ieee->basic_rate;
			else
				tcb_desc->data_rate = rtllib_current_rate(ieee);

			if (bdhcp) {
				if (ieee->ht_info->iot_action &
				    HT_IOT_ACT_WA_IOT_Broadcom) {
					tcb_desc->data_rate =
					   mgnt_query_tx_rate_exclude_cck_rates(ieee);
					tcb_desc->tx_dis_rate_fallback = false;
				} else {
					tcb_desc->data_rate = MGN_1M;
					tcb_desc->tx_dis_rate_fallback = 1;
				}

				tcb_desc->ratr_index = 7;
				tcb_desc->tx_use_drv_assinged_rate = 1;
				tcb_desc->bdhcp = 1;
			}

			rtllib_query_ShortPreambleMode(ieee, tcb_desc);
			rtllib_tx_query_agg_cap(ieee, txb->fragments[0],
						tcb_desc);
			rtllib_query_HTCapShortGI(ieee, tcb_desc);
			rtllib_query_BandwidthMode(ieee, tcb_desc);
			rtllib_query_protectionmode(ieee, tcb_desc,
						    txb->fragments[0]);
		}
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
	dev_kfree_skb_any(skb);
	if (txb) {
		if (ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE) {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += le16_to_cpu(txb->payload_size);
			rtllib_softmac_xmit(txb, ieee);
		} else {
			rtllib_txb_free(txb);
		}
	}

	return 0;

 failed:
	spin_unlock_irqrestore(&ieee->lock, flags);
	netif_stop_queue(dev);
	stats->tx_errors++;
	return 1;
}

netdev_tx_t rtllib_xmit(struct sk_buff *skb, struct net_device *dev)
{
	memset(skb->cb, 0, sizeof(skb->cb));
	return rtllib_xmit_inter(skb, dev) ? NETDEV_TX_BUSY : NETDEV_TX_OK;
}
EXPORT_SYMBOL(rtllib_xmit);
