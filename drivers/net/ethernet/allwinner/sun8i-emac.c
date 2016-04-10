/*
 * sun8i-h3-emac driver
 *
 * Copyright (C) 2015-2016 Corentin LABBE <clabbe.montjoie@gmail.com>
 *
 * This is the driver for Allwinner Ethernet MAC found in H3/A83T/A64 SoC
 *
 * This is a mono block driver that need to be splited:
 * - A classic ethernet MAC driver
 * - A PHY driver
 * - A clk driver
 *
 * TODO:
 * - NAPI
 * - conditional SG handling (useful ?)
 * - MAC filtering
 * - Jumbo frame
 * - features rx-all
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/gpio.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/reset.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/iopoll.h>
#include <linux/regulator/consumer.h>

#define SUN8I_EMAC_BASIC_CTL0 0x00
#define SUN8I_EMAC_BASIC_CTL1 0x04

#define SUN8I_EMAC_MDIO_CMD 0x48
#define SUN8I_EMAC_MDIO_DATA 0x4C

#define SUN8I_EMAC_RX_CTL0 0x24
#define SUN8I_EMAC_RX_CTL1 0x28

#define SUN8I_EMAC_TX_CTL0 0x10
#define SUN8I_EMAC_TX_CTL1 0x14

#define SUN8I_EMAC_TX_FLOW_CTL 0x1C

#define SUN8I_EMAC_RX_FRM_FLT 0x38

#define SUN8I_EMAC_INT_STA 0x08
#define SUN8I_EMAC_INT_EN 0x0C
#define SUN8I_EMAC_RGMII_STA 0xD0

#define SUN8I_EMAC_TX_DMA_STA 0xB0
#define SUN8I_EMAC_TX_CUR_DESC 0xB4
#define SUN8I_EMAC_TX_CUR_BUF 0xB8
#define SUN8I_EMAC_RX_DMA_STA 0xC0

#define MDIO_CMD_MII_BUSY	BIT(0)
#define MDIO_CMD_MII_WRITE	BIT(1)
#define MDIO_CMD_MII_PHY_REG_ADDR_MASK	GENMASK(8, 4)
#define MDIO_CMD_MII_PHY_REG_ADDR_SHIFT	4
#define MDIO_CMD_MII_PHY_ADDR_MASK	GENMASK(16, 12)
#define MDIO_CMD_MII_PHY_ADDR_SHIFT	12

#define SUN8I_EMAC_MACADDR_HI	0x50
#define SUN8I_EMAC_MACADDR_LO	0x54

#define SUN8I_EMAC_RX_DESC_LIST 0x34
#define SUN8I_EMAC_TX_DESC_LIST 0x20

#define SUN8I_EMAC_TX_DO_CRC (BIT(27) | BIT(28))
#define SUN8I_EMAC_RX_DO_CRC BIT(27)
#define SUN8I_EMAC_RX_STRIP_FCS BIT(28)

#define SUN8I_COULD_BE_USED_BY_DMA BIT(31)

struct ethtool_str {
	char name[ETH_GSTRING_LEN];
};

static const struct ethtool_str estats_str[] = {
	/* errors */
	{ "rx_payload_error" },
	{ "rx_CRC_error" },
	{ "rx_phy_error" },
	{ "rx_length_error" },
	{ "rx_col_error" },
	{ "rx_header_error" },
	{ "rx_overflow_error" },
	{ "rx_saf_error" },
	{ "rx_daf_error" },
	{ "rx_buf_error" },
	/* misc infos */
	{ "tx_stop_queue" },
	{ "rx_dma_ua" },
	{ "rx_dma_stop" },
	{ "tx_dma_ua" },
	{ "tx_dma_stop" },
	{ "rx_hw_csum" },
	{ "tx_hw_csum" },
	/* interrupts */
	{ "rx_early_int" },
	{ "tx_early_int" },
	{ "tx_underflow_int" },
	/* debug */
	{ "tx_used_desc" },
};

struct sun8i_emac_stats {
	u64 rx_payload_error;
	u64 rx_crc_error;
	u64 rx_phy_error;
	u64 rx_length_error;
	u64 rx_col_error;
	u64 rx_header_error;
	u64 rx_overflow_error;
	u64 rx_saf_fail;
	u64 rx_daf_fail;
	u64 rx_buf_error;

	u64 tx_stop_queue;
	u64 rx_dma_ua;
	u64 rx_dma_stop;
	u64 tx_dma_ua;
	u64 tx_dma_stop;
	u64 rx_hw_csum;
	u64 tx_hw_csum;

	u64 rx_early_int;
	u64 tx_early_int;
	u64 tx_underflow_int;
	u64 tx_used_desc;
};

/* The datasheet said that each descriptor can transfers up to 4096bytes
 * But latter, a register documentation reduce that value to 2048
 * Anyway using 2048 cause strange behaviours and even BSP driver use 2047
 */
#define DESC_BUF_MAX 2044
/* TODO check for a min value (ETH_DATA_LEN + ETH_HLEN + ). customizable value ? if not jumbo less value ?*/

/* MAGIC value for knowing if a descriptor is available or not */
#define DCLEAN (BIT(16) | BIT(14) | BIT(12) | BIT(10) | BIT(9))

/* Structure of DMA descriptor used by the hardware  */
struct dma_desc {
	u32 status; /* status of the descriptor */
	u32 st; /* Information on the frame */
	u32 buf_addr; /* physical address of the frame data */
	u32 next; /* physical address of next dma_desc */
} __packed __aligned(8);/* TODO check this aligned */

/* TODO flow_ctrl via ethtool */
static int flow_ctrl;
static int pause = 0x400;

/* Benched on OPIPC with 100M, setting more than 256 does not give any
 * perf boost
 */
