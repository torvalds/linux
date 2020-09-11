// SPDX-License-Identifier: GPL-2.0
/*  Atheros AR71xx built-in ethernet mac driver
 *
 *  Copyright (C) 2019 Oleksij Rempel <o.rempel@pengutronix.de>
 *
 *  List of authors contributed to this driver before mainlining:
 *  Alexander Couzens <lynxis@fe80.eu>
 *  Christian Lamparter <chunkeey@gmail.com>
 *  Chuanhong Guo <gch981213@gmail.com>
 *  Daniel F. Dickinson <cshored@thecshore.com>
 *  David Bauer <mail@david-bauer.net>
 *  Felix Fietkau <nbd@nbd.name>
 *  Gabor Juhos <juhosg@freemail.hu>
 *  Hauke Mehrtens <hauke@hauke-m.de>
 *  Johann Neuhauser <johann@it-neuhauser.de>
 *  John Crispin <john@phrozen.org>
 *  Jo-Philipp Wich <jo@mein.io>
 *  Koen Vandeputte <koen.vandeputte@ncentric.com>
 *  Lucian Cristian <lucian.cristian@gmail.com>
 *  Matt Merhar <mattmerhar@protonmail.com>
 *  Milan Krstic <milan.krstic@gmail.com>
 *  Petr Štetiar <ynezz@true.cz>
 *  Rosen Penev <rosenp@gmail.com>
 *  Stephen Walker <stephendwalker+github@gmail.com>
 *  Vittorio Gambaletta <openwrt@vittgam.net>
 *  Weijie Gao <hackpascal@gmail.com>
 *  Imre Kaloz <kaloz@openwrt.org>
 */

#include <linux/if_vlan.h>
#include <linux/mfd/syscon.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phylink.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/io.h>

/* For our NAPI weight bigger does *NOT* mean better - it means more
 * D-cache misses and lots more wasted cycles than we'll ever
 * possibly gain from saving instructions.
 */
#define AG71XX_NAPI_WEIGHT	32
#define AG71XX_OOM_REFILL	(1 + HZ / 10)

#define AG71XX_INT_ERR	(AG71XX_INT_RX_BE | AG71XX_INT_TX_BE)
#define AG71XX_INT_TX	(AG71XX_INT_TX_PS)
#define AG71XX_INT_RX	(AG71XX_INT_RX_PR | AG71XX_INT_RX_OF)

#define AG71XX_INT_POLL	(AG71XX_INT_RX | AG71XX_INT_TX)
#define AG71XX_INT_INIT	(AG71XX_INT_ERR | AG71XX_INT_POLL)

#define AG71XX_TX_MTU_LEN	1540

#define AG71XX_TX_RING_SPLIT		512
#define AG71XX_TX_RING_DS_PER_PKT	DIV_ROUND_UP(AG71XX_TX_MTU_LEN, \
						     AG71XX_TX_RING_SPLIT)
#define AG71XX_TX_RING_SIZE_DEFAULT	128
#define AG71XX_RX_RING_SIZE_DEFAULT	256

#define AG71XX_MDIO_RETRY	1000
#define AG71XX_MDIO_DELAY	5
#define AG71XX_MDIO_MAX_CLK	5000000

/* Register offsets */
#define AG71XX_REG_MAC_CFG1	0x0000
#define MAC_CFG1_TXE		BIT(0)	/* Tx Enable */
#define MAC_CFG1_STX		BIT(1)	/* Synchronize Tx Enable */
#define MAC_CFG1_RXE		BIT(2)	/* Rx Enable */
#define MAC_CFG1_SRX		BIT(3)	/* Synchronize Rx Enable */
#define MAC_CFG1_TFC		BIT(4)	/* Tx Flow Control Enable */
#define MAC_CFG1_RFC		BIT(5)	/* Rx Flow Control Enable */
#define MAC_CFG1_SR		BIT(31)	/* Soft Reset */
#define MAC_CFG1_INIT	(MAC_CFG1_RXE | MAC_CFG1_TXE | \
			 MAC_CFG1_SRX | MAC_CFG1_STX)

#define AG71XX_REG_MAC_CFG2	0x0004
#define MAC_CFG2_FDX		BIT(0)
#define MAC_CFG2_PAD_CRC_EN	BIT(2)
#define MAC_CFG2_LEN_CHECK	BIT(4)
#define MAC_CFG2_IF_1000	BIT(9)
#define MAC_CFG2_IF_10_100	BIT(8)

#define AG71XX_REG_MAC_MFL	0x0010

#define AG71XX_REG_MII_CFG	0x0020
#define MII_CFG_CLK_DIV_4	0
#define MII_CFG_CLK_DIV_6	2
#define MII_CFG_CLK_DIV_8	3
#define MII_CFG_CLK_DIV_10	4
#define MII_CFG_CLK_DIV_14	5
#define MII_CFG_CLK_DIV_20	6
#define MII_CFG_CLK_DIV_28	7
#define MII_CFG_CLK_DIV_34	8
#define MII_CFG_CLK_DIV_42	9
#define MII_CFG_CLK_DIV_50	10
#define MII_CFG_CLK_DIV_58	11
#define MII_CFG_CLK_DIV_66	12
#define MII_CFG_CLK_DIV_74	13
#define MII_CFG_CLK_DIV_82	14
#define MII_CFG_CLK_DIV_98	15
#define MII_CFG_RESET		BIT(31)

#define AG71XX_REG_MII_CMD	0x0024
#define MII_CMD_READ		BIT(0)

#define AG71XX_REG_MII_ADDR	0x0028
#define MII_ADDR_SHIFT		8

#define AG71XX_REG_MII_CTRL	0x002c
#define AG71XX_REG_MII_STATUS	0x0030
#define AG71XX_REG_MII_IND	0x0034
#define MII_IND_BUSY		BIT(0)
#define MII_IND_INVALID		BIT(2)

#define AG71XX_REG_MAC_IFCTL	0x0038
#define MAC_IFCTL_SPEED		BIT(16)

#define AG71XX_REG_MAC_ADDR1	0x0040
#define AG71XX_REG_MAC_ADDR2	0x0044
#define AG71XX_REG_FIFO_CFG0	0x0048
#define FIFO_CFG0_WTM		BIT(0)	/* Watermark Module */
#define FIFO_CFG0_RXS		BIT(1)	/* Rx System Module */
#define FIFO_CFG0_RXF		BIT(2)	/* Rx Fabric Module */
#define FIFO_CFG0_TXS		BIT(3)	/* Tx System Module */
#define FIFO_CFG0_TXF		BIT(4)	/* Tx Fabric Module */
#define FIFO_CFG0_ALL	(FIFO_CFG0_WTM | FIFO_CFG0_RXS | FIFO_CFG0_RXF \
			| FIFO_CFG0_TXS | FIFO_CFG0_TXF)
#define FIFO_CFG0_INIT	(FIFO_CFG0_ALL << FIFO_CFG0_ENABLE_SHIFT)

#define FIFO_CFG0_ENABLE_SHIFT	8

#define AG71XX_REG_FIFO_CFG1	0x004c
#define AG71XX_REG_FIFO_CFG2	0x0050
#define AG71XX_REG_FIFO_CFG3	0x0054
#define AG71XX_REG_FIFO_CFG4	0x0058
#define FIFO_CFG4_DE		BIT(0)	/* Drop Event */
#define FIFO_CFG4_DV		BIT(1)	/* RX_DV Event */
#define FIFO_CFG4_FC		BIT(2)	/* False Carrier */
#define FIFO_CFG4_CE		BIT(3)	/* Code Error */
#define FIFO_CFG4_CR		BIT(4)	/* CRC error */
#define FIFO_CFG4_LM		BIT(5)	/* Length Mismatch */
#define FIFO_CFG4_LO		BIT(6)	/* Length out of range */
#define FIFO_CFG4_OK		BIT(7)	/* Packet is OK */
#define FIFO_CFG4_MC		BIT(8)	/* Multicast Packet */
#define FIFO_CFG4_BC		BIT(9)	/* Broadcast Packet */
#define FIFO_CFG4_DR		BIT(10)	/* Dribble */
#define FIFO_CFG4_LE		BIT(11)	/* Long Event */
#define FIFO_CFG4_CF		BIT(12)	/* Control Frame */
#define FIFO_CFG4_PF		BIT(13)	/* Pause Frame */
#define FIFO_CFG4_UO		BIT(14)	/* Unsupported Opcode */
#define FIFO_CFG4_VT		BIT(15)	/* VLAN tag detected */
#define FIFO_CFG4_FT		BIT(16)	/* Frame Truncated */
#define FIFO_CFG4_UC		BIT(17)	/* Unicast Packet */
#define FIFO_CFG4_INIT	(FIFO_CFG4_DE | FIFO_CFG4_DV | FIFO_CFG4_FC | \
			 FIFO_CFG4_CE | FIFO_CFG4_CR | FIFO_CFG4_LM | \
			 FIFO_CFG4_LO | FIFO_CFG4_OK | FIFO_CFG4_MC | \
			 FIFO_CFG4_BC | FIFO_CFG4_DR | FIFO_CFG4_LE | \
			 FIFO_CFG4_CF | FIFO_CFG4_PF | FIFO_CFG4_UO | \
			 FIFO_CFG4_VT)

