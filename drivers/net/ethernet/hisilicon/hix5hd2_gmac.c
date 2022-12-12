// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2014 Linaro Ltd.
 * Copyright (c) 2014 Hisilicon Limited.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/circ_buf.h>

#define STATION_ADDR_LOW		0x0000
#define STATION_ADDR_HIGH		0x0004
#define MAC_DUPLEX_HALF_CTRL		0x0008
#define MAX_FRM_SIZE			0x003c
#define PORT_MODE			0x0040
#define PORT_EN				0x0044
#define BITS_TX_EN			BIT(2)
#define BITS_RX_EN			BIT(1)
#define REC_FILT_CONTROL		0x0064
#define BIT_CRC_ERR_PASS		BIT(5)
#define BIT_PAUSE_FRM_PASS		BIT(4)
#define BIT_VLAN_DROP_EN		BIT(3)
#define BIT_BC_DROP_EN			BIT(2)
#define BIT_MC_MATCH_EN			BIT(1)
#define BIT_UC_MATCH_EN			BIT(0)
#define PORT_MC_ADDR_LOW		0x0068
#define PORT_MC_ADDR_HIGH		0x006C
#define CF_CRC_STRIP			0x01b0
#define MODE_CHANGE_EN			0x01b4
#define BIT_MODE_CHANGE_EN		BIT(0)
#define COL_SLOT_TIME			0x01c0
#define RECV_CONTROL			0x01e0
#define BIT_STRIP_PAD_EN		BIT(3)
#define BIT_RUNT_PKT_EN			BIT(4)
#define CONTROL_WORD			0x0214
#define MDIO_SINGLE_CMD			0x03c0
#define MDIO_SINGLE_DATA		0x03c4
#define MDIO_CTRL			0x03cc
#define MDIO_RDATA_STATUS		0x03d0

#define MDIO_START			BIT(20)
#define MDIO_R_VALID			BIT(0)
#define MDIO_READ			(BIT(17) | MDIO_START)
#define MDIO_WRITE			(BIT(16) | MDIO_START)

#define RX_FQ_START_ADDR		0x0500
#define RX_FQ_DEPTH			0x0504
#define RX_FQ_WR_ADDR			0x0508
#define RX_FQ_RD_ADDR			0x050c
#define RX_FQ_VLDDESC_CNT		0x0510
#define RX_FQ_ALEMPTY_TH		0x0514
#define RX_FQ_REG_EN			0x0518
#define BITS_RX_FQ_START_ADDR_EN	BIT(2)
#define BITS_RX_FQ_DEPTH_EN		BIT(1)
#define BITS_RX_FQ_RD_ADDR_EN		BIT(0)
#define RX_FQ_ALFULL_TH			0x051c
#define RX_BQ_START_ADDR		0x0520
#define RX_BQ_DEPTH			0x0524
#define RX_BQ_WR_ADDR			0x0528
#define RX_BQ_RD_ADDR			0x052c
#define RX_BQ_FREE_DESC_CNT		0x0530
#define RX_BQ_ALEMPTY_TH		0x0534
#define RX_BQ_REG_EN			0x0538
#define BITS_RX_BQ_START_ADDR_EN	BIT(2)
#define BITS_RX_BQ_DEPTH_EN		BIT(1)
#define BITS_RX_BQ_WR_ADDR_EN		BIT(0)
#define RX_BQ_ALFULL_TH			0x053c
#define TX_BQ_START_ADDR		0x0580
#define TX_BQ_DEPTH			0x0584
#define TX_BQ_WR_ADDR			0x0588
#define TX_BQ_RD_ADDR			0x058c
#define TX_BQ_VLDDESC_CNT		0x0590
#define TX_BQ_ALEMPTY_TH		0x0594
#define TX_BQ_REG_EN			0x0598
#define BITS_TX_BQ_START_ADDR_EN	BIT(2)
#define BITS_TX_BQ_DEPTH_EN		BIT(1)
#define BITS_TX_BQ_RD_ADDR_EN		BIT(0)
#define TX_BQ_ALFULL_TH			0x059c
#define TX_RQ_START_ADDR		0x05a0
#define TX_RQ_DEPTH			0x05a4
#define TX_RQ_WR_ADDR			0x05a8
#define TX_RQ_RD_ADDR			0x05ac
#define TX_RQ_FREE_DESC_CNT		0x05b0
#define TX_RQ_ALEMPTY_TH		0x05b4
#define TX_RQ_REG_EN			0x05b8
#define BITS_TX_RQ_START_ADDR_EN	BIT(2)
#define BITS_TX_RQ_DEPTH_EN		BIT(1)
#define BITS_TX_RQ_WR_ADDR_EN		BIT(0)
#define TX_RQ_ALFULL_TH			0x05bc
#define RAW_PMU_INT			0x05c0
#define ENA_PMU_INT			0x05c4
#define STATUS_PMU_INT			0x05c8
#define MAC_FIFO_ERR_IN			BIT(30)
#define TX_RQ_IN_TIMEOUT_INT		BIT(29)
#define RX_BQ_IN_TIMEOUT_INT		BIT(28)
#define TXOUTCFF_FULL_INT		BIT(27)
#define TXOUTCFF_EMPTY_INT		BIT(26)
#define TXCFF_FULL_INT			BIT(25)
#define TXCFF_EMPTY_INT			BIT(24)
#define RXOUTCFF_FULL_INT		BIT(23)
#define RXOUTCFF_EMPTY_INT		BIT(22)
#define RXCFF_FULL_INT			BIT(21)
#define RXCFF_EMPTY_INT			BIT(20)
#define TX_RQ_IN_INT			BIT(19)
#define TX_BQ_OUT_INT			BIT(18)
#define RX_BQ_IN_INT			BIT(17)
#define RX_FQ_OUT_INT			BIT(16)
#define TX_RQ_EMPTY_INT			BIT(15)
#define TX_RQ_FULL_INT			BIT(14)
#define TX_RQ_ALEMPTY_INT		BIT(13)
#define TX_RQ_ALFULL_INT		BIT(12)
#define TX_BQ_EMPTY_INT			BIT(11)
#define TX_BQ_FULL_INT			BIT(10)
#define TX_BQ_ALEMPTY_INT		BIT(9)
#define TX_BQ_ALFULL_INT		BIT(8)
#define RX_BQ_EMPTY_INT			BIT(7)
#define RX_BQ_FULL_INT			BIT(6)
#define RX_BQ_ALEMPTY_INT		BIT(5)
#define RX_BQ_ALFULL_INT		BIT(4)
#define RX_FQ_EMPTY_INT			BIT(3)
#define RX_FQ_FULL_INT			BIT(2)
#define RX_FQ_ALEMPTY_INT		BIT(1)
#define RX_FQ_ALFULL_INT		BIT(0)

