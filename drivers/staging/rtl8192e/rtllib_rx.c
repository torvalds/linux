/*
 * Original code based Host AP (software wireless LAN access point) driver
 * for Intersil Prism2/2.5/3 - hostap.o module, common routines
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2004, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
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

#include "rtllib.h"
#include "dot11d.h"

static inline void rtllib_monitor_rx(struct rtllib_device *ieee,
				struct sk_buff *skb, struct rtllib_rx_stats *rx_status,
				size_t hdr_length)
{
	skb->dev = ieee->dev;
	skb_reset_mac_header(skb);
	skb_pull(skb, hdr_length);
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = __constant_htons(ETH_P_80211_RAW);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

/* Called only as a tasklet (software IRQ) */
static struct rtllib_frag_entry *
rtllib_frag_cache_find(struct rtllib_device *ieee, unsigned int seq,
			  unsigned int frag, u8 tid, u8 *src, u8 *dst)
{
	struct rtllib_frag_entry *entry;
	int i;

	for (i = 0; i < RTLLIB_FRAG_CACHE_LEN; i++) {
		entry = &ieee->frag_cache[tid][i];
		if (entry->skb != NULL &&
		    time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			RTLLIB_DEBUG_FRAG(
				"expiring fragment cache entry "
				"seq=%u last_frag=%u\n",
				entry->seq, entry->last_frag);
			dev_kfree_skb_any(entry->skb);
			entry->skb = NULL;
		}

		if (entry->skb != NULL && entry->seq == seq &&
		    (entry->last_frag + 1 == frag || frag == -1) &&
		    memcmp(entry->src_addr, src, ETH_ALEN) == 0 &&
		    memcmp(entry->dst_addr, dst, ETH_ALEN) == 0)
			return entry;
	}

	return NULL;
}

/* Called only as a tasklet (software IRQ) */
static struct sk_buff *
rtllib_frag_cache_get(struct rtllib_device *ieee,
			 struct rtllib_hdr_4addr *hdr)
{
	struct sk_buff *skb = NULL;
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	u16 sc = le16_to_cpu(hdr->seq_ctl);
	unsigned int frag = WLAN_GET_SEQ_FRAG(sc);
	unsigned int seq = WLAN_GET_SEQ_SEQ(sc);
	struct rtllib_frag_entry *entry;
	struct rtllib_hdr_3addrqos *hdr_3addrqos;
	struct rtllib_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & RTLLIB_FCTL_DSTODS) == RTLLIB_FCTL_DSTODS) && RTLLIB_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct rtllib_hdr_4addrqos *)hdr;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & RTLLIB_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (RTLLIB_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct rtllib_hdr_3addrqos *)hdr;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & RTLLIB_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
		tid = 0;
	}

	if (frag == 0) {
		/* Reserve enough space to fit maximum frame length */
		skb = dev_alloc_skb(ieee->dev->mtu +
				    sizeof(struct rtllib_hdr_4addr) +
				    8 /* LLC */ +
				    2 /* alignment */ +
				    8 /* WEP */ +
				    ETH_ALEN /* WDS */ +
				    (RTLLIB_QOS_HAS_SEQ(fc) ? 2 : 0) /* QOS Control */);
		if (skb == NULL)
			return NULL;

		entry = &ieee->frag_cache[tid][ieee->frag_next_idx[tid]];
		ieee->frag_next_idx[tid]++;
		if (ieee->frag_next_idx[tid] >= RTLLIB_FRAG_CACHE_LEN)
			ieee->frag_next_idx[tid] = 0;

		if (entry->skb != NULL)
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
		entry = rtllib_frag_cache_find(ieee, seq, frag, tid, hdr->addr2,
						  hdr->addr1);
		if (entry != NULL) {
			entry->last_frag = frag;
			skb = entry->skb;
		}
	}

	return skb;
}


/* Called only as a tasklet (software IRQ) */
static int rtllib_frag_cache_invalidate(struct rtllib_device *ieee,
					   struct rtllib_hdr_4addr *hdr)
{
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	u16 sc = le16_to_cpu(hdr->seq_ctl);
	unsigned int seq = WLAN_GET_SEQ_SEQ(sc);
	struct rtllib_frag_entry *entry;
	struct rtllib_hdr_3addrqos *hdr_3addrqos;
	struct rtllib_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & RTLLIB_FCTL_DSTODS) == RTLLIB_FCTL_DSTODS) && RTLLIB_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct rtllib_hdr_4addrqos *)hdr;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & RTLLIB_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (RTLLIB_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct rtllib_hdr_3addrqos *)hdr;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & RTLLIB_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
		tid = 0;
	}

	entry = rtllib_frag_cache_find(ieee, seq, -1, tid, hdr->addr2,
					  hdr->addr1);

	if (entry == NULL) {
		RTLLIB_DEBUG_FRAG(
			"could not invalidate fragment cache "
			"entry (seq=%u)\n", seq);
		return -1;
	}

	entry->skb = NULL;
	return 0;
}

/* rtllib_rx_frame_mgtmt
 *
 * Responsible for handling management control frames
 *
 * Called by rtllib_rx */
static inline int
rtllib_rx_frame_mgmt(struct rtllib_device *ieee, struct sk_buff *skb,
			struct rtllib_rx_stats *rx_stats, u16 type,
			u16 stype)
{
	/* On the struct stats definition there is written that
	 * this is not mandatory.... but seems that the probe
	 * response parser uses it
	 */
	struct rtllib_hdr_3addr *hdr = (struct rtllib_hdr_3addr *)skb->data;

	rx_stats->len = skb->len;
	rtllib_rx_mgt(ieee, skb, rx_stats);
	if ((memcmp(hdr->addr1, ieee->dev->dev_addr, ETH_ALEN))) {
		dev_kfree_skb_any(skb);
		return 0;
	}
	rtllib_rx_frame_softmac(ieee, skb, rx_stats, type, stype);

	dev_kfree_skb_any(skb);

	return 0;
}

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static unsigned char rfc1042_header[] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00
};
/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static unsigned char bridge_tunnel_header[] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8
};
/* No encapsulation header if EtherType < 0x600 (=length) */

/* Called by rtllib_rx_frame_decrypt */
static int rtllib_is_eapol_frame(struct rtllib_device *ieee,
				    struct sk_buff *skb, size_t hdrlen)
{
	struct net_device *dev = ieee->dev;
	u16 fc, ethertype;
	struct rtllib_hdr_4addr *hdr;
	u8 *pos;

	if (skb->len < 24)
		return 0;

	hdr = (struct rtllib_hdr_4addr *) skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);

	/* check that the frame is unicast frame to us */
	if ((fc & (RTLLIB_FCTL_TODS | RTLLIB_FCTL_FROMDS)) ==
	    RTLLIB_FCTL_TODS &&
	    memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN) == 0 &&
	    memcmp(hdr->addr3, dev->dev_addr, ETH_ALEN) == 0) {
		/* ToDS frame with own addr BSSID and DA */
	} else if ((fc & (RTLLIB_FCTL_TODS | RTLLIB_FCTL_FROMDS)) ==
		   RTLLIB_FCTL_FROMDS &&
		   memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN) == 0) {
		/* FromDS frame with own addr as DA */
	} else
		return 0;

	if (skb->len < 24 + 8)
		return 0;

	/* check for port access entity Ethernet type */
	pos = skb->data + hdrlen;
	ethertype = (pos[6] << 8) | pos[7];
	if (ethertype == ETH_P_PAE)
		return 1;

	return 0;
}

/* Called only as a tasklet (software IRQ), by rtllib_rx */
static inline int
rtllib_rx_frame_decrypt(struct rtllib_device *ieee, struct sk_buff *skb,
			struct lib80211_crypt_data *crypt)
{
	struct rtllib_hdr_4addr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_mpdu == NULL)
		return 0;

	if (ieee->hwsec_active) {
		struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->bHwSec = 1;

		if (ieee->need_sw_enc)
			tcb_desc->bHwSec = 0;
	}

	hdr = (struct rtllib_hdr_4addr *) skb->data;
	hdrlen = rtllib_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_mpdu(skb, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		RTLLIB_DEBUG_DROP(
			"decryption failed (SA= %pM"
			") res=%d\n", hdr->addr2, res);
		if (res == -2)
			RTLLIB_DEBUG_DROP("Decryption failed ICV "
					     "mismatch (key %d)\n",
					     skb->data[hdrlen + 3] >> 6);
		ieee->ieee_stats.rx_discards_undecryptable++;
		return -1;
	}

	return res;
}


/* Called only as a tasklet (software IRQ), by rtllib_rx */
static inline int
rtllib_rx_frame_decrypt_msdu(struct rtllib_device *ieee, struct sk_buff *skb,
			     int keyidx, struct lib80211_crypt_data *crypt)
{
	struct rtllib_hdr_4addr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_msdu == NULL)
		return 0;
	if (ieee->hwsec_active) {
		struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->bHwSec = 1;

		if (ieee->need_sw_enc)
			tcb_desc->bHwSec = 0;
	}

	hdr = (struct rtllib_hdr_4addr *) skb->data;
	hdrlen = rtllib_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_msdu(skb, keyidx, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_DEBUG "%s: MSDU decryption/MIC verification failed"
		       " (SA= %pM keyidx=%d)\n",
		       ieee->dev->name, hdr->addr2, keyidx);
		return -1;
	}

	return 0;
}


/* this function is stolen from ipw2200 driver*/
#define IEEE_PACKET_RETRY_TIME (5*HZ)
static int is_duplicate_packet(struct rtllib_device *ieee,
				      struct rtllib_hdr_4addr *header)
{
	u16 fc = le16_to_cpu(header->frame_ctl);
	u16 sc = le16_to_cpu(header->seq_ctl);
	u16 seq = WLAN_GET_SEQ_SEQ(sc);
	u16 frag = WLAN_GET_SEQ_FRAG(sc);
	u16 *last_seq, *last_frag;
	unsigned long *last_time;
	struct rtllib_hdr_3addrqos *hdr_3addrqos;
	struct rtllib_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & RTLLIB_FCTL_DSTODS) == RTLLIB_FCTL_DSTODS) && RTLLIB_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct rtllib_hdr_4addrqos *)header;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & RTLLIB_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (RTLLIB_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct rtllib_hdr_3addrqos *)header;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & RTLLIB_QCTL_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
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
		if (p == &ieee->ibss_mac_hash[index]) {
			entry = kmalloc(sizeof(struct ieee_ibss_seq), GFP_ATOMIC);
			if (!entry) {
				printk(KERN_WARNING "Cannot malloc new mac entry\n");
				return 0;
			}
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

	return 1;
}

static bool AddReorderEntry(struct rx_ts_record *pTS,
			    struct rx_reorder_entry *pReorderEntry)
{
	struct list_head *pList = &pTS->RxPendingPktList;

