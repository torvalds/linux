/*
 * Broadcom GENET (Gigabit Ethernet) controller driver
 *
 * Copyright (c) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)				"bcmgenet: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <net/arp.h>

#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/phy.h>

#include <asm/unaligned.h>

#include "bcmgenet.h"

/* Maximum number of hardware queues, downsized if needed */
#define GENET_MAX_MQ_CNT	4

/* Default highest priority queue for multi queue support */
#define GENET_Q0_PRIORITY	0

#define GENET_DEFAULT_BD_CNT	\
	(TOTAL_DESC - priv->hw_params->tx_queues * priv->hw_params->bds_cnt)

#define RX_BUF_LENGTH		2048
#define SKB_ALIGNMENT		32

/* Tx/Rx DMA register offset, skip 256 descriptors */
#define WORDS_PER_BD(p)		(p->hw_params->words_per_bd)
#define DMA_DESC_SIZE		(WORDS_PER_BD(priv) * sizeof(u32))

#define GENET_TDMA_REG_OFF	(priv->hw_params->tdma_offset + \
				TOTAL_DESC * DMA_DESC_SIZE)

#define GENET_RDMA_REG_OFF	(priv->hw_params->rdma_offset + \
				TOTAL_DESC * DMA_DESC_SIZE)

static inline void dmadesc_set_length_status(struct bcmgenet_priv *priv,
					     void __iomem *d, u32 value)
{
	__raw_writel(value, d + DMA_DESC_LENGTH_STATUS);
}

static inline u32 dmadesc_get_length_status(struct bcmgenet_priv *priv,
					    void __iomem *d)
{
	return __raw_readl(d + DMA_DESC_LENGTH_STATUS);
}

static inline void dmadesc_set_addr(struct bcmgenet_priv *priv,
				    void __iomem *d,
				    dma_addr_t addr)
{
	__raw_writel(lower_32_bits(addr), d + DMA_DESC_ADDRESS_LO);

	/* Register writes to GISB bus can take couple hundred nanoseconds
	 * and are done for each packet, save these expensive writes unless
	 * the platform is explicitly configured for 64-bits/LPAE.
	 */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (priv->hw_params->flags & GENET_HAS_40BITS)
		__raw_writel(upper_32_bits(addr), d + DMA_DESC_ADDRESS_HI);
#endif
}

/* Combined address + length/status setter */
static inline void dmadesc_set(struct bcmgenet_priv *priv,
			       void __iomem *d, dma_addr_t addr, u32 val)
{
	dmadesc_set_length_status(priv, d, val);
	dmadesc_set_addr(priv, d, addr);
}

static inline dma_addr_t dmadesc_get_addr(struct bcmgenet_priv *priv,
					  void __iomem *d)
{
	dma_addr_t addr;

	addr = __raw_readl(d + DMA_DESC_ADDRESS_LO);

	/* Register writes to GISB bus can take couple hundred nanoseconds
	 * and are done for each packet, save these expensive writes unless
	 * the platform is explicitly configured for 64-bits/LPAE.
	 */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (priv->hw_params->flags & GENET_HAS_40BITS)
		addr |= (u64)__raw_readl(d + DMA_DESC_ADDRESS_HI) << 32;
#endif
	return addr;
}

#define GENET_VER_FMT	"%1d.%1d EPHY: 0x%04x"

#define GENET_MSG_DEFAULT	(NETIF_MSG_DRV | NETIF_MSG_PROBE | \
				NETIF_MSG_LINK)

static inline u32 bcmgenet_rbuf_ctrl_get(struct bcmgenet_priv *priv)
{
	if (GENET_IS_V1(priv))
		return bcmgenet_rbuf_readl(priv, RBUF_FLUSH_CTRL_V1);
	else
		return bcmgenet_sys_readl(priv, SYS_RBUF_FLUSH_CTRL);
}

static inline void bcmgenet_rbuf_ctrl_set(struct bcmgenet_priv *priv, u32 val)
{
	if (GENET_IS_V1(priv))
		bcmgenet_rbuf_writel(priv, val, RBUF_FLUSH_CTRL_V1);
	else
		bcmgenet_sys_writel(priv, val, SYS_RBUF_FLUSH_CTRL);
}

/* These macros are defined to deal with register map change
 * between GENET1.1 and GENET2. Only those currently being used
 * by driver are defined.
 */
static inline u32 bcmgenet_tbuf_ctrl_get(struct bcmgenet_priv *priv)
{
	if (GENET_IS_V1(priv))
		return bcmgenet_rbuf_readl(priv, TBUF_CTRL_V1);
	else
		return __raw_readl(priv->base +
				priv->hw_params->tbuf_offset + TBUF_CTRL);
}

static inline void bcmgenet_tbuf_ctrl_set(struct bcmgenet_priv *priv, u32 val)
{
	if (GENET_IS_V1(priv))
		bcmgenet_rbuf_writel(priv, val, TBUF_CTRL_V1);
	else
		__raw_writel(val, priv->base +
				priv->hw_params->tbuf_offset + TBUF_CTRL);
}

static inline u32 bcmgenet_bp_mc_get(struct bcmgenet_priv *priv)
{
	if (GENET_IS_V1(priv))
		return bcmgenet_rbuf_readl(priv, TBUF_BP_MC_V1);
	else
		return __raw_readl(priv->base +
				priv->hw_params->tbuf_offset + TBUF_BP_MC);
}

static inline void bcmgenet_bp_mc_set(struct bcmgenet_priv *priv, u32 val)
{
	if (GENET_IS_V1(priv))
		bcmgenet_rbuf_writel(priv, val, TBUF_BP_MC_V1);
	else
		__raw_writel(val, priv->base +
				priv->hw_params->tbuf_offset + TBUF_BP_MC);
}

/* RX/TX DMA register accessors */
enum dma_reg {
	DMA_RING_CFG = 0,
	DMA_CTRL,
	DMA_STATUS,
	DMA_SCB_BURST_SIZE,
	DMA_ARB_CTRL,
	DMA_PRIORITY,
	DMA_RING_PRIORITY,
};

static const u8 bcmgenet_dma_regs_v3plus[] = {
	[DMA_RING_CFG]		= 0x00,
	[DMA_CTRL]		= 0x04,
	[DMA_STATUS]		= 0x08,
	[DMA_SCB_BURST_SIZE]	= 0x0C,
	[DMA_ARB_CTRL]		= 0x2C,
	[DMA_PRIORITY]		= 0x30,
	[DMA_RING_PRIORITY]	= 0x38,
};

static const u8 bcmgenet_dma_regs_v2[] = {
	[DMA_RING_CFG]		= 0x00,
	[DMA_CTRL]		= 0x04,
	[DMA_STATUS]		= 0x08,
	[DMA_SCB_BURST_SIZE]	= 0x0C,
	[DMA_ARB_CTRL]		= 0x30,
	[DMA_PRIORITY]		= 0x34,
	[DMA_RING_PRIORITY]	= 0x3C,
};

static const u8 bcmgenet_dma_regs_v1[] = {
	[DMA_CTRL]		= 0x00,
	[DMA_STATUS]		= 0x04,
	[DMA_SCB_BURST_SIZE]	= 0x0C,
	[DMA_ARB_CTRL]		= 0x30,
	[DMA_PRIORITY]		= 0x34,
	[DMA_RING_PRIORITY]	= 0x3C,
};

/* Set at runtime once bcmgenet version is known */
static const u8 *bcmgenet_dma_regs;

static inline struct bcmgenet_priv *dev_to_priv(struct device *dev)
{
	return netdev_priv(dev_get_drvdata(dev));
}

