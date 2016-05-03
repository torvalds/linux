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
#include <linux/platform_data/bcmgenet.h>

#include <asm/unaligned.h>

#include "bcmgenet.h"

/* Maximum number of hardware queues, downsized if needed */
#define GENET_MAX_MQ_CNT	4

/* Default highest priority queue for multi queue support */
#define GENET_Q0_PRIORITY	0

#define GENET_Q16_RX_BD_CNT	\
	(TOTAL_DESC - priv->hw_params->rx_queues * priv->hw_params->rx_bds_per_q)
#define GENET_Q16_TX_BD_CNT	\
	(TOTAL_DESC - priv->hw_params->tx_queues * priv->hw_params->tx_bds_per_q)

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
	DMA_PRIORITY_0,
	DMA_PRIORITY_1,
	DMA_PRIORITY_2,
	DMA_INDEX2RING_0,
	DMA_INDEX2RING_1,
	DMA_INDEX2RING_2,
	DMA_INDEX2RING_3,
	DMA_INDEX2RING_4,
	DMA_INDEX2RING_5,
	DMA_INDEX2RING_6,
	DMA_INDEX2RING_7,
	DMA_RING0_TIMEOUT,
	DMA_RING1_TIMEOUT,
	DMA_RING2_TIMEOUT,
	DMA_RING3_TIMEOUT,
	DMA_RING4_TIMEOUT,
	DMA_RING5_TIMEOUT,
	DMA_RING6_TIMEOUT,
	DMA_RING7_TIMEOUT,
	DMA_RING8_TIMEOUT,
	DMA_RING9_TIMEOUT,
	DMA_RING10_TIMEOUT,
	DMA_RING11_TIMEOUT,
	DMA_RING12_TIMEOUT,
	DMA_RING13_TIMEOUT,
	DMA_RING14_TIMEOUT,
	DMA_RING15_TIMEOUT,
	DMA_RING16_TIMEOUT,
};

static const u8 bcmgenet_dma_regs_v3plus[] = {
	[DMA_RING_CFG]		= 0x00,
	[DMA_CTRL]		= 0x04,
	[DMA_STATUS]		= 0x08,
	[DMA_SCB_BURST_SIZE]	= 0x0C,
	[DMA_ARB_CTRL]		= 0x2C,
	[DMA_PRIORITY_0]	= 0x30,
	[DMA_PRIORITY_1]	= 0x34,
	[DMA_PRIORITY_2]	= 0x38,
	[DMA_RING0_TIMEOUT]	= 0x2C,
	[DMA_RING1_TIMEOUT]	= 0x30,
	[DMA_RING2_TIMEOUT]	= 0x34,
	[DMA_RING3_TIMEOUT]	= 0x38,
	[DMA_RING4_TIMEOUT]	= 0x3c,
	[DMA_RING5_TIMEOUT]	= 0x40,
	[DMA_RING6_TIMEOUT]	= 0x44,
	[DMA_RING7_TIMEOUT]	= 0x48,
	[DMA_RING8_TIMEOUT]	= 0x4c,
	[DMA_RING9_TIMEOUT]	= 0x50,
	[DMA_RING10_TIMEOUT]	= 0x54,
	[DMA_RING11_TIMEOUT]	= 0x58,
	[DMA_RING12_TIMEOUT]	= 0x5c,
	[DMA_RING13_TIMEOUT]	= 0x60,
	[DMA_RING14_TIMEOUT]	= 0x64,
	[DMA_RING15_TIMEOUT]	= 0x68,
	[DMA_RING16_TIMEOUT]	= 0x6C,
	[DMA_INDEX2RING_0]	= 0x70,
	[DMA_INDEX2RING_1]	= 0x74,
	[DMA_INDEX2RING_2]	= 0x78,
	[DMA_INDEX2RING_3]	= 0x7C,
	[DMA_INDEX2RING_4]	= 0x80,
	[DMA_INDEX2RING_5]	= 0x84,
	[DMA_INDEX2RING_6]	= 0x88,
	[DMA_INDEX2RING_7]	= 0x8C,
};

static const u8 bcmgenet_dma_regs_v2[] = {
	[DMA_RING_CFG]		= 0x00,
	[DMA_CTRL]		= 0x04,
	[DMA_STATUS]		= 0x08,
	[DMA_SCB_BURST_SIZE]	= 0x0C,
	[DMA_ARB_CTRL]		= 0x30,
	[DMA_PRIORITY_0]	= 0x34,
	[DMA_PRIORITY_1]	= 0x38,
	[DMA_PRIORITY_2]	= 0x3C,
	[DMA_RING0_TIMEOUT]	= 0x2C,
	[DMA_RING1_TIMEOUT]	= 0x30,
	[DMA_RING2_TIMEOUT]	= 0x34,
	[DMA_RING3_TIMEOUT]	= 0x38,
	[DMA_RING4_TIMEOUT]	= 0x3c,
	[DMA_RING5_TIMEOUT]	= 0x40,
	[DMA_RING6_TIMEOUT]	= 0x44,
	[DMA_RING7_TIMEOUT]	= 0x48,
	[DMA_RING8_TIMEOUT]	= 0x4c,
	[DMA_RING9_TIMEOUT]	= 0x50,
	[DMA_RING10_TIMEOUT]	= 0x54,
	[DMA_RING11_TIMEOUT]	= 0x58,
	[DMA_RING12_TIMEOUT]	= 0x5c,
	[DMA_RING13_TIMEOUT]	= 0x60,
	[DMA_RING14_TIMEOUT]	= 0x64,
	[DMA_RING15_TIMEOUT]	= 0x68,
	[DMA_RING16_TIMEOUT]	= 0x6C,
};

static const u8 bcmgenet_dma_regs_v1[] = {
	[DMA_CTRL]		= 0x00,
	[DMA_STATUS]		= 0x04,
	[DMA_SCB_BURST_SIZE]	= 0x0C,
	[DMA_ARB_CTRL]		= 0x30,
	[DMA_PRIORITY_0]	= 0x34,
	[DMA_PRIORITY_1]	= 0x38,
	[DMA_PRIORITY_2]	= 0x3C,
	[DMA_RING0_TIMEOUT]	= 0x2C,
	[DMA_RING1_TIMEOUT]	= 0x30,
	[DMA_RING2_TIMEOUT]	= 0x34,
	[DMA_RING3_TIMEOUT]	= 0x38,
	[DMA_RING4_TIMEOUT]	= 0x3c,
	[DMA_RING5_TIMEOUT]	= 0x40,
	[DMA_RING6_TIMEOUT]	= 0x44,
	[DMA_RING7_TIMEOUT]	= 0x48,
	[DMA_RING8_TIMEOUT]	= 0x4c,
	[DMA_RING9_TIMEOUT]	= 0x50,
	[DMA_RING10_TIMEOUT]	= 0x54,
	[DMA_RING11_TIMEOUT]	= 0x58,
	[DMA_RING12_TIMEOUT]	= 0x5c,
	[DMA_RING13_TIMEOUT]	= 0x60,
	[DMA_RING14_TIMEOUT]	= 0x64,
	[DMA_RING15_TIMEOUT]	= 0x68,
	[DMA_RING16_TIMEOUT]	= 0x6C,
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

static int bcmgenet_get_coalesce(struct net_device *dev,
				 struct ethtool_coalesce *ec)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	ec->tx_max_coalesced_frames =
		bcmgenet_tdma_ring_readl(priv, DESC_INDEX,
					 DMA_MBUF_DONE_THRESH);
	ec->rx_max_coalesced_frames =
		bcmgenet_rdma_ring_readl(priv, DESC_INDEX,
					 DMA_MBUF_DONE_THRESH);
	ec->rx_coalesce_usecs =
		bcmgenet_rdma_readl(priv, DMA_RING16_TIMEOUT) * 8192 / 1000;

	return 0;
}

