// SPDX-License-Identifier: GPL-2.0
/*
 * Lantiq / Intel PMAC driver for XRX200 SoCs
 *
 * Copyright (C) 2010 Lantiq Deutschland
 * Copyright (C) 2012 John Crispin <john@phrozen.org>
 * Copyright (C) 2017 - 2018 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <linux/if_vlan.h>

#include <linux/of_net.h>
#include <linux/of_platform.h>

#include <xway_dma.h>

/* DMA */
#define XRX200_DMA_DATA_LEN	(SZ_64K - 1)
#define XRX200_DMA_RX		0
#define XRX200_DMA_TX		1
#define XRX200_DMA_BURST_LEN	8

#define XRX200_DMA_PACKET_COMPLETE	0
#define XRX200_DMA_PACKET_IN_PROGRESS	1

/* cpu port mac */
#define PMAC_RX_IPG		0x0024
#define PMAC_RX_IPG_MASK	0xf

#define PMAC_HD_CTL		0x0000
/* Add Ethernet header to packets from DMA to PMAC */
#define PMAC_HD_CTL_ADD		BIT(0)
/* Add VLAN tag to Packets from DMA to PMAC */
#define PMAC_HD_CTL_TAG		BIT(1)
/* Add CRC to packets from DMA to PMAC */
#define PMAC_HD_CTL_AC		BIT(2)
/* Add status header to packets from PMAC to DMA */
#define PMAC_HD_CTL_AS		BIT(3)
/* Remove CRC from packets from PMAC to DMA */
#define PMAC_HD_CTL_RC		BIT(4)
/* Remove Layer-2 header from packets from PMAC to DMA */
#define PMAC_HD_CTL_RL2		BIT(5)
/* Status header is present from DMA to PMAC */
#define PMAC_HD_CTL_RXSH	BIT(6)
/* Add special tag from PMAC to switch */
#define PMAC_HD_CTL_AST		BIT(7)
/* Remove specail Tag from PMAC to DMA */
#define PMAC_HD_CTL_RST		BIT(8)
/* Check CRC from DMA to PMAC */
#define PMAC_HD_CTL_CCRC	BIT(9)
/* Enable reaction to Pause frames in the PMAC */
#define PMAC_HD_CTL_FC		BIT(10)

struct xrx200_chan {
	int tx_free;

	struct napi_struct napi;
	struct ltq_dma_channel dma;

	union {
		struct sk_buff *skb[LTQ_DESC_NUM];
		void *rx_buff[LTQ_DESC_NUM];
	};

	struct sk_buff *skb_head;
	struct sk_buff *skb_tail;

	struct xrx200_priv *priv;
};

struct xrx200_priv {
	struct clk *clk;

	struct xrx200_chan chan_tx;
	struct xrx200_chan chan_rx;

	u16 rx_buf_size;
	u16 rx_skb_size;

	struct net_device *net_dev;
	struct device *dev;

	__iomem void *pmac_reg;
};

static u32 xrx200_pmac_r32(struct xrx200_priv *priv, u32 offset)
{
	return __raw_readl(priv->pmac_reg + offset);
}

static void xrx200_pmac_w32(struct xrx200_priv *priv, u32 val, u32 offset)
{
	__raw_writel(val, priv->pmac_reg + offset);
}

static void xrx200_pmac_mask(struct xrx200_priv *priv, u32 clear, u32 set,
			     u32 offset)
{
	u32 val = xrx200_pmac_r32(priv, offset);

	val &= ~(clear);
	val |= set;
	xrx200_pmac_w32(priv, val, offset);
}

static int xrx200_max_frame_len(int mtu)
{
	return VLAN_ETH_HLEN + mtu;
}

static int xrx200_buffer_size(int mtu)
{
	return round_up(xrx200_max_frame_len(mtu), 4 * XRX200_DMA_BURST_LEN);
}

