// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom GENET (Gigabit Ethernet) controller driver
 *
 * Copyright (c) 2014-2025 Broadcom
 */

#define pr_fmt(fmt)				"bcmgenet: " fmt

#include <linux/acpi.h>
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

#include <linux/unaligned.h>

#include "bcmgenet.h"

/* Default highest priority queue for multi queue support */
#define GENET_Q1_PRIORITY	0
#define GENET_Q0_PRIORITY	1

#define GENET_Q0_RX_BD_CNT	\
	(TOTAL_DESC - priv->hw_params->rx_queues * priv->hw_params->rx_bds_per_q)
#define GENET_Q0_TX_BD_CNT	\
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

/* Forward declarations */
static void bcmgenet_set_rx_mode(struct net_device *dev);

static inline void bcmgenet_writel(u32 value, void __iomem *offset)
{
	/* MIPS chips strapped for BE will automagically configure the
	 * peripheral registers for CPU-native byte order.
	 */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(value, offset);
	else
		writel_relaxed(value, offset);
}

static inline u32 bcmgenet_readl(void __iomem *offset)
{
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(offset);
	else
		return readl_relaxed(offset);
}

static inline void dmadesc_set_length_status(struct bcmgenet_priv *priv,
					     void __iomem *d, u32 value)
{
	bcmgenet_writel(value, d + DMA_DESC_LENGTH_STATUS);
}

static inline void dmadesc_set_addr(struct bcmgenet_priv *priv,
				    void __iomem *d,
				    dma_addr_t addr)
{
	bcmgenet_writel(lower_32_bits(addr), d + DMA_DESC_ADDRESS_LO);

	/* Register writes to GISB bus can take couple hundred nanoseconds
	 * and are done for each packet, save these expensive writes unless
	 * the platform is explicitly configured for 64-bits/LPAE.
	 */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (bcmgenet_has_40bits(priv))
		bcmgenet_writel(upper_32_bits(addr), d + DMA_DESC_ADDRESS_HI);
#endif
}

/* Combined address + length/status setter */
static inline void dmadesc_set(struct bcmgenet_priv *priv,
			       void __iomem *d, dma_addr_t addr, u32 val)
{
	dmadesc_set_addr(priv, d, addr);
	dmadesc_set_length_status(priv, d, val);
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
		return bcmgenet_readl(priv->base +
				      priv->hw_params->tbuf_offset + TBUF_CTRL);
}

static inline void bcmgenet_tbuf_ctrl_set(struct bcmgenet_priv *priv, u32 val)
{
	if (GENET_IS_V1(priv))
		bcmgenet_rbuf_writel(priv, val, TBUF_CTRL_V1);
	else
		bcmgenet_writel(val, priv->base +
				priv->hw_params->tbuf_offset + TBUF_CTRL);
}

static inline u32 bcmgenet_bp_mc_get(struct bcmgenet_priv *priv)
{
	if (GENET_IS_V1(priv))
		return bcmgenet_rbuf_readl(priv, TBUF_BP_MC_V1);
	else
		return bcmgenet_readl(priv->base +
				      priv->hw_params->tbuf_offset + TBUF_BP_MC);
}

static inline void bcmgenet_bp_mc_set(struct bcmgenet_priv *priv, u32 val)
{
	if (GENET_IS_V1(priv))
		bcmgenet_rbuf_writel(priv, val, TBUF_BP_MC_V1);
	else
		bcmgenet_writel(val, priv->base +
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
	return bcmgenet_readl(priv->base + GENET_TDMA_REG_OFF +
			      DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

static inline void bcmgenet_tdma_writel(struct bcmgenet_priv *priv,
					u32 val, enum dma_reg r)
{
	bcmgenet_writel(val, priv->base + GENET_TDMA_REG_OFF +
			DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

static inline u32 bcmgenet_rdma_readl(struct bcmgenet_priv *priv,
				      enum dma_reg r)
{
	return bcmgenet_readl(priv->base + GENET_RDMA_REG_OFF +
			      DMA_RINGS_SIZE + bcmgenet_dma_regs[r]);
}

static inline void bcmgenet_rdma_writel(struct bcmgenet_priv *priv,
					u32 val, enum dma_reg r)
{
	bcmgenet_writel(val, priv->base + GENET_RDMA_REG_OFF +
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
	return bcmgenet_readl(priv->base + GENET_TDMA_REG_OFF +
			      (DMA_RING_SIZE * ring) +
			      genet_dma_ring_regs[r]);
}

static inline void bcmgenet_tdma_ring_writel(struct bcmgenet_priv *priv,
					     unsigned int ring, u32 val,
					     enum dma_ring_reg r)
{
	bcmgenet_writel(val, priv->base + GENET_TDMA_REG_OFF +
			(DMA_RING_SIZE * ring) +
			genet_dma_ring_regs[r]);
}

static inline u32 bcmgenet_rdma_ring_readl(struct bcmgenet_priv *priv,
					   unsigned int ring,
					   enum dma_ring_reg r)
{
	return bcmgenet_readl(priv->base + GENET_RDMA_REG_OFF +
			      (DMA_RING_SIZE * ring) +
			      genet_dma_ring_regs[r]);
}

static inline void bcmgenet_rdma_ring_writel(struct bcmgenet_priv *priv,
					     unsigned int ring, u32 val,
					     enum dma_ring_reg r)
{
	bcmgenet_writel(val, priv->base + GENET_RDMA_REG_OFF +
			(DMA_RING_SIZE * ring) +
			genet_dma_ring_regs[r]);
}

static void bcmgenet_hfb_enable_filter(struct bcmgenet_priv *priv, u32 f_index)
{
	u32 offset;
	u32 reg;

	if (GENET_IS_V1(priv) || GENET_IS_V2(priv)) {
		reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
		reg |= (1 << ((f_index % 32) + RBUF_HFB_FILTER_EN_SHIFT)) |
			RBUF_HFB_EN;
		bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);
	} else {
		offset = HFB_FLT_ENABLE_V3PLUS + (f_index < 32) * sizeof(u32);
		reg = bcmgenet_hfb_reg_readl(priv, offset);
		reg |= (1 << (f_index % 32));
		bcmgenet_hfb_reg_writel(priv, reg, offset);
		reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
		reg |= RBUF_HFB_EN;
		bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);
	}
}

static void bcmgenet_hfb_disable_filter(struct bcmgenet_priv *priv, u32 f_index)
{
	u32 offset, reg, reg1;

	if (GENET_IS_V1(priv) || GENET_IS_V2(priv)) {
		reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
		reg &= ~(1 << ((f_index % 32) + RBUF_HFB_FILTER_EN_SHIFT));
		if (!(reg & RBUF_HFB_FILTER_EN_MASK))
			reg &= ~RBUF_HFB_EN;
		bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);
	} else {
		offset = HFB_FLT_ENABLE_V3PLUS;
		reg = bcmgenet_hfb_reg_readl(priv, offset);
		reg1 = bcmgenet_hfb_reg_readl(priv, offset + sizeof(u32));
		if  (f_index < 32) {
			reg1 &= ~(1 << (f_index % 32));
			bcmgenet_hfb_reg_writel(priv, reg1, offset + sizeof(u32));
		} else {
			reg &= ~(1 << (f_index % 32));
			bcmgenet_hfb_reg_writel(priv, reg, offset);
		}
		if (!reg && !reg1) {
			reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
			reg &= ~RBUF_HFB_EN;
			bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);
		}
	}
}

static void bcmgenet_hfb_set_filter_rx_queue_mapping(struct bcmgenet_priv *priv,
						     u32 f_index, u32 rx_queue)
{
	u32 offset;
	u32 reg;

	if (GENET_IS_V1(priv) || GENET_IS_V2(priv))
		return;

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

	if (GENET_IS_V1(priv) || GENET_IS_V2(priv))
		offset = HFB_FLT_LEN_V2;
	else
		offset = HFB_FLT_LEN_V3PLUS;

	offset += sizeof(u32) *
		  ((priv->hw_params->hfb_filter_cnt - 1 - f_index) / 4);
	reg = bcmgenet_hfb_reg_readl(priv, offset);
	reg &= ~(0xFF << (8 * (f_index % 4)));
	reg |= ((f_length & 0xFF) << (8 * (f_index % 4)));
	bcmgenet_hfb_reg_writel(priv, reg, offset);
}

static int bcmgenet_hfb_validate_mask(void *mask, size_t size)
{
	while (size) {
		switch (*(unsigned char *)mask++) {
		case 0x00:
		case 0x0f:
		case 0xf0:
		case 0xff:
			size--;
			continue;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

#define VALIDATE_MASK(x) \
	bcmgenet_hfb_validate_mask(&(x), sizeof(x))

static int bcmgenet_hfb_insert_data(struct bcmgenet_priv *priv, u32 f_index,
				    u32 offset, void *val, void *mask,
				    size_t size)
{
	u32 index, tmp;

	index = f_index * priv->hw_params->hfb_filter_size + offset / 2;
	tmp = bcmgenet_hfb_readl(priv, index * sizeof(u32));

	while (size--) {
		if (offset++ & 1) {
			tmp &= ~0x300FF;
			tmp |= (*(unsigned char *)val++);
			switch ((*(unsigned char *)mask++)) {
			case 0xFF:
				tmp |= 0x30000;
				break;
			case 0xF0:
				tmp |= 0x20000;
				break;
			case 0x0F:
				tmp |= 0x10000;
				break;
			}
			bcmgenet_hfb_writel(priv, tmp, index++ * sizeof(u32));
			if (size)
				tmp = bcmgenet_hfb_readl(priv,
							 index * sizeof(u32));
		} else {
			tmp &= ~0xCFF00;
			tmp |= (*(unsigned char *)val++) << 8;
			switch ((*(unsigned char *)mask++)) {
			case 0xFF:
				tmp |= 0xC0000;
				break;
			case 0xF0:
				tmp |= 0x80000;
				break;
			case 0x0F:
				tmp |= 0x40000;
				break;
			}
			if (!size)
				bcmgenet_hfb_writel(priv, tmp, index * sizeof(u32));
		}
	}

	return 0;
}

static void bcmgenet_hfb_create_rxnfc_filter(struct bcmgenet_priv *priv,
					     struct bcmgenet_rxnfc_rule *rule)
{
	struct ethtool_rx_flow_spec *fs = &rule->fs;
	u32 offset = 0, f_length = 0, f, q;
	u8 val_8, mask_8;
	__be16 val_16;
	u16 mask_16;
	size_t size;

	f = fs->location + 1;
	if (fs->flow_type & FLOW_MAC_EXT) {
		bcmgenet_hfb_insert_data(priv, f, 0,
					 &fs->h_ext.h_dest, &fs->m_ext.h_dest,
					 sizeof(fs->h_ext.h_dest));
	}

	if (fs->flow_type & FLOW_EXT) {
		if (fs->m_ext.vlan_etype ||
		    fs->m_ext.vlan_tci) {
			bcmgenet_hfb_insert_data(priv, f, 12,
						 &fs->h_ext.vlan_etype,
						 &fs->m_ext.vlan_etype,
						 sizeof(fs->h_ext.vlan_etype));
			bcmgenet_hfb_insert_data(priv, f, 14,
						 &fs->h_ext.vlan_tci,
						 &fs->m_ext.vlan_tci,
						 sizeof(fs->h_ext.vlan_tci));
			offset += VLAN_HLEN;
			f_length += DIV_ROUND_UP(VLAN_HLEN, 2);
		}
	}

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case ETHER_FLOW:
		f_length += DIV_ROUND_UP(ETH_HLEN, 2);
		bcmgenet_hfb_insert_data(priv, f, 0,
					 &fs->h_u.ether_spec.h_dest,
					 &fs->m_u.ether_spec.h_dest,
					 sizeof(fs->h_u.ether_spec.h_dest));
		bcmgenet_hfb_insert_data(priv, f, ETH_ALEN,
					 &fs->h_u.ether_spec.h_source,
					 &fs->m_u.ether_spec.h_source,
					 sizeof(fs->h_u.ether_spec.h_source));
		bcmgenet_hfb_insert_data(priv, f, (2 * ETH_ALEN) + offset,
					 &fs->h_u.ether_spec.h_proto,
					 &fs->m_u.ether_spec.h_proto,
					 sizeof(fs->h_u.ether_spec.h_proto));
		break;
	case IP_USER_FLOW:
		f_length += DIV_ROUND_UP(ETH_HLEN + 20, 2);
		/* Specify IP Ether Type */
		val_16 = htons(ETH_P_IP);
		mask_16 = 0xFFFF;
		bcmgenet_hfb_insert_data(priv, f, (2 * ETH_ALEN) + offset,
					 &val_16, &mask_16, sizeof(val_16));
		bcmgenet_hfb_insert_data(priv, f, 15 + offset,
					 &fs->h_u.usr_ip4_spec.tos,
					 &fs->m_u.usr_ip4_spec.tos,
					 sizeof(fs->h_u.usr_ip4_spec.tos));
		bcmgenet_hfb_insert_data(priv, f, 23 + offset,
					 &fs->h_u.usr_ip4_spec.proto,
					 &fs->m_u.usr_ip4_spec.proto,
					 sizeof(fs->h_u.usr_ip4_spec.proto));
		bcmgenet_hfb_insert_data(priv, f, 26 + offset,
					 &fs->h_u.usr_ip4_spec.ip4src,
					 &fs->m_u.usr_ip4_spec.ip4src,
					 sizeof(fs->h_u.usr_ip4_spec.ip4src));
		bcmgenet_hfb_insert_data(priv, f, 30 + offset,
					 &fs->h_u.usr_ip4_spec.ip4dst,
					 &fs->m_u.usr_ip4_spec.ip4dst,
					 sizeof(fs->h_u.usr_ip4_spec.ip4dst));
		if (!fs->m_u.usr_ip4_spec.l4_4_bytes)
			break;

		/* Only supports 20 byte IPv4 header */
		val_8 = 0x45;
		mask_8 = 0xFF;
		bcmgenet_hfb_insert_data(priv, f, ETH_HLEN + offset,
					 &val_8, &mask_8,
					 sizeof(val_8));
		size = sizeof(fs->h_u.usr_ip4_spec.l4_4_bytes);
		bcmgenet_hfb_insert_data(priv, f,
					 ETH_HLEN + 20 + offset,
					 &fs->h_u.usr_ip4_spec.l4_4_bytes,
					 &fs->m_u.usr_ip4_spec.l4_4_bytes,
					 size);
		f_length += DIV_ROUND_UP(size, 2);
		break;
	}

	bcmgenet_hfb_set_filter_length(priv, f, 2 * f_length);
	if (fs->ring_cookie == RX_CLS_FLOW_WAKE)
		q = 0;
	else if (fs->ring_cookie == RX_CLS_FLOW_DISC)
		q = priv->hw_params->rx_queues + 1;
	else
		/* Other Rx rings are direct mapped here */
		q = fs->ring_cookie;
	bcmgenet_hfb_set_filter_rx_queue_mapping(priv, f, q);
	bcmgenet_hfb_enable_filter(priv, f);
	rule->state = BCMGENET_RXNFC_STATE_ENABLED;
}

/* bcmgenet_hfb_clear
 *
 * Clear Hardware Filter Block and disable all filtering.
 */
static void bcmgenet_hfb_clear_filter(struct bcmgenet_priv *priv, u32 f_index)
{
	u32 base, i;

	bcmgenet_hfb_set_filter_length(priv, f_index, 0);
	base = f_index * priv->hw_params->hfb_filter_size;
	for (i = 0; i < priv->hw_params->hfb_filter_size; i++)
		bcmgenet_hfb_writel(priv, 0x0, (base + i) * sizeof(u32));
}

static void bcmgenet_hfb_clear(struct bcmgenet_priv *priv)
{
	u32 i;

	bcmgenet_hfb_reg_writel(priv, 0, HFB_CTRL);

	if (!GENET_IS_V1(priv) && !GENET_IS_V2(priv)) {
		bcmgenet_hfb_reg_writel(priv, 0,
					HFB_FLT_ENABLE_V3PLUS);
		bcmgenet_hfb_reg_writel(priv, 0,
					HFB_FLT_ENABLE_V3PLUS + 4);
		for (i = DMA_INDEX2RING_0; i <= DMA_INDEX2RING_7; i++)
			bcmgenet_rdma_writel(priv, 0, i);
	}

	for (i = 0; i < priv->hw_params->hfb_filter_cnt; i++)
		bcmgenet_hfb_clear_filter(priv, i);

	/* Enable filter 0 to send default flow to ring 0 */
	bcmgenet_hfb_set_filter_length(priv, 0, 4);
	bcmgenet_hfb_enable_filter(priv, 0);
}

static void bcmgenet_hfb_init(struct bcmgenet_priv *priv)
{
	int i;

	INIT_LIST_HEAD(&priv->rxnfc_list);
	for (i = 0; i < MAX_NUM_OF_FS_RULES; i++) {
		INIT_LIST_HEAD(&priv->rxnfc_rules[i].list);
		priv->rxnfc_rules[i].state = BCMGENET_RXNFC_STATE_UNUSED;
	}

	bcmgenet_hfb_clear(priv);
}

static int bcmgenet_begin(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	/* Turn on the clock */
	return clk_prepare_enable(priv->clk);
}

static void bcmgenet_complete(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	/* Turn off the clock */
	clk_disable_unprepare(priv->clk);
}

static int bcmgenet_get_link_ksettings(struct net_device *dev,
				       struct ethtool_link_ksettings *cmd)
{
	if (!netif_running(dev))
		return -EINVAL;

	if (!dev->phydev)
		return -ENODEV;

	phy_ethtool_ksettings_get(dev->phydev, cmd);

	return 0;
}

static int bcmgenet_set_link_ksettings(struct net_device *dev,
				       const struct ethtool_link_ksettings *cmd)
{
	if (!netif_running(dev))
		return -EINVAL;

	if (!dev->phydev)
		return -ENODEV;

	return phy_ethtool_ksettings_set(dev->phydev, cmd);
}

static int bcmgenet_set_features(struct net_device *dev,
				 netdev_features_t features)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg;
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	/* Make sure we reflect the value of CRC_CMD_FWD */
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	priv->crc_fwd_en = !!(reg & CMD_CRC_FWD);

	clk_disable_unprepare(priv->clk);

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
				 struct ethtool_coalesce *ec,
				 struct kernel_ethtool_coalesce *kernel_coal,
				 struct netlink_ext_ack *extack)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rx_ring *ring;
	unsigned int i;

	ec->tx_max_coalesced_frames =
		bcmgenet_tdma_ring_readl(priv, 0, DMA_MBUF_DONE_THRESH);
	ec->rx_max_coalesced_frames =
		bcmgenet_rdma_ring_readl(priv, 0, DMA_MBUF_DONE_THRESH);
	ec->rx_coalesce_usecs =
		bcmgenet_rdma_readl(priv, DMA_RING0_TIMEOUT) * 8192 / 1000;

	for (i = 0; i <= priv->hw_params->rx_queues; i++) {
		ring = &priv->rx_rings[i];
		ec->use_adaptive_rx_coalesce |= ring->dim.use_dim;
	}

	return 0;
}

static void bcmgenet_set_rx_coalesce(struct bcmgenet_rx_ring *ring,
				     u32 usecs, u32 pkts)
{
	struct bcmgenet_priv *priv = ring->priv;
	unsigned int i = ring->index;
	u32 reg;

	bcmgenet_rdma_ring_writel(priv, i, pkts, DMA_MBUF_DONE_THRESH);

	reg = bcmgenet_rdma_readl(priv, DMA_RING0_TIMEOUT + i);
	reg &= ~DMA_TIMEOUT_MASK;
	reg |= DIV_ROUND_UP(usecs * 1000, 8192);
	bcmgenet_rdma_writel(priv, reg, DMA_RING0_TIMEOUT + i);
}

static void bcmgenet_set_ring_rx_coalesce(struct bcmgenet_rx_ring *ring,
					  struct ethtool_coalesce *ec)
{
	struct dim_cq_moder moder;
	u32 usecs, pkts;

	ring->rx_coalesce_usecs = ec->rx_coalesce_usecs;
	ring->rx_max_coalesced_frames = ec->rx_max_coalesced_frames;
	usecs = ring->rx_coalesce_usecs;
	pkts = ring->rx_max_coalesced_frames;

	if (ec->use_adaptive_rx_coalesce && !ring->dim.use_dim) {
		moder = net_dim_get_def_rx_moderation(ring->dim.dim.mode);
		usecs = moder.usec;
		pkts = moder.pkts;
	}

	ring->dim.use_dim = ec->use_adaptive_rx_coalesce;
	bcmgenet_set_rx_coalesce(ring, usecs, pkts);
}

static int bcmgenet_set_coalesce(struct net_device *dev,
				 struct ethtool_coalesce *ec,
				 struct kernel_ethtool_coalesce *kernel_coal,
				 struct netlink_ext_ack *extack)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned int i;

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
	 * transmitted, or when the ring is empty.
	 */

	/* Program all TX queues with the same values, as there is no
	 * ethtool knob to do coalescing on a per-queue basis
	 */
	for (i = 0; i <= priv->hw_params->tx_queues; i++)
		bcmgenet_tdma_ring_writel(priv, i,
					  ec->tx_max_coalesced_frames,
					  DMA_MBUF_DONE_THRESH);

	for (i = 0; i <= priv->hw_params->rx_queues; i++)
		bcmgenet_set_ring_rx_coalesce(&priv->rx_rings[i], ec);

	return 0;
}

static void bcmgenet_get_pauseparam(struct net_device *dev,
				    struct ethtool_pauseparam *epause)
{
	struct bcmgenet_priv *priv;
	u32 umac_cmd;

	priv = netdev_priv(dev);

	epause->autoneg = priv->autoneg_pause;

	if (netif_carrier_ok(dev)) {
		/* report active state when link is up */
		umac_cmd = bcmgenet_umac_readl(priv, UMAC_CMD);
		epause->tx_pause = !(umac_cmd & CMD_TX_PAUSE_IGNORE);
		epause->rx_pause = !(umac_cmd & CMD_RX_PAUSE_IGNORE);
	} else {
		/* otherwise report stored settings */
		epause->tx_pause = priv->tx_pause;
		epause->rx_pause = priv->rx_pause;
	}
}

static int bcmgenet_set_pauseparam(struct net_device *dev,
				   struct ethtool_pauseparam *epause)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	if (!dev->phydev)
		return -ENODEV;

	if (!phy_validate_pause(dev->phydev, epause))
		return -EINVAL;

	priv->autoneg_pause = !!epause->autoneg;
	priv->tx_pause = !!epause->tx_pause;
	priv->rx_pause = !!epause->rx_pause;

	bcmgenet_phy_pause_set(dev, priv->rx_pause, priv->tx_pause);

	return 0;
}

