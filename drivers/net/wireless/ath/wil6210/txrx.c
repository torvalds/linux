/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>
#include <linux/moduleparam.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <linux/prefetch.h>

#include "wil6210.h"
#include "wmi.h"
#include "txrx.h"
#include "trace.h"

static bool rtap_include_phy_info;
module_param(rtap_include_phy_info, bool, 0444);
MODULE_PARM_DESC(rtap_include_phy_info,
		 " Include PHY info in the radiotap header, default - no");

bool rx_align_2;
module_param(rx_align_2, bool, 0444);
MODULE_PARM_DESC(rx_align_2, " align Rx buffers on 4*n+2, default - no");

bool rx_large_buf;
module_param(rx_large_buf, bool, 0444);
MODULE_PARM_DESC(rx_large_buf, " allocate 8KB RX buffers, default - no");

static inline uint wil_rx_snaplen(void)
{
	return rx_align_2 ? 6 : 0;
}

static inline int wil_vring_is_empty(struct vring *vring)
{
	return vring->swhead == vring->swtail;
}

static inline u32 wil_vring_next_tail(struct vring *vring)
{
	return (vring->swtail + 1) % vring->size;
}

static inline void wil_vring_advance_head(struct vring *vring, int n)
{
	vring->swhead = (vring->swhead + n) % vring->size;
}

static inline int wil_vring_is_full(struct vring *vring)
{
	return wil_vring_next_tail(vring) == vring->swhead;
}

/* Used space in Tx Vring */
static inline int wil_vring_used_tx(struct vring *vring)
{
	u32 swhead = vring->swhead;
	u32 swtail = vring->swtail;
	return (vring->size + swhead - swtail) % vring->size;
}

/* Available space in Tx Vring */
static inline int wil_vring_avail_tx(struct vring *vring)
{
	return vring->size - wil_vring_used_tx(vring) - 1;
}

/* wil_vring_wmark_low - low watermark for available descriptor space */
static inline int wil_vring_wmark_low(struct vring *vring)
{
	return vring->size/8;
}

/* wil_vring_wmark_high - high watermark for available descriptor space */
static inline int wil_vring_wmark_high(struct vring *vring)
{
	return vring->size/4;
}

/* returns true if num avail descriptors is lower than wmark_low */
static inline int wil_vring_avail_low(struct vring *vring)
{
	return wil_vring_avail_tx(vring) < wil_vring_wmark_low(vring);
}

/* returns true if num avail descriptors is higher than wmark_high */
static inline int wil_vring_avail_high(struct vring *vring)
{
	return wil_vring_avail_tx(vring) > wil_vring_wmark_high(vring);
}

/* wil_val_in_range - check if value in [min,max) */
static inline bool wil_val_in_range(int val, int min, int max)
{
	return val >= min && val < max;
}

static int wil_vring_alloc(struct wil6210_priv *wil, struct vring *vring)
{
	struct device *dev = wil_to_dev(wil);
	size_t sz = vring->size * sizeof(vring->va[0]);
	uint i;

	wil_dbg_misc(wil, "vring_alloc:\n");

	BUILD_BUG_ON(sizeof(vring->va[0]) != 32);

	vring->swhead = 0;
	vring->swtail = 0;
	vring->ctx = kcalloc(vring->size, sizeof(vring->ctx[0]), GFP_KERNEL);
	if (!vring->ctx) {
		vring->va = NULL;
		return -ENOMEM;
	}

	/* vring->va should be aligned on its size rounded up to power of 2
	 * This is granted by the dma_alloc_coherent.
	 *
	 * HW has limitation that all vrings addresses must share the same
	 * upper 16 msb bits part of 48 bits address. To workaround that,
	 * if we are using 48 bit addresses switch to 32 bit allocation
	 * before allocating vring memory.
	 *
	 * There's no check for the return value of dma_set_mask_and_coherent,
	 * since we assume if we were able to set the mask during
	 * initialization in this system it will not fail if we set it again
	 */
	if (wil->use_extended_dma_addr)
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));

	vring->va = dma_alloc_coherent(dev, sz, &vring->pa, GFP_KERNEL);
	if (!vring->va) {
		kfree(vring->ctx);
		vring->ctx = NULL;
		return -ENOMEM;
	}

	if (wil->use_extended_dma_addr)
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));

	/* initially, all descriptors are SW owned
	 * For Tx and Rx, ownership bit is at the same location, thus
	 * we can use any
	 */
	for (i = 0; i < vring->size; i++) {
		volatile struct vring_tx_desc *_d = &vring->va[i].tx;

		_d->dma.status = TX_DMA_STATUS_DU;
	}

	wil_dbg_misc(wil, "vring[%d] 0x%p:%pad 0x%p\n", vring->size,
		     vring->va, &vring->pa, vring->ctx);

	return 0;
}

static void wil_txdesc_unmap(struct device *dev, struct vring_tx_desc *d,
			     struct wil_ctx *ctx)
{
	dma_addr_t pa = wil_desc_addr(&d->dma.addr);
	u16 dmalen = le16_to_cpu(d->dma.length);

	switch (ctx->mapped_as) {
	case wil_mapped_as_single:
		dma_unmap_single(dev, pa, dmalen, DMA_TO_DEVICE);
		break;
	case wil_mapped_as_page:
		dma_unmap_page(dev, pa, dmalen, DMA_TO_DEVICE);
		break;
	default:
		break;
	}
}

static void wil_vring_free(struct wil6210_priv *wil, struct vring *vring,
			   int tx)
{
	struct device *dev = wil_to_dev(wil);
	size_t sz = vring->size * sizeof(vring->va[0]);

	lockdep_assert_held(&wil->mutex);
	if (tx) {
		int vring_index = vring - wil->vring_tx;

		wil_dbg_misc(wil, "free Tx vring %d [%d] 0x%p:%pad 0x%p\n",
			     vring_index, vring->size, vring->va,
			     &vring->pa, vring->ctx);
	} else {
		wil_dbg_misc(wil, "free Rx vring [%d] 0x%p:%pad 0x%p\n",
			     vring->size, vring->va,
			     &vring->pa, vring->ctx);
	}

	while (!wil_vring_is_empty(vring)) {
		dma_addr_t pa;
		u16 dmalen;
		struct wil_ctx *ctx;

		if (tx) {
			struct vring_tx_desc dd, *d = &dd;
			volatile struct vring_tx_desc *_d =
					&vring->va[vring->swtail].tx;

			ctx = &vring->ctx[vring->swtail];
			if (!ctx) {
				wil_dbg_txrx(wil,
					     "ctx(%d) was already completed\n",
					     vring->swtail);
				vring->swtail = wil_vring_next_tail(vring);
				continue;
			}
			*d = *_d;
			wil_txdesc_unmap(dev, d, ctx);
			if (ctx->skb)
				dev_kfree_skb_any(ctx->skb);
			vring->swtail = wil_vring_next_tail(vring);
		} else { /* rx */
			struct vring_rx_desc dd, *d = &dd;
			volatile struct vring_rx_desc *_d =
					&vring->va[vring->swhead].rx;

			ctx = &vring->ctx[vring->swhead];
			*d = *_d;
			pa = wil_desc_addr(&d->dma.addr);
			dmalen = le16_to_cpu(d->dma.length);
			dma_unmap_single(dev, pa, dmalen, DMA_FROM_DEVICE);
			kfree_skb(ctx->skb);
			wil_vring_advance_head(vring, 1);
		}
	}
	dma_free_coherent(dev, sz, (void *)vring->va, vring->pa);
	kfree(vring->ctx);
	vring->pa = 0;
	vring->va = NULL;
	vring->ctx = NULL;
}

/**
 * Allocate one skb for Rx VRING
 *
 * Safe to call from IRQ
 */
static int wil_vring_alloc_skb(struct wil6210_priv *wil, struct vring *vring,
			       u32 i, int headroom)
{
	struct device *dev = wil_to_dev(wil);
	unsigned int sz = wil->rx_buf_len + ETH_HLEN + wil_rx_snaplen();
	struct vring_rx_desc dd, *d = &dd;
	volatile struct vring_rx_desc *_d = &vring->va[i].rx;
	dma_addr_t pa;
	struct sk_buff *skb = dev_alloc_skb(sz + headroom);

	if (unlikely(!skb))
		return -ENOMEM;

	skb_reserve(skb, headroom);
	skb_put(skb, sz);

	pa = dma_map_single(dev, skb->data, skb->len, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, pa))) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	d->dma.d0 = RX_DMA_D0_CMD_DMA_RT | RX_DMA_D0_CMD_DMA_IT;
	wil_desc_addr_set(&d->dma.addr, pa);
	/* ip_length don't care */
	/* b11 don't care */
	/* error don't care */
	d->dma.status = 0; /* BIT(0) should be 0 for HW_OWNED */
	d->dma.length = cpu_to_le16(sz);
	*_d = *d;
	vring->ctx[i].skb = skb;

	return 0;
}

/**
 * Adds radiotap header
 *
 * Any error indicated as "Bad FCS"
 *
 * Vendor data for 04:ce:14-1 (Wilocity-1) consists of:
 *  - Rx descriptor: 32 bytes
 *  - Phy info
 */
