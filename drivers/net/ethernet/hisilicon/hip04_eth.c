
/* Copyright (c) 2014 Linaro Ltd.
 * Copyright (c) 2014 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/dmapool.h>

#define PPE_CFG_RX_CFF_ADDR	0x100
#define PPE_CFG_POOL_GRP	0x300
#define PPE_CFG_RX_BUF_SIZE	0x400
#define PPE_CFG_RX_FIFO_SIZE	0x500
#define PPE_CURR_BUF_CNT_REG	0xa200

#define GE_DUPLEX_TYPE		0x8
#define GE_PORT_MODE		0x40
#define GE_PORT_EN		0x44
#define GE_MODE_CHANGE_EN	0x1b4
#define GE_STATION_MAC_ADDRESS	0x210
#define PPE_CFG_TX_PKT_BD_ADDR	0x420
#define PPE_CFG_RX_CTRL_REG	0x428
#define PPE_CFG_QOS_VMID_GEN	0x500
#define PPE_CFG_RX_PKT_INT	0x538
#define PPE_INTEN		0x600
#define PPE_INTSTS		0x608
#define PPE_RINT		0x604
#define PPE_CFG_STS_MODE	0x700
#define PPE_HIS_RX_PKT_CNT	0x804

/* REG_INTERRUPT_MASK */
#define RCV_INT			BIT(10)
#define RCV_NOBUF		BIT(8)
#define DEF_INT_MASK		0x41fdf

#define RX_DESC_NUM		64
#define TX_DESC_NUM		64
#define TX_NEXT(N)		(((N) + 1) & (TX_DESC_NUM-1))
#define RX_NEXT(N)		(((N) + 1) & (RX_DESC_NUM-1))

#define DESC_DEF_CFG		0x14
#define RX_BUF_SIZE		1600
#define TX_TIMEOUT		(6 * HZ)
#define DRV_NAME		"hip04-ether"

#define OBSOLETE_BUFFER

struct tx_desc {
	u32 send_addr;
	u16 send_size;
	u16 reserved[3];
	u32 cfg;
	u32 wb_addr;
};

struct rx_desc {
	u16 pkt_len;
	u16 reserved_16;
	u32 reserve[8];		/* simplified */
};

struct hip04_priv {
	void __iomem *base;
	unsigned int port;
	unsigned int speed;
	unsigned int id;
	unsigned int reg_inten;

	struct napi_struct napi;
	struct net_device *ndev;
	struct dma_pool *desc_pool;

	struct sk_buff *tx_skb[TX_DESC_NUM];
	struct tx_desc *td_ring[TX_DESC_NUM];
	dma_addr_t td_phys[TX_DESC_NUM];
	spinlock_t txlock;
	unsigned int tx_head;
	unsigned int tx_tail;
	unsigned int tx_count;

	struct sk_buff *rx_skb[RX_DESC_NUM];
	unsigned char *rx_buf[RX_DESC_NUM];
	unsigned int rx_head;
	unsigned int rx_buf_size;
};

static void __iomem *ppebase;

static void hip04_config_port(struct hip04_priv *priv, u32 speed, u32 duplex)
{
	u32 val, reg;

	switch (speed) {
	case SPEED_1000:
		reg = 8;
		break;
	case SPEED_100:
		reg = 1;
		break;
	default:
		reg = 0;
		break;
	}

	val = readl_relaxed(priv->base + GE_PORT_MODE);
	val |= reg;
	writel_relaxed(val, priv->base + GE_PORT_MODE);

	reg = (duplex) ? BIT(1) : BIT(0);
	val = readl_relaxed(priv->base + GE_DUPLEX_TYPE);
	val |= reg;
	writel_relaxed(val, priv->base + GE_DUPLEX_TYPE);

	val = readl_relaxed(priv->base + GE_MODE_CHANGE_EN);
	val |= BIT(1);
	writel_relaxed(val, priv->base + GE_MODE_CHANGE_EN);
}

#ifdef OBSOLETE_CONFIG
#define GE_MAX_FRM_SIZE_REG	0x3c
#define GE_SHORT_RUNTS_THR_REG	0x50
#define GE_TX_LOCAL_PAGE_REG	0x5c
#define GE_TRANSMIT_CONTROL_REG	0x60
#define GE_CF_CRC_STRIP_REG	0x1b0
#define GE_RECV_CONTROL_REG	0x1e0
#define PPE_CFG_MAX_FRAME_LEN_REG	0x408
#define PPE_CFG_BUS_CTRL_REG	0x424
#define PPE_CFG_RX_PKT_MODE_REG	0x438
#define GMAC_PPE_RX_PKT_MAX_LEN  (379)
#define GMAC_MAX_PKT_LEN         1516