/* standard ethtool support functions. */
enum bcmgenet_stat_type {
	BCMGENET_STAT_RTNL = -1,
	BCMGENET_STAT_MIB_RX,
	BCMGENET_STAT_MIB_TX,
	BCMGENET_STAT_RUNT,
	BCMGENET_STAT_MISC,
	BCMGENET_STAT_SOFT,
	BCMGENET_STAT_SOFT64,
};

struct bcmgenet_stats {
	char stat_string[ETH_GSTRING_LEN];
	int stat_sizeof;
	int stat_offset;
	enum bcmgenet_stat_type type;
	/* reg offset from UMAC base for misc counters */
	u16 reg_offset;
	/* sync for u64 stats counters */
	int syncp_offset;
};

#define STAT_RTNL(m) { \
	.stat_string = __stringify(m), \
	.stat_sizeof = sizeof(((struct rtnl_link_stats64 *)0)->m), \
	.stat_offset = offsetof(struct rtnl_link_stats64, m), \
	.type = BCMGENET_STAT_RTNL, \
}

#define STAT_GENET_MIB(str, m, _type) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcmgenet_priv *)0)->m), \
	.stat_offset = offsetof(struct bcmgenet_priv, m), \
	.type = _type, \
}

#define STAT_GENET_SOFT_MIB64(str, s, m) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcmgenet_priv *)0)->s.m), \
	.stat_offset = offsetof(struct bcmgenet_priv, s.m), \
	.type = BCMGENET_STAT_SOFT64, \
	.syncp_offset = offsetof(struct bcmgenet_priv, s.syncp), \
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

#define STAT_GENET_Q(num) \
	STAT_GENET_SOFT_MIB64("txq" __stringify(num) "_packets", \
			tx_rings[num].stats64, packets), \
	STAT_GENET_SOFT_MIB64("txq" __stringify(num) "_bytes", \
			tx_rings[num].stats64, bytes), \
	STAT_GENET_SOFT_MIB64("txq" __stringify(num) "_errors", \
			tx_rings[num].stats64, errors), \
	STAT_GENET_SOFT_MIB64("txq" __stringify(num) "_dropped", \
			tx_rings[num].stats64, dropped), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_bytes", \
			rx_rings[num].stats64, bytes),	 \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_packets", \
			rx_rings[num].stats64, packets), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_errors", \
			rx_rings[num].stats64, errors), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_dropped", \
			rx_rings[num].stats64, dropped), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_multicast", \
			rx_rings[num].stats64, multicast), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_missed", \
			rx_rings[num].stats64, missed), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_length_errors", \
			rx_rings[num].stats64, length_errors), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_over_errors", \
			rx_rings[num].stats64, over_errors), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_crc_errors", \
			rx_rings[num].stats64, crc_errors), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_frame_errors", \
			rx_rings[num].stats64, frame_errors), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_fragmented_errors", \
			rx_rings[num].stats64, fragmented_errors), \
	STAT_GENET_SOFT_MIB64("rxq" __stringify(num) "_broadcast", \
			rx_rings[num].stats64, broadcast)

/* There is a 0xC gap between the end of RX and beginning of TX stats and then
 * between the end of TX stats and the beginning of the RX RUNT
 */
#define BCMGENET_STAT_OFFSET	0xc

/* Hardware counters must be kept in sync because the order/offset
 * is important here (order in structure declaration = order in hardware)
 */
static const struct bcmgenet_stats bcmgenet_gstrings_stats[] = {
	/* general stats */
	STAT_RTNL(rx_packets),
	STAT_RTNL(tx_packets),
	STAT_RTNL(rx_bytes),
	STAT_RTNL(tx_bytes),
	STAT_RTNL(rx_errors),
	STAT_RTNL(tx_errors),
	STAT_RTNL(rx_dropped),
	STAT_RTNL(tx_dropped),
	STAT_RTNL(multicast),
	STAT_RTNL(rx_missed_errors),
	STAT_RTNL(rx_length_errors),
	STAT_RTNL(rx_over_errors),
	STAT_RTNL(rx_crc_errors),
	STAT_RTNL(rx_frame_errors),
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
			UMAC_RBUF_OVFL_CNT_V1),
	STAT_GENET_MISC("rbuf_err_cnt", mib.rbuf_err_cnt,
			UMAC_RBUF_ERR_CNT_V1),
	STAT_GENET_MISC("mdf_err_cnt", mib.mdf_err_cnt, UMAC_MDF_ERR_CNT),
	STAT_GENET_SOFT_MIB("alloc_rx_buff_failed", mib.alloc_rx_buff_failed),
	STAT_GENET_SOFT_MIB("rx_dma_failed", mib.rx_dma_failed),
	STAT_GENET_SOFT_MIB("tx_dma_failed", mib.tx_dma_failed),
	STAT_GENET_SOFT_MIB("tx_realloc_tsb", mib.tx_realloc_tsb),
	STAT_GENET_SOFT_MIB("tx_realloc_tsb_failed",
			    mib.tx_realloc_tsb_failed),
	/* Per TX queues */
	STAT_GENET_Q(0),
	STAT_GENET_Q(1),
	STAT_GENET_Q(2),
	STAT_GENET_Q(3),
	STAT_GENET_Q(4),
};

#define BCMGENET_STATS_LEN	ARRAY_SIZE(bcmgenet_gstrings_stats)

#define BCMGENET_STATS64_ADD(stats, m, v) \
	do { \
		u64_stats_update_begin(&stats->syncp); \
		u64_stats_add(&stats->m, v); \
		u64_stats_update_end(&stats->syncp); \
	} while (0)

#define BCMGENET_STATS64_INC(stats, m) \
	do { \
		u64_stats_update_begin(&stats->syncp); \
		u64_stats_inc(&stats->m); \
		u64_stats_update_end(&stats->syncp); \
	} while (0)

static void bcmgenet_get_drvinfo(struct net_device *dev,
				 struct ethtool_drvinfo *info)
{
	strscpy(info->driver, "bcmgenet", sizeof(info->driver));
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
	const char *str;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < BCMGENET_STATS_LEN; i++) {
			str = bcmgenet_gstrings_stats[i].stat_string;
			ethtool_puts(&data, str);
		}
		break;
	}
}

