// SPDX-License-Identifier: GPL-2.0-only
/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#include "sxgbe_common.h"
#include "sxgbe_reg.h"

/* MAC core initialization */
static void sxgbe_core_init(void __iomem *ioaddr)
{
	u32 regval;

	/* TX configuration */
	regval = readl(ioaddr + SXGBE_CORE_TX_CONFIG_REG);
	/* Other configurable parameters IFP, IPG, ISR, ISM
	 * needs to be set if needed
	 */
	regval |= SXGBE_TX_JABBER_DISABLE;
	writel(regval, ioaddr + SXGBE_CORE_TX_CONFIG_REG);

	/* RX configuration */
	regval = readl(ioaddr + SXGBE_CORE_RX_CONFIG_REG);
	/* Other configurable parameters CST, SPEN, USP, GPSLCE
	 * WD, LM, S2KP, HDSMS, GPSL, ELEN, ARPEN needs to be
	 * set if needed
	 */
	regval |= SXGBE_RX_JUMBPKT_ENABLE | SXGBE_RX_ACS_ENABLE;
	writel(regval, ioaddr + SXGBE_CORE_RX_CONFIG_REG);
}

/* Dump MAC registers */
static void sxgbe_core_dump_regs(void __iomem *ioaddr)
{
}

static int sxgbe_get_lpi_status(void __iomem *ioaddr, const u32 irq_status)
{
	int status = 0;
	int lpi_status;

	/* Reading this register shall clear all the LPI status bits */
	lpi_status = readl(ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);

	if (lpi_status & LPI_CTRL_STATUS_TLPIEN)
		status |= TX_ENTRY_LPI_MODE;
	if (lpi_status & LPI_CTRL_STATUS_TLPIEX)
		status |= TX_EXIT_LPI_MODE;
	if (lpi_status & LPI_CTRL_STATUS_RLPIEN)
		status |= RX_ENTRY_LPI_MODE;
	if (lpi_status & LPI_CTRL_STATUS_RLPIEX)
		status |= RX_EXIT_LPI_MODE;

	return status;
}

/* Handle extra events on specific interrupts hw dependent */
static int sxgbe_core_host_irq_status(void __iomem *ioaddr,
				      struct sxgbe_extra_stats *x)
{
	int irq_status, status = 0;

	irq_status = readl(ioaddr + SXGBE_CORE_INT_STATUS_REG);

	if (unlikely(irq_status & LPI_INT_STATUS))
		status |= sxgbe_get_lpi_status(ioaddr, irq_status);

	return status;
}

/* Set power management mode (e.g. magic frame) */
static void sxgbe_core_pmt(void __iomem *ioaddr, unsigned long mode)
{
}

/* Set/Get Unicast MAC addresses */
static void sxgbe_core_set_umac_addr(void __iomem *ioaddr, unsigned char *addr,
				     unsigned int reg_n)
{
	u32 high_word, low_word;

	high_word = (addr[5] << 8) | (addr[4]);
	low_word = (addr[3] << 24) | (addr[2] << 16) |
		   (addr[1] << 8) | (addr[0]);
	writel(high_word, ioaddr + SXGBE_CORE_ADD_HIGHOFFSET(reg_n));
	writel(low_word, ioaddr + SXGBE_CORE_ADD_LOWOFFSET(reg_n));
}

static void sxgbe_core_get_umac_addr(void __iomem *ioaddr, unsigned char *addr,
				     unsigned int reg_n)
{
	u32 high_word, low_word;

	high_word = readl(ioaddr + SXGBE_CORE_ADD_HIGHOFFSET(reg_n));
	low_word = readl(ioaddr + SXGBE_CORE_ADD_LOWOFFSET(reg_n));

	/* extract and assign address */
	addr[5] = (high_word & 0x0000FF00) >> 8;
	addr[4] = (high_word & 0x000000FF);
	addr[3] = (low_word & 0xFF000000) >> 24;
	addr[2] = (low_word & 0x00FF0000) >> 16;
	addr[1] = (low_word & 0x0000FF00) >> 8;
	addr[0] = (low_word & 0x000000FF);
}

static void sxgbe_enable_tx(void __iomem *ioaddr, bool enable)
{
	u32 tx_config;

	tx_config = readl(ioaddr + SXGBE_CORE_TX_CONFIG_REG);
	tx_config &= ~SXGBE_TX_ENABLE;

	if (enable)
		tx_config |= SXGBE_TX_ENABLE;
	writel(tx_config, ioaddr + SXGBE_CORE_TX_CONFIG_REG);
}

static void sxgbe_enable_rx(void __iomem *ioaddr, bool enable)
{
	u32 rx_config;

	rx_config = readl(ioaddr + SXGBE_CORE_RX_CONFIG_REG);
	rx_config &= ~SXGBE_RX_ENABLE;

	if (enable)
		rx_config |= SXGBE_RX_ENABLE;
	writel(rx_config, ioaddr + SXGBE_CORE_RX_CONFIG_REG);
}

static int sxgbe_get_controller_version(void __iomem *ioaddr)
{
	return readl(ioaddr + SXGBE_CORE_VERSION_REG);
}

/* If supported then get the optional core features */
static unsigned int sxgbe_get_hw_feature(void __iomem *ioaddr,
					 unsigned char feature_index)
{
	return readl(ioaddr + (SXGBE_CORE_HW_FEA_REG(feature_index)));
}

