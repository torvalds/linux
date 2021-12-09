// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2016-2017, National Instruments Corp.
 *
 * Author: Moritz Fischer <mdf@kernel.org>
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/skbuff.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/nvmem-consumer.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>

#define TX_BD_NUM		64
#define RX_BD_NUM		128

/* Axi DMA Register definitions */
#define XAXIDMA_TX_CR_OFFSET	0x00 /* Channel control */
#define XAXIDMA_TX_SR_OFFSET	0x04 /* Status */
#define XAXIDMA_TX_CDESC_OFFSET	0x08 /* Current descriptor pointer */
#define XAXIDMA_TX_TDESC_OFFSET	0x10 /* Tail descriptor pointer */

#define XAXIDMA_RX_CR_OFFSET	0x30 /* Channel control */
#define XAXIDMA_RX_SR_OFFSET	0x34 /* Status */
#define XAXIDMA_RX_CDESC_OFFSET	0x38 /* Current descriptor pointer */
#define XAXIDMA_RX_TDESC_OFFSET	0x40 /* Tail descriptor pointer */

#define XAXIDMA_CR_RUNSTOP_MASK	0x1 /* Start/stop DMA channel */
#define XAXIDMA_CR_RESET_MASK	0x4 /* Reset DMA engine */

#define XAXIDMA_BD_CTRL_LENGTH_MASK	0x007FFFFF /* Requested len */
#define XAXIDMA_BD_CTRL_TXSOF_MASK	0x08000000 /* First tx packet */
#define XAXIDMA_BD_CTRL_TXEOF_MASK	0x04000000 /* Last tx packet */
#define XAXIDMA_BD_CTRL_ALL_MASK	0x0C000000 /* All control bits */

#define XAXIDMA_DELAY_MASK		0xFF000000 /* Delay timeout counter */
#define XAXIDMA_COALESCE_MASK		0x00FF0000 /* Coalesce counter */

#define XAXIDMA_DELAY_SHIFT		24
#define XAXIDMA_COALESCE_SHIFT		16

#define XAXIDMA_IRQ_IOC_MASK		0x00001000 /* Completion intr */
#define XAXIDMA_IRQ_DELAY_MASK		0x00002000 /* Delay interrupt */
#define XAXIDMA_IRQ_ERROR_MASK		0x00004000 /* Error interrupt */
#define XAXIDMA_IRQ_ALL_MASK		0x00007000 /* All interrupts */

/* Default TX/RX Threshold and waitbound values for SGDMA mode */
#define XAXIDMA_DFT_TX_THRESHOLD	24
#define XAXIDMA_DFT_TX_WAITBOUND	254
#define XAXIDMA_DFT_RX_THRESHOLD	24
#define XAXIDMA_DFT_RX_WAITBOUND	254

#define XAXIDMA_BD_STS_ACTUAL_LEN_MASK	0x007FFFFF /* Actual len */
#define XAXIDMA_BD_STS_COMPLETE_MASK	0x80000000 /* Completed */
#define XAXIDMA_BD_STS_DEC_ERR_MASK	0x40000000 /* Decode error */
#define XAXIDMA_BD_STS_SLV_ERR_MASK	0x20000000 /* Slave error */
#define XAXIDMA_BD_STS_INT_ERR_MASK	0x10000000 /* Internal err */
#define XAXIDMA_BD_STS_ALL_ERR_MASK	0x70000000 /* All errors */
#define XAXIDMA_BD_STS_RXSOF_MASK	0x08000000 /* First rx pkt */
#define XAXIDMA_BD_STS_RXEOF_MASK	0x04000000 /* Last rx pkt */
#define XAXIDMA_BD_STS_ALL_MASK		0xFC000000 /* All status bits */

#define NIXGE_REG_CTRL_OFFSET	0x4000
#define NIXGE_REG_INFO		0x00
#define NIXGE_REG_MAC_CTL	0x04
#define NIXGE_REG_PHY_CTL	0x08
#define NIXGE_REG_LED_CTL	0x0c
#define NIXGE_REG_MDIO_DATA	0x10
#define NIXGE_REG_MDIO_ADDR	0x14
#define NIXGE_REG_MDIO_OP	0x18
#define NIXGE_REG_MDIO_CTRL	0x1c

#define NIXGE_ID_LED_CTL_EN	BIT(0)
#define NIXGE_ID_LED_CTL_VAL	BIT(1)

#define NIXGE_MDIO_CLAUSE45	BIT(12)
#define NIXGE_MDIO_CLAUSE22	0
#define NIXGE_MDIO_OP(n)     (((n) & 0x3) << 10)
#define NIXGE_MDIO_OP_ADDRESS	0
#define NIXGE_MDIO_C45_WRITE	BIT(0)
#define NIXGE_MDIO_C45_READ	(BIT(1) | BIT(0))
#define NIXGE_MDIO_C22_WRITE	BIT(0)
#define NIXGE_MDIO_C22_READ	BIT(1)
#define NIXGE_MDIO_ADDR(n)   (((n) & 0x1f) << 5)
#define NIXGE_MDIO_MMD(n)    (((n) & 0x1f) << 0)

#define NIXGE_REG_MAC_LSB	0x1000
#define NIXGE_REG_MAC_MSB	0x1004

/* Packet size info */
#define NIXGE_HDR_SIZE		14 /* Size of Ethernet header */
#define NIXGE_TRL_SIZE		4 /* Size of Ethernet trailer (FCS) */
#define NIXGE_MTU		1500 /* Max MTU of an Ethernet frame */
#define NIXGE_JUMBO_MTU		9000 /* Max MTU of a jumbo Eth. frame */

#define NIXGE_MAX_FRAME_SIZE	 (NIXGE_MTU + NIXGE_HDR_SIZE + NIXGE_TRL_SIZE)
#define NIXGE_MAX_JUMBO_FRAME_SIZE \
	(NIXGE_JUMBO_MTU + NIXGE_HDR_SIZE + NIXGE_TRL_SIZE)

enum nixge_version {
	NIXGE_V2,
	NIXGE_V3,
	NIXGE_VERSION_COUNT
};