static inline u32 bcmgenet_tdma_readl(struct bcmgenet_priv *priv,
				      enum dma_reg r)
{
	return __raw_readl(priv->base + GENET_TDMA_REG_OFF +
			DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

static inline void bcmgenet_tdma_writel(struct bcmgenet_priv *priv,
					u32 val, enum dma_reg r)
{
	__raw_writel(val, priv->base + GENET_TDMA_REG_OFF +
			DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

static inline u32 bcmgenet_rdma_readl(struct bcmgenet_priv *priv,
				      enum dma_reg r)
{
	return __raw_readl(priv->base + GENET_RDMA_REG_OFF +
			DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

static inline void bcmgenet_rdma_writel(struct bcmgenet_priv *priv,
					u32 val, enum dma_reg r)
{
	__raw_writel(val, priv->base + GENET_RDMA_REG_OFF +
			DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

/* RDMA/TDMA ring registers and accessors
 * we merge the common fields and just prefix with T/D the registers
 * having different meaning depending on the direction
 */
enum dma_ring_reg {
	TDMA_READ_PTR = 0,
	RDMA_WRITE_PTR = TDMA_READ_PTR,
	TDMA_READ_PTR_HI,
	RDMA_WRITE_PTR_HI = TDMA_READ_PTR_HI,
	TDMA_CONS_INDEX,
	RDMA_PROD_INDEX = TDMA_CONS_INDEX,
	TDMA_PROD_INDEX,
	RDMA_CONS_INDEX = TDMA_PROD_INDEX,
	DMA_RING_BUF_SIZE,
	DMA_START_ADDR,
	DMA_START_ADDR_HI,
	DMA_END_ADDR,
	DMA_END_ADDR_HI,
	DMA_MBUF_DONE_THRESH,
	TDMA_FLOW_PERIOD,
	RDMA_XON_XOFF_THRESH = TDMA_FLOW_PERIOD,
	TDMA_WRITE_PTR,
	RDMA_READ_PTR = TDMA_WRITE_PTR,
	TDMA_WRITE_PTR_HI,
	RDMA_READ_PTR_HI = TDMA_WRITE_PTR_HI
};

/* GENET v4 supports 40-bits pointer addressing
 * for obvious reasons the LO and HI word parts
 * are contiguous, but this offsets the other
 * registers.
 */
static const u8 genet_dma_ring_regs_v4[] = {
	[TDMA_READ_PTR]			= 0x00,
	[TDMA_READ_PTR_HI]		= 0x04,
	[TDMA_CONS_INDEX]		= 0x08,
	[TDMA_PROD_INDEX]		= 0x0C,
	[DMA_RING_BUF_SIZE]		= 0x10,
	[DMA_START_ADDR]		= 0x14,
	[DMA_START_ADDR_HI]		= 0x18,
	[DMA_END_ADDR]			= 0x1C,
	[DMA_END_ADDR_HI]		= 0x20,
	[DMA_MBUF_DONE_THRESH]		= 0x24,
	[TDMA_FLOW_PERIOD]		= 0x28,
	[TDMA_WRITE_PTR]		= 0x2C,
	[TDMA_WRITE_PTR_HI]		= 0x30,
};

static const u8 genet_dma_ring_regs_v123[] = {
	[TDMA_READ_PTR]			= 0x00,
	[TDMA_CONS_INDEX]		= 0x04,
	[TDMA_PROD_INDEX]		= 0x08,
	[DMA_RING_BUF_SIZE]		= 0x0C,
	[DMA_START_ADDR]		= 0x10,
	[DMA_END_ADDR]			= 0x14,
	[DMA_MBUF_DONE_THRESH]		= 0x18,
	[TDMA_FLOW_PERIOD]		= 0x1C,
	[TDMA_WRITE_PTR]		= 0x20,
};

/* Set at runtime once GENET version is known */
static const u8 *genet_dma_ring_regs;

static inline u32 bcmgenet_tdma_ring_readl(struct bcmgenet_priv *priv,
					   unsigned int ring,
					   enum dma_ring_reg r)
{
	return __raw_readl(priv->base + GENET_TDMA_REG_OFF +
			(DMA_RING_SIZE * ring) +
			genet_dma_ring_regs[r]);
}

static inline void bcmgenet_tdma_ring_writel(struct bcmgenet_priv *priv,
					     unsigned int ring, u32 val,
					     enum dma_ring_reg r)
{
	__raw_writel(val, priv->base + GENET_TDMA_REG_OFF +
			(DMA_RING_SIZE * ring) +
			genet_dma_ring_regs[r]);
}

static inline u32 bcmgenet_rdma_ring_readl(struct bcmgenet_priv *priv,
					   unsigned int ring,
					   enum dma_ring_reg r)
{
	return __raw_readl(priv->base + GENET_RDMA_REG_OFF +
			(DMA_RING_SIZE * ring) +
			genet_dma_ring_regs[r]);
}

static inline void bcmgenet_rdma_ring_writel(struct bcmgenet_priv *priv,
					     unsigned int ring, u32 val,
					     enum dma_ring_reg r)
{
	__raw_writel(val, priv->base + GENET_RDMA_REG_OFF +
			(DMA_RING_SIZE * ring) +
			genet_dma_ring_regs[r]);
}

static int bcmgenet_get_settings(struct net_device *dev,
				 struct ethtool_cmd *cmd)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	if (!priv->phydev)
		return -ENODEV;

	return phy_ethtool_gset(priv->phydev, cmd);
}

static int bcmgenet_set_settings(struct net_device *dev,
				 struct ethtool_cmd *cmd)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	if (!priv->phydev)
		return -ENODEV;

	return phy_ethtool_sset(priv->phydev, cmd);
}

static int bcmgenet_set_rx_csum(struct net_device *dev,
				netdev_features_t wanted)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 rbuf_chk_ctrl;
	bool rx_csum_en;

	rx_csum_en = !!(wanted & NETIF_F_RXCSUM);

	rbuf_chk_ctrl = bcmgenet_rbuf_readl(priv, RBUF_CHK_CTRL);

	/* enable rx checksumming */
	if (rx_csum_en)
		rbuf_chk_ctrl |= RBUF_RXCHK_EN;
	else
		rbuf_chk_ctrl &= ~RBUF_RXCHK_EN;
	priv->desc_rxchk_en = rx_csum_en;

	/* If UniMAC forwards CRC, we need to skip over it to get
	 * a valid CHK bit to be set in the per-packet status word
	*/
	if (rx_csum_en && priv->crc_fwd_en)
		rbuf_chk_ctrl |= RBUF_SKIP_FCS;
	else
		rbuf_chk_ctrl &= ~RBUF_SKIP_FCS;

	bcmgenet_rbuf_writel(priv, rbuf_chk_ctrl, RBUF_CHK_CTRL);

	return 0;
}

static int bcmgenet_set_tx_csum(struct net_device *dev,
				netdev_features_t wanted)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	bool desc_64b_en;
	u32 tbuf_ctrl, rbuf_ctrl;

	tbuf_ctrl = bcmgenet_tbuf_ctrl_get(priv);
	rbuf_ctrl = bcmgenet_rbuf_readl(priv, RBUF_CTRL);

	desc_64b_en = !!(wanted & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM));

	/* enable 64 bytes descriptor in both directions (RBUF and TBUF) */
	if (desc_64b_en) {
		tbuf_ctrl |= RBUF_64B_EN;
		rbuf_ctrl |= RBUF_64B_EN;
	} else {
		tbuf_ctrl &= ~RBUF_64B_EN;
		rbuf_ctrl &= ~RBUF_64B_EN;
	}
	priv->desc_64b_en = desc_64b_en;

	bcmgenet_tbuf_ctrl_set(priv, tbuf_ctrl);
	bcmgenet_rbuf_writel(priv, rbuf_ctrl, RBUF_CTRL);

	return 0;
}

static int bcmgenet_set_features(struct net_device *dev,
				 netdev_features_t features)
{
	netdev_features_t changed = features ^ dev->features;
	netdev_features_t wanted = dev->wanted_features;
	int ret = 0;

	if (changed & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM))
		ret = bcmgenet_set_tx_csum(dev, wanted);
	if (changed & (NETIF_F_RXCSUM))
		ret = bcmgenet_set_rx_csum(dev, wanted);

	return ret;
}

static u32 bcmgenet_get_msglevel(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	return priv->msg_enable;
}

static void bcmgenet_set_msglevel(struct net_device *dev, u32 level)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	priv->msg_enable = level;
}

/* standard ethtool support functions. */
enum bcmgenet_stat_type {
	BCMGENET_STAT_NETDEV = -1,
	BCMGENET_STAT_MIB_RX,
	BCMGENET_STAT_MIB_TX,
	BCMGENET_STAT_RUNT,
	BCMGENET_STAT_MISC,
};

struct bcmgenet_stats {
	char stat_string[ETH_GSTRING_LEN];
	int stat_sizeof;
	int stat_offset;
	enum bcmgenet_stat_type type;
	/* reg offset from UMAC base for misc counters */
	u16 reg_offset;
};

#define STAT_NETDEV(m) { \
	.stat_string = __stringify(m), \
	.stat_sizeof = sizeof(((struct net_device_stats *)0)->m), \
	.stat_offset = offsetof(struct net_device_stats, m), \
	.type = BCMGENET_STAT_NETDEV, \
}

#define STAT_GENET_MIB(str, m, _type) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcmgenet_priv *)0)->m), \
	.stat_offset = offsetof(struct bcmgenet_priv, m), \
	.type = _type, \
}

#define STAT_GENET_MIB_RX(str, m) STAT_GENET_MIB(str, m, BCMGENET_STAT_MIB_RX)
#define STAT_GENET_MIB_TX(str, m) STAT_GENET_MIB(str, m, BCMGENET_STAT_MIB_TX)
#define STAT_GENET_RUNT(str, m) STAT_GENET_MIB(str, m, BCMGENET_STAT_RUNT)

#define STAT_GENET_MISC(str, m, offset) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcmgenet_priv *)0)->m), \
	.stat_offset = offsetof(struct bcmgenet_priv, m), \
	.type = BCMGENET_STAT_MISC, \
	.reg_offset = offset, \
}


/* There is a 0xC gap between the end of RX and beginning of TX stats and then
 * between the end of TX stats and the beginning of the RX RUNT
 */
#define BCMGENET_STAT_OFFSET	0xc

/* Hardware counters must be kept in sync because the order/offset
 * is important here (order in structure declaration = order in hardware)
 */
static const struct bcmgenet_stats bcmgenet_gstrings_stats[] = {
	/* general stats */
	STAT_NETDEV(rx_packets),
	STAT_NETDEV(tx_packets),
	STAT_NETDEV(rx_bytes),
	STAT_NETDEV(tx_bytes),
	STAT_NETDEV(rx_errors),
	STAT_NETDEV(tx_errors),
	STAT_NETDEV(rx_dropped),
	STAT_NETDEV(tx_dropped),
	STAT_NETDEV(multicast),
	/* UniMAC RSV counters */
	STAT_GENET_MIB_RX("rx_64_octets", mib.rx.pkt_cnt.cnt_64),
	STAT_GENET_MIB_RX("rx_65_127_oct", mib.rx.pkt_cnt.cnt_127),
	STAT_GENET_MIB_RX("rx_128_255_oct", mib.rx.pkt_cnt.cnt_255),
	STAT_GENET_MIB_RX("rx_256_511_oct", mib.rx.pkt_cnt.cnt_511),
	STAT_GENET_MIB_RX("rx_512_1023_oct", mib.rx.pkt_cnt.cnt_1023),
	STAT_GENET_MIB_RX("rx_1024_1518_oct", mib.rx.pkt_cnt.cnt_1518),
	STAT_GENET_MIB_RX("rx_vlan_1519_1522_oct", mib.rx.pkt_cnt.cnt_mgv),
	STAT_GENET_MIB_RX("rx_1522_2047_oct", mib.rx.pkt_cnt.cnt_2047),
	STAT_GENET_MIB_RX("rx_2048_4095_oct", mib.rx.pkt_cnt.cnt_4095),
	STAT_GENET_MIB_RX("rx_4096_9216_oct", mib.rx.pkt_cnt.cnt_9216),
	STAT_GENET_MIB_RX("rx_pkts", mib.rx.pkt),
	STAT_GENET_MIB_RX("rx_bytes", mib.rx.bytes),
	STAT_GENET_MIB_RX("rx_multicast", mib.rx.mca),
	STAT_GENET_MIB_RX("rx_broadcast", mib.rx.bca),
	STAT_GENET_MIB_RX("rx_fcs", mib.rx.fcs),
	STAT_GENET_MIB_RX("rx_control", mib.rx.cf),
	STAT_GENET_MIB_RX("rx_pause", mib.rx.pf),
	STAT_GENET_MIB_RX("rx_unknown", mib.rx.uo),
	STAT_GENET_MIB_RX("rx_align", mib.rx.aln),
	STAT_GENET_MIB_RX("rx_outrange", mib.rx.flr),
	STAT_GENET_MIB_RX("rx_code", mib.rx.cde),
	STAT_GENET_MIB_RX("rx_carrier", mib.rx.fcr),
	STAT_GENET_MIB_RX("rx_oversize", mib.rx.ovr),
	STAT_GENET_MIB_RX("rx_jabber", mib.rx.jbr),
	STAT_GENET_MIB_RX("rx_mtu_err", mib.rx.mtue),
	STAT_GENET_MIB_RX("rx_good_pkts", mib.rx.pok),
	STAT_GENET_MIB_RX("rx_unicast", mib.rx.uc),
	STAT_GENET_MIB_RX("rx_ppp", mib.rx.ppp),
	STAT_GENET_MIB_RX("rx_crc", mib.rx.rcrc),
	/* UniMAC TSV counters */
	STAT_GENET_MIB_TX("tx_64_octets", mib.tx.pkt_cnt.cnt_64),
	STAT_GENET_MIB_TX("tx_65_127_oct", mib.tx.pkt_cnt.cnt_127),
	STAT_GENET_MIB_TX("tx_128_255_oct", mib.tx.pkt_cnt.cnt_255),
	STAT_GENET_MIB_TX("tx_256_511_oct", mib.tx.pkt_cnt.cnt_511),
	STAT_GENET_MIB_TX("tx_512_1023_oct", mib.tx.pkt_cnt.cnt_1023),
	STAT_GENET_MIB_TX("tx_1024_1518_oct", mib.tx.pkt_cnt.cnt_1518),
	STAT_GENET_MIB_TX("tx_vlan_1519_1522_oct", mib.tx.pkt_cnt.cnt_mgv),
	STAT_GENET_MIB_TX("tx_1522_2047_oct", mib.tx.pkt_cnt.cnt_2047),
	STAT_GENET_MIB_TX("tx_2048_4095_oct", mib.tx.pkt_cnt.cnt_4095),
	STAT_GENET_MIB_TX("tx_4096_9216_oct", mib.tx.pkt_cnt.cnt_9216),
	STAT_GENET_MIB_TX("tx_pkts", mib.tx.pkts),
	STAT_GENET_MIB_TX("tx_multicast", mib.tx.mca),
	STAT_GENET_MIB_TX("tx_broadcast", mib.tx.bca),
	STAT_GENET_MIB_TX("tx_pause", mib.tx.pf),
	STAT_GENET_MIB_TX("tx_control", mib.tx.cf),
	STAT_GENET_MIB_TX("tx_fcs_err", mib.tx.fcs),
	STAT_GENET_MIB_TX("tx_oversize", mib.tx.ovr),
	STAT_GENET_MIB_TX("tx_defer", mib.tx.drf),
	STAT_GENET_MIB_TX("tx_excess_defer", mib.tx.edf),
	STAT_GENET_MIB_TX("tx_single_col", mib.tx.scl),
	STAT_GENET_MIB_TX("tx_multi_col", mib.tx.mcl),
	STAT_GENET_MIB_TX("tx_late_col", mib.tx.lcl),
	STAT_GENET_MIB_TX("tx_excess_col", mib.tx.ecl),
	STAT_GENET_MIB_TX("tx_frags", mib.tx.frg),
	STAT_GENET_MIB_TX("tx_total_col", mib.tx.ncl),
	STAT_GENET_MIB_TX("tx_jabber", mib.tx.jbr),
	STAT_GENET_MIB_TX("tx_bytes", mib.tx.bytes),
	STAT_GENET_MIB_TX("tx_good_pkts", mib.tx.pok),
	STAT_GENET_MIB_TX("tx_unicast", mib.tx.uc),
	/* UniMAC RUNT counters */
	STAT_GENET_RUNT("rx_runt_pkts", mib.rx_runt_cnt),
	STAT_GENET_RUNT("rx_runt_valid_fcs", mib.rx_runt_fcs),
	STAT_GENET_RUNT("rx_runt_inval_fcs_align", mib.rx_runt_fcs_align),
	STAT_GENET_RUNT("rx_runt_bytes", mib.rx_runt_bytes),
	/* Misc UniMAC counters */
	STAT_GENET_MISC("rbuf_ovflow_cnt", mib.rbuf_ovflow_cnt,
			UMAC_RBUF_OVFL_CNT),
	STAT_GENET_MISC("rbuf_err_cnt", mib.rbuf_err_cnt, UMAC_RBUF_ERR_CNT),
	STAT_GENET_MISC("mdf_err_cnt", mib.mdf_err_cnt, UMAC_MDF_ERR_CNT),
};