static int bcmgenet_set_coalesce(struct net_device *dev,
				 struct ethtool_coalesce *ec)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned int i;
	u32 reg;

	/* Base system clock is 125Mhz, DMA timeout is this reference clock
	 * divided by 1024, which yields roughly 8.192us, our maximum value
	 * has to fit in the DMA_TIMEOUT_MASK (16 bits)
	 */
	if (ec->tx_max_coalesced_frames > DMA_INTR_THRESHOLD_MASK ||
	    ec->tx_max_coalesced_frames == 0 ||
	    ec->rx_max_coalesced_frames > DMA_INTR_THRESHOLD_MASK ||
	    ec->rx_coalesce_usecs > (DMA_TIMEOUT_MASK * 8) + 1)
		return -EINVAL;

	if (ec->rx_coalesce_usecs == 0 && ec->rx_max_coalesced_frames == 0)
		return -EINVAL;

	/* GENET TDMA hardware does not support a configurable timeout, but will
	 * always generate an interrupt either after MBDONE packets have been
	 * transmitted, or when the ring is emtpy.
	 */
	if (ec->tx_coalesce_usecs || ec->tx_coalesce_usecs_high ||
	    ec->tx_coalesce_usecs_irq || ec->tx_coalesce_usecs_low)
		return -EOPNOTSUPP;

	/* Program all TX queues with the same values, as there is no
	 * ethtool knob to do coalescing on a per-queue basis
	 */
	for (i = 0; i < priv->hw_params->tx_queues; i++)
		bcmgenet_tdma_ring_writel(priv, i,
					  ec->tx_max_coalesced_frames,
					  DMA_MBUF_DONE_THRESH);
	bcmgenet_tdma_ring_writel(priv, DESC_INDEX,
				  ec->tx_max_coalesced_frames,
				  DMA_MBUF_DONE_THRESH);

	for (i = 0; i < priv->hw_params->rx_queues; i++) {
		bcmgenet_rdma_ring_writel(priv, i,
					  ec->rx_max_coalesced_frames,
					  DMA_MBUF_DONE_THRESH);

		reg = bcmgenet_rdma_readl(priv, DMA_RING0_TIMEOUT + i);
		reg &= ~DMA_TIMEOUT_MASK;
		reg |= DIV_ROUND_UP(ec->rx_coalesce_usecs * 1000, 8192);
		bcmgenet_rdma_writel(priv, reg, DMA_RING0_TIMEOUT + i);
	}

	bcmgenet_rdma_ring_writel(priv, DESC_INDEX,
				  ec->rx_max_coalesced_frames,
				  DMA_MBUF_DONE_THRESH);

	reg = bcmgenet_rdma_readl(priv, DMA_RING16_TIMEOUT);
	reg &= ~DMA_TIMEOUT_MASK;
	reg |= DIV_ROUND_UP(ec->rx_coalesce_usecs * 1000, 8192);
	bcmgenet_rdma_writel(priv, reg, DMA_RING16_TIMEOUT);

	return 0;
}

/* standard ethtool support functions. */
enum bcmgenet_stat_type {
	BCMGENET_STAT_NETDEV = -1,
	BCMGENET_STAT_MIB_RX,
	BCMGENET_STAT_MIB_TX,
	BCMGENET_STAT_RUNT,
	BCMGENET_STAT_MISC,
	BCMGENET_STAT_SOFT,
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
#define STAT_GENET_SOFT_MIB(str, m) STAT_GENET_MIB(str, m, BCMGENET_STAT_SOFT)

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
	STAT_GENET_SOFT_MIB("alloc_rx_buff_failed", mib.alloc_rx_buff_failed),
	STAT_GENET_SOFT_MIB("rx_dma_failed", mib.rx_dma_failed),
	STAT_GENET_SOFT_MIB("tx_dma_failed", mib.tx_dma_failed),
};

#define BCMGENET_STATS_LEN	ARRAY_SIZE(bcmgenet_gstrings_stats)

static void bcmgenet_get_drvinfo(struct net_device *dev,
				 struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, "bcmgenet", sizeof(info->driver));
	strlcpy(info->version, "v2.0", sizeof(info->version));
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
		case BCMGENET_STAT_SOFT:
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

static void bcmgenet_eee_enable_set(struct net_device *dev, bool enable)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 off = priv->hw_params->tbuf_offset + TBUF_ENERGY_CTRL;
	u32 reg;

	if (enable && !priv->clk_eee_enabled) {
		clk_prepare_enable(priv->clk_eee);
		priv->clk_eee_enabled = true;
	}

	reg = bcmgenet_umac_readl(priv, UMAC_EEE_CTRL);
	if (enable)
		reg |= EEE_EN;
	else
		reg &= ~EEE_EN;
	bcmgenet_umac_writel(priv, reg, UMAC_EEE_CTRL);

	/* Enable EEE and switch to a 27Mhz clock automatically */
	reg = __raw_readl(priv->base + off);
	if (enable)
		reg |= TBUF_EEE_EN | TBUF_PM_EN;
	else
		reg &= ~(TBUF_EEE_EN | TBUF_PM_EN);
	__raw_writel(reg, priv->base + off);

	/* Do the same for thing for RBUF */
	reg = bcmgenet_rbuf_readl(priv, RBUF_ENERGY_CTRL);
	if (enable)
		reg |= RBUF_EEE_EN | RBUF_PM_EN;
	else
		reg &= ~(RBUF_EEE_EN | RBUF_PM_EN);
	bcmgenet_rbuf_writel(priv, reg, RBUF_ENERGY_CTRL);

	if (!enable && priv->clk_eee_enabled) {
		clk_disable_unprepare(priv->clk_eee);
		priv->clk_eee_enabled = false;
	}

	priv->eee.eee_enabled = enable;
	priv->eee.eee_active = enable;
}

static int bcmgenet_get_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct ethtool_eee *p = &priv->eee;

	if (GENET_IS_V1(priv))
		return -EOPNOTSUPP;

	e->eee_enabled = p->eee_enabled;
	e->eee_active = p->eee_active;
	e->tx_lpi_timer = bcmgenet_umac_readl(priv, UMAC_EEE_LPI_TIMER);

	return phy_ethtool_get_eee(priv->phydev, e);
}

static int bcmgenet_set_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct ethtool_eee *p = &priv->eee;
	int ret = 0;

	if (GENET_IS_V1(priv))
		return -EOPNOTSUPP;

	p->eee_enabled = e->eee_enabled;

	if (!p->eee_enabled) {
		bcmgenet_eee_enable_set(dev, false);
	} else {
		ret = phy_init_eee(priv->phydev, 0);
		if (ret) {
			netif_err(priv, hw, dev, "EEE initialization failed\n");
			return ret;
		}

		bcmgenet_umac_writel(priv, e->tx_lpi_timer, UMAC_EEE_LPI_TIMER);
		bcmgenet_eee_enable_set(dev, true);
	}

	return phy_ethtool_set_eee(priv->phydev, e);
}

static int bcmgenet_nway_reset(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	return genphy_restart_aneg(priv->phydev);
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
	.get_eee		= bcmgenet_get_eee,
	.set_eee		= bcmgenet_set_eee,
	.nway_reset		= bcmgenet_nway_reset,
	.get_coalesce		= bcmgenet_get_coalesce,
	.set_coalesce		= bcmgenet_set_coalesce,
};

/* Power down the unimac, based on mode. */
static int bcmgenet_power_down(struct bcmgenet_priv *priv,
				enum bcmgenet_power_mode mode)
{
	int ret = 0;
	u32 reg;

	switch (mode) {
	case GENET_POWER_CABLE_SENSE:
		phy_detach(priv->phydev);
		break;

	case GENET_POWER_WOL_MAGIC:
		ret = bcmgenet_wol_power_down_cfg(priv, mode);
		break;

	case GENET_POWER_PASSIVE:
		/* Power down LED */
		if (priv->hw_params->flags & GENET_HAS_EXT) {
			reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
			reg |= (EXT_PWR_DOWN_PHY |
				EXT_PWR_DOWN_DLL | EXT_PWR_DOWN_BIAS);
			bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);

			bcmgenet_phy_power_set(priv->dev, false);
		}
		break;
	default:
		break;
	}

	return 0;
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
	if (mode == GENET_POWER_PASSIVE) {
		bcmgenet_phy_power_set(priv->dev, true);
		bcmgenet_mii_reset(priv->dev);
	}
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

static inline void bcmgenet_rx_ring16_int_disable(struct bcmgenet_rx_ring *ring)
{
	bcmgenet_intrl2_0_writel(ring->priv, UMAC_IRQ_RXDMA_DONE,
				 INTRL2_CPU_MASK_SET);
}

static inline void bcmgenet_rx_ring16_int_enable(struct bcmgenet_rx_ring *ring)
{
	bcmgenet_intrl2_0_writel(ring->priv, UMAC_IRQ_RXDMA_DONE,
				 INTRL2_CPU_MASK_CLEAR);
}

static inline void bcmgenet_rx_ring_int_disable(struct bcmgenet_rx_ring *ring)
{
	bcmgenet_intrl2_1_writel(ring->priv,
				 1 << (UMAC_IRQ1_RX_INTR_SHIFT + ring->index),
				 INTRL2_CPU_MASK_SET);
}

static inline void bcmgenet_rx_ring_int_enable(struct bcmgenet_rx_ring *ring)
{
	bcmgenet_intrl2_1_writel(ring->priv,
				 1 << (UMAC_IRQ1_RX_INTR_SHIFT + ring->index),
				 INTRL2_CPU_MASK_CLEAR);
}