#define DEF_INT_MASK			(RX_BQ_IN_INT | RX_BQ_IN_TIMEOUT_INT | \
					TX_RQ_IN_INT | TX_RQ_IN_TIMEOUT_INT)

#define DESC_WR_RD_ENA			0x05cc
#define IN_QUEUE_TH			0x05d8
#define OUT_QUEUE_TH			0x05dc
#define QUEUE_TX_BQ_SHIFT		16
#define RX_BQ_IN_TIMEOUT_TH		0x05e0
#define TX_RQ_IN_TIMEOUT_TH		0x05e4
#define STOP_CMD			0x05e8
#define BITS_TX_STOP			BIT(1)
#define BITS_RX_STOP			BIT(0)
#define FLUSH_CMD			0x05eC
#define BITS_TX_FLUSH_CMD		BIT(5)
#define BITS_RX_FLUSH_CMD		BIT(4)
#define BITS_TX_FLUSH_FLAG_DOWN		BIT(3)
#define BITS_TX_FLUSH_FLAG_UP		BIT(2)
#define BITS_RX_FLUSH_FLAG_DOWN		BIT(1)
#define BITS_RX_FLUSH_FLAG_UP		BIT(0)
#define RX_CFF_NUM_REG			0x05f0
#define PMU_FSM_REG			0x05f8
#define RX_FIFO_PKT_IN_NUM		0x05fc
#define RX_FIFO_PKT_OUT_NUM		0x0600

#define RGMII_SPEED_1000		0x2c
#define RGMII_SPEED_100			0x2f
#define RGMII_SPEED_10			0x2d
#define MII_SPEED_100			0x0f
#define MII_SPEED_10			0x0d
#define GMAC_SPEED_1000			0x05
#define GMAC_SPEED_100			0x01
#define GMAC_SPEED_10			0x00
#define GMAC_FULL_DUPLEX		BIT(4)

#define RX_BQ_INT_THRESHOLD		0x01
#define TX_RQ_INT_THRESHOLD		0x01
#define RX_BQ_IN_TIMEOUT		0x10000
#define TX_RQ_IN_TIMEOUT		0x50000

#define MAC_MAX_FRAME_SIZE		1600
#define DESC_SIZE			32
#define RX_DESC_NUM			1024
#define TX_DESC_NUM			1024

#define DESC_VLD_FREE			0
#define DESC_VLD_BUSY			0x80000000
#define DESC_FL_MID			0
#define DESC_FL_LAST			0x20000000
#define DESC_FL_FIRST			0x40000000
#define DESC_FL_FULL			0x60000000
#define DESC_DATA_LEN_OFF		16
#define DESC_BUFF_LEN_OFF		0
#define DESC_DATA_MASK			0x7ff
#define DESC_SG				BIT(30)
#define DESC_FRAGS_NUM_OFF		11

/* DMA descriptor ring helpers */
#define dma_ring_incr(n, s)		(((n) + 1) & ((s) - 1))
#define dma_cnt(n)			((n) >> 5)
#define dma_byte(n)			((n) << 5)

#define HW_CAP_TSO			BIT(0)
#define GEMAC_V1			0
#define GEMAC_V2			(GEMAC_V1 | HW_CAP_TSO)
#define HAS_CAP_TSO(hw_cap)		((hw_cap) & HW_CAP_TSO)

#define PHY_RESET_DELAYS_PROPERTY	"hisilicon,phy-reset-delays-us"

enum phy_reset_delays {
	PRE_DELAY,
	PULSE,
	POST_DELAY,
	DELAYS_NUM,
};

struct hix5hd2_desc {
	__le32 buff_addr;
	__le32 cmd;
} __aligned(32);

struct hix5hd2_desc_sw {
	struct hix5hd2_desc *desc;
	dma_addr_t	phys_addr;
	unsigned int	count;
	unsigned int	size;
};

struct hix5hd2_sg_desc_ring {
	struct sg_desc *desc;
	dma_addr_t phys_addr;
};

struct frags_info {
	__le32 addr;
	__le32 size;
};

/* hardware supported max skb frags num */
#define SG_MAX_SKB_FRAGS	17
struct sg_desc {
	__le32 total_len;
	__le32 resvd0;
	__le32 linear_addr;
	__le32 linear_len;
	/* reserve one more frags for memory alignment */
	struct frags_info frags[SG_MAX_SKB_FRAGS + 1];
};

#define QUEUE_NUMS	4
struct hix5hd2_priv {
	struct hix5hd2_desc_sw pool[QUEUE_NUMS];
#define rx_fq		pool[0]
#define rx_bq		pool[1]
#define tx_bq		pool[2]
#define tx_rq		pool[3]
	struct hix5hd2_sg_desc_ring tx_ring;

	void __iomem *base;
	void __iomem *ctrl_base;

	struct sk_buff *tx_skb[TX_DESC_NUM];
	struct sk_buff *rx_skb[RX_DESC_NUM];

	struct device *dev;
	struct net_device *netdev;

	struct device_node *phy_node;
	phy_interface_t	phy_mode;

	unsigned long hw_cap;
	unsigned int speed;
	unsigned int duplex;

