// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 MediaTek Corporation
 * Copyright (c) 2020 BayLibre SAS
 *
 * Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#define MTK_STAR_DRVNAME			"mtk_star_emac"

#define MTK_STAR_WAIT_TIMEOUT			300
#define MTK_STAR_MAX_FRAME_SIZE			1514
#define MTK_STAR_SKB_ALIGNMENT			16
#define MTK_STAR_NAPI_WEIGHT			64
#define MTK_STAR_HASHTABLE_MC_LIMIT		256
#define MTK_STAR_HASHTABLE_SIZE_MAX		512

/* Normally we'd use NET_IP_ALIGN but on arm64 its value is 0 and it doesn't
 * work for this controller.
 */
#define MTK_STAR_IP_ALIGN			2

static const char *const mtk_star_clk_names[] = { "core", "reg", "trans" };
#define MTK_STAR_NCLKS ARRAY_SIZE(mtk_star_clk_names)

/* PHY Control Register 0 */
#define MTK_STAR_REG_PHY_CTRL0			0x0000
#define MTK_STAR_BIT_PHY_CTRL0_WTCMD		BIT(13)
#define MTK_STAR_BIT_PHY_CTRL0_RDCMD		BIT(14)
#define MTK_STAR_BIT_PHY_CTRL0_RWOK		BIT(15)
#define MTK_STAR_MSK_PHY_CTRL0_PREG		GENMASK(12, 8)
#define MTK_STAR_OFF_PHY_CTRL0_PREG		8
#define MTK_STAR_MSK_PHY_CTRL0_RWDATA		GENMASK(31, 16)
#define MTK_STAR_OFF_PHY_CTRL0_RWDATA		16

/* PHY Control Register 1 */
#define MTK_STAR_REG_PHY_CTRL1			0x0004
#define MTK_STAR_BIT_PHY_CTRL1_LINK_ST		BIT(0)
#define MTK_STAR_BIT_PHY_CTRL1_AN_EN		BIT(8)
#define MTK_STAR_OFF_PHY_CTRL1_FORCE_SPD	9
#define MTK_STAR_VAL_PHY_CTRL1_FORCE_SPD_10M	0x00
#define MTK_STAR_VAL_PHY_CTRL1_FORCE_SPD_100M	0x01
#define MTK_STAR_VAL_PHY_CTRL1_FORCE_SPD_1000M	0x02
#define MTK_STAR_BIT_PHY_CTRL1_FORCE_DPX	BIT(11)
#define MTK_STAR_BIT_PHY_CTRL1_FORCE_FC_RX	BIT(12)
#define MTK_STAR_BIT_PHY_CTRL1_FORCE_FC_TX	BIT(13)

/* MAC Configuration Register */
#define MTK_STAR_REG_MAC_CFG			0x0008
#define MTK_STAR_OFF_MAC_CFG_IPG		10
#define MTK_STAR_VAL_MAC_CFG_IPG_96BIT		GENMASK(4, 0)
#define MTK_STAR_BIT_MAC_CFG_MAXLEN_1522	BIT(16)
#define MTK_STAR_BIT_MAC_CFG_AUTO_PAD		BIT(19)
#define MTK_STAR_BIT_MAC_CFG_CRC_STRIP		BIT(20)
#define MTK_STAR_BIT_MAC_CFG_VLAN_STRIP		BIT(22)
#define MTK_STAR_BIT_MAC_CFG_NIC_PD		BIT(31)

/* Flow-Control Configuration Register */
#define MTK_STAR_REG_FC_CFG			0x000c
#define MTK_STAR_BIT_FC_CFG_BP_EN		BIT(7)
#define MTK_STAR_BIT_FC_CFG_UC_PAUSE_DIR	BIT(8)
#define MTK_STAR_OFF_FC_CFG_SEND_PAUSE_TH	16
#define MTK_STAR_MSK_FC_CFG_SEND_PAUSE_TH	GENMASK(27, 16)
#define MTK_STAR_VAL_FC_CFG_SEND_PAUSE_TH_2K	0x800

/* ARL Configuration Register */
#define MTK_STAR_REG_ARL_CFG			0x0010
#define MTK_STAR_BIT_ARL_CFG_HASH_ALG		BIT(0)
#define MTK_STAR_BIT_ARL_CFG_MISC_MODE		BIT(4)

/* MAC High and Low Bytes Registers */
#define MTK_STAR_REG_MY_MAC_H			0x0014
#define MTK_STAR_REG_MY_MAC_L			0x0018

/* Hash Table Control Register */
#define MTK_STAR_REG_HASH_CTRL			0x001c
#define MTK_STAR_MSK_HASH_CTRL_HASH_BIT_ADDR	GENMASK(8, 0)
#define MTK_STAR_BIT_HASH_CTRL_HASH_BIT_DATA	BIT(12)
#define MTK_STAR_BIT_HASH_CTRL_ACC_CMD		BIT(13)
#define MTK_STAR_BIT_HASH_CTRL_CMD_START	BIT(14)
#define MTK_STAR_BIT_HASH_CTRL_BIST_OK		BIT(16)
#define MTK_STAR_BIT_HASH_CTRL_BIST_DONE	BIT(17)
#define MTK_STAR_BIT_HASH_CTRL_BIST_EN		BIT(31)

/* TX DMA Control Register */
#define MTK_STAR_REG_TX_DMA_CTRL		0x0034
#define MTK_STAR_BIT_TX_DMA_CTRL_START		BIT(0)
#define MTK_STAR_BIT_TX_DMA_CTRL_STOP		BIT(1)
#define MTK_STAR_BIT_TX_DMA_CTRL_RESUME		BIT(2)

/* RX DMA Control Register */
#define MTK_STAR_REG_RX_DMA_CTRL		0x0038
#define MTK_STAR_BIT_RX_DMA_CTRL_START		BIT(0)
#define MTK_STAR_BIT_RX_DMA_CTRL_STOP		BIT(1)
#define MTK_STAR_BIT_RX_DMA_CTRL_RESUME		BIT(2)

/* DMA Address Registers */
#define MTK_STAR_REG_TX_DPTR			0x003c
#define MTK_STAR_REG_RX_DPTR			0x0040
#define MTK_STAR_REG_TX_BASE_ADDR		0x0044
#define MTK_STAR_REG_RX_BASE_ADDR		0x0048

/* Interrupt Status Register */
#define MTK_STAR_REG_INT_STS			0x0050
#define MTK_STAR_REG_INT_STS_PORT_STS_CHG	BIT(2)
#define MTK_STAR_REG_INT_STS_MIB_CNT_TH		BIT(3)
#define MTK_STAR_BIT_INT_STS_FNRC		BIT(6)
#define MTK_STAR_BIT_INT_STS_TNTC		BIT(8)

/* Interrupt Mask Register */
#define MTK_STAR_REG_INT_MASK			0x0054
#define MTK_STAR_BIT_INT_MASK_FNRC		BIT(6)

/* Misc. Config Register */
#define MTK_STAR_REG_TEST1			0x005c
#define MTK_STAR_BIT_TEST1_RST_HASH_MBIST	BIT(31)

