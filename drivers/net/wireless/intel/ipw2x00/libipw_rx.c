// SPDX-License-Identifier: GPL-2.0-only
/*
 * Original code based Host AP (software wireless LAN access point) driver
 * for Intersil Prism2/2.5/3 - hostap.o module, common routines
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <j@w1.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2005, Intel Corporation
 */

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/gfp.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include "libipw.h"

static void libipw_monitor_rx(struct libipw_device *ieee,
					struct sk_buff *skb,
					struct libipw_rx_stats *rx_stats)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_control);

	skb->dev = ieee->dev;
	skb_reset_mac_header(skb);
	skb_pull(skb, libipw_get_hdrlen(fc));
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_80211_RAW);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

/* Called only as a tasklet (software IRQ) */
static struct libipw_frag_entry *libipw_frag_cache_find(struct
							      libipw_device
							      *ieee,
							      unsigned int seq,
							      unsigned int frag,
							      u8 * src,
							      u8 * dst)
{
	struct libipw_frag_entry *entry;
	int i;

	for (i = 0; i < LIBIPW_FRAG_CACHE_LEN; i++) {
		entry = &ieee->frag_cache[i];
		if (entry->skb != NULL &&
		    time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			LIBIPW_DEBUG_FRAG("expiring fragment cache entry "
					     "seq=%u last_frag=%u\n",
					     entry->seq, entry->last_frag);
			dev_kfree_skb_any(entry->skb);
			entry->skb = NULL;
		}

		if (entry->skb != NULL && entry->seq == seq &&
		    (entry->last_frag + 1 == frag || frag == -1) &&
		    ether_addr_equal(entry->src_addr, src) &&
		    ether_addr_equal(entry->dst_addr, dst))
			return entry;
	}

	return NULL;
}

/* Called only as a tasklet (software IRQ) */
static struct sk_buff *libipw_frag_cache_get(struct libipw_device *ieee,
						struct libipw_hdr_4addr *hdr)
{
	struct sk_buff *skb = NULL;
	u16 sc;
	unsigned int frag, seq;
	struct libipw_frag_entry *entry;

	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	seq = WLAN_GET_SEQ_SEQ(sc);

	if (frag == 0) {
		/* Reserve enough space to fit maximum frame length */
		skb = dev_alloc_skb(ieee->dev->mtu +
				    sizeof(struct libipw_hdr_4addr) +
				    8 /* LLC */  +
				    2 /* alignment */  +
				    8 /* WEP */  + ETH_ALEN /* WDS */ );
		if (skb == NULL)
			return NULL;

		entry = &ieee->frag_cache[ieee->frag_next_idx];
		ieee->frag_next_idx++;
		if (ieee->frag_next_idx >= LIBIPW_FRAG_CACHE_LEN)
			ieee->frag_next_idx = 0;

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
		entry = libipw_frag_cache_find(ieee, seq, frag, hdr->addr2,
						  hdr->addr1);
		if (entry != NULL) {
			entry->last_frag = frag;
			skb = entry->skb;
		}
	}

	return skb;
}

/* Called only as a tasklet (software IRQ) */
static int libipw_frag_cache_invalidate(struct libipw_device *ieee,
					   struct libipw_hdr_4addr *hdr)
{
	u16 sc;
	unsigned int seq;
	struct libipw_frag_entry *entry;

	sc = le16_to_cpu(hdr->seq_ctl);
	seq = WLAN_GET_SEQ_SEQ(sc);

	entry = libipw_frag_cache_find(ieee, seq, -1, hdr->addr2,
					  hdr->addr1);

	if (entry == NULL) {
		LIBIPW_DEBUG_FRAG("could not invalidate fragment cache "
				     "entry (seq=%u)\n", seq);
		return -1;
	}

	entry->skb = NULL;
	return 0;
}

#ifdef NOT_YET
/* libipw_rx_frame_mgtmt
 *
 * Responsible for handling management control frames
 *
 * Called by libipw_rx */
static int
libipw_rx_frame_mgmt(struct libipw_device *ieee, struct sk_buff *skb,
			struct libipw_rx_stats *rx_stats, u16 type,
			u16 stype)
{
	if (ieee->iw_mode == IW_MODE_MASTER) {
		printk(KERN_DEBUG "%s: Master mode not yet supported.\n",
		       ieee->dev->name);
		return 0;
/*
  hostap_update_sta_ps(ieee, (struct hostap_libipw_hdr_4addr *)
  skb->data);*/
	}

	if (ieee->hostapd && type == WLAN_FC_TYPE_MGMT) {
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
			printk(KERN_DEBUG "%s: unknown management frame "
			       "(type=0x%02x, stype=0x%02x) dropped\n",
			       skb->dev->name, type, stype);
			return -1;
		}

		hostap_rx(skb->dev, skb, rx_stats);
		return 0;
	}

	printk(KERN_DEBUG "%s: hostap_rx_frame_mgmt: management frame "
	       "received in non-Host AP mode\n", skb->dev->name);
	return -1;
}
#endif

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static unsigned char libipw_rfc1042_header[] =
    { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static unsigned char libipw_bridge_tunnel_header[] =
    { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
/* No encapsulation header if EtherType < 0x600 (=length) */

/* Called by libipw_rx_frame_decrypt */
static int libipw_is_eapol_frame(struct libipw_device *ieee,
				    struct sk_buff *skb)
{
	struct net_device *dev = ieee->dev;
	u16 fc, ethertype;
	struct libipw_hdr_3addr *hdr;
	u8 *pos;

	if (skb->len < 24)
		return 0;

	hdr = (struct libipw_hdr_3addr *)skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);

	/* check that the frame is unicast frame to us */
	if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    IEEE80211_FCTL_TODS &&
	    ether_addr_equal(hdr->addr1, dev->dev_addr) &&
	    ether_addr_equal(hdr->addr3, dev->dev_addr)) {
		/* ToDS frame with own addr BSSID and DA */
	} else if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		   IEEE80211_FCTL_FROMDS &&
		   ether_addr_equal(hdr->addr1, dev->dev_addr)) {
		/* FromDS frame with own addr as DA */
	} else
		return 0;

	if (skb->len < 24 + 8)
		return 0;

	/* check for port access entity Ethernet type */
	pos = skb->data + 24;
	ethertype = (pos[6] << 8) | pos[7];
	if (ethertype == ETH_P_PAE)
		return 1;

	return 0;
}

