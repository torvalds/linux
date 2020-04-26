/* MOXA ART Ethernet (RTL8201CP) driver.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
 *
 * Based on code from
 * Moxa Technology Co., Ltd. <www.moxa.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/crc32.h>
#include <linux/crc32c.h>
#include <linux/circ_buf.h>

#include "moxart_ether.h"

static inline void moxart_desc_write(u32 data, u32 *desc)
{
	*desc = cpu_to_le32(data);
}

static inline u32 moxart_desc_read(u32 *desc)
{
	return le32_to_cpu(*desc);
}

static inline void moxart_emac_write(struct net_device *ndev,
				     unsigned int reg, unsigned long value)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	writel(value, priv->base + reg);
}

static void moxart_update_mac_address(struct net_device *ndev)
{
	moxart_emac_write(ndev, REG_MAC_MS_ADDRESS,
			  ((ndev->dev_addr[0] << 8) | (ndev->dev_addr[1])));
	moxart_emac_write(ndev, REG_MAC_MS_ADDRESS + 4,
			  ((ndev->dev_addr[2] << 24) |
			   (ndev->dev_addr[3] << 16) |
			   (ndev->dev_addr[4] << 8) |
			   (ndev->dev_addr[5])));
}

static int moxart_set_mac_address(struct net_device *ndev, void *addr)
{
	struct sockaddr *address = addr;

	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, address->sa_data, ndev->addr_len);
	moxart_update_mac_address(ndev);

	return 0;
}

static void moxart_mac_free_memory(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);
	int i;

	for (i = 0; i < RX_DESC_NUM; i++)
		dma_unmap_single(&ndev->dev, priv->rx_mapping[i],
				 priv->rx_buf_size, DMA_FROM_DEVICE);

	if (priv->tx_desc_base)
		dma_free_coherent(NULL, TX_REG_DESC_SIZE * TX_DESC_NUM,
				  priv->tx_desc_base, priv->tx_base);

	if (priv->rx_desc_base)
		dma_free_coherent(NULL, RX_REG_DESC_SIZE * RX_DESC_NUM,
				  priv->rx_desc_base, priv->rx_base);

	kfree(priv->tx_buf_base);
	kfree(priv->rx_buf_base);
}

static void moxart_mac_reset(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	writel(SW_RST, priv->base + REG_MAC_CTRL);
	while (readl(priv->base + REG_MAC_CTRL) & SW_RST)
		mdelay(10);

	writel(0, priv->base + REG_INTERRUPT_MASK);

	priv->reg_maccr = RX_BROADPKT | FULLDUP | CRC_APD | RX_FTL;
}

static void moxart_mac_enable(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	writel(0x00001010, priv->base + REG_INT_TIMER_CTRL);
	writel(0x00000001, priv->base + REG_APOLL_TIMER_CTRL);
	writel(0x00000390, priv->base + REG_DMA_BLEN_CTRL);

	priv->reg_imr |= (RPKT_FINISH_M | XPKT_FINISH_M);
	writel(priv->reg_imr, priv->base + REG_INTERRUPT_MASK);

	priv->reg_maccr |= (RCV_EN | XMT_EN | RDMA_EN | XDMA_EN);
	writel(priv->reg_maccr, priv->base + REG_MAC_CTRL);
}

static void moxart_mac_setup_desc_ring(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);
	void *desc;
	int i;

	for (i = 0; i < TX_DESC_NUM; i++) {
		desc = priv->tx_desc_base + i * TX_REG_DESC_SIZE;
		memset(desc, 0, TX_REG_DESC_SIZE);

		priv->tx_buf[i] = priv->tx_buf_base + priv->tx_buf_size * i;
	}
	moxart_desc_write(TX_DESC1_END, desc + TX_REG_OFFSET_DESC1);

	priv->tx_head = 0;
	priv->tx_tail = 0;

	for (i = 0; i < RX_DESC_NUM; i++) {
		desc = priv->rx_desc_base + i * RX_REG_DESC_SIZE;
		memset(desc, 0, RX_REG_DESC_SIZE);
		moxart_desc_write(RX_DESC0_DMA_OWN, desc + RX_REG_OFFSET_DESC0);
		moxart_desc_write(RX_BUF_SIZE & RX_DESC1_BUF_SIZE_MASK,
		       desc + RX_REG_OFFSET_DESC1);

		priv->rx_buf[i] = priv->rx_buf_base + priv->rx_buf_size * i;
		priv->rx_mapping[i] = dma_map_single(&ndev->dev,
						     priv->rx_buf[i],
						     priv->rx_buf_size,
						     DMA_FROM_DEVICE);
		if (dma_mapping_error(&ndev->dev, priv->rx_mapping[i]))
			netdev_err(ndev, "DMA mapping error\n");

		moxart_desc_write(priv->rx_mapping[i],
		       desc + RX_REG_OFFSET_DESC2 + RX_DESC2_ADDRESS_PHYS);
		moxart_desc_write((uintptr_t)priv->rx_buf[i],
		       desc + RX_REG_OFFSET_DESC2 + RX_DESC2_ADDRESS_VIRT);
	}
	moxart_desc_write(RX_DESC1_END, desc + RX_REG_OFFSET_DESC1);

	priv->rx_head = 0;

	/* reset the MAC controller TX/RX descriptor base address */
	writel(priv->tx_base, priv->base + REG_TXR_BASE_ADDRESS);
	writel(priv->rx_base, priv->base + REG_RXR_BASE_ADDRESS);
}