static void hip04_test_config(struct hip04_priv *priv)
{
	u32 val;

	/* pkt mode */
	val = readl_relaxed(priv->base + PPE_CFG_RX_PKT_MODE_REG);
	val &= ~(0xc0000);		/* [19:18] */
	val |= 1 << 18;			/* align */
	writel_relaxed(val, priv->base + PPE_CFG_RX_PKT_MODE_REG);

	/* set bus ctrl */
	val = 1 << 14;	/* buffer locally release */
	val |= 1;	/* big endian */
	/* fixme: fe only ?*/
	val &= ~BIT(7);	/* disable poe */
	writel_relaxed(val, priv->base + PPE_CFG_BUS_CTRL_REG);

	/* set max pkt len, curtail if exceed */
	val = readl_relaxed(priv->base + PPE_CFG_MAX_FRAME_LEN_REG);
	val &= ~(0x3fff);		/* [13:0]*/
	val |= GMAC_PPE_RX_PKT_MAX_LEN;	/* max buffer len */
	writel_relaxed(val, priv->base + PPE_CFG_MAX_FRAME_LEN_REG);

	/* set max len of each pkt */
	val = readl_relaxed(priv->base + GE_MAX_FRM_SIZE_REG);
	val &= ~(0xffff);		/* [15:0]*/
	val |= GMAC_MAX_PKT_LEN;	/* max buffer len */
	writel_relaxed(val, priv->base + GE_MAX_FRM_SIZE_REG);

	/* set min len of each pkt */
	val = readl_relaxed(priv->base + GE_SHORT_RUNTS_THR_REG);
	val |= 31;			/* min buffer len */
	writel_relaxed(val, priv->base + GE_SHORT_RUNTS_THR_REG);

	/* tx */
	val = readl_relaxed(priv->base + GE_TRANSMIT_CONTROL_REG);
	val |= 1 << 5;			/* tx auto neg */
	val |= 1 << 6;			/* tx add crc */
	val |= 1 << 7;			/* tx short pad through */
	writel_relaxed(val, priv->base + GE_TRANSMIT_CONTROL_REG);

	/* rx crc */
	val = readl_relaxed(priv->base + GE_CF_CRC_STRIP_REG);
	val |= 1;			/* rx strip crc */
	writel_relaxed(val, priv->base + GE_CF_CRC_STRIP_REG);

	/* rx pad */
	val = readl_relaxed(priv->base + GE_RECV_CONTROL_REG);
	val |= 1 << 3;			/* rx strip pad */
	writel_relaxed(val, priv->base + GE_RECV_CONTROL_REG);

	/* auto neg control */
	val = readl_relaxed(priv->base + GE_TX_LOCAL_PAGE_REG);
	val |= 1;
	val &= ~(0x1e0);	/* [8:5] = 0*/
	val &= ~(0x3c0);	/* [13:10] = 0*/
	val &= ~(0x8000);	/* [15] = 0*/
	writel_relaxed(val, priv->base + GE_TX_LOCAL_PAGE_REG);
}
#endif

static void hip04_reset_ppe(struct hip04_priv *priv)
{
	u32 val;

	do {
		val =
		readl_relaxed(ppebase + priv->port * 4 + PPE_CURR_BUF_CNT_REG);
		readl_relaxed(ppebase + priv->port * 4 + PPE_CFG_RX_CFF_ADDR);
	} while (val & 0xfff);
}

