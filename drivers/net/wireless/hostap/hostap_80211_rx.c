#include <linux/etherdevice.h>
#include <net/ieee80211_crypt.h>

#include "hostap_80211.h"
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

void hostap_dump_rx_80211(const char *name, struct sk_buff *skb,
			  struct hostap_80211_rx_status *rx_stats)
{
	struct ieee80211_hdr_4addr *hdr;
	u16 fc;
	DECLARE_MAC_BUF(mac);

	hdr = (struct ieee80211_hdr_4addr *) skb->data;

	printk(KERN_DEBUG "%s: RX signal=%d noise=%d rate=%d len=%d "
	       "jiffies=%ld\n",
	       name, rx_stats->signal, rx_stats->noise, rx_stats->rate,
	       skb->len, jiffies);

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


/* Send RX frame to netif with 802.11 (and possible prism) header.
 * Called from hardware or software IRQ context. */
int prism2_rx_80211(struct net_device *dev, struct sk_buff *skb,
		    struct hostap_80211_rx_status *rx_stats, int type)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int hdrlen, phdrlen, head_need, tail_need;
	u16 fc;
	int prism_header, ret;
	struct ieee80211_hdr_4addr *hdr;

	iface = netdev_priv(dev);
	local = iface->local;
	dev->last_rx = jiffies;

	if (dev->type == ARPHRD_IEEE80211_PRISM) {
		if (local->monitor_type == PRISM2_MONITOR_PRISM) {
			prism_header = 1;
			phdrlen = sizeof(struct linux_wlan_ng_prism_hdr);
		} else { /* local->monitor_type == PRISM2_MONITOR_CAPHDR */
			prism_header = 2;
			phdrlen = sizeof(struct linux_wlan_ng_cap_hdr);
		}
	} else {
		prism_header = 0;
		phdrlen = 0;
	}

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);

	if (type == PRISM2_RX_MGMT && (fc & IEEE80211_FCTL_VERS)) {
		printk(KERN_DEBUG "%s: dropped management frame with header "
		       "version %d\n", dev->name, fc & IEEE80211_FCTL_VERS);
		dev_kfree_skb_any(skb);
		return 0;
	}

	hdrlen = hostap_80211_get_hdrlen(fc);

	/* check if there is enough room for extra data; if not, expand skb
	 * buffer to be large enough for the changes */
	head_need = phdrlen;
	tail_need = 0;
#ifdef PRISM2_ADD_BOGUS_CRC
	tail_need += 4;
#endif /* PRISM2_ADD_BOGUS_CRC */

	head_need -= skb_headroom(skb);
	tail_need -= skb_tailroom(skb);

	if (head_need > 0 || tail_need > 0) {
		if (pskb_expand_head(skb, head_need > 0 ? head_need : 0,
				     tail_need > 0 ? tail_need : 0,
				     GFP_ATOMIC)) {
			printk(KERN_DEBUG "%s: prism2_rx_80211 failed to "
			       "reallocate skb buffer\n", dev->name);
			dev_kfree_skb_any(skb);
			return 0;
		}
	}

	/* We now have an skb with enough head and tail room, so just insert
	 * the extra data */

#ifdef PRISM2_ADD_BOGUS_CRC
	memset(skb_put(skb, 4), 0xff, 4); /* Prism2 strips CRC */