#define AG71XX_REG_FIFO_CFG5	0x005c
#define FIFO_CFG5_DE		BIT(0)	/* Drop Event */
#define FIFO_CFG5_DV		BIT(1)	/* RX_DV Event */
#define FIFO_CFG5_FC		BIT(2)	/* False Carrier */
#define FIFO_CFG5_CE		BIT(3)	/* Code Error */
#define FIFO_CFG5_LM		BIT(4)	/* Length Mismatch */
#define FIFO_CFG5_LO		BIT(5)	/* Length Out of Range */
#define FIFO_CFG5_OK		BIT(6)	/* Packet is OK */
#define FIFO_CFG5_MC		BIT(7)	/* Multicast Packet */
#define FIFO_CFG5_BC		BIT(8)	/* Broadcast Packet */
#define FIFO_CFG5_DR		BIT(9)	/* Dribble */
#define FIFO_CFG5_CF		BIT(10)	/* Control Frame */
#define FIFO_CFG5_PF		BIT(11)	/* Pause Frame */
#define FIFO_CFG5_UO		BIT(12)	/* Unsupported Opcode */
#define FIFO_CFG5_VT		BIT(13)	/* VLAN tag detected */
#define FIFO_CFG5_LE		BIT(14)	/* Long Event */
#define FIFO_CFG5_FT		BIT(15)	/* Frame Truncated */
#define FIFO_CFG5_16		BIT(16)	/* unknown */
#define FIFO_CFG5_17		BIT(17)	/* unknown */
#define FIFO_CFG5_SF		BIT(18)	/* Short Frame */
#define FIFO_CFG5_BM		BIT(19)	/* Byte Mode */
#define FIFO_CFG5_INIT	(FIFO_CFG5_DE | FIFO_CFG5_DV | FIFO_CFG5_FC | \
			 FIFO_CFG5_CE | FIFO_CFG5_LO | FIFO_CFG5_OK | \
			 FIFO_CFG5_MC | FIFO_CFG5_BC | FIFO_CFG5_DR | \
			 FIFO_CFG5_CF | FIFO_CFG5_PF | FIFO_CFG5_VT | \
			 FIFO_CFG5_LE | FIFO_CFG5_FT | FIFO_CFG5_16 | \
			 FIFO_CFG5_17 | FIFO_CFG5_SF)

#define AG71XX_REG_TX_CTRL	0x0180
#define TX_CTRL_TXE		BIT(0)	/* Tx Enable */

#define AG71XX_REG_TX_DESC	0x0184
#define AG71XX_REG_TX_STATUS	0x0188
#define TX_STATUS_PS		BIT(0)	/* Packet Sent */
#define TX_STATUS_UR		BIT(1)	/* Tx Underrun */
#define TX_STATUS_BE		BIT(3)	/* Bus Error */

#define AG71XX_REG_RX_CTRL	0x018c
#define RX_CTRL_RXE		BIT(0)	/* Rx Enable */

#define AG71XX_DMA_RETRY	10
#define AG71XX_DMA_DELAY	1

#define AG71XX_REG_RX_DESC	0x0190
#define AG71XX_REG_RX_STATUS	0x0194
#define RX_STATUS_PR		BIT(0)	/* Packet Received */
#define RX_STATUS_OF		BIT(2)	/* Rx Overflow */
#define RX_STATUS_BE		BIT(3)	/* Bus Error */

#define AG71XX_REG_INT_ENABLE	0x0198
#define AG71XX_REG_INT_STATUS	0x019c
#define AG71XX_INT_TX_PS	BIT(0)
#define AG71XX_INT_TX_UR	BIT(1)
#define AG71XX_INT_TX_BE	BIT(3)
#define AG71XX_INT_RX_PR	BIT(4)
#define AG71XX_INT_RX_OF	BIT(6)
#define AG71XX_INT_RX_BE	BIT(7)

#define AG71XX_REG_FIFO_DEPTH	0x01a8
#define AG71XX_REG_RX_SM	0x01b0
#define AG71XX_REG_TX_SM	0x01b4

#define ETH_SWITCH_HEADER_LEN	2

#define AG71XX_DEFAULT_MSG_ENABLE	\
	(NETIF_MSG_DRV			\
	| NETIF_MSG_PROBE		\
	| NETIF_MSG_LINK		\
	| NETIF_MSG_TIMER		\
	| NETIF_MSG_IFDOWN		\
	| NETIF_MSG_IFUP		\
	| NETIF_MSG_RX_ERR		\
	| NETIF_MSG_TX_ERR)

struct ag71xx_statistic {
	unsigned short offset;
	u32 mask;
	const char name[ETH_GSTRING_LEN];
};

static const struct ag71xx_statistic ag71xx_statistics[] = {
	{ 0x0080, GENMASK(17, 0), "Tx/Rx 64 Byte", },
	{ 0x0084, GENMASK(17, 0), "Tx/Rx 65-127 Byte", },
	{ 0x0088, GENMASK(17, 0), "Tx/Rx 128-255 Byte", },
	{ 0x008C, GENMASK(17, 0), "Tx/Rx 256-511 Byte", },
	{ 0x0090, GENMASK(17, 0), "Tx/Rx 512-1023 Byte", },
	{ 0x0094, GENMASK(17, 0), "Tx/Rx 1024-1518 Byte", },
	{ 0x0098, GENMASK(17, 0), "Tx/Rx 1519-1522 Byte VLAN", },
	{ 0x009C, GENMASK(23, 0), "Rx Byte", },
	{ 0x00A0, GENMASK(17, 0), "Rx Packet", },
	{ 0x00A4, GENMASK(11, 0), "Rx FCS Error", },
	{ 0x00A8, GENMASK(17, 0), "Rx Multicast Packet", },
	{ 0x00AC, GENMASK(21, 0), "Rx Broadcast Packet", },
	{ 0x00B0, GENMASK(17, 0), "Rx Control Frame Packet", },
	{ 0x00B4, GENMASK(11, 0), "Rx Pause Frame Packet", },
	{ 0x00B8, GENMASK(11, 0), "Rx Unknown OPCode Packet", },
	{ 0x00BC, GENMASK(11, 0), "Rx Alignment Error", },
	{ 0x00C0, GENMASK(15, 0), "Rx Frame Length Error", },
	{ 0x00C4, GENMASK(11, 0), "Rx Code Error", },
	{ 0x00C8, GENMASK(11, 0), "Rx Carrier Sense Error", },
	{ 0x00CC, GENMASK(11, 0), "Rx Undersize Packet", },
	{ 0x00D0, GENMASK(11, 0), "Rx Oversize Packet", },
	{ 0x00D4, GENMASK(11, 0), "Rx Fragments", },
	{ 0x00D8, GENMASK(11, 0), "Rx Jabber", },
	{ 0x00DC, GENMASK(11, 0), "Rx Dropped Packet", },
	{ 0x00E0, GENMASK(23, 0), "Tx Byte", },
	{ 0x00E4, GENMASK(17, 0), "Tx Packet", },
	{ 0x00E8, GENMASK(17, 0), "Tx Multicast Packet", },
	{ 0x00EC, GENMASK(17, 0), "Tx Broadcast Packet", },
	{ 0x00F0, GENMASK(11, 0), "Tx Pause Control Frame", },
	{ 0x00F4, GENMASK(11, 0), "Tx Deferral Packet", },
	{ 0x00F8, GENMASK(11, 0), "Tx Excessive Deferral Packet", },
	{ 0x00FC, GENMASK(11, 0), "Tx Single Collision Packet", },
	{ 0x0100, GENMASK(11, 0), "Tx Multiple Collision", },
	{ 0x0104, GENMASK(11, 0), "Tx Late Collision Packet", },
	{ 0x0108, GENMASK(11, 0), "Tx Excessive Collision Packet", },
	{ 0x010C, GENMASK(12, 0), "Tx Total Collision", },
	{ 0x0110, GENMASK(11, 0), "Tx Pause Frames Honored", },
	{ 0x0114, GENMASK(11, 0), "Tx Drop Frame", },
	{ 0x0118, GENMASK(11, 0), "Tx Jabber Frame", },
	{ 0x011C, GENMASK(11, 0), "Tx FCS Error", },
	{ 0x0120, GENMASK(11, 0), "Tx Control Frame", },
	{ 0x0124, GENMASK(11, 0), "Tx Oversize Frame", },
	{ 0x0128, GENMASK(11, 0), "Tx Undersize Frame", },
	{ 0x012C, GENMASK(11, 0), "Tx Fragment", },
};

#define DESC_EMPTY		BIT(31)
#define DESC_MORE		BIT(24)
#define DESC_PKTLEN_M		0xfff
struct ag71xx_desc {
	u32 data;
	u32 ctrl;
	u32 next;
	u32 pad;
} __aligned(4);

#define AG71XX_DESC_SIZE	roundup(sizeof(struct ag71xx_desc), \
					L1_CACHE_BYTES)

struct ag71xx_buf {
	union {
		struct {
			struct sk_buff *skb;
			unsigned int len;
		} tx;
		struct {
			dma_addr_t dma_addr;
			void *rx_buf;
		} rx;
	};
};

struct ag71xx_ring {
	/* "Hot" fields in the data path. */
	unsigned int curr;
	unsigned int dirty;

	/* "Cold" fields - not used in the data path. */
	struct ag71xx_buf *buf;
	u16 order;
	u16 desc_split;
	dma_addr_t descs_dma;
	u8 *descs_cpu;
};

enum ag71xx_type {
	AR7100,
	AR7240,
	AR9130,
	AR9330,
	AR9340,
	QCA9530,
	QCA9550,
};

struct ag71xx_dcfg {
	u32 max_frame_len;
	const u32 *fifodata;
	u16 desc_pktlen_mask;
	bool tx_hang_workaround;
	enum ag71xx_type type;
};

struct ag71xx {
	/* Critical data related to the per-packet data path are clustered
	 * early in this structure to help improve the D-cache footprint.
	 */
	struct ag71xx_ring rx_ring ____cacheline_aligned;
	struct ag71xx_ring tx_ring ____cacheline_aligned;

	u16 rx_buf_size;
	u8 rx_buf_offset;

	struct net_device *ndev;
	struct platform_device *pdev;
	struct napi_struct napi;
	u32 msg_enable;
	const struct ag71xx_dcfg *dcfg;