static void wil_rx_add_radiotap_header(struct wil6210_priv *wil,
				       struct sk_buff *skb)
{
	struct wireless_dev *wdev = wil->wdev;
	struct wil6210_rtap {
		struct ieee80211_radiotap_header rthdr;
		/* fields should be in the order of bits in rthdr.it_present */
		/* flags */
		u8 flags;
		/* channel */
		__le16 chnl_freq __aligned(2);
		__le16 chnl_flags;
		/* MCS */
		u8 mcs_present;
		u8 mcs_flags;
		u8 mcs_index;
	} __packed;
	struct wil6210_rtap_vendor {
		struct wil6210_rtap rtap;
		/* vendor */
		u8 vendor_oui[3] __aligned(2);
		u8 vendor_ns;
		__le16 vendor_skip;
		u8 vendor_data[0];
	} __packed;
	struct vring_rx_desc *d = wil_skb_rxdesc(skb);
	struct wil6210_rtap_vendor *rtap_vendor;
	int rtap_len = sizeof(struct wil6210_rtap);
	int phy_length = 0; /* phy info header size, bytes */
	static char phy_data[128];
	struct ieee80211_channel *ch = wdev->preset_chandef.chan;

	if (rtap_include_phy_info) {
		rtap_len = sizeof(*rtap_vendor) + sizeof(*d);
		/* calculate additional length */
		if (d->dma.status & RX_DMA_STATUS_PHY_INFO) {
			/**
			 * PHY info starts from 8-byte boundary
			 * there are 8-byte lines, last line may be partially
			 * written (HW bug), thus FW configures for last line
			 * to be excessive. Driver skips this last line.
			 */
			int len = min_t(int, 8 + sizeof(phy_data),
					wil_rxdesc_phy_length(d));

			if (len > 8) {
				void *p = skb_tail_pointer(skb);
				void *pa = PTR_ALIGN(p, 8);

				if (skb_tailroom(skb) >= len + (pa - p)) {
					phy_length = len - 8;
					memcpy(phy_data, pa, phy_length);
				}
			}
		}
		rtap_len += phy_length;
	}

	if (skb_headroom(skb) < rtap_len &&
	    pskb_expand_head(skb, rtap_len, 0, GFP_ATOMIC)) {
		wil_err(wil, "Unable to expand headroom to %d\n", rtap_len);
		return;
	}

	rtap_vendor = (void *)skb_push(skb, rtap_len);
	memset(rtap_vendor, 0, rtap_len);

	rtap_vendor->rtap.rthdr.it_version = PKTHDR_RADIOTAP_VERSION;
	rtap_vendor->rtap.rthdr.it_len = cpu_to_le16(rtap_len);
	rtap_vendor->rtap.rthdr.it_present = cpu_to_le32(
			(1 << IEEE80211_RADIOTAP_FLAGS) |
			(1 << IEEE80211_RADIOTAP_CHANNEL) |
			(1 << IEEE80211_RADIOTAP_MCS));
	if (d->dma.status & RX_DMA_STATUS_ERROR)
		rtap_vendor->rtap.flags |= IEEE80211_RADIOTAP_F_BADFCS;

	rtap_vendor->rtap.chnl_freq = cpu_to_le16(ch ? ch->center_freq : 58320);
	rtap_vendor->rtap.chnl_flags = cpu_to_le16(0);

	rtap_vendor->rtap.mcs_present = IEEE80211_RADIOTAP_MCS_HAVE_MCS;
	rtap_vendor->rtap.mcs_flags = 0;
	rtap_vendor->rtap.mcs_index = wil_rxdesc_mcs(d);

	if (rtap_include_phy_info) {
		rtap_vendor->rtap.rthdr.it_present |= cpu_to_le32(1 <<
				IEEE80211_RADIOTAP_VENDOR_NAMESPACE);
		/* OUI for Wilocity 04:ce:14 */
		rtap_vendor->vendor_oui[0] = 0x04;
		rtap_vendor->vendor_oui[1] = 0xce;
		rtap_vendor->vendor_oui[2] = 0x14;
		rtap_vendor->vendor_ns = 1;
		/* Rx descriptor + PHY data  */
		rtap_vendor->vendor_skip = cpu_to_le16(sizeof(*d) +
						       phy_length);
		memcpy(rtap_vendor->vendor_data, (void *)d, sizeof(*d));
		memcpy(rtap_vendor->vendor_data + sizeof(*d), phy_data,
		       phy_length);
	}
}

/* similar to ieee80211_ version, but FC contain only 1-st byte */
static inline int wil_is_back_req(u8 fc)
{
	return (fc & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK_REQ);
}

/**
 * reap 1 frame from @swhead
 *
 * Rx descriptor copied to skb->cb
 *
 * Safe to call from IRQ
 */
static struct sk_buff *wil_vring_reap_rx(struct wil6210_priv *wil,
					 struct vring *vring)
{
	struct device *dev = wil_to_dev(wil);
	struct net_device *ndev = wil_to_ndev(wil);
	volatile struct vring_rx_desc *_d;
	struct vring_rx_desc *d;
	struct sk_buff *skb;
	dma_addr_t pa;
	unsigned int snaplen = wil_rx_snaplen();
	unsigned int sz = wil->rx_buf_len + ETH_HLEN + snaplen;
	u16 dmalen;
	u8 ftype;
	int cid;
	int i;
	struct wil_net_stats *stats;

	BUILD_BUG_ON(sizeof(struct vring_rx_desc) > sizeof(skb->cb));

again:
	if (unlikely(wil_vring_is_empty(vring)))
		return NULL;

	i = (int)vring->swhead;
	_d = &vring->va[i].rx;
	if (unlikely(!(_d->dma.status & RX_DMA_STATUS_DU))) {
		/* it is not error, we just reached end of Rx done area */
		return NULL;
	}

	skb = vring->ctx[i].skb;
	vring->ctx[i].skb = NULL;
	wil_vring_advance_head(vring, 1);
	if (!skb) {
		wil_err(wil, "No Rx skb at [%d]\n", i);
		goto again;
	}
	d = wil_skb_rxdesc(skb);
	*d = *_d;
	pa = wil_desc_addr(&d->dma.addr);

	dma_unmap_single(dev, pa, sz, DMA_FROM_DEVICE);
	dmalen = le16_to_cpu(d->dma.length);

	trace_wil6210_rx(i, d);
	wil_dbg_txrx(wil, "Rx[%3d] : %d bytes\n", i, dmalen);
	wil_hex_dump_txrx("RxD ", DUMP_PREFIX_NONE, 32, 4,
			  (const void *)d, sizeof(*d), false);

	cid = wil_rxdesc_cid(d);
	stats = &wil->sta[cid].stats;

	if (unlikely(dmalen > sz)) {
		wil_err(wil, "Rx size too large: %d bytes!\n", dmalen);
		stats->rx_large_frame++;
		kfree_skb(skb);
		goto again;
	}
	skb_trim(skb, dmalen);

	prefetch(skb->data);

	wil_hex_dump_txrx("Rx ", DUMP_PREFIX_OFFSET, 16, 1,
			  skb->data, skb_headlen(skb), false);

	stats->last_mcs_rx = wil_rxdesc_mcs(d);
	if (stats->last_mcs_rx < ARRAY_SIZE(stats->rx_per_mcs))
		stats->rx_per_mcs[stats->last_mcs_rx]++;

	/* use radiotap header only if required */
	if (ndev->type == ARPHRD_IEEE80211_RADIOTAP)
		wil_rx_add_radiotap_header(wil, skb);

	/* no extra checks if in sniffer mode */
	if (ndev->type != ARPHRD_ETHER)
		return skb;
	/* Non-data frames may be delivered through Rx DMA channel (ex: BAR)
	 * Driver should recognize it by frame type, that is found
	 * in Rx descriptor. If type is not data, it is 802.11 frame as is
	 */
	ftype = wil_rxdesc_ftype(d) << 2;
	if (unlikely(ftype != IEEE80211_FTYPE_DATA)) {
		u8 fc1 = wil_rxdesc_fc1(d);
		int mid = wil_rxdesc_mid(d);
		int tid = wil_rxdesc_tid(d);
		u16 seq = wil_rxdesc_seq(d);

		wil_dbg_txrx(wil,
			     "Non-data frame FC[7:0] 0x%02x MID %d CID %d TID %d Seq 0x%03x\n",
			     fc1, mid, cid, tid, seq);
		stats->rx_non_data_frame++;
		if (wil_is_back_req(fc1)) {
			wil_dbg_txrx(wil,
				     "BAR: MID %d CID %d TID %d Seq 0x%03x\n",
				     mid, cid, tid, seq);
			wil_rx_bar(wil, cid, tid, seq);
		} else {
			/* print again all info. One can enable only this
			 * without overhead for printing every Rx frame
			 */
			wil_dbg_txrx(wil,
				     "Unhandled non-data frame FC[7:0] 0x%02x MID %d CID %d TID %d Seq 0x%03x\n",
				     fc1, mid, cid, tid, seq);
			wil_hex_dump_txrx("RxD ", DUMP_PREFIX_NONE, 32, 4,
					  (const void *)d, sizeof(*d), false);
			wil_hex_dump_txrx("Rx ", DUMP_PREFIX_OFFSET, 16, 1,
					  skb->data, skb_headlen(skb), false);
		}
		kfree_skb(skb);
		goto again;
	}

	if (unlikely(skb->len < ETH_HLEN + snaplen)) {
		wil_err(wil, "Short frame, len = %d\n", skb->len);
		stats->rx_short_frame++;
		kfree_skb(skb);
		goto again;
	}