static int nbdesc_tx = 256;
module_param(nbdesc_tx, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(nbdesc_tx, "Number of descriptors in the TX list");
static int nbdesc_rx = 128;
module_param(nbdesc_rx, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(nbdesc_rx, "Number of descriptors in the RX list");

struct sun8i_emac_priv {
	void __iomem *base;
	int irq;
	struct device *dev;
	struct net_device *ndev;
	struct mii_bus *mdio;
	spinlock_t lock;/* for adjust_link */
	spinlock_t tx_lock;/* control the access of transmit descriptors */
	int duplex;
	int speed;
	int link;
	int phy_interface;
	struct device_node *phy_node;
	struct clk *ahb_clk;
	struct clk *tx_clk;
	struct regulator *regulator;

	struct reset_control *rst_phy;
	struct reset_control *rst;

	struct dma_desc *dd_rx __aligned(4);
	dma_addr_t dd_rx_phy __aligned(4);
	struct dma_desc *dd_tx __aligned(4);
	dma_addr_t dd_tx_phy __aligned(4);
	struct sk_buff **rx_sk;
	struct sk_buff **tx_sk;
	int *tx_map;

	int tx_slot;
	int tx_dirty;
	int tx_dma_start;
	int rx_dirty;
	struct sun8i_emac_stats estats;
	u32 msg_enable;
};

/* TODO use % */
void rb_inc(int *p, int max)
{
	(*p)++;
	if (*p >= max)
		*p = 0;
}

/* Return the number of contiguous free descriptors
 * starting from tx_slot
 */
static int rb_tx_numfreedesc(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	if (priv->tx_slot < priv->tx_dirty)
		return priv->tx_dirty - priv->tx_slot;

	return (nbdesc_tx - priv->tx_slot) + priv->tx_dirty;
}

/* Allocate a skb in a DMA descriptor
 *
 * @i index of slot to fill
*/
static int sun8i_emac_rx_sk(struct net_device *ndev, int i)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct dma_desc *ddesc;
	struct sk_buff *sk;

	ddesc = priv->dd_rx + i;

	ddesc->st = 0;

	sk = netdev_alloc_skb_ip_align(ndev, DESC_BUF_MAX);
	if (!sk)
		return -ENOMEM;

	/* TODO should not happen */
	if (unlikely(priv->rx_sk[i]))
		dev_warn(priv->dev, "BUG: Leaking a skbuff\n");

	priv->rx_sk[i] = sk;

	ddesc->buf_addr = dma_map_single(priv->dev, sk->data,
					 DESC_BUF_MAX, DMA_FROM_DEVICE);
	if (dma_mapping_error(priv->dev, ddesc->buf_addr)) {
		dev_err(priv->dev, "ERROR: Cannot dma_map RX buffer\n");
		dev_kfree_skb(sk);
		return -EFAULT;
	}
	ddesc->st |= DESC_BUF_MAX;
	ddesc->status = BIT(31);

	return 0;
}

/* Set MAC address for slot index
 * @addr: the MAC address to set
 * @index: The index of slot where to set address.
 * The slot 0 is the main MACaddr
 */
static void sun8i_emac_set_macaddr(struct sun8i_emac_priv *priv,
				   const u8 *addr, int index)
{
	u32 v;

	if (!is_valid_ether_addr(addr)) {
		random_ether_addr(priv->ndev->dev_addr);
		addr = priv->ndev->dev_addr;
	}
	dev_info(priv->dev, "device MAC address slot %d %02x:%02x:%02x:%02x:%02x:%02x\n",
		 index, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	v = (addr[5] << 8) | addr[4];
	writel(v, priv->base + SUN8I_EMAC_MACADDR_HI + index * 8);
	v = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(v, priv->base + SUN8I_EMAC_MACADDR_LO + index * 8);
}

void sun8i_emac_set_link_mode(struct sun8i_emac_priv *priv)
{
	u32 v;

	v = readl(priv->base + SUN8I_EMAC_BASIC_CTL0);

	if (priv->duplex)
		v |= BIT(0);
	else
		v &= ~BIT(0);

	v &= ~0x0C;
	switch (priv->speed) {
	case 1000:
		break;
	case 100:
		v |= BIT(2);
		v |= BIT(3);
		break;
	case 10:
		v |= BIT(3);
		break;
	}

	writel(v, priv->base + SUN8I_EMAC_BASIC_CTL0);
}

/* TODO */
static void sun8i_emac_flow_ctrl(struct sun8i_emac_priv *priv, int duplex,
				 int fc, int pause)
{
	u32 flow = 0;

	dev_dbg(priv->dev, "%s %d %d %d\n", __func__, duplex, fc, pause);

	if (fc & BIT(0)) {
		flow = readl(priv->base + SUN8I_EMAC_RX_CTL0);
		flow |= BIT(16);
		writel(flow, priv->base + SUN8I_EMAC_RX_CTL0);
	}

	if (fc & BIT(1)) {
		flow = readl(priv->base + SUN8I_EMAC_TX_FLOW_CTL);
		flow |= BIT(0);
		writel(flow, priv->base + SUN8I_EMAC_TX_FLOW_CTL);
	}

	if (duplex) {
		flow = readl(priv->base + SUN8I_EMAC_TX_FLOW_CTL);
		flow |= (pause << 4);
		/* pause & BIT(4)*/
		writel(flow, priv->base + SUN8I_EMAC_TX_FLOW_CTL);
	}
}

/* Grab a frame into a skb from descriptor number i */
static int sun8i_emac_rx_from_ddesc(struct net_device *ndev, int i)
{
	struct sk_buff *skb;
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct dma_desc *ddesc = priv->dd_rx + i;
	int frame_len;
	int crc_checked = 0;

	if (ndev->features & NETIF_F_RXCSUM)
		crc_checked = 1;

	/* possible errors */
	/* TODO document properly the bit0/bit7 problem */
	/* the checksum or length of received frame's payload is wrong*/
	if (ddesc->status & BIT(0)) {
		priv->estats.rx_payload_error++;
		crc_checked = 0;
	}
	if (ddesc->status & BIT(1)) {
		priv->ndev->stats.rx_errors++;
		priv->ndev->stats.rx_crc_errors++;
		priv->estats.rx_crc_error++;
		goto discard_frame;
	}
	if ((ddesc->status & BIT(3))) {
		priv->ndev->stats.rx_errors++;
		priv->estats.rx_phy_error++;
		goto discard_frame;
	}
	if ((ddesc->status & BIT(4))) {
		priv->ndev->stats.rx_errors++;
		priv->ndev->stats.rx_length_errors++;
		priv->estats.rx_length_error++;
		goto discard_frame;
	}
	if ((ddesc->status & BIT(6))) {
		priv->ndev->stats.rx_errors++;
		priv->estats.rx_col_error++;
		goto discard_frame;
	}
	if ((ddesc->status & BIT(7))) {
		priv->estats.rx_header_error++;
		crc_checked = 0;
	}
	if ((ddesc->status & BIT(11))) {
		priv->ndev->stats.rx_over_errors++;
		priv->estats.rx_overflow_error++;
		goto discard_frame;
	}
	if ((ddesc->status & BIT(14))) {
		priv->ndev->stats.rx_errors++;
		priv->estats.rx_buf_error++;
		goto discard_frame;
	}

	if ((ddesc->status & BIT(9)) == 0) {
		/* begin of a Jumbo frame */
		dev_warn(priv->dev, "This should not happen\n");
		goto discard_frame;
	}
	frame_len = (ddesc->status >> 16) & 0x3FFF;
	if (!(ndev->features & NETIF_F_RXFCS))
		frame_len -= ETH_FCS_LEN;

	skb = priv->rx_sk[i];

	dev_dbg(priv->dev, "%s from %02d %pad len=%d status=%x st=%x\n",
		__func__, i, &ddesc, frame_len, ddesc->status, ddesc->st);

	skb_put(skb, frame_len);

	dma_unmap_single(priv->dev, ddesc->buf_addr, DESC_BUF_MAX,
			 DMA_FROM_DEVICE);
	skb->protocol = eth_type_trans(skb, priv->ndev);
	if (crc_checked) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		priv->estats.rx_hw_csum++;
	} else {
		skb->ip_summed = CHECKSUM_PARTIAL;
	}
	skb->dev = priv->ndev;

	priv->ndev->stats.rx_packets++;
	priv->ndev->stats.rx_bytes += frame_len;
	priv->rx_sk[i] = NULL;

	/* this frame is not the last */
	/* TODO jumbo frame
	if ((ddesc->status & BIT(8)) == 0) {
		dev_warn(priv->dev, "Multi frame not implemented currlen=%d\n",
			 frame_len);
		rb_inc(&i);
		ddesc = priv->dd_rx + i;
		frame_len = (ddesc->status >> 16) & 0x3FFF;
		dev_info(priv->dev, "Multi frame currlen=%d\n", frame_len);
	}
	*/

	sun8i_emac_rx_sk(ndev, i);

	netif_rx(skb);

	return 0;
	/* If the frame need to be dropped, we simply reuse the buffer */
discard_frame:
	ddesc->st = DESC_BUF_MAX;
	ddesc->status = BIT(31);
	return 0;
}

/* Cycle over RX DMA descriptors for finding frame to receive
 */
static int sun8i_emac_receive_all(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct dma_desc *ddesc;

	ddesc = priv->dd_rx + priv->rx_dirty;
	while (!(ddesc->status & BIT(31))) {
		sun8i_emac_rx_from_ddesc(ndev, priv->rx_dirty);
		rb_inc(&priv->rx_dirty, nbdesc_rx);
		ddesc = priv->dd_rx + priv->rx_dirty;
	};

	return 0;
}

/* iterate over dma_desc for finding completed xmit.
 * Called from interrupt context, so no need to spinlock tx
 *
 * The problem is: how to know that a descriptor is sent and not just in
 * preparation.
 * Need to have status=0 and st set but this is the state of first frame just
 * before setting the own-by-DMA bit.
 * The solution is to used the artificial value DCLEAN.
 */
static int sun8i_emac_complete_xmit(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct dma_desc *ddesc;
	int frame_len;

	do {
		ddesc = priv->dd_tx + priv->tx_dirty;

		if (ddesc->status & BIT(31)) {
			dev_info(priv->dev, "BUG: DMA still set %d %d\n",
				 priv->tx_dirty, priv->tx_slot);
			return 0;
		}

		if (ddesc->status == DCLEAN)
			return 0;

		if (ddesc->status == 0 && !ddesc->st) {
			dev_info(priv->dev, "BUG: reached the void %d %d\n",
				 priv->tx_dirty, priv->tx_slot);
			return 0;
		}

		/* TX_UNDERFLOW_ERR */
		if (ddesc->status & BIT(1))
			priv->ndev->stats.tx_errors++;
		/* TX_DEFER_ERR */
		if (ddesc->status & BIT(2))
			priv->ndev->stats.tx_errors++;
		/* BIT 6:3 numbers of collisions */
		if (ddesc->status & 0x78)
			priv->ndev->stats.collisions +=
				(ddesc->status & 0x78) >> 3;
		/* TX_COL_ERR_1 */
		if (ddesc->status & BIT(8))
			priv->ndev->stats.tx_errors++;
		/* TX_COL_ERR_0 */
		if (ddesc->status & BIT(9))
			priv->ndev->stats.tx_errors++;
		/* TX_CRS_ERR */
		if (ddesc->status & BIT(10))
			priv->ndev->stats.tx_carrier_errors++;
		/* TX_PAYLOAD_ERR */
		if (ddesc->status & BIT(12))
			priv->ndev->stats.tx_errors++;
		/* TX_LENGTH_ERR */
		if (ddesc->status & BIT(14))
			priv->ndev->stats.tx_errors++;
		/* TX_HEADER_ERR */
		if (ddesc->status & BIT(16))
			priv->ndev->stats.tx_errors++;
		frame_len = ddesc->st & 0x3FFF;
		/* we can free skb only on last frame */
		if (priv->tx_sk[priv->tx_dirty] && (ddesc->st & BIT(30))) {
			dev_kfree_skb_irq(priv->tx_sk[priv->tx_dirty]);
			priv->tx_sk[priv->tx_dirty] = NULL;
		}
		if (priv->tx_map[priv->tx_dirty] == 1)
			dma_unmap_single(priv->dev, ddesc->buf_addr,
					 frame_len, DMA_TO_DEVICE);
		else
			dma_unmap_page(priv->dev, ddesc->buf_addr,
				       frame_len, DMA_TO_DEVICE);
		ddesc->status = DCLEAN;
		ddesc->st = 0;

		rb_inc(&priv->tx_dirty, nbdesc_tx);
		ddesc = priv->dd_tx + priv->tx_dirty;
	} while (ddesc->st && !(ddesc->status & BIT(31)));

	if (netif_queue_stopped(ndev) &&
	    rb_tx_numfreedesc(ndev) > MAX_SKB_FRAGS + 1)
		netif_wake_queue(ndev);

	return 0;
}

static int sun8i_mdio_read(struct mii_bus *bus, int phy_addr, int phy_reg)
{
	struct net_device *ndev = bus->priv;
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	int err;
	u32 reg;

	err = readl_poll_timeout(priv->base + SUN8I_EMAC_MDIO_CMD, reg,
				 !(reg & MDIO_CMD_MII_BUSY), 100, 10000);
	if (err) {
		dev_err(priv->dev, "%s timeout %x\n", __func__, reg);
		return err;
	}

	reg &= ~MDIO_CMD_MII_WRITE;
	reg &= ~MDIO_CMD_MII_PHY_REG_ADDR_MASK;
	reg |= (phy_reg << MDIO_CMD_MII_PHY_REG_ADDR_SHIFT) &
		MDIO_CMD_MII_PHY_REG_ADDR_MASK;

	reg &= ~MDIO_CMD_MII_PHY_ADDR_MASK;

	reg |= (phy_addr << MDIO_CMD_MII_PHY_ADDR_SHIFT) &
		MDIO_CMD_MII_PHY_ADDR_MASK;

	reg |= MDIO_CMD_MII_BUSY;

	writel(reg, priv->base + SUN8I_EMAC_MDIO_CMD);

	err = readl_poll_timeout(priv->base + SUN8I_EMAC_MDIO_CMD, reg,
				 !(reg & MDIO_CMD_MII_BUSY), 100, 10000);

	if (err) {
		dev_err(priv->dev, "%s timeout %x\n", __func__, reg);
		return err;
	}

	return readl(priv->base + SUN8I_EMAC_MDIO_DATA);
}

static int sun8i_mdio_write(struct mii_bus *bus, int phy_addr, int phy_reg,
			    u16 data)
{
	struct net_device *ndev = bus->priv;
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	u32 reg;
	int err;

	err = readl_poll_timeout(priv->base + SUN8I_EMAC_MDIO_CMD, reg,
				 !(reg & MDIO_CMD_MII_BUSY), 100, 10000);
	if (err) {
		dev_err(priv->dev, "%s timeout %x\n", __func__, reg);
		return err;
	}

	reg &= ~MDIO_CMD_MII_PHY_REG_ADDR_MASK;
	reg |= (phy_reg << MDIO_CMD_MII_PHY_REG_ADDR_SHIFT) &
		MDIO_CMD_MII_PHY_REG_ADDR_MASK;

	reg &= ~MDIO_CMD_MII_PHY_ADDR_MASK;
	reg |= (phy_addr << MDIO_CMD_MII_PHY_ADDR_SHIFT) &
		MDIO_CMD_MII_PHY_ADDR_MASK;

	reg |= MDIO_CMD_MII_WRITE;
	reg |= MDIO_CMD_MII_BUSY;

	writel(reg, priv->base + SUN8I_EMAC_MDIO_CMD);
	writel(data, priv->base + SUN8I_EMAC_MDIO_DATA);
	dev_dbg(priv->dev, "%s %d %d %x %x\n", __func__, phy_addr, phy_reg,
		reg, data);

	err = readl_poll_timeout(priv->base + SUN8I_EMAC_MDIO_CMD, reg,
				 !(reg & MDIO_CMD_MII_BUSY), 100, 10000);
	if (err) {
		dev_err(priv->dev, "%s timeout %x\n", __func__, reg);
		return err;
	}

	return 0;
}

static int sun8i_emac_mdio_register(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(priv->dev);
	if (!bus) {
		netdev_err(ndev, "Failed to allocate new mdio bus\n");
		return -ENOMEM;
	}

	bus->name = dev_name(priv->dev);
	bus->read = &sun8i_mdio_read;
	bus->write = &sun8i_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%x", bus->name, 0);

	bus->parent = priv->dev;
	bus->priv = ndev;

	ret = of_mdiobus_register(bus, priv->dev->of_node);
	if (ret) {
		netdev_err(ndev, "Could not register as MDIO bus: %d\n", ret);
		return ret;
	}

	priv->mdio = bus;

	return 0;
}

static void sun8i_emac_adjust_link(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	unsigned long flags;
	int new_state = 0;

	dev_dbg(priv->dev, "%s link=%x duplex=%x speed=%x\n", __func__,
		phydev->link, phydev->duplex, phydev->speed);
	if (!phydev)
		return;

	spin_lock_irqsave(&priv->lock, flags);

	if (phydev->link) {
		if (phydev->duplex != priv->duplex) {
			new_state = 1;
			priv->duplex = phydev->duplex;
		}
		if (phydev->pause)
			sun8i_emac_flow_ctrl(priv, phydev->duplex,
					     flow_ctrl, pause);

		if (phydev->speed != priv->speed) {
			new_state = 1;
			priv->speed = phydev->speed;
		}

		if (priv->link == 0) {
			new_state = 1;
			priv->link = phydev->link;
		}

		dev_dbg(priv->dev, "%s new=%d link=%d pause=%d\n",
			__func__, new_state, priv->link, phydev->pause);
		if (new_state)
			sun8i_emac_set_link_mode(priv);
	} else if (priv->link != phydev->link) {
		new_state = 1;
		priv->link = 0;
		priv->speed = 0;
		priv->duplex = -1;
	}

	if (new_state)
		phy_print_status(phydev);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void sun8i_emac_set_mdc(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	unsigned long rate;
	u32 reg;

	rate = clk_get_rate(priv->ahb_clk);
	if (rate > 160000000)
		reg = 0x3 << 20; /* AHB / 128 */
	else if (rate > 80000000)
		reg = 0x2 << 20; /* AHB / 64 */
	else if (rate > 40000000)
		reg = 0x1 << 20; /* AHB / 32 */
	else
		reg = 0x0 << 20; /* AHB / 16 */
	dev_dbg(priv->dev, "MDC auto : %x\n", reg);
	writel(reg, priv->base + SUN8I_EMAC_MDIO_CMD);
}

#define SUN7I_GMAC_GMII_RGMII_RATE	125000000
#define SUN7I_GMAC_MII_RATE		25000000

static int sun8i_emac_init(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct device_node *node = priv->dev->of_node;
	const u8 *addr;
	int ret;

	/* Try to get MAC address from DT, or assign a random one */
	addr = of_get_mac_address(node);
	if (addr)
		ether_addr_copy(ndev->dev_addr, addr);
	else
		eth_hw_addr_random(ndev);

	priv->phy_interface = of_get_phy_mode(node);
	if (priv->phy_interface < 0) {
		netdev_err(ndev, "PHY interface mode node unspecified\n");
		return priv->phy_interface;
	}

	priv->phy_node = of_parse_phandle(node, "phy", 0);
	if (!priv->phy_node) {
		netdev_err(ndev, "no associated PHY\n");
		return -ENODEV;
	}

	ret = clk_prepare_enable(priv->ahb_clk);
	if (ret) {
		netdev_err(ndev, "Could not enable ahb clock");
		return ret;
	}

	if (priv->regulator) {
		ret = regulator_enable(priv->regulator);
		if (ret)
			goto err_disable_ahb_clk;
	}

	if (priv->rst) {
		ret = reset_control_deassert(priv->rst);
		if (ret) {
			netdev_err(ndev, "Could not deassert reset\n");
			goto err_regulator;
		}
	}

	sun8i_emac_set_mdc(ndev);

	/* The GMAC TX clock lines are configured by setting the clock
	 * rate, which then uses the auto-reparenting feature of the
	 * clock driver, and enabling/disabling the clock.
	 */
	if (priv->phy_interface == PHY_INTERFACE_MODE_RGMII) {
		clk_set_rate(priv->tx_clk, SUN7I_GMAC_GMII_RGMII_RATE);
		clk_prepare_enable(priv->tx_clk);
	} else {
		clk_set_rate(priv->tx_clk, SUN7I_GMAC_MII_RATE);
	}

	ret = sun8i_emac_mdio_register(ndev);
	if (ret)
		goto err_disable_tx_clk;

	return 0;

err_disable_tx_clk:
	if (priv->phy_interface == PHY_INTERFACE_MODE_RGMII)
		clk_disable_unprepare(priv->tx_clk);
	if (priv->rst)
		reset_control_assert(priv->rst);
err_regulator:
	if (priv->regulator)
		regulator_disable(priv->regulator);
err_disable_ahb_clk:
	clk_disable_unprepare(priv->ahb_clk);
	return ret;
}

static void sun8i_emac_uninit(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	mdiobus_unregister(priv->mdio);

	if (priv->phy_interface == PHY_INTERFACE_MODE_RGMII)
		clk_disable_unprepare(priv->tx_clk);

	if (priv->rst)
		reset_control_assert(priv->rst);

	if (priv->regulator)
		regulator_disable(priv->regulator);

	clk_disable_unprepare(priv->ahb_clk);
}

static int sun8i_emac_mdio_probe(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = NULL;
	int err;

	phydev = of_phy_connect(ndev, priv->phy_node, &sun8i_emac_adjust_link,
				0, priv->phy_interface);

	if (IS_ERR(phydev)) {
		err = PTR_ERR(phydev);
		netdev_err(ndev, "Could not attach to PHY: %d\n", err);
		return err;
	}

	phy_attached_info(phydev);

	/* mask with MAC supported features */
	phydev->supported &= PHY_GBIT_FEATURES;
	phydev->advertising = phydev->supported;

	priv->link = 0;
	priv->speed = 0;
	priv->duplex = -1;

	return 0;
}

static int sun8i_emac_open(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	int err;
	u32 v, dr, dt, vtxphy;
	struct dma_desc *ddesc;
	int i;
	int timeout = 0;

	if (nbdesc_tx < MAX_SKB_FRAGS + 1) {
		/* TODO */
		dev_err(priv->dev, "The number of TX descriptors is too low");
		return -EINVAL;
	}

	err = sun8i_emac_mdio_probe(ndev);
	if (err)
		return err;

	/* Do SOFT RST */
	v = readl(priv->base + SUN8I_EMAC_BASIC_CTL1);
	writel(v | 0x01, priv->base + SUN8I_EMAC_BASIC_CTL1);

	/*TODO err = readl_poll_timeout(priv->base + SUN8I_EMAC_BASIC_CTL1, v,
				 !(v & 0x01), 100, 10000);*/
	/* wait for reset to be ended */
	do {
		v = readl(priv->base + SUN8I_EMAC_BASIC_CTL1);
	} while ((v & 0x01) != 0 && timeout++ < 50);
	if (timeout >= 50) {
		dev_err(priv->dev, "EMAC reset timeout\n");
		err = -EFAULT;
		goto err_emac_timeout;
	}

	sun8i_emac_set_mdc(ndev);

	/* DMA */
	v = (8 << 24);/* burst len */
	writel(v, priv->base + SUN8I_EMAC_BASIC_CTL1);
#define RX_INT                  BIT(8)
#define TX_INT                  BIT(0)
#define TX_UNF_INT              BIT(4)
	writel(RX_INT | TX_INT | TX_UNF_INT, priv->base + SUN8I_EMAC_INT_EN);

	v = readl(priv->base + SUN8I_EMAC_TX_CTL0);
	/* TX_FRM_LEN_CL */
	/*v |= BIT(30);*/
	writel(v, priv->base + SUN8I_EMAC_TX_CTL0);

	v = readl(priv->base + SUN8I_EMAC_RX_CTL0);
	/* CHECK_CRC */
	if (ndev->features & NETIF_F_RXCSUM)
		v |= SUN8I_EMAC_RX_DO_CRC;
	else
		v &= ~SUN8I_EMAC_RX_DO_CRC;
	/* STRIP_FCS */
	if (ndev->features & NETIF_F_RXFCS)
		v &= ~SUN8I_EMAC_RX_STRIP_FCS;
	else
		v |= SUN8I_EMAC_RX_STRIP_FCS;
	/* JUMBO_FRM_EN */
	v |= BIT(29);
	writel(v, priv->base + SUN8I_EMAC_RX_CTL0);

	v = readl(priv->base + SUN8I_EMAC_TX_CTL1);
	/* TX_MD Transmission starts after a full frame located in TX DMA FIFO*/
	v |= BIT(1);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL1);

	v = readl(priv->base + SUN8I_EMAC_RX_CTL1);
	/* RX_MD RX DMA reads data from RX DMA FIFO to host memory after a
	 * complete frame has been written to RX DMA FIFO
	*/
	v |= BIT(1);
	writel(v, priv->base + SUN8I_EMAC_RX_CTL1);

	sun8i_emac_set_macaddr(priv, ndev->dev_addr, 0);

	priv->rx_sk = kcalloc(nbdesc_rx, sizeof(struct sk_buff *), GFP_KERNEL);
	if (!priv->rx_sk) {
		err = -ENOMEM;
		goto rx_sk_error;
	}
	priv->tx_sk = kcalloc(nbdesc_tx, sizeof(struct sk_buff *), GFP_KERNEL);
	if (!priv->tx_sk) {
		err = -ENOMEM;
		goto tx_sk_error;
	}
	priv->tx_map = kcalloc(nbdesc_tx, sizeof(int), GFP_KERNEL);
	if (!priv->tx_map) {
		err = -ENOMEM;
		goto tx_map_error;
	}

	priv->dd_rx = dma_alloc_coherent(priv->dev,
			nbdesc_rx * sizeof(struct dma_desc),
			&priv->dd_rx_phy,
			GFP_KERNEL);
	if (!priv->dd_rx) {
		dev_err(priv->dev, "ERROR: cannot DMA RX");
		err = -ENOMEM;
		goto dma_rx_error;
	}
	memset(priv->dd_rx, 0, nbdesc_rx * sizeof(struct dma_desc));
	ddesc = priv->dd_rx;
	for (i = 0; i < nbdesc_rx; i++) {
		sun8i_emac_rx_sk(ndev, i);
		ddesc->next = (u32)priv->dd_rx_phy + (i + 1)
			* sizeof(struct dma_desc);
		ddesc++;
	}
	/* last descriptor point back to first one */
	ddesc--;
	ddesc->next = (u32)priv->dd_rx_phy;

	priv->dd_tx = dma_alloc_coherent(priv->dev,
			nbdesc_tx * sizeof(struct dma_desc),
			&priv->dd_tx_phy,
			GFP_KERNEL);
	if (!priv->dd_tx) {
		dev_err(priv->dev, "ERROR: cannot DMA TX");
		err = -ENOMEM;
		goto dma_tx_error;
	}
	memset(priv->dd_tx, 0, nbdesc_tx * sizeof(struct dma_desc));
	ddesc = priv->dd_tx;
	for (i = 0; i < nbdesc_tx; i++) {
		ddesc->status = DCLEAN;
		ddesc->st = 0;
		ddesc->next = (u32)(priv->dd_tx_phy + (i + 1)
			* sizeof(struct dma_desc));
		ddesc++;
	}
	/* last descriptor point back to first one */
	ddesc--;
	ddesc->next = (u32)priv->dd_tx_phy;
	i--;

	if (ndev->phydev)
		phy_start(ndev->phydev);

	writel(priv->dd_rx_phy, priv->base + SUN8I_EMAC_RX_DESC_LIST);
	/* start RX DMA */
	v = readl(priv->base + SUN8I_EMAC_RX_CTL1);
	v |= BIT(30);
	writel(v, priv->base + SUN8I_EMAC_RX_CTL1);

	writel(priv->dd_tx_phy, priv->base + SUN8I_EMAC_TX_DESC_LIST);
	v = readl(priv->base + SUN8I_EMAC_TX_CTL1);
	v |= BIT(30);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL1);

	/* activate transmitter */
	v = readl(priv->base + SUN8I_EMAC_TX_CTL0);
	v |= (1 << 31);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL0);

	/* activate receiver */
	v = readl(priv->base + SUN8I_EMAC_RX_CTL0);
	v |= (1 << 31);
	writel(v, priv->base + SUN8I_EMAC_RX_CTL0);

	/* TODO */
	writel(0x3FCF, priv->base + SUN8I_EMAC_INT_STA);
	netif_start_queue(ndev);

	v = readl(priv->base + SUN8I_EMAC_RX_DESC_LIST);
	vtxphy = readl(priv->base + SUN8I_EMAC_TX_DESC_LIST);
	dt = readl(priv->base + SUN8I_EMAC_TX_DMA_STA);
	dr = readl(priv->base + SUN8I_EMAC_RX_DMA_STA);

	return 0;
