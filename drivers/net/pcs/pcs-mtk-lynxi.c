// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 MediaTek Inc.
/* A library for MediaTek SGMII circuit
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 * Author: Alexander Couzens <lynxis@fe80.eu>
 * Author: Daniel Golle <daniel@makrotopia.org>
 *
 */

#include <linux/mdio.h>
#include <linux/of.h>
#include <linux/pcs/pcs-mtk-lynxi.h>
#include <linux/phylink.h>
#include <linux/regmap.h>

/* SGMII subsystem config registers */
/* BMCR (low 16) BMSR (high 16) */
#define SGMSYS_PCS_CONTROL_1		0x0
#define SGMII_BMCR			GENMASK(15, 0)
#define SGMII_BMSR			GENMASK(31, 16)

#define SGMSYS_PCS_DEVICE_ID		0x4
#define SGMII_LYNXI_DEV_ID		0x4d544950

#define SGMSYS_PCS_ADVERTISE		0x8
#define SGMII_ADVERTISE			GENMASK(15, 0)
#define SGMII_LPA			GENMASK(31, 16)

#define SGMSYS_PCS_SCRATCH		0x14
#define SGMII_DEV_VERSION		GENMASK(31, 16)

/* Register to programmable link timer, the unit in 2 * 8ns */
#define SGMSYS_PCS_LINK_TIMER		0x18
#define SGMII_LINK_TIMER_MASK		GENMASK(19, 0)
#define SGMII_LINK_TIMER_VAL(ns)	FIELD_PREP(SGMII_LINK_TIMER_MASK, \
						   ((ns) / 2 / 8))

/* Register to control remote fault */
#define SGMSYS_SGMII_MODE		0x20
#define SGMII_IF_MODE_SGMII		BIT(0)
#define SGMII_SPEED_DUPLEX_AN		BIT(1)
#define SGMII_SPEED_MASK		GENMASK(3, 2)
#define SGMII_SPEED_10			FIELD_PREP(SGMII_SPEED_MASK, 0)
#define SGMII_SPEED_100			FIELD_PREP(SGMII_SPEED_MASK, 1)
#define SGMII_SPEED_1000		FIELD_PREP(SGMII_SPEED_MASK, 2)
#define SGMII_DUPLEX_HALF		BIT(4)
#define SGMII_REMOTE_FAULT_DIS		BIT(8)

/* Register to reset SGMII design */
#define SGMSYS_RESERVED_0		0x34
#define SGMII_SW_RESET			BIT(0)

/* Register to set SGMII speed, ANA RG_ Control Signals III */
#define SGMII_PHY_SPEED_MASK		GENMASK(3, 2)
#define SGMII_PHY_SPEED_1_25G		FIELD_PREP(SGMII_PHY_SPEED_MASK, 0)
#define SGMII_PHY_SPEED_3_125G		FIELD_PREP(SGMII_PHY_SPEED_MASK, 1)

/* Register to power up QPHY */
#define SGMSYS_QPHY_PWR_STATE_CTRL	0xe8
#define	SGMII_PHYA_PWD			BIT(4)

/* Register to QPHY wrapper control */
#define SGMSYS_QPHY_WRAP_CTRL		0xec
#define SGMII_PN_SWAP_MASK		GENMASK(1, 0)
#define SGMII_PN_SWAP_TX_RX		(BIT(0) | BIT(1))

/* struct mtk_pcs_lynxi -  This structure holds each sgmii regmap andassociated
 *                         data
 * @regmap:                The register map pointing at the range used to setup
 *                         SGMII modes
 * @dev:                   Pointer to device owning the PCS
 * @ana_rgc3:              The offset of register ANA_RGC3 relative to regmap
 * @interface:             Currently configured interface mode
 * @pcs:                   Phylink PCS structure
 * @flags:                 Flags indicating hardware properties
 */
struct mtk_pcs_lynxi {
	struct regmap		*regmap;
	u32			ana_rgc3;
	phy_interface_t		interface;
	struct			phylink_pcs pcs;
	u32			flags;
};

static struct mtk_pcs_lynxi *pcs_to_mtk_pcs_lynxi(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mtk_pcs_lynxi, pcs);
}

static unsigned int mtk_pcs_lynxi_inband_caps(struct phylink_pcs *pcs,
					      phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;

	default:
		return 0;
	}
}

static void mtk_pcs_lynxi_get_state(struct phylink_pcs *pcs,
				    struct phylink_link_state *state)
{
	struct mtk_pcs_lynxi *mpcs = pcs_to_mtk_pcs_lynxi(pcs);
	unsigned int bm, adv;