	/* L4 IDENT is on when HW calculated checksum, check status
	 * and in case of error drop the packet
	 * higher stack layers will handle retransmission (if required)
	 */
	if (likely(d->dma.status & RX_DMA_STATUS_L4I)) {
		/* L4 protocol identified, csum calculated */
		if (likely((d->dma.error & RX_DMA_ERROR_L4_ERR) == 0))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		/* If HW reports bad checksum, let IP stack re-check it
		 * For example, HW don't understand Microsoft IP stack that
		 * mis-calculates TCP checksum - if it should be 0x0,
		 * it writes 0xffff in violation of RFC 1624
		 */
	}

	if (snaplen) {
		/* Packet layout
		 * +-------+-------+---------+------------+------+
		 * | SA(6) | DA(6) | SNAP(6) | ETHTYPE(2) | DATA |
		 * +-------+-------+---------+------------+------+
		 * Need to remove SNAP, shifting SA and DA forward
		 */
		memmove(skb->data + snaplen, skb->data, 2 * ETH_ALEN);
		skb_pull(skb, snaplen);
	}

	return skb;
}

/**
 * allocate and fill up to @count buffers in rx ring
 * buffers posted at @swtail
 */
static int wil_rx_refill(struct wil6210_priv *wil, int count)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct vring *v = &wil->vring_rx;
	u32 next_tail;
	int rc = 0;
	int headroom = ndev->type == ARPHRD_IEEE80211_RADIOTAP ?
			WIL6210_RTAP_SIZE : 0;

	for (; next_tail = wil_vring_next_tail(v),
			(next_tail != v->swhead) && (count-- > 0);
			v->swtail = next_tail) {
		rc = wil_vring_alloc_skb(wil, v, v->swtail, headroom);
		if (unlikely(rc)) {
			wil_err(wil, "Error %d in wil_rx_refill[%d]\n",
				rc, v->swtail);
			break;
		}
	}

	/* make sure all writes to descriptors (shared memory) are done before
	 * committing them to HW
	 */
	wmb();

	wil_w(wil, v->hwtail, v->swtail);

	return rc;
}

/**
 * reverse_memcmp - Compare two areas of memory, in reverse order
 * @cs: One area of memory
 * @ct: Another area of memory
 * @count: The size of the area.
 *
 * Cut'n'paste from original memcmp (see lib/string.c)
 * with minimal modifications
 */
static int reverse_memcmp(const void *cs, const void *ct, size_t count)
{
	const unsigned char *su1, *su2;
	int res = 0;

	for (su1 = cs + count - 1, su2 = ct + count - 1; count > 0;
	     --su1, --su2, count--) {
		res = *su1 - *su2;
		if (res)
			break;
	}
	return res;
}

static int wil_rx_crypto_check(struct wil6210_priv *wil, struct sk_buff *skb)
{
	struct vring_rx_desc *d = wil_skb_rxdesc(skb);
	int cid = wil_rxdesc_cid(d);
	int tid = wil_rxdesc_tid(d);
	int key_id = wil_rxdesc_key_id(d);
	int mc = wil_rxdesc_mcast(d);
	struct wil_sta_info *s = &wil->sta[cid];
	struct wil_tid_crypto_rx *c = mc ? &s->group_crypto_rx :
				      &s->tid_crypto_rx[tid];
	struct wil_tid_crypto_rx_single *cc = &c->key_id[key_id];
	const u8 *pn = (u8 *)&d->mac.pn_15_0;

	if (!cc->key_set) {
		wil_err_ratelimited(wil,
				    "Key missing. CID %d TID %d MCast %d KEY_ID %d\n",
				    cid, tid, mc, key_id);
		return -EINVAL;
	}

	if (reverse_memcmp(pn, cc->pn, IEEE80211_GCMP_PN_LEN) <= 0) {
		wil_err_ratelimited(wil,
				    "Replay attack. CID %d TID %d MCast %d KEY_ID %d PN %6phN last %6phN\n",
				    cid, tid, mc, key_id, pn, cc->pn);
		return -EINVAL;
	}
	memcpy(cc->pn, pn, IEEE80211_GCMP_PN_LEN);

	return 0;
}

/*
 * Pass Rx packet to the netif. Update statistics.
 * Called in softirq context (NAPI poll).
 */
void wil_netif_rx_any(struct sk_buff *skb, struct net_device *ndev)
{
	gro_result_t rc = GRO_NORMAL;
	struct wil6210_priv *wil = ndev_to_wil(ndev);
	struct wireless_dev *wdev = wil_to_wdev(wil);
	unsigned int len = skb->len;
	struct vring_rx_desc *d = wil_skb_rxdesc(skb);
	int cid = wil_rxdesc_cid(d); /* always 0..7, no need to check */
	int security = wil_rxdesc_security(d);
	struct ethhdr *eth = (void *)skb->data;
	/* here looking for DA, not A1, thus Rxdesc's 'mcast' indication
	 * is not suitable, need to look at data
	 */
	int mcast = is_multicast_ether_addr(eth->h_dest);
	struct wil_net_stats *stats = &wil->sta[cid].stats;
	struct sk_buff *xmit_skb = NULL;
	static const char * const gro_res_str[] = {
		[GRO_MERGED]		= "GRO_MERGED",
		[GRO_MERGED_FREE]	= "GRO_MERGED_FREE",
		[GRO_HELD]		= "GRO_HELD",
		[GRO_NORMAL]		= "GRO_NORMAL",
		[GRO_DROP]		= "GRO_DROP",
	};

	if (ndev->features & NETIF_F_RXHASH)
		/* fake L4 to ensure it won't be re-calculated later
		 * set hash to any non-zero value to activate rps
		 * mechanism, core will be chosen according
		 * to user-level rps configuration.
		 */
		skb_set_hash(skb, 1, PKT_HASH_TYPE_L4);

	skb_orphan(skb);

	if (security && (wil_rx_crypto_check(wil, skb) != 0)) {
		rc = GRO_DROP;
		dev_kfree_skb(skb);
		stats->rx_replay++;
		goto stats;
	}

	if (wdev->iftype == NL80211_IFTYPE_AP && !wil->ap_isolate) {
		if (mcast) {
			/* send multicast frames both to higher layers in
			 * local net stack and back to the wireless medium
			 */
			xmit_skb = skb_copy(skb, GFP_ATOMIC);
		} else {
			int xmit_cid = wil_find_cid(wil, eth->h_dest);

			if (xmit_cid >= 0) {
				/* The destination station is associated to
				 * this AP (in this VLAN), so send the frame
				 * directly to it and do not pass it to local
				 * net stack.
				 */
				xmit_skb = skb;
				skb = NULL;
			}
		}
	}
	if (xmit_skb) {
		/* Send to wireless media and increase priority by 256 to
		 * keep the received priority instead of reclassifying
		 * the frame (see cfg80211_classify8021d).
		 */
		xmit_skb->dev = ndev;
		xmit_skb->priority += 256;
		xmit_skb->protocol = htons(ETH_P_802_3);
		skb_reset_network_header(xmit_skb);
		skb_reset_mac_header(xmit_skb);
		wil_dbg_txrx(wil, "Rx -> Tx %d bytes\n", len);
		dev_queue_xmit(xmit_skb);
	}

	if (skb) { /* deliver to local stack */

		skb->protocol = eth_type_trans(skb, ndev);
		rc = napi_gro_receive(&wil->napi_rx, skb);
		wil_dbg_txrx(wil, "Rx complete %d bytes => %s\n",
			     len, gro_res_str[rc]);
	}
stats:
	/* statistics. rc set to GRO_NORMAL for AP bridging */
	if (unlikely(rc == GRO_DROP)) {
		ndev->stats.rx_dropped++;
		stats->rx_dropped++;
		wil_dbg_txrx(wil, "Rx drop %d bytes\n", len);
	} else {
		ndev->stats.rx_packets++;
		stats->rx_packets++;
		ndev->stats.rx_bytes += len;
		stats->rx_bytes += len;
		if (mcast)
			ndev->stats.multicast++;
	}
}

/**
 * Proceed all completed skb's from Rx VRING
 *
 * Safe to call from NAPI poll, i.e. softirq with interrupts enabled
 */
void wil_rx_handle(struct wil6210_priv *wil, int *quota)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct vring *v = &wil->vring_rx;
	struct sk_buff *skb;

	if (unlikely(!v->va)) {
		wil_err(wil, "Rx IRQ while Rx not yet initialized\n");
		return;
	}
	wil_dbg_txrx(wil, "rx_handle\n");
	while ((*quota > 0) && (NULL != (skb = wil_vring_reap_rx(wil, v)))) {
		(*quota)--;

		if (wil->wdev->iftype == NL80211_IFTYPE_MONITOR) {
			skb->dev = ndev;
			skb_reset_mac_header(skb);
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->pkt_type = PACKET_OTHERHOST;
			skb->protocol = htons(ETH_P_802_2);
			wil_netif_rx_any(skb, ndev);
		} else {
			wil_rx_reorder(wil, skb);
		}
	}
	wil_rx_refill(wil, v->size);
}

