// SPDX-License-Identifier: GPL-2.0
/*
 * Original code based Host AP (software wireless LAN access point) driver
 * for Intersil Prism2/2.5/3 - hostap.o module, common routines
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2004, Intel Corporation
 ******************************************************************************

  Few modifications for Realtek's Wi-Fi drivers by
  Andrea Merello <andrea.merello@gmail.com>

  A special thanks goes to Realtek for their support !

******************************************************************************/


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
#include <linux/ctype.h>

#include "ieee80211.h"
#include "dot11d.h"
static inline void ieee80211_monitor_rx(struct ieee80211_device *ieee,
					struct sk_buff *skb,
					struct ieee80211_rx_stats *rx_stats)
{
	struct rtl_80211_hdr_4addr *hdr = (struct rtl_80211_hdr_4addr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_ctl);

	skb->dev = ieee->dev;
	skb_reset_mac_header(skb);

	skb_pull(skb, ieee80211_get_hdrlen(fc));
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_80211_RAW);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}


/* Called only as a tasklet (software IRQ) */
static struct ieee80211_frag_entry *
ieee80211_frag_cache_find(struct ieee80211_device *ieee, unsigned int seq,
			  unsigned int frag, u8 tid, u8 *src, u8 *dst)
{
	struct ieee80211_frag_entry *entry;
	int i;

	for (i = 0; i < IEEE80211_FRAG_CACHE_LEN; i++) {
		entry = &ieee->frag_cache[tid][i];
		if (entry->skb &&
		    time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			IEEE80211_DEBUG_FRAG(
				"expiring fragment cache entry "
				"seq=%u last_frag=%u\n",
				entry->seq, entry->last_frag);
			dev_kfree_skb_any(entry->skb);
			entry->skb = NULL;
		}

		if (entry->skb && entry->seq == seq &&
		    (entry->last_frag + 1 == frag || frag == -1) &&
		    memcmp(entry->src_addr, src, ETH_ALEN) == 0 &&
		    memcmp(entry->dst_addr, dst, ETH_ALEN) == 0)
			return entry;
	}

	return NULL;
}

/* Called only as a tasklet (software IRQ) */
static struct sk_buff *
ieee80211_frag_cache_get(struct ieee80211_device *ieee,
			 struct rtl_80211_hdr_4addr *hdr)
{
	struct sk_buff *skb = NULL;
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	u16 sc = le16_to_cpu(hdr->seq_ctl);
	unsigned int frag = WLAN_GET_SEQ_FRAG(sc);
	unsigned int seq = WLAN_GET_SEQ_SEQ(sc);
	struct ieee80211_frag_entry *entry;
	struct rtl_80211_hdr_3addrqos *hdr_3addrqos;
	struct rtl_80211_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & IEEE80211_FCTL_DSTODS) == IEEE80211_FCTL_DSTODS) && IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct rtl_80211_hdr_4addrqos *)hdr;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & IEEE80211_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct rtl_80211_hdr_3addrqos *)hdr;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & IEEE80211_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
		tid = 0;
	}

	if (frag == 0) {
		/* Reserve enough space to fit maximum frame length */
		skb = dev_alloc_skb(ieee->dev->mtu +
				    sizeof(struct rtl_80211_hdr_4addr) +
				    8 /* LLC */ +
				    2 /* alignment */ +
				    8 /* WEP */ +
				    ETH_ALEN /* WDS */ +
				    (IEEE80211_QOS_HAS_SEQ(fc) ? 2 : 0) /* QOS Control */);
		if (!skb)
			return NULL;

		entry = &ieee->frag_cache[tid][ieee->frag_next_idx[tid]];
		ieee->frag_next_idx[tid]++;
		if (ieee->frag_next_idx[tid] >= IEEE80211_FRAG_CACHE_LEN)
			ieee->frag_next_idx[tid] = 0;

		if (entry->skb)
			dev_kfree_skb_any(entry->skb);

		entry->first_frag_time = jiffies;
		entry->seq = seq;
		entry->last_frag = frag;
		entry->skb = skb;
		memcpy(entry->src_addr, hdr->addr2, ETH_ALEN);
		memcpy(entry->dst_addr, hdr->addr1, ETH_ALEN);
	} else {
		/* received a fragment of a frame for which the head fragment
		 * should have already been received */
		entry = ieee80211_frag_cache_find(ieee, seq, frag, tid, hdr->addr2,
						  hdr->addr1);
		if (entry) {
			entry->last_frag = frag;
			skb = entry->skb;
		}
	}

	return skb;
}


/* Called only as a tasklet (software IRQ) */
static int ieee80211_frag_cache_invalidate(struct ieee80211_device *ieee,
					   struct rtl_80211_hdr_4addr *hdr)
{
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	u16 sc = le16_to_cpu(hdr->seq_ctl);
	unsigned int seq = WLAN_GET_SEQ_SEQ(sc);
	struct ieee80211_frag_entry *entry;
	struct rtl_80211_hdr_3addrqos *hdr_3addrqos;
	struct rtl_80211_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & IEEE80211_FCTL_DSTODS) == IEEE80211_FCTL_DSTODS) && IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct rtl_80211_hdr_4addrqos *)hdr;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & IEEE80211_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct rtl_80211_hdr_3addrqos *)hdr;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & IEEE80211_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
		tid = 0;
	}

	entry = ieee80211_frag_cache_find(ieee, seq, -1, tid, hdr->addr2,
					  hdr->addr1);

	if (!entry) {
		IEEE80211_DEBUG_FRAG(
			"could not invalidate fragment cache "
			"entry (seq=%u)\n", seq);
		return -1;
	}

	entry->skb = NULL;
	return 0;
}



/* ieee80211_rx_frame_mgtmt
 *
 * Responsible for handling management control frames
 *
 * Called by ieee80211_rx */
static inline int
ieee80211_rx_frame_mgmt(struct ieee80211_device *ieee, struct sk_buff *skb,
			struct ieee80211_rx_stats *rx_stats, u16 type,
			u16 stype)
{
	/* On the struct stats definition there is written that
	 * this is not mandatory.... but seems that the probe
	 * response parser uses it
	 */
	struct rtl_80211_hdr_3addr *hdr = (struct rtl_80211_hdr_3addr *)skb->data;

	rx_stats->len = skb->len;
	ieee80211_rx_mgt(ieee, (struct rtl_80211_hdr_4addr *)skb->data, rx_stats);
	/* if ((ieee->state == IEEE80211_LINKED) && (memcmp(hdr->addr3, ieee->current_network.bssid, ETH_ALEN))) */
	if ((memcmp(hdr->addr1, ieee->dev->dev_addr, ETH_ALEN))) {
		/* use ADDR1 to perform address matching for Management frames */
		dev_kfree_skb_any(skb);
		return 0;
	}

	ieee80211_rx_frame_softmac(ieee, skb, rx_stats, type, stype);

	dev_kfree_skb_any(skb);

	return 0;

	#ifdef NOT_YET
	if (ieee->iw_mode == IW_MODE_MASTER) {
		netdev_dbg(ieee->dev, "Master mode not yet supported.\n");
		return 0;
/*
  hostap_update_sta_ps(ieee, (struct hostap_ieee80211_hdr_4addr *)
  skb->data);*/
	}

	if (ieee->hostapd && type == IEEE80211_TYPE_MGMT) {
		if (stype == WLAN_FC_STYPE_BEACON &&
		    ieee->iw_mode == IW_MODE_MASTER) {
			struct sk_buff *skb2;
			/* Process beacon frames also in kernel driver to
			 * update STA(AP) table statistics */
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2)
				hostap_rx(skb2->dev, skb2, rx_stats);
		}

		/* send management frames to the user space daemon for
		 * processing */
		ieee->apdevstats.rx_packets++;
		ieee->apdevstats.rx_bytes += skb->len;
		prism2_rx_80211(ieee->apdev, skb, rx_stats, PRISM2_RX_MGMT);
		return 0;
	}

	    if (ieee->iw_mode == IW_MODE_MASTER) {
		if (type != WLAN_FC_TYPE_MGMT && type != WLAN_FC_TYPE_CTRL) {
			netdev_dbg(skb->dev, "unknown management frame "
			       "(type=0x%02x, stype=0x%02x) dropped\n",
			       type, stype);
			return -1;
		}

		hostap_rx(skb->dev, skb, rx_stats);
		return 0;
	}

	netdev_dbg(skb->dev, "hostap_rx_frame_mgmt: management frame "
	       "received in non-Host AP mode\n");
	return -1;
	#endif
}



/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static unsigned char rfc1042_header[] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static unsigned char bridge_tunnel_header[] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
/* No encapsulation header if EtherType < 0x600 (=length) */

/* Called by ieee80211_rx_frame_decrypt */
static int ieee80211_is_eapol_frame(struct ieee80211_device *ieee,
				    struct sk_buff *skb, size_t hdrlen)
{
	struct net_device *dev = ieee->dev;
	u16 fc, ethertype;
	struct rtl_80211_hdr_4addr *hdr;
	u8 *pos;

	if (skb->len < 24)
		return 0;

	hdr = (struct rtl_80211_hdr_4addr *)skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);

	/* check that the frame is unicast frame to us */
	if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    IEEE80211_FCTL_TODS &&
	    memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN) == 0 &&
	    memcmp(hdr->addr3, dev->dev_addr, ETH_ALEN) == 0) {
		/* ToDS frame with own addr BSSID and DA */
	} else if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		   IEEE80211_FCTL_FROMDS &&
		   memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN) == 0) {
		/* FromDS frame with own addr as DA */
	} else
		return 0;

	if (skb->len < 24 + 8)
		return 0;

	/* check for port access entity Ethernet type */
//	pos = skb->data + 24;
	pos = skb->data + hdrlen;
	ethertype = (pos[6] << 8) | pos[7];
	if (ethertype == ETH_P_PAE)
		return 1;

	return 0;
}

/* Called only as a tasklet (software IRQ), by ieee80211_rx */
static inline int
ieee80211_rx_frame_decrypt(struct ieee80211_device *ieee, struct sk_buff *skb,
			   struct ieee80211_crypt_data *crypt)
{
	struct rtl_80211_hdr_4addr *hdr;
	int res, hdrlen;

	if (!crypt || !crypt->ops->decrypt_mpdu)
		return 0;
	if (ieee->hwsec_active) {
		struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->bHwSec = 1;
	}
	hdr = (struct rtl_80211_hdr_4addr *)skb->data;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	if (ieee->tkip_countermeasures &&
	    strcmp(crypt->ops->name, "TKIP") == 0) {
		if (net_ratelimit()) {
			netdev_dbg(ieee->dev, "TKIP countermeasures: dropped "
			       "received packet from %pM\n",
			       hdr->addr2);
		}
		return -1;
	}

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_mpdu(skb, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		IEEE80211_DEBUG_DROP(
			"decryption failed (SA=%pM"
			") res=%d\n", hdr->addr2, res);
		if (res == -2)
			IEEE80211_DEBUG_DROP("Decryption failed ICV "
					     "mismatch (key %d)\n",
					     skb->data[hdrlen + 3] >> 6);
		ieee->ieee_stats.rx_discards_undecryptable++;
		return -1;
	}

	return res;
}


/* Called only as a tasklet (software IRQ), by ieee80211_rx */
static inline int
ieee80211_rx_frame_decrypt_msdu(struct ieee80211_device *ieee, struct sk_buff *skb,
			     int keyidx, struct ieee80211_crypt_data *crypt)
{
	struct rtl_80211_hdr_4addr *hdr;
	int res, hdrlen;

	if (!crypt || !crypt->ops->decrypt_msdu)
		return 0;
	if (ieee->hwsec_active) {
		struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->bHwSec = 1;
	}

	hdr = (struct rtl_80211_hdr_4addr *)skb->data;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_msdu(skb, keyidx, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		netdev_dbg(ieee->dev, "MSDU decryption/MIC verification failed"
		       " (SA=%pM keyidx=%d)\n",
		       hdr->addr2, keyidx);
		return -1;
	}

	return 0;
}