#endif /* PRISM2_ADD_BOGUS_CRC */

	if (prism_header == 1) {
		struct linux_wlan_ng_prism_hdr *hdr;
		hdr = (struct linux_wlan_ng_prism_hdr *)
			skb_push(skb, phdrlen);
		memset(hdr, 0, phdrlen);
		hdr->msgcode = LWNG_CAP_DID_BASE;
		hdr->msglen = sizeof(*hdr);
		memcpy(hdr->devname, dev->name, sizeof(hdr->devname));
#define LWNG_SETVAL(f,i,s,l,d) \
hdr->f.did = LWNG_CAP_DID_BASE | (i << 12); \
hdr->f.status = s; hdr->f.len = l; hdr->f.data = d
		LWNG_SETVAL(hosttime, 1, 0, 4, jiffies);
		LWNG_SETVAL(mactime, 2, 0, 4, rx_stats->mac_time);
		LWNG_SETVAL(channel, 3, 1 /* no value */, 4, 0);
		LWNG_SETVAL(rssi, 4, 1 /* no value */, 4, 0);
		LWNG_SETVAL(sq, 5, 1 /* no value */, 4, 0);
		LWNG_SETVAL(signal, 6, 0, 4, rx_stats->signal);
		LWNG_SETVAL(noise, 7, 0, 4, rx_stats->noise);
		LWNG_SETVAL(rate, 8, 0, 4, rx_stats->rate / 5);
		LWNG_SETVAL(istx, 9, 0, 4, 0);
		LWNG_SETVAL(frmlen, 10, 0, 4, skb->len - phdrlen);
#undef LWNG_SETVAL
	} else if (prism_header == 2) {
		struct linux_wlan_ng_cap_hdr *hdr;
		hdr = (struct linux_wlan_ng_cap_hdr *)
			skb_push(skb, phdrlen);
		memset(hdr, 0, phdrlen);
		hdr->version    = htonl(LWNG_CAPHDR_VERSION);
		hdr->length     = htonl(phdrlen);
		hdr->mactime    = __cpu_to_be64(rx_stats->mac_time);
		hdr->hosttime   = __cpu_to_be64(jiffies);
		hdr->phytype    = htonl(4); /* dss_dot11_b */
		hdr->channel    = htonl(local->channel);
		hdr->datarate   = htonl(rx_stats->rate);
		hdr->antenna    = htonl(0); /* unknown */
		hdr->priority   = htonl(0); /* unknown */
		hdr->ssi_type   = htonl(3); /* raw */
		hdr->ssi_signal = htonl(rx_stats->signal);
		hdr->ssi_noise  = htonl(rx_stats->noise);
		hdr->preamble   = htonl(0); /* unknown */
		hdr->encoding   = htonl(1); /* cck */
	}

	ret = skb->len - phdrlen;
	skb->dev = dev;
	skb_reset_mac_header(skb);
	skb_pull(skb, hdrlen);
	if (prism_header)
		skb_pull(skb, phdrlen);
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = __constant_htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);

	return ret;
}


/* Called only as a tasklet (software IRQ) */
static void monitor_rx(struct net_device *dev, struct sk_buff *skb,
		       struct hostap_80211_rx_status *rx_stats)
{
	struct net_device_stats *stats;
	int len;

	len = prism2_rx_80211(dev, skb, rx_stats, PRISM2_RX_MONITOR);
	stats = hostap_get_stats(dev);
	stats->rx_packets++;
	stats->rx_bytes += len;
}