	/* From this point onwards we're not looking at per-packet fields. */
	void __iomem *mac_base;

	struct ag71xx_desc *stop_desc;
	dma_addr_t stop_desc_dma;

	phy_interface_t phy_if_mode;
	struct phylink *phylink;
	struct phylink_config phylink_config;

	struct delayed_work restart_work;
	struct timer_list oom_timer;

	struct reset_control *mac_reset;

	u32 fifodata[3];
	int mac_idx;

	struct reset_control *mdio_reset;
	struct mii_bus *mii_bus;
	struct clk *clk_mdio;
	struct clk *clk_eth;
};

static int ag71xx_desc_empty(struct ag71xx_desc *desc)
{
	return (desc->ctrl & DESC_EMPTY) != 0;
}

static struct ag71xx_desc *ag71xx_ring_desc(struct ag71xx_ring *ring, int idx)
{
	return (struct ag71xx_desc *)&ring->descs_cpu[idx * AG71XX_DESC_SIZE];
}

static int ag71xx_ring_size_order(int size)
{
	return fls(size - 1);
}

static bool ag71xx_is(struct ag71xx *ag, enum ag71xx_type type)
{
	return ag->dcfg->type == type;
}

static void ag71xx_wr(struct ag71xx *ag, unsigned int reg, u32 value)
{
	iowrite32(value, ag->mac_base + reg);
	/* flush write */
	(void)ioread32(ag->mac_base + reg);
}

static u32 ag71xx_rr(struct ag71xx *ag, unsigned int reg)
{
	return ioread32(ag->mac_base + reg);
}

static void ag71xx_sb(struct ag71xx *ag, unsigned int reg, u32 mask)
{
	void __iomem *r;

	r = ag->mac_base + reg;
	iowrite32(ioread32(r) | mask, r);
	/* flush write */
	(void)ioread32(r);
}

static void ag71xx_cb(struct ag71xx *ag, unsigned int reg, u32 mask)
{
	void __iomem *r;

	r = ag->mac_base + reg;
	iowrite32(ioread32(r) & ~mask, r);
	/* flush write */
	(void)ioread32(r);
}

static void ag71xx_int_enable(struct ag71xx *ag, u32 ints)
{
	ag71xx_sb(ag, AG71XX_REG_INT_ENABLE, ints);
}

static void ag71xx_int_disable(struct ag71xx *ag, u32 ints)
{
	ag71xx_cb(ag, AG71XX_REG_INT_ENABLE, ints);
}

static void ag71xx_get_drvinfo(struct net_device *ndev,
			       struct ethtool_drvinfo *info)
{
	struct ag71xx *ag = netdev_priv(ndev);

	strlcpy(info->driver, "ag71xx", sizeof(info->driver));
	strlcpy(info->bus_info, of_node_full_name(ag->pdev->dev.of_node),
		sizeof(info->bus_info));
}

static int ag71xx_get_link_ksettings(struct net_device *ndev,
				   struct ethtool_link_ksettings *kset)
{
	struct ag71xx *ag = netdev_priv(ndev);

	return phylink_ethtool_ksettings_get(ag->phylink, kset);
}

static int ag71xx_set_link_ksettings(struct net_device *ndev,
				   const struct ethtool_link_ksettings *kset)
{
	struct ag71xx *ag = netdev_priv(ndev);

	return phylink_ethtool_ksettings_set(ag->phylink, kset);
}

static int ag71xx_ethtool_nway_reset(struct net_device *ndev)
{
	struct ag71xx *ag = netdev_priv(ndev);

	return phylink_ethtool_nway_reset(ag->phylink);
}

static void ag71xx_ethtool_get_pauseparam(struct net_device *ndev,
					  struct ethtool_pauseparam *pause)
{
	struct ag71xx *ag = netdev_priv(ndev);

	phylink_ethtool_get_pauseparam(ag->phylink, pause);
}

static int ag71xx_ethtool_set_pauseparam(struct net_device *ndev,
					 struct ethtool_pauseparam *pause)
{
	struct ag71xx *ag = netdev_priv(ndev);

	return phylink_ethtool_set_pauseparam(ag->phylink, pause);
}

static void ag71xx_ethtool_get_strings(struct net_device *netdev, u32 sset,
				       u8 *data)
{
	if (sset == ETH_SS_STATS) {
		int i;

		for (i = 0; i < ARRAY_SIZE(ag71xx_statistics); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       ag71xx_statistics[i].name, ETH_GSTRING_LEN);
	}
}

static void ag71xx_ethtool_get_stats(struct net_device *ndev,
				     struct ethtool_stats *stats, u64 *data)
{
	struct ag71xx *ag = netdev_priv(ndev);
	int i;

	for (i = 0; i < ARRAY_SIZE(ag71xx_statistics); i++)
		*data++ = ag71xx_rr(ag, ag71xx_statistics[i].offset)
				& ag71xx_statistics[i].mask;
}

static int ag71xx_ethtool_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(ag71xx_statistics);
	return -EOPNOTSUPP;
}

static const struct ethtool_ops ag71xx_ethtool_ops = {
	.get_drvinfo			= ag71xx_get_drvinfo,
	.get_link			= ethtool_op_get_link,
	.get_ts_info			= ethtool_op_get_ts_info,
	.get_link_ksettings		= ag71xx_get_link_ksettings,
	.set_link_ksettings		= ag71xx_set_link_ksettings,
	.nway_reset			= ag71xx_ethtool_nway_reset,
	.get_pauseparam			= ag71xx_ethtool_get_pauseparam,
	.set_pauseparam			= ag71xx_ethtool_set_pauseparam,
	.get_strings			= ag71xx_ethtool_get_strings,
	.get_ethtool_stats		= ag71xx_ethtool_get_stats,
	.get_sset_count			= ag71xx_ethtool_get_sset_count,
};

static int ag71xx_mdio_wait_busy(struct ag71xx *ag)
{
	struct net_device *ndev = ag->ndev;
	int i;

	for (i = 0; i < AG71XX_MDIO_RETRY; i++) {
		u32 busy;

		udelay(AG71XX_MDIO_DELAY);

		busy = ag71xx_rr(ag, AG71XX_REG_MII_IND);
		if (!busy)
			return 0;

		udelay(AG71XX_MDIO_DELAY);
	}

	netif_err(ag, link, ndev, "MDIO operation timed out\n");

	return -ETIMEDOUT;
}

static int ag71xx_mdio_mii_read(struct mii_bus *bus, int addr, int reg)
{
	struct ag71xx *ag = bus->priv;
	int err, val;

	err = ag71xx_mdio_wait_busy(ag);
	if (err)
		return err;

	ag71xx_wr(ag, AG71XX_REG_MII_ADDR,
		  ((addr & 0x1f) << MII_ADDR_SHIFT) | (reg & 0xff));
	/* enable read mode */
	ag71xx_wr(ag, AG71XX_REG_MII_CMD, MII_CMD_READ);

	err = ag71xx_mdio_wait_busy(ag);
	if (err)
		return err;

	val = ag71xx_rr(ag, AG71XX_REG_MII_STATUS);
	/* disable read mode */
	ag71xx_wr(ag, AG71XX_REG_MII_CMD, 0);

	netif_dbg(ag, link, ag->ndev, "mii_read: addr=%04x, reg=%04x, value=%04x\n",
		  addr, reg, val);

	return val;
}

static int ag71xx_mdio_mii_write(struct mii_bus *bus, int addr, int reg,
				 u16 val)
{
	struct ag71xx *ag = bus->priv;

	netif_dbg(ag, link, ag->ndev, "mii_write: addr=%04x, reg=%04x, value=%04x\n",
		  addr, reg, val);

	ag71xx_wr(ag, AG71XX_REG_MII_ADDR,
		  ((addr & 0x1f) << MII_ADDR_SHIFT) | (reg & 0xff));
	ag71xx_wr(ag, AG71XX_REG_MII_CTRL, val);

	return ag71xx_mdio_wait_busy(ag);
}

static const u32 ar71xx_mdio_div_table[] = {
	4, 4, 6, 8, 10, 14, 20, 28,
};

static const u32 ar7240_mdio_div_table[] = {
	2, 2, 4, 6, 8, 12, 18, 26, 32, 40, 48, 56, 62, 70, 78, 96,
};

static const u32 ar933x_mdio_div_table[] = {
	4, 4, 6, 8, 10, 14, 20, 28, 34, 42, 50, 58, 66, 74, 82, 98,
};

static int ag71xx_mdio_get_divider(struct ag71xx *ag, u32 *div)
{
	unsigned long ref_clock;
	const u32 *table;
	int ndivs, i;

	ref_clock = clk_get_rate(ag->clk_mdio);
	if (!ref_clock)
		return -EINVAL;

	if (ag71xx_is(ag, AR9330) || ag71xx_is(ag, AR9340)) {
		table = ar933x_mdio_div_table;
		ndivs = ARRAY_SIZE(ar933x_mdio_div_table);
	} else if (ag71xx_is(ag, AR7240)) {
		table = ar7240_mdio_div_table;
		ndivs = ARRAY_SIZE(ar7240_mdio_div_table);
	} else {
		table = ar71xx_mdio_div_table;
		ndivs = ARRAY_SIZE(ar71xx_mdio_div_table);
	}

	for (i = 0; i < ndivs; i++) {
		unsigned long t;

		t = ref_clock / table[i];
		if (t <= AG71XX_MDIO_MAX_CLK) {
			*div = i;
			return 0;
		}
	}

	return -ENOENT;
}

static int ag71xx_mdio_reset(struct mii_bus *bus)
{
	struct ag71xx *ag = bus->priv;
	int err;
	u32 t;

	err = ag71xx_mdio_get_divider(ag, &t);
	if (err)
		return err;

	ag71xx_wr(ag, AG71XX_REG_MII_CFG, t | MII_CFG_RESET);
	usleep_range(100, 200);

	ag71xx_wr(ag, AG71XX_REG_MII_CFG, t);
	usleep_range(100, 200);

	return 0;
}