static u32 bcmgenet_update_stat_misc(struct bcmgenet_priv *priv, u16 offset)
{
	u16 new_offset;
	u32 val;

	switch (offset) {
	case UMAC_RBUF_OVFL_CNT_V1:
		if (GENET_IS_V2(priv))
			new_offset = RBUF_OVFL_CNT_V2;
		else
			new_offset = RBUF_OVFL_CNT_V3PLUS;

		val = bcmgenet_rbuf_readl(priv,	new_offset);
		/* clear if overflowed */
		if (val == ~0)
			bcmgenet_rbuf_writel(priv, 0, new_offset);
		break;
	case UMAC_RBUF_ERR_CNT_V1:
		if (GENET_IS_V2(priv))
			new_offset = RBUF_ERR_CNT_V2;
		else
			new_offset = RBUF_ERR_CNT_V3PLUS;

		val = bcmgenet_rbuf_readl(priv,	new_offset);
		/* clear if overflowed */
		if (val == ~0)
			bcmgenet_rbuf_writel(priv, 0, new_offset);
		break;
	default:
		val = bcmgenet_umac_readl(priv, offset);
		/* clear if overflowed */
		if (val == ~0)
			bcmgenet_umac_writel(priv, 0, offset);
		break;
	}

	return val;
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
		case BCMGENET_STAT_RTNL:
		case BCMGENET_STAT_SOFT:
		case BCMGENET_STAT_SOFT64:
			continue;
		case BCMGENET_STAT_RUNT:
			offset += BCMGENET_STAT_OFFSET;
			fallthrough;
		case BCMGENET_STAT_MIB_TX:
			offset += BCMGENET_STAT_OFFSET;
			fallthrough;
		case BCMGENET_STAT_MIB_RX:
			val = bcmgenet_umac_readl(priv,
						  UMAC_MIB_START + j + offset);
			offset = 0;	/* Reset Offset */
			break;
		case BCMGENET_STAT_MISC:
			if (GENET_IS_V1(priv)) {
				val = bcmgenet_umac_readl(priv, s->reg_offset);
				/* clear if overflowed */
				if (val == ~0)
					bcmgenet_umac_writel(priv, 0,
							     s->reg_offset);
			} else {
				val = bcmgenet_update_stat_misc(priv,
								s->reg_offset);
			}
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
	struct rtnl_link_stats64 stats64;
	struct u64_stats_sync *syncp;
	unsigned int start;
	int i;

	if (netif_running(dev))
		bcmgenet_update_mib_counters(priv);

	dev_get_stats(dev, &stats64);

	for (i = 0; i < BCMGENET_STATS_LEN; i++) {
		const struct bcmgenet_stats *s;
		char *p;

		s = &bcmgenet_gstrings_stats[i];
		p = (char *)priv;

		if (s->type == BCMGENET_STAT_SOFT64) {
			syncp = (struct u64_stats_sync *)(p + s->syncp_offset);
			do {
				start = u64_stats_fetch_begin(syncp);
				data[i] = u64_stats_read((u64_stats_t *)(p + s->stat_offset));
			} while (u64_stats_fetch_retry(syncp, start));
		} else {
			if (s->type == BCMGENET_STAT_RTNL)
				p = (char *)&stats64;

			p += s->stat_offset;
			if (sizeof(unsigned long) != sizeof(u32) &&
				s->stat_sizeof == sizeof(unsigned long))
				data[i] = *(unsigned long *)p;
			else
				data[i] = *(u32 *)p;
		}
	}
}

void bcmgenet_eee_enable_set(struct net_device *dev, bool enable,
			     bool tx_lpi_enabled)
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
	reg = bcmgenet_readl(priv->base + off);
	if (tx_lpi_enabled)
		reg |= TBUF_EEE_EN | TBUF_PM_EN;
	else
		reg &= ~(TBUF_EEE_EN | TBUF_PM_EN);
	bcmgenet_writel(reg, priv->base + off);

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
	priv->eee.tx_lpi_enabled = tx_lpi_enabled;
}

static int bcmgenet_get_eee(struct net_device *dev, struct ethtool_keee *e)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct ethtool_keee *p = &priv->eee;

	if (GENET_IS_V1(priv))
		return -EOPNOTSUPP;

	if (!dev->phydev)
		return -ENODEV;

	e->tx_lpi_enabled = p->tx_lpi_enabled;
	e->tx_lpi_timer = bcmgenet_umac_readl(priv, UMAC_EEE_LPI_TIMER);

	return phy_ethtool_get_eee(dev->phydev, e);
}

static int bcmgenet_set_eee(struct net_device *dev, struct ethtool_keee *e)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct ethtool_keee *p = &priv->eee;
	bool active;

	if (GENET_IS_V1(priv))
		return -EOPNOTSUPP;

	if (!dev->phydev)
		return -ENODEV;

	p->eee_enabled = e->eee_enabled;

	if (!p->eee_enabled) {
		bcmgenet_eee_enable_set(dev, false, false);
	} else {
		active = phy_init_eee(dev->phydev, false) >= 0;
		bcmgenet_umac_writel(priv, e->tx_lpi_timer, UMAC_EEE_LPI_TIMER);
		bcmgenet_eee_enable_set(dev, active, e->tx_lpi_enabled);
	}

	return phy_ethtool_set_eee(dev->phydev, e);
}

static int bcmgenet_validate_flow(struct net_device *dev,
				  struct ethtool_rxnfc *cmd)
{
	struct ethtool_usrip4_spec *l4_mask;
	struct ethhdr *eth_mask;

	if (cmd->fs.location >= MAX_NUM_OF_FS_RULES &&
	    cmd->fs.location != RX_CLS_LOC_ANY) {
		netdev_err(dev, "rxnfc: Invalid location (%d)\n",
			   cmd->fs.location);
		return -EINVAL;
	}

	switch (cmd->fs.flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case IP_USER_FLOW:
		l4_mask = &cmd->fs.m_u.usr_ip4_spec;
		/* don't allow mask which isn't valid */
		if (VALIDATE_MASK(l4_mask->ip4src) ||
		    VALIDATE_MASK(l4_mask->ip4dst) ||
		    VALIDATE_MASK(l4_mask->l4_4_bytes) ||
		    VALIDATE_MASK(l4_mask->proto) ||
		    VALIDATE_MASK(l4_mask->ip_ver) ||
		    VALIDATE_MASK(l4_mask->tos)) {
			netdev_err(dev, "rxnfc: Unsupported mask\n");
			return -EINVAL;
		}
		break;
	case ETHER_FLOW:
		eth_mask = &cmd->fs.m_u.ether_spec;
		/* don't allow mask which isn't valid */
		if (VALIDATE_MASK(eth_mask->h_dest) ||
		    VALIDATE_MASK(eth_mask->h_source) ||
		    VALIDATE_MASK(eth_mask->h_proto)) {
			netdev_err(dev, "rxnfc: Unsupported mask\n");
			return -EINVAL;
		}
		break;
	default:
		netdev_err(dev, "rxnfc: Unsupported flow type (0x%x)\n",
			   cmd->fs.flow_type);
		return -EINVAL;
	}

	if ((cmd->fs.flow_type & FLOW_EXT)) {
		/* don't allow mask which isn't valid */
		if (VALIDATE_MASK(cmd->fs.m_ext.vlan_etype) ||
		    VALIDATE_MASK(cmd->fs.m_ext.vlan_tci)) {
			netdev_err(dev, "rxnfc: Unsupported mask\n");
			return -EINVAL;
		}
		if (cmd->fs.m_ext.data[0] || cmd->fs.m_ext.data[1]) {
			netdev_err(dev, "rxnfc: user-def not supported\n");
			return -EINVAL;
		}
	}

	if ((cmd->fs.flow_type & FLOW_MAC_EXT)) {
		/* don't allow mask which isn't valid */
		if (VALIDATE_MASK(cmd->fs.m_ext.h_dest)) {
			netdev_err(dev, "rxnfc: Unsupported mask\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int bcmgenet_insert_flow(struct net_device *dev,
				struct ethtool_rxnfc *cmd)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rxnfc_rule *loc_rule;
	int err, i;

	if (priv->hw_params->hfb_filter_size < 128) {
		netdev_err(dev, "rxnfc: Not supported by this device\n");
		return -EINVAL;
	}

	if (cmd->fs.ring_cookie > priv->hw_params->rx_queues &&
	    cmd->fs.ring_cookie != RX_CLS_FLOW_WAKE &&
	    cmd->fs.ring_cookie != RX_CLS_FLOW_DISC) {
		netdev_err(dev, "rxnfc: Unsupported action (%llu)\n",
			   cmd->fs.ring_cookie);
		return -EINVAL;
	}

	err = bcmgenet_validate_flow(dev, cmd);
	if (err)
		return err;

	if (cmd->fs.location == RX_CLS_LOC_ANY) {
		list_for_each_entry(loc_rule, &priv->rxnfc_list, list) {
			cmd->fs.location = loc_rule->fs.location;
			err = memcmp(&loc_rule->fs, &cmd->fs,
				     sizeof(struct ethtool_rx_flow_spec));
			if (!err)
				/* rule exists so return current location */
				return 0;
		}
		for (i = 0; i < MAX_NUM_OF_FS_RULES; i++) {
			loc_rule = &priv->rxnfc_rules[i];
			if (loc_rule->state == BCMGENET_RXNFC_STATE_UNUSED) {
				cmd->fs.location = i;
				break;
			}
		}
		if (i == MAX_NUM_OF_FS_RULES) {
			cmd->fs.location = RX_CLS_LOC_ANY;
			return -ENOSPC;
		}
	} else {
		loc_rule = &priv->rxnfc_rules[cmd->fs.location];
	}
	if (loc_rule->state == BCMGENET_RXNFC_STATE_ENABLED)
		bcmgenet_hfb_disable_filter(priv, cmd->fs.location + 1);
	if (loc_rule->state != BCMGENET_RXNFC_STATE_UNUSED) {
		list_del(&loc_rule->list);
		bcmgenet_hfb_clear_filter(priv, cmd->fs.location + 1);
	}
	loc_rule->state = BCMGENET_RXNFC_STATE_UNUSED;
	memcpy(&loc_rule->fs, &cmd->fs,
	       sizeof(struct ethtool_rx_flow_spec));

	bcmgenet_hfb_create_rxnfc_filter(priv, loc_rule);

	list_add_tail(&loc_rule->list, &priv->rxnfc_list);

	return 0;
}

static int bcmgenet_delete_flow(struct net_device *dev,
				struct ethtool_rxnfc *cmd)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rxnfc_rule *rule;
	int err = 0;

	if (cmd->fs.location >= MAX_NUM_OF_FS_RULES)
		return -EINVAL;

	rule = &priv->rxnfc_rules[cmd->fs.location];
	if (rule->state == BCMGENET_RXNFC_STATE_UNUSED) {
		err =  -ENOENT;
		goto out;
	}

	if (rule->state == BCMGENET_RXNFC_STATE_ENABLED)
		bcmgenet_hfb_disable_filter(priv, cmd->fs.location + 1);
	if (rule->state != BCMGENET_RXNFC_STATE_UNUSED) {
		list_del(&rule->list);
		bcmgenet_hfb_clear_filter(priv, cmd->fs.location + 1);
	}
	rule->state = BCMGENET_RXNFC_STATE_UNUSED;
	memset(&rule->fs, 0, sizeof(struct ethtool_rx_flow_spec));

out:
	return err;
}

static int bcmgenet_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		err = bcmgenet_insert_flow(dev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		err = bcmgenet_delete_flow(dev, cmd);
		break;
	default:
		netdev_warn(priv->dev, "Unsupported ethtool command. (%d)\n",
			    cmd->cmd);
		return -EINVAL;
	}

	return err;
}

static int bcmgenet_get_flow(struct net_device *dev, struct ethtool_rxnfc *cmd,
			     int loc)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rxnfc_rule *rule;
	int err = 0;

	if (loc < 0 || loc >= MAX_NUM_OF_FS_RULES)
		return -EINVAL;

	rule = &priv->rxnfc_rules[loc];
	if (rule->state == BCMGENET_RXNFC_STATE_UNUSED)
		err = -ENOENT;
	else
		memcpy(&cmd->fs, &rule->fs,
		       sizeof(struct ethtool_rx_flow_spec));

	return err;
}

static int bcmgenet_get_num_flows(struct bcmgenet_priv *priv)
{
	struct list_head *pos;
	int res = 0;

	list_for_each(pos, &priv->rxnfc_list)
		res++;

	return res;
}

static int bcmgenet_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			      u32 *rule_locs)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rxnfc_rule *rule;
	int err = 0;
	int i = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = priv->hw_params->rx_queues ?: 1;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = bcmgenet_get_num_flows(priv);
		cmd->data = MAX_NUM_OF_FS_RULES | RX_CLS_LOC_SPECIAL;
		break;
	case ETHTOOL_GRXCLSRULE:
		err = bcmgenet_get_flow(dev, cmd, cmd->fs.location);
		break;
	case ETHTOOL_GRXCLSRLALL:
		list_for_each_entry(rule, &priv->rxnfc_list, list)
			if (i < cmd->rule_cnt)
				rule_locs[i++] = rule->fs.location;
		cmd->rule_cnt = i;
		cmd->data = MAX_NUM_OF_FS_RULES;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

/* standard ethtool support functions. */
static const struct ethtool_ops bcmgenet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.begin			= bcmgenet_begin,
	.complete		= bcmgenet_complete,
	.get_strings		= bcmgenet_get_strings,
	.get_sset_count		= bcmgenet_get_sset_count,
	.get_ethtool_stats	= bcmgenet_get_ethtool_stats,
	.get_drvinfo		= bcmgenet_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= bcmgenet_get_msglevel,
	.set_msglevel		= bcmgenet_set_msglevel,
	.get_wol		= bcmgenet_get_wol,
	.set_wol		= bcmgenet_set_wol,
	.get_eee		= bcmgenet_get_eee,
	.set_eee		= bcmgenet_set_eee,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_coalesce		= bcmgenet_get_coalesce,
	.set_coalesce		= bcmgenet_set_coalesce,
	.get_link_ksettings	= bcmgenet_get_link_ksettings,
	.set_link_ksettings	= bcmgenet_set_link_ksettings,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_rxnfc		= bcmgenet_get_rxnfc,
	.set_rxnfc		= bcmgenet_set_rxnfc,
	.get_pauseparam		= bcmgenet_get_pauseparam,
	.set_pauseparam		= bcmgenet_set_pauseparam,
};

/* Power down the unimac, based on mode. */
static int bcmgenet_power_down(struct bcmgenet_priv *priv,
				enum bcmgenet_power_mode mode)
{
	int ret = 0;
	u32 reg;

	switch (mode) {
	case GENET_POWER_CABLE_SENSE:
		phy_detach(priv->dev->phydev);
		break;

	case GENET_POWER_WOL_MAGIC:
		ret = bcmgenet_wol_power_down_cfg(priv, mode);
		break;

	case GENET_POWER_PASSIVE:
		/* Power down LED */
		if (bcmgenet_has_ext(priv)) {
			reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
			if (GENET_IS_V5(priv) && !bcmgenet_has_ephy_16nm(priv))
				reg |= EXT_PWR_DOWN_PHY_EN |
				       EXT_PWR_DOWN_PHY_RD |
				       EXT_PWR_DOWN_PHY_SD |
				       EXT_PWR_DOWN_PHY_RX |
				       EXT_PWR_DOWN_PHY_TX |
				       EXT_IDDQ_GLBL_PWR;
			else
				reg |= EXT_PWR_DOWN_PHY;

			reg |= (EXT_PWR_DOWN_DLL | EXT_PWR_DOWN_BIAS);
			bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);

			bcmgenet_phy_power_set(priv->dev, false);
		}
		break;
	default:
		break;
	}

	return ret;
}

static int bcmgenet_power_up(struct bcmgenet_priv *priv,
			     enum bcmgenet_power_mode mode)
{
	int ret = 0;
	u32 reg;

	if (!bcmgenet_has_ext(priv))
		return ret;

	reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);

	switch (mode) {
	case GENET_POWER_PASSIVE:
		reg &= ~(EXT_PWR_DOWN_DLL | EXT_PWR_DOWN_BIAS |
			 EXT_ENERGY_DET_MASK);
		if (GENET_IS_V5(priv) && !bcmgenet_has_ephy_16nm(priv)) {
			reg &= ~(EXT_PWR_DOWN_PHY_EN |
				 EXT_PWR_DOWN_PHY_RD |
				 EXT_PWR_DOWN_PHY_SD |
				 EXT_PWR_DOWN_PHY_RX |
				 EXT_PWR_DOWN_PHY_TX |
				 EXT_IDDQ_GLBL_PWR);
			reg |=   EXT_PHY_RESET;
			bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
			mdelay(1);

			reg &=  ~EXT_PHY_RESET;
		} else {
			reg &= ~EXT_PWR_DOWN_PHY;
			reg |= EXT_PWR_DN_EN_LD;
		}
		bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
		bcmgenet_phy_power_set(priv->dev, true);
		break;

	case GENET_POWER_CABLE_SENSE:
		/* enable APD */
		if (!GENET_IS_V5(priv)) {
			reg |= EXT_PWR_DN_EN_LD;
			bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
		}
		break;
	case GENET_POWER_WOL_MAGIC:
		ret = bcmgenet_wol_power_up_cfg(priv, mode);
		break;
	default:
		break;
	}

	return ret;
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