static void hip04_config_fifo(struct hip04_priv *priv)
{
	u32 val;

	val = readl_relaxed(priv->base + PPE_CFG_STS_MODE);
	val |= BIT(12);		/* PPE_HIS_RX_PKT_CNT read clear */
	writel_relaxed(val, priv->base + PPE_CFG_STS_MODE);

	val = readl_relaxed(ppebase + priv->port * 4 + PPE_CFG_POOL_GRP);
	val |= BIT(priv->port);
	writel_relaxed(val, ppebase + priv->port * 4 + PPE_CFG_POOL_GRP);

	val = readl_relaxed(priv->base + PPE_CFG_QOS_VMID_GEN);
	val &= ~(0x7f00);		/* [14:8]*/
	val |= BIT(14);
	val |= priv->port << 8;
	writel_relaxed(val, priv->base + PPE_CFG_QOS_VMID_GEN);

	val = readl_relaxed(ppebase + priv->port * 4 + PPE_CFG_RX_BUF_SIZE);
	val &= ~(0xffff);		/* [15:0]*/
	val |= RX_BUF_SIZE;
	writel_relaxed(val, ppebase + priv->port * 4 + PPE_CFG_RX_BUF_SIZE);

	val = readl_relaxed(ppebase + priv->port * 4 + PPE_CFG_RX_FIFO_SIZE);
	val &= ~(0xfff0fff);			/* [27:16] [11:0]*/
	val |= RX_DESC_NUM << 16;		/* depth */
	val |= BIT(11);				/* seq: first set first ues */
	val |= RX_DESC_NUM * priv->id;		/* start_addr */
	writel_relaxed(val, ppebase + priv->port * 4 + PPE_CFG_RX_FIFO_SIZE);

	/* pkt store format */
	val = readl_relaxed(priv->base + PPE_CFG_RX_CTRL_REG);
	val &= ~(0x1f80f);		/* [16:11] [3:0]*/
	val |= 2 << 11;			/* align */
	writel_relaxed(val, priv->base + PPE_CFG_RX_CTRL_REG);
}

static void hip04_mac_enable(struct net_device *ndev, bool enable)
{
	struct hip04_priv *priv = netdev_priv(ndev);
	u32 val;

	if (enable) {
		/* enable tx & rx */
		val = readl_relaxed(priv->base + GE_PORT_EN);
		val |= 0x1 << 1;	/* rx*/
		val |= 0x1 << 2;	/* tx*/
		writel_relaxed(val, priv->base + GE_PORT_EN);

		/* enable interrupt */
		priv->reg_inten = DEF_INT_MASK;
		writel_relaxed(priv->reg_inten, priv->base + PPE_INTEN);

		/* clear rx int */
		val = RCV_INT;
		writel_relaxed(val, priv->base + PPE_RINT);

		/* config recv int*/
		val = readl_relaxed(priv->base + PPE_CFG_RX_PKT_INT);
		val &= ~(0x0fff);	/* [11:0] */
		val |= 0x1 << 6;	/* int threshold 1 package */
		val |= 0x4;		/* recv timeout */
		writel_relaxed(val, priv->base + PPE_CFG_RX_PKT_INT);
	} else {
		/* disable int */
		priv->reg_inten &= ~(RCV_INT | RCV_NOBUF);
		writel_relaxed(priv->reg_inten, priv->base + PPE_INTEN);

		/* disable tx & rx */
		val = readl_relaxed(priv->base + GE_PORT_EN);
		val &= ~(0x1 << 1);	/* rx*/
		val &= ~(0x1 << 2);	/* tx*/
		writel_relaxed(val, priv->base + GE_PORT_EN);
	}
}

static void hip04_set_xmit_desc(struct hip04_priv *priv, dma_addr_t phys)
{
	writel_relaxed(phys, priv->base + PPE_CFG_TX_PKT_BD_ADDR);
}

static void hip04_set_recv_desc(struct hip04_priv *priv, dma_addr_t phys)
{
	writel_relaxed(phys, ppebase + priv->port * 4 + PPE_CFG_RX_CFF_ADDR);
}

static u32 hip04_recv_cnt(struct hip04_priv *priv)
{
	return readl_relaxed(priv->base + PPE_HIS_RX_PKT_CNT);
}

static void hip04_update_mac_address(struct net_device *ndev)
{
	struct hip04_priv *priv = netdev_priv(ndev);

	writel_relaxed(((ndev->dev_addr[0] << 8) | (ndev->dev_addr[1])),
			priv->base + GE_STATION_MAC_ADDRESS);
	writel_relaxed(((ndev->dev_addr[2] << 24) | (ndev->dev_addr[3] << 16) |
			(ndev->dev_addr[4] << 8) | (ndev->dev_addr[5])),
			priv->base + GE_STATION_MAC_ADDRESS + 4);
}

static int hip04_set_mac_address(struct net_device *ndev, void *addr)
{
	struct sockaddr *address = addr;

	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, address->sa_data, ndev->addr_len);
	hip04_update_mac_address(ndev);

	return 0;
}

static void endian_change(void *p, int size)
{
	unsigned int *to_cover = (unsigned int *)p;
	int i;

	size = size >> 2;
	for (i = 0; i < size; i++)
		*(to_cover+i) = htonl(*(to_cover+i));
}

