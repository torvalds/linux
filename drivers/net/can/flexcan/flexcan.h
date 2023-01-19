/* SPDX-License-Identifier: GPL-2.0
 * flexcan.c - FLEXCAN CAN controller driver
 *
 * Copyright (c) 2005-2006 Varma Electronics Oy
 * Copyright (c) 2009 Sascha Hauer, Pengutronix
 * Copyright (c) 2010-2017 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 * Copyright (c) 2014 David Jander, Protonic Holland
 * Copyright (C) 2022 Amarula Solutions, Dario Binacchi <dario.binacchi@amarulasolutions.com>
 *
 * Based on code originally by Andrey Volkov <avolkov@varma-el.com>
 *
 */

#ifndef _FLEXCAN_H
#define _FLEXCAN_H

#include <linux/can/rx-offload.h>

/* FLEXCAN hardware feature flags
 *
 * Below is some version info we got:
 *    SOC   Version   IP-Version  Glitch- [TR]WRN_INT IRQ Err Memory err RTR rece-   FD Mode     MB
 *                                Filter? connected?  Passive detection  ption in MB Supported?
 * MCF5441X FlexCAN2  ?               no       yes        no       no        no           no     16
 *    MX25  FlexCAN2  03.00.00.00     no        no        no       no        no           no     64
 *    MX28  FlexCAN2  03.00.04.00    yes       yes        no       no        no           no     64
 *    MX35  FlexCAN2  03.00.00.00     no        no        no       no        no           no     64
 *    MX53  FlexCAN2  03.00.00.00    yes        no        no       no        no           no     64
 *    MX6s  FlexCAN3  10.00.12.00    yes       yes        no       no       yes           no     64
 *    MX8QM FlexCAN3  03.00.23.00    yes       yes        no       no       yes          yes     64
 *    MX8MP FlexCAN3  03.00.17.01    yes       yes        no      yes       yes          yes     64
 *    VF610 FlexCAN3  ?               no       yes        no      yes       yes?          no     64
 *  LS1021A FlexCAN2  03.00.04.00     no       yes        no       no       yes           no     64
 *  LX2160A FlexCAN3  03.00.23.00     no       yes        no      yes       yes          yes     64
 *
 * Some SOCs do not have the RX_WARN & TX_WARN interrupt line connected.
 */

/* [TR]WRN_INT not connected */
#define FLEXCAN_QUIRK_BROKEN_WERR_STATE BIT(1)
 /* Disable RX FIFO Global mask */
#define FLEXCAN_QUIRK_DISABLE_RXFG BIT(2)
/* Enable EACEN and RRS bit in ctrl2 */
#define FLEXCAN_QUIRK_ENABLE_EACEN_RRS  BIT(3)
/* Disable non-correctable errors interrupt and freeze mode */
#define FLEXCAN_QUIRK_DISABLE_MECR BIT(4)
/* Use mailboxes (not FIFO) for RX path */
#define FLEXCAN_QUIRK_USE_RX_MAILBOX BIT(5)
/* No interrupt for error passive */
#define FLEXCAN_QUIRK_BROKEN_PERR_STATE BIT(6)
/* default to BE register access */
#define FLEXCAN_QUIRK_DEFAULT_BIG_ENDIAN BIT(7)
/* Setup stop mode with GPR to support wakeup */
#define FLEXCAN_QUIRK_SETUP_STOP_MODE_GPR BIT(8)
/* Support CAN-FD mode */
#define FLEXCAN_QUIRK_SUPPORT_FD BIT(9)
/* support memory detection and correction */
#define FLEXCAN_QUIRK_SUPPORT_ECC BIT(10)
/* Setup stop mode with SCU firmware to support wakeup */
#define FLEXCAN_QUIRK_SETUP_STOP_MODE_SCFW BIT(11)
/* Setup 3 separate interrupts, main, boff and err */
#define FLEXCAN_QUIRK_NR_IRQ_3 BIT(12)
/* Setup 16 mailboxes */
#define FLEXCAN_QUIRK_NR_MB_16 BIT(13)
/* Device supports RX via mailboxes */
#define FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX BIT(14)
/* Device supports RTR reception via mailboxes */
#define FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX_RTR BIT(15)
/* Device supports RX via FIFO */
#define FLEXCAN_QUIRK_SUPPORT_RX_FIFO BIT(16)
/* auto enter stop mode to support wakeup */
#define FLEXCAN_QUIRK_AUTO_STOP_MODE BIT(17)

struct flexcan_devtype_data {
	u32 quirks;		/* quirks needed for different IP cores */
};

struct flexcan_stop_mode {
	struct regmap *gpr;
	u8 req_gpr;
	u8 req_bit;
};

struct flexcan_priv {
	struct can_priv can;
	struct can_rx_offload offload;
	struct device *dev;

	struct flexcan_regs __iomem *regs;
	struct flexcan_mb __iomem *tx_mb;
	struct flexcan_mb __iomem *tx_mb_reserved;
	u8 tx_mb_idx;
	u8 mb_count;
	u8 mb_size;
	u8 clk_src;	/* clock source of CAN Protocol Engine */
	u8 scu_idx;

	u64 rx_mask;
	u64 tx_mask;
	u32 reg_ctrl_default;

	struct clk *clk_ipg;
	struct clk *clk_per;
	struct flexcan_devtype_data devtype_data;
	struct regulator *reg_xceiver;
	struct flexcan_stop_mode stm;

	int irq_boff;
	int irq_err;

	/* IPC handle when setup stop mode by System Controller firmware(scfw) */
	struct imx_sc_ipc *sc_ipc_handle;

	/* Read and Write APIs */
	u32 (*read)(void __iomem *addr);
	void (*write)(u32 val, void __iomem *addr);
};

extern const struct ethtool_ops flexcan_ethtool_ops;

static inline bool
flexcan_supports_rx_mailbox(const struct flexcan_priv *priv)
{
	const u32 quirks = priv->devtype_data.quirks;

	return quirks & FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX;
}

static inline bool
flexcan_supports_rx_mailbox_rtr(const struct flexcan_priv *priv)
{
	const u32 quirks = priv->devtype_data.quirks;

	return (quirks & (FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX |
			  FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX_RTR)) ==
		(FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX |
		 FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX_RTR);
}

static inline bool
flexcan_supports_rx_fifo(const struct flexcan_priv *priv)
{
	const u32 quirks = priv->devtype_data.quirks;

	return quirks & FLEXCAN_QUIRK_SUPPORT_RX_FIFO;
}

static inline bool
flexcan_active_rx_rtr(const struct flexcan_priv *priv)
{
	const u32 quirks = priv->devtype_data.quirks;

	if (quirks & FLEXCAN_QUIRK_USE_RX_MAILBOX) {
		if (quirks & FLEXCAN_QUIRK_SUPPORT_RX_MAILBOX_RTR)
			return true;
	} else {
		/*  RX-FIFO is always RTR capable */
		return true;
	}

	return false;
}


#endif /* _FLEXCAN_H */