static struct enet_cb *bcmgenet_put_txcb(struct bcmgenet_priv *priv,
					 struct bcmgenet_tx_ring *ring)
{
	struct enet_cb *tx_cb_ptr;

	tx_cb_ptr = ring->cbs;
	tx_cb_ptr += ring->write_ptr - ring->cb_ptr;

	/* Rewinding local write pointer */
	if (ring->write_ptr == ring->cb_ptr)
		ring->write_ptr = ring->end_ptr;
	else
		ring->write_ptr--;

	return tx_cb_ptr;
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

/* Simple helper to free a transmit control block's resources
 * Returns an skb when the last transmit control block associated with the
 * skb is freed.  The skb should be freed by the caller if necessary.
 */
static struct sk_buff *bcmgenet_free_tx_cb(struct device *dev,
					   struct enet_cb *cb)
{
	struct sk_buff *skb;

	skb = cb->skb;

	if (skb) {
		cb->skb = NULL;
		if (cb == GENET_CB(skb)->first_cb)
			dma_unmap_single(dev, dma_unmap_addr(cb, dma_addr),
					 dma_unmap_len(cb, dma_len),
					 DMA_TO_DEVICE);
		else
			dma_unmap_page(dev, dma_unmap_addr(cb, dma_addr),
				       dma_unmap_len(cb, dma_len),
				       DMA_TO_DEVICE);
		dma_unmap_addr_set(cb, dma_addr, 0);

		if (cb == GENET_CB(skb)->last_cb)
			return skb;

	} else if (dma_unmap_addr(cb, dma_addr)) {
		dma_unmap_page(dev,
			       dma_unmap_addr(cb, dma_addr),
			       dma_unmap_len(cb, dma_len),
			       DMA_TO_DEVICE);
		dma_unmap_addr_set(cb, dma_addr, 0);
	}

	return NULL;
}

/* Simple helper to free a receive control block's resources */
static struct sk_buff *bcmgenet_free_rx_cb(struct device *dev,
					   struct enet_cb *cb)
{
	struct sk_buff *skb;

	skb = cb->skb;
	cb->skb = NULL;

	if (dma_unmap_addr(cb, dma_addr)) {
		dma_unmap_single(dev, dma_unmap_addr(cb, dma_addr),
				 dma_unmap_len(cb, dma_len), DMA_FROM_DEVICE);
		dma_unmap_addr_set(cb, dma_addr, 0);
	}

	return skb;
}

/* Unlocked version of the reclaim routine */
static unsigned int __bcmgenet_tx_reclaim(struct net_device *dev,
					  struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_tx_stats64 *stats = &ring->stats64;
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned int txbds_processed = 0;
	unsigned int bytes_compl = 0;
	unsigned int pkts_compl = 0;
	unsigned int txbds_ready;
	unsigned int c_index;
	struct sk_buff *skb;

	/* Clear status before servicing to reduce spurious interrupts */
	bcmgenet_intrl2_1_writel(priv, (1 << ring->index), INTRL2_CPU_CLEAR);

	/* Compute how many buffers are transmitted since last xmit call */
	c_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_CONS_INDEX)
		& DMA_C_INDEX_MASK;
	txbds_ready = (c_index - ring->c_index) & DMA_C_INDEX_MASK;

	netif_dbg(priv, tx_done, dev,
		  "%s ring=%d old_c_index=%u c_index=%u txbds_ready=%u\n",
		  __func__, ring->index, ring->c_index, c_index, txbds_ready);

	/* Reclaim transmitted buffers */
	while (txbds_processed < txbds_ready) {
		skb = bcmgenet_free_tx_cb(&priv->pdev->dev,
					  &priv->tx_cbs[ring->clean_ptr]);
		if (skb) {
			pkts_compl++;
			bytes_compl += GENET_CB(skb)->bytes_sent;
			dev_consume_skb_any(skb);
		}

		txbds_processed++;
		if (likely(ring->clean_ptr < ring->end_ptr))
			ring->clean_ptr++;
		else
			ring->clean_ptr = ring->cb_ptr;
	}

	ring->free_bds += txbds_processed;
	ring->c_index = c_index;

	u64_stats_update_begin(&stats->syncp);
	u64_stats_add(&stats->packets, pkts_compl);
	u64_stats_add(&stats->bytes, bytes_compl);
	u64_stats_update_end(&stats->syncp);

	netdev_tx_completed_queue(netdev_get_tx_queue(dev, ring->index),
				  pkts_compl, bytes_compl);

	return txbds_processed;
}

static unsigned int bcmgenet_tx_reclaim(struct net_device *dev,
				struct bcmgenet_tx_ring *ring,
				bool all)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	unsigned int released, drop, wr_ptr;
	struct enet_cb *cb_ptr;
	struct sk_buff *skb;

	spin_lock_bh(&ring->lock);
	released = __bcmgenet_tx_reclaim(dev, ring);
	if (all) {
		skb = NULL;
		drop = (ring->prod_index - ring->c_index) & DMA_C_INDEX_MASK;
		released += drop;
		ring->prod_index = ring->c_index & DMA_C_INDEX_MASK;
		while (drop--) {
			cb_ptr = bcmgenet_put_txcb(priv, ring);
			skb = cb_ptr->skb;
			bcmgenet_free_tx_cb(kdev, cb_ptr);
			if (skb && cb_ptr == GENET_CB(skb)->first_cb) {
				dev_consume_skb_any(skb);
				skb = NULL;
			}
		}
		if (skb)
			dev_consume_skb_any(skb);
		bcmgenet_tdma_ring_writel(priv, ring->index,
					  ring->prod_index, TDMA_PROD_INDEX);
		wr_ptr = ring->write_ptr * WORDS_PER_BD(priv);
		bcmgenet_tdma_ring_writel(priv, ring->index, wr_ptr,
					  TDMA_WRITE_PTR);
	}
	spin_unlock_bh(&ring->lock);

	return released;
}

static int bcmgenet_tx_poll(struct napi_struct *napi, int budget)
{
	struct bcmgenet_tx_ring *ring =
		container_of(napi, struct bcmgenet_tx_ring, napi);
	unsigned int work_done = 0;
	struct netdev_queue *txq;

	spin_lock(&ring->lock);
	work_done = __bcmgenet_tx_reclaim(ring->priv->dev, ring);
	if (ring->free_bds > (MAX_SKB_FRAGS + 1)) {
		txq = netdev_get_tx_queue(ring->priv->dev, ring->index);
		netif_tx_wake_queue(txq);
	}
	spin_unlock(&ring->lock);

	if (work_done == 0) {
		napi_complete(napi);
		bcmgenet_tx_ring_int_enable(ring);

		return 0;
	}

	return budget;
}

static void bcmgenet_tx_reclaim_all(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int i = 0;

	do {
		bcmgenet_tx_reclaim(dev, &priv->tx_rings[i++], true);
	} while (i <= priv->hw_params->tx_queues && netif_is_multiqueue(dev));
}

/* Reallocate the SKB to put enough headroom in front of it and insert
 * the transmit checksum offsets in the descriptors
 */
static struct sk_buff *bcmgenet_add_tsb(struct net_device *dev,
					struct sk_buff *skb,
					struct bcmgenet_tx_ring *ring)
{
	struct bcmgenet_tx_stats64 *stats = &ring->stats64;
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct status_64 *status = NULL;
	struct sk_buff *new_skb;
	u16 offset;
	u8 ip_proto;
	__be16 ip_ver;
	u32 tx_csum_info;

	if (unlikely(skb_headroom(skb) < sizeof(*status))) {
		/* If 64 byte status block enabled, must make sure skb has
		 * enough headroom for us to insert 64B status block.
		 */
		new_skb = skb_realloc_headroom(skb, sizeof(*status));
		if (!new_skb) {
			dev_kfree_skb_any(skb);
			priv->mib.tx_realloc_tsb_failed++;
			BCMGENET_STATS64_INC(stats, dropped);
			return NULL;
		}
		dev_consume_skb_any(skb);
		skb = new_skb;
		priv->mib.tx_realloc_tsb++;
	}

	skb_push(skb, sizeof(*status));
	status = (struct status_64 *)skb->data;

	if (skb->ip_summed  == CHECKSUM_PARTIAL) {
		ip_ver = skb->protocol;
		switch (ip_ver) {
		case htons(ETH_P_IP):
			ip_proto = ip_hdr(skb)->protocol;
			break;
		case htons(ETH_P_IPV6):
			ip_proto = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			/* don't use UDP flag */
			ip_proto = 0;
			break;
		}

		offset = skb_checksum_start_offset(skb) - sizeof(*status);
		tx_csum_info = (offset << STATUS_TX_CSUM_START_SHIFT) |
				(offset + skb->csum_offset) |
				STATUS_TX_CSUM_LV;

		/* Set the special UDP flag for UDP */
		if (ip_proto == IPPROTO_UDP)
			tx_csum_info |= STATUS_TX_CSUM_PROTO_UDP;

		status->tx_csum_info = tx_csum_info;
	}

	return skb;
}

static void bcmgenet_hide_tsb(struct sk_buff *skb)
{
	__skb_pull(skb, sizeof(struct status_64));
}

static netdev_tx_t bcmgenet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	struct bcmgenet_tx_ring *ring = NULL;
	struct enet_cb *tx_cb_ptr;
	struct netdev_queue *txq;
	int nr_frags, index;
	dma_addr_t mapping;
	unsigned int size;
	skb_frag_t *frag;
	u32 len_stat;
	int ret;
	int i;

	index = skb_get_queue_mapping(skb);
	/* Mapping strategy:
	 * queue_mapping = 0, unclassified, packet xmited through ring 0
	 * queue_mapping = 1, goes to ring 1. (highest priority queue)
	 * queue_mapping = 2, goes to ring 2.
	 * queue_mapping = 3, goes to ring 3.
	 * queue_mapping = 4, goes to ring 4.
	 */
	ring = &priv->tx_rings[index];
	txq = netdev_get_tx_queue(dev, index);

	nr_frags = skb_shinfo(skb)->nr_frags;

	spin_lock(&ring->lock);
	if (ring->free_bds <= (nr_frags + 1)) {
		if (!netif_tx_queue_stopped(txq))
			netif_tx_stop_queue(txq);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	/* Retain how many bytes will be sent on the wire, without TSB inserted
	 * by transmit checksum offload
	 */
	GENET_CB(skb)->bytes_sent = skb->len;

	/* add the Transmit Status Block */
	skb = bcmgenet_add_tsb(dev, skb, ring);
	if (!skb) {
		ret = NETDEV_TX_OK;
		goto out;
	}

	for (i = 0; i <= nr_frags; i++) {
		tx_cb_ptr = bcmgenet_get_txcb(priv, ring);

		BUG_ON(!tx_cb_ptr);

		if (!i) {
			/* Transmit single SKB or head of fragment list */
			GENET_CB(skb)->first_cb = tx_cb_ptr;
			size = skb_headlen(skb);
			mapping = dma_map_single(kdev, skb->data, size,
						 DMA_TO_DEVICE);
		} else {
			/* xmit fragment */
			frag = &skb_shinfo(skb)->frags[i - 1];
			size = skb_frag_size(frag);
			mapping = skb_frag_dma_map(kdev, frag, 0, size,
						   DMA_TO_DEVICE);
		}

		ret = dma_mapping_error(kdev, mapping);
		if (ret) {
			priv->mib.tx_dma_failed++;
			netif_err(priv, tx_err, dev, "Tx DMA map failed\n");
			ret = NETDEV_TX_OK;
			goto out_unmap_frags;
		}
		dma_unmap_addr_set(tx_cb_ptr, dma_addr, mapping);
		dma_unmap_len_set(tx_cb_ptr, dma_len, size);

		tx_cb_ptr->skb = skb;

		len_stat = (size << DMA_BUFLENGTH_SHIFT) |
			   (priv->hw_params->qtag_mask << DMA_TX_QTAG_SHIFT);

		/* Note: if we ever change from DMA_TX_APPEND_CRC below we
		 * will need to restore software padding of "runt" packets
		 */
		len_stat |= DMA_TX_APPEND_CRC;

		if (!i) {
			len_stat |= DMA_SOP;
			if (skb->ip_summed == CHECKSUM_PARTIAL)
				len_stat |= DMA_TX_DO_CSUM;
		}
		if (i == nr_frags)
			len_stat |= DMA_EOP;

		dmadesc_set(priv, tx_cb_ptr->bd_addr, mapping, len_stat);
	}

	GENET_CB(skb)->last_cb = tx_cb_ptr;

	bcmgenet_hide_tsb(skb);
	skb_tx_timestamp(skb);

	/* Decrement total BD count and advance our write pointer */
	ring->free_bds -= nr_frags + 1;
	ring->prod_index += nr_frags + 1;
	ring->prod_index &= DMA_P_INDEX_MASK;

	netdev_tx_sent_queue(txq, GENET_CB(skb)->bytes_sent);

	if (ring->free_bds <= (MAX_SKB_FRAGS + 1))
		netif_tx_stop_queue(txq);

	if (!netdev_xmit_more() || netif_xmit_stopped(txq))
		/* Packets are ready, update producer index */
		bcmgenet_tdma_ring_writel(priv, ring->index,
					  ring->prod_index, TDMA_PROD_INDEX);
out:
	spin_unlock(&ring->lock);

	return ret;

out_unmap_frags:
	/* Back up for failed control block mapping */
	bcmgenet_put_txcb(priv, ring);

	/* Unmap successfully mapped control blocks */
	while (i-- > 0) {
		tx_cb_ptr = bcmgenet_put_txcb(priv, ring);
		bcmgenet_free_tx_cb(kdev, tx_cb_ptr);
	}

	dev_kfree_skb(skb);
	goto out;
}

static struct sk_buff *bcmgenet_rx_refill(struct bcmgenet_priv *priv,
					  struct enet_cb *cb)
{
	struct device *kdev = &priv->pdev->dev;
	struct sk_buff *skb;
	struct sk_buff *rx_skb;
	dma_addr_t mapping;

	/* Allocate a new Rx skb */
	skb = __netdev_alloc_skb(priv->dev, priv->rx_buf_len + SKB_ALIGNMENT,
				 GFP_ATOMIC | __GFP_NOWARN);
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
	rx_skb = bcmgenet_free_rx_cb(kdev, cb);

	/* Put the new Rx skb on the ring */
	cb->skb = skb;
	dma_unmap_addr_set(cb, dma_addr, mapping);
	dma_unmap_len_set(cb, dma_len, priv->rx_buf_len);
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
	struct bcmgenet_rx_stats64 *stats = &ring->stats64;
	struct bcmgenet_priv *priv = ring->priv;
	struct net_device *dev = priv->dev;
	struct enet_cb *cb;
	struct sk_buff *skb;
	u32 dma_length_status;
	unsigned long dma_flag;
	int len;
	unsigned int rxpktprocessed = 0, rxpkttoprocess;
	unsigned int bytes_processed = 0;
	unsigned int p_index, mask;
	unsigned int discards;

