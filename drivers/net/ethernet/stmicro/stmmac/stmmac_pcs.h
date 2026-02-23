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

/* AN Configuration defines */
#define GMAC_AN_CTRL_RAN	BIT_U32(9)	/* Restart Auto-Negotiation */
#define GMAC_AN_CTRL_ANE	BIT_U32(12)	/* Auto-Negotiation Enable */
#define GMAC_AN_CTRL_ELE	BIT_U32(14)	/* External Loopback Enable */
#define GMAC_AN_CTRL_ECD	BIT_U32(16)	/* Enable Comma Detect */
#define GMAC_AN_CTRL_LR		BIT_U32(17)	/* Lock to Reference */
#define GMAC_AN_CTRL_SGMRAL	BIT_U32(18)	/* SGMII RAL Control */

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
int stmmac_integrated_pcs_get_phy_intf_sel(struct phylink_pcs *pcs,
					   phy_interface_t interface);
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