/* Called only as a tasklet (software IRQ), by libipw_rx */
static int
libipw_rx_frame_decrypt(struct libipw_device *ieee, struct sk_buff *skb,
			   struct libipw_crypt_data *crypt)
{
	struct libipw_hdr_3addr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_mpdu == NULL)
		return 0;

	hdr = (struct libipw_hdr_3addr *)skb->data;
	hdrlen = libipw_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_mpdu(skb, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		LIBIPW_DEBUG_DROP("decryption failed (SA=%pM) res=%d\n",
				     hdr->addr2, res);
		if (res == -2)
			LIBIPW_DEBUG_DROP("Decryption failed ICV "
					     "mismatch (key %d)\n",
					     skb->data[hdrlen + 3] >> 6);
		ieee->ieee_stats.rx_discards_undecryptable++;
		return -1;
	}

	return res;
}

/* Called only as a tasklet (software IRQ), by libipw_rx */
static int
libipw_rx_frame_decrypt_msdu(struct libipw_device *ieee,
				struct sk_buff *skb, int keyidx,
				struct libipw_crypt_data *crypt)
{
	struct libipw_hdr_3addr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_msdu == NULL)
		return 0;

	hdr = (struct libipw_hdr_3addr *)skb->data;
	hdrlen = libipw_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_msdu(skb, keyidx, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_DEBUG "%s: MSDU decryption/MIC verification failed"
		       " (SA=%pM keyidx=%d)\n", ieee->dev->name, hdr->addr2,
		       keyidx);
		return -1;
	}

	return 0;
}

/* All received frames are sent to this function. @skb contains the frame in
 * IEEE 802.11 format, i.e., in the format it was sent over air.
 * This function is called only as a tasklet (software IRQ). */
int libipw_rx(struct libipw_device *ieee, struct sk_buff *skb,
		 struct libipw_rx_stats *rx_stats)
{
	struct net_device *dev = ieee->dev;
	struct libipw_hdr_4addr *hdr;
	size_t hdrlen;
	u16 fc, type, stype, sc;
	unsigned int frag;
	u8 *payload;
	u16 ethertype;
#ifdef NOT_YET
	struct net_device *wds = NULL;
	struct sk_buff *skb2 = NULL;
	struct net_device *wds = NULL;
	int frame_authorized = 0;
	int from_assoc_ap = 0;
	void *sta = NULL;
#endif
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	struct libipw_crypt_data *crypt = NULL;
	int keyidx = 0;
	int can_be_decrypted = 0;

	hdr = (struct libipw_hdr_4addr *)skb->data;
	if (skb->len < 10) {
		printk(KERN_INFO "%s: SKB length < 10\n", dev->name);
		goto rx_dropped;
	}

	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	hdrlen = libipw_get_hdrlen(fc);

	if (skb->len < hdrlen) {
		printk(KERN_INFO "%s: invalid SKB length %d\n",
			dev->name, skb->len);
		goto rx_dropped;
	}

	/* Put this code here so that we avoid duplicating it in all
	 * Rx paths. - Jean II */
#ifdef CONFIG_WIRELESS_EXT
#ifdef IW_WIRELESS_SPY		/* defined in iw_handler.h */
	/* If spy monitoring on */
	if (ieee->spy_data.spy_number > 0) {
		struct iw_quality wstats;

		wstats.updated = 0;
		if (rx_stats->mask & LIBIPW_STATMASK_RSSI) {
			wstats.level = rx_stats->signal;
			wstats.updated |= IW_QUAL_LEVEL_UPDATED;
		} else
			wstats.updated |= IW_QUAL_LEVEL_INVALID;

		if (rx_stats->mask & LIBIPW_STATMASK_NOISE) {
			wstats.noise = rx_stats->noise;
			wstats.updated |= IW_QUAL_NOISE_UPDATED;
		} else
			wstats.updated |= IW_QUAL_NOISE_INVALID;

		if (rx_stats->mask & LIBIPW_STATMASK_SIGNAL) {
			wstats.qual = rx_stats->signal;
			wstats.updated |= IW_QUAL_QUAL_UPDATED;
		} else
			wstats.updated |= IW_QUAL_QUAL_INVALID;

		/* Update spy records */
		libipw_spy_update(ieee->dev, hdr->addr2, &wstats);
	}
#endif				/* IW_WIRELESS_SPY */
#endif				/* CONFIG_WIRELESS_EXT */

#ifdef NOT_YET
	hostap_update_rx_stats(local->ap, hdr, rx_stats);
#endif

	if (ieee->iw_mode == IW_MODE_MONITOR) {
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
		libipw_monitor_rx(ieee, skb, rx_stats);
		return 1;
	}

	can_be_decrypted = (is_multicast_ether_addr(hdr->addr1) ||
			    is_broadcast_ether_addr(hdr->addr2)) ?
	    ieee->host_mc_decrypt : ieee->host_decrypt;

	if (can_be_decrypted) {
		if (skb->len >= hdrlen + 3) {
			/* Top two-bits of byte 3 are the key index */
			keyidx = skb->data[hdrlen + 3] >> 6;
		}

		/* ieee->crypt[] is WEP_KEY (4) in length.  Given that keyidx
		 * is only allowed 2-bits of storage, no value of keyidx can
		 * be provided via above code that would result in keyidx
		 * being out of range */
		crypt = ieee->crypt_info.crypt[keyidx];

#ifdef NOT_YET
		sta = NULL;

		/* Use station specific key to override default keys if the
		 * receiver address is a unicast address ("individual RA"). If
		 * bcrx_sta_key parameter is set, station specific key is used
		 * even with broad/multicast targets (this is against IEEE
		 * 802.11, but makes it easier to use different keys with
		 * stations that do not support WEP key mapping). */

		if (is_unicast_ether_addr(hdr->addr1) || local->bcrx_sta_key)
			(void)hostap_handle_sta_crypto(local, hdr, &crypt,
						       &sta);
#endif

		/* allow NULL decrypt to indicate an station specific override
		 * for default encryption */
		if (crypt && (crypt->ops == NULL ||
			      crypt->ops->decrypt_mpdu == NULL))
			crypt = NULL;

		if (!crypt && (fc & IEEE80211_FCTL_PROTECTED)) {
			/* This seems to be triggered by some (multicast?)
			 * frames from other than current BSS, so just drop the
			 * frames silently instead of filling system log with
			 * these reports. */
			LIBIPW_DEBUG_DROP("Decryption failed (not set)"
					     " (SA=%pM)\n", hdr->addr2);
			ieee->ieee_stats.rx_discards_undecryptable++;
			goto rx_dropped;
		}
	}
#ifdef NOT_YET
	if (type != WLAN_FC_TYPE_DATA) {
		if (type == WLAN_FC_TYPE_MGMT && stype == WLAN_FC_STYPE_AUTH &&
		    fc & IEEE80211_FCTL_PROTECTED && ieee->host_decrypt &&
		    (keyidx = hostap_rx_frame_decrypt(ieee, skb, crypt)) < 0) {
			printk(KERN_DEBUG "%s: failed to decrypt mgmt::auth "
			       "from %pM\n", dev->name, hdr->addr2);
			/* TODO: could inform hostapd about this so that it
			 * could send auth failure report */
			goto rx_dropped;
		}

		if (libipw_rx_frame_mgmt(ieee, skb, rx_stats, type, stype))
			goto rx_dropped;
		else
			goto rx_exit;
	}
#endif
	/* drop duplicate 802.11 retransmissions (IEEE 802.11 Chap. 9.29) */
	if (sc == ieee->prev_seq_ctl)
		goto rx_dropped;
	else
		ieee->prev_seq_ctl = sc;