/* Called only as a tasklet (software IRQ) */
static struct prism2_frag_entry *
prism2_frag_cache_find(local_info_t *local, unsigned int seq,
		       unsigned int frag, u8 *src, u8 *dst)
{
	struct prism2_frag_entry *entry;
	int i;

	for (i = 0; i < PRISM2_FRAG_CACHE_LEN; i++) {
		entry = &local->frag_cache[i];
		if (entry->skb != NULL &&
		    time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			printk(KERN_DEBUG "%s: expiring fragment cache entry "
			       "seq=%u last_frag=%u\n",
			       local->dev->name, entry->seq, entry->last_frag);
			dev_kfree_skb(entry->skb);
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
prism2_frag_cache_get(local_info_t *local, struct ieee80211_hdr_4addr *hdr)
{
	struct sk_buff *skb = NULL;
	u16 sc;
	unsigned int frag, seq;
	struct prism2_frag_entry *entry;

	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	seq = WLAN_GET_SEQ_SEQ(sc) >> 4;

	if (frag == 0) {
		/* Reserve enough space to fit maximum frame length */
		skb = dev_alloc_skb(local->dev->mtu +
				    sizeof(struct ieee80211_hdr_4addr) +
				    8 /* LLC */ +
				    2 /* alignment */ +
				    8 /* WEP */ + ETH_ALEN /* WDS */);
		if (skb == NULL)
			return NULL;

		entry = &local->frag_cache[local->frag_next_idx];
		local->frag_next_idx++;
		if (local->frag_next_idx >= PRISM2_FRAG_CACHE_LEN)
			local->frag_next_idx = 0;

		if (entry->skb != NULL)
			dev_kfree_skb(entry->skb);

		entry->first_frag_time = jiffies;
		entry->seq = seq;
		entry->last_frag = frag;
		entry->skb = skb;
		memcpy(entry->src_addr, hdr->addr2, ETH_ALEN);
		memcpy(entry->dst_addr, hdr->addr1, ETH_ALEN);
	} else {
		/* received a fragment of a frame for which the head fragment
		 * should have already been received */
		entry = prism2_frag_cache_find(local, seq, frag, hdr->addr2,
					       hdr->addr1);
		if (entry != NULL) {
			entry->last_frag = frag;
			skb = entry->skb;
		}
	}

	return skb;
}


/* Called only as a tasklet (software IRQ) */
static int prism2_frag_cache_invalidate(local_info_t *local,
					struct ieee80211_hdr_4addr *hdr)
{
	u16 sc;
	unsigned int seq;
	struct prism2_frag_entry *entry;

	sc = le16_to_cpu(hdr->seq_ctl);
	seq = WLAN_GET_SEQ_SEQ(sc) >> 4;

	entry = prism2_frag_cache_find(local, seq, -1, hdr->addr2, hdr->addr1);

	if (entry == NULL) {
		printk(KERN_DEBUG "%s: could not invalidate fragment cache "
		       "entry (seq=%u)\n",
		       local->dev->name, seq);
		return -1;
	}

	entry->skb = NULL;
	return 0;
}


static struct hostap_bss_info *__hostap_get_bss(local_info_t *local, u8 *bssid,
						u8 *ssid, size_t ssid_len)
{
	struct list_head *ptr;
	struct hostap_bss_info *bss;

	list_for_each(ptr, &local->bss_list) {
		bss = list_entry(ptr, struct hostap_bss_info, list);
		if (memcmp(bss->bssid, bssid, ETH_ALEN) == 0 &&
		    (ssid == NULL ||
		     (ssid_len == bss->ssid_len &&
		      memcmp(ssid, bss->ssid, ssid_len) == 0))) {
			list_move(&bss->list, &local->bss_list);
			return bss;
		}
	}

	return NULL;
}


static struct hostap_bss_info *__hostap_add_bss(local_info_t *local, u8 *bssid,
						u8 *ssid, size_t ssid_len)
{
	struct hostap_bss_info *bss;

	if (local->num_bss_info >= HOSTAP_MAX_BSS_COUNT) {
		bss = list_entry(local->bss_list.prev,
				 struct hostap_bss_info, list);
		list_del(&bss->list);
		local->num_bss_info--;
	} else {
		bss = (struct hostap_bss_info *)
			kmalloc(sizeof(*bss), GFP_ATOMIC);
		if (bss == NULL)
			return NULL;
	}

	memset(bss, 0, sizeof(*bss));
	memcpy(bss->bssid, bssid, ETH_ALEN);
	memcpy(bss->ssid, ssid, ssid_len);
	bss->ssid_len = ssid_len;
	local->num_bss_info++;
	list_add(&bss->list, &local->bss_list);
	return bss;
}


static void __hostap_expire_bss(local_info_t *local)
{
	struct hostap_bss_info *bss;

	while (local->num_bss_info > 0) {
		bss = list_entry(local->bss_list.prev,
				 struct hostap_bss_info, list);
		if (!time_after(jiffies, bss->last_update + 60 * HZ))
			break;

		list_del(&bss->list);
		local->num_bss_info--;
		kfree(bss);
	}
}


/* Both IEEE 802.11 Beacon and Probe Response frames have similar structure, so
 * the same routine can be used to parse both of them. */
static void hostap_rx_sta_beacon(local_info_t *local, struct sk_buff *skb,
				 int stype)
{
	struct hostap_ieee80211_mgmt *mgmt;
	int left, chan = 0;
	u8 *pos;
	u8 *ssid = NULL, *wpa = NULL, *rsn = NULL;
	size_t ssid_len = 0, wpa_len = 0, rsn_len = 0;
	struct hostap_bss_info *bss;

	if (skb->len < IEEE80211_MGMT_HDR_LEN + sizeof(mgmt->u.beacon))
		return;

	mgmt = (struct hostap_ieee80211_mgmt *) skb->data;
	pos = mgmt->u.beacon.variable;
	left = skb->len - (pos - skb->data);

	while (left >= 2) {
		if (2 + pos[1] > left)
			return; /* parse failed */
		switch (*pos) {
		case WLAN_EID_SSID:
			ssid = pos + 2;
			ssid_len = pos[1];
			break;
		case WLAN_EID_GENERIC:
			if (pos[1] >= 4 &&
			    pos[2] == 0x00 && pos[3] == 0x50 &&
			    pos[4] == 0xf2 && pos[5] == 1) {
				wpa = pos;
				wpa_len = pos[1] + 2;
			}
			break;
		case WLAN_EID_RSN:
			rsn = pos;
			rsn_len = pos[1] + 2;
			break;
		case WLAN_EID_DS_PARAMS:
			if (pos[1] >= 1)
				chan = pos[2];
			break;
		}
		left -= 2 + pos[1];
		pos += 2 + pos[1];
	}

	if (wpa_len > MAX_WPA_IE_LEN)
		wpa_len = MAX_WPA_IE_LEN;
	if (rsn_len > MAX_WPA_IE_LEN)
		rsn_len = MAX_WPA_IE_LEN;
	if (ssid_len > sizeof(bss->ssid))
		ssid_len = sizeof(bss->ssid);

	spin_lock(&local->lock);
	bss = __hostap_get_bss(local, mgmt->bssid, ssid, ssid_len);
	if (bss == NULL)
		bss = __hostap_add_bss(local, mgmt->bssid, ssid, ssid_len);
	if (bss) {
		bss->last_update = jiffies;
		bss->count++;
		bss->capab_info = le16_to_cpu(mgmt->u.beacon.capab_info);
		if (wpa) {
			memcpy(bss->wpa_ie, wpa, wpa_len);
			bss->wpa_ie_len = wpa_len;
		} else
			bss->wpa_ie_len = 0;
		if (rsn) {
			memcpy(bss->rsn_ie, rsn, rsn_len);
			bss->rsn_ie_len = rsn_len;
		} else
			bss->rsn_ie_len = 0;
		bss->chan = chan;
	}
	__hostap_expire_bss(local);
	spin_unlock(&local->lock);
}


static int
hostap_rx_frame_mgmt(local_info_t *local, struct sk_buff *skb,
		     struct hostap_80211_rx_status *rx_stats, u16 type,
		     u16 stype)
{
	if (local->iw_mode == IW_MODE_MASTER) {
		hostap_update_sta_ps(local, (struct ieee80211_hdr_4addr *)
				     skb->data);
	}

	if (local->hostapd && type == IEEE80211_FTYPE_MGMT) {
		if (stype == IEEE80211_STYPE_BEACON &&
		    local->iw_mode == IW_MODE_MASTER) {
			struct sk_buff *skb2;
			/* Process beacon frames also in kernel driver to
			 * update STA(AP) table statistics */
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2)
				hostap_rx(skb2->dev, skb2, rx_stats);
		}

		/* send management frames to the user space daemon for
		 * processing */
		local->apdevstats.rx_packets++;
		local->apdevstats.rx_bytes += skb->len;
		if (local->apdev == NULL)
			return -1;
		prism2_rx_80211(local->apdev, skb, rx_stats, PRISM2_RX_MGMT);
		return 0;
	}

	if (local->iw_mode == IW_MODE_MASTER) {
		if (type != IEEE80211_FTYPE_MGMT &&
		    type != IEEE80211_FTYPE_CTL) {
			printk(KERN_DEBUG "%s: unknown management frame "
			       "(type=0x%02x, stype=0x%02x) dropped\n",
			       skb->dev->name, type >> 2, stype >> 4);
			return -1;
		}

		hostap_rx(skb->dev, skb, rx_stats);
		return 0;
	} else if (type == IEEE80211_FTYPE_MGMT &&
		   (stype == IEEE80211_STYPE_BEACON ||
		    stype == IEEE80211_STYPE_PROBE_RESP)) {
		hostap_rx_sta_beacon(local, skb, stype);
		return -1;
	} else if (type == IEEE80211_FTYPE_MGMT &&
		   (stype == IEEE80211_STYPE_ASSOC_RESP ||
		    stype == IEEE80211_STYPE_REASSOC_RESP)) {
		/* Ignore (Re)AssocResp silently since these are not currently
		 * needed but are still received when WPA/RSN mode is enabled.
		 */
		return -1;
	} else {
		printk(KERN_DEBUG "%s: hostap_rx_frame_mgmt: dropped unhandled"
		       " management frame in non-Host AP mode (type=%d:%d)\n",
		       skb->dev->name, type >> 2, stype >> 4);
		return -1;
	}
}


/* Called only as a tasklet (software IRQ) */
static struct net_device *prism2_rx_get_wds(local_info_t *local,
						   u8 *addr)
{
	struct hostap_interface *iface = NULL;
	struct list_head *ptr;

	read_lock_bh(&local->iface_lock);
	list_for_each(ptr, &local->hostap_interfaces) {
		iface = list_entry(ptr, struct hostap_interface, list);
		if (iface->type == HOSTAP_INTERFACE_WDS &&
		    memcmp(iface->u.wds.remote_addr, addr, ETH_ALEN) == 0)
			break;
		iface = NULL;
	}
	read_unlock_bh(&local->iface_lock);

	return iface ? iface->dev : NULL;
}


static int
hostap_rx_frame_wds(local_info_t *local, struct ieee80211_hdr_4addr *hdr,
		    u16 fc, struct net_device **wds)
{
	DECLARE_MAC_BUF(mac);
	/* FIX: is this really supposed to accept WDS frames only in Master
	 * mode? What about Repeater or Managed with WDS frames? */
	if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) !=
	    (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS) &&
	    (local->iw_mode != IW_MODE_MASTER || !(fc & IEEE80211_FCTL_TODS)))
		return 0; /* not a WDS frame */

	/* Possible WDS frame: either IEEE 802.11 compliant (if FromDS)
	 * or own non-standard frame with 4th address after payload */
	if (memcmp(hdr->addr1, local->dev->dev_addr, ETH_ALEN) != 0 &&
	    (hdr->addr1[0] != 0xff || hdr->addr1[1] != 0xff ||
	     hdr->addr1[2] != 0xff || hdr->addr1[3] != 0xff ||
	     hdr->addr1[4] != 0xff || hdr->addr1[5] != 0xff)) {
		/* RA (or BSSID) is not ours - drop */
		PDEBUG(DEBUG_EXTRA, "%s: received WDS frame with "
		       "not own or broadcast %s=%s\n",
		       local->dev->name,
		       fc & IEEE80211_FCTL_FROMDS ? "RA" : "BSSID",
		       print_mac(mac, hdr->addr1));
		return -1;
	}

	/* check if the frame came from a registered WDS connection */
	*wds = prism2_rx_get_wds(local, hdr->addr2);
	if (*wds == NULL && fc & IEEE80211_FCTL_FROMDS &&
	    (local->iw_mode != IW_MODE_INFRA ||
	     !(local->wds_type & HOSTAP_WDS_AP_CLIENT) ||
	     memcmp(hdr->addr2, local->bssid, ETH_ALEN) != 0)) {
		/* require that WDS link has been registered with TA or the
		 * frame is from current AP when using 'AP client mode' */
		PDEBUG(DEBUG_EXTRA, "%s: received WDS[4 addr] frame "
		       "from unknown TA=%s\n",
		       local->dev->name, print_mac(mac, hdr->addr2));
		if (local->ap && local->ap->autom_ap_wds)
			hostap_wds_link_oper(local, hdr->addr2, WDS_ADD);
		return -1;
	}

	if (*wds && !(fc & IEEE80211_FCTL_FROMDS) && local->ap &&
	    hostap_is_sta_assoc(local->ap, hdr->addr2)) {
		/* STA is actually associated with us even though it has a
		 * registered WDS link. Assume it is in 'AP client' mode.
		 * Since this is a 3-addr frame, assume it is not (bogus) WDS
		 * frame and process it like any normal ToDS frame from
		 * associated STA. */
		*wds = NULL;
	}

	return 0;
}


