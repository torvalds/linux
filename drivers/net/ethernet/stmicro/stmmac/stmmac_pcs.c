// SPDX-License-Identifier: GPL-2.0-only
#include "stmmac.h"
#include "stmmac_pcs.h"

/*
 * GMAC_AN_STATUS is equivalent to MII_BMSR
 * GMAC_ANE_ADV is equivalent to 802.3z MII_ADVERTISE
 * GMAC_ANE_LPA is equivalent to 802.3z MII_LPA
 * GMAC_ANE_EXP is equivalent to MII_EXPANSION
 * GMAC_TBI is equivalent to MII_ESTATUS
 *
 * ADV, LPA and EXP are only available for the TBI and RTBI modes.
 */
#define GMAC_AN_STATUS	0x04	/* AN status */
#define GMAC_ANE_ADV	0x08	/* ANE Advertisement */
#define GMAC_ANE_LPA	0x0c	/* ANE link partener ability */
#define GMAC_TBI	0x14	/* TBI extend status */

/*
 * RGSMII status bitfield definitions.
 */
#define GMAC_RGSMII_LNKMOD		BIT(0)
#define GMAC_RGSMII_SPEED_MASK		GENMASK(2, 1)
#define GMAC_RGSMII_SPEED_125		2
#define GMAC_RGSMII_SPEED_25		1
#define GMAC_RGSMII_SPEED_2_5		0
#define GMAC_RGSMII_LNKSTS		BIT(3)

static unsigned int dwmac_integrated_pcs_inband_caps(struct phylink_pcs *pcs,
						     phy_interface_t interface)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);
	unsigned int ib_caps;

	if (phy_interface_mode_is_8023z(interface)) {
		ib_caps = LINK_INBAND_DISABLE;

		/* If the PCS supports TBI/RTBI, then BASE-X negotiation is
		 * supported.
		 */
		if (spcs->support_tbi_rtbi)
			ib_caps |= LINK_INBAND_ENABLE;

		return ib_caps;
	}

	return 0;
}

static int dwmac_integrated_pcs_enable(struct phylink_pcs *pcs)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);

	stmmac_mac_irq_modify(spcs->priv, 0, spcs->int_mask);

	return 0;
}

static void dwmac_integrated_pcs_disable(struct phylink_pcs *pcs)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);

	stmmac_mac_irq_modify(spcs->priv, spcs->int_mask, 0);
}

static void dwmac_integrated_pcs_get_state(struct phylink_pcs *pcs,
					   unsigned int neg_mode,
					   struct phylink_link_state *state)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);
	u32 status, lpa, rgsmii;

	status = readl(spcs->base + GMAC_AN_STATUS);

	if (phy_interface_mode_is_8023z(state->interface)) {
		/* For BASE-X modes, the PCS block supports the advertisement
		 * and link partner advertisement registers using standard
		 * 802.3 format. The status register also has the link status
		 * and AN complete bits in the same bit location. This will
		 * only be used when AN is enabled.
		 */
		lpa = readl(spcs->base + GMAC_ANE_LPA);

		phylink_mii_c22_pcs_decode_state(state, neg_mode, status, lpa);
	} else {
		rgsmii = field_get(spcs->rgsmii_status_mask,
				   readl(spcs->rgsmii));

		state->link = status & BMSR_LSTATUS &&
			      rgsmii & GMAC_RGSMII_LNKSTS;

		if (state->link && neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
			state->duplex = rgsmii & GMAC_RGSMII_LNKMOD ?
					DUPLEX_FULL : DUPLEX_HALF;
			switch (FIELD_GET(GMAC_RGSMII_SPEED_MASK, rgsmii)) {
			case GMAC_RGSMII_SPEED_2_5:
				state->speed = SPEED_10;
				break;

			case GMAC_RGSMII_SPEED_25:
				state->speed = SPEED_100;
				break;

			case GMAC_RGSMII_SPEED_125:
				state->speed = SPEED_1000;
				break;

			default:
				state->link = false;
				break;
			}
		}
	}
}

static int dwmac_integrated_pcs_config_aneg(struct stmmac_pcs *spcs,
					    phy_interface_t interface,
					    const unsigned long *advertising)
{
	bool changed = false;
	u32 adv;

	adv = phylink_mii_c22_pcs_encode_advertisement(interface, advertising);
	if (readl(spcs->base + GMAC_ANE_ADV) != adv)
		changed = true;
	writel(adv, spcs->base + GMAC_ANE_ADV);

	return changed;
}