static void wil_rx_buf_len_init(struct wil6210_priv *wil)
{
	wil->rx_buf_len = rx_large_buf ?
		WIL_MAX_ETH_MTU : TXRX_BUF_LEN_DEFAULT - WIL_MAX_MPDU_OVERHEAD;
	if (mtu_max > wil->rx_buf_len) {
		/* do not allow RX buffers to be smaller than mtu_max, for
		 * backward compatibility (mtu_max parameter was also used
		 * to support receiving large packets)
		 */
		wil_info(wil, "Override RX buffer to mtu_max(%d)\n", mtu_max);
		wil->rx_buf_len = mtu_max;
	}
}

int wil_rx_init(struct wil6210_priv *wil, u16 size)
{
	struct vring *vring = &wil->vring_rx;
	int rc;

	wil_dbg_misc(wil, "rx_init\n");

	if (vring->va) {
		wil_err(wil, "Rx ring already allocated\n");
		return -EINVAL;
	}

	wil_rx_buf_len_init(wil);

	vring->size = size;
	rc = wil_vring_alloc(wil, vring);
	if (rc)
		return rc;

	rc = wmi_rx_chain_add(wil, vring);
	if (rc)
		goto err_free;

	rc = wil_rx_refill(wil, vring->size);
	if (rc)
		goto err_free;

	return 0;
 err_free:
	wil_vring_free(wil, vring, 0);

	return rc;
}

void wil_rx_fini(struct wil6210_priv *wil)
{
	struct vring *vring = &wil->vring_rx;

	wil_dbg_misc(wil, "rx_fini\n");

	if (vring->va)
		wil_vring_free(wil, vring, 0);
}

static inline void wil_tx_data_init(struct vring_tx_data *txdata)
{
	spin_lock_bh(&txdata->lock);
	txdata->dot1x_open = 0;
	txdata->enabled = 0;
	txdata->idle = 0;
	txdata->last_idle = 0;
	txdata->begin = 0;
	txdata->agg_wsize = 0;
	txdata->agg_timeout = 0;
	txdata->agg_amsdu = 0;
	txdata->addba_in_progress = false;
	spin_unlock_bh(&txdata->lock);
}

int wil_vring_init_tx(struct wil6210_priv *wil, int id, int size,
		      int cid, int tid)
{
	int rc;
	struct wmi_vring_cfg_cmd cmd = {
		.action = cpu_to_le32(WMI_VRING_CMD_ADD),
		.vring_cfg = {
			.tx_sw_ring = {
				.max_mpdu_size =
					cpu_to_le16(wil_mtu2macbuf(mtu_max)),
				.ring_size = cpu_to_le16(size),
			},
			.ringid = id,
			.cidxtid = mk_cidxtid(cid, tid),
			.encap_trans_type = WMI_VRING_ENC_TYPE_802_3,
			.mac_ctrl = 0,
			.to_resolution = 0,
			.agg_max_wsize = 0,
			.schd_params = {
				.priority = cpu_to_le16(0),
				.timeslot_us = cpu_to_le16(0xfff),
			},
		},
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_vring_cfg_done_event cmd;
	} __packed reply;
	struct vring *vring = &wil->vring_tx[id];
	struct vring_tx_data *txdata = &wil->vring_tx_data[id];

	wil_dbg_misc(wil, "vring_init_tx: max_mpdu_size %d\n",
		     cmd.vring_cfg.tx_sw_ring.max_mpdu_size);
	lockdep_assert_held(&wil->mutex);

	if (vring->va) {
		wil_err(wil, "Tx ring [%d] already allocated\n", id);
		rc = -EINVAL;
		goto out;
	}

	wil_tx_data_init(txdata);
	vring->size = size;
	rc = wil_vring_alloc(wil, vring);
	if (rc)
		goto out;

	wil->vring2cid_tid[id][0] = cid;
	wil->vring2cid_tid[id][1] = tid;

	cmd.vring_cfg.tx_sw_ring.ring_mem_base = cpu_to_le64(vring->pa);

	if (!wil->privacy)
		txdata->dot1x_open = true;
	rc = wmi_call(wil, WMI_VRING_CFG_CMDID, &cmd, sizeof(cmd),
		      WMI_VRING_CFG_DONE_EVENTID, &reply, sizeof(reply), 100);
	if (rc)
		goto out_free;

	if (reply.cmd.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "Tx config failed, status 0x%02x\n",
			reply.cmd.status);
		rc = -EINVAL;
		goto out_free;
	}

	spin_lock_bh(&txdata->lock);
	vring->hwtail = le32_to_cpu(reply.cmd.tx_vring_tail_ptr);
	txdata->enabled = 1;
	spin_unlock_bh(&txdata->lock);

	if (txdata->dot1x_open && (agg_wsize >= 0))
		wil_addba_tx_request(wil, id, agg_wsize);

	return 0;
 out_free:
	spin_lock_bh(&txdata->lock);
	txdata->dot1x_open = false;
	txdata->enabled = 0;
	spin_unlock_bh(&txdata->lock);
	wil_vring_free(wil, vring, 1);
	wil->vring2cid_tid[id][0] = WIL6210_MAX_CID;
	wil->vring2cid_tid[id][1] = 0;

 out:

	return rc;
}

int wil_vring_init_bcast(struct wil6210_priv *wil, int id, int size)
{
	int rc;
	struct wmi_bcast_vring_cfg_cmd cmd = {
		.action = cpu_to_le32(WMI_VRING_CMD_ADD),
		.vring_cfg = {
			.tx_sw_ring = {
				.max_mpdu_size =
					cpu_to_le16(wil_mtu2macbuf(mtu_max)),
				.ring_size = cpu_to_le16(size),
			},
			.ringid = id,
			.encap_trans_type = WMI_VRING_ENC_TYPE_802_3,
		},
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_vring_cfg_done_event cmd;
	} __packed reply;
	struct vring *vring = &wil->vring_tx[id];
	struct vring_tx_data *txdata = &wil->vring_tx_data[id];

	wil_dbg_misc(wil, "vring_init_bcast: max_mpdu_size %d\n",
		     cmd.vring_cfg.tx_sw_ring.max_mpdu_size);
	lockdep_assert_held(&wil->mutex);

	if (vring->va) {
		wil_err(wil, "Tx ring [%d] already allocated\n", id);
		rc = -EINVAL;
		goto out;
	}

	wil_tx_data_init(txdata);
	vring->size = size;
	rc = wil_vring_alloc(wil, vring);
	if (rc)
		goto out;

	wil->vring2cid_tid[id][0] = WIL6210_MAX_CID; /* CID */
	wil->vring2cid_tid[id][1] = 0; /* TID */

	cmd.vring_cfg.tx_sw_ring.ring_mem_base = cpu_to_le64(vring->pa);

	if (!wil->privacy)
		txdata->dot1x_open = true;
	rc = wmi_call(wil, WMI_BCAST_VRING_CFG_CMDID, &cmd, sizeof(cmd),
		      WMI_VRING_CFG_DONE_EVENTID, &reply, sizeof(reply), 100);
	if (rc)
		goto out_free;

	if (reply.cmd.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "Tx config failed, status 0x%02x\n",
			reply.cmd.status);
		rc = -EINVAL;
		goto out_free;
	}

	spin_lock_bh(&txdata->lock);
	vring->hwtail = le32_to_cpu(reply.cmd.tx_vring_tail_ptr);
	txdata->enabled = 1;
	spin_unlock_bh(&txdata->lock);

	return 0;
 out_free:
	spin_lock_bh(&txdata->lock);
	txdata->enabled = 0;
	txdata->dot1x_open = false;
	spin_unlock_bh(&txdata->lock);
	wil_vring_free(wil, vring, 1);
 out:

	return rc;
}

void wil_vring_fini_tx(struct wil6210_priv *wil, int id)
{
	struct vring *vring = &wil->vring_tx[id];
	struct vring_tx_data *txdata = &wil->vring_tx_data[id];

	lockdep_assert_held(&wil->mutex);

	if (!vring->va)
		return;

	wil_dbg_misc(wil, "vring_fini_tx: id=%d\n", id);

	spin_lock_bh(&txdata->lock);
	txdata->dot1x_open = false;
	txdata->enabled = 0; /* no Tx can be in progress or start anew */
	spin_unlock_bh(&txdata->lock);
	/* napi_synchronize waits for completion of the current NAPI but will
	 * not prevent the next NAPI run.
	 * Add a memory barrier to guarantee that txdata->enabled is zeroed
	 * before napi_synchronize so that the next scheduled NAPI will not
	 * handle this vring
	 */
	wmb();
	/* make sure NAPI won't touch this vring */
	if (test_bit(wil_status_napi_en, wil->status))
		napi_synchronize(&wil->napi_tx);

	wil_vring_free(wil, vring, 1);
}

static struct vring *wil_find_tx_ucast(struct wil6210_priv *wil,
				       struct sk_buff *skb)
{
	int i;
	struct ethhdr *eth = (void *)skb->data;
	int cid = wil_find_cid(wil, eth->h_dest);

	if (cid < 0)
		return NULL;

	/* TODO: fix for multiple TID */
	for (i = 0; i < ARRAY_SIZE(wil->vring2cid_tid); i++) {
		if (!wil->vring_tx_data[i].dot1x_open &&
		    (skb->protocol != cpu_to_be16(ETH_P_PAE)))
			continue;
		if (wil->vring2cid_tid[i][0] == cid) {
			struct vring *v = &wil->vring_tx[i];
			struct vring_tx_data *txdata = &wil->vring_tx_data[i];

			wil_dbg_txrx(wil, "find_tx_ucast: (%pM) -> [%d]\n",
				     eth->h_dest, i);
			if (v->va && txdata->enabled) {
				return v;
			} else {
				wil_dbg_txrx(wil,
					     "find_tx_ucast: vring[%d] not valid\n",
					     i);
				return NULL;
			}
		}
	}

