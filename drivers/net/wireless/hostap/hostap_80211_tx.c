#include "hostap_80211.h"
#include "hostap_common.h"
#include "hostap_wlan.h"
#include "hostap.h"
#include "hostap_ap.h"

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static unsigned char rfc1042_header[] =
{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static unsigned char bridge_tunnel_header[] =
{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
/* No encapsulation header if EtherType < 0x600 (=length) */

void hostap_dump_tx_80211(const char *name, struct sk_buff *skb)
{
	struct ieee80211_hdr_4addr *hdr;
	u16 fc;
	DECLARE_MAC_BUF(mac);

	hdr = (struct ieee80211_hdr_4addr *) skb->data;

	printk(KERN_DEBUG "%s: TX len=%d jiffies=%ld\n",
	       name, skb->len, jiffies);

	if (skb->len < 2)
		return;

	fc = le16_to_cpu(hdr->frame_ctl);
	printk(KERN_DEBUG "   FC=0x%04x (type=%d:%d)%s%s",
	       fc, WLAN_FC_GET_TYPE(fc) >> 2, WLAN_FC_GET_STYPE(fc) >> 4,
	       fc & IEEE80211_FCTL_TODS ? " [ToDS]" : "",
	       fc & IEEE80211_FCTL_FROMDS ? " [FromDS]" : "");

	if (skb->len < IEEE80211_DATA_HDR3_LEN) {
		printk("\n");
		return;
	}

	printk(" dur=0x%04x seq=0x%04x\n", le16_to_cpu(hdr->duration_id),
	       le16_to_cpu(hdr->seq_ctl));

	printk(KERN_DEBUG "   A1=%s", print_mac(mac, hdr->addr1));
	printk(" A2=%s", print_mac(mac, hdr->addr2));
	printk(" A3=%s", print_mac(mac, hdr->addr3));
	if (skb->len >= 30)
		printk(" A4=%s", print_mac(mac, hdr->addr4));
	printk("\n");
}


/* hard_start_xmit function for data interfaces (wlan#, wlan#wds#, wlan#sta)
 * Convert Ethernet header into a suitable IEEE 802.11 header depending on
 * device configuration. */
int hostap_data_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int need_headroom, need_tailroom = 0;
	struct ieee80211_hdr_4addr hdr;
	u16 fc, ethertype = 0;
	enum {
		WDS_NO = 0, WDS_OWN_FRAME, WDS_COMPLIANT_FRAME
	} use_wds = WDS_NO;
	u8 *encaps_data;
	int hdr_len, encaps_len, skip_header_bytes;
	int to_assoc_ap = 0;
	struct hostap_skb_tx_data *meta;

	iface = netdev_priv(dev);
	local = iface->local;

	if (skb->len < ETH_HLEN) {
		printk(KERN_DEBUG "%s: hostap_data_start_xmit: short skb "
		       "(len=%d)\n", dev->name, skb->len);
		kfree_skb(skb);
		return 0;
	}

	if (local->ddev != dev) {
		use_wds = (local->iw_mode == IW_MODE_MASTER &&
			   !(local->wds_type & HOSTAP_WDS_STANDARD_FRAME)) ?
			WDS_OWN_FRAME : WDS_COMPLIANT_FRAME;
		if (dev == local->stadev) {
			to_assoc_ap = 1;
			use_wds = WDS_NO;
		} else if (dev == local->apdev) {
			printk(KERN_DEBUG "%s: prism2_tx: trying to use "
			       "AP device with Ethernet net dev\n", dev->name);
			kfree_skb(skb);
			return 0;
		}
	} else {
		if (local->iw_mode == IW_MODE_REPEAT) {
			printk(KERN_DEBUG "%s: prism2_tx: trying to use "
			       "non-WDS link in Repeater mode\n", dev->name);
			kfree_skb(skb);
			return 0;
		} else if (local->iw_mode == IW_MODE_INFRA &&
			   (local->wds_type & HOSTAP_WDS_AP_CLIENT) &&
			   memcmp(skb->data + ETH_ALEN, dev->dev_addr,
				  ETH_ALEN) != 0) {
			/* AP client mode: send frames with foreign src addr
			 * using 4-addr WDS frames */
			use_wds = WDS_COMPLIANT_FRAME;
		}
	}

	/* Incoming skb->data: dst_addr[6], src_addr[6], proto[2], payload
	 * ==>
	 * Prism2 TX frame with 802.11 header:
	 * txdesc (address order depending on used mode; includes dst_addr and
	 * src_addr), possible encapsulation (RFC1042/Bridge-Tunnel;
	 * proto[2], payload {, possible addr4[6]} */

	ethertype = (skb->data[12] << 8) | skb->data[13];

	memset(&hdr, 0, sizeof(hdr));

	/* Length of data after IEEE 802.11 header */
	encaps_data = NULL;
	encaps_len = 0;
	skip_header_bytes = ETH_HLEN;
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		skip_header_bytes -= 2;
	} else if (ethertype >= 0x600) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		skip_header_bytes -= 2;
	}

	fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA;
	hdr_len = IEEE80211_DATA_HDR3_LEN;

	if (use_wds != WDS_NO) {
		/* Note! Prism2 station firmware has problems with sending real
		 * 802.11 frames with four addresses; until these problems can
		 * be fixed or worked around, 4-addr frames needed for WDS are
		 * using incompatible format: FromDS flag is not set and the
		 * fourth address is added after the frame payload; it is
		 * assumed, that the receiving station knows how to handle this
		 * frame format */

		if (use_wds == WDS_COMPLIANT_FRAME) {
			fc |= IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS;
			/* From&To DS: Addr1 = RA, Addr2 = TA, Addr3 = DA,
			 * Addr4 = SA */
			skb_copy_from_linear_data_offset(skb, ETH_ALEN,
							 &hdr.addr4, ETH_ALEN);
			hdr_len += ETH_ALEN;
		} else {
			/* bogus 4-addr format to workaround Prism2 station
			 * f/w bug */
			fc |= IEEE80211_FCTL_TODS;
			/* From DS: Addr1 = DA (used as RA),
			 * Addr2 = BSSID (used as TA), Addr3 = SA (used as DA),
			 */

			/* SA from skb->data + ETH_ALEN will be added after
			 * frame payload; use hdr.addr4 as a temporary buffer
			 */
			skb_copy_from_linear_data_offset(skb, ETH_ALEN,
							 &hdr.addr4, ETH_ALEN);
			need_tailroom += ETH_ALEN;
		}

		/* send broadcast and multicast frames to broadcast RA, if
		 * configured; otherwise, use unicast RA of the WDS link */
		if ((local->wds_type & HOSTAP_WDS_BROADCAST_RA) &&
		    skb->data[0] & 0x01)
			memset(&hdr.addr1, 0xff, ETH_ALEN);
		else if (iface->type == HOSTAP_INTERFACE_WDS)
			memcpy(&hdr.addr1, iface->u.wds.remote_addr,
			       ETH_ALEN);
		else
			memcpy(&hdr.addr1, local->bssid, ETH_ALEN);
		memcpy(&hdr.addr2, dev->dev_addr, ETH_ALEN);
		skb_copy_from_linear_data(skb, &hdr.addr3, ETH_ALEN);
	} else if (local->iw_mode == IW_MODE_MASTER && !to_assoc_ap) {
		fc |= IEEE80211_FCTL_FROMDS;
		/* From DS: Addr1 = DA, Addr2 = BSSID, Addr3 = SA */
		skb_copy_from_linear_data(skb, &hdr.addr1, ETH_ALEN);
		memcpy(&hdr.addr2, dev->dev_addr, ETH_ALEN);
		skb_copy_from_linear_data_offset(skb, ETH_ALEN, &hdr.addr3,
						 ETH_ALEN);
	} else if (local->iw_mode == IW_MODE_INFRA || to_assoc_ap) {
		fc |= IEEE80211_FCTL_TODS;
		/* To DS: Addr1 = BSSID, Addr2 = SA, Addr3 = DA */
		memcpy(&hdr.addr1, to_assoc_ap ?
		       local->assoc_ap_addr : local->bssid, ETH_ALEN);
		skb_copy_from_linear_data_offset(skb, ETH_ALEN, &hdr.addr2,
						 ETH_ALEN);
		skb_copy_from_linear_data(skb, &hdr.addr3, ETH_ALEN);
	} else if (local->iw_mode == IW_MODE_ADHOC) {
		/* not From/To DS: Addr1 = DA, Addr2 = SA, Addr3 = BSSID */
		skb_copy_from_linear_data(skb, &hdr.addr1, ETH_ALEN);
		skb_copy_from_linear_data_offset(skb, ETH_ALEN, &hdr.addr2,
						 ETH_ALEN);
		memcpy(&hdr.addr3, local->bssid, ETH_ALEN);
	}

	hdr.frame_ctl = cpu_to_le16(fc);

	skb_pull(skb, skip_header_bytes);
	need_headroom = local->func->need_tx_headroom + hdr_len + encaps_len;
	if (skb_tailroom(skb) < need_tailroom) {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (skb == NULL) {
			iface->stats.tx_dropped++;
			return 0;
		}
		if (pskb_expand_head(skb, need_headroom, need_tailroom,
				     GFP_ATOMIC)) {
			kfree_skb(skb);
			iface->stats.tx_dropped++;
			return 0;
		}
	} else if (skb_headroom(skb) < need_headroom) {
		struct sk_buff *tmp = skb;
		skb = skb_realloc_headroom(skb, need_headroom);
		kfree_skb(tmp);
		if (skb == NULL) {
			iface->stats.tx_dropped++;
			return 0;
		}
	} else {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (skb == NULL) {
			iface->stats.tx_dropped++;
			return 0;
		}
	}

	if (encaps_data)
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);
	memcpy(skb_push(skb, hdr_len), &hdr, hdr_len);
	if (use_wds == WDS_OWN_FRAME) {
		memcpy(skb_put(skb, ETH_ALEN), &hdr.addr4, ETH_ALEN);
	}

	iface->stats.tx_packets++;
	iface->stats.tx_bytes += skb->len;

	skb_reset_mac_header(skb);
	meta = (struct hostap_skb_tx_data *) skb->cb;
	memset(meta, 0, sizeof(*meta));
	meta->magic = HOSTAP_SKB_TX_DATA_MAGIC;
	if (use_wds)
		meta->flags |= HOSTAP_TX_FLAGS_WDS;
	meta->ethertype = ethertype;
	meta->iface = iface;

	/* Send IEEE 802.11 encapsulated frame using the master radio device */
	skb->dev = local->dev;
	dev_queue_xmit(skb);
	return 0;
}