struct nixge_hw_dma_bd {
	u32 next_lo;
	u32 next_hi;
	u32 phys_lo;
	u32 phys_hi;
	u32 reserved3;
	u32 reserved4;
	u32 cntrl;
	u32 status;
	u32 app0;
	u32 app1;
	u32 app2;
	u32 app3;
	u32 app4;
	u32 sw_id_offset_lo;
	u32 sw_id_offset_hi;
	u32 reserved6;
};

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define nixge_hw_dma_bd_set_addr(bd, field, addr) \
	do { \
		(bd)->field##_lo = lower_32_bits((addr)); \
		(bd)->field##_hi = upper_32_bits((addr)); \
	} while (0)
#else
#define nixge_hw_dma_bd_set_addr(bd, field, addr) \
	((bd)->field##_lo = lower_32_bits((addr)))
#endif

#define nixge_hw_dma_bd_set_phys(bd, addr) \
	nixge_hw_dma_bd_set_addr((bd), phys, (addr))

#define nixge_hw_dma_bd_set_next(bd, addr) \
	nixge_hw_dma_bd_set_addr((bd), next, (addr))

#define nixge_hw_dma_bd_set_offset(bd, addr) \
	nixge_hw_dma_bd_set_addr((bd), sw_id_offset, (addr))

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define nixge_hw_dma_bd_get_addr(bd, field) \
	(dma_addr_t)((((u64)(bd)->field##_hi) << 32) | ((bd)->field##_lo))
#else
#define nixge_hw_dma_bd_get_addr(bd, field) \
	(dma_addr_t)((bd)->field##_lo)
#endif

struct nixge_tx_skb {
	struct sk_buff *skb;
	dma_addr_t mapping;
	size_t size;
	bool mapped_as_page;
};

struct nixge_priv {
	struct net_device *ndev;
	struct napi_struct napi;
	struct device *dev;

	/* Connection to PHY device */
	struct device_node *phy_node;
	phy_interface_t		phy_mode;

	int link;
	unsigned int speed;
	unsigned int duplex;

	/* MDIO bus data */
	struct mii_bus *mii_bus;	/* MII bus reference */

	/* IO registers, dma functions and IRQs */
	void __iomem *ctrl_regs;
	void __iomem *dma_regs;

	struct tasklet_struct dma_err_tasklet;

	int tx_irq;
	int rx_irq;

	/* Buffer descriptors */
	struct nixge_hw_dma_bd *tx_bd_v;
	struct nixge_tx_skb *tx_skb;
	dma_addr_t tx_bd_p;

	struct nixge_hw_dma_bd *rx_bd_v;
	dma_addr_t rx_bd_p;
	u32 tx_bd_ci;
	u32 tx_bd_tail;
	u32 rx_bd_ci;

	u32 coalesce_count_rx;
	u32 coalesce_count_tx;
};

static void nixge_dma_write_reg(struct nixge_priv *priv, off_t offset, u32 val)
{
	writel(val, priv->dma_regs + offset);
}

static void nixge_dma_write_desc_reg(struct nixge_priv *priv, off_t offset,
				     dma_addr_t addr)
{
	writel(lower_32_bits(addr), priv->dma_regs + offset);
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	writel(upper_32_bits(addr), priv->dma_regs + offset + 4);
#endif
}

static u32 nixge_dma_read_reg(const struct nixge_priv *priv, off_t offset)
{
	return readl(priv->dma_regs + offset);
}

static void nixge_ctrl_write_reg(struct nixge_priv *priv, off_t offset, u32 val)
{
	writel(val, priv->ctrl_regs + offset);
}

static u32 nixge_ctrl_read_reg(struct nixge_priv *priv, off_t offset)
{
	return readl(priv->ctrl_regs + offset);
}

#define nixge_ctrl_poll_timeout(priv, addr, val, cond, sleep_us, timeout_us) \
	readl_poll_timeout((priv)->ctrl_regs + (addr), (val), (cond), \
			   (sleep_us), (timeout_us))

#define nixge_dma_poll_timeout(priv, addr, val, cond, sleep_us, timeout_us) \
	readl_poll_timeout((priv)->dma_regs + (addr), (val), (cond), \
			   (sleep_us), (timeout_us))

static void nixge_hw_dma_bd_release(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	dma_addr_t phys_addr;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < RX_BD_NUM; i++) {
		phys_addr = nixge_hw_dma_bd_get_addr(&priv->rx_bd_v[i],
						     phys);

		dma_unmap_single(ndev->dev.parent, phys_addr,
				 NIXGE_MAX_JUMBO_FRAME_SIZE,
				 DMA_FROM_DEVICE);

		skb = (struct sk_buff *)(uintptr_t)
			nixge_hw_dma_bd_get_addr(&priv->rx_bd_v[i],
						 sw_id_offset);
		dev_kfree_skb(skb);
	}

	if (priv->rx_bd_v)
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*priv->rx_bd_v) * RX_BD_NUM,
				  priv->rx_bd_v,
				  priv->rx_bd_p);

	if (priv->tx_skb)
		devm_kfree(ndev->dev.parent, priv->tx_skb);

	if (priv->tx_bd_v)
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*priv->tx_bd_v) * TX_BD_NUM,
				  priv->tx_bd_v,
				  priv->tx_bd_p);
}