	return NULL;
}

static int wil_tx_vring(struct wil6210_priv *wil, struct vring *vring,
			struct sk_buff *skb);

static struct vring *wil_find_tx_vring_sta(struct wil6210_priv *wil,
					   struct sk_buff *skb)
{
	struct vring *v;
	int i;
	u8 cid;
	struct vring_tx_data *txdata;

	/* In the STA mode, it is expected to have only 1 VRING
	 * for the AP we connected to.
	 * find 1-st vring eligible for this skb and use it.
	 */
	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++) {
		v = &wil->vring_tx[i];
		txdata = &wil->vring_tx_data[i];
		if (!v->va || !txdata->enabled)
			continue;

		cid = wil->vring2cid_tid[i][0];
		if (cid >= WIL6210_MAX_CID) /* skip BCAST */
			continue;

		if (!wil->vring_tx_data[i].dot1x_open &&
		    (skb->protocol != cpu_to_be16(ETH_P_PAE)))
			continue;

		wil_dbg_txrx(wil, "Tx -> ring %d\n", i);

		return v;
	}

	wil_dbg_txrx(wil, "Tx while no vrings active?\n");

	return NULL;
}

/* Use one of 2 strategies:
 *
 * 1. New (real broadcast):
 *    use dedicated broadcast vring
 * 2. Old (pseudo-DMS):
 *    Find 1-st vring and return it;
 *    duplicate skb and send it to other active vrings;
 *    in all cases override dest address to unicast peer's address
 * Use old strategy when new is not supported yet:
 *  - for PBSS
 */
static struct vring *wil_find_tx_bcast_1(struct wil6210_priv *wil,
					 struct sk_buff *skb)
{
	struct vring *v;
	struct vring_tx_data *txdata;
	int i = wil->bcast_vring;

	if (i < 0)
		return NULL;
	v = &wil->vring_tx[i];
	txdata = &wil->vring_tx_data[i];
	if (!v->va || !txdata->enabled)
		return NULL;
	if (!wil->vring_tx_data[i].dot1x_open &&
	    (skb->protocol != cpu_to_be16(ETH_P_PAE)))
		return NULL;

	return v;
}

static void wil_set_da_for_vring(struct wil6210_priv *wil,
				 struct sk_buff *skb, int vring_index)
{
	struct ethhdr *eth = (void *)skb->data;
	int cid = wil->vring2cid_tid[vring_index][0];

	ether_addr_copy(eth->h_dest, wil->sta[cid].addr);
}

static struct vring *wil_find_tx_bcast_2(struct wil6210_priv *wil,
					 struct sk_buff *skb)
{
	struct vring *v, *v2;
	struct sk_buff *skb2;
	int i;
	u8 cid;
	struct ethhdr *eth = (void *)skb->data;
	char *src = eth->h_source;
	struct vring_tx_data *txdata;

	/* find 1-st vring eligible for data */
	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++) {
		v = &wil->vring_tx[i];
		txdata = &wil->vring_tx_data[i];
		if (!v->va || !txdata->enabled)
			continue;

		cid = wil->vring2cid_tid[i][0];
		if (cid >= WIL6210_MAX_CID) /* skip BCAST */
			continue;
		if (!wil->vring_tx_data[i].dot1x_open &&
		    (skb->protocol != cpu_to_be16(ETH_P_PAE)))
			continue;

		/* don't Tx back to source when re-routing Rx->Tx at the AP */
		if (0 == memcmp(wil->sta[cid].addr, src, ETH_ALEN))
			continue;

		goto found;
	}

	wil_dbg_txrx(wil, "Tx while no vrings active?\n");

	return NULL;

found:
	wil_dbg_txrx(wil, "BCAST -> ring %d\n", i);
	wil_set_da_for_vring(wil, skb, i);

	/* find other active vrings and duplicate skb for each */
	for (i++; i < WIL6210_MAX_TX_RINGS; i++) {
		v2 = &wil->vring_tx[i];
		if (!v2->va)
			continue;
		cid = wil->vring2cid_tid[i][0];
		if (cid >= WIL6210_MAX_CID) /* skip BCAST */
			continue;
		if (!wil->vring_tx_data[i].dot1x_open &&
		    (skb->protocol != cpu_to_be16(ETH_P_PAE)))
			continue;

		if (0 == memcmp(wil->sta[cid].addr, src, ETH_ALEN))
			continue;

		skb2 = skb_copy(skb, GFP_ATOMIC);
		if (skb2) {
			wil_dbg_txrx(wil, "BCAST DUP -> ring %d\n", i);
			wil_set_da_for_vring(wil, skb2, i);
			wil_tx_vring(wil, v2, skb2);
		} else {
			wil_err(wil, "skb_copy failed\n");
		}
	}

	return v;
}

static int wil_tx_desc_map(struct vring_tx_desc *d, dma_addr_t pa, u32 len,
			   int vring_index)
{
	wil_desc_addr_set(&d->dma.addr, pa);
	d->dma.ip_length = 0;
	/* 0..6: mac_length; 7:ip_version 0-IP6 1-IP4*/
	d->dma.b11 = 0/*14 | BIT(7)*/;
	d->dma.error = 0;
	d->dma.status = 0; /* BIT(0) should be 0 for HW_OWNED */
	d->dma.length = cpu_to_le16((u16)len);
	d->dma.d0 = (vring_index << DMA_CFG_DESC_TX_0_QID_POS);
	d->mac.d[0] = 0;
	d->mac.d[1] = 0;
	d->mac.d[2] = 0;
	d->mac.ucode_cmd = 0;
	/* translation type:  0 - bypass; 1 - 802.3; 2 - native wifi */
	d->mac.d[2] = BIT(MAC_CFG_DESC_TX_2_SNAP_HDR_INSERTION_EN_POS) |
		      (1 << MAC_CFG_DESC_TX_2_L2_TRANSLATION_TYPE_POS);

	return 0;
}

static inline
void wil_tx_desc_set_nr_frags(struct vring_tx_desc *d, int nr_frags)
{
	d->mac.d[2] |= (nr_frags << MAC_CFG_DESC_TX_2_NUM_OF_DESCRIPTORS_POS);
}

/**
 * Sets the descriptor @d up for csum and/or TSO offloading. The corresponding
 * @skb is used to obtain the protocol and headers length.
 * @tso_desc_type is a descriptor type for TSO: 0 - a header, 1 - first data,
 * 2 - middle, 3 - last descriptor.
 */

static void wil_tx_desc_offload_setup_tso(struct vring_tx_desc *d,
					  struct sk_buff *skb,
					  int tso_desc_type, bool is_ipv4,
					  int tcp_hdr_len, int skb_net_hdr_len)
{
	d->dma.b11 = ETH_HLEN; /* MAC header length */
	d->dma.b11 |= is_ipv4 << DMA_CFG_DESC_TX_OFFLOAD_CFG_L3T_IPV4_POS;

	d->dma.d0 |= (2 << DMA_CFG_DESC_TX_0_L4_TYPE_POS);
	/* L4 header len: TCP header length */
	d->dma.d0 |= (tcp_hdr_len & DMA_CFG_DESC_TX_0_L4_LENGTH_MSK);

	/* Setup TSO: bit and desc type */
	d->dma.d0 |= (BIT(DMA_CFG_DESC_TX_0_TCP_SEG_EN_POS)) |
		(tso_desc_type << DMA_CFG_DESC_TX_0_SEGMENT_BUF_DETAILS_POS);
	d->dma.d0 |= (is_ipv4 << DMA_CFG_DESC_TX_0_IPV4_CHECKSUM_EN_POS);

	d->dma.ip_length = skb_net_hdr_len;
	/* Enable TCP/UDP checksum */
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_TCP_UDP_CHECKSUM_EN_POS);
	/* Calculate pseudo-header */
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_PSEUDO_HEADER_CALC_EN_POS);
}

/**
 * Sets the descriptor @d up for csum. The corresponding
 * @skb is used to obtain the protocol and headers length.
 * Returns the protocol: 0 - not TCP, 1 - TCPv4, 2 - TCPv6.
 * Note, if d==NULL, the function only returns the protocol result.
 *
 * It is very similar to previous wil_tx_desc_offload_setup_tso. This
 * is "if unrolling" to optimize the critical path.
 */

static int wil_tx_desc_offload_setup(struct vring_tx_desc *d,
				     struct sk_buff *skb){
	int protocol;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	d->dma.b11 = ETH_HLEN; /* MAC header length */

	switch (skb->protocol) {
	case cpu_to_be16(ETH_P_IP):
		protocol = ip_hdr(skb)->protocol;
		d->dma.b11 |= BIT(DMA_CFG_DESC_TX_OFFLOAD_CFG_L3T_IPV4_POS);
		break;
	case cpu_to_be16(ETH_P_IPV6):
		protocol = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		return -EINVAL;
	}

	switch (protocol) {
	case IPPROTO_TCP:
		d->dma.d0 |= (2 << DMA_CFG_DESC_TX_0_L4_TYPE_POS);
		/* L4 header len: TCP header length */
		d->dma.d0 |=
		(tcp_hdrlen(skb) & DMA_CFG_DESC_TX_0_L4_LENGTH_MSK);
		break;
	case IPPROTO_UDP:
		/* L4 header len: UDP header length */
		d->dma.d0 |=
		(sizeof(struct udphdr) & DMA_CFG_DESC_TX_0_L4_LENGTH_MSK);
		break;
	default:
		return -EINVAL;
	}

	d->dma.ip_length = skb_network_header_len(skb);
	/* Enable TCP/UDP checksum */
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_TCP_UDP_CHECKSUM_EN_POS);
	/* Calculate pseudo-header */
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_PSEUDO_HEADER_CALC_EN_POS);

	return 0;
}