	struct clk *mac_core_clk;
	struct clk *mac_ifc_clk;
	struct reset_control *mac_core_rst;
	struct reset_control *mac_ifc_rst;
	struct reset_control *phy_rst;
	u32 phy_reset_delays[DELAYS_NUM];
	struct mii_bus *bus;
	struct napi_struct napi;
	struct work_struct tx_timeout_task;
};

static inline void hix5hd2_mac_interface_reset(struct hix5hd2_priv *priv)
{
	if (!priv->mac_ifc_rst)
		return;

	reset_control_assert(priv->mac_ifc_rst);
	reset_control_deassert(priv->mac_ifc_rst);
}

static void hix5hd2_config_port(struct net_device *dev, u32 speed, u32 duplex)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);
	u32 val;

	priv->speed = speed;
	priv->duplex = duplex;

	switch (priv->phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
		if (speed == SPEED_1000)
			val = RGMII_SPEED_1000;
		else if (speed == SPEED_100)
			val = RGMII_SPEED_100;
		else
			val = RGMII_SPEED_10;
		break;
	case PHY_INTERFACE_MODE_MII:
		if (speed == SPEED_100)
			val = MII_SPEED_100;
		else
			val = MII_SPEED_10;
		break;
	default:
		netdev_warn(dev, "not supported mode\n");
		val = MII_SPEED_10;
		break;
	}

	if (duplex)
		val |= GMAC_FULL_DUPLEX;
	writel_relaxed(val, priv->ctrl_base);
	hix5hd2_mac_interface_reset(priv);

	writel_relaxed(BIT_MODE_CHANGE_EN, priv->base + MODE_CHANGE_EN);
	if (speed == SPEED_1000)
		val = GMAC_SPEED_1000;
	else if (speed == SPEED_100)
		val = GMAC_SPEED_100;
	else
		val = GMAC_SPEED_10;
	writel_relaxed(val, priv->base + PORT_MODE);
	writel_relaxed(0, priv->base + MODE_CHANGE_EN);
	writel_relaxed(duplex, priv->base + MAC_DUPLEX_HALF_CTRL);
}

static void hix5hd2_set_desc_depth(struct hix5hd2_priv *priv, int rx, int tx)
{
	writel_relaxed(BITS_RX_FQ_DEPTH_EN, priv->base + RX_FQ_REG_EN);
	writel_relaxed(rx << 3, priv->base + RX_FQ_DEPTH);
	writel_relaxed(0, priv->base + RX_FQ_REG_EN);

	writel_relaxed(BITS_RX_BQ_DEPTH_EN, priv->base + RX_BQ_REG_EN);
	writel_relaxed(rx << 3, priv->base + RX_BQ_DEPTH);
	writel_relaxed(0, priv->base + RX_BQ_REG_EN);

	writel_relaxed(BITS_TX_BQ_DEPTH_EN, priv->base + TX_BQ_REG_EN);
	writel_relaxed(tx << 3, priv->base + TX_BQ_DEPTH);
	writel_relaxed(0, priv->base + TX_BQ_REG_EN);

	writel_relaxed(BITS_TX_RQ_DEPTH_EN, priv->base + TX_RQ_REG_EN);
	writel_relaxed(tx << 3, priv->base + TX_RQ_DEPTH);
	writel_relaxed(0, priv->base + TX_RQ_REG_EN);
}

static void hix5hd2_set_rx_fq(struct hix5hd2_priv *priv, dma_addr_t phy_addr)
{
	writel_relaxed(BITS_RX_FQ_START_ADDR_EN, priv->base + RX_FQ_REG_EN);
	writel_relaxed(phy_addr, priv->base + RX_FQ_START_ADDR);
	writel_relaxed(0, priv->base + RX_FQ_REG_EN);
}

static void hix5hd2_set_rx_bq(struct hix5hd2_priv *priv, dma_addr_t phy_addr)
{
	writel_relaxed(BITS_RX_BQ_START_ADDR_EN, priv->base + RX_BQ_REG_EN);
	writel_relaxed(phy_addr, priv->base + RX_BQ_START_ADDR);
	writel_relaxed(0, priv->base + RX_BQ_REG_EN);
}

static void hix5hd2_set_tx_bq(struct hix5hd2_priv *priv, dma_addr_t phy_addr)
{
	writel_relaxed(BITS_TX_BQ_START_ADDR_EN, priv->base + TX_BQ_REG_EN);
	writel_relaxed(phy_addr, priv->base + TX_BQ_START_ADDR);
	writel_relaxed(0, priv->base + TX_BQ_REG_EN);
}

static void hix5hd2_set_tx_rq(struct hix5hd2_priv *priv, dma_addr_t phy_addr)
{
	writel_relaxed(BITS_TX_RQ_START_ADDR_EN, priv->base + TX_RQ_REG_EN);
	writel_relaxed(phy_addr, priv->base + TX_RQ_START_ADDR);
	writel_relaxed(0, priv->base + TX_RQ_REG_EN);
}

static void hix5hd2_set_desc_addr(struct hix5hd2_priv *priv)
{
	hix5hd2_set_rx_fq(priv, priv->rx_fq.phys_addr);
	hix5hd2_set_rx_bq(priv, priv->rx_bq.phys_addr);
	hix5hd2_set_tx_rq(priv, priv->tx_rq.phys_addr);
	hix5hd2_set_tx_bq(priv, priv->tx_bq.phys_addr);
}

static void hix5hd2_hw_init(struct hix5hd2_priv *priv)
{
	u32 val;

	/* disable and clear all interrupts */
	writel_relaxed(0, priv->base + ENA_PMU_INT);
	writel_relaxed(~0, priv->base + RAW_PMU_INT);

	writel_relaxed(BIT_CRC_ERR_PASS, priv->base + REC_FILT_CONTROL);
	writel_relaxed(MAC_MAX_FRAME_SIZE, priv->base + CONTROL_WORD);
	writel_relaxed(0, priv->base + COL_SLOT_TIME);

	val = RX_BQ_INT_THRESHOLD | TX_RQ_INT_THRESHOLD << QUEUE_TX_BQ_SHIFT;
	writel_relaxed(val, priv->base + IN_QUEUE_TH);

	writel_relaxed(RX_BQ_IN_TIMEOUT, priv->base + RX_BQ_IN_TIMEOUT_TH);
	writel_relaxed(TX_RQ_IN_TIMEOUT, priv->base + TX_RQ_IN_TIMEOUT_TH);

	hix5hd2_set_desc_depth(priv, RX_DESC_NUM, TX_DESC_NUM);
	hix5hd2_set_desc_addr(priv);
}