/* hard_start_xmit function for hostapd wlan#ap interfaces */
int hostap_mgmt_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct hostap_skb_tx_data *meta;
	struct ieee80211_hdr_4addr *hdr;
	u16 fc;

	iface = netdev_priv(dev);
	local = iface->local;

	if (skb->len < 10) {
		printk(KERN_DEBUG "%s: hostap_mgmt_start_xmit: short skb "
		       "(len=%d)\n", dev->name, skb->len);
		kfree_skb(skb);
		return 0;
	}

	iface->stats.tx_packets++;
	iface->stats.tx_bytes += skb->len;

	meta = (struct hostap_skb_tx_data *) skb->cb;
	memset(meta, 0, sizeof(*meta));
	meta->magic = HOSTAP_SKB_TX_DATA_MAGIC;
	meta->iface = iface;

	if (skb->len >= IEEE80211_DATA_HDR3_LEN + sizeof(rfc1042_header) + 2) {
		hdr = (struct ieee80211_hdr_4addr *) skb->data;
		fc = le16_to_cpu(hdr->frame_ctl);
		if (WLAN_FC_GET_TYPE(fc) == IEEE80211_FTYPE_DATA &&
		    WLAN_FC_GET_STYPE(fc) == IEEE80211_STYPE_DATA) {
			u8 *pos = &skb->data[IEEE80211_DATA_HDR3_LEN +
					     sizeof(rfc1042_header)];
			meta->ethertype = (pos[0] << 8) | pos[1];
		}
	}

	/* Send IEEE 802.11 encapsulated frame using the master radio device */
	skb->dev = local->dev;
	dev_queue_xmit(skb);
	return 0;
}


