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
//#include <linux/config.h>
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
	struct ieee80211_hdr_4addr *hdr =
		(struct ieee80211_hdr_4addr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_ctl);

	skb->dev = ieee->dev;
	skb_reset_mac_header(skb);
	skb_pull(skb, ieee80211_get_hdrlen(fc));
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = __constant_htons(ETH_P_80211_RAW);
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
		if (entry->skb != NULL &&
		    time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			IEEE80211_DEBUG_FRAG(
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
ieee80211_frag_cache_get(struct ieee80211_device *ieee,
			 struct ieee80211_hdr_4addr *hdr)
{
	struct sk_buff *skb = NULL;
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	u16 sc = le16_to_cpu(hdr->seq_ctl);
	unsigned int frag = WLAN_GET_SEQ_FRAG(sc);
	unsigned int seq = WLAN_GET_SEQ_SEQ(sc);
	struct ieee80211_frag_entry *entry;
	struct ieee80211_hdr_3addrqos *hdr_3addrqos;
	struct ieee80211_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & IEEE80211_FCTL_DSTODS) == IEEE80211_FCTL_DSTODS) && IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct ieee80211_hdr_4addrqos *)hdr;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & IEEE80211_QOS_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct ieee80211_hdr_3addrqos *)hdr;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & IEEE80211_QOS_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
		tid = 0;
	}

	if (frag == 0) {
		/* Reserve enough space to fit maximum frame length */
		skb = dev_alloc_skb(ieee->dev->mtu +
				    sizeof(struct ieee80211_hdr_4addr) +
				    8 /* LLC */ +
				    2 /* alignment */ +
				    8 /* WEP */ +
				    ETH_ALEN /* WDS */ +
				    (IEEE80211_QOS_HAS_SEQ(fc) ? 2 : 0) /* QOS Control */);
		if (skb == NULL)
			return NULL;

		entry = &ieee->frag_cache[tid][ieee->frag_next_idx[tid]];
		ieee->frag_next_idx[tid]++;
		if (ieee->frag_next_idx[tid] >= IEEE80211_FRAG_CACHE_LEN)
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
		entry = ieee80211_frag_cache_find(ieee, seq, frag, tid, hdr->addr2,
						  hdr->addr1);
		if (entry != NULL) {
			entry->last_frag = frag;
			skb = entry->skb;
		}
	}

	return skb;
}


/* Called only as a tasklet (software IRQ) */
static int ieee80211_frag_cache_invalidate(struct ieee80211_device *ieee,
					   struct ieee80211_hdr_4addr *hdr)
{
	u16 fc = le16_to_cpu(hdr->frame_ctl);
	u16 sc = le16_to_cpu(hdr->seq_ctl);
	unsigned int seq = WLAN_GET_SEQ_SEQ(sc);
	struct ieee80211_frag_entry *entry;
	struct ieee80211_hdr_3addrqos *hdr_3addrqos;
	struct ieee80211_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	if (((fc & IEEE80211_FCTL_DSTODS) == IEEE80211_FCTL_DSTODS) && IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct ieee80211_hdr_4addrqos *)hdr;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & IEEE80211_QOS_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_3addrqos = (struct ieee80211_hdr_3addrqos *)hdr;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & IEEE80211_QOS_TID;
		tid = UP2AC(tid);
		tid++;
	} else {
		tid = 0;
	}

	entry = ieee80211_frag_cache_find(ieee, seq, -1, tid, hdr->addr2,
					  hdr->addr1);

	if (entry == NULL) {
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
	struct ieee80211_hdr_4addr *hdr;

	// cheat the the hdr type
	hdr = (struct ieee80211_hdr_4addr *)skb->data;

	/* On the struct stats definition there is written that
	 * this is not mandatory.... but seems that the probe
	 * response parser uses it
	 */
	rx_stats->len = skb->len;
	ieee80211_rx_mgt(ieee, (struct ieee80211_hdr_4addr *)skb->data,
			 rx_stats);

	if ((ieee->state == IEEE80211_LINKED) && (memcmp(hdr->addr3, ieee->current_network.bssid, ETH_ALEN))) {
		dev_kfree_skb_any(skb);
		return 0;
	}

	ieee80211_rx_frame_softmac(ieee, skb, rx_stats, type, stype);

	dev_kfree_skb_any(skb);

	return 0;

}



/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static unsigned char rfc1042_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static unsigned char bridge_tunnel_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
/* No encapsulation header if EtherType < 0x600 (=length) */

/* Called by ieee80211_rx_frame_decrypt */
static int ieee80211_is_eapol_frame(struct ieee80211_device *ieee,
				    struct sk_buff *skb, size_t hdrlen)
{
	struct net_device *dev = ieee->dev;
	u16 fc, ethertype;
	struct ieee80211_hdr_4addr *hdr;
	u8 *pos;