/* this function is stolen from ipw2200 driver*/
#define IEEE_PACKET_RETRY_TIME (5 * HZ)
static int is_duplicate_packet(struct ieee80211_device *ieee,
				      struct rtl_80211_hdr_4addr *header)
{
	u16 fc = le16_to_cpu(header->frame_ctl);
	u16 sc = le16_to_cpu(header->seq_ctl);
	u16 seq = WLAN_GET_SEQ_SEQ(sc);
	u16 frag = WLAN_GET_SEQ_FRAG(sc);
	u16 *last_seq, *last_frag;
	unsigned long *last_time;
	struct rtl_80211_hdr_3addrqos *hdr_3addrqos;
	struct rtl_80211_hdr_4addrqos *hdr_4addrqos;
	u8 tid;


	//TO2DS and QoS
	if (((fc & IEEE80211_FCTL_DSTODS) == IEEE80211_FCTL_DSTODS) && IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct rtl_80211_hdr_4addrqos *)header;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & IEEE80211_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (IEEE80211_QOS_HAS_SEQ(fc)) { //QoS
		hdr_3addrqos = (struct rtl_80211_hdr_3addrqos *)header;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & IEEE80211_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else { // no QoS
		tid = 0;
	}

	switch (ieee->iw_mode) {
	case IW_MODE_ADHOC:
	{
		struct list_head *p;
		struct ieee_ibss_seq *entry = NULL;
		u8 *mac = header->addr2;
		int index = mac[5] % IEEE_IBSS_MAC_HASH_SIZE;

		list_for_each(p, &ieee->ibss_mac_hash[index]) {
			entry = list_entry(p, struct ieee_ibss_seq, list);
			if (!memcmp(entry->mac, mac, ETH_ALEN))
				break;
		}
	//	if (memcmp(entry->mac, mac, ETH_ALEN)){
		if (p == &ieee->ibss_mac_hash[index]) {
			entry = kmalloc(sizeof(struct ieee_ibss_seq), GFP_ATOMIC);
			if (!entry)
				return 0;
			memcpy(entry->mac, mac, ETH_ALEN);
			entry->seq_num[tid] = seq;
			entry->frag_num[tid] = frag;
			entry->packet_time[tid] = jiffies;
			list_add(&entry->list, &ieee->ibss_mac_hash[index]);
			return 0;
		}
		last_seq = &entry->seq_num[tid];
		last_frag = &entry->frag_num[tid];
		last_time = &entry->packet_time[tid];
		break;
	}

	case IW_MODE_INFRA:
		last_seq = &ieee->last_rxseq_num[tid];
		last_frag = &ieee->last_rxfrag_num[tid];
		last_time = &ieee->last_packet_time[tid];

		break;
	default:
		return 0;
	}

//	if(tid != 0) {
//		printk(KERN_WARNING ":)))))))))))%x %x %x, fc(%x)\n", tid, *last_seq, seq, header->frame_ctl);
//	}
	if ((*last_seq == seq) &&
	    time_after(*last_time + IEEE_PACKET_RETRY_TIME, jiffies)) {
		if (*last_frag == frag)
			goto drop;
		if (*last_frag + 1 != frag)
			/* out-of-order fragment */
			goto drop;
	} else
		*last_seq = seq;

	*last_frag = frag;
	*last_time = jiffies;
	return 0;

drop:
//	BUG_ON(!(fc & IEEE80211_FCTL_RETRY));

	return 1;
}

static bool AddReorderEntry(struct rx_ts_record *pTS, struct rx_reorder_entry *pReorderEntry)
{
	struct list_head *pList = &pTS->rx_pending_pkt_list;
	while (pList->next != &pTS->rx_pending_pkt_list) {
		if (SN_LESS(pReorderEntry->SeqNum, list_entry(pList->next, struct rx_reorder_entry, List)->SeqNum))
			pList = pList->next;
		else if (SN_EQUAL(pReorderEntry->SeqNum, list_entry(pList->next, struct rx_reorder_entry, List)->SeqNum))
			return false;
		else
			break;
	}
	pReorderEntry->List.next = pList->next;
	pReorderEntry->List.next->prev = &pReorderEntry->List;
	pReorderEntry->List.prev = pList;
	pList->next = &pReorderEntry->List;

	return true;
}

static void indicate_packets(struct ieee80211_device *ieee,
			     struct ieee80211_rxb *rxb)
{
	struct net_device_stats *stats = &ieee->stats;
	struct net_device *dev = ieee->dev;
	u16 ethertype;
	u8 i;

	for (i = 0; i < rxb->nr_subframes; i++) {
		struct sk_buff *sub_skb = rxb->subframes[i];

		if (!sub_skb)
			continue;

		/* convert hdr + possible LLC headers into Ethernet header */
		ethertype = (sub_skb->data[6] << 8) | sub_skb->data[7];
		if (sub_skb->len >= 8 &&
		    ((!memcmp(sub_skb->data, rfc1042_header, SNAP_SIZE) &&
			ethertype != ETH_P_AARP &&
			ethertype != ETH_P_IPX) ||
		     !memcmp(sub_skb->data, bridge_tunnel_header, SNAP_SIZE))) {
			/* remove RFC1042 or Bridge-Tunnel encapsulation and
			 * replace EtherType */
			skb_pull(sub_skb, SNAP_SIZE);
		} else {
			/* Leave Ethernet header part of hdr and full payload */
			put_unaligned_be16(sub_skb->len, skb_push(sub_skb, 2));
		}
		memcpy(skb_push(sub_skb, ETH_ALEN), rxb->src, ETH_ALEN);
		memcpy(skb_push(sub_skb, ETH_ALEN), rxb->dst, ETH_ALEN);

		stats->rx_packets++;
		stats->rx_bytes += sub_skb->len;
		if (is_multicast_ether_addr(rxb->dst))
			stats->multicast++;

		/* Indicate the packets to upper layer */
		sub_skb->protocol = eth_type_trans(sub_skb, dev);
		memset(sub_skb->cb, 0, sizeof(sub_skb->cb));
		sub_skb->dev = dev;
		/* 802.11 crc not sufficient */
		sub_skb->ip_summed = CHECKSUM_NONE;
		ieee->last_rx_ps_time = jiffies;
		netif_rx(sub_skb);
	}
}

void ieee80211_indicate_packets(struct ieee80211_device *ieee,
				struct ieee80211_rxb **prxbIndicateArray,
				u8 index)
{
	u8 i;

	for (i = 0; i < index; i++) {
		struct ieee80211_rxb *prxb = prxbIndicateArray[i];

		indicate_packets(ieee, prxb);
		kfree(prxb);
		prxb = NULL;
	}
}

static void RxReorderIndicatePacket(struct ieee80211_device *ieee,
				    struct ieee80211_rxb *prxb,
				    struct rx_ts_record *pTS, u16 SeqNum)
{
	PRT_HIGH_THROUGHPUT	pHTInfo = ieee->pHTInfo;
	struct rx_reorder_entry *pReorderEntry = NULL;
	struct ieee80211_rxb **prxbIndicateArray;
	u8			WinSize = pHTInfo->RxReorderWinSize;
	u16			WinEnd = (pTS->rx_indicate_seq + WinSize - 1) % 4096;
	u8			index = 0;
	bool			bMatchWinStart = false, bPktInBuf = false;
	IEEE80211_DEBUG(IEEE80211_DL_REORDER, "%s(): Seq is %d,pTS->rx_indicate_seq is %d, WinSize is %d\n", __func__, SeqNum, pTS->rx_indicate_seq, WinSize);

	prxbIndicateArray = kmalloc_array(REORDER_WIN_SIZE,
					  sizeof(struct ieee80211_rxb *),
					  GFP_ATOMIC);
	if (!prxbIndicateArray)
		return;

	/* Rx Reorder initialize condition.*/
	if (pTS->rx_indicate_seq == 0xffff)
		pTS->rx_indicate_seq = SeqNum;

	/* Drop out the packet which SeqNum is smaller than WinStart */
	if (SN_LESS(SeqNum, pTS->rx_indicate_seq)) {
		IEEE80211_DEBUG(IEEE80211_DL_REORDER, "Packet Drop! IndicateSeq: %d, NewSeq: %d\n",
				 pTS->rx_indicate_seq, SeqNum);
		pHTInfo->RxReorderDropCounter++;
		{
			int i;
			for (i = 0; i < prxb->nr_subframes; i++)
				dev_kfree_skb(prxb->subframes[i]);

			kfree(prxb);
			prxb = NULL;
		}

		kfree(prxbIndicateArray);
		return;
	}

	/*
	 * Sliding window manipulation. Conditions includes:
	 * 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	 * 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	 */
	if (SN_EQUAL(SeqNum, pTS->rx_indicate_seq)) {
		pTS->rx_indicate_seq = (pTS->rx_indicate_seq + 1) % 4096;
		bMatchWinStart = true;
	} else if (SN_LESS(WinEnd, SeqNum)) {
		if (SeqNum >= (WinSize - 1))
			pTS->rx_indicate_seq = SeqNum + 1 - WinSize;
		else
			pTS->rx_indicate_seq = 4095 - (WinSize - (SeqNum + 1)) + 1;

		IEEE80211_DEBUG(IEEE80211_DL_REORDER, "Window Shift! IndicateSeq: %d, NewSeq: %d\n", pTS->rx_indicate_seq, SeqNum);
	}

	/*
	 * Indication process.
	 * After Packet dropping and Sliding Window shifting as above, we can now just indicate the packets
	 * with the SeqNum smaller than latest WinStart and buffer other packets.
	 */
	/* For Rx Reorder condition:
	 * 1. All packets with SeqNum smaller than WinStart => Indicate
	 * 2. All packets with SeqNum larger than or equal to WinStart => Buffer it.
	 */
	if (bMatchWinStart) {
		/* Current packet is going to be indicated.*/
		IEEE80211_DEBUG(IEEE80211_DL_REORDER, "Packets indication!! IndicateSeq: %d, NewSeq: %d\n",\
				pTS->rx_indicate_seq, SeqNum);
		prxbIndicateArray[0] = prxb;
//		printk("========================>%s(): SeqNum is %d\n",__func__,SeqNum);
		index = 1;
	} else {
		/* Current packet is going to be inserted into pending list.*/
		//IEEE80211_DEBUG(IEEE80211_DL_REORDER,"%s(): We RX no ordered packed, insert to ordered list\n",__func__);
		if (!list_empty(&ieee->RxReorder_Unused_List)) {
			pReorderEntry = list_entry(ieee->RxReorder_Unused_List.next, struct rx_reorder_entry, List);
			list_del_init(&pReorderEntry->List);

			/* Make a reorder entry and insert into a the packet list.*/
			pReorderEntry->SeqNum = SeqNum;
			pReorderEntry->prxb = prxb;
	//		IEEE80211_DEBUG(IEEE80211_DL_REORDER,"%s(): pREorderEntry->SeqNum is %d\n",__func__,pReorderEntry->SeqNum);

			if (!AddReorderEntry(pTS, pReorderEntry)) {
				IEEE80211_DEBUG(IEEE80211_DL_REORDER, "%s(): Duplicate packet is dropped!! IndicateSeq: %d, NewSeq: %d\n",
					__func__, pTS->rx_indicate_seq, SeqNum);
				list_add_tail(&pReorderEntry->List, &ieee->RxReorder_Unused_List);
				{
					int i;
					for (i = 0; i < prxb->nr_subframes; i++)
						dev_kfree_skb(prxb->subframes[i]);

					kfree(prxb);
					prxb = NULL;
				}
			} else {
				IEEE80211_DEBUG(IEEE80211_DL_REORDER,
					 "Pkt insert into buffer!! IndicateSeq: %d, NewSeq: %d\n", pTS->rx_indicate_seq, SeqNum);
			}
		} else {
			/*
			 * Packets are dropped if there is not enough reorder entries.
			 * This part shall be modified!! We can just indicate all the
			 * packets in buffer and get reorder entries.
			 */
			IEEE80211_DEBUG(IEEE80211_DL_ERR, "RxReorderIndicatePacket(): There is no reorder entry!! Packet is dropped!!\n");
			{
				int i;
				for (i = 0; i < prxb->nr_subframes; i++)
					dev_kfree_skb(prxb->subframes[i]);

				kfree(prxb);
				prxb = NULL;
			}
		}
	}