static inline void bcmgenet_tx_ring16_int_disable(struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_0_writel(ring->priv, UMAC_IRQ_TXDMA_DONE,
				 INTRL2_CPU_MASK_SET);
}

static inline void bcmgenet_tx_ring16_int_enable(struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_0_writel(ring->priv, UMAC_IRQ_TXDMA_DONE,
				 INTRL2_CPU_MASK_CLEAR);
}

static inline void bcmgenet_tx_ring_int_enable(struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_1_writel(ring->priv, 1 << ring->index,
				 INTRL2_CPU_MASK_CLEAR);
}

static inline void bcmgenet_tx_ring_int_disable(struct bcmgenet_tx_ring *ring)
{
	bcmgenet_intrl2_1_writel(ring->priv, 1 << ring->index,
				 INTRL2_CPU_MASK_SET);
}

/* Unlocked version of the reclaim routine */
static unsigned int __bcmgenet_tx_reclaim(struct net_device *dev,
					  struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct enet_cb *tx_cb_ptr;
	struct netdev_queue *txq;
	unsigned int pkts_compl = 0;
	unsigned int c_index;
	unsigned int txbds_ready;
	unsigned int txbds_processed = 0;

	/* Compute how many buffers are transmitted since last xmit call */
	c_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_CONS_INDEX);
	c_index &= DMA_C_INDEX_MASK;

	if (likely(c_index >= ring->c_index))
		txbds_ready = c_index - ring->c_index;
	else
		txbds_ready = (DMA_C_INDEX_MASK + 1) - ring->c_index + c_index;

	netif_dbg(priv, tx_done, dev,
		  "%s ring=%d old_c_index=%u c_index=%u txbds_ready=%u\n",
		  __func__, ring->index, ring->c_index, c_index, txbds_ready);

	/* Reclaim transmitted buffers */
	while (txbds_processed < txbds_ready) {
		tx_cb_ptr = &priv->tx_cbs[ring->clean_ptr];
		if (tx_cb_ptr->skb) {
			pkts_compl++;
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += tx_cb_ptr->skb->len;
			dma_unmap_single(&dev->dev,
					 dma_unmap_addr(tx_cb_ptr, dma_addr),
					 dma_unmap_len(tx_cb_ptr, dma_len),
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

		txbds_processed++;
		if (likely(ring->clean_ptr < ring->end_ptr))
			ring->clean_ptr++;
		else
			ring->clean_ptr = ring->cb_ptr;
	}

	ring->free_bds += txbds_processed;
	ring->c_index = (ring->c_index + txbds_processed) & DMA_C_INDEX_MASK;

	if (ring->free_bds > (MAX_SKB_FRAGS + 1)) {
		txq = netdev_get_tx_queue(dev, ring->queue);
		if (netif_tx_queue_stopped(txq))
			netif_tx_wake_queue(txq);
	}

	return pkts_compl;
}

static unsigned int bcmgenet_tx_reclaim(struct net_device *dev,
				struct bcmgenet_tx_ring *ring)
{
	unsigned int released;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	released = __bcmgenet_tx_reclaim(dev, ring);
	spin_unlock_irqrestore(&ring->lock, flags);

	return released;
}

static int bcmgenet_tx_poll(struct napi_struct *napi, int budget)
{
	struct bcmgenet_tx_ring *ring =
		container_of(napi, struct bcmgenet_tx_ring, napi);
	unsigned int work_done = 0;

	work_done = bcmgenet_tx_reclaim(ring->priv->dev, ring);

	if (work_done == 0) {
		napi_complete(napi);
		ring->int_enable(ring);

		return 0;
	}

	return budget;
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
		priv->mib.tx_dma_failed++;
		netif_err(priv, tx_err, dev, "Tx DMA map failed\n");
		dev_kfree_skb(skb);
		return ret;
	}

	dma_unmap_addr_set(tx_cb_ptr, dma_addr, mapping);
	dma_unmap_len_set(tx_cb_ptr, dma_len, skb_len);
	length_status = (skb_len << DMA_BUFLENGTH_SHIFT) | dma_desc_flags |
			(priv->hw_params->qtag_mask << DMA_TX_QTAG_SHIFT) |
			DMA_TX_APPEND_CRC;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		length_status |= DMA_TX_DO_CSUM;

	dmadesc_set(priv, tx_cb_ptr->bd_addr, mapping, length_status);

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
		priv->mib.tx_dma_failed++;
		netif_err(priv, tx_err, dev, "%s: Tx DMA map failed\n",
			  __func__);
		return ret;
	}

	dma_unmap_addr_set(tx_cb_ptr, dma_addr, mapping);
	dma_unmap_len_set(tx_cb_ptr, dma_len, frag->size);

	dmadesc_set(priv, tx_cb_ptr->bd_addr, mapping,
		    (frag->size << DMA_BUFLENGTH_SHIFT) | dma_desc_flags |
		    (priv->hw_params->qtag_mask << DMA_TX_QTAG_SHIFT));

	return 0;
}

/* Reallocate the SKB to put enough headroom in front of it and insert
 * the transmit checksum offsets in the descriptors
 */
static struct sk_buff *bcmgenet_put_tx_csum(struct net_device *dev,
					    struct sk_buff *skb)
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
			dev->stats.tx_dropped++;
			return NULL;
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
			return skb;
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

	return skb;
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
		skb = bcmgenet_put_tx_csum(dev, skb);
		if (!skb) {
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

	/* Decrement total BD count and advance our write pointer */
	ring->free_bds -= nr_frags + 1;
	ring->prod_index += nr_frags + 1;
	ring->prod_index &= DMA_P_INDEX_MASK;

	if (ring->free_bds <= (MAX_SKB_FRAGS + 1))
		netif_tx_stop_queue(txq);

	if (!skb->xmit_more || netif_xmit_stopped(txq))
		/* Packets are ready, update producer index */
		bcmgenet_tdma_ring_writel(priv, ring->index,
					  ring->prod_index, TDMA_PROD_INDEX);
out:
	spin_unlock_irqrestore(&ring->lock, flags);

	return ret;
}

static struct sk_buff *bcmgenet_rx_refill(struct bcmgenet_priv *priv,
					  struct enet_cb *cb)
{
	struct device *kdev = &priv->pdev->dev;
	struct sk_buff *skb;
	struct sk_buff *rx_skb;
	dma_addr_t mapping;

	/* Allocate a new Rx skb */
	skb = netdev_alloc_skb(priv->dev, priv->rx_buf_len + SKB_ALIGNMENT);
	if (!skb) {
		priv->mib.alloc_rx_buff_failed++;
		netif_err(priv, rx_err, priv->dev,
			  "%s: Rx skb allocation failed\n", __func__);
		return NULL;
	}

	/* DMA-map the new Rx skb */
	mapping = dma_map_single(kdev, skb->data, priv->rx_buf_len,
				 DMA_FROM_DEVICE);
	if (dma_mapping_error(kdev, mapping)) {
		priv->mib.rx_dma_failed++;
		dev_kfree_skb_any(skb);
		netif_err(priv, rx_err, priv->dev,
			  "%s: Rx skb DMA mapping failed\n", __func__);
		return NULL;
	}

	/* Grab the current Rx skb from the ring and DMA-unmap it */
	rx_skb = cb->skb;
	if (likely(rx_skb))
		dma_unmap_single(kdev, dma_unmap_addr(cb, dma_addr),
				 priv->rx_buf_len, DMA_FROM_DEVICE);

	/* Put the new Rx skb on the ring */
	cb->skb = skb;
	dma_unmap_addr_set(cb, dma_addr, mapping);
	dmadesc_set_addr(priv, cb->bd_addr, mapping);

	/* Return the current Rx skb to caller */
	return rx_skb;
}

/* bcmgenet_desc_rx - descriptor based rx process.
 * this could be called from bottom half, or from NAPI polling method.
 */
