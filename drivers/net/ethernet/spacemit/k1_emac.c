// SPDX-License-Identifier: GPL-2.0
/*
 * SpacemiT K1 Ethernet driver
 *
 * Copyright (C) 2023-2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (C) 2025 Vivian Wang <wangruikang@iscas.ac.cn>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rtnetlink.h>
#include <linux/timer.h>
#include <linux/types.h>

#include "k1_emac.h"

#define DRIVER_NAME "k1_emac"

#define EMAC_DEFAULT_BUFSIZE		1536
#define EMAC_RX_BUF_2K			2048
#define EMAC_RX_BUF_4K			4096

/* Tuning parameters from SpacemiT */
#define EMAC_TX_FRAMES			64
#define EMAC_TX_COAL_TIMEOUT		40000
#define EMAC_RX_FRAMES			64
#define EMAC_RX_COAL_TIMEOUT		(600 * 312)

#define DEFAULT_FC_PAUSE_TIME		0xffff
#define DEFAULT_FC_FIFO_HIGH		1600
#define DEFAULT_TX_ALMOST_FULL		0x1f8
#define DEFAULT_TX_THRESHOLD		1518
#define DEFAULT_RX_THRESHOLD		12
#define DEFAULT_TX_RING_NUM		1024
#define DEFAULT_RX_RING_NUM		1024
#define DEFAULT_DMA_BURST		MREGBIT_BURST_16WORD
#define HASH_TABLE_SIZE			64

struct desc_buf {
	u64 dma_addr;
	void *buff_addr;
	u16 dma_len;
	u8 map_as_page;
};

struct emac_tx_desc_buffer {
	struct sk_buff *skb;
	struct desc_buf buf[2];
};

struct emac_rx_desc_buffer {
	struct sk_buff *skb;
	u64 dma_addr;
	void *buff_addr;
	u16 dma_len;
	u8 map_as_page;
};

/**
 * struct emac_desc_ring - Software-side information for one descriptor ring
 * Same structure used for both RX and TX
 * @desc_addr: Virtual address to the descriptor ring memory
 * @desc_dma_addr: DMA address of the descriptor ring
 * @total_size: Size of ring in bytes
 * @total_cnt: Number of descriptors
 * @head: Next descriptor to associate a buffer with
 * @tail: Next descriptor to check status bit
 * @rx_desc_buf: Array of descriptors for RX
 * @tx_desc_buf: Array of descriptors for TX, with max of two buffers each
 */
struct emac_desc_ring {
	void *desc_addr;
	dma_addr_t desc_dma_addr;
	u32 total_size;
	u32 total_cnt;
	u32 head;
	u32 tail;
	union {
		struct emac_rx_desc_buffer *rx_desc_buf;
		struct emac_tx_desc_buffer *tx_desc_buf;
	};
};

struct emac_priv {
	void __iomem *iobase;
	u32 dma_buf_sz;
	struct emac_desc_ring tx_ring;
	struct emac_desc_ring rx_ring;

	struct net_device *ndev;
	struct napi_struct napi;
	struct platform_device *pdev;
	struct clk *bus_clk;
	struct clk *ref_clk;
	struct regmap *regmap_apmu;
	u32 regmap_apmu_offset;
	int irq;

	phy_interface_t phy_interface;

	union emac_hw_tx_stats tx_stats, tx_stats_off;
	union emac_hw_rx_stats rx_stats, rx_stats_off;

	u32 tx_count_frames;
	u32 tx_coal_frames;
	u32 tx_coal_timeout;
	struct work_struct tx_timeout_task;

	struct timer_list txtimer;
	struct timer_list stats_timer;

	u32 tx_delay;
	u32 rx_delay;

	bool flow_control_autoneg;
	u8 flow_control;

	/* Softirq-safe, hold while touching hardware statistics */
	spinlock_t stats_lock;
};

static void emac_wr(struct emac_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->iobase + reg);
}

static u32 emac_rd(struct emac_priv *priv, u32 reg)
{
	return readl(priv->iobase + reg);
}

static int emac_phy_interface_config(struct emac_priv *priv)
{
	u32 val = 0, mask = REF_CLK_SEL | RGMII_TX_CLK_SEL | PHY_INTF_RGMII;

	if (phy_interface_mode_is_rgmii(priv->phy_interface))
		val |= PHY_INTF_RGMII;

	regmap_update_bits(priv->regmap_apmu,
			   priv->regmap_apmu_offset + APMU_EMAC_CTRL_REG,
			   mask, val);

	return 0;
}

/*
 * Where the hardware expects a MAC address, it is laid out in this high, med,
 * low order in three consecutive registers and in this format.
 */

static void emac_set_mac_addr_reg(struct emac_priv *priv,
				  const unsigned char *addr,
				  u32 reg)
{
	emac_wr(priv, reg + sizeof(u32) * 0, addr[1] << 8 | addr[0]);
	emac_wr(priv, reg + sizeof(u32) * 1, addr[3] << 8 | addr[2]);
	emac_wr(priv, reg + sizeof(u32) * 2, addr[5] << 8 | addr[4]);
}

static void emac_set_mac_addr(struct emac_priv *priv, const unsigned char *addr)
{
	/* We use only one address, so set the same for flow control as well */
	emac_set_mac_addr_reg(priv, addr, MAC_ADDRESS1_HIGH);
	emac_set_mac_addr_reg(priv, addr, MAC_FC_SOURCE_ADDRESS_HIGH);
}

static void emac_reset_hw(struct emac_priv *priv)
{
	/* Disable all interrupts */
	emac_wr(priv, MAC_INTERRUPT_ENABLE, 0x0);
	emac_wr(priv, DMA_INTERRUPT_ENABLE, 0x0);

	/* Disable transmit and receive units */
	emac_wr(priv, MAC_RECEIVE_CONTROL, 0x0);
	emac_wr(priv, MAC_TRANSMIT_CONTROL, 0x0);

	/* Disable DMA */
	emac_wr(priv, DMA_CONTROL, 0x0);
}

static void emac_init_hw(struct emac_priv *priv)
{
	/* Destination address for 802.3x Ethernet flow control */
	u8 fc_dest_addr[ETH_ALEN] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x01 };

	u32 rxirq = 0, dma = 0;

	regmap_set_bits(priv->regmap_apmu,
			priv->regmap_apmu_offset + APMU_EMAC_CTRL_REG,
			AXI_SINGLE_ID);

	/* Disable transmit and receive units */
	emac_wr(priv, MAC_RECEIVE_CONTROL, 0x0);
	emac_wr(priv, MAC_TRANSMIT_CONTROL, 0x0);

	/* Enable MAC address 1 filtering */
	emac_wr(priv, MAC_ADDRESS_CONTROL, MREGBIT_MAC_ADDRESS1_ENABLE);

	/* Zero initialize the multicast hash table */
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, 0x0);

	/* Configure thresholds */
	emac_wr(priv, MAC_TRANSMIT_FIFO_ALMOST_FULL, DEFAULT_TX_ALMOST_FULL);
	emac_wr(priv, MAC_TRANSMIT_PACKET_START_THRESHOLD,
		DEFAULT_TX_THRESHOLD);
	emac_wr(priv, MAC_RECEIVE_PACKET_START_THRESHOLD, DEFAULT_RX_THRESHOLD);

	/* Configure flow control (enabled in emac_adjust_link() later) */
	emac_set_mac_addr_reg(priv, fc_dest_addr, MAC_FC_SOURCE_ADDRESS_HIGH);
	emac_wr(priv, MAC_FC_PAUSE_HIGH_THRESHOLD, DEFAULT_FC_FIFO_HIGH);
	emac_wr(priv, MAC_FC_HIGH_PAUSE_TIME, DEFAULT_FC_PAUSE_TIME);
	emac_wr(priv, MAC_FC_PAUSE_LOW_THRESHOLD, 0);

	/* RX IRQ mitigation */
	rxirq = FIELD_PREP(MREGBIT_RECEIVE_IRQ_FRAME_COUNTER_MASK,
			   EMAC_RX_FRAMES);
	rxirq |= FIELD_PREP(MREGBIT_RECEIVE_IRQ_TIMEOUT_COUNTER_MASK,
			    EMAC_RX_COAL_TIMEOUT);
	rxirq |= MREGBIT_RECEIVE_IRQ_MITIGATION_ENABLE;
	emac_wr(priv, DMA_RECEIVE_IRQ_MITIGATION_CTRL, rxirq);

	/* Disable and set DMA config */
	emac_wr(priv, DMA_CONTROL, 0x0);

	emac_wr(priv, DMA_CONFIGURATION, MREGBIT_SOFTWARE_RESET);
	usleep_range(9000, 10000);
	emac_wr(priv, DMA_CONFIGURATION, 0x0);
	usleep_range(9000, 10000);

	dma |= MREGBIT_STRICT_BURST;
	dma |= MREGBIT_DMA_64BIT_MODE;
	dma |= DEFAULT_DMA_BURST;

	emac_wr(priv, DMA_CONFIGURATION, dma);
}