/* Extended Configuration Register */
#define MTK_STAR_REG_EXT_CFG			0x0060
#define MTK_STAR_OFF_EXT_CFG_SND_PAUSE_RLS	16
#define MTK_STAR_MSK_EXT_CFG_SND_PAUSE_RLS	GENMASK(26, 16)
#define MTK_STAR_VAL_EXT_CFG_SND_PAUSE_RLS_1K	0x400

/* EthSys Configuration Register */
#define MTK_STAR_REG_SYS_CONF			0x0094
#define MTK_STAR_BIT_MII_PAD_OUT_ENABLE		BIT(0)
#define MTK_STAR_BIT_EXT_MDC_MODE		BIT(1)
#define MTK_STAR_BIT_SWC_MII_MODE		BIT(2)

/* MAC Clock Configuration Register */
#define MTK_STAR_REG_MAC_CLK_CONF		0x00ac
#define MTK_STAR_MSK_MAC_CLK_CONF		GENMASK(7, 0)
#define MTK_STAR_BIT_CLK_DIV_10			0x0a

/* Counter registers. */
#define MTK_STAR_REG_C_RXOKPKT			0x0100
#define MTK_STAR_REG_C_RXOKBYTE			0x0104
#define MTK_STAR_REG_C_RXRUNT			0x0108
#define MTK_STAR_REG_C_RXLONG			0x010c
#define MTK_STAR_REG_C_RXDROP			0x0110
#define MTK_STAR_REG_C_RXCRC			0x0114
#define MTK_STAR_REG_C_RXARLDROP		0x0118
#define MTK_STAR_REG_C_RXVLANDROP		0x011c
#define MTK_STAR_REG_C_RXCSERR			0x0120
#define MTK_STAR_REG_C_RXPAUSE			0x0124
#define MTK_STAR_REG_C_TXOKPKT			0x0128
#define MTK_STAR_REG_C_TXOKBYTE			0x012c
#define MTK_STAR_REG_C_TXPAUSECOL		0x0130
#define MTK_STAR_REG_C_TXRTY			0x0134
#define MTK_STAR_REG_C_TXSKIP			0x0138
#define MTK_STAR_REG_C_TX_ARP			0x013c
#define MTK_STAR_REG_C_RX_RERR			0x01d8
#define MTK_STAR_REG_C_RX_UNI			0x01dc
#define MTK_STAR_REG_C_RX_MULTI			0x01e0
#define MTK_STAR_REG_C_RX_BROAD			0x01e4
#define MTK_STAR_REG_C_RX_ALIGNERR		0x01e8
#define MTK_STAR_REG_C_TX_UNI			0x01ec
#define MTK_STAR_REG_C_TX_MULTI			0x01f0
#define MTK_STAR_REG_C_TX_BROAD			0x01f4
#define MTK_STAR_REG_C_TX_TIMEOUT		0x01f8
#define MTK_STAR_REG_C_TX_LATECOL		0x01fc
#define MTK_STAR_REG_C_RX_LENGTHERR		0x0214
#define MTK_STAR_REG_C_RX_TWIST			0x0218

/* Ethernet CFG Control */
#define MTK_PERICFG_REG_NIC_CFG_CON		0x03c4
#define MTK_PERICFG_MSK_NIC_CFG_CON_CFG_MII	GENMASK(3, 0)
#define MTK_PERICFG_BIT_NIC_CFG_CON_RMII	BIT(0)

/* Represents the actual structure of descriptors used by the MAC. We can
 * reuse the same structure for both TX and RX - the layout is the same, only
 * the flags differ slightly.
 */
struct mtk_star_ring_desc {
	/* Contains both the status flags as well as packet length. */
	u32 status;
	u32 data_ptr;
	u32 vtag;
	u32 reserved;
};

#define MTK_STAR_DESC_MSK_LEN			GENMASK(15, 0)
#define MTK_STAR_DESC_BIT_RX_CRCE		BIT(24)
#define MTK_STAR_DESC_BIT_RX_OSIZE		BIT(25)
#define MTK_STAR_DESC_BIT_INT			BIT(27)
#define MTK_STAR_DESC_BIT_LS			BIT(28)
#define MTK_STAR_DESC_BIT_FS			BIT(29)
#define MTK_STAR_DESC_BIT_EOR			BIT(30)
#define MTK_STAR_DESC_BIT_COWN			BIT(31)

/* Helper structure for storing data read from/written to descriptors in order
 * to limit reads from/writes to DMA memory.
 */
struct mtk_star_ring_desc_data {
	unsigned int len;
	unsigned int flags;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
};

#define MTK_STAR_RING_NUM_DESCS			128
#define MTK_STAR_NUM_TX_DESCS			MTK_STAR_RING_NUM_DESCS
#define MTK_STAR_NUM_RX_DESCS			MTK_STAR_RING_NUM_DESCS
#define MTK_STAR_NUM_DESCS_TOTAL		(MTK_STAR_RING_NUM_DESCS * 2)
#define MTK_STAR_DMA_SIZE \
		(MTK_STAR_NUM_DESCS_TOTAL * sizeof(struct mtk_star_ring_desc))

struct mtk_star_ring {
	struct mtk_star_ring_desc *descs;
	struct sk_buff *skbs[MTK_STAR_RING_NUM_DESCS];
	dma_addr_t dma_addrs[MTK_STAR_RING_NUM_DESCS];
	unsigned int head;
	unsigned int tail;
};

struct mtk_star_priv {
	struct net_device *ndev;

	struct regmap *regs;
	struct regmap *pericfg;

	struct clk_bulk_data clks[MTK_STAR_NCLKS];

	void *ring_base;
	struct mtk_star_ring_desc *descs_base;
	dma_addr_t dma_addr;
	struct mtk_star_ring tx_ring;
	struct mtk_star_ring rx_ring;

	struct mii_bus *mii;
	struct napi_struct napi;

	struct device_node *phy_node;
	phy_interface_t phy_intf;
	struct phy_device *phydev;
	unsigned int link;
	int speed;
	int duplex;
	int pause;

	/* Protects against concurrent descriptor access. */
	spinlock_t lock;

	struct rtnl_link_stats64 stats;
};

static struct device *mtk_star_get_dev(struct mtk_star_priv *priv)
{
	return priv->ndev->dev.parent;
}

static const struct regmap_config mtk_star_regmap_config = {
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_stride		= 4,
	.disable_locking	= true,
};

static void mtk_star_ring_init(struct mtk_star_ring *ring,
			       struct mtk_star_ring_desc *descs)
{
	memset(ring, 0, sizeof(*ring));
	ring->descs = descs;
	ring->head = 0;
	ring->tail = 0;
}

static int mtk_star_ring_pop_tail(struct mtk_star_ring *ring,
				  struct mtk_star_ring_desc_data *desc_data)
{
	struct mtk_star_ring_desc *desc = &ring->descs[ring->tail];
	unsigned int status;

	status = READ_ONCE(desc->status);
	dma_rmb(); /* Make sure we read the status bits before checking it. */

	if (!(status & MTK_STAR_DESC_BIT_COWN))
		return -1;

	desc_data->len = status & MTK_STAR_DESC_MSK_LEN;
	desc_data->flags = status & ~MTK_STAR_DESC_MSK_LEN;
	desc_data->dma_addr = ring->dma_addrs[ring->tail];
	desc_data->skb = ring->skbs[ring->tail];

