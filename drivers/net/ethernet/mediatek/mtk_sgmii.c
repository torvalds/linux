// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 MediaTek Inc.

/* A library for MediaTek SGMII circuit
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/phylink.h>
#include <linux/regmap.h>

#include "mtk_eth_soc.h"

/* For SGMII interface mode */
static int mtk_pcs_setup_mode_an(struct mtk_pcs *mpcs)
{
	unsigned int val;

	if (!mpcs->regmap)
		return -EINVAL;

	/* Setup the link timer and QPHY power up inside SGMIISYS */
	regmap_write(mpcs->regmap, SGMSYS_PCS_LINK_TIMER,
		     SGMII_LINK_TIMER_DEFAULT);

	regmap_read(mpcs->regmap, SGMSYS_SGMII_MODE, &val);
	val |= SGMII_REMOTE_FAULT_DIS;
	regmap_write(mpcs->regmap, SGMSYS_SGMII_MODE, val);

	regmap_read(mpcs->regmap, SGMSYS_PCS_CONTROL_1, &val);
	val |= SGMII_AN_RESTART;
	regmap_write(mpcs->regmap, SGMSYS_PCS_CONTROL_1, val);

	regmap_read(mpcs->regmap, SGMSYS_QPHY_PWR_STATE_CTRL, &val);
	val &= ~SGMII_PHYA_PWD;
	regmap_write(mpcs->regmap, SGMSYS_QPHY_PWR_STATE_CTRL, val);

	return 0;

}

/* For 1000BASE-X and 2500BASE-X interface modes, which operate at a
 * fixed speed.
 */
static int mtk_pcs_setup_mode_force(struct mtk_pcs *mpcs,
				    phy_interface_t interface)
{
	unsigned int val;

	if (!mpcs->regmap)
		return -EINVAL;

	regmap_read(mpcs->regmap, mpcs->ana_rgc3, &val);
	val &= ~RG_PHY_SPEED_MASK;
	if (interface == PHY_INTERFACE_MODE_2500BASEX)
		val |= RG_PHY_SPEED_3_125G;
	regmap_write(mpcs->regmap, mpcs->ana_rgc3, val);

	/* Disable SGMII AN */
	regmap_read(mpcs->regmap, SGMSYS_PCS_CONTROL_1, &val);
	val &= ~SGMII_AN_ENABLE;
	regmap_write(mpcs->regmap, SGMSYS_PCS_CONTROL_1, val);

	/* Set the speed etc but leave the duplex unchanged */
	regmap_read(mpcs->regmap, SGMSYS_SGMII_MODE, &val);
	val &= SGMII_DUPLEX_FULL | ~SGMII_IF_MODE_MASK;
	val |= SGMII_SPEED_1000;
	regmap_write(mpcs->regmap, SGMSYS_SGMII_MODE, val);

	/* Release PHYA power down state */
	regmap_read(mpcs->regmap, SGMSYS_QPHY_PWR_STATE_CTRL, &val);
	val &= ~SGMII_PHYA_PWD;
	regmap_write(mpcs->regmap, SGMSYS_QPHY_PWR_STATE_CTRL, val);

	return 0;
}

int mtk_sgmii_config(struct mtk_sgmii *ss, int id, unsigned int mode,
		     phy_interface_t interface)
{
	struct mtk_pcs *mpcs = &ss->pcs[id];
	int err = 0;

	/* Setup SGMIISYS with the determined property */
	if (interface != PHY_INTERFACE_MODE_SGMII)
		err = mtk_pcs_setup_mode_force(mpcs, interface);
	else if (phylink_autoneg_inband(mode))
		err = mtk_pcs_setup_mode_an(mpcs);

	return err;
}

static void mtk_pcs_restart_an(struct mtk_pcs *mpcs)
{
	unsigned int val;

	if (!mpcs->regmap)
		return;

	regmap_read(mpcs->regmap, SGMSYS_PCS_CONTROL_1, &val);
	val |= SGMII_AN_RESTART;
	regmap_write(mpcs->regmap, SGMSYS_PCS_CONTROL_1, val);
}

static void mtk_pcs_link_up(struct mtk_pcs *mpcs, int speed, int duplex)
{
	unsigned int val;

	/* SGMII force duplex setting */
	regmap_read(mpcs->regmap, SGMSYS_SGMII_MODE, &val);
	val &= ~SGMII_DUPLEX_FULL;
	if (duplex == DUPLEX_FULL)
		val |= SGMII_DUPLEX_FULL;

	regmap_write(mpcs->regmap, SGMSYS_SGMII_MODE, val);
}

/* For 1000BASE-X and 2500BASE-X interface modes */
void mtk_sgmii_link_up(struct mtk_sgmii *ss, int id, int speed, int duplex)
{
	mtk_pcs_link_up(&ss->pcs[id], speed, duplex);
}

int mtk_sgmii_init(struct mtk_sgmii *ss, struct device_node *r, u32 ana_rgc3)
{
	struct device_node *np;
	int i;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		np = of_parse_phandle(r, "mediatek,sgmiisys", i);
		if (!np)
			break;

		ss->pcs[i].ana_rgc3 = ana_rgc3;
		ss->pcs[i].regmap = syscon_node_to_regmap(np);
		of_node_put(np);
		if (IS_ERR(ss->pcs[i].regmap))
			return PTR_ERR(ss->pcs[i].regmap);
	}

	return 0;
}

void mtk_sgmii_restart_an(struct mtk_eth *eth, int mac_id)
{
	unsigned int sid;

	/* Decide how GMAC and SGMIISYS be mapped */
	sid = (MTK_HAS_CAPS(eth->soc->caps, MTK_SHARED_SGMII)) ?
	       0 : mac_id;

	mtk_pcs_restart_an(&eth->sgmii->pcs[sid]);
}
