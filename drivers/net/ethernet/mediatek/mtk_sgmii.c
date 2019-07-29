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
	const char *str;
	int i, err;

	ss->ana_rgc3 = ana_rgc3;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		np = of_parse_phandle(r, "mediatek,sgmiisys", i);
		if (!np)
			break;

		ss->regmap[i] = syscon_node_to_regmap(np);
		if (IS_ERR(ss->regmap[i]))
			return PTR_ERR(ss->regmap[i]);

		err = of_property_read_string(np, "mediatek,physpeed", &str);
		if (err)
			return err;

		if (!strcmp(str, "2500"))
			ss->flags[i] |= MTK_SGMII_PHYSPEED_2500;
		else if (!strcmp(str, "1000"))
			ss->flags[i] |= MTK_SGMII_PHYSPEED_1000;
		else if (!strcmp(str, "auto"))
			ss->flags[i] |= MTK_SGMII_PHYSPEED_AN;
		else
			return -EINVAL;
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

int mtk_sgmii_setup_mode_force(struct mtk_sgmii *ss, int id)
{
	unsigned int val;
	int mode;

	if (!ss->regmap[id])
		return -EINVAL;

	regmap_read(ss->regmap[id], ss->ana_rgc3, &val);
	val &= ~GENMASK(3, 2);
	mode = ss->flags[id] & MTK_SGMII_PHYSPEED_MASK;
	val |= (mode == MTK_SGMII_PHYSPEED_1000) ? 0 : BIT(2);
	regmap_write(ss->regmap[id], ss->ana_rgc3, val);

	/* Disable SGMII AN */
	regmap_read(ss->regmap[id], SGMSYS_PCS_CONTROL_1, &val);
	val &= ~BIT(12);
	regmap_write(ss->regmap[id], SGMSYS_PCS_CONTROL_1, val);

	/* SGMII force mode setting */
	val = 0x31120019;
	regmap_write(ss->regmap[id], SGMSYS_SGMII_MODE, val);

	/* Release PHYA power down state */
	regmap_read(ss->regmap[id], SGMSYS_QPHY_PWR_STATE_CTRL, &val);
	val &= ~SGMII_PHYA_PWD;
	regmap_write(ss->regmap[id], SGMSYS_QPHY_PWR_STATE_CTRL, val);

	return 0;
}
