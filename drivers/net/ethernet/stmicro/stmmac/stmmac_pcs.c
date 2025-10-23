// SPDX-License-Identifier: GPL-2.0-only
#include "stmmac.h"
#include "stmmac_pcs.h"

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
	state->link = false;
}

static int dwmac_integrated_pcs_config(struct phylink_pcs *pcs,
				       unsigned int neg_mode,
				       phy_interface_t interface,
				       const unsigned long *advertising,
				       bool permit_pause_to_mac)
{
	struct stmmac_pcs *spcs = phylink_pcs_to_stmmac_pcs(pcs);

	dwmac_ctrl_ane(spcs->base, 0, 1, spcs->priv->hw->reverse_sgmii_enable);

	return 0;
}

static const struct phylink_pcs_ops dwmac_integrated_pcs_ops = {
	.pcs_enable = dwmac_integrated_pcs_enable,
	.pcs_disable = dwmac_integrated_pcs_disable,
	.pcs_get_state = dwmac_integrated_pcs_get_state,
	.pcs_config = dwmac_integrated_pcs_config,
};

int stmmac_integrated_pcs_init(struct stmmac_priv *priv, unsigned int offset,
			       u32 int_mask)
{
	struct stmmac_pcs *spcs;

	spcs = devm_kzalloc(priv->device, sizeof(*spcs), GFP_KERNEL);
	if (!spcs)
		return -ENOMEM;

	spcs->priv = priv;
	spcs->base = priv->ioaddr + offset;
	spcs->int_mask = int_mask;
	spcs->pcs.ops = &dwmac_integrated_pcs_ops;

	__set_bit(PHY_INTERFACE_MODE_SGMII, spcs->pcs.supported_interfaces);

	priv->integrated_pcs = spcs;

	return 0;
}