/* Called only from software IRQ */
static struct sk_buff * hostap_tx_encrypt(struct sk_buff *skb,
					  struct ieee80211_crypt_data *crypt)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct ieee80211_hdr_4addr *hdr;
	u16 fc;
	int prefix_len, postfix_len, hdr_len, res;
	DECLARE_MAC_BUF(mac);

	iface = netdev_priv(skb->dev);
	local = iface->local;

	if (skb->len < IEEE80211_DATA_HDR3_LEN) {
		kfree_skb(skb);
		return NULL;
	}

	if (local->tkip_countermeasures &&
	    strcmp(crypt->ops->name, "TKIP") == 0) {
		hdr = (struct ieee80211_hdr_4addr *) skb->data;
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: TKIP countermeasures: dropped "
			       "TX packet to %s\n",
			       local->dev->name, print_mac(mac, hdr->addr1));
		}
		kfree_skb(skb);
		return NULL;
	}

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	prefix_len = crypt->ops->extra_mpdu_prefix_len +
		crypt->ops->extra_msdu_prefix_len;
	postfix_len = crypt->ops->extra_mpdu_postfix_len +
		crypt->ops->extra_msdu_postfix_len;
	if ((skb_headroom(skb) < prefix_len ||
	     skb_tailroom(skb) < postfix_len) &&
	    pskb_expand_head(skb, prefix_len, postfix_len, GFP_ATOMIC)) {
		kfree_skb(skb);
		return NULL;
	}

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
 	fc = le16_to_cpu(hdr->frame_ctl);
	hdr_len = hostap_80211_get_hdrlen(fc);

	/* Host-based IEEE 802.11 fragmentation for TX is not yet supported, so
	 * call both MSDU and MPDU encryption functions from here. */
	atomic_inc(&crypt->refcnt);
	res = 0;
	if (crypt->ops->encrypt_msdu)
		res = crypt->ops->encrypt_msdu(skb, hdr_len, crypt->priv);
	if (res == 0 && crypt->ops->encrypt_mpdu)
		res = crypt->ops->encrypt_mpdu(skb, hdr_len, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		kfree_skb(skb);
		return NULL;
	}

	return skb;
}