static int ag71xx_mdio_probe(struct ag71xx *ag)
{
	struct device *dev = &ag->pdev->dev;
	struct net_device *ndev = ag->ndev;
	static struct mii_bus *mii_bus;
	struct device_node *np, *mnp;
	int err;

	np = dev->of_node;
	ag->mii_bus = NULL;

	ag->clk_mdio = devm_clk_get(dev, "mdio");
	if (IS_ERR(ag->clk_mdio)) {
		netif_err(ag, probe, ndev, "Failed to get mdio clk.\n");
		return PTR_ERR(ag->clk_mdio);
	}

	err = clk_prepare_enable(ag->clk_mdio);
	if (err) {
		netif_err(ag, probe, ndev, "Failed to enable mdio clk.\n");
		return err;
	}

	mii_bus = devm_mdiobus_alloc(dev);
	if (!mii_bus) {
		err = -ENOMEM;
		goto mdio_err_put_clk;
	}

	ag->mdio_reset = of_reset_control_get_exclusive(np, "mdio");
	if (IS_ERR(ag->mdio_reset)) {
		netif_err(ag, probe, ndev, "Failed to get reset mdio.\n");
		err = PTR_ERR(ag->mdio_reset);
		goto mdio_err_put_clk;
	}

	mii_bus->name = "ag71xx_mdio";
	mii_bus->read = ag71xx_mdio_mii_read;
	mii_bus->write = ag71xx_mdio_mii_write;
	mii_bus->reset = ag71xx_mdio_reset;
	mii_bus->priv = ag;
	mii_bus->parent = dev;
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s.%d", np->name, ag->mac_idx);

	if (!IS_ERR(ag->mdio_reset)) {
		reset_control_assert(ag->mdio_reset);
		msleep(100);
		reset_control_deassert(ag->mdio_reset);
		msleep(200);
	}

	mnp = of_get_child_by_name(np, "mdio");
	err = of_mdiobus_register(mii_bus, mnp);
	of_node_put(mnp);
	if (err)
		goto mdio_err_put_clk;

	ag->mii_bus = mii_bus;

	return 0;

mdio_err_put_clk:
	clk_disable_unprepare(ag->clk_mdio);
	return err;
}

static void ag71xx_mdio_remove(struct ag71xx *ag)
{
	if (ag->mii_bus)
		mdiobus_unregister(ag->mii_bus);
	clk_disable_unprepare(ag->clk_mdio);
}

static void ag71xx_hw_stop(struct ag71xx *ag)
{
	/* disable all interrupts and stop the rx/tx engine */
	ag71xx_wr(ag, AG71XX_REG_INT_ENABLE, 0);
	ag71xx_wr(ag, AG71XX_REG_RX_CTRL, 0);
	ag71xx_wr(ag, AG71XX_REG_TX_CTRL, 0);
}

static bool ag71xx_check_dma_stuck(struct ag71xx *ag)
{
	unsigned long timestamp;
	u32 rx_sm, tx_sm, rx_fd;

	timestamp = netdev_get_tx_queue(ag->ndev, 0)->trans_start;
	if (likely(time_before(jiffies, timestamp + HZ / 10)))
		return false;

	if (!netif_carrier_ok(ag->ndev))
		return false;

	rx_sm = ag71xx_rr(ag, AG71XX_REG_RX_SM);
	if ((rx_sm & 0x7) == 0x3 && ((rx_sm >> 4) & 0x7) == 0x6)
		return true;

	tx_sm = ag71xx_rr(ag, AG71XX_REG_TX_SM);
	rx_fd = ag71xx_rr(ag, AG71XX_REG_FIFO_DEPTH);
	if (((tx_sm >> 4) & 0x7) == 0 && ((rx_sm & 0x7) == 0) &&
	    ((rx_sm >> 4) & 0x7) == 0 && rx_fd == 0)
		return true;

	return false;
}

static int ag71xx_tx_packets(struct ag71xx *ag, bool flush)
{
	struct ag71xx_ring *ring = &ag->tx_ring;
	int sent = 0, bytes_compl = 0, n = 0;
	struct net_device *ndev = ag->ndev;
	int ring_mask, ring_size;
	bool dma_stuck = false;

	ring_mask = BIT(ring->order) - 1;
	ring_size = BIT(ring->order);

	netif_dbg(ag, tx_queued, ndev, "processing TX ring\n");

	while (ring->dirty + n != ring->curr) {
		struct ag71xx_desc *desc;
		struct sk_buff *skb;
		unsigned int i;

		i = (ring->dirty + n) & ring_mask;
		desc = ag71xx_ring_desc(ring, i);
		skb = ring->buf[i].tx.skb;

		if (!flush && !ag71xx_desc_empty(desc)) {
			if (ag->dcfg->tx_hang_workaround &&
			    ag71xx_check_dma_stuck(ag)) {
				schedule_delayed_work(&ag->restart_work,
						      HZ / 2);
				dma_stuck = true;
			}
			break;
		}

		if (flush)
			desc->ctrl |= DESC_EMPTY;

		n++;
		if (!skb)
			continue;

		dev_kfree_skb_any(skb);
		ring->buf[i].tx.skb = NULL;

		bytes_compl += ring->buf[i].tx.len;

		sent++;
		ring->dirty += n;

		while (n > 0) {
			ag71xx_wr(ag, AG71XX_REG_TX_STATUS, TX_STATUS_PS);
			n--;
		}
	}

	netif_dbg(ag, tx_done, ndev, "%d packets sent out\n", sent);

	if (!sent)
		return 0;

	ag->ndev->stats.tx_bytes += bytes_compl;
	ag->ndev->stats.tx_packets += sent;

	netdev_completed_queue(ag->ndev, sent, bytes_compl);
	if ((ring->curr - ring->dirty) < (ring_size * 3) / 4)
		netif_wake_queue(ag->ndev);

	if (!dma_stuck)
		cancel_delayed_work(&ag->restart_work);

	return sent;
}

static void ag71xx_dma_wait_stop(struct ag71xx *ag)
{
	struct net_device *ndev = ag->ndev;
	int i;

	for (i = 0; i < AG71XX_DMA_RETRY; i++) {
		u32 rx, tx;

		mdelay(AG71XX_DMA_DELAY);

		rx = ag71xx_rr(ag, AG71XX_REG_RX_CTRL) & RX_CTRL_RXE;
		tx = ag71xx_rr(ag, AG71XX_REG_TX_CTRL) & TX_CTRL_TXE;
		if (!rx && !tx)
			return;
	}

	netif_err(ag, hw, ndev, "DMA stop operation timed out\n");
}

static void ag71xx_dma_reset(struct ag71xx *ag)
{
	struct net_device *ndev = ag->ndev;
	u32 val;
	int i;

	/* stop RX and TX */
	ag71xx_wr(ag, AG71XX_REG_RX_CTRL, 0);
	ag71xx_wr(ag, AG71XX_REG_TX_CTRL, 0);

	/* give the hardware some time to really stop all rx/tx activity
	 * clearing the descriptors too early causes random memory corruption
	 */
	ag71xx_dma_wait_stop(ag);

	/* clear descriptor addresses */
	ag71xx_wr(ag, AG71XX_REG_TX_DESC, ag->stop_desc_dma);
	ag71xx_wr(ag, AG71XX_REG_RX_DESC, ag->stop_desc_dma);

	/* clear pending RX/TX interrupts */
	for (i = 0; i < 256; i++) {
		ag71xx_wr(ag, AG71XX_REG_RX_STATUS, RX_STATUS_PR);
		ag71xx_wr(ag, AG71XX_REG_TX_STATUS, TX_STATUS_PS);
	}

	/* clear pending errors */
	ag71xx_wr(ag, AG71XX_REG_RX_STATUS, RX_STATUS_BE | RX_STATUS_OF);
	ag71xx_wr(ag, AG71XX_REG_TX_STATUS, TX_STATUS_BE | TX_STATUS_UR);

	val = ag71xx_rr(ag, AG71XX_REG_RX_STATUS);
	if (val)
		netif_err(ag, hw, ndev, "unable to clear DMA Rx status: %08x\n",
			  val);

	val = ag71xx_rr(ag, AG71XX_REG_TX_STATUS);

	/* mask out reserved bits */
	val &= ~0xff000000;

	if (val)
		netif_err(ag, hw, ndev, "unable to clear DMA Tx status: %08x\n",
			  val);
}

static void ag71xx_hw_setup(struct ag71xx *ag)
{
	u32 init = MAC_CFG1_INIT;

	/* setup MAC configuration registers */
	ag71xx_wr(ag, AG71XX_REG_MAC_CFG1, init);

	ag71xx_sb(ag, AG71XX_REG_MAC_CFG2,
		  MAC_CFG2_PAD_CRC_EN | MAC_CFG2_LEN_CHECK);

	/* setup max frame length to zero */
	ag71xx_wr(ag, AG71XX_REG_MAC_MFL, 0);

	/* setup FIFO configuration registers */
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG0, FIFO_CFG0_INIT);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG1, ag->fifodata[0]);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG2, ag->fifodata[1]);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG4, FIFO_CFG4_INIT);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG5, FIFO_CFG5_INIT);
}

static unsigned int ag71xx_max_frame_len(unsigned int mtu)
{
	return ETH_SWITCH_HEADER_LEN + ETH_HLEN + VLAN_HLEN + mtu + ETH_FCS_LEN;
}