static void emac_dma_start_transmit(struct emac_priv *priv)
{
	/* The actual value written does not matter */
	emac_wr(priv, DMA_TRANSMIT_POLL_DEMAND, 1);
}

static void emac_enable_interrupt(struct emac_priv *priv)
{
	u32 val;

	val = emac_rd(priv, DMA_INTERRUPT_ENABLE);
	val |= MREGBIT_TRANSMIT_TRANSFER_DONE_INTR_ENABLE;
	val |= MREGBIT_RECEIVE_TRANSFER_DONE_INTR_ENABLE;
	emac_wr(priv, DMA_INTERRUPT_ENABLE, val);
}

static void emac_disable_interrupt(struct emac_priv *priv)
{
	u32 val;

	val = emac_rd(priv, DMA_INTERRUPT_ENABLE);
	val &= ~MREGBIT_TRANSMIT_TRANSFER_DONE_INTR_ENABLE;
	val &= ~MREGBIT_RECEIVE_TRANSFER_DONE_INTR_ENABLE;
	emac_wr(priv, DMA_INTERRUPT_ENABLE, val);
}

static u32 emac_tx_avail(struct emac_priv *priv)
{
	struct emac_desc_ring *tx_ring = &priv->tx_ring;
	u32 avail;

	if (tx_ring->tail > tx_ring->head)
		avail = tx_ring->tail - tx_ring->head - 1;
	else
		avail = tx_ring->total_cnt - tx_ring->head + tx_ring->tail - 1;

	return avail;
}

static void emac_tx_coal_timer_resched(struct emac_priv *priv)
{
	mod_timer(&priv->txtimer,
		  jiffies + usecs_to_jiffies(priv->tx_coal_timeout));
}

static void emac_tx_coal_timer(struct timer_list *t)
{
	struct emac_priv *priv = timer_container_of(priv, t, txtimer);

	napi_schedule(&priv->napi);
}

static bool emac_tx_should_interrupt(struct emac_priv *priv, u32 pkt_num)
{
	priv->tx_count_frames += pkt_num;
	if (likely(priv->tx_coal_frames > priv->tx_count_frames)) {
		emac_tx_coal_timer_resched(priv);
		return false;
	}

	priv->tx_count_frames = 0;
	return true;
}

static void emac_free_tx_buf(struct emac_priv *priv, int i)
{
	struct emac_tx_desc_buffer *tx_buf;
	struct emac_desc_ring *tx_ring;
	struct desc_buf *buf;
	int j;

	tx_ring = &priv->tx_ring;
	tx_buf = &tx_ring->tx_desc_buf[i];

	for (j = 0; j < 2; j++) {
		buf = &tx_buf->buf[j];
		if (!buf->dma_addr)
			continue;

		if (buf->map_as_page)
			dma_unmap_page(&priv->pdev->dev, buf->dma_addr,
				       buf->dma_len, DMA_TO_DEVICE);
		else
			dma_unmap_single(&priv->pdev->dev,
					 buf->dma_addr, buf->dma_len,
					 DMA_TO_DEVICE);

		buf->dma_addr = 0;
		buf->map_as_page = false;
		buf->buff_addr = NULL;
	}

	if (tx_buf->skb) {
		dev_kfree_skb_any(tx_buf->skb);
		tx_buf->skb = NULL;
	}
}

static void emac_clean_tx_desc_ring(struct emac_priv *priv)
{
	struct emac_desc_ring *tx_ring = &priv->tx_ring;
	u32 i;

	for (i = 0; i < tx_ring->total_cnt; i++)
		emac_free_tx_buf(priv, i);

	tx_ring->head = 0;
	tx_ring->tail = 0;
}

static void emac_clean_rx_desc_ring(struct emac_priv *priv)
{
	struct emac_rx_desc_buffer *rx_buf;
	struct emac_desc_ring *rx_ring;
	u32 i;

	rx_ring = &priv->rx_ring;

	for (i = 0; i < rx_ring->total_cnt; i++) {
		rx_buf = &rx_ring->rx_desc_buf[i];

		if (!rx_buf->skb)
			continue;

		dma_unmap_single(&priv->pdev->dev, rx_buf->dma_addr,
				 rx_buf->dma_len, DMA_FROM_DEVICE);

		dev_kfree_skb(rx_buf->skb);
		rx_buf->skb = NULL;
	}

	rx_ring->tail = 0;
	rx_ring->head = 0;
}

static int emac_alloc_tx_resources(struct emac_priv *priv)
{
	struct emac_desc_ring *tx_ring = &priv->tx_ring;
	struct platform_device *pdev = priv->pdev;

	tx_ring->tx_desc_buf = kcalloc(tx_ring->total_cnt,
				       sizeof(*tx_ring->tx_desc_buf),
				       GFP_KERNEL);

	if (!tx_ring->tx_desc_buf)
		return -ENOMEM;

	tx_ring->total_size = tx_ring->total_cnt * sizeof(struct emac_desc);
	tx_ring->total_size = ALIGN(tx_ring->total_size, PAGE_SIZE);

	tx_ring->desc_addr = dma_alloc_coherent(&pdev->dev, tx_ring->total_size,
						&tx_ring->desc_dma_addr,
						GFP_KERNEL);
	if (!tx_ring->desc_addr) {
		kfree(tx_ring->tx_desc_buf);
		return -ENOMEM;
	}

	tx_ring->head = 0;
	tx_ring->tail = 0;

	return 0;
}

static int emac_alloc_rx_resources(struct emac_priv *priv)
{
	struct emac_desc_ring *rx_ring = &priv->rx_ring;
	struct platform_device *pdev = priv->pdev;

	rx_ring->rx_desc_buf = kcalloc(rx_ring->total_cnt,
				       sizeof(*rx_ring->rx_desc_buf),
				       GFP_KERNEL);
	if (!rx_ring->rx_desc_buf)
		return -ENOMEM;

	rx_ring->total_size = rx_ring->total_cnt * sizeof(struct emac_desc);

	rx_ring->total_size = ALIGN(rx_ring->total_size, PAGE_SIZE);

	rx_ring->desc_addr = dma_alloc_coherent(&pdev->dev, rx_ring->total_size,
						&rx_ring->desc_dma_addr,
						GFP_KERNEL);
	if (!rx_ring->desc_addr) {
		kfree(rx_ring->rx_desc_buf);
		return -ENOMEM;
	}

	rx_ring->head = 0;
	rx_ring->tail = 0;

	return 0;
}

static void emac_free_tx_resources(struct emac_priv *priv)
{
	struct emac_desc_ring *tr = &priv->tx_ring;
	struct device *dev = &priv->pdev->dev;

	emac_clean_tx_desc_ring(priv);

	kfree(tr->tx_desc_buf);
	tr->tx_desc_buf = NULL;

	dma_free_coherent(dev, tr->total_size, tr->desc_addr,
			  tr->desc_dma_addr);
	tr->desc_addr = NULL;
}

static void emac_free_rx_resources(struct emac_priv *priv)
{
	struct emac_desc_ring *rr = &priv->rx_ring;
	struct device *dev = &priv->pdev->dev;

	emac_clean_rx_desc_ring(priv);

	kfree(rr->rx_desc_buf);
	rr->rx_desc_buf = NULL;

	dma_free_coherent(dev, rr->total_size, rr->desc_addr,
			  rr->desc_dma_addr);
	rr->desc_addr = NULL;
}

static int emac_tx_clean_desc(struct emac_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	struct emac_desc_ring *tx_ring;
	struct emac_desc *tx_desc;
	u32 i;

	netif_tx_lock(ndev);

	tx_ring = &priv->tx_ring;

	i = tx_ring->tail;

	while (i != tx_ring->head) {
		tx_desc = &((struct emac_desc *)tx_ring->desc_addr)[i];

		/* Stop checking if desc still own by DMA */
		if (READ_ONCE(tx_desc->desc0) & TX_DESC_0_OWN)
			break;

		emac_free_tx_buf(priv, i);
		memset(tx_desc, 0, sizeof(struct emac_desc));

		if (++i == tx_ring->total_cnt)
			i = 0;
	}

	tx_ring->tail = i;

	if (unlikely(netif_queue_stopped(ndev) &&
		     emac_tx_avail(priv) > tx_ring->total_cnt / 4))
		netif_wake_queue(ndev);

	netif_tx_unlock(ndev);

	return 0;
}