	ring->dma_addrs[ring->tail] = 0;
	ring->skbs[ring->tail] = NULL;

	status &= MTK_STAR_DESC_BIT_COWN | MTK_STAR_DESC_BIT_EOR;

	WRITE_ONCE(desc->data_ptr, 0);
	WRITE_ONCE(desc->status, status);

	ring->tail = (ring->tail + 1) % MTK_STAR_RING_NUM_DESCS;

	return 0;
}

static void mtk_star_ring_push_head(struct mtk_star_ring *ring,
				    struct mtk_star_ring_desc_data *desc_data,
				    unsigned int flags)
{
	struct mtk_star_ring_desc *desc = &ring->descs[ring->head];
	unsigned int status;

	status = READ_ONCE(desc->status);

	ring->skbs[ring->head] = desc_data->skb;
	ring->dma_addrs[ring->head] = desc_data->dma_addr;

	status |= desc_data->len;
	if (flags)
		status |= flags;

	WRITE_ONCE(desc->data_ptr, desc_data->dma_addr);
	WRITE_ONCE(desc->status, status);
	status &= ~MTK_STAR_DESC_BIT_COWN;
	/* Flush previous modifications before ownership change. */
	dma_wmb();
	WRITE_ONCE(desc->status, status);

	ring->head = (ring->head + 1) % MTK_STAR_RING_NUM_DESCS;
}

static void
mtk_star_ring_push_head_rx(struct mtk_star_ring *ring,
			   struct mtk_star_ring_desc_data *desc_data)
{
	mtk_star_ring_push_head(ring, desc_data, 0);
}

static void
mtk_star_ring_push_head_tx(struct mtk_star_ring *ring,
			   struct mtk_star_ring_desc_data *desc_data)
{
	static const unsigned int flags = MTK_STAR_DESC_BIT_FS |
					  MTK_STAR_DESC_BIT_LS |
					  MTK_STAR_DESC_BIT_INT;

	mtk_star_ring_push_head(ring, desc_data, flags);
}

static unsigned int mtk_star_ring_num_used_descs(struct mtk_star_ring *ring)
{
	return abs(ring->head - ring->tail);
}

static bool mtk_star_ring_full(struct mtk_star_ring *ring)
{
	return mtk_star_ring_num_used_descs(ring) == MTK_STAR_RING_NUM_DESCS;
}

static bool mtk_star_ring_descs_available(struct mtk_star_ring *ring)
{
	return mtk_star_ring_num_used_descs(ring) > 0;
}

static dma_addr_t mtk_star_dma_map_rx(struct mtk_star_priv *priv,
				      struct sk_buff *skb)
{
	struct device *dev = mtk_star_get_dev(priv);

	/* Data pointer for the RX DMA descriptor must be aligned to 4N + 2. */
	return dma_map_single(dev, skb_tail_pointer(skb) - 2,
			      skb_tailroom(skb), DMA_FROM_DEVICE);
}

static void mtk_star_dma_unmap_rx(struct mtk_star_priv *priv,
				  struct mtk_star_ring_desc_data *desc_data)
{
	struct device *dev = mtk_star_get_dev(priv);

	dma_unmap_single(dev, desc_data->dma_addr,
			 skb_tailroom(desc_data->skb), DMA_FROM_DEVICE);
}

static dma_addr_t mtk_star_dma_map_tx(struct mtk_star_priv *priv,
				      struct sk_buff *skb)
{
	struct device *dev = mtk_star_get_dev(priv);

	return dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
}

static void mtk_star_dma_unmap_tx(struct mtk_star_priv *priv,
				  struct mtk_star_ring_desc_data *desc_data)
{
	struct device *dev = mtk_star_get_dev(priv);

	return dma_unmap_single(dev, desc_data->dma_addr,
				skb_headlen(desc_data->skb), DMA_TO_DEVICE);
}

static void mtk_star_nic_disable_pd(struct mtk_star_priv *priv)
{
	regmap_clear_bits(priv->regs, MTK_STAR_REG_MAC_CFG,
			  MTK_STAR_BIT_MAC_CFG_NIC_PD);
}

/* Unmask the three interrupts we care about, mask all others. */
static void mtk_star_intr_enable(struct mtk_star_priv *priv)
{
	unsigned int val = MTK_STAR_BIT_INT_STS_TNTC |
			   MTK_STAR_BIT_INT_STS_FNRC |
			   MTK_STAR_REG_INT_STS_MIB_CNT_TH;

	regmap_write(priv->regs, MTK_STAR_REG_INT_MASK, ~val);
}

static void mtk_star_intr_disable(struct mtk_star_priv *priv)
{
	regmap_write(priv->regs, MTK_STAR_REG_INT_MASK, ~0);
}

static unsigned int mtk_star_intr_read(struct mtk_star_priv *priv)
{
	unsigned int val;

	regmap_read(priv->regs, MTK_STAR_REG_INT_STS, &val);

	return val;
}

static unsigned int mtk_star_intr_ack_all(struct mtk_star_priv *priv)
{
	unsigned int val;

	val = mtk_star_intr_read(priv);
	regmap_write(priv->regs, MTK_STAR_REG_INT_STS, val);

	return val;
}

static void mtk_star_dma_init(struct mtk_star_priv *priv)
{
	struct mtk_star_ring_desc *desc;
	unsigned int val;
	int i;

	priv->descs_base = (struct mtk_star_ring_desc *)priv->ring_base;

	for (i = 0; i < MTK_STAR_NUM_DESCS_TOTAL; i++) {
		desc = &priv->descs_base[i];

		memset(desc, 0, sizeof(*desc));
		desc->status = MTK_STAR_DESC_BIT_COWN;
		if ((i == MTK_STAR_NUM_TX_DESCS - 1) ||
		    (i == MTK_STAR_NUM_DESCS_TOTAL - 1))
			desc->status |= MTK_STAR_DESC_BIT_EOR;
	}

	mtk_star_ring_init(&priv->tx_ring, priv->descs_base);
	mtk_star_ring_init(&priv->rx_ring,
			   priv->descs_base + MTK_STAR_NUM_TX_DESCS);

	/* Set DMA pointers. */
	val = (unsigned int)priv->dma_addr;
	regmap_write(priv->regs, MTK_STAR_REG_TX_BASE_ADDR, val);
	regmap_write(priv->regs, MTK_STAR_REG_TX_DPTR, val);

	val += sizeof(struct mtk_star_ring_desc) * MTK_STAR_NUM_TX_DESCS;
	regmap_write(priv->regs, MTK_STAR_REG_RX_BASE_ADDR, val);
	regmap_write(priv->regs, MTK_STAR_REG_RX_DPTR, val);
}

static void mtk_star_dma_start(struct mtk_star_priv *priv)
{
	regmap_set_bits(priv->regs, MTK_STAR_REG_TX_DMA_CTRL,
			MTK_STAR_BIT_TX_DMA_CTRL_START);
	regmap_set_bits(priv->regs, MTK_STAR_REG_RX_DMA_CTRL,
			MTK_STAR_BIT_RX_DMA_CTRL_START);
}