	if (skb->len < 24)
		return 0;

	hdr = (struct ieee80211_hdr_4addr *)skb->data;
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
	struct ieee80211_hdr_4addr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_mpdu == NULL)
		return 0;

	hdr = (struct ieee80211_hdr_4addr *)skb->data;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

#ifdef CONFIG_IEEE80211_CRYPT_TKIP
	if (ieee->tkip_countermeasures &&
	    strcmp(crypt->ops->name, "TKIP") == 0) {
		if (net_ratelimit()) {
			netdev_dbg(ieee->dev,
				   "TKIP countermeasures: dropped received packet from %pM\n",
				   ieee->dev->name, hdr->addr2);
		}
		return -1;
	}
#endif

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
	struct ieee80211_hdr_4addr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_msdu == NULL)
		return 0;

	hdr = (struct ieee80211_hdr_4addr *)skb->data;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_msdu(skb, keyidx, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		netdev_dbg(ieee->dev,
			   "MSDU decryption/MIC verification failed (SA=%pM keyidx=%d)\n",
			   hdr->addr2, keyidx);
		return -1;
	}

	return 0;
}


/* this function is stolen from ipw2200 driver*/
#define IEEE_PACKET_RETRY_TIME (5*HZ)
static int is_duplicate_packet(struct ieee80211_device *ieee,
				      struct ieee80211_hdr_4addr *header)
{
	u16 fc = le16_to_cpu(header->frame_ctl);
	u16 sc = le16_to_cpu(header->seq_ctl);
	u16 seq = WLAN_GET_SEQ_SEQ(sc);
	u16 frag = WLAN_GET_SEQ_FRAG(sc);
	u16 *last_seq, *last_frag;
	unsigned long *last_time;
	struct ieee80211_hdr_3addrqos *hdr_3addrqos;
	struct ieee80211_hdr_4addrqos *hdr_4addrqos;
	u8 tid;

	//TO2DS and QoS
	if (((fc & IEEE80211_FCTL_DSTODS) == IEEE80211_FCTL_DSTODS) && IEEE80211_QOS_HAS_SEQ(fc)) {
		hdr_4addrqos = (struct ieee80211_hdr_4addrqos *)header;
		tid = le16_to_cpu(hdr_4addrqos->qos_ctl) & IEEE80211_QOS_TID;
		tid = UP2AC(tid);
		tid++;
	} else if (IEEE80211_QOS_HAS_SEQ(fc)) { //QoS
		hdr_3addrqos = (struct ieee80211_hdr_3addrqos *)header;
		tid = le16_to_cpu(hdr_3addrqos->qos_ctl) & IEEE80211_QOS_TID;
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
		if (*last_frag == frag) {
			//printk(KERN_WARNING "[1] go drop!\n");
			goto drop;

		}
		if (*last_frag + 1 != frag)
			/* out-of-order fragment */
			//printk(KERN_WARNING "[2] go drop!\n");
			goto drop;
	} else
		*last_seq = seq;

	*last_frag = frag;
	*last_time = jiffies;
	return 0;

drop:
//	BUG_ON(!(fc & IEEE80211_FCTL_RETRY));
//	printk("DUP\n");

	return 1;
}


/* All received frames are sent to this function. @skb contains the frame in
 * IEEE 802.11 format, i.e., in the format it was sent over air.
 * This function is called only as a tasklet (software IRQ). */
int ieee80211_rtl_rx(struct ieee80211_device *ieee, struct sk_buff *skb,
		 struct ieee80211_rx_stats *rx_stats)
{
	struct net_device *dev = ieee->dev;
	//struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	struct ieee80211_hdr_4addr *hdr;

	size_t hdrlen;
	u16 fc, type, stype, sc;
	struct net_device_stats *stats;
	unsigned int frag;
	u8 *payload;
	u16 ethertype;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	struct ieee80211_crypt_data *crypt = NULL;
	int keyidx = 0;

	// cheat the the hdr type
	hdr = (struct ieee80211_hdr_4addr *)skb->data;
	stats = &ieee->stats;

	if (skb->len < 10) {
		netdev_info(ieee->dev, "SKB length < 10\n");
		goto rx_dropped;
	}

	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);

	frag = WLAN_GET_SEQ_FRAG(sc);

//YJ,add,080828,for keep alive
	if ((fc & IEEE80211_FCTL_TODS) != IEEE80211_FCTL_TODS) {
		if (!memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN))
			ieee->NumRxUnicast++;
	} else {
		if (!memcmp(hdr->addr3, dev->dev_addr, ETH_ALEN))
			ieee->NumRxUnicast++;
	}