static int nixge_hw_dma_bd_init(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	dma_addr_t phys;
	u32 cr;
	int i;

	/* Reset the indexes which are used for accessing the BDs */
	priv->tx_bd_ci = 0;
	priv->tx_bd_tail = 0;
	priv->rx_bd_ci = 0;

	/* Allocate the Tx and Rx buffer descriptors. */
	priv->tx_bd_v = dma_alloc_coherent(ndev->dev.parent,
					   sizeof(*priv->tx_bd_v) * TX_BD_NUM,
					   &priv->tx_bd_p, GFP_KERNEL);
	if (!priv->tx_bd_v)
		goto out;

	priv->tx_skb = devm_kcalloc(ndev->dev.parent,
				    TX_BD_NUM, sizeof(*priv->tx_skb),
				    GFP_KERNEL);
	if (!priv->tx_skb)
		goto out;

	priv->rx_bd_v = dma_alloc_coherent(ndev->dev.parent,
					   sizeof(*priv->rx_bd_v) * RX_BD_NUM,
					   &priv->rx_bd_p, GFP_KERNEL);
	if (!priv->rx_bd_v)
		goto out;

	for (i = 0; i < TX_BD_NUM; i++) {
		nixge_hw_dma_bd_set_next(&priv->tx_bd_v[i],
					 priv->tx_bd_p +
					 sizeof(*priv->tx_bd_v) *
					 ((i + 1) % TX_BD_NUM));
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		nixge_hw_dma_bd_set_next(&priv->rx_bd_v[i],
					 priv->rx_bd_p
					 + sizeof(*priv->rx_bd_v) *
					 ((i + 1) % RX_BD_NUM));

		skb = netdev_alloc_skb_ip_align(ndev,
						NIXGE_MAX_JUMBO_FRAME_SIZE);
		if (!skb)
			goto out;

		nixge_hw_dma_bd_set_offset(&priv->rx_bd_v[i], (uintptr_t)skb);
		phys = dma_map_single(ndev->dev.parent, skb->data,
				      NIXGE_MAX_JUMBO_FRAME_SIZE,
				      DMA_FROM_DEVICE);

		nixge_hw_dma_bd_set_phys(&priv->rx_bd_v[i], phys);

		priv->rx_bd_v[i].cntrl = NIXGE_MAX_JUMBO_FRAME_SIZE;
	}

	/* Start updating the Rx channel control register */
	cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XAXIDMA_COALESCE_MASK) |
	      ((priv->coalesce_count_rx) << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XAXIDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Write to the Rx channel control register */
	nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET, cr);

	/* Start updating the Tx channel control register */
	cr = nixge_dma_read_reg(priv, XAXIDMA_TX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XAXIDMA_COALESCE_MASK)) |
	      ((priv->coalesce_count_tx) << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XAXIDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Write to the Tx channel control register */
	nixge_dma_write_reg(priv, XAXIDMA_TX_CR_OFFSET, cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	nixge_dma_write_desc_reg(priv, XAXIDMA_RX_CDESC_OFFSET, priv->rx_bd_p);
	cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
	nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET,
			    cr | XAXIDMA_CR_RUNSTOP_MASK);
	nixge_dma_write_desc_reg(priv, XAXIDMA_RX_TDESC_OFFSET, priv->rx_bd_p +
			    (sizeof(*priv->rx_bd_v) * (RX_BD_NUM - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting.
	 */
	nixge_dma_write_desc_reg(priv, XAXIDMA_TX_CDESC_OFFSET, priv->tx_bd_p);
	cr = nixge_dma_read_reg(priv, XAXIDMA_TX_CR_OFFSET);
	nixge_dma_write_reg(priv, XAXIDMA_TX_CR_OFFSET,
			    cr | XAXIDMA_CR_RUNSTOP_MASK);

	return 0;
out:
	nixge_hw_dma_bd_release(ndev);
	return -ENOMEM;
}

static void __nixge_device_reset(struct nixge_priv *priv, off_t offset)
{
	u32 status;
	int err;

	/* Reset Axi DMA. This would reset NIXGE Ethernet core as well.
	 * The reset process of Axi DMA takes a while to complete as all
	 * pending commands/transfers will be flushed or completed during
	 * this reset process.
	 */
	nixge_dma_write_reg(priv, offset, XAXIDMA_CR_RESET_MASK);
	err = nixge_dma_poll_timeout(priv, offset, status,
				     !(status & XAXIDMA_CR_RESET_MASK), 10,
				     1000);
	if (err)
		netdev_err(priv->ndev, "%s: DMA reset timeout!\n", __func__);
}

static void nixge_device_reset(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);

	__nixge_device_reset(priv, XAXIDMA_TX_CR_OFFSET);
	__nixge_device_reset(priv, XAXIDMA_RX_CR_OFFSET);

	if (nixge_hw_dma_bd_init(ndev))
		netdev_err(ndev, "%s: descriptor allocation failed\n",
			   __func__);

	netif_trans_update(ndev);
}

static void nixge_handle_link_change(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (phydev->link != priv->link || phydev->speed != priv->speed ||
	    phydev->duplex != priv->duplex) {
		priv->link = phydev->link;
		priv->speed = phydev->speed;
		priv->duplex = phydev->duplex;
		phy_print_status(phydev);
	}
}

static void nixge_tx_skb_unmap(struct nixge_priv *priv,
			       struct nixge_tx_skb *tx_skb)
{
	if (tx_skb->mapping) {
		if (tx_skb->mapped_as_page)
			dma_unmap_page(priv->ndev->dev.parent, tx_skb->mapping,
				       tx_skb->size, DMA_TO_DEVICE);
		else
			dma_unmap_single(priv->ndev->dev.parent,
					 tx_skb->mapping,
					 tx_skb->size, DMA_TO_DEVICE);
		tx_skb->mapping = 0;
	}

	if (tx_skb->skb) {
		dev_kfree_skb_any(tx_skb->skb);
		tx_skb->skb = NULL;
	}
}

static void nixge_start_xmit_done(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	struct nixge_hw_dma_bd *cur_p;
	struct nixge_tx_skb *tx_skb;
	unsigned int status = 0;
	u32 packets = 0;
	u32 size = 0;

	cur_p = &priv->tx_bd_v[priv->tx_bd_ci];
	tx_skb = &priv->tx_skb[priv->tx_bd_ci];

	status = cur_p->status;

	while (status & XAXIDMA_BD_STS_COMPLETE_MASK) {
		nixge_tx_skb_unmap(priv, tx_skb);
		cur_p->status = 0;

		size += status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		packets++;

		++priv->tx_bd_ci;
		priv->tx_bd_ci %= TX_BD_NUM;
		cur_p = &priv->tx_bd_v[priv->tx_bd_ci];
		tx_skb = &priv->tx_skb[priv->tx_bd_ci];
		status = cur_p->status;
	}

	ndev->stats.tx_packets += packets;
	ndev->stats.tx_bytes += size;

	if (packets)
		netif_wake_queue(ndev);
}

static int nixge_check_tx_bd_space(struct nixge_priv *priv,
				   int num_frag)
{
	struct nixge_hw_dma_bd *cur_p;

	cur_p = &priv->tx_bd_v[(priv->tx_bd_tail + num_frag) % TX_BD_NUM];
	if (cur_p->status & XAXIDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
	return 0;
}

static netdev_tx_t nixge_start_xmit(struct sk_buff *skb,
				    struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	struct nixge_hw_dma_bd *cur_p;
	struct nixge_tx_skb *tx_skb;
	dma_addr_t tail_p, cur_phys;
	skb_frag_t *frag;
	u32 num_frag;
	u32 ii;

	num_frag = skb_shinfo(skb)->nr_frags;
	cur_p = &priv->tx_bd_v[priv->tx_bd_tail];
	tx_skb = &priv->tx_skb[priv->tx_bd_tail];

	if (nixge_check_tx_bd_space(priv, num_frag)) {
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
		return NETDEV_TX_OK;
	}

	cur_phys = dma_map_single(ndev->dev.parent, skb->data,
				  skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, cur_phys))
		goto drop;
	nixge_hw_dma_bd_set_phys(cur_p, cur_phys);

	cur_p->cntrl = skb_headlen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK;

	tx_skb->skb = NULL;
	tx_skb->mapping = cur_phys;
	tx_skb->size = skb_headlen(skb);
	tx_skb->mapped_as_page = false;

	for (ii = 0; ii < num_frag; ii++) {
		++priv->tx_bd_tail;
		priv->tx_bd_tail %= TX_BD_NUM;
		cur_p = &priv->tx_bd_v[priv->tx_bd_tail];
		tx_skb = &priv->tx_skb[priv->tx_bd_tail];
		frag = &skb_shinfo(skb)->frags[ii];

		cur_phys = skb_frag_dma_map(ndev->dev.parent, frag, 0,
					    skb_frag_size(frag),
					    DMA_TO_DEVICE);
		if (dma_mapping_error(ndev->dev.parent, cur_phys))
			goto frag_err;
		nixge_hw_dma_bd_set_phys(cur_p, cur_phys);

		cur_p->cntrl = skb_frag_size(frag);

		tx_skb->skb = NULL;
		tx_skb->mapping = cur_phys;
		tx_skb->size = skb_frag_size(frag);
		tx_skb->mapped_as_page = true;
	}

	/* last buffer of the frame */
	tx_skb->skb = skb;

	cur_p->cntrl |= XAXIDMA_BD_CTRL_TXEOF_MASK;

	tail_p = priv->tx_bd_p + sizeof(*priv->tx_bd_v) * priv->tx_bd_tail;
	/* Start the transfer */
	nixge_dma_write_desc_reg(priv, XAXIDMA_TX_TDESC_OFFSET, tail_p);
	++priv->tx_bd_tail;
	priv->tx_bd_tail %= TX_BD_NUM;

	return NETDEV_TX_OK;
frag_err:
	for (; ii > 0; ii--) {
		if (priv->tx_bd_tail)
			priv->tx_bd_tail--;
		else
			priv->tx_bd_tail = TX_BD_NUM - 1;

		tx_skb = &priv->tx_skb[priv->tx_bd_tail];
		nixge_tx_skb_unmap(priv, tx_skb);

		cur_p = &priv->tx_bd_v[priv->tx_bd_tail];
		cur_p->status = 0;
	}
	dma_unmap_single(priv->ndev->dev.parent,
			 tx_skb->mapping,
			 tx_skb->size, DMA_TO_DEVICE);
drop:
	ndev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static int nixge_recv(struct net_device *ndev, int budget)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb, *new_skb;
	struct nixge_hw_dma_bd *cur_p;
	dma_addr_t tail_p = 0, cur_phys = 0;
	u32 packets = 0;
	u32 length = 0;
	u32 size = 0;

	cur_p = &priv->rx_bd_v[priv->rx_bd_ci];

	while ((cur_p->status & XAXIDMA_BD_STS_COMPLETE_MASK &&
		budget > packets)) {
		tail_p = priv->rx_bd_p + sizeof(*priv->rx_bd_v) *
			 priv->rx_bd_ci;

		skb = (struct sk_buff *)(uintptr_t)
			nixge_hw_dma_bd_get_addr(cur_p, sw_id_offset);

		length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		if (length > NIXGE_MAX_JUMBO_FRAME_SIZE)
			length = NIXGE_MAX_JUMBO_FRAME_SIZE;

		dma_unmap_single(ndev->dev.parent,
				 nixge_hw_dma_bd_get_addr(cur_p, phys),
				 NIXGE_MAX_JUMBO_FRAME_SIZE,
				 DMA_FROM_DEVICE);

		skb_put(skb, length);

		skb->protocol = eth_type_trans(skb, ndev);
		skb_checksum_none_assert(skb);

		/* For now mark them as CHECKSUM_NONE since
		 * we don't have offload capabilities
		 */
		skb->ip_summed = CHECKSUM_NONE;

		napi_gro_receive(&priv->napi, skb);

		size += length;
		packets++;

		new_skb = netdev_alloc_skb_ip_align(ndev,
						    NIXGE_MAX_JUMBO_FRAME_SIZE);
		if (!new_skb)
			return packets;

		cur_phys = dma_map_single(ndev->dev.parent, new_skb->data,
					  NIXGE_MAX_JUMBO_FRAME_SIZE,
					  DMA_FROM_DEVICE);
		if (dma_mapping_error(ndev->dev.parent, cur_phys)) {
			/* FIXME: bail out and clean up */
			netdev_err(ndev, "Failed to map ...\n");
		}
		nixge_hw_dma_bd_set_phys(cur_p, cur_phys);
		cur_p->cntrl = NIXGE_MAX_JUMBO_FRAME_SIZE;
		cur_p->status = 0;
		nixge_hw_dma_bd_set_offset(cur_p, (uintptr_t)new_skb);

		++priv->rx_bd_ci;
		priv->rx_bd_ci %= RX_BD_NUM;
		cur_p = &priv->rx_bd_v[priv->rx_bd_ci];
	}

	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;

	if (tail_p)
		nixge_dma_write_desc_reg(priv, XAXIDMA_RX_TDESC_OFFSET, tail_p);

	return packets;
}

static int nixge_poll(struct napi_struct *napi, int budget)
{
	struct nixge_priv *priv = container_of(napi, struct nixge_priv, napi);
	int work_done;
	u32 status, cr;

	work_done = 0;

	work_done = nixge_recv(priv->ndev, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		status = nixge_dma_read_reg(priv, XAXIDMA_RX_SR_OFFSET);

		if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
			/* If there's more, reschedule, but clear */
			nixge_dma_write_reg(priv, XAXIDMA_RX_SR_OFFSET, status);
			napi_reschedule(napi);
		} else {
			/* if not, turn on RX IRQs again ... */
			cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
			cr |= (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK);
			nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET, cr);
		}
	}

	return work_done;
}