	/* Check if there is any packet need indicate.*/
	while (!list_empty(&pTS->rx_pending_pkt_list)) {
		IEEE80211_DEBUG(IEEE80211_DL_REORDER, "%s(): start RREORDER indicate\n", __func__);
		pReorderEntry = list_entry(pTS->rx_pending_pkt_list.prev, struct rx_reorder_entry, List);
		if (SN_LESS(pReorderEntry->SeqNum, pTS->rx_indicate_seq) ||
		    SN_EQUAL(pReorderEntry->SeqNum, pTS->rx_indicate_seq)) {
			/* This protect buffer from overflow. */
			if (index >= REORDER_WIN_SIZE) {
				IEEE80211_DEBUG(IEEE80211_DL_ERR, "RxReorderIndicatePacket(): Buffer overflow!! \n");
				bPktInBuf = true;
				break;
			}

			list_del_init(&pReorderEntry->List);

			if (SN_EQUAL(pReorderEntry->SeqNum, pTS->rx_indicate_seq))
				pTS->rx_indicate_seq = (pTS->rx_indicate_seq + 1) % 4096;

			IEEE80211_DEBUG(IEEE80211_DL_REORDER, "Packets indication!! IndicateSeq: %d, NewSeq: %d\n", pTS->rx_indicate_seq, SeqNum);
			prxbIndicateArray[index] = pReorderEntry->prxb;
		//	printk("========================>%s(): pReorderEntry->SeqNum is %d\n",__func__,pReorderEntry->SeqNum);
			index++;

			list_add_tail(&pReorderEntry->List, &ieee->RxReorder_Unused_List);
		} else {
			bPktInBuf = true;
			break;
		}
	}

	/* Handling pending timer. Set this timer to prevent from long time Rx buffering.*/
	if (index > 0) {
		// Cancel previous pending timer.
	//	del_timer_sync(&pTS->rx_pkt_pending_timer);
		pTS->rx_timeout_indicate_seq = 0xffff;

		// Indicate packets
		if (index > REORDER_WIN_SIZE) {
			IEEE80211_DEBUG(IEEE80211_DL_ERR, "RxReorderIndicatePacket(): Rx Reorder buffer full!! \n");
			kfree(prxbIndicateArray);
			return;
		}
		ieee80211_indicate_packets(ieee, prxbIndicateArray, index);
	}

	if (bPktInBuf && pTS->rx_timeout_indicate_seq == 0xffff) {
		// Set new pending timer.
		IEEE80211_DEBUG(IEEE80211_DL_REORDER, "%s(): SET rx timeout timer\n", __func__);
		pTS->rx_timeout_indicate_seq = pTS->rx_indicate_seq;
		if (timer_pending(&pTS->rx_pkt_pending_timer))
			del_timer_sync(&pTS->rx_pkt_pending_timer);
		pTS->rx_pkt_pending_timer.expires = jiffies +
				msecs_to_jiffies(pHTInfo->RxReorderPendingTime);
		add_timer(&pTS->rx_pkt_pending_timer);
	}

	kfree(prxbIndicateArray);
}

static u8 parse_subframe(struct ieee80211_device *ieee,
			 struct sk_buff *skb,
			 struct ieee80211_rx_stats *rx_stats,
			 struct ieee80211_rxb *rxb, u8 *src, u8 *dst)
{
	struct rtl_80211_hdr_3addr  *hdr = (struct rtl_80211_hdr_3addr *)skb->data;
	u16		fc = le16_to_cpu(hdr->frame_ctl);

	u16		LLCOffset = sizeof(struct rtl_80211_hdr_3addr);
	u16		ChkLength;
	bool		bIsAggregateFrame = false;
	u16		nSubframe_Length;
	u8		nPadding_Length = 0;
	u16		SeqNum = 0;

	struct sk_buff *sub_skb;
	/* just for debug purpose */
	SeqNum = WLAN_GET_SEQ_SEQ(le16_to_cpu(hdr->seq_ctl));

	if ((IEEE80211_QOS_HAS_SEQ(fc)) && \
			(((frameqos *)(skb->data + IEEE80211_3ADDR_LEN))->field.reserved)) {
		bIsAggregateFrame = true;
	}

	if (IEEE80211_QOS_HAS_SEQ(fc))
		LLCOffset += 2;

	if (rx_stats->bContainHTC)
		LLCOffset += HTCLNG;

	// Null packet, don't indicate it to upper layer
	ChkLength = LLCOffset;/* + (Frame_WEP(frame)!=0 ?Adapter->MgntInfo.SecurityInfo.EncryptionHeadOverhead:0);*/

	if (skb->len <= ChkLength)
		return 0;

	skb_pull(skb, LLCOffset);

	if (!bIsAggregateFrame) {
		rxb->nr_subframes = 1;
#ifdef JOHN_NOCPY
		rxb->subframes[0] = skb;
#else
		rxb->subframes[0] = skb_copy(skb, GFP_ATOMIC);
#endif

		memcpy(rxb->src, src, ETH_ALEN);
		memcpy(rxb->dst, dst, ETH_ALEN);
		//IEEE80211_DEBUG_DATA(IEEE80211_DL_RX,skb->data,skb->len);
		return 1;
	} else {
		rxb->nr_subframes = 0;
		memcpy(rxb->src, src, ETH_ALEN);
		memcpy(rxb->dst, dst, ETH_ALEN);
		while (skb->len > ETHERNET_HEADER_SIZE) {
			/* Offset 12 denote 2 mac address */
			nSubframe_Length = *((u16 *)(skb->data + 12));
			//==m==>change the length order
			nSubframe_Length = (nSubframe_Length >> 8) + (nSubframe_Length << 8);

			if (skb->len < (ETHERNET_HEADER_SIZE + nSubframe_Length)) {
				netdev_dbg(ieee->dev, "A-MSDU parse error!! pRfd->nTotalSubframe : %d\n",
					   rxb->nr_subframes);
				netdev_dbg(ieee->dev, "A-MSDU parse error!! Subframe Length: %d\n", nSubframe_Length);
				netdev_dbg(ieee->dev, "nRemain_Length is %d and nSubframe_Length is : %d\n", skb->len, nSubframe_Length);
				netdev_dbg(ieee->dev, "The Packet SeqNum is %d\n", SeqNum);
				return 0;
			}

			/* move the data point to data content */
			skb_pull(skb, ETHERNET_HEADER_SIZE);

#ifdef JOHN_NOCPY
			sub_skb = skb_clone(skb, GFP_ATOMIC);
			sub_skb->len = nSubframe_Length;
			sub_skb->tail = sub_skb->data + nSubframe_Length;
#else
			/* Allocate new skb for releasing to upper layer */
			sub_skb = dev_alloc_skb(nSubframe_Length + 12);
			if (!sub_skb)
				return 0;
			skb_reserve(sub_skb, 12);
			skb_put_data(sub_skb, skb->data, nSubframe_Length);
#endif
			rxb->subframes[rxb->nr_subframes++] = sub_skb;
			if (rxb->nr_subframes >= MAX_SUBFRAME_COUNT) {
				IEEE80211_DEBUG_RX("ParseSubframe(): Too many Subframes! Packets dropped!\n");
				break;
			}
			skb_pull(skb, nSubframe_Length);

			if (skb->len != 0) {
				nPadding_Length = 4 - ((nSubframe_Length + ETHERNET_HEADER_SIZE) % 4);
				if (nPadding_Length == 4)
					nPadding_Length = 0;

				if (skb->len < nPadding_Length)
					return 0;

				skb_pull(skb, nPadding_Length);
			}
		}
#ifdef JOHN_NOCPY
		dev_kfree_skb(skb);
#endif
		//{just for debug added by david
		//printk("AMSDU::rxb->nr_subframes = %d\n",rxb->nr_subframes);
		//}
		return rxb->nr_subframes;
	}
}

/* All received frames are sent to this function. @skb contains the frame in
 * IEEE 802.11 format, i.e., in the format it was sent over air.
 * This function is called only as a tasklet (software IRQ). */
int ieee80211_rx(struct ieee80211_device *ieee, struct sk_buff *skb,
		 struct ieee80211_rx_stats *rx_stats)
{
	struct net_device *dev = ieee->dev;
	struct rtl_80211_hdr_4addr *hdr;
	//struct rtl_80211_hdr_3addrqos *hdr;

	size_t hdrlen;
	u16 fc, type, stype, sc;
	struct net_device_stats *stats;
	unsigned int frag;
	//added by amy for reorder
	u8	TID = 0;
	u16	SeqNum = 0;
	struct rx_ts_record *pTS = NULL;
	//bool bIsAggregateFrame = false;
	//added by amy for reorder
#ifdef NOT_YET
	struct net_device *wds = NULL;
	struct net_device *wds = NULL;
	int from_assoc_ap = 0;
	void *sta = NULL;
#endif
//	u16 qos_ctl = 0;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	struct ieee80211_crypt_data *crypt = NULL;
	int keyidx = 0;

	int i;
	struct ieee80211_rxb *rxb = NULL;
	// cheat the hdr type
	hdr = (struct rtl_80211_hdr_4addr *)skb->data;
	stats = &ieee->stats;

	if (skb->len < 10) {
		netdev_info(dev, "SKB length < 10\n");
		goto rx_dropped;
	}

	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);

	frag = WLAN_GET_SEQ_FRAG(sc);
	hdrlen = ieee80211_get_hdrlen(fc);

	if (HTCCheck(ieee, skb->data)) {
		if (net_ratelimit())
			netdev_warn(dev, "find HTCControl\n");
		hdrlen += 4;
		rx_stats->bContainHTC = true;
	}

	//IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA, skb->data, skb->len);
#ifdef NOT_YET
	/* Put this code here so that we avoid duplicating it in all
	 * Rx paths. - Jean II */
#ifdef IW_WIRELESS_SPY		/* defined in iw_handler.h */
	/* If spy monitoring on */
	if (iface->spy_data.spy_number > 0) {
		struct iw_quality wstats;
		wstats.level = rx_stats->rssi;
		wstats.noise = rx_stats->noise;
		wstats.updated = 6;	/* No qual value */
		/* Update spy records */
		wireless_spy_update(dev, hdr->addr2, &wstats);
	}
#endif /* IW_WIRELESS_SPY */
	hostap_update_rx_stats(local->ap, hdr, rx_stats);
#endif

	if (ieee->iw_mode == IW_MODE_MONITOR) {
		ieee80211_monitor_rx(ieee, skb, rx_stats);
		stats->rx_packets++;
		stats->rx_bytes += skb->len;
		return 1;
	}