#define BCMGENET_STATS_LEN	ARRAY_SIZE(bcmgenet_gstrings_stats)

static void bcmgenet_get_drvinfo(struct net_device *dev,
				 struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, "bcmgenet", sizeof(info->driver));
	strlcpy(info->version, "v2.0", sizeof(info->version));
	info->n_stats = BCMGENET_STATS_LEN;
}

static int bcmgenet_get_sset_count(struct net_device *dev, int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return BCMGENET_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void bcmgenet_get_strings(struct net_device *dev, u32 stringset,
				 u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < BCMGENET_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
			       bcmgenet_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
		}
		break;
	}
}

static void bcmgenet_update_mib_counters(struct bcmgenet_priv *priv)
{
	int i, j = 0;

	for (i = 0; i < BCMGENET_STATS_LEN; i++) {
		const struct bcmgenet_stats *s;
		u8 offset = 0;
		u32 val = 0;
		char *p;

		s = &bcmgenet_gstrings_stats[i];
		switch (s->type) {
		case BCMGENET_STAT_NETDEV:
			continue;
		case BCMGENET_STAT_MIB_RX:
		case BCMGENET_STAT_MIB_TX:
		case BCMGENET_STAT_RUNT:
			if (s->type != BCMGENET_STAT_MIB_RX)
				offset = BCMGENET_STAT_OFFSET;
			val = bcmgenet_umac_readl(priv,
						  UMAC_MIB_START + j + offset);
			break;
		case BCMGENET_STAT_MISC:
			val = bcmgenet_umac_readl(priv, s->reg_offset);
			/* clear if overflowed */
			if (val == ~0)
				bcmgenet_umac_writel(priv, 0, s->reg_offset);
			break;
		}

		j += s->stat_sizeof;
		p = (char *)priv + s->stat_offset;
		*(u32 *)p = val;
	}
}

static void bcmgenet_get_ethtool_stats(struct net_device *dev,
				       struct ethtool_stats *stats,
				       u64 *data)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int i;

	if (netif_running(dev))
		bcmgenet_update_mib_counters(priv);

	for (i = 0; i < BCMGENET_STATS_LEN; i++) {
		const struct bcmgenet_stats *s;
		char *p;

		s = &bcmgenet_gstrings_stats[i];
		if (s->type == BCMGENET_STAT_NETDEV)
			p = (char *)&dev->stats;
		else
			p = (char *)priv;
		p += s->stat_offset;
		data[i] = *(u32 *)p;
	}
}

/* standard ethtool support functions. */
static struct ethtool_ops bcmgenet_ethtool_ops = {
	.get_strings		= bcmgenet_get_strings,
	.get_sset_count		= bcmgenet_get_sset_count,
	.get_ethtool_stats	= bcmgenet_get_ethtool_stats,
	.get_settings		= bcmgenet_get_settings,
	.set_settings		= bcmgenet_set_settings,
	.get_drvinfo		= bcmgenet_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= bcmgenet_get_msglevel,
	.set_msglevel		= bcmgenet_set_msglevel,
	.get_wol		= bcmgenet_get_wol,
	.set_wol		= bcmgenet_set_wol,
};

/* Power down the unimac, based on mode. */
static void bcmgenet_power_down(struct bcmgenet_priv *priv,
				enum bcmgenet_power_mode mode)
{
	u32 reg;

	switch (mode) {
	case GENET_POWER_CABLE_SENSE:
		phy_detach(priv->phydev);
		break;

	case GENET_POWER_WOL_MAGIC:
		bcmgenet_wol_power_down_cfg(priv, mode);
		break;

	case GENET_POWER_PASSIVE:
		/* Power down LED */
		if (priv->hw_params->flags & GENET_HAS_EXT) {
			reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
			reg |= (EXT_PWR_DOWN_PHY |
				EXT_PWR_DOWN_DLL | EXT_PWR_DOWN_BIAS);
			bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
		}
		break;
	default:
		break;
	}
}

static void bcmgenet_power_up(struct bcmgenet_priv *priv,
			      enum bcmgenet_power_mode mode)
{
	u32 reg;

	if (!(priv->hw_params->flags & GENET_HAS_EXT))
		return;

	reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);

	switch (mode) {
	case GENET_POWER_PASSIVE:
		reg &= ~(EXT_PWR_DOWN_DLL | EXT_PWR_DOWN_PHY |
				EXT_PWR_DOWN_BIAS);
		/* fallthrough */
	case GENET_POWER_CABLE_SENSE:
		/* enable APD */
		reg |= EXT_PWR_DN_EN_LD;
		break;
	case GENET_POWER_WOL_MAGIC:
		bcmgenet_wol_power_up_cfg(priv, mode);
		return;
	default:
		break;
	}

	bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);

	if (mode == GENET_POWER_PASSIVE)
		bcmgenet_mii_reset(priv->dev);
}

/* ioctl handle special commands that are not present in ethtool. */
static int bcmgenet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int val = 0;

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (!priv->phydev)
			val = -ENODEV;
		else
			val = phy_mii_ioctl(priv->phydev, rq, cmd);
		break;

	default:
		val = -EINVAL;
		break;
	}

	return val;
}

static struct enet_cb *bcmgenet_get_txcb(struct bcmgenet_priv *priv,
					 struct bcmgenet_tx_ring *ring)
{
	struct enet_cb *tx_cb_ptr;

	tx_cb_ptr = ring->cbs;
	tx_cb_ptr += ring->write_ptr - ring->cb_ptr;
	tx_cb_ptr->bd_addr = priv->tx_bds + ring->write_ptr * DMA_DESC_SIZE;
	/* Advancing local write pointer */
	if (ring->write_ptr == ring->end_ptr)
		ring->write_ptr = ring->cb_ptr;
	else
		ring->write_ptr++;

	return tx_cb_ptr;
}

/* Simple helper to free a control block's resources */
static void bcmgenet_free_cb(struct enet_cb *cb)
{
	dev_kfree_skb_any(cb->skb);
	cb->skb = NULL;
	dma_unmap_addr_set(cb, dma_addr, 0);
}

static inline void bcmgenet_tx_ring16_int_disable(struct bcmgenet_priv *priv,
						  struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_0_writel(priv,
				 UMAC_IRQ_TXDMA_BDONE | UMAC_IRQ_TXDMA_PDONE,
				 INTRL2_CPU_MASK_SET);
}

static inline void bcmgenet_tx_ring16_int_enable(struct bcmgenet_priv *priv,
						 struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_0_writel(priv,
				 UMAC_IRQ_TXDMA_BDONE | UMAC_IRQ_TXDMA_PDONE,
				 INTRL2_CPU_MASK_CLEAR);
}

static inline void bcmgenet_tx_ring_int_enable(struct bcmgenet_priv *priv,
					       struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_1_writel(priv, (1 << ring->index),
				 INTRL2_CPU_MASK_CLEAR);
	priv->int1_mask &= ~(1 << ring->index);
}

static inline void bcmgenet_tx_ring_int_disable(struct bcmgenet_priv *priv,
						struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_1_writel(priv, (1 << ring->index),
				 INTRL2_CPU_MASK_SET);
	priv->int1_mask |= (1 << ring->index);
}

/* Unlocked version of the reclaim routine */
static void __bcmgenet_tx_reclaim(struct net_device *dev,
				  struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int last_tx_cn, last_c_index, num_tx_bds;
	struct enet_cb *tx_cb_ptr;
	struct netdev_queue *txq;
	unsigned int bds_compl;
	unsigned int c_index;

	/* Compute how many buffers are transmitted since last xmit call */
	c_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_CONS_INDEX);
	txq = netdev_get_tx_queue(dev, ring->queue);

	last_c_index = ring->c_index;
	num_tx_bds = ring->size;

	c_index &= (num_tx_bds - 1);

	if (c_index >= last_c_index)
		last_tx_cn = c_index - last_c_index;
	else
		last_tx_cn = num_tx_bds - last_c_index + c_index;

	netif_dbg(priv, tx_done, dev,
		  "%s ring=%d index=%d last_tx_cn=%d last_index=%d\n",
		  __func__, ring->index,
		  c_index, last_tx_cn, last_c_index);

	/* Reclaim transmitted buffers */
	while (last_tx_cn-- > 0) {
		tx_cb_ptr = ring->cbs + last_c_index;
		bds_compl = 0;
		if (tx_cb_ptr->skb) {
			bds_compl = skb_shinfo(tx_cb_ptr->skb)->nr_frags + 1;
			dev->stats.tx_bytes += tx_cb_ptr->skb->len;
			dma_unmap_single(&dev->dev,
					 dma_unmap_addr(tx_cb_ptr, dma_addr),
					 tx_cb_ptr->skb->len,
					 DMA_TO_DEVICE);
			bcmgenet_free_cb(tx_cb_ptr);
		} else if (dma_unmap_addr(tx_cb_ptr, dma_addr)) {
			dev->stats.tx_bytes +=
				dma_unmap_len(tx_cb_ptr, dma_len);
			dma_unmap_page(&dev->dev,
				       dma_unmap_addr(tx_cb_ptr, dma_addr),
				       dma_unmap_len(tx_cb_ptr, dma_len),
				       DMA_TO_DEVICE);
			dma_unmap_addr_set(tx_cb_ptr, dma_addr, 0);
		}
		dev->stats.tx_packets++;
		ring->free_bds += bds_compl;

		last_c_index++;
		last_c_index &= (num_tx_bds - 1);
	}

	if (ring->free_bds > (MAX_SKB_FRAGS + 1))
		ring->int_disable(priv, ring);

	if (netif_tx_queue_stopped(txq))
		netif_tx_wake_queue(txq);

	ring->c_index = c_index;
}

static void bcmgenet_tx_reclaim(struct net_device *dev,
				struct bcmgenet_tx_ring *ring)
{
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	__bcmgenet_tx_reclaim(dev, ring);
	spin_unlock_irqrestore(&ring->lock, flags);
}

static void bcmgenet_tx_reclaim_all(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int i;

	if (netif_is_multiqueue(dev)) {
		for (i = 0; i < priv->hw_params->tx_queues; i++)
			bcmgenet_tx_reclaim(dev, &priv->tx_rings[i]);
	}

	bcmgenet_tx_reclaim(dev, &priv->tx_rings[DESC_INDEX]);
}

/* Transmits a single SKB (either head of a fragment or a single SKB)
 * caller must hold priv->lock
 */
