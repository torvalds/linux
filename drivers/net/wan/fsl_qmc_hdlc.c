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
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/err.h>
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
	bool is_crc32;
	spinlock_t tx_lock; /* Protect tx descriptors */
	struct qmc_hdlc_desc tx_descs[8];
	unsigned int tx_out;
	struct qmc_hdlc_desc rx_descs[4];
};

static struct qmc_hdlc *netdev_to_qmc_hdlc(struct net_device *netdev)
{
	return dev_to_hdlc(netdev)->priv;
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

static int qmc_hdlc_open(struct net_device *netdev)
{
	struct qmc_hdlc *qmc_hdlc = netdev_to_qmc_hdlc(netdev);
	struct qmc_chan_param chan_param;
	struct qmc_hdlc_desc *desc;
	int ret;
	int i;

	ret = hdlc_open(netdev);
	if (ret)
		return ret;

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
	.ndo_siocwandev	= hdlc_ioctl,
};

static int qmc_hdlc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qmc_hdlc *qmc_hdlc;
	struct qmc_chan_info info;
	hdlc_device *hdlc;
	int ret;

	qmc_hdlc = devm_kzalloc(dev, sizeof(*qmc_hdlc), GFP_KERNEL);
	if (!qmc_hdlc)
		return -ENOMEM;

	qmc_hdlc->dev = dev;
	spin_lock_init(&qmc_hdlc->tx_lock);

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

	qmc_hdlc->netdev = alloc_hdlcdev(qmc_hdlc);
	if (!qmc_hdlc->netdev)
		return -ENOMEM;

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
	return ret;
}

static int qmc_hdlc_remove(struct platform_device *pdev)
{
	struct qmc_hdlc *qmc_hdlc = platform_get_drvdata(pdev);

	unregister_hdlc_device(qmc_hdlc->netdev);
	free_netdev(qmc_hdlc->netdev);

	return 0;
}

static const struct of_device_id qmc_hdlc_id_table[] = {
	{ .compatible = "fsl,qmc-hdlc" },
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, qmc_hdlc_driver);

static struct platform_driver qmc_hdlc_driver = {
	.driver = {
		.name = "fsl-qmc-hdlc",
		.of_match_table = qmc_hdlc_id_table,
	},
	.probe = qmc_hdlc_probe,
	.remove = qmc_hdlc_remove,
};
module_platform_driver(qmc_hdlc_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("QMC HDLC driver");
MODULE_LICENSE("GPL");