	while (pList->next != &pTS->RxPendingPktList) {
		if (SN_LESS(pReorderEntry->SeqNum, ((struct rx_reorder_entry *)
		    list_entry(pList->next, struct rx_reorder_entry,
		    List))->SeqNum))
			pList = pList->next;
		else if (SN_EQUAL(pReorderEntry->SeqNum,
			((struct rx_reorder_entry *)list_entry(pList->next,
			struct rx_reorder_entry, List))->SeqNum))
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

void rtllib_indicate_packets(struct rtllib_device *ieee, struct rtllib_rxb **prxbIndicateArray, u8 index)
{
	struct net_device_stats *stats = &ieee->stats;
	u8 i = 0 , j = 0;
	u16 ethertype;
	for (j = 0; j < index; j++) {
		struct rtllib_rxb *prxb = prxbIndicateArray[j];
		for (i = 0; i < prxb->nr_subframes; i++) {
			struct sk_buff *sub_skb = prxb->subframes[i];

		/* convert hdr + possible LLC headers into Ethernet header */
			ethertype = (sub_skb->data[6] << 8) | sub_skb->data[7];
			if (sub_skb->len >= 8 &&
			    ((memcmp(sub_skb->data, rfc1042_header, SNAP_SIZE) == 0 &&
			    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
			    memcmp(sub_skb->data, bridge_tunnel_header, SNAP_SIZE) == 0)) {
				/* remove RFC1042 or Bridge-Tunnel encapsulation
				 * and replace EtherType */
				skb_pull(sub_skb, SNAP_SIZE);
				memcpy(skb_push(sub_skb, ETH_ALEN), prxb->src, ETH_ALEN);
				memcpy(skb_push(sub_skb, ETH_ALEN), prxb->dst, ETH_ALEN);
			} else {
				u16 len;
			/* Leave Ethernet header part of hdr and full payload */
				len = sub_skb->len;
				memcpy(skb_push(sub_skb, 2), &len, 2);
				memcpy(skb_push(sub_skb, ETH_ALEN), prxb->src, ETH_ALEN);
				memcpy(skb_push(sub_skb, ETH_ALEN), prxb->dst, ETH_ALEN);
			}

			/* Indicate the packets to upper layer */
			if (sub_skb) {
				stats->rx_packets++;
				stats->rx_bytes += sub_skb->len;

				memset(sub_skb->cb, 0, sizeof(sub_skb->cb));
				sub_skb->protocol = eth_type_trans(sub_skb, ieee->dev);
				sub_skb->dev = ieee->dev;
				sub_skb->dev->stats.rx_packets++;
				sub_skb->dev->stats.rx_bytes += sub_skb->len;
				sub_skb->ip_summed = CHECKSUM_NONE; /* 802.11 crc not sufficient */
				ieee->last_rx_ps_time = jiffies;
				netif_rx(sub_skb);
			}
		}
		kfree(prxb);
		prxb = NULL;
	}
}

void rtllib_FlushRxTsPendingPkts(struct rtllib_device *ieee,	struct rx_ts_record *pTS)
{
	struct rx_reorder_entry *pRxReorderEntry;
	u8 RfdCnt = 0;

	del_timer_sync(&pTS->RxPktPendingTimer);
	while (!list_empty(&pTS->RxPendingPktList)) {
		if (RfdCnt >= REORDER_WIN_SIZE) {
			printk(KERN_INFO "-------------->%s() error! RfdCnt >= REORDER_WIN_SIZE\n", __func__);
			break;
		}

		pRxReorderEntry = (struct rx_reorder_entry *)list_entry(pTS->RxPendingPktList.prev, struct rx_reorder_entry, List);
		RTLLIB_DEBUG(RTLLIB_DL_REORDER, "%s(): Indicate SeqNum %d!\n", __func__, pRxReorderEntry->SeqNum);
		list_del_init(&pRxReorderEntry->List);

		ieee->RfdArray[RfdCnt] = pRxReorderEntry->prxb;

		RfdCnt = RfdCnt + 1;
		list_add_tail(&pRxReorderEntry->List, &ieee->RxReorder_Unused_List);
	}
	rtllib_indicate_packets(ieee, ieee->RfdArray, RfdCnt);

	pTS->RxIndicateSeq = 0xffff;
}

static void RxReorderIndicatePacket(struct rtllib_device *ieee,
				    struct rtllib_rxb *prxb,
				    struct rx_ts_record *pTS, u16 SeqNum)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	struct rx_reorder_entry *pReorderEntry = NULL;
	u8 WinSize = pHTInfo->RxReorderWinSize;
	u16 WinEnd = 0;
	u8 index = 0;
	bool bMatchWinStart = false, bPktInBuf = false;
	unsigned long flags;

	RTLLIB_DEBUG(RTLLIB_DL_REORDER, "%s(): Seq is %d, pTS->RxIndicateSeq"
		     " is %d, WinSize is %d\n", __func__, SeqNum,
		     pTS->RxIndicateSeq, WinSize);

	spin_lock_irqsave(&(ieee->reorder_spinlock), flags);

	WinEnd = (pTS->RxIndicateSeq + WinSize - 1) % 4096;
	/* Rx Reorder initialize condition.*/
	if (pTS->RxIndicateSeq == 0xffff)
		pTS->RxIndicateSeq = SeqNum;

	/* Drop out the packet which SeqNum is smaller than WinStart */
	if (SN_LESS(SeqNum, pTS->RxIndicateSeq)) {
		RTLLIB_DEBUG(RTLLIB_DL_REORDER, "Packet Drop! IndicateSeq: %d, NewSeq: %d\n",
				 pTS->RxIndicateSeq, SeqNum);
		pHTInfo->RxReorderDropCounter++;
		{
			int i;
			for (i = 0; i < prxb->nr_subframes; i++)
				dev_kfree_skb(prxb->subframes[i]);
			kfree(prxb);
			prxb = NULL;
		}
		spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
		return;
	}

	/*
	 * Sliding window manipulation. Conditions includes:
	 * 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	 * 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	 */
	if (SN_EQUAL(SeqNum, pTS->RxIndicateSeq)) {
		pTS->RxIndicateSeq = (pTS->RxIndicateSeq + 1) % 4096;
		bMatchWinStart = true;
	} else if (SN_LESS(WinEnd, SeqNum)) {
		if (SeqNum >= (WinSize - 1))
			pTS->RxIndicateSeq = SeqNum + 1 - WinSize;
		else
			pTS->RxIndicateSeq = 4095 - (WinSize - (SeqNum + 1)) + 1;
		RTLLIB_DEBUG(RTLLIB_DL_REORDER, "Window Shift! IndicateSeq: %d,"
			     " NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum);
	}

	/*
	 * Indication process.
	 * After Packet dropping and Sliding Window shifting as above, we can
	 * now just indicate the packets with the SeqNum smaller than latest
	 * WinStart and struct buffer other packets.
	 */
	/* For Rx Reorder condition:
	 * 1. All packets with SeqNum smaller than WinStart => Indicate
	 * 2. All packets with SeqNum larger than or equal to
	 *	 WinStart => Buffer it.
	 */
	if (bMatchWinStart) {
		/* Current packet is going to be indicated.*/
		RTLLIB_DEBUG(RTLLIB_DL_REORDER, "Packets indication!! "
				"IndicateSeq: %d, NewSeq: %d\n",
				pTS->RxIndicateSeq, SeqNum);
		ieee->prxbIndicateArray[0] = prxb;
		index = 1;
	} else {
		/* Current packet is going to be inserted into pending list.*/
		if (!list_empty(&ieee->RxReorder_Unused_List)) {
			pReorderEntry = (struct rx_reorder_entry *)
					list_entry(ieee->RxReorder_Unused_List.next,
					struct rx_reorder_entry, List);
			list_del_init(&pReorderEntry->List);

			/* Make a reorder entry and insert into a the packet list.*/
			pReorderEntry->SeqNum = SeqNum;
			pReorderEntry->prxb = prxb;

			if (!AddReorderEntry(pTS, pReorderEntry)) {
				RTLLIB_DEBUG(RTLLIB_DL_REORDER,
					     "%s(): Duplicate packet is "
					     "dropped!! IndicateSeq: %d, "
					     "NewSeq: %d\n",
					    __func__, pTS->RxIndicateSeq,
					    SeqNum);
				list_add_tail(&pReorderEntry->List,
					      &ieee->RxReorder_Unused_List); {
					int i;
					for (i = 0; i < prxb->nr_subframes; i++)
						dev_kfree_skb(prxb->subframes[i]);
					kfree(prxb);
					prxb = NULL;
				}
			} else {
				RTLLIB_DEBUG(RTLLIB_DL_REORDER,
					 "Pkt insert into struct buffer!! "
					 "IndicateSeq: %d, NewSeq: %d\n",
					 pTS->RxIndicateSeq, SeqNum);
			}
		} else {
			/*
			 * Packets are dropped if there are not enough reorder
			 * entries. This part should be modified!! We can just
			 * indicate all the packets in struct buffer and get
			 * reorder entries.
			 */
			RTLLIB_DEBUG(RTLLIB_DL_ERR, "RxReorderIndicatePacket():"
				     " There is no reorder entry!! Packet is "
				     "dropped!!\n");
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
	while (!list_empty(&pTS->RxPendingPktList)) {
		RTLLIB_DEBUG(RTLLIB_DL_REORDER, "%s(): start RREORDER indicate\n", __func__);

		pReorderEntry = (struct rx_reorder_entry *)list_entry(pTS->RxPendingPktList.prev,
				 struct rx_reorder_entry, List);
		if (SN_LESS(pReorderEntry->SeqNum, pTS->RxIndicateSeq) ||
				SN_EQUAL(pReorderEntry->SeqNum, pTS->RxIndicateSeq)) {
			/* This protect struct buffer from overflow. */
			if (index >= REORDER_WIN_SIZE) {
				RTLLIB_DEBUG(RTLLIB_DL_ERR, "RxReorderIndicate"
					     "Packet(): Buffer overflow!!\n");
				bPktInBuf = true;
				break;
			}

			list_del_init(&pReorderEntry->List);

			if (SN_EQUAL(pReorderEntry->SeqNum, pTS->RxIndicateSeq))
				pTS->RxIndicateSeq = (pTS->RxIndicateSeq + 1) % 4096;

			ieee->prxbIndicateArray[index] = pReorderEntry->prxb;
			RTLLIB_DEBUG(RTLLIB_DL_REORDER, "%s(): Indicate SeqNum"
				     " %d!\n", __func__, pReorderEntry->SeqNum);
			index++;

			list_add_tail(&pReorderEntry->List,
				      &ieee->RxReorder_Unused_List);
		} else {
			bPktInBuf = true;
			break;
		}
	}

	/* Handling pending timer. Set this timer to prevent from long time
	 * Rx buffering.*/
	if (index > 0) {
		if (timer_pending(&pTS->RxPktPendingTimer))
			del_timer_sync(&pTS->RxPktPendingTimer);
		pTS->RxTimeoutIndicateSeq = 0xffff;

		if (index > REORDER_WIN_SIZE) {
			RTLLIB_DEBUG(RTLLIB_DL_ERR, "RxReorderIndicatePacket():"
				     " Rx Reorder struct buffer full!!\n");
			spin_unlock_irqrestore(&(ieee->reorder_spinlock),
					       flags);
			return;
		}
		rtllib_indicate_packets(ieee, ieee->prxbIndicateArray, index);
		bPktInBuf = false;
	}

	if (bPktInBuf && pTS->RxTimeoutIndicateSeq == 0xffff) {
		RTLLIB_DEBUG(RTLLIB_DL_REORDER, "%s(): SET rx timeout timer\n",
			     __func__);
		pTS->RxTimeoutIndicateSeq = pTS->RxIndicateSeq;
		mod_timer(&pTS->RxPktPendingTimer, jiffies +
			  MSECS(pHTInfo->RxReorderPendingTime));
	}
	spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
}

static u8 parse_subframe(struct rtllib_device *ieee, struct sk_buff *skb,
			 struct rtllib_rx_stats *rx_stats,
			 struct rtllib_rxb *rxb, u8 *src, u8 *dst)
{
	struct rtllib_hdr_3addr  *hdr = (struct rtllib_hdr_3addr *)skb->data;
	u16		fc = le16_to_cpu(hdr->frame_ctl);

	u16		LLCOffset = sizeof(struct rtllib_hdr_3addr);
	u16		ChkLength;
	bool		bIsAggregateFrame = false;
	u16		nSubframe_Length;
	u8		nPadding_Length = 0;
	u16		SeqNum = 0;
	struct sk_buff *sub_skb;
	u8	     *data_ptr;
	/* just for debug purpose */
	SeqNum = WLAN_GET_SEQ_SEQ(le16_to_cpu(hdr->seq_ctl));
	if ((RTLLIB_QOS_HAS_SEQ(fc)) &&
	   (((union frameqos *)(skb->data + RTLLIB_3ADDR_LEN))->field.reserved))
		bIsAggregateFrame = true;

	if (RTLLIB_QOS_HAS_SEQ(fc))
		LLCOffset += 2;
	if (rx_stats->bContainHTC)
		LLCOffset += sHTCLng;

	ChkLength = LLCOffset;

	if (skb->len <= ChkLength)
		return 0;

	skb_pull(skb, LLCOffset);
	ieee->bIsAggregateFrame = bIsAggregateFrame;
	if (!bIsAggregateFrame) {
		rxb->nr_subframes = 1;

		/* altered by clark 3/30/2010
		 * The struct buffer size of the skb indicated to upper layer
		 * must be less than 5000, or the defraged IP datagram
		 * in the IP layer will exceed "ipfrag_high_tresh" and be
		 * discarded. so there must not use the function
		 * "skb_copy" and "skb_clone" for "skb".
		 */

		/* Allocate new skb for releasing to upper layer */
		sub_skb = dev_alloc_skb(RTLLIB_SKBBUFFER_SIZE);
		if (!sub_skb)
			return 0;
		skb_reserve(sub_skb, 12);
		data_ptr = (u8 *)skb_put(sub_skb, skb->len);
		memcpy(data_ptr, skb->data, skb->len);
		sub_skb->dev = ieee->dev;

		rxb->subframes[0] = sub_skb;

		memcpy(rxb->src, src, ETH_ALEN);
		memcpy(rxb->dst, dst, ETH_ALEN);
		rxb->subframes[0]->dev = ieee->dev;
		return 1;
	} else {
		rxb->nr_subframes = 0;
		memcpy(rxb->src, src, ETH_ALEN);
		memcpy(rxb->dst, dst, ETH_ALEN);
		while (skb->len > ETHERNET_HEADER_SIZE) {
			/* Offset 12 denote 2 mac address */
			nSubframe_Length = *((u16 *)(skb->data + 12));
			nSubframe_Length = (nSubframe_Length >> 8) +
					   (nSubframe_Length << 8);

			if (skb->len < (ETHERNET_HEADER_SIZE + nSubframe_Length)) {
				printk(KERN_INFO "%s: A-MSDU parse error!! "
				       "pRfd->nTotalSubframe : %d\n",\
				       __func__, rxb->nr_subframes);
				printk(KERN_INFO "%s: A-MSDU parse error!! "
				       "Subframe Length: %d\n", __func__,
				       nSubframe_Length);
				printk(KERN_INFO "nRemain_Length is %d and "
				       "nSubframe_Length is : %d\n", skb->len,
				       nSubframe_Length);
				printk(KERN_INFO "The Packet SeqNum is %d\n", SeqNum);
				return 0;
			}

			/* move the data point to data content */
			skb_pull(skb, ETHERNET_HEADER_SIZE);

			/* altered by clark 3/30/2010
			 * The struct buffer size of the skb indicated to upper layer
			 * must be less than 5000, or the defraged IP datagram
			 * in the IP layer will exceed "ipfrag_high_tresh" and be
			 * discarded. so there must not use the function
			 * "skb_copy" and "skb_clone" for "skb".
			 */

			/* Allocate new skb for releasing to upper layer */
			sub_skb = dev_alloc_skb(nSubframe_Length + 12);
			if (!sub_skb)
				return 0;
			skb_reserve(sub_skb, 12);
			data_ptr = (u8 *)skb_put(sub_skb, nSubframe_Length);
			memcpy(data_ptr, skb->data, nSubframe_Length);

			sub_skb->dev = ieee->dev;
			rxb->subframes[rxb->nr_subframes++] = sub_skb;
			if (rxb->nr_subframes >= MAX_SUBFRAME_COUNT) {
				RTLLIB_DEBUG_RX("ParseSubframe(): Too many "
						"Subframes! Packets dropped!\n");
				break;
			}
			skb_pull(skb, nSubframe_Length);

			if (skb->len != 0) {
				nPadding_Length = 4 - ((nSubframe_Length +
						  ETHERNET_HEADER_SIZE) % 4);
				if (nPadding_Length == 4)
					nPadding_Length = 0;

				if (skb->len < nPadding_Length)
					return 0;

				skb_pull(skb, nPadding_Length);
			}
		}

		return rxb->nr_subframes;
	}
}


static size_t rtllib_rx_get_hdrlen(struct rtllib_device *ieee,
				   struct sk_buff *skb,
				   struct rtllib_rx_stats *rx_stats)
{
	struct rtllib_hdr_4addr *hdr = (struct rtllib_hdr_4addr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	size_t hdrlen = 0;

	hdrlen = rtllib_get_hdrlen(fc);
	if (HTCCheck(ieee, skb->data)) {
		if (net_ratelimit())
			printk(KERN_INFO "%s: find HTCControl!\n", __func__);
		hdrlen += 4;
		rx_stats->bContainHTC = true;
	}

	 if (RTLLIB_QOS_HAS_SEQ(fc))
		rx_stats->bIsQosData = true;

	return hdrlen;
}

static int rtllib_rx_check_duplicate(struct rtllib_device *ieee,
				     struct sk_buff *skb, u8 multicast)
{
	struct rtllib_hdr_4addr *hdr = (struct rtllib_hdr_4addr *)skb->data;
	u16 fc, sc;
	u8 frag, type, stype;

	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);

	if ((ieee->pHTInfo->bCurRxReorderEnable == false) ||
		!ieee->current_network.qos_data.active ||
		!IsDataFrame(skb->data) ||
		IsLegacyDataFrame(skb->data)) {
		if (!((type == RTLLIB_FTYPE_MGMT) && (stype == RTLLIB_STYPE_BEACON))) {
			if (is_duplicate_packet(ieee, hdr))
				return -1;
		}
	} else {
		struct rx_ts_record *pRxTS = NULL;
		if (GetTs(ieee, (struct ts_common_info **) &pRxTS, hdr->addr2,
			(u8)Frame_QoSTID((u8 *)(skb->data)), RX_DIR, true)) {
			if ((fc & (1<<11)) && (frag == pRxTS->RxLastFragNum) &&
			    (WLAN_GET_SEQ_SEQ(sc) == pRxTS->RxLastSeqNum)) {
				return -1;
			} else {
				pRxTS->RxLastFragNum = frag;
				pRxTS->RxLastSeqNum = WLAN_GET_SEQ_SEQ(sc);
			}
		} else {
			RTLLIB_DEBUG(RTLLIB_DL_ERR, "ERR!!%s(): No TS!! Skip"
				     " the check!!\n", __func__);
			return -1;
		}
	}

	return 0;
}

static void rtllib_rx_extract_addr(struct rtllib_device *ieee,
				   struct rtllib_hdr_4addr *hdr, u8 *dst,
				   u8 *src, u8 *bssid)
{
	u16 fc = le16_to_cpu(hdr->frame_ctl);

	switch (fc & (RTLLIB_FCTL_FROMDS | RTLLIB_FCTL_TODS)) {
	case RTLLIB_FCTL_FROMDS:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);
		memcpy(bssid, hdr->addr2, ETH_ALEN);
		break;
	case RTLLIB_FCTL_TODS:
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		memcpy(bssid, hdr->addr1, ETH_ALEN);
		break;
	case RTLLIB_FCTL_FROMDS | RTLLIB_FCTL_TODS:
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);
		memcpy(bssid, ieee->current_network.bssid, ETH_ALEN);
		break;
	case 0:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		memcpy(bssid, hdr->addr3, ETH_ALEN);
		break;
	}
}

static int rtllib_rx_data_filter(struct rtllib_device *ieee, u16 fc,
				 u8 *dst, u8 *src, u8 *bssid, u8 *addr2)
{
	u8 type, stype;

	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);

	/* Filter frames from different BSS */
	if (((fc & RTLLIB_FCTL_DSTODS) != RTLLIB_FCTL_DSTODS) &&
	    !ether_addr_equal(ieee->current_network.bssid, bssid) &&
	    !is_zero_ether_addr(ieee->current_network.bssid)) {
		return -1;
	}

	/* Filter packets sent by an STA that will be forwarded by AP */
	if (ieee->IntelPromiscuousModeInfo.bPromiscuousOn  &&
		ieee->IntelPromiscuousModeInfo.bFilterSourceStationFrame) {
		if ((fc & RTLLIB_FCTL_TODS) && !(fc & RTLLIB_FCTL_FROMDS) &&
		    !ether_addr_equal(dst, ieee->current_network.bssid) &&
		    ether_addr_equal(bssid, ieee->current_network.bssid)) {
			return -1;
		}
	}

	/* Nullfunc frames may have PS-bit set, so they must be passed to
	 * hostap_handle_sta_rx() before being dropped here. */
	if (!ieee->IntelPromiscuousModeInfo.bPromiscuousOn) {
		if (stype != RTLLIB_STYPE_DATA &&
		    stype != RTLLIB_STYPE_DATA_CFACK &&
		    stype != RTLLIB_STYPE_DATA_CFPOLL &&
		    stype != RTLLIB_STYPE_DATA_CFACKPOLL &&
		    stype != RTLLIB_STYPE_QOS_DATA) {
			if (stype != RTLLIB_STYPE_NULLFUNC)
				RTLLIB_DEBUG_DROP(
					"RX: dropped data frame "
					"with no data (type=0x%02x, "
					"subtype=0x%02x)\n",
					type, stype);
			return -1;
		}
	}

	if (ieee->iw_mode != IW_MODE_MESH) {
		/* packets from our adapter are dropped (echo) */
		if (!memcmp(src, ieee->dev->dev_addr, ETH_ALEN))
			return -1;

		/* {broad,multi}cast packets to our BSS go through */
		if (is_multicast_ether_addr(dst)) {
			if (memcmp(bssid, ieee->current_network.bssid, ETH_ALEN))
				return -1;
		}
	}
	return 0;
}

static int rtllib_rx_get_crypt(struct rtllib_device *ieee, struct sk_buff *skb,
			struct lib80211_crypt_data **crypt, size_t hdrlen)
{
	struct rtllib_hdr_4addr *hdr = (struct rtllib_hdr_4addr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	int idx = 0;

	if (ieee->host_decrypt) {
		if (skb->len >= hdrlen + 3)
			idx = skb->data[hdrlen + 3] >> 6;

		*crypt = ieee->crypt_info.crypt[idx];
		/* allow NULL decrypt to indicate an station specific override
		 * for default encryption */
		if (*crypt && ((*crypt)->ops == NULL ||
			      (*crypt)->ops->decrypt_mpdu == NULL))
			*crypt = NULL;

		if (!*crypt && (fc & RTLLIB_FCTL_WEP)) {
			/* This seems to be triggered by some (multicast?)
			 * frames from other than current BSS, so just drop the
			 * frames silently instead of filling system log with
			 * these reports. */
			RTLLIB_DEBUG_DROP("Decryption failed (not set)"
					     " (SA= %pM)\n",
					     hdr->addr2);
			ieee->ieee_stats.rx_discards_undecryptable++;
			return -1;
		}
	}

	return 0;
}

static int rtllib_rx_decrypt(struct rtllib_device *ieee, struct sk_buff *skb,
		      struct rtllib_rx_stats *rx_stats,
		      struct lib80211_crypt_data *crypt, size_t hdrlen)
{
	struct rtllib_hdr_4addr *hdr;
	int keyidx = 0;
	u16 fc, sc;
	u8 frag;

	hdr = (struct rtllib_hdr_4addr *)skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);

	if ((!rx_stats->Decrypted))
		ieee->need_sw_enc = 1;
	else
		ieee->need_sw_enc = 0;

	keyidx = rtllib_rx_frame_decrypt(ieee, skb, crypt);
	if (ieee->host_decrypt && (fc & RTLLIB_FCTL_WEP) && (keyidx < 0)) {
		printk(KERN_INFO "%s: decrypt frame error\n", __func__);
		return -1;
	}

	hdr = (struct rtllib_hdr_4addr *) skb->data;
	if ((frag != 0 || (fc & RTLLIB_FCTL_MOREFRAGS))) {
		int flen;
		struct sk_buff *frag_skb = rtllib_frag_cache_get(ieee, hdr);
		RTLLIB_DEBUG_FRAG("Rx Fragment received (%u)\n", frag);

		if (!frag_skb) {
			RTLLIB_DEBUG(RTLLIB_DL_RX | RTLLIB_DL_FRAG,
					"Rx cannot get skb from fragment "
					"cache (morefrag=%d seq=%u frag=%u)\n",
					(fc & RTLLIB_FCTL_MOREFRAGS) != 0,
					WLAN_GET_SEQ_SEQ(sc), frag);
			return -1;
		}
		flen = skb->len;
		if (frag != 0)
			flen -= hdrlen;

		if (frag_skb->tail + flen > frag_skb->end) {
			printk(KERN_WARNING "%s: host decrypted and "
			       "reassembled frame did not fit skb\n",
			       __func__);
			rtllib_frag_cache_invalidate(ieee, hdr);
			return -1;
		}

		if (frag == 0) {
			/* copy first fragment (including full headers) into
			 * beginning of the fragment cache skb */
			memcpy(skb_put(frag_skb, flen), skb->data, flen);
		} else {
			/* append frame payload to the end of the fragment
			 * cache skb */
			memcpy(skb_put(frag_skb, flen), skb->data + hdrlen,
			       flen);
		}
		dev_kfree_skb_any(skb);
		skb = NULL;

		if (fc & RTLLIB_FCTL_MOREFRAGS) {
			/* more fragments expected - leave the skb in fragment
			 * cache for now; it will be delivered to upper layers
			 * after all fragments have been received */
			return -2;
		}

		/* this was the last fragment and the frame will be
		 * delivered, so remove skb from fragment cache */
		skb = frag_skb;
		hdr = (struct rtllib_hdr_4addr *) skb->data;
		rtllib_frag_cache_invalidate(ieee, hdr);
	}

	/* skb: hdr + (possible reassembled) full MSDU payload; possibly still
	 * encrypted/authenticated */
	if (ieee->host_decrypt && (fc & RTLLIB_FCTL_WEP) &&
		rtllib_rx_frame_decrypt_msdu(ieee, skb, keyidx, crypt)) {
		printk(KERN_INFO "%s: ==>decrypt msdu error\n", __func__);
		return -1;
	}

	hdr = (struct rtllib_hdr_4addr *) skb->data;
	if (crypt && !(fc & RTLLIB_FCTL_WEP) && !ieee->open_wep) {
		if (/*ieee->ieee802_1x &&*/
		    rtllib_is_eapol_frame(ieee, skb, hdrlen)) {

			/* pass unencrypted EAPOL frames even if encryption is
			 * configured */
			struct eapol *eap = (struct eapol *)(skb->data +
				24);
			RTLLIB_DEBUG_EAP("RX: IEEE 802.1X EAPOL frame: %s\n",
						eap_get_type(eap->type));
		} else {
			RTLLIB_DEBUG_DROP(
				"encryption configured, but RX "
				"frame not encrypted (SA= %pM)\n",
				hdr->addr2);
			return -1;
		}
	}

	if (crypt && !(fc & RTLLIB_FCTL_WEP) &&
	    rtllib_is_eapol_frame(ieee, skb, hdrlen)) {
			struct eapol *eap = (struct eapol *)(skb->data +
				24);
			RTLLIB_DEBUG_EAP("RX: IEEE 802.1X EAPOL frame: %s\n",
						eap_get_type(eap->type));
	}

	if (crypt && !(fc & RTLLIB_FCTL_WEP) && !ieee->open_wep &&
	    !rtllib_is_eapol_frame(ieee, skb, hdrlen)) {
		RTLLIB_DEBUG_DROP(
			"dropped unencrypted RX data "
			"frame from %pM"
			" (drop_unencrypted=1)\n",
			hdr->addr2);
		return -1;
	}

	if (rtllib_is_eapol_frame(ieee, skb, hdrlen))
		printk(KERN_WARNING "RX: IEEE802.1X EAPOL frame!\n");

	return 0;
}

static void rtllib_rx_check_leave_lps(struct rtllib_device *ieee, u8 unicast, u8 nr_subframes)
{
	if (unicast) {

		if ((ieee->state == RTLLIB_LINKED)) {
			if (((ieee->LinkDetectInfo.NumRxUnicastOkInPeriod +
			    ieee->LinkDetectInfo.NumTxOkInPeriod) > 8) ||
			    (ieee->LinkDetectInfo.NumRxUnicastOkInPeriod > 2)) {
				if (ieee->LeisurePSLeave)
					ieee->LeisurePSLeave(ieee->dev);
			}
		}
	}
	ieee->last_rx_ps_time = jiffies;
}

static void rtllib_rx_indicate_pkt_legacy(struct rtllib_device *ieee,
		struct rtllib_rx_stats *rx_stats,
		struct rtllib_rxb *rxb,
		u8 *dst,
		u8 *src)
{
	struct net_device *dev = ieee->dev;
	u16 ethertype;
	int i = 0;

	if (rxb == NULL) {
		printk(KERN_INFO "%s: rxb is NULL!!\n", __func__);
		return ;
	}

	for (i = 0; i < rxb->nr_subframes; i++) {
		struct sk_buff *sub_skb = rxb->subframes[i];

		if (sub_skb) {
			/* convert hdr + possible LLC headers into Ethernet header */
			ethertype = (sub_skb->data[6] << 8) | sub_skb->data[7];
			if (sub_skb->len >= 8 &&
				((memcmp(sub_skb->data, rfc1042_header, SNAP_SIZE) == 0 &&
				ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
				memcmp(sub_skb->data, bridge_tunnel_header, SNAP_SIZE) == 0)) {
				/* remove RFC1042 or Bridge-Tunnel encapsulation and
				 * replace EtherType */
				skb_pull(sub_skb, SNAP_SIZE);
				memcpy(skb_push(sub_skb, ETH_ALEN), src, ETH_ALEN);
				memcpy(skb_push(sub_skb, ETH_ALEN), dst, ETH_ALEN);
			} else {
				u16 len;
				/* Leave Ethernet header part of hdr and full payload */
				len = sub_skb->len;
				memcpy(skb_push(sub_skb, 2), &len, 2);
				memcpy(skb_push(sub_skb, ETH_ALEN), src, ETH_ALEN);
				memcpy(skb_push(sub_skb, ETH_ALEN), dst, ETH_ALEN);
			}

			ieee->stats.rx_packets++;
			ieee->stats.rx_bytes += sub_skb->len;

			if (is_multicast_ether_addr(dst))
				ieee->stats.multicast++;

			/* Indicate the packets to upper layer */
			memset(sub_skb->cb, 0, sizeof(sub_skb->cb));
			sub_skb->protocol = eth_type_trans(sub_skb, dev);
			sub_skb->dev = dev;
			sub_skb->dev->stats.rx_packets++;
			sub_skb->dev->stats.rx_bytes += sub_skb->len;
			sub_skb->ip_summed = CHECKSUM_NONE; /* 802.11 crc not sufficient */
			netif_rx(sub_skb);
		}
	}
	kfree(rxb);
	rxb = NULL;
}

static int rtllib_rx_InfraAdhoc(struct rtllib_device *ieee, struct sk_buff *skb,
		 struct rtllib_rx_stats *rx_stats)
{
	struct net_device *dev = ieee->dev;
	struct rtllib_hdr_4addr *hdr = (struct rtllib_hdr_4addr *)skb->data;
	struct lib80211_crypt_data *crypt = NULL;
	struct rtllib_rxb *rxb = NULL;
	struct rx_ts_record *pTS = NULL;
	u16 fc, sc, SeqNum = 0;
	u8 type, stype, multicast = 0, unicast = 0, nr_subframes = 0, TID = 0;
	u8 dst[ETH_ALEN], src[ETH_ALEN], bssid[ETH_ALEN] = {0}, *payload;
	size_t hdrlen = 0;
	bool bToOtherSTA = false;
	int ret = 0, i = 0;

	hdr = (struct rtllib_hdr_4addr *)skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);

	/*Filter pkt not to me*/
	multicast = is_multicast_ether_addr(hdr->addr1);
	unicast = !multicast;
	if (unicast && !ether_addr_equal(dev->dev_addr, hdr->addr1)) {
		if (ieee->bNetPromiscuousMode)
			bToOtherSTA = true;
		else
			goto rx_dropped;
	}

	/*Filter pkt has too small length */
	hdrlen = rtllib_rx_get_hdrlen(ieee, skb, rx_stats);
	if (skb->len < hdrlen) {
		printk(KERN_INFO "%s():ERR!!! skb->len is smaller than hdrlen\n", __func__);
		goto rx_dropped;
	}

	/* Filter Duplicate pkt */
	ret = rtllib_rx_check_duplicate(ieee, skb, multicast);
	if (ret < 0)
		goto rx_dropped;

	/* Filter CTRL Frame */
	if (type == RTLLIB_FTYPE_CTL)
		goto rx_dropped;

	/* Filter MGNT Frame */
	if (type == RTLLIB_FTYPE_MGMT) {
		if (bToOtherSTA)
			goto rx_dropped;
		if (rtllib_rx_frame_mgmt(ieee, skb, rx_stats, type, stype))
			goto rx_dropped;
		else
			goto rx_exit;
	}

	/* Filter WAPI DATA Frame */

	/* Update statstics for AP roaming */
	if (!bToOtherSTA) {
		ieee->LinkDetectInfo.NumRecvDataInPeriod++;
		ieee->LinkDetectInfo.NumRxOkInPeriod++;
	}
	dev->last_rx = jiffies;

	/* Data frame - extract src/dst addresses */
	rtllib_rx_extract_addr(ieee, hdr, dst, src, bssid);

	/* Filter Data frames */
	ret = rtllib_rx_data_filter(ieee, fc, dst, src, bssid, hdr->addr2);
	if (ret < 0)
		goto rx_dropped;

	if (skb->len == hdrlen)
		goto rx_dropped;

	/* Send pspoll based on moredata */
	if ((ieee->iw_mode == IW_MODE_INFRA)  && (ieee->sta_sleep == LPS_IS_SLEEP)
		&& (ieee->polling) && (!bToOtherSTA)) {
		if (WLAN_FC_MORE_DATA(fc)) {
			/* more data bit is set, let's request a new frame from the AP */
			rtllib_sta_ps_send_pspoll_frame(ieee);
		} else {
			ieee->polling =  false;
		}
	}

	/* Get crypt if encrypted */
	ret = rtllib_rx_get_crypt(ieee, skb, &crypt, hdrlen);
	if (ret == -1)
		goto rx_dropped;

	/* Decrypt data frame (including reassemble) */
	ret = rtllib_rx_decrypt(ieee, skb, rx_stats, crypt, hdrlen);
	if (ret == -1)
		goto rx_dropped;
	else if (ret == -2)
		goto rx_exit;

	/* Get TS for Rx Reorder  */
	hdr = (struct rtllib_hdr_4addr *) skb->data;
	if (ieee->current_network.qos_data.active && IsQoSDataFrame(skb->data)
		&& !is_multicast_ether_addr(hdr->addr1)
		&& (!bToOtherSTA)) {
		TID = Frame_QoSTID(skb->data);
		SeqNum = WLAN_GET_SEQ_SEQ(sc);
		GetTs(ieee, (struct ts_common_info **) &pTS, hdr->addr2, TID, RX_DIR, true);
		if (TID != 0 && TID != 3)
			ieee->bis_any_nonbepkts = true;
	}

	/* Parse rx data frame (For AMSDU) */
	/* skb: hdr + (possible reassembled) full plaintext payload */
	payload = skb->data + hdrlen;
	rxb = kmalloc(sizeof(struct rtllib_rxb), GFP_ATOMIC);
	if (rxb == NULL) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "%s(): kmalloc rxb error\n", __func__);
		goto rx_dropped;
	}
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