static bool emac_rx_frame_good(struct emac_priv *priv, struct emac_desc *desc)
{
	const char *msg;
	u32 len;

	len = FIELD_GET(RX_DESC_0_FRAME_PACKET_LENGTH_MASK, desc->desc0);

	if (WARN_ON_ONCE(!(desc->desc0 & RX_DESC_0_LAST_DESCRIPTOR)))
		msg = "Not last descriptor"; /* This would be a bug */
	else if (desc->desc0 & RX_DESC_0_FRAME_RUNT)
		msg = "Runt frame";
	else if (desc->desc0 & RX_DESC_0_FRAME_CRC_ERR)
		msg = "Frame CRC error";
	else if (desc->desc0 & RX_DESC_0_FRAME_MAX_LEN_ERR)
		msg = "Frame exceeds max length";
	else if (desc->desc0 & RX_DESC_0_FRAME_JABBER_ERR)
		msg = "Frame jabber error";
	else if (desc->desc0 & RX_DESC_0_FRAME_LENGTH_ERR)
		msg = "Frame length error";
	else if (len <= ETH_FCS_LEN || len > priv->dma_buf_sz)
		msg = "Frame length unacceptable";
	else
		return true; /* All good */

	dev_dbg_ratelimited(&priv->ndev->dev, "RX error: %s", msg);

	return false;
}

static void emac_alloc_rx_desc_buffers(struct emac_priv *priv)
{
	struct emac_desc_ring *rx_ring = &priv->rx_ring;
	struct emac_desc rx_desc, *rx_desc_addr;
	struct net_device *ndev = priv->ndev;
	struct emac_rx_desc_buffer *rx_buf;
	struct sk_buff *skb;
	u32 i;

	i = rx_ring->head;
	rx_buf = &rx_ring->rx_desc_buf[i];

	while (!rx_buf->skb) {
		skb = netdev_alloc_skb_ip_align(ndev, priv->dma_buf_sz);
		if (!skb)
			break;

		skb->dev = ndev;

		rx_buf->skb = skb;
		rx_buf->dma_len = priv->dma_buf_sz;
		rx_buf->dma_addr = dma_map_single(&priv->pdev->dev, skb->data,
						  priv->dma_buf_sz,
						  DMA_FROM_DEVICE);
		if (dma_mapping_error(&priv->pdev->dev, rx_buf->dma_addr)) {
			dev_err_ratelimited(&ndev->dev, "Mapping skb failed\n");
			goto err_free_skb;
		}

		rx_desc_addr = &((struct emac_desc *)rx_ring->desc_addr)[i];

		memset(&rx_desc, 0, sizeof(rx_desc));

		rx_desc.buffer_addr_1 = rx_buf->dma_addr;
		rx_desc.desc1 = FIELD_PREP(RX_DESC_1_BUFFER_SIZE_1_MASK,
					   rx_buf->dma_len);

		if (++i == rx_ring->total_cnt) {
			rx_desc.desc1 |= RX_DESC_1_END_RING;
			i = 0;
		}

		*rx_desc_addr = rx_desc;
		dma_wmb();
		WRITE_ONCE(rx_desc_addr->desc0, rx_desc.desc0 | RX_DESC_0_OWN);

		rx_buf = &rx_ring->rx_desc_buf[i];
	}

	rx_ring->head = i;
	return;

err_free_skb:
	dev_kfree_skb_any(skb);
	rx_buf->skb = NULL;
}

/* Returns number of packets received */
static int emac_rx_clean_desc(struct emac_priv *priv, int budget)
{
	struct net_device *ndev = priv->ndev;
	struct emac_rx_desc_buffer *rx_buf;
	struct emac_desc_ring *rx_ring;
	struct sk_buff *skb = NULL;
	struct emac_desc *rx_desc;
	u32 got = 0, skb_len, i;

	rx_ring = &priv->rx_ring;

	i = rx_ring->tail;

	while (budget--) {
		rx_desc = &((struct emac_desc *)rx_ring->desc_addr)[i];

		/* Stop checking if rx_desc still owned by DMA */
		if (READ_ONCE(rx_desc->desc0) & RX_DESC_0_OWN)
			break;

		dma_rmb();

		rx_buf = &rx_ring->rx_desc_buf[i];

		if (!rx_buf->skb)
			break;

		got++;

		dma_unmap_single(&priv->pdev->dev, rx_buf->dma_addr,
				 rx_buf->dma_len, DMA_FROM_DEVICE);

		if (likely(emac_rx_frame_good(priv, rx_desc))) {
			skb = rx_buf->skb;

			skb_len = FIELD_GET(RX_DESC_0_FRAME_PACKET_LENGTH_MASK,
					    rx_desc->desc0);
			skb_len -= ETH_FCS_LEN;

			skb_put(skb, skb_len);
			skb->dev = ndev;
			ndev->hard_header_len = ETH_HLEN;

			skb->protocol = eth_type_trans(skb, ndev);

			skb->ip_summed = CHECKSUM_NONE;

			napi_gro_receive(&priv->napi, skb);

			memset(rx_desc, 0, sizeof(struct emac_desc));
			rx_buf->skb = NULL;
		} else {
			dev_kfree_skb_irq(rx_buf->skb);
			rx_buf->skb = NULL;
		}

		if (++i == rx_ring->total_cnt)
			i = 0;
	}

	rx_ring->tail = i;

	emac_alloc_rx_desc_buffers(priv);

	return got;
}

static int emac_rx_poll(struct napi_struct *napi, int budget)
{
	struct emac_priv *priv = container_of(napi, struct emac_priv, napi);
	int work_done;

	emac_tx_clean_desc(priv);

	work_done = emac_rx_clean_desc(priv, budget);
	if (work_done < budget && napi_complete_done(napi, work_done))
		emac_enable_interrupt(priv);

	return work_done;
}

/*
 * For convenience, skb->data is fragment 0, frags[0] is fragment 1, etc.
 *
 * Each descriptor can hold up to two fragments, called buffer 1 and 2. For each
 * fragment f, if f % 2 == 0, it uses buffer 1, otherwise it uses buffer 2.
 */

static int emac_tx_map_frag(struct device *dev, struct emac_desc *tx_desc,
			    struct emac_tx_desc_buffer *tx_buf,
			    struct sk_buff *skb, u32 frag_idx)
{
	bool map_as_page, buf_idx;
	const skb_frag_t *frag;
	phys_addr_t addr;
	u32 len;
	int ret;

	buf_idx = frag_idx % 2;

	if (frag_idx == 0) {
		/* Non-fragmented part */
		len = skb_headlen(skb);
		addr = dma_map_single(dev, skb->data, len, DMA_TO_DEVICE);
		map_as_page = false;
	} else {
		/* Fragment */
		frag = &skb_shinfo(skb)->frags[frag_idx - 1];
		len = skb_frag_size(frag);
		addr = skb_frag_dma_map(dev, frag, 0, len, DMA_TO_DEVICE);
		map_as_page = true;
	}

	ret = dma_mapping_error(dev, addr);
	if (ret)
		return ret;

	tx_buf->buf[buf_idx].dma_addr = addr;
	tx_buf->buf[buf_idx].dma_len = len;
	tx_buf->buf[buf_idx].map_as_page = map_as_page;

	if (buf_idx == 0) {
		tx_desc->buffer_addr_1 = addr;
		tx_desc->desc1 |= FIELD_PREP(TX_DESC_1_BUFFER_SIZE_1_MASK, len);
	} else {
		tx_desc->buffer_addr_2 = addr;
		tx_desc->desc1 |= FIELD_PREP(TX_DESC_1_BUFFER_SIZE_2_MASK, len);
	}

	return 0;
}