	if (ieee->host_decrypt) {
		int idx = 0;
		if (skb->len >= hdrlen + 3)
			idx = skb->data[hdrlen + 3] >> 6;
		crypt = ieee->crypt[idx];
#ifdef NOT_YET
		sta = NULL;

		/* Use station specific key to override default keys if the
		 * receiver address is a unicast address ("individual RA"). If
		 * bcrx_sta_key parameter is set, station specific key is used
		 * even with broad/multicast targets (this is against IEEE
		 * 802.11, but makes it easier to use different keys with
		 * stations that do not support WEP key mapping). */

		if (!(hdr->addr1[0] & 0x01) || local->bcrx_sta_key)
			(void)hostap_handle_sta_crypto(local, hdr, &crypt,
							&sta);
#endif

		/* allow NULL decrypt to indicate an station specific override
		 * for default encryption */
		if (crypt && (!crypt->ops || !crypt->ops->decrypt_mpdu))
			crypt = NULL;

		if (!crypt && (fc & IEEE80211_FCTL_WEP)) {
			/* This seems to be triggered by some (multicast?)
			 * frames from other than current BSS, so just drop the
			 * frames silently instead of filling system log with
			 * these reports. */
			IEEE80211_DEBUG_DROP("Decryption failed (not set)"
					     " (SA=%pM)\n",
					     hdr->addr2);
			ieee->ieee_stats.rx_discards_undecryptable++;
			goto rx_dropped;
		}
	}

	if (skb->len < IEEE80211_DATA_HDR3_LEN)
		goto rx_dropped;

	// if QoS enabled, should check the sequence for each of the AC
	if ((!ieee->pHTInfo->bCurRxReorderEnable) || !ieee->current_network.qos_data.active || !IsDataFrame(skb->data) || IsLegacyDataFrame(skb->data)) {
		if (is_duplicate_packet(ieee, hdr))
			goto rx_dropped;

	} else {
		struct rx_ts_record *pRxTS = NULL;
			//IEEE80211_DEBUG(IEEE80211_DL_REORDER,"%s(): QOS ENABLE AND RECEIVE QOS DATA , we will get Ts, tid:%d\n",__func__, tid);
		if (GetTs(
				ieee,
				(struct ts_common_info **)&pRxTS,
				hdr->addr2,
				Frame_QoSTID((u8 *)(skb->data)),
				RX_DIR,
				true)) {

		//	IEEE80211_DEBUG(IEEE80211_DL_REORDER,"%s(): pRxTS->rx_last_frag_num is %d,frag is %d,pRxTS->rx_last_seq_num is %d,seq is %d\n",__func__,pRxTS->rx_last_frag_num,frag,pRxTS->rx_last_seq_num,WLAN_GET_SEQ_SEQ(sc));
			if ((fc & (1 << 11)) &&
			    (frag == pRxTS->rx_last_frag_num) &&
			    (WLAN_GET_SEQ_SEQ(sc) == pRxTS->rx_last_seq_num)) {
				goto rx_dropped;
			} else {
				pRxTS->rx_last_frag_num = frag;
				pRxTS->rx_last_seq_num = WLAN_GET_SEQ_SEQ(sc);
			}
		} else {
			IEEE80211_DEBUG(IEEE80211_DL_ERR, "%s(): No TS!! Skip the check!!\n", __func__);
			goto rx_dropped;
		}
	}
	if (type == IEEE80211_FTYPE_MGMT) {


	//IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA, skb->data, skb->len);
		if (ieee80211_rx_frame_mgmt(ieee, skb, rx_stats, type, stype))
			goto rx_dropped;
		else
			goto rx_exit;
	}

	/* Data frame - extract src/dst addresses */
	switch (fc & (IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) {
	case IEEE80211_FCTL_FROMDS:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);
		memcpy(bssid, hdr->addr2, ETH_ALEN);
		break;
	case IEEE80211_FCTL_TODS:
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		memcpy(bssid, hdr->addr1, ETH_ALEN);
		break;
	case IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS:
		if (skb->len < IEEE80211_DATA_HDR4_LEN)
			goto rx_dropped;
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);
		memcpy(bssid, ieee->current_network.bssid, ETH_ALEN);
		break;
	default:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		memcpy(bssid, hdr->addr3, ETH_ALEN);
		break;
	}

#ifdef NOT_YET
	if (hostap_rx_frame_wds(ieee, hdr, fc, &wds))
		goto rx_dropped;
	if (wds) {
		skb->dev = dev = wds;
		stats = hostap_get_stats(dev);
	}

	if (ieee->iw_mode == IW_MODE_MASTER && !wds &&
	    (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) == IEEE80211_FCTL_FROMDS &&
	    ieee->stadev &&
	    memcmp(hdr->addr2, ieee->assoc_ap_addr, ETH_ALEN) == 0) {
		/* Frame from BSSID of the AP for which we are a client */
		skb->dev = dev = ieee->stadev;
		stats = hostap_get_stats(dev);
		from_assoc_ap = 1;
	}

	if ((ieee->iw_mode == IW_MODE_MASTER ||
	     ieee->iw_mode == IW_MODE_REPEAT) &&
	    !from_assoc_ap) {
		switch (hostap_handle_sta_rx(ieee, dev, skb, rx_stats,
					     wds)) {
		case AP_RX_CONTINUE_NOT_AUTHORIZED:
		case AP_RX_CONTINUE:
			break;
		case AP_RX_DROP:
			goto rx_dropped;
		case AP_RX_EXIT:
			goto rx_exit;
		}
	}
#endif
	//IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA, skb->data, skb->len);
	/* Nullfunc frames may have PS-bit set, so they must be passed to
	 * hostap_handle_sta_rx() before being dropped here. */
	if (stype != IEEE80211_STYPE_DATA &&
	    stype != IEEE80211_STYPE_DATA_CFACK &&
	    stype != IEEE80211_STYPE_DATA_CFPOLL &&
	    stype != IEEE80211_STYPE_DATA_CFACKPOLL &&
	    stype != IEEE80211_STYPE_QOS_DATA//add by David,2006.8.4
	    ) {
		if (stype != IEEE80211_STYPE_NULLFUNC)
			IEEE80211_DEBUG_DROP(
				"RX: dropped data frame "
				"with no data (type=0x%02x, "
				"subtype=0x%02x, len=%d)\n",
				type, stype, skb->len);
		goto rx_dropped;
	}
	if (memcmp(bssid, ieee->current_network.bssid, ETH_ALEN))
		goto rx_dropped;

	/* skb: hdr + (possibly fragmented, possibly encrypted) payload */

	if (ieee->host_decrypt && (fc & IEEE80211_FCTL_WEP) &&
	    (keyidx = ieee80211_rx_frame_decrypt(ieee, skb, crypt)) < 0) {
		netdev_dbg(ieee->dev, "decrypt frame error\n");
		goto rx_dropped;
	}


	hdr = (struct rtl_80211_hdr_4addr *)skb->data;

	/* skb: hdr + (possibly fragmented) plaintext payload */
	// PR: FIXME: hostap has additional conditions in the "if" below:
	// ieee->host_decrypt && (fc & IEEE80211_FCTL_WEP) &&
	if ((frag != 0 || (fc & IEEE80211_FCTL_MOREFRAGS))) {
		int flen;
		struct sk_buff *frag_skb = ieee80211_frag_cache_get(ieee, hdr);
		IEEE80211_DEBUG_FRAG("Rx Fragment received (%u)\n", frag);

		if (!frag_skb) {
			IEEE80211_DEBUG(IEEE80211_DL_RX | IEEE80211_DL_FRAG,
					"Rx cannot get skb from fragment "
					"cache (morefrag=%d seq=%u frag=%u)\n",
					(fc & IEEE80211_FCTL_MOREFRAGS) != 0,
					WLAN_GET_SEQ_SEQ(sc), frag);
			goto rx_dropped;
		}
		flen = skb->len;
		if (frag != 0)
			flen -= hdrlen;

		if (frag_skb->tail + flen > frag_skb->end) {
			netdev_warn(dev, "host decrypted and "
			       "reassembled frame did not fit skb\n");
			ieee80211_frag_cache_invalidate(ieee, hdr);
			goto rx_dropped;
		}

		if (frag == 0) {
			/* copy first fragment (including full headers) into
			 * beginning of the fragment cache skb */
			skb_put_data(frag_skb, skb->data, flen);
		} else {
			/* append frame payload to the end of the fragment
			 * cache skb */
			skb_put_data(frag_skb, skb->data + hdrlen, flen);
		}
		dev_kfree_skb_any(skb);
		skb = NULL;

		if (fc & IEEE80211_FCTL_MOREFRAGS) {
			/* more fragments expected - leave the skb in fragment
			 * cache for now; it will be delivered to upper layers
			 * after all fragments have been received */
			goto rx_exit;
		}

		/* this was the last fragment and the frame will be
		 * delivered, so remove skb from fragment cache */
		skb = frag_skb;
		hdr = (struct rtl_80211_hdr_4addr *)skb->data;
		ieee80211_frag_cache_invalidate(ieee, hdr);
	}

	/* skb: hdr + (possible reassembled) full MSDU payload; possibly still
	 * encrypted/authenticated */
	if (ieee->host_decrypt && (fc & IEEE80211_FCTL_WEP) &&
	    ieee80211_rx_frame_decrypt_msdu(ieee, skb, keyidx, crypt)) {
		netdev_dbg(ieee->dev, "==>decrypt msdu error\n");
		goto rx_dropped;
	}

	//added by amy for AP roaming
	ieee->LinkDetectInfo.NumRecvDataInPeriod++;
	ieee->LinkDetectInfo.NumRxOkInPeriod++;

	hdr = (struct rtl_80211_hdr_4addr *)skb->data;
	if (crypt && !(fc & IEEE80211_FCTL_WEP) && !ieee->open_wep) {
		if (/*ieee->ieee802_1x &&*/
		    ieee80211_is_eapol_frame(ieee, skb, hdrlen)) {

#ifdef CONFIG_IEEE80211_DEBUG
			/* pass unencrypted EAPOL frames even if encryption is
			 * configured */
			struct eapol *eap = (struct eapol *)(skb->data +
				24);
			IEEE80211_DEBUG_EAP("RX: IEEE 802.1X EAPOL frame: %s\n",
						eap_get_type(eap->type));
#endif
		} else {
			IEEE80211_DEBUG_DROP(
				"encryption configured, but RX "
				"frame not encrypted (SA=%pM)\n",
				hdr->addr2);
			goto rx_dropped;
		}
	}

#ifdef CONFIG_IEEE80211_DEBUG
	if (crypt && !(fc & IEEE80211_FCTL_WEP) &&
	    ieee80211_is_eapol_frame(ieee, skb, hdrlen)) {
		struct eapol *eap = (struct eapol *)(skb->data +
			24);
		IEEE80211_DEBUG_EAP("RX: IEEE 802.1X EAPOL frame: %s\n",
					eap_get_type(eap->type));
	}
#endif

	if (crypt && !(fc & IEEE80211_FCTL_WEP) && !ieee->open_wep &&
	    !ieee80211_is_eapol_frame(ieee, skb, hdrlen)) {
		IEEE80211_DEBUG_DROP(
			"dropped unencrypted RX data "
			"frame from %pM"
			" (drop_unencrypted=1)\n",
			hdr->addr2);
		goto rx_dropped;
	}
/*
	if(ieee80211_is_eapol_frame(ieee, skb, hdrlen)) {
		printk(KERN_WARNING "RX: IEEE802.1X EPAOL frame!\n");
	}
*/
//added by amy for reorder
	if (ieee->current_network.qos_data.active && IsQoSDataFrame(skb->data)
		&& !is_multicast_ether_addr(hdr->addr1)) {
		TID = Frame_QoSTID(skb->data);
		SeqNum = WLAN_GET_SEQ_SEQ(sc);
		GetTs(ieee, (struct ts_common_info **)&pTS, hdr->addr2, TID, RX_DIR, true);
		if (TID != 0 && TID != 3)
			ieee->bis_any_nonbepkts = true;
	}