static unsigned int bcmgenet_desc_rx(struct bcmgenet_rx_ring *ring,
				     unsigned int budget)
{
	struct bcmgenet_priv *priv = ring->priv;
	struct net_device *dev = priv->dev;
	struct enet_cb *cb;
	struct sk_buff *skb;
	u32 dma_length_status;
	unsigned long dma_flag;
	int len;
	unsigned int rxpktprocessed = 0, rxpkttoprocess;
	unsigned int p_index;
	unsigned int discards;
	unsigned int chksum_ok = 0;

	p_index = bcmgenet_rdma_ring_readl(priv, ring->index, RDMA_PROD_INDEX);

	discards = (p_index >> DMA_P_INDEX_DISCARD_CNT_SHIFT) &
		   DMA_P_INDEX_DISCARD_CNT_MASK;
	if (discards > ring->old_discards) {
		discards = discards - ring->old_discards;
		dev->stats.rx_missed_errors += discards;
		dev->stats.rx_errors += discards;
		ring->old_discards += discards;

		/* Clear HW register when we reach 75% of maximum 0xFFFF */
		if (ring->old_discards >= 0xC000) {
			ring->old_discards = 0;
			bcmgenet_rdma_ring_writel(priv, ring->index, 0,
						  RDMA_PROD_INDEX);
		}
	}

	p_index &= DMA_P_INDEX_MASK;

	if (likely(p_index >= ring->c_index))
		rxpkttoprocess = p_index - ring->c_index;
	else
		rxpkttoprocess = (DMA_C_INDEX_MASK + 1) - ring->c_index +
				 p_index;

	netif_dbg(priv, rx_status, dev,
		  "RDMA: rxpkttoprocess=%d\n", rxpkttoprocess);

	while ((rxpktprocessed < rxpkttoprocess) &&
	       (rxpktprocessed < budget)) {
		cb = &priv->rx_cbs[ring->read_ptr];
		skb = bcmgenet_rx_refill(priv, cb);

		if (unlikely(!skb)) {
			dev->stats.rx_dropped++;
			goto next;
		}

		if (!priv->desc_64b_en) {
			dma_length_status =
				dmadesc_get_length_status(priv, cb->bd_addr);
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
			  __func__, p_index, ring->c_index,
			  ring->read_ptr, dma_length_status);

		if (unlikely(!(dma_flag & DMA_EOP) || !(dma_flag & DMA_SOP))) {
			netif_err(priv, rx_status, dev,
				  "dropping fragmented packet!\n");
			dev->stats.rx_errors++;
			dev_kfree_skb_any(skb);
			goto next;
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
			dev->stats.rx_errors++;
			dev_kfree_skb_any(skb);
			goto next;
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
		napi_gro_receive(&ring->napi, skb);
		netif_dbg(priv, rx_status, dev, "pushed up to kernel\n");

next:
		rxpktprocessed++;
		if (likely(ring->read_ptr < ring->end_ptr))
			ring->read_ptr++;
		else
			ring->read_ptr = ring->cb_ptr;

		ring->c_index = (ring->c_index + 1) & DMA_C_INDEX_MASK;
		bcmgenet_rdma_ring_writel(priv, ring->index, ring->c_index, RDMA_CONS_INDEX);
	}

	return rxpktprocessed;
}

/* Rx NAPI polling method */
static int bcmgenet_rx_poll(struct napi_struct *napi, int budget)
{
	struct bcmgenet_rx_ring *ring = container_of(napi,
			struct bcmgenet_rx_ring, napi);
	unsigned int work_done;

	work_done = bcmgenet_desc_rx(ring, budget);

	if (work_done < budget) {
		napi_complete(napi);
		ring->int_enable(ring);
	}

	return work_done;
}

/* Assign skb to RX DMA descriptor. */
static int bcmgenet_alloc_rx_buffers(struct bcmgenet_priv *priv,
				     struct bcmgenet_rx_ring *ring)
{
	struct enet_cb *cb;
	struct sk_buff *skb;
	int i;

	netif_dbg(priv, hw, priv->dev, "%s\n", __func__);

	/* loop here for each buffer needing assign */
	for (i = 0; i < ring->size; i++) {
		cb = ring->cbs + i;
		skb = bcmgenet_rx_refill(priv, cb);
		if (skb)
			dev_kfree_skb_any(skb);
		if (!cb->skb)
			return -ENOMEM;
	}

	return 0;
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

static void bcmgenet_link_intr_enable(struct bcmgenet_priv *priv)
{
	u32 int0_enable = 0;

	/* Monitor cable plug/unplugged event for internal PHY, external PHY
	 * and MoCA PHY
	 */
	if (priv->internal_phy) {
		int0_enable |= UMAC_IRQ_LINK_EVENT;
	} else if (priv->ext_phy) {
		int0_enable |= UMAC_IRQ_LINK_EVENT;
	} else if (priv->phy_interface == PHY_INTERFACE_MODE_MOCA) {
		if (priv->hw_params->flags & GENET_HAS_MOCA_LINK_DET)
			int0_enable |= UMAC_IRQ_LINK_EVENT;
	}
	bcmgenet_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);
}

static int init_umac(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	int ret;
	u32 reg;
	u32 int0_enable = 0;
	u32 int1_enable = 0;
	int i;

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

	/* Enable Rx default queue 16 interrupts */
	int0_enable |= UMAC_IRQ_RXDMA_DONE;

	/* Enable Tx default queue 16 interrupts */
	int0_enable |= UMAC_IRQ_TXDMA_DONE;

	/* Configure backpressure vectors for MoCA */
	if (priv->phy_interface == PHY_INTERFACE_MODE_MOCA) {
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
		int0_enable |= (UMAC_IRQ_MDIO_DONE | UMAC_IRQ_MDIO_ERROR);

	/* Enable Rx priority queue interrupts */
	for (i = 0; i < priv->hw_params->rx_queues; ++i)
		int1_enable |= (1 << (UMAC_IRQ1_RX_INTR_SHIFT + i));

	/* Enable Tx priority queue interrupts */
	for (i = 0; i < priv->hw_params->tx_queues; ++i)
		int1_enable |= (1 << i);

	bcmgenet_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);
	bcmgenet_intrl2_1_writel(priv, int1_enable, INTRL2_CPU_MASK_CLEAR);

	/* Enable rx/tx engine.*/
	dev_dbg(kdev, "done init umac\n");

	return 0;
}

/* Initialize a Tx ring along with corresponding hardware registers */
static void bcmgenet_init_tx_ring(struct bcmgenet_priv *priv,
				  unsigned int index, unsigned int size,
				  unsigned int start_ptr, unsigned int end_ptr)
{
	struct bcmgenet_tx_ring *ring = &priv->tx_rings[index];
	u32 words_per_bd = WORDS_PER_BD(priv);
	u32 flow_period_val = 0;

	spin_lock_init(&ring->lock);
	ring->priv = priv;
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
	ring->cbs = priv->tx_cbs + start_ptr;
	ring->size = size;
	ring->clean_ptr = start_ptr;
	ring->c_index = 0;
	ring->free_bds = size;
	ring->write_ptr = start_ptr;
	ring->cb_ptr = start_ptr;
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
	bcmgenet_tdma_ring_writel(priv, index,
				  ((size << DMA_RING_SIZE_SHIFT) |
				   RX_BUF_LENGTH), DMA_RING_BUF_SIZE);

	/* Set start and end address, read and write pointers */
	bcmgenet_tdma_ring_writel(priv, index, start_ptr * words_per_bd,
				  DMA_START_ADDR);
	bcmgenet_tdma_ring_writel(priv, index, start_ptr * words_per_bd,
				  TDMA_READ_PTR);
	bcmgenet_tdma_ring_writel(priv, index, start_ptr * words_per_bd,
				  TDMA_WRITE_PTR);
	bcmgenet_tdma_ring_writel(priv, index, end_ptr * words_per_bd - 1,
				  DMA_END_ADDR);
}

/* Initialize a RDMA ring */
static int bcmgenet_init_rx_ring(struct bcmgenet_priv *priv,
				 unsigned int index, unsigned int size,
				 unsigned int start_ptr, unsigned int end_ptr)
{
	struct bcmgenet_rx_ring *ring = &priv->rx_rings[index];
	u32 words_per_bd = WORDS_PER_BD(priv);
	int ret;

	ring->priv = priv;
	ring->index = index;
	if (index == DESC_INDEX) {
		ring->int_enable = bcmgenet_rx_ring16_int_enable;
		ring->int_disable = bcmgenet_rx_ring16_int_disable;
	} else {
		ring->int_enable = bcmgenet_rx_ring_int_enable;
		ring->int_disable = bcmgenet_rx_ring_int_disable;
	}
	ring->cbs = priv->rx_cbs + start_ptr;
	ring->size = size;
	ring->c_index = 0;
	ring->read_ptr = start_ptr;
	ring->cb_ptr = start_ptr;
	ring->end_ptr = end_ptr - 1;

	ret = bcmgenet_alloc_rx_buffers(priv, ring);
	if (ret)
		return ret;

	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_PROD_INDEX);
	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_CONS_INDEX);
	bcmgenet_rdma_ring_writel(priv, index, 1, DMA_MBUF_DONE_THRESH);
	bcmgenet_rdma_ring_writel(priv, index,
				  ((size << DMA_RING_SIZE_SHIFT) |
				   RX_BUF_LENGTH), DMA_RING_BUF_SIZE);
	bcmgenet_rdma_ring_writel(priv, index,
				  (DMA_FC_THRESH_LO <<
				   DMA_XOFF_THRESHOLD_SHIFT) |
				   DMA_FC_THRESH_HI, RDMA_XON_XOFF_THRESH);

	/* Set start and end address, read and write pointers */
	bcmgenet_rdma_ring_writel(priv, index, start_ptr * words_per_bd,
				  DMA_START_ADDR);
	bcmgenet_rdma_ring_writel(priv, index, start_ptr * words_per_bd,
				  RDMA_READ_PTR);
	bcmgenet_rdma_ring_writel(priv, index, start_ptr * words_per_bd,
				  RDMA_WRITE_PTR);
	bcmgenet_rdma_ring_writel(priv, index, end_ptr * words_per_bd - 1,
				  DMA_END_ADDR);

	return ret;
}