static int dwmac_integrated_pcs_config(struct phylink_pcs *pcs,
				       unsigned int neg_mode,
				       phy_interface_t interface,
				       const unsigned long *advertising,
				       bool permit_pause_to_mac)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);
	bool changed = false, ane = true;

	/* Only configure the advertisement and allow AN in BASE-X mode if
	 * the core supports TBI/RTBI. AN will be filtered out by via phylink
	 * and the .pcs_inband_caps() method above.
	 */
	if (phy_interface_mode_is_8023z(interface) &&
	    spcs->support_tbi_rtbi) {
		ane = neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED;

		changed = dwmac_integrated_pcs_config_aneg(spcs, interface,
							   advertising);
	}

	dwmac_ctrl_ane(spcs->base, 0, ane,
		       spcs->priv->hw->reverse_sgmii_enable);

	return changed;
}

static void dwmac_integrated_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);
	void __iomem *an_control = spcs->base + GMAC_AN_CTRL(0);
	u32 ctrl;

	/* We can only do AN restart if using TBI/RTBI mode */
	if (spcs->support_tbi_rtbi) {
		ctrl = readl(an_control) | GMAC_AN_CTRL_RAN;
		writel(ctrl, an_control);
	}
}

static const struct phylink_pcs_ops dwmac_integrated_pcs_ops = {
	.pcs_inband_caps = dwmac_integrated_pcs_inband_caps,
	.pcs_enable = dwmac_integrated_pcs_enable,
	.pcs_disable = dwmac_integrated_pcs_disable,
	.pcs_get_state = dwmac_integrated_pcs_get_state,
	.pcs_config = dwmac_integrated_pcs_config,
	.pcs_an_restart = dwmac_integrated_pcs_an_restart,
};

void stmmac_integrated_pcs_irq(struct stmmac_priv *priv, u32 status,
			       struct stmmac_extra_stats *x)
{
	struct stmmac_pcs *spcs = priv->integrated_pcs;
	u32 val = readl(spcs->base + GMAC_AN_STATUS);

	if (status & PCS_ANE_IRQ) {
		x->irq_pcs_ane_n++;
		if (val & BMSR_ANEGCOMPLETE)
			dev_info(priv->device,
				 "PCS ANE process completed\n");
	}

	if (status & PCS_LINK_IRQ) {
		x->irq_pcs_link_n++;
		dev_info(priv->device, "PCS Link %s\n",
			 val & BMSR_LSTATUS ? "Up" : "Down");

		phylink_pcs_change(&spcs->pcs, val & BMSR_LSTATUS);
	}
}

int stmmac_integrated_pcs_get_phy_intf_sel(struct phylink_pcs *pcs,
					   phy_interface_t interface)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);

	if (interface == PHY_INTERFACE_MODE_SGMII)
		return PHY_INTF_SEL_SGMII;

	if (phy_interface_mode_is_8023z(interface)) {
		if (spcs->support_tbi_rtbi)
			return PHY_INTF_SEL_TBI;
		else
			return PHY_INTF_SEL_SGMII;
	}

	return -EINVAL;
}

int stmmac_integrated_pcs_init(struct stmmac_priv *priv,
			       const struct stmmac_pcs_info *pcs_info)
{
	struct stmmac_pcs *spcs;

	spcs = devm_kzalloc(priv->device, sizeof(*spcs), GFP_KERNEL);
	if (!spcs)
		return -ENOMEM;

	spcs->priv = priv;
	spcs->base = priv->ioaddr + pcs_info->pcs_offset;
	spcs->rgsmii = priv->ioaddr + pcs_info->rgsmii_offset;
	spcs->rgsmii_status_mask = pcs_info->rgsmii_status_mask;
	spcs->int_mask = pcs_info->int_mask;
	spcs->pcs.ops = &dwmac_integrated_pcs_ops;

	/* If the PCS supports extended status, then it supports BASE-X AN
	 * with a TBI interface to the SerDes. Otherwise, we can support
	 * BASE-X without AN using SGMII, which is required for qcom-ethqos.
	 */
	if (readl(spcs->base + GMAC_AN_STATUS) & BMSR_ESTATEN)
		spcs->support_tbi_rtbi = true;

	__set_bit(PHY_INTERFACE_MODE_SGMII, spcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX, spcs->pcs.supported_interfaces);

	/* Only allow 2500BASE-X if the SerDes has support. */
	if (priv->plat->flags & STMMAC_FLAG_SERDES_SUPPORTS_2500M)
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  spcs->pcs.supported_interfaces);

	priv->integrated_pcs = spcs;

	return 0;
}
