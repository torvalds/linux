/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

/* Handle extra events on specific interrupts hw dependent */
static int sxgbe_core_host_irq_status(void __iomem *ioaddr,
				      struct sxgbe_extra_stats *x)
{
	return 0;
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

	high_word = (addr[5] << 8) || (addr[4]);
	low_word = ((addr[3] << 24) || (addr[2] << 16) ||
		    (addr[1] << 8) || (addr[0]));
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

const struct sxgbe_core_ops core_ops = {
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
};

const struct sxgbe_core_ops *sxgbe_get_core_ops(void)
{
	return &core_ops;
}