static int hostap_is_eapol_frame(local_info_t *local, struct sk_buff *skb)
{
	struct net_device *dev = local->dev;
	u16 fc, ethertype;
	struct ieee80211_hdr_4addr *hdr;
	u8 *pos;

	if (skb->len < 24)
		return 0;

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
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
	pos = skb->data + 24;
	ethertype = (pos[6] << 8) | pos[7];
	if (ethertype == ETH_P_PAE)
		return 1;

	return 0;
}


/* Called only as a tasklet (software IRQ) */
static int
hostap_rx_frame_decrypt(local_info_t *local, struct sk_buff *skb,
			struct ieee80211_crypt_data *crypt)
{
	struct ieee80211_hdr_4addr *hdr;
	int res, hdrlen;
	DECLARE_MAC_BUF(mac);

	if (crypt == NULL || crypt->ops->decrypt_mpdu == NULL)
		return 0;

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
	hdrlen = hostap_80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	if (local->tkip_countermeasures &&
	    strcmp(crypt->ops->name, "TKIP") == 0) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: TKIP countermeasures: dropped "
			       "received packet from %s\n",
			       local->dev->name, print_mac(mac, hdr->addr2));
		}
		return -1;
	}

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_mpdu(skb, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_DEBUG "%s: decryption failed (SA=%s"
		       ") res=%d\n",
		       local->dev->name, print_mac(mac, hdr->addr2), res);
		local->comm_tallies.rx_discards_wep_undecryptable++;
		return -1;
	}

	return res;
}


