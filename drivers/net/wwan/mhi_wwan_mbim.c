// SPDX-License-Identifier: GPL-2.0-or-later
/* MHI MBIM Network driver - Network/MBIM over MHI bus
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
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/mhi.h>
#include <linux/mii.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/u64_stats_sync.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc_ncm.h>
#include <linux/wwan.h>

/* 3500 allows to optimize skb allocation, the skbs will basically fit in
 * one 4K page. Large MBIM packets will simply be split over several MHI
 * transfers and chained by the MHI net layer (zerocopy).
 */
#define MHI_DEFAULT_MRU 3500

#define MHI_MBIM_DEFAULT_MTU 1500
#define MHI_MAX_BUF_SZ 0xffff

#define MBIM_NDP16_SIGN_MASK 0x00ffffff

#define MHI_MBIM_LINK_HASH_SIZE 8
#define LINK_HASH(session) ((session) % MHI_MBIM_LINK_HASH_SIZE)

struct mhi_mbim_link {
	struct mhi_mbim_context *mbim;
	struct net_device *ndev;
	unsigned int session;

	/* stats */
	u64_stats_t rx_packets;
	u64_stats_t rx_bytes;
	u64_stats_t rx_errors;
	u64_stats_t tx_packets;
	u64_stats_t tx_bytes;
	u64_stats_t tx_errors;
	u64_stats_t tx_dropped;
	struct u64_stats_sync tx_syncp;
	struct u64_stats_sync rx_syncp;

	struct hlist_node hlnode;
};

struct mhi_mbim_context {
	struct mhi_device *mdev;
	struct sk_buff *skbagg_head;
	struct sk_buff *skbagg_tail;
	unsigned int mru;
	u32 rx_queue_sz;
	u16 rx_seq;
	u16 tx_seq;
	struct delayed_work rx_refill;
	spinlock_t tx_lock;
	struct hlist_head link_list[MHI_MBIM_LINK_HASH_SIZE];
};

struct mbim_tx_hdr {
	struct usb_cdc_ncm_nth16 nth16;
	struct usb_cdc_ncm_ndp16 ndp16;
	struct usb_cdc_ncm_dpe16 dpe16[2];
} __packed;

static struct mhi_mbim_link *mhi_mbim_get_link_rcu(struct mhi_mbim_context *mbim,
						   unsigned int session)
{
	struct mhi_mbim_link *link;

	hlist_for_each_entry_rcu(link, &mbim->link_list[LINK_HASH(session)], hlnode) {
		if (link->session == session)
			return link;
	}

	return NULL;
}

static struct sk_buff *mbim_tx_fixup(struct sk_buff *skb, unsigned int session,
				     u16 tx_seq)
{
	unsigned int dgram_size = skb->len;
	struct usb_cdc_ncm_nth16 *nth16;
	struct usb_cdc_ncm_ndp16 *ndp16;
	struct mbim_tx_hdr *mbim_hdr;

	/* Only one NDP is sent, containing the IP packet (no aggregation) */

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
	nth16->wSequence = cpu_to_le16(tx_seq);
	nth16->wBlockLength = cpu_to_le16(skb->len);
	nth16->wNdpIndex = cpu_to_le16(sizeof(struct usb_cdc_ncm_nth16));

	/* Fill the unique NDP */
	ndp16 = &mbim_hdr->ndp16;
	ndp16->dwSignature = cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN | (session << 24));
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

static netdev_tx_t mhi_mbim_ndo_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct mhi_mbim_link *link = wwan_netdev_drvpriv(ndev);
	struct mhi_mbim_context *mbim = link->mbim;
	unsigned long flags;
	int err = -ENOMEM;

	/* Serialize MHI channel queuing and MBIM seq */
	spin_lock_irqsave(&mbim->tx_lock, flags);

	skb = mbim_tx_fixup(skb, link->session, mbim->tx_seq);
	if (unlikely(!skb))
		goto exit_unlock;

	err = mhi_queue_skb(mbim->mdev, DMA_TO_DEVICE, skb, skb->len, MHI_EOT);

	if (mhi_queue_is_full(mbim->mdev, DMA_TO_DEVICE))
		netif_stop_queue(ndev);

	if (!err)
		mbim->tx_seq++;