static irqreturn_t nixge_tx_irq(int irq, void *_ndev)
{
	struct nixge_priv *priv = netdev_priv(_ndev);
	struct net_device *ndev = _ndev;
	unsigned int status;
	dma_addr_t phys;
	u32 cr;

	status = nixge_dma_read_reg(priv, XAXIDMA_TX_SR_OFFSET);
	if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
		nixge_dma_write_reg(priv, XAXIDMA_TX_SR_OFFSET, status);
		nixge_start_xmit_done(priv->ndev);
		goto out;
	}
	if (!(status & XAXIDMA_IRQ_ALL_MASK)) {
		netdev_err(ndev, "No interrupts asserted in Tx path\n");
		return IRQ_NONE;
	}
	if (status & XAXIDMA_IRQ_ERROR_MASK) {
		phys = nixge_hw_dma_bd_get_addr(&priv->tx_bd_v[priv->tx_bd_ci],
						phys);

		netdev_err(ndev, "DMA Tx error 0x%x\n", status);
		netdev_err(ndev, "Current BD is at: 0x%llx\n", (u64)phys);

		cr = nixge_dma_read_reg(priv, XAXIDMA_TX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Write to the Tx channel control register */
		nixge_dma_write_reg(priv, XAXIDMA_TX_CR_OFFSET, cr);

		cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Write to the Rx channel control register */
		nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET, cr);

		tasklet_schedule(&priv->dma_err_tasklet);
		nixge_dma_write_reg(priv, XAXIDMA_TX_SR_OFFSET, status);
	}