static int bcmgenet_xmit_single(struct net_device *dev,
				struct sk_buff *skb,
				u16 dma_desc_flags,
				struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	struct enet_cb *tx_cb_ptr;
	unsigned int skb_len;
	dma_addr_t mapping;
	u32 length_status;
	int ret;

	tx_cb_ptr = bcmgenet_get_txcb(priv, ring);

	if (unlikely(!tx_cb_ptr))
		BUG();

	tx_cb_ptr->skb = skb;

	skb_len = skb_headlen(skb) < ETH_ZLEN ? ETH_ZLEN : skb_headlen(skb);

	mapping = dma_map_single(kdev, skb->data, skb_len, DMA_TO_DEVICE);
	ret = dma_mapping_error(kdev, mapping);
	if (ret) {
		netif_err(priv, tx_err, dev, "Tx DMA map failed\n");
		dev_kfree_skb(skb);
		return ret;
	}

	dma_unmap_addr_set(tx_cb_ptr, dma_addr, mapping);
	dma_unmap_len_set(tx_cb_ptr, dma_len, skb->len);
	length_status = (skb_len << DMA_BUFLENGTH_SHIFT) | dma_desc_flags |
			(priv->hw_params->qtag_mask << DMA_TX_QTAG_SHIFT) |
			DMA_TX_APPEND_CRC;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		length_status |= DMA_TX_DO_CSUM;

	dmadesc_set(priv, tx_cb_ptr->bd_addr, mapping, length_status);

	/* Decrement total BD count and advance our write pointer */
	ring->free_bds -= 1;
	ring->prod_index += 1;
	ring->prod_index &= DMA_P_INDEX_MASK;

	return 0;
}

/* Transmit a SKB fragment */
static int bcmgenet_xmit_frag(struct net_device *dev,
			      skb_frag_t *frag,
			      u16 dma_desc_flags,
			      struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	struct enet_cb *tx_cb_ptr;
	dma_addr_t mapping;
	int ret;

	tx_cb_ptr = bcmgenet_get_txcb(priv, ring);

	if (unlikely(!tx_cb_ptr))
		BUG();
	tx_cb_ptr->skb = NULL;

	mapping = skb_frag_dma_map(kdev, frag, 0,
				   skb_frag_size(frag), DMA_TO_DEVICE);
	ret = dma_mapping_error(kdev, mapping);
	if (ret) {
		netif_err(priv, tx_err, dev, "%s: Tx DMA map failed\n",
			  __func__);
		return ret;
	}

	dma_unmap_addr_set(tx_cb_ptr, dma_addr, mapping);
	dma_unmap_len_set(tx_cb_ptr, dma_len, frag->size);

	dmadesc_set(priv, tx_cb_ptr->bd_addr, mapping,
		    (frag->size << DMA_BUFLENGTH_SHIFT) | dma_desc_flags |
		    (priv->hw_params->qtag_mask << DMA_TX_QTAG_SHIFT));


	ring->free_bds -= 1;
	ring->prod_index += 1;
	ring->prod_index &= DMA_P_INDEX_MASK;

	return 0;
}

/* Reallocate the SKB to put enough headroom in front of it and insert
 * the transmit checksum offsets in the descriptors
 */
static int bcmgenet_put_tx_csum(struct net_device *dev, struct sk_buff *skb)
{
	struct status_64 *status = NULL;
	struct sk_buff *new_skb;
	u16 offset;
	u8 ip_proto;
	u16 ip_ver;
	u32 tx_csum_info;

	if (unlikely(skb_headroom(skb) < sizeof(*status))) {
		/* If 64 byte status block enabled, must make sure skb has
		 * enough headroom for us to insert 64B status block.
		 */
		new_skb = skb_realloc_headroom(skb, sizeof(*status));
		dev_kfree_skb(skb);
		if (!new_skb) {
			dev->stats.tx_errors++;
			dev->stats.tx_dropped++;
			return -ENOMEM;
		}
		skb = new_skb;
	}

	skb_push(skb, sizeof(*status));
	status = (struct status_64 *)skb->data;

	if (skb->ip_summed  == CHECKSUM_PARTIAL) {
		ip_ver = htons(skb->protocol);
		switch (ip_ver) {
		case ETH_P_IP:
			ip_proto = ip_hdr(skb)->protocol;
			break;
		case ETH_P_IPV6:
			ip_proto = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			return 0;
		}

		offset = skb_checksum_start_offset(skb) - sizeof(*status);
		tx_csum_info = (offset << STATUS_TX_CSUM_START_SHIFT) |
				(offset + skb->csum_offset);

		/* Set the length valid bit for TCP and UDP and just set
		 * the special UDP flag for IPv4, else just set to 0.
		 */
		if (ip_proto == IPPROTO_TCP || ip_proto == IPPROTO_UDP) {
			tx_csum_info |= STATUS_TX_CSUM_LV;
			if (ip_proto == IPPROTO_UDP && ip_ver == ETH_P_IP)
				tx_csum_info |= STATUS_TX_CSUM_PROTO_UDP;
		} else {
			tx_csum_info = 0;
		}

		status->tx_csum_info = tx_csum_info;
	}

	return 0;
}

static netdev_tx_t bcmgenet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_tx_ring *ring = NULL;
	struct netdev_queue *txq;
	unsigned long flags = 0;
	int nr_frags, index;
	u16 dma_desc_flags;
	int ret;
	int i;

	index = skb_get_queue_mapping(skb);
	/* Mapping strategy:
	 * queue_mapping = 0, unclassified, packet xmited through ring16
	 * queue_mapping = 1, goes to ring 0. (highest priority queue
	 * queue_mapping = 2, goes to ring 1.
	 * queue_mapping = 3, goes to ring 2.
	 * queue_mapping = 4, goes to ring 3.
	 */
	if (index == 0)
		index = DESC_INDEX;
	else
		index -= 1;

	nr_frags = skb_shinfo(skb)->nr_frags;
	ring = &priv->tx_rings[index];
	txq = netdev_get_tx_queue(dev, ring->queue);

	spin_lock_irqsave(&ring->lock, flags);
	if (ring->free_bds <= nr_frags + 1) {
		netif_tx_stop_queue(txq);
		netdev_err(dev, "%s: tx ring %d full when queue %d awake\n",
			   __func__, index, ring->queue);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (skb_padto(skb, ETH_ZLEN)) {
		ret = NETDEV_TX_OK;
		goto out;
	}

	/* set the SKB transmit checksum */
	if (priv->desc_64b_en) {
		ret = bcmgenet_put_tx_csum(dev, skb);
		if (ret) {
			ret = NETDEV_TX_OK;
			goto out;
		}
	}

	dma_desc_flags = DMA_SOP;
	if (nr_frags == 0)
		dma_desc_flags |= DMA_EOP;

	/* Transmit single SKB or head of fragment list */
	ret = bcmgenet_xmit_single(dev, skb, dma_desc_flags, ring);
	if (ret) {
		ret = NETDEV_TX_OK;
		goto out;
	}

	/* xmit fragment */
	for (i = 0; i < nr_frags; i++) {
		ret = bcmgenet_xmit_frag(dev,
					 &skb_shinfo(skb)->frags[i],
					 (i == nr_frags - 1) ? DMA_EOP : 0,
					 ring);
		if (ret) {
			ret = NETDEV_TX_OK;
			goto out;
		}
	}

	skb_tx_timestamp(skb);

	/* we kept a software copy of how much we should advance the TDMA
	 * producer index, now write it down to the hardware
	 */
	bcmgenet_tdma_ring_writel(priv, ring->index,
				  ring->prod_index, TDMA_PROD_INDEX);

	if (ring->free_bds <= (MAX_SKB_FRAGS + 1)) {
		netif_tx_stop_queue(txq);
		ring->int_enable(priv, ring);
	}

out:
	spin_unlock_irqrestore(&ring->lock, flags);

	return ret;
}


static int bcmgenet_rx_refill(struct bcmgenet_priv *priv, struct enet_cb *cb)
{
	struct device *kdev = &priv->pdev->dev;
	struct sk_buff *skb;
	dma_addr_t mapping;
	int ret;

	skb = netdev_alloc_skb(priv->dev, priv->rx_buf_len + SKB_ALIGNMENT);
	if (!skb)
		return -ENOMEM;

	/* a caller did not release this control block */
	WARN_ON(cb->skb != NULL);
	cb->skb = skb;
	mapping = dma_map_single(kdev, skb->data,
				 priv->rx_buf_len, DMA_FROM_DEVICE);
	ret = dma_mapping_error(kdev, mapping);
	if (ret) {
		bcmgenet_free_cb(cb);
		netif_err(priv, rx_err, priv->dev,
			  "%s DMA map failed\n", __func__);
		return ret;
	}

	dma_unmap_addr_set(cb, dma_addr, mapping);
	/* assign packet, prepare descriptor, and advance pointer */

	dmadesc_set_addr(priv, priv->rx_bd_assign_ptr, mapping);

	/* turn on the newly assigned BD for DMA to use */
	priv->rx_bd_assign_index++;
	priv->rx_bd_assign_index &= (priv->num_rx_bds - 1);

	priv->rx_bd_assign_ptr = priv->rx_bds +
		(priv->rx_bd_assign_index * DMA_DESC_SIZE);

	return 0;
}

/* bcmgenet_desc_rx - descriptor based rx process.
 * this could be called from bottom half, or from NAPI polling method.
 */
static unsigned int bcmgenet_desc_rx(struct bcmgenet_priv *priv,
				     unsigned int budget)
{
	struct net_device *dev = priv->dev;
	struct enet_cb *cb;
	struct sk_buff *skb;
	u32 dma_length_status;
	unsigned long dma_flag;
	int len, err;
	unsigned int rxpktprocessed = 0, rxpkttoprocess;
	unsigned int p_index;
	unsigned int chksum_ok = 0;

	p_index = bcmgenet_rdma_ring_readl(priv, DESC_INDEX, RDMA_PROD_INDEX);
	p_index &= DMA_P_INDEX_MASK;

	if (p_index < priv->rx_c_index)
		rxpkttoprocess = (DMA_C_INDEX_MASK + 1) -
			priv->rx_c_index + p_index;
	else
		rxpkttoprocess = p_index - priv->rx_c_index;

	netif_dbg(priv, rx_status, dev,
		  "RDMA: rxpkttoprocess=%d\n", rxpkttoprocess);

	while ((rxpktprocessed < rxpkttoprocess) &&
	       (rxpktprocessed < budget)) {
		cb = &priv->rx_cbs[priv->rx_read_ptr];
		skb = cb->skb;

		rxpktprocessed++;

		priv->rx_read_ptr++;
		priv->rx_read_ptr &= (priv->num_rx_bds - 1);

		/* We do not have a backing SKB, so we do not have a
		 * corresponding DMA mapping for this incoming packet since
		 * bcmgenet_rx_refill always either has both skb and mapping or
		 * none.
		 */
		if (unlikely(!skb)) {
			dev->stats.rx_dropped++;
			dev->stats.rx_errors++;
			goto refill;
		}

		/* Unmap the packet contents such that we can use the
		 * RSV from the 64 bytes descriptor when enabled and save
		 * a 32-bits register read
		 */
		dma_unmap_single(&dev->dev, dma_unmap_addr(cb, dma_addr),
				 priv->rx_buf_len, DMA_FROM_DEVICE);

		if (!priv->desc_64b_en) {
			dma_length_status =
				dmadesc_get_length_status(priv,
							  priv->rx_bds +
							  (priv->rx_read_ptr *
							   DMA_DESC_SIZE));
		} else {
			struct status_64 *status;

			status = (struct status_64 *)skb->data;
			dma_length_status = status->length_status;
		}

		/* DMA flags and length are still valid no matter how
		 * we got the Receive Status Vector (64B RSB or register)
		 */
		dma_flag = dma_length_status & 0xffff;
		len = dma_length_status >> DMA_BUFLENGTH_SHIFT;

		netif_dbg(priv, rx_status, dev,
			  "%s:p_ind=%d c_ind=%d read_ptr=%d len_stat=0x%08x\n",
			  __func__, p_index, priv->rx_c_index,
			  priv->rx_read_ptr, dma_length_status);

		if (unlikely(!(dma_flag & DMA_EOP) || !(dma_flag & DMA_SOP))) {
			netif_err(priv, rx_status, dev,
				  "dropping fragmented packet!\n");
			dev->stats.rx_dropped++;
			dev->stats.rx_errors++;
			dev_kfree_skb_any(cb->skb);
			cb->skb = NULL;
			goto refill;
		}
		/* report errors */
		if (unlikely(dma_flag & (DMA_RX_CRC_ERROR |
						DMA_RX_OV |
						DMA_RX_NO |
						DMA_RX_LG |
						DMA_RX_RXER))) {
			netif_err(priv, rx_status, dev, "dma_flag=0x%x\n",
				  (unsigned int)dma_flag);
			if (dma_flag & DMA_RX_CRC_ERROR)
				dev->stats.rx_crc_errors++;
			if (dma_flag & DMA_RX_OV)
				dev->stats.rx_over_errors++;
			if (dma_flag & DMA_RX_NO)
				dev->stats.rx_frame_errors++;
			if (dma_flag & DMA_RX_LG)
				dev->stats.rx_length_errors++;
			dev->stats.rx_dropped++;
			dev->stats.rx_errors++;

			/* discard the packet and advance consumer index.*/
			dev_kfree_skb_any(cb->skb);
			cb->skb = NULL;
			goto refill;
		} /* error packet */

		chksum_ok = (dma_flag & priv->dma_rx_chk_bit) &&
			     priv->desc_rxchk_en;

		skb_put(skb, len);
		if (priv->desc_64b_en) {
			skb_pull(skb, 64);
			len -= 64;
		}

		if (likely(chksum_ok))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* remove hardware 2bytes added for IP alignment */
		skb_pull(skb, 2);
		len -= 2;

		if (priv->crc_fwd_en) {
			skb_trim(skb, len - ETH_FCS_LEN);
			len -= ETH_FCS_LEN;
		}

		/*Finish setting up the received SKB and send it to the kernel*/
		skb->protocol = eth_type_trans(skb, priv->dev);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += len;
		if (dma_flag & DMA_RX_MULT)
			dev->stats.multicast++;

		/* Notify kernel */
		napi_gro_receive(&priv->napi, skb);
		cb->skb = NULL;
		netif_dbg(priv, rx_status, dev, "pushed up to kernel\n");

		/* refill RX path on the current control block */
refill:
		err = bcmgenet_rx_refill(priv, cb);
		if (err)
			netif_err(priv, rx_err, dev, "Rx refill failed\n");
	}

	return rxpktprocessed;
}