static void hix5hd2_irq_enable(struct hix5hd2_priv *priv)
{
	writel_relaxed(DEF_INT_MASK, priv->base + ENA_PMU_INT);
}

static void hix5hd2_irq_disable(struct hix5hd2_priv *priv)
{
	writel_relaxed(0, priv->base + ENA_PMU_INT);
}

static void hix5hd2_port_enable(struct hix5hd2_priv *priv)
{
	writel_relaxed(0xf, priv->base + DESC_WR_RD_ENA);
	writel_relaxed(BITS_RX_EN | BITS_TX_EN, priv->base + PORT_EN);
}

static void hix5hd2_port_disable(struct hix5hd2_priv *priv)
{
	writel_relaxed(~(u32)(BITS_RX_EN | BITS_TX_EN), priv->base + PORT_EN);
	writel_relaxed(0, priv->base + DESC_WR_RD_ENA);
}

static void hix5hd2_hw_set_mac_addr(struct net_device *dev)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);
	const unsigned char *mac = dev->dev_addr;
	u32 val;

	val = mac[1] | (mac[0] << 8);
	writel_relaxed(val, priv->base + STATION_ADDR_HIGH);

	val = mac[5] | (mac[4] << 8) | (mac[3] << 16) | (mac[2] << 24);
	writel_relaxed(val, priv->base + STATION_ADDR_LOW);
}

static int hix5hd2_net_set_mac_address(struct net_device *dev, void *p)
{
	int ret;

	ret = eth_mac_addr(dev, p);
	if (!ret)
		hix5hd2_hw_set_mac_addr(dev);

	return ret;
}

static void hix5hd2_adjust_link(struct net_device *dev)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);
	struct phy_device *phy = dev->phydev;

	if ((priv->speed != phy->speed) || (priv->duplex != phy->duplex)) {
		hix5hd2_config_port(dev, phy->speed, phy->duplex);
		phy_print_status(phy);
	}
}

static void hix5hd2_rx_refill(struct hix5hd2_priv *priv)
{
	struct hix5hd2_desc *desc;
	struct sk_buff *skb;
	u32 start, end, num, pos, i;
	u32 len = MAC_MAX_FRAME_SIZE;
	dma_addr_t addr;

	/* software write pointer */
	start = dma_cnt(readl_relaxed(priv->base + RX_FQ_WR_ADDR));
	/* logic read pointer */
	end = dma_cnt(readl_relaxed(priv->base + RX_FQ_RD_ADDR));
	num = CIRC_SPACE(start, end, RX_DESC_NUM);

	for (i = 0, pos = start; i < num; i++) {
		if (priv->rx_skb[pos]) {
			break;
		} else {
			skb = netdev_alloc_skb_ip_align(priv->netdev, len);
			if (unlikely(skb == NULL))
				break;
		}

		addr = dma_map_single(priv->dev, skb->data, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(priv->dev, addr)) {
			dev_kfree_skb_any(skb);
			break;
		}

		desc = priv->rx_fq.desc + pos;
		desc->buff_addr = cpu_to_le32(addr);
		priv->rx_skb[pos] = skb;
		desc->cmd = cpu_to_le32(DESC_VLD_FREE |
					(len - 1) << DESC_BUFF_LEN_OFF);
		pos = dma_ring_incr(pos, RX_DESC_NUM);
	}

	/* ensure desc updated */
	wmb();

	if (pos != start)
		writel_relaxed(dma_byte(pos), priv->base + RX_FQ_WR_ADDR);
}

static int hix5hd2_rx(struct net_device *dev, int limit)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	struct hix5hd2_desc *desc;
	dma_addr_t addr;
	u32 start, end, num, pos, i, len;

	/* software read pointer */
	start = dma_cnt(readl_relaxed(priv->base + RX_BQ_RD_ADDR));
	/* logic write pointer */
	end = dma_cnt(readl_relaxed(priv->base + RX_BQ_WR_ADDR));
	num = CIRC_CNT(end, start, RX_DESC_NUM);
	if (num > limit)
		num = limit;

	/* ensure get updated desc */
	rmb();
	for (i = 0, pos = start; i < num; i++) {
		skb = priv->rx_skb[pos];
		if (unlikely(!skb)) {
			netdev_err(dev, "inconsistent rx_skb\n");
			break;
		}
		priv->rx_skb[pos] = NULL;

		desc = priv->rx_bq.desc + pos;
		len = (le32_to_cpu(desc->cmd) >> DESC_DATA_LEN_OFF) &
		       DESC_DATA_MASK;
		addr = le32_to_cpu(desc->buff_addr);
		dma_unmap_single(priv->dev, addr, MAC_MAX_FRAME_SIZE,
				 DMA_FROM_DEVICE);

		skb_put(skb, len);
		if (skb->len > MAC_MAX_FRAME_SIZE) {
			netdev_err(dev, "rcv len err, len = %d\n", skb->len);
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			dev_kfree_skb_any(skb);
			goto next;
		}

		skb->protocol = eth_type_trans(skb, dev);
		napi_gro_receive(&priv->napi, skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
next:
		pos = dma_ring_incr(pos, RX_DESC_NUM);
	}

	if (pos != start)
		writel_relaxed(dma_byte(pos), priv->base + RX_BQ_RD_ADDR);

	hix5hd2_rx_refill(priv);

	return num;
}

