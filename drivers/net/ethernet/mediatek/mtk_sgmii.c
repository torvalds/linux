// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 MediaTek Inc.

/* A library for MediaTek SGMII circuit
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "mtk_eth_soc.h"

int mtk_sgmii_init(struct mtk_sgmii *ss, struct device_node *r, u32 ana_rgc3)
{
	struct device_node *np;
	int i;

	ss->ana_rgc3 = ana_rgc3;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		np = of_parse_phandle(r, "mediatek,sgmiisys", i);
		if (!np)
			break;

		ss->regmap[i] = syscon_node_to_regmap(np);
		if (IS_ERR(ss->regmap[i]))
			return PTR_ERR(ss->regmap[i]);
	}

	return 0;
}

int mtk_sgmii_setup_mode_an(struct mtk_sgmii *ss, int id)
{
	unsigned int val;

	if (!ss->regmap[id])
		return -EINVAL;

	/* Setup the link timer and QPHY power up inside SGMIISYS */
	regmap_write(ss->regmap[id], SGMSYS_PCS_LINK_TIMER,
		     SGMII_LINK_TIMER_DEFAULT);

	regmap_read(ss->regmap[id], SGMSYS_SGMII_MODE, &val);
	val |= SGMII_REMOTE_FAULT_DIS;
	regmap_write(ss->regmap[id], SGMSYS_SGMII_MODE, val);

	regmap_read(ss->regmap[id], SGMSYS_PCS_CONTROL_1, &val);
	val |= SGMII_AN_RESTART;
	regmap_write(ss->regmap[id], SGMSYS_PCS_CONTROL_1, val);

	regmap_read(ss->regmap[id], SGMSYS_QPHY_PWR_STATE_CTRL, &val);
	val &= ~SGMII_PHYA_PWD;
	regmap_write(ss->regmap[id], SGMSYS_QPHY_PWR_STATE_CTRL, val);

	return 0;
}

int mtk_sgmii_setup_mode_force(struct mtk_sgmii *ss, int id,
			       const struct phylink_link_state *state)
{
	unsigned int val;

	if (!ss->regmap[id])
		return -EINVAL;

	regmap_read(ss->regmap[id], ss->ana_rgc3, &val);
	val &= ~RG_PHY_SPEED_MASK;
	if (state->interface == PHY_INTERFACE_MODE_2500BASEX)
		val |= RG_PHY_SPEED_3_125G;
	regmap_write(ss->regmap[id], ss->ana_rgc3, val);

	/* Disable SGMII AN */
	regmap_read(ss->regmap[id], SGMSYS_PCS_CONTROL_1, &val);
	val &= ~SGMII_AN_ENABLE;
	regmap_write(ss->regmap[id], SGMSYS_PCS_CONTROL_1, val);

	/* SGMII force mode setting */
	regmap_read(ss->regmap[id], SGMSYS_SGMII_MODE, &val);
	val &= ~SGMII_IF_MODE_MASK;

	switch (state->speed) {
	case SPEED_10:
		val |= SGMII_SPEED_10;
		break;
	case SPEED_100:
		val |= SGMII_SPEED_100;
		break;
	case SPEED_2500:
	case SPEED_1000:
		val |= SGMII_SPEED_1000;
		break;
	};

	if (state->duplex == DUPLEX_FULL)
		val |= SGMII_DUPLEX_FULL;

	regmap_write(ss->regmap[id], SGMSYS_SGMII_MODE, val);

	/* Release PHYA power down state */
	regmap_read(ss->regmap[id], SGMSYS_QPHY_PWR_STATE_CTRL, &val);
	val &= ~SGMII_PHYA_PWD;
	regmap_write(ss->regmap[id], SGMSYS_QPHY_PWR_STATE_CTRL, val);

	return 0;
}

void mtk_sgmii_restart_an(struct mtk_eth *eth, int mac_id)
{
	struct mtk_sgmii *ss = eth->sgmii;
	unsigned int val, sid;

	/* Decide how GMAC and SGMIISYS be mapped */
	sid = (MTK_HAS_CAPS(eth->soc->caps, MTK_SHARED_SGMII)) ?
	       0 : mac_id;

	if (!ss->regmap[sid])
		return;

	regmap_read(ss->regmap[sid], SGMSYS_PCS_CONTROL_1, &val);
	val |= SGMII_AN_RESTART;
	regmap_write(ss->regmap[sid], SGMSYS_PCS_CONTROL_1, val);
}
