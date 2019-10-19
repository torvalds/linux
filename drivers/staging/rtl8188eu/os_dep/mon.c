// SPDX-License-Identifier: GPL-2.0
/*
 * RTL8188EU monitor interface
 *
 * Copyright (C) 2015 Jakub Sitnicki
 */

#include <linux/ieee80211.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

#include <drv_types.h>
#include <rtw_recv.h>
#include <rtw_xmit.h>
#include <mon.h>

/**
 * unprotect_frame() - unset Protected flag and strip off IV and ICV/MIC
 */
static void unprotect_frame(struct sk_buff *skb, int iv_len, int icv_len)
{
	struct ieee80211_hdr *hdr;
	int hdr_len;

	hdr = (struct ieee80211_hdr *)skb->data;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if (skb->len < hdr_len + iv_len + icv_len)
		return;
	if (!ieee80211_has_protected(hdr->frame_control))
		return;

	hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_PROTECTED);

	memmove(skb->data + iv_len, skb->data, hdr_len);
	skb_pull(skb, iv_len);
	skb_trim(skb, skb->len - icv_len);
}

static void mon_recv_decrypted(struct net_device *dev, const u8 *data,
			       int data_len, int iv_len, int icv_len)
{
	struct sk_buff *skb;

	skb = netdev_alloc_skb(dev, data_len);
	if (!skb)
		return;
	skb_put_data(skb, data, data_len);

	/*
	 * Frame data is not encrypted. Strip off protection so
	 * userspace doesn't think that it is.
	 */
	unprotect_frame(skb, iv_len, icv_len);

	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
}

static void mon_recv_encrypted(struct net_device *dev, const u8 *data,
			       int data_len)
{
	if (net_ratelimit())
		netdev_info(dev, "Encrypted packets are not supported");
}

/**
 * rtl88eu_mon_recv_hook() - forward received frame to the monitor interface
 *
 * Assumes that the frame contains an IV and an ICV/MIC, and that
 * encrypt field in frame->attrib have been set accordingly.
 */
void rtl88eu_mon_recv_hook(struct net_device *dev, struct recv_frame *frame)
{
	struct rx_pkt_attrib *attr;
	int iv_len, icv_len;
	int data_len;
	u8 *data;

	if (!dev || !frame)
		return;
	if (!netif_running(dev))
		return;

	attr = &frame->attrib;
	data = frame->pkt->data;
	data_len = frame->pkt->len;

	/* Broadcast and multicast frames don't have attr->{iv,icv}_len set */
	SET_ICE_IV_LEN(iv_len, icv_len, attr->encrypt);

	if (attr->bdecrypted)
		mon_recv_decrypted(dev, data, data_len, iv_len, icv_len);
	else
		mon_recv_encrypted(dev, data, data_len);
}

/**
 * rtl88eu_mon_xmit_hook() - forward trasmitted frame to the monitor interface
 *
 * Assumes that:
 * - frame header contains an IV and frame->attrib.iv_len is set accordingly,
 * - data is not encrypted and ICV/MIC has not been appended yet.
 */
void rtl88eu_mon_xmit_hook(struct net_device *dev, struct xmit_frame *frame,
			   uint frag_len)
{
	struct pkt_attrib *attr;
	u8 *data;
	int i, offset;

	if (!dev || !frame)
		return;
	if (!netif_running(dev))
		return;

	attr = &frame->attrib;

	offset = TXDESC_SIZE + frame->pkt_offset * PACKET_OFFSET_SZ;
	data = frame->buf_addr + offset;

	for (i = 0; i < attr->nr_frags - 1; i++) {
		mon_recv_decrypted(dev, data, frag_len, attr->iv_len, 0);
		data += frag_len;
		data = (u8 *)round_up((size_t)data, 4);
	}
	/* Last fragment has different length */
	mon_recv_decrypted(dev, data, attr->last_txcmdsz, attr->iv_len, 0);
}

static netdev_tx_t mon_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops mon_netdev_ops = {
	.ndo_start_xmit		= mon_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void mon_setup(struct net_device *dev)
{
	dev->netdev_ops = &mon_netdev_ops;
	dev->needs_free_netdev = true;
	ether_setup(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->type = ARPHRD_IEEE80211;
	/*
	 * Use a locally administered address (IEEE 802)
	 * XXX: Copied from mac80211_hwsim driver. Revisit.
	 */
	eth_zero_addr(dev->dev_addr);
	dev->dev_addr[0] = 0x12;
}

struct net_device *rtl88eu_mon_init(void)
{
	struct net_device *dev;
	int err;

	dev = alloc_netdev(0, "mon%d", NET_NAME_UNKNOWN, mon_setup);
	if (!dev)
		goto fail;

	err = register_netdev(dev);
	if (err < 0)
		goto fail_free_dev;

	return dev;

fail_free_dev:
	free_netdev(dev);
fail:
	return NULL;
}

void rtl88eu_mon_deinit(struct net_device *dev)
{
	if (!dev)
		return;

	unregister_netdev(dev);
}