static void mtk_star_dma_stop(struct mtk_star_priv *priv)
{
	regmap_write(priv->regs, MTK_STAR_REG_TX_DMA_CTRL,
		     MTK_STAR_BIT_TX_DMA_CTRL_STOP);
	regmap_write(priv->regs, MTK_STAR_REG_RX_DMA_CTRL,
		     MTK_STAR_BIT_RX_DMA_CTRL_STOP);
}

static void mtk_star_dma_disable(struct mtk_star_priv *priv)
{
	int i;

	mtk_star_dma_stop(priv);

	/* Take back all descriptors. */
	for (i = 0; i < MTK_STAR_NUM_DESCS_TOTAL; i++)
		priv->descs_base[i].status |= MTK_STAR_DESC_BIT_COWN;
}

static void mtk_star_dma_resume_rx(struct mtk_star_priv *priv)
{
	regmap_set_bits(priv->regs, MTK_STAR_REG_RX_DMA_CTRL,
			MTK_STAR_BIT_RX_DMA_CTRL_RESUME);
}

static void mtk_star_dma_resume_tx(struct mtk_star_priv *priv)
{
	regmap_set_bits(priv->regs, MTK_STAR_REG_TX_DMA_CTRL,
			MTK_STAR_BIT_TX_DMA_CTRL_RESUME);
}

static void mtk_star_set_mac_addr(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	const u8 *mac_addr = ndev->dev_addr;
	unsigned int high, low;

	high = mac_addr[0] << 8 | mac_addr[1] << 0;
	low = mac_addr[2] << 24 | mac_addr[3] << 16 |
	      mac_addr[4] << 8 | mac_addr[5];

	regmap_write(priv->regs, MTK_STAR_REG_MY_MAC_H, high);
	regmap_write(priv->regs, MTK_STAR_REG_MY_MAC_L, low);
}

static void mtk_star_reset_counters(struct mtk_star_priv *priv)
{
	static const unsigned int counter_regs[] = {
		MTK_STAR_REG_C_RXOKPKT,
		MTK_STAR_REG_C_RXOKBYTE,
		MTK_STAR_REG_C_RXRUNT,
		MTK_STAR_REG_C_RXLONG,
		MTK_STAR_REG_C_RXDROP,
		MTK_STAR_REG_C_RXCRC,
		MTK_STAR_REG_C_RXARLDROP,
		MTK_STAR_REG_C_RXVLANDROP,
		MTK_STAR_REG_C_RXCSERR,
		MTK_STAR_REG_C_RXPAUSE,
		MTK_STAR_REG_C_TXOKPKT,
		MTK_STAR_REG_C_TXOKBYTE,
		MTK_STAR_REG_C_TXPAUSECOL,
		MTK_STAR_REG_C_TXRTY,
		MTK_STAR_REG_C_TXSKIP,
		MTK_STAR_REG_C_TX_ARP,
		MTK_STAR_REG_C_RX_RERR,
		MTK_STAR_REG_C_RX_UNI,
		MTK_STAR_REG_C_RX_MULTI,
		MTK_STAR_REG_C_RX_BROAD,
		MTK_STAR_REG_C_RX_ALIGNERR,
		MTK_STAR_REG_C_TX_UNI,
		MTK_STAR_REG_C_TX_MULTI,
		MTK_STAR_REG_C_TX_BROAD,
		MTK_STAR_REG_C_TX_TIMEOUT,
		MTK_STAR_REG_C_TX_LATECOL,
		MTK_STAR_REG_C_RX_LENGTHERR,
		MTK_STAR_REG_C_RX_TWIST,
	};

	unsigned int i, val;

	for (i = 0; i < ARRAY_SIZE(counter_regs); i++)
		regmap_read(priv->regs, counter_regs[i], &val);
}

static void mtk_star_update_stat(struct mtk_star_priv *priv,
				 unsigned int reg, u64 *stat)
{
	unsigned int val;

	regmap_read(priv->regs, reg, &val);
	*stat += val;
}

/* Try to get as many stats as possible from the internal registers instead
 * of tracking them ourselves.
 */
static void mtk_star_update_stats(struct mtk_star_priv *priv)
{
	struct rtnl_link_stats64 *stats = &priv->stats;

	/* OK packets and bytes. */
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RXOKPKT, &stats->rx_packets);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_TXOKPKT, &stats->tx_packets);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RXOKBYTE, &stats->rx_bytes);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_TXOKBYTE, &stats->tx_bytes);

	/* RX & TX multicast. */
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RX_MULTI, &stats->multicast);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_TX_MULTI, &stats->multicast);

	/* Collisions. */
	mtk_star_update_stat(priv, MTK_STAR_REG_C_TXPAUSECOL,
			     &stats->collisions);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_TX_LATECOL,
			     &stats->collisions);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RXRUNT, &stats->collisions);

	/* RX Errors. */
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RX_LENGTHERR,
			     &stats->rx_length_errors);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RXLONG,
			     &stats->rx_over_errors);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RXCRC, &stats->rx_crc_errors);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RX_ALIGNERR,
			     &stats->rx_frame_errors);
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RXDROP,
			     &stats->rx_fifo_errors);
	/* Sum of the general RX error counter + all of the above. */
	mtk_star_update_stat(priv, MTK_STAR_REG_C_RX_RERR, &stats->rx_errors);
	stats->rx_errors += stats->rx_length_errors;
	stats->rx_errors += stats->rx_over_errors;
	stats->rx_errors += stats->rx_crc_errors;
	stats->rx_errors += stats->rx_frame_errors;
	stats->rx_errors += stats->rx_fifo_errors;
}

static struct sk_buff *mtk_star_alloc_skb(struct net_device *ndev)
{
	uintptr_t tail, offset;
	struct sk_buff *skb;

	skb = dev_alloc_skb(MTK_STAR_MAX_FRAME_SIZE);
	if (!skb)
		return NULL;

	/* Align to 16 bytes. */
	tail = (uintptr_t)skb_tail_pointer(skb);
	if (tail & (MTK_STAR_SKB_ALIGNMENT - 1)) {
		offset = tail & (MTK_STAR_SKB_ALIGNMENT - 1);
		skb_reserve(skb, MTK_STAR_SKB_ALIGNMENT - offset);
	}

	/* Ensure 16-byte alignment of the skb pointer: eth_type_trans() will
	 * extract the Ethernet header (14 bytes) so we need two more bytes.
	 */
	skb_reserve(skb, MTK_STAR_IP_ALIGN);

	return skb;
}

static int mtk_star_prepare_rx_skbs(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	struct mtk_star_ring *ring = &priv->rx_ring;
	struct device *dev = mtk_star_get_dev(priv);
	struct mtk_star_ring_desc *desc;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	int i;

	for (i = 0; i < MTK_STAR_NUM_RX_DESCS; i++) {
		skb = mtk_star_alloc_skb(ndev);
		if (!skb)
			return -ENOMEM;

		dma_addr = mtk_star_dma_map_rx(priv, skb);
		if (dma_mapping_error(dev, dma_addr)) {
			dev_kfree_skb(skb);
			return -ENOMEM;
		}

		desc = &ring->descs[i];
		desc->data_ptr = dma_addr;
		desc->status |= skb_tailroom(skb) & MTK_STAR_DESC_MSK_LEN;
		desc->status &= ~MTK_STAR_DESC_BIT_COWN;
		ring->skbs[i] = skb;
		ring->dma_addrs[i] = dma_addr;
	}

	return 0;
}

