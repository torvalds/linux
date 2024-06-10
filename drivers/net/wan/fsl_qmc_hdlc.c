// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale QMC HDLC Device Driver
 *
 * Copyright 2023 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <linux/array_size.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/bitmap.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/framer/framer.h>
#include <linux/hdlc.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <soc/fsl/qe/qmc.h>

struct qmc_hdlc_desc {
	struct net_device *netdev;
	struct sk_buff *skb; /* NULL if the descriptor is not in use */
	dma_addr_t dma_addr;
	size_t dma_size;
};

struct qmc_hdlc {
	struct device *dev;
	struct qmc_chan *qmc_chan;
	struct net_device *netdev;
	struct framer *framer;
	spinlock_t carrier_lock; /* Protect carrier detection */
	struct notifier_block nb;
	bool is_crc32;
	spinlock_t tx_lock; /* Protect tx descriptors */
	struct qmc_hdlc_desc tx_descs[8];
	unsigned int tx_out;
	struct qmc_hdlc_desc rx_descs[4];
	u32 slot_map;
};

static struct qmc_hdlc *netdev_to_qmc_hdlc(struct net_device *netdev)
{
	return dev_to_hdlc(netdev)->priv;
}

static int qmc_hdlc_framer_set_carrier(struct qmc_hdlc *qmc_hdlc)
{
	struct framer_status framer_status;
	int ret;

	if (!qmc_hdlc->framer)
		return 0;

	guard(spinlock_irqsave)(&qmc_hdlc->carrier_lock);

	ret = framer_get_status(qmc_hdlc->framer, &framer_status);
	if (ret) {
		dev_err(qmc_hdlc->dev, "get framer status failed (%d)\n", ret);
		return ret;
	}
	if (framer_status.link_is_on)
		netif_carrier_on(qmc_hdlc->netdev);
	else
		netif_carrier_off(qmc_hdlc->netdev);

	return 0;
}

static int qmc_hdlc_framer_notifier(struct notifier_block *nb, unsigned long action,
				    void *data)
{
	struct qmc_hdlc *qmc_hdlc = container_of(nb, struct qmc_hdlc, nb);
	int ret;

	if (action != FRAMER_EVENT_STATUS)
		return NOTIFY_DONE;

	ret = qmc_hdlc_framer_set_carrier(qmc_hdlc);
	return ret ? NOTIFY_DONE : NOTIFY_OK;
}

static int qmc_hdlc_framer_start(struct qmc_hdlc *qmc_hdlc)
{
	struct framer_status framer_status;
	int ret;

	if (!qmc_hdlc->framer)
		return 0;

	ret = framer_power_on(qmc_hdlc->framer);
	if (ret) {
		dev_err(qmc_hdlc->dev, "framer power-on failed (%d)\n", ret);
		return ret;
	}

	/* Be sure that get_status is supported */
	ret = framer_get_status(qmc_hdlc->framer, &framer_status);
	if (ret) {
		dev_err(qmc_hdlc->dev, "get framer status failed (%d)\n", ret);
		goto framer_power_off;
	}

	qmc_hdlc->nb.notifier_call = qmc_hdlc_framer_notifier;
	ret = framer_notifier_register(qmc_hdlc->framer, &qmc_hdlc->nb);
	if (ret) {
		dev_err(qmc_hdlc->dev, "framer notifier register failed (%d)\n", ret);
		goto framer_power_off;
	}

	return 0;

framer_power_off:
	framer_power_off(qmc_hdlc->framer);
	return ret;
}

static void qmc_hdlc_framer_stop(struct qmc_hdlc *qmc_hdlc)
{
	if (!qmc_hdlc->framer)
		return;

	framer_notifier_unregister(qmc_hdlc->framer, &qmc_hdlc->nb);
	framer_power_off(qmc_hdlc->framer);
}