static void emac_tx_mem_map(struct emac_priv *priv, struct sk_buff *skb)
{
	struct emac_desc_ring *tx_ring = &priv->tx_ring;
	struct emac_desc tx_desc, *tx_desc_addr;
	struct device *dev = &priv->pdev->dev;
	struct emac_tx_desc_buffer *tx_buf;
	u32 head, old_head, frag_num, f;
	bool buf_idx;

	frag_num = skb_shinfo(skb)->nr_frags;
	head = tx_ring->head;
	old_head = head;

	for (f = 0; f < frag_num + 1; f++) {
		buf_idx = f % 2;

		/*
		 * If using buffer 1, initialize a new desc. Otherwise, use
		 * buffer 2 of previous fragment's desc.
		 */
		if (!buf_idx) {
			tx_buf = &tx_ring->tx_desc_buf[head];
			tx_desc_addr =
				&((struct emac_desc *)tx_ring->desc_addr)[head];
			memset(&tx_desc, 0, sizeof(tx_desc));

			/*
			 * Give ownership for all but first desc initially. For
			 * first desc, give at the end so DMA cannot start
			 * reading uninitialized descs.
			 */
			if (head != old_head)
				tx_desc.desc0 |= TX_DESC_0_OWN;

			if (++head == tx_ring->total_cnt) {
				/* Just used last desc in ring */
				tx_desc.desc1 |= TX_DESC_1_END_RING;
				head = 0;
			}
		}

		if (emac_tx_map_frag(dev, &tx_desc, tx_buf, skb, f)) {
			dev_err_ratelimited(&priv->ndev->dev,
					    "Map TX frag %d failed\n", f);
			goto err_free_skb;
		}

		if (f == 0)
			tx_desc.desc1 |= TX_DESC_1_FIRST_SEGMENT;

		if (f == frag_num) {
			tx_desc.desc1 |= TX_DESC_1_LAST_SEGMENT;
			tx_buf->skb = skb;
			if (emac_tx_should_interrupt(priv, frag_num + 1))
				tx_desc.desc1 |=
					TX_DESC_1_INTERRUPT_ON_COMPLETION;
		}

		*tx_desc_addr = tx_desc;
	}

	/* All descriptors are ready, give ownership for first desc */
	tx_desc_addr = &((struct emac_desc *)tx_ring->desc_addr)[old_head];
	dma_wmb();
	WRITE_ONCE(tx_desc_addr->desc0, tx_desc_addr->desc0 | TX_DESC_0_OWN);

	emac_dma_start_transmit(priv);

	tx_ring->head = head;

	return;

err_free_skb:
	dev_dstats_tx_dropped(priv->ndev);
	dev_kfree_skb_any(skb);
}

static netdev_tx_t emac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);
	int nfrags = skb_shinfo(skb)->nr_frags;
	struct device *dev = &priv->pdev->dev;

	if (unlikely(emac_tx_avail(priv) < nfrags + 1)) {
		if (!netif_queue_stopped(ndev)) {
			netif_stop_queue(ndev);
			dev_err_ratelimited(dev, "TX ring full, stop TX queue\n");
		}
		return NETDEV_TX_BUSY;
	}

	emac_tx_mem_map(priv, skb);

	/* Make sure there is space in the ring for the next TX. */
	if (unlikely(emac_tx_avail(priv) <= MAX_SKB_FRAGS + 2))
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;
}

static int emac_set_mac_address(struct net_device *ndev, void *addr)
{
	struct emac_priv *priv = netdev_priv(ndev);
	int ret = eth_mac_addr(ndev, addr);

	if (ret)
		return ret;

	/* If running, set now; if not running it will be set in emac_up. */
	if (netif_running(ndev))
		emac_set_mac_addr(priv, ndev->dev_addr);

	return 0;
}

static void emac_mac_multicast_filter_clear(struct emac_priv *priv)
{
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, 0x0);
}

/*
 * The upper 6 bits of the Ethernet CRC of the MAC address is used as the hash
 * when matching multicast addresses.
 */
static u32 emac_ether_addr_hash(u8 addr[ETH_ALEN])
{
	u32 crc32 = ether_crc(ETH_ALEN, addr);

	return crc32 >> 26;
}

/* Configure Multicast and Promiscuous modes */
static void emac_set_rx_mode(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	u32 mc_filter[4] = { 0 };
	u32 hash, reg, bit, val;

	val = emac_rd(priv, MAC_ADDRESS_CONTROL);

	val &= ~MREGBIT_PROMISCUOUS_MODE;

	if (ndev->flags & IFF_PROMISC) {
		/* Enable promisc mode */
		val |= MREGBIT_PROMISCUOUS_MODE;
	} else if ((ndev->flags & IFF_ALLMULTI) ||
		   (netdev_mc_count(ndev) > HASH_TABLE_SIZE)) {
		/* Accept all multicast frames by setting every bit */
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, 0xffff);
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, 0xffff);
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, 0xffff);
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, 0xffff);
	} else if (!netdev_mc_empty(ndev)) {
		emac_mac_multicast_filter_clear(priv);
		netdev_for_each_mc_addr(ha, ndev) {
			/*
			 * The hash table is an array of 4 16-bit registers. It
			 * is treated like an array of 64 bits (bits[hash]).
			 */
			hash = emac_ether_addr_hash(ha->addr);
			reg = hash / 16;
			bit = hash % 16;
			mc_filter[reg] |= BIT(bit);
		}
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, mc_filter[0]);
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, mc_filter[1]);
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, mc_filter[2]);
		emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, mc_filter[3]);
	}

	emac_wr(priv, MAC_ADDRESS_CONTROL, val);
}

static int emac_change_mtu(struct net_device *ndev, int mtu)
{
	struct emac_priv *priv = netdev_priv(ndev);
	u32 frame_len;

	if (netif_running(ndev)) {
		netdev_err(ndev, "must be stopped to change MTU\n");
		return -EBUSY;
	}

	frame_len = mtu + ETH_HLEN + ETH_FCS_LEN;

	if (frame_len <= EMAC_DEFAULT_BUFSIZE)
		priv->dma_buf_sz = EMAC_DEFAULT_BUFSIZE;
	else if (frame_len <= EMAC_RX_BUF_2K)
		priv->dma_buf_sz = EMAC_RX_BUF_2K;
	else
		priv->dma_buf_sz = EMAC_RX_BUF_4K;

	ndev->mtu = mtu;

	return 0;
}

static void emac_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct emac_priv *priv = netdev_priv(ndev);

	schedule_work(&priv->tx_timeout_task);
}

static int emac_mii_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct emac_priv *priv = bus->priv;
	u32 cmd = 0, val;
	int ret;

	cmd |= FIELD_PREP(MREGBIT_PHY_ADDRESS, phy_addr);
	cmd |= FIELD_PREP(MREGBIT_REGISTER_ADDRESS, regnum);
	cmd |= MREGBIT_START_MDIO_TRANS | MREGBIT_MDIO_READ_WRITE;

	emac_wr(priv, MAC_MDIO_DATA, 0x0);
	emac_wr(priv, MAC_MDIO_CONTROL, cmd);

	ret = readl_poll_timeout(priv->iobase + MAC_MDIO_CONTROL, val,
				 !(val & MREGBIT_START_MDIO_TRANS), 100, 10000);

	if (ret)
		return ret;

	val = emac_rd(priv, MAC_MDIO_DATA);
	return FIELD_GET(MREGBIT_MDIO_DATA, val);
}

static int emac_mii_write(struct mii_bus *bus, int phy_addr, int regnum,
			  u16 value)
{
	struct emac_priv *priv = bus->priv;
	u32 cmd = 0, val;
	int ret;

	emac_wr(priv, MAC_MDIO_DATA, value);

	cmd |= FIELD_PREP(MREGBIT_PHY_ADDRESS, phy_addr);
	cmd |= FIELD_PREP(MREGBIT_REGISTER_ADDRESS, regnum);
	cmd |= MREGBIT_START_MDIO_TRANS;

	emac_wr(priv, MAC_MDIO_CONTROL, cmd);

	ret = readl_poll_timeout(priv->iobase + MAC_MDIO_CONTROL, val,
				 !(val & MREGBIT_START_MDIO_TRANS), 100, 10000);

	return ret;
}

static int emac_mdio_init(struct emac_priv *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct device_node *mii_np;
	struct mii_bus *mii;
	int ret;

	mii = devm_mdiobus_alloc(dev);
	if (!mii)
		return -ENOMEM;

	mii->priv = priv;
	mii->name = "k1_emac_mii";
	mii->read = emac_mii_read;
	mii->write = emac_mii_write;
	mii->parent = dev;
	mii->phy_mask = ~0;
	snprintf(mii->id, MII_BUS_ID_SIZE, "%s", priv->pdev->name);

	mii_np = of_get_available_child_by_name(dev->of_node, "mdio-bus");

	ret = devm_of_mdiobus_register(dev, mii, mii_np);
	if (ret)
		dev_err_probe(dev, ret, "Failed to register mdio bus\n");

	of_node_put(mii_np);
	return ret;
}

static void emac_set_tx_fc(struct emac_priv *priv, bool enable)
{
	u32 val;

	val = emac_rd(priv, MAC_FC_CONTROL);

	FIELD_MODIFY(MREGBIT_FC_GENERATION_ENABLE, &val, enable);
	FIELD_MODIFY(MREGBIT_AUTO_FC_GENERATION_ENABLE, &val, enable);

	emac_wr(priv, MAC_FC_CONTROL, val);
}

static void emac_set_rx_fc(struct emac_priv *priv, bool enable)
{
	u32 val = emac_rd(priv, MAC_FC_CONTROL);

	FIELD_MODIFY(MREGBIT_FC_DECODE_ENABLE, &val, enable);

	emac_wr(priv, MAC_FC_CONTROL, val);
}