static void ag71xx_hw_set_macaddr(struct ag71xx *ag, unsigned char *mac)
{
	u32 t;

	t = (((u32)mac[5]) << 24) | (((u32)mac[4]) << 16)
	  | (((u32)mac[3]) << 8) | ((u32)mac[2]);

	ag71xx_wr(ag, AG71XX_REG_MAC_ADDR1, t);

	t = (((u32)mac[1]) << 24) | (((u32)mac[0]) << 16);
	ag71xx_wr(ag, AG71XX_REG_MAC_ADDR2, t);
}

static void ag71xx_fast_reset(struct ag71xx *ag)
{
	struct net_device *dev = ag->ndev;
	u32 rx_ds;
	u32 mii_reg;

	ag71xx_hw_stop(ag);

	mii_reg = ag71xx_rr(ag, AG71XX_REG_MII_CFG);
	rx_ds = ag71xx_rr(ag, AG71XX_REG_RX_DESC);

	ag71xx_tx_packets(ag, true);

	reset_control_assert(ag->mac_reset);
	usleep_range(10, 20);
	reset_control_deassert(ag->mac_reset);
	usleep_range(10, 20);

	ag71xx_dma_reset(ag);
	ag71xx_hw_setup(ag);
	ag->tx_ring.curr = 0;
	ag->tx_ring.dirty = 0;
	netdev_reset_queue(ag->ndev);

	/* setup max frame length */
	ag71xx_wr(ag, AG71XX_REG_MAC_MFL,
		  ag71xx_max_frame_len(ag->ndev->mtu));

	ag71xx_wr(ag, AG71XX_REG_RX_DESC, rx_ds);
	ag71xx_wr(ag, AG71XX_REG_TX_DESC, ag->tx_ring.descs_dma);
	ag71xx_wr(ag, AG71XX_REG_MII_CFG, mii_reg);

	ag71xx_hw_set_macaddr(ag, dev->dev_addr);
}

static void ag71xx_hw_start(struct ag71xx *ag)
{
	/* start RX engine */
	ag71xx_wr(ag, AG71XX_REG_RX_CTRL, RX_CTRL_RXE);

	/* enable interrupts */
	ag71xx_wr(ag, AG71XX_REG_INT_ENABLE, AG71XX_INT_INIT);

	netif_wake_queue(ag->ndev);
}

static void ag71xx_mac_config(struct phylink_config *config, unsigned int mode,
			      const struct phylink_link_state *state)
{
	struct ag71xx *ag = netdev_priv(to_net_dev(config->dev));

	if (phylink_autoneg_inband(mode))
		return;

	if (!ag71xx_is(ag, AR7100) && !ag71xx_is(ag, AR9130))
		ag71xx_fast_reset(ag);

	if (ag->tx_ring.desc_split) {
		ag->fifodata[2] &= 0xffff;
		ag->fifodata[2] |= ((2048 - ag->tx_ring.desc_split) / 4) << 16;
	}

	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG3, ag->fifodata[2]);
}

static void ag71xx_mac_validate(struct phylink_config *config,
			    unsigned long *supported,
			    struct phylink_link_state *state)
{
	struct ag71xx *ag = netdev_priv(to_net_dev(config->dev));
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	switch (state->interface) {
	case PHY_INTERFACE_MODE_NA:
		break;
	case PHY_INTERFACE_MODE_MII:
		if ((ag71xx_is(ag, AR9330) && ag->mac_idx == 0) ||
		    ag71xx_is(ag, AR9340) ||
		    ag71xx_is(ag, QCA9530) ||
		    (ag71xx_is(ag, QCA9550) && ag->mac_idx == 1))
			break;
		goto unsupported;
	case PHY_INTERFACE_MODE_GMII:
		if ((ag71xx_is(ag, AR9330) && ag->mac_idx == 1) ||
		    (ag71xx_is(ag, AR9340) && ag->mac_idx == 1) ||
		    (ag71xx_is(ag, QCA9530) && ag->mac_idx == 1))
			break;
		goto unsupported;
	case PHY_INTERFACE_MODE_SGMII:
		if (ag71xx_is(ag, QCA9550) && ag->mac_idx == 0)
			break;
		goto unsupported;
	case PHY_INTERFACE_MODE_RMII:
		if (ag71xx_is(ag, AR9340) && ag->mac_idx == 0)
			break;
		goto unsupported;
	case PHY_INTERFACE_MODE_RGMII:
		if ((ag71xx_is(ag, AR9340) && ag->mac_idx == 0) ||
		    (ag71xx_is(ag, QCA9550) && ag->mac_idx == 1))
			break;
		goto unsupported;
	default:
		goto unsupported;
	}

	phylink_set(mask, MII);

	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);
	phylink_set(mask, Autoneg);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);

	if (state->interface == PHY_INTERFACE_MODE_NA ||
	    state->interface == PHY_INTERFACE_MODE_SGMII ||
	    state->interface == PHY_INTERFACE_MODE_RGMII ||
	    state->interface == PHY_INTERFACE_MODE_GMII) {
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseX_Full);
	}

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);

	return;
unsupported:
	bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void ag71xx_mac_pcs_get_state(struct phylink_config *config,
				     struct phylink_link_state *state)
{
	state->link = 0;
}

static void ag71xx_mac_an_restart(struct phylink_config *config)
{
	/* Not Supported */
}

static void ag71xx_mac_link_down(struct phylink_config *config,
				 unsigned int mode, phy_interface_t interface)
{
	struct ag71xx *ag = netdev_priv(to_net_dev(config->dev));

	ag71xx_hw_stop(ag);
}

static void ag71xx_mac_link_up(struct phylink_config *config,
			       struct phy_device *phy,
			       unsigned int mode, phy_interface_t interface,
			       int speed, int duplex,
			       bool tx_pause, bool rx_pause)
{
	struct ag71xx *ag = netdev_priv(to_net_dev(config->dev));
	u32 cfg1, cfg2;
	u32 ifctl;
	u32 fifo5;

	cfg2 = ag71xx_rr(ag, AG71XX_REG_MAC_CFG2);
	cfg2 &= ~(MAC_CFG2_IF_1000 | MAC_CFG2_IF_10_100 | MAC_CFG2_FDX);
	cfg2 |= duplex ? MAC_CFG2_FDX : 0;

	ifctl = ag71xx_rr(ag, AG71XX_REG_MAC_IFCTL);
	ifctl &= ~(MAC_IFCTL_SPEED);

	fifo5 = ag71xx_rr(ag, AG71XX_REG_FIFO_CFG5);
	fifo5 &= ~FIFO_CFG5_BM;

	switch (speed) {
	case SPEED_1000:
		cfg2 |= MAC_CFG2_IF_1000;
		fifo5 |= FIFO_CFG5_BM;
		break;
	case SPEED_100:
		cfg2 |= MAC_CFG2_IF_10_100;
		ifctl |= MAC_IFCTL_SPEED;
		break;
	case SPEED_10:
		cfg2 |= MAC_CFG2_IF_10_100;
		break;
	default:
		return;
	}

	ag71xx_wr(ag, AG71XX_REG_MAC_CFG2, cfg2);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG5, fifo5);
	ag71xx_wr(ag, AG71XX_REG_MAC_IFCTL, ifctl);

	cfg1 = ag71xx_rr(ag, AG71XX_REG_MAC_CFG1);
	cfg1 &= ~(MAC_CFG1_TFC | MAC_CFG1_RFC);
	if (tx_pause)
		cfg1 |= MAC_CFG1_TFC;

	if (rx_pause)
		cfg1 |= MAC_CFG1_RFC;
	ag71xx_wr(ag, AG71XX_REG_MAC_CFG1, cfg1);

	ag71xx_hw_start(ag);
}

static const struct phylink_mac_ops ag71xx_phylink_mac_ops = {
	.validate = ag71xx_mac_validate,
	.mac_pcs_get_state = ag71xx_mac_pcs_get_state,
	.mac_an_restart = ag71xx_mac_an_restart,
	.mac_config = ag71xx_mac_config,
	.mac_link_down = ag71xx_mac_link_down,
	.mac_link_up = ag71xx_mac_link_up,
};