	/* Update WAPI PN */

	/* Check if leave LPS */
	if (!bToOtherSTA) {
		if (ieee->bIsAggregateFrame)
			nr_subframes = rxb->nr_subframes;
		else
			nr_subframes = 1;
		if (unicast)
			ieee->LinkDetectInfo.NumRxUnicastOkInPeriod += nr_subframes;
		rtllib_rx_check_leave_lps(ieee, unicast, nr_subframes);
	}

	/* Indicate packets to upper layer or Rx Reorder */
	if (ieee->pHTInfo->bCurRxReorderEnable == false || pTS == NULL || bToOtherSTA)
		rtllib_rx_indicate_pkt_legacy(ieee, rx_stats, rxb, dst, src);
	else
		RxReorderIndicatePacket(ieee, rxb, pTS, SeqNum);

	dev_kfree_skb(skb);

 rx_exit:
	return 1;

 rx_dropped:
	if (rxb != NULL) {
		kfree(rxb);
		rxb = NULL;
	}
	ieee->stats.rx_dropped++;

	/* Returning 0 indicates to caller that we have not handled the SKB--
	 * so it is still allocated and can be used again by underlying
	 * hardware as a DMA target */
	return 0;
}

static int rtllib_rx_Master(struct rtllib_device *ieee, struct sk_buff *skb,
		 struct rtllib_rx_stats *rx_stats)
{
	return 0;
}

