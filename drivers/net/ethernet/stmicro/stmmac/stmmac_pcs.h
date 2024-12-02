/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * stmmac_pcs.h: Physical Coding Sublayer Header File
 *
 * Copyright (C) 2016 STMicroelectronics (R&D) Limited
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 */

#ifndef __STMMAC_PCS_H__
#define __STMMAC_PCS_H__

#include <linux/slab.h>
#include <linux/io.h>
#include "common.h"

/* PCS registers (AN/TBI/SGMII/RGMII) offsets */
#define GMAC_AN_CTRL(x)		(x)		/* AN control */
#define GMAC_AN_STATUS(x)	(x + 0x4)	/* AN status */
#define GMAC_ANE_ADV(x)		(x + 0x8)	/* ANE Advertisement */
#define GMAC_ANE_LPA(x)		(x + 0xc)	/* ANE link partener ability */
#define GMAC_ANE_EXP(x)		(x + 0x10)	/* ANE expansion */
#define GMAC_TBI(x)		(x + 0x14)	/* TBI extend status */

/* AN Configuration defines */
#define GMAC_AN_CTRL_RAN	BIT(9)	/* Restart Auto-Negotiation */
#define GMAC_AN_CTRL_ANE	BIT(12)	/* Auto-Negotiation Enable */
#define GMAC_AN_CTRL_ELE	BIT(14)	/* External Loopback Enable */
#define GMAC_AN_CTRL_ECD	BIT(16)	/* Enable Comma Detect */
#define GMAC_AN_CTRL_LR		BIT(17)	/* Lock to Reference */
#define GMAC_AN_CTRL_SGMRAL	BIT(18)	/* SGMII RAL Control */

/* AN Status defines */
#define GMAC_AN_STATUS_LS	BIT(2)	/* Link Status 0:down 1:up */
#define GMAC_AN_STATUS_ANA	BIT(3)	/* Auto-Negotiation Ability */
#define GMAC_AN_STATUS_ANC	BIT(5)	/* Auto-Negotiation Complete */
#define GMAC_AN_STATUS_ES	BIT(8)	/* Extended Status */

/* ADV and LPA defines */
#define GMAC_ANE_FD		BIT(5)
#define GMAC_ANE_HD		BIT(6)
#define GMAC_ANE_PSE		GENMASK(8, 7)
#define GMAC_ANE_PSE_SHIFT	7
#define GMAC_ANE_RFE		GENMASK(13, 12)
#define GMAC_ANE_RFE_SHIFT	12
#define GMAC_ANE_ACK		BIT(14)

/**
 * dwmac_pcs_isr - TBI, RTBI, or SGMII PHY ISR
 * @ioaddr: IO registers pointer
 * @reg: Base address of the AN Control Register.
 * @intr_status: GMAC core interrupt status
 * @x: pointer to log these events as stats
 * Description: it is the ISR for PCS events: Auto-Negotiation Completed and
 * Link status.
 */
static inline void dwmac_pcs_isr(void __iomem *ioaddr, u32 reg,
				 unsigned int intr_status,
				 struct stmmac_extra_stats *x)
{
	u32 val = readl(ioaddr + GMAC_AN_STATUS(reg));

	if (intr_status & PCS_ANE_IRQ) {
		x->irq_pcs_ane_n++;
		if (val & GMAC_AN_STATUS_ANC)
			pr_info("stmmac_pcs: ANE process completed\n");
	}

	if (intr_status & PCS_LINK_IRQ) {
		x->irq_pcs_link_n++;
		if (val & GMAC_AN_STATUS_LS)
			pr_info("stmmac_pcs: Link Up\n");
		else
			pr_info("stmmac_pcs: Link Down\n");
	}
}

/**
 * dwmac_rane - To restart ANE
 * @ioaddr: IO registers pointer
 * @reg: Base address of the AN Control Register.
 * @restart: to restart ANE
 * Description: this is to just restart the Auto-Negotiation.
 */
static inline void dwmac_rane(void __iomem *ioaddr, u32 reg, bool restart)
{
	u32 value = readl(ioaddr + GMAC_AN_CTRL(reg));

	if (restart)
		value |= GMAC_AN_CTRL_RAN;

	writel(value, ioaddr + GMAC_AN_CTRL(reg));
}

/**
 * dwmac_ctrl_ane - To program the AN Control Register.
 * @ioaddr: IO registers pointer
 * @reg: Base address of the AN Control Register.
 * @ane: to enable the auto-negotiation
 * @srgmi_ral: to manage MAC-2-MAC SGMII connections.
 * @loopback: to cause the PHY to loopback tx data into rx path.
 * Description: this is the main function to configure the AN control register
 * and init the ANE, select loopback (usually for debugging purpose) and
 * configure SGMII RAL.
 */
static inline void dwmac_ctrl_ane(void __iomem *ioaddr, u32 reg, bool ane,
				  bool srgmi_ral, bool loopback)
{
	u32 value = readl(ioaddr + GMAC_AN_CTRL(reg));

	/* Enable and restart the Auto-Negotiation */
	if (ane)
		value |= GMAC_AN_CTRL_ANE | GMAC_AN_CTRL_RAN;

	/* In case of MAC-2-MAC connection, block is configured to operate
	 * according to MAC conf register.
	 */
	if (srgmi_ral)
		value |= GMAC_AN_CTRL_SGMRAL;

	if (loopback)
		value |= GMAC_AN_CTRL_ELE;

	writel(value, ioaddr + GMAC_AN_CTRL(reg));
}

/**
 * dwmac_get_adv_lp - Get ADV and LP cap
 * @ioaddr: IO registers pointer
 * @reg: Base address of the AN Control Register.
 * @adv_lp: structure to store the adv,lp status
 * Description: this is to expose the ANE advertisement and Link partner ability
 * status to ethtool support.
 */
static inline void dwmac_get_adv_lp(void __iomem *ioaddr, u32 reg,
				    struct rgmii_adv *adv_lp)
{
	u32 value = readl(ioaddr + GMAC_ANE_ADV(reg));

	if (value & GMAC_ANE_FD)
		adv_lp->duplex = DUPLEX_FULL;
	if (value & GMAC_ANE_HD)
		adv_lp->duplex |= DUPLEX_HALF;

	adv_lp->pause = (value & GMAC_ANE_PSE) >> GMAC_ANE_PSE_SHIFT;

	value = readl(ioaddr + GMAC_ANE_LPA(reg));

	if (value & GMAC_ANE_FD)
		adv_lp->lp_duplex = DUPLEX_FULL;
	if (value & GMAC_ANE_HD)
		adv_lp->lp_duplex = DUPLEX_HALF;

	adv_lp->lp_pause = (value & GMAC_ANE_PSE) >> GMAC_ANE_PSE_SHIFT;
}
#endif /* __STMMAC_PCS_H__ */