	/* Data frame - extract src/dst addresses */
	if (skb->len < LIBIPW_3ADDR_LEN)
		goto rx_dropped;

	switch (fc & (IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) {
	case IEEE80211_FCTL_FROMDS:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);
		break;
	case IEEE80211_FCTL_TODS:
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		break;
	case IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS:
		if (skb->len < LIBIPW_4ADDR_LEN)
			goto rx_dropped;
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);
		break;
	default:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
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
	    (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    IEEE80211_FCTL_FROMDS && ieee->stadev &&
	    ether_addr_equal(hdr->addr2, ieee->assoc_ap_addr)) {
		/* Frame from BSSID of the AP for which we are a client */
		skb->dev = dev = ieee->stadev;
		stats = hostap_get_stats(dev);
		from_assoc_ap = 1;
	}
#endif

#ifdef NOT_YET
	if ((ieee->iw_mode == IW_MODE_MASTER ||
	     ieee->iw_mode == IW_MODE_REPEAT) && !from_assoc_ap) {
		switch (hostap_handle_sta_rx(ieee, dev, skb, rx_stats,
					     wds != NULL)) {
		case AP_RX_CONTINUE_NOT_AUTHORIZED:
			frame_authorized = 0;
			break;
		case AP_RX_CONTINUE:
			frame_authorized = 1;
			break;
		case AP_RX_DROP:
			goto rx_dropped;
		case AP_RX_EXIT:
			goto rx_exit;
		}
	}
#endif

	/* Nullfunc frames may have PS-bit set, so they must be passed to
	 * hostap_handle_sta_rx() before being dropped here. */

	stype &= ~IEEE80211_STYPE_QOS_DATA;

	if (stype != IEEE80211_STYPE_DATA &&
	    stype != IEEE80211_STYPE_DATA_CFACK &&
	    stype != IEEE80211_STYPE_DATA_CFPOLL &&
	    stype != IEEE80211_STYPE_DATA_CFACKPOLL) {
		if (stype != IEEE80211_STYPE_NULLFUNC)
			LIBIPW_DEBUG_DROP("RX: dropped data frame "
					     "with no data (type=0x%02x, "
					     "subtype=0x%02x, len=%d)\n",
					     type, stype, skb->len);
		goto rx_dropped;
	}

	/* skb: hdr + (possibly fragmented, possibly encrypted) payload */

	if ((fc & IEEE80211_FCTL_PROTECTED) && can_be_decrypted &&
	    (keyidx = libipw_rx_frame_decrypt(ieee, skb, crypt)) < 0)
		goto rx_dropped;

	hdr = (struct libipw_hdr_4addr *)skb->data;

	/* skb: hdr + (possibly fragmented) plaintext payload */
	// PR: FIXME: hostap has additional conditions in the "if" below:
	// ieee->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	if ((frag != 0) || (fc & IEEE80211_FCTL_MOREFRAGS)) {
		int flen;
		struct sk_buff *frag_skb = libipw_frag_cache_get(ieee, hdr);
		LIBIPW_DEBUG_FRAG("Rx Fragment received (%u)\n", frag);