dma_tx_error:
	dma_free_coherent(priv->dev, nbdesc_rx * sizeof(struct dma_desc),
			  priv->dd_rx, priv->dd_rx_phy);
dma_rx_error:
	kfree(priv->tx_map);
tx_map_error:
	kfree(priv->tx_sk);
tx_sk_error:
	kfree(priv->rx_sk);
rx_sk_error:
err_emac_timeout:
	phy_disconnect(ndev->phydev);
	return err;
}

static int sun8i_emac_stop(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	dev_dbg(priv->dev, "%s\n", __func__);

	/* Stop receiver */
	writel(0, priv->base + SUN8I_EMAC_RX_CTL0);

	/* TODO clean TX ring buffer */

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	phy_stop(ndev->phydev);
	phy_disconnect(ndev->phydev);

	return 0;
}

static netdev_tx_t sun8i_emac_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	struct dma_desc *ddesc;
	struct dma_desc *first;
	int i = 0, rbd_first;
	unsigned int len, fraglen;
	u32 v;
	int n;
	int nf;
	const skb_frag_t *frag;
	int do_csum = 0;

	len = skb_headlen(skb);
	/* TODO check for ETH_ZLEN ?? */
	n = skb_shinfo(skb)->nr_frags;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		do_csum = 1;
		priv->estats.tx_hw_csum++;
	}
	dev_dbg(priv->dev, "%s len=%u skblen=%u %x\n", __func__, len, skb->len,
		(skb->ip_summed == CHECKSUM_PARTIAL));

	spin_lock(&priv->tx_lock);

	/* check for contigous space
	 * We need at least 1(skb->data) + n(numfrags) + 1(one clean slot)
	 */
	if (rb_tx_numfreedesc(ndev) < n + 2) {
		dev_err_ratelimited(priv->dev, "BUG!: TX is full %d %d\n",
				    priv->tx_dirty, priv->tx_slot);
		netif_stop_queue(ndev);
		spin_unlock(&priv->tx_lock);
		return NETDEV_TX_BUSY;
	}
	i = priv->tx_slot;

	ddesc = priv->dd_tx + i;
	first = priv->dd_tx + i;
	rbd_first = i;

	priv->tx_slot = (i + 1 + n) % nbdesc_tx;

	ddesc->buf_addr = dma_map_single(priv->dev, skb->data, len,
					 DMA_TO_DEVICE);
	if (dma_mapping_error(priv->dev, ddesc->buf_addr)) {
		dev_err(priv->dev, "ERROR: Cannot dmamap buf\n");
		goto xmit_error;
	}
	priv->tx_map[i] = 1;
	priv->tx_sk[i] = skb;
	priv->ndev->stats.tx_packets++;
	priv->ndev->stats.tx_bytes += len;

	ddesc->st = len;
	/* undocumented bit that make it works TODO */
	ddesc->st |= BIT(24);
	if (do_csum)
		ddesc->st |= SUN8I_EMAC_TX_DO_CRC;

	/* handle fragmented skb, one descriptor per fragment  */
	for (nf = 0; nf < n; nf++) {
		frag = &skb_shinfo(skb)->frags[nf];
		rb_inc(&i, nbdesc_tx);
		priv->tx_sk[i] = skb;
		ddesc = priv->dd_tx + i;
		fraglen = skb_frag_size(frag);
		ddesc->st = fraglen;
		priv->ndev->stats.tx_bytes += fraglen;
		ddesc->st |= BIT(24);
		if (do_csum)
			ddesc->st |= SUN8I_EMAC_TX_DO_CRC;

		ddesc->buf_addr = skb_frag_dma_map(priv->dev, frag, 0,
				fraglen, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, ddesc->buf_addr)) {
			dev_err(priv->dev, "DMA MAP ERROR\n");
			goto xmit_error;
		}
		priv->tx_map[i] = 2;
		ddesc->status = BIT(31);
	}

	/* frame end */
	ddesc->st |= BIT(30);
	/* We want an interrupt after transmission */
	ddesc->st |= BIT(31);

	rb_inc(&i, nbdesc_tx);

	/* frame begin */
	first->st |= BIT(29);
	first->status = BIT(31);
	/*smp_wmb();*/
	priv->tx_slot = i;

	/* TODO trying to optimize this seems to lead to errors */