static void bcmgenet_init_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i < priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		netif_tx_napi_add(priv->dev, &ring->napi, bcmgenet_tx_poll, 64);
	}

	ring = &priv->tx_rings[DESC_INDEX];
	netif_tx_napi_add(priv->dev, &ring->napi, bcmgenet_tx_poll, 64);
}

static void bcmgenet_enable_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i < priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		napi_enable(&ring->napi);
	}

	ring = &priv->tx_rings[DESC_INDEX];
	napi_enable(&ring->napi);
}

static void bcmgenet_disable_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i < priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		napi_disable(&ring->napi);
	}

	ring = &priv->tx_rings[DESC_INDEX];
	napi_disable(&ring->napi);
}

static void bcmgenet_fini_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i < priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		netif_napi_del(&ring->napi);
	}

	ring = &priv->tx_rings[DESC_INDEX];
	netif_napi_del(&ring->napi);
}

/* Initialize Tx queues
 *
 * Queues 0-3 are priority-based, each one has 32 descriptors,
 * with queue 0 being the highest priority queue.
 *
 * Queue 16 is the default Tx queue with
 * GENET_Q16_TX_BD_CNT = 256 - 4 * 32 = 128 descriptors.
 *
 * The transmit control block pool is then partitioned as follows:
 * - Tx queue 0 uses tx_cbs[0..31]
 * - Tx queue 1 uses tx_cbs[32..63]
 * - Tx queue 2 uses tx_cbs[64..95]
 * - Tx queue 3 uses tx_cbs[96..127]
 * - Tx queue 16 uses tx_cbs[128..255]
 */
static void bcmgenet_init_tx_queues(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 i, dma_enable;
	u32 dma_ctrl, ring_cfg;
	u32 dma_priority[3] = {0, 0, 0};

	dma_ctrl = bcmgenet_tdma_readl(priv, DMA_CTRL);
	dma_enable = dma_ctrl & DMA_EN;
	dma_ctrl &= ~DMA_EN;
	bcmgenet_tdma_writel(priv, dma_ctrl, DMA_CTRL);

	dma_ctrl = 0;
	ring_cfg = 0;

	/* Enable strict priority arbiter mode */
	bcmgenet_tdma_writel(priv, DMA_ARBITER_SP, DMA_ARB_CTRL);

	/* Initialize Tx priority queues */
	for (i = 0; i < priv->hw_params->tx_queues; i++) {
		bcmgenet_init_tx_ring(priv, i, priv->hw_params->tx_bds_per_q,
				      i * priv->hw_params->tx_bds_per_q,
				      (i + 1) * priv->hw_params->tx_bds_per_q);
		ring_cfg |= (1 << i);
		dma_ctrl |= (1 << (i + DMA_RING_BUF_EN_SHIFT));
		dma_priority[DMA_PRIO_REG_INDEX(i)] |=
			((GENET_Q0_PRIORITY + i) << DMA_PRIO_REG_SHIFT(i));
	}

	/* Initialize Tx default queue 16 */
	bcmgenet_init_tx_ring(priv, DESC_INDEX, GENET_Q16_TX_BD_CNT,
			      priv->hw_params->tx_queues *
			      priv->hw_params->tx_bds_per_q,
			      TOTAL_DESC);
	ring_cfg |= (1 << DESC_INDEX);
	dma_ctrl |= (1 << (DESC_INDEX + DMA_RING_BUF_EN_SHIFT));
	dma_priority[DMA_PRIO_REG_INDEX(DESC_INDEX)] |=
		((GENET_Q0_PRIORITY + priv->hw_params->tx_queues) <<
		 DMA_PRIO_REG_SHIFT(DESC_INDEX));

	/* Set Tx queue priorities */
	bcmgenet_tdma_writel(priv, dma_priority[0], DMA_PRIORITY_0);
	bcmgenet_tdma_writel(priv, dma_priority[1], DMA_PRIORITY_1);
	bcmgenet_tdma_writel(priv, dma_priority[2], DMA_PRIORITY_2);

	/* Initialize Tx NAPI */
	bcmgenet_init_tx_napi(priv);

	/* Enable Tx queues */
	bcmgenet_tdma_writel(priv, ring_cfg, DMA_RING_CFG);

	/* Enable Tx DMA */
	if (dma_enable)
		dma_ctrl |= DMA_EN;
	bcmgenet_tdma_writel(priv, dma_ctrl, DMA_CTRL);
}

static void bcmgenet_init_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i < priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		netif_napi_add(priv->dev, &ring->napi, bcmgenet_rx_poll, 64);
	}

	ring = &priv->rx_rings[DESC_INDEX];
	netif_napi_add(priv->dev, &ring->napi, bcmgenet_rx_poll, 64);
}

static void bcmgenet_enable_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i < priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		napi_enable(&ring->napi);
	}

	ring = &priv->rx_rings[DESC_INDEX];
	napi_enable(&ring->napi);
}

static void bcmgenet_disable_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i < priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		napi_disable(&ring->napi);
	}

	ring = &priv->rx_rings[DESC_INDEX];
	napi_disable(&ring->napi);
}

static void bcmgenet_fini_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i < priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		netif_napi_del(&ring->napi);
	}

	ring = &priv->rx_rings[DESC_INDEX];
	netif_napi_del(&ring->napi);
}

/* Initialize Rx queues
 *
 * Queues 0-15 are priority queues. Hardware Filtering Block (HFB) can be
 * used to direct traffic to these queues.
 *
 * Queue 16 is the default Rx queue with GENET_Q16_RX_BD_CNT descriptors.
 */
static int bcmgenet_init_rx_queues(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 i;
	u32 dma_enable;
	u32 dma_ctrl;
	u32 ring_cfg;
	int ret;

	dma_ctrl = bcmgenet_rdma_readl(priv, DMA_CTRL);
	dma_enable = dma_ctrl & DMA_EN;
	dma_ctrl &= ~DMA_EN;
	bcmgenet_rdma_writel(priv, dma_ctrl, DMA_CTRL);

	dma_ctrl = 0;
	ring_cfg = 0;

	/* Initialize Rx priority queues */
	for (i = 0; i < priv->hw_params->rx_queues; i++) {
		ret = bcmgenet_init_rx_ring(priv, i,
					    priv->hw_params->rx_bds_per_q,
					    i * priv->hw_params->rx_bds_per_q,
					    (i + 1) *
					    priv->hw_params->rx_bds_per_q);
		if (ret)
			return ret;

		ring_cfg |= (1 << i);
		dma_ctrl |= (1 << (i + DMA_RING_BUF_EN_SHIFT));
	}

	/* Initialize Rx default queue 16 */
	ret = bcmgenet_init_rx_ring(priv, DESC_INDEX, GENET_Q16_RX_BD_CNT,
				    priv->hw_params->rx_queues *
				    priv->hw_params->rx_bds_per_q,
				    TOTAL_DESC);
	if (ret)
		return ret;

	ring_cfg |= (1 << DESC_INDEX);
	dma_ctrl |= (1 << (DESC_INDEX + DMA_RING_BUF_EN_SHIFT));

	/* Initialize Rx NAPI */
	bcmgenet_init_rx_napi(priv);

	/* Enable rings */
	bcmgenet_rdma_writel(priv, ring_cfg, DMA_RING_CFG);

	/* Configure ring as descriptor ring and re-enable DMA if enabled */
	if (dma_enable)
		dma_ctrl |= DMA_EN;
	bcmgenet_rdma_writel(priv, dma_ctrl, DMA_CTRL);

	return 0;
}

static int bcmgenet_dma_teardown(struct bcmgenet_priv *priv)
{
	int ret = 0;
	int timeout = 0;
	u32 reg;
	u32 dma_ctrl;
	int i;

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

	dma_ctrl = 0;
	for (i = 0; i < priv->hw_params->rx_queues; i++)
		dma_ctrl |= (1 << (i + DMA_RING_BUF_EN_SHIFT));
	reg = bcmgenet_rdma_readl(priv, DMA_CTRL);
	reg &= ~dma_ctrl;
	bcmgenet_rdma_writel(priv, reg, DMA_CTRL);

	dma_ctrl = 0;
	for (i = 0; i < priv->hw_params->tx_queues; i++)
		dma_ctrl |= (1 << (i + DMA_RING_BUF_EN_SHIFT));
	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	reg &= ~dma_ctrl;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);

	return ret;
}