exit_unlock:
	spin_unlock_irqrestore(&mbim->tx_lock, flags);

	if (unlikely(err)) {
		net_err_ratelimited("%s: Failed to queue TX buf (%d)\n",
				    ndev->name, err);
		dev_kfree_skb_any(skb);
		goto exit_drop;
	}

	return NETDEV_TX_OK;

exit_drop:
	u64_stats_update_begin(&link->tx_syncp);
	u64_stats_inc(&link->tx_dropped);
	u64_stats_update_end(&link->tx_syncp);

	return NETDEV_TX_OK;
}

static int mbim_rx_verify_nth16(struct mhi_mbim_context *mbim, struct sk_buff *skb)
{
	struct usb_cdc_ncm_nth16 *nth16;
	int len;

	if (skb->len < sizeof(struct usb_cdc_ncm_nth16) +
			sizeof(struct usb_cdc_ncm_ndp16)) {
		net_err_ratelimited("frame too short\n");
		return -EINVAL;
	}

	nth16 = (struct usb_cdc_ncm_nth16 *)skb->data;

	if (nth16->dwSignature != cpu_to_le32(USB_CDC_NCM_NTH16_SIGN)) {
		net_err_ratelimited("invalid NTH16 signature <%#010x>\n",
				    le32_to_cpu(nth16->dwSignature));
		return -EINVAL;
	}

	/* No limit on the block length, except the size of the data pkt */
	len = le16_to_cpu(nth16->wBlockLength);
	if (len > skb->len) {
		net_err_ratelimited("NTB does not fit into the skb %u/%u\n",
				    len, skb->len);
		return -EINVAL;
	}

	if (mbim->rx_seq + 1 != le16_to_cpu(nth16->wSequence) &&
	    (mbim->rx_seq || le16_to_cpu(nth16->wSequence)) &&
	    !(mbim->rx_seq == 0xffff && !le16_to_cpu(nth16->wSequence))) {
		net_err_ratelimited("sequence number glitch prev=%d curr=%d\n",
				    mbim->rx_seq, le16_to_cpu(nth16->wSequence));
	}
	mbim->rx_seq = le16_to_cpu(nth16->wSequence);

	return le16_to_cpu(nth16->wNdpIndex);
}

static int mbim_rx_verify_ndp16(struct sk_buff *skb, struct usb_cdc_ncm_ndp16 *ndp16)
{
	int ret;

	if (le16_to_cpu(ndp16->wLength) < USB_CDC_NCM_NDP16_LENGTH_MIN) {
		net_err_ratelimited("invalid DPT16 length <%u>\n",
				    le16_to_cpu(ndp16->wLength));
		return -EINVAL;
	}

	ret = ((le16_to_cpu(ndp16->wLength) - sizeof(struct usb_cdc_ncm_ndp16))
			/ sizeof(struct usb_cdc_ncm_dpe16));
	ret--; /* Last entry is always a NULL terminator */

	if (sizeof(struct usb_cdc_ncm_ndp16) +
	     ret * sizeof(struct usb_cdc_ncm_dpe16) > skb->len) {
		net_err_ratelimited("Invalid nframes = %d\n", ret);
		return -EINVAL;
	}

	return ret;
}