/*	if (priv->tx_dma_start == 0) {*/
		v = readl(priv->base + SUN8I_EMAC_TX_CTL1);
		/* TX DMA START */
		v |= BIT(31);/* mandatory */
		/* Start an run DMA */
		v |= BIT(30);/* TODO do a define */
		writel(v, priv->base + SUN8I_EMAC_TX_CTL1);
		priv->tx_dma_start = 1;
/*	}*/

	if (rb_tx_numfreedesc(ndev) < MAX_SKB_FRAGS + 1) {
		netif_stop_queue(ndev);
		priv->estats.tx_stop_queue++;
	}
	priv->estats.tx_used_desc = rb_tx_numfreedesc(ndev);

	spin_unlock(&priv->tx_lock);

	return NETDEV_TX_OK;

xmit_error:
	/* TODO destroy skb and return TX OK Documentation/DMA-API-HOWTO.txt */
	/* clean descritors from rbd_first to i */
	ddesc->st = 0;
	ddesc->status = DCLEAN;
	do {
		ddesc = priv->dd_tx + rbd_first;
		ddesc->st = 0;
		ddesc->status = DCLEAN;
		rb_inc(&rbd_first, nbdesc_tx);
	} while (rbd_first != i);
	spin_unlock(&priv->tx_lock);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int sun8i_emac_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	int max_mtu;

	dev_info(priv->dev, "%s set MTU to %d\n", __func__, new_mtu);

	if (netif_running(ndev)) {
		dev_err(priv->dev, "%s: must be stopped to change its MTU\n",
			ndev->name);
		return -EBUSY;
	}

	max_mtu = SKB_MAX_HEAD(NET_SKB_PAD + NET_IP_ALIGN);

	if ((new_mtu < 68) || (new_mtu > max_mtu)) {
		dev_err(priv->dev, "%s: invalid MTU, max MTU is: %d\n",
			ndev->name, max_mtu);
		return -EINVAL;
	}

	ndev->mtu = new_mtu;
	netdev_update_features(ndev);
	return 0;
}