/* Called only as a tasklet (software IRQ) */
static int
hostap_rx_frame_decrypt_msdu(local_info_t *local, struct sk_buff *skb,
			     int keyidx, struct ieee80211_crypt_data *crypt)
{
	struct ieee80211_hdr_4addr *hdr;
	int res, hdrlen;
	DECLARE_MAC_BUF(mac);

	if (crypt == NULL || crypt->ops->decrypt_msdu == NULL)
		return 0;

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
	hdrlen = hostap_80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_msdu(skb, keyidx, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_DEBUG "%s: MSDU decryption/MIC verification failed"
		       " (SA=%s keyidx=%d)\n",
		       local->dev->name, print_mac(mac, hdr->addr2), keyidx);
		return -1;
	}

	return 0;
}


/* All received frames are sent to this function. @skb contains the frame in
 * IEEE 802.11 format, i.e., in the format it was sent over air.
 * This function is called only as a tasklet (software IRQ). */
void hostap_80211_rx(struct net_device *dev, struct sk_buff *skb,
		     struct hostap_80211_rx_status *rx_stats)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct ieee80211_hdr_4addr *hdr;
	size_t hdrlen;
	u16 fc, type, stype, sc;
	struct net_device *wds = NULL;
	struct net_device_stats *stats;
	unsigned int frag;
	u8 *payload;
	struct sk_buff *skb2 = NULL;
	u16 ethertype;
	int frame_authorized = 0;
	int from_assoc_ap = 0;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	struct ieee80211_crypt_data *crypt = NULL;
	void *sta = NULL;
	int keyidx = 0;
	DECLARE_MAC_BUF(mac);

	iface = netdev_priv(dev);
	local = iface->local;
	iface->stats.rx_packets++;
	iface->stats.rx_bytes += skb->len;

	/* dev is the master radio device; change this to be the default
	 * virtual interface (this may be changed to WDS device below) */
	dev = local->ddev;
	iface = netdev_priv(dev);

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
	stats = hostap_get_stats(dev);

	if (skb->len < 10)
		goto rx_dropped;

	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	hdrlen = hostap_80211_get_hdrlen(fc);

	/* Put this code here so that we avoid duplicating it in all
	 * Rx paths. - Jean II */