out:
	return IRQ_HANDLED;
}

static irqreturn_t nixge_rx_irq(int irq, void *_ndev)
{
	struct nixge_priv *priv = netdev_priv(_ndev);
	struct net_device *ndev = _ndev;
	unsigned int status;
	dma_addr_t phys;
	u32 cr;

	status = nixge_dma_read_reg(priv, XAXIDMA_RX_SR_OFFSET);
	if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
		/* Turn of IRQs because NAPI */
		nixge_dma_write_reg(priv, XAXIDMA_RX_SR_OFFSET, status);
		cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
		cr &= ~(XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK);
		nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET, cr);

		if (napi_schedule_prep(&priv->napi))
			__napi_schedule(&priv->napi);
		goto out;
	}
	if (!(status & XAXIDMA_IRQ_ALL_MASK)) {
		netdev_err(ndev, "No interrupts asserted in Rx path\n");
		return IRQ_NONE;
	}
	if (status & XAXIDMA_IRQ_ERROR_MASK) {
		phys = nixge_hw_dma_bd_get_addr(&priv->rx_bd_v[priv->rx_bd_ci],
						phys);
		netdev_err(ndev, "DMA Rx error 0x%x\n", status);
		netdev_err(ndev, "Current BD is at: 0x%llx\n", (u64)phys);

		cr = nixge_dma_read_reg(priv, XAXIDMA_TX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Finally write to the Tx channel control register */
		nixge_dma_write_reg(priv, XAXIDMA_TX_CR_OFFSET, cr);

		cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* write to the Rx channel control register */
		nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET, cr);

		tasklet_schedule(&priv->dma_err_tasklet);
		nixge_dma_write_reg(priv, XAXIDMA_RX_SR_OFFSET, status);
	}
out:
	return IRQ_HANDLED;
}