#ifdef OBSOLETE_BUFFER
static int
hip04_rx_refill_one_buffer(struct net_device *ndev, unsigned int index)
{
	struct hip04_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;

	skb = dev_alloc_skb(RX_BUF_SIZE + NET_SKB_PAD);
	if (NULL == skb)
		return -ENOMEM;

	memset(skb->data, 0x0, RX_BUF_SIZE);
	priv->rx_skb[index] = skb;
	dma_map_single(&(ndev->dev), skb->data, RX_BUF_SIZE, DMA_TO_DEVICE);
	hip04_set_recv_desc(priv, virt_to_phys(skb->data));

	return 0;
}
#endif

static int hip04_rx_poll(struct napi_struct *napi, int budget)
{
	struct hip04_priv *priv = container_of(napi,
			      struct hip04_priv, napi);
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	struct rx_desc *desc;
	unsigned char *buf;
	int rx = 0;
	unsigned int cnt = hip04_recv_cnt(priv);
	unsigned int len, tmp[16];

	while (cnt) {
#ifdef OBSOLETE_BUFFER
		skb = priv->rx_skb[priv->rx_head];
#else
		buf = priv->rx_buf[priv->rx_head];
		skb = build_skb(buf, priv->rx_buf_size);
		if (unlikely(!skb))
			net_dbg_ratelimited("build_skb failed\n");
#endif
		dma_map_single(&ndev->dev, skb->data,
			RX_BUF_SIZE, DMA_FROM_DEVICE);
		memcpy(tmp, skb->data, 64);
		endian_change((void *)tmp, 64);
		desc = (struct rx_desc *)tmp;
		len = desc->pkt_len;

		if (len > RX_BUF_SIZE)
			len = RX_BUF_SIZE;
		if (0 == len)
			break;

		skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&priv->napi, skb);

#ifdef OBSOLETE_BUFFER
		hip04_rx_refill_one_buffer(ndev, priv->rx_head);
#else
		dma_map_single(&ndev->dev, buf,	RX_BUF_SIZE, DMA_TO_DEVICE);
		/* reuse the buffer after data exhausted */
		hip04_set_recv_desc(priv, virt_to_phys(buf));
#endif
		priv->rx_head = RX_NEXT(priv->rx_head);

		if (rx++ >= budget)
			break;

		if (--cnt == 0)
			cnt = hip04_recv_cnt(priv);
	}

	if (rx < budget) {
		napi_gro_flush(napi, false);
		__napi_complete(napi);
	}

	/* enable rx interrupt */
	priv->reg_inten |= RCV_INT | RCV_NOBUF;
	writel_relaxed(priv->reg_inten, priv->base + PPE_INTEN);

	return rx;
}

static irqreturn_t hip04_mac_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *) dev_id;
	struct hip04_priv *priv = netdev_priv(ndev);
	unsigned int ists = readl(priv->base + PPE_INTSTS);
	u32 val;

	val = DEF_INT_MASK;
	writel_relaxed(val, priv->base + PPE_RINT);

	if ((ists & RCV_INT) || (ists & RCV_NOBUF)) {
		if (napi_schedule_prep(&priv->napi)) {
			/* disable rx interrupt */
			priv->reg_inten &= ~(RCV_INT | RCV_NOBUF);
			writel_relaxed(priv->reg_inten, priv->base + PPE_INTEN);
			__napi_schedule(&priv->napi);
		}
	}

	return IRQ_HANDLED;
}

static void hip04_tx_reclaim(struct net_device *ndev, bool force)
{
	struct hip04_priv *priv = netdev_priv(ndev);
	unsigned tx_head = priv->tx_head;
	unsigned tx_tail = priv->tx_tail;
	struct tx_desc *desc = priv->td_ring[priv->tx_tail];

	spin_lock_irq(&priv->txlock);
	while (tx_tail != tx_head) {
		if (desc->send_addr != 0) {
			if (force)
				desc->send_addr = 0;
			else
				break;
		}
		dev_kfree_skb_irq(priv->tx_skb[tx_tail]);
		priv->tx_skb[tx_tail] = NULL;
		tx_tail = TX_NEXT(tx_tail);
		priv->tx_count--;
	}
	priv->tx_tail = tx_tail;
	spin_unlock_irq(&priv->txlock);
}