/* hard_start_xmit function for master radio interface wifi#.
 * AP processing (TX rate control, power save buffering, etc.).
 * Use hardware TX function to send the frame. */
int hostap_master_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 1;
	u16 fc;
	struct hostap_tx_data tx;
	ap_tx_ret tx_ret;
	struct hostap_skb_tx_data *meta;
	int no_encrypt = 0;
	struct ieee80211_hdr_4addr *hdr;

	iface = netdev_priv(dev);
	local = iface->local;

	tx.skb = skb;
	tx.sta_ptr = NULL;

	meta = (struct hostap_skb_tx_data *) skb->cb;
	if (meta->magic != HOSTAP_SKB_TX_DATA_MAGIC) {
		printk(KERN_DEBUG "%s: invalid skb->cb magic (0x%08x, "
		       "expected 0x%08x)\n",
		       dev->name, meta->magic, HOSTAP_SKB_TX_DATA_MAGIC);
		ret = 0;
		iface->stats.tx_dropped++;
		goto fail;
	}

	if (local->host_encrypt) {
		/* Set crypt to default algorithm and key; will be replaced in
		 * AP code if STA has own alg/key */
		tx.crypt = local->crypt[local->tx_keyidx];
		tx.host_encrypt = 1;
	} else {
		tx.crypt = NULL;
		tx.host_encrypt = 0;
	}

	if (skb->len < 24) {
		printk(KERN_DEBUG "%s: hostap_master_start_xmit: short skb "
		       "(len=%d)\n", dev->name, skb->len);
		ret = 0;
		iface->stats.tx_dropped++;
		goto fail;
	}

	/* FIX (?):
	 * Wi-Fi 802.11b test plan suggests that AP should ignore power save
	 * bit in authentication and (re)association frames and assume tha
	 * STA remains awake for the response. */
	tx_ret = hostap_handle_sta_tx(local, &tx);
	skb = tx.skb;
	meta = (struct hostap_skb_tx_data *) skb->cb;
	hdr = (struct ieee80211_hdr_4addr *) skb->data;
 	fc = le16_to_cpu(hdr->frame_ctl);
	switch (tx_ret) {
	case AP_TX_CONTINUE:
		break;
	case AP_TX_CONTINUE_NOT_AUTHORIZED:
		if (local->ieee_802_1x &&
		    WLAN_FC_GET_TYPE(fc) == IEEE80211_FTYPE_DATA &&
		    meta->ethertype != ETH_P_PAE &&
		    !(meta->flags & HOSTAP_TX_FLAGS_WDS)) {
			printk(KERN_DEBUG "%s: dropped frame to unauthorized "
			       "port (IEEE 802.1X): ethertype=0x%04x\n",
			       dev->name, meta->ethertype);
			hostap_dump_tx_80211(dev->name, skb);

			ret = 0; /* drop packet */
			iface->stats.tx_dropped++;
			goto fail;
		}
		break;
	case AP_TX_DROP:
		ret = 0; /* drop packet */
		iface->stats.tx_dropped++;
		goto fail;
	case AP_TX_RETRY:
		goto fail;
	case AP_TX_BUFFERED:
		/* do not free skb here, it will be freed when the
		 * buffered frame is sent/timed out */
		ret = 0;
		goto tx_exit;
	}

	/* Request TX callback if protocol version is 2 in 802.11 header;
	 * this version 2 is a special case used between hostapd and kernel
	 * driver */
	if (((fc & IEEE80211_FCTL_VERS) == BIT(1)) &&
	    local->ap && local->ap->tx_callback_idx && meta->tx_cb_idx == 0) {
		meta->tx_cb_idx = local->ap->tx_callback_idx;

		/* remove special version from the frame header */
		fc &= ~IEEE80211_FCTL_VERS;
		hdr->frame_ctl = cpu_to_le16(fc);
	}

	if (WLAN_FC_GET_TYPE(fc) != IEEE80211_FTYPE_DATA) {
		no_encrypt = 1;
		tx.crypt = NULL;
	}

	if (local->ieee_802_1x && meta->ethertype == ETH_P_PAE && tx.crypt &&
	    !(fc & IEEE80211_FCTL_PROTECTED)) {
		no_encrypt = 1;
		PDEBUG(DEBUG_EXTRA2, "%s: TX: IEEE 802.1X - passing "
		       "unencrypted EAPOL frame\n", dev->name);
		tx.crypt = NULL; /* no encryption for IEEE 802.1X frames */
	}

	if (tx.crypt && (!tx.crypt->ops || !tx.crypt->ops->encrypt_mpdu))
		tx.crypt = NULL;
	else if ((tx.crypt || local->crypt[local->tx_keyidx]) && !no_encrypt) {
		/* Add ISWEP flag both for firmware and host based encryption
		 */
		fc |= IEEE80211_FCTL_PROTECTED;
		hdr->frame_ctl = cpu_to_le16(fc);
	} else if (local->drop_unencrypted &&
		   WLAN_FC_GET_TYPE(fc) == IEEE80211_FTYPE_DATA &&
		   meta->ethertype != ETH_P_PAE) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: dropped unencrypted TX data "
			       "frame (drop_unencrypted=1)\n", dev->name);
		}
		iface->stats.tx_dropped++;
		ret = 0;
		goto fail;
	}

	if (tx.crypt) {
		skb = hostap_tx_encrypt(skb, tx.crypt);
		if (skb == NULL) {
			printk(KERN_DEBUG "%s: TX - encryption failed\n",
			       dev->name);
			ret = 0;
			goto fail;
		}
		meta = (struct hostap_skb_tx_data *) skb->cb;
		if (meta->magic != HOSTAP_SKB_TX_DATA_MAGIC) {
			printk(KERN_DEBUG "%s: invalid skb->cb magic (0x%08x, "
			       "expected 0x%08x) after hostap_tx_encrypt\n",
			       dev->name, meta->magic,
			       HOSTAP_SKB_TX_DATA_MAGIC);
			ret = 0;
			iface->stats.tx_dropped++;
			goto fail;
		}
	}

	if (local->func->tx == NULL || local->func->tx(skb, dev)) {
		ret = 0;
		iface->stats.tx_dropped++;
	} else {
		ret = 0;
		iface->stats.tx_packets++;
		iface->stats.tx_bytes += skb->len;
	}

 fail:
	if (!ret && skb)
		dev_kfree_skb(skb);
 tx_exit:
	if (tx.sta_ptr)
		hostap_handle_sta_release(tx.sta_ptr);
	return ret;
}


EXPORT_SYMBOL(hostap_master_start_xmit);
