// SPDX-License-Identifier: GPL-2.0-or-later
/* MHI Network driver - Network over MHI bus
 *
 * Copyright (C) 2021 Linaro Ltd <loic.poulain@linaro.org>
 *
 * This driver copy some code from cdc_ncm, which is:
 * Copyright (C) ST-Ericsson 2010-2012
 * and cdc_mbim, which is:
 * Copyright (c) 2012  Smith Micro Software, Inc.
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 */

#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/wwan.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc_ncm.h>

#include "mhi.h"

#define MBIM_NDP16_SIGN_MASK 0x00ffffff

/* Usual WWAN MTU */
#define MHI_MBIM_DEFAULT_MTU 1500

/* 3500 allows to optimize skb allocation, the skbs will basically fit in
 * one 4K page. Large MBIM packets will simply be split over several MHI
 * transfers and chained by the MHI net layer (zerocopy).
 */
#define MHI_MBIM_DEFAULT_MRU 3500

struct mbim_context {
	u16 rx_seq;
	u16 tx_seq;
};

static void __mbim_length_errors_inc(struct mhi_net_dev *dev)
{
	u64_stats_update_begin(&dev->stats.rx_syncp);
	u64_stats_inc(&dev->stats.rx_length_errors);
	u64_stats_update_end(&dev->stats.rx_syncp);
}

static void __mbim_errors_inc(struct mhi_net_dev *dev)
{
	u64_stats_update_begin(&dev->stats.rx_syncp);
	u64_stats_inc(&dev->stats.rx_errors);
	u64_stats_update_end(&dev->stats.rx_syncp);
}

static int mbim_rx_verify_nth16(struct sk_buff *skb)
{
	struct mhi_net_dev *dev = wwan_netdev_drvpriv(skb->dev);
	struct mbim_context *ctx = dev->proto_data;
	struct usb_cdc_ncm_nth16 *nth16;
	int len;

	if (skb->len < sizeof(struct usb_cdc_ncm_nth16) +
			sizeof(struct usb_cdc_ncm_ndp16)) {
		netif_dbg(dev, rx_err, dev->ndev, "frame too short\n");
		__mbim_length_errors_inc(dev);
		return -EINVAL;
	}

	nth16 = (struct usb_cdc_ncm_nth16 *)skb->data;

	if (nth16->dwSignature != cpu_to_le32(USB_CDC_NCM_NTH16_SIGN)) {
		netif_dbg(dev, rx_err, dev->ndev,
			  "invalid NTH16 signature <%#010x>\n",
			  le32_to_cpu(nth16->dwSignature));
		__mbim_errors_inc(dev);
		return -EINVAL;
	}

	/* No limit on the block length, except the size of the data pkt */
	len = le16_to_cpu(nth16->wBlockLength);
	if (len > skb->len) {
		netif_dbg(dev, rx_err, dev->ndev,
			  "NTB does not fit into the skb %u/%u\n", len,
			  skb->len);
		__mbim_length_errors_inc(dev);
		return -EINVAL;
	}

	if (ctx->rx_seq + 1 != le16_to_cpu(nth16->wSequence) &&
	    (ctx->rx_seq || le16_to_cpu(nth16->wSequence)) &&
	    !(ctx->rx_seq == 0xffff && !le16_to_cpu(nth16->wSequence))) {
		netif_dbg(dev, rx_err, dev->ndev,
			  "sequence number glitch prev=%d curr=%d\n",
			  ctx->rx_seq, le16_to_cpu(nth16->wSequence));
	}
	ctx->rx_seq = le16_to_cpu(nth16->wSequence);

	return le16_to_cpu(nth16->wNdpIndex);
}