static int xrx200_skb_size(u16 buf_size)
{
	return SKB_DATA_ALIGN(buf_size + NET_SKB_PAD + NET_IP_ALIGN) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
}

/* drop all the packets from the DMA ring */
static void xrx200_flush_dma(struct xrx200_chan *ch)
{
	int i;

	for (i = 0; i < LTQ_DESC_NUM; i++) {
		struct ltq_dma_desc *desc = &ch->dma.desc_base[ch->dma.desc];

		if ((desc->ctl & (LTQ_DMA_OWN | LTQ_DMA_C)) != LTQ_DMA_C)
			break;

		desc->ctl = LTQ_DMA_OWN | LTQ_DMA_RX_OFFSET(NET_IP_ALIGN) |
			    ch->priv->rx_buf_size;
		ch->dma.desc++;
		ch->dma.desc %= LTQ_DESC_NUM;
	}
}

static int xrx200_open(struct net_device *net_dev)
{
	struct xrx200_priv *priv = netdev_priv(net_dev);

	napi_enable(&priv->chan_tx.napi);
	ltq_dma_open(&priv->chan_tx.dma);
	ltq_dma_enable_irq(&priv->chan_tx.dma);

	napi_enable(&priv->chan_rx.napi);
	ltq_dma_open(&priv->chan_rx.dma);
	/* The boot loader does not always deactivate the receiving of frames
	 * on the ports and then some packets queue up in the PPE buffers.
	 * They already passed the PMAC so they do not have the tags
	 * configured here. Read the these packets here and drop them.
	 * The HW should have written them into memory after 10us
	 */
	usleep_range(20, 40);
	xrx200_flush_dma(&priv->chan_rx);
	ltq_dma_enable_irq(&priv->chan_rx.dma);

	netif_wake_queue(net_dev);

	return 0;
}

static int xrx200_close(struct net_device *net_dev)
{
	struct xrx200_priv *priv = netdev_priv(net_dev);

	netif_stop_queue(net_dev);

	napi_disable(&priv->chan_rx.napi);
	ltq_dma_close(&priv->chan_rx.dma);

	napi_disable(&priv->chan_tx.napi);
	ltq_dma_close(&priv->chan_tx.dma);

	return 0;
}

static int xrx200_alloc_buf(struct xrx200_chan *ch, void *(*alloc)(unsigned int size))
{
	void *buf = ch->rx_buff[ch->dma.desc];
	struct xrx200_priv *priv = ch->priv;
	dma_addr_t mapping;
	int ret = 0;

	ch->rx_buff[ch->dma.desc] = alloc(priv->rx_skb_size);
	if (!ch->rx_buff[ch->dma.desc]) {
		ch->rx_buff[ch->dma.desc] = buf;
		ret = -ENOMEM;
		goto skip;
	}

	mapping = dma_map_single(priv->dev, ch->rx_buff[ch->dma.desc],
				 priv->rx_buf_size, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, mapping))) {
		skb_free_frag(ch->rx_buff[ch->dma.desc]);
		ch->rx_buff[ch->dma.desc] = buf;
		ret = -ENOMEM;
		goto skip;
	}

	ch->dma.desc_base[ch->dma.desc].addr = mapping + NET_SKB_PAD + NET_IP_ALIGN;
	/* Make sure the address is written before we give it to HW */
	wmb();
skip:
	ch->dma.desc_base[ch->dma.desc].ctl =
		LTQ_DMA_OWN | LTQ_DMA_RX_OFFSET(NET_IP_ALIGN) | priv->rx_buf_size;

	return ret;
}