static void nixge_dma_err_handler(struct tasklet_struct *t)
{
	struct nixge_priv *lp = from_tasklet(lp, t, dma_err_tasklet);
	struct nixge_hw_dma_bd *cur_p;
	struct nixge_tx_skb *tx_skb;
	u32 cr, i;

	__nixge_device_reset(lp, XAXIDMA_TX_CR_OFFSET);
	__nixge_device_reset(lp, XAXIDMA_RX_CR_OFFSET);

	for (i = 0; i < TX_BD_NUM; i++) {
		cur_p = &lp->tx_bd_v[i];
		tx_skb = &lp->tx_skb[i];
		nixge_tx_skb_unmap(lp, tx_skb);

		nixge_hw_dma_bd_set_phys(cur_p, 0);
		cur_p->cntrl = 0;
		cur_p->status = 0;
		nixge_hw_dma_bd_set_offset(cur_p, 0);
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		cur_p = &lp->rx_bd_v[i];
		cur_p->status = 0;
	}

	lp->tx_bd_ci = 0;
	lp->tx_bd_tail = 0;
	lp->rx_bd_ci = 0;

	/* Start updating the Rx channel control register */
	cr = nixge_dma_read_reg(lp, XAXIDMA_RX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XAXIDMA_COALESCE_MASK) |
	      (XAXIDMA_DFT_RX_THRESHOLD << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XAXIDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Finally write to the Rx channel control register */
	nixge_dma_write_reg(lp, XAXIDMA_RX_CR_OFFSET, cr);

	/* Start updating the Tx channel control register */
	cr = nixge_dma_read_reg(lp, XAXIDMA_TX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XAXIDMA_COALESCE_MASK)) |
	      (XAXIDMA_DFT_TX_THRESHOLD << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XAXIDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Finally write to the Tx channel control register */
	nixge_dma_write_reg(lp, XAXIDMA_TX_CR_OFFSET, cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	nixge_dma_write_desc_reg(lp, XAXIDMA_RX_CDESC_OFFSET, lp->rx_bd_p);
	cr = nixge_dma_read_reg(lp, XAXIDMA_RX_CR_OFFSET);
	nixge_dma_write_reg(lp, XAXIDMA_RX_CR_OFFSET,
			    cr | XAXIDMA_CR_RUNSTOP_MASK);
	nixge_dma_write_desc_reg(lp, XAXIDMA_RX_TDESC_OFFSET, lp->rx_bd_p +
			    (sizeof(*lp->rx_bd_v) * (RX_BD_NUM - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting
	 */
	nixge_dma_write_desc_reg(lp, XAXIDMA_TX_CDESC_OFFSET, lp->tx_bd_p);
	cr = nixge_dma_read_reg(lp, XAXIDMA_TX_CR_OFFSET);
	nixge_dma_write_reg(lp, XAXIDMA_TX_CR_OFFSET,
			    cr | XAXIDMA_CR_RUNSTOP_MASK);
}

static int nixge_open(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	struct phy_device *phy;
	int ret;

	nixge_device_reset(ndev);

	phy = of_phy_connect(ndev, priv->phy_node,
			     &nixge_handle_link_change, 0, priv->phy_mode);
	if (!phy)
		return -ENODEV;

	phy_start(phy);

	/* Enable tasklets for Axi DMA error handling */
	tasklet_setup(&priv->dma_err_tasklet, nixge_dma_err_handler);

	napi_enable(&priv->napi);

	/* Enable interrupts for Axi DMA Tx */
	ret = request_irq(priv->tx_irq, nixge_tx_irq, 0, ndev->name, ndev);
	if (ret)
		goto err_tx_irq;
	/* Enable interrupts for Axi DMA Rx */
	ret = request_irq(priv->rx_irq, nixge_rx_irq, 0, ndev->name, ndev);
	if (ret)
		goto err_rx_irq;

	netif_start_queue(ndev);

	return 0;

err_rx_irq:
	free_irq(priv->tx_irq, ndev);
err_tx_irq:
	phy_stop(phy);
	phy_disconnect(phy);
	tasklet_kill(&priv->dma_err_tasklet);
	netdev_err(ndev, "request_irq() failed\n");
	return ret;
}

static int nixge_stop(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	u32 cr;

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);

	if (ndev->phydev) {
		phy_stop(ndev->phydev);
		phy_disconnect(ndev->phydev);
	}

	cr = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
	nixge_dma_write_reg(priv, XAXIDMA_RX_CR_OFFSET,
			    cr & (~XAXIDMA_CR_RUNSTOP_MASK));
	cr = nixge_dma_read_reg(priv, XAXIDMA_TX_CR_OFFSET);
	nixge_dma_write_reg(priv, XAXIDMA_TX_CR_OFFSET,
			    cr & (~XAXIDMA_CR_RUNSTOP_MASK));

	tasklet_kill(&priv->dma_err_tasklet);

	free_irq(priv->tx_irq, ndev);
	free_irq(priv->rx_irq, ndev);

	nixge_hw_dma_bd_release(ndev);

	return 0;
}

static int nixge_change_mtu(struct net_device *ndev, int new_mtu)
{
	if (netif_running(ndev))
		return -EBUSY;

	if ((new_mtu + NIXGE_HDR_SIZE + NIXGE_TRL_SIZE) >
	     NIXGE_MAX_JUMBO_FRAME_SIZE)
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

static s32 __nixge_hw_set_mac_address(struct net_device *ndev)
{
	struct nixge_priv *priv = netdev_priv(ndev);

	nixge_ctrl_write_reg(priv, NIXGE_REG_MAC_LSB,
			     (ndev->dev_addr[2]) << 24 |
			     (ndev->dev_addr[3] << 16) |
			     (ndev->dev_addr[4] << 8) |
			     (ndev->dev_addr[5] << 0));

	nixge_ctrl_write_reg(priv, NIXGE_REG_MAC_MSB,
			     (ndev->dev_addr[1] | (ndev->dev_addr[0] << 8)));

	return 0;
}

static int nixge_net_set_mac_address(struct net_device *ndev, void *p)
{
	int err;

	err = eth_mac_addr(ndev, p);
	if (!err)
		__nixge_hw_set_mac_address(ndev);

	return err;
}

static const struct net_device_ops nixge_netdev_ops = {
	.ndo_open = nixge_open,
	.ndo_stop = nixge_stop,
	.ndo_start_xmit = nixge_start_xmit,
	.ndo_change_mtu	= nixge_change_mtu,
	.ndo_set_mac_address = nixge_net_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
};

static void nixge_ethtools_get_drvinfo(struct net_device *ndev,
				       struct ethtool_drvinfo *ed)
{
	strlcpy(ed->driver, "nixge", sizeof(ed->driver));
	strlcpy(ed->bus_info, "platform", sizeof(ed->bus_info));
}

static int
nixge_ethtools_get_coalesce(struct net_device *ndev,
			    struct ethtool_coalesce *ecoalesce,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	u32 regval = 0;

	regval = nixge_dma_read_reg(priv, XAXIDMA_RX_CR_OFFSET);
	ecoalesce->rx_max_coalesced_frames = (regval & XAXIDMA_COALESCE_MASK)
					     >> XAXIDMA_COALESCE_SHIFT;
	regval = nixge_dma_read_reg(priv, XAXIDMA_TX_CR_OFFSET);
	ecoalesce->tx_max_coalesced_frames = (regval & XAXIDMA_COALESCE_MASK)
					     >> XAXIDMA_COALESCE_SHIFT;
	return 0;
}

static int
nixge_ethtools_set_coalesce(struct net_device *ndev,
			    struct ethtool_coalesce *ecoalesce,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct nixge_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
			   "Please stop netif before applying configuration\n");
		return -EBUSY;
	}

	if (ecoalesce->rx_max_coalesced_frames)
		priv->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->tx_max_coalesced_frames)
		priv->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;

	return 0;
}

static int nixge_ethtools_set_phys_id(struct net_device *ndev,
				      enum ethtool_phys_id_state state)
{
	struct nixge_priv *priv = netdev_priv(ndev);
	u32 ctrl;

	ctrl = nixge_ctrl_read_reg(priv, NIXGE_REG_LED_CTL);
	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		ctrl |= NIXGE_ID_LED_CTL_EN;
		/* Enable identification LED override*/
		nixge_ctrl_write_reg(priv, NIXGE_REG_LED_CTL, ctrl);
		return 2;

	case ETHTOOL_ID_ON:
		ctrl |= NIXGE_ID_LED_CTL_VAL;
		nixge_ctrl_write_reg(priv, NIXGE_REG_LED_CTL, ctrl);
		break;

	case ETHTOOL_ID_OFF:
		ctrl &= ~NIXGE_ID_LED_CTL_VAL;
		nixge_ctrl_write_reg(priv, NIXGE_REG_LED_CTL, ctrl);
		break;

	case ETHTOOL_ID_INACTIVE:
		/* Restore LED settings */
		ctrl &= ~NIXGE_ID_LED_CTL_EN;
		nixge_ctrl_write_reg(priv, NIXGE_REG_LED_CTL, ctrl);
		break;
	}

	return 0;
}

static const struct ethtool_ops nixge_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_MAX_FRAMES,
	.get_drvinfo    = nixge_ethtools_get_drvinfo,
	.get_coalesce   = nixge_ethtools_get_coalesce,
	.set_coalesce   = nixge_ethtools_set_coalesce,
	.set_phys_id    = nixge_ethtools_set_phys_id,
	.get_link_ksettings     = phy_ethtool_get_link_ksettings,
	.set_link_ksettings     = phy_ethtool_set_link_ksettings,
	.get_link		= ethtool_op_get_link,
};