static inline void wil_tx_last_desc(struct vring_tx_desc *d)
{
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_CMD_EOP_POS) |
	      BIT(DMA_CFG_DESC_TX_0_CMD_MARK_WB_POS) |
	      BIT(DMA_CFG_DESC_TX_0_CMD_DMA_IT_POS);
}

static inline void wil_set_tx_desc_last_tso(volatile struct vring_tx_desc *d)
{
	d->dma.d0 |= wil_tso_type_lst <<
		  DMA_CFG_DESC_TX_0_SEGMENT_BUF_DETAILS_POS;
}

static int __wil_tx_vring_tso(struct wil6210_priv *wil, struct vring *vring,
			      struct sk_buff *skb)
{
	struct device *dev = wil_to_dev(wil);

	/* point to descriptors in shared memory */
	volatile struct vring_tx_desc *_desc = NULL, *_hdr_desc,
				      *_first_desc = NULL;

	/* pointers to shadow descriptors */
	struct vring_tx_desc desc_mem, hdr_desc_mem, first_desc_mem,
			     *d = &hdr_desc_mem, *hdr_desc = &hdr_desc_mem,
			     *first_desc = &first_desc_mem;

	/* pointer to shadow descriptors' context */
	struct wil_ctx *hdr_ctx, *first_ctx = NULL;

	int descs_used = 0; /* total number of used descriptors */
	int sg_desc_cnt = 0; /* number of descriptors for current mss*/

	u32 swhead = vring->swhead;
	int used, avail = wil_vring_avail_tx(vring);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int min_desc_required = nr_frags + 1;
	int mss = skb_shinfo(skb)->gso_size;	/* payload size w/o headers */
	int f, len, hdrlen, headlen;
	int vring_index = vring - wil->vring_tx;
	struct vring_tx_data *txdata = &wil->vring_tx_data[vring_index];
	uint i = swhead;
	dma_addr_t pa;
	const skb_frag_t *frag = NULL;
	int rem_data = mss;
	int lenmss;
	int hdr_compensation_need = true;
	int desc_tso_type = wil_tso_type_first;
	bool is_ipv4;
	int tcp_hdr_len;
	int skb_net_hdr_len;
	int gso_type;
	int rc = -EINVAL;

	wil_dbg_txrx(wil, "tx_vring_tso: %d bytes to vring %d\n", skb->len,
		     vring_index);

	if (unlikely(!txdata->enabled))
		return -EINVAL;

	/* A typical page 4K is 3-4 payloads, we assume each fragment
	 * is a full payload, that's how min_desc_required has been
	 * calculated. In real we might need more or less descriptors,
	 * this is the initial check only.
	 */
	if (unlikely(avail < min_desc_required)) {
		wil_err_ratelimited(wil,
				    "TSO: Tx ring[%2d] full. No space for %d fragments\n",
				    vring_index, min_desc_required);
		return -ENOMEM;
	}

	/* Header Length = MAC header len + IP header len + TCP header len*/
	hdrlen = ETH_HLEN +
		(int)skb_network_header_len(skb) +
		tcp_hdrlen(skb);

	gso_type = skb_shinfo(skb)->gso_type & (SKB_GSO_TCPV6 | SKB_GSO_TCPV4);
	switch (gso_type) {
	case SKB_GSO_TCPV4:
		/* TCP v4, zero out the IP length and IPv4 checksum fields
		 * as required by the offloading doc
		 */
		ip_hdr(skb)->tot_len = 0;
		ip_hdr(skb)->check = 0;
		is_ipv4 = true;
		break;
	case SKB_GSO_TCPV6:
		/* TCP v6, zero out the payload length */
		ipv6_hdr(skb)->payload_len = 0;
		is_ipv4 = false;
		break;
	default:
		/* other than TCPv4 or TCPv6 types are not supported for TSO.
		 * It is also illegal for both to be set simultaneously
		 */
		return -EINVAL;
	}

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return -EINVAL;

	/* tcp header length and skb network header length are fixed for all
	 * packet's descriptors - read then once here
	 */
	tcp_hdr_len = tcp_hdrlen(skb);
	skb_net_hdr_len = skb_network_header_len(skb);

	_hdr_desc = &vring->va[i].tx;

	pa = dma_map_single(dev, skb->data, hdrlen, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, pa))) {
		wil_err(wil, "TSO: Skb head DMA map error\n");
		goto err_exit;
	}

	wil_tx_desc_map(hdr_desc, pa, hdrlen, vring_index);
	wil_tx_desc_offload_setup_tso(hdr_desc, skb, wil_tso_type_hdr, is_ipv4,
				      tcp_hdr_len, skb_net_hdr_len);
	wil_tx_last_desc(hdr_desc);

	vring->ctx[i].mapped_as = wil_mapped_as_single;
	hdr_ctx = &vring->ctx[i];

	descs_used++;
	headlen = skb_headlen(skb) - hdrlen;

	for (f = headlen ? -1 : 0; f < nr_frags; f++)  {
		if (headlen) {
			len = headlen;
			wil_dbg_txrx(wil, "TSO: process skb head, len %u\n",
				     len);
		} else {
			frag = &skb_shinfo(skb)->frags[f];
			len = frag->size;
			wil_dbg_txrx(wil, "TSO: frag[%d]: len %u\n", f, len);
		}

		while (len) {
			wil_dbg_txrx(wil,
				     "TSO: len %d, rem_data %d, descs_used %d\n",
				     len, rem_data, descs_used);

			if (descs_used == avail)  {
				wil_err_ratelimited(wil, "TSO: ring overflow\n");
				rc = -ENOMEM;
				goto mem_error;
			}

			lenmss = min_t(int, rem_data, len);
			i = (swhead + descs_used) % vring->size;
			wil_dbg_txrx(wil, "TSO: lenmss %d, i %d\n", lenmss, i);

			if (!headlen) {
				pa = skb_frag_dma_map(dev, frag,
						      frag->size - len, lenmss,
						      DMA_TO_DEVICE);
				vring->ctx[i].mapped_as = wil_mapped_as_page;
			} else {
				pa = dma_map_single(dev,
						    skb->data +
						    skb_headlen(skb) - headlen,
						    lenmss,
						    DMA_TO_DEVICE);
				vring->ctx[i].mapped_as = wil_mapped_as_single;
				headlen -= lenmss;
			}

			if (unlikely(dma_mapping_error(dev, pa))) {
				wil_err(wil, "TSO: DMA map page error\n");
				goto mem_error;
			}

			_desc = &vring->va[i].tx;

			if (!_first_desc) {
				_first_desc = _desc;
				first_ctx = &vring->ctx[i];
				d = first_desc;
			} else {
				d = &desc_mem;
			}

			wil_tx_desc_map(d, pa, lenmss, vring_index);
			wil_tx_desc_offload_setup_tso(d, skb, desc_tso_type,
						      is_ipv4, tcp_hdr_len,
						      skb_net_hdr_len);

			/* use tso_type_first only once */
			desc_tso_type = wil_tso_type_mid;

			descs_used++;  /* desc used so far */
			sg_desc_cnt++; /* desc used for this segment */
			len -= lenmss;
			rem_data -= lenmss;

			wil_dbg_txrx(wil,
				     "TSO: len %d, rem_data %d, descs_used %d, sg_desc_cnt %d,\n",
				     len, rem_data, descs_used, sg_desc_cnt);

			/* Close the segment if reached mss size or last frag*/
			if (rem_data == 0 || (f == nr_frags - 1 && len == 0)) {
				if (hdr_compensation_need) {
					/* first segment include hdr desc for
					 * release
					 */
					hdr_ctx->nr_frags = sg_desc_cnt;
					wil_tx_desc_set_nr_frags(first_desc,
								 sg_desc_cnt +
								 1);
					hdr_compensation_need = false;
				} else {
					wil_tx_desc_set_nr_frags(first_desc,
								 sg_desc_cnt);
				}
				first_ctx->nr_frags = sg_desc_cnt - 1;

				wil_tx_last_desc(d);

				/* first descriptor may also be the last
				 * for this mss - make sure not to copy
				 * it twice
				 */
				if (first_desc != d)
					*_first_desc = *first_desc;

				/*last descriptor will be copied at the end
				 * of this TS processing
				 */
				if (f < nr_frags - 1 || len > 0)
					*_desc = *d;

				rem_data = mss;
				_first_desc = NULL;
				sg_desc_cnt = 0;
			} else if (first_desc != d) /* update mid descriptor */
					*_desc = *d;
		}
	}

	/* first descriptor may also be the last.
	 * in this case d pointer is invalid
	 */
	if (_first_desc == _desc)
		d = first_desc;

	/* Last data descriptor */
	wil_set_tx_desc_last_tso(d);
	*_desc = *d;

	/* Fill the total number of descriptors in first desc (hdr)*/
	wil_tx_desc_set_nr_frags(hdr_desc, descs_used);
	*_hdr_desc = *hdr_desc;

	/* hold reference to skb
	 * to prevent skb release before accounting
	 * in case of immediate "tx done"
	 */
	vring->ctx[i].skb = skb_get(skb);

	/* performance monitoring */
	used = wil_vring_used_tx(vring);
	if (wil_val_in_range(vring_idle_trsh,
			     used, used + descs_used)) {
		txdata->idle += get_cycles() - txdata->last_idle;
		wil_dbg_txrx(wil,  "Ring[%2d] not idle %d -> %d\n",
			     vring_index, used, used + descs_used);
	}

	/* Make sure to advance the head only after descriptor update is done.
	 * This will prevent a race condition where the completion thread
	 * will see the DU bit set from previous run and will handle the
	 * skb before it was completed.
	 */
	wmb();

	/* advance swhead */
	wil_vring_advance_head(vring, descs_used);
	wil_dbg_txrx(wil, "TSO: Tx swhead %d -> %d\n", swhead, vring->swhead);

	/* make sure all writes to descriptors (shared memory) are done before
	 * committing them to HW
	 */
	wmb();

	wil_w(wil, vring->hwtail, vring->swhead);
	return 0;