static void emac_set_fc(struct emac_priv *priv, u8 fc)
{
	emac_set_tx_fc(priv, fc & FLOW_CTRL_TX);
	emac_set_rx_fc(priv, fc & FLOW_CTRL_RX);
	priv->flow_control = fc;
}

static void emac_set_fc_autoneg(struct emac_priv *priv)
{
	struct phy_device *phydev = priv->ndev->phydev;
	u32 local_adv, remote_adv;
	u8 fc;

	local_adv = linkmode_adv_to_lcl_adv_t(phydev->advertising);

	remote_adv = 0;

	if (phydev->pause)
		remote_adv |= LPA_PAUSE_CAP;

	if (phydev->asym_pause)
		remote_adv |= LPA_PAUSE_ASYM;

	fc = mii_resolve_flowctrl_fdx(local_adv, remote_adv);

	priv->flow_control_autoneg = true;

	emac_set_fc(priv, fc);
}

/*
 * Even though this MAC supports gigabit operation, it only provides 32-bit
 * statistics counters. The most overflow-prone counters are the "bytes" ones,
 * which at gigabit overflow about twice a minute.
 *
 * Therefore, we maintain the high 32 bits of counters ourselves, incrementing
 * every time statistics seem to go backwards. Also, update periodically to
 * catch overflows when we are not otherwise checking the statistics often
 * enough.
 */

#define EMAC_STATS_TIMER_PERIOD		20

static int emac_read_stat_cnt(struct emac_priv *priv, u8 cnt, u32 *res,
			      u32 control_reg, u32 high_reg, u32 low_reg)
{
	u32 val, high, low;
	int ret;

	/* The "read" bit is the same for TX and RX */

	val = MREGBIT_START_TX_COUNTER_READ | cnt;
	emac_wr(priv, control_reg, val);
	val = emac_rd(priv, control_reg);

	ret = readl_poll_timeout_atomic(priv->iobase + control_reg, val,
					!(val & MREGBIT_START_TX_COUNTER_READ),
					100, 10000);

	if (ret) {
		netdev_err(priv->ndev, "Read stat timeout\n");
		return ret;
	}

	high = emac_rd(priv, high_reg);
	low = emac_rd(priv, low_reg);
	*res = high << 16 | lower_16_bits(low);

	return 0;
}

static int emac_tx_read_stat_cnt(struct emac_priv *priv, u8 cnt, u32 *res)
{
	return emac_read_stat_cnt(priv, cnt, res, MAC_TX_STATCTR_CONTROL,
				  MAC_TX_STATCTR_DATA_HIGH,
				  MAC_TX_STATCTR_DATA_LOW);
}

static int emac_rx_read_stat_cnt(struct emac_priv *priv, u8 cnt, u32 *res)
{
	return emac_read_stat_cnt(priv, cnt, res, MAC_RX_STATCTR_CONTROL,
				  MAC_RX_STATCTR_DATA_HIGH,
				  MAC_RX_STATCTR_DATA_LOW);
}

static void emac_update_counter(u64 *counter, u32 new_low)
{
	u32 old_low = lower_32_bits(*counter);
	u64 high = upper_32_bits(*counter);

	if (old_low > new_low) {
		/* Overflowed, increment high 32 bits */
		high++;
	}

	*counter = (high << 32) | new_low;
}

static void emac_stats_update(struct emac_priv *priv)
{
	u64 *tx_stats_off = priv->tx_stats_off.array;
	u64 *rx_stats_off = priv->rx_stats_off.array;
	u64 *tx_stats = priv->tx_stats.array;
	u64 *rx_stats = priv->rx_stats.array;
	u32 i, res, offset;

	assert_spin_locked(&priv->stats_lock);

	if (!netif_running(priv->ndev) || !netif_device_present(priv->ndev)) {
		/* Not up, don't try to update */
		return;
	}

	for (i = 0; i < sizeof(priv->tx_stats) / sizeof(*tx_stats); i++) {
		/*
		 * If reading stats times out, everything is broken and there's
		 * nothing we can do. Reading statistics also can't return an
		 * error, so just return without updating and without
		 * rescheduling.
		 */
		if (emac_tx_read_stat_cnt(priv, i, &res))
			return;

		/*
		 * Re-initializing while bringing interface up resets counters
		 * to zero, so to provide continuity, we add the values saved
		 * last time we did emac_down() to the new hardware-provided
		 * value.
		 */
		offset = lower_32_bits(tx_stats_off[i]);
		emac_update_counter(&tx_stats[i], res + offset);
	}

	/* Similar remarks as TX stats */
	for (i = 0; i < sizeof(priv->rx_stats) / sizeof(*rx_stats); i++) {
		if (emac_rx_read_stat_cnt(priv, i, &res))
			return;
		offset = lower_32_bits(rx_stats_off[i]);
		emac_update_counter(&rx_stats[i], res + offset);
	}

	mod_timer(&priv->stats_timer, jiffies + EMAC_STATS_TIMER_PERIOD * HZ);
}

static void emac_stats_timer(struct timer_list *t)
{
	struct emac_priv *priv = timer_container_of(priv, t, stats_timer);

	spin_lock(&priv->stats_lock);

	emac_stats_update(priv);

	spin_unlock(&priv->stats_lock);
}

static const struct ethtool_rmon_hist_range emac_rmon_hist_ranges[] = {
	{   64,   64 },
	{   65,  127 },
	{  128,  255 },
	{  256,  511 },
	{  512, 1023 },
	{ 1024, 1518 },
	{ 1519, 4096 },
	{ /* sentinel */ },
};

/* Like dev_fetch_dstats(), but we only use tx_drops */
static u64 emac_get_stat_tx_drops(struct emac_priv *priv)
{
	const struct pcpu_dstats *stats;
	u64 tx_drops, total = 0;
	unsigned int start;
	int cpu;

	for_each_possible_cpu(cpu) {
		stats = per_cpu_ptr(priv->ndev->dstats, cpu);
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			tx_drops = u64_stats_read(&stats->tx_drops);
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		total += tx_drops;
	}

	return total;
}

static void emac_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *storage)
{
	struct emac_priv *priv = netdev_priv(dev);
	union emac_hw_tx_stats *tx_stats;
	union emac_hw_rx_stats *rx_stats;

	tx_stats = &priv->tx_stats;
	rx_stats = &priv->rx_stats;

	/* This is the only software counter */
	storage->tx_dropped = emac_get_stat_tx_drops(priv);

	spin_lock_bh(&priv->stats_lock);

	emac_stats_update(priv);

	storage->tx_packets = tx_stats->stats.tx_ok_pkts;
	storage->tx_bytes = tx_stats->stats.tx_ok_bytes;
	storage->tx_errors = tx_stats->stats.tx_err_pkts;

	storage->rx_packets = rx_stats->stats.rx_ok_pkts;
	storage->rx_bytes = rx_stats->stats.rx_ok_bytes;
	storage->rx_errors = rx_stats->stats.rx_err_total_pkts;
	storage->rx_crc_errors = rx_stats->stats.rx_crc_err_pkts;
	storage->rx_frame_errors = rx_stats->stats.rx_align_err_pkts;
	storage->rx_length_errors = rx_stats->stats.rx_len_err_pkts;

	storage->collisions = tx_stats->stats.tx_singleclsn_pkts;
	storage->collisions += tx_stats->stats.tx_multiclsn_pkts;
	storage->collisions += tx_stats->stats.tx_excessclsn_pkts;

	storage->rx_missed_errors = rx_stats->stats.rx_drp_fifo_full_pkts;
	storage->rx_missed_errors += rx_stats->stats.rx_truncate_fifo_full_pkts;

	spin_unlock_bh(&priv->stats_lock);
}

static void emac_get_rmon_stats(struct net_device *dev,
				struct ethtool_rmon_stats *rmon_stats,
				const struct ethtool_rmon_hist_range **ranges)
{
	struct emac_priv *priv = netdev_priv(dev);
	union emac_hw_rx_stats *rx_stats;

	rx_stats = &priv->rx_stats;

	*ranges = emac_rmon_hist_ranges;

	spin_lock_bh(&priv->stats_lock);

	emac_stats_update(priv);

	rmon_stats->undersize_pkts = rx_stats->stats.rx_len_undersize_pkts;
	rmon_stats->oversize_pkts = rx_stats->stats.rx_len_oversize_pkts;
	rmon_stats->fragments = rx_stats->stats.rx_len_fragment_pkts;
	rmon_stats->jabbers = rx_stats->stats.rx_len_jabber_pkts;

	/* Only RX has histogram stats */

	rmon_stats->hist[0] = rx_stats->stats.rx_64_pkts;
	rmon_stats->hist[1] = rx_stats->stats.rx_65_127_pkts;
	rmon_stats->hist[2] = rx_stats->stats.rx_128_255_pkts;
	rmon_stats->hist[3] = rx_stats->stats.rx_256_511_pkts;
	rmon_stats->hist[4] = rx_stats->stats.rx_512_1023_pkts;
	rmon_stats->hist[5] = rx_stats->stats.rx_1024_1518_pkts;
	rmon_stats->hist[6] = rx_stats->stats.rx_1519_plus_pkts;

	spin_unlock_bh(&priv->stats_lock);
}