static int rtllib_rx_Monitor(struct rtllib_device *ieee, struct sk_buff *skb,
		 struct rtllib_rx_stats *rx_stats)
{
	struct rtllib_hdr_4addr *hdr = (struct rtllib_hdr_4addr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	size_t hdrlen = rtllib_get_hdrlen(fc);

	if (skb->len < hdrlen) {
		printk(KERN_INFO "%s():ERR!!! skb->len is smaller than hdrlen\n", __func__);
		return 0;
	}

	if (HTCCheck(ieee, skb->data)) {
		if (net_ratelimit())
			printk(KERN_INFO "%s: Find HTCControl!\n", __func__);
		hdrlen += 4;
	}

	rtllib_monitor_rx(ieee, skb, rx_stats, hdrlen);
	ieee->stats.rx_packets++;
	ieee->stats.rx_bytes += skb->len;

	return 1;
}

static int rtllib_rx_Mesh(struct rtllib_device *ieee, struct sk_buff *skb,
		 struct rtllib_rx_stats *rx_stats)
{
	return 0;
}

/* All received frames are sent to this function. @skb contains the frame in
 * IEEE 802.11 format, i.e., in the format it was sent over air.
 * This function is called only as a tasklet (software IRQ). */
int rtllib_rx(struct rtllib_device *ieee, struct sk_buff *skb,
		 struct rtllib_rx_stats *rx_stats)
{
	int ret = 0;