static int xrx200_hw_receive(struct xrx200_chan *ch)
{
	struct xrx200_priv *priv = ch->priv;
	struct ltq_dma_desc *desc = &ch->dma.desc_base[ch->dma.desc];
	void *buf = ch->rx_buff[ch->dma.desc];
	u32 ctl = desc->ctl;
	int len = (ctl & LTQ_DMA_SIZE_MASK);
	struct net_device *net_dev = priv->net_dev;
	struct sk_buff *skb;
	int ret;

	ret = xrx200_alloc_buf(ch, napi_alloc_frag);

	ch->dma.desc++;
	ch->dma.desc %= LTQ_DESC_NUM;

	if (ret) {
		net_dev->stats.rx_dropped++;
		netdev_err(net_dev, "failed to allocate new rx buffer\n");
		return ret;
	}

	skb = build_skb(buf, priv->rx_skb_size);
	if (!skb) {
		skb_free_frag(buf);
		net_dev->stats.rx_dropped++;
		return -ENOMEM;
	}

	skb_reserve(skb, NET_SKB_PAD);
	skb_put(skb, len);

	/* add buffers to skb via skb->frag_list */
	if (ctl & LTQ_DMA_SOP) {
		ch->skb_head = skb;
		ch->skb_tail = skb;
		skb_reserve(skb, NET_IP_ALIGN);
	} else if (ch->skb_head) {
		if (ch->skb_head == ch->skb_tail)
			skb_shinfo(ch->skb_tail)->frag_list = skb;
		else
			ch->skb_tail->next = skb;
		ch->skb_tail = skb;
		ch->skb_head->len += skb->len;
		ch->skb_head->data_len += skb->len;
		ch->skb_head->truesize += skb->truesize;
	}

	if (ctl & LTQ_DMA_EOP) {
		ch->skb_head->protocol = eth_type_trans(ch->skb_head, net_dev);
		net_dev->stats.rx_packets++;
		net_dev->stats.rx_bytes += ch->skb_head->len;
		netif_receive_skb(ch->skb_head);
		ch->skb_head = NULL;
		ch->skb_tail = NULL;
		ret = XRX200_DMA_PACKET_COMPLETE;
	} else {
		ret = XRX200_DMA_PACKET_IN_PROGRESS;
	}

	return ret;
}

static int xrx200_poll_rx(struct napi_struct *napi, int budget)
{
	struct xrx200_chan *ch = container_of(napi,
				struct xrx200_chan, napi);
	int rx = 0;
	int ret;

	while (rx < budget) {
		struct ltq_dma_desc *desc = &ch->dma.desc_base[ch->dma.desc];

		if ((desc->ctl & (LTQ_DMA_OWN | LTQ_DMA_C)) == LTQ_DMA_C) {
			ret = xrx200_hw_receive(ch);
			if (ret == XRX200_DMA_PACKET_IN_PROGRESS)
				continue;
			if (ret != XRX200_DMA_PACKET_COMPLETE)
				break;
			rx++;
		} else {
			break;
		}
	}

	if (rx < budget) {
		if (napi_complete_done(&ch->napi, rx))
			ltq_dma_enable_irq(&ch->dma);
	}

	return rx;
}

static int xrx200_tx_housekeeping(struct napi_struct *napi, int budget)
{
	struct xrx200_chan *ch = container_of(napi,
				struct xrx200_chan, napi);
	struct net_device *net_dev = ch->priv->net_dev;
	int pkts = 0;
	int bytes = 0;

	netif_tx_lock(net_dev);
	while (pkts < budget) {
		struct ltq_dma_desc *desc = &ch->dma.desc_base[ch->tx_free];

		if ((desc->ctl & (LTQ_DMA_OWN | LTQ_DMA_C)) == LTQ_DMA_C) {
			struct sk_buff *skb = ch->skb[ch->tx_free];

			pkts++;
			bytes += skb->len;
			ch->skb[ch->tx_free] = NULL;
			consume_skb(skb);
			memset(&ch->dma.desc_base[ch->tx_free], 0,
			       sizeof(struct ltq_dma_desc));
			ch->tx_free++;
			ch->tx_free %= LTQ_DESC_NUM;
		} else {
			break;
		}
	}

	net_dev->stats.tx_packets += pkts;
	net_dev->stats.tx_bytes += bytes;
	netdev_completed_queue(ch->priv->net_dev, pkts, bytes);

	netif_tx_unlock(net_dev);
	if (netif_queue_stopped(net_dev))
		netif_wake_queue(net_dev);

	if (pkts < budget) {
		if (napi_complete_done(&ch->napi, pkts))
			ltq_dma_enable_irq(&ch->dma);
	}

	return pkts;
}