//YJ,add,080828,for keep alive,end

	hdrlen = ieee80211_get_hdrlen(fc);


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

		/* allow NULL decrypt to indicate an station specific override
		 * for default encryption */
		if (crypt && (crypt->ops == NULL ||
			      crypt->ops->decrypt_mpdu == NULL))
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
	if (is_duplicate_packet(ieee, hdr))
		goto rx_dropped;


	if (type == IEEE80211_FTYPE_MGMT) {
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
	case 0:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		memcpy(bssid, hdr->addr3, ETH_ALEN);
		break;
	}


	dev->last_rx = jiffies;


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

	ieee->NumRxDataInPeriod++;
	ieee->NumRxOkTotal++;
	/* skb: hdr + (possibly fragmented, possibly encrypted) payload */

	if (ieee->host_decrypt && (fc & IEEE80211_FCTL_WEP) &&
	    (keyidx = ieee80211_rx_frame_decrypt(ieee, skb, crypt)) < 0)
		goto rx_dropped;

	hdr = (struct ieee80211_hdr_4addr *)skb->data;

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
			netdev_warn(ieee->dev,
				    "host decrypted and reassembled frame did not fit skb\n");
			ieee80211_frag_cache_invalidate(ieee, hdr);
			goto rx_dropped;
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

		if (fc & IEEE80211_FCTL_MOREFRAGS) {
			/* more fragments expected - leave the skb in fragment
			 * cache for now; it will be delivered to upper layers
			 * after all fragments have been received */
			goto rx_exit;
		}

		/* this was the last fragment and the frame will be
		 * delivered, so remove skb from fragment cache */
		skb = frag_skb;
		hdr = (struct ieee80211_hdr_4addr *)skb->data;
		ieee80211_frag_cache_invalidate(ieee, hdr);
	}

	/* skb: hdr + (possible reassembled) full MSDU payload; possibly still
	 * encrypted/authenticated */
	if (ieee->host_decrypt && (fc & IEEE80211_FCTL_WEP) &&
	    ieee80211_rx_frame_decrypt_msdu(ieee, skb, keyidx, crypt))
		goto rx_dropped;

	hdr = (struct ieee80211_hdr_4addr *)skb->data;
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
	/* skb: hdr + (possible reassembled) full plaintext payload */
	payload = skb->data + hdrlen;
	ethertype = (payload[6] << 8) | payload[7];


	/* convert hdr + possible LLC headers into Ethernet header */
	if (skb->len - hdrlen >= 8 &&
	    ((memcmp(payload, rfc1042_header, SNAP_SIZE) == 0 &&
	      ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
	     memcmp(payload, bridge_tunnel_header, SNAP_SIZE) == 0)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		skb_pull(skb, hdrlen + SNAP_SIZE);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	} else {
		u16 len;
		/* Leave Ethernet header part of hdr and full payload */
		skb_pull(skb, hdrlen);
		len = htons(skb->len);
		memcpy(skb_push(skb, 2), &len, 2);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	}


	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	if (skb) {
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		skb->dev = dev;
		skb->ip_summed = CHECKSUM_NONE; /* 802.11 crc not sufficient */
		ieee->last_rx_ps_time = jiffies;
		netif_rx(skb);
	}

 rx_exit:
	return 1;

 rx_dropped:
	stats->rx_dropped++;

	/* Returning 0 indicates to caller that we have not handled the SKB--
	 * so it is still allocated and can be used again by underlying
	 * hardware as a DMA target */
	return 0;
}

#define MGMT_FRAME_FIXED_PART_LENGTH		0x24

static inline int ieee80211_is_ofdm_rate(u8 rate)
{
	switch (rate & ~IEEE80211_BASIC_RATE_MASK) {
	case IEEE80211_OFDM_RATE_6MB:
	case IEEE80211_OFDM_RATE_9MB:
	case IEEE80211_OFDM_RATE_12MB:
	case IEEE80211_OFDM_RATE_18MB:
	case IEEE80211_OFDM_RATE_24MB:
	case IEEE80211_OFDM_RATE_36MB:
	case IEEE80211_OFDM_RATE_48MB:
	case IEEE80211_OFDM_RATE_54MB:
		return 1;
	}
	return 0;
}