	if ((NULL == ieee) || (NULL == skb) || (NULL == rx_stats)) {
		printk(KERN_INFO "%s: Input parameters NULL!\n", __func__);
		goto rx_dropped;
	}
	if (skb->len < 10) {
		printk(KERN_INFO "%s: SKB length < 10\n", __func__);
		goto rx_dropped;
	}

	switch (ieee->iw_mode) {
	case IW_MODE_ADHOC:
	case IW_MODE_INFRA:
		ret = rtllib_rx_InfraAdhoc(ieee, skb, rx_stats);
		break;
	case IW_MODE_MASTER:
	case IW_MODE_REPEAT:
		ret = rtllib_rx_Master(ieee, skb, rx_stats);
		break;
	case IW_MODE_MONITOR:
		ret = rtllib_rx_Monitor(ieee, skb, rx_stats);
		break;
	case IW_MODE_MESH:
		ret = rtllib_rx_Mesh(ieee, skb, rx_stats);
		break;
	default:
		printk(KERN_INFO"%s: ERR iw mode!!!\n", __func__);
		break;
	}

	return ret;

 rx_dropped:
	if (ieee)
		ieee->stats.rx_dropped++;
	return 0;
}
EXPORT_SYMBOL(rtllib_rx);

static u8 qos_oui[QOS_OUI_LEN] = { 0x00, 0x50, 0xF2 };

/*
* Make ther structure we read from the beacon packet has
* the right values
*/
static int rtllib_verify_qos_info(struct rtllib_qos_information_element
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
static int rtllib_read_qos_param_element(struct rtllib_qos_parameter_info
					    *element_param, struct rtllib_info_element
					    *info_element)
{
	int ret = 0;
	u16 size = sizeof(struct rtllib_qos_parameter_info) - 2;

	if ((info_element == NULL) || (element_param == NULL))
		return -1;

	if (info_element->id == QOS_ELEMENT_ID && info_element->len == size) {
		memcpy(element_param->info_element.qui, info_element->data,
		       info_element->len);
		element_param->info_element.elementID = info_element->id;
		element_param->info_element.length = info_element->len;
	} else
		ret = -1;
	if (ret == 0)
		ret = rtllib_verify_qos_info(&element_param->info_element,
						QOS_OUI_PARAM_SUB_TYPE);
	return ret;
}

/*
 * Parse a QoS information element
 */
static int rtllib_read_qos_info_element(struct
					   rtllib_qos_information_element
					   *element_info, struct rtllib_info_element
					   *info_element)
{
	int ret = 0;
	u16 size = sizeof(struct rtllib_qos_information_element) - 2;

	if (element_info == NULL)
		return -1;
	if (info_element == NULL)
		return -1;

	if ((info_element->id == QOS_ELEMENT_ID) && (info_element->len == size)) {
		memcpy(element_info->qui, info_element->data,
		       info_element->len);
		element_info->elementID = info_element->id;
		element_info->length = info_element->len;
	} else
		ret = -1;

	if (ret == 0)
		ret = rtllib_verify_qos_info(element_info,
						QOS_OUI_INFO_SUB_TYPE);
	return ret;
}


/*
 * Write QoS parameters from the ac parameters.
 */
static int rtllib_qos_convert_ac_to_parameters(struct rtllib_qos_parameter_info *param_elm,
		struct rtllib_qos_data *qos_data)
{
	struct rtllib_qos_ac_parameter *ac_params;
	struct rtllib_qos_parameters *qos_param = &(qos_data->parameters);
	int i;
	u8 aci;
	u8 acm;

	qos_data->wmm_acm = 0;
	for (i = 0; i < QOS_QUEUE_NUM; i++) {
		ac_params = &(param_elm->ac_params_record[i]);

		aci = (ac_params->aci_aifsn & 0x60) >> 5;
		acm = (ac_params->aci_aifsn & 0x10) >> 4;

		if (aci >= QOS_QUEUE_NUM)
			continue;
		switch (aci) {
		case 1:
			/* BIT(0) | BIT(3) */
			if (acm)
				qos_data->wmm_acm |= (0x01<<0)|(0x01<<3);
			break;
		case 2:
			/* BIT(4) | BIT(5) */
			if (acm)
				qos_data->wmm_acm |= (0x01<<4)|(0x01<<5);
			break;
		case 3:
			/* BIT(6) | BIT(7) */
			if (acm)
				qos_data->wmm_acm |= (0x01<<6)|(0x01<<7);
			break;
		case 0:
		default:
			/* BIT(1) | BIT(2) */
			if (acm)
				qos_data->wmm_acm |= (0x01<<1)|(0x01<<2);
			break;
		}

		qos_param->aifs[aci] = (ac_params->aci_aifsn) & 0x0f;

		/* WMM spec P.11: The minimum value for AIFSN shall be 2 */
		qos_param->aifs[aci] = (qos_param->aifs[aci] < 2) ? 2 : qos_param->aifs[aci];

		qos_param->cw_min[aci] = cpu_to_le16(ac_params->ecw_min_max & 0x0F);

		qos_param->cw_max[aci] = cpu_to_le16((ac_params->ecw_min_max & 0xF0) >> 4);

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
static int rtllib_parse_qos_info_param_IE(struct rtllib_info_element
					     *info_element,
					     struct rtllib_network *network)
{
	int rc = 0;
	struct rtllib_qos_information_element qos_info_element;

	rc = rtllib_read_qos_info_element(&qos_info_element, info_element);

	if (rc == 0) {
		network->qos_data.param_count = qos_info_element.ac_info & 0x0F;
		network->flags |= NETWORK_HAS_QOS_INFORMATION;
	} else {
		struct rtllib_qos_parameter_info param_element;

		rc = rtllib_read_qos_param_element(&param_element,
						      info_element);
		if (rc == 0) {
			rtllib_qos_convert_ac_to_parameters(&param_element,
							       &(network->qos_data));
			network->flags |= NETWORK_HAS_QOS_PARAMETERS;
			network->qos_data.param_count =
			    param_element.info_element.ac_info & 0x0F;
		}
	}

	if (rc == 0) {
		RTLLIB_DEBUG_QOS("QoS is supported\n");
		network->qos_data.supported = 1;
	}
	return rc;
}

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
	MFIE_STRING(RSN);
	MFIE_STRING(RATES_EX);
	MFIE_STRING(GENERIC);
	MFIE_STRING(QOS_PARAMETER);
	default:
		return "UNKNOWN";
	}
}

static inline void rtllib_extract_country_ie(
	struct rtllib_device *ieee,
	struct rtllib_info_element *info_element,
	struct rtllib_network *network,
	u8 *addr2)
{
	if (IS_DOT11D_ENABLE(ieee)) {
		if (info_element->len != 0) {
			memcpy(network->CountryIeBuf, info_element->data, info_element->len);
			network->CountryIeLen = info_element->len;

			if (!IS_COUNTRY_IE_VALID(ieee)) {
				if (rtllib_act_scanning(ieee, false) && ieee->FirstIe_InScan)
					printk(KERN_INFO "Received beacon ContryIE, SSID: <%s>\n", network->ssid);
				Dot11d_UpdateCountryIe(ieee, addr2, info_element->len, info_element->data);
			}
		}

		if (IS_EQUAL_CIE_SRC(ieee, addr2))
			UPDATE_CIE_WATCHDOG(ieee);
	}

}

int rtllib_parse_info_param(struct rtllib_device *ieee,
		struct rtllib_info_element *info_element,
		u16 length,
		struct rtllib_network *network,
		struct rtllib_rx_stats *stats)
{
	u8 i;
	short offset;
	u16	tmp_htcap_len = 0;
	u16	tmp_htinfo_len = 0;
	u16 ht_realtek_agg_len = 0;
	u8  ht_realtek_agg_buf[MAX_IE_LEN];
	char rates_str[64];
	char *p;

	while (length >= sizeof(*info_element)) {
		if (sizeof(*info_element) + info_element->len > length) {
			RTLLIB_DEBUG_MGMT("Info elem: parse failed: "
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
			if (rtllib_is_empty_essid(info_element->data,
						     info_element->len)) {
				network->flags |= NETWORK_EMPTY_ESSID;
				break;
			}

			network->ssid_len = min(info_element->len,
						(u8) IW_ESSID_MAX_SIZE);
			memcpy(network->ssid, info_element->data, network->ssid_len);
			if (network->ssid_len < IW_ESSID_MAX_SIZE)
				memset(network->ssid + network->ssid_len, 0,
				       IW_ESSID_MAX_SIZE - network->ssid_len);

			RTLLIB_DEBUG_MGMT("MFIE_TYPE_SSID: '%s' len=%d.\n",
					     network->ssid, network->ssid_len);
			break;

		case MFIE_TYPE_RATES:
			p = rates_str;
			network->rates_len = min(info_element->len,
						 MAX_RATES_LENGTH);
			for (i = 0; i < network->rates_len; i++) {
				network->rates[i] = info_element->data[i];
				p += snprintf(p, sizeof(rates_str) -
					      (p - rates_str), "%02X ",
					      network->rates[i]);
				if (rtllib_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    RTLLIB_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}

				if (rtllib_is_cck_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_CCK;
				}
			}

			RTLLIB_DEBUG_MGMT("MFIE_TYPE_RATES: '%s' (%d)\n",
					     rates_str, network->rates_len);
			break;

		case MFIE_TYPE_RATES_EX:
			p = rates_str;
			network->rates_ex_len = min(info_element->len,
						    MAX_RATES_EX_LENGTH);
			for (i = 0; i < network->rates_ex_len; i++) {
				network->rates_ex[i] = info_element->data[i];
				p += snprintf(p, sizeof(rates_str) -
					      (p - rates_str), "%02X ",
					      network->rates_ex[i]);
				if (rtllib_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    RTLLIB_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			RTLLIB_DEBUG_MGMT("MFIE_TYPE_RATES_EX: '%s' (%d)\n",
					     rates_str, network->rates_ex_len);
			break;

		case MFIE_TYPE_DS_SET:
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_DS_SET: %d\n",
					     info_element->data[0]);
			network->channel = info_element->data[0];
			break;

		case MFIE_TYPE_FH_SET:
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_FH_SET: ignored\n");
			break;

		case MFIE_TYPE_CF_SET:
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_CF_SET: ignored\n");
			break;

		case MFIE_TYPE_TIM:
			if (info_element->len < 4)
				break;

			network->tim.tim_count = info_element->data[0];
			network->tim.tim_period = info_element->data[1];

			network->dtim_period = info_element->data[1];
			if (ieee->state != RTLLIB_LINKED)
				break;
			network->last_dtim_sta_time = jiffies;

			network->dtim_data = RTLLIB_DTIM_VALID;


			if (info_element->data[2] & 1)
				network->dtim_data |= RTLLIB_DTIM_MBCAST;

			offset = (info_element->data[2] >> 1)*2;


			if (ieee->assoc_id < 8*offset ||
			    ieee->assoc_id > 8*(offset + info_element->len - 3))
				break;

			offset = (ieee->assoc_id / 8) - offset;
			if (info_element->data[3 + offset] &
			   (1 << (ieee->assoc_id % 8)))
				network->dtim_data |= RTLLIB_DTIM_UCAST;

			network->listen_interval = network->dtim_period;
			break;

		case MFIE_TYPE_ERP:
			network->erp_value = info_element->data[0];
			network->flags |= NETWORK_HAS_ERP_VALUE;
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_ERP_SET: %d\n",
					     network->erp_value);
			break;
		case MFIE_TYPE_IBSS_SET:
			network->atim_window = info_element->data[0];
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_IBSS_SET: %d\n",
					     network->atim_window);
			break;

		case MFIE_TYPE_CHALLENGE:
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_CHALLENGE: ignored\n");
			break;

		case MFIE_TYPE_GENERIC:
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_GENERIC: %d bytes\n",
					     info_element->len);
			if (!rtllib_parse_qos_info_param_IE(info_element,
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
			if (info_element->len == 7 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0xe0 &&
			    info_element->data[2] == 0x4c &&
			    info_element->data[3] == 0x01 &&
			    info_element->data[4] == 0x02)
				network->Turbo_Enable = 1;

			if (tmp_htcap_len == 0) {
				if (info_element->len >= 4 &&
				   info_element->data[0] == 0x00 &&
				   info_element->data[1] == 0x90 &&
				   info_element->data[2] == 0x4c &&
				   info_element->data[3] == 0x033) {

						tmp_htcap_len = min(info_element->len, (u8)MAX_IE_LEN);
						if (tmp_htcap_len != 0) {
							network->bssht.bdHTSpecVer = HT_SPEC_VER_EWC;
							network->bssht.bdHTCapLen = tmp_htcap_len > sizeof(network->bssht.bdHTCapBuf) ?
								sizeof(network->bssht.bdHTCapBuf) : tmp_htcap_len;
							memcpy(network->bssht.bdHTCapBuf, info_element->data, network->bssht.bdHTCapLen);
						}
				}
				if (tmp_htcap_len != 0) {
					network->bssht.bdSupportHT = true;
					network->bssht.bdHT1R = ((((struct ht_capab_ele *)(network->bssht.bdHTCapBuf))->MCS[1]) == 0);
				} else {
					network->bssht.bdSupportHT = false;
					network->bssht.bdHT1R = false;
				}
			}


			if (tmp_htinfo_len == 0) {
				if (info_element->len >= 4 &&
				    info_element->data[0] == 0x00 &&
				    info_element->data[1] == 0x90 &&
				    info_element->data[2] == 0x4c &&
				    info_element->data[3] == 0x034) {
					tmp_htinfo_len = min(info_element->len, (u8)MAX_IE_LEN);
					if (tmp_htinfo_len != 0) {
						network->bssht.bdHTSpecVer = HT_SPEC_VER_EWC;
						if (tmp_htinfo_len) {
							network->bssht.bdHTInfoLen = tmp_htinfo_len > sizeof(network->bssht.bdHTInfoBuf) ?
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
					    info_element->data[3] == 0x02) {
						ht_realtek_agg_len = min(info_element->len, (u8)MAX_IE_LEN);
						memcpy(ht_realtek_agg_buf, info_element->data, info_element->len);
					}
					if (ht_realtek_agg_len >= 5) {
						network->realtek_cap_exit = true;
						network->bssht.bdRT2RTAggregation = true;

						if ((ht_realtek_agg_buf[4] == 1) && (ht_realtek_agg_buf[5] & 0x02))
							network->bssht.bdRT2RTLongSlotTime = true;

						if ((ht_realtek_agg_buf[4] == 1) && (ht_realtek_agg_buf[5] & RT_HT_CAP_USE_92SE))
							network->bssht.RT2RT_HT_Mode |= RT_HT_CAP_USE_92SE;
					}
				}
				if (ht_realtek_agg_len >= 5) {
					if ((ht_realtek_agg_buf[5] & RT_HT_CAP_USE_SOFTAP))
						network->bssht.RT2RT_HT_Mode |= RT_HT_CAP_USE_SOFTAP;
				}
			}

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
			     info_element->data[2] == 0x18)) {
				network->broadcom_cap_exist = true;
			}
			if (info_element->len >= 3 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x0c &&
			    info_element->data[2] == 0x43)
				network->ralink_cap_exist = true;
			if ((info_element->len >= 3 &&
			     info_element->data[0] == 0x00 &&
			     info_element->data[1] == 0x03 &&
			     info_element->data[2] == 0x7f) ||
			     (info_element->len >= 3 &&
			     info_element->data[0] == 0x00 &&
			     info_element->data[1] == 0x13 &&
			     info_element->data[2] == 0x74))
				network->atheros_cap_exist = true;

			if ((info_element->len >= 3 &&
			     info_element->data[0] == 0x00 &&
			     info_element->data[1] == 0x50 &&
			     info_element->data[2] == 0x43))
				network->marvell_cap_exist = true;
			if (info_element->len >= 3 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x40 &&
			    info_element->data[2] == 0x96)
				network->cisco_cap_exist = true;


			if (info_element->len >= 3 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x0a &&
			    info_element->data[2] == 0xf5)
				network->airgo_cap_exist = true;

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
					network->MBssidMask = network->CcxRmState[1] & 0x07;
					if (network->MBssidMask != 0) {
						network->bMBssidValid = true;
						network->MBssidMask = 0xff << (network->MBssidMask);
						memcpy(network->MBssid, network->bssid, ETH_ALEN);
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
			if (info_element->len > 4  &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x50 &&
			    info_element->data[2] == 0xf2 &&
			    info_element->data[3] == 0x04) {
				RTLLIB_DEBUG_MGMT("MFIE_TYPE_WZC: %d bytes\n",
						     info_element->len);
				network->wzc_ie_len = min(info_element->len+2,
							  MAX_WZC_IE_LEN);
				memcpy(network->wzc_ie, info_element,
						network->wzc_ie_len);
			}
			break;

		case MFIE_TYPE_RSN:
			RTLLIB_DEBUG_MGMT("MFIE_TYPE_RSN: %d bytes\n",
					     info_element->len);
			network->rsn_ie_len = min(info_element->len + 2,
						  MAX_WPA_IE_LEN);
			memcpy(network->rsn_ie, info_element,
			       network->rsn_ie_len);
			break;

		case MFIE_TYPE_HT_CAP:
			RTLLIB_DEBUG_SCAN("MFIE_TYPE_HT_CAP: %d bytes\n",
					     info_element->len);
			tmp_htcap_len = min(info_element->len, (u8)MAX_IE_LEN);
			if (tmp_htcap_len != 0) {
				network->bssht.bdHTSpecVer = HT_SPEC_VER_EWC;
				network->bssht.bdHTCapLen = tmp_htcap_len > sizeof(network->bssht.bdHTCapBuf) ?
					sizeof(network->bssht.bdHTCapBuf) : tmp_htcap_len;
				memcpy(network->bssht.bdHTCapBuf,
				       info_element->data,
				       network->bssht.bdHTCapLen);

				network->bssht.bdSupportHT = true;
				network->bssht.bdHT1R = ((((struct ht_capab_ele *)
							network->bssht.bdHTCapBuf))->MCS[1]) == 0;

				network->bssht.bdBandWidth = (enum ht_channel_width)
							     (((struct ht_capab_ele *)
							     (network->bssht.bdHTCapBuf))->ChlWidth);
			} else {
				network->bssht.bdSupportHT = false;
				network->bssht.bdHT1R = false;
				network->bssht.bdBandWidth = HT_CHANNEL_WIDTH_20;
			}
			break;


		case MFIE_TYPE_HT_INFO:
			RTLLIB_DEBUG_SCAN("MFIE_TYPE_HT_INFO: %d bytes\n",
					     info_element->len);
			tmp_htinfo_len = min(info_element->len, (u8)MAX_IE_LEN);
			if (tmp_htinfo_len) {
				network->bssht.bdHTSpecVer = HT_SPEC_VER_IEEE;
				network->bssht.bdHTInfoLen = tmp_htinfo_len >
					sizeof(network->bssht.bdHTInfoBuf) ?
					sizeof(network->bssht.bdHTInfoBuf) :
					tmp_htinfo_len;
				memcpy(network->bssht.bdHTInfoBuf,
				       info_element->data,
				       network->bssht.bdHTInfoLen);
			}
			break;

		case MFIE_TYPE_AIRONET:
			RTLLIB_DEBUG_SCAN("MFIE_TYPE_AIRONET: %d bytes\n",
					     info_element->len);
			if (info_element->len > IE_CISCO_FLAG_POSITION) {
				network->bWithAironetIE = true;

				if ((info_element->data[IE_CISCO_FLAG_POSITION]
				     & SUPPORT_CKIP_MIC) ||
				     (info_element->data[IE_CISCO_FLAG_POSITION]
				     & SUPPORT_CKIP_PK))
					network->bCkipSupported = true;
				else
					network->bCkipSupported = false;
			} else {
				network->bWithAironetIE = false;
				network->bCkipSupported = false;
			}
			break;
		case MFIE_TYPE_QOS_PARAMETER:
			printk(KERN_ERR
			       "QoS Error need to parse QOS_PARAMETER IE\n");
			break;

		case MFIE_TYPE_COUNTRY:
			RTLLIB_DEBUG_SCAN("MFIE_TYPE_COUNTRY: %d bytes\n",
					     info_element->len);
			rtllib_extract_country_ie(ieee, info_element, network,
						  network->bssid);
			break;
/* TODO */
		default:
			RTLLIB_DEBUG_MGMT
			    ("Unsupported info element: %s (%d)\n",
			     get_info_element_string(info_element->id),
			     info_element->id);
			break;
		}

		length -= sizeof(*info_element) + info_element->len;
		info_element =
		    (struct rtllib_info_element *)&info_element->
		    data[info_element->len];
	}

	if (!network->atheros_cap_exist && !network->broadcom_cap_exist &&
	    !network->cisco_cap_exist && !network->ralink_cap_exist &&
	    !network->bssht.bdRT2RTAggregation)
		network->unknown_cap_exist = true;
	else
		network->unknown_cap_exist = false;
	return 0;
}

static inline u8 rtllib_SignalStrengthTranslate(u8  CurrSS)
{
	u8 RetSS;

	if (CurrSS >= 71 && CurrSS <= 100)
		RetSS = 90 + ((CurrSS - 70) / 3);
	else if (CurrSS >= 41 && CurrSS <= 70)
		RetSS = 78 + ((CurrSS - 40) / 3);
	else if (CurrSS >= 31 && CurrSS <= 40)
		RetSS = 66 + (CurrSS - 30);
	else if (CurrSS >= 21 && CurrSS <= 30)
		RetSS = 54 + (CurrSS - 20);
	else if (CurrSS >= 5 && CurrSS <= 20)
		RetSS = 42 + (((CurrSS - 5) * 2) / 3);
	else if (CurrSS == 4)
		RetSS = 36;
	else if (CurrSS == 3)
		RetSS = 27;
	else if (CurrSS == 2)
		RetSS = 18;
	else if (CurrSS == 1)
		RetSS = 9;
	else
		RetSS = CurrSS;

	return RetSS;
}

static long rtllib_translate_todbm(u8 signal_strength_index)
{
	long	signal_power;

	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;

	return signal_power;
}

static inline int rtllib_network_init(
	struct rtllib_device *ieee,
	struct rtllib_probe_response *beacon,
	struct rtllib_network *network,
	struct rtllib_rx_stats *stats)
{

	/*
	network->qos_data.active = 0;
	network->qos_data.supported = 0;
	network->qos_data.param_count = 0;
	network->qos_data.old_param_count = 0;
	*/
	memset(&network->qos_data, 0, sizeof(struct rtllib_qos_data));

	/* Pull out fixed field data */
	memcpy(network->bssid, beacon->header.addr3, ETH_ALEN);
	network->capability = le16_to_cpu(beacon->capability);
	network->last_scanned = jiffies;
	network->time_stamp[0] = beacon->time_stamp[0];
	network->time_stamp[1] = beacon->time_stamp[1];
	network->beacon_interval = le16_to_cpu(beacon->beacon_interval);
	/* Where to pull this? beacon->listen_interval;*/
	network->listen_interval = 0x0A;
	network->rates_len = network->rates_ex_len = 0;
	network->last_associate = 0;
	network->ssid_len = 0;
	network->hidden_ssid_len = 0;
	memset(network->hidden_ssid, 0, sizeof(network->hidden_ssid));
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
	network->realtek_cap_exit = false;
	network->marvell_cap_exist = false;
	network->airgo_cap_exist = false;
	network->Turbo_Enable = 0;
	network->SignalStrength = stats->SignalStrength;
	network->RSSI = stats->SignalStrength;
	network->CountryIeLen = 0;
	memset(network->CountryIeBuf, 0, MAX_IE_LEN);
	HTInitializeBssDesc(&network->bssht);
	if (stats->freq == RTLLIB_52GHZ_BAND) {
		/* for A band (No DS info) */
		network->channel = stats->received_channel;
	} else
		network->flags |= NETWORK_HAS_CCK;

	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;
	network->wzc_ie_len = 0;

	if (rtllib_parse_info_param(ieee,
			beacon->info_element,
			(stats->len - sizeof(*beacon)),
			network,
			stats))
		return 1;

	network->mode = 0;
	if (stats->freq == RTLLIB_52GHZ_BAND)
		network->mode = IEEE_A;
	else {
		if (network->flags & NETWORK_HAS_OFDM)
			network->mode |= IEEE_G;
		if (network->flags & NETWORK_HAS_CCK)
			network->mode |= IEEE_B;
	}

	if (network->mode == 0) {
		RTLLIB_DEBUG_SCAN("Filtered out '%s (%pM)' "
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
	if (rtllib_is_empty_essid(network->ssid, network->ssid_len))
		network->flags |= NETWORK_EMPTY_ESSID;
	stats->signal = 30 + (stats->SignalStrength * 70) / 100;
	stats->noise = rtllib_translate_todbm((u8)(100-stats->signal)) - 25;

	memcpy(&network->stats, stats, sizeof(network->stats));

	return 0;
}

static inline int is_same_network(struct rtllib_network *src,
				  struct rtllib_network *dst, u8 ssidbroad)
{
	/* A network is only a duplicate if the channel, BSSID, ESSID
	 * and the capability field (in particular IBSS and BSS) all match.
	 * We treat all <hidden> with the same BSSID and channel
	 * as one network */
	return (((src->ssid_len == dst->ssid_len) || (!ssidbroad)) &&
		(src->channel == dst->channel) &&
		!memcmp(src->bssid, dst->bssid, ETH_ALEN) &&
		(!memcmp(src->ssid, dst->ssid, src->ssid_len) ||
		(!ssidbroad)) &&
		((src->capability & WLAN_CAPABILITY_IBSS) ==
		(dst->capability & WLAN_CAPABILITY_IBSS)) &&
		((src->capability & WLAN_CAPABILITY_ESS) ==
		(dst->capability & WLAN_CAPABILITY_ESS)));
}

static inline void update_ibss_network(struct rtllib_network *dst,
				  struct rtllib_network *src)
{
	memcpy(&dst->stats, &src->stats, sizeof(struct rtllib_rx_stats));
	dst->last_scanned = jiffies;
}


static inline void update_network(struct rtllib_network *dst,
				  struct rtllib_network *src)
{
	int qos_active;
	u8 old_param;

	memcpy(&dst->stats, &src->stats, sizeof(struct rtllib_rx_stats));
	dst->capability = src->capability;
	memcpy(dst->rates, src->rates, src->rates_len);
	dst->rates_len = src->rates_len;
	memcpy(dst->rates_ex, src->rates_ex, src->rates_ex_len);
	dst->rates_ex_len = src->rates_ex_len;
	if (src->ssid_len > 0) {
		if (dst->ssid_len == 0) {
			memset(dst->hidden_ssid, 0, sizeof(dst->hidden_ssid));
			dst->hidden_ssid_len = src->ssid_len;
			memcpy(dst->hidden_ssid, src->ssid, src->ssid_len);
		} else {
			memset(dst->ssid, 0, dst->ssid_len);
			dst->ssid_len = src->ssid_len;
			memcpy(dst->ssid, src->ssid, src->ssid_len);
		}
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
	dst->last_dtim_sta_time = src->last_dtim_sta_time;
	memcpy(&dst->tim, &src->tim, sizeof(struct rtllib_tim_parameters));

	dst->bssht.bdSupportHT = src->bssht.bdSupportHT;
	dst->bssht.bdRT2RTAggregation = src->bssht.bdRT2RTAggregation;
	dst->bssht.bdHTCapLen = src->bssht.bdHTCapLen;
	memcpy(dst->bssht.bdHTCapBuf, src->bssht.bdHTCapBuf,
	       src->bssht.bdHTCapLen);
	dst->bssht.bdHTInfoLen = src->bssht.bdHTInfoLen;
	memcpy(dst->bssht.bdHTInfoBuf, src->bssht.bdHTInfoBuf,
	       src->bssht.bdHTInfoLen);
	dst->bssht.bdHTSpecVer = src->bssht.bdHTSpecVer;
	dst->bssht.bdRT2RTLongSlotTime = src->bssht.bdRT2RTLongSlotTime;
	dst->broadcom_cap_exist = src->broadcom_cap_exist;
	dst->ralink_cap_exist = src->ralink_cap_exist;
	dst->atheros_cap_exist = src->atheros_cap_exist;
	dst->realtek_cap_exit = src->realtek_cap_exit;
	dst->marvell_cap_exist = src->marvell_cap_exist;
	dst->cisco_cap_exist = src->cisco_cap_exist;
	dst->airgo_cap_exist = src->airgo_cap_exist;
	dst->unknown_cap_exist = src->unknown_cap_exist;
	memcpy(dst->wpa_ie, src->wpa_ie, src->wpa_ie_len);
	dst->wpa_ie_len = src->wpa_ie_len;
	memcpy(dst->rsn_ie, src->rsn_ie, src->rsn_ie_len);
	dst->rsn_ie_len = src->rsn_ie_len;
	memcpy(dst->wzc_ie, src->wzc_ie, src->wzc_ie_len);
	dst->wzc_ie_len = src->wzc_ie_len;

	dst->last_scanned = jiffies;
	/* qos related parameters */
	qos_active = dst->qos_data.active;
	old_param = dst->qos_data.param_count;
	dst->qos_data.supported = src->qos_data.supported;
	if (dst->flags & NETWORK_HAS_QOS_PARAMETERS)
		memcpy(&dst->qos_data, &src->qos_data,
		       sizeof(struct rtllib_qos_data));
	if (dst->qos_data.supported == 1) {
		if (dst->ssid_len)
			RTLLIB_DEBUG_QOS
				("QoS the network %s is QoS supported\n",
				dst->ssid);
		else
			RTLLIB_DEBUG_QOS
				("QoS the network is QoS supported\n");
	}
	dst->qos_data.active = qos_active;
	dst->qos_data.old_param_count = old_param;

	/* dst->last_associate is not overwritten */
	dst->wmm_info = src->wmm_info;
	if (src->wmm_param[0].ac_aci_acm_aifsn ||
	   src->wmm_param[1].ac_aci_acm_aifsn ||
	   src->wmm_param[2].ac_aci_acm_aifsn ||
	   src->wmm_param[3].ac_aci_acm_aifsn)
		memcpy(dst->wmm_param, src->wmm_param, WME_AC_PRAM_LEN);

	dst->SignalStrength = src->SignalStrength;
	dst->RSSI = src->RSSI;
	dst->Turbo_Enable = src->Turbo_Enable;

	dst->CountryIeLen = src->CountryIeLen;
	memcpy(dst->CountryIeBuf, src->CountryIeBuf, src->CountryIeLen);

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
	return (WLAN_FC_GET_STYPE(le16_to_cpu(fc)) == RTLLIB_STYPE_BEACON);
}

static int IsPassiveChannel(struct rtllib_device *rtllib, u8 channel)
{
	if (MAX_CHANNEL_NUMBER < channel) {
		printk(KERN_INFO "%s(): Invalid Channel\n", __func__);
		return 0;
	}

	if (rtllib->active_channel_map[channel] == 2)
		return 1;

	return 0;
}

int rtllib_legal_channel(struct rtllib_device *rtllib, u8 channel)
{
	if (MAX_CHANNEL_NUMBER < channel) {
		printk(KERN_INFO "%s(): Invalid Channel\n", __func__);
		return 0;
	}
	if (rtllib->active_channel_map[channel] > 0)
		return 1;

	return 0;
}
EXPORT_SYMBOL(rtllib_legal_channel);

static inline void rtllib_process_probe_response(
	struct rtllib_device *ieee,
	struct rtllib_probe_response *beacon,
	struct rtllib_rx_stats *stats)
{
	struct rtllib_network *target;
	struct rtllib_network *oldest = NULL;
	struct rtllib_info_element *info_element = &beacon->info_element[0];
	unsigned long flags;
	short renew;
	struct rtllib_network *network = kzalloc(sizeof(struct rtllib_network),
						 GFP_ATOMIC);

	if (!network)
		return;

	RTLLIB_DEBUG_SCAN(
		"'%s' ( %pM ): %c%c%c%c %c%c%c%c-%c%c%c%c %c%c%c%c\n",
		escape_essid(info_element->data, info_element->len),
		beacon->header.addr3,
		(le16_to_cpu(beacon->capability) & (1<<0xf)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0xe)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0xd)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0xc)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0xb)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0xa)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x9)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x8)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x7)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x6)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x5)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x4)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x3)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x2)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x1)) ? '1' : '0',
		(le16_to_cpu(beacon->capability) & (1<<0x0)) ? '1' : '0');

	if (rtllib_network_init(ieee, beacon, network, stats)) {
		RTLLIB_DEBUG_SCAN("Dropped '%s' ( %pM) via %s.\n",
				  escape_essid(info_element->data,
				  info_element->len),
				  beacon->header.addr3,
				  WLAN_FC_GET_STYPE(
					  le16_to_cpu(beacon->header.frame_ctl)) ==
				  RTLLIB_STYPE_PROBE_RESP ?
				  "PROBE RESPONSE" : "BEACON");
		goto free_network;
	}


	if (!rtllib_legal_channel(ieee, network->channel))
		goto free_network;

	if (WLAN_FC_GET_STYPE(le16_to_cpu(beacon->header.frame_ctl)) ==
	    RTLLIB_STYPE_PROBE_RESP) {
		if (IsPassiveChannel(ieee, network->channel)) {
			printk(KERN_INFO "GetScanInfo(): For Global Domain, "
			       "filter probe response at channel(%d).\n",
			       network->channel);
			goto free_network;
		}
	}

	/* The network parsed correctly -- so now we scan our known networks
	 * to see if we can find it in our list.
	 *
	 * NOTE:  This search is definitely not optimized.  Once its doing
	 *	the "right thing" we'll optimize it for efficiency if
	 *	necessary */

	/* Search for this entry in the list and update it if it is
	 * already there. */

	spin_lock_irqsave(&ieee->lock, flags);
	if (is_same_network(&ieee->current_network, network,
	   (network->ssid_len ? 1 : 0))) {
		update_network(&ieee->current_network, network);
		if ((ieee->current_network.mode == IEEE_N_24G ||
		     ieee->current_network.mode == IEEE_G)
		     && ieee->current_network.berp_info_valid) {
			if (ieee->current_network.erp_value & ERP_UseProtection)
				ieee->current_network.buseprotection = true;
			else
				ieee->current_network.buseprotection = false;
		}
		if (is_beacon(beacon->header.frame_ctl)) {
			if (ieee->state >= RTLLIB_LINKED)
				ieee->LinkDetectInfo.NumRecvBcnInPeriod++;
		}
	}
	list_for_each_entry(target, &ieee->network_list, list) {
		if (is_same_network(target, network,
		   (target->ssid_len ? 1 : 0)))
			break;
		if ((oldest == NULL) ||
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
			RTLLIB_DEBUG_SCAN("Expired '%s' ( %pM) from "
					     "network list.\n",
					     escape_essid(target->ssid,
							  target->ssid_len),
					     target->bssid);
		} else {
			/* Otherwise just pull from the free list */
			target = list_entry(ieee->network_free_list.next,
					    struct rtllib_network, list);
			list_del(ieee->network_free_list.next);
		}


		RTLLIB_DEBUG_SCAN("Adding '%s' ( %pM) via %s.\n",
				  escape_essid(network->ssid,
				  network->ssid_len), network->bssid,
				  WLAN_FC_GET_STYPE(
					  le16_to_cpu(beacon->header.frame_ctl)) ==
				  RTLLIB_STYPE_PROBE_RESP ?
				  "PROBE RESPONSE" : "BEACON");
		memcpy(target, network, sizeof(*target));
		list_add_tail(&target->list, &ieee->network_list);
		if (ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE)
			rtllib_softmac_new_net(ieee, network);
	} else {
		RTLLIB_DEBUG_SCAN("Updating '%s' ( %pM) via %s.\n",
				  escape_essid(target->ssid,
				  target->ssid_len), target->bssid,
				  WLAN_FC_GET_STYPE(
					  le16_to_cpu(beacon->header.frame_ctl)) ==
				  RTLLIB_STYPE_PROBE_RESP ?
				  "PROBE RESPONSE" : "BEACON");

		/* we have an entry and we are going to update it. But this
		 *  entry may be already expired. In this case we do the same
		 * as we found a new net and call the new_net handler
		 */
		renew = !time_after(target->last_scanned + ieee->scan_age,
				    jiffies);
		if ((!target->ssid_len) &&
		    (((network->ssid_len > 0) && (target->hidden_ssid_len == 0))
		    || ((ieee->current_network.ssid_len == network->ssid_len) &&
		    (strncmp(ieee->current_network.ssid, network->ssid,
		    network->ssid_len) == 0) &&
		    (ieee->state == RTLLIB_NOLINK))))
			renew = 1;
		update_network(target, network);
		if (renew && (ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE))
			rtllib_softmac_new_net(ieee, network);
	}

	spin_unlock_irqrestore(&ieee->lock, flags);
	if (is_beacon(beacon->header.frame_ctl) &&
	    is_same_network(&ieee->current_network, network,
	    (network->ssid_len ? 1 : 0)) &&
	    (ieee->state == RTLLIB_LINKED)) {
		if (ieee->handle_beacon != NULL)
			ieee->handle_beacon(ieee->dev, beacon,
					    &ieee->current_network);
	}