/* Assign skb to RX DMA descriptor. */
static int bcmgenet_alloc_rx_buffers(struct bcmgenet_priv *priv)
{
	struct enet_cb *cb;
	int ret = 0;
	int i;

	netif_dbg(priv, hw, priv->dev, "%s:\n", __func__);

	/* loop here for each buffer needing assign */
	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = &priv->rx_cbs[priv->rx_bd_assign_index];
		if (cb->skb)
			continue;

		ret = bcmgenet_rx_refill(priv, cb);
		if (ret)
			break;
	}

	return ret;
}

static void bcmgenet_free_rx_buffers(struct bcmgenet_priv *priv)
{
	struct enet_cb *cb;
	int i;

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = &priv->rx_cbs[i];

		if (dma_unmap_addr(cb, dma_addr)) {
			dma_unmap_single(&priv->dev->dev,
					 dma_unmap_addr(cb, dma_addr),
					 priv->rx_buf_len, DMA_FROM_DEVICE);
			dma_unmap_addr_set(cb, dma_addr, 0);
		}

		if (cb->skb)
			bcmgenet_free_cb(cb);
	}
}

static void umac_enable_set(struct bcmgenet_priv *priv, u32 mask, bool enable)
{
	u32 reg;

	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	if (enable)
		reg |= mask;
	else
		reg &= ~mask;
	bcmgenet_umac_writel(priv, reg, UMAC_CMD);

	/* UniMAC stops on a packet boundary, wait for a full-size packet
	 * to be processed
	 */
	if (enable == 0)
		usleep_range(1000, 2000);
}

static int reset_umac(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	unsigned int timeout = 0;
	u32 reg;

	/* 7358a0/7552a0: bad default in RBUF_FLUSH_CTRL.umac_sw_rst */
	bcmgenet_rbuf_ctrl_set(priv, 0);
	udelay(10);

	/* disable MAC while updating its registers */
	bcmgenet_umac_writel(priv, 0, UMAC_CMD);

	/* issue soft reset, wait for it to complete */
	bcmgenet_umac_writel(priv, CMD_SW_RESET, UMAC_CMD);
	while (timeout++ < 1000) {
		reg = bcmgenet_umac_readl(priv, UMAC_CMD);
		if (!(reg & CMD_SW_RESET))
			return 0;

		udelay(1);
	}

	if (timeout == 1000) {
		dev_err(kdev,
			"timeout waiting for MAC to come out of reset\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void bcmgenet_intr_disable(struct bcmgenet_priv *priv)
{
	/* Mask all interrupts.*/
	bcmgenet_intrl2_0_writel(priv, 0xFFFFFFFF, INTRL2_CPU_MASK_SET);
	bcmgenet_intrl2_0_writel(priv, 0xFFFFFFFF, INTRL2_CPU_CLEAR);
	bcmgenet_intrl2_0_writel(priv, 0, INTRL2_CPU_MASK_CLEAR);
	bcmgenet_intrl2_1_writel(priv, 0xFFFFFFFF, INTRL2_CPU_MASK_SET);
	bcmgenet_intrl2_1_writel(priv, 0xFFFFFFFF, INTRL2_CPU_CLEAR);
	bcmgenet_intrl2_1_writel(priv, 0, INTRL2_CPU_MASK_CLEAR);
}

static int init_umac(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	int ret;
	u32 reg, cpu_mask_clear;

	dev_dbg(&priv->pdev->dev, "bcmgenet: init_umac\n");

	ret = reset_umac(priv);
	if (ret)
		return ret;

	bcmgenet_umac_writel(priv, 0, UMAC_CMD);
	/* clear tx/rx counter */
	bcmgenet_umac_writel(priv,
			     MIB_RESET_RX | MIB_RESET_TX | MIB_RESET_RUNT,
			     UMAC_MIB_CTRL);
	bcmgenet_umac_writel(priv, 0, UMAC_MIB_CTRL);

	bcmgenet_umac_writel(priv, ENET_MAX_MTU_SIZE, UMAC_MAX_FRAME_LEN);

	/* init rx registers, enable ip header optimization */
	reg = bcmgenet_rbuf_readl(priv, RBUF_CTRL);
	reg |= RBUF_ALIGN_2B;
	bcmgenet_rbuf_writel(priv, reg, RBUF_CTRL);

	if (!GENET_IS_V1(priv) && !GENET_IS_V2(priv))
		bcmgenet_rbuf_writel(priv, 1, RBUF_TBUF_SIZE_CTRL);

	bcmgenet_intr_disable(priv);

	cpu_mask_clear = UMAC_IRQ_RXDMA_BDONE;

	dev_dbg(kdev, "%s:Enabling RXDMA_BDONE interrupt\n", __func__);

	/* Monitor cable plug/unplugged event for internal PHY */
	if (phy_is_internal(priv->phydev)) {
		cpu_mask_clear |= (UMAC_IRQ_LINK_DOWN | UMAC_IRQ_LINK_UP);
	} else if (priv->ext_phy) {
		cpu_mask_clear |= (UMAC_IRQ_LINK_DOWN | UMAC_IRQ_LINK_UP);
	} else if (priv->phy_interface == PHY_INTERFACE_MODE_MOCA) {
		reg = bcmgenet_bp_mc_get(priv);
		reg |= BIT(priv->hw_params->bp_in_en_shift);

		/* bp_mask: back pressure mask */
		if (netif_is_multiqueue(priv->dev))
			reg |= priv->hw_params->bp_in_mask;
		else
			reg &= ~priv->hw_params->bp_in_mask;
		bcmgenet_bp_mc_set(priv, reg);
	}

	/* Enable MDIO interrupts on GENET v3+ */
	if (priv->hw_params->flags & GENET_HAS_MDIO_INTR)
		cpu_mask_clear |= UMAC_IRQ_MDIO_DONE | UMAC_IRQ_MDIO_ERROR;

	bcmgenet_intrl2_0_writel(priv, cpu_mask_clear, INTRL2_CPU_MASK_CLEAR);

	/* Enable rx/tx engine.*/
	dev_dbg(kdev, "done init umac\n");

	return 0;
}

/* Initialize all house-keeping variables for a TX ring, along
 * with corresponding hardware registers
 */
static void bcmgenet_init_tx_ring(struct bcmgenet_priv *priv,
				  unsigned int index, unsigned int size,
				  unsigned int write_ptr, unsigned int end_ptr)
{
	struct bcmgenet_tx_ring *ring = &priv->tx_rings[index];
	u32 words_per_bd = WORDS_PER_BD(priv);
	u32 flow_period_val = 0;
	unsigned int first_bd;

	spin_lock_init(&ring->lock);
	ring->index = index;
	if (index == DESC_INDEX) {
		ring->queue = 0;
		ring->int_enable = bcmgenet_tx_ring16_int_enable;
		ring->int_disable = bcmgenet_tx_ring16_int_disable;
	} else {
		ring->queue = index + 1;
		ring->int_enable = bcmgenet_tx_ring_int_enable;
		ring->int_disable = bcmgenet_tx_ring_int_disable;
	}
	ring->cbs = priv->tx_cbs + write_ptr;
	ring->size = size;
	ring->c_index = 0;
	ring->free_bds = size;
	ring->write_ptr = write_ptr;
	ring->cb_ptr = write_ptr;
	ring->end_ptr = end_ptr - 1;
	ring->prod_index = 0;

	/* Set flow period for ring != 16 */
	if (index != DESC_INDEX)
		flow_period_val = ENET_MAX_MTU_SIZE << 16;

	bcmgenet_tdma_ring_writel(priv, index, 0, TDMA_PROD_INDEX);
	bcmgenet_tdma_ring_writel(priv, index, 0, TDMA_CONS_INDEX);
	bcmgenet_tdma_ring_writel(priv, index, 1, DMA_MBUF_DONE_THRESH);
	/* Disable rate control for now */
	bcmgenet_tdma_ring_writel(priv, index, flow_period_val,
				  TDMA_FLOW_PERIOD);
	/* Unclassified traffic goes to ring 16 */
	bcmgenet_tdma_ring_writel(priv, index,
				  ((size << DMA_RING_SIZE_SHIFT) |
				   RX_BUF_LENGTH), DMA_RING_BUF_SIZE);

	first_bd = write_ptr;

	/* Set start and end address, read and write pointers */
	bcmgenet_tdma_ring_writel(priv, index, first_bd * words_per_bd,
				  DMA_START_ADDR);
	bcmgenet_tdma_ring_writel(priv, index, first_bd * words_per_bd,
				  TDMA_READ_PTR);
	bcmgenet_tdma_ring_writel(priv, index, first_bd,
				  TDMA_WRITE_PTR);
	bcmgenet_tdma_ring_writel(priv, index, end_ptr * words_per_bd - 1,
				  DMA_END_ADDR);
}

/* Initialize a RDMA ring */
static int bcmgenet_init_rx_ring(struct bcmgenet_priv *priv,
				 unsigned int index, unsigned int size)
{
	u32 words_per_bd = WORDS_PER_BD(priv);
	int ret;

	priv->num_rx_bds = TOTAL_DESC;
	priv->rx_bds = priv->base + priv->hw_params->rdma_offset;
	priv->rx_bd_assign_ptr = priv->rx_bds;
	priv->rx_bd_assign_index = 0;
	priv->rx_c_index = 0;
	priv->rx_read_ptr = 0;
	priv->rx_cbs = kcalloc(priv->num_rx_bds, sizeof(struct enet_cb),
			       GFP_KERNEL);
	if (!priv->rx_cbs)
		return -ENOMEM;

	ret = bcmgenet_alloc_rx_buffers(priv);
	if (ret) {
		kfree(priv->rx_cbs);
		return ret;
	}

	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_WRITE_PTR);
	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_PROD_INDEX);
	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_CONS_INDEX);
	bcmgenet_rdma_ring_writel(priv, index,
				  ((size << DMA_RING_SIZE_SHIFT) |
				   RX_BUF_LENGTH), DMA_RING_BUF_SIZE);
	bcmgenet_rdma_ring_writel(priv, index, 0, DMA_START_ADDR);
	bcmgenet_rdma_ring_writel(priv, index,
				  words_per_bd * size - 1, DMA_END_ADDR);
	bcmgenet_rdma_ring_writel(priv, index,
				  (DMA_FC_THRESH_LO <<
				   DMA_XOFF_THRESHOLD_SHIFT) |
				   DMA_FC_THRESH_HI, RDMA_XON_XOFF_THRESH);
	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_READ_PTR);

	return ret;
}