static void sxgbe_core_set_speed(void __iomem *ioaddr, unsigned char speed)
{
	u32 tx_cfg = readl(ioaddr + SXGBE_CORE_TX_CONFIG_REG);

	/* clear the speed bits */
	tx_cfg &= ~0x60000000;
	tx_cfg |= (speed << SXGBE_SPEED_LSHIFT);

	/* set the speed */
	writel(tx_cfg, ioaddr + SXGBE_CORE_TX_CONFIG_REG);
}

static void sxgbe_core_enable_rxqueue(void __iomem *ioaddr, int queue_num)
{
	u32 reg_val;

	reg_val = readl(ioaddr + SXGBE_CORE_RX_CTL0_REG);
	reg_val &= ~(SXGBE_CORE_RXQ_ENABLE_MASK << queue_num);
	reg_val |= SXGBE_CORE_RXQ_ENABLE;
	writel(reg_val, ioaddr + SXGBE_CORE_RX_CTL0_REG);
}

static void sxgbe_core_disable_rxqueue(void __iomem *ioaddr, int queue_num)
{
	u32 reg_val;

	reg_val = readl(ioaddr + SXGBE_CORE_RX_CTL0_REG);
	reg_val &= ~(SXGBE_CORE_RXQ_ENABLE_MASK << queue_num);
	reg_val |= SXGBE_CORE_RXQ_DISABLE;
	writel(reg_val, ioaddr + SXGBE_CORE_RX_CTL0_REG);
}

static void  sxgbe_set_eee_mode(void __iomem *ioaddr)
{
	u32 ctrl;

	/* Enable the LPI mode for transmit path with Tx automate bit set.
	 * When Tx Automate bit is set, MAC internally handles the entry
	 * to LPI mode after all outstanding and pending packets are
	 * transmitted.
	 */
	ctrl = readl(ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);
	ctrl |= LPI_CTRL_STATUS_LPIEN | LPI_CTRL_STATUS_TXA;
	writel(ctrl, ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);
}

static void  sxgbe_reset_eee_mode(void __iomem *ioaddr)
{
	u32 ctrl;

	ctrl = readl(ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);
	ctrl &= ~(LPI_CTRL_STATUS_LPIEN | LPI_CTRL_STATUS_TXA);
	writel(ctrl, ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);
}

static void  sxgbe_set_eee_pls(void __iomem *ioaddr, const int link)
{
	u32 ctrl;

	ctrl = readl(ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);

	/* If the PHY link status is UP then set PLS */
	if (link)
		ctrl |= LPI_CTRL_STATUS_PLS;
	else
		ctrl &= ~LPI_CTRL_STATUS_PLS;

	writel(ctrl, ioaddr + SXGBE_CORE_LPI_CTRL_STATUS);
}

static void  sxgbe_set_eee_timer(void __iomem *ioaddr,
				 const int ls, const int tw)
{
	int value = ((tw & 0xffff)) | ((ls & 0x7ff) << 16);

	/* Program the timers in the LPI timer control register:
	 * LS: minimum time (ms) for which the link
	 *  status from PHY should be ok before transmitting
	 *  the LPI pattern.
	 * TW: minimum time (us) for which the core waits
	 *  after it has stopped transmitting the LPI pattern.
	 */
	writel(value, ioaddr + SXGBE_CORE_LPI_TIMER_CTRL);
}

static void sxgbe_enable_rx_csum(void __iomem *ioaddr)
{
	u32 ctrl;

	ctrl = readl(ioaddr + SXGBE_CORE_RX_CONFIG_REG);
	ctrl |= SXGBE_RX_CSUMOFFLOAD_ENABLE;
	writel(ctrl, ioaddr + SXGBE_CORE_RX_CONFIG_REG);
}

static void sxgbe_disable_rx_csum(void __iomem *ioaddr)
{
	u32 ctrl;

	ctrl = readl(ioaddr + SXGBE_CORE_RX_CONFIG_REG);
	ctrl &= ~SXGBE_RX_CSUMOFFLOAD_ENABLE;
	writel(ctrl, ioaddr + SXGBE_CORE_RX_CONFIG_REG);
}

static const struct sxgbe_core_ops core_ops = {
	.core_init		= sxgbe_core_init,
	.dump_regs		= sxgbe_core_dump_regs,
	.host_irq_status	= sxgbe_core_host_irq_status,
	.pmt			= sxgbe_core_pmt,
	.set_umac_addr		= sxgbe_core_set_umac_addr,
	.get_umac_addr		= sxgbe_core_get_umac_addr,
	.enable_rx		= sxgbe_enable_rx,
	.enable_tx		= sxgbe_enable_tx,
	.get_controller_version	= sxgbe_get_controller_version,
	.get_hw_feature		= sxgbe_get_hw_feature,
	.set_speed		= sxgbe_core_set_speed,
	.set_eee_mode		= sxgbe_set_eee_mode,
	.reset_eee_mode		= sxgbe_reset_eee_mode,
	.set_eee_timer		= sxgbe_set_eee_timer,
	.set_eee_pls		= sxgbe_set_eee_pls,
	.enable_rx_csum		= sxgbe_enable_rx_csum,
	.disable_rx_csum	= sxgbe_disable_rx_csum,
	.enable_rxqueue		= sxgbe_core_enable_rxqueue,
	.disable_rxqueue	= sxgbe_core_disable_rxqueue,
};

const struct sxgbe_core_ops *sxgbe_get_core_ops(void)
{
	return &core_ops;
}