	/* Clear status before servicing to reduce spurious interrupts */
	mask = 1 << (UMAC_IRQ1_RX_INTR_SHIFT + ring->index);
	bcmgenet_intrl2_1_writel(priv, mask, INTRL2_CPU_CLEAR);

	p_index = bcmgenet_rdma_ring_readl(priv, ring->index, RDMA_PROD_INDEX);

	discards = (p_index >> DMA_P_INDEX_DISCARD_CNT_SHIFT) &
		   DMA_P_INDEX_DISCARD_CNT_MASK;
	if (discards > ring->old_discards) {
		discards = discards - ring->old_discards;
		BCMGENET_STATS64_ADD(stats, missed, discards);
		ring->old_discards += discards;

		/* Clear HW register when we reach 75% of maximum 0xFFFF */
		if (ring->old_discards >= 0xC000) {
			ring->old_discards = 0;
			bcmgenet_rdma_ring_writel(priv, ring->index, 0,
						  RDMA_PROD_INDEX);
		}
	}

	p_index &= DMA_P_INDEX_MASK;
	rxpkttoprocess = (p_index - ring->c_index) & DMA_C_INDEX_MASK;

	netif_dbg(priv, rx_status, dev,
		  "RDMA: rxpkttoprocess=%d\n", rxpkttoprocess);

	while ((rxpktprocessed < rxpkttoprocess) &&
	       (rxpktprocessed < budget)) {
		struct status_64 *status;
		__be16 rx_csum;

		cb = &priv->rx_cbs[ring->read_ptr];
		skb = bcmgenet_rx_refill(priv, cb);

		if (unlikely(!skb)) {
			BCMGENET_STATS64_INC(stats, dropped);
			goto next;
		}

		status = (struct status_64 *)skb->data;
		dma_length_status = status->length_status;
		if (dev->features & NETIF_F_RXCSUM) {
			rx_csum = (__force __be16)(status->rx_csum & 0xffff);
			if (rx_csum) {
				skb->csum = (__force __wsum)ntohs(rx_csum);
				skb->ip_summed = CHECKSUM_COMPLETE;
			}
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

		if (unlikely(len > RX_BUF_LENGTH)) {
			netif_err(priv, rx_status, dev, "oversized packet\n");
			BCMGENET_STATS64_INC(stats, length_errors);
			dev_kfree_skb_any(skb);
			goto next;
		}

		if (unlikely(!(dma_flag & DMA_EOP) || !(dma_flag & DMA_SOP))) {
			netif_err(priv, rx_status, dev,
				  "dropping fragmented packet!\n");
			BCMGENET_STATS64_INC(stats, fragmented_errors);
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
			u64_stats_update_begin(&stats->syncp);
			if (dma_flag & DMA_RX_CRC_ERROR)
				u64_stats_inc(&stats->crc_errors);
			if (dma_flag & DMA_RX_OV)
				u64_stats_inc(&stats->over_errors);
			if (dma_flag & DMA_RX_NO)
				u64_stats_inc(&stats->frame_errors);
			if (dma_flag & DMA_RX_LG)
				u64_stats_inc(&stats->length_errors);
			if ((dma_flag & (DMA_RX_CRC_ERROR |
						DMA_RX_OV |
						DMA_RX_NO |
						DMA_RX_LG |
						DMA_RX_RXER)) == DMA_RX_RXER)
				u64_stats_inc(&stats->errors);
			u64_stats_update_end(&stats->syncp);
			dev_kfree_skb_any(skb);
			goto next;
		} /* error packet */

		skb_put(skb, len);

		/* remove RSB and hardware 2bytes added for IP alignment */
		skb_pull(skb, 66);
		len -= 66;

		if (priv->crc_fwd_en) {
			skb_trim(skb, len - ETH_FCS_LEN);
			len -= ETH_FCS_LEN;
		}

		bytes_processed += len;

		/*Finish setting up the received SKB and send it to the kernel*/
		skb->protocol = eth_type_trans(skb, priv->dev);

		u64_stats_update_begin(&stats->syncp);
		u64_stats_inc(&stats->packets);
		u64_stats_add(&stats->bytes, len);
		if (dma_flag & DMA_RX_MULT)
			u64_stats_inc(&stats->multicast);
		else if (dma_flag & DMA_RX_BRDCAST)
			u64_stats_inc(&stats->broadcast);
		u64_stats_update_end(&stats->syncp);

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

	ring->dim.bytes = bytes_processed;
	ring->dim.packets = rxpktprocessed;

	return rxpktprocessed;
}

/* Rx NAPI polling method */
static int bcmgenet_rx_poll(struct napi_struct *napi, int budget)
{
	struct bcmgenet_rx_ring *ring = container_of(napi,
			struct bcmgenet_rx_ring, napi);
	struct dim_sample dim_sample = {};
	unsigned int work_done;

	work_done = bcmgenet_desc_rx(ring, budget);

	if (work_done < budget && napi_complete_done(napi, work_done))
		bcmgenet_rx_ring_int_enable(ring);

	if (ring->dim.use_dim) {
		dim_update_sample(ring->dim.event_ctr, ring->dim.packets,
				  ring->dim.bytes, &dim_sample);
		net_dim(&ring->dim.dim, &dim_sample);
	}

	return work_done;
}

static void bcmgenet_dim_work(struct work_struct *work)
{
	struct dim *dim = container_of(work, struct dim, work);
	struct bcmgenet_net_dim *ndim =
			container_of(dim, struct bcmgenet_net_dim, dim);
	struct bcmgenet_rx_ring *ring =
			container_of(ndim, struct bcmgenet_rx_ring, dim);
	struct dim_cq_moder cur_profile =
			net_dim_get_rx_moderation(dim->mode, dim->profile_ix);

	bcmgenet_set_rx_coalesce(ring, cur_profile.usec, cur_profile.pkts);
	dim->state = DIM_START_MEASURE;
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
			dev_consume_skb_any(skb);
		if (!cb->skb)
			return -ENOMEM;
	}

	return 0;
}

static void bcmgenet_free_rx_buffers(struct bcmgenet_priv *priv)
{
	struct sk_buff *skb;
	struct enet_cb *cb;
	int i;

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = &priv->rx_cbs[i];

		skb = bcmgenet_free_rx_cb(&priv->pdev->dev, cb);
		if (skb)
			dev_consume_skb_any(skb);
	}
}

static void umac_enable_set(struct bcmgenet_priv *priv, u32 mask, bool enable)
{
	u32 reg;

	spin_lock_bh(&priv->reg_lock);
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	if (reg & CMD_SW_RESET) {
		spin_unlock_bh(&priv->reg_lock);
		return;
	}
	if (enable)
		reg |= mask;
	else
		reg &= ~mask;
	bcmgenet_umac_writel(priv, reg, UMAC_CMD);
	spin_unlock_bh(&priv->reg_lock);

	/* UniMAC stops on a packet boundary, wait for a full-size packet
	 * to be processed
	 */
	if (enable == 0)
		usleep_range(1000, 2000);
}

static void reset_umac(struct bcmgenet_priv *priv)
{
	/* 7358a0/7552a0: bad default in RBUF_FLUSH_CTRL.umac_sw_rst */
	bcmgenet_rbuf_ctrl_set(priv, 0);
	udelay(10);

	/* issue soft reset and disable MAC while updating its registers */
	spin_lock_bh(&priv->reg_lock);
	bcmgenet_umac_writel(priv, CMD_SW_RESET, UMAC_CMD);
	udelay(2);
	spin_unlock_bh(&priv->reg_lock);
}

static void bcmgenet_intr_disable(struct bcmgenet_priv *priv)
{
	/* Mask all interrupts.*/
	bcmgenet_intrl2_0_writel(priv, 0xFFFFFFFF, INTRL2_CPU_MASK_SET);
	bcmgenet_intrl2_0_writel(priv, 0xFFFFFFFF, INTRL2_CPU_CLEAR);
	bcmgenet_intrl2_1_writel(priv, 0xFFFFFFFF, INTRL2_CPU_MASK_SET);
	bcmgenet_intrl2_1_writel(priv, 0xFFFFFFFF, INTRL2_CPU_CLEAR);
}

static void bcmgenet_link_intr_enable(struct bcmgenet_priv *priv)
{
	u32 int0_enable = 0;

	/* Monitor cable plug/unplugged event for internal PHY, external PHY
	 * and MoCA PHY
	 */
	if (priv->internal_phy) {
		int0_enable |= UMAC_IRQ_LINK_EVENT;
		if (GENET_IS_V1(priv) || GENET_IS_V2(priv) || GENET_IS_V3(priv))
			int0_enable |= UMAC_IRQ_PHY_DET_R;
	} else if (priv->ext_phy) {
		int0_enable |= UMAC_IRQ_LINK_EVENT;
	} else if (priv->phy_interface == PHY_INTERFACE_MODE_MOCA) {
		if (bcmgenet_has_moca_link_det(priv))
			int0_enable |= UMAC_IRQ_LINK_EVENT;
	}
	bcmgenet_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);
}

static void init_umac(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	u32 reg;
	u32 int0_enable = 0;

	dev_dbg(&priv->pdev->dev, "bcmgenet: init_umac\n");

	reset_umac(priv);

	/* clear tx/rx counter */
	bcmgenet_umac_writel(priv,
			     MIB_RESET_RX | MIB_RESET_TX | MIB_RESET_RUNT,
			     UMAC_MIB_CTRL);
	bcmgenet_umac_writel(priv, 0, UMAC_MIB_CTRL);

	bcmgenet_umac_writel(priv, ENET_MAX_MTU_SIZE, UMAC_MAX_FRAME_LEN);

	/* init tx registers, enable TSB */
	reg = bcmgenet_tbuf_ctrl_get(priv);
	reg |= TBUF_64B_EN;
	bcmgenet_tbuf_ctrl_set(priv, reg);

	/* init rx registers, enable ip header optimization and RSB */
	reg = bcmgenet_rbuf_readl(priv, RBUF_CTRL);
	reg |= RBUF_ALIGN_2B | RBUF_64B_EN;
	bcmgenet_rbuf_writel(priv, reg, RBUF_CTRL);

	/* enable rx checksumming */
	reg = bcmgenet_rbuf_readl(priv, RBUF_CHK_CTRL);
	reg |= RBUF_RXCHK_EN | RBUF_L3_PARSE_DIS;
	/* If UniMAC forwards CRC, we need to skip over it to get
	 * a valid CHK bit to be set in the per-packet status word
	 */
	if (priv->crc_fwd_en)
		reg |= RBUF_SKIP_FCS;
	else
		reg &= ~RBUF_SKIP_FCS;
	bcmgenet_rbuf_writel(priv, reg, RBUF_CHK_CTRL);

	if (!GENET_IS_V1(priv) && !GENET_IS_V2(priv))
		bcmgenet_rbuf_writel(priv, 1, RBUF_TBUF_SIZE_CTRL);

	bcmgenet_intr_disable(priv);

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
	if (bcmgenet_has_mdio_intr(priv))
		int0_enable |= UMAC_IRQ_MDIO_EVENT;

	bcmgenet_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);

	dev_dbg(kdev, "done init umac\n");
}

static void bcmgenet_init_dim(struct bcmgenet_rx_ring *ring,
			      void (*cb)(struct work_struct *work))
{
	struct bcmgenet_net_dim *dim = &ring->dim;

	INIT_WORK(&dim->dim.work, cb);
	dim->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	dim->event_ctr = 0;
	dim->packets = 0;
	dim->bytes = 0;
}

static void bcmgenet_init_rx_coalesce(struct bcmgenet_rx_ring *ring)
{
	struct bcmgenet_net_dim *dim = &ring->dim;
	struct dim_cq_moder moder;
	u32 usecs, pkts;

	usecs = ring->rx_coalesce_usecs;
	pkts = ring->rx_max_coalesced_frames;

	/* If DIM was enabled, re-apply default parameters */
	if (dim->use_dim) {
		moder = net_dim_get_def_rx_moderation(dim->dim.mode);
		usecs = moder.usec;
		pkts = moder.pkts;
	}

	bcmgenet_set_rx_coalesce(ring, usecs, pkts);
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
	ring->cbs = priv->tx_cbs + start_ptr;
	ring->size = size;
	ring->clean_ptr = start_ptr;
	ring->c_index = 0;
	ring->free_bds = size;
	ring->write_ptr = start_ptr;
	ring->cb_ptr = start_ptr;
	ring->end_ptr = end_ptr - 1;
	ring->prod_index = 0;

	/* Set flow period for ring != 0 */
	if (index)
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

	/* Initialize Tx NAPI */
	netif_napi_add_tx(priv->dev, &ring->napi, bcmgenet_tx_poll);
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
	ring->cbs = priv->rx_cbs + start_ptr;
	ring->size = size;
	ring->c_index = 0;
	ring->read_ptr = start_ptr;
	ring->cb_ptr = start_ptr;
	ring->end_ptr = end_ptr - 1;

	ret = bcmgenet_alloc_rx_buffers(priv, ring);
	if (ret)
		return ret;

	bcmgenet_init_dim(ring, bcmgenet_dim_work);
	bcmgenet_init_rx_coalesce(ring);

	/* Initialize Rx NAPI */
	netif_napi_add(priv->dev, &ring->napi, bcmgenet_rx_poll);

	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_PROD_INDEX);
	bcmgenet_rdma_ring_writel(priv, index, 0, RDMA_CONS_INDEX);
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

static void bcmgenet_enable_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i <= priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		napi_enable(&ring->napi);
		bcmgenet_tx_ring_int_enable(ring);
	}
}

static void bcmgenet_disable_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i <= priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		napi_disable(&ring->napi);
	}
}

static void bcmgenet_fini_tx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_tx_ring *ring;

	for (i = 0; i <= priv->hw_params->tx_queues; ++i) {
		ring = &priv->tx_rings[i];
		netif_napi_del(&ring->napi);
	}
}