/* init multi xmit queues, only available for GENET2+
 * the queue is partitioned as follows:
 *
 * queue 0 - 3 is priority based, each one has 32 descriptors,
 * with queue 0 being the highest priority queue.
 *
 * queue 16 is the default tx queue with GENET_DEFAULT_BD_CNT
 * descriptors: 256 - (number of tx queues * bds per queues) = 128
 * descriptors.
 *
 * The transmit control block pool is then partitioned as following:
 * - tx_cbs[0...127] are for queue 16
 * - tx_ring_cbs[0] points to tx_cbs[128..159]
 * - tx_ring_cbs[1] points to tx_cbs[160..191]
 * - tx_ring_cbs[2] points to tx_cbs[192..223]
 * - tx_ring_cbs[3] points to tx_cbs[224..255]
 */
static void bcmgenet_init_multiq(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned int i, dma_enable;
	u32 reg, dma_ctrl, ring_cfg = 0, dma_priority = 0;

	if (!netif_is_multiqueue(dev)) {
		netdev_warn(dev, "called with non multi queue aware HW\n");
		return;
	}

	dma_ctrl = bcmgenet_tdma_readl(priv, DMA_CTRL);
	dma_enable = dma_ctrl & DMA_EN;
	dma_ctrl &= ~DMA_EN;
	bcmgenet_tdma_writel(priv, dma_ctrl, DMA_CTRL);

	/* Enable strict priority arbiter mode */
	bcmgenet_tdma_writel(priv, DMA_ARBITER_SP, DMA_ARB_CTRL);

	for (i = 0; i < priv->hw_params->tx_queues; i++) {
		/* first 64 tx_cbs are reserved for default tx queue
		 * (ring 16)
		 */
		bcmgenet_init_tx_ring(priv, i, priv->hw_params->bds_cnt,
				      i * priv->hw_params->bds_cnt,
				      (i + 1) * priv->hw_params->bds_cnt);

		/* Configure ring as descriptor ring and setup priority */
		ring_cfg |= 1 << i;
		dma_priority |= ((GENET_Q0_PRIORITY + i) <<
				(GENET_MAX_MQ_CNT + 1) * i);
		dma_ctrl |= 1 << (i + DMA_RING_BUF_EN_SHIFT);
	}

	/* Enable rings */
	reg = bcmgenet_tdma_readl(priv, DMA_RING_CFG);
	reg |= ring_cfg;
	bcmgenet_tdma_writel(priv, reg, DMA_RING_CFG);

	/* Use configured rings priority and set ring #16 priority */
	reg = bcmgenet_tdma_readl(priv, DMA_RING_PRIORITY);
	reg |= ((GENET_Q0_PRIORITY + priv->hw_params->tx_queues) << 20);
	reg |= dma_priority;
	bcmgenet_tdma_writel(priv, reg, DMA_PRIORITY);

	/* Configure ring as descriptor ring and re-enable DMA if enabled */
	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	reg |= dma_ctrl;
	if (dma_enable)
		reg |= DMA_EN;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);
}

static int bcmgenet_dma_teardown(struct bcmgenet_priv *priv)
{
	int ret = 0;
	int timeout = 0;
	u32 reg;

	/* Disable TDMA to stop add more frames in TX DMA */
	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	reg &= ~DMA_EN;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);

	/* Check TDMA status register to confirm TDMA is disabled */
	while (timeout++ < DMA_TIMEOUT_VAL) {
		reg = bcmgenet_tdma_readl(priv, DMA_STATUS);
		if (reg & DMA_DISABLED)
			break;

		udelay(1);
	}

	if (timeout == DMA_TIMEOUT_VAL) {
		netdev_warn(priv->dev, "Timed out while disabling TX DMA\n");
		ret = -ETIMEDOUT;
	}

	/* Wait 10ms for packet drain in both tx and rx dma */
	usleep_range(10000, 20000);

	/* Disable RDMA */
	reg = bcmgenet_rdma_readl(priv, DMA_CTRL);
	reg &= ~DMA_EN;
	bcmgenet_rdma_writel(priv, reg, DMA_CTRL);

	timeout = 0;
	/* Check RDMA status register to confirm RDMA is disabled */
	while (timeout++ < DMA_TIMEOUT_VAL) {
		reg = bcmgenet_rdma_readl(priv, DMA_STATUS);
		if (reg & DMA_DISABLED)
			break;

		udelay(1);
	}

	if (timeout == DMA_TIMEOUT_VAL) {
		netdev_warn(priv->dev, "Timed out while disabling RX DMA\n");
		ret = -ETIMEDOUT;
	}

	return ret;
}

static void bcmgenet_fini_dma(struct bcmgenet_priv *priv)
{
	int i;

	/* disable DMA */
	bcmgenet_dma_teardown(priv);

	for (i = 0; i < priv->num_tx_bds; i++) {
		if (priv->tx_cbs[i].skb != NULL) {
			dev_kfree_skb(priv->tx_cbs[i].skb);
			priv->tx_cbs[i].skb = NULL;
		}
	}

	bcmgenet_free_rx_buffers(priv);
	kfree(priv->rx_cbs);
	kfree(priv->tx_cbs);
}

/* init_edma: Initialize DMA control register */
static int bcmgenet_init_dma(struct bcmgenet_priv *priv)
{
	int ret;

	netif_dbg(priv, hw, priv->dev, "bcmgenet: init_edma\n");

	/* by default, enable ring 16 (descriptor based) */
	ret = bcmgenet_init_rx_ring(priv, DESC_INDEX, TOTAL_DESC);
	if (ret) {
		netdev_err(priv->dev, "failed to initialize RX ring\n");
		return ret;
	}

	/* init rDma */
	bcmgenet_rdma_writel(priv, DMA_MAX_BURST_LENGTH, DMA_SCB_BURST_SIZE);

	/* Init tDma */
	bcmgenet_tdma_writel(priv, DMA_MAX_BURST_LENGTH, DMA_SCB_BURST_SIZE);

	/* Initialize common TX ring structures */
	priv->tx_bds = priv->base + priv->hw_params->tdma_offset;
	priv->num_tx_bds = TOTAL_DESC;
	priv->tx_cbs = kcalloc(priv->num_tx_bds, sizeof(struct enet_cb),
			       GFP_KERNEL);
	if (!priv->tx_cbs) {
		bcmgenet_fini_dma(priv);
		return -ENOMEM;
	}

	/* initialize multi xmit queue */
	bcmgenet_init_multiq(priv->dev);

	/* initialize special ring 16 */
	bcmgenet_init_tx_ring(priv, DESC_INDEX, GENET_DEFAULT_BD_CNT,
			      priv->hw_params->tx_queues *
			      priv->hw_params->bds_cnt,
			      TOTAL_DESC);

	return 0;
}

/* NAPI polling method*/
static int bcmgenet_poll(struct napi_struct *napi, int budget)
{
	struct bcmgenet_priv *priv = container_of(napi,
			struct bcmgenet_priv, napi);
	unsigned int work_done;

	/* tx reclaim */
	bcmgenet_tx_reclaim(priv->dev, &priv->tx_rings[DESC_INDEX]);

	work_done = bcmgenet_desc_rx(priv, budget);

	/* Advancing our consumer index*/
	priv->rx_c_index += work_done;
	priv->rx_c_index &= DMA_C_INDEX_MASK;
	bcmgenet_rdma_ring_writel(priv, DESC_INDEX,
				  priv->rx_c_index, RDMA_CONS_INDEX);
	if (work_done < budget) {
		napi_complete(napi);
		bcmgenet_intrl2_0_writel(priv, UMAC_IRQ_RXDMA_BDONE,
					 INTRL2_CPU_MASK_CLEAR);
	}

	return work_done;
}

/* Interrupt bottom half */
static void bcmgenet_irq_task(struct work_struct *work)
{
	struct bcmgenet_priv *priv = container_of(
			work, struct bcmgenet_priv, bcmgenet_irq_work);

	netif_dbg(priv, intr, priv->dev, "%s\n", __func__);

	if (priv->irq0_stat & UMAC_IRQ_MPD_R) {
		priv->irq0_stat &= ~UMAC_IRQ_MPD_R;
		netif_dbg(priv, wol, priv->dev,
			  "magic packet detected, waking up\n");
		bcmgenet_power_up(priv, GENET_POWER_WOL_MAGIC);
	}

	/* Link UP/DOWN event */
	if ((priv->hw_params->flags & GENET_HAS_MDIO_INTR) &&
	    (priv->irq0_stat & (UMAC_IRQ_LINK_UP|UMAC_IRQ_LINK_DOWN))) {
		phy_mac_interrupt(priv->phydev,
				  priv->irq0_stat & UMAC_IRQ_LINK_UP);
		priv->irq0_stat &= ~(UMAC_IRQ_LINK_UP|UMAC_IRQ_LINK_DOWN);
	}
}

/* bcmgenet_isr1: interrupt handler for ring buffer. */
static irqreturn_t bcmgenet_isr1(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;
	unsigned int index;

	/* Save irq status for bottom-half processing. */
	priv->irq1_stat =
		bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_STAT) &
		~priv->int1_mask;
	/* clear interrupts */
	bcmgenet_intrl2_1_writel(priv, priv->irq1_stat, INTRL2_CPU_CLEAR);

	netif_dbg(priv, intr, priv->dev,
		  "%s: IRQ=0x%x\n", __func__, priv->irq1_stat);
	/* Check the MBDONE interrupts.
	 * packet is done, reclaim descriptors
	 */
	if (priv->irq1_stat & 0x0000ffff) {
		index = 0;
		for (index = 0; index < 16; index++) {
			if (priv->irq1_stat & (1 << index))
				bcmgenet_tx_reclaim(priv->dev,
						    &priv->tx_rings[index]);
		}
	}
	return IRQ_HANDLED;
}

/* bcmgenet_isr0: Handle various interrupts. */
static irqreturn_t bcmgenet_isr0(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;

	/* Save irq status for bottom-half processing. */
	priv->irq0_stat =
		bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_STAT) &
		~bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_MASK_STATUS);
	/* clear interrupts */
	bcmgenet_intrl2_0_writel(priv, priv->irq0_stat, INTRL2_CPU_CLEAR);

	netif_dbg(priv, intr, priv->dev,
		  "IRQ=0x%x\n", priv->irq0_stat);

	if (priv->irq0_stat & (UMAC_IRQ_RXDMA_BDONE | UMAC_IRQ_RXDMA_PDONE)) {
		/* We use NAPI(software interrupt throttling, if
		 * Rx Descriptor throttling is not used.
		 * Disable interrupt, will be enabled in the poll method.
		 */
		if (likely(napi_schedule_prep(&priv->napi))) {
			bcmgenet_intrl2_0_writel(priv, UMAC_IRQ_RXDMA_BDONE,
						 INTRL2_CPU_MASK_SET);
			__napi_schedule(&priv->napi);
		}
	}
	if (priv->irq0_stat &
			(UMAC_IRQ_TXDMA_BDONE | UMAC_IRQ_TXDMA_PDONE)) {
		/* Tx reclaim */
		bcmgenet_tx_reclaim(priv->dev, &priv->tx_rings[DESC_INDEX]);
	}
	if (priv->irq0_stat & (UMAC_IRQ_PHY_DET_R |
				UMAC_IRQ_PHY_DET_F |
				UMAC_IRQ_LINK_UP |
				UMAC_IRQ_LINK_DOWN |
				UMAC_IRQ_HFB_SM |
				UMAC_IRQ_HFB_MM |
				UMAC_IRQ_MPD_R)) {
		/* all other interested interrupts handled in bottom half */
		schedule_work(&priv->bcmgenet_irq_work);
	}

	if ((priv->hw_params->flags & GENET_HAS_MDIO_INTR) &&
	    priv->irq0_stat & (UMAC_IRQ_MDIO_DONE | UMAC_IRQ_MDIO_ERROR)) {
		priv->irq0_stat &= ~(UMAC_IRQ_MDIO_DONE | UMAC_IRQ_MDIO_ERROR);
		wake_up(&priv->wq);
	}

	return IRQ_HANDLED;
}