static inline int ieee80211_SignalStrengthTranslate(
	int  CurrSS
	)
{
	int RetSS;

	// Step 1. Scale mapping.
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

	//RT_TRACE(COMP_DBG, DBG_LOUD, ("##### After Mapping:  LastSS: %d, CurrSS: %d, RetSS: %d\n", LastSS, CurrSS, RetSS));

	// Step 2. Smoothing.

	//RT_TRACE(COMP_DBG, DBG_LOUD, ("$$$$$ After Smoothing:  LastSS: %d, CurrSS: %d, RetSS: %d\n", LastSS, CurrSS, RetSS));

	return RetSS;
}

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

			if (!IS_COUNTRY_IE_VALID(ieee))
				Dot11d_UpdateCountryIe(ieee, addr2, info_element->len, info_element->data);
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

static int
ieee80211_TranslateToDbm(
	unsigned char SignalStrengthIndex	// 0-100 index.
	)
{
	unsigned char SignalPower; // in dBm.

	// Translate to dBm (x=0.5y-95).
	SignalPower = (int)SignalStrengthIndex * 7 / 10;
	SignalPower -= 95;

	return SignalPower;
}
inline int ieee80211_network_init(
	struct ieee80211_device *ieee,
	struct ieee80211_probe_response *beacon,
	struct ieee80211_network *network,
	struct ieee80211_rx_stats *stats)
{
#ifdef CONFIG_IEEE80211_DEBUG
	char rates_str[64];
	char *p;
#endif
	struct ieee80211_info_element *info_element;
	u16 left;
	u8 i;
	short offset;
	u8 curRate = 0, hOpRate = 0, curRate_ex = 0;

	/* Pull out fixed field data */
	memcpy(network->bssid, beacon->header.addr3, ETH_ALEN);
	network->capability = beacon->capability;
	network->last_scanned = jiffies;
	network->time_stamp[0] = beacon->time_stamp[0];
	network->time_stamp[1] = beacon->time_stamp[1];
	network->beacon_interval = beacon->beacon_interval;
	/* Where to pull this? beacon->listen_interval;*/
	network->listen_interval = 0x0A;
	network->rates_len = network->rates_ex_len = 0;
	network->last_associate = 0;
	network->ssid_len = 0;
	network->flags = 0;
	network->atim_window = 0;
	network->QoS_Enable = 0;
//by amy 080312
	network->HighestOperaRate = 0;
//by amy 080312
	network->Turbo_Enable = 0;
	network->CountryIeLen = 0;
	memset(network->CountryIeBuf, 0, MAX_IE_LEN);

	if (stats->freq == IEEE80211_52GHZ_BAND) {
		/* for A band (No DS info) */
		network->channel = stats->received_channel;
	} else
		network->flags |= NETWORK_HAS_CCK;

	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;