static void emac_get_eth_mac_stats(struct net_device *dev,
				   struct ethtool_eth_mac_stats *mac_stats)
{
	struct emac_priv *priv = netdev_priv(dev);
	union emac_hw_tx_stats *tx_stats;
	union emac_hw_rx_stats *rx_stats;

	tx_stats = &priv->tx_stats;
	rx_stats = &priv->rx_stats;

	spin_lock_bh(&priv->stats_lock);

	emac_stats_update(priv);

	mac_stats->MulticastFramesXmittedOK = tx_stats->stats.tx_multicast_pkts;
	mac_stats->BroadcastFramesXmittedOK = tx_stats->stats.tx_broadcast_pkts;

	mac_stats->MulticastFramesReceivedOK =
		rx_stats->stats.rx_multicast_pkts;
	mac_stats->BroadcastFramesReceivedOK =
		rx_stats->stats.rx_broadcast_pkts;

	mac_stats->SingleCollisionFrames = tx_stats->stats.tx_singleclsn_pkts;
	mac_stats->MultipleCollisionFrames = tx_stats->stats.tx_multiclsn_pkts;
	mac_stats->LateCollisions = tx_stats->stats.tx_lateclsn_pkts;
	mac_stats->FramesAbortedDueToXSColls =
		tx_stats->stats.tx_excessclsn_pkts;

	spin_unlock_bh(&priv->stats_lock);
}

static void emac_get_pause_stats(struct net_device *dev,
				 struct ethtool_pause_stats *pause_stats)
{
	struct emac_priv *priv = netdev_priv(dev);
	union emac_hw_tx_stats *tx_stats;
	union emac_hw_rx_stats *rx_stats;

	tx_stats = &priv->tx_stats;
	rx_stats = &priv->rx_stats;

	spin_lock_bh(&priv->stats_lock);

	emac_stats_update(priv);

	pause_stats->tx_pause_frames = tx_stats->stats.tx_pause_pkts;
	pause_stats->rx_pause_frames = rx_stats->stats.rx_pause_pkts;

	spin_unlock_bh(&priv->stats_lock);
}

/* Other statistics that are not derivable from standard statistics */

#define EMAC_ETHTOOL_STAT(type, name) \
	{ offsetof(type, stats.name) / sizeof(u64), #name }

static const struct emac_ethtool_stats {
	size_t offset;
	char str[ETH_GSTRING_LEN];
} emac_ethtool_rx_stats[] = {
	EMAC_ETHTOOL_STAT(union emac_hw_rx_stats, rx_drp_fifo_full_pkts),
	EMAC_ETHTOOL_STAT(union emac_hw_rx_stats, rx_truncate_fifo_full_pkts),
};

static int emac_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(emac_ethtool_rx_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void emac_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(emac_ethtool_rx_stats); i++) {
			memcpy(data, emac_ethtool_rx_stats[i].str,
			       ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void emac_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct emac_priv *priv = netdev_priv(dev);
	u64 *rx_stats = (u64 *)&priv->rx_stats;
	int i;

	spin_lock_bh(&priv->stats_lock);

	emac_stats_update(priv);

	for (i = 0; i < ARRAY_SIZE(emac_ethtool_rx_stats); i++)
		data[i] = rx_stats[emac_ethtool_rx_stats[i].offset];

	spin_unlock_bh(&priv->stats_lock);
}

static int emac_ethtool_get_regs_len(struct net_device *dev)
{
	return (EMAC_DMA_REG_CNT + EMAC_MAC_REG_CNT) * sizeof(u32);
}

static void emac_ethtool_get_regs(struct net_device *dev,
				  struct ethtool_regs *regs, void *space)
{
	struct emac_priv *priv = netdev_priv(dev);
	u32 *reg_space = space;
	int i;

	regs->version = 1;

	for (i = 0; i < EMAC_DMA_REG_CNT; i++)
		reg_space[i] = emac_rd(priv, DMA_CONFIGURATION + i * 4);

	for (i = 0; i < EMAC_MAC_REG_CNT; i++)
		reg_space[i + EMAC_DMA_REG_CNT] =
			emac_rd(priv, MAC_GLOBAL_CONTROL + i * 4);
}

static void emac_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *pause)
{
	struct emac_priv *priv = netdev_priv(dev);

	pause->autoneg = priv->flow_control_autoneg;
	pause->tx_pause = !!(priv->flow_control & FLOW_CTRL_TX);
	pause->rx_pause = !!(priv->flow_control & FLOW_CTRL_RX);
}

static int emac_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *pause)
{
	struct emac_priv *priv = netdev_priv(dev);
	u8 fc = 0;

	if (!netif_running(dev))
		return -ENETDOWN;

	priv->flow_control_autoneg = pause->autoneg;

	if (pause->autoneg) {
		emac_set_fc_autoneg(priv);
	} else {
		if (pause->tx_pause)
			fc |= FLOW_CTRL_TX;

		if (pause->rx_pause)
			fc |= FLOW_CTRL_RX;

		emac_set_fc(priv, fc);
	}

	return 0;
}

static void emac_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	info->n_stats = ARRAY_SIZE(emac_ethtool_rx_stats);
}

static void emac_tx_timeout_task(struct work_struct *work)
{
	struct net_device *ndev;
	struct emac_priv *priv;

	priv = container_of(work, struct emac_priv, tx_timeout_task);
	ndev = priv->ndev;

	rtnl_lock();

	/* No need to reset if already down */
	if (!netif_running(ndev)) {
		rtnl_unlock();
		return;
	}

	netdev_err(ndev, "MAC reset due to TX timeout\n");

	netif_trans_update(ndev); /* prevent tx timeout */
	dev_close(ndev);
	dev_open(ndev, NULL);

	rtnl_unlock();
}

static void emac_sw_init(struct emac_priv *priv)
{
	priv->dma_buf_sz = EMAC_DEFAULT_BUFSIZE;

	priv->tx_ring.total_cnt = DEFAULT_TX_RING_NUM;
	priv->rx_ring.total_cnt = DEFAULT_RX_RING_NUM;

	spin_lock_init(&priv->stats_lock);

	INIT_WORK(&priv->tx_timeout_task, emac_tx_timeout_task);

	priv->tx_coal_frames = EMAC_TX_FRAMES;
	priv->tx_coal_timeout = EMAC_TX_COAL_TIMEOUT;

	timer_setup(&priv->txtimer, emac_tx_coal_timer, 0);
	timer_setup(&priv->stats_timer, emac_stats_timer, 0);
}

static irqreturn_t emac_interrupt_handler(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct emac_priv *priv = netdev_priv(ndev);
	bool should_schedule = false;
	u32 clr = 0;
	u32 status;

	status = emac_rd(priv, DMA_STATUS_IRQ);

	if (status & MREGBIT_TRANSMIT_TRANSFER_DONE_IRQ) {
		clr |= MREGBIT_TRANSMIT_TRANSFER_DONE_IRQ;
		should_schedule = true;
	}

	if (status & MREGBIT_TRANSMIT_DES_UNAVAILABLE_IRQ)
		clr |= MREGBIT_TRANSMIT_DES_UNAVAILABLE_IRQ;

	if (status & MREGBIT_TRANSMIT_DMA_STOPPED_IRQ)
		clr |= MREGBIT_TRANSMIT_DMA_STOPPED_IRQ;

	if (status & MREGBIT_RECEIVE_TRANSFER_DONE_IRQ) {
		clr |= MREGBIT_RECEIVE_TRANSFER_DONE_IRQ;
		should_schedule = true;
	}

	if (status & MREGBIT_RECEIVE_DES_UNAVAILABLE_IRQ)
		clr |= MREGBIT_RECEIVE_DES_UNAVAILABLE_IRQ;

	if (status & MREGBIT_RECEIVE_DMA_STOPPED_IRQ)
		clr |= MREGBIT_RECEIVE_DMA_STOPPED_IRQ;

	if (status & MREGBIT_RECEIVE_MISSED_FRAME_IRQ)
		clr |= MREGBIT_RECEIVE_MISSED_FRAME_IRQ;

	if (should_schedule) {
		if (napi_schedule_prep(&priv->napi)) {
			emac_disable_interrupt(priv);
			__napi_schedule_irqoff(&priv->napi);
		}
	}

	emac_wr(priv, DMA_STATUS_IRQ, clr);

	return IRQ_HANDLED;
}