static int nixge_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct nixge_priv *priv = bus->priv;
	u32 status, tmp;
	int err;
	u16 device;

	if (reg & MII_ADDR_C45) {
		device = (reg >> 16) & 0x1f;

		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_ADDR, reg & 0xffff);

		tmp = NIXGE_MDIO_CLAUSE45 | NIXGE_MDIO_OP(NIXGE_MDIO_OP_ADDRESS)
			| NIXGE_MDIO_ADDR(phy_id) | NIXGE_MDIO_MMD(device);

		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_OP, tmp);
		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_CTRL, 1);

		err = nixge_ctrl_poll_timeout(priv, NIXGE_REG_MDIO_CTRL, status,
					      !status, 10, 1000);
		if (err) {
			dev_err(priv->dev, "timeout setting address");
			return err;
		}

		tmp = NIXGE_MDIO_CLAUSE45 | NIXGE_MDIO_OP(NIXGE_MDIO_C45_READ) |
			NIXGE_MDIO_ADDR(phy_id) | NIXGE_MDIO_MMD(device);
	} else {
		device = reg & 0x1f;

		tmp = NIXGE_MDIO_CLAUSE22 | NIXGE_MDIO_OP(NIXGE_MDIO_C22_READ) |
			NIXGE_MDIO_ADDR(phy_id) | NIXGE_MDIO_MMD(device);
	}

	nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_OP, tmp);
	nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_CTRL, 1);

	err = nixge_ctrl_poll_timeout(priv, NIXGE_REG_MDIO_CTRL, status,
				      !status, 10, 1000);
	if (err) {
		dev_err(priv->dev, "timeout setting read command");
		return err;
	}

	status = nixge_ctrl_read_reg(priv, NIXGE_REG_MDIO_DATA);

	return status;
}

static int nixge_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 val)
{
	struct nixge_priv *priv = bus->priv;
	u32 status, tmp;
	u16 device;
	int err;

	if (reg & MII_ADDR_C45) {
		device = (reg >> 16) & 0x1f;

		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_ADDR, reg & 0xffff);

		tmp = NIXGE_MDIO_CLAUSE45 | NIXGE_MDIO_OP(NIXGE_MDIO_OP_ADDRESS)
			| NIXGE_MDIO_ADDR(phy_id) | NIXGE_MDIO_MMD(device);

		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_OP, tmp);
		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_CTRL, 1);

		err = nixge_ctrl_poll_timeout(priv, NIXGE_REG_MDIO_CTRL, status,
					      !status, 10, 1000);
		if (err) {
			dev_err(priv->dev, "timeout setting address");
			return err;
		}

		tmp = NIXGE_MDIO_CLAUSE45 | NIXGE_MDIO_OP(NIXGE_MDIO_C45_WRITE)
			| NIXGE_MDIO_ADDR(phy_id) | NIXGE_MDIO_MMD(device);

		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_DATA, val);
		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_OP, tmp);
		err = nixge_ctrl_poll_timeout(priv, NIXGE_REG_MDIO_CTRL, status,
					      !status, 10, 1000);
		if (err)
			dev_err(priv->dev, "timeout setting write command");
	} else {
		device = reg & 0x1f;

		tmp = NIXGE_MDIO_CLAUSE22 |
			NIXGE_MDIO_OP(NIXGE_MDIO_C22_WRITE) |
			NIXGE_MDIO_ADDR(phy_id) | NIXGE_MDIO_MMD(device);

		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_DATA, val);
		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_OP, tmp);
		nixge_ctrl_write_reg(priv, NIXGE_REG_MDIO_CTRL, 1);

		err = nixge_ctrl_poll_timeout(priv, NIXGE_REG_MDIO_CTRL, status,
					      !status, 10, 1000);
		if (err)
			dev_err(priv->dev, "timeout setting write command");
	}

	return err;
}

static int nixge_mdio_setup(struct nixge_priv *priv, struct device_node *np)
{
	struct mii_bus *bus;

	bus = devm_mdiobus_alloc(priv->dev);
	if (!bus)
		return -ENOMEM;

	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(priv->dev));
	bus->priv = priv;
	bus->name = "nixge_mii_bus";
	bus->read = nixge_mdio_read;
	bus->write = nixge_mdio_write;
	bus->parent = priv->dev;

	priv->mii_bus = bus;

	return of_mdiobus_register(bus, np);
}