static netdev_tx_t xrx200_start_xmit(struct sk_buff *skb,
				     struct net_device *net_dev)
{
	struct xrx200_priv *priv = netdev_priv(net_dev);
	struct xrx200_chan *ch = &priv->chan_tx;
	struct ltq_dma_desc *desc = &ch->dma.desc_base[ch->dma.desc];
	u32 byte_offset;
	dma_addr_t mapping;
	int len;

	skb->dev = net_dev;
	if (skb_put_padto(skb, ETH_ZLEN)) {
		net_dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	len = skb->len;

	if ((desc->ctl & (LTQ_DMA_OWN | LTQ_DMA_C)) || ch->skb[ch->dma.desc]) {
		netdev_err(net_dev, "tx ring full\n");
		netif_stop_queue(net_dev);
		return NETDEV_TX_BUSY;
	}

	ch->skb[ch->dma.desc] = skb;

	mapping = dma_map_single(priv->dev, skb->data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, mapping)))
		goto err_drop;

	/* dma needs to start on a burst length value aligned address */
	byte_offset = mapping % (XRX200_DMA_BURST_LEN * 4);

	desc->addr = mapping - byte_offset;
	/* Make sure the address is written before we give it to HW */
	wmb();
	desc->ctl = LTQ_DMA_OWN | LTQ_DMA_SOP | LTQ_DMA_EOP |
		LTQ_DMA_TX_OFFSET(byte_offset) | (len & LTQ_DMA_SIZE_MASK);
	ch->dma.desc++;
	ch->dma.desc %= LTQ_DESC_NUM;
	if (ch->dma.desc == ch->tx_free)
		netif_stop_queue(net_dev);

	netdev_sent_queue(net_dev, len);

	return NETDEV_TX_OK;

err_drop:
	dev_kfree_skb(skb);
	net_dev->stats.tx_dropped++;
	net_dev->stats.tx_errors++;
	return NETDEV_TX_OK;
}

static int
xrx200_change_mtu(struct net_device *net_dev, int new_mtu)
{
	struct xrx200_priv *priv = netdev_priv(net_dev);
	struct xrx200_chan *ch_rx = &priv->chan_rx;
	int old_mtu = net_dev->mtu;
	bool running = false;
	void *buff;
	int curr_desc;
	int ret = 0;

	WRITE_ONCE(net_dev->mtu, new_mtu);
	priv->rx_buf_size = xrx200_buffer_size(new_mtu);
	priv->rx_skb_size = xrx200_skb_size(priv->rx_buf_size);

	if (new_mtu <= old_mtu)
		return ret;

	running = netif_running(net_dev);
	if (running) {
		napi_disable(&ch_rx->napi);
		ltq_dma_close(&ch_rx->dma);
	}

	xrx200_poll_rx(&ch_rx->napi, LTQ_DESC_NUM);
	curr_desc = ch_rx->dma.desc;

	for (ch_rx->dma.desc = 0; ch_rx->dma.desc < LTQ_DESC_NUM;
	     ch_rx->dma.desc++) {
		buff = ch_rx->rx_buff[ch_rx->dma.desc];
		ret = xrx200_alloc_buf(ch_rx, netdev_alloc_frag);
		if (ret) {
			WRITE_ONCE(net_dev->mtu, old_mtu);
			priv->rx_buf_size = xrx200_buffer_size(old_mtu);
			priv->rx_skb_size = xrx200_skb_size(priv->rx_buf_size);
			break;
		}
		skb_free_frag(buff);
	}

	ch_rx->dma.desc = curr_desc;
	if (running) {
		napi_enable(&ch_rx->napi);
		ltq_dma_open(&ch_rx->dma);
		ltq_dma_enable_irq(&ch_rx->dma);
	}

	return ret;
}