static void emac_configure_tx(struct emac_priv *priv)
{
	u32 val;

	/* Set base address */
	val = (u32)priv->tx_ring.desc_dma_addr;
	emac_wr(priv, DMA_TRANSMIT_BASE_ADDRESS, val);

	/* Set TX inter-frame gap value, enable transmit */
	val = emac_rd(priv, MAC_TRANSMIT_CONTROL);
	val &= ~MREGBIT_IFG_LEN;
	val |= MREGBIT_TRANSMIT_ENABLE;
	val |= MREGBIT_TRANSMIT_AUTO_RETRY;
	emac_wr(priv, MAC_TRANSMIT_CONTROL, val);

	emac_wr(priv, DMA_TRANSMIT_AUTO_POLL_COUNTER, 0x0);

	/* Start TX DMA */
	val = emac_rd(priv, DMA_CONTROL);
	val |= MREGBIT_START_STOP_TRANSMIT_DMA;
	emac_wr(priv, DMA_CONTROL, val);
}

static void emac_configure_rx(struct emac_priv *priv)
{
	u32 val;

	/* Set base address */
	val = (u32)priv->rx_ring.desc_dma_addr;
	emac_wr(priv, DMA_RECEIVE_BASE_ADDRESS, val);

	/* Enable receive */
	val = emac_rd(priv, MAC_RECEIVE_CONTROL);
	val |= MREGBIT_RECEIVE_ENABLE;
	val |= MREGBIT_STORE_FORWARD;
	emac_wr(priv, MAC_RECEIVE_CONTROL, val);

	/* Start RX DMA */
	val = emac_rd(priv, DMA_CONTROL);
	val |= MREGBIT_START_STOP_RECEIVE_DMA;
	emac_wr(priv, DMA_CONTROL, val);
}

static void emac_adjust_link(struct net_device *dev)
{
	struct emac_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	u32 ctrl;

	if (phydev->link) {
		ctrl = emac_rd(priv, MAC_GLOBAL_CONTROL);

		/* Update duplex and speed from PHY */

		FIELD_MODIFY(MREGBIT_FULL_DUPLEX_MODE, &ctrl,
			     phydev->duplex == DUPLEX_FULL);

		ctrl &= ~MREGBIT_SPEED;

		switch (phydev->speed) {
		case SPEED_1000:
			ctrl |= MREGBIT_SPEED_1000M;
			break;
		case SPEED_100:
			ctrl |= MREGBIT_SPEED_100M;
			break;
		case SPEED_10:
			ctrl |= MREGBIT_SPEED_10M;
			break;
		default:
			netdev_err(dev, "Unknown speed: %d\n", phydev->speed);
			phydev->speed = SPEED_UNKNOWN;
			break;
		}

		emac_wr(priv, MAC_GLOBAL_CONTROL, ctrl);

		emac_set_fc_autoneg(priv);
	}

	phy_print_status(phydev);
}

static void emac_update_delay_line(struct emac_priv *priv)
{
	u32 mask = 0, val = 0;

	mask |= EMAC_RX_DLINE_EN;
	mask |= EMAC_RX_DLINE_STEP_MASK | EMAC_RX_DLINE_CODE_MASK;
	mask |= EMAC_TX_DLINE_EN;
	mask |= EMAC_TX_DLINE_STEP_MASK | EMAC_TX_DLINE_CODE_MASK;

	if (phy_interface_mode_is_rgmii(priv->phy_interface)) {
		val |= EMAC_RX_DLINE_EN;
		val |= FIELD_PREP(EMAC_RX_DLINE_STEP_MASK,
				  EMAC_DLINE_STEP_15P6);
		val |= FIELD_PREP(EMAC_RX_DLINE_CODE_MASK, priv->rx_delay);

		val |= EMAC_TX_DLINE_EN;
		val |= FIELD_PREP(EMAC_TX_DLINE_STEP_MASK,
				  EMAC_DLINE_STEP_15P6);
		val |= FIELD_PREP(EMAC_TX_DLINE_CODE_MASK, priv->tx_delay);
	}

	regmap_update_bits(priv->regmap_apmu,
			   priv->regmap_apmu_offset + APMU_EMAC_DLINE_REG,
			   mask, val);
}

static int emac_phy_connect(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct device *dev = &priv->pdev->dev;
	struct phy_device *phydev;
	struct device_node *np;
	int ret;

	ret = of_get_phy_mode(dev->of_node, &priv->phy_interface);
	if (ret) {
		netdev_err(ndev, "No phy-mode found");
		return ret;
	}

	switch (priv->phy_interface) {
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		break;
	default:
		netdev_err(ndev, "Unsupported PHY interface %s",
			   phy_modes(priv->phy_interface));
		return -EINVAL;
	}

	np = of_parse_phandle(dev->of_node, "phy-handle", 0);
	if (!np && of_phy_is_fixed_link(dev->of_node))
		np = of_node_get(dev->of_node);

	if (!np) {
		netdev_err(ndev, "No PHY specified");
		return -ENODEV;
	}

	ret = emac_phy_interface_config(priv);
	if (ret)
		goto err_node_put;

	phydev = of_phy_connect(ndev, np, &emac_adjust_link, 0,
				priv->phy_interface);
	if (!phydev) {
		netdev_err(ndev, "Could not attach to PHY\n");
		ret = -ENODEV;
		goto err_node_put;
	}

	phy_support_asym_pause(phydev);

	phydev->mac_managed_pm = true;

	emac_update_delay_line(priv);

err_node_put:
	of_node_put(np);
	return ret;
}

static int emac_up(struct emac_priv *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct net_device *ndev = priv->ndev;
	int ret;

	pm_runtime_get_sync(&pdev->dev);

	ret = emac_phy_connect(ndev);
	if (ret) {
		dev_err(&pdev->dev, "emac_phy_connect failed\n");
		goto err_pm_put;
	}

	emac_init_hw(priv);

	emac_set_mac_addr(priv, ndev->dev_addr);
	emac_configure_tx(priv);
	emac_configure_rx(priv);

	emac_alloc_rx_desc_buffers(priv);

	phy_start(ndev->phydev);

	ret = request_irq(priv->irq, emac_interrupt_handler, IRQF_SHARED,
			  ndev->name, ndev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_reset_disconnect_phy;
	}

	/* Don't enable MAC interrupts */
	emac_wr(priv, MAC_INTERRUPT_ENABLE, 0x0);

	/* Enable DMA interrupts */
	emac_wr(priv, DMA_INTERRUPT_ENABLE,
		MREGBIT_TRANSMIT_TRANSFER_DONE_INTR_ENABLE |
			MREGBIT_TRANSMIT_DMA_STOPPED_INTR_ENABLE |
			MREGBIT_RECEIVE_TRANSFER_DONE_INTR_ENABLE |
			MREGBIT_RECEIVE_DMA_STOPPED_INTR_ENABLE |
			MREGBIT_RECEIVE_MISSED_FRAME_INTR_ENABLE);

	napi_enable(&priv->napi);

	netif_start_queue(ndev);

	mod_timer(&priv->stats_timer, jiffies);

	return 0;

err_reset_disconnect_phy:
	emac_reset_hw(priv);
	phy_disconnect(ndev->phydev);

err_pm_put:
	pm_runtime_put_sync(&pdev->dev);
	return ret;
}

static int emac_down(struct emac_priv *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct net_device *ndev = priv->ndev;

	netif_stop_queue(ndev);

	phy_disconnect(ndev->phydev);

	emac_wr(priv, MAC_INTERRUPT_ENABLE, 0x0);
	emac_wr(priv, DMA_INTERRUPT_ENABLE, 0x0);

	free_irq(priv->irq, ndev);

	napi_disable(&priv->napi);

	timer_delete_sync(&priv->txtimer);
	cancel_work_sync(&priv->tx_timeout_task);

	timer_delete_sync(&priv->stats_timer);

	emac_reset_hw(priv);

	/* Update and save current stats, see emac_stats_update() for usage */

	spin_lock_bh(&priv->stats_lock);

	emac_stats_update(priv);

	priv->tx_stats_off = priv->tx_stats;
	priv->rx_stats_off = priv->rx_stats;

	spin_unlock_bh(&priv->stats_lock);

	pm_runtime_put_sync(&pdev->dev);
	return 0;
}