static int bcmgenet_tdma_disable(struct bcmgenet_priv *priv)
{
	int timeout = 0;
	u32 reg, mask;

	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	mask = (1 << (priv->hw_params->tx_queues + 1)) - 1;
	mask = (mask << DMA_RING_BUF_EN_SHIFT) | DMA_EN;
	reg &= ~mask;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);

	/* Check DMA status register to confirm DMA is disabled */
	while (timeout++ < DMA_TIMEOUT_VAL) {
		reg = bcmgenet_tdma_readl(priv, DMA_STATUS);
		if ((reg & mask) == mask)
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

static int bcmgenet_rdma_disable(struct bcmgenet_priv *priv)
{
	int timeout = 0;
	u32 reg, mask;

	reg = bcmgenet_rdma_readl(priv, DMA_CTRL);
	mask = (1 << (priv->hw_params->rx_queues + 1)) - 1;
	mask = (mask << DMA_RING_BUF_EN_SHIFT) | DMA_EN;
	reg &= ~mask;
	bcmgenet_rdma_writel(priv, reg, DMA_CTRL);

	/* Check DMA status register to confirm DMA is disabled */
	while (timeout++ < DMA_TIMEOUT_VAL) {
		reg = bcmgenet_rdma_readl(priv, DMA_STATUS);
		if ((reg & mask) == mask)
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

/* Initialize Tx queues
 *
 * Queues 1-4 are priority-based, each one has 32 descriptors,
 * with queue 1 being the highest priority queue.
 *
 * Queue 0 is the default Tx queue with
 * GENET_Q0_TX_BD_CNT = 256 - 4 * 32 = 128 descriptors.
 *
 * The transmit control block pool is then partitioned as follows:
 * - Tx queue 0 uses tx_cbs[0..127]
 * - Tx queue 1 uses tx_cbs[128..159]
 * - Tx queue 2 uses tx_cbs[160..191]
 * - Tx queue 3 uses tx_cbs[192..223]
 * - Tx queue 4 uses tx_cbs[224..255]
 */
static void bcmgenet_init_tx_queues(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned int start = 0, end = GENET_Q0_TX_BD_CNT;
	u32 i, ring_mask, dma_priority[3] = {0, 0, 0};

	/* Enable strict priority arbiter mode */
	bcmgenet_tdma_writel(priv, DMA_ARBITER_SP, DMA_ARB_CTRL);

	/* Initialize Tx priority queues */
	for (i = 0; i <= priv->hw_params->tx_queues; i++) {
		bcmgenet_init_tx_ring(priv, i, end - start, start, end);
		start = end;
		end += priv->hw_params->tx_bds_per_q;
		dma_priority[DMA_PRIO_REG_INDEX(i)] |=
			(i ? GENET_Q1_PRIORITY : GENET_Q0_PRIORITY)
			<< DMA_PRIO_REG_SHIFT(i);
	}

	/* Set Tx queue priorities */
	bcmgenet_tdma_writel(priv, dma_priority[0], DMA_PRIORITY_0);
	bcmgenet_tdma_writel(priv, dma_priority[1], DMA_PRIORITY_1);
	bcmgenet_tdma_writel(priv, dma_priority[2], DMA_PRIORITY_2);

	/* Configure Tx queues as descriptor rings */
	ring_mask = (1 << (priv->hw_params->tx_queues + 1)) - 1;
	bcmgenet_tdma_writel(priv, ring_mask, DMA_RING_CFG);

	/* Enable Tx rings */
	ring_mask <<= DMA_RING_BUF_EN_SHIFT;
	bcmgenet_tdma_writel(priv, ring_mask, DMA_CTRL);
}

static void bcmgenet_enable_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i <= priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		napi_enable(&ring->napi);
		bcmgenet_rx_ring_int_enable(ring);
	}
}

static void bcmgenet_disable_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i <= priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		napi_disable(&ring->napi);
		cancel_work_sync(&ring->dim.dim.work);
	}
}

static void bcmgenet_fini_rx_napi(struct bcmgenet_priv *priv)
{
	unsigned int i;
	struct bcmgenet_rx_ring *ring;

	for (i = 0; i <= priv->hw_params->rx_queues; ++i) {
		ring = &priv->rx_rings[i];
		netif_napi_del(&ring->napi);
	}
}

/* Initialize Rx queues
 *
 * Queues 0-15 are priority queues. Hardware Filtering Block (HFB) can be
 * used to direct traffic to these queues.
 *
 * Queue 0 is also the default Rx queue with GENET_Q0_RX_BD_CNT descriptors.
 */
static int bcmgenet_init_rx_queues(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	unsigned int start = 0, end = GENET_Q0_RX_BD_CNT;
	u32 i, ring_mask;
	int ret;

	/* Initialize Rx priority queues */
	for (i = 0; i <= priv->hw_params->rx_queues; i++) {
		ret = bcmgenet_init_rx_ring(priv, i, end - start, start, end);
		if (ret)
			return ret;

		start = end;
		end += priv->hw_params->rx_bds_per_q;
	}

	/* Configure Rx queues as descriptor rings */
	ring_mask = (1 << (priv->hw_params->rx_queues + 1)) - 1;
	bcmgenet_rdma_writel(priv, ring_mask, DMA_RING_CFG);

	/* Enable Rx rings */
	ring_mask <<= DMA_RING_BUF_EN_SHIFT;
	bcmgenet_rdma_writel(priv, ring_mask, DMA_CTRL);

	return 0;
}

static int bcmgenet_dma_teardown(struct bcmgenet_priv *priv)
{
	int ret = 0;

	/* Disable TDMA to stop add more frames in TX DMA */
	if (-ETIMEDOUT == bcmgenet_tdma_disable(priv)) {
		netdev_warn(priv->dev, "Timed out while disabling TX DMA\n");
		ret = -ETIMEDOUT;
	}

	/* Wait 10ms for packet drain in both tx and rx dma */
	usleep_range(10000, 20000);

	/* Disable RDMA */
	if (-ETIMEDOUT == bcmgenet_rdma_disable(priv)) {
		netdev_warn(priv->dev, "Timed out while disabling RX DMA\n");
		ret = -ETIMEDOUT;
	}

	return ret;
}

static void bcmgenet_fini_dma(struct bcmgenet_priv *priv)
{
	struct netdev_queue *txq;
	int i;

	bcmgenet_fini_rx_napi(priv);
	bcmgenet_fini_tx_napi(priv);

	for (i = 0; i <= priv->hw_params->tx_queues; i++) {
		txq = netdev_get_tx_queue(priv->dev, i);
		netdev_tx_reset_queue(txq);
	}

	bcmgenet_free_rx_buffers(priv);
	kfree(priv->rx_cbs);
	kfree(priv->tx_cbs);
}

/* init_edma: Initialize DMA control register */
static int bcmgenet_init_dma(struct bcmgenet_priv *priv, bool flush_rx)
{
	struct enet_cb *cb;
	unsigned int i;
	int ret;
	u32 reg;

	netif_dbg(priv, hw, priv->dev, "%s\n", __func__);

	/* Disable TX DMA */
	ret = bcmgenet_tdma_disable(priv);
	if (ret) {
		netdev_err(priv->dev, "failed to halt Tx DMA\n");
		return ret;
	}

	/* Disable RX DMA */
	ret = bcmgenet_rdma_disable(priv);
	if (ret) {
		netdev_err(priv->dev, "failed to halt Rx DMA\n");
		return ret;
	}

	/* Flush TX queues */
	bcmgenet_umac_writel(priv, 1, UMAC_TX_FLUSH);
	udelay(10);
	bcmgenet_umac_writel(priv, 0, UMAC_TX_FLUSH);

	if (flush_rx) {
		reg = bcmgenet_rbuf_ctrl_get(priv);
		bcmgenet_rbuf_ctrl_set(priv, reg | BIT(0));
		udelay(10);
		bcmgenet_rbuf_ctrl_set(priv, reg);
		udelay(10);
	}

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
	bcmgenet_rdma_writel(priv, priv->dma_max_burst_length,
			     DMA_SCB_BURST_SIZE);

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
	bcmgenet_tdma_writel(priv, priv->dma_max_burst_length,
			     DMA_SCB_BURST_SIZE);

	/* Initialize Tx queues */
	bcmgenet_init_tx_queues(priv->dev);

	/* Enable RX/TX DMA */
	reg = bcmgenet_rdma_readl(priv, DMA_CTRL);
	reg |= DMA_EN;
	bcmgenet_rdma_writel(priv, reg, DMA_CTRL);

	reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
	reg |= DMA_EN;
	bcmgenet_tdma_writel(priv, reg, DMA_CTRL);

	return 0;
}

/* Interrupt bottom half */
static void bcmgenet_irq_task(struct work_struct *work)
{
	unsigned int status;
	struct bcmgenet_priv *priv = container_of(
			work, struct bcmgenet_priv, bcmgenet_irq_work);

	netif_dbg(priv, intr, priv->dev, "%s\n", __func__);

	spin_lock_irq(&priv->lock);
	status = priv->irq0_stat;
	priv->irq0_stat = 0;
	spin_unlock_irq(&priv->lock);

	if (status & UMAC_IRQ_PHY_DET_R &&
	    priv->dev->phydev->autoneg != AUTONEG_ENABLE) {
		phy_init_hw(priv->dev->phydev);
		genphy_config_aneg(priv->dev->phydev);
	}

	/* Link UP/DOWN event */
	if (status & UMAC_IRQ_LINK_EVENT)
		phy_mac_interrupt(priv->dev->phydev);

}

/* bcmgenet_isr1: handle Rx and Tx queues */
static irqreturn_t bcmgenet_isr1(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;
	struct bcmgenet_rx_ring *rx_ring;
	struct bcmgenet_tx_ring *tx_ring;
	unsigned int index, status;

	/* Read irq status */
	status = bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_STAT) &
		~bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_MASK_STATUS);

	/* clear interrupts */
	bcmgenet_intrl2_1_writel(priv, status, INTRL2_CPU_CLEAR);

	netif_dbg(priv, intr, priv->dev,
		  "%s: IRQ=0x%x\n", __func__, status);

	/* Check Rx priority queue interrupts */
	for (index = 0; index <= priv->hw_params->rx_queues; index++) {
		if (!(status & BIT(UMAC_IRQ1_RX_INTR_SHIFT + index)))
			continue;

		rx_ring = &priv->rx_rings[index];
		rx_ring->dim.event_ctr++;

		if (likely(napi_schedule_prep(&rx_ring->napi))) {
			bcmgenet_rx_ring_int_disable(rx_ring);
			__napi_schedule_irqoff(&rx_ring->napi);
		}
	}

	/* Check Tx priority queue interrupts */
	for (index = 0; index <= priv->hw_params->tx_queues; index++) {
		if (!(status & BIT(index)))
			continue;

		tx_ring = &priv->tx_rings[index];

		if (likely(napi_schedule_prep(&tx_ring->napi))) {
			bcmgenet_tx_ring_int_disable(tx_ring);
			__napi_schedule_irqoff(&tx_ring->napi);
		}
	}

	return IRQ_HANDLED;
}

/* bcmgenet_isr0: handle other stuff */
static irqreturn_t bcmgenet_isr0(int irq, void *dev_id)
{
	struct bcmgenet_priv *priv = dev_id;
	unsigned int status;
	unsigned long flags;

	/* Read irq status */
	status = bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_STAT) &
		~bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_MASK_STATUS);

	/* clear interrupts */
	bcmgenet_intrl2_0_writel(priv, status, INTRL2_CPU_CLEAR);

	netif_dbg(priv, intr, priv->dev,
		  "IRQ=0x%x\n", status);

	if (bcmgenet_has_mdio_intr(priv) && status & UMAC_IRQ_MDIO_EVENT)
		wake_up(&priv->wq);

	/* all other interested interrupts handled in bottom half */
	status &= (UMAC_IRQ_LINK_EVENT | UMAC_IRQ_PHY_DET_R);
	if (status) {
		/* Save irq status for bottom-half processing. */
		spin_lock_irqsave(&priv->lock, flags);
		priv->irq0_stat |= status;
		spin_unlock_irqrestore(&priv->lock, flags);

		schedule_work(&priv->bcmgenet_irq_work);
	}

	return IRQ_HANDLED;
}

static irqreturn_t bcmgenet_wol_isr(int irq, void *dev_id)
{
	/* Acknowledge the interrupt */
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
				 const unsigned char *addr)
{
	bcmgenet_umac_writel(priv, get_unaligned_be32(&addr[0]), UMAC_MAC0);
	bcmgenet_umac_writel(priv, get_unaligned_be16(&addr[4]), UMAC_MAC1);
}

static void bcmgenet_get_hw_addr(struct bcmgenet_priv *priv,
				 unsigned char *addr)
{
	u32 addr_tmp;

	addr_tmp = bcmgenet_umac_readl(priv, UMAC_MAC0);
	put_unaligned_be32(addr_tmp, &addr[0]);
	addr_tmp = bcmgenet_umac_readl(priv, UMAC_MAC1);
	put_unaligned_be16(addr_tmp, &addr[4]);
}

static void bcmgenet_netif_start(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	/* Start the network engine */
	netif_addr_lock_bh(dev);
	bcmgenet_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	bcmgenet_enable_rx_napi(priv);

	umac_enable_set(priv, CMD_TX_EN | CMD_RX_EN, true);

	bcmgenet_enable_tx_napi(priv);

	/* Monitor link interrupts now */
	bcmgenet_link_intr_enable(priv);

	phy_start(dev->phydev);
}

static int bcmgenet_open(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
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

	init_umac(priv);

	/* Apply features again in case we changed them while interface was
	 * down
	 */
	bcmgenet_set_features(dev, dev->features);

	bcmgenet_set_hw_addr(priv, dev->dev_addr);

	/* HFB init */
	bcmgenet_hfb_init(priv);

	/* Reinitialize TDMA and RDMA and SW housekeeping */
	ret = bcmgenet_init_dma(priv, true);
	if (ret) {
		netdev_err(dev, "failed to initialize DMA\n");
		goto err_clk_disable;
	}

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

	bcmgenet_phy_pause_set(dev, priv->rx_pause, priv->tx_pause);

	bcmgenet_netif_start(dev);

	netif_tx_start_all_queues(dev);

	return 0;

err_irq1:
	free_irq(priv->irq1, priv);
err_irq0:
	free_irq(priv->irq0, priv);
err_fini_dma:
	bcmgenet_dma_teardown(priv);
	bcmgenet_fini_dma(priv);
err_clk_disable:
	if (priv->internal_phy)
		bcmgenet_power_down(priv, GENET_POWER_PASSIVE);
	clk_disable_unprepare(priv->clk);
	return ret;
}

static void bcmgenet_netif_stop(struct net_device *dev, bool stop_phy)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	netif_tx_disable(dev);

	/* Disable MAC receive */
	bcmgenet_hfb_reg_writel(priv, 0, HFB_CTRL);
	umac_enable_set(priv, CMD_RX_EN, false);

	if (stop_phy)
		phy_stop(dev->phydev);

	bcmgenet_dma_teardown(priv);

	/* Disable MAC transmit. TX DMA disabled must be done before this */
	umac_enable_set(priv, CMD_TX_EN, false);

	bcmgenet_disable_tx_napi(priv);
	bcmgenet_disable_rx_napi(priv);
	bcmgenet_intr_disable(priv);

	/* Wait for pending work items to complete. Since interrupts are
	 * disabled no new work will be scheduled.
	 */
	cancel_work_sync(&priv->bcmgenet_irq_work);

	/* tx reclaim */
	bcmgenet_tx_reclaim_all(dev);
	bcmgenet_fini_dma(priv);
}