mem_error:
	while (descs_used > 0) {
		struct wil_ctx *ctx;

		i = (swhead + descs_used - 1) % vring->size;
		d = (struct vring_tx_desc *)&vring->va[i].tx;
		_desc = &vring->va[i].tx;
		*d = *_desc;
		_desc->dma.status = TX_DMA_STATUS_DU;
		ctx = &vring->ctx[i];
		wil_txdesc_unmap(dev, d, ctx);
		memset(ctx, 0, sizeof(*ctx));
		descs_used--;
	}
err_exit:
	return rc;
}

static int __wil_tx_vring(struct wil6210_priv *wil, struct vring *vring,
			  struct sk_buff *skb)
{
	struct device *dev = wil_to_dev(wil);
	struct vring_tx_desc dd, *d = &dd;
	volatile struct vring_tx_desc *_d;
	u32 swhead = vring->swhead;
	int avail = wil_vring_avail_tx(vring);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	uint f = 0;
	int vring_index = vring - wil->vring_tx;
	struct vring_tx_data *txdata = &wil->vring_tx_data[vring_index];
	uint i = swhead;
	dma_addr_t pa;
	int used;
	bool mcast = (vring_index == wil->bcast_vring);
	uint len = skb_headlen(skb);

	wil_dbg_txrx(wil, "tx_vring: %d bytes to vring %d\n", skb->len,
		     vring_index);

	if (unlikely(!txdata->enabled))
		return -EINVAL;

	if (unlikely(avail < 1 + nr_frags)) {
		wil_err_ratelimited(wil,
				    "Tx ring[%2d] full. No space for %d fragments\n",
				    vring_index, 1 + nr_frags);
		return -ENOMEM;
	}
	_d = &vring->va[i].tx;

	pa = dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);

	wil_dbg_txrx(wil, "Tx[%2d] skb %d bytes 0x%p -> %pad\n", vring_index,
		     skb_headlen(skb), skb->data, &pa);
	wil_hex_dump_txrx("Tx ", DUMP_PREFIX_OFFSET, 16, 1,
			  skb->data, skb_headlen(skb), false);

	if (unlikely(dma_mapping_error(dev, pa)))
		return -EINVAL;
	vring->ctx[i].mapped_as = wil_mapped_as_single;
	/* 1-st segment */
	wil_tx_desc_map(d, pa, len, vring_index);
	if (unlikely(mcast)) {
		d->mac.d[0] |= BIT(MAC_CFG_DESC_TX_0_MCS_EN_POS); /* MCS 0 */
		if (unlikely(len > WIL_BCAST_MCS0_LIMIT)) /* set MCS 1 */
			d->mac.d[0] |= (1 << MAC_CFG_DESC_TX_0_MCS_INDEX_POS);
	}
	/* Process TCP/UDP checksum offloading */
	if (unlikely(wil_tx_desc_offload_setup(d, skb))) {
		wil_err(wil, "Tx[%2d] Failed to set cksum, drop packet\n",
			vring_index);
		goto dma_error;
	}

	vring->ctx[i].nr_frags = nr_frags;
	wil_tx_desc_set_nr_frags(d, nr_frags + 1);

	/* middle segments */
	for (; f < nr_frags; f++) {
		const struct skb_frag_struct *frag =
				&skb_shinfo(skb)->frags[f];
		int len = skb_frag_size(frag);

		*_d = *d;
		wil_dbg_txrx(wil, "Tx[%2d] desc[%4d]\n", vring_index, i);
		wil_hex_dump_txrx("TxD ", DUMP_PREFIX_NONE, 32, 4,
				  (const void *)d, sizeof(*d), false);
		i = (swhead + f + 1) % vring->size;
		_d = &vring->va[i].tx;
		pa = skb_frag_dma_map(dev, frag, 0, skb_frag_size(frag),
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, pa))) {
			wil_err(wil, "Tx[%2d] failed to map fragment\n",
				vring_index);
			goto dma_error;
		}
		vring->ctx[i].mapped_as = wil_mapped_as_page;
		wil_tx_desc_map(d, pa, len, vring_index);
		/* no need to check return code -
		 * if it succeeded for 1-st descriptor,
		 * it will succeed here too
		 */
		wil_tx_desc_offload_setup(d, skb);
	}
	/* for the last seg only */
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_CMD_EOP_POS);
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_CMD_MARK_WB_POS);
	d->dma.d0 |= BIT(DMA_CFG_DESC_TX_0_CMD_DMA_IT_POS);
	*_d = *d;
	wil_dbg_txrx(wil, "Tx[%2d] desc[%4d]\n", vring_index, i);
	wil_hex_dump_txrx("TxD ", DUMP_PREFIX_NONE, 32, 4,
			  (const void *)d, sizeof(*d), false);

	/* hold reference to skb
	 * to prevent skb release before accounting
	 * in case of immediate "tx done"
	 */
	vring->ctx[i].skb = skb_get(skb);

	/* performance monitoring */
	used = wil_vring_used_tx(vring);
	if (wil_val_in_range(vring_idle_trsh,
			     used, used + nr_frags + 1)) {
		txdata->idle += get_cycles() - txdata->last_idle;
		wil_dbg_txrx(wil,  "Ring[%2d] not idle %d -> %d\n",
			     vring_index, used, used + nr_frags + 1);
	}

	/* Make sure to advance the head only after descriptor update is done.
	 * This will prevent a race condition where the completion thread
	 * will see the DU bit set from previous run and will handle the
	 * skb before it was completed.
	 */
	wmb();

	/* advance swhead */
	wil_vring_advance_head(vring, nr_frags + 1);
	wil_dbg_txrx(wil, "Tx[%2d] swhead %d -> %d\n", vring_index, swhead,
		     vring->swhead);
	trace_wil6210_tx(vring_index, swhead, skb->len, nr_frags);

	/* make sure all writes to descriptors (shared memory) are done before
	 * committing them to HW
	 */
	wmb();

	wil_w(wil, vring->hwtail, vring->swhead);

	return 0;
 dma_error:
	/* unmap what we have mapped */
	nr_frags = f + 1; /* frags mapped + one for skb head */
	for (f = 0; f < nr_frags; f++) {
		struct wil_ctx *ctx;

		i = (swhead + f) % vring->size;
		ctx = &vring->ctx[i];
		_d = &vring->va[i].tx;
		*d = *_d;
		_d->dma.status = TX_DMA_STATUS_DU;
		wil_txdesc_unmap(dev, d, ctx);

		memset(ctx, 0, sizeof(*ctx));
	}

	return -EINVAL;
}

static int wil_tx_vring(struct wil6210_priv *wil, struct vring *vring,
			struct sk_buff *skb)
{
	int vring_index = vring - wil->vring_tx;
	struct vring_tx_data *txdata = &wil->vring_tx_data[vring_index];
	int rc;

	spin_lock(&txdata->lock);

	rc = (skb_is_gso(skb) ? __wil_tx_vring_tso : __wil_tx_vring)
	     (wil, vring, skb);

	spin_unlock(&txdata->lock);

	return rc;
}

/**
 * Check status of tx vrings and stop/wake net queues if needed
 *
 * This function does one of two checks:
 * In case check_stop is true, will check if net queues need to be stopped. If
 * the conditions for stopping are met, netif_tx_stop_all_queues() is called.
 * In case check_stop is false, will check if net queues need to be waked. If
 * the conditions for waking are met, netif_tx_wake_all_queues() is called.
 * vring is the vring which is currently being modified by either adding
 * descriptors (tx) into it or removing descriptors (tx complete) from it. Can
 * be null when irrelevant (e.g. connect/disconnect events).
 *
 * The implementation is to stop net queues if modified vring has low
 * descriptor availability. Wake if all vrings are not in low descriptor
 * availability and modified vring has high descriptor availability.
 */