//added by amy for reorder
	/* skb: hdr + (possible reassembled) full plaintext payload */
	//ethertype = (payload[6] << 8) | payload[7];
	rxb = kmalloc(sizeof(struct ieee80211_rxb), GFP_ATOMIC);
	if (!rxb)
		goto rx_dropped;
	/* to parse amsdu packets */
	/* qos data packets & reserved bit is 1 */
	if (parse_subframe(ieee, skb, rx_stats, rxb, src, dst) == 0) {
		/* only to free rxb, and not submit the packets to upper layer */
		for (i = 0; i < rxb->nr_subframes; i++)
			dev_kfree_skb(rxb->subframes[i]);

		kfree(rxb);
		rxb = NULL;
		goto rx_dropped;
	}

//added by amy for reorder
	if (!ieee->pHTInfo->bCurRxReorderEnable || !pTS) {
		indicate_packets(ieee, rxb);
		kfree(rxb);
		rxb = NULL;

	} else {
		IEEE80211_DEBUG(IEEE80211_DL_REORDER, "%s(): REORDER ENABLE AND PTS not NULL, and we will enter RxReorderIndicatePacket()\n", __func__);
		RxReorderIndicatePacket(ieee, rxb, pTS, SeqNum);
	}
#ifndef JOHN_NOCPY
	dev_kfree_skb(skb);
#endif

 rx_exit:
#ifdef NOT_YET
	if (sta)
		hostap_handle_sta_release(sta);
#endif
	return 1;

 rx_dropped:
	kfree(rxb);
	rxb = NULL;
	stats->rx_dropped++;

	/* Returning 0 indicates to caller that we have not handled the SKB--
	 * so it is still allocated and can be used again by underlying
	 * hardware as a DMA target */
	return 0;
}
EXPORT_SYMBOL(ieee80211_rx);

#define MGMT_FRAME_FIXED_PART_LENGTH            0x24

static u8 qos_oui[QOS_OUI_LEN] = { 0x00, 0x50, 0xF2 };

/*
* Make the structure we read from the beacon packet to have
* the right values
*/
static int ieee80211_verify_qos_info(struct ieee80211_qos_information_element
				     *info_element, int sub_type)
{

	if (info_element->qui_subtype != sub_type)
		return -1;
	if (memcmp(info_element->qui, qos_oui, QOS_OUI_LEN))
		return -1;
	if (info_element->qui_type != QOS_OUI_TYPE)
		return -1;
	if (info_element->version != QOS_VERSION_1)
		return -1;

	return 0;
}


/*
 * Parse a QoS parameter element
 */
static int ieee80211_read_qos_param_element(struct ieee80211_qos_parameter_info
					    *element_param, struct ieee80211_info_element
					    *info_element)
{
	int ret = 0;
	u16 size = sizeof(struct ieee80211_qos_parameter_info) - 2;

	if (!info_element || !element_param)
		return -1;

	if (info_element->id == QOS_ELEMENT_ID && info_element->len == size) {
		memcpy(element_param->info_element.qui, info_element->data,
		       info_element->len);
		element_param->info_element.elementID = info_element->id;
		element_param->info_element.length = info_element->len;
	} else
		ret = -1;
	if (ret == 0)
		ret = ieee80211_verify_qos_info(&element_param->info_element,
						QOS_OUI_PARAM_SUB_TYPE);
	return ret;
}

/*
 * Parse a QoS information element
 */
static int ieee80211_read_qos_info_element(
		struct ieee80211_qos_information_element *element_info,
		struct ieee80211_info_element *info_element)
{
	int ret = 0;
	u16 size = sizeof(struct ieee80211_qos_information_element) - 2;

	if (!element_info)
		return -1;
	if (!info_element)
		return -1;

	if ((info_element->id == QOS_ELEMENT_ID) && (info_element->len == size)) {
		memcpy(element_info->qui, info_element->data,
		       info_element->len);
		element_info->elementID = info_element->id;
		element_info->length = info_element->len;
	} else
		ret = -1;

	if (ret == 0)
		ret = ieee80211_verify_qos_info(element_info,
						QOS_OUI_INFO_SUB_TYPE);
	return ret;
}


/*
 * Write QoS parameters from the ac parameters.
 */
static int ieee80211_qos_convert_ac_to_parameters(
		struct ieee80211_qos_parameter_info *param_elm,
		struct ieee80211_qos_parameters *qos_param)
{
	int i;
	struct ieee80211_qos_ac_parameter *ac_params;
	u8 aci;
	//u8 cw_min;
	//u8 cw_max;

	for (i = 0; i < QOS_QUEUE_NUM; i++) {
		ac_params = &(param_elm->ac_params_record[i]);

		aci = (ac_params->aci_aifsn & 0x60) >> 5;

		if (aci >= QOS_QUEUE_NUM)
			continue;
		qos_param->aifs[aci] = (ac_params->aci_aifsn) & 0x0f;

		/* WMM spec P.11: The minimum value for AIFSN shall be 2 */
		qos_param->aifs[aci] = (qos_param->aifs[aci] < 2) ? 2 : qos_param->aifs[aci];

		qos_param->cw_min[aci] =
		    cpu_to_le16(ac_params->ecw_min_max & 0x0F);

		qos_param->cw_max[aci] =
		    cpu_to_le16((ac_params->ecw_min_max & 0xF0) >> 4);

		qos_param->flag[aci] =
		    (ac_params->aci_aifsn & 0x10) ? 0x01 : 0x00;
		qos_param->tx_op_limit[aci] = ac_params->tx_op_limit;
	}
	return 0;
}

/*
 * we have a generic data element which it may contain QoS information or
 * parameters element. check the information element length to decide
 * which type to read
 */
static int ieee80211_parse_qos_info_param_IE(struct ieee80211_info_element
					     *info_element,
					     struct ieee80211_network *network)
{
	int rc = 0;
	struct ieee80211_qos_parameters *qos_param = NULL;
	struct ieee80211_qos_information_element qos_info_element;

	rc = ieee80211_read_qos_info_element(&qos_info_element, info_element);

	if (rc == 0) {
		network->qos_data.param_count = qos_info_element.ac_info & 0x0F;
		network->flags |= NETWORK_HAS_QOS_INFORMATION;
	} else {
		struct ieee80211_qos_parameter_info param_element;

		rc = ieee80211_read_qos_param_element(&param_element,
						      info_element);
		if (rc == 0) {
			qos_param = &(network->qos_data.parameters);
			ieee80211_qos_convert_ac_to_parameters(&param_element,
							       qos_param);
			network->flags |= NETWORK_HAS_QOS_PARAMETERS;
			network->qos_data.param_count =
			    param_element.info_element.ac_info & 0x0F;
		}
	}

	if (rc == 0) {
		IEEE80211_DEBUG_QOS("QoS is supported\n");
		network->qos_data.supported = 1;
	}
	return rc;
}

#ifdef CONFIG_IEEE80211_DEBUG
#define MFIE_STRING(x) case MFIE_TYPE_ ##x: return #x

static const char *get_info_element_string(u16 id)
{
	switch (id) {
		MFIE_STRING(SSID);
		MFIE_STRING(RATES);
		MFIE_STRING(FH_SET);
		MFIE_STRING(DS_SET);
		MFIE_STRING(CF_SET);
		MFIE_STRING(TIM);
		MFIE_STRING(IBSS_SET);
		MFIE_STRING(COUNTRY);
		MFIE_STRING(HOP_PARAMS);
		MFIE_STRING(HOP_TABLE);
		MFIE_STRING(REQUEST);
		MFIE_STRING(CHALLENGE);
		MFIE_STRING(POWER_CONSTRAINT);
		MFIE_STRING(POWER_CAPABILITY);
		MFIE_STRING(TPC_REQUEST);
		MFIE_STRING(TPC_REPORT);
		MFIE_STRING(SUPP_CHANNELS);
		MFIE_STRING(CSA);
		MFIE_STRING(MEASURE_REQUEST);
		MFIE_STRING(MEASURE_REPORT);
		MFIE_STRING(QUIET);
		MFIE_STRING(IBSS_DFS);
	       // MFIE_STRING(ERP_INFO);
		MFIE_STRING(RSN);
		MFIE_STRING(RATES_EX);
		MFIE_STRING(GENERIC);
		MFIE_STRING(QOS_PARAMETER);
	default:
		return "UNKNOWN";
	}
}
#endif

static inline void ieee80211_extract_country_ie(
	struct ieee80211_device *ieee,
	struct ieee80211_info_element *info_element,
	struct ieee80211_network *network,
	u8 *addr2
)
{
	if (IS_DOT11D_ENABLE(ieee)) {
		if (info_element->len != 0) {
			memcpy(network->CountryIeBuf, info_element->data, info_element->len);
			network->CountryIeLen = info_element->len;

			if (!IS_COUNTRY_IE_VALID(ieee)) {
				dot11d_update_country_ie(ieee, addr2, info_element->len, info_element->data);
			}
		}

		//
		// 070305, rcnjko: I update country IE watch dog here because
		// some AP (e.g. Cisco 1242) don't include country IE in their
		// probe response frame.
		//
		if (IS_EQUAL_CIE_SRC(ieee, addr2))
			UPDATE_CIE_WATCHDOG(ieee);
	}
}

int ieee80211_parse_info_param(struct ieee80211_device *ieee,
		struct ieee80211_info_element *info_element,
		u16 length,
		struct ieee80211_network *network,
		struct ieee80211_rx_stats *stats)
{
	u8 i;
	short offset;
	u16	tmp_htcap_len = 0;
	u16	tmp_htinfo_len = 0;
	u16 ht_realtek_agg_len = 0;
	u8  ht_realtek_agg_buf[MAX_IE_LEN];
//	u16 broadcom_len = 0;
#ifdef CONFIG_IEEE80211_DEBUG
	char rates_str[64];
	char *p;
#endif