static int qmc_hdlc_framer_set_iface(struct qmc_hdlc *qmc_hdlc, int if_iface,
				     const te1_settings *te1)
{
	struct framer_config config;
	int ret;

	if (!qmc_hdlc->framer)
		return 0;

	ret = framer_get_config(qmc_hdlc->framer, &config);
	if (ret)
		return ret;

	switch (if_iface) {
	case IF_IFACE_E1:
		config.iface = FRAMER_IFACE_E1;
		break;
	case IF_IFACE_T1:
		config.iface = FRAMER_IFACE_T1;
		break;
	default:
		return -EINVAL;
	}

	switch (te1->clock_type) {
	case CLOCK_DEFAULT:
		/* Keep current value */
		break;
	case CLOCK_EXT:
		config.clock_type = FRAMER_CLOCK_EXT;
		break;
	case CLOCK_INT:
		config.clock_type = FRAMER_CLOCK_INT;
		break;
	default:
		return -EINVAL;
	}
	config.line_clock_rate = te1->clock_rate;

	return framer_set_config(qmc_hdlc->framer, &config);
}

static int qmc_hdlc_framer_get_iface(struct qmc_hdlc *qmc_hdlc, int *if_iface, te1_settings *te1)
{
	struct framer_config config;
	int ret;

	if (!qmc_hdlc->framer) {
		*if_iface = IF_IFACE_E1;
		return 0;
	}

	ret = framer_get_config(qmc_hdlc->framer, &config);
	if (ret)
		return ret;

	switch (config.iface) {
	case FRAMER_IFACE_E1:
		*if_iface = IF_IFACE_E1;
		break;
	case FRAMER_IFACE_T1:
		*if_iface = IF_IFACE_T1;
		break;
	}

	if (!te1)
		return 0; /* Only iface type requested */

	switch (config.clock_type) {
	case FRAMER_CLOCK_EXT:
		te1->clock_type = CLOCK_EXT;
		break;
	case FRAMER_CLOCK_INT:
		te1->clock_type = CLOCK_INT;
		break;
	default:
		return -EINVAL;
	}
	te1->clock_rate = config.line_clock_rate;
	return 0;
}