static void mhi_mbim_rx(struct mhi_mbim_context *mbim, struct sk_buff *skb)
{
	int ndpoffset;

	/* Check NTB header and retrieve first NDP offset */
	ndpoffset = mbim_rx_verify_nth16(mbim, skb);
	if (ndpoffset < 0) {
		net_err_ratelimited("mbim: Incorrect NTB header\n");
		goto error;
	}

	/* Process each NDP */
	while (1) {
		struct usb_cdc_ncm_ndp16 ndp16;
		struct usb_cdc_ncm_dpe16 dpe16;
		struct mhi_mbim_link *link;
		int nframes, n, dpeoffset;
		unsigned int session;

		if (skb_copy_bits(skb, ndpoffset, &ndp16, sizeof(ndp16))) {
			net_err_ratelimited("mbim: Incorrect NDP offset (%u)\n",
					    ndpoffset);
			goto error;
		}

		/* Check NDP header and retrieve number of datagrams */
		nframes = mbim_rx_verify_ndp16(skb, &ndp16);
		if (nframes < 0) {
			net_err_ratelimited("mbim: Incorrect NDP16\n");
			goto error;
		}

		 /* Only IP data type supported, no DSS in MHI context */
		if ((ndp16.dwSignature & cpu_to_le32(MBIM_NDP16_SIGN_MASK))
				!= cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN)) {
			net_err_ratelimited("mbim: Unsupported NDP type\n");
			goto next_ndp;
		}

		session = (le32_to_cpu(ndp16.dwSignature) & ~MBIM_NDP16_SIGN_MASK) >> 24;

		rcu_read_lock();

		link = mhi_mbim_get_link_rcu(mbim, session);
		if (!link) {
			net_err_ratelimited("mbim: bad packet session (%u)\n", session);
			goto unlock;
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

			skbn = netdev_alloc_skb(link->ndev, dgram_len);
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
						    link->ndev->name);
				dev_kfree_skb_any(skbn);
				u64_stats_update_begin(&link->rx_syncp);
				u64_stats_inc(&link->rx_errors);
				u64_stats_update_end(&link->rx_syncp);
				continue;
			}

			u64_stats_update_begin(&link->rx_syncp);
			u64_stats_inc(&link->rx_packets);
			u64_stats_add(&link->rx_bytes, skbn->len);
			u64_stats_update_end(&link->rx_syncp);

			netif_rx(skbn);
		}
unlock:
		rcu_read_unlock();
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

static struct sk_buff *mhi_net_skb_agg(struct mhi_mbim_context *mbim,
				       struct sk_buff *skb)
{
	struct sk_buff *head = mbim->skbagg_head;
	struct sk_buff *tail = mbim->skbagg_tail;

	/* This is non-paged skb chaining using frag_list */
	if (!head) {
		mbim->skbagg_head = skb;
		return skb;
	}

	if (!skb_shinfo(head)->frag_list)
		skb_shinfo(head)->frag_list = skb;
	else
		tail->next = skb;

	head->len += skb->len;
	head->data_len += skb->len;
	head->truesize += skb->truesize;

	mbim->skbagg_tail = skb;

	return mbim->skbagg_head;
}

static void mhi_net_rx_refill_work(struct work_struct *work)
{
	struct mhi_mbim_context *mbim = container_of(work, struct mhi_mbim_context,
						     rx_refill.work);
	struct mhi_device *mdev = mbim->mdev;
	int err;

	while (!mhi_queue_is_full(mdev, DMA_FROM_DEVICE)) {
		struct sk_buff *skb = alloc_skb(mbim->mru, GFP_KERNEL);

		if (unlikely(!skb))
			break;

		err = mhi_queue_skb(mdev, DMA_FROM_DEVICE, skb,
				    mbim->mru, MHI_EOT);
		if (unlikely(err)) {
			kfree_skb(skb);
			break;
		}

		/* Do not hog the CPU if rx buffers are consumed faster than
		 * queued (unlikely).
		 */
		cond_resched();
	}

	/* If we're still starved of rx buffers, reschedule later */
	if (mhi_get_free_desc_count(mdev, DMA_FROM_DEVICE) == mbim->rx_queue_sz)
		schedule_delayed_work(&mbim->rx_refill, HZ / 2);
}

static void mhi_mbim_dl_callback(struct mhi_device *mhi_dev,
				 struct mhi_result *mhi_res)
{
	struct mhi_mbim_context *mbim = dev_get_drvdata(&mhi_dev->dev);
	struct sk_buff *skb = mhi_res->buf_addr;
	int free_desc_count;

	free_desc_count = mhi_get_free_desc_count(mhi_dev, DMA_FROM_DEVICE);

	if (unlikely(mhi_res->transaction_status)) {
		switch (mhi_res->transaction_status) {
		case -EOVERFLOW:
			/* Packet has been split over multiple transfers */
			skb_put(skb, mhi_res->bytes_xferd);
			mhi_net_skb_agg(mbim, skb);
			break;
		case -ENOTCONN:
			/* MHI layer stopping/resetting the DL channel */
			dev_kfree_skb_any(skb);
			return;
		default:
			/* Unknown error, simply drop */
			dev_kfree_skb_any(skb);
		}
	} else {
		skb_put(skb, mhi_res->bytes_xferd);

		if (mbim->skbagg_head) {
			/* Aggregate the final fragment */
			skb = mhi_net_skb_agg(mbim, skb);
			mbim->skbagg_head = NULL;
		}

		mhi_mbim_rx(mbim, skb);
	}