static irqreturn_t bcmgenet_wol_isr(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;

	pm_wakeup_event(&priv->pdev->dev, 0);

	return IRQ_HANDLED;
}

static void bcmgenet_umac_reset(struct bcmgenet_priv *priv)
{
	u32 reg;

	reg = bcmgenet_rbuf_ctrl_get(priv);
	reg |= BIT(1);
	bcmgenet_rbuf_ctrl_set(priv, reg);
	udelay(10);

	reg &= ~BIT(1);
	bcmgenet_rbuf_ctrl_set(priv, reg);
	udelay(10);
}

static void bcmgenet_set_hw_addr(struct bcmgenet_priv *priv,
				 unsigned char *addr)
{
	bcmgenet_umac_writel(priv, (addr[0] << 24) | (addr[1] << 16) |
			(addr[2] << 8) | addr[3], UMAC_MAC0);
	bcmgenet_umac_writel(priv, (addr[4] << 8) | addr[5], UMAC_MAC1);
}

/* Returns a reusable dma control register value */
static u32 bcmgenet_dma_disable(struct bcmgenet_priv *priv)
{
	u32 reg;
	u32 dma_ctrl;

	/* disable DMA */
	dma_ctrl = 1 << (DESC_INDEX + DMA_RING_BUF_EN_SHIFT) | DMA_EN;
	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	reg &= ~dma_ctrl;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);

	reg = bcmgenet_rdma_readl(priv, DMA_CTRL);
	reg &= ~dma_ctrl;
	bcmgenet_rdma_writel(priv, reg, DMA_CTRL);

	bcmgenet_umac_writel(priv, 1, UMAC_TX_FLUSH);
	udelay(10);
	bcmgenet_umac_writel(priv, 0, UMAC_TX_FLUSH);

	return dma_ctrl;
}

static void bcmgenet_enable_dma(struct bcmgenet_priv *priv, u32 dma_ctrl)
{
	u32 reg;

	reg = bcmgenet_rdma_readl(priv, DMA_CTRL);
	reg |= dma_ctrl;
	bcmgenet_rdma_writel(priv, reg, DMA_CTRL);

	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	reg |= dma_ctrl;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);
}

static void bcmgenet_netif_start(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	/* Start the network engine */
	napi_enable(&priv->napi);

	umac_enable_set(priv, CMD_TX_EN | CMD_RX_EN, true);

	if (phy_is_internal(priv->phydev))
		bcmgenet_power_up(priv, GENET_POWER_PASSIVE);

	netif_tx_start_all_queues(dev);

	phy_start(priv->phydev);
}

static int bcmgenet_open(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned long dma_ctrl;
	u32 reg;
	int ret;

	netif_dbg(priv, ifup, dev, "bcmgenet_open\n");

	/* Turn on the clock */
	if (!IS_ERR(priv->clk))
		clk_prepare_enable(priv->clk);

	/* take MAC out of reset */
	bcmgenet_umac_reset(priv);

	ret = init_umac(priv);
	if (ret)
		goto err_clk_disable;

	/* disable ethernet MAC while updating its registers */
	umac_enable_set(priv, CMD_TX_EN | CMD_RX_EN, false);

	/* Make sure we reflect the value of CRC_CMD_FWD */
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	priv->crc_fwd_en = !!(reg & CMD_CRC_FWD);

	bcmgenet_set_hw_addr(priv, dev->dev_addr);

	if (phy_is_internal(priv->phydev)) {
		reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
		reg |= EXT_ENERGY_DET_MASK;
		bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
	}

	/* Disable RX/TX DMA and flush TX queues */
	dma_ctrl = bcmgenet_dma_disable(priv);

	/* Reinitialize TDMA and RDMA and SW housekeeping */
	ret = bcmgenet_init_dma(priv);
	if (ret) {
		netdev_err(dev, "failed to initialize DMA\n");
		goto err_fini_dma;
	}

	/* Always enable ring 16 - descriptor ring */
	bcmgenet_enable_dma(priv, dma_ctrl);

	ret = request_irq(priv->irq0, bcmgenet_isr0, IRQF_SHARED,
			  dev->name, priv);
	if (ret < 0) {
		netdev_err(dev, "can't request IRQ %d\n", priv->irq0);
		goto err_fini_dma;
	}

	ret = request_irq(priv->irq1, bcmgenet_isr1, IRQF_SHARED,
			  dev->name, priv);
	if (ret < 0) {
		netdev_err(dev, "can't request IRQ %d\n", priv->irq1);
		goto err_irq0;
	}

	bcmgenet_netif_start(dev);

	return 0;

err_irq0:
	free_irq(priv->irq0, dev);
err_fini_dma:
	bcmgenet_fini_dma(priv);
err_clk_disable:
	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);
	return ret;
}

static void bcmgenet_netif_stop(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	netif_tx_stop_all_queues(dev);
	napi_disable(&priv->napi);
	phy_stop(priv->phydev);

	bcmgenet_intr_disable(priv);

	/* Wait for pending work items to complete. Since interrupts are
	 * disabled no new work will be scheduled.
	 */
	cancel_work_sync(&priv->bcmgenet_irq_work);

	priv->old_pause = -1;
	priv->old_link = -1;
	priv->old_duplex = -1;
}

static int bcmgenet_close(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;

	netif_dbg(priv, ifdown, dev, "bcmgenet_close\n");

	bcmgenet_netif_stop(dev);

	/* Disable MAC receive */
	umac_enable_set(priv, CMD_RX_EN, false);

	ret = bcmgenet_dma_teardown(priv);
	if (ret)
		return ret;

	/* Disable MAC transmit. TX DMA disabled have to done before this */
	umac_enable_set(priv, CMD_TX_EN, false);

	/* tx reclaim */
	bcmgenet_tx_reclaim_all(dev);
	bcmgenet_fini_dma(priv);

	free_irq(priv->irq0, priv);
	free_irq(priv->irq1, priv);

	if (phy_is_internal(priv->phydev))
		bcmgenet_power_down(priv, GENET_POWER_PASSIVE);

	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);

	return 0;
}

static void bcmgenet_timeout(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	netif_dbg(priv, tx_err, dev, "bcmgenet_timeout\n");

	dev->trans_start = jiffies;

	dev->stats.tx_errors++;

	netif_tx_wake_all_queues(dev);
}

#define MAX_MC_COUNT	16

static inline void bcmgenet_set_mdf_addr(struct bcmgenet_priv *priv,
					 unsigned char *addr,
					 int *i,
					 int *mc)
{
	u32 reg;

	bcmgenet_umac_writel(priv, addr[0] << 8 | addr[1],
			     UMAC_MDF_ADDR + (*i * 4));
	bcmgenet_umac_writel(priv, addr[2] << 24 | addr[3] << 16 |
			     addr[4] << 8 | addr[5],
			     UMAC_MDF_ADDR + ((*i + 1) * 4));
	reg = bcmgenet_umac_readl(priv, UMAC_MDF_CTRL);
	reg |= (1 << (MAX_MC_COUNT - *mc));
	bcmgenet_umac_writel(priv, reg, UMAC_MDF_CTRL);
	*i += 2;
	(*mc)++;
}

static void bcmgenet_set_rx_mode(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int i, mc;
	u32 reg;

	netif_dbg(priv, hw, dev, "%s: %08X\n", __func__, dev->flags);

	/* Promiscuous mode */
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	if (dev->flags & IFF_PROMISC) {
		reg |= CMD_PROMISC;
		bcmgenet_umac_writel(priv, reg, UMAC_CMD);
		bcmgenet_umac_writel(priv, 0, UMAC_MDF_CTRL);
		return;
	} else {
		reg &= ~CMD_PROMISC;
		bcmgenet_umac_writel(priv, reg, UMAC_CMD);
	}

	/* UniMac doesn't support ALLMULTI */
	if (dev->flags & IFF_ALLMULTI) {
		netdev_warn(dev, "ALLMULTI is not supported\n");
		return;
	}

	/* update MDF filter */
	i = 0;
	mc = 0;
	/* Broadcast */
	bcmgenet_set_mdf_addr(priv, dev->broadcast, &i, &mc);
	/* my own address.*/
	bcmgenet_set_mdf_addr(priv, dev->dev_addr, &i, &mc);
	/* Unicast list*/
	if (netdev_uc_count(dev) > (MAX_MC_COUNT - mc))
		return;

	if (!netdev_uc_empty(dev))
		netdev_for_each_uc_addr(ha, dev)
			bcmgenet_set_mdf_addr(priv, ha->addr, &i, &mc);
	/* Multicast */
	if (netdev_mc_empty(dev) || netdev_mc_count(dev) >= (MAX_MC_COUNT - mc))
		return;

	netdev_for_each_mc_addr(ha, dev)
		bcmgenet_set_mdf_addr(priv, ha->addr, &i, &mc);
}

/* Set the hardware MAC address. */
static int bcmgenet_set_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	/* Setting the MAC address at the hardware level is not possible
	 * without disabling the UniMAC RX/TX enable bits.
	 */
	if (netif_running(dev))
		return -EBUSY;

	ether_addr_copy(dev->dev_addr, addr->sa_data);

	return 0;
}

static const struct net_device_ops bcmgenet_netdev_ops = {
	.ndo_open		= bcmgenet_open,
	.ndo_stop		= bcmgenet_close,
	.ndo_start_xmit		= bcmgenet_xmit,
	.ndo_tx_timeout		= bcmgenet_timeout,
	.ndo_set_rx_mode	= bcmgenet_set_rx_mode,
	.ndo_set_mac_address	= bcmgenet_set_mac_addr,
	.ndo_do_ioctl		= bcmgenet_ioctl,
	.ndo_set_features	= bcmgenet_set_features,
};

/* Array of GENET hardware parameters/characteristics */
static struct bcmgenet_hw_params bcmgenet_hw_params[] = {
	[GENET_V1] = {
		.tx_queues = 0,
		.rx_queues = 0,
		.bds_cnt = 0,
		.bp_in_en_shift = 16,
		.bp_in_mask = 0xffff,
		.hfb_filter_cnt = 16,
		.qtag_mask = 0x1F,
		.hfb_offset = 0x1000,
		.rdma_offset = 0x2000,
		.tdma_offset = 0x3000,
		.words_per_bd = 2,
	},
	[GENET_V2] = {
		.tx_queues = 4,
		.rx_queues = 4,
		.bds_cnt = 32,
		.bp_in_en_shift = 16,
		.bp_in_mask = 0xffff,
		.hfb_filter_cnt = 16,
		.qtag_mask = 0x1F,
		.tbuf_offset = 0x0600,
		.hfb_offset = 0x1000,
		.hfb_reg_offset = 0x2000,
		.rdma_offset = 0x3000,
		.tdma_offset = 0x4000,
		.words_per_bd = 2,
		.flags = GENET_HAS_EXT,
	},
	[GENET_V3] = {
		.tx_queues = 4,
		.rx_queues = 4,
		.bds_cnt = 32,
		.bp_in_en_shift = 17,
		.bp_in_mask = 0x1ffff,
		.hfb_filter_cnt = 48,
		.qtag_mask = 0x3F,
		.tbuf_offset = 0x0600,
		.hfb_offset = 0x8000,
		.hfb_reg_offset = 0xfc00,
		.rdma_offset = 0x10000,
		.tdma_offset = 0x11000,
		.words_per_bd = 2,
		.flags = GENET_HAS_EXT | GENET_HAS_MDIO_INTR,
	},
	[GENET_V4] = {
		.tx_queues = 4,
		.rx_queues = 4,
		.bds_cnt = 32,
		.bp_in_en_shift = 17,
		.bp_in_mask = 0x1ffff,
		.hfb_filter_cnt = 48,
		.qtag_mask = 0x3F,
		.tbuf_offset = 0x0600,
		.hfb_offset = 0x8000,
		.hfb_reg_offset = 0xfc00,
		.rdma_offset = 0x2000,
		.tdma_offset = 0x4000,
		.words_per_bd = 3,
		.flags = GENET_HAS_40BITS | GENET_HAS_EXT | GENET_HAS_MDIO_INTR,
	},
};