static int qmc_hdlc_framer_init(struct qmc_hdlc *qmc_hdlc)
{
	int ret;

	if (!qmc_hdlc->framer)
		return 0;

	ret = framer_init(qmc_hdlc->framer);
	if (ret) {
		dev_err(qmc_hdlc->dev, "framer init failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void qmc_hdlc_framer_exit(struct qmc_hdlc *qmc_hdlc)
{
	if (!qmc_hdlc->framer)
		return;

	framer_exit(qmc_hdlc->framer);
}

static int qmc_hdlc_recv_queue(struct qmc_hdlc *qmc_hdlc, struct qmc_hdlc_desc *desc, size_t size);

#define QMC_HDLC_RX_ERROR_FLAGS				\
	(QMC_RX_FLAG_HDLC_OVF | QMC_RX_FLAG_HDLC_UNA |	\
	 QMC_RX_FLAG_HDLC_CRC | QMC_RX_FLAG_HDLC_ABORT)

static void qmc_hcld_recv_complete(void *context, size_t length, unsigned int flags)
{
	struct qmc_hdlc_desc *desc = context;
	struct net_device *netdev;
	struct qmc_hdlc *qmc_hdlc;
	int ret;

	netdev = desc->netdev;
	qmc_hdlc = netdev_to_qmc_hdlc(netdev);

	dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size, DMA_FROM_DEVICE);

	if (flags & QMC_HDLC_RX_ERROR_FLAGS) {
		netdev->stats.rx_errors++;
		if (flags & QMC_RX_FLAG_HDLC_OVF) /* Data overflow */
			netdev->stats.rx_over_errors++;
		if (flags & QMC_RX_FLAG_HDLC_UNA) /* bits received not multiple of 8 */
			netdev->stats.rx_frame_errors++;
		if (flags & QMC_RX_FLAG_HDLC_ABORT) /* Received an abort sequence */
			netdev->stats.rx_frame_errors++;
		if (flags & QMC_RX_FLAG_HDLC_CRC) /* CRC error */
			netdev->stats.rx_crc_errors++;
		kfree_skb(desc->skb);
	} else {
		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += length;

		skb_put(desc->skb, length);
		desc->skb->protocol = hdlc_type_trans(desc->skb, netdev);
		netif_rx(desc->skb);
	}

	/* Re-queue a transfer using the same descriptor */
	ret = qmc_hdlc_recv_queue(qmc_hdlc, desc, desc->dma_size);
	if (ret) {
		dev_err(qmc_hdlc->dev, "queue recv desc failed (%d)\n", ret);
		netdev->stats.rx_errors++;
	}
}

static int qmc_hdlc_recv_queue(struct qmc_hdlc *qmc_hdlc, struct qmc_hdlc_desc *desc, size_t size)
{
	int ret;

	desc->skb = dev_alloc_skb(size);
	if (!desc->skb)
		return -ENOMEM;

	desc->dma_size = size;
	desc->dma_addr = dma_map_single(qmc_hdlc->dev, desc->skb->data,
					desc->dma_size, DMA_FROM_DEVICE);
	ret = dma_mapping_error(qmc_hdlc->dev, desc->dma_addr);
	if (ret)
		goto free_skb;

	ret = qmc_chan_read_submit(qmc_hdlc->qmc_chan, desc->dma_addr, desc->dma_size,
				   qmc_hcld_recv_complete, desc);
	if (ret)
		goto dma_unmap;

	return 0;

dma_unmap:
	dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size, DMA_FROM_DEVICE);
free_skb:
	kfree_skb(desc->skb);
	desc->skb = NULL;
	return ret;
}

static void qmc_hdlc_xmit_complete(void *context)
{
	struct qmc_hdlc_desc *desc = context;
	struct net_device *netdev;
	struct qmc_hdlc *qmc_hdlc;
	struct sk_buff *skb;

	netdev = desc->netdev;
	qmc_hdlc = netdev_to_qmc_hdlc(netdev);

	scoped_guard(spinlock_irqsave, &qmc_hdlc->tx_lock) {
		dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size, DMA_TO_DEVICE);
		skb = desc->skb;
		desc->skb = NULL; /* Release the descriptor */
		if (netif_queue_stopped(netdev))
			netif_wake_queue(netdev);
	}

	netdev->stats.tx_packets++;
	netdev->stats.tx_bytes += skb->len;

	dev_consume_skb_any(skb);
}

static int qmc_hdlc_xmit_queue(struct qmc_hdlc *qmc_hdlc, struct qmc_hdlc_desc *desc)
{
	int ret;

	desc->dma_addr = dma_map_single(qmc_hdlc->dev, desc->skb->data,
					desc->dma_size, DMA_TO_DEVICE);
	ret = dma_mapping_error(qmc_hdlc->dev, desc->dma_addr);
	if (ret) {
		dev_err(qmc_hdlc->dev, "failed to map skb\n");
		return ret;
	}

	ret = qmc_chan_write_submit(qmc_hdlc->qmc_chan, desc->dma_addr, desc->dma_size,
				    qmc_hdlc_xmit_complete, desc);
	if (ret) {
		dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size, DMA_TO_DEVICE);
		dev_err(qmc_hdlc->dev, "qmc chan write returns %d\n", ret);
		return ret;
	}

	return 0;
}