static netdev_features_t sun8i_emac_fix_features(struct net_device *ndev,
						 netdev_features_t features)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	dev_dbg(priv->dev, "%s %llx\n", __func__, features);
	return features;
}

static int sun8i_emac_set_features(struct net_device *ndev,
				   netdev_features_t features)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	u32 v;

	v = readl(priv->base + SUN8I_EMAC_BASIC_CTL0);
	if (features & NETIF_F_LOOPBACK && netif_running(ndev)) {
		netif_info(priv, hw, ndev, "Set loopback features");
		v |= BIT(1);
	} else {
		netif_info(priv, hw, ndev, "Unset loopback features");
		v &= ~BIT(1);
	}
	writel(v, priv->base + SUN8I_EMAC_BASIC_CTL0);

	/* TODO do func setup_receiver */
	v = readl(priv->base + SUN8I_EMAC_RX_CTL0);
	if (features & NETIF_F_RXCSUM) {
		v |= SUN8I_EMAC_RX_DO_CRC;
		netif_info(priv, hw, ndev, "Doing RX CRC check by hardware");
	} else {
		v &= ~SUN8I_EMAC_RX_DO_CRC;
		netif_info(priv, hw, ndev, "No RX CRC check by hardware");
	}
	if (features & NETIF_F_RXFCS) {
		v &= ~SUN8I_EMAC_RX_STRIP_FCS;
		netif_info(priv, hw, ndev, "Keep FCS");
	} else {
		v |= SUN8I_EMAC_RX_STRIP_FCS;
		netif_info(priv, hw, ndev, "Strip FCS");
	}
	writel(v, priv->base + SUN8I_EMAC_RX_CTL0);

	dev_dbg(priv->dev, "%s %llx %x\n", __func__, features, v);

	return 0;
}