static void hix5hd2_clean_sg_desc(struct hix5hd2_priv *priv,
				  struct sk_buff *skb, u32 pos)
{
	struct sg_desc *desc;
	dma_addr_t addr;
	u32 len;
	int i;

	desc = priv->tx_ring.desc + pos;

	addr = le32_to_cpu(desc->linear_addr);
	len = le32_to_cpu(desc->linear_len);
	dma_unmap_single(priv->dev, addr, len, DMA_TO_DEVICE);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		addr = le32_to_cpu(desc->frags[i].addr);
		len = le32_to_cpu(desc->frags[i].size);
		dma_unmap_page(priv->dev, addr, len, DMA_TO_DEVICE);
	}
}

static void hix5hd2_xmit_reclaim(struct net_device *dev)
{
	struct sk_buff *skb;
	struct hix5hd2_desc *desc;
	struct hix5hd2_priv *priv = netdev_priv(dev);
	unsigned int bytes_compl = 0, pkts_compl = 0;
	u32 start, end, num, pos, i;
	dma_addr_t addr;

	netif_tx_lock(dev);

	/* software read */
	start = dma_cnt(readl_relaxed(priv->base + TX_RQ_RD_ADDR));
	/* logic write */
	end = dma_cnt(readl_relaxed(priv->base + TX_RQ_WR_ADDR));
	num = CIRC_CNT(end, start, TX_DESC_NUM);

	for (i = 0, pos = start; i < num; i++) {
		skb = priv->tx_skb[pos];
		if (unlikely(!skb)) {
			netdev_err(dev, "inconsistent tx_skb\n");
			break;
		}

		pkts_compl++;
		bytes_compl += skb->len;
		desc = priv->tx_rq.desc + pos;

		if (skb_shinfo(skb)->nr_frags) {
			hix5hd2_clean_sg_desc(priv, skb, pos);
		} else {
			addr = le32_to_cpu(desc->buff_addr);
			dma_unmap_single(priv->dev, addr, skb->len,
					 DMA_TO_DEVICE);
		}

		priv->tx_skb[pos] = NULL;
		dev_consume_skb_any(skb);
		pos = dma_ring_incr(pos, TX_DESC_NUM);
	}

	if (pos != start)
		writel_relaxed(dma_byte(pos), priv->base + TX_RQ_RD_ADDR);

	netif_tx_unlock(dev);

	if (pkts_compl || bytes_compl)
		netdev_completed_queue(dev, pkts_compl, bytes_compl);

	if (unlikely(netif_queue_stopped(priv->netdev)) && pkts_compl)
		netif_wake_queue(priv->netdev);
}

static int hix5hd2_poll(struct napi_struct *napi, int budget)
{
	struct hix5hd2_priv *priv = container_of(napi,
				struct hix5hd2_priv, napi);
	struct net_device *dev = priv->netdev;
	int work_done = 0, task = budget;
	int ints, num;

	do {
		hix5hd2_xmit_reclaim(dev);
		num = hix5hd2_rx(dev, task);
		work_done += num;
		task -= num;
		if ((work_done >= budget) || (num == 0))
			break;

		ints = readl_relaxed(priv->base + RAW_PMU_INT);
		writel_relaxed(ints, priv->base + RAW_PMU_INT);
	} while (ints & DEF_INT_MASK);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		hix5hd2_irq_enable(priv);
	}

	return work_done;
}

static irqreturn_t hix5hd2_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct hix5hd2_priv *priv = netdev_priv(dev);
	int ints = readl_relaxed(priv->base + RAW_PMU_INT);

	writel_relaxed(ints, priv->base + RAW_PMU_INT);
	if (likely(ints & DEF_INT_MASK)) {
		hix5hd2_irq_disable(priv);
		napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

static u32 hix5hd2_get_desc_cmd(struct sk_buff *skb, unsigned long hw_cap)
{
	u32 cmd = 0;

	if (HAS_CAP_TSO(hw_cap)) {
		if (skb_shinfo(skb)->nr_frags)
			cmd |= DESC_SG;
		cmd |= skb_shinfo(skb)->nr_frags << DESC_FRAGS_NUM_OFF;
	} else {
		cmd |= DESC_FL_FULL |
			((skb->len & DESC_DATA_MASK) << DESC_BUFF_LEN_OFF);
	}

	cmd |= (skb->len & DESC_DATA_MASK) << DESC_DATA_LEN_OFF;
	cmd |= DESC_VLD_BUSY;

	return cmd;
}

static int hix5hd2_fill_sg_desc(struct hix5hd2_priv *priv,
				struct sk_buff *skb, u32 pos)
{
	struct sg_desc *desc;
	dma_addr_t addr;
	int ret;
	int i;

	desc = priv->tx_ring.desc + pos;

	desc->total_len = cpu_to_le32(skb->len);
	addr = dma_map_single(priv->dev, skb->data, skb_headlen(skb),
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, addr)))
		return -EINVAL;
	desc->linear_addr = cpu_to_le32(addr);
	desc->linear_len = cpu_to_le32(skb_headlen(skb));

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int len = skb_frag_size(frag);

		addr = skb_frag_dma_map(priv->dev, frag, 0, len, DMA_TO_DEVICE);
		ret = dma_mapping_error(priv->dev, addr);
		if (unlikely(ret))
			return -EINVAL;
		desc->frags[i].addr = cpu_to_le32(addr);
		desc->frags[i].size = cpu_to_le32(len);
	}

	return 0;
}