static int hip04_mac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct hip04_priv *priv = netdev_priv(ndev);
	struct tx_desc *desc = priv->td_ring[priv->tx_head];
	unsigned int tx_head = priv->tx_head;
	int ret;

	hip04_tx_reclaim(ndev, false);

	spin_lock_irq(&priv->txlock);
	if (priv->tx_count++ >= TX_DESC_NUM) {
		net_dbg_ratelimited("no TX space for packet\n");
		netif_stop_queue(ndev);
		ret = NETDEV_TX_BUSY;
		goto out_unlock;
	}

	priv->tx_skb[tx_head] = skb;
	dma_map_single(&ndev->dev, skb->data, skb->len, DMA_TO_DEVICE);
	memset((void *)desc, 0, sizeof(*desc));
	desc->send_addr = (unsigned int)virt_to_phys(skb->data);
	desc->send_size = skb->len;
	desc->cfg = DESC_DEF_CFG;
	desc->wb_addr = priv->td_phys[tx_head];
	endian_change(desc, 64);
	skb_tx_timestamp(skb);
	hip04_set_xmit_desc(priv, priv->td_phys[tx_head]);

	priv->tx_head = TX_NEXT(tx_head);
	ret = NETDEV_TX_OK;
out_unlock:
	spin_unlock_irq(&priv->txlock);

	return ret;
}

static int hip04_mac_open(struct net_device *ndev)
{
	struct hip04_priv *priv = netdev_priv(ndev);
	int i;

#ifndef OBSOLETE_BUFFER
	hip04_reset_ppe(priv);
	for (i = 0; i < RX_DESC_NUM; i++) {
		dma_map_single(&ndev->dev, priv->rx_buf[i],
				RX_BUF_SIZE, DMA_TO_DEVICE);
		hip04_set_recv_desc(priv, virt_to_phys(priv->rx_buf[i]));
	}
	priv->rx_head = 0;
#endif
	priv->tx_head = 0;
	priv->tx_tail = 0;
	priv->tx_count = 0;

	netif_start_queue(ndev);
	hip04_mac_enable(ndev, true);
	napi_enable(&priv->napi);
	return 0;
}

static int hip04_mac_stop(struct net_device *ndev)
{
	struct hip04_priv *priv = netdev_priv(ndev);

	napi_disable(&priv->napi);
	netif_stop_queue(ndev);
	hip04_mac_enable(ndev, false);
	hip04_tx_reclaim(ndev, true);
#ifndef OBSOLETE_BUFFER
	hip04_reset_ppe(priv);
#endif
	return 0;
}

static void hip04_timeout(struct net_device *ndev)
{
	netif_wake_queue(ndev);
	return;
}

static struct net_device_ops hip04_netdev_ops = {
	.ndo_open		= hip04_mac_open,
	.ndo_stop		= hip04_mac_stop,
	.ndo_start_xmit		= hip04_mac_start_xmit,
	.ndo_set_mac_address	= hip04_set_mac_address,
	.ndo_tx_timeout         = hip04_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

#ifdef OBSOLETE_BUFFER
static int hip04_rx_ring_init(struct net_device *dev)
{
	struct hip04_priv *priv = netdev_priv(dev);
	struct sk_buff      *skb       = NULL;
	unsigned int         i;

	for (i = 0; i < RX_DESC_NUM; i++) {
		skb = dev_alloc_skb(RX_BUF_SIZE);
		if (NULL == skb)
			return -ENOMEM;
		memset(skb->data, 0x0, RX_BUF_SIZE);
		dma_map_single(&dev->dev, (skb->data),
				RX_BUF_SIZE, DMA_TO_DEVICE);
		hip04_set_recv_desc(priv, virt_to_phys(skb->data));
		priv->rx_skb[i] = skb;
	}
	priv->rx_head = 0;
	return 0;
}
#endif

static int hip04_alloc_ring(struct net_device *ndev, struct device *d)
{
	struct hip04_priv *priv = netdev_priv(ndev);
	void *base;
	int i;

	priv->desc_pool = dma_pool_create(DRV_NAME, d, sizeof(struct tx_desc),
				SKB_DATA_ALIGN(sizeof(struct tx_desc)),	0);
	if (!priv->desc_pool)
		return -ENOMEM;

	for (i = 0; i < TX_DESC_NUM; i++) {
		priv->td_ring[i] = dma_pool_alloc(priv->desc_pool,
					GFP_ATOMIC, &priv->td_phys[i]);
		if (!priv->td_ring[i])
			return -ENOMEM;
	}

#ifndef OBSOLETE_BUFFER
	priv->rx_head = 0;
	priv->rx_buf_size = RX_BUF_SIZE +
			    SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	/* mallco one more buffer for SKB_DATA_ALIGN */
	base = devm_kzalloc(d, priv->rx_buf_size * (RX_DESC_NUM + 1),
			GFP_ATOMIC);
	if (!base)
		return -ENOMEM;
	base = (void *)SKB_DATA_ALIGN((unsigned int)base);
	for (i = 0; i < RX_DESC_NUM; i++)
		priv->rx_buf[i] = base + i * priv->rx_buf_size;
#endif
	return 0;
}

static void hip04_free_ring(struct net_device *dev)
{
	struct hip04_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < TX_DESC_NUM; i++) {
		if (priv->tx_skb[i])
			dev_kfree_skb_any(priv->tx_skb[i]);
		if ((priv->desc_pool) && (priv->td_ring[i]))
			dma_pool_free(priv->desc_pool, priv->td_ring[i],
					priv->td_phys[i]);
	}