	info_element = &beacon->info_element;
	left = stats->len - ((void *)info_element - (void *)beacon);
	while (left >= sizeof(struct ieee80211_info_element_hdr)) {
		if (sizeof(struct ieee80211_info_element_hdr) + info_element->len > left) {
			IEEE80211_DEBUG_SCAN("SCAN: parse failed: info_element->len + 2 > left : info_element->len+2=%d left=%d.\n",
					     info_element->len + sizeof(struct ieee80211_info_element),
					     left);
			return 1;
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

			IEEE80211_DEBUG_SCAN("MFIE_TYPE_SSID: '%s' len=%d.\n",
					     network->ssid, network->ssid_len);
			break;

		case MFIE_TYPE_RATES:
#ifdef CONFIG_IEEE80211_DEBUG
			p = rates_str;
#endif
			network->rates_len = min(info_element->len, MAX_RATES_LENGTH);
			for (i = 0; i < network->rates_len; i++) {
				network->rates[i] = info_element->data[i];
				curRate = network->rates[i] & 0x7f;
				if (hOpRate < curRate)
					hOpRate = curRate;
#ifdef CONFIG_IEEE80211_DEBUG
				p += snprintf(p, sizeof(rates_str) - (p - rates_str), "%02X ", network->rates[i]);
#endif
				if (ieee80211_is_ofdm_rate(info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    IEEE80211_BASIC_RATE_MASK)
						network->flags &=
							~NETWORK_HAS_CCK;
				}
			}

			IEEE80211_DEBUG_SCAN("MFIE_TYPE_RATES: '%s' (%d)\n",
					     rates_str, network->rates_len);
			break;

		case MFIE_TYPE_RATES_EX:
#ifdef CONFIG_IEEE80211_DEBUG
			p = rates_str;
#endif
			network->rates_ex_len = min(info_element->len, MAX_RATES_EX_LENGTH);
			for (i = 0; i < network->rates_ex_len; i++) {
				network->rates_ex[i] = info_element->data[i];
				curRate_ex = network->rates_ex[i] & 0x7f;
				if (hOpRate < curRate_ex)
					hOpRate = curRate_ex;
#ifdef CONFIG_IEEE80211_DEBUG
				p += snprintf(p, sizeof(rates_str) - (p - rates_str), "%02X ", network->rates[i]);
#endif
				if (ieee80211_is_ofdm_rate(info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    IEEE80211_BASIC_RATE_MASK)
						network->flags &=
							~NETWORK_HAS_CCK;
				}
			}

			IEEE80211_DEBUG_SCAN("MFIE_TYPE_RATES_EX: '%s' (%d)\n",
					     rates_str, network->rates_ex_len);
			break;

		case MFIE_TYPE_DS_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_DS_SET: %d\n",
					     info_element->data[0]);
			if (stats->freq == IEEE80211_24GHZ_BAND)
				network->channel = info_element->data[0];
			break;

		case MFIE_TYPE_FH_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_FH_SET: ignored\n");
			break;

		case MFIE_TYPE_CF_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_CF_SET: ignored\n");
			break;

		case MFIE_TYPE_TIM:

			if (info_element->len < 4)
				break;

			network->dtim_period = info_element->data[1];

			if (ieee->state != IEEE80211_LINKED)
				break;

			network->last_dtim_sta_time[0] = jiffies;
			network->last_dtim_sta_time[1] = stats->mac_time[1];

			network->dtim_data = IEEE80211_DTIM_VALID;

			if (info_element->data[0] != 0)
				break;

			if (info_element->data[2] & 1)
				network->dtim_data |= IEEE80211_DTIM_MBCAST;

			offset = (info_element->data[2] >> 1)*2;

			//printk("offset1:%x aid:%x\n",offset, ieee->assoc_id);

			/* add and modified for ps 2008.1.22 */
			if (ieee->assoc_id < 8*offset ||
				ieee->assoc_id > 8*(offset + info_element->len - 3)) {
				break;
			}

			offset = (ieee->assoc_id/8) - offset;// + ((aid % 8)? 0 : 1) ;

		//	printk("offset:%x data:%x, ucast:%d\n", offset,
			//	info_element->data[3+offset] ,
			//	info_element->data[3+offset] & (1<<(ieee->assoc_id%8)));

			if (info_element->data[3+offset] & (1<<(ieee->assoc_id%8)))
				network->dtim_data |= IEEE80211_DTIM_UCAST;

			break;

		case MFIE_TYPE_IBSS_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_IBSS_SET: ignored\n");
			break;

		case MFIE_TYPE_CHALLENGE:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_CHALLENGE: ignored\n");
			break;

		case MFIE_TYPE_GENERIC:
			//nic is 87B
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_GENERIC: %d bytes\n",
					     info_element->len);
			if (info_element->len >= 4  &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x50 &&
			    info_element->data[2] == 0xf2 &&
			    info_element->data[3] == 0x01) {
				network->wpa_ie_len = min(info_element->len + 2,
							 MAX_WPA_IE_LEN);
				memcpy(network->wpa_ie, info_element,
				       network->wpa_ie_len);
			}

			if (info_element->len == 7 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0xe0 &&
			    info_element->data[2] == 0x4c &&
			    info_element->data[3] == 0x01 &&
			    info_element->data[4] == 0x02) {
				network->Turbo_Enable = 1;
			}
			if (1 == stats->nic_type) //nic 87
				break;

			if (info_element->len >= 5  &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x50 &&
			    info_element->data[2] == 0xf2 &&
			    info_element->data[3] == 0x02 &&
			    info_element->data[4] == 0x00) {
				//printk(KERN_WARNING "wmm info updated: %x\n", info_element->data[6]);
				//WMM Information Element
				network->wmm_info = info_element->data[6];
				network->QoS_Enable = 1;
			}

			if (info_element->len >= 8  &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x50 &&
			    info_element->data[2] == 0xf2 &&
			    info_element->data[3] == 0x02 &&
			    info_element->data[4] == 0x01) {
				// Not care about version at present.
				//WMM Information Element
				//printk(KERN_WARNING "wmm info&param updated: %x\n", info_element->data[6]);
				network->wmm_info = info_element->data[6];
				//WMM Parameter Element
				memcpy(network->wmm_param, (u8 *)(info_element->data + 8), (info_element->len - 8));
				network->QoS_Enable = 1;
			}
			break;

		case MFIE_TYPE_RSN:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_RSN: %d bytes\n",
					     info_element->len);
			network->rsn_ie_len = min(info_element->len + 2,
						 MAX_WPA_IE_LEN);
			memcpy(network->rsn_ie, info_element,
			       network->rsn_ie_len);
			break;
		case MFIE_TYPE_COUNTRY:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_COUNTRY: %d bytes\n",
					     info_element->len);