static const struct net_device_ops xrx200_netdev_ops = {
	.ndo_open		= xrx200_open,
	.ndo_stop		= xrx200_close,
	.ndo_start_xmit		= xrx200_start_xmit,
	.ndo_change_mtu		= xrx200_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static irqreturn_t xrx200_dma_irq(int irq, void *ptr)
{
	struct xrx200_chan *ch = ptr;

	if (napi_schedule_prep(&ch->napi)) {
		ltq_dma_disable_irq(&ch->dma);
		__napi_schedule(&ch->napi);
	}

	ltq_dma_ack_irq(&ch->dma);

	return IRQ_HANDLED;
}

static int xrx200_dma_init(struct xrx200_priv *priv)
{
	struct xrx200_chan *ch_rx = &priv->chan_rx;
	struct xrx200_chan *ch_tx = &priv->chan_tx;
	int ret = 0;
	int i;

	ltq_dma_init_port(DMA_PORT_ETOP, XRX200_DMA_BURST_LEN,
			  XRX200_DMA_BURST_LEN);

	ch_rx->dma.nr = XRX200_DMA_RX;
	ch_rx->dma.dev = priv->dev;
	ch_rx->priv = priv;

	ltq_dma_alloc_rx(&ch_rx->dma);
	for (ch_rx->dma.desc = 0; ch_rx->dma.desc < LTQ_DESC_NUM;
	     ch_rx->dma.desc++) {
		ret = xrx200_alloc_buf(ch_rx, netdev_alloc_frag);
		if (ret)
			goto rx_free;
	}
	ch_rx->dma.desc = 0;
	ret = devm_request_irq(priv->dev, ch_rx->dma.irq, xrx200_dma_irq, 0,
			       "xrx200_net_rx", &priv->chan_rx);
	if (ret) {
		dev_err(priv->dev, "failed to request RX irq %d\n",
			ch_rx->dma.irq);
		goto rx_ring_free;
	}

	ch_tx->dma.nr = XRX200_DMA_TX;
	ch_tx->dma.dev = priv->dev;
	ch_tx->priv = priv;

	ltq_dma_alloc_tx(&ch_tx->dma);
	ret = devm_request_irq(priv->dev, ch_tx->dma.irq, xrx200_dma_irq, 0,
			       "xrx200_net_tx", &priv->chan_tx);
	if (ret) {
		dev_err(priv->dev, "failed to request TX irq %d\n",
			ch_tx->dma.irq);
		goto tx_free;
	}

	return ret;

tx_free:
	ltq_dma_free(&ch_tx->dma);

rx_ring_free:
	/* free the allocated RX ring */
	for (i = 0; i < LTQ_DESC_NUM; i++) {
		if (priv->chan_rx.skb[i])
			skb_free_frag(priv->chan_rx.rx_buff[i]);
	}

rx_free:
	ltq_dma_free(&ch_rx->dma);
	return ret;
}

static void xrx200_hw_cleanup(struct xrx200_priv *priv)
{
	int i;

	ltq_dma_free(&priv->chan_tx.dma);
	ltq_dma_free(&priv->chan_rx.dma);

	/* free the allocated RX ring */
	for (i = 0; i < LTQ_DESC_NUM; i++)
		skb_free_frag(priv->chan_rx.rx_buff[i]);
}

static int xrx200_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct xrx200_priv *priv;
	struct net_device *net_dev;
	int err;

	/* alloc the network device */
	net_dev = devm_alloc_etherdev(dev, sizeof(struct xrx200_priv));
	if (!net_dev)
		return -ENOMEM;

	priv = netdev_priv(net_dev);
	priv->net_dev = net_dev;
	priv->dev = dev;

	net_dev->netdev_ops = &xrx200_netdev_ops;
	SET_NETDEV_DEV(net_dev, dev);
	net_dev->min_mtu = ETH_ZLEN;
	net_dev->max_mtu = XRX200_DMA_DATA_LEN - xrx200_max_frame_len(0);
	priv->rx_buf_size = xrx200_buffer_size(ETH_DATA_LEN);
	priv->rx_skb_size = xrx200_skb_size(priv->rx_buf_size);

	/* load the memory ranges */
	priv->pmac_reg = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(priv->pmac_reg))
		return PTR_ERR(priv->pmac_reg);

	priv->chan_rx.dma.irq = platform_get_irq_byname(pdev, "rx");
	if (priv->chan_rx.dma.irq < 0)
		return -ENOENT;
	priv->chan_tx.dma.irq = platform_get_irq_byname(pdev, "tx");
	if (priv->chan_tx.dma.irq < 0)
		return -ENOENT;

	/* get the clock */
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	err = of_get_ethdev_address(np, net_dev);
	if (err)
		eth_hw_addr_random(net_dev);

	/* bring up the dma engine and IP core */
	err = xrx200_dma_init(priv);
	if (err)
		return err;

	/* enable clock gate */
	err = clk_prepare_enable(priv->clk);
	if (err)
		goto err_uninit_dma;

	/* set IPG to 12 */
	xrx200_pmac_mask(priv, PMAC_RX_IPG_MASK, 0xb, PMAC_RX_IPG);

	/* enable status header, enable CRC */
	xrx200_pmac_mask(priv, 0,
			 PMAC_HD_CTL_RST | PMAC_HD_CTL_AST | PMAC_HD_CTL_RXSH |
			 PMAC_HD_CTL_AS | PMAC_HD_CTL_AC | PMAC_HD_CTL_RC,
			 PMAC_HD_CTL);

	/* setup NAPI */
	netif_napi_add(net_dev, &priv->chan_rx.napi, xrx200_poll_rx);
	netif_napi_add_tx(net_dev, &priv->chan_tx.napi,
			  xrx200_tx_housekeeping);

	platform_set_drvdata(pdev, priv);

	err = register_netdev(net_dev);
	if (err)
		goto err_unprepare_clk;

	return 0;