static int moxart_mac_open(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	if (!is_valid_ether_addr(ndev->dev_addr))
		return -EADDRNOTAVAIL;

	napi_enable(&priv->napi);

	moxart_mac_reset(ndev);
	moxart_update_mac_address(ndev);
	moxart_mac_setup_desc_ring(ndev);
	moxart_mac_enable(ndev);
	netif_start_queue(ndev);

	netdev_dbg(ndev, "%s: IMR=0x%x, MACCR=0x%x\n",
		   __func__, readl(priv->base + REG_INTERRUPT_MASK),
		   readl(priv->base + REG_MAC_CTRL));

	return 0;
}

static int moxart_mac_stop(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	napi_disable(&priv->napi);

	netif_stop_queue(ndev);

	/* disable all interrupts */
	writel(0, priv->base + REG_INTERRUPT_MASK);

	/* disable all functions */
	writel(0, priv->base + REG_MAC_CTRL);

	return 0;
}

static int moxart_rx_poll(struct napi_struct *napi, int budget)
{
	struct moxart_mac_priv_t *priv = container_of(napi,
						      struct moxart_mac_priv_t,
						      napi);
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	void *desc;
	unsigned int desc0, len;
	int rx_head = priv->rx_head;
	int rx = 0;

	while (rx < budget) {
		desc = priv->rx_desc_base + (RX_REG_DESC_SIZE * rx_head);
		desc0 = moxart_desc_read(desc + RX_REG_OFFSET_DESC0);
		rmb(); /* ensure desc0 is up to date */

		if (desc0 & RX_DESC0_DMA_OWN)
			break;

		if (desc0 & (RX_DESC0_ERR | RX_DESC0_CRC_ERR | RX_DESC0_FTL |
			     RX_DESC0_RUNT | RX_DESC0_ODD_NB)) {
			net_dbg_ratelimited("packet error\n");
			ndev->stats.rx_dropped++;
			ndev->stats.rx_errors++;
			goto rx_next;
		}

		len = desc0 & RX_DESC0_FRAME_LEN_MASK;

		if (len > RX_BUF_SIZE)
			len = RX_BUF_SIZE;

		dma_sync_single_for_cpu(&ndev->dev,
					priv->rx_mapping[rx_head],
					priv->rx_buf_size, DMA_FROM_DEVICE);
		skb = netdev_alloc_skb_ip_align(ndev, len);

		if (unlikely(!skb)) {
			net_dbg_ratelimited("netdev_alloc_skb_ip_align failed\n");
			ndev->stats.rx_dropped++;
			ndev->stats.rx_errors++;
			goto rx_next;
		}

		memcpy(skb->data, priv->rx_buf[rx_head], len);
		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&priv->napi, skb);
		rx++;

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += len;
		if (desc0 & RX_DESC0_MULTICAST)
			ndev->stats.multicast++;

rx_next:
		wmb(); /* prevent setting ownership back too early */
		moxart_desc_write(RX_DESC0_DMA_OWN, desc + RX_REG_OFFSET_DESC0);

		rx_head = RX_NEXT(rx_head);
		priv->rx_head = rx_head;
	}

	if (rx < budget)
		napi_complete_done(napi, rx);

	priv->reg_imr |= RPKT_FINISH_M;
	writel(priv->reg_imr, priv->base + REG_INTERRUPT_MASK);

	return rx;
}

static int moxart_tx_queue_space(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	return CIRC_SPACE(priv->tx_head, priv->tx_tail, TX_DESC_NUM);
}

static void moxart_tx_finished(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);
	unsigned int tx_head = priv->tx_head;
	unsigned int tx_tail = priv->tx_tail;

	while (tx_tail != tx_head) {
		dma_unmap_single(&ndev->dev, priv->tx_mapping[tx_tail],
				 priv->tx_len[tx_tail], DMA_TO_DEVICE);

		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += priv->tx_skb[tx_tail]->len;

		dev_kfree_skb_irq(priv->tx_skb[tx_tail]);
		priv->tx_skb[tx_tail] = NULL;

		tx_tail = TX_NEXT(tx_tail);
	}
	priv->tx_tail = tx_tail;
	if (netif_queue_stopped(ndev) &&
	    moxart_tx_queue_space(ndev) >= TX_WAKE_THRESHOLD)
		netif_wake_queue(ndev);
}