static netdev_tx_t hix5hd2_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);
	struct hix5hd2_desc *desc;
	dma_addr_t addr;
	u32 pos;
	u32 cmd;
	int ret;

	/* software write pointer */
	pos = dma_cnt(readl_relaxed(priv->base + TX_BQ_WR_ADDR));
	if (unlikely(priv->tx_skb[pos])) {
		dev->stats.tx_dropped++;
		dev->stats.tx_fifo_errors++;
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	desc = priv->tx_bq.desc + pos;

	cmd = hix5hd2_get_desc_cmd(skb, priv->hw_cap);
	desc->cmd = cpu_to_le32(cmd);

	if (skb_shinfo(skb)->nr_frags) {
		ret = hix5hd2_fill_sg_desc(priv, skb, pos);
		if (unlikely(ret)) {
			dev_kfree_skb_any(skb);
			dev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}
		addr = priv->tx_ring.phys_addr + pos * sizeof(struct sg_desc);
	} else {
		addr = dma_map_single(priv->dev, skb->data, skb->len,
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(priv->dev, addr))) {
			dev_kfree_skb_any(skb);
			dev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}
	}
	desc->buff_addr = cpu_to_le32(addr);

	priv->tx_skb[pos] = skb;

	/* ensure desc updated */
	wmb();

	pos = dma_ring_incr(pos, TX_DESC_NUM);
	writel_relaxed(dma_byte(pos), priv->base + TX_BQ_WR_ADDR);

	netif_trans_update(dev);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	netdev_sent_queue(dev, skb->len);

	return NETDEV_TX_OK;
}

static void hix5hd2_free_dma_desc_rings(struct hix5hd2_priv *priv)
{
	struct hix5hd2_desc *desc;
	dma_addr_t addr;
	int i;

	for (i = 0; i < RX_DESC_NUM; i++) {
		struct sk_buff *skb = priv->rx_skb[i];
		if (skb == NULL)
			continue;

		desc = priv->rx_fq.desc + i;
		addr = le32_to_cpu(desc->buff_addr);
		dma_unmap_single(priv->dev, addr,
				 MAC_MAX_FRAME_SIZE, DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		priv->rx_skb[i] = NULL;
	}

	for (i = 0; i < TX_DESC_NUM; i++) {
		struct sk_buff *skb = priv->tx_skb[i];
		if (skb == NULL)
			continue;

		desc = priv->tx_rq.desc + i;
		addr = le32_to_cpu(desc->buff_addr);
		dma_unmap_single(priv->dev, addr, skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		priv->tx_skb[i] = NULL;
	}
}

static int hix5hd2_net_open(struct net_device *dev)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);
	struct phy_device *phy;
	int ret;

	ret = clk_prepare_enable(priv->mac_core_clk);
	if (ret < 0) {
		netdev_err(dev, "failed to enable mac core clk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->mac_ifc_clk);
	if (ret < 0) {
		clk_disable_unprepare(priv->mac_core_clk);
		netdev_err(dev, "failed to enable mac ifc clk %d\n", ret);
		return ret;
	}

	phy = of_phy_connect(dev, priv->phy_node,
			     &hix5hd2_adjust_link, 0, priv->phy_mode);
	if (!phy) {
		clk_disable_unprepare(priv->mac_ifc_clk);
		clk_disable_unprepare(priv->mac_core_clk);
		return -ENODEV;
	}

	phy_start(phy);
	hix5hd2_hw_init(priv);
	hix5hd2_rx_refill(priv);

	netdev_reset_queue(dev);
	netif_start_queue(dev);
	napi_enable(&priv->napi);

	hix5hd2_port_enable(priv);
	hix5hd2_irq_enable(priv);

	return 0;
}

static int hix5hd2_net_close(struct net_device *dev)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);

	hix5hd2_port_disable(priv);
	hix5hd2_irq_disable(priv);
	napi_disable(&priv->napi);
	netif_stop_queue(dev);
	hix5hd2_free_dma_desc_rings(priv);

	if (dev->phydev) {
		phy_stop(dev->phydev);
		phy_disconnect(dev->phydev);
	}

	clk_disable_unprepare(priv->mac_ifc_clk);
	clk_disable_unprepare(priv->mac_core_clk);

	return 0;
}

static void hix5hd2_tx_timeout_task(struct work_struct *work)
{
	struct hix5hd2_priv *priv;

	priv = container_of(work, struct hix5hd2_priv, tx_timeout_task);
	hix5hd2_net_close(priv->netdev);
	hix5hd2_net_open(priv->netdev);
}

static void hix5hd2_net_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct hix5hd2_priv *priv = netdev_priv(dev);

	schedule_work(&priv->tx_timeout_task);
}

static const struct net_device_ops hix5hd2_netdev_ops = {
	.ndo_open		= hix5hd2_net_open,
	.ndo_stop		= hix5hd2_net_close,
	.ndo_start_xmit		= hix5hd2_net_xmit,
	.ndo_tx_timeout		= hix5hd2_net_timeout,
	.ndo_set_mac_address	= hix5hd2_net_set_mac_address,
};

static const struct ethtool_ops hix5hd2_ethtools_ops = {
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings     = phy_ethtool_get_link_ksettings,
	.set_link_ksettings     = phy_ethtool_set_link_ksettings,
};

static int hix5hd2_mdio_wait_ready(struct mii_bus *bus)
{
	struct hix5hd2_priv *priv = bus->priv;
	void __iomem *base = priv->base;
	int i, timeout = 10000;

	for (i = 0; readl_relaxed(base + MDIO_SINGLE_CMD) & MDIO_START; i++) {
		if (i == timeout)
			return -ETIMEDOUT;
		usleep_range(10, 20);
	}

	return 0;
}

static int hix5hd2_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct hix5hd2_priv *priv = bus->priv;
	void __iomem *base = priv->base;
	int val, ret;

	ret = hix5hd2_mdio_wait_ready(bus);
	if (ret < 0)
		goto out;

	writel_relaxed(MDIO_READ | phy << 8 | reg, base + MDIO_SINGLE_CMD);
	ret = hix5hd2_mdio_wait_ready(bus);
	if (ret < 0)
		goto out;

	val = readl_relaxed(base + MDIO_RDATA_STATUS);
	if (val & MDIO_R_VALID) {
		dev_err(bus->parent, "SMI bus read not valid\n");
		ret = -ENODEV;
		goto out;
	}

	val = readl_relaxed(priv->base + MDIO_SINGLE_DATA);
	ret = (val >> 16) & 0xFFFF;
out:
	return ret;
}