static void sun8i_emac_set_rx_mode(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	u32 v = 0;
	int i = 0;
	struct netdev_hw_addr *ha;

	/* Receive all multicast frames */
	v |= BIT(16);
	/* Receive all control frames */
	v |= BIT(13);
	if (ndev->flags & IFF_PROMISC)
		v |= BIT(1);
	if (netdev_uc_count(ndev) > 7) {
		v |= BIT(1);
	} else {
		netdev_for_each_uc_addr(ha, ndev) {
			i++;
			sun8i_emac_set_macaddr(priv, ha->addr, i);
		}
	}
	writel(v, priv->base + SUN8I_EMAC_RX_FRM_FLT);
}

/* TODO Need a way to test that case */
static void sun8i_emac_tx_timeout(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	int i;
	struct dma_desc *ddesc;
	u32 v;

	dev_info(priv->dev, "%s\n", __func__);
	/* TODO reset/re-init all (see stmmac)*/
	netif_stop_queue(ndev);

	spin_lock(&priv->tx_lock);

	v = readl(priv->base + SUN8I_EMAC_TX_CUR_DESC);
	dev_info(priv->dev, "SUN8I_EMAC_TX_CUR_DESC %x dirty=%d clean=%d\n",
		 v, priv->tx_dirty, priv->tx_slot);
	priv->tx_slot = (v - priv->dd_tx_phy) / 16;
	dev_info(priv->dev, "Setting clean to %d\n", priv->tx_slot);
	v = readl(priv->base + SUN8I_EMAC_TX_DMA_STA);
	dev_info(priv->dev, "SUN8I_EMAC_TX_DMA_STA %x\n", v);

	v = readl(priv->base + SUN8I_EMAC_TX_CTL0);
	v &= ~BIT(31);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL0);

	v = readl(priv->base + SUN8I_EMAC_TX_CTL1);
	v &= ~BIT(31);
	v &= ~BIT(30);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL1);
	priv->tx_dma_start = 0;

	for (i = 0; i < nbdesc_tx; i++) {
		ddesc = priv->dd_tx + i;
		dev_info(priv->dev, "desc%02d %x %x %x:\n", i, ddesc->status,
			 ddesc->st, priv->dd_tx_phy + i * 16);
		if (priv->tx_sk[i] && (ddesc->st & BIT(30)))
			dev_kfree_skb_any(priv->tx_sk[i]);
		ddesc->st = 0;
		ddesc->status = DCLEAN;
	}
	v = readl(priv->base + SUN8I_EMAC_TX_CTL0);
	v |= BIT(31);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL0);

	v = readl(priv->base + SUN8I_EMAC_TX_CTL1);
	v |= BIT(31);
	v |= BIT(30);
	writel(v, priv->base + SUN8I_EMAC_TX_CTL1);

	spin_unlock(&priv->tx_lock);

	netdev_reset_queue(ndev);

	ndev->stats.tx_errors++;
	netif_wake_queue(ndev);
}