static int bcmgenet_close(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret = 0;

	netif_dbg(priv, ifdown, dev, "bcmgenet_close\n");

	bcmgenet_netif_stop(dev, false);

	/* Really kill the PHY state machine and disconnect from it */
	phy_disconnect(dev->phydev);

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
	bool txq_stopped;

	if (!netif_msg_tx_err(priv))
		return;

	txq = netdev_get_tx_queue(priv->dev, ring->index);

	spin_lock(&ring->lock);
	intsts = ~bcmgenet_intrl2_1_readl(priv, INTRL2_CPU_MASK_STATUS);
	intmsk = 1 << ring->index;
	c_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_CONS_INDEX);
	p_index = bcmgenet_tdma_ring_readl(priv, ring->index, TDMA_PROD_INDEX);
	txq_stopped = netif_tx_queue_stopped(txq);
	free_bds = ring->free_bds;
	spin_unlock(&ring->lock);

	netif_err(priv, tx_err, priv->dev, "Ring %d queue %d status summary\n"
		  "TX queue status: %s, interrupts: %s\n"
		  "(sw)free_bds: %d (sw)size: %d\n"
		  "(sw)p_index: %d (hw)p_index: %d\n"
		  "(sw)c_index: %d (hw)c_index: %d\n"
		  "(sw)clean_p: %d (sw)write_p: %d\n"
		  "(sw)cb_ptr: %d (sw)end_ptr: %d\n",
		  ring->index, ring->index,
		  txq_stopped ? "stopped" : "active",
		  intsts & intmsk ? "enabled" : "disabled",
		  free_bds, ring->size,
		  ring->prod_index, p_index & DMA_P_INDEX_MASK,
		  ring->c_index, c_index & DMA_C_INDEX_MASK,
		  ring->clean_ptr, ring->write_ptr,
		  ring->cb_ptr, ring->end_ptr);
}

static void bcmgenet_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 int1_enable = 0;
	unsigned int q;

	netif_dbg(priv, tx_err, dev, "bcmgenet_timeout\n");

	for (q = 0; q <= priv->hw_params->tx_queues; q++)
		bcmgenet_dump_tx_queue(&priv->tx_rings[q]);

	bcmgenet_tx_reclaim_all(dev);

	for (q = 0; q <= priv->hw_params->tx_queues; q++)
		int1_enable |= (1 << q);

	/* Re-enable TX interrupts if disabled */
	bcmgenet_intrl2_1_writel(priv, int1_enable, INTRL2_CPU_MASK_CLEAR);

	netif_trans_update(dev);

	BCMGENET_STATS64_INC((&priv->tx_rings[txqueue].stats64), errors);

	netif_tx_wake_all_queues(dev);
}

#define MAX_MDF_FILTER	17

static inline void bcmgenet_set_mdf_addr(struct bcmgenet_priv *priv,
					 const unsigned char *addr,
					 int *i)
{
	bcmgenet_umac_writel(priv, addr[0] << 8 | addr[1],
			     UMAC_MDF_ADDR + (*i * 4));
	bcmgenet_umac_writel(priv, addr[2] << 24 | addr[3] << 16 |
			     addr[4] << 8 | addr[5],
			     UMAC_MDF_ADDR + ((*i + 1) * 4));
	*i += 2;
}

static void bcmgenet_set_rx_mode(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int i, nfilter;
	u32 reg;

	netif_dbg(priv, hw, dev, "%s: %08X\n", __func__, dev->flags);

	/* Number of filters needed */
	nfilter = netdev_uc_count(dev) + netdev_mc_count(dev) + 2;

	/*
	 * Turn on promicuous mode for three scenarios
	 * 1. IFF_PROMISC flag is set
	 * 2. IFF_ALLMULTI flag is set
	 * 3. The number of filters needed exceeds the number filters
	 *    supported by the hardware.
	*/
	spin_lock(&priv->reg_lock);
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	if ((dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) ||
	    (nfilter > MAX_MDF_FILTER)) {
		reg |= CMD_PROMISC;
		bcmgenet_umac_writel(priv, reg, UMAC_CMD);
		spin_unlock(&priv->reg_lock);
		bcmgenet_umac_writel(priv, 0, UMAC_MDF_CTRL);
		return;
	} else {
		reg &= ~CMD_PROMISC;
		bcmgenet_umac_writel(priv, reg, UMAC_CMD);
		spin_unlock(&priv->reg_lock);
	}

	/* update MDF filter */
	i = 0;
	/* Broadcast */
	bcmgenet_set_mdf_addr(priv, dev->broadcast, &i);
	/* my own address.*/
	bcmgenet_set_mdf_addr(priv, dev->dev_addr, &i);

	/* Unicast */
	netdev_for_each_uc_addr(ha, dev)
		bcmgenet_set_mdf_addr(priv, ha->addr, &i);

	/* Multicast */
	netdev_for_each_mc_addr(ha, dev)
		bcmgenet_set_mdf_addr(priv, ha->addr, &i);

	/* Enable filters */
	reg = GENMASK(MAX_MDF_FILTER - 1, MAX_MDF_FILTER - nfilter);
	bcmgenet_umac_writel(priv, reg, UMAC_MDF_CTRL);
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

	eth_hw_addr_set(dev, addr->sa_data);

	return 0;
}

static void bcmgenet_get_stats64(struct net_device *dev,
				 struct rtnl_link_stats64 *stats)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_tx_stats64 *tx_stats;
	struct bcmgenet_rx_stats64 *rx_stats;
	u64 rx_length_errors, rx_over_errors;
	u64 rx_missed, rx_fragmented_errors;
	u64 rx_crc_errors, rx_frame_errors;
	u64 tx_errors, tx_dropped;
	u64 rx_errors, rx_dropped;
	u64 tx_bytes, tx_packets;
	u64 rx_bytes, rx_packets;
	unsigned int start;
	unsigned int q;
	u64 multicast;

	for (q = 0; q <= priv->hw_params->tx_queues; q++) {
		tx_stats = &priv->tx_rings[q].stats64;
		do {
			start = u64_stats_fetch_begin(&tx_stats->syncp);
			tx_bytes = u64_stats_read(&tx_stats->bytes);
			tx_packets = u64_stats_read(&tx_stats->packets);
			tx_errors = u64_stats_read(&tx_stats->errors);
			tx_dropped = u64_stats_read(&tx_stats->dropped);
		} while (u64_stats_fetch_retry(&tx_stats->syncp, start));

		stats->tx_bytes += tx_bytes;
		stats->tx_packets += tx_packets;
		stats->tx_errors += tx_errors;
		stats->tx_dropped += tx_dropped;
	}

	for (q = 0; q <= priv->hw_params->rx_queues; q++) {
		rx_stats = &priv->rx_rings[q].stats64;
		do {
			start = u64_stats_fetch_begin(&rx_stats->syncp);
			rx_bytes = u64_stats_read(&rx_stats->bytes);
			rx_packets = u64_stats_read(&rx_stats->packets);
			rx_errors = u64_stats_read(&rx_stats->errors);
			rx_dropped = u64_stats_read(&rx_stats->dropped);
			rx_missed = u64_stats_read(&rx_stats->missed);
			rx_length_errors = u64_stats_read(&rx_stats->length_errors);
			rx_over_errors = u64_stats_read(&rx_stats->over_errors);
			rx_crc_errors = u64_stats_read(&rx_stats->crc_errors);
			rx_frame_errors = u64_stats_read(&rx_stats->frame_errors);
			rx_fragmented_errors = u64_stats_read(&rx_stats->fragmented_errors);
			multicast = u64_stats_read(&rx_stats->multicast);
		} while (u64_stats_fetch_retry(&rx_stats->syncp, start));

		rx_errors += rx_length_errors;
		rx_errors += rx_crc_errors;
		rx_errors += rx_frame_errors;
		rx_errors += rx_fragmented_errors;

		stats->rx_bytes += rx_bytes;
		stats->rx_packets += rx_packets;
		stats->rx_errors += rx_errors;
		stats->rx_dropped += rx_dropped;
		stats->rx_missed_errors += rx_missed;
		stats->rx_length_errors += rx_length_errors;
		stats->rx_over_errors += rx_over_errors;
		stats->rx_crc_errors += rx_crc_errors;
		stats->rx_frame_errors += rx_frame_errors;
		stats->multicast += multicast;
	}
}

static int bcmgenet_change_carrier(struct net_device *dev, bool new_carrier)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	if (!dev->phydev || !phy_is_pseudo_fixed_link(dev->phydev) ||
	    priv->phy_interface != PHY_INTERFACE_MODE_MOCA)
		return -EOPNOTSUPP;

	if (new_carrier)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);

	return 0;
}

static const struct net_device_ops bcmgenet_netdev_ops = {
	.ndo_open		= bcmgenet_open,
	.ndo_stop		= bcmgenet_close,
	.ndo_start_xmit		= bcmgenet_xmit,
	.ndo_tx_timeout		= bcmgenet_timeout,
	.ndo_set_rx_mode	= bcmgenet_set_rx_mode,
	.ndo_set_mac_address	= bcmgenet_set_mac_addr,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_set_features	= bcmgenet_set_features,
	.ndo_get_stats64	= bcmgenet_get_stats64,
	.ndo_change_carrier	= bcmgenet_change_carrier,
};

/* GENET hardware parameters/characteristics */
static const struct bcmgenet_hw_params bcmgenet_hw_params_v1 = {
	.tx_queues = 0,
	.tx_bds_per_q = 0,
	.rx_queues = 0,
	.rx_bds_per_q = 0,
	.bp_in_en_shift = 16,
	.bp_in_mask = 0xffff,
	.hfb_filter_cnt = 16,
	.hfb_filter_size = 64,
	.qtag_mask = 0x1F,
	.hfb_offset = 0x1000,
	.hfb_reg_offset = GENET_RBUF_OFF + RBUF_HFB_CTRL_V1,
	.rdma_offset = 0x2000,
	.tdma_offset = 0x3000,
	.words_per_bd = 2,
};

static const struct bcmgenet_hw_params bcmgenet_hw_params_v2 = {
	.tx_queues = 4,
	.tx_bds_per_q = 32,
	.rx_queues = 0,
	.rx_bds_per_q = 0,
	.bp_in_en_shift = 16,
	.bp_in_mask = 0xffff,
	.hfb_filter_cnt = 16,
	.hfb_filter_size = 64,
	.qtag_mask = 0x1F,
	.tbuf_offset = 0x0600,
	.hfb_offset = 0x1000,
	.hfb_reg_offset = 0x2000,
	.rdma_offset = 0x3000,
	.tdma_offset = 0x4000,
	.words_per_bd = 2,
};

static const struct bcmgenet_hw_params bcmgenet_hw_params_v3 = {
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
};

static const struct bcmgenet_hw_params bcmgenet_hw_params_v4 = {
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
};