free_network:
	kfree(network);
	return;
}

void rtllib_rx_mgt(struct rtllib_device *ieee,
		      struct sk_buff *skb,
		      struct rtllib_rx_stats *stats)
{
	struct rtllib_hdr_4addr *header = (struct rtllib_hdr_4addr *)skb->data ;

	if ((WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl)) !=
	    RTLLIB_STYPE_PROBE_RESP) &&
	    (WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl)) !=
	    RTLLIB_STYPE_BEACON))
		ieee->last_rx_ps_time = jiffies;

	switch (WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl))) {

	case RTLLIB_STYPE_BEACON:
		RTLLIB_DEBUG_MGMT("received BEACON (%d)\n",
				  WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl)));
		RTLLIB_DEBUG_SCAN("Beacon\n");
		rtllib_process_probe_response(
				ieee, (struct rtllib_probe_response *)header,
				stats);

		if (ieee->sta_sleep || (ieee->ps != RTLLIB_PS_DISABLED &&
		    ieee->iw_mode == IW_MODE_INFRA &&
		    ieee->state == RTLLIB_LINKED))
			tasklet_schedule(&ieee->ps_task);

		break;

	case RTLLIB_STYPE_PROBE_RESP:
		RTLLIB_DEBUG_MGMT("received PROBE RESPONSE (%d)\n",
			WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl)));
		RTLLIB_DEBUG_SCAN("Probe response\n");
		rtllib_process_probe_response(ieee,
			      (struct rtllib_probe_response *)header, stats);
		break;
	case RTLLIB_STYPE_PROBE_REQ:
		RTLLIB_DEBUG_MGMT("received PROBE RESQUEST (%d)\n",
				  WLAN_FC_GET_STYPE(
					  le16_to_cpu(header->frame_ctl)));
		RTLLIB_DEBUG_SCAN("Probe request\n");
		if ((ieee->softmac_features & IEEE_SOFTMAC_PROBERS) &&
		    ((ieee->iw_mode == IW_MODE_ADHOC ||
		    ieee->iw_mode == IW_MODE_MASTER) &&
		    ieee->state == RTLLIB_LINKED))
			rtllib_rx_probe_rq(ieee, skb);
		break;
	}
}