static int hix5hd2_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct hix5hd2_priv *priv = bus->priv;
	void __iomem *base = priv->base;
	int ret;

	ret = hix5hd2_mdio_wait_ready(bus);
	if (ret < 0)
		goto out;

	writel_relaxed(val, base + MDIO_SINGLE_DATA);
	writel_relaxed(MDIO_WRITE | phy << 8 | reg, base + MDIO_SINGLE_CMD);
	ret = hix5hd2_mdio_wait_ready(bus);
out:
	return ret;
}

static void hix5hd2_destroy_hw_desc_queue(struct hix5hd2_priv *priv)
{
	int i;

	for (i = 0; i < QUEUE_NUMS; i++) {
		if (priv->pool[i].desc) {
			dma_free_coherent(priv->dev, priv->pool[i].size,
					  priv->pool[i].desc,
					  priv->pool[i].phys_addr);
			priv->pool[i].desc = NULL;
		}
	}
}

static int hix5hd2_init_hw_desc_queue(struct hix5hd2_priv *priv)
{
	struct device *dev = priv->dev;
	struct hix5hd2_desc *virt_addr;
	dma_addr_t phys_addr;
	int size, i;

	priv->rx_fq.count = RX_DESC_NUM;
	priv->rx_bq.count = RX_DESC_NUM;
	priv->tx_bq.count = TX_DESC_NUM;
	priv->tx_rq.count = TX_DESC_NUM;

	for (i = 0; i < QUEUE_NUMS; i++) {
		size = priv->pool[i].count * sizeof(struct hix5hd2_desc);
		virt_addr = dma_alloc_coherent(dev, size, &phys_addr,
					       GFP_KERNEL);
		if (virt_addr == NULL)
			goto error_free_pool;

		priv->pool[i].size = size;
		priv->pool[i].desc = virt_addr;
		priv->pool[i].phys_addr = phys_addr;
	}
	return 0;

error_free_pool:
	hix5hd2_destroy_hw_desc_queue(priv);

	return -ENOMEM;
}

static int hix5hd2_init_sg_desc_queue(struct hix5hd2_priv *priv)
{
	struct sg_desc *desc;
	dma_addr_t phys_addr;

	desc = dma_alloc_coherent(priv->dev,
				  TX_DESC_NUM * sizeof(struct sg_desc),
				  &phys_addr, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	priv->tx_ring.desc = desc;
	priv->tx_ring.phys_addr = phys_addr;

	return 0;
}

static void hix5hd2_destroy_sg_desc_queue(struct hix5hd2_priv *priv)
{
	if (priv->tx_ring.desc) {
		dma_free_coherent(priv->dev,
				  TX_DESC_NUM * sizeof(struct sg_desc),
				  priv->tx_ring.desc, priv->tx_ring.phys_addr);
		priv->tx_ring.desc = NULL;
	}
}

static inline void hix5hd2_mac_core_reset(struct hix5hd2_priv *priv)
{
	if (!priv->mac_core_rst)
		return;

	reset_control_assert(priv->mac_core_rst);
	reset_control_deassert(priv->mac_core_rst);
}

static void hix5hd2_sleep_us(u32 time_us)
{
	u32 time_ms;

	if (!time_us)
		return;

	time_ms = DIV_ROUND_UP(time_us, 1000);
	if (time_ms < 20)
		usleep_range(time_us, time_us + 500);
	else
		msleep(time_ms);
}

static void hix5hd2_phy_reset(struct hix5hd2_priv *priv)
{
	/* To make sure PHY hardware reset success,
	 * we must keep PHY in deassert state first and
	 * then complete the hardware reset operation
	 */
	reset_control_deassert(priv->phy_rst);
	hix5hd2_sleep_us(priv->phy_reset_delays[PRE_DELAY]);

	reset_control_assert(priv->phy_rst);
	/* delay some time to ensure reset ok,
	 * this depends on PHY hardware feature
	 */
	hix5hd2_sleep_us(priv->phy_reset_delays[PULSE]);
	reset_control_deassert(priv->phy_rst);
	/* delay some time to ensure later MDIO access */
	hix5hd2_sleep_us(priv->phy_reset_delays[POST_DELAY]);
}

static const struct of_device_id hix5hd2_of_match[];

static int hix5hd2_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct of_device_id *of_id = NULL;
	struct net_device *ndev;
	struct hix5hd2_priv *priv;
	struct mii_bus *bus;
	int ret;

	ndev = alloc_etherdev(sizeof(struct hix5hd2_priv));
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	priv = netdev_priv(ndev);
	priv->dev = dev;
	priv->netdev = ndev;

	of_id = of_match_device(hix5hd2_of_match, dev);
	if (!of_id) {
		ret = -EINVAL;
		goto out_free_netdev;
	}
	priv->hw_cap = (unsigned long)of_id->data;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto out_free_netdev;
	}

	priv->ctrl_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->ctrl_base)) {
		ret = PTR_ERR(priv->ctrl_base);
		goto out_free_netdev;
	}

	priv->mac_core_clk = devm_clk_get(&pdev->dev, "mac_core");
	if (IS_ERR(priv->mac_core_clk)) {
		netdev_err(ndev, "failed to get mac core clk\n");
		ret = -ENODEV;
		goto out_free_netdev;
	}

	ret = clk_prepare_enable(priv->mac_core_clk);
	if (ret < 0) {
		netdev_err(ndev, "failed to enable mac core clk %d\n", ret);
		goto out_free_netdev;
	}

	priv->mac_ifc_clk = devm_clk_get(&pdev->dev, "mac_ifc");
	if (IS_ERR(priv->mac_ifc_clk))
		priv->mac_ifc_clk = NULL;

	ret = clk_prepare_enable(priv->mac_ifc_clk);
	if (ret < 0) {
		netdev_err(ndev, "failed to enable mac ifc clk %d\n", ret);
		goto out_disable_mac_core_clk;
	}

	priv->mac_core_rst = devm_reset_control_get(dev, "mac_core");
	if (IS_ERR(priv->mac_core_rst))
		priv->mac_core_rst = NULL;
	hix5hd2_mac_core_reset(priv);

	priv->mac_ifc_rst = devm_reset_control_get(dev, "mac_ifc");
	if (IS_ERR(priv->mac_ifc_rst))
		priv->mac_ifc_rst = NULL;

	priv->phy_rst = devm_reset_control_get(dev, "phy");
	if (IS_ERR(priv->phy_rst)) {
		priv->phy_rst = NULL;
	} else {
		ret = of_property_read_u32_array(node,
						 PHY_RESET_DELAYS_PROPERTY,
						 priv->phy_reset_delays,
						 DELAYS_NUM);
		if (ret)
			goto out_disable_clk;
		hix5hd2_phy_reset(priv);
	}

	bus = mdiobus_alloc();
	if (bus == NULL) {
		ret = -ENOMEM;
		goto out_disable_clk;
	}

	bus->priv = priv;
	bus->name = "hix5hd2_mii_bus";
	bus->read = hix5hd2_mdio_read;
	bus->write = hix5hd2_mdio_write;
	bus->parent = &pdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(&pdev->dev));
	priv->bus = bus;

	ret = of_mdiobus_register(bus, node);
	if (ret)
		goto err_free_mdio;

	ret = of_get_phy_mode(node, &priv->phy_mode);
	if (ret) {
		netdev_err(ndev, "not find phy-mode\n");
		goto err_mdiobus;
	}

	priv->phy_node = of_parse_phandle(node, "phy-handle", 0);
	if (!priv->phy_node) {
		netdev_err(ndev, "not find phy-handle\n");
		ret = -EINVAL;
		goto err_mdiobus;
	}

	ndev->irq = platform_get_irq(pdev, 0);
	if (ndev->irq <= 0) {
		netdev_err(ndev, "No irq resource\n");
		ret = -EINVAL;
		goto out_phy_node;
	}

	ret = devm_request_irq(dev, ndev->irq, hix5hd2_interrupt,
			       0, pdev->name, ndev);
	if (ret) {
		netdev_err(ndev, "devm_request_irq failed\n");
		goto out_phy_node;
	}

	ret = of_get_ethdev_address(node, ndev);
	if (ret) {
		eth_hw_addr_random(ndev);
		netdev_warn(ndev, "using random MAC address %pM\n",
			    ndev->dev_addr);
	}

	INIT_WORK(&priv->tx_timeout_task, hix5hd2_tx_timeout_task);
	ndev->watchdog_timeo = 6 * HZ;
	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->netdev_ops = &hix5hd2_netdev_ops;
	ndev->ethtool_ops = &hix5hd2_ethtools_ops;
	SET_NETDEV_DEV(ndev, dev);

	if (HAS_CAP_TSO(priv->hw_cap))
		ndev->hw_features |= NETIF_F_SG;

	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;
	ndev->vlan_features |= ndev->features;

	ret = hix5hd2_init_hw_desc_queue(priv);
	if (ret)
		goto out_phy_node;

	netif_napi_add(ndev, &priv->napi, hix5hd2_poll);

	if (HAS_CAP_TSO(priv->hw_cap)) {
		ret = hix5hd2_init_sg_desc_queue(priv);
		if (ret)
			goto out_destroy_queue;
	}

	ret = register_netdev(priv->netdev);
	if (ret) {
		netdev_err(ndev, "register_netdev failed!");
		goto out_destroy_queue;
	}

	clk_disable_unprepare(priv->mac_ifc_clk);
	clk_disable_unprepare(priv->mac_core_clk);

	return ret;