err_unprepare_clk:
	clk_disable_unprepare(priv->clk);

err_uninit_dma:
	xrx200_hw_cleanup(priv);

	return err;
}

static void xrx200_remove(struct platform_device *pdev)
{
	struct xrx200_priv *priv = platform_get_drvdata(pdev);
	struct net_device *net_dev = priv->net_dev;

	/* free stack related instances */
	netif_stop_queue(net_dev);
	netif_napi_del(&priv->chan_tx.napi);
	netif_napi_del(&priv->chan_rx.napi);

	/* remove the actual device */
	unregister_netdev(net_dev);

	/* release the clock */
	clk_disable_unprepare(priv->clk);

	/* shut down hardware */
	xrx200_hw_cleanup(priv);
}

static const struct of_device_id xrx200_match[] = {
	{ .compatible = "lantiq,xrx200-net" },
	{},
};
MODULE_DEVICE_TABLE(of, xrx200_match);

static struct platform_driver xrx200_driver = {
	.probe = xrx200_probe,
	.remove = xrx200_remove,
	.driver = {
		.name = "lantiq,xrx200-net",
		.of_match_table = xrx200_match,
	},
};

module_platform_driver(xrx200_driver);

MODULE_AUTHOR("John Crispin <john@phrozen.org>");
MODULE_DESCRIPTION("Lantiq SoC XRX200 ethernet");
MODULE_LICENSE("GPL");