#ifdef IW_WIRELESS_SPY		/* defined in iw_handler.h */
	/* If spy monitoring on */
	if (iface->spy_data.spy_number > 0) {
		struct iw_quality wstats;
		wstats.level = rx_stats->signal;
		wstats.noise = rx_stats->noise;
		wstats.updated = IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_UPDATED
			| IW_QUAL_QUAL_INVALID | IW_QUAL_DBM;
		/* Update spy records */
		wireless_spy_update(dev, hdr->addr2, &wstats);
	}
#endif /* IW_WIRELESS_SPY */
	hostap_update_rx_stats(local->ap, hdr, rx_stats);

	if (local->iw_mode == IW_MODE_MONITOR) {
		monitor_rx(dev, skb, rx_stats);
		return;
	}

	if (local->host_decrypt) {
		int idx = 0;
		if (skb->len >= hdrlen + 3)
			idx = skb->data[hdrlen + 3] >> 6;
		crypt = local->crypt[idx];
		sta = NULL;

		/* Use station specific key to override default keys if the
		 * receiver address is a unicast address ("individual RA"). If
		 * bcrx_sta_key parameter is set, station specific key is used
		 * even with broad/multicast targets (this is against IEEE
		 * 802.11, but makes it easier to use different keys with
		 * stations that do not support WEP key mapping). */

		if (!(hdr->addr1[0] & 0x01) || local->bcrx_sta_key)
			(void) hostap_handle_sta_crypto(local, hdr, &crypt,
							&sta);

		/* allow NULL decrypt to indicate an station specific override
		 * for default encryption */
		if (crypt && (crypt->ops == NULL ||
			      crypt->ops->decrypt_mpdu == NULL))
			crypt = NULL;

		if (!crypt && (fc & IEEE80211_FCTL_PROTECTED)) {
#if 0
			/* This seems to be triggered by some (multicast?)
			 * frames from other than current BSS, so just drop the
			 * frames silently instead of filling system log with
			 * these reports. */
			printk(KERN_DEBUG "%s: WEP decryption failed (not set)"
			       " (SA=%s)\n",
			       local->dev->name, print_mac(mac, hdr->addr2));
#endif
			local->comm_tallies.rx_discards_wep_undecryptable++;
			goto rx_dropped;
		}
	}

	if (type != IEEE80211_FTYPE_DATA) {
		if (type == IEEE80211_FTYPE_MGMT &&
		    stype == IEEE80211_STYPE_AUTH &&
		    fc & IEEE80211_FCTL_PROTECTED && local->host_decrypt &&
		    (keyidx = hostap_rx_frame_decrypt(local, skb, crypt)) < 0)
		{
			printk(KERN_DEBUG "%s: failed to decrypt mgmt::auth "
			       "from %s\n", dev->name,
			       print_mac(mac, hdr->addr2));
			/* TODO: could inform hostapd about this so that it
			 * could send auth failure report */
			goto rx_dropped;
		}

		if (hostap_rx_frame_mgmt(local, skb, rx_stats, type, stype))
			goto rx_dropped;
		else
			goto rx_exit;
	}

	/* Data frame - extract src/dst addresses */
	if (skb->len < IEEE80211_DATA_HDR3_LEN)
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
		if (skb->len < IEEE80211_DATA_HDR4_LEN)
			goto rx_dropped;
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);
		break;
	case 0:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		break;
	}

	if (hostap_rx_frame_wds(local, hdr, fc, &wds))
		goto rx_dropped;
	if (wds) {
		skb->dev = dev = wds;
		stats = hostap_get_stats(dev);
	}

	if (local->iw_mode == IW_MODE_MASTER && !wds &&
	    (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    IEEE80211_FCTL_FROMDS &&
	    local->stadev &&
	    memcmp(hdr->addr2, local->assoc_ap_addr, ETH_ALEN) == 0) {
		/* Frame from BSSID of the AP for which we are a client */
		skb->dev = dev = local->stadev;
		stats = hostap_get_stats(dev);
		from_assoc_ap = 1;
	}

	dev->last_rx = jiffies;

	if ((local->iw_mode == IW_MODE_MASTER ||
	     local->iw_mode == IW_MODE_REPEAT) &&
	    !from_assoc_ap) {
		switch (hostap_handle_sta_rx(local, dev, skb, rx_stats,
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

	/* Nullfunc frames may have PS-bit set, so they must be passed to
	 * hostap_handle_sta_rx() before being dropped here. */
	if (stype != IEEE80211_STYPE_DATA &&
	    stype != IEEE80211_STYPE_DATA_CFACK &&
	    stype != IEEE80211_STYPE_DATA_CFPOLL &&
	    stype != IEEE80211_STYPE_DATA_CFACKPOLL) {
		if (stype != IEEE80211_STYPE_NULLFUNC)
			printk(KERN_DEBUG "%s: RX: dropped data frame "
			       "with no data (type=0x%02x, subtype=0x%02x)\n",
			       dev->name, type >> 2, stype >> 4);
		goto rx_dropped;
	}

	/* skb: hdr + (possibly fragmented, possibly encrypted) payload */

	if (local->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	    (keyidx = hostap_rx_frame_decrypt(local, skb, crypt)) < 0)
		goto rx_dropped;
	hdr = (struct ieee80211_hdr_4addr *) skb->data;

	/* skb: hdr + (possibly fragmented) plaintext payload */

	if (local->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	    (frag != 0 || (fc & IEEE80211_FCTL_MOREFRAGS))) {
		int flen;
		struct sk_buff *frag_skb =
			prism2_frag_cache_get(local, hdr);
		if (!frag_skb) {
			printk(KERN_DEBUG "%s: Rx cannot get skb from "
			       "fragment cache (morefrag=%d seq=%u frag=%u)\n",
			       dev->name, (fc & IEEE80211_FCTL_MOREFRAGS) != 0,
			       WLAN_GET_SEQ_SEQ(sc) >> 4, frag);
			goto rx_dropped;
		}

		flen = skb->len;
		if (frag != 0)
			flen -= hdrlen;

		if (frag_skb->tail + flen > frag_skb->end) {
			printk(KERN_WARNING "%s: host decrypted and "
			       "reassembled frame did not fit skb\n",
			       dev->name);
			prism2_frag_cache_invalidate(local, hdr);
			goto rx_dropped;
		}

		if (frag == 0) {
			/* copy first fragment (including full headers) into
			 * beginning of the fragment cache skb */
			skb_copy_from_linear_data(skb, skb_put(frag_skb, flen),
						  flen);
		} else {
			/* append frame payload to the end of the fragment
			 * cache skb */
			skb_copy_from_linear_data_offset(skb, hdrlen,
							 skb_put(frag_skb,
								 flen), flen);
		}
		dev_kfree_skb(skb);
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
		hdr = (struct ieee80211_hdr_4addr *) skb->data;
		prism2_frag_cache_invalidate(local, hdr);
	}

	/* skb: hdr + (possible reassembled) full MSDU payload; possibly still
	 * encrypted/authenticated */

	if (local->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	    hostap_rx_frame_decrypt_msdu(local, skb, keyidx, crypt))
		goto rx_dropped;

	hdr = (struct ieee80211_hdr_4addr *) skb->data;
	if (crypt && !(fc & IEEE80211_FCTL_PROTECTED) && !local->open_wep) {
		if (local->ieee_802_1x &&
		    hostap_is_eapol_frame(local, skb)) {
			/* pass unencrypted EAPOL frames even if encryption is
			 * configured */
			PDEBUG(DEBUG_EXTRA2, "%s: RX: IEEE 802.1X - passing "
			       "unencrypted EAPOL frame\n", local->dev->name);
		} else {
			printk(KERN_DEBUG "%s: encryption configured, but RX "
			       "frame not encrypted (SA=%s)\n",
			       local->dev->name, print_mac(mac, hdr->addr2));
			goto rx_dropped;
		}
	}

	if (local->drop_unencrypted && !(fc & IEEE80211_FCTL_PROTECTED) &&
	    !hostap_is_eapol_frame(local, skb)) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: dropped unencrypted RX data "
			       "frame from %s"
			       " (drop_unencrypted=1)\n",
			       dev->name, print_mac(mac, hdr->addr2));
		}
		goto rx_dropped;
	}

	/* skb: hdr + (possible reassembled) full plaintext payload */

	payload = skb->data + hdrlen;
	ethertype = (payload[6] << 8) | payload[7];

	/* If IEEE 802.1X is used, check whether the port is authorized to send
	 * the received frame. */
	if (local->ieee_802_1x && local->iw_mode == IW_MODE_MASTER) {
		if (ethertype == ETH_P_PAE) {
			PDEBUG(DEBUG_EXTRA2, "%s: RX: IEEE 802.1X frame\n",
			       dev->name);
			if (local->hostapd && local->apdev) {
				/* Send IEEE 802.1X frames to the user
				 * space daemon for processing */
				prism2_rx_80211(local->apdev, skb, rx_stats,
						PRISM2_RX_MGMT);
				local->apdevstats.rx_packets++;
				local->apdevstats.rx_bytes += skb->len;
				goto rx_exit;
			}
		} else if (!frame_authorized) {
			printk(KERN_DEBUG "%s: dropped frame from "
			       "unauthorized port (IEEE 802.1X): "
			       "ethertype=0x%04x\n",
			       dev->name, ethertype);
			goto rx_dropped;
		}
	}

	/* convert hdr + possible LLC headers into Ethernet header */
	if (skb->len - hdrlen >= 8 &&
	    ((memcmp(payload, rfc1042_header, 6) == 0 &&
	      ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
	     memcmp(payload, bridge_tunnel_header, 6) == 0)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		skb_pull(skb, hdrlen + 6);
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

	if (wds && ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		    IEEE80211_FCTL_TODS) &&
	    skb->len >= ETH_HLEN + ETH_ALEN) {
		/* Non-standard frame: get addr4 from its bogus location after
		 * the payload */
		skb_copy_from_linear_data_offset(skb, skb->len - ETH_ALEN,
						 skb->data + ETH_ALEN,
						 ETH_ALEN);
		skb_trim(skb, skb->len - ETH_ALEN);
	}

	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	if (local->iw_mode == IW_MODE_MASTER && !wds &&
	    local->ap->bridge_packets) {
		if (dst[0] & 0x01) {
			/* copy multicast frame both to the higher layers and
			 * to the wireless media */
			local->ap->bridged_multicast++;
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2 == NULL)
				printk(KERN_DEBUG "%s: skb_clone failed for "
				       "multicast frame\n", dev->name);
		} else if (hostap_is_sta_authorized(local->ap, dst)) {
			/* send frame directly to the associated STA using
			 * wireless media and not passing to higher layers */
			local->ap->bridged_unicast++;
			skb2 = skb;
			skb = NULL;
		}
	}

	if (skb2 != NULL) {
		/* send to wireless media */
		skb2->dev = dev;
		skb2->protocol = __constant_htons(ETH_P_802_3);
		skb_reset_mac_header(skb2);
		skb_reset_network_header(skb2);
		/* skb2->network_header += ETH_HLEN; */
		dev_queue_xmit(skb2);
	}

	if (skb) {
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		netif_rx(skb);
	}

 rx_exit:
	if (sta)
		hostap_handle_sta_release(sta);
	return;

 rx_dropped:
	dev_kfree_skb(skb);

	stats->rx_dropped++;
	goto rx_exit;
}


EXPORT_SYMBOL(hostap_80211_rx);