		if (!frag_skb) {
			LIBIPW_DEBUG(LIBIPW_DL_RX | LIBIPW_DL_FRAG,
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
			printk(KERN_WARNING "%s: host decrypted and "
			       "reassembled frame did not fit skb\n",
			       dev->name);
			libipw_frag_cache_invalidate(ieee, hdr);
			goto rx_dropped;
		}

		if (frag == 0) {
			/* copy first fragment (including full headers) into
			 * beginning of the fragment cache skb */
			skb_copy_from_linear_data(skb, skb_put(frag_skb, flen), flen);
		} else {
			/* append frame payload to the end of the fragment
			 * cache skb */
			skb_copy_from_linear_data_offset(skb, hdrlen,
				      skb_put(frag_skb, flen), flen);
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
		hdr = (struct libipw_hdr_4addr *)skb->data;
		libipw_frag_cache_invalidate(ieee, hdr);
	}

	/* skb: hdr + (possible reassembled) full MSDU payload; possibly still
	 * encrypted/authenticated */
	if ((fc & IEEE80211_FCTL_PROTECTED) && can_be_decrypted &&
	    libipw_rx_frame_decrypt_msdu(ieee, skb, keyidx, crypt))
		goto rx_dropped;

	hdr = (struct libipw_hdr_4addr *)skb->data;
	if (crypt && !(fc & IEEE80211_FCTL_PROTECTED) && !ieee->open_wep) {
		if (		/*ieee->ieee802_1x && */
			   libipw_is_eapol_frame(ieee, skb)) {
			/* pass unencrypted EAPOL frames even if encryption is
			 * configured */
		} else {
			LIBIPW_DEBUG_DROP("encryption configured, but RX "
					     "frame not encrypted (SA=%pM)\n",
					     hdr->addr2);
			goto rx_dropped;
		}
	}

	if (crypt && !(fc & IEEE80211_FCTL_PROTECTED) && !ieee->open_wep &&
	    !libipw_is_eapol_frame(ieee, skb)) {
		LIBIPW_DEBUG_DROP("dropped unencrypted RX data "
				     "frame from %pM (drop_unencrypted=1)\n",
				     hdr->addr2);
		goto rx_dropped;
	}

	/* If the frame was decrypted in hardware, we may need to strip off
	 * any security data (IV, ICV, etc) that was left behind */
	if (!can_be_decrypted && (fc & IEEE80211_FCTL_PROTECTED) &&
	    ieee->host_strip_iv_icv) {
		int trimlen = 0;

		/* Top two-bits of byte 3 are the key index */
		if (skb->len >= hdrlen + 3)
			keyidx = skb->data[hdrlen + 3] >> 6;

		/* To strip off any security data which appears before the
		 * payload, we simply increase hdrlen (as the header gets
		 * chopped off immediately below). For the security data which
		 * appears after the payload, we use skb_trim. */

		switch (ieee->sec.encode_alg[keyidx]) {
		case SEC_ALG_WEP:
			/* 4 byte IV */
			hdrlen += 4;
			/* 4 byte ICV */
			trimlen = 4;
			break;
		case SEC_ALG_TKIP:
			/* 4 byte IV, 4 byte ExtIV */
			hdrlen += 8;
			/* 8 byte MIC, 4 byte ICV */
			trimlen = 12;
			break;
		case SEC_ALG_CCMP:
			/* 8 byte CCMP header */
			hdrlen += 8;
			/* 8 byte MIC */
			trimlen = 8;
			break;
		}

		if (skb->len < trimlen)
			goto rx_dropped;

		__skb_trim(skb, skb->len - trimlen);

		if (skb->len < hdrlen)
			goto rx_dropped;
	}

	/* skb: hdr + (possible reassembled) full plaintext payload */

	payload = skb->data + hdrlen;
	ethertype = (payload[6] << 8) | payload[7];

#ifdef NOT_YET
	/* If IEEE 802.1X is used, check whether the port is authorized to send
	 * the received frame. */
	if (ieee->ieee802_1x && ieee->iw_mode == IW_MODE_MASTER) {
		if (ethertype == ETH_P_PAE) {
			printk(KERN_DEBUG "%s: RX: IEEE 802.1X frame\n",
			       dev->name);
			if (ieee->hostapd && ieee->apdev) {
				/* Send IEEE 802.1X frames to the user
				 * space daemon for processing */
				prism2_rx_80211(ieee->apdev, skb, rx_stats,
						PRISM2_RX_MGMT);
				ieee->apdevstats.rx_packets++;
				ieee->apdevstats.rx_bytes += skb->len;
				goto rx_exit;
			}
		} else if (!frame_authorized) {
			printk(KERN_DEBUG "%s: dropped frame from "
			       "unauthorized port (IEEE 802.1X): "
			       "ethertype=0x%04x\n", dev->name, ethertype);
			goto rx_dropped;
		}
	}
#endif

	/* convert hdr + possible LLC headers into Ethernet header */
	if (skb->len - hdrlen >= 8 &&
	    ((memcmp(payload, libipw_rfc1042_header, SNAP_SIZE) == 0 &&
	      ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
	     memcmp(payload, libipw_bridge_tunnel_header, SNAP_SIZE) == 0)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		skb_pull(skb, hdrlen + SNAP_SIZE);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	} else {
		__be16 len;
		/* Leave Ethernet header part of hdr and full payload */
		skb_pull(skb, hdrlen);
		len = htons(skb->len);
		memcpy(skb_push(skb, 2), &len, 2);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	}

#ifdef NOT_YET
	if (wds && ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		    IEEE80211_FCTL_TODS) && skb->len >= ETH_HLEN + ETH_ALEN) {
		/* Non-standard frame: get addr4 from its bogus location after
		 * the payload */
		skb_copy_to_linear_data_offset(skb, ETH_ALEN,
					       skb->data + skb->len - ETH_ALEN,
					       ETH_ALEN);
		skb_trim(skb, skb->len - ETH_ALEN);
	}
#endif

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;

#ifdef NOT_YET
	if (ieee->iw_mode == IW_MODE_MASTER && !wds && ieee->ap->bridge_packets) {
		if (is_multicast_ether_addr(dst)) {
			/* copy multicast frame both to the higher layers and
			 * to the wireless media */
			ieee->ap->bridged_multicast++;
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2 == NULL)
				printk(KERN_DEBUG "%s: skb_clone failed for "
				       "multicast frame\n", dev->name);
		} else if (hostap_is_sta_assoc(ieee->ap, dst)) {
			/* send frame directly to the associated STA using
			 * wireless media and not passing to higher layers */
			ieee->ap->bridged_unicast++;
			skb2 = skb;
			skb = NULL;
		}
	}

	if (skb2 != NULL) {
		/* send to wireless media */
		skb2->dev = dev;
		skb2->protocol = htons(ETH_P_802_3);
		skb_reset_mac_header(skb2);
		skb_reset_network_header(skb2);
		/* skb2->network_header += ETH_HLEN; */
		dev_queue_xmit(skb2);
	}
#endif

	if (skb) {
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		skb->ip_summed = CHECKSUM_NONE;	/* 802.11 crc not sufficient */
		if (netif_rx(skb) == NET_RX_DROP) {
			/* netif_rx always succeeds, but it might drop
			 * the packet.  If it drops the packet, we log that
			 * in our stats. */
			LIBIPW_DEBUG_DROP
			    ("RX: netif_rx dropped the packet\n");
			dev->stats.rx_dropped++;
		}
	}

      rx_exit:
#ifdef NOT_YET
	if (sta)
		hostap_handle_sta_release(sta);
#endif
	return 1;

      rx_dropped:
	dev->stats.rx_dropped++;

	/* Returning 0 indicates to caller that we have not handled the SKB--
	 * so it is still allocated and can be used again by underlying
	 * hardware as a DMA target */
	return 0;
}

/* Filter out unrelated packets, call libipw_rx[_mgt]
 * This function takes over the skb, it should not be used again after calling
 * this function. */
void libipw_rx_any(struct libipw_device *ieee,
		     struct sk_buff *skb, struct libipw_rx_stats *stats)
{
	struct libipw_hdr_4addr *hdr;
	int is_packet_for_us;
	u16 fc;

	if (ieee->iw_mode == IW_MODE_MONITOR) {
		if (!libipw_rx(ieee, skb, stats))
			dev_kfree_skb_irq(skb);
		return;
	}

	if (skb->len < sizeof(struct ieee80211_hdr))
		goto drop_free;

	hdr = (struct libipw_hdr_4addr *)skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);

	if ((fc & IEEE80211_FCTL_VERS) != 0)
		goto drop_free;

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_MGMT:
		if (skb->len < sizeof(struct libipw_hdr_3addr))
			goto drop_free;
		libipw_rx_mgt(ieee, hdr, stats);
		dev_kfree_skb_irq(skb);
		return;
	case IEEE80211_FTYPE_DATA:
		break;
	case IEEE80211_FTYPE_CTL:
		return;
	default:
		return;
	}

	is_packet_for_us = 0;
	switch (ieee->iw_mode) {
	case IW_MODE_ADHOC:
		/* our BSS and not from/to DS */
		if (ether_addr_equal(hdr->addr3, ieee->bssid) &&
		    ((fc & (IEEE80211_FCTL_TODS + IEEE80211_FCTL_FROMDS)) == 0)) {
			/* promisc: get all */
			if (ieee->dev->flags & IFF_PROMISC)
				is_packet_for_us = 1;
			/* to us */
			else if (ether_addr_equal(hdr->addr1, ieee->dev->dev_addr))
				is_packet_for_us = 1;
			/* mcast */
			else if (is_multicast_ether_addr(hdr->addr1))
				is_packet_for_us = 1;
		}
		break;
	case IW_MODE_INFRA:
		/* our BSS (== from our AP) and from DS */
		if (ether_addr_equal(hdr->addr2, ieee->bssid) &&
		    ((fc & (IEEE80211_FCTL_TODS + IEEE80211_FCTL_FROMDS)) == IEEE80211_FCTL_FROMDS)) {
			/* promisc: get all */
			if (ieee->dev->flags & IFF_PROMISC)
				is_packet_for_us = 1;
			/* to us */
			else if (ether_addr_equal(hdr->addr1, ieee->dev->dev_addr))
				is_packet_for_us = 1;
			/* mcast */
			else if (is_multicast_ether_addr(hdr->addr1)) {
				/* not our own packet bcasted from AP */
				if (!ether_addr_equal(hdr->addr3, ieee->dev->dev_addr))
					is_packet_for_us = 1;
			}
		}
		break;
	default:
		/* ? */
		break;
	}

	if (is_packet_for_us)
		if (!libipw_rx(ieee, skb, stats))
			dev_kfree_skb_irq(skb);
	return;