static int mbim_rx_verify_ndp16(struct sk_buff *skb, struct usb_cdc_ncm_ndp16 *ndp16)
{
	struct mhi_net_dev *dev = wwan_netdev_drvpriv(skb->dev);
	int ret;

	if (le16_to_cpu(ndp16->wLength) < USB_CDC_NCM_NDP16_LENGTH_MIN) {
		netif_dbg(dev, rx_err, dev->ndev, "invalid DPT16 length <%u>\n",
			  le16_to_cpu(ndp16->wLength));
		return -EINVAL;
	}

	ret = ((le16_to_cpu(ndp16->wLength) - sizeof(struct usb_cdc_ncm_ndp16))
			/ sizeof(struct usb_cdc_ncm_dpe16));
	ret--; /* Last entry is always a NULL terminator */

	if (sizeof(struct usb_cdc_ncm_ndp16) +
	     ret * sizeof(struct usb_cdc_ncm_dpe16) > skb->len) {
		netif_dbg(dev, rx_err, dev->ndev,
			  "Invalid nframes = %d\n", ret);
		return -EINVAL;
	}

	return ret;
}

static void mbim_rx(struct mhi_net_dev *mhi_netdev, struct sk_buff *skb)
{
	struct net_device *ndev = mhi_netdev->ndev;
	int ndpoffset;

	/* Check NTB header and retrieve first NDP offset */
	ndpoffset = mbim_rx_verify_nth16(skb);
	if (ndpoffset < 0) {
		net_err_ratelimited("%s: Incorrect NTB header\n", ndev->name);
		goto error;
	}

	/* Process each NDP */
	while (1) {
		struct usb_cdc_ncm_ndp16 ndp16;
		struct usb_cdc_ncm_dpe16 dpe16;
		int nframes, n, dpeoffset;

		if (skb_copy_bits(skb, ndpoffset, &ndp16, sizeof(ndp16))) {
			net_err_ratelimited("%s: Incorrect NDP offset (%u)\n",
					    ndev->name, ndpoffset);
			__mbim_length_errors_inc(mhi_netdev);
			goto error;
		}

		/* Check NDP header and retrieve number of datagrams */
		nframes = mbim_rx_verify_ndp16(skb, &ndp16);
		if (nframes < 0) {
			net_err_ratelimited("%s: Incorrect NDP16\n", ndev->name);
			__mbim_length_errors_inc(mhi_netdev);
			goto error;
		}

		 /* Only IP data type supported, no DSS in MHI context */
		if ((ndp16.dwSignature & cpu_to_le32(MBIM_NDP16_SIGN_MASK))
				!= cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN)) {
			net_err_ratelimited("%s: Unsupported NDP type\n", ndev->name);
			__mbim_errors_inc(mhi_netdev);
			goto next_ndp;
		}

		/* Only primary IP session 0 (0x00) supported for now */
		if (ndp16.dwSignature & ~cpu_to_le32(MBIM_NDP16_SIGN_MASK)) {
			net_err_ratelimited("%s: bad packet session\n", ndev->name);
			__mbim_errors_inc(mhi_netdev);
			goto next_ndp;
		}

		/* de-aggregate and deliver IP packets */
		dpeoffset = ndpoffset + sizeof(struct usb_cdc_ncm_ndp16);
		for (n = 0; n < nframes; n++, dpeoffset += sizeof(dpe16)) {
			u16 dgram_offset, dgram_len;
			struct sk_buff *skbn;

			if (skb_copy_bits(skb, dpeoffset, &dpe16, sizeof(dpe16)))
				break;

			dgram_offset = le16_to_cpu(dpe16.wDatagramIndex);
			dgram_len = le16_to_cpu(dpe16.wDatagramLength);

			if (!dgram_offset || !dgram_len)
				break; /* null terminator */

			skbn = netdev_alloc_skb(ndev, dgram_len);
			if (!skbn)
				continue;

			skb_put(skbn, dgram_len);
			skb_copy_bits(skb, dgram_offset, skbn->data, dgram_len);

			switch (skbn->data[0] & 0xf0) {
			case 0x40:
				skbn->protocol = htons(ETH_P_IP);
				break;
			case 0x60:
				skbn->protocol = htons(ETH_P_IPV6);
				break;
			default:
				net_err_ratelimited("%s: unknown protocol\n",
						    ndev->name);
				__mbim_errors_inc(mhi_netdev);
				dev_kfree_skb_any(skbn);
				continue;
			}

			netif_rx(skbn);
		}
next_ndp:
		/* Other NDP to process? */
		ndpoffset = (int)le16_to_cpu(ndp16.wNextNdpIndex);
		if (!ndpoffset)
			break;
	}

	/* free skb */
	dev_consume_skb_any(skb);
	return;