static void bcmgenet_fini_dma(struct bcmgenet_priv *priv)
{
	int i;

	bcmgenet_fini_rx_napi(priv);
	bcmgenet_fini_tx_napi(priv);

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
	unsigned int i;
	struct enet_cb *cb;

	netif_dbg(priv, hw, priv->dev, "%s\n", __func__);

	/* Initialize common Rx ring structures */
	priv->rx_bds = priv->base + priv->hw_params->rdma_offset;
	priv->num_rx_bds = TOTAL_DESC;
	priv->rx_cbs = kcalloc(priv->num_rx_bds, sizeof(struct enet_cb),
			       GFP_KERNEL);
	if (!priv->rx_cbs)
		return -ENOMEM;

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = priv->rx_cbs + i;
		cb->bd_addr = priv->rx_bds + i * DMA_DESC_SIZE;
	}

	/* Initialize common TX ring structures */
	priv->tx_bds = priv->base + priv->hw_params->tdma_offset;
	priv->num_tx_bds = TOTAL_DESC;
	priv->tx_cbs = kcalloc(priv->num_tx_bds, sizeof(struct enet_cb),
			       GFP_KERNEL);
	if (!priv->tx_cbs) {
		kfree(priv->rx_cbs);
		return -ENOMEM;
	}

	for (i = 0; i < priv->num_tx_bds; i++) {
		cb = priv->tx_cbs + i;
		cb->bd_addr = priv->tx_bds + i * DMA_DESC_SIZE;
	}

	/* Init rDma */
	bcmgenet_rdma_writel(priv, DMA_MAX_BURST_LENGTH, DMA_SCB_BURST_SIZE);

	/* Initialize Rx queues */
	ret = bcmgenet_init_rx_queues(priv->dev);
	if (ret) {
		netdev_err(priv->dev, "failed to initialize Rx queues\n");
		bcmgenet_free_rx_buffers(priv);
		kfree(priv->rx_cbs);
		kfree(priv->tx_cbs);
		return ret;
	}

	/* Init tDma */
	bcmgenet_tdma_writel(priv, DMA_MAX_BURST_LENGTH, DMA_SCB_BURST_SIZE);

	/* Initialize Tx queues */
	bcmgenet_init_tx_queues(priv->dev);

	return 0;
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
	if (priv->irq0_stat & UMAC_IRQ_LINK_EVENT) {
		phy_mac_interrupt(priv->phydev,
				  !!(priv->irq0_stat & UMAC_IRQ_LINK_UP));
		priv->irq0_stat &= ~UMAC_IRQ_LINK_EVENT;
	}
}

/* bcmgenet_isr1: handle Rx and Tx priority queues */
static irqreturn_t bcmgenet_isr1(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;
	struct bcmgenet_rx_ring *rx_ring;
	struct bcmgenet_tx_ring *tx_ring;
	unsigned int index;

	/* Save irq status for bottom-half processing. */
	priv->irq1_stat =
		bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_STAT) &
		~bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_MASK_STATUS);

	/* clear interrupts */
	bcmgenet_intrl2_1_writel(priv, priv->irq1_stat, INTRL2_CPU_CLEAR);

	netif_dbg(priv, intr, priv->dev,
		  "%s: IRQ=0x%x\n", __func__, priv->irq1_stat);

	/* Check Rx priority queue interrupts */
	for (index = 0; index < priv->hw_params->rx_queues; index++) {
		if (!(priv->irq1_stat & BIT(UMAC_IRQ1_RX_INTR_SHIFT + index)))
			continue;

		rx_ring = &priv->rx_rings[index];

		if (likely(napi_schedule_prep(&rx_ring->napi))) {
			rx_ring->int_disable(rx_ring);
			__napi_schedule(&rx_ring->napi);
		}
	}

	/* Check Tx priority queue interrupts */
	for (index = 0; index < priv->hw_params->tx_queues; index++) {
		if (!(priv->irq1_stat & BIT(index)))
			continue;

		tx_ring = &priv->tx_rings[index];

		if (likely(napi_schedule_prep(&tx_ring->napi))) {
			tx_ring->int_disable(tx_ring);
			__napi_schedule(&tx_ring->napi);
		}
	}

	return IRQ_HANDLED;
}