static int ag71xx_phylink_setup(struct ag71xx *ag)
{
	struct phylink *phylink;

	ag->phylink_config.dev = &ag->ndev->dev;
	ag->phylink_config.type = PHYLINK_NETDEV;

	phylink = phylink_create(&ag->phylink_config, ag->pdev->dev.fwnode,
				 ag->phy_if_mode, &ag71xx_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	ag->phylink = phylink;
	return 0;
}

static void ag71xx_ring_tx_clean(struct ag71xx *ag)
{
	struct ag71xx_ring *ring = &ag->tx_ring;
	int ring_mask = BIT(ring->order) - 1;
	u32 bytes_compl = 0, pkts_compl = 0;
	struct net_device *ndev = ag->ndev;

	while (ring->curr != ring->dirty) {
		struct ag71xx_desc *desc;
		u32 i = ring->dirty & ring_mask;

		desc = ag71xx_ring_desc(ring, i);
		if (!ag71xx_desc_empty(desc)) {
			desc->ctrl = 0;
			ndev->stats.tx_errors++;
		}

		if (ring->buf[i].tx.skb) {
			bytes_compl += ring->buf[i].tx.len;
			pkts_compl++;
			dev_kfree_skb_any(ring->buf[i].tx.skb);
		}
		ring->buf[i].tx.skb = NULL;
		ring->dirty++;
	}

	/* flush descriptors */
	wmb();

	netdev_completed_queue(ndev, pkts_compl, bytes_compl);
}

static void ag71xx_ring_tx_init(struct ag71xx *ag)
{
	struct ag71xx_ring *ring = &ag->tx_ring;
	int ring_size = BIT(ring->order);
	int ring_mask = ring_size - 1;
	int i;

	for (i = 0; i < ring_size; i++) {
		struct ag71xx_desc *desc = ag71xx_ring_desc(ring, i);

		desc->next = (u32)(ring->descs_dma +
			AG71XX_DESC_SIZE * ((i + 1) & ring_mask));

		desc->ctrl = DESC_EMPTY;
		ring->buf[i].tx.skb = NULL;
	}

	/* flush descriptors */
	wmb();

	ring->curr = 0;
	ring->dirty = 0;
	netdev_reset_queue(ag->ndev);
}

static void ag71xx_ring_rx_clean(struct ag71xx *ag)
{
	struct ag71xx_ring *ring = &ag->rx_ring;
	int ring_size = BIT(ring->order);
	int i;

	if (!ring->buf)
		return;

	for (i = 0; i < ring_size; i++)
		if (ring->buf[i].rx.rx_buf) {
			dma_unmap_single(&ag->pdev->dev,
					 ring->buf[i].rx.dma_addr,
					 ag->rx_buf_size, DMA_FROM_DEVICE);
			skb_free_frag(ring->buf[i].rx.rx_buf);
		}
}

static int ag71xx_buffer_size(struct ag71xx *ag)
{
	return ag->rx_buf_size +
	       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
}

static bool ag71xx_fill_rx_buf(struct ag71xx *ag, struct ag71xx_buf *buf,
			       int offset,
			       void *(*alloc)(unsigned int size))
{
	struct ag71xx_ring *ring = &ag->rx_ring;
	struct ag71xx_desc *desc;
	void *data;

	desc = ag71xx_ring_desc(ring, buf - &ring->buf[0]);

	data = alloc(ag71xx_buffer_size(ag));
	if (!data)
		return false;

	buf->rx.rx_buf = data;
	buf->rx.dma_addr = dma_map_single(&ag->pdev->dev, data, ag->rx_buf_size,
					  DMA_FROM_DEVICE);
	desc->data = (u32)buf->rx.dma_addr + offset;
	return true;
}

static int ag71xx_ring_rx_init(struct ag71xx *ag)
{
	struct ag71xx_ring *ring = &ag->rx_ring;
	struct net_device *ndev = ag->ndev;
	int ring_mask = BIT(ring->order) - 1;
	int ring_size = BIT(ring->order);
	unsigned int i;
	int ret;

	ret = 0;
	for (i = 0; i < ring_size; i++) {
		struct ag71xx_desc *desc = ag71xx_ring_desc(ring, i);

		desc->next = (u32)(ring->descs_dma +
			AG71XX_DESC_SIZE * ((i + 1) & ring_mask));

		netif_dbg(ag, rx_status, ndev, "RX desc at %p, next is %08x\n",
			  desc, desc->next);
	}

	for (i = 0; i < ring_size; i++) {
		struct ag71xx_desc *desc = ag71xx_ring_desc(ring, i);

		if (!ag71xx_fill_rx_buf(ag, &ring->buf[i], ag->rx_buf_offset,
					netdev_alloc_frag)) {
			ret = -ENOMEM;
			break;
		}

		desc->ctrl = DESC_EMPTY;
	}

	/* flush descriptors */
	wmb();

	ring->curr = 0;
	ring->dirty = 0;

	return ret;
}

static int ag71xx_ring_rx_refill(struct ag71xx *ag)
{
	struct ag71xx_ring *ring = &ag->rx_ring;
	int ring_mask = BIT(ring->order) - 1;
	int offset = ag->rx_buf_offset;
	unsigned int count;

	count = 0;
	for (; ring->curr - ring->dirty > 0; ring->dirty++) {
		struct ag71xx_desc *desc;
		unsigned int i;

		i = ring->dirty & ring_mask;
		desc = ag71xx_ring_desc(ring, i);

		if (!ring->buf[i].rx.rx_buf &&
		    !ag71xx_fill_rx_buf(ag, &ring->buf[i], offset,
					napi_alloc_frag))
			break;

		desc->ctrl = DESC_EMPTY;
		count++;
	}

	/* flush descriptors */
	wmb();

	netif_dbg(ag, rx_status, ag->ndev, "%u rx descriptors refilled\n",
		  count);

	return count;
}

static int ag71xx_rings_init(struct ag71xx *ag)
{
	struct ag71xx_ring *tx = &ag->tx_ring;
	struct ag71xx_ring *rx = &ag->rx_ring;
	int ring_size, tx_size;

	ring_size = BIT(tx->order) + BIT(rx->order);
	tx_size = BIT(tx->order);

	tx->buf = kcalloc(ring_size, sizeof(*tx->buf), GFP_KERNEL);
	if (!tx->buf)
		return -ENOMEM;

	tx->descs_cpu = dma_alloc_coherent(&ag->pdev->dev,
					   ring_size * AG71XX_DESC_SIZE,
					   &tx->descs_dma, GFP_KERNEL);
	if (!tx->descs_cpu) {
		kfree(tx->buf);
		tx->buf = NULL;
		return -ENOMEM;
	}

	rx->buf = &tx->buf[tx_size];
	rx->descs_cpu = ((void *)tx->descs_cpu) + tx_size * AG71XX_DESC_SIZE;
	rx->descs_dma = tx->descs_dma + tx_size * AG71XX_DESC_SIZE;

	ag71xx_ring_tx_init(ag);
	return ag71xx_ring_rx_init(ag);
}

static void ag71xx_rings_free(struct ag71xx *ag)
{
	struct ag71xx_ring *tx = &ag->tx_ring;
	struct ag71xx_ring *rx = &ag->rx_ring;
	int ring_size;

	ring_size = BIT(tx->order) + BIT(rx->order);

	if (tx->descs_cpu)
		dma_free_coherent(&ag->pdev->dev, ring_size * AG71XX_DESC_SIZE,
				  tx->descs_cpu, tx->descs_dma);

	kfree(tx->buf);

	tx->descs_cpu = NULL;
	rx->descs_cpu = NULL;
	tx->buf = NULL;
	rx->buf = NULL;
}

static void ag71xx_rings_cleanup(struct ag71xx *ag)
{
	ag71xx_ring_rx_clean(ag);
	ag71xx_ring_tx_clean(ag);
	ag71xx_rings_free(ag);

	netdev_reset_queue(ag->ndev);
}

static void ag71xx_hw_init(struct ag71xx *ag)
{
	ag71xx_hw_stop(ag);

	ag71xx_sb(ag, AG71XX_REG_MAC_CFG1, MAC_CFG1_SR);
	usleep_range(20, 30);

	reset_control_assert(ag->mac_reset);
	msleep(100);
	reset_control_deassert(ag->mac_reset);
	msleep(200);

	ag71xx_hw_setup(ag);

	ag71xx_dma_reset(ag);
}

static int ag71xx_hw_enable(struct ag71xx *ag)
{
	int ret;

	ret = ag71xx_rings_init(ag);
	if (ret)
		return ret;

	napi_enable(&ag->napi);
	ag71xx_wr(ag, AG71XX_REG_TX_DESC, ag->tx_ring.descs_dma);
	ag71xx_wr(ag, AG71XX_REG_RX_DESC, ag->rx_ring.descs_dma);
	netif_start_queue(ag->ndev);

	return 0;
}

static void ag71xx_hw_disable(struct ag71xx *ag)
{
	netif_stop_queue(ag->ndev);

	ag71xx_hw_stop(ag);
	ag71xx_dma_reset(ag);

	napi_disable(&ag->napi);
	del_timer_sync(&ag->oom_timer);

	ag71xx_rings_cleanup(ag);
}

static int ag71xx_open(struct net_device *ndev)
{
	struct ag71xx *ag = netdev_priv(ndev);
	unsigned int max_frame_len;
	int ret;

	ret = phylink_of_phy_connect(ag->phylink, ag->pdev->dev.of_node, 0);
	if (ret) {
		netif_err(ag, link, ndev, "phylink_of_phy_connect filed with err: %i\n",
			  ret);
		goto err;
	}

	max_frame_len = ag71xx_max_frame_len(ndev->mtu);
	ag->rx_buf_size =
		SKB_DATA_ALIGN(max_frame_len + NET_SKB_PAD + NET_IP_ALIGN);

	/* setup max frame length */
	ag71xx_wr(ag, AG71XX_REG_MAC_MFL, max_frame_len);
	ag71xx_hw_set_macaddr(ag, ndev->dev_addr);

	ret = ag71xx_hw_enable(ag);
	if (ret)
		goto err;

	phylink_start(ag->phylink);

	return 0;

err:
	ag71xx_rings_cleanup(ag);
	return ret;
}

static int ag71xx_stop(struct net_device *ndev)
{
	struct ag71xx *ag = netdev_priv(ndev);

	phylink_stop(ag->phylink);
	phylink_disconnect_phy(ag->phylink);
	ag71xx_hw_disable(ag);

	return 0;
}

static int ag71xx_fill_dma_desc(struct ag71xx_ring *ring, u32 addr, int len)
{
	int i, ring_mask, ndesc, split;
	struct ag71xx_desc *desc;

	ring_mask = BIT(ring->order) - 1;
	ndesc = 0;
	split = ring->desc_split;

	if (!split)
		split = len;

	while (len > 0) {
		unsigned int cur_len = len;

		i = (ring->curr + ndesc) & ring_mask;
		desc = ag71xx_ring_desc(ring, i);

		if (!ag71xx_desc_empty(desc))
			return -1;

		if (cur_len > split) {
			cur_len = split;

			/*  TX will hang if DMA transfers <= 4 bytes,
			 * make sure next segment is more than 4 bytes long.
			 */
			if (len <= split + 4)
				cur_len -= 4;
		}

		desc->data = addr;
		addr += cur_len;
		len -= cur_len;

		if (len > 0)
			cur_len |= DESC_MORE;

		/* prevent early tx attempt of this descriptor */
		if (!ndesc)
			cur_len |= DESC_EMPTY;

		desc->ctrl = cur_len;
		ndesc++;
	}

	return ndesc;
}

static netdev_tx_t ag71xx_hard_start_xmit(struct sk_buff *skb,
					  struct net_device *ndev)
{
	int i, n, ring_min, ring_mask, ring_size;
	struct ag71xx *ag = netdev_priv(ndev);
	struct ag71xx_ring *ring;
	struct ag71xx_desc *desc;
	dma_addr_t dma_addr;

	ring = &ag->tx_ring;
	ring_mask = BIT(ring->order) - 1;
	ring_size = BIT(ring->order);

	if (skb->len <= 4) {
		netif_dbg(ag, tx_err, ndev, "packet len is too small\n");
		goto err_drop;
	}

	dma_addr = dma_map_single(&ag->pdev->dev, skb->data, skb->len,
				  DMA_TO_DEVICE);

	i = ring->curr & ring_mask;
	desc = ag71xx_ring_desc(ring, i);

	/* setup descriptor fields */
	n = ag71xx_fill_dma_desc(ring, (u32)dma_addr,
				 skb->len & ag->dcfg->desc_pktlen_mask);
	if (n < 0)
		goto err_drop_unmap;

	i = (ring->curr + n - 1) & ring_mask;
	ring->buf[i].tx.len = skb->len;
	ring->buf[i].tx.skb = skb;

	netdev_sent_queue(ndev, skb->len);

	skb_tx_timestamp(skb);

	desc->ctrl &= ~DESC_EMPTY;
	ring->curr += n;

	/* flush descriptor */
	wmb();

	ring_min = 2;
	if (ring->desc_split)
		ring_min *= AG71XX_TX_RING_DS_PER_PKT;

	if (ring->curr - ring->dirty >= ring_size - ring_min) {
		netif_dbg(ag, tx_err, ndev, "tx queue full\n");
		netif_stop_queue(ndev);
	}

	netif_dbg(ag, tx_queued, ndev, "packet injected into TX queue\n");

	/* enable TX engine */
	ag71xx_wr(ag, AG71XX_REG_TX_CTRL, TX_CTRL_TXE);

	return NETDEV_TX_OK;

err_drop_unmap:
	dma_unmap_single(&ag->pdev->dev, dma_addr, skb->len, DMA_TO_DEVICE);

err_drop:
	ndev->stats.tx_dropped++;

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void ag71xx_oom_timer_handler(struct timer_list *t)
{
	struct ag71xx *ag = from_timer(ag, t, oom_timer);

	napi_schedule(&ag->napi);
}

static void ag71xx_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct ag71xx *ag = netdev_priv(ndev);

	netif_err(ag, tx_err, ndev, "tx timeout\n");

	schedule_delayed_work(&ag->restart_work, 1);
}

static void ag71xx_restart_work_func(struct work_struct *work)
{
	struct ag71xx *ag = container_of(work, struct ag71xx,
					 restart_work.work);

	rtnl_lock();
	ag71xx_hw_disable(ag);
	ag71xx_hw_enable(ag);

	phylink_stop(ag->phylink);
	phylink_start(ag->phylink);

	rtnl_unlock();
}

static int ag71xx_rx_packets(struct ag71xx *ag, int limit)
{
	struct net_device *ndev = ag->ndev;
	int ring_mask, ring_size, done = 0;
	unsigned int pktlen_mask, offset;
	struct sk_buff *next, *skb;
	struct ag71xx_ring *ring;
	struct list_head rx_list;

	ring = &ag->rx_ring;
	pktlen_mask = ag->dcfg->desc_pktlen_mask;
	offset = ag->rx_buf_offset;
	ring_mask = BIT(ring->order) - 1;
	ring_size = BIT(ring->order);

	netif_dbg(ag, rx_status, ndev, "rx packets, limit=%d, curr=%u, dirty=%u\n",
		  limit, ring->curr, ring->dirty);

	INIT_LIST_HEAD(&rx_list);

	while (done < limit) {
		unsigned int i = ring->curr & ring_mask;
		struct ag71xx_desc *desc = ag71xx_ring_desc(ring, i);
		int pktlen;
		int err = 0;

		if (ag71xx_desc_empty(desc))
			break;

		if ((ring->dirty + ring_size) == ring->curr) {
			WARN_ONCE(1, "RX out of ring");
			break;
		}

		ag71xx_wr(ag, AG71XX_REG_RX_STATUS, RX_STATUS_PR);

		pktlen = desc->ctrl & pktlen_mask;
		pktlen -= ETH_FCS_LEN;

		dma_unmap_single(&ag->pdev->dev, ring->buf[i].rx.dma_addr,
				 ag->rx_buf_size, DMA_FROM_DEVICE);

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += pktlen;

		skb = build_skb(ring->buf[i].rx.rx_buf, ag71xx_buffer_size(ag));
		if (!skb) {
			skb_free_frag(ring->buf[i].rx.rx_buf);
			goto next;
		}

		skb_reserve(skb, offset);
		skb_put(skb, pktlen);

		if (err) {
			ndev->stats.rx_dropped++;
			kfree_skb(skb);
		} else {
			skb->dev = ndev;
			skb->ip_summed = CHECKSUM_NONE;
			list_add_tail(&skb->list, &rx_list);
		}

next:
		ring->buf[i].rx.rx_buf = NULL;
		done++;

		ring->curr++;
	}

	ag71xx_ring_rx_refill(ag);

	list_for_each_entry_safe(skb, next, &rx_list, list)
		skb->protocol = eth_type_trans(skb, ndev);
	netif_receive_skb_list(&rx_list);

	netif_dbg(ag, rx_status, ndev, "rx finish, curr=%u, dirty=%u, done=%d\n",
		  ring->curr, ring->dirty, done);

	return done;
}

static int ag71xx_poll(struct napi_struct *napi, int limit)
{
	struct ag71xx *ag = container_of(napi, struct ag71xx, napi);
	struct ag71xx_ring *rx_ring = &ag->rx_ring;
	int rx_ring_size = BIT(rx_ring->order);
	struct net_device *ndev = ag->ndev;
	int tx_done, rx_done;
	u32 status;

	tx_done = ag71xx_tx_packets(ag, false);

	netif_dbg(ag, rx_status, ndev, "processing RX ring\n");
	rx_done = ag71xx_rx_packets(ag, limit);

	if (!rx_ring->buf[rx_ring->dirty % rx_ring_size].rx.rx_buf)
		goto oom;

	status = ag71xx_rr(ag, AG71XX_REG_RX_STATUS);
	if (unlikely(status & RX_STATUS_OF)) {
		ag71xx_wr(ag, AG71XX_REG_RX_STATUS, RX_STATUS_OF);
		ndev->stats.rx_fifo_errors++;

		/* restart RX */
		ag71xx_wr(ag, AG71XX_REG_RX_CTRL, RX_CTRL_RXE);
	}

	if (rx_done < limit) {
		if (status & RX_STATUS_PR)
			goto more;

		status = ag71xx_rr(ag, AG71XX_REG_TX_STATUS);
		if (status & TX_STATUS_PS)
			goto more;

		netif_dbg(ag, rx_status, ndev, "disable polling mode, rx=%d, tx=%d,limit=%d\n",
			  rx_done, tx_done, limit);

		napi_complete(napi);

		/* enable interrupts */
		ag71xx_int_enable(ag, AG71XX_INT_POLL);
		return rx_done;
	}

more:
	netif_dbg(ag, rx_status, ndev, "stay in polling mode, rx=%d, tx=%d, limit=%d\n",
		  rx_done, tx_done, limit);
	return limit;

oom:
	netif_err(ag, rx_err, ndev, "out of memory\n");

	mod_timer(&ag->oom_timer, jiffies + AG71XX_OOM_REFILL);
	napi_complete(napi);
	return 0;
}

static irqreturn_t ag71xx_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct ag71xx *ag;
	u32 status;

	ag = netdev_priv(ndev);
	status = ag71xx_rr(ag, AG71XX_REG_INT_STATUS);

	if (unlikely(!status))
		return IRQ_NONE;

	if (unlikely(status & AG71XX_INT_ERR)) {
		if (status & AG71XX_INT_TX_BE) {
			ag71xx_wr(ag, AG71XX_REG_TX_STATUS, TX_STATUS_BE);
			netif_err(ag, intr, ndev, "TX BUS error\n");
		}
		if (status & AG71XX_INT_RX_BE) {
			ag71xx_wr(ag, AG71XX_REG_RX_STATUS, RX_STATUS_BE);
			netif_err(ag, intr, ndev, "RX BUS error\n");
		}
	}

	if (likely(status & AG71XX_INT_POLL)) {
		ag71xx_int_disable(ag, AG71XX_INT_POLL);
		netif_dbg(ag, intr, ndev, "enable polling mode\n");
		napi_schedule(&ag->napi);
	}

	return IRQ_HANDLED;
}