static irqreturn_t moxart_mac_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);
	unsigned int ists = readl(priv->base + REG_INTERRUPT_STATUS);

	if (ists & XPKT_OK_INT_STS)
		moxart_tx_finished(ndev);

	if (ists & RPKT_FINISH) {
		if (napi_schedule_prep(&priv->napi)) {
			priv->reg_imr &= ~RPKT_FINISH_M;
			writel(priv->reg_imr, priv->base + REG_INTERRUPT_MASK);
			__napi_schedule(&priv->napi);
		}
	}

	return IRQ_HANDLED;
}

static int moxart_mac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);
	void *desc;
	unsigned int len;
	unsigned int tx_head;
	u32 txdes1;
	int ret = NETDEV_TX_BUSY;

	spin_lock_irq(&priv->txlock);

	tx_head = priv->tx_head;
	desc = priv->tx_desc_base + (TX_REG_DESC_SIZE * tx_head);

	if (moxart_tx_queue_space(ndev) == 1)
		netif_stop_queue(ndev);

	if (moxart_desc_read(desc + TX_REG_OFFSET_DESC0) & TX_DESC0_DMA_OWN) {
		net_dbg_ratelimited("no TX space for packet\n");
		ndev->stats.tx_dropped++;
		goto out_unlock;
	}
	rmb(); /* ensure data is only read that had TX_DESC0_DMA_OWN cleared */

	len = skb->len > TX_BUF_SIZE ? TX_BUF_SIZE : skb->len;

	priv->tx_mapping[tx_head] = dma_map_single(&ndev->dev, skb->data,
						   len, DMA_TO_DEVICE);
	if (dma_mapping_error(&ndev->dev, priv->tx_mapping[tx_head])) {
		netdev_err(ndev, "DMA mapping error\n");
		goto out_unlock;
	}

	priv->tx_len[tx_head] = len;
	priv->tx_skb[tx_head] = skb;

	moxart_desc_write(priv->tx_mapping[tx_head],
	       desc + TX_REG_OFFSET_DESC2 + TX_DESC2_ADDRESS_PHYS);
	moxart_desc_write((uintptr_t)skb->data,
	       desc + TX_REG_OFFSET_DESC2 + TX_DESC2_ADDRESS_VIRT);

	if (skb->len < ETH_ZLEN) {
		memset(&skb->data[skb->len],
		       0, ETH_ZLEN - skb->len);
		len = ETH_ZLEN;
	}

	dma_sync_single_for_device(&ndev->dev, priv->tx_mapping[tx_head],
				   priv->tx_buf_size, DMA_TO_DEVICE);

	txdes1 = TX_DESC1_LTS | TX_DESC1_FTS | (len & TX_DESC1_BUF_SIZE_MASK);
	if (tx_head == TX_DESC_NUM_MASK)
		txdes1 |= TX_DESC1_END;
	moxart_desc_write(txdes1, desc + TX_REG_OFFSET_DESC1);
	wmb(); /* flush descriptor before transferring ownership */
	moxart_desc_write(TX_DESC0_DMA_OWN, desc + TX_REG_OFFSET_DESC0);

	/* start to send packet */
	writel(0xffffffff, priv->base + REG_TX_POLL_DEMAND);

	priv->tx_head = TX_NEXT(tx_head);

	netif_trans_update(ndev);
	ret = NETDEV_TX_OK;
out_unlock:
	spin_unlock_irq(&priv->txlock);

	return ret;
}

static void moxart_mac_setmulticast(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	int crc_val;

	netdev_for_each_mc_addr(ha, ndev) {
		crc_val = crc32_le(~0, ha->addr, ETH_ALEN);
		crc_val = (crc_val >> 26) & 0x3f;
		if (crc_val >= 32) {
			writel(readl(priv->base + REG_MCAST_HASH_TABLE1) |
			       (1UL << (crc_val - 32)),
			       priv->base + REG_MCAST_HASH_TABLE1);
		} else {
			writel(readl(priv->base + REG_MCAST_HASH_TABLE0) |
			       (1UL << crc_val),
			       priv->base + REG_MCAST_HASH_TABLE0);
		}
	}
}