	while (length >= sizeof(*info_element)) {
		if (sizeof(*info_element) + info_element->len > length) {
			IEEE80211_DEBUG_MGMT("Info elem: parse failed: "
					     "info_element->len + 2 > left : "
					     "info_element->len+2=%zd left=%d, id=%d.\n",
					     info_element->len +
					     sizeof(*info_element),
					     length, info_element->id);
			/* We stop processing but don't return an error here
			 * because some misbehaviour APs break this rule. ie.
			 * Orinoco AP1000. */
			break;
		}

		switch (info_element->id) {
		case MFIE_TYPE_SSID:
			if (ieee80211_is_empty_essid(info_element->data,
						     info_element->len)) {
				network->flags |= NETWORK_EMPTY_ESSID;
				break;
			}

			network->ssid_len = min(info_element->len,
						(u8)IW_ESSID_MAX_SIZE);
			memcpy(network->ssid, info_element->data, network->ssid_len);
			if (network->ssid_len < IW_ESSID_MAX_SIZE)
				memset(network->ssid + network->ssid_len, 0,
				       IW_ESSID_MAX_SIZE - network->ssid_len);

			IEEE80211_DEBUG_MGMT("MFIE_TYPE_SSID: '%s' len=%d.\n",
					     network->ssid, network->ssid_len);
			break;

		case MFIE_TYPE_RATES:
#ifdef CONFIG_IEEE80211_DEBUG
			p = rates_str;
#endif
			network->rates_len = min(info_element->len,
						 MAX_RATES_LENGTH);
			for (i = 0; i < network->rates_len; i++) {
				network->rates[i] = info_element->data[i];
#ifdef CONFIG_IEEE80211_DEBUG
				p += scnprintf(p, sizeof(rates_str) -
					      (p - rates_str), "%02X ",
					      network->rates[i]);
#endif
				if (ieee80211_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    IEEE80211_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			IEEE80211_DEBUG_MGMT("MFIE_TYPE_RATES: '%s' (%d)\n",
					     rates_str, network->rates_len);
			break;

		case MFIE_TYPE_RATES_EX:
#ifdef CONFIG_IEEE80211_DEBUG
			p = rates_str;
#endif
			network->rates_ex_len = min(info_element->len,
						    MAX_RATES_EX_LENGTH);
			for (i = 0; i < network->rates_ex_len; i++) {
				network->rates_ex[i] = info_element->data[i];
#ifdef CONFIG_IEEE80211_DEBUG
				p += scnprintf(p, sizeof(rates_str) -
					      (p - rates_str), "%02X ",
					      network->rates_ex[i]);
#endif
				if (ieee80211_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    IEEE80211_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			IEEE80211_DEBUG_MGMT("MFIE_TYPE_RATES_EX: '%s' (%d)\n",
					     rates_str, network->rates_ex_len);
			break;

		case MFIE_TYPE_DS_SET:
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_DS_SET: %d\n",
					     info_element->data[0]);
			network->channel = info_element->data[0];
			break;

		case MFIE_TYPE_FH_SET:
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_FH_SET: ignored\n");
			break;

		case MFIE_TYPE_CF_SET:
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_CF_SET: ignored\n");
			break;

		case MFIE_TYPE_TIM:
			if (info_element->len < 4)
				break;

			network->tim.tim_count = info_element->data[0];
			network->tim.tim_period = info_element->data[1];

			network->dtim_period = info_element->data[1];
			if (ieee->state != IEEE80211_LINKED)
				break;

			network->last_dtim_sta_time[0] = stats->mac_time[0];
			network->last_dtim_sta_time[1] = stats->mac_time[1];

			network->dtim_data = IEEE80211_DTIM_VALID;

			if (info_element->data[0] != 0)
				break;

			if (info_element->data[2] & 1)
				network->dtim_data |= IEEE80211_DTIM_MBCAST;

			offset = (info_element->data[2] >> 1) * 2;

			if (ieee->assoc_id < 8 * offset ||
				ieee->assoc_id > 8 * (offset + info_element->len - 3))

				break;

			offset = (ieee->assoc_id / 8) - offset;// + ((aid % 8)? 0 : 1) ;

			if (info_element->data[3 + offset] & (1 << (ieee->assoc_id % 8)))
				network->dtim_data |= IEEE80211_DTIM_UCAST;

			//IEEE80211_DEBUG_MGMT("MFIE_TYPE_TIM: partially ignored\n");
			break;

		case MFIE_TYPE_ERP:
			network->erp_value = info_element->data[0];
			network->flags |= NETWORK_HAS_ERP_VALUE;
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_ERP_SET: %d\n",
					     network->erp_value);
			break;
		case MFIE_TYPE_IBSS_SET:
			network->atim_window = info_element->data[0];
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_IBSS_SET: %d\n",
					     network->atim_window);
			break;

		case MFIE_TYPE_CHALLENGE:
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_CHALLENGE: ignored\n");
			break;

		case MFIE_TYPE_GENERIC:
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_GENERIC: %d bytes\n",
					     info_element->len);
			if (!ieee80211_parse_qos_info_param_IE(info_element,
							       network))
				break;

			if (info_element->len >= 4 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x50 &&
			    info_element->data[2] == 0xf2 &&
			    info_element->data[3] == 0x01) {
				network->wpa_ie_len = min(info_element->len + 2,
							  MAX_WPA_IE_LEN);
				memcpy(network->wpa_ie, info_element,
				       network->wpa_ie_len);
				break;
			}

#ifdef THOMAS_TURBO
			if (info_element->len == 7 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0xe0 &&
			    info_element->data[2] == 0x4c &&
			    info_element->data[3] == 0x01 &&
			    info_element->data[4] == 0x02) {
				network->Turbo_Enable = 1;
			}
#endif

			//for HTcap and HTinfo parameters
			if (tmp_htcap_len == 0) {
				if (info_element->len >= 4 &&
				   info_element->data[0] == 0x00 &&
				   info_element->data[1] == 0x90 &&
				   info_element->data[2] == 0x4c &&
				   info_element->data[3] == 0x033){

					tmp_htcap_len = min(info_element->len, (u8)MAX_IE_LEN);
					if (tmp_htcap_len != 0) {
						network->bssht.bdHTSpecVer = HT_SPEC_VER_EWC;
						network->bssht.bdHTCapLen = tmp_htcap_len > sizeof(network->bssht.bdHTCapBuf) ? \
							sizeof(network->bssht.bdHTCapBuf) : tmp_htcap_len;
						memcpy(network->bssht.bdHTCapBuf, info_element->data, network->bssht.bdHTCapLen);
					}
				}
				if (tmp_htcap_len != 0)
					network->bssht.bdSupportHT = true;
				else
					network->bssht.bdSupportHT = false;
			}


			if (tmp_htinfo_len == 0) {
				if (info_element->len >= 4 &&
					info_element->data[0] == 0x00 &&
					info_element->data[1] == 0x90 &&
					info_element->data[2] == 0x4c &&
					info_element->data[3] == 0x034){

					tmp_htinfo_len = min(info_element->len, (u8)MAX_IE_LEN);
					if (tmp_htinfo_len != 0) {
						network->bssht.bdHTSpecVer = HT_SPEC_VER_EWC;
						if (tmp_htinfo_len) {
							network->bssht.bdHTInfoLen = tmp_htinfo_len > sizeof(network->bssht.bdHTInfoBuf) ? \
								sizeof(network->bssht.bdHTInfoBuf) : tmp_htinfo_len;
							memcpy(network->bssht.bdHTInfoBuf, info_element->data, network->bssht.bdHTInfoLen);
						}

					}

				}
			}

			if (ieee->aggregation) {
				if (network->bssht.bdSupportHT) {
					if (info_element->len >= 4 &&
						info_element->data[0] == 0x00 &&
						info_element->data[1] == 0xe0 &&
						info_element->data[2] == 0x4c &&
						info_element->data[3] == 0x02){

						ht_realtek_agg_len = min(info_element->len, (u8)MAX_IE_LEN);
						memcpy(ht_realtek_agg_buf, info_element->data, info_element->len);

					}
					if (ht_realtek_agg_len >= 5) {
						network->bssht.bdRT2RTAggregation = true;

						if ((ht_realtek_agg_buf[4] == 1) && (ht_realtek_agg_buf[5] & 0x02))
							network->bssht.bdRT2RTLongSlotTime = true;
					}
				}

			}

			//if(tmp_htcap_len !=0  ||  tmp_htinfo_len != 0)
			{
				if ((info_element->len >= 3 &&
					 info_element->data[0] == 0x00 &&
					 info_element->data[1] == 0x05 &&
					 info_element->data[2] == 0xb5) ||
					 (info_element->len >= 3 &&
					 info_element->data[0] == 0x00 &&
					 info_element->data[1] == 0x0a &&
					 info_element->data[2] == 0xf7) ||
					 (info_element->len >= 3 &&
					 info_element->data[0] == 0x00 &&
					 info_element->data[1] == 0x10 &&
					 info_element->data[2] == 0x18)){

					network->broadcom_cap_exist = true;

				}
			}
			if (info_element->len >= 3 &&
				info_element->data[0] == 0x00 &&
				info_element->data[1] == 0x0c &&
				info_element->data[2] == 0x43) {
				network->ralink_cap_exist = true;
			} else
				network->ralink_cap_exist = false;
			//added by amy for atheros AP
			if ((info_element->len >= 3 &&
				info_element->data[0] == 0x00 &&
				info_element->data[1] == 0x03 &&
				info_element->data[2] == 0x7f) ||
				(info_element->len >= 3 &&
				info_element->data[0] == 0x00 &&
				info_element->data[1] == 0x13 &&
				info_element->data[2] == 0x74)) {
				netdev_dbg(ieee->dev, "========> athros AP is exist\n");
				network->atheros_cap_exist = true;
			} else
				network->atheros_cap_exist = false;

			if (info_element->len >= 3 &&
				info_element->data[0] == 0x00 &&
				info_element->data[1] == 0x40 &&
				info_element->data[2] == 0x96) {
				network->cisco_cap_exist = true;
			} else
				network->cisco_cap_exist = false;
			//added by amy for LEAP of cisco
			if (info_element->len > 4 &&
				info_element->data[0] == 0x00 &&
				info_element->data[1] == 0x40 &&
				info_element->data[2] == 0x96 &&
				info_element->data[3] == 0x01) {
				if (info_element->len == 6) {
					memcpy(network->CcxRmState, &info_element[4], 2);
					if (network->CcxRmState[0] != 0)
						network->bCcxRmEnable = true;
					else
						network->bCcxRmEnable = false;
					//
					// CCXv4 Table 59-1 MBSSID Masks.
					//
					network->MBssidMask = network->CcxRmState[1] & 0x07;
					if (network->MBssidMask != 0) {
						network->bMBssidValid = true;
						network->MBssidMask = 0xff << (network->MBssidMask);
						ether_addr_copy(network->MBssid, network->bssid);
						network->MBssid[5] &= network->MBssidMask;
					} else {
						network->bMBssidValid = false;
					}
				} else {
					network->bCcxRmEnable = false;
				}
			}
			if (info_element->len > 4  &&
				info_element->data[0] == 0x00 &&
				info_element->data[1] == 0x40 &&
				info_element->data[2] == 0x96 &&
				info_element->data[3] == 0x03) {
				if (info_element->len == 5) {
					network->bWithCcxVerNum = true;
					network->BssCcxVerNumber = info_element->data[4];
				} else {
					network->bWithCcxVerNum = false;
					network->BssCcxVerNumber = 0;
				}
			}
			break;

		case MFIE_TYPE_RSN:
			IEEE80211_DEBUG_MGMT("MFIE_TYPE_RSN: %d bytes\n",
					     info_element->len);
			network->rsn_ie_len = min(info_element->len + 2,
						  MAX_WPA_IE_LEN);
			memcpy(network->rsn_ie, info_element,
			       network->rsn_ie_len);
			break;

			//HT related element.
		case MFIE_TYPE_HT_CAP:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_HT_CAP: %d bytes\n",
					     info_element->len);
			tmp_htcap_len = min(info_element->len, (u8)MAX_IE_LEN);
			if (tmp_htcap_len != 0) {
				network->bssht.bdHTSpecVer = HT_SPEC_VER_EWC;
				network->bssht.bdHTCapLen = tmp_htcap_len > sizeof(network->bssht.bdHTCapBuf) ? \
					sizeof(network->bssht.bdHTCapBuf) : tmp_htcap_len;
				memcpy(network->bssht.bdHTCapBuf, info_element->data, network->bssht.bdHTCapLen);

				//If peer is HT, but not WMM, call QosSetLegacyWMMParamWithHT()
				// windows driver will update WMM parameters each beacon received once connected
				// Linux driver is a bit different.
				network->bssht.bdSupportHT = true;
			} else
				network->bssht.bdSupportHT = false;
			break;


		case MFIE_TYPE_HT_INFO:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_HT_INFO: %d bytes\n",
					     info_element->len);
			tmp_htinfo_len = min(info_element->len, (u8)MAX_IE_LEN);
			if (tmp_htinfo_len) {
				network->bssht.bdHTSpecVer = HT_SPEC_VER_IEEE;
				network->bssht.bdHTInfoLen = tmp_htinfo_len > sizeof(network->bssht.bdHTInfoBuf) ? \
					sizeof(network->bssht.bdHTInfoBuf) : tmp_htinfo_len;
				memcpy(network->bssht.bdHTInfoBuf, info_element->data, network->bssht.bdHTInfoLen);
			}
			break;