static int sun8i_emac_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, rq, cmd);
}

static void sun8i_emac_poll_controller(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	dev_info(priv->dev, "%s\n", __func__);
}

static int sun8i_emac_check_if_running(struct net_device *ndev)
{
	if (!netif_running(ndev))
		return -EBUSY;
	return 0;
}

static int sun8i_emac_get_sset_count(struct net_device *ndev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(estats_str);
	}
	return -EOPNOTSUPP;
}

static int sun8i_emac_ethtool_get_settings(struct net_device *ndev,
					   struct ethtool_cmd *cmd)
{
	struct phy_device *phy = ndev->phydev;
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	if (!phy) {
		netdev_err(ndev, "%s: %s: PHY is not registered\n",
			   __func__, ndev->name);
		return -ENODEV;
	}

	if (!netif_running(ndev)) {
		dev_err(priv->dev, "interface disabled: we cannot track link speed / duplex setting\n");
		return -EBUSY;
	}

	cmd->transceiver = XCVR_INTERNAL;
	return phy_ethtool_gset(phy, cmd);
}

static int sun8i_emac_ethtool_set_settings(struct net_device *ndev,
					   struct ethtool_cmd *cmd)
{
	struct phy_device *phy = ndev->phydev;

	return phy_ethtool_sset(phy, cmd);
}

static void sun8i_emac_ethtool_getdrvinfo(struct net_device *ndev,
					  struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, "sun8i_emac", sizeof(info->driver));
	strcpy(info->version, "00");
	info->fw_version[0] = '\0';
}

static void sun8i_emac_ethtool_stats(struct net_device *ndev,
				     struct ethtool_stats *dummy, u64 *data)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	memcpy(data, &priv->estats,
	       sun8i_emac_get_sset_count(ndev, ETH_SS_STATS) * sizeof(u64));
}

static void sun8i_emac_ethtool_strings(struct net_device *dev, u32 stringset,
				       u8 *buffer)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buffer, &estats_str,
		       sun8i_emac_get_sset_count(dev, ETH_SS_STATS) *
			sizeof(struct ethtool_str));
		break;
	}
}

static u32 sun8i_emac_ethtool_getmsglevel(struct net_device *ndev)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	return priv->msg_enable;
}

static void sun8i_emac_ethtool_setmsglevel(struct net_device *ndev, u32 level)
{
	struct sun8i_emac_priv *priv = netdev_priv(ndev);

	priv->msg_enable = level;
}

static const struct ethtool_ops sun8i_emac_ethtool_ops = {
	.begin = sun8i_emac_check_if_running,
	.get_settings = sun8i_emac_ethtool_get_settings,
	.set_settings = sun8i_emac_ethtool_set_settings,
	.get_link = ethtool_op_get_link,
	.get_pauseparam = NULL,
	.set_pauseparam = NULL,
	.get_ethtool_stats = sun8i_emac_ethtool_stats,
	.get_strings = sun8i_emac_ethtool_strings,
	.get_wol = NULL,
	.set_wol = NULL,
	.get_sset_count = sun8i_emac_get_sset_count,
	.get_drvinfo = sun8i_emac_ethtool_getdrvinfo,
	.get_msglevel = sun8i_emac_ethtool_getmsglevel,
	.set_msglevel = sun8i_emac_ethtool_setmsglevel,
};