/* Called when net interface is brought up. */
static int emac_open(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct device *dev = &priv->pdev->dev;
	int ret;

	ret = emac_alloc_tx_resources(priv);
	if (ret) {
		dev_err(dev, "Cannot allocate TX resources\n");
		return ret;
	}

	ret = emac_alloc_rx_resources(priv);
	if (ret) {
		dev_err(dev, "Cannot allocate RX resources\n");
		goto err_free_tx;
	}

	ret = emac_up(priv);
	if (ret) {
		dev_err(dev, "Error when bringing interface up\n");
		goto err_free_rx;
	}
	return 0;

err_free_rx:
	emac_free_rx_resources(priv);
err_free_tx:
	emac_free_tx_resources(priv);

	return ret;
}

/* Called when interface is brought down. */
static int emac_stop(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	emac_down(priv);
	emac_free_tx_resources(priv);
	emac_free_rx_resources(priv);

	return 0;
}

static const struct ethtool_ops emac_ethtool_ops = {
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_drvinfo		= emac_get_drvinfo,
	.get_link		= ethtool_op_get_link,

	.get_regs		= emac_ethtool_get_regs,
	.get_regs_len		= emac_ethtool_get_regs_len,

	.get_rmon_stats		= emac_get_rmon_stats,
	.get_pause_stats	= emac_get_pause_stats,
	.get_eth_mac_stats	= emac_get_eth_mac_stats,

	.get_sset_count		= emac_get_sset_count,
	.get_strings		= emac_get_strings,
	.get_ethtool_stats	= emac_get_ethtool_stats,

	.get_pauseparam		= emac_get_pauseparam,
	.set_pauseparam		= emac_set_pauseparam,
};

static const struct net_device_ops emac_netdev_ops = {
	.ndo_open               = emac_open,
	.ndo_stop               = emac_stop,
	.ndo_start_xmit         = emac_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address    = emac_set_mac_address,
	.ndo_eth_ioctl          = phy_do_ioctl_running,
	.ndo_change_mtu         = emac_change_mtu,
	.ndo_tx_timeout         = emac_tx_timeout,
	.ndo_set_rx_mode        = emac_set_rx_mode,
	.ndo_get_stats64	= emac_get_stats64,
};

/* Currently we always use 15.6 ps/step for the delay line */

static u32 delay_ps_to_unit(u32 ps)
{
	return DIV_ROUND_CLOSEST(ps * 10, 156);
}

static u32 delay_unit_to_ps(u32 unit)
{
	return DIV_ROUND_CLOSEST(unit * 156, 10);
}

#define EMAC_MAX_DELAY_UNIT	FIELD_MAX(EMAC_TX_DLINE_CODE_MASK)

/* Minus one just to be safe from rounding errors */
#define EMAC_MAX_DELAY_PS	(delay_unit_to_ps(EMAC_MAX_DELAY_UNIT - 1))

static int emac_config_dt(struct platform_device *pdev, struct emac_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	u8 mac_addr[ETH_ALEN] = { 0 };
	int ret;

	priv->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->iobase))
		return dev_err_probe(dev, PTR_ERR(priv->iobase),
				     "ioremap failed\n");

	priv->regmap_apmu =
		syscon_regmap_lookup_by_phandle_args(np, "spacemit,apmu", 1,
						     &priv->regmap_apmu_offset);

	if (IS_ERR(priv->regmap_apmu))
		return dev_err_probe(dev, PTR_ERR(priv->regmap_apmu),
				     "failed to get syscon\n");

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	ret = of_get_mac_address(np, mac_addr);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			return dev_err_probe(dev, ret,
					     "Can't get MAC address\n");

		dev_info(&pdev->dev, "Using random MAC address\n");
		eth_hw_addr_random(priv->ndev);
	} else {
		eth_hw_addr_set(priv->ndev, mac_addr);
	}

	priv->tx_delay = 0;
	priv->rx_delay = 0;

	of_property_read_u32(np, "tx-internal-delay-ps", &priv->tx_delay);
	of_property_read_u32(np, "rx-internal-delay-ps", &priv->rx_delay);

	if (priv->tx_delay > EMAC_MAX_DELAY_PS) {
		dev_err(&pdev->dev,
			"tx-internal-delay-ps too large: max %d, got %d",
			EMAC_MAX_DELAY_PS, priv->tx_delay);
		return -EINVAL;
	}

	if (priv->rx_delay > EMAC_MAX_DELAY_PS) {
		dev_err(&pdev->dev,
			"rx-internal-delay-ps too large: max %d, got %d",
			EMAC_MAX_DELAY_PS, priv->rx_delay);
		return -EINVAL;
	}

	priv->tx_delay = delay_ps_to_unit(priv->tx_delay);
	priv->rx_delay = delay_ps_to_unit(priv->rx_delay);

	return 0;
}

static void emac_phy_deregister_fixed_link(void *data)
{
	struct device_node *of_node = data;

	of_phy_deregister_fixed_link(of_node);
}

static int emac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reset_control *reset;
	struct net_device *ndev;
	struct emac_priv *priv;
	int ret;

	ndev = devm_alloc_etherdev(dev, sizeof(struct emac_priv));
	if (!ndev)
		return -ENOMEM;

	ndev->hw_features = NETIF_F_SG;
	ndev->features |= ndev->hw_features;

	ndev->max_mtu = EMAC_RX_BUF_4K - (ETH_HLEN + ETH_FCS_LEN);
	ndev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->pdev = pdev;
	platform_set_drvdata(pdev, priv);

	ret = emac_config_dt(pdev, priv);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Configuration failed\n");

	ndev->watchdog_timeo = 5 * HZ;
	ndev->base_addr = (unsigned long)priv->iobase;
	ndev->irq = priv->irq;

	ndev->ethtool_ops = &emac_ethtool_ops;
	ndev->netdev_ops = &emac_netdev_ops;

	devm_pm_runtime_enable(&pdev->dev);

	priv->bus_clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(priv->bus_clk))
		return dev_err_probe(dev, PTR_ERR(priv->bus_clk),
				     "Failed to get clock\n");

	reset = devm_reset_control_get_optional_exclusive_deasserted(&pdev->dev,
								     NULL);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset),
				     "Failed to get reset\n");

	if (of_phy_is_fixed_link(dev->of_node)) {
		ret = of_phy_register_fixed_link(dev->of_node);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to register fixed-link\n");

		ret = devm_add_action_or_reset(dev,
					       emac_phy_deregister_fixed_link,
					       dev->of_node);

		if (ret) {
			dev_err(dev, "devm_add_action_or_reset failed\n");
			return ret;
		}
	}

	emac_sw_init(priv);

	ret = emac_mdio_init(priv);
	if (ret)
		goto err_timer_delete;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	ret = devm_register_netdev(dev, ndev);
	if (ret) {
		dev_err(dev, "devm_register_netdev failed\n");
		goto err_timer_delete;
	}

	netif_napi_add(ndev, &priv->napi, emac_rx_poll);
	netif_carrier_off(ndev);

	return 0;

err_timer_delete:
	timer_delete_sync(&priv->txtimer);
	timer_delete_sync(&priv->stats_timer);

	return ret;
}

static void emac_remove(struct platform_device *pdev)
{
	struct emac_priv *priv = platform_get_drvdata(pdev);

	timer_shutdown_sync(&priv->txtimer);
	cancel_work_sync(&priv->tx_timeout_task);

	timer_shutdown_sync(&priv->stats_timer);

	emac_reset_hw(priv);
}

static int emac_resume(struct device *dev)
{
	struct emac_priv *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->ndev;
	int ret;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable bus clock: %d\n", ret);
		return ret;
	}

	if (!netif_running(ndev))
		return 0;

	ret = emac_open(ndev);
	if (ret) {
		clk_disable_unprepare(priv->bus_clk);
		return ret;
	}

	netif_device_attach(ndev);

	mod_timer(&priv->stats_timer, jiffies);

	return 0;
}

static int emac_suspend(struct device *dev)
{
	struct emac_priv *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->ndev;

	if (!ndev || !netif_running(ndev)) {
		clk_disable_unprepare(priv->bus_clk);
		return 0;
	}

	emac_stop(ndev);

	clk_disable_unprepare(priv->bus_clk);
	netif_device_detach(ndev);
	return 0;
}

static const struct dev_pm_ops emac_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(emac_suspend, emac_resume)
};

static const struct of_device_id emac_of_match[] = {
	{ .compatible = "spacemit,k1-emac" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, emac_of_match);

static struct platform_driver emac_driver = {
	.probe = emac_probe,
	.remove = emac_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(emac_of_match),
		.pm = &emac_pm_ops,
	},
};
module_platform_driver(emac_driver);

MODULE_DESCRIPTION("SpacemiT K1 Ethernet driver");
MODULE_AUTHOR("Vivian Wang <wangruikang@iscas.ac.cn>");
MODULE_LICENSE("GPL");