/* Infer hardware parameters from the detected GENET version */
static void bcmgenet_set_hw_params(struct bcmgenet_priv *priv)
{
	struct bcmgenet_hw_params *params;
	u32 reg;
	u8 major;

	if (GENET_IS_V4(priv)) {
		bcmgenet_dma_regs = bcmgenet_dma_regs_v3plus;
		genet_dma_ring_regs = genet_dma_ring_regs_v4;
		priv->dma_rx_chk_bit = DMA_RX_CHK_V3PLUS;
		priv->version = GENET_V4;
	} else if (GENET_IS_V3(priv)) {
		bcmgenet_dma_regs = bcmgenet_dma_regs_v3plus;
		genet_dma_ring_regs = genet_dma_ring_regs_v123;
		priv->dma_rx_chk_bit = DMA_RX_CHK_V3PLUS;
		priv->version = GENET_V3;
	} else if (GENET_IS_V2(priv)) {
		bcmgenet_dma_regs = bcmgenet_dma_regs_v2;
		genet_dma_ring_regs = genet_dma_ring_regs_v123;
		priv->dma_rx_chk_bit = DMA_RX_CHK_V12;
		priv->version = GENET_V2;
	} else if (GENET_IS_V1(priv)) {
		bcmgenet_dma_regs = bcmgenet_dma_regs_v1;
		genet_dma_ring_regs = genet_dma_ring_regs_v123;
		priv->dma_rx_chk_bit = DMA_RX_CHK_V12;
		priv->version = GENET_V1;
	}

	/* enum genet_version starts at 1 */
	priv->hw_params = &bcmgenet_hw_params[priv->version];
	params = priv->hw_params;

	/* Read GENET HW version */
	reg = bcmgenet_sys_readl(priv, SYS_REV_CTRL);
	major = (reg >> 24 & 0x0f);
	if (major == 5)
		major = 4;
	else if (major == 0)
		major = 1;
	if (major != priv->version) {
		dev_err(&priv->pdev->dev,
			"GENET version mismatch, got: %d, configured for: %d\n",
			major, priv->version);
	}

	/* Print the GENET core version */
	dev_info(&priv->pdev->dev, "GENET " GENET_VER_FMT,
		 major, (reg >> 16) & 0x0f, reg & 0xffff);

	/* Store the integrated PHY revision for the MDIO probing function
	 * to pass this information to the PHY driver. The PHY driver expects
	 * to find the PHY major revision in bits 15:8 while the GENET register
	 * stores that information in bits 7:0, account for that.
	 */
	priv->gphy_rev = (reg & 0xffff) << 8;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (!(params->flags & GENET_HAS_40BITS))
		pr_warn("GENET does not support 40-bits PA\n");
#endif

	pr_debug("Configuration for version: %d\n"
		"TXq: %1d, RXq: %1d, BDs: %1d\n"
		"BP << en: %2d, BP msk: 0x%05x\n"
		"HFB count: %2d, QTAQ msk: 0x%05x\n"
		"TBUF: 0x%04x, HFB: 0x%04x, HFBreg: 0x%04x\n"
		"RDMA: 0x%05x, TDMA: 0x%05x\n"
		"Words/BD: %d\n",
		priv->version,
		params->tx_queues, params->rx_queues, params->bds_cnt,
		params->bp_in_en_shift, params->bp_in_mask,
		params->hfb_filter_cnt, params->qtag_mask,
		params->tbuf_offset, params->hfb_offset,
		params->hfb_reg_offset,
		params->rdma_offset, params->tdma_offset,
		params->words_per_bd);
}

static const struct of_device_id bcmgenet_match[] = {
	{ .compatible = "brcm,genet-v1", .data = (void *)GENET_V1 },
	{ .compatible = "brcm,genet-v2", .data = (void *)GENET_V2 },
	{ .compatible = "brcm,genet-v3", .data = (void *)GENET_V3 },
	{ .compatible = "brcm,genet-v4", .data = (void *)GENET_V4 },
	{ },
};

static int bcmgenet_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct bcmgenet_priv *priv;
	struct net_device *dev;
	const void *macaddr;
	struct resource *r;
	int err = -EIO;

	/* Up to GENET_MAX_MQ_CNT + 1 TX queues and a single RX queue */
	dev = alloc_etherdev_mqs(sizeof(*priv), GENET_MAX_MQ_CNT + 1, 1);
	if (!dev) {
		dev_err(&pdev->dev, "can't allocate net device\n");
		return -ENOMEM;
	}

	of_id = of_match_node(bcmgenet_match, dn);
	if (!of_id)
		return -EINVAL;

	priv = netdev_priv(dev);
	priv->irq0 = platform_get_irq(pdev, 0);
	priv->irq1 = platform_get_irq(pdev, 1);
	priv->wol_irq = platform_get_irq(pdev, 2);
	if (!priv->irq0 || !priv->irq1) {
		dev_err(&pdev->dev, "can't find IRQs\n");
		err = -EINVAL;
		goto err;
	}

	macaddr = of_get_mac_address(dn);
	if (!macaddr) {
		dev_err(&pdev->dev, "can't find MAC address\n");
		err = -EINVAL;
		goto err;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(priv->base)) {
		err = PTR_ERR(priv->base);
		goto err;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	dev_set_drvdata(&pdev->dev, dev);
	ether_addr_copy(dev->dev_addr, macaddr);
	dev->watchdog_timeo = 2 * HZ;
	dev->ethtool_ops = &bcmgenet_ethtool_ops;
	dev->netdev_ops = &bcmgenet_netdev_ops;
	netif_napi_add(dev, &priv->napi, bcmgenet_poll, 64);

	priv->msg_enable = netif_msg_init(-1, GENET_MSG_DEFAULT);

	/* Set hardware features */
	dev->hw_features |= NETIF_F_SG | NETIF_F_IP_CSUM |
		NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM;

	/* Request the WOL interrupt and advertise suspend if available */
	priv->wol_irq_disabled = true;
	err = devm_request_irq(&pdev->dev, priv->wol_irq, bcmgenet_wol_isr, 0,
			       dev->name, priv);
	if (!err)
		device_set_wakeup_capable(&pdev->dev, 1);

	/* Set the needed headroom to account for any possible
	 * features enabling/disabling at runtime
	 */
	dev->needed_headroom += 64;

	netdev_boot_setup_check(dev);

	priv->dev = dev;
	priv->pdev = pdev;
	priv->version = (enum bcmgenet_version)of_id->data;

	priv->clk = devm_clk_get(&priv->pdev->dev, "enet");
	if (IS_ERR(priv->clk))
		dev_warn(&priv->pdev->dev, "failed to get enet clock\n");

	if (!IS_ERR(priv->clk))
		clk_prepare_enable(priv->clk);

	bcmgenet_set_hw_params(priv);

	/* Mii wait queue */
	init_waitqueue_head(&priv->wq);
	/* Always use RX_BUF_LENGTH (2KB) buffer for all chips */
	priv->rx_buf_len = RX_BUF_LENGTH;
	INIT_WORK(&priv->bcmgenet_irq_work, bcmgenet_irq_task);

	priv->clk_wol = devm_clk_get(&priv->pdev->dev, "enet-wol");
	if (IS_ERR(priv->clk_wol))
		dev_warn(&priv->pdev->dev, "failed to get enet-wol clock\n");

	err = reset_umac(priv);
	if (err)
		goto err_clk_disable;

	err = bcmgenet_mii_init(dev);
	if (err)
		goto err_clk_disable;

	/* setup number of real queues  + 1 (GENET_V1 has 0 hardware queues
	 * just the ring 16 descriptor based TX
	 */
	netif_set_real_num_tx_queues(priv->dev, priv->hw_params->tx_queues + 1);
	netif_set_real_num_rx_queues(priv->dev, priv->hw_params->rx_queues + 1);

	/* libphy will determine the link state */
	netif_carrier_off(dev);

	/* Turn off the main clock, WOL clock is handled separately */
	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);

	err = register_netdev(dev);
	if (err)
		goto err;

	return err;

err_clk_disable:
	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);
err:
	free_netdev(dev);
	return err;
}

static int bcmgenet_remove(struct platform_device *pdev)
{
	struct bcmgenet_priv *priv = dev_to_priv(&pdev->dev);

	dev_set_drvdata(&pdev->dev, NULL);
	unregister_netdev(priv->dev);
	bcmgenet_mii_exit(priv->dev);
	free_netdev(priv->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bcmgenet_suspend(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;

	if (!netif_running(dev))
		return 0;

	bcmgenet_netif_stop(dev);

	phy_suspend(priv->phydev);

	netif_device_detach(dev);

	/* Disable MAC receive */
	umac_enable_set(priv, CMD_RX_EN, false);

	ret = bcmgenet_dma_teardown(priv);
	if (ret)
		return ret;

	/* Disable MAC transmit. TX DMA disabled have to done before this */
	umac_enable_set(priv, CMD_TX_EN, false);

	/* tx reclaim */
	bcmgenet_tx_reclaim_all(dev);
	bcmgenet_fini_dma(priv);

	/* Prepare the device for Wake-on-LAN and switch to the slow clock */
	if (device_may_wakeup(d) && priv->wolopts) {
		bcmgenet_power_down(priv, GENET_POWER_WOL_MAGIC);
		clk_prepare_enable(priv->clk_wol);
	}

	/* Turn off the clocks */
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int bcmgenet_resume(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned long dma_ctrl;
	int ret;
	u32 reg;

	if (!netif_running(dev))
		return 0;

	/* Turn on the clock */
	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	bcmgenet_umac_reset(priv);

	ret = init_umac(priv);
	if (ret)
		goto out_clk_disable;

	/* From WOL-enabled suspend, switch to regular clock */
	if (priv->wolopts)
		clk_disable_unprepare(priv->clk_wol);

	phy_init_hw(priv->phydev);
	/* Speed settings must be restored */
	bcmgenet_mii_config(priv->dev);

	/* disable ethernet MAC while updating its registers */
	umac_enable_set(priv, CMD_TX_EN | CMD_RX_EN, false);

	bcmgenet_set_hw_addr(priv, dev->dev_addr);

	if (phy_is_internal(priv->phydev)) {
		reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
		reg |= EXT_ENERGY_DET_MASK;
		bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
	}

	if (priv->wolopts)
		bcmgenet_power_up(priv, GENET_POWER_WOL_MAGIC);

	/* Disable RX/TX DMA and flush TX queues */
	dma_ctrl = bcmgenet_dma_disable(priv);

	/* Reinitialize TDMA and RDMA and SW housekeeping */
	ret = bcmgenet_init_dma(priv);
	if (ret) {
		netdev_err(dev, "failed to initialize DMA\n");
		goto out_clk_disable;
	}

	/* Always enable ring 16 - descriptor ring */
	bcmgenet_enable_dma(priv, dma_ctrl);

	netif_device_attach(dev);

	phy_resume(priv->phydev);

	bcmgenet_netif_start(dev);

	return 0;

out_clk_disable:
	clk_disable_unprepare(priv->clk);
	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(bcmgenet_pm_ops, bcmgenet_suspend, bcmgenet_resume);

static struct platform_driver bcmgenet_driver = {
	.probe	= bcmgenet_probe,
	.remove	= bcmgenet_remove,
	.driver	= {
		.name	= "bcmgenet",
		.owner	= THIS_MODULE,
		.of_match_table = bcmgenet_match,
		.pm	= &bcmgenet_pm_ops,
	},
};
module_platform_driver(bcmgenet_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom GENET Ethernet controller driver");
MODULE_ALIAS("platform:bcmgenet");
MODULE_LICENSE("GPL");