/* Infer hardware parameters from the detected GENET version */
static void bcmgenet_set_hw_params(struct bcmgenet_priv *priv)
{
	const struct bcmgenet_hw_params *params;
	u32 reg;
	u8 major;
	u16 gphy_rev;

	/* default to latest values */
	params = &bcmgenet_hw_params_v4;
	bcmgenet_dma_regs = bcmgenet_dma_regs_v3plus;
	genet_dma_ring_regs = genet_dma_ring_regs_v4;
	if (GENET_IS_V3(priv)) {
		params = &bcmgenet_hw_params_v3;
		bcmgenet_dma_regs = bcmgenet_dma_regs_v3plus;
		genet_dma_ring_regs = genet_dma_ring_regs_v123;
	} else if (GENET_IS_V2(priv)) {
		params = &bcmgenet_hw_params_v2;
		bcmgenet_dma_regs = bcmgenet_dma_regs_v2;
		genet_dma_ring_regs = genet_dma_ring_regs_v123;
	} else if (GENET_IS_V1(priv)) {
		params = &bcmgenet_hw_params_v1;
		bcmgenet_dma_regs = bcmgenet_dma_regs_v1;
		genet_dma_ring_regs = genet_dma_ring_regs_v123;
	}
	priv->hw_params = params;

	/* Read GENET HW version */
	reg = bcmgenet_sys_readl(priv, SYS_REV_CTRL);
	major = (reg >> 24 & 0x0f);
	if (major == 6 || major == 7)
		major = 5;
	else if (major == 5)
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

	if (GENET_IS_V5(priv)) {
		/* The EPHY revision should come from the MDIO registers of
		 * the PHY not from GENET.
		 */
		if (gphy_rev != 0) {
			pr_warn("GENET is reporting EPHY revision: 0x%04x\n",
				gphy_rev);
		}
	/* This is reserved so should require special treatment */
	} else if (gphy_rev == 0 || gphy_rev == 0x01ff) {
		pr_warn("Invalid GPHY revision detected: 0x%04x\n", gphy_rev);
		return;
	/* This is the good old scheme, just GPHY major, no minor nor patch */
	} else if ((gphy_rev & 0xf0) != 0) {
		priv->gphy_rev = gphy_rev << 8;
	/* This is the new scheme, GPHY major rolls over with 0x10 = rev G0 */
	} else if ((gphy_rev & 0xff00) != 0) {
		priv->gphy_rev = gphy_rev;
	}

#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (!bcmgenet_has_40bits(priv))
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

struct bcmgenet_plat_data {
	enum bcmgenet_version version;
	u32 dma_max_burst_length;
	u32 flags;
};

static const struct bcmgenet_plat_data v1_plat_data = {
	.version = GENET_V1,
	.dma_max_burst_length = DMA_MAX_BURST_LENGTH,
};

static const struct bcmgenet_plat_data v2_plat_data = {
	.version = GENET_V2,
	.dma_max_burst_length = DMA_MAX_BURST_LENGTH,
	.flags = GENET_HAS_EXT,
};

static const struct bcmgenet_plat_data v3_plat_data = {
	.version = GENET_V3,
	.dma_max_burst_length = DMA_MAX_BURST_LENGTH,
	.flags = GENET_HAS_EXT | GENET_HAS_MDIO_INTR |
		 GENET_HAS_MOCA_LINK_DET,
};

static const struct bcmgenet_plat_data v4_plat_data = {
	.version = GENET_V4,
	.dma_max_burst_length = DMA_MAX_BURST_LENGTH,
	.flags = GENET_HAS_40BITS | GENET_HAS_EXT |
		 GENET_HAS_MDIO_INTR | GENET_HAS_MOCA_LINK_DET,
};

static const struct bcmgenet_plat_data v5_plat_data = {
	.version = GENET_V5,
	.dma_max_burst_length = DMA_MAX_BURST_LENGTH,
	.flags = GENET_HAS_40BITS | GENET_HAS_EXT |
		 GENET_HAS_MDIO_INTR | GENET_HAS_MOCA_LINK_DET,
};

static const struct bcmgenet_plat_data bcm2711_plat_data = {
	.version = GENET_V5,
	.dma_max_burst_length = 0x08,
	.flags = GENET_HAS_40BITS | GENET_HAS_EXT |
		 GENET_HAS_MDIO_INTR | GENET_HAS_MOCA_LINK_DET,
};

static const struct bcmgenet_plat_data bcm7712_plat_data = {
	.version = GENET_V5,
	.dma_max_burst_length = DMA_MAX_BURST_LENGTH,
	.flags = GENET_HAS_40BITS | GENET_HAS_EXT |
		 GENET_HAS_MDIO_INTR | GENET_HAS_MOCA_LINK_DET |
		 GENET_HAS_EPHY_16NM,
};

static const struct of_device_id bcmgenet_match[] = {
	{ .compatible = "brcm,genet-v1", .data = &v1_plat_data },
	{ .compatible = "brcm,genet-v2", .data = &v2_plat_data },
	{ .compatible = "brcm,genet-v3", .data = &v3_plat_data },
	{ .compatible = "brcm,genet-v4", .data = &v4_plat_data },
	{ .compatible = "brcm,genet-v5", .data = &v5_plat_data },
	{ .compatible = "brcm,bcm2711-genet-v5", .data = &bcm2711_plat_data },
	{ .compatible = "brcm,bcm7712-genet-v5", .data = &bcm7712_plat_data },
	{ },
};
MODULE_DEVICE_TABLE(of, bcmgenet_match);

static int bcmgenet_probe(struct platform_device *pdev)
{
	struct bcmgenet_platform_data *pd = pdev->dev.platform_data;
	const struct bcmgenet_plat_data *pdata;
	struct bcmgenet_priv *priv;
	struct net_device *dev;
	unsigned int i;
	int err = -EIO;

	/* Up to GENET_MAX_MQ_CNT + 1 TX queues and RX queues */
	dev = alloc_etherdev_mqs(sizeof(*priv), GENET_MAX_MQ_CNT + 1,
				 GENET_MAX_MQ_CNT + 1);
	if (!dev) {
		dev_err(&pdev->dev, "can't allocate net device\n");
		return -ENOMEM;
	}

	priv = netdev_priv(dev);
	priv->irq0 = platform_get_irq(pdev, 0);
	if (priv->irq0 < 0) {
		err = priv->irq0;
		goto err;
	}
	priv->irq1 = platform_get_irq(pdev, 1);
	if (priv->irq1 < 0) {
		err = priv->irq1;
		goto err;
	}
	priv->wol_irq = platform_get_irq_optional(pdev, 2);
	if (priv->wol_irq == -EPROBE_DEFER) {
		err = priv->wol_irq;
		goto err;
	}

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		err = PTR_ERR(priv->base);
		goto err;
	}

	spin_lock_init(&priv->reg_lock);
	spin_lock_init(&priv->lock);

	/* Set default pause parameters */
	priv->autoneg_pause = 1;
	priv->tx_pause = 1;
	priv->rx_pause = 1;

	SET_NETDEV_DEV(dev, &pdev->dev);
	dev_set_drvdata(&pdev->dev, dev);
	dev->watchdog_timeo = 2 * HZ;
	dev->ethtool_ops = &bcmgenet_ethtool_ops;
	dev->netdev_ops = &bcmgenet_netdev_ops;

	priv->msg_enable = netif_msg_init(-1, GENET_MSG_DEFAULT);

	/* Set default features */
	dev->features |= NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_HW_CSUM |
			 NETIF_F_RXCSUM;
	dev->hw_features |= dev->features;
	dev->vlan_features |= dev->features;

	netdev_sw_irq_coalesce_default_on(dev);

	/* Request the WOL interrupt and advertise suspend if available */
	priv->wol_irq_disabled = true;
	if (priv->wol_irq > 0) {
		err = devm_request_irq(&pdev->dev, priv->wol_irq,
				       bcmgenet_wol_isr, 0, dev->name, priv);
		if (!err)
			device_set_wakeup_capable(&pdev->dev, 1);
	}

	/* Set the needed headroom to account for any possible
	 * features enabling/disabling at runtime
	 */
	dev->needed_headroom += 64;

	priv->dev = dev;
	priv->pdev = pdev;

	pdata = device_get_match_data(&pdev->dev);
	if (pdata) {
		priv->version = pdata->version;
		priv->dma_max_burst_length = pdata->dma_max_burst_length;
		priv->flags = pdata->flags;
	} else {
		priv->version = pd->genet_version;
		priv->dma_max_burst_length = DMA_MAX_BURST_LENGTH;
	}

	priv->clk = devm_clk_get_optional(&priv->pdev->dev, "enet");
	if (IS_ERR(priv->clk)) {
		dev_dbg(&priv->pdev->dev, "failed to get enet clock\n");
		err = PTR_ERR(priv->clk);
		goto err;
	}

	err = clk_prepare_enable(priv->clk);
	if (err)
		goto err;

	bcmgenet_set_hw_params(priv);

	err = -EIO;
	if (bcmgenet_has_40bits(priv))
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		goto err_clk_disable;

	/* Mii wait queue */
	init_waitqueue_head(&priv->wq);
	/* Always use RX_BUF_LENGTH (2KB) buffer for all chips */
	priv->rx_buf_len = RX_BUF_LENGTH;
	INIT_WORK(&priv->bcmgenet_irq_work, bcmgenet_irq_task);

	priv->clk_wol = devm_clk_get_optional(&priv->pdev->dev, "enet-wol");
	if (IS_ERR(priv->clk_wol)) {
		dev_dbg(&priv->pdev->dev, "failed to get enet-wol clock\n");
		err = PTR_ERR(priv->clk_wol);
		goto err_clk_disable;
	}

	priv->clk_eee = devm_clk_get_optional(&priv->pdev->dev, "enet-eee");
	if (IS_ERR(priv->clk_eee)) {
		dev_dbg(&priv->pdev->dev, "failed to get enet-eee clock\n");
		err = PTR_ERR(priv->clk_eee);
		goto err_clk_disable;
	}

	/* If this is an internal GPHY, power it on now, before UniMAC is
	 * brought out of reset as absolutely no UniMAC activity is allowed
	 */
	if (device_get_phy_mode(&pdev->dev) == PHY_INTERFACE_MODE_INTERNAL)
		bcmgenet_power_up(priv, GENET_POWER_PASSIVE);

	if (pd && !IS_ERR_OR_NULL(pd->mac_address))
		eth_hw_addr_set(dev, pd->mac_address);
	else
		if (device_get_ethdev_address(&pdev->dev, dev))
			if (has_acpi_companion(&pdev->dev)) {
				u8 addr[ETH_ALEN];

				bcmgenet_get_hw_addr(priv, addr);
				eth_hw_addr_set(dev, addr);
			}

	if (!is_valid_ether_addr(dev->dev_addr)) {
		dev_warn(&pdev->dev, "using random Ethernet MAC\n");
		eth_hw_addr_random(dev);
	}

	reset_umac(priv);

	err = bcmgenet_mii_init(dev);
	if (err)
		goto err_clk_disable;

	/* setup number of real queues + 1 */
	netif_set_real_num_tx_queues(priv->dev, priv->hw_params->tx_queues + 1);
	netif_set_real_num_rx_queues(priv->dev, priv->hw_params->rx_queues + 1);

	/* Set default coalescing parameters */
	for (i = 0; i <= priv->hw_params->rx_queues; i++)
		priv->rx_rings[i].rx_max_coalesced_frames = 1;

	/* Initialize u64 stats seq counter for 32bit machines */
	for (i = 0; i <= priv->hw_params->rx_queues; i++)
		u64_stats_init(&priv->rx_rings[i].stats64.syncp);
	for (i = 0; i <= priv->hw_params->tx_queues; i++)
		u64_stats_init(&priv->tx_rings[i].stats64.syncp);

	/* libphy will determine the link state */
	netif_carrier_off(dev);

	/* Turn off the main clock, WOL clock is handled separately */
	clk_disable_unprepare(priv->clk);

	err = register_netdev(dev);
	if (err) {
		bcmgenet_mii_exit(dev);
		goto err;
	}

	return err;

err_clk_disable:
	clk_disable_unprepare(priv->clk);
err:
	free_netdev(dev);
	return err;
}

static void bcmgenet_remove(struct platform_device *pdev)
{
	struct bcmgenet_priv *priv = dev_to_priv(&pdev->dev);

	dev_set_drvdata(&pdev->dev, NULL);
	unregister_netdev(priv->dev);
	bcmgenet_mii_exit(priv->dev);
	free_netdev(priv->dev);
}

static void bcmgenet_shutdown(struct platform_device *pdev)
{
	bcmgenet_remove(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int bcmgenet_resume_noirq(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;
	u32 reg;

	if (!netif_running(dev))
		return 0;

	/* Turn on the clock */
	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	if (device_may_wakeup(d) && priv->wolopts) {
		/* Account for Wake-on-LAN events and clear those events
		 * (Some devices need more time between enabling the clocks
		 *  and the interrupt register reflecting the wake event so
		 *  read the register twice)
		 */
		reg = bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_STAT);
		reg = bcmgenet_intrl2_0_readl(priv, INTRL2_CPU_STAT);
		if (reg & UMAC_IRQ_WAKE_EVENT)
			pm_wakeup_event(&priv->pdev->dev, 0);

		/* From WOL-enabled suspend, switch to regular clock */
		if (!bcmgenet_power_up(priv, GENET_POWER_WOL_MAGIC))
			return 0;

		/* Failed so fall through to reset MAC */
	}

	/* If this is an internal GPHY, power it back on now, before UniMAC is
	 * brought out of reset as absolutely no UniMAC activity is allowed
	 */
	if (priv->internal_phy)
		bcmgenet_power_up(priv, GENET_POWER_PASSIVE);

	/* take MAC out of reset */
	bcmgenet_umac_reset(priv);

	return 0;
}

static int bcmgenet_resume(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rxnfc_rule *rule;
	int ret;
	u32 reg;

	if (!netif_running(dev))
		return 0;

	if (device_may_wakeup(d) && priv->wolopts) {
		reg = bcmgenet_umac_readl(priv, UMAC_CMD);
		if (reg & CMD_RX_EN) {
			/* Successfully exited WoL, just resume data flows */
			list_for_each_entry(rule, &priv->rxnfc_list, list)
				if (rule->state == BCMGENET_RXNFC_STATE_ENABLED)
					bcmgenet_hfb_enable_filter(priv,
							rule->fs.location + 1);
			bcmgenet_hfb_enable_filter(priv, 0);
			bcmgenet_set_rx_mode(dev);
			bcmgenet_enable_rx_napi(priv);

			/* Reinitialize Tx flows */
			bcmgenet_tdma_disable(priv);
			bcmgenet_init_tx_queues(priv->dev);
			reg = bcmgenet_tdma_readl(priv, DMA_CTRL);
			reg |= DMA_EN;
			bcmgenet_tdma_writel(priv, reg, DMA_CTRL);
			bcmgenet_enable_tx_napi(priv);

			bcmgenet_link_intr_enable(priv);
			phy_start_machine(dev->phydev);

			netif_device_attach(dev);
			enable_irq(priv->irq1);
			return 0;
		}
		/* MAC was reset so complete bcmgenet_netif_stop() */
		umac_enable_set(priv, CMD_RX_EN | CMD_TX_EN, false);
		bcmgenet_rdma_disable(priv);
		bcmgenet_intr_disable(priv);
		bcmgenet_fini_dma(priv);
		enable_irq(priv->irq1);
	}

	init_umac(priv);

	phy_init_hw(dev->phydev);

	/* Speed settings must be restored */
	genphy_config_aneg(dev->phydev);
	bcmgenet_mii_config(priv->dev, false);

	/* Restore enabled features */
	bcmgenet_set_features(dev, dev->features);

	bcmgenet_set_hw_addr(priv, dev->dev_addr);

	/* Restore hardware filters */
	bcmgenet_hfb_clear(priv);
	list_for_each_entry(rule, &priv->rxnfc_list, list)
		if (rule->state != BCMGENET_RXNFC_STATE_UNUSED)
			bcmgenet_hfb_create_rxnfc_filter(priv, rule);

	/* Reinitialize TDMA and RDMA and SW housekeeping */
	ret = bcmgenet_init_dma(priv, false);
	if (ret) {
		netdev_err(dev, "failed to initialize DMA\n");
		goto out_clk_disable;
	}

	if (!device_may_wakeup(d))
		phy_resume(dev->phydev);

	bcmgenet_netif_start(dev);

	netif_device_attach(dev);

	return 0;

out_clk_disable:
	if (priv->internal_phy)
		bcmgenet_power_down(priv, GENET_POWER_PASSIVE);
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int bcmgenet_suspend(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct bcmgenet_rxnfc_rule *rule;
	u32 reg, hfb_enable = 0;

	if (!netif_running(dev))
		return 0;

	netif_device_detach(dev);

	if (device_may_wakeup(d) && priv->wolopts) {
		netif_tx_disable(dev);

		/* Suspend non-wake Rx data flows */
		if (priv->wolopts & WAKE_FILTER)
			list_for_each_entry(rule, &priv->rxnfc_list, list)
				if (rule->fs.ring_cookie == RX_CLS_FLOW_WAKE &&
				    rule->state == BCMGENET_RXNFC_STATE_ENABLED)
					hfb_enable |= 1 << rule->fs.location;
		reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
		if (GENET_IS_V1(priv) || GENET_IS_V2(priv)) {
			reg &= ~RBUF_HFB_FILTER_EN_MASK;
			reg |= hfb_enable << (RBUF_HFB_FILTER_EN_SHIFT + 1);
		} else {
			bcmgenet_hfb_reg_writel(priv, hfb_enable << 1,
						HFB_FLT_ENABLE_V3PLUS + 4);
		}
		if (!hfb_enable)
			reg &= ~RBUF_HFB_EN;
		bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);

		/* Clear any old filter matches so only new matches wake */
		bcmgenet_intrl2_0_writel(priv, 0xFFFFFFFF, INTRL2_CPU_MASK_SET);
		bcmgenet_intrl2_0_writel(priv, 0xFFFFFFFF, INTRL2_CPU_CLEAR);

		if (-ETIMEDOUT == bcmgenet_tdma_disable(priv))
			netdev_warn(priv->dev,
				    "Timed out while disabling TX DMA\n");

		bcmgenet_disable_tx_napi(priv);
		bcmgenet_disable_rx_napi(priv);
		disable_irq(priv->irq1);
		bcmgenet_tx_reclaim_all(dev);
		bcmgenet_fini_tx_napi(priv);
	} else {
		/* Teardown the interface */
		bcmgenet_netif_stop(dev, true);
	}

	return 0;
}

static int bcmgenet_suspend_noirq(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret = 0;

	if (!netif_running(dev))
		return 0;

	/* Prepare the device for Wake-on-LAN and switch to the slow clock */
	if (device_may_wakeup(d) && priv->wolopts)
		ret = bcmgenet_power_down(priv, GENET_POWER_WOL_MAGIC);
	else if (priv->internal_phy)
		ret = bcmgenet_power_down(priv, GENET_POWER_PASSIVE);

	/* Let the framework handle resumption and leave the clocks on */
	if (ret)
		return ret;

	/* Turn off the clocks */
	clk_disable_unprepare(priv->clk);

	return 0;
}
#else
#define bcmgenet_suspend	NULL
#define bcmgenet_suspend_noirq	NULL
#define bcmgenet_resume		NULL
#define bcmgenet_resume_noirq	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops bcmgenet_pm_ops = {
	.suspend	= bcmgenet_suspend,
	.suspend_noirq	= bcmgenet_suspend_noirq,
	.resume		= bcmgenet_resume,
	.resume_noirq	= bcmgenet_resume_noirq,
};

static const struct acpi_device_id genet_acpi_match[] = {
	{ "BCM6E4E", (kernel_ulong_t)&bcm2711_plat_data },
	{ },
};
MODULE_DEVICE_TABLE(acpi, genet_acpi_match);

static struct platform_driver bcmgenet_driver = {
	.probe	= bcmgenet_probe,
	.remove = bcmgenet_remove,
	.shutdown = bcmgenet_shutdown,
	.driver	= {
		.name	= "bcmgenet",
		.of_match_table = bcmgenet_match,
		.pm	= &bcmgenet_pm_ops,
		.acpi_match_table = genet_acpi_match,
	},
};
module_platform_driver(bcmgenet_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom GENET Ethernet controller driver");
MODULE_ALIAS("platform:bcmgenet");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: mdio-bcm-unimac");