static void moxart_mac_set_rx_mode(struct net_device *ndev)
{
	struct moxart_mac_priv_t *priv = netdev_priv(ndev);

	spin_lock_irq(&priv->txlock);

	(ndev->flags & IFF_PROMISC) ? (priv->reg_maccr |= RCV_ALL) :
				      (priv->reg_maccr &= ~RCV_ALL);

	(ndev->flags & IFF_ALLMULTI) ? (priv->reg_maccr |= RX_MULTIPKT) :
				       (priv->reg_maccr &= ~RX_MULTIPKT);

	if ((ndev->flags & IFF_MULTICAST) && netdev_mc_count(ndev)) {
		priv->reg_maccr |= HT_MULTI_EN;
		moxart_mac_setmulticast(ndev);
	} else {
		priv->reg_maccr &= ~HT_MULTI_EN;
	}

	writel(priv->reg_maccr, priv->base + REG_MAC_CTRL);

	spin_unlock_irq(&priv->txlock);
}

static const struct net_device_ops moxart_netdev_ops = {
	.ndo_open		= moxart_mac_open,
	.ndo_stop		= moxart_mac_stop,
	.ndo_start_xmit		= moxart_mac_start_xmit,
	.ndo_set_rx_mode	= moxart_mac_set_rx_mode,
	.ndo_set_mac_address	= moxart_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};

static int moxart_mac_probe(struct platform_device *pdev)
{
	struct device *p_dev = &pdev->dev;
	struct device_node *node = p_dev->of_node;
	struct net_device *ndev;
	struct moxart_mac_priv_t *priv;
	struct resource *res;
	unsigned int irq;
	int ret;

	ndev = alloc_etherdev(sizeof(struct moxart_mac_priv_t));
	if (!ndev)
		return -ENOMEM;

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		netdev_err(ndev, "irq_of_parse_and_map failed\n");
		ret = -EINVAL;
		goto irq_map_fail;
	}

	priv = netdev_priv(ndev);
	priv->ndev = ndev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ndev->base_addr = res->start;
	priv->base = devm_ioremap_resource(p_dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(p_dev, "devm_ioremap_resource failed\n");
		ret = PTR_ERR(priv->base);
		goto init_fail;
	}

	spin_lock_init(&priv->txlock);

	priv->tx_buf_size = TX_BUF_SIZE;
	priv->rx_buf_size = RX_BUF_SIZE;

	priv->tx_desc_base = dma_alloc_coherent(NULL, TX_REG_DESC_SIZE *
						TX_DESC_NUM, &priv->tx_base,
						GFP_DMA | GFP_KERNEL);
	if (!priv->tx_desc_base) {
		ret = -ENOMEM;
		goto init_fail;
	}

	priv->rx_desc_base = dma_alloc_coherent(NULL, RX_REG_DESC_SIZE *
						RX_DESC_NUM, &priv->rx_base,
						GFP_DMA | GFP_KERNEL);
	if (!priv->rx_desc_base) {
		ret = -ENOMEM;
		goto init_fail;
	}

	priv->tx_buf_base = kmalloc_array(priv->tx_buf_size, TX_DESC_NUM,
					  GFP_ATOMIC);
	if (!priv->tx_buf_base) {
		ret = -ENOMEM;
		goto init_fail;
	}

	priv->rx_buf_base = kmalloc_array(priv->rx_buf_size, RX_DESC_NUM,
					  GFP_ATOMIC);
	if (!priv->rx_buf_base) {
		ret = -ENOMEM;
		goto init_fail;
	}

	platform_set_drvdata(pdev, ndev);

	ret = devm_request_irq(p_dev, irq, moxart_mac_interrupt, 0,
			       pdev->name, ndev);
	if (ret) {
		netdev_err(ndev, "devm_request_irq failed\n");
		goto init_fail;
	}

	ndev->netdev_ops = &moxart_netdev_ops;
	netif_napi_add(ndev, &priv->napi, moxart_rx_poll, RX_DESC_NUM);
	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->irq = irq;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	ret = register_netdev(ndev);
	if (ret) {
		free_netdev(ndev);
		goto init_fail;
	}

	netdev_dbg(ndev, "%s: IRQ=%d address=%pM\n",
		   __func__, ndev->irq, ndev->dev_addr);

	return 0;

init_fail:
	netdev_err(ndev, "init failed\n");
	moxart_mac_free_memory(ndev);
irq_map_fail:
	free_netdev(ndev);
	return ret;
}

static int moxart_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);
	devm_free_irq(&pdev->dev, ndev->irq, ndev);
	moxart_mac_free_memory(ndev);
	free_netdev(ndev);

	return 0;
}

static const struct of_device_id moxart_mac_match[] = {
	{ .compatible = "moxa,moxart-mac" },
	{ }
};
MODULE_DEVICE_TABLE(of, moxart_mac_match);

static struct platform_driver moxart_mac_driver = {
	.probe	= moxart_mac_probe,
	.remove	= moxart_remove,
	.driver	= {
		.name		= "moxart-ethernet",
		.of_match_table	= moxart_mac_match,
	},
};
module_platform_driver(moxart_mac_driver);

MODULE_DESCRIPTION("MOXART RTL8201CP Ethernet driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jonas Jensen <jonas.jensen@gmail.com>");