static void
mtk_star_ring_free_skbs(struct mtk_star_priv *priv, struct mtk_star_ring *ring,
			void (*unmap_func)(struct mtk_star_priv *,
					   struct mtk_star_ring_desc_data *))
{
	struct mtk_star_ring_desc_data desc_data;
	int i;

	for (i = 0; i < MTK_STAR_RING_NUM_DESCS; i++) {
		if (!ring->dma_addrs[i])
			continue;

		desc_data.dma_addr = ring->dma_addrs[i];
		desc_data.skb = ring->skbs[i];

		unmap_func(priv, &desc_data);
		dev_kfree_skb(desc_data.skb);
	}
}

static void mtk_star_free_rx_skbs(struct mtk_star_priv *priv)
{
	struct mtk_star_ring *ring = &priv->rx_ring;

	mtk_star_ring_free_skbs(priv, ring, mtk_star_dma_unmap_rx);
}

static void mtk_star_free_tx_skbs(struct mtk_star_priv *priv)
{
	struct mtk_star_ring *ring = &priv->tx_ring;

	mtk_star_ring_free_skbs(priv, ring, mtk_star_dma_unmap_tx);
}

/* All processing for TX and RX happens in the napi poll callback.
 *
 * FIXME: The interrupt handling should be more fine-grained with each
 * interrupt enabled/disabled independently when needed. Unfortunatly this
 * turned out to impact the driver's stability and until we have something
 * working properly, we're disabling all interrupts during TX & RX processing
 * or when resetting the counter registers.
 */