//			printk("=====>Receive <%s> Country IE\n",network->ssid);
			ieee80211_extract_country_ie(ieee, info_element, network, beacon->header.addr2);
			break;
		default:
			IEEE80211_DEBUG_SCAN("unsupported IE %d\n",
					     info_element->id);
			break;
		}

		left -= sizeof(struct ieee80211_info_element_hdr) +
			info_element->len;
		info_element = (struct ieee80211_info_element *)
			&info_element->data[info_element->len];
	}
//by amy 080312
	network->HighestOperaRate = hOpRate;
//by amy 080312
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

	if (ieee80211_is_empty_essid(network->ssid, network->ssid_len))
		network->flags |= NETWORK_EMPTY_ESSID;

	stats->signal = ieee80211_TranslateToDbm(stats->signalstrength);
	//stats->noise = stats->signal - stats->noise;
	stats->noise = ieee80211_TranslateToDbm(100 - stats->signalstrength) - 25;
	memcpy(&network->stats, stats, sizeof(network->stats));

	return 0;
}

static inline int is_same_network(struct ieee80211_network *src,
				  struct ieee80211_network *dst,
				  struct ieee80211_device *ieee)
{
	/* A network is only a duplicate if the channel, BSSID, ESSID
	 * and the capability field (in particular IBSS and BSS) all match.
	 * We treat all <hidden> with the same BSSID and channel
	 * as one network */
	return (((src->ssid_len == dst->ssid_len) || (ieee->iw_mode == IW_MODE_INFRA)) &&  //YJ,mod,080819,for hidden ap
		//((src->ssid_len == dst->ssid_len) &&
		(src->channel == dst->channel) &&
		!memcmp(src->bssid, dst->bssid, ETH_ALEN) &&
		(!memcmp(src->ssid, dst->ssid, src->ssid_len) || (ieee->iw_mode == IW_MODE_INFRA)) && //YJ,mod,080819,for hidden ap
		//!memcmp(src->ssid, dst->ssid, src->ssid_len) &&
		((src->capability & WLAN_CAPABILITY_IBSS) ==
		(dst->capability & WLAN_CAPABILITY_IBSS)) &&
		((src->capability & WLAN_CAPABILITY_BSS) ==
		(dst->capability & WLAN_CAPABILITY_BSS)));
}

inline void update_network(struct ieee80211_network *dst,
				  struct ieee80211_network *src)
{
	unsigned char quality = src->stats.signalstrength;
	unsigned char signal = 0;
	unsigned char noise = 0;
	if (dst->stats.signalstrength > 0)
		quality = (dst->stats.signalstrength * 5 + src->stats.signalstrength + 5)/6;
	signal = ieee80211_TranslateToDbm(quality);
	//noise = signal - src->stats.noise;
	if (dst->stats.noise > 0)
		noise = (dst->stats.noise * 5 + src->stats.noise)/6;
        //if(strcmp(dst->ssid, "linksys_lzm000") == 0)
//	printk("ssid:%s, quality:%d, signal:%d\n", dst->ssid, quality, signal);
	memcpy(&dst->stats, &src->stats, sizeof(struct ieee80211_rx_stats));
	dst->stats.signalstrength = quality;
	dst->stats.signal = signal;
//	printk("==================>stats.signal is %d\n",dst->stats.signal);
	dst->stats.noise = noise;


	dst->capability = src->capability;
	memcpy(dst->rates, src->rates, src->rates_len);
	dst->rates_len = src->rates_len;
	memcpy(dst->rates_ex, src->rates_ex, src->rates_ex_len);
	dst->rates_ex_len = src->rates_ex_len;
	dst->HighestOperaRate = src->HighestOperaRate;
	//printk("==========>in %s: src->ssid is %s,chan is %d\n",__func__,src->ssid,src->channel);

	//YJ,add,080819,for hidden ap
	if (src->ssid_len > 0) {
		//if(src->ssid_len == 13)
		//	printk("=====================>>>>>>>> Dst ssid: %s Src ssid: %s\n", dst->ssid, src->ssid);
		memset(dst->ssid, 0, dst->ssid_len);
		dst->ssid_len = src->ssid_len;
		memcpy(dst->ssid, src->ssid, src->ssid_len);
	}
	//YJ,add,080819,for hidden ap,end

	dst->channel = src->channel;
	dst->mode = src->mode;
	dst->flags = src->flags;
	dst->time_stamp[0] = src->time_stamp[0];
	dst->time_stamp[1] = src->time_stamp[1];