static netdev_tx_t qmc_hdlc_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct qmc_hdlc *qmc_hdlc = netdev_to_qmc_hdlc(netdev);
	struct qmc_hdlc_desc *desc;
	int err;

	guard(spinlock_irqsave)(&qmc_hdlc->tx_lock);

	desc = &qmc_hdlc->tx_descs[qmc_hdlc->tx_out];
	if (WARN_ONCE(desc->skb, "No tx descriptors available\n")) {
		/* Should never happen.
		 * Previous xmit should have already stopped the queue.
		 */
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	desc->netdev = netdev;
	desc->dma_size = skb->len;
	desc->skb = skb;
	err = qmc_hdlc_xmit_queue(qmc_hdlc, desc);
	if (err) {
		desc->skb = NULL; /* Release the descriptor */
		if (err == -EBUSY) {
			netif_stop_queue(netdev);
			return NETDEV_TX_BUSY;
		}
		dev_kfree_skb(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	qmc_hdlc->tx_out = (qmc_hdlc->tx_out + 1) % ARRAY_SIZE(qmc_hdlc->tx_descs);

	if (qmc_hdlc->tx_descs[qmc_hdlc->tx_out].skb)
		netif_stop_queue(netdev);

	return NETDEV_TX_OK;
}

static int qmc_hdlc_xlate_slot_map(struct qmc_hdlc *qmc_hdlc,
				   u32 slot_map, struct qmc_chan_ts_info *ts_info)
{
	DECLARE_BITMAP(ts_mask_avail, 64);
	DECLARE_BITMAP(ts_mask, 64);
	DECLARE_BITMAP(map, 64);

	/* Tx and Rx available masks must be identical */
	if (ts_info->rx_ts_mask_avail != ts_info->tx_ts_mask_avail) {
		dev_err(qmc_hdlc->dev, "tx and rx available timeslots mismatch (0x%llx, 0x%llx)\n",
			ts_info->rx_ts_mask_avail, ts_info->tx_ts_mask_avail);
		return -EINVAL;
	}

	bitmap_from_u64(ts_mask_avail, ts_info->rx_ts_mask_avail);
	bitmap_from_u64(map, slot_map);
	bitmap_scatter(ts_mask, map, ts_mask_avail, 64);

	if (bitmap_weight(ts_mask, 64) != bitmap_weight(map, 64)) {
		dev_err(qmc_hdlc->dev, "Cannot translate timeslots %64pb -> (%64pb, %64pb)\n",
			map, ts_mask_avail, ts_mask);
		return -EINVAL;
	}

	bitmap_to_arr64(&ts_info->tx_ts_mask, ts_mask, 64);
	ts_info->rx_ts_mask = ts_info->tx_ts_mask;
	return 0;
}

static int qmc_hdlc_xlate_ts_info(struct qmc_hdlc *qmc_hdlc,
				  const struct qmc_chan_ts_info *ts_info, u32 *slot_map)
{
	DECLARE_BITMAP(ts_mask_avail, 64);
	DECLARE_BITMAP(ts_mask, 64);
	DECLARE_BITMAP(map, 64);
	u32 slot_array[2];

	/* Tx and Rx masks and available masks must be identical */
	if (ts_info->rx_ts_mask_avail != ts_info->tx_ts_mask_avail) {
		dev_err(qmc_hdlc->dev, "tx and rx available timeslots mismatch (0x%llx, 0x%llx)\n",
			ts_info->rx_ts_mask_avail, ts_info->tx_ts_mask_avail);
		return -EINVAL;
	}
	if (ts_info->rx_ts_mask != ts_info->tx_ts_mask) {
		dev_err(qmc_hdlc->dev, "tx and rx timeslots mismatch (0x%llx, 0x%llx)\n",
			ts_info->rx_ts_mask, ts_info->tx_ts_mask);
		return -EINVAL;
	}

	bitmap_from_u64(ts_mask_avail, ts_info->rx_ts_mask_avail);
	bitmap_from_u64(ts_mask, ts_info->rx_ts_mask);
	bitmap_gather(map, ts_mask, ts_mask_avail, 64);

	if (bitmap_weight(ts_mask, 64) != bitmap_weight(map, 64)) {
		dev_err(qmc_hdlc->dev, "Cannot translate timeslots (%64pb, %64pb) -> %64pb\n",
			ts_mask_avail, ts_mask, map);
		return -EINVAL;
	}

	bitmap_to_arr32(slot_array, map, 64);
	if (slot_array[1]) {
		dev_err(qmc_hdlc->dev, "Slot map out of 32bit (%64pb, %64pb) -> %64pb\n",
			ts_mask_avail, ts_mask, map);
		return -EINVAL;
	}

	*slot_map = slot_array[0];
	return 0;
}

static int qmc_hdlc_set_iface(struct qmc_hdlc *qmc_hdlc, int if_iface, const te1_settings *te1)
{
	struct qmc_chan_ts_info ts_info;
	int ret;

	ret = qmc_chan_get_ts_info(qmc_hdlc->qmc_chan, &ts_info);
	if (ret) {
		dev_err(qmc_hdlc->dev, "get QMC channel ts info failed %d\n", ret);
		return ret;
	}
	ret = qmc_hdlc_xlate_slot_map(qmc_hdlc, te1->slot_map, &ts_info);
	if (ret)
		return ret;

	ret = qmc_chan_set_ts_info(qmc_hdlc->qmc_chan, &ts_info);
	if (ret) {
		dev_err(qmc_hdlc->dev, "set QMC channel ts info failed %d\n", ret);
		return ret;
	}

	qmc_hdlc->slot_map = te1->slot_map;

	ret = qmc_hdlc_framer_set_iface(qmc_hdlc, if_iface, te1);
	if (ret) {
		dev_err(qmc_hdlc->dev, "framer set iface failed %d\n", ret);
		return ret;
	}

	return 0;
}

static int qmc_hdlc_ioctl(struct net_device *netdev, struct if_settings *ifs)
{
	struct qmc_hdlc *qmc_hdlc = netdev_to_qmc_hdlc(netdev);
	te1_settings te1;
	int ret;

	switch (ifs->type) {
	case IF_GET_IFACE:
		if (ifs->size < sizeof(te1)) {
			/* Retrieve type only */
			ret = qmc_hdlc_framer_get_iface(qmc_hdlc, &ifs->type, NULL);
			if (ret)
				return ret;

			if (!ifs->size)
				return 0; /* only type requested */

			ifs->size = sizeof(te1); /* data size wanted */
			return -ENOBUFS;
		}

		memset(&te1, 0, sizeof(te1));

		/* Retrieve info from framer */
		ret = qmc_hdlc_framer_get_iface(qmc_hdlc, &ifs->type, &te1);
		if (ret)
			return ret;

		/* Update slot_map */
		te1.slot_map = qmc_hdlc->slot_map;

		if (copy_to_user(ifs->ifs_ifsu.te1, &te1, sizeof(te1)))
			return -EFAULT;
		return 0;

	case IF_IFACE_E1:
	case IF_IFACE_T1:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (netdev->flags & IFF_UP)
			return -EBUSY;

		if (copy_from_user(&te1, ifs->ifs_ifsu.te1, sizeof(te1)))
			return -EFAULT;

		return qmc_hdlc_set_iface(qmc_hdlc, ifs->type, &te1);

	default:
		return hdlc_ioctl(netdev, ifs);
	}
}

static int qmc_hdlc_open(struct net_device *netdev)
{
	struct qmc_hdlc *qmc_hdlc = netdev_to_qmc_hdlc(netdev);
	struct qmc_chan_param chan_param;
	struct qmc_hdlc_desc *desc;
	int ret;
	int i;

	ret = qmc_hdlc_framer_start(qmc_hdlc);
	if (ret)
		return ret;

	ret = hdlc_open(netdev);
	if (ret)
		goto framer_stop;

	/* Update carrier */
	qmc_hdlc_framer_set_carrier(qmc_hdlc);

	chan_param.mode = QMC_HDLC;
	/* HDLC_MAX_MRU + 4 for the CRC
	 * HDLC_MAX_MRU + 4 + 8 for the CRC and some extraspace needed by the QMC
	 */
	chan_param.hdlc.max_rx_buf_size = HDLC_MAX_MRU + 4 + 8;
	chan_param.hdlc.max_rx_frame_size = HDLC_MAX_MRU + 4;
	chan_param.hdlc.is_crc32 = qmc_hdlc->is_crc32;
	ret = qmc_chan_set_param(qmc_hdlc->qmc_chan, &chan_param);
	if (ret) {
		dev_err(qmc_hdlc->dev, "failed to set param (%d)\n", ret);
		goto hdlc_close;
	}

	/* Queue as many recv descriptors as possible */
	for (i = 0; i < ARRAY_SIZE(qmc_hdlc->rx_descs); i++) {
		desc = &qmc_hdlc->rx_descs[i];

		desc->netdev = netdev;
		ret = qmc_hdlc_recv_queue(qmc_hdlc, desc, chan_param.hdlc.max_rx_buf_size);
		if (ret == -EBUSY && i != 0)
			break; /* We use all the QMC chan capability */
		if (ret)
			goto free_desc;
	}

	ret = qmc_chan_start(qmc_hdlc->qmc_chan, QMC_CHAN_ALL);
	if (ret) {
		dev_err(qmc_hdlc->dev, "qmc chan start failed (%d)\n", ret);
		goto free_desc;
	}

	netif_start_queue(netdev);

	return 0;

free_desc:
	qmc_chan_reset(qmc_hdlc->qmc_chan, QMC_CHAN_ALL);
	while (i--) {
		desc = &qmc_hdlc->rx_descs[i];
		dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size,
				 DMA_FROM_DEVICE);
		kfree_skb(desc->skb);
		desc->skb = NULL;
	}
hdlc_close:
	hdlc_close(netdev);
framer_stop:
	qmc_hdlc_framer_stop(qmc_hdlc);
	return ret;
}

static int qmc_hdlc_close(struct net_device *netdev)
{
	struct qmc_hdlc *qmc_hdlc = netdev_to_qmc_hdlc(netdev);
	struct qmc_hdlc_desc *desc;
	int i;

	qmc_chan_stop(qmc_hdlc->qmc_chan, QMC_CHAN_ALL);
	qmc_chan_reset(qmc_hdlc->qmc_chan, QMC_CHAN_ALL);

	netif_stop_queue(netdev);

	for (i = 0; i < ARRAY_SIZE(qmc_hdlc->tx_descs); i++) {
		desc = &qmc_hdlc->tx_descs[i];
		if (!desc->skb)
			continue;
		dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size,
				 DMA_TO_DEVICE);
		kfree_skb(desc->skb);
		desc->skb = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(qmc_hdlc->rx_descs); i++) {
		desc = &qmc_hdlc->rx_descs[i];
		if (!desc->skb)
			continue;
		dma_unmap_single(qmc_hdlc->dev, desc->dma_addr, desc->dma_size,
				 DMA_FROM_DEVICE);
		kfree_skb(desc->skb);
		desc->skb = NULL;
	}

	hdlc_close(netdev);
	qmc_hdlc_framer_stop(qmc_hdlc);
	return 0;
}