/* bcmgenet_isr0: handle Rx and Tx default queues + other stuff */
static irqreturn_t bcmgenet_isr0(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;
	struct bcmgenet_rx_ring *rx_ring;
	struct bcmgenet_tx_ring *tx_ring;

	/* Save irq status for bottom-half processing. */
	priv->irq0_stat =
		bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_STAT) &
		~bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_MASK_STATUS);

	/* clear interrupts */
	bcmgenet_intrl2_0_writel(priv, priv->irq0_stat, INTRL2_CPU_CLEAR);

	netif_dbg(priv, intr, priv->dev,
		  "IRQ=0x%x\n", priv->irq0_stat);

	if (priv->irq0_stat & UMAC_IRQ_RXDMA_DONE) {
		rx_ring = &priv->rx_rings[DESC_INDEX];

		if (likely(napi_schedule_prep(&rx_ring->napi))) {
			rx_ring->int_disable(rx_ring);
			__napi_schedule(&rx_ring->napi);
		}
	}

	if (priv->irq0_stat & UMAC_IRQ_TXDMA_DONE) {
		tx_ring = &priv->tx_rings[DESC_INDEX];

		if (likely(napi_schedule_prep(&tx_ring->napi))) {
			tx_ring->int_disable(tx_ring);
			__napi_schedule(&tx_ring->napi);
		}
	}

	if (priv->irq0_stat & (UMAC_IRQ_PHY_DET_R |
				UMAC_IRQ_PHY_DET_F |
				UMAC_IRQ_LINK_EVENT |
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

#ifdef CONFIG_NET_POLL_CONTROLLER
static void bcmgenet_poll_controller(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	/* Invoke the main RX/TX interrupt handler */
	disable_irq(priv->irq0);
	bcmgenet_isr0(priv->irq0, priv);
	enable_irq(priv->irq0);

	/* And the interrupt handler for RX/TX priority queues */
	disable_irq(priv->irq1);
	bcmgenet_isr1(priv->irq1, priv);
	enable_irq(priv->irq1);
}
#endif

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

static bool bcmgenet_hfb_is_filter_enabled(struct bcmgenet_priv *priv,
					   u32 f_index)
{
	u32 offset;
	u32 reg;

	offset = HFB_FLT_ENABLE_V3PLUS + (f_index < 32) * sizeof(u32);
	reg = bcmgenet_hfb_reg_readl(priv, offset);
	return !!(reg & (1 << (f_index % 32)));
}

static void bcmgenet_hfb_enable_filter(struct bcmgenet_priv *priv, u32 f_index)
{
	u32 offset;
	u32 reg;

	offset = HFB_FLT_ENABLE_V3PLUS + (f_index < 32) * sizeof(u32);
	reg = bcmgenet_hfb_reg_readl(priv, offset);
	reg |= (1 << (f_index % 32));
	bcmgenet_hfb_reg_writel(priv, reg, offset);
}

static void bcmgenet_hfb_set_filter_rx_queue_mapping(struct bcmgenet_priv *priv,
						     u32 f_index, u32 rx_queue)
{
	u32 offset;
	u32 reg;

	offset = f_index / 8;
	reg = bcmgenet_rdma_readl(priv, DMA_INDEX2RING_0 + offset);
	reg &= ~(0xF << (4 * (f_index % 8)));
	reg |= ((rx_queue & 0xF) << (4 * (f_index % 8)));
	bcmgenet_rdma_writel(priv, reg, DMA_INDEX2RING_0 + offset);
}

static void bcmgenet_hfb_set_filter_length(struct bcmgenet_priv *priv,
					   u32 f_index, u32 f_length)
{
	u32 offset;
	u32 reg;

	offset = HFB_FLT_LEN_V3PLUS +
		 ((priv->hw_params->hfb_filter_cnt - 1 - f_index) / 4) *
		 sizeof(u32);
	reg = bcmgenet_hfb_reg_readl(priv, offset);
	reg &= ~(0xFF << (8 * (f_index % 4)));
	reg |= ((f_length & 0xFF) << (8 * (f_index % 4)));
	bcmgenet_hfb_reg_writel(priv, reg, offset);
}

static int bcmgenet_hfb_find_unused_filter(struct bcmgenet_priv *priv)
{
	u32 f_index;

	for (f_index = 0; f_index < priv->hw_params->hfb_filter_cnt; f_index++)
		if (!bcmgenet_hfb_is_filter_enabled(priv, f_index))
			return f_index;

	return -ENOMEM;
}

/* bcmgenet_hfb_add_filter
 *
 * Add new filter to Hardware Filter Block to match and direct Rx traffic to
 * desired Rx queue.
 *
 * f_data is an array of unsigned 32-bit integers where each 32-bit integer
 * provides filter data for 2 bytes (4 nibbles) of Rx frame:
 *
 * bits 31:20 - unused
 * bit  19    - nibble 0 match enable
 * bit  18    - nibble 1 match enable
 * bit  17    - nibble 2 match enable
 * bit  16    - nibble 3 match enable
 * bits 15:12 - nibble 0 data
 * bits 11:8  - nibble 1 data
 * bits 7:4   - nibble 2 data
 * bits 3:0   - nibble 3 data
 *
 * Example:
 * In order to match:
 * - Ethernet frame type = 0x0800 (IP)
 * - IP version field = 4
 * - IP protocol field = 0x11 (UDP)
 *
 * The following filter is needed:
 * u32 hfb_filter_ipv4_udp[] = {
 *   Rx frame offset 0x00: 0x00000000, 0x00000000, 0x00000000, 0x00000000,
 *   Rx frame offset 0x08: 0x00000000, 0x00000000, 0x000F0800, 0x00084000,
 *   Rx frame offset 0x10: 0x00000000, 0x00000000, 0x00000000, 0x00030011,
 * };
 *
 * To add the filter to HFB and direct the traffic to Rx queue 0, call:
 * bcmgenet_hfb_add_filter(priv, hfb_filter_ipv4_udp,
 *                         ARRAY_SIZE(hfb_filter_ipv4_udp), 0);
 */
int bcmgenet_hfb_add_filter(struct bcmgenet_priv *priv, u32 *f_data,
			    u32 f_length, u32 rx_queue)
{
	int f_index;
	u32 i;

	f_index = bcmgenet_hfb_find_unused_filter(priv);
	if (f_index < 0)
		return -ENOMEM;

	if (f_length > priv->hw_params->hfb_filter_size)
		return -EINVAL;

	for (i = 0; i < f_length; i++)
		bcmgenet_hfb_writel(priv, f_data[i],
			(f_index * priv->hw_params->hfb_filter_size + i) *
			sizeof(u32));

	bcmgenet_hfb_set_filter_length(priv, f_index, 2 * f_length);
	bcmgenet_hfb_set_filter_rx_queue_mapping(priv, f_index, rx_queue);
	bcmgenet_hfb_enable_filter(priv, f_index);
	bcmgenet_hfb_reg_writel(priv, 0x1, HFB_CTRL);

	return 0;
}

/* bcmgenet_hfb_clear
 *
 * Clear Hardware Filter Block and disable all filtering.
 */
static void bcmgenet_hfb_clear(struct bcmgenet_priv *priv)
{
	u32 i;

	bcmgenet_hfb_reg_writel(priv, 0x0, HFB_CTRL);
	bcmgenet_hfb_reg_writel(priv, 0x0, HFB_FLT_ENABLE_V3PLUS);
	bcmgenet_hfb_reg_writel(priv, 0x0, HFB_FLT_ENABLE_V3PLUS + 4);

	for (i = DMA_INDEX2RING_0; i <= DMA_INDEX2RING_7; i++)
		bcmgenet_rdma_writel(priv, 0x0, i);

	for (i = 0; i < (priv->hw_params->hfb_filter_cnt / 4); i++)
		bcmgenet_hfb_reg_writel(priv, 0x0,
					HFB_FLT_LEN_V3PLUS + i * sizeof(u32));

	for (i = 0; i < priv->hw_params->hfb_filter_cnt *
			priv->hw_params->hfb_filter_size; i++)
		bcmgenet_hfb_writel(priv, 0x0, i * sizeof(u32));
}

static void bcmgenet_hfb_init(struct bcmgenet_priv *priv)
{
	if (GENET_IS_V1(priv) || GENET_IS_V2(priv))
		return;

	bcmgenet_hfb_clear(priv);
}

static void bcmgenet_netif_start(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	/* Start the network engine */
	bcmgenet_enable_rx_napi(priv);
	bcmgenet_enable_tx_napi(priv);

	umac_enable_set(priv, CMD_TX_EN | CMD_RX_EN, true);

	netif_tx_start_all_queues(dev);

	/* Monitor link interrupts now */
	bcmgenet_link_intr_enable(priv);

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
	clk_prepare_enable(priv->clk);

	/* If this is an internal GPHY, power it back on now, before UniMAC is
	 * brought out of reset as absolutely no UniMAC activity is allowed
	 */
	if (priv->internal_phy)
		bcmgenet_power_up(priv, GENET_POWER_PASSIVE);

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

	if (priv->internal_phy) {
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
		goto err_clk_disable;
	}

	/* Always enable ring 16 - descriptor ring */
	bcmgenet_enable_dma(priv, dma_ctrl);

	/* HFB init */
	bcmgenet_hfb_init(priv);

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

	ret = bcmgenet_mii_probe(dev);
	if (ret) {
		netdev_err(dev, "failed to connect to PHY\n");
		goto err_irq1;
	}

	bcmgenet_netif_start(dev);

	return 0;

err_irq1:
	free_irq(priv->irq1, priv);
err_irq0:
	free_irq(priv->irq0, priv);
err_fini_dma:
	bcmgenet_fini_dma(priv);
err_clk_disable:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static void bcmgenet_netif_stop(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	netif_tx_stop_all_queues(dev);
	phy_stop(priv->phydev);
	bcmgenet_intr_disable(priv);
	bcmgenet_disable_rx_napi(priv);
	bcmgenet_disable_tx_napi(priv);

	/* Wait for pending work items to complete. Since interrupts are
	 * disabled no new work will be scheduled.
	 */
	cancel_work_sync(&priv->bcmgenet_irq_work);

	priv->old_link = -1;
	priv->old_speed = -1;
	priv->old_duplex = -1;
	priv->old_pause = -1;
}

static int bcmgenet_close(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;

	netif_dbg(priv, ifdown, dev, "bcmgenet_close\n");

	bcmgenet_netif_stop(dev);

	/* Really kill the PHY state machine and disconnect from it */
	phy_disconnect(priv->phydev);

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

	if (priv->internal_phy)
		ret = bcmgenet_power_down(priv, GENET_POWER_PASSIVE);

	clk_disable_unprepare(priv->clk);

	return ret;
}

static void bcmgenet_dump_tx_queue(struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_priv *priv = ring->priv;
	u32 p_index, c_index, intsts, intmsk;
	struct netdev_queue *txq;
	unsigned int free_bds;
	unsigned long flags;
	bool txq_stopped;

	if (!netif_msg_tx_err(priv))
		return;

	txq = netdev_get_tx_queue(priv->dev, ring->queue);

	spin_lock_irqsave(&ring->lock, flags);
	if (ring->index == DESC_INDEX) {
		intsts = ~bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_MASK_STATUS);
		intmsk = UMAC_IRQ_TXDMA_DONE | UMAC_IRQ_TXDMA_MBDONE;
	} else {
		intsts = ~bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_MASK_STATUS);
		intmsk = 1 << ring->index;
	}
	c_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_CONS_INDEX);
	p_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_PROD_INDEX);
	txq_stopped = netif_tx_queue_stopped(txq);
	free_bds = ring->free_bds;
	spin_unlock_irqrestore(&ring->lock, flags);

	netif_err(priv, tx_err, priv->dev, "Ring %d queue %d status summary\n"
		  "TX queue status: %s, interrupts: %s\n"
		  "(sw)free_bds: %d (sw)size: %d\n"
		  "(sw)p_index: %d (hw)p_index: %d\n"
		  "(sw)c_index: %d (hw)c_index: %d\n"
		  "(sw)clean_p: %d (sw)write_p: %d\n"
		  "(sw)cb_ptr: %d (sw)end_ptr: %d\n",
		  ring->index, ring->queue,
		  txq_stopped ? "stopped" : "active",
		  intsts & intmsk ? "enabled" : "disabled",
		  free_bds, ring->size,
		  ring->prod_index, p_index & DMA_P_INDEX_MASK,
		  ring->c_index, c_index & DMA_C_INDEX_MASK,
		  ring->clean_ptr, ring->write_ptr,
		  ring->cb_ptr, ring->end_ptr);
}