	dst->beacon_interval = src->beacon_interval;
	dst->listen_interval = src->listen_interval;
	dst->atim_window = src->atim_window;
	dst->dtim_period = src->dtim_period;
	dst->dtim_data = src->dtim_data;
	dst->last_dtim_sta_time[0] = src->last_dtim_sta_time[0];
	dst->last_dtim_sta_time[1] = src->last_dtim_sta_time[1];
//	printk("update:%s, dtim_period:%x, dtim_data:%x\n", src->ssid, src->dtim_period, src->dtim_data);
	memcpy(dst->wpa_ie, src->wpa_ie, src->wpa_ie_len);
	dst->wpa_ie_len = src->wpa_ie_len;
	memcpy(dst->rsn_ie, src->rsn_ie, src->rsn_ie_len);
	dst->rsn_ie_len = src->rsn_ie_len;

	dst->last_scanned = jiffies;
	/* dst->last_associate is not overwritten */
// disable QoS process now, added by David 2006/7/25
#if 1
	dst->wmm_info = src->wmm_info; //sure to exist in beacon or probe response frame.
/*
	if((dst->wmm_info^src->wmm_info)&0x0f) {//Param Set Count change, update Parameter
	  memcpy(dst->wmm_param, src->wmm_param, IEEE80211_AC_PRAM_LEN);
	}
*/
	if (src->wmm_param[0].ac_aci_acm_aifsn || \
	   src->wmm_param[1].ac_aci_acm_aifsn || \
	   src->wmm_param[2].ac_aci_acm_aifsn || \
	   src->wmm_param[3].ac_aci_acm_aifsn) {
		memcpy(dst->wmm_param, src->wmm_param, WME_AC_PRAM_LEN);
	}
	dst->QoS_Enable = src->QoS_Enable;
#else
	dst->QoS_Enable = 1;//for Rtl8187 simulation
#endif
	dst->SignalStrength = src->SignalStrength;
	dst->Turbo_Enable = src->Turbo_Enable;
	dst->CountryIeLen = src->CountryIeLen;
	memcpy(dst->CountryIeBuf, src->CountryIeBuf, src->CountryIeLen);
}


inline void ieee80211_process_probe_response(
	struct ieee80211_device *ieee,
	struct ieee80211_probe_response *beacon,
	struct ieee80211_rx_stats *stats)
{
	struct ieee80211_network network;
	struct ieee80211_network *target;
	struct ieee80211_network *oldest = NULL;
#ifdef CONFIG_IEEE80211_DEBUG
	struct ieee80211_info_element *info_element = &beacon->info_element;
#endif
	unsigned long flags;
	short renew;
	u8 wmm_info;
	u8 is_beacon = (WLAN_FC_GET_STYPE(beacon->header.frame_ctl) == IEEE80211_STYPE_BEACON) ? 1 : 0;  //YJ,add,080819,for hidden ap

	memset(&network, 0, sizeof(struct ieee80211_network));

	IEEE80211_DEBUG_SCAN(
		"'%s' (%pM): %c%c%c%c %c%c%c%c-%c%c%c%c %c%c%c%c\n",
		escape_essid(info_element->data, info_element->len),
		beacon->header.addr3,
		(beacon->capability & (1<<0xf)) ? '1' : '0',
		(beacon->capability & (1<<0xe)) ? '1' : '0',
		(beacon->capability & (1<<0xd)) ? '1' : '0',
		(beacon->capability & (1<<0xc)) ? '1' : '0',
		(beacon->capability & (1<<0xb)) ? '1' : '0',
		(beacon->capability & (1<<0xa)) ? '1' : '0',
		(beacon->capability & (1<<0x9)) ? '1' : '0',
		(beacon->capability & (1<<0x8)) ? '1' : '0',
		(beacon->capability & (1<<0x7)) ? '1' : '0',
		(beacon->capability & (1<<0x6)) ? '1' : '0',
		(beacon->capability & (1<<0x5)) ? '1' : '0',
		(beacon->capability & (1<<0x4)) ? '1' : '0',
		(beacon->capability & (1<<0x3)) ? '1' : '0',
		(beacon->capability & (1<<0x2)) ? '1' : '0',
		(beacon->capability & (1<<0x1)) ? '1' : '0',
		(beacon->capability & (1<<0x0)) ? '1' : '0');