out_destroy_queue:
	if (HAS_CAP_TSO(priv->hw_cap))
		hix5hd2_destroy_sg_desc_queue(priv);
	netif_napi_del(&priv->napi);
	hix5hd2_destroy_hw_desc_queue(priv);
out_phy_node:
	of_node_put(priv->phy_node);
err_mdiobus:
	mdiobus_unregister(bus);
err_free_mdio:
	mdiobus_free(bus);
out_disable_clk:
	clk_disable_unprepare(priv->mac_ifc_clk);
out_disable_mac_core_clk:
	clk_disable_unprepare(priv->mac_core_clk);
out_free_netdev:
	free_netdev(ndev);

	return ret;
}

static int hix5hd2_dev_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct hix5hd2_priv *priv = netdev_priv(ndev);

	netif_napi_del(&priv->napi);
	unregister_netdev(ndev);
	mdiobus_unregister(priv->bus);
	mdiobus_free(priv->bus);

	if (HAS_CAP_TSO(priv->hw_cap))
		hix5hd2_destroy_sg_desc_queue(priv);
	hix5hd2_destroy_hw_desc_queue(priv);
	of_node_put(priv->phy_node);
	cancel_work_sync(&priv->tx_timeout_task);
	free_netdev(ndev);

	return 0;
}

static const struct of_device_id hix5hd2_of_match[] = {
	{ .compatible = "hisilicon,hisi-gmac-v1", .data = (void *)GEMAC_V1 },
	{ .compatible = "hisilicon,hisi-gmac-v2", .data = (void *)GEMAC_V2 },
	{ .compatible = "hisilicon,hix5hd2-gmac", .data = (void *)GEMAC_V1 },
	{ .compatible = "hisilicon,hi3798cv200-gmac", .data = (void *)GEMAC_V2 },
	{ .compatible = "hisilicon,hi3516a-gmac", .data = (void *)GEMAC_V2 },
	{},
};

MODULE_DEVICE_TABLE(of, hix5hd2_of_match);

static struct platform_driver hix5hd2_dev_driver = {
	.driver = {
		.name = "hisi-gmac",
		.of_match_table = hix5hd2_of_match,
	},
	.probe = hix5hd2_dev_probe,
	.remove = hix5hd2_dev_remove,
};

module_platform_driver(hix5hd2_dev_driver);

MODULE_DESCRIPTION("HISILICON Gigabit Ethernet MAC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hisi-gmac");