error:
	dev_kfree_skb_any(skb);
}

struct mbim_tx_hdr {
	struct usb_cdc_ncm_nth16 nth16;
	struct usb_cdc_ncm_ndp16 ndp16;
	struct usb_cdc_ncm_dpe16 dpe16[2];
} __packed;

static struct sk_buff *mbim_tx_fixup(struct mhi_net_dev *mhi_netdev,
				     struct sk_buff *skb)
{
	struct mbim_context *ctx = mhi_netdev->proto_data;
	unsigned int dgram_size = skb->len;
	struct usb_cdc_ncm_nth16 *nth16;
	struct usb_cdc_ncm_ndp16 *ndp16;
	struct mbim_tx_hdr *mbim_hdr;

	/* For now, this is a partial implementation of CDC MBIM, only one NDP
	 * is sent, containing the IP packet (no aggregation).
	 */

	/* Ensure we have enough headroom for crafting MBIM header */
	if (skb_cow_head(skb, sizeof(struct mbim_tx_hdr))) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	mbim_hdr = skb_push(skb, sizeof(struct mbim_tx_hdr));

	/* Fill NTB header */
	nth16 = &mbim_hdr->nth16;
	nth16->dwSignature = cpu_to_le32(USB_CDC_NCM_NTH16_SIGN);
	nth16->wHeaderLength = cpu_to_le16(sizeof(struct usb_cdc_ncm_nth16));
	nth16->wSequence = cpu_to_le16(ctx->tx_seq++);
	nth16->wBlockLength = cpu_to_le16(skb->len);
	nth16->wNdpIndex = cpu_to_le16(sizeof(struct usb_cdc_ncm_nth16));

	/* Fill the unique NDP */
	ndp16 = &mbim_hdr->ndp16;
	ndp16->dwSignature = cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN);
	ndp16->wLength = cpu_to_le16(sizeof(struct usb_cdc_ncm_ndp16)
					+ sizeof(struct usb_cdc_ncm_dpe16) * 2);
	ndp16->wNextNdpIndex = 0;

	/* Datagram follows the mbim header */
	ndp16->dpe16[0].wDatagramIndex = cpu_to_le16(sizeof(struct mbim_tx_hdr));
	ndp16->dpe16[0].wDatagramLength = cpu_to_le16(dgram_size);

	/* null termination */
	ndp16->dpe16[1].wDatagramIndex = 0;
	ndp16->dpe16[1].wDatagramLength = 0;

	return skb;
}

static int mbim_init(struct mhi_net_dev *mhi_netdev)
{
	struct net_device *ndev = mhi_netdev->ndev;

	mhi_netdev->proto_data = devm_kzalloc(&ndev->dev,
					      sizeof(struct mbim_context),
					      GFP_KERNEL);
	if (!mhi_netdev->proto_data)
		return -ENOMEM;

	ndev->needed_headroom = sizeof(struct mbim_tx_hdr);
	ndev->mtu = MHI_MBIM_DEFAULT_MTU;
	mhi_netdev->mru = MHI_MBIM_DEFAULT_MRU;

	return 0;
}

const struct mhi_net_proto proto_mbim = {
	.init = mbim_init,
	.rx = mbim_rx,
	.tx_fixup = mbim_tx_fixup,
};