	/* Read the BMSR and LPA */
	regmap_read(mpcs->regmap, SGMSYS_PCS_CONTROL_1, &bm);
	regmap_read(mpcs->regmap, SGMSYS_PCS_ADVERTISE, &adv);

	phylink_mii_c22_pcs_decode_state(state, FIELD_GET(SGMII_BMSR, bm),
					 FIELD_GET(SGMII_LPA, adv));
}

static int mtk_pcs_lynxi_config(struct phylink_pcs *pcs, unsigned int neg_mode,
				phy_interface_t interface,
				const unsigned long *advertising,
				bool permit_pause_to_mac)
{
	struct mtk_pcs_lynxi *mpcs = pcs_to_mtk_pcs_lynxi(pcs);
	bool mode_changed = false, changed;
	unsigned int rgc3, sgm_mode, bmcr;
	int advertise, link_timer;

	advertise = phylink_mii_c22_pcs_encode_advertisement(interface,
							     advertising);
	if (advertise < 0)
		return advertise;

	/* Clearing IF_MODE_BIT0 switches the PCS to BASE-X mode, and
	 * we assume that fixes it's speed at bitrate = line rate (in
	 * other words, 1000Mbps or 2500Mbps).
	 */
	if (interface == PHY_INTERFACE_MODE_SGMII)
		sgm_mode = SGMII_IF_MODE_SGMII;
	else
		sgm_mode = 0;

	if (neg_mode & PHYLINK_PCS_NEG_INBAND)
		sgm_mode |= SGMII_REMOTE_FAULT_DIS;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
		if (interface == PHY_INTERFACE_MODE_SGMII)
			sgm_mode |= SGMII_SPEED_DUPLEX_AN;
		bmcr = BMCR_ANENABLE;
	} else {
		bmcr = 0;
	}

	if (mpcs->interface != interface) {
		link_timer = phylink_get_link_timer_ns(interface);
		if (link_timer < 0)
			return link_timer;

		/* PHYA power down */
		regmap_set_bits(mpcs->regmap, SGMSYS_QPHY_PWR_STATE_CTRL,
				SGMII_PHYA_PWD);

		/* Reset SGMII PCS state */
		regmap_set_bits(mpcs->regmap, SGMSYS_RESERVED_0,
				SGMII_SW_RESET);

		if (mpcs->flags & MTK_SGMII_FLAG_PN_SWAP)
			regmap_update_bits(mpcs->regmap, SGMSYS_QPHY_WRAP_CTRL,
					   SGMII_PN_SWAP_MASK,
					   SGMII_PN_SWAP_TX_RX);

		if (interface == PHY_INTERFACE_MODE_2500BASEX)
			rgc3 = SGMII_PHY_SPEED_3_125G;
		else
			rgc3 = SGMII_PHY_SPEED_1_25G;

		/* Configure the underlying interface speed */
		regmap_update_bits(mpcs->regmap, mpcs->ana_rgc3,
				   SGMII_PHY_SPEED_MASK, rgc3);

		/* Setup the link timer */
		regmap_write(mpcs->regmap, SGMSYS_PCS_LINK_TIMER,
			     SGMII_LINK_TIMER_VAL(link_timer));

		mpcs->interface = interface;
		mode_changed = true;
	}

	/* Update the advertisement, noting whether it has changed */
	regmap_update_bits_check(mpcs->regmap, SGMSYS_PCS_ADVERTISE,
				 SGMII_ADVERTISE, advertise, &changed);

	/* Update the sgmsys mode register */
	regmap_update_bits(mpcs->regmap, SGMSYS_SGMII_MODE,
			   SGMII_REMOTE_FAULT_DIS | SGMII_SPEED_DUPLEX_AN |
			   SGMII_IF_MODE_SGMII, sgm_mode);

	/* Update the BMCR */
	regmap_update_bits(mpcs->regmap, SGMSYS_PCS_CONTROL_1,
			   BMCR_ANENABLE, bmcr);

	/* Release PHYA power down state
	 * Only removing bit SGMII_PHYA_PWD isn't enough.
	 * There are cases when the SGMII_PHYA_PWD register contains 0x9 which
	 * prevents SGMII from working. The SGMII still shows link but no traffic
	 * can flow. Writing 0x0 to the PHYA_PWD register fix the issue. 0x0 was
	 * taken from a good working state of the SGMII interface.
	 * Unknown how much the QPHY needs but it is racy without a sleep.
	 * Tested on mt7622 & mt7986.
	 */
	usleep_range(50, 100);
	regmap_write(mpcs->regmap, SGMSYS_QPHY_PWR_STATE_CTRL, 0);

	return changed || mode_changed;
}