drop_free:
	dev_kfree_skb_irq(skb);
	ieee->dev->stats.rx_dropped++;
}

#define MGMT_FRAME_FIXED_PART_LENGTH		0x24

static u8 qos_oui[QOS_OUI_LEN] = { 0x00, 0x50, 0xF2 };

/*
* Make the structure we read from the beacon packet to have
* the right values
*/
static int libipw_verify_qos_info(struct libipw_qos_information_element
				     *info_element, int sub_type)
{
	if (info_element->elementID != QOS_ELEMENT_ID)
		return -1;
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
static int libipw_read_qos_param_element(
			struct libipw_qos_parameter_info *element_param,
			struct libipw_info_element *info_element)
{
	size_t size = sizeof(*element_param);

	if (!element_param || !info_element || info_element->len != size - 2)
		return -1;

	memcpy(element_param, info_element, size);
	return libipw_verify_qos_info(&element_param->info_element,
				      QOS_OUI_PARAM_SUB_TYPE);
}

/*
 * Parse a QoS information element
 */
static int libipw_read_qos_info_element(
			struct libipw_qos_information_element *element_info,
			struct libipw_info_element *info_element)
{
	size_t size = sizeof(struct libipw_qos_information_element) - 2;

	if (!element_info || !info_element || info_element->len != size - 2)
		return -1;

	memcpy(element_info, info_element, size);
	return libipw_verify_qos_info(element_info, QOS_OUI_INFO_SUB_TYPE);
}

/*
 * Write QoS parameters from the ac parameters.
 */
static void libipw_qos_convert_ac_to_parameters(struct
						  libipw_qos_parameter_info
						  *param_elm, struct
						  libipw_qos_parameters
						  *qos_param)
{
	int i;
	struct libipw_qos_ac_parameter *ac_params;
	u32 txop;
	u8 cw_min;
	u8 cw_max;

	for (i = 0; i < QOS_QUEUE_NUM; i++) {
		ac_params = &(param_elm->ac_params_record[i]);

		qos_param->aifs[i] = (ac_params->aci_aifsn) & 0x0F;
		qos_param->aifs[i] -= (qos_param->aifs[i] < 2) ? 0 : 2;

		cw_min = ac_params->ecw_min_max & 0x0F;
		qos_param->cw_min[i] = cpu_to_le16((1 << cw_min) - 1);

		cw_max = (ac_params->ecw_min_max & 0xF0) >> 4;
		qos_param->cw_max[i] = cpu_to_le16((1 << cw_max) - 1);

		qos_param->flag[i] =
		    (ac_params->aci_aifsn & 0x10) ? 0x01 : 0x00;

		txop = le16_to_cpu(ac_params->tx_op_limit) * 32;
		qos_param->tx_op_limit[i] = cpu_to_le16(txop);
	}
}

/*
 * we have a generic data element which it may contain QoS information or
 * parameters element. check the information element length to decide
 * which type to read
 */
static int libipw_parse_qos_info_param_IE(struct libipw_info_element
					     *info_element,
					     struct libipw_network *network)
{
	int rc = 0;
	struct libipw_qos_parameters *qos_param = NULL;
	struct libipw_qos_information_element qos_info_element;

	rc = libipw_read_qos_info_element(&qos_info_element, info_element);

	if (rc == 0) {
		network->qos_data.param_count = qos_info_element.ac_info & 0x0F;
		network->flags |= NETWORK_HAS_QOS_INFORMATION;
	} else {
		struct libipw_qos_parameter_info param_element;

		rc = libipw_read_qos_param_element(&param_element,
						      info_element);
		if (rc == 0) {
			qos_param = &(network->qos_data.parameters);
			libipw_qos_convert_ac_to_parameters(&param_element,
							       qos_param);
			network->flags |= NETWORK_HAS_QOS_PARAMETERS;
			network->qos_data.param_count =
			    param_element.info_element.ac_info & 0x0F;
		}
	}

	if (rc == 0) {
		LIBIPW_DEBUG_QOS("QoS is supported\n");
		network->qos_data.supported = 1;
	}
	return rc;
}

#ifdef CONFIG_LIBIPW_DEBUG
#define MFIE_STRING(x) case WLAN_EID_ ##x: return #x

static const char *get_info_element_string(u16 id)
{
	switch (id) {
		MFIE_STRING(SSID);
		MFIE_STRING(SUPP_RATES);
		MFIE_STRING(FH_PARAMS);
		MFIE_STRING(DS_PARAMS);
		MFIE_STRING(CF_PARAMS);
		MFIE_STRING(TIM);
		MFIE_STRING(IBSS_PARAMS);
		MFIE_STRING(COUNTRY);
		MFIE_STRING(REQUEST);
		MFIE_STRING(CHALLENGE);
		MFIE_STRING(PWR_CONSTRAINT);
		MFIE_STRING(PWR_CAPABILITY);
		MFIE_STRING(TPC_REQUEST);
		MFIE_STRING(TPC_REPORT);
		MFIE_STRING(SUPPORTED_CHANNELS);
		MFIE_STRING(CHANNEL_SWITCH);
		MFIE_STRING(MEASURE_REQUEST);
		MFIE_STRING(MEASURE_REPORT);
		MFIE_STRING(QUIET);
		MFIE_STRING(IBSS_DFS);
		MFIE_STRING(ERP_INFO);
		MFIE_STRING(RSN);
		MFIE_STRING(EXT_SUPP_RATES);
		MFIE_STRING(VENDOR_SPECIFIC);
		MFIE_STRING(QOS_PARAMETER);
	default:
		return "UNKNOWN";
	}
}
#endif

static int libipw_parse_info_param(struct libipw_info_element
				      *info_element, u16 length,
				      struct libipw_network *network)
{
	u8 i;
#ifdef CONFIG_LIBIPW_DEBUG
	char rates_str[64];
	char *p;
#endif

	while (length >= sizeof(*info_element)) {
		if (sizeof(*info_element) + info_element->len > length) {
			LIBIPW_DEBUG_MGMT("Info elem: parse failed: "
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
		case WLAN_EID_SSID:
			network->ssid_len = min(info_element->len,
						(u8) IW_ESSID_MAX_SIZE);
			memcpy(network->ssid, info_element->data,
			       network->ssid_len);
			if (network->ssid_len < IW_ESSID_MAX_SIZE)
				memset(network->ssid + network->ssid_len, 0,
				       IW_ESSID_MAX_SIZE - network->ssid_len);

			LIBIPW_DEBUG_MGMT("WLAN_EID_SSID: '%*pE' len=%d.\n",
					  network->ssid_len, network->ssid,
					  network->ssid_len);
			break;

		case WLAN_EID_SUPP_RATES:
#ifdef CONFIG_LIBIPW_DEBUG
			p = rates_str;
#endif
			network->rates_len = min(info_element->len,
						 MAX_RATES_LENGTH);
			for (i = 0; i < network->rates_len; i++) {
				network->rates[i] = info_element->data[i];
#ifdef CONFIG_LIBIPW_DEBUG
				p += scnprintf(p, sizeof(rates_str) -
					      (p - rates_str), "%02X ",
					      network->rates[i]);
#endif
				if (libipw_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    LIBIPW_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			LIBIPW_DEBUG_MGMT("WLAN_EID_SUPP_RATES: '%s' (%d)\n",
					     rates_str, network->rates_len);
			break;

		case WLAN_EID_EXT_SUPP_RATES:
#ifdef CONFIG_LIBIPW_DEBUG
			p = rates_str;
#endif
			network->rates_ex_len = min(info_element->len,
						    MAX_RATES_EX_LENGTH);
			for (i = 0; i < network->rates_ex_len; i++) {
				network->rates_ex[i] = info_element->data[i];
#ifdef CONFIG_LIBIPW_DEBUG
				p += scnprintf(p, sizeof(rates_str) -
					      (p - rates_str), "%02X ",
					      network->rates_ex[i]);
#endif
				if (libipw_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    LIBIPW_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			LIBIPW_DEBUG_MGMT("WLAN_EID_EXT_SUPP_RATES: '%s' (%d)\n",
					     rates_str, network->rates_ex_len);
			break;

		case WLAN_EID_DS_PARAMS:
			LIBIPW_DEBUG_MGMT("WLAN_EID_DS_PARAMS: %d\n",
					     info_element->data[0]);
			network->channel = info_element->data[0];
			break;

		case WLAN_EID_FH_PARAMS:
			LIBIPW_DEBUG_MGMT("WLAN_EID_FH_PARAMS: ignored\n");
			break;

		case WLAN_EID_CF_PARAMS:
			LIBIPW_DEBUG_MGMT("WLAN_EID_CF_PARAMS: ignored\n");
			break;

		case WLAN_EID_TIM:
			network->tim.tim_count = info_element->data[0];
			network->tim.tim_period = info_element->data[1];
			LIBIPW_DEBUG_MGMT("WLAN_EID_TIM: partially ignored\n");
			break;

		case WLAN_EID_ERP_INFO:
			network->erp_value = info_element->data[0];
			network->flags |= NETWORK_HAS_ERP_VALUE;
			LIBIPW_DEBUG_MGMT("MFIE_TYPE_ERP_SET: %d\n",
					     network->erp_value);
			break;

		case WLAN_EID_IBSS_PARAMS:
			network->atim_window = info_element->data[0];
			LIBIPW_DEBUG_MGMT("WLAN_EID_IBSS_PARAMS: %d\n",
					     network->atim_window);
			break;

		case WLAN_EID_CHALLENGE:
			LIBIPW_DEBUG_MGMT("WLAN_EID_CHALLENGE: ignored\n");
			break;

		case WLAN_EID_VENDOR_SPECIFIC:
			LIBIPW_DEBUG_MGMT("WLAN_EID_VENDOR_SPECIFIC: %d bytes\n",
					     info_element->len);
			if (!libipw_parse_qos_info_param_IE(info_element,
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
			}
			break;

		case WLAN_EID_RSN:
			LIBIPW_DEBUG_MGMT("WLAN_EID_RSN: %d bytes\n",
					     info_element->len);
			network->rsn_ie_len = min(info_element->len + 2,
						  MAX_WPA_IE_LEN);
			memcpy(network->rsn_ie, info_element,
			       network->rsn_ie_len);
			break;

		case WLAN_EID_QOS_PARAMETER:
			printk(KERN_ERR
			       "QoS Error need to parse QOS_PARAMETER IE\n");
			break;
			/* 802.11h */
		case WLAN_EID_PWR_CONSTRAINT:
			network->power_constraint = info_element->data[0];
			network->flags |= NETWORK_HAS_POWER_CONSTRAINT;
			break;

		case WLAN_EID_CHANNEL_SWITCH:
			network->power_constraint = info_element->data[0];
			network->flags |= NETWORK_HAS_CSA;
			break;

		case WLAN_EID_QUIET:
			network->quiet.count = info_element->data[0];
			network->quiet.period = info_element->data[1];
			network->quiet.duration = info_element->data[2];
			network->quiet.offset = info_element->data[3];
			network->flags |= NETWORK_HAS_QUIET;
			break;

		case WLAN_EID_IBSS_DFS:
			network->flags |= NETWORK_HAS_IBSS_DFS;
			break;

		case WLAN_EID_TPC_REPORT:
			network->tpc_report.transmit_power =
			    info_element->data[0];
			network->tpc_report.link_margin = info_element->data[1];
			network->flags |= NETWORK_HAS_TPC_REPORT;
			break;

		default:
			LIBIPW_DEBUG_MGMT
			    ("Unsupported info element: %s (%d)\n",
			     get_info_element_string(info_element->id),
			     info_element->id);
			break;
		}

		length -= sizeof(*info_element) + info_element->len;
		info_element =
		    (struct libipw_info_element *)&info_element->
		    data[info_element->len];
	}

	return 0;
}

static int libipw_handle_assoc_resp(struct libipw_device *ieee, struct libipw_assoc_response
				       *frame, struct libipw_rx_stats *stats)
{
	struct libipw_network network_resp = { };
	struct libipw_network *network = &network_resp;
	struct net_device *dev = ieee->dev;

	network->flags = 0;
	network->qos_data.active = 0;
	network->qos_data.supported = 0;
	network->qos_data.param_count = 0;
	network->qos_data.old_param_count = 0;

	//network->atim_window = le16_to_cpu(frame->aid) & (0x3FFF);
	network->atim_window = le16_to_cpu(frame->aid);
	network->listen_interval = le16_to_cpu(frame->status);
	memcpy(network->bssid, frame->header.addr3, ETH_ALEN);
	network->capability = le16_to_cpu(frame->capability);
	network->last_scanned = jiffies;
	network->rates_len = network->rates_ex_len = 0;
	network->last_associate = 0;
	network->ssid_len = 0;
	network->erp_value =
	    (network->capability & WLAN_CAPABILITY_IBSS) ? 0x3 : 0x0;

	if (stats->freq == LIBIPW_52GHZ_BAND) {
		/* for A band (No DS info) */
		network->channel = stats->received_channel;
	} else
		network->flags |= NETWORK_HAS_CCK;

	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;

	if (libipw_parse_info_param((void *)frame->variable,
				    stats->len - sizeof(*frame), network))
		return 1;

	network->mode = 0;
	if (stats->freq == LIBIPW_52GHZ_BAND)
		network->mode = IEEE_A;
	else {
		if (network->flags & NETWORK_HAS_OFDM)
			network->mode |= IEEE_G;
		if (network->flags & NETWORK_HAS_CCK)
			network->mode |= IEEE_B;
	}

	memcpy(&network->stats, stats, sizeof(network->stats));

	if (ieee->handle_assoc_response != NULL)
		ieee->handle_assoc_response(dev, frame, network);

	return 0;
}

/***************************************************/

static int libipw_network_init(struct libipw_device *ieee, struct libipw_probe_response
					 *beacon,
					 struct libipw_network *network,
					 struct libipw_rx_stats *stats)
{
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
	/* Where to pull this? beacon->listen_interval; */
	network->listen_interval = 0x0A;
	network->rates_len = network->rates_ex_len = 0;
	network->last_associate = 0;
	network->ssid_len = 0;
	network->flags = 0;
	network->atim_window = 0;
	network->erp_value = (network->capability & WLAN_CAPABILITY_IBSS) ?
	    0x3 : 0x0;

	if (stats->freq == LIBIPW_52GHZ_BAND) {
		/* for A band (No DS info) */
		network->channel = stats->received_channel;
	} else
		network->flags |= NETWORK_HAS_CCK;

	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;

	if (libipw_parse_info_param((void *)beacon->variable,
				    stats->len - sizeof(*beacon), network))
		return 1;

	network->mode = 0;
	if (stats->freq == LIBIPW_52GHZ_BAND)
		network->mode = IEEE_A;
	else {
		if (network->flags & NETWORK_HAS_OFDM)
			network->mode |= IEEE_G;
		if (network->flags & NETWORK_HAS_CCK)
			network->mode |= IEEE_B;
	}

	if (network->mode == 0) {
		LIBIPW_DEBUG_SCAN("Filtered out '%*pE (%pM)' network.\n",
				  network->ssid_len, network->ssid,
				  network->bssid);
		return 1;
	}

	memcpy(&network->stats, stats, sizeof(network->stats));

	return 0;
}

static inline int is_same_network(struct libipw_network *src,
				  struct libipw_network *dst)
{
	/* A network is only a duplicate if the channel, BSSID, and ESSID
	 * all match.  We treat all <hidden> with the same BSSID and channel
	 * as one network */
	return ((src->ssid_len == dst->ssid_len) &&
		(src->channel == dst->channel) &&
		ether_addr_equal_64bits(src->bssid, dst->bssid) &&
		!memcmp(src->ssid, dst->ssid, src->ssid_len));
}

static void update_network(struct libipw_network *dst,
				  struct libipw_network *src)
{
	int qos_active;
	u8 old_param;

	/* We only update the statistics if they were created by receiving
	 * the network information on the actual channel the network is on.
	 *
	 * This keeps beacons received on neighbor channels from bringing
	 * down the signal level of an AP. */
	if (dst->channel == src->stats.received_channel)
		memcpy(&dst->stats, &src->stats,
		       sizeof(struct libipw_rx_stats));
	else
		LIBIPW_DEBUG_SCAN("Network %pM info received "
			"off channel (%d vs. %d)\n", src->bssid,
			dst->channel, src->stats.received_channel);

	dst->capability = src->capability;
	memcpy(dst->rates, src->rates, src->rates_len);
	dst->rates_len = src->rates_len;
	memcpy(dst->rates_ex, src->rates_ex, src->rates_ex_len);
	dst->rates_ex_len = src->rates_ex_len;

	dst->mode = src->mode;
	dst->flags = src->flags;
	dst->time_stamp[0] = src->time_stamp[0];
	dst->time_stamp[1] = src->time_stamp[1];

	dst->beacon_interval = src->beacon_interval;
	dst->listen_interval = src->listen_interval;
	dst->atim_window = src->atim_window;
	dst->erp_value = src->erp_value;
	dst->tim = src->tim;

	memcpy(dst->wpa_ie, src->wpa_ie, src->wpa_ie_len);
	dst->wpa_ie_len = src->wpa_ie_len;
	memcpy(dst->rsn_ie, src->rsn_ie, src->rsn_ie_len);
	dst->rsn_ie_len = src->rsn_ie_len;

	dst->last_scanned = jiffies;
	qos_active = src->qos_data.active;
	old_param = dst->qos_data.old_param_count;
	if (dst->flags & NETWORK_HAS_QOS_MASK)
		memcpy(&dst->qos_data, &src->qos_data,
		       sizeof(struct libipw_qos_data));
	else {
		dst->qos_data.supported = src->qos_data.supported;
		dst->qos_data.param_count = src->qos_data.param_count;
	}

	if (dst->qos_data.supported == 1) {
		if (dst->ssid_len)
			LIBIPW_DEBUG_QOS
			    ("QoS the network %s is QoS supported\n",
			     dst->ssid);
		else
			LIBIPW_DEBUG_QOS
			    ("QoS the network is QoS supported\n");
	}
	dst->qos_data.active = qos_active;
	dst->qos_data.old_param_count = old_param;

	/* dst->last_associate is not overwritten */
}

static inline int is_beacon(__le16 fc)
{
	return (WLAN_FC_GET_STYPE(le16_to_cpu(fc)) == IEEE80211_STYPE_BEACON);
}

static void libipw_process_probe_response(struct libipw_device
						    *ieee, struct
						    libipw_probe_response
						    *beacon, struct libipw_rx_stats
						    *stats)
{
	struct net_device *dev = ieee->dev;
	struct libipw_network network = { };
	struct libipw_network *target;
	struct libipw_network *oldest = NULL;
#ifdef CONFIG_LIBIPW_DEBUG
	struct libipw_info_element *info_element = (void *)beacon->variable;
#endif
	unsigned long flags;

	LIBIPW_DEBUG_SCAN("'%*pE' (%pM): %c%c%c%c %c%c%c%c-%c%c%c%c %c%c%c%c\n",
		     info_element->len, info_element->data,
		     beacon->header.addr3,
		     (beacon->capability & cpu_to_le16(1 << 0xf)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0xe)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0xd)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0xc)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0xb)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0xa)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x9)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x8)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x7)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x6)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x5)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x4)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x3)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x2)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x1)) ? '1' : '0',
		     (beacon->capability & cpu_to_le16(1 << 0x0)) ? '1' : '0');

	if (libipw_network_init(ieee, beacon, &network, stats)) {
		LIBIPW_DEBUG_SCAN("Dropped '%*pE' (%pM) via %s.\n",
				  info_element->len, info_element->data,
				  beacon->header.addr3,
				  is_beacon(beacon->header.frame_ctl) ?
				  "BEACON" : "PROBE RESPONSE");
		return;
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

	list_for_each_entry(target, &ieee->network_list, list) {
		if (is_same_network(target, &network))
			break;

		if ((oldest == NULL) ||
		    time_before(target->last_scanned, oldest->last_scanned))
			oldest = target;
	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (&target->list == &ieee->network_list) {
		if (list_empty(&ieee->network_free_list)) {
			/* If there are no more slots, expire the oldest */
			list_del(&oldest->list);
			target = oldest;
			LIBIPW_DEBUG_SCAN("Expired '%*pE' (%pM) from network list.\n",
					  target->ssid_len, target->ssid,
					  target->bssid);
		} else {
			/* Otherwise just pull from the free list */
			target = list_entry(ieee->network_free_list.next,
					    struct libipw_network, list);
			list_del(ieee->network_free_list.next);
		}

#ifdef CONFIG_LIBIPW_DEBUG
		LIBIPW_DEBUG_SCAN("Adding '%*pE' (%pM) via %s.\n",
				  network.ssid_len, network.ssid,
				  network.bssid,
				  is_beacon(beacon->header.frame_ctl) ?
				  "BEACON" : "PROBE RESPONSE");
#endif
		memcpy(target, &network, sizeof(*target));
		list_add_tail(&target->list, &ieee->network_list);
	} else {
		LIBIPW_DEBUG_SCAN("Updating '%*pE' (%pM) via %s.\n",
				  target->ssid_len, target->ssid,
				  target->bssid,
				  is_beacon(beacon->header.frame_ctl) ?
				  "BEACON" : "PROBE RESPONSE");
		update_network(target, &network);
	}

	spin_unlock_irqrestore(&ieee->lock, flags);

	if (is_beacon(beacon->header.frame_ctl)) {
		if (ieee->handle_beacon != NULL)
			ieee->handle_beacon(dev, beacon, target);
	} else {
		if (ieee->handle_probe_response != NULL)
			ieee->handle_probe_response(dev, beacon, target);
	}
}

void libipw_rx_mgt(struct libipw_device *ieee,
		      struct libipw_hdr_4addr *header,
		      struct libipw_rx_stats *stats)
{
	switch (WLAN_FC_GET_STYPE(le16_to_cpu(header->frame_ctl))) {
	case IEEE80211_STYPE_ASSOC_RESP:
		LIBIPW_DEBUG_MGMT("received ASSOCIATION RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));
		libipw_handle_assoc_resp(ieee,
					    (struct libipw_assoc_response *)
					    header, stats);
		break;

	case IEEE80211_STYPE_REASSOC_RESP:
		LIBIPW_DEBUG_MGMT("received REASSOCIATION RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));
		break;

	case IEEE80211_STYPE_PROBE_REQ:
		LIBIPW_DEBUG_MGMT("received auth (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));

		if (ieee->handle_probe_request != NULL)
			ieee->handle_probe_request(ieee->dev,
						   (struct
						    libipw_probe_request *)
						   header, stats);
		break;

	case IEEE80211_STYPE_PROBE_RESP:
		LIBIPW_DEBUG_MGMT("received PROBE RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));
		LIBIPW_DEBUG_SCAN("Probe response\n");
		libipw_process_probe_response(ieee,
						 (struct
						  libipw_probe_response *)
						 header, stats);
		break;

	case IEEE80211_STYPE_BEACON:
		LIBIPW_DEBUG_MGMT("received BEACON (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));
		LIBIPW_DEBUG_SCAN("Beacon\n");
		libipw_process_probe_response(ieee,
						 (struct
						  libipw_probe_response *)
						 header, stats);
		break;
	case IEEE80211_STYPE_AUTH:

		LIBIPW_DEBUG_MGMT("received auth (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));

		if (ieee->handle_auth != NULL)
			ieee->handle_auth(ieee->dev,
					  (struct libipw_auth *)header);
		break;

	case IEEE80211_STYPE_DISASSOC:
		if (ieee->handle_disassoc != NULL)
			ieee->handle_disassoc(ieee->dev,
					      (struct libipw_disassoc *)
					      header);
		break;

	case IEEE80211_STYPE_ACTION:
		LIBIPW_DEBUG_MGMT("ACTION\n");
		if (ieee->handle_action)
			ieee->handle_action(ieee->dev,
					    (struct libipw_action *)
					    header, stats);
		break;

	case IEEE80211_STYPE_REASSOC_REQ:
		LIBIPW_DEBUG_MGMT("received reassoc (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));

		LIBIPW_DEBUG_MGMT("%s: LIBIPW_REASSOC_REQ received\n",
				     ieee->dev->name);
		if (ieee->handle_reassoc_request != NULL)
			ieee->handle_reassoc_request(ieee->dev,
						    (struct libipw_reassoc_request *)
						     header);
		break;

	case IEEE80211_STYPE_ASSOC_REQ:
		LIBIPW_DEBUG_MGMT("received assoc (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));

		LIBIPW_DEBUG_MGMT("%s: LIBIPW_ASSOC_REQ received\n",
				     ieee->dev->name);
		if (ieee->handle_assoc_request != NULL)
			ieee->handle_assoc_request(ieee->dev);
		break;

	case IEEE80211_STYPE_DEAUTH:
		LIBIPW_DEBUG_MGMT("DEAUTH\n");
		if (ieee->handle_deauth != NULL)
			ieee->handle_deauth(ieee->dev,
					    (struct libipw_deauth *)
					    header);
		break;
	default:
		LIBIPW_DEBUG_MGMT("received UNKNOWN (%d)\n",
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));
		LIBIPW_DEBUG_MGMT("%s: Unknown management packet: %d\n",
				     ieee->dev->name,
				     WLAN_FC_GET_STYPE(le16_to_cpu
						       (header->frame_ctl)));
		break;
	}
}

EXPORT_SYMBOL_GPL(libipw_rx_any);
EXPORT_SYMBOL(libipw_rx_mgt);
EXPORT_SYMBOL(libipw_rx);