static void *nixge_get_nvmem_address(struct device *dev)
{
	struct nvmem_cell *cell;
	size_t cell_size;
	char *mac;

	cell = nvmem_cell_get(dev, "address");
	if (IS_ERR(cell))
		return NULL;

	mac = nvmem_cell_read(cell, &cell_size);
	nvmem_cell_put(cell);

	return mac;
}

/* Match table for of_platform binding */
static const struct of_device_id nixge_dt_ids[] = {
	{ .compatible = "ni,xge-enet-2.00", .data = (void *)NIXGE_V2 },
	{ .compatible = "ni,xge-enet-3.00", .data = (void *)NIXGE_V3 },
	{},
};
MODULE_DEVICE_TABLE(of, nixge_dt_ids);

static int nixge_of_get_resources(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	enum nixge_version version;
	struct net_device *ndev;
	struct nixge_priv *priv;

	ndev = platform_get_drvdata(pdev);
	priv = netdev_priv(ndev);
	of_id = of_match_node(nixge_dt_ids, pdev->dev.of_node);
	if (!of_id)
		return -ENODEV;

	version = (enum nixge_version)of_id->data;
	if (version <= NIXGE_V2)
		priv->dma_regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	else
		priv->dma_regs = devm_platform_ioremap_resource_byname(pdev, "dma");
	if (IS_ERR(priv->dma_regs)) {
		netdev_err(ndev, "failed to map dma regs\n");
		return PTR_ERR(priv->dma_regs);
	}
	if (version <= NIXGE_V2)
		priv->ctrl_regs = priv->dma_regs + NIXGE_REG_CTRL_OFFSET;
	else
		priv->ctrl_regs = devm_platform_ioremap_resource_byname(pdev, "ctrl");
	if (IS_ERR(priv->ctrl_regs)) {
		netdev_err(ndev, "failed to map ctrl regs\n");
		return PTR_ERR(priv->ctrl_regs);
	}
	return 0;
}

static int nixge_probe(struct platform_device *pdev)
{
	struct device_node *mn, *phy_node;
	struct nixge_priv *priv;
	struct net_device *ndev;
	const u8 *mac_addr;
	int err;

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	ndev->features = NETIF_F_SG;
	ndev->netdev_ops = &nixge_netdev_ops;
	ndev->ethtool_ops = &nixge_ethtool_ops;

	/* MTU range: 64 - 9000 */
	ndev->min_mtu = 64;
	ndev->max_mtu = NIXGE_JUMBO_MTU;

	mac_addr = nixge_get_nvmem_address(&pdev->dev);
	if (mac_addr && is_valid_ether_addr(mac_addr)) {
		ether_addr_copy(ndev->dev_addr, mac_addr);
		kfree(mac_addr);
	} else {
		eth_hw_addr_random(ndev);
	}

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->dev = &pdev->dev;

	netif_napi_add(ndev, &priv->napi, nixge_poll, NAPI_POLL_WEIGHT);
	err = nixge_of_get_resources(pdev);
	if (err)
		goto free_netdev;
	__nixge_hw_set_mac_address(ndev);

	priv->tx_irq = platform_get_irq_byname(pdev, "tx");
	if (priv->tx_irq < 0) {
		netdev_err(ndev, "could not find 'tx' irq");
		err = priv->tx_irq;
		goto free_netdev;
	}

	priv->rx_irq = platform_get_irq_byname(pdev, "rx");
	if (priv->rx_irq < 0) {
		netdev_err(ndev, "could not find 'rx' irq");
		err = priv->rx_irq;
		goto free_netdev;
	}

	priv->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	priv->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

	mn = of_get_child_by_name(pdev->dev.of_node, "mdio");
	if (mn) {
		err = nixge_mdio_setup(priv, mn);
		of_node_put(mn);
		if (err) {
			netdev_err(ndev, "error registering mdio bus");
			goto free_netdev;
		}
	}

	err = of_get_phy_mode(pdev->dev.of_node, &priv->phy_mode);
	if (err) {
		netdev_err(ndev, "not find \"phy-mode\" property\n");
		goto unregister_mdio;
	}

	phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (!phy_node && of_phy_is_fixed_link(pdev->dev.of_node)) {
		err = of_phy_register_fixed_link(pdev->dev.of_node);
		if (err < 0) {
			netdev_err(ndev, "broken fixed-link specification\n");
			goto unregister_mdio;
		}
		phy_node = of_node_get(pdev->dev.of_node);
	}
	priv->phy_node = phy_node;

	err = register_netdev(priv->ndev);
	if (err) {
		netdev_err(ndev, "register_netdev() error (%i)\n", err);
		goto free_phy;
	}

	return 0;

free_phy:
	if (of_phy_is_fixed_link(pdev->dev.of_node))
		of_phy_deregister_fixed_link(pdev->dev.of_node);
	of_node_put(phy_node);

unregister_mdio:
	if (priv->mii_bus)
		mdiobus_unregister(priv->mii_bus);

free_netdev:
	free_netdev(ndev);

	return err;
}

static int nixge_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct nixge_priv *priv = netdev_priv(ndev);

	unregister_netdev(ndev);

	if (of_phy_is_fixed_link(pdev->dev.of_node))
		of_phy_deregister_fixed_link(pdev->dev.of_node);
	of_node_put(priv->phy_node);

	if (priv->mii_bus)
		mdiobus_unregister(priv->mii_bus);

	free_netdev(ndev);

	return 0;
}

static struct platform_driver nixge_driver = {
	.probe		= nixge_probe,
	.remove		= nixge_remove,
	.driver		= {
		.name		= "nixge",
		.of_match_table	= of_match_ptr(nixge_dt_ids),
	},
};
module_platform_driver(nixge_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("National Instruments XGE Management MAC");
MODULE_AUTHOR("Moritz Fischer <mdf@kernel.org>");