	/* Refill if RX buffers queue becomes low */
	if (free_desc_count >= mbim->rx_queue_sz / 2)
		schedule_delayed_work(&mbim->rx_refill, 0);
}

static void mhi_mbim_ndo_get_stats64(struct net_device *ndev,
				     struct rtnl_link_stats64 *stats)
{
	struct mhi_mbim_link *link = wwan_netdev_drvpriv(ndev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin_irq(&link->rx_syncp);
		stats->rx_packets = u64_stats_read(&link->rx_packets);
		stats->rx_bytes = u64_stats_read(&link->rx_bytes);
		stats->rx_errors = u64_stats_read(&link->rx_errors);
	} while (u64_stats_fetch_retry_irq(&link->rx_syncp, start));

	do {
		start = u64_stats_fetch_begin_irq(&link->tx_syncp);
		stats->tx_packets = u64_stats_read(&link->tx_packets);
		stats->tx_bytes = u64_stats_read(&link->tx_bytes);
		stats->tx_errors = u64_stats_read(&link->tx_errors);
		stats->tx_dropped = u64_stats_read(&link->tx_dropped);
	} while (u64_stats_fetch_retry_irq(&link->tx_syncp, start));
}

static void mhi_mbim_ul_callback(struct mhi_device *mhi_dev,
				 struct mhi_result *mhi_res)
{
	struct mhi_mbim_context *mbim = dev_get_drvdata(&mhi_dev->dev);
	struct sk_buff *skb = mhi_res->buf_addr;
	struct net_device *ndev = skb->dev;
	struct mhi_mbim_link *link = wwan_netdev_drvpriv(ndev);

	/* Hardware has consumed the buffer, so free the skb (which is not
	 * freed by the MHI stack) and perform accounting.
	 */
	dev_consume_skb_any(skb);

	u64_stats_update_begin(&link->tx_syncp);
	if (unlikely(mhi_res->transaction_status)) {
		/* MHI layer stopping/resetting the UL channel */
		if (mhi_res->transaction_status == -ENOTCONN) {
			u64_stats_update_end(&link->tx_syncp);
			return;
		}

		u64_stats_inc(&link->tx_errors);
	} else {
		u64_stats_inc(&link->tx_packets);
		u64_stats_add(&link->tx_bytes, mhi_res->bytes_xferd);
	}
	u64_stats_update_end(&link->tx_syncp);

	if (netif_queue_stopped(ndev) && !mhi_queue_is_full(mbim->mdev, DMA_TO_DEVICE))
		netif_wake_queue(ndev);
}

static int mhi_mbim_ndo_open(struct net_device *ndev)
{
	struct mhi_mbim_link *link = wwan_netdev_drvpriv(ndev);

	/* Feed the MHI rx buffer pool */
	schedule_delayed_work(&link->mbim->rx_refill, 0);

	/* Carrier is established via out-of-band channel (e.g. qmi) */
	netif_carrier_on(ndev);

	netif_start_queue(ndev);

	return 0;
}

static int mhi_mbim_ndo_stop(struct net_device *ndev)
{
	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	return 0;
}

static const struct net_device_ops mhi_mbim_ndo = {
	.ndo_open = mhi_mbim_ndo_open,
	.ndo_stop = mhi_mbim_ndo_stop,
	.ndo_start_xmit = mhi_mbim_ndo_xmit,
	.ndo_get_stats64 = mhi_mbim_ndo_get_stats64,
};

static int mhi_mbim_newlink(void *ctxt, struct net_device *ndev, u32 if_id,
			    struct netlink_ext_ack *extack)
{
	struct mhi_mbim_link *link = wwan_netdev_drvpriv(ndev);
	struct mhi_mbim_context *mbim = ctxt;

	link->session = if_id;
	link->mbim = mbim;
	link->ndev = ndev;
	u64_stats_init(&link->rx_syncp);
	u64_stats_init(&link->tx_syncp);

	rcu_read_lock();
	if (mhi_mbim_get_link_rcu(mbim, if_id)) {
		rcu_read_unlock();
		return -EEXIST;
	}
	rcu_read_unlock();

