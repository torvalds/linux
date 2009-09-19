/******************************************************************************

  Copyright(c) 2003 - 2005 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  Intel Linux Wireless <ilw@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

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
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <asm/uaccess.h>

#include "libipw.h"

/*

802.11 Data Frame

      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  Frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `--------------------------------------------------|         |------'
Total: 28 non-data bytes                                 `----.----'
							      |
       .- 'Frame data' expands, if WEP enabled, to <----------'
       |
       V
      ,-----------------------.
Bytes |  4  |   0-2296  |  4  |
      |-----|-----------|-----|
Desc. | IV  | Encrypted | ICV |
      |     | Packet    |     |
      `-----|           |-----'
	    `-----.-----'
		  |
       .- 'Encrypted Packet' expands to
       |
       V
      ,---------------------------------------------------.
Bytes |  1   |  1   |    1    |    3     |  2   |  0-2304 |
      |------|------|---------|----------|------|---------|
Desc. | SNAP | SNAP | Control |Eth Tunnel| Type | IP      |
      | DSAP | SSAP |         |          |      | Packet  |
      | 0xAA | 0xAA |0x03 (UI)|0x00-00-F8|      |         |
      `----------------------------------------------------
Total: 8 non-data bytes

802.3 Ethernet Data Frame

      ,-----------------------------------------.
Bytes |   6   |   6   |  2   |  Variable |   4  |
      |-------|-------|------|-----------|------|
Desc. | Dest. | Source| Type | IP Packet |  fcs |
      |  MAC  |  MAC  |      |           |      |
      `-----------------------------------------'
Total: 18 non-data bytes

In the event that fragmentation is required, the incoming payload is split into
N parts of size ieee->fts.  The first fragment contains the SNAP header and the
remaining packets are just data.

If encryption is enabled, each fragment payload size is reduced by enough space
to add the prefix and postfix (IV and ICV totalling 8 bytes in the case of WEP)
So if you have 1500 bytes of payload with ieee->fts set to 500 without
encryption it will take 3 frames.  With WEP it will take 4 frames as the
payload of each frame is reduced to 492 bytes.

* SKB visualization
*
*  ,- skb->data
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

static int libipw_copy_snap(u8 * data, __be16 h_proto)
{
	struct libipw_snap_hdr *snap;
	u8 *oui;

	snap = (struct libipw_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;

	if (h_proto == htons(ETH_P_AARP) || h_proto == htons(ETH_P_IPX))
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;
	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];

	memcpy(data + SNAP_SIZE, &h_proto, sizeof(u16));

	return SNAP_SIZE + sizeof(u16);
}

static int libipw_encrypt_fragment(struct libipw_device *ieee,
					     struct sk_buff *frag, int hdr_len)
{
	struct lib80211_crypt_data *crypt =
		ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	int res;

	if (crypt == NULL)
		return -1;

	/* To encrypt, frame format is:
	 * IV (4 bytes), clear payload (including SNAP), ICV (4 bytes) */
	atomic_inc(&crypt->refcnt);
	res = 0;
	if (crypt->ops && crypt->ops->encrypt_mpdu)
		res = crypt->ops->encrypt_mpdu(frag, hdr_len, crypt->priv);

	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_INFO "%s: Encryption failed: len=%d.\n",
		       ieee->dev->name, frag->len);
		ieee->ieee_stats.tx_discards++;
		return -1;
	}

	return 0;
}

void libipw_txb_free(struct libipw_txb *txb)
{
	int i;
	if (unlikely(!txb))
		return;
	for (i = 0; i < txb->nr_frags; i++)
		if (txb->fragments[i])
			dev_kfree_skb_any(txb->fragments[i]);
	kfree(txb);
}