static irqreturn_t mtk_star_handle_irq(int irq, void *data)
{
	struct mtk_star_priv *priv;
	struct net_device *ndev;

	ndev = data;
	priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		mtk_star_intr_disable(priv);
		napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

/* Wait for the completion of any previous command - CMD_START bit must be
 * cleared by hardware.
 */
static int mtk_star_hash_wait_cmd_start(struct mtk_star_priv *priv)
{
	unsigned int val;

	return regmap_read_poll_timeout_atomic(priv->regs,
				MTK_STAR_REG_HASH_CTRL, val,
				!(val & MTK_STAR_BIT_HASH_CTRL_CMD_START),
				10, MTK_STAR_WAIT_TIMEOUT);
}

static int mtk_star_hash_wait_ok(struct mtk_star_priv *priv)
{
	unsigned int val;
	int ret;

	/* Wait for BIST_DONE bit. */
	ret = regmap_read_poll_timeout_atomic(priv->regs,
					MTK_STAR_REG_HASH_CTRL, val,
					val & MTK_STAR_BIT_HASH_CTRL_BIST_DONE,
					10, MTK_STAR_WAIT_TIMEOUT);
	if (ret)
		return ret;

	/* Check the BIST_OK bit. */
	if (!regmap_test_bits(priv->regs, MTK_STAR_REG_HASH_CTRL,
			      MTK_STAR_BIT_HASH_CTRL_BIST_OK))
		return -EIO;

	return 0;
}

static int mtk_star_set_hashbit(struct mtk_star_priv *priv,
				unsigned int hash_addr)
{
	unsigned int val;
	int ret;

	ret = mtk_star_hash_wait_cmd_start(priv);
	if (ret)
		return ret;

	val = hash_addr & MTK_STAR_MSK_HASH_CTRL_HASH_BIT_ADDR;
	val |= MTK_STAR_BIT_HASH_CTRL_ACC_CMD;
	val |= MTK_STAR_BIT_HASH_CTRL_CMD_START;
	val |= MTK_STAR_BIT_HASH_CTRL_BIST_EN;
	val |= MTK_STAR_BIT_HASH_CTRL_HASH_BIT_DATA;
	regmap_write(priv->regs, MTK_STAR_REG_HASH_CTRL, val);

	return mtk_star_hash_wait_ok(priv);
}

static int mtk_star_reset_hash_table(struct mtk_star_priv *priv)
{
	int ret;

	ret = mtk_star_hash_wait_cmd_start(priv);
	if (ret)
		return ret;

	regmap_set_bits(priv->regs, MTK_STAR_REG_HASH_CTRL,
			MTK_STAR_BIT_HASH_CTRL_BIST_EN);
	regmap_set_bits(priv->regs, MTK_STAR_REG_TEST1,
			MTK_STAR_BIT_TEST1_RST_HASH_MBIST);

	return mtk_star_hash_wait_ok(priv);
}

static void mtk_star_phy_config(struct mtk_star_priv *priv)
{
	unsigned int val;

	if (priv->speed == SPEED_1000)
		val = MTK_STAR_VAL_PHY_CTRL1_FORCE_SPD_1000M;
	else if (priv->speed == SPEED_100)
		val = MTK_STAR_VAL_PHY_CTRL1_FORCE_SPD_100M;
	else
		val = MTK_STAR_VAL_PHY_CTRL1_FORCE_SPD_10M;
	val <<= MTK_STAR_OFF_PHY_CTRL1_FORCE_SPD;

	val |= MTK_STAR_BIT_PHY_CTRL1_AN_EN;
	val |= MTK_STAR_BIT_PHY_CTRL1_FORCE_FC_RX;
	val |= MTK_STAR_BIT_PHY_CTRL1_FORCE_FC_TX;
	/* Only full-duplex supported for now. */
	val |= MTK_STAR_BIT_PHY_CTRL1_FORCE_DPX;

	regmap_write(priv->regs, MTK_STAR_REG_PHY_CTRL1, val);

	if (priv->pause) {
		val = MTK_STAR_VAL_FC_CFG_SEND_PAUSE_TH_2K;
		val <<= MTK_STAR_OFF_FC_CFG_SEND_PAUSE_TH;
		val |= MTK_STAR_BIT_FC_CFG_UC_PAUSE_DIR;
	} else {
		val = 0;
	}

	regmap_update_bits(priv->regs, MTK_STAR_REG_FC_CFG,
			   MTK_STAR_MSK_FC_CFG_SEND_PAUSE_TH |
			   MTK_STAR_BIT_FC_CFG_UC_PAUSE_DIR, val);

	if (priv->pause) {
		val = MTK_STAR_VAL_EXT_CFG_SND_PAUSE_RLS_1K;
		val <<= MTK_STAR_OFF_EXT_CFG_SND_PAUSE_RLS;
	} else {
		val = 0;
	}

	regmap_update_bits(priv->regs, MTK_STAR_REG_EXT_CFG,
			   MTK_STAR_MSK_EXT_CFG_SND_PAUSE_RLS, val);
}

static void mtk_star_adjust_link(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = priv->phydev;
	bool new_state = false;

	if (phydev->link) {
		if (!priv->link) {
			priv->link = phydev->link;
			new_state = true;
		}

		if (priv->speed != phydev->speed) {
			priv->speed = phydev->speed;
			new_state = true;
		}

		if (priv->pause != phydev->pause) {
			priv->pause = phydev->pause;
			new_state = true;
		}
	} else {
		if (priv->link) {
			priv->link = phydev->link;
			new_state = true;
		}
	}

	if (new_state) {
		if (phydev->link)
			mtk_star_phy_config(priv);

		phy_print_status(ndev->phydev);
	}
}

static void mtk_star_init_config(struct mtk_star_priv *priv)
{
	unsigned int val;

	val = (MTK_STAR_BIT_MII_PAD_OUT_ENABLE |
	       MTK_STAR_BIT_EXT_MDC_MODE |
	       MTK_STAR_BIT_SWC_MII_MODE);

	regmap_write(priv->regs, MTK_STAR_REG_SYS_CONF, val);
	regmap_update_bits(priv->regs, MTK_STAR_REG_MAC_CLK_CONF,
			   MTK_STAR_MSK_MAC_CLK_CONF,
			   MTK_STAR_BIT_CLK_DIV_10);
}

static void mtk_star_set_mode_rmii(struct mtk_star_priv *priv)
{
	regmap_update_bits(priv->pericfg, MTK_PERICFG_REG_NIC_CFG_CON,
			   MTK_PERICFG_MSK_NIC_CFG_CON_CFG_MII,
			   MTK_PERICFG_BIT_NIC_CFG_CON_RMII);
}

static int mtk_star_enable(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	unsigned int val;
	int ret;

	mtk_star_nic_disable_pd(priv);
	mtk_star_intr_disable(priv);
	mtk_star_dma_stop(priv);

	mtk_star_set_mac_addr(ndev);

	/* Configure the MAC */
	val = MTK_STAR_VAL_MAC_CFG_IPG_96BIT;
	val <<= MTK_STAR_OFF_MAC_CFG_IPG;
	val |= MTK_STAR_BIT_MAC_CFG_MAXLEN_1522;
	val |= MTK_STAR_BIT_MAC_CFG_AUTO_PAD;
	val |= MTK_STAR_BIT_MAC_CFG_CRC_STRIP;
	regmap_write(priv->regs, MTK_STAR_REG_MAC_CFG, val);

	/* Enable Hash Table BIST and reset it */
	ret = mtk_star_reset_hash_table(priv);
	if (ret)
		return ret;

	/* Setup the hashing algorithm */
	regmap_clear_bits(priv->regs, MTK_STAR_REG_ARL_CFG,
			  MTK_STAR_BIT_ARL_CFG_HASH_ALG |
			  MTK_STAR_BIT_ARL_CFG_MISC_MODE);

	/* Don't strip VLAN tags */
	regmap_clear_bits(priv->regs, MTK_STAR_REG_MAC_CFG,
			  MTK_STAR_BIT_MAC_CFG_VLAN_STRIP);

	/* Setup DMA */
	mtk_star_dma_init(priv);

	ret = mtk_star_prepare_rx_skbs(ndev);
	if (ret)
		goto err_out;

	/* Request the interrupt */
	ret = request_irq(ndev->irq, mtk_star_handle_irq,
			  IRQF_TRIGGER_FALLING, ndev->name, ndev);
	if (ret)
		goto err_free_skbs;

	napi_enable(&priv->napi);

	mtk_star_intr_ack_all(priv);
	mtk_star_intr_enable(priv);

	/* Connect to and start PHY */
	priv->phydev = of_phy_connect(ndev, priv->phy_node,
				      mtk_star_adjust_link, 0, priv->phy_intf);
	if (!priv->phydev) {
		netdev_err(ndev, "failed to connect to PHY\n");
		ret = -ENODEV;
		goto err_free_irq;
	}

	mtk_star_dma_start(priv);
	phy_start(priv->phydev);
	netif_start_queue(ndev);

	return 0;

err_free_irq:
	free_irq(ndev->irq, ndev);
err_free_skbs:
	mtk_star_free_rx_skbs(priv);
err_out:
	return ret;
}

static void mtk_star_disable(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	mtk_star_intr_disable(priv);
	mtk_star_dma_disable(priv);
	mtk_star_intr_ack_all(priv);
	phy_stop(priv->phydev);
	phy_disconnect(priv->phydev);
	free_irq(ndev->irq, ndev);
	mtk_star_free_rx_skbs(priv);
	mtk_star_free_tx_skbs(priv);
}

static int mtk_star_netdev_open(struct net_device *ndev)
{
	return mtk_star_enable(ndev);
}

static int mtk_star_netdev_stop(struct net_device *ndev)
{
	mtk_star_disable(ndev);

	return 0;
}

static int mtk_star_netdev_ioctl(struct net_device *ndev,
				 struct ifreq *req, int cmd)
{
	if (!netif_running(ndev))
		return -EINVAL;

	return phy_mii_ioctl(ndev->phydev, req, cmd);
}

static int mtk_star_netdev_start_xmit(struct sk_buff *skb,
				      struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	struct mtk_star_ring *ring = &priv->tx_ring;
	struct device *dev = mtk_star_get_dev(priv);
	struct mtk_star_ring_desc_data desc_data;

	desc_data.dma_addr = mtk_star_dma_map_tx(priv, skb);
	if (dma_mapping_error(dev, desc_data.dma_addr))
		goto err_drop_packet;

	desc_data.skb = skb;
	desc_data.len = skb->len;

	spin_lock_bh(&priv->lock);

	mtk_star_ring_push_head_tx(ring, &desc_data);

	netdev_sent_queue(ndev, skb->len);

	if (mtk_star_ring_full(ring))
		netif_stop_queue(ndev);

	spin_unlock_bh(&priv->lock);

	mtk_star_dma_resume_tx(priv);

	return NETDEV_TX_OK;

err_drop_packet:
	dev_kfree_skb(skb);
	ndev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

/* Returns the number of bytes sent or a negative number on the first
 * descriptor owned by DMA.
 */
static int mtk_star_tx_complete_one(struct mtk_star_priv *priv)
{
	struct mtk_star_ring *ring = &priv->tx_ring;
	struct mtk_star_ring_desc_data desc_data;
	int ret;

	ret = mtk_star_ring_pop_tail(ring, &desc_data);
	if (ret)
		return ret;

	mtk_star_dma_unmap_tx(priv, &desc_data);
	ret = desc_data.skb->len;
	dev_kfree_skb_irq(desc_data.skb);

	return ret;
}

static void mtk_star_tx_complete_all(struct mtk_star_priv *priv)
{
	struct mtk_star_ring *ring = &priv->tx_ring;
	struct net_device *ndev = priv->ndev;
	int ret, pkts_compl, bytes_compl;
	bool wake = false;

	spin_lock(&priv->lock);

	for (pkts_compl = 0, bytes_compl = 0;;
	     pkts_compl++, bytes_compl += ret, wake = true) {
		if (!mtk_star_ring_descs_available(ring))
			break;

		ret = mtk_star_tx_complete_one(priv);
		if (ret < 0)
			break;
	}

	netdev_completed_queue(ndev, pkts_compl, bytes_compl);

	if (wake && netif_queue_stopped(ndev))
		netif_wake_queue(ndev);

	spin_unlock(&priv->lock);
}

static void mtk_star_netdev_get_stats64(struct net_device *ndev,
					struct rtnl_link_stats64 *stats)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);

	mtk_star_update_stats(priv);

	memcpy(stats, &priv->stats, sizeof(*stats));
}