		case MFIE_TYPE_AIRONET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_AIRONET: %d bytes\n",
					     info_element->len);
			if (info_element->len > IE_CISCO_FLAG_POSITION) {
				network->bWithAironetIE = true;

				// CCX 1 spec v1.13, A01.1 CKIP Negotiation (page23):
				// "A Cisco access point advertises support for CKIP in beacon and probe response packets,
				//  by adding an Aironet element and setting one or both of the CKIP negotiation bits."
				if ((info_element->data[IE_CISCO_FLAG_POSITION] & SUPPORT_CKIP_MIC)	||
					(info_element->data[IE_CISCO_FLAG_POSITION] & SUPPORT_CKIP_PK)) {
					network->bCkipSupported = true;
				} else {
					network->bCkipSupported = false;
				}
			} else {
				network->bWithAironetIE = false;
				network->bCkipSupported = false;
			}
			break;
		case MFIE_TYPE_QOS_PARAMETER:
			netdev_err(ieee->dev,
				   "QoS Error need to parse QOS_PARAMETER IE\n");
			break;

		case MFIE_TYPE_COUNTRY:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_COUNTRY: %d bytes\n",
					     info_element->len);
			ieee80211_extract_country_ie(ieee, info_element, network, network->bssid);//addr2 is same as addr3 when from an AP
			break;
/* TODO */
		default:
			IEEE80211_DEBUG_MGMT
			    ("Unsupported info element: %s (%d)\n",
			     get_info_element_string(info_element->id),
			     info_element->id);
			break;
		}

		length -= sizeof(*info_element) + info_element->len;
		info_element =
		    (struct ieee80211_info_element *)&info_element->
		    data[info_element->len];
	}

	if (!network->atheros_cap_exist && !network->broadcom_cap_exist &&
		!network->cisco_cap_exist && !network->ralink_cap_exist && !network->bssht.bdRT2RTAggregation) {
		network->unknown_cap_exist = true;
	} else {
		network->unknown_cap_exist = false;
	}
	return 0;
}

static inline u8 ieee80211_SignalStrengthTranslate(
	u8  CurrSS
	)
{
	u8 RetSS;

	// Step 1. Scale mapping.
	if (CurrSS >= 71 && CurrSS <= 100) {
		RetSS = 90 + ((CurrSS - 70) / 3);
	} else if (CurrSS >= 41 && CurrSS <= 70) {
		RetSS = 78 + ((CurrSS - 40) / 3);
	} else if (CurrSS >= 31 && CurrSS <= 40) {
		RetSS = 66 + (CurrSS - 30);
	} else if (CurrSS >= 21 && CurrSS <= 30) {
		RetSS = 54 + (CurrSS - 20);
	} else if (CurrSS >= 5 && CurrSS <= 20) {
		RetSS = 42 + (((CurrSS - 5) * 2) / 3);
	} else if (CurrSS == 4) {
		RetSS = 36;
	} else if (CurrSS == 3) {
		RetSS = 27;
	} else if (CurrSS == 2) {
		RetSS = 18;
	} else if (CurrSS == 1) {
		RetSS = 9;
	} else {
		RetSS = CurrSS;
	}
	//RT_TRACE(COMP_DBG, DBG_LOUD, ("##### After Mapping:  LastSS: %d, CurrSS: %d, RetSS: %d\n", LastSS, CurrSS, RetSS));

	// Step 2. Smoothing.

	//RT_TRACE(COMP_DBG, DBG_LOUD, ("$$$$$ After Smoothing:  LastSS: %d, CurrSS: %d, RetSS: %d\n", LastSS, CurrSS, RetSS));

	return RetSS;
}

/* 0-100 index */
static long ieee80211_translate_todbm(u8 signal_strength_index)
{
	long	signal_power; // in dBm.

	// Translate to dBm (x=0.5y-95).
	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;

	return signal_power;
}

static inline int ieee80211_network_init(
	struct ieee80211_device *ieee,
	struct ieee80211_probe_response *beacon,
	struct ieee80211_network *network,
	struct ieee80211_rx_stats *stats)
{
#ifdef CONFIG_IEEE80211_DEBUG
	//char rates_str[64];
	//char *p;
#endif

	network->qos_data.active = 0;
	network->qos_data.supported = 0;
	network->qos_data.param_count = 0;
	network->qos_data.old_param_count = 0;

	/* Pull out fixed field data */
	memcpy(network->bssid, beacon->header.addr3, ETH_ALEN);
	network->capability = le16_to_cpu(beacon->capability);
	network->last_scanned = jiffies;
	network->time_stamp[0] = le32_to_cpu(beacon->time_stamp[0]);
	network->time_stamp[1] = le32_to_cpu(beacon->time_stamp[1]);
	network->beacon_interval = le16_to_cpu(beacon->beacon_interval);
	/* Where to pull this? beacon->listen_interval;*/
	network->listen_interval = 0x0A;
	network->rates_len = network->rates_ex_len = 0;
	network->last_associate = 0;
	network->ssid_len = 0;
	network->flags = 0;
	network->atim_window = 0;
	network->erp_value = (network->capability & WLAN_CAPABILITY_IBSS) ?
	    0x3 : 0x0;
	network->berp_info_valid = false;
	network->broadcom_cap_exist = false;
	network->ralink_cap_exist = false;
	network->atheros_cap_exist = false;
	network->cisco_cap_exist = false;
	network->unknown_cap_exist = false;
#ifdef THOMAS_TURBO
	network->Turbo_Enable = 0;
#endif
	network->CountryIeLen = 0;
	memset(network->CountryIeBuf, 0, MAX_IE_LEN);
//Initialize HT parameters
	//ieee80211_ht_initialize(&network->bssht);
	HTInitializeBssDesc(&network->bssht);
	if (stats->freq == IEEE80211_52GHZ_BAND) {
		/* for A band (No DS info) */
		network->channel = stats->received_channel;
	} else
		network->flags |= NETWORK_HAS_CCK;

	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;

	if (ieee80211_parse_info_param
	    (ieee, beacon->info_element, stats->len - sizeof(*beacon), network, stats))
		return 1;

	network->mode = 0;
	if (stats->freq == IEEE80211_52GHZ_BAND)
		network->mode = IEEE_A;
	else {
		if (network->flags & NETWORK_HAS_OFDM)
			network->mode |= IEEE_G;
		if (network->flags & NETWORK_HAS_CCK)
			network->mode |= IEEE_B;
	}

	if (network->mode == 0) {
		IEEE80211_DEBUG_SCAN("Filtered out '%s (%pM)' "
				     "network.\n",
				     escape_essid(network->ssid,
						  network->ssid_len),
				     network->bssid);
		return 1;
	}

	if (network->bssht.bdSupportHT) {
		if (network->mode == IEEE_A)
			network->mode = IEEE_N_5G;
		else if (network->mode & (IEEE_G | IEEE_B))
			network->mode = IEEE_N_24G;
	}
	if (ieee80211_is_empty_essid(network->ssid, network->ssid_len))
		network->flags |= NETWORK_EMPTY_ESSID;

	stats->signal = 30 + (stats->SignalStrength * 70) / 100;
	//stats->signal = ieee80211_SignalStrengthTranslate(stats->signal);
	stats->noise = ieee80211_translate_todbm((u8)(100 - stats->signal)) - 25;

	memcpy(&network->stats, stats, sizeof(network->stats));

	return 0;
}

static inline int is_same_network(struct ieee80211_network *src,
				  struct ieee80211_network *dst, struct ieee80211_device *ieee)
{
	/* A network is only a duplicate if the channel, BSSID, ESSID
	 * and the capability field (in particular IBSS and BSS) all match.
	 * We treat all <hidden> with the same BSSID and channel
	 * as one network */
	return //((src->ssid_len == dst->ssid_len) &&
		(((src->ssid_len == dst->ssid_len) || (ieee->iw_mode == IW_MODE_INFRA)) &&
		(src->channel == dst->channel) &&
		!memcmp(src->bssid, dst->bssid, ETH_ALEN) &&
		//!memcmp(src->ssid, dst->ssid, src->ssid_len) &&
		(!memcmp(src->ssid, dst->ssid, src->ssid_len) || (ieee->iw_mode == IW_MODE_INFRA)) &&
		((src->capability & WLAN_CAPABILITY_IBSS) ==
		(dst->capability & WLAN_CAPABILITY_IBSS)) &&
		((src->capability & WLAN_CAPABILITY_BSS) ==
		(dst->capability & WLAN_CAPABILITY_BSS)));
}

static inline void update_network(struct ieee80211_network *dst,
				  struct ieee80211_network *src)
{
	int qos_active;
	u8 old_param;

	memcpy(&dst->stats, &src->stats, sizeof(struct ieee80211_rx_stats));
	dst->capability = src->capability;
	memcpy(dst->rates, src->rates, src->rates_len);
	dst->rates_len = src->rates_len;
	memcpy(dst->rates_ex, src->rates_ex, src->rates_ex_len);
	dst->rates_ex_len = src->rates_ex_len;
	if (src->ssid_len > 0) {
		memset(dst->ssid, 0, dst->ssid_len);
		dst->ssid_len = src->ssid_len;
		memcpy(dst->ssid, src->ssid, src->ssid_len);
	}
	dst->mode = src->mode;
	dst->flags = src->flags;
	dst->time_stamp[0] = src->time_stamp[0];
	dst->time_stamp[1] = src->time_stamp[1];
	if (src->flags & NETWORK_HAS_ERP_VALUE) {
		dst->erp_value = src->erp_value;
		dst->berp_info_valid = src->berp_info_valid = true;
	}
	dst->beacon_interval = src->beacon_interval;
	dst->listen_interval = src->listen_interval;
	dst->atim_window = src->atim_window;
	dst->dtim_period = src->dtim_period;
	dst->dtim_data = src->dtim_data;
	dst->last_dtim_sta_time[0] = src->last_dtim_sta_time[0];
	dst->last_dtim_sta_time[1] = src->last_dtim_sta_time[1];
	memcpy(&dst->tim, &src->tim, sizeof(struct ieee80211_tim_parameters));

	dst->bssht.bdSupportHT = src->bssht.bdSupportHT;
	dst->bssht.bdRT2RTAggregation = src->bssht.bdRT2RTAggregation;
	dst->bssht.bdHTCapLen = src->bssht.bdHTCapLen;
	memcpy(dst->bssht.bdHTCapBuf, src->bssht.bdHTCapBuf, src->bssht.bdHTCapLen);
	dst->bssht.bdHTInfoLen = src->bssht.bdHTInfoLen;
	memcpy(dst->bssht.bdHTInfoBuf, src->bssht.bdHTInfoBuf, src->bssht.bdHTInfoLen);
	dst->bssht.bdHTSpecVer = src->bssht.bdHTSpecVer;
	dst->bssht.bdRT2RTLongSlotTime = src->bssht.bdRT2RTLongSlotTime;
	dst->broadcom_cap_exist = src->broadcom_cap_exist;
	dst->ralink_cap_exist = src->ralink_cap_exist;
	dst->atheros_cap_exist = src->atheros_cap_exist;
	dst->cisco_cap_exist = src->cisco_cap_exist;
	dst->unknown_cap_exist = src->unknown_cap_exist;
	memcpy(dst->wpa_ie, src->wpa_ie, src->wpa_ie_len);
	dst->wpa_ie_len = src->wpa_ie_len;
	memcpy(dst->rsn_ie, src->rsn_ie, src->rsn_ie_len);
	dst->rsn_ie_len = src->rsn_ie_len;