static void mtk_pcs_lynxi_restart_an(struct phylink_pcs *pcs)
{
	struct mtk_pcs_lynxi *mpcs = pcs_to_mtk_pcs_lynxi(pcs);

	regmap_set_bits(mpcs->regmap, SGMSYS_PCS_CONTROL_1, BMCR_ANRESTART);
}

static void mtk_pcs_lynxi_link_up(struct phylink_pcs *pcs,
				  unsigned int neg_mode,
				  phy_interface_t interface, int speed,
				  int duplex)
{
	struct mtk_pcs_lynxi *mpcs = pcs_to_mtk_pcs_lynxi(pcs);
	unsigned int sgm_mode;

	if (neg_mode != PHYLINK_PCS_NEG_INBAND_ENABLED) {
		/* Force the speed and duplex setting */
		if (speed == SPEED_10)
			sgm_mode = SGMII_SPEED_10;
		else if (speed == SPEED_100)
			sgm_mode = SGMII_SPEED_100;
		else
			sgm_mode = SGMII_SPEED_1000;

		if (duplex != DUPLEX_FULL)
			sgm_mode |= SGMII_DUPLEX_HALF;

		regmap_update_bits(mpcs->regmap, SGMSYS_SGMII_MODE,
				   SGMII_DUPLEX_HALF | SGMII_SPEED_MASK,
				   sgm_mode);
	}
}

static void mtk_pcs_lynxi_disable(struct phylink_pcs *pcs)
{
	struct mtk_pcs_lynxi *mpcs = pcs_to_mtk_pcs_lynxi(pcs);

	mpcs->interface = PHY_INTERFACE_MODE_NA;
}

static const struct phylink_pcs_ops mtk_pcs_lynxi_ops = {
	.pcs_inband_caps = mtk_pcs_lynxi_inband_caps,
	.pcs_get_state = mtk_pcs_lynxi_get_state,
	.pcs_config = mtk_pcs_lynxi_config,
	.pcs_an_restart = mtk_pcs_lynxi_restart_an,
	.pcs_link_up = mtk_pcs_lynxi_link_up,
	.pcs_disable = mtk_pcs_lynxi_disable,
};

struct phylink_pcs *mtk_pcs_lynxi_create(struct device *dev,
					 struct regmap *regmap, u32 ana_rgc3,
					 u32 flags)
{
	struct mtk_pcs_lynxi *mpcs;
	u32 id, ver;
	int ret;

	ret = regmap_read(regmap, SGMSYS_PCS_DEVICE_ID, &id);
	if (ret < 0)
		return NULL;

	if (id != SGMII_LYNXI_DEV_ID) {
		dev_err(dev, "unknown PCS device id %08x\n", id);
		return NULL;
	}

	ret = regmap_read(regmap, SGMSYS_PCS_SCRATCH, &ver);
	if (ret < 0)
		return NULL;

	ver = FIELD_GET(SGMII_DEV_VERSION, ver);
	if (ver != 0x1) {
		dev_err(dev, "unknown PCS device version %04x\n", ver);
		return NULL;
	}

	dev_dbg(dev, "MediaTek LynxI SGMII PCS (id 0x%08x, ver 0x%04x)\n", id,
		ver);

	mpcs = kzalloc(sizeof(*mpcs), GFP_KERNEL);
	if (!mpcs)
		return NULL;

	mpcs->ana_rgc3 = ana_rgc3;
	mpcs->regmap = regmap;
	mpcs->flags = flags;
	mpcs->pcs.ops = &mtk_pcs_lynxi_ops;
	mpcs->pcs.neg_mode = true;
	mpcs->pcs.poll = true;
	mpcs->interface = PHY_INTERFACE_MODE_NA;

	return &mpcs->pcs;
}
EXPORT_SYMBOL(mtk_pcs_lynxi_create);

void mtk_pcs_lynxi_destroy(struct phylink_pcs *pcs)
{
	if (!pcs)
		return;

	kfree(pcs_to_mtk_pcs_lynxi(pcs));
}
EXPORT_SYMBOL(mtk_pcs_lynxi_destroy);

MODULE_DESCRIPTION("MediaTek SGMII library for LynxI");
MODULE_LICENSE("GPL");