static void mtk_star_set_rx_mode(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	struct netdev_hw_addr *hw_addr;
	unsigned int hash_addr, i;
	int ret;

	if (ndev->flags & IFF_PROMISC) {
		regmap_set_bits(priv->regs, MTK_STAR_REG_ARL_CFG,
				MTK_STAR_BIT_ARL_CFG_MISC_MODE);
	} else if (netdev_mc_count(ndev) > MTK_STAR_HASHTABLE_MC_LIMIT ||
		   ndev->flags & IFF_ALLMULTI) {
		for (i = 0; i < MTK_STAR_HASHTABLE_SIZE_MAX; i++) {
			ret = mtk_star_set_hashbit(priv, i);
			if (ret)
				goto hash_fail;
		}
	} else {
		/* Clear previous settings. */
		ret = mtk_star_reset_hash_table(priv);
		if (ret)
			goto hash_fail;

		netdev_for_each_mc_addr(hw_addr, ndev) {
			hash_addr = (hw_addr->addr[0] & 0x01) << 8;
			hash_addr += hw_addr->addr[5];
			ret = mtk_star_set_hashbit(priv, hash_addr);
			if (ret)
				goto hash_fail;
		}
	}

	return;

hash_fail:
	if (ret == -ETIMEDOUT)
		netdev_err(ndev, "setting hash bit timed out\n");
	else
		/* Should be -EIO */
		netdev_err(ndev, "unable to set hash bit");
}

static const struct net_device_ops mtk_star_netdev_ops = {
	.ndo_open		= mtk_star_netdev_open,
	.ndo_stop		= mtk_star_netdev_stop,
	.ndo_start_xmit		= mtk_star_netdev_start_xmit,
	.ndo_get_stats64	= mtk_star_netdev_get_stats64,
	.ndo_set_rx_mode	= mtk_star_set_rx_mode,
	.ndo_eth_ioctl		= mtk_star_netdev_ioctl,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void mtk_star_get_drvinfo(struct net_device *dev,
				 struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, MTK_STAR_DRVNAME, sizeof(info->driver));
}