static int qmc_hdlc_attach(struct net_device *netdev, unsigned short encoding,
			   unsigned short parity)
{
	struct qmc_hdlc *qmc_hdlc = netdev_to_qmc_hdlc(netdev);

	if (encoding != ENCODING_NRZ)
		return -EINVAL;

	switch (parity) {
	case PARITY_CRC16_PR1_CCITT:
		qmc_hdlc->is_crc32 = false;
		break;
	case PARITY_CRC32_PR1_CCITT:
		qmc_hdlc->is_crc32 = true;
		break;
	default:
		dev_err(qmc_hdlc->dev, "unsupported parity %u\n", parity);
		return -EINVAL;
	}

	return 0;
}

static const struct net_device_ops qmc_hdlc_netdev_ops = {
	.ndo_open       = qmc_hdlc_open,
	.ndo_stop       = qmc_hdlc_close,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_siocwandev = qmc_hdlc_ioctl,
};

static int qmc_hdlc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qmc_chan_ts_info ts_info;
	struct qmc_hdlc *qmc_hdlc;
	struct qmc_chan_info info;
	hdlc_device *hdlc;
	int ret;

	qmc_hdlc = devm_kzalloc(dev, sizeof(*qmc_hdlc), GFP_KERNEL);
	if (!qmc_hdlc)
		return -ENOMEM;

	qmc_hdlc->dev = dev;
	spin_lock_init(&qmc_hdlc->tx_lock);
	spin_lock_init(&qmc_hdlc->carrier_lock);

	qmc_hdlc->qmc_chan = devm_qmc_chan_get_bychild(dev, dev->of_node);
	if (IS_ERR(qmc_hdlc->qmc_chan))
		return dev_err_probe(dev, PTR_ERR(qmc_hdlc->qmc_chan),
				     "get QMC channel failed\n");

	ret = qmc_chan_get_info(qmc_hdlc->qmc_chan, &info);
	if (ret)
		return dev_err_probe(dev, ret, "get QMC channel info failed\n");

	if (info.mode != QMC_HDLC)
		return dev_err_probe(dev, -EINVAL, "QMC chan mode %d is not QMC_HDLC\n",
				     info.mode);

	ret = qmc_chan_get_ts_info(qmc_hdlc->qmc_chan, &ts_info);
	if (ret)
		return dev_err_probe(dev, ret, "get QMC channel ts info failed\n");

	ret = qmc_hdlc_xlate_ts_info(qmc_hdlc, &ts_info, &qmc_hdlc->slot_map);
	if (ret)
		return ret;

	qmc_hdlc->framer = devm_framer_optional_get(dev, "fsl,framer");
	if (IS_ERR(qmc_hdlc->framer))
		return PTR_ERR(qmc_hdlc->framer);

	ret = qmc_hdlc_framer_init(qmc_hdlc);
	if (ret)
		return ret;

	qmc_hdlc->netdev = alloc_hdlcdev(qmc_hdlc);
	if (!qmc_hdlc->netdev) {
		ret = -ENOMEM;
		goto framer_exit;
	}

	hdlc = dev_to_hdlc(qmc_hdlc->netdev);
	hdlc->attach = qmc_hdlc_attach;
	hdlc->xmit = qmc_hdlc_xmit;
	SET_NETDEV_DEV(qmc_hdlc->netdev, dev);
	qmc_hdlc->netdev->tx_queue_len = ARRAY_SIZE(qmc_hdlc->tx_descs);
	qmc_hdlc->netdev->netdev_ops = &qmc_hdlc_netdev_ops;
	ret = register_hdlc_device(qmc_hdlc->netdev);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register hdlc device\n");
		goto free_netdev;
	}

	platform_set_drvdata(pdev, qmc_hdlc);
	return 0;

free_netdev:
	free_netdev(qmc_hdlc->netdev);
framer_exit:
	qmc_hdlc_framer_exit(qmc_hdlc);
	return ret;
}

static void qmc_hdlc_remove(struct platform_device *pdev)
{
	struct qmc_hdlc *qmc_hdlc = platform_get_drvdata(pdev);

	unregister_hdlc_device(qmc_hdlc->netdev);
	free_netdev(qmc_hdlc->netdev);
	qmc_hdlc_framer_exit(qmc_hdlc);
}

static const struct of_device_id qmc_hdlc_id_table[] = {
	{ .compatible = "fsl,qmc-hdlc" },
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, qmc_hdlc_id_table);

static struct platform_driver qmc_hdlc_driver = {
	.driver = {
		.name = "fsl-qmc-hdlc",
		.of_match_table = qmc_hdlc_id_table,
	},
	.probe = qmc_hdlc_probe,
	.remove_new = qmc_hdlc_remove,
};
module_platform_driver(qmc_hdlc_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("QMC HDLC driver");
MODULE_LICENSE("GPL");