static struct libipw_txb *libipw_alloc_txb(int nr_frags, int txb_size,
						 int headroom, gfp_t gfp_mask)
{
	struct libipw_txb *txb;
	int i;
	txb = kmalloc(sizeof(struct libipw_txb) + (sizeof(u8 *) * nr_frags),
		      gfp_mask);
	if (!txb)
		return NULL;

	memset(txb, 0, sizeof(struct libipw_txb));
	txb->nr_frags = nr_frags;
	txb->frag_size = txb_size;

	for (i = 0; i < nr_frags; i++) {
		txb->fragments[i] = __dev_alloc_skb(txb_size + headroom,
						    gfp_mask);
		if (unlikely(!txb->fragments[i])) {
			i--;
			break;
		}
		skb_reserve(txb->fragments[i], headroom);
	}
	if (unlikely(i != nr_frags)) {
		while (i >= 0)
			dev_kfree_skb_any(txb->fragments[i--]);
		kfree(txb);
		return NULL;
	}
	return txb;
}

static int libipw_classify(struct sk_buff *skb)
{
	struct ethhdr *eth;
	struct iphdr *ip;

	eth = (struct ethhdr *)skb->data;
	if (eth->h_proto != htons(ETH_P_IP))
		return 0;

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

/* Incoming skb is converted to a txb which consists of
 * a block of 802.11 fragment packets (stored as skbs) */
netdev_tx_t libipw_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct libipw_device *ieee = netdev_priv(dev);
	struct libipw_txb *txb = NULL;
	struct libipw_hdr_3addrqos *frag_hdr;
	int i, bytes_per_frag, nr_frags, bytes_last_frag, frag_size,
	    rts_required;
	unsigned long flags;
	int encrypt, host_encrypt, host_encrypt_msdu, host_build_iv;
	__be16 ether_type;
	int bytes, fc, hdr_len;
	struct sk_buff *skb_frag;
	struct libipw_hdr_3addrqos header = {/* Ensure zero initialized */
		.duration_id = 0,
		.seq_ctl = 0,
		.qos_ctl = 0
	};
	u8 dest[ETH_ALEN], src[ETH_ALEN];
	struct lib80211_crypt_data *crypt;
	int priority = skb->priority;
	int snapped = 0;

	if (ieee->is_queue_full && (*ieee->is_queue_full) (dev, priority))
		return NETDEV_TX_BUSY;

	spin_lock_irqsave(&ieee->lock, flags);

	/* If there is no driver handler to take the TXB, dont' bother
	 * creating it... */
	if (!ieee->hard_start_xmit) {
		printk(KERN_WARNING "%s: No xmit handler.\n", ieee->dev->name);
		goto success;
	}

	if (unlikely(skb->len < SNAP_SIZE + sizeof(u16))) {
		printk(KERN_WARNING "%s: skb too small (%d).\n",
		       ieee->dev->name, skb->len);
		goto success;
	}

	ether_type = ((struct ethhdr *)skb->data)->h_proto;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];

	encrypt = !(ether_type == htons(ETH_P_PAE) && ieee->ieee802_1x) &&
	    ieee->sec.encrypt;

	host_encrypt = ieee->host_encrypt && encrypt && crypt;
	host_encrypt_msdu = ieee->host_encrypt_msdu && encrypt && crypt;
	host_build_iv = ieee->host_build_iv && encrypt && crypt;

	if (!encrypt && ieee->ieee802_1x &&
	    ieee->drop_unencrypted && ether_type != htons(ETH_P_PAE)) {
		dev->stats.tx_dropped++;
		goto success;
	}

	/* Save source and destination addresses */
	skb_copy_from_linear_data(skb, dest, ETH_ALEN);
	skb_copy_from_linear_data_offset(skb, ETH_ALEN, src, ETH_ALEN);

	if (host_encrypt || host_build_iv)
		fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA |
		    IEEE80211_FCTL_PROTECTED;
	else
		fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA;

	if (ieee->iw_mode == IW_MODE_INFRA) {
		fc |= IEEE80211_FCTL_TODS;
		/* To DS: Addr1 = BSSID, Addr2 = SA, Addr3 = DA */
		memcpy(header.addr1, ieee->bssid, ETH_ALEN);
		memcpy(header.addr2, src, ETH_ALEN);
		memcpy(header.addr3, dest, ETH_ALEN);
	} else if (ieee->iw_mode == IW_MODE_ADHOC) {
		/* not From/To DS: Addr1 = DA, Addr2 = SA, Addr3 = BSSID */
		memcpy(header.addr1, dest, ETH_ALEN);
		memcpy(header.addr2, src, ETH_ALEN);
		memcpy(header.addr3, ieee->bssid, ETH_ALEN);
	}
	hdr_len = LIBIPW_3ADDR_LEN;

	if (ieee->is_qos_active && ieee->is_qos_active(dev, skb)) {
		fc |= IEEE80211_STYPE_QOS_DATA;
		hdr_len += 2;

		skb->priority = libipw_classify(skb);
		header.qos_ctl |= cpu_to_le16(skb->priority & LIBIPW_QCTL_TID);
	}
	header.frame_ctl = cpu_to_le16(fc);

	/* Advance the SKB to the start of the payload */
	skb_pull(skb, sizeof(struct ethhdr));

	/* Determine total amount of storage required for TXB packets */
	bytes = skb->len + SNAP_SIZE + sizeof(u16);

	/* Encrypt msdu first on the whole data packet. */
	if ((host_encrypt || host_encrypt_msdu) &&
	    crypt && crypt->ops && crypt->ops->encrypt_msdu) {
		int res = 0;
		int len = bytes + hdr_len + crypt->ops->extra_msdu_prefix_len +
		    crypt->ops->extra_msdu_postfix_len;
		struct sk_buff *skb_new = dev_alloc_skb(len);

		if (unlikely(!skb_new))
			goto failed;

		skb_reserve(skb_new, crypt->ops->extra_msdu_prefix_len);
		memcpy(skb_put(skb_new, hdr_len), &header, hdr_len);
		snapped = 1;
		libipw_copy_snap(skb_put(skb_new, SNAP_SIZE + sizeof(u16)),
				    ether_type);
		skb_copy_from_linear_data(skb, skb_put(skb_new, skb->len), skb->len);
		res = crypt->ops->encrypt_msdu(skb_new, hdr_len, crypt->priv);
		if (res < 0) {
			LIBIPW_ERROR("msdu encryption failed\n");
			dev_kfree_skb_any(skb_new);
			goto failed;
		}
		dev_kfree_skb_any(skb);
		skb = skb_new;
		bytes += crypt->ops->extra_msdu_prefix_len +
		    crypt->ops->extra_msdu_postfix_len;
		skb_pull(skb, hdr_len);
	}

	if (host_encrypt || ieee->host_open_frag) {
		/* Determine fragmentation size based on destination (multicast
		 * and broadcast are not fragmented) */
		if (is_multicast_ether_addr(dest) ||
		    is_broadcast_ether_addr(dest))
			frag_size = MAX_FRAG_THRESHOLD;
		else
			frag_size = ieee->fts;

		/* Determine amount of payload per fragment.  Regardless of if
		 * this stack is providing the full 802.11 header, one will
		 * eventually be affixed to this fragment -- so we must account
		 * for it when determining the amount of payload space. */
		bytes_per_frag = frag_size - hdr_len;
		if (ieee->config &
		    (CFG_LIBIPW_COMPUTE_FCS | CFG_LIBIPW_RESERVE_FCS))
			bytes_per_frag -= LIBIPW_FCS_LEN;

		/* Each fragment may need to have room for encryptiong
		 * pre/postfix */
		if (host_encrypt)
			bytes_per_frag -= crypt->ops->extra_mpdu_prefix_len +
			    crypt->ops->extra_mpdu_postfix_len;

		/* Number of fragments is the total
		 * bytes_per_frag / payload_per_fragment */
		nr_frags = bytes / bytes_per_frag;
		bytes_last_frag = bytes % bytes_per_frag;
		if (bytes_last_frag)
			nr_frags++;
		else
			bytes_last_frag = bytes_per_frag;
	} else {
		nr_frags = 1;
		bytes_per_frag = bytes_last_frag = bytes;
		frag_size = bytes + hdr_len;
	}

	rts_required = (frag_size > ieee->rts
			&& ieee->config & CFG_LIBIPW_RTS);
	if (rts_required)
		nr_frags++;

	/* When we allocate the TXB we allocate enough space for the reserve
	 * and full fragment bytes (bytes_per_frag doesn't include prefix,
	 * postfix, header, FCS, etc.) */
	txb = libipw_alloc_txb(nr_frags, frag_size,
				  ieee->tx_headroom, GFP_ATOMIC);
	if (unlikely(!txb)) {
		printk(KERN_WARNING "%s: Could not allocate TXB\n",
		       ieee->dev->name);
		goto failed;
	}
	txb->encrypted = encrypt;
	if (host_encrypt)
		txb->payload_size = frag_size * (nr_frags - 1) +
		    bytes_last_frag;
	else
		txb->payload_size = bytes;

	if (rts_required) {
		skb_frag = txb->fragments[0];
		frag_hdr =
		    (struct libipw_hdr_3addrqos *)skb_put(skb_frag, hdr_len);

		/*
		 * Set header frame_ctl to the RTS.
		 */
		header.frame_ctl =
		    cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
		memcpy(frag_hdr, &header, hdr_len);

		/*
		 * Restore header frame_ctl to the original data setting.
		 */
		header.frame_ctl = cpu_to_le16(fc);

		if (ieee->config &
		    (CFG_LIBIPW_COMPUTE_FCS | CFG_LIBIPW_RESERVE_FCS))
			skb_put(skb_frag, 4);

		txb->rts_included = 1;
		i = 1;
	} else
		i = 0;

	for (; i < nr_frags; i++) {
		skb_frag = txb->fragments[i];

		if (host_encrypt || host_build_iv)
			skb_reserve(skb_frag,
				    crypt->ops->extra_mpdu_prefix_len);

		frag_hdr =
		    (struct libipw_hdr_3addrqos *)skb_put(skb_frag, hdr_len);
		memcpy(frag_hdr, &header, hdr_len);

		/* If this is not the last fragment, then add the MOREFRAGS
		 * bit to the frame control */
		if (i != nr_frags - 1) {
			frag_hdr->frame_ctl =
			    cpu_to_le16(fc | IEEE80211_FCTL_MOREFRAGS);
			bytes = bytes_per_frag;
		} else {
			/* The last fragment takes the remaining length */
			bytes = bytes_last_frag;
		}

		if (i == 0 && !snapped) {
			libipw_copy_snap(skb_put
					    (skb_frag, SNAP_SIZE + sizeof(u16)),
					    ether_type);
			bytes -= SNAP_SIZE + sizeof(u16);
		}

		skb_copy_from_linear_data(skb, skb_put(skb_frag, bytes), bytes);

		/* Advance the SKB... */
		skb_pull(skb, bytes);

		/* Encryption routine will move the header forward in order
		 * to insert the IV between the header and the payload */
		if (host_encrypt)
			libipw_encrypt_fragment(ieee, skb_frag, hdr_len);
		else if (host_build_iv) {
			atomic_inc(&crypt->refcnt);
			if (crypt->ops->build_iv)
				crypt->ops->build_iv(skb_frag, hdr_len,
				      ieee->sec.keys[ieee->sec.active_key],
				      ieee->sec.key_sizes[ieee->sec.active_key],
				      crypt->priv);
			atomic_dec(&crypt->refcnt);
		}

		if (ieee->config &
		    (CFG_LIBIPW_COMPUTE_FCS | CFG_LIBIPW_RESERVE_FCS))
			skb_put(skb_frag, 4);
	}

      success:
	spin_unlock_irqrestore(&ieee->lock, flags);

	dev_kfree_skb_any(skb);

	if (txb) {
		netdev_tx_t ret = (*ieee->hard_start_xmit)(txb, dev, priority);
		if (ret == NETDEV_TX_OK) {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += txb->payload_size;
			return NETDEV_TX_OK;
		}

		libipw_txb_free(txb);
	}

	return NETDEV_TX_OK;

      failed:
	spin_unlock_irqrestore(&ieee->lock, flags);
	netif_stop_queue(dev);
	dev->stats.tx_errors++;
	return NETDEV_TX_BUSY;
}
EXPORT_SYMBOL(libipw_xmit);

EXPORT_SYMBOL(libipw_txb_free);