/* TODO Add ethtool stats. */
static const struct ethtool_ops mtk_star_ethtool_ops = {
	.get_drvinfo		= mtk_star_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static int mtk_star_receive_packet(struct mtk_star_priv *priv)
{
	struct mtk_star_ring *ring = &priv->rx_ring;
	struct device *dev = mtk_star_get_dev(priv);
	struct mtk_star_ring_desc_data desc_data;
	struct net_device *ndev = priv->ndev;
	struct sk_buff *curr_skb, *new_skb;
	dma_addr_t new_dma_addr;
	int ret;

	spin_lock(&priv->lock);
	ret = mtk_star_ring_pop_tail(ring, &desc_data);
	spin_unlock(&priv->lock);
	if (ret)
		return -1;

	curr_skb = desc_data.skb;

	if ((desc_data.flags & MTK_STAR_DESC_BIT_RX_CRCE) ||
	    (desc_data.flags & MTK_STAR_DESC_BIT_RX_OSIZE)) {
		/* Error packet -> drop and reuse skb. */
		new_skb = curr_skb;
		goto push_new_skb;
	}

	/* Prepare new skb before receiving the current one. Reuse the current
	 * skb if we fail at any point.
	 */
	new_skb = mtk_star_alloc_skb(ndev);
	if (!new_skb) {
		ndev->stats.rx_dropped++;
		new_skb = curr_skb;
		goto push_new_skb;
	}

	new_dma_addr = mtk_star_dma_map_rx(priv, new_skb);
	if (dma_mapping_error(dev, new_dma_addr)) {
		ndev->stats.rx_dropped++;
		dev_kfree_skb(new_skb);
		new_skb = curr_skb;
		netdev_err(ndev, "DMA mapping error of RX descriptor\n");
		goto push_new_skb;
	}

	/* We can't fail anymore at this point: it's safe to unmap the skb. */
	mtk_star_dma_unmap_rx(priv, &desc_data);

	skb_put(desc_data.skb, desc_data.len);
	desc_data.skb->ip_summed = CHECKSUM_NONE;
	desc_data.skb->protocol = eth_type_trans(desc_data.skb, ndev);
	desc_data.skb->dev = ndev;
	netif_receive_skb(desc_data.skb);

	/* update dma_addr for new skb */
	desc_data.dma_addr = new_dma_addr;

push_new_skb:
	desc_data.len = skb_tailroom(new_skb);
	desc_data.skb = new_skb;

	spin_lock(&priv->lock);
	mtk_star_ring_push_head_rx(ring, &desc_data);
	spin_unlock(&priv->lock);

	return 0;
}

static int mtk_star_process_rx(struct mtk_star_priv *priv, int budget)
{
	int received, ret;

	for (received = 0, ret = 0; received < budget && ret == 0; received++)
		ret = mtk_star_receive_packet(priv);

	mtk_star_dma_resume_rx(priv);

	return received;
}

static int mtk_star_poll(struct napi_struct *napi, int budget)
{
	struct mtk_star_priv *priv;
	unsigned int status;
	int received = 0;

	priv = container_of(napi, struct mtk_star_priv, napi);

	status = mtk_star_intr_read(priv);
	mtk_star_intr_ack_all(priv);

	if (status & MTK_STAR_BIT_INT_STS_TNTC)
		/* Clean-up all TX descriptors. */
		mtk_star_tx_complete_all(priv);

	if (status & MTK_STAR_BIT_INT_STS_FNRC)
		/* Receive up to $budget packets. */
		received = mtk_star_process_rx(priv, budget);

	if (unlikely(status & MTK_STAR_REG_INT_STS_MIB_CNT_TH)) {
		mtk_star_update_stats(priv);
		mtk_star_reset_counters(priv);
	}

	if (received < budget)
		napi_complete_done(napi, received);

	mtk_star_intr_enable(priv);

	return received;
}

static void mtk_star_mdio_rwok_clear(struct mtk_star_priv *priv)
{
	regmap_write(priv->regs, MTK_STAR_REG_PHY_CTRL0,
		     MTK_STAR_BIT_PHY_CTRL0_RWOK);
}

static int mtk_star_mdio_rwok_wait(struct mtk_star_priv *priv)
{
	unsigned int val;

	return regmap_read_poll_timeout(priv->regs, MTK_STAR_REG_PHY_CTRL0,
					val, val & MTK_STAR_BIT_PHY_CTRL0_RWOK,
					10, MTK_STAR_WAIT_TIMEOUT);
}

static int mtk_star_mdio_read(struct mii_bus *mii, int phy_id, int regnum)
{
	struct mtk_star_priv *priv = mii->priv;
	unsigned int val, data;
	int ret;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	mtk_star_mdio_rwok_clear(priv);

	val = (regnum << MTK_STAR_OFF_PHY_CTRL0_PREG);
	val &= MTK_STAR_MSK_PHY_CTRL0_PREG;
	val |= MTK_STAR_BIT_PHY_CTRL0_RDCMD;

	regmap_write(priv->regs, MTK_STAR_REG_PHY_CTRL0, val);

	ret = mtk_star_mdio_rwok_wait(priv);
	if (ret)
		return ret;

	regmap_read(priv->regs, MTK_STAR_REG_PHY_CTRL0, &data);

	data &= MTK_STAR_MSK_PHY_CTRL0_RWDATA;
	data >>= MTK_STAR_OFF_PHY_CTRL0_RWDATA;

	return data;
}

static int mtk_star_mdio_write(struct mii_bus *mii, int phy_id,
			       int regnum, u16 data)
{
	struct mtk_star_priv *priv = mii->priv;
	unsigned int val;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	mtk_star_mdio_rwok_clear(priv);

	val = data;
	val <<= MTK_STAR_OFF_PHY_CTRL0_RWDATA;
	val &= MTK_STAR_MSK_PHY_CTRL0_RWDATA;
	regnum <<= MTK_STAR_OFF_PHY_CTRL0_PREG;
	regnum &= MTK_STAR_MSK_PHY_CTRL0_PREG;
	val |= regnum;
	val |= MTK_STAR_BIT_PHY_CTRL0_WTCMD;

	regmap_write(priv->regs, MTK_STAR_REG_PHY_CTRL0, val);

	return mtk_star_mdio_rwok_wait(priv);
}

static int mtk_star_mdio_init(struct net_device *ndev)
{
	struct mtk_star_priv *priv = netdev_priv(ndev);
	struct device *dev = mtk_star_get_dev(priv);
	struct device_node *of_node, *mdio_node;
	int ret;

	of_node = dev->of_node;

	mdio_node = of_get_child_by_name(of_node, "mdio");
	if (!mdio_node)
		return -ENODEV;

	if (!of_device_is_available(mdio_node)) {
		ret = -ENODEV;
		goto out_put_node;
	}

	priv->mii = devm_mdiobus_alloc(dev);
	if (!priv->mii) {
		ret = -ENOMEM;
		goto out_put_node;
	}

	snprintf(priv->mii->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	priv->mii->name = "mtk-mac-mdio";
	priv->mii->parent = dev;
	priv->mii->read = mtk_star_mdio_read;
	priv->mii->write = mtk_star_mdio_write;
	priv->mii->priv = priv;

	ret = devm_of_mdiobus_register(dev, priv->mii, mdio_node);

out_put_node:
	of_node_put(mdio_node);
	return ret;
}

static __maybe_unused int mtk_star_suspend(struct device *dev)
{
	struct mtk_star_priv *priv;
	struct net_device *ndev;

	ndev = dev_get_drvdata(dev);
	priv = netdev_priv(ndev);

	if (netif_running(ndev))
		mtk_star_disable(ndev);

	clk_bulk_disable_unprepare(MTK_STAR_NCLKS, priv->clks);

	return 0;
}

static __maybe_unused int mtk_star_resume(struct device *dev)
{
	struct mtk_star_priv *priv;
	struct net_device *ndev;
	int ret;

	ndev = dev_get_drvdata(dev);
	priv = netdev_priv(ndev);

	ret = clk_bulk_prepare_enable(MTK_STAR_NCLKS, priv->clks);
	if (ret)
		return ret;

	if (netif_running(ndev)) {
		ret = mtk_star_enable(ndev);
		if (ret)
			clk_bulk_disable_unprepare(MTK_STAR_NCLKS, priv->clks);
	}

	return ret;
}

static void mtk_star_clk_disable_unprepare(void *data)
{
	struct mtk_star_priv *priv = data;

	clk_bulk_disable_unprepare(MTK_STAR_NCLKS, priv->clks);
}

static int mtk_star_probe(struct platform_device *pdev)
{
	struct device_node *of_node;
	struct mtk_star_priv *priv;
	struct net_device *ndev;
	struct device *dev;
	void __iomem *base;
	int ret, i;

	dev = &pdev->dev;
	of_node = dev->of_node;

	ndev = devm_alloc_etherdev(dev, sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	SET_NETDEV_DEV(ndev, dev);
	platform_set_drvdata(pdev, ndev);

	ndev->min_mtu = ETH_ZLEN;
	ndev->max_mtu = MTK_STAR_MAX_FRAME_SIZE;

	spin_lock_init(&priv->lock);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* We won't be checking the return values of regmap read & write
	 * functions. They can only fail for mmio if there's a clock attached
	 * to regmap which is not the case here.
	 */
	priv->regs = devm_regmap_init_mmio(dev, base,
					   &mtk_star_regmap_config);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	priv->pericfg = syscon_regmap_lookup_by_phandle(of_node,
							"mediatek,pericfg");
	if (IS_ERR(priv->pericfg)) {
		dev_err(dev, "Failed to lookup the PERICFG syscon\n");
		return PTR_ERR(priv->pericfg);
	}

	ndev->irq = platform_get_irq(pdev, 0);
	if (ndev->irq < 0)
		return ndev->irq;

	for (i = 0; i < MTK_STAR_NCLKS; i++)
		priv->clks[i].id = mtk_star_clk_names[i];
	ret = devm_clk_bulk_get(dev, MTK_STAR_NCLKS, priv->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(MTK_STAR_NCLKS, priv->clks);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev,
				       mtk_star_clk_disable_unprepare, priv);
	if (ret)
		return ret;

	ret = of_get_phy_mode(of_node, &priv->phy_intf);
	if (ret) {
		return ret;
	} else if (priv->phy_intf != PHY_INTERFACE_MODE_RMII) {
		dev_err(dev, "unsupported phy mode: %s\n",
			phy_modes(priv->phy_intf));
		return -EINVAL;
	}

	priv->phy_node = of_parse_phandle(of_node, "phy-handle", 0);
	if (!priv->phy_node) {
		dev_err(dev, "failed to retrieve the phy handle from device tree\n");
		return -ENODEV;
	}

	mtk_star_set_mode_rmii(priv);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "unsupported DMA mask\n");
		return ret;
	}

	priv->ring_base = dmam_alloc_coherent(dev, MTK_STAR_DMA_SIZE,
					      &priv->dma_addr,
					      GFP_KERNEL | GFP_DMA);
	if (!priv->ring_base)
		return -ENOMEM;

	mtk_star_nic_disable_pd(priv);
	mtk_star_init_config(priv);

	ret = mtk_star_mdio_init(ndev);
	if (ret)
		return ret;

	ret = platform_get_ethdev_address(dev, ndev);
	if (ret || !is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	ndev->netdev_ops = &mtk_star_netdev_ops;
	ndev->ethtool_ops = &mtk_star_ethtool_ops;

	netif_napi_add(ndev, &priv->napi, mtk_star_poll, MTK_STAR_NAPI_WEIGHT);

	return devm_register_netdev(dev, ndev);
}

#ifdef CONFIG_OF
static const struct of_device_id mtk_star_of_match[] = {
	{ .compatible = "mediatek,mt8516-eth", },
	{ .compatible = "mediatek,mt8518-eth", },
	{ .compatible = "mediatek,mt8175-eth", },
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_star_of_match);
#endif

static SIMPLE_DEV_PM_OPS(mtk_star_pm_ops,
			 mtk_star_suspend, mtk_star_resume);

static struct platform_driver mtk_star_driver = {
	.driver = {
		.name = MTK_STAR_DRVNAME,
		.pm = &mtk_star_pm_ops,
		.of_match_table = of_match_ptr(mtk_star_of_match),
	},
	.probe = mtk_star_probe,
};
module_platform_driver(mtk_star_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_DESCRIPTION("Mediatek STAR Ethernet MAC Driver");
MODULE_LICENSE("GPL");