static int ag71xx_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct ag71xx *ag = netdev_priv(ndev);

	ndev->mtu = new_mtu;
	ag71xx_wr(ag, AG71XX_REG_MAC_MFL,
		  ag71xx_max_frame_len(ndev->mtu));

	return 0;
}

static const struct net_device_ops ag71xx_netdev_ops = {
	.ndo_open		= ag71xx_open,
	.ndo_stop		= ag71xx_stop,
	.ndo_start_xmit		= ag71xx_hard_start_xmit,
	.ndo_do_ioctl		= phy_do_ioctl,
	.ndo_tx_timeout		= ag71xx_tx_timeout,
	.ndo_change_mtu		= ag71xx_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static const u32 ar71xx_addr_ar7100[] = {
	0x19000000, 0x1a000000,
};

static int ag71xx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct ag71xx_dcfg *dcfg;
	struct net_device *ndev;
	struct resource *res;
	const void *mac_addr;
	int tx_size, err, i;
	struct ag71xx *ag;

	if (!np)
		return -ENODEV;

	ndev = devm_alloc_etherdev(&pdev->dev, sizeof(*ag));
	if (!ndev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	dcfg = of_device_get_match_data(&pdev->dev);
	if (!dcfg)
		return -EINVAL;

	ag = netdev_priv(ndev);
	ag->mac_idx = -1;
	for (i = 0; i < ARRAY_SIZE(ar71xx_addr_ar7100); i++) {
		if (ar71xx_addr_ar7100[i] == res->start)
			ag->mac_idx = i;
	}

	if (ag->mac_idx < 0) {
		netif_err(ag, probe, ndev, "unknown mac idx\n");
		return -EINVAL;
	}

	ag->clk_eth = devm_clk_get(&pdev->dev, "eth");
	if (IS_ERR(ag->clk_eth)) {
		netif_err(ag, probe, ndev, "Failed to get eth clk.\n");
		return PTR_ERR(ag->clk_eth);
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	ag->pdev = pdev;
	ag->ndev = ndev;
	ag->dcfg = dcfg;
	ag->msg_enable = netif_msg_init(-1, AG71XX_DEFAULT_MSG_ENABLE);
	memcpy(ag->fifodata, dcfg->fifodata, sizeof(ag->fifodata));

	ag->mac_reset = devm_reset_control_get(&pdev->dev, "mac");
	if (IS_ERR(ag->mac_reset)) {
		netif_err(ag, probe, ndev, "missing mac reset\n");
		err = PTR_ERR(ag->mac_reset);
		goto err_free;
	}

	ag->mac_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!ag->mac_base) {
		err = -ENOMEM;
		goto err_free;
	}

	ndev->irq = platform_get_irq(pdev, 0);
	err = devm_request_irq(&pdev->dev, ndev->irq, ag71xx_interrupt,
			       0x0, dev_name(&pdev->dev), ndev);
	if (err) {
		netif_err(ag, probe, ndev, "unable to request IRQ %d\n",
			  ndev->irq);
		goto err_free;
	}

	ndev->netdev_ops = &ag71xx_netdev_ops;
	ndev->ethtool_ops = &ag71xx_ethtool_ops;

	INIT_DELAYED_WORK(&ag->restart_work, ag71xx_restart_work_func);
	timer_setup(&ag->oom_timer, ag71xx_oom_timer_handler, 0);

	tx_size = AG71XX_TX_RING_SIZE_DEFAULT;
	ag->rx_ring.order = ag71xx_ring_size_order(AG71XX_RX_RING_SIZE_DEFAULT);

	ndev->min_mtu = 68;
	ndev->max_mtu = dcfg->max_frame_len - ag71xx_max_frame_len(0);

	ag->rx_buf_offset = NET_SKB_PAD;
	if (!ag71xx_is(ag, AR7100) && !ag71xx_is(ag, AR9130))
		ag->rx_buf_offset += NET_IP_ALIGN;

	if (ag71xx_is(ag, AR7100)) {
		ag->tx_ring.desc_split = AG71XX_TX_RING_SPLIT;
		tx_size *= AG71XX_TX_RING_DS_PER_PKT;
	}
	ag->tx_ring.order = ag71xx_ring_size_order(tx_size);

	ag->stop_desc = dmam_alloc_coherent(&pdev->dev,
					    sizeof(struct ag71xx_desc),
					    &ag->stop_desc_dma, GFP_KERNEL);
	if (!ag->stop_desc) {
		err = -ENOMEM;
		goto err_free;
	}

	ag->stop_desc->data = 0;
	ag->stop_desc->ctrl = 0;
	ag->stop_desc->next = (u32)ag->stop_desc_dma;

	mac_addr = of_get_mac_address(np);
	if (!IS_ERR(mac_addr))
		memcpy(ndev->dev_addr, mac_addr, ETH_ALEN);
	if (IS_ERR(mac_addr) || !is_valid_ether_addr(ndev->dev_addr)) {
		netif_err(ag, probe, ndev, "invalid MAC address, using random address\n");
		eth_random_addr(ndev->dev_addr);
	}

	err = of_get_phy_mode(np, &ag->phy_if_mode);
	if (err) {
		netif_err(ag, probe, ndev, "missing phy-mode property in DT\n");
		goto err_free;
	}

	netif_napi_add(ndev, &ag->napi, ag71xx_poll, AG71XX_NAPI_WEIGHT);

	err = clk_prepare_enable(ag->clk_eth);
	if (err) {
		netif_err(ag, probe, ndev, "Failed to enable eth clk.\n");
		goto err_free;
	}

	ag71xx_wr(ag, AG71XX_REG_MAC_CFG1, 0);

	ag71xx_hw_init(ag);

	err = ag71xx_mdio_probe(ag);
	if (err)
		goto err_put_clk;

	platform_set_drvdata(pdev, ndev);

	err = ag71xx_phylink_setup(ag);
	if (err) {
		netif_err(ag, probe, ndev, "failed to setup phylink (%d)\n", err);
		goto err_mdio_remove;
	}

	err = register_netdev(ndev);
	if (err) {
		netif_err(ag, probe, ndev, "unable to register net device\n");
		platform_set_drvdata(pdev, NULL);
		goto err_mdio_remove;
	}

	netif_info(ag, probe, ndev, "Atheros AG71xx at 0x%08lx, irq %d, mode:%s\n",
		   (unsigned long)ag->mac_base, ndev->irq,
		   phy_modes(ag->phy_if_mode));

	return 0;

err_mdio_remove:
	ag71xx_mdio_remove(ag);
err_put_clk:
	clk_disable_unprepare(ag->clk_eth);
err_free:
	free_netdev(ndev);
	return err;
}

static int ag71xx_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ag71xx *ag;

	if (!ndev)
		return 0;

	ag = netdev_priv(ndev);
	unregister_netdev(ndev);
	ag71xx_mdio_remove(ag);
	clk_disable_unprepare(ag->clk_eth);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const u32 ar71xx_fifo_ar7100[] = {
	0x0fff0000, 0x00001fff, 0x00780fff,
};

static const u32 ar71xx_fifo_ar9130[] = {
	0x0fff0000, 0x00001fff, 0x008001ff,
};

static const u32 ar71xx_fifo_ar9330[] = {
	0x0010ffff, 0x015500aa, 0x01f00140,
};

static const struct ag71xx_dcfg ag71xx_dcfg_ar7100 = {
	.type = AR7100,
	.fifodata = ar71xx_fifo_ar7100,
	.max_frame_len = 1540,
	.desc_pktlen_mask = SZ_4K - 1,
	.tx_hang_workaround = false,
};

static const struct ag71xx_dcfg ag71xx_dcfg_ar7240 = {
	.type = AR7240,
	.fifodata = ar71xx_fifo_ar7100,
	.max_frame_len = 1540,
	.desc_pktlen_mask = SZ_4K - 1,
	.tx_hang_workaround = true,
};

static const struct ag71xx_dcfg ag71xx_dcfg_ar9130 = {
	.type = AR9130,
	.fifodata = ar71xx_fifo_ar9130,
	.max_frame_len = 1540,
	.desc_pktlen_mask = SZ_4K - 1,
	.tx_hang_workaround = false,
};

static const struct ag71xx_dcfg ag71xx_dcfg_ar9330 = {
	.type = AR9330,
	.fifodata = ar71xx_fifo_ar9330,
	.max_frame_len = 1540,
	.desc_pktlen_mask = SZ_4K - 1,
	.tx_hang_workaround = true,
};

static const struct ag71xx_dcfg ag71xx_dcfg_ar9340 = {
	.type = AR9340,
	.fifodata = ar71xx_fifo_ar9330,
	.max_frame_len = SZ_16K - 1,
	.desc_pktlen_mask = SZ_16K - 1,
	.tx_hang_workaround = true,
};

static const struct ag71xx_dcfg ag71xx_dcfg_qca9530 = {
	.type = QCA9530,
	.fifodata = ar71xx_fifo_ar9330,
	.max_frame_len = SZ_16K - 1,
	.desc_pktlen_mask = SZ_16K - 1,
	.tx_hang_workaround = true,
};

static const struct ag71xx_dcfg ag71xx_dcfg_qca9550 = {
	.type = QCA9550,
	.fifodata = ar71xx_fifo_ar9330,
	.max_frame_len = 1540,
	.desc_pktlen_mask = SZ_16K - 1,
	.tx_hang_workaround = true,
};

static const struct of_device_id ag71xx_match[] = {
	{ .compatible = "qca,ar7100-eth", .data = &ag71xx_dcfg_ar7100 },
	{ .compatible = "qca,ar7240-eth", .data = &ag71xx_dcfg_ar7240 },
	{ .compatible = "qca,ar7241-eth", .data = &ag71xx_dcfg_ar7240 },
	{ .compatible = "qca,ar7242-eth", .data = &ag71xx_dcfg_ar7240 },
	{ .compatible = "qca,ar9130-eth", .data = &ag71xx_dcfg_ar9130 },
	{ .compatible = "qca,ar9330-eth", .data = &ag71xx_dcfg_ar9330 },
	{ .compatible = "qca,ar9340-eth", .data = &ag71xx_dcfg_ar9340 },
	{ .compatible = "qca,qca9530-eth", .data = &ag71xx_dcfg_qca9530 },
	{ .compatible = "qca,qca9550-eth", .data = &ag71xx_dcfg_qca9550 },
	{ .compatible = "qca,qca9560-eth", .data = &ag71xx_dcfg_qca9550 },
	{}
};

static struct platform_driver ag71xx_driver = {
	.probe		= ag71xx_probe,
	.remove		= ag71xx_remove,
	.driver = {
		.name	= "ag71xx",
		.of_match_table = ag71xx_match,
	}
};

module_platform_driver(ag71xx_driver);
MODULE_LICENSE("GPL v2");