	if (priv->desc_pool)
		dma_pool_destroy(priv->desc_pool);
}

static int hip04_mac_probe(struct platform_device *pdev)
{
	struct device *d = &pdev->dev;
	struct device_node *node = d->of_node;
	struct net_device *ndev;
	struct hip04_priv *priv;
	struct resource *res;
	unsigned int irq, val;
	int ret;

	ndev = alloc_etherdev(sizeof(struct hip04_priv));
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	platform_set_drvdata(pdev, ndev);
	spin_lock_init(&priv->txlock);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto init_fail;
	}
	ndev->base_addr = res->start;
	priv->base = devm_ioremap_resource(d, res);
	ret = IS_ERR(priv->base);
	if (ret) {
		dev_err(d, "devm_ioremap_resource failed\n");
		goto init_fail;
	}

	if (!ppebase) {
		struct device_node *n;

		n = of_find_compatible_node(NULL, NULL, "hisilicon,ppebase");
		if (!n) {
			ret = -EINVAL;
			netdev_err(ndev, "not find hisilicon,ppebase\n");
			goto init_fail;
		}
		ppebase = of_iomap(n, 0);
	}

	ret = of_property_read_u32(node, "port", &val);
	if (ret) {
		dev_warn(d, "not find port info\n");
		goto init_fail;
	}
	priv->port = val & 0x1f;

	ret = of_property_read_u32(node, "speed", &val);
	if (ret) {
		dev_warn(d, "not find speed info\n");
		priv->speed = SPEED_1000;
	}

	if (SPEED_100 == val)
		priv->speed = SPEED_100;
	else
		priv->speed = SPEED_1000;

	/* fixme any id can be used directly? */
	ret = of_property_read_u32(node, "id", &priv->id);
	if (ret) {
		dev_warn(d, "not find id info\n");
		goto init_fail;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		ret = -EINVAL;
		goto init_fail;
	}
	ether_setup(ndev);
	ndev->netdev_ops = &hip04_netdev_ops;
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->irq = irq;
	netif_napi_add(ndev, &priv->napi, hip04_rx_poll, RX_DESC_NUM);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	hip04_reset_ppe(priv);
	hip04_config_port(priv, priv->speed, DUPLEX_FULL);
	hip04_config_fifo(priv);
	random_ether_addr(ndev->dev_addr);
	hip04_update_mac_address(ndev);
#ifdef OBSOLETE_BUFFER
	hip04_rx_ring_init(ndev);
#endif
	ret = hip04_alloc_ring(ndev, d);
	if (ret) {
		netdev_err(ndev, "alloc ring fail\n");
		goto alloc_fail;
	}

	ret = devm_request_irq(d, irq, hip04_mac_interrupt,
					0, pdev->name, ndev);
	if (ret) {
		netdev_err(ndev, "devm_request_irq failed\n");
		goto init_fail;
	}

	ret = register_netdev(ndev);
	if (ret) {
		free_netdev(ndev);
		goto init_fail;
	}

	return 0;
alloc_fail:
	hip04_free_ring(ndev);
init_fail:
	free_netdev(ndev);
	return ret;
}

static int hip04_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);
	free_irq(ndev->irq, ndev);
	free_netdev(ndev);
	free_netdev(ndev);

	return 0;
}

static const struct of_device_id hip04_mac_match[] = {
	{ .compatible = "hisilicon,hip04-mac" },
	{ }
};

static struct platform_driver hip04_mac_driver = {
	.probe	= hip04_mac_probe,
	.remove	= hip04_remove,
	.driver	= {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= hip04_mac_match,
	},
};
module_platform_driver(hip04_mac_driver);

MODULE_DESCRIPTION("HISILICON P04 Ethernet driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hip04-ether");