	if (ieee80211_network_init(ieee, beacon, &network, stats)) {
		IEEE80211_DEBUG_SCAN("Dropped '%s' (%pM) via %s.\n",
				     escape_essid(info_element->data,
						  info_element->len),
				     beacon->header.addr3,
				     WLAN_FC_GET_STYPE(beacon->header.frame_ctl) ==
				     IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
		return;
	}

	// For Asus EeePc request,
	// (1) if wireless adapter receive get any 802.11d country code in AP beacon,
	//	   wireless adapter should follow the country code.
	// (2)  If there is no any country code in beacon,
	//       then wireless adapter should do active scan from ch1~11 and
	//       passive scan from ch12~14
	if (ieee->bGlobalDomain) {
		if (WLAN_FC_GET_STYPE(beacon->header.frame_ctl) == IEEE80211_STYPE_PROBE_RESP) {
			// Case 1: Country code
			if (IS_COUNTRY_IE_VALID(ieee)) {
				if (!IsLegalChannel(ieee, network.channel)) {
					printk("GetScanInfo(): For Country code, filter probe response at channel(%d).\n", network.channel);
					return;
				}
			}
			// Case 2: No any country code.
			else {
				// Filter over channel ch12~14
				if (network.channel > 11) {
					printk("GetScanInfo(): For Global Domain, filter probe response at channel(%d).\n", network.channel);
					return;
				}
			}
		} else {
			// Case 1: Country code
			if (IS_COUNTRY_IE_VALID(ieee)) {
				if (!IsLegalChannel(ieee, network.channel)) {
					printk("GetScanInfo(): For Country code, filter beacon at channel(%d).\n", network.channel);
					return;
				}
			}
			// Case 2: No any country code.
			else {
				// Filter over channel ch12~14
				if (network.channel > 14) {
					printk("GetScanInfo(): For Global Domain, filter beacon at channel(%d).\n", network.channel);
					return;
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

	if (is_same_network(&ieee->current_network, &network, ieee)) {
		wmm_info = ieee->current_network.wmm_info;
		//YJ,add,080819,for hidden ap
		if (is_beacon == 0)
			network.flags = (~NETWORK_EMPTY_ESSID & network.flags)|(NETWORK_EMPTY_ESSID & ieee->current_network.flags);
		else if (ieee->state == IEEE80211_LINKED)
			ieee->NumRxBcnInPeriod++;
		//YJ,add,080819,for hidden ap,end
		//printk("====>network.ssid=%s cur_ssid=%s\n", network.ssid, ieee->current_network.ssid);
		update_network(&ieee->current_network, &network);
	}

	list_for_each_entry(target, &ieee->network_list, list) {
		if (is_same_network(target, &network, ieee))
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
				     escape_essid(network.ssid,
						  network.ssid_len),
				     network.bssid,
				     WLAN_FC_GET_STYPE(beacon->header.frame_ctl) ==
				     IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
#endif

		memcpy(target, &network, sizeof(*target));
		list_add_tail(&target->list, &ieee->network_list);
	} else {
		IEEE80211_DEBUG_SCAN("Updating '%s' (%pM) via %s.\n",
				     escape_essid(target->ssid,
						  target->ssid_len),
				     target->bssid,
				     WLAN_FC_GET_STYPE(beacon->header.frame_ctl) ==
				     IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");

		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		renew = !time_after(target->last_scanned + ieee->scan_age, jiffies);
		//YJ,add,080819,for hidden ap
		if (is_beacon == 0)
			network.flags = (~NETWORK_EMPTY_ESSID & network.flags)|(NETWORK_EMPTY_ESSID & target->flags);
		//if(strncmp(network.ssid, "linksys-c",9) == 0)
		//	printk("====>2 network.ssid=%s FLAG=%d target.ssid=%s FLAG=%d\n", network.ssid, network.flags, target->ssid, target->flags);
		if (((network.flags & NETWORK_EMPTY_ESSID) == NETWORK_EMPTY_ESSID) \
		    && (((network.ssid_len > 0) && (strncmp(target->ssid, network.ssid, network.ssid_len)))\
		    || ((ieee->current_network.ssid_len == network.ssid_len) && (strncmp(ieee->current_network.ssid, network.ssid, network.ssid_len) == 0) && (ieee->state == IEEE80211_NOLINK))))
			renew = 1;
		//YJ,add,080819,for hidden ap,end
		update_network(target, &network);
	}

	spin_unlock_irqrestore(&ieee->lock, flags);
}

void ieee80211_rx_mgt(struct ieee80211_device *ieee,
		      struct ieee80211_hdr_4addr *header,
		      struct ieee80211_rx_stats *stats)
{
	switch (WLAN_FC_GET_STYPE(header->frame_ctl)) {

	case IEEE80211_STYPE_BEACON:
		IEEE80211_DEBUG_MGMT("received BEACON (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		IEEE80211_DEBUG_SCAN("Beacon\n");
		ieee80211_process_probe_response(
			ieee, (struct ieee80211_probe_response *)header, stats);
		break;

	case IEEE80211_STYPE_PROBE_RESP:
		IEEE80211_DEBUG_MGMT("received PROBE RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		IEEE80211_DEBUG_SCAN("Probe response\n");
		ieee80211_process_probe_response(
			ieee, (struct ieee80211_probe_response *)header, stats);
		break;
	}
}