static inline void __wil_update_net_queues(struct wil6210_priv *wil,
					   struct vring *vring,
					   bool check_stop)
{
	int i;

	if (vring)
		wil_dbg_txrx(wil, "vring %d, check_stop=%d, stopped=%d",
			     (int)(vring - wil->vring_tx), check_stop,
			     wil->net_queue_stopped);
	else
		wil_dbg_txrx(wil, "check_stop=%d, stopped=%d",
			     check_stop, wil->net_queue_stopped);

	if (check_stop == wil->net_queue_stopped)
		/* net queues already in desired state */
		return;

	if (check_stop) {
		if (!vring || unlikely(wil_vring_avail_low(vring))) {
			/* not enough room in the vring */
			netif_tx_stop_all_queues(wil_to_ndev(wil));
			wil->net_queue_stopped = true;
			wil_dbg_txrx(wil, "netif_tx_stop called\n");
		}
		return;
	}

	/* check wake */
	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++) {
		struct vring *cur_vring = &wil->vring_tx[i];
		struct vring_tx_data *txdata = &wil->vring_tx_data[i];

		if (!cur_vring->va || !txdata->enabled || cur_vring == vring)
			continue;

		if (wil_vring_avail_low(cur_vring)) {
			wil_dbg_txrx(wil, "vring %d full, can't wake\n",
				     (int)(cur_vring - wil->vring_tx));
			return;
		}
	}

	if (!vring || wil_vring_avail_high(vring)) {
		/* enough room in the vring */
		wil_dbg_txrx(wil, "calling netif_tx_wake\n");
		netif_tx_wake_all_queues(wil_to_ndev(wil));
		wil->net_queue_stopped = false;
	}
}

void wil_update_net_queues(struct wil6210_priv *wil, struct vring *vring,
			   bool check_stop)
{
	spin_lock(&wil->net_queue_lock);
	__wil_update_net_queues(wil, vring, check_stop);
	spin_unlock(&wil->net_queue_lock);
}

void wil_update_net_queues_bh(struct wil6210_priv *wil, struct vring *vring,
			      bool check_stop)
{
	spin_lock_bh(&wil->net_queue_lock);
	__wil_update_net_queues(wil, vring, check_stop);
	spin_unlock_bh(&wil->net_queue_lock);
}

netdev_tx_t wil_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct wil6210_priv *wil = ndev_to_wil(ndev);
	struct ethhdr *eth = (void *)skb->data;
	bool bcast = is_multicast_ether_addr(eth->h_dest);
	struct vring *vring;
	static bool pr_once_fw;
	int rc;

	wil_dbg_txrx(wil, "start_xmit\n");
	if (unlikely(!test_bit(wil_status_fwready, wil->status))) {
		if (!pr_once_fw) {
			wil_err(wil, "FW not ready\n");
			pr_once_fw = true;
		}
		goto drop;
	}
	if (unlikely(!test_bit(wil_status_fwconnected, wil->status))) {
		wil_dbg_ratelimited(wil, "FW not connected, packet dropped\n");
		goto drop;
	}
	if (unlikely(wil->wdev->iftype == NL80211_IFTYPE_MONITOR)) {
		wil_err(wil, "Xmit in monitor mode not supported\n");
		goto drop;
	}
	pr_once_fw = false;

	/* find vring */
	if (wil->wdev->iftype == NL80211_IFTYPE_STATION && !wil->pbss) {
		/* in STA mode (ESS), all to same VRING (to AP) */
		vring = wil_find_tx_vring_sta(wil, skb);
	} else if (bcast) {
		if (wil->pbss)
			/* in pbss, no bcast VRING - duplicate skb in
			 * all stations VRINGs
			 */
			vring = wil_find_tx_bcast_2(wil, skb);
		else if (wil->wdev->iftype == NL80211_IFTYPE_AP)
			/* AP has a dedicated bcast VRING */
			vring = wil_find_tx_bcast_1(wil, skb);
		else
			/* unexpected combination, fallback to duplicating
			 * the skb in all stations VRINGs
			 */
			vring = wil_find_tx_bcast_2(wil, skb);
	} else {
		/* unicast, find specific VRING by dest. address */
		vring = wil_find_tx_ucast(wil, skb);
	}
	if (unlikely(!vring)) {
		wil_dbg_txrx(wil, "No Tx VRING found for %pM\n", eth->h_dest);
		goto drop;
	}
	/* set up vring entry */
	rc = wil_tx_vring(wil, vring, skb);

	switch (rc) {
	case 0:
		/* shall we stop net queues? */
		wil_update_net_queues_bh(wil, vring, true);
		/* statistics will be updated on the tx_complete */
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	case -ENOMEM:
		return NETDEV_TX_BUSY;
	default:
		break; /* goto drop; */
	}
 drop:
	ndev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);

	return NET_XMIT_DROP;
}

static inline bool wil_need_txstat(struct sk_buff *skb)
{
	struct ethhdr *eth = (void *)skb->data;

	return is_unicast_ether_addr(eth->h_dest) && skb->sk &&
	       (skb_shinfo(skb)->tx_flags & SKBTX_WIFI_STATUS);
}

static inline void wil_consume_skb(struct sk_buff *skb, bool acked)
{
	if (unlikely(wil_need_txstat(skb)))
		skb_complete_wifi_ack(skb, acked);
	else
		acked ? dev_consume_skb_any(skb) : dev_kfree_skb_any(skb);
}

/**
 * Clean up transmitted skb's from the Tx VRING
 *
 * Return number of descriptors cleared
 *
 * Safe to call from IRQ
 */
int wil_tx_complete(struct wil6210_priv *wil, int ringid)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct device *dev = wil_to_dev(wil);
	struct vring *vring = &wil->vring_tx[ringid];
	struct vring_tx_data *txdata = &wil->vring_tx_data[ringid];
	int done = 0;
	int cid = wil->vring2cid_tid[ringid][0];
	struct wil_net_stats *stats = NULL;
	volatile struct vring_tx_desc *_d;
	int used_before_complete;
	int used_new;

	if (unlikely(!vring->va)) {
		wil_err(wil, "Tx irq[%d]: vring not initialized\n", ringid);
		return 0;
	}

	if (unlikely(!txdata->enabled)) {
		wil_info(wil, "Tx irq[%d]: vring disabled\n", ringid);
		return 0;
	}

	wil_dbg_txrx(wil, "tx_complete: (%d)\n", ringid);

	used_before_complete = wil_vring_used_tx(vring);

	if (cid < WIL6210_MAX_CID)
		stats = &wil->sta[cid].stats;

	while (!wil_vring_is_empty(vring)) {
		int new_swtail;
		struct wil_ctx *ctx = &vring->ctx[vring->swtail];
		/**
		 * For the fragmented skb, HW will set DU bit only for the
		 * last fragment. look for it.
		 * In TSO the first DU will include hdr desc
		 */
		int lf = (vring->swtail + ctx->nr_frags) % vring->size;
		/* TODO: check we are not past head */

		_d = &vring->va[lf].tx;
		if (unlikely(!(_d->dma.status & TX_DMA_STATUS_DU)))
			break;

		new_swtail = (lf + 1) % vring->size;
		while (vring->swtail != new_swtail) {
			struct vring_tx_desc dd, *d = &dd;
			u16 dmalen;
			struct sk_buff *skb;

			ctx = &vring->ctx[vring->swtail];
			skb = ctx->skb;
			_d = &vring->va[vring->swtail].tx;

			*d = *_d;

			dmalen = le16_to_cpu(d->dma.length);
			trace_wil6210_tx_done(ringid, vring->swtail, dmalen,
					      d->dma.error);
			wil_dbg_txrx(wil,
				     "TxC[%2d][%3d] : %d bytes, status 0x%02x err 0x%02x\n",
				     ringid, vring->swtail, dmalen,
				     d->dma.status, d->dma.error);
			wil_hex_dump_txrx("TxCD ", DUMP_PREFIX_NONE, 32, 4,
					  (const void *)d, sizeof(*d), false);

			wil_txdesc_unmap(dev, d, ctx);

			if (skb) {
				if (likely(d->dma.error == 0)) {
					ndev->stats.tx_packets++;
					ndev->stats.tx_bytes += skb->len;
					if (stats) {
						stats->tx_packets++;
						stats->tx_bytes += skb->len;
					}
				} else {
					ndev->stats.tx_errors++;
					if (stats)
						stats->tx_errors++;
				}
				wil_consume_skb(skb, d->dma.error == 0);
			}
			memset(ctx, 0, sizeof(*ctx));
			/* Make sure the ctx is zeroed before updating the tail
			 * to prevent a case where wil_tx_vring will see
			 * this descriptor as used and handle it before ctx zero
			 * is completed.
			 */
			wmb();
			/* There is no need to touch HW descriptor:
			 * - ststus bit TX_DMA_STATUS_DU is set by design,
			 *   so hardware will not try to process this desc.,
			 * - rest of descriptor will be initialized on Tx.
			 */
			vring->swtail = wil_vring_next_tail(vring);
			done++;
		}
	}

	/* performance monitoring */
	used_new = wil_vring_used_tx(vring);
	if (wil_val_in_range(vring_idle_trsh,
			     used_new, used_before_complete)) {
		wil_dbg_txrx(wil, "Ring[%2d] idle %d -> %d\n",
			     ringid, used_before_complete, used_new);
		txdata->last_idle = get_cycles();
	}

	/* shall we wake net queues? */
	if (done)
		wil_update_net_queues(wil, vring, false);

	return done;
}