static void bcmgenet_timeout(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 int0_enable = 0;
	u32 int1_enable = 0;
	unsigned int q;

	netif_dbg(priv, tx_err, dev, "bcmgenet_timeout\n");

	for (q = 0; q < priv->hw_params->tx_queues; q++)
		bcmgenet_dump_tx_queue(&priv->tx_rings[q]);
	bcmgenet_dump_tx_queue(&priv->tx_rings[DESC_INDEX]);

	bcmgenet_tx_reclaim_all(dev);

	for (q = 0; q < priv->hw_params->tx_queues; q++)
		int1_enable |= (1 << q);

	int0_enable = UMAC_IRQ_TXDMA_DONE;

	/* Re-enable TX interrupts if disabled */
	bcmgenet_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);
	bcmgenet_intrl2_1_writel(priv, int1_enable, INTRL2_CPU_MASK_CLEAR);

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
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= bcmgenet_poll_controller,
#endif
};

/* Array of GENET hardware parameters/characteristics */
static struct bcmgenet_hw_params bcmgenet_hw_params[] = {
	[GENET_V1] = {
		.tx_queues = 0,
		.tx_bds_per_q = 0,
		.rx_queues = 0,
		.rx_bds_per_q = 0,
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
		.tx_bds_per_q = 32,
		.rx_queues = 0,
		.rx_bds_per_q = 0,
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
		.tx_bds_per_q = 32,
		.rx_queues = 0,
		.rx_bds_per_q = 0,
		.bp_in_en_shift = 17,
		.bp_in_mask = 0x1ffff,
		.hfb_filter_cnt = 48,
		.hfb_filter_size = 128,
		.qtag_mask = 0x3F,
		.tbuf_offset = 0x0600,
		.hfb_offset = 0x8000,
		.hfb_reg_offset = 0xfc00,
		.rdma_offset = 0x10000,
		.tdma_offset = 0x11000,
		.words_per_bd = 2,
		.flags = GENET_HAS_EXT | GENET_HAS_MDIO_INTR |
			 GENET_HAS_MOCA_LINK_DET,
	},
	[GENET_V4] = {
		.tx_queues = 4,
		.tx_bds_per_q = 32,
		.rx_queues = 0,
		.rx_bds_per_q = 0,
		.bp_in_en_shift = 17,
		.bp_in_mask = 0x1ffff,
		.hfb_filter_cnt = 48,
		.hfb_filter_size = 128,
		.qtag_mask = 0x3F,
		.tbuf_offset = 0x0600,
		.hfb_offset = 0x8000,
		.hfb_reg_offset = 0xfc00,
		.rdma_offset = 0x2000,
		.tdma_offset = 0x4000,
		.words_per_bd = 3,
		.flags = GENET_HAS_40BITS | GENET_HAS_EXT |
			 GENET_HAS_MDIO_INTR | GENET_HAS_MOCA_LINK_DET,
	},
};

/* Infer hardware parameters from the detected GENET version */
static void bcmgenet_set_hw_params(struct bcmgenet_priv *priv)
{
	struct bcmgenet_hw_params *params;
	u32 reg;
	u8 major;
	u16 gphy_rev;

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
	 *
	 * On newer chips, starting with PHY revision G0, a new scheme is
	 * deployed similar to the Starfighter 2 switch with GPHY major
	 * revision in bits 15:8 and patch level in bits 7:0. Major revision 0
	 * is reserved as well as special value 0x01ff, we have a small
	 * heuristic to check for the new GPHY revision and re-arrange things
	 * so the GPHY driver is happy.
	 */
	gphy_rev = reg & 0xffff;

	/* This is the good old scheme, just GPHY major, no minor nor patch */
	if ((gphy_rev & 0xf0) != 0)
		priv->gphy_rev = gphy_rev << 8;

	/* This is the new scheme, GPHY major rolls over with 0x10 = rev G0 */
	else if ((gphy_rev & 0xff00) != 0)
		priv->gphy_rev = gphy_rev;

	/* This is reserved so should require special treatment */
	else if (gphy_rev == 0 || gphy_rev == 0x01ff) {
		pr_warn("Invalid GPHY revision detected: 0x%04x\n", gphy_rev);
		return;
	}

#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (!(params->flags & GENET_HAS_40BITS))
		pr_warn("GENET does not support 40-bits PA\n");
#endif

	pr_debug("Configuration for version: %d\n"
		"TXq: %1d, TXqBDs: %1d, RXq: %1d, RXqBDs: %1d\n"
		"BP << en: %2d, BP msk: 0x%05x\n"
		"HFB count: %2d, QTAQ msk: 0x%05x\n"
		"TBUF: 0x%04x, HFB: 0x%04x, HFBreg: 0x%04x\n"
		"RDMA: 0x%05x, TDMA: 0x%05x\n"
		"Words/BD: %d\n",
		priv->version,
		params->tx_queues, params->tx_bds_per_q,
		params->rx_queues, params->rx_bds_per_q,
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
MODULE_DEVICE_TABLE(of, bcmgenet_match);

static int bcmgenet_probe(struct platform_device *pdev)
{
	struct bcmgenet_platform_data *pd = pdev->dev.platform_data;
	struct device_node *dn = pdev->dev.of_node;
	const struct of_device_id *of_id = NULL;
	struct bcmgenet_priv *priv;
	struct net_device *dev;
	const void *macaddr;
	struct resource *r;
	int err = -EIO;

	/* Up to GENET_MAX_MQ_CNT + 1 TX queues and RX queues */
	dev = alloc_etherdev_mqs(sizeof(*priv), GENET_MAX_MQ_CNT + 1,
				 GENET_MAX_MQ_CNT + 1);
	if (!dev) {
		dev_err(&pdev->dev, "can't allocate net device\n");
		return -ENOMEM;
	}

	if (dn) {
		of_id = of_match_node(bcmgenet_match, dn);
		if (!of_id)
			return -EINVAL;
	}

	priv = netdev_priv(dev);
	priv->irq0 = platform_get_irq(pdev, 0);
	priv->irq1 = platform_get_irq(pdev, 1);
	priv->wol_irq = platform_get_irq(pdev, 2);
	if (!priv->irq0 || !priv->irq1) {
		dev_err(&pdev->dev, "can't find IRQs\n");
		err = -EINVAL;
		goto err;
	}

	if (dn) {
		macaddr = of_get_mac_address(dn);
		if (!macaddr) {
			dev_err(&pdev->dev, "can't find MAC address\n");
			err = -EINVAL;
			goto err;
		}
	} else {
		macaddr = pd->mac_address;
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
	if (of_id)
		priv->version = (enum bcmgenet_version)of_id->data;
	else
		priv->version = pd->genet_version;

	priv->clk = devm_clk_get(&priv->pdev->dev, "enet");
	if (IS_ERR(priv->clk)) {
		dev_warn(&priv->pdev->dev, "failed to get enet clock\n");
		priv->clk = NULL;
	}

	clk_prepare_enable(priv->clk);

	bcmgenet_set_hw_params(priv);

	/* Mii wait queue */
	init_waitqueue_head(&priv->wq);
	/* Always use RX_BUF_LENGTH (2KB) buffer for all chips */
	priv->rx_buf_len = RX_BUF_LENGTH;
	INIT_WORK(&priv->bcmgenet_irq_work, bcmgenet_irq_task);

	priv->clk_wol = devm_clk_get(&priv->pdev->dev, "enet-wol");
	if (IS_ERR(priv->clk_wol)) {
		dev_warn(&priv->pdev->dev, "failed to get enet-wol clock\n");
		priv->clk_wol = NULL;
	}

	priv->clk_eee = devm_clk_get(&priv->pdev->dev, "enet-eee");
	if (IS_ERR(priv->clk_eee)) {
		dev_warn(&priv->pdev->dev, "failed to get enet-eee clock\n");
		priv->clk_eee = NULL;
	}

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
	clk_disable_unprepare(priv->clk);

	err = register_netdev(dev);
	if (err)
		goto err;

	return err;

err_clk_disable:
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
		ret = bcmgenet_power_down(priv, GENET_POWER_WOL_MAGIC);
		clk_prepare_enable(priv->clk_wol);
	} else if (priv->internal_phy) {
		ret = bcmgenet_power_down(priv, GENET_POWER_PASSIVE);
	}

	/* Turn off the clocks */
	clk_disable_unprepare(priv->clk);

	return ret;
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

	/* If this is an internal GPHY, power it back on now, before UniMAC is
	 * brought out of reset as absolutely no UniMAC activity is allowed
	 */
	if (priv->internal_phy)
		bcmgenet_power_up(priv, GENET_POWER_PASSIVE);

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

	if (priv->internal_phy) {
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

	if (priv->eee.eee_enabled)
		bcmgenet_eee_enable_set(dev, true);

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
		.of_match_table = bcmgenet_match,
		.pm	= &bcmgenet_pm_ops,
	},
};
module_platform_driver(bcmgenet_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom GENET Ethernet controller driver");
MODULE_ALIAS("platform:bcmgenet");
MODULE_LICENSE("GPL");