	/* Already protected by RTNL lock */
	hlist_add_head_rcu(&link->hlnode, &mbim->link_list[LINK_HASH(if_id)]);

	return register_netdevice(ndev);
}

static void mhi_mbim_dellink(void *ctxt, struct net_device *ndev,
			     struct list_head *head)
{
	struct mhi_mbim_link *link = wwan_netdev_drvpriv(ndev);

	hlist_del_init_rcu(&link->hlnode);
	synchronize_rcu();

	unregister_netdevice_queue(ndev, head);
}

static void mhi_mbim_setup(struct net_device *ndev)
{
	ndev->header_ops = NULL;  /* No header */
	ndev->type = ARPHRD_RAWIP;
	ndev->needed_headroom = sizeof(struct mbim_tx_hdr);
	ndev->hard_header_len = 0;
	ndev->addr_len = 0;
	ndev->flags = IFF_POINTOPOINT | IFF_NOARP;
	ndev->netdev_ops = &mhi_mbim_ndo;
	ndev->mtu = MHI_MBIM_DEFAULT_MTU;
	ndev->min_mtu = ETH_MIN_MTU;
	ndev->max_mtu = MHI_MAX_BUF_SZ - ndev->needed_headroom;
	ndev->tx_queue_len = 1000;
	ndev->needs_free_netdev = true;
}

static const struct wwan_ops mhi_mbim_wwan_ops = {
	.priv_size = sizeof(struct mhi_mbim_link),
	.setup = mhi_mbim_setup,
	.newlink = mhi_mbim_newlink,
	.dellink = mhi_mbim_dellink,
};

static int mhi_mbim_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct mhi_controller *cntrl = mhi_dev->mhi_cntrl;
	struct mhi_mbim_context *mbim;
	int err;

	mbim = devm_kzalloc(&mhi_dev->dev, sizeof(*mbim), GFP_KERNEL);
	if (!mbim)
		return -ENOMEM;

	spin_lock_init(&mbim->tx_lock);
	dev_set_drvdata(&mhi_dev->dev, mbim);
	mbim->mdev = mhi_dev;
	mbim->mru = mhi_dev->mhi_cntrl->mru ? mhi_dev->mhi_cntrl->mru : MHI_DEFAULT_MRU;

	INIT_DELAYED_WORK(&mbim->rx_refill, mhi_net_rx_refill_work);

	/* Start MHI channels */
	err = mhi_prepare_for_transfer(mhi_dev);
	if (err)
		return err;

	/* Number of transfer descriptors determines size of the queue */
	mbim->rx_queue_sz = mhi_get_free_desc_count(mhi_dev, DMA_FROM_DEVICE);

	/* Register wwan link ops with MHI controller representing WWAN instance */
	return wwan_register_ops(&cntrl->mhi_dev->dev, &mhi_mbim_wwan_ops, mbim, 0);
}

static void mhi_mbim_remove(struct mhi_device *mhi_dev)
{
	struct mhi_mbim_context *mbim = dev_get_drvdata(&mhi_dev->dev);
	struct mhi_controller *cntrl = mhi_dev->mhi_cntrl;

	mhi_unprepare_from_transfer(mhi_dev);
	cancel_delayed_work_sync(&mbim->rx_refill);
	wwan_unregister_ops(&cntrl->mhi_dev->dev);
	kfree_skb(mbim->skbagg_head);
	dev_set_drvdata(&mhi_dev->dev, NULL);
}

static const struct mhi_device_id mhi_mbim_id_table[] = {
	/* Hardware accelerated data PATH (to modem IPA), MBIM protocol */
	{ .chan = "IP_HW0_MBIM", .driver_data = 0 },
	{}
};
MODULE_DEVICE_TABLE(mhi, mhi_mbim_id_table);

static struct mhi_driver mhi_mbim_driver = {
	.probe = mhi_mbim_probe,
	.remove = mhi_mbim_remove,
	.dl_xfer_cb = mhi_mbim_dl_callback,
	.ul_xfer_cb = mhi_mbim_ul_callback,
	.id_table = mhi_mbim_id_table,
	.driver = {
		.name = "mhi_wwan_mbim",
		.owner = THIS_MODULE,
	},
};

module_mhi_driver(mhi_mbim_driver);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("Network/MBIM over MHI");
MODULE_LICENSE("GPL v2");
