/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * stmmac_pcs.h: Physical Coding Sublayer Header File
 *
 * Copyright (C) 2016 STMicroelectronics (R&D) Limited
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 */

#ifndef __STMMAC_PCS_H__
#define __STMMAC_PCS_H__

#include <linux/phylink.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "common.h"

/* PCS registers (AN/TBI/SGMII/RGMII) offsets */
#define GMAC_AN_CTRL(x)		(x)		/* AN control */
#define GMAC_AN_STATUS(x)	(x + 0x4)	/* AN status */

/* ADV, LPA and EXP are only available for the TBI and RTBI interfaces */
#define GMAC_ANE_ADV(x)		(x + 0x8)	/* ANE Advertisement */
#define GMAC_ANE_LPA(x)		(x + 0xc)	/* ANE link partener ability */
#define GMAC_ANE_EXP(x)		(x + 0x10)	/* ANE expansion */
#define GMAC_TBI(x)		(x + 0x14)	/* TBI extend status */

/* AN Configuration defines */
#define GMAC_AN_CTRL_RAN	BIT_U32(9)	/* Restart Auto-Negotiation */
#define GMAC_AN_CTRL_ANE	BIT_U32(12)	/* Auto-Negotiation Enable */
#define GMAC_AN_CTRL_ELE	BIT_U32(14)	/* External Loopback Enable */
#define GMAC_AN_CTRL_ECD	BIT_U32(16)	/* Enable Comma Detect */
#define GMAC_AN_CTRL_LR		BIT_U32(17)	/* Lock to Reference */
#define GMAC_AN_CTRL_SGMRAL	BIT_U32(18)	/* SGMII RAL Control */

/* AN Status defines */
#define GMAC_AN_STATUS_LS	BIT_U32(2)	/* Link Status 0:down 1:up */
#define GMAC_AN_STATUS_ANA	BIT_U32(3)	/* Auto-Negotiation Ability */
#define GMAC_AN_STATUS_ANC	BIT_U32(5)	/* Auto-Negotiation Complete */
#define GMAC_AN_STATUS_ES	BIT_U32(8)	/* Extended Status */

/* ADV and LPA defines */
#define GMAC_ANE_FD		BIT_U32(5)
#define GMAC_ANE_HD		BIT_U32(6)
#define GMAC_ANE_PSE		GENMASK_U32(8, 7)
#define GMAC_ANE_PSE_SHIFT	7
#define GMAC_ANE_RFE		GENMASK_U32(13, 12)
#define GMAC_ANE_RFE_SHIFT	12
#define GMAC_ANE_ACK		BIT_U32(14)

struct stmmac_priv;

struct stmmac_pcs {
	struct stmmac_priv *priv;
	void __iomem *base;
	u32 int_mask;
	struct phylink_pcs pcs;
};

static inline struct stmmac_pcs *
phylink_pcs_to_stmmac_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct stmmac_pcs, pcs);
}

void stmmac_integrated_pcs_irq(struct stmmac_priv *priv, u32 status,
			       struct stmmac_extra_stats *x);
int stmmac_integrated_pcs_init(struct stmmac_priv *priv, unsigned int offset,
			       u32 int_mask);

/**
 * dwmac_ctrl_ane - To program the AN Control Register.
 * @ioaddr: IO registers pointer
 * @reg: Base address of the AN Control Register.
 * @ane: to enable the auto-negotiation
 * @srgmi_ral: to manage MAC-2-MAC SGMII connections.
 * Description: this is the main function to configure the AN control register
 * and init the ANE, select loopback (usually for debugging purpose) and
 * configure SGMII RAL.
 */
static inline void dwmac_ctrl_ane(void __iomem *ioaddr, u32 reg, bool ane,
				  bool srgmi_ral)
{
	u32 value = readl(ioaddr + GMAC_AN_CTRL(reg));

	/* Enable and restart the Auto-Negotiation */
	if (ane)
		value |= GMAC_AN_CTRL_ANE | GMAC_AN_CTRL_RAN;
	else
		value &= ~GMAC_AN_CTRL_ANE;

	/* In case of MAC-2-MAC connection, block is configured to operate
	 * according to MAC conf register.
	 */
	if (srgmi_ral)
		value |= GMAC_AN_CTRL_SGMRAL;

	writel(value, ioaddr + GMAC_AN_CTRL(reg));
}
#endif /* __STMMAC_PCS_H__ */