	dst->last_scanned = jiffies;
	/* qos related parameters */
	//qos_active = src->qos_data.active;
	qos_active = dst->qos_data.active;
	//old_param = dst->qos_data.old_param_count;
	old_param = dst->qos_data.param_count;
	if (dst->flags & NETWORK_HAS_QOS_MASK)
		memcpy(&dst->qos_data, &src->qos_data,
			sizeof(struct ieee80211_qos_data));
	else {
		dst->qos_data.supported = src->qos_data.supported;
		dst->qos_data.param_count = src->qos_data.param_count;
	}

	if (dst->qos_data.supported == 1) {
		dst->QoS_Enable = 1;
		if (dst->ssid_len)
			IEEE80211_DEBUG_QOS
				("QoS the network %s is QoS supported\n",
				dst->ssid);
		else
			IEEE80211_DEBUG_QOS
				("QoS the network is QoS supported\n");
	}
	dst->qos_data.active = qos_active;
	dst->qos_data.old_param_count = old_param;

	/* dst->last_associate is not overwritten */
	dst->wmm_info = src->wmm_info; //sure to exist in beacon or probe response frame.
	if (src->wmm_param[0].aci_aifsn || \
	   src->wmm_param[1].aci_aifsn || \
	   src->wmm_param[2].aci_aifsn || \
	   src->wmm_param[3].aci_aifsn) {
		memcpy(dst->wmm_param, src->wmm_param, WME_AC_PRAM_LEN);
	}
	//dst->QoS_Enable = src->QoS_Enable;
#ifdef THOMAS_TURBO
	dst->Turbo_Enable = src->Turbo_Enable;
#endif

	dst->CountryIeLen = src->CountryIeLen;
	memcpy(dst->CountryIeBuf, src->CountryIeBuf, src->CountryIeLen);

	//added by amy for LEAP
	dst->bWithAironetIE = src->bWithAironetIE;
	dst->bCkipSupported = src->bCkipSupported;
	memcpy(dst->CcxRmState, src->CcxRmState, 2);
	dst->bCcxRmEnable = src->bCcxRmEnable;
	dst->MBssidMask = src->MBssidMask;
	dst->bMBssidValid = src->bMBssidValid;
	memcpy(dst->MBssid, src->MBssid, 6);
	dst->bWithCcxVerNum = src->bWithCcxVerNum;
	dst->BssCcxVerNumber = src->BssCcxVerNumber;

}

static inline int is_beacon(__le16 fc)
{
	return (WLAN_FC_GET_STYPE(le16_to_cpu(fc)) == IEEE80211_STYPE_BEACON);
}

static inline void ieee80211_process_probe_response(
	struct ieee80211_device *ieee,
	struct ieee80211_probe_response *beacon,
	struct ieee80211_rx_stats *stats)
{
	struct ieee80211_network *network;
	struct ieee80211_network *target;
	struct ieee80211_network *oldest = NULL;
#ifdef CONFIG_IEEE80211_DEBUG
	struct ieee80211_info_element *info_element = &beacon->info_element[0];
#endif
	int fc = WLAN_FC_GET_STYPE(le16_to_cpu(beacon->header.frame_ctl));
	unsigned long flags;
	short renew;
	u16 capability;
	//u8 wmm_info;

	network = kzalloc(sizeof(*network), GFP_ATOMIC);
	if (!network)
		goto out;

	capability = le16_to_cpu(beacon->capability);
	IEEE80211_DEBUG_SCAN(
		"'%s' (%pM): %c%c%c%c %c%c%c%c-%c%c%c%c %c%c%c%c\n",
		escape_essid(info_element->data, info_element->len),
		beacon->header.addr3,
		(capability & BIT(0xf)) ? '1' : '0',
		(capability & BIT(0xe)) ? '1' : '0',
		(capability & BIT(0xd)) ? '1' : '0',
		(capability & BIT(0xc)) ? '1' : '0',
		(capability & BIT(0xb)) ? '1' : '0',
		(capability & BIT(0xa)) ? '1' : '0',
		(capability & BIT(0x9)) ? '1' : '0',
		(capability & BIT(0x8)) ? '1' : '0',
		(capability & BIT(0x7)) ? '1' : '0',
		(capability & BIT(0x6)) ? '1' : '0',
		(capability & BIT(0x5)) ? '1' : '0',
		(capability & BIT(0x4)) ? '1' : '0',
		(capability & BIT(0x3)) ? '1' : '0',
		(capability & BIT(0x2)) ? '1' : '0',
		(capability & BIT(0x1)) ? '1' : '0',
		(capability & BIT(0x0)) ? '1' : '0');

	if (ieee80211_network_init(ieee, beacon, network, stats)) {
		IEEE80211_DEBUG_SCAN("Dropped '%s' (%pM) via %s.\n",
				     escape_essid(info_element->data,
						  info_element->len),
				     beacon->header.addr3,
				     fc == IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
		goto out;
	}

	// For Asus EeePc request,
	// (1) if wireless adapter receive get any 802.11d country code in AP beacon,
	//	   wireless adapter should follow the country code.
	// (2)  If there is no any country code in beacon,
	//       then wireless adapter should do active scan from ch1~11 and
	//       passive scan from ch12~14

	if (!is_legal_channel(ieee, network->channel))
		goto out;
	if (ieee->bGlobalDomain) {
		if (fc == IEEE80211_STYPE_PROBE_RESP) {
			if (IS_COUNTRY_IE_VALID(ieee)) {
				// Case 1: Country code
				if (!is_legal_channel(ieee, network->channel)) {
					netdev_warn(ieee->dev, "GetScanInfo(): For Country code, filter probe response at channel(%d).\n", network->channel);
					goto out;
				}
			} else {
				// Case 2: No any country code.
				// Filter over channel ch12~14
				if (network->channel > 11) {
					netdev_warn(ieee->dev, "GetScanInfo(): For Global Domain, filter probe response at channel(%d).\n", network->channel);
					goto out;
				}
			}
		} else {
			if (IS_COUNTRY_IE_VALID(ieee)) {
				// Case 1: Country code
				if (!is_legal_channel(ieee, network->channel)) {
					netdev_warn(ieee->dev, "GetScanInfo(): For Country code, filter beacon at channel(%d).\n", network->channel);
					goto out;
				}
			} else {
				// Case 2: No any country code.
				// Filter over channel ch12~14
				if (network->channel > 14) {
					netdev_warn(ieee->dev, "GetScanInfo(): For Global Domain, filter beacon at channel(%d).\n", network->channel);
					goto out;
				}
			}
		}
	}

	/* The network parsed correctly -- so now we scan our known networks
	 * to see if we can find it in our list.
	 *
	 * NOTE:  This search is definitely not optimized.  Once its doing
	 *        the "right thing" we'll optimize it for efficiency if
	 *        necessary */

	/* Search for this entry in the list and update it if it is
	 * already there. */

	spin_lock_irqsave(&ieee->lock, flags);

	if (is_same_network(&ieee->current_network, network, ieee)) {
		update_network(&ieee->current_network, network);
		if ((ieee->current_network.mode == IEEE_N_24G || ieee->current_network.mode == IEEE_G)
		    && ieee->current_network.berp_info_valid){
			if (ieee->current_network.erp_value & ERP_UseProtection)
				ieee->current_network.buseprotection = true;
			else
				ieee->current_network.buseprotection = false;
		}
		if (is_beacon(beacon->header.frame_ctl)) {
			if (ieee->state == IEEE80211_LINKED)
				ieee->LinkDetectInfo.NumRecvBcnInPeriod++;
		} else //hidden AP
			network->flags = (~NETWORK_EMPTY_ESSID & network->flags) | (NETWORK_EMPTY_ESSID & ieee->current_network.flags);
	}

	list_for_each_entry(target, &ieee->network_list, list) {
		if (is_same_network(target, network, ieee))
			break;
		if (!oldest ||
		    (target->last_scanned < oldest->last_scanned))
			oldest = target;
	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (&target->list == &ieee->network_list) {
		if (list_empty(&ieee->network_free_list)) {
			/* If there are no more slots, expire the oldest */
			list_del(&oldest->list);
			target = oldest;
			IEEE80211_DEBUG_SCAN("Expired '%s' (%pM) from "
					     "network list.\n",
					     escape_essid(target->ssid,
							  target->ssid_len),
					     target->bssid);
		} else {
			/* Otherwise just pull from the free list */
			target = list_entry(ieee->network_free_list.next,
					    struct ieee80211_network, list);
			list_del(ieee->network_free_list.next);
		}


#ifdef CONFIG_IEEE80211_DEBUG
		IEEE80211_DEBUG_SCAN("Adding '%s' (%pM) via %s.\n",
				     escape_essid(network->ssid,
						  network->ssid_len),
				     network->bssid,
				     fc == IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
#endif
		memcpy(target, network, sizeof(*target));
		list_add_tail(&target->list, &ieee->network_list);
		if (ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE)
			ieee80211_softmac_new_net(ieee, network);
	} else {
		IEEE80211_DEBUG_SCAN("Updating '%s' (%pM) via %s.\n",
				     escape_essid(target->ssid,
						  target->ssid_len),
				     target->bssid,
				     fc == IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");

		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		renew = !time_after(target->last_scanned + ieee->scan_age, jiffies);
		//YJ,add,080819,for hidden ap
		if (is_beacon(beacon->header.frame_ctl) == 0)
			network->flags = (~NETWORK_EMPTY_ESSID & network->flags) | (NETWORK_EMPTY_ESSID & target->flags);
		//if(strncmp(network->ssid, "linksys-c",9) == 0)
		//	printk("====>2 network->ssid=%s FLAG=%d target.ssid=%s FLAG=%d\n", network->ssid, network->flags, target->ssid, target->flags);
		if (((network->flags & NETWORK_EMPTY_ESSID) == NETWORK_EMPTY_ESSID) \
		    && (((network->ssid_len > 0) && (strncmp(target->ssid, network->ssid, network->ssid_len)))\
 || ((ieee->current_network.ssid_len == network->ssid_len) && (strncmp(ieee->current_network.ssid, network->ssid, network->ssid_len) == 0) && (ieee->state == IEEE80211_NOLINK))))
			renew = 1;
		//YJ,add,080819,for hidden ap,end

		update_network(target, network);
		if (renew && (ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE))
			ieee80211_softmac_new_net(ieee, network);
	}

	spin_unlock_irqrestore(&ieee->lock, flags);
	if (is_beacon(beacon->header.frame_ctl) && is_same_network(&ieee->current_network, network, ieee) && \
		(ieee->state == IEEE80211_LINKED)) {
		if (ieee->handle_beacon)
			ieee->handle_beacon(ieee->dev, beacon, &ieee->current_network);
	}

out:
	kfree(network);
}

void ieee80211_rx_mgt(struct ieee80211_device *ieee,
		      struct rtl_80211_hdr_4addr *header,
		      struct ieee80211_rx_stats *stats)
{
	switch (WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl))) {

	case IEEE80211_STYPE_BEACON:
		IEEE80211_DEBUG_MGMT("received BEACON (%d)\n",
			WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl)));
		IEEE80211_DEBUG_SCAN("Beacon\n");
		ieee80211_process_probe_response(
			ieee, (struct ieee80211_probe_response *)header, stats);
		break;

	case IEEE80211_STYPE_PROBE_RESP:
		IEEE80211_DEBUG_MGMT("received PROBE RESPONSE (%d)\n",
			WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl)));
		IEEE80211_DEBUG_SCAN("Probe response\n");
		ieee80211_process_probe_response(
			ieee, (struct ieee80211_probe_response *)header, stats);
		break;

	}
}
EXPORT_SYMBOL(ieee80211_rx_mgt);