static const struct net_device_ops sun8i_emac_netdev_ops = {
	.ndo_init = sun8i_emac_init,
	.ndo_uninit = sun8i_emac_uninit,
	.ndo_open = sun8i_emac_open,
	.ndo_start_xmit = sun8i_emac_xmit,
	.ndo_stop = sun8i_emac_stop,
	.ndo_change_mtu = sun8i_emac_change_mtu,
	.ndo_fix_features = sun8i_emac_fix_features,
	.ndo_set_rx_mode = sun8i_emac_set_rx_mode,
	.ndo_tx_timeout = sun8i_emac_tx_timeout,
	.ndo_do_ioctl = sun8i_emac_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = sun8i_emac_poll_controller,
#endif
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_set_features = sun8i_emac_set_features,
};

static irqreturn_t sun8i_emac_dma_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct sun8i_emac_priv *priv = netdev_priv(ndev);
	u32 v, u;

	v = readl(priv->base + SUN8I_EMAC_INT_STA);

	netif_info(priv, intr, ndev, "%s %x\n", __func__, v);

	/* When this bit is asserted, a frame transmission is completed. */
	if (v & BIT(0))
		sun8i_emac_complete_xmit(ndev);

	/* When this bit is asserted, the TX DMA FSM is stopped. */
	if (v & BIT(1)) {
		priv->tx_dma_start = 0;
		priv->estats.tx_dma_stop++;
	}

	/* When this asserted, the TX DMA can not acquire next TX descriptor
	 * and TX DMA FSM is suspended.
	*/
	if (v & BIT(2)) {
		priv->tx_dma_start = 0;
		priv->estats.tx_dma_ua++;
	}

	if (v & BIT(3))
		dev_info(priv->dev, "Unhandled interrupt TX TIMEOUT\n");

	if (v & BIT(4)) {
		dev_dbg(priv->dev, "Unhandled interrupt TX underflow\n");
		priv->estats.tx_underflow_int++;
	}

	/* When this bit asserted , the frame is transmitted to FIFO totally. */
	if (v & BIT(5)) {
		dev_dbg(priv->dev, "Unhandled interrupt TX_EARLY_INT\n");
		priv->estats.tx_early_int++;
	}

	/* When this bit is asserted, a frame reception is completed  */
	if (v & BIT(8))
		sun8i_emac_receive_all(ndev);

	/* When this asserted, the RX DMA can not acquire next TX descriptor
	 * and RX DMA FSM is suspended.
	*/
	if (v & BIT(9)) {
		u = readl(priv->base + SUN8I_EMAC_RX_CTL1);
		dev_info(priv->dev, "Re-run RX DMA %x\n", u);
		writel(u | BIT(31), priv->base + SUN8I_EMAC_RX_CTL1);
		priv->estats.rx_dma_ua++;
	}

	if (v & BIT(10)) {
		dev_info(priv->dev, "Unhandled interrupt RX_DMA_STOPPED_INT\n");
		priv->estats.rx_dma_stop++;
	}
	if (v & BIT(11))
		dev_info(priv->dev, "Unhandled interrupt RX_TIMEOUT\n");
	if (v & BIT(12))
		dev_info(priv->dev, "Unhandled interrupt RX OVERFLOW\n");
	if (v & BIT(13)) {
		dev_dbg(priv->dev, "Unhandled interrupt RX EARLY\n");
		priv->estats.rx_early_int++;
	}
	if (v & BIT(16))
		dev_info(priv->dev, "Unhandled interrupt RGMII\n");

	/* TODO the datasheet state those register as read-only */
	writel(v & 0x3FCF, priv->base + SUN8I_EMAC_INT_STA);

	return IRQ_HANDLED;
}

static int sun8i_emac_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sun8i_emac_priv *priv;
	struct net_device *ndev;
	int ret;

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	priv = netdev_priv(ndev);
	platform_set_drvdata(pdev, ndev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		dev_err(&pdev->dev, "Cannot request MMIO: %d\n", ret);
		goto probe_err;
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		ret = priv->irq;
		dev_err(&pdev->dev, "Cannot claim IRQ: %d\n", ret);
		goto probe_err;
	}

	ret = devm_request_irq(&pdev->dev, priv->irq, sun8i_emac_dma_interrupt,
			       0, dev_name(&pdev->dev), ndev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request IRQ: %d\n", ret);
		goto probe_err;
	}

	priv->ahb_clk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(priv->ahb_clk)) {
		ret = PTR_ERR(priv->ahb_clk);
		dev_err(&pdev->dev, "Cannot get AHB clock err=%d\n", ret);
		goto probe_err;
	}

	priv->tx_clk = devm_clk_get(&pdev->dev, "tx");
	if (IS_ERR(priv->tx_clk)) {
		ret = PTR_ERR(priv->tx_clk);
		dev_err(&pdev->dev, "Cannot get TX clock err=%d\n", ret);
		goto probe_err;
	}

	priv->rst = devm_reset_control_get_optional(&pdev->dev, "ahb");
	if (IS_ERR(priv->rst)) {
		ret = PTR_ERR(priv->rst);
		dev_info(&pdev->dev, "no mac reset control found %d\n", ret);
		priv->rst = NULL;
	}

	/* Optional regulator for PHY */
	priv->regulator = devm_regulator_get_optional(priv->dev, "phy");
	if (IS_ERR(priv->regulator)) {
		if (PTR_ERR(priv->regulator) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(priv->dev, "no regulator found\n");
		priv->regulator = NULL;
	}

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->tx_lock);

	priv->tx_slot = 0;
	priv->tx_dirty = 0;
	priv->rx_dirty = 0;

	ndev->netdev_ops = &sun8i_emac_netdev_ops;
	ndev->ethtool_ops = &sun8i_emac_ethtool_ops;

	priv->ndev = ndev;
	priv->dev = &pdev->dev;

	ndev->base_addr = (unsigned long)priv->base;
	ndev->irq = priv->irq;

	ndev->hw_features = NETIF_F_SG | NETIF_F_HIGHDMA;
	ndev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_RXCSUM;
	/* TODO NETIF_F_RXALL_BIT */
	ndev->features |= ndev->hw_features;
	ndev->hw_features |= NETIF_F_RXFCS;
	ndev->hw_features |= NETIF_F_RXALL;
	ndev->hw_features |= NETIF_F_LOOPBACK;
	ndev->priv_flags |= IFF_UNICAST_FLT;

	ndev->watchdog_timeo = msecs_to_jiffies(5000);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "ERROR: Register %s failed\n", ndev->name);
		goto probe_err;
	}

	sun8i_emac_set_macaddr(priv, ndev->dev_addr, 0);

	return 0;

probe_err:
	free_netdev(ndev);
	return ret;
}

static int sun8i_emac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);
	platform_set_drvdata(pdev, NULL);
	free_netdev(ndev);

	return 0;
}

static const struct of_device_id sun8i_emac_of_match_table[] = {
	{ .compatible = "allwinner,sun8i-h3-emac" },
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_emac_of_match_table);

static struct platform_driver sun8i_emac_driver = {
	.probe          = sun8i_emac_probe,
	.remove         = sun8i_emac_remove,
	.driver         = {
		.name           = "sun8i-emac",
		.of_match_table	= sun8i_emac_of_match_table,
	},
};

module_platform_driver(sun8i_emac_driver);

MODULE_DESCRIPTION("SUN8I Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LABBE Corentin <clabbe.montjoie@gmail.co");
