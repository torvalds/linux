// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare XPCS helpers
 *
 * Author: Jose Abreu <Jose.Abreu@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/property.h>

#include "pcs-xpcs.h"

#define phylink_pcs_to_xpcs(pl_pcs) \
	container_of((pl_pcs), struct dw_xpcs, pcs)

static const int xpcs_usxgmii_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_10gkr_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_xlgmii_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseDR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_10gbaser_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseER_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_sgmii_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_10baseT_Half_BIT,
	ETHTOOL_LINK_MODE_10baseT_Full_BIT,
	ETHTOOL_LINK_MODE_100baseT_Half_BIT,
	ETHTOOL_LINK_MODE_100baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_1000basex_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_2500basex_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
	ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

struct dw_xpcs_compat {
	phy_interface_t interface;
	const int *supported;
	int an_mode;
	int (*pma_config)(struct dw_xpcs *xpcs);
};

struct dw_xpcs_desc {
	u32 id;
	u32 mask;
	const struct dw_xpcs_compat *compat;
};

static const struct dw_xpcs_compat *
xpcs_find_compat(struct dw_xpcs *xpcs, phy_interface_t interface)
{
	const struct dw_xpcs_compat *compat;

	for (compat = xpcs->desc->compat; compat->supported; compat++)
		if (compat->interface == interface)
			return compat;

	return NULL;
}

struct phylink_pcs *xpcs_to_phylink_pcs(struct dw_xpcs *xpcs)
{
	return &xpcs->pcs;
}
EXPORT_SYMBOL_GPL(xpcs_to_phylink_pcs);

int xpcs_get_an_mode(struct dw_xpcs *xpcs, phy_interface_t interface)
{
	const struct dw_xpcs_compat *compat;

	compat = xpcs_find_compat(xpcs, interface);
	if (!compat)
		return -ENODEV;

	return compat->an_mode;
}
EXPORT_SYMBOL_GPL(xpcs_get_an_mode);

static bool __xpcs_linkmode_supported(const struct dw_xpcs_compat *compat,
				      enum ethtool_link_mode_bit_indices linkmode)
{
	int i;

	for (i = 0; compat->supported[i] != __ETHTOOL_LINK_MODE_MASK_NBITS; i++)
		if (compat->supported[i] == linkmode)
			return true;

	return false;
}

#define xpcs_linkmode_supported(compat, mode) \
	__xpcs_linkmode_supported(compat, ETHTOOL_LINK_MODE_ ## mode ## _BIT)

int xpcs_read(struct dw_xpcs *xpcs, int dev, u32 reg)
{
	return mdiodev_c45_read(xpcs->mdiodev, dev, reg);
}

int xpcs_write(struct dw_xpcs *xpcs, int dev, u32 reg, u16 val)
{
	return mdiodev_c45_write(xpcs->mdiodev, dev, reg, val);
}

int xpcs_modify(struct dw_xpcs *xpcs, int dev, u32 reg, u16 mask, u16 set)
{
	return mdiodev_c45_modify(xpcs->mdiodev, dev, reg, mask, set);
}

static int xpcs_modify_changed(struct dw_xpcs *xpcs, int dev, u32 reg,
			       u16 mask, u16 set)
{
	return mdiodev_c45_modify_changed(xpcs->mdiodev, dev, reg, mask, set);
}

static int xpcs_read_vendor(struct dw_xpcs *xpcs, int dev, u32 reg)
{
	return xpcs_read(xpcs, dev, DW_VENDOR | reg);
}

static int xpcs_write_vendor(struct dw_xpcs *xpcs, int dev, int reg,
			     u16 val)
{
	return xpcs_write(xpcs, dev, DW_VENDOR | reg, val);
}

static int xpcs_modify_vendor(struct dw_xpcs *xpcs, int dev, int reg, u16 mask,
			      u16 set)
{
	return xpcs_modify(xpcs, dev, DW_VENDOR | reg, mask, set);
}

int xpcs_read_vpcs(struct dw_xpcs *xpcs, int reg)
{
	return xpcs_read_vendor(xpcs, MDIO_MMD_PCS, reg);
}

int xpcs_write_vpcs(struct dw_xpcs *xpcs, int reg, u16 val)
{
	return xpcs_write_vendor(xpcs, MDIO_MMD_PCS, reg, val);
}

static int xpcs_modify_vpcs(struct dw_xpcs *xpcs, int reg, u16 mask, u16 val)
{
	return xpcs_modify_vendor(xpcs, MDIO_MMD_PCS, reg, mask, val);
}

static int xpcs_poll_reset(struct dw_xpcs *xpcs, int dev)
{
	int ret, val;

	ret = read_poll_timeout(xpcs_read, val,
				val < 0 || !(val & BMCR_RESET),
				50000, 600000, true, xpcs, dev, MII_BMCR);
	if (val < 0)
		ret = val;

	return ret;
}

static int xpcs_soft_reset(struct dw_xpcs *xpcs,
			   const struct dw_xpcs_compat *compat)
{
	int ret, dev;

	switch (compat->an_mode) {
	case DW_AN_C73:
	case DW_10GBASER:
		dev = MDIO_MMD_PCS;
		break;
	case DW_AN_C37_SGMII:
	case DW_2500BASEX:
	case DW_AN_C37_1000BASEX:
		dev = MDIO_MMD_VEND2;
		break;
	default:
		return -EINVAL;
	}

	ret = xpcs_write(xpcs, dev, MII_BMCR, BMCR_RESET);
	if (ret < 0)
		return ret;

	return xpcs_poll_reset(xpcs, dev);
}

#define xpcs_warn(__xpcs, __state, __args...) \
({ \
	if ((__state)->link) \
		dev_warn(&(__xpcs)->mdiodev->dev, ##__args); \
})

static int xpcs_read_fault_c73(struct dw_xpcs *xpcs,
			       struct phylink_link_state *state,
			       u16 pcs_stat1)
{
	int ret;

	if (pcs_stat1 & MDIO_STAT1_FAULT) {
		xpcs_warn(xpcs, state, "Link fault condition detected!\n");
		return -EFAULT;
	}

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT2);
	if (ret < 0)
		return ret;

	if (ret & MDIO_STAT2_RXFAULT)
		xpcs_warn(xpcs, state, "Receiver fault detected!\n");
	if (ret & MDIO_STAT2_TXFAULT)
		xpcs_warn(xpcs, state, "Transmitter fault detected!\n");

	ret = xpcs_read_vendor(xpcs, MDIO_MMD_PCS, DW_VR_XS_PCS_DIG_STS);
	if (ret < 0)
		return ret;

	if (ret & DW_RXFIFO_ERR) {
		xpcs_warn(xpcs, state, "FIFO fault condition detected!\n");
		return -EFAULT;
	}

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_PCS_10GBRT_STAT1);
	if (ret < 0)
		return ret;

	if (!(ret & MDIO_PCS_10GBRT_STAT1_BLKLK))
		xpcs_warn(xpcs, state, "Link is not locked!\n");

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_PCS_10GBRT_STAT2);
	if (ret < 0)
		return ret;

	if (ret & MDIO_PCS_10GBRT_STAT2_ERR) {
		xpcs_warn(xpcs, state, "Link has errors!\n");
		return -EFAULT;
	}

	return 0;
}

static void xpcs_link_up_usxgmii(struct dw_xpcs *xpcs, int speed)
{
	int ret, speed_sel;

	switch (speed) {
	case SPEED_10:
		speed_sel = DW_USXGMII_10;
		break;
	case SPEED_100:
		speed_sel = DW_USXGMII_100;
		break;
	case SPEED_1000:
		speed_sel = DW_USXGMII_1000;
		break;
	case SPEED_2500:
		speed_sel = DW_USXGMII_2500;
		break;
	case SPEED_5000:
		speed_sel = DW_USXGMII_5000;
		break;
	case SPEED_10000:
		speed_sel = DW_USXGMII_10000;
		break;
	default:
		/* Nothing to do here */
		return;
	}

	ret = xpcs_modify_vpcs(xpcs, MDIO_CTRL1, DW_USXGMII_EN, DW_USXGMII_EN);
	if (ret < 0)
		goto out;

	ret = xpcs_modify(xpcs, MDIO_MMD_VEND2, MII_BMCR, DW_USXGMII_SS_MASK,
			  speed_sel | DW_USXGMII_FULL);
	if (ret < 0)
		goto out;

	ret = xpcs_modify_vpcs(xpcs, MDIO_CTRL1, DW_USXGMII_RST,
			       DW_USXGMII_RST);
	if (ret < 0)
		goto out;

	return;

out:
	dev_err(&xpcs->mdiodev->dev, "%s: XPCS access returned %pe\n",
		__func__, ERR_PTR(ret));
}

static int _xpcs_config_aneg_c73(struct dw_xpcs *xpcs,
				 const struct dw_xpcs_compat *compat)
{
	int ret, adv;

	/* By default, in USXGMII mode XPCS operates at 10G baud and
	 * replicates data to achieve lower speeds. Hereby, in this
	 * default configuration we need to advertise all supported
	 * modes and not only the ones we want to use.
	 */

	/* SR_AN_ADV3 */
	adv = 0;
	if (xpcs_linkmode_supported(compat, 2500baseX_Full))
		adv |= DW_C73_2500KX;

	/* TODO: 5000baseKR */

	ret = xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV3, adv);
	if (ret < 0)
		return ret;

	/* SR_AN_ADV2 */
	adv = 0;
	if (xpcs_linkmode_supported(compat, 1000baseKX_Full))
		adv |= DW_C73_1000KX;
	if (xpcs_linkmode_supported(compat, 10000baseKX4_Full))
		adv |= DW_C73_10000KX4;
	if (xpcs_linkmode_supported(compat, 10000baseKR_Full))
		adv |= DW_C73_10000KR;

	ret = xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV2, adv);
	if (ret < 0)
		return ret;

	/* SR_AN_ADV1 */
	adv = DW_C73_AN_ADV_SF;
	if (xpcs_linkmode_supported(compat, Pause))
		adv |= DW_C73_PAUSE;
	if (xpcs_linkmode_supported(compat, Asym_Pause))
		adv |= DW_C73_ASYM_PAUSE;

	return xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV1, adv);
}

static int xpcs_config_aneg_c73(struct dw_xpcs *xpcs,
				const struct dw_xpcs_compat *compat)
{
	int ret;

	ret = _xpcs_config_aneg_c73(xpcs, compat);
	if (ret < 0)
		return ret;

	return xpcs_modify(xpcs, MDIO_MMD_AN, MDIO_CTRL1,
			   MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART,
			   MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);
}

static int xpcs_aneg_done_c73(struct dw_xpcs *xpcs,
			      struct phylink_link_state *state,
			      const struct dw_xpcs_compat *compat, u16 an_stat1)
{
	int ret;

	if (an_stat1 & MDIO_AN_STAT1_COMPLETE) {
		ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_AN_LPA);
		if (ret < 0)
			return ret;

		/* Check if Aneg outcome is valid */
		if (!(ret & DW_C73_AN_ADV_SF)) {
			xpcs_config_aneg_c73(xpcs, compat);
			return 0;
		}

		return 1;
	}

	return 0;
}

static int xpcs_read_lpa_c73(struct dw_xpcs *xpcs,
			     struct phylink_link_state *state, u16 an_stat1)
{
	u16 lpa[3];
	int i, ret;

	if (!(an_stat1 & MDIO_AN_STAT1_LPABLE)) {
		phylink_clear(state->lp_advertising, Autoneg);
		return 0;
	}

	phylink_set(state->lp_advertising, Autoneg);

	/* Read Clause 73 link partner advertisement */
	for (i = ARRAY_SIZE(lpa); --i >= 0; ) {
		ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_AN_LPA + i);
		if (ret < 0)
			return ret;

		lpa[i] = ret;
	}

	mii_c73_mod_linkmode(state->lp_advertising, lpa);

	return 0;
}

static int xpcs_get_max_xlgmii_speed(struct dw_xpcs *xpcs,
				     struct phylink_link_state *state)
{
	unsigned long *adv = state->advertising;
	int speed = SPEED_UNKNOWN;
	int bit;

	for_each_set_bit(bit, adv, __ETHTOOL_LINK_MODE_MASK_NBITS) {
		int new_speed = SPEED_UNKNOWN;

		switch (bit) {
		case ETHTOOL_LINK_MODE_25000baseCR_Full_BIT:
		case ETHTOOL_LINK_MODE_25000baseKR_Full_BIT:
		case ETHTOOL_LINK_MODE_25000baseSR_Full_BIT:
			new_speed = SPEED_25000;
			break;
		case ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT:
		case ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT:
		case ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT:
		case ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT:
			new_speed = SPEED_40000;
			break;
		case ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseKR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseSR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseCR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseDR_Full_BIT:
			new_speed = SPEED_50000;
			break;
		case ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT:
			new_speed = SPEED_100000;
			break;
		default:
			continue;
		}

		if (new_speed > speed)
			speed = new_speed;
	}

	return speed;
}

static void xpcs_resolve_pma(struct dw_xpcs *xpcs,
			     struct phylink_link_state *state)
{
	state->pause = MLO_PAUSE_TX | MLO_PAUSE_RX;
	state->duplex = DUPLEX_FULL;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_10GKR:
		state->speed = SPEED_10000;
		break;
	case PHY_INTERFACE_MODE_XLGMII:
		state->speed = xpcs_get_max_xlgmii_speed(xpcs, state);
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}
}

static int xpcs_validate(struct phylink_pcs *pcs, unsigned long *supported,
			 const struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(xpcs_supported) = { 0, };
	const struct dw_xpcs_compat *compat;
	struct dw_xpcs *xpcs;
	int i;

	xpcs = phylink_pcs_to_xpcs(pcs);
	compat = xpcs_find_compat(xpcs, state->interface);
	if (!compat)
		return -EINVAL;

	/* Populate the supported link modes for this PHY interface type.
	 * FIXME: what about the port modes and autoneg bit? This masks
	 * all those away.
	 */
	for (i = 0; compat->supported[i] != __ETHTOOL_LINK_MODE_MASK_NBITS; i++)
		set_bit(compat->supported[i], xpcs_supported);

	linkmode_and(supported, supported, xpcs_supported);

	return 0;
}

static unsigned int xpcs_inband_caps(struct phylink_pcs *pcs,
				     phy_interface_t interface)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);
	const struct dw_xpcs_compat *compat;

	compat = xpcs_find_compat(xpcs, interface);
	if (!compat)
		return 0;

	switch (compat->an_mode) {
	case DW_AN_C73:
		return LINK_INBAND_ENABLE;

	case DW_AN_C37_SGMII:
	case DW_AN_C37_1000BASEX:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;

	case DW_10GBASER:
	case DW_2500BASEX:
		return LINK_INBAND_DISABLE;

	default:
		return 0;
	}
}

static void xpcs_get_interfaces(struct dw_xpcs *xpcs, unsigned long *interfaces)
{
	const struct dw_xpcs_compat *compat;

	for (compat = xpcs->desc->compat; compat->supported; compat++)
		__set_bit(compat->interface, interfaces);
}

static int xpcs_switch_interface_mode(struct dw_xpcs *xpcs,
				      phy_interface_t interface)
{
	int ret = 0;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		ret = txgbe_xpcs_switch_mode(xpcs, interface);
	} else if (xpcs->interface != interface) {
		if (interface == PHY_INTERFACE_MODE_SGMII)
			xpcs->need_reset = true;
		xpcs->interface = interface;
	}

	return ret;
}

static void xpcs_pre_config(struct phylink_pcs *pcs, phy_interface_t interface)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);
	const struct dw_xpcs_compat *compat;
	int ret;

	ret = xpcs_switch_interface_mode(xpcs, interface);
	if (ret)
		dev_err(&xpcs->mdiodev->dev, "switch interface failed: %pe\n",
			ERR_PTR(ret));

	if (!xpcs->need_reset)
		return;

	compat = xpcs_find_compat(xpcs, interface);
	if (!compat) {
		dev_err(&xpcs->mdiodev->dev, "unsupported interface %s\n",
			phy_modes(interface));
		return;
	}

	ret = xpcs_soft_reset(xpcs, compat);
	if (ret)
		dev_err(&xpcs->mdiodev->dev, "soft reset failed: %pe\n",
			ERR_PTR(ret));

	xpcs->need_reset = false;
}

static int xpcs_config_aneg_c37_sgmii(struct dw_xpcs *xpcs,
				      unsigned int neg_mode)
{
	int ret, mdio_ctrl, tx_conf;
	u16 mask, val;

	/* For AN for C37 SGMII mode, the settings are :-
	 * 1) VR_MII_MMD_CTRL Bit(12) [AN_ENABLE] = 0b (Disable SGMII AN in case
	      it is already enabled)
	 * 2) VR_MII_AN_CTRL Bit(2:1)[PCS_MODE] = 10b (SGMII AN)
	 * 3) VR_MII_AN_CTRL Bit(3) [TX_CONFIG] = 0b (MAC side SGMII)
	 *    DW xPCS used with DW EQoS MAC is always MAC side SGMII.
	 * 4) VR_MII_DIG_CTRL1 Bit(9) [MAC_AUTO_SW] = 1b (Automatic
	 *    speed/duplex mode change by HW after SGMII AN complete)
	 * 5) VR_MII_MMD_CTRL Bit(12) [AN_ENABLE] = 1b (Enable SGMII AN)
	 *
	 * Note that VR_MII_MMD_CTRL is MII_BMCR.
	 *
	 * Note: Since it is MAC side SGMII, there is no need to set
	 *	 SR_MII_AN_ADV. MAC side SGMII receives AN Tx Config from
	 *	 PHY about the link state change after C28 AN is completed
	 *	 between PHY and Link Partner. There is also no need to
	 *	 trigger AN restart for MAC-side SGMII.
	 */
	mdio_ctrl = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_BMCR);
	if (mdio_ctrl < 0)
		return mdio_ctrl;

	if (mdio_ctrl & BMCR_ANENABLE) {
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MII_BMCR,
				 mdio_ctrl & ~BMCR_ANENABLE);
		if (ret < 0)
			return ret;
	}

	mask = DW_VR_MII_PCS_MODE_MASK | DW_VR_MII_TX_CONFIG_MASK;
	val = FIELD_PREP(DW_VR_MII_PCS_MODE_MASK,
			 DW_VR_MII_PCS_MODE_C37_SGMII);

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		mask |= DW_VR_MII_AN_CTRL_8BIT;
		val |= DW_VR_MII_AN_CTRL_8BIT;
		/* Hardware requires it to be PHY side SGMII */
		tx_conf = DW_VR_MII_TX_CONFIG_PHY_SIDE_SGMII;
	} else {
		tx_conf = DW_VR_MII_TX_CONFIG_MAC_SIDE_SGMII;
	}

	val |= FIELD_PREP(DW_VR_MII_TX_CONFIG_MASK, tx_conf);

	ret = xpcs_modify(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL, mask, val);
	if (ret < 0)
		return ret;

	val = 0;
	mask = DW_VR_MII_DIG_CTRL1_2G5_EN | DW_VR_MII_DIG_CTRL1_MAC_AUTO_SW;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		val = DW_VR_MII_DIG_CTRL1_MAC_AUTO_SW;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		mask |= DW_VR_MII_DIG_CTRL1_PHY_MODE_CTRL;
		val |= DW_VR_MII_DIG_CTRL1_PHY_MODE_CTRL;
	}

	ret = xpcs_modify(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL1, mask, val);
	if (ret < 0)
		return ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MII_BMCR,
				 mdio_ctrl | BMCR_ANENABLE);

	return ret;
}

static int xpcs_config_aneg_c37_1000basex(struct dw_xpcs *xpcs,
					  unsigned int neg_mode,
					  const unsigned long *advertising)
{
	phy_interface_t interface = PHY_INTERFACE_MODE_1000BASEX;
	int ret, mdio_ctrl, adv;
	bool changed = 0;
	u16 mask, val;

	/* According to Chap 7.12, to set 1000BASE-X C37 AN, AN must
	 * be disabled first:-
	 * 1) VR_MII_MMD_CTRL Bit(12)[AN_ENABLE] = 0b
	 * 2) VR_MII_AN_CTRL Bit(2:1)[PCS_MODE] = 00b (1000BASE-X C37)
	 *
	 * Note that VR_MII_MMD_CTRL is MII_BMCR.
	 */
	mdio_ctrl = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_BMCR);
	if (mdio_ctrl < 0)
		return mdio_ctrl;

	if (mdio_ctrl & BMCR_ANENABLE) {
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MII_BMCR,
				 mdio_ctrl & ~BMCR_ANENABLE);
		if (ret < 0)
			return ret;
	}

	mask = DW_VR_MII_PCS_MODE_MASK;
	val = FIELD_PREP(DW_VR_MII_PCS_MODE_MASK,
			 DW_VR_MII_PCS_MODE_C37_1000BASEX);

	if (!xpcs->pcs.poll) {
		mask |= DW_VR_MII_AN_INTR_EN;
		val |= DW_VR_MII_AN_INTR_EN;
	}

	ret = xpcs_modify(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL, mask, val);
	if (ret < 0)
		return ret;

	/* Check for advertising changes and update the C45 MII ADV
	 * register accordingly.
	 */
	adv = phylink_mii_c22_pcs_encode_advertisement(interface,
						       advertising);
	if (adv >= 0) {
		ret = xpcs_modify_changed(xpcs, MDIO_MMD_VEND2,
					  MII_ADVERTISE, 0xffff, adv);
		if (ret < 0)
			return ret;

		changed = ret;
	}

	/* Clear CL37 AN complete status */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS, 0);
	if (ret < 0)
		return ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MII_BMCR,
				 mdio_ctrl | BMCR_ANENABLE);
		if (ret < 0)
			return ret;
	}

	return changed;
}

static int xpcs_config_2500basex(struct dw_xpcs *xpcs)
{
	int ret;

	ret = xpcs_modify(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL1,
			  DW_VR_MII_DIG_CTRL1_2G5_EN |
			  DW_VR_MII_DIG_CTRL1_MAC_AUTO_SW,
			  DW_VR_MII_DIG_CTRL1_2G5_EN);
	if (ret < 0)
		return ret;

	return xpcs_modify(xpcs, MDIO_MMD_VEND2, MII_BMCR,
			   BMCR_ANENABLE | BMCR_SPEED1000 | BMCR_SPEED100,
			   BMCR_SPEED1000);
}

static int xpcs_do_config(struct dw_xpcs *xpcs, phy_interface_t interface,
			  const unsigned long *advertising,
			  unsigned int neg_mode)
{
	const struct dw_xpcs_compat *compat;
	int ret;

	compat = xpcs_find_compat(xpcs, interface);
	if (!compat)
		return -ENODEV;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		/* Wangxun devices need backplane CL37 AN enabled for
		 * SGMII and 1000base-X
		 */
		if (interface == PHY_INTERFACE_MODE_SGMII ||
		    interface == PHY_INTERFACE_MODE_1000BASEX)
			xpcs_write_vpcs(xpcs, DW_VR_XS_PCS_DIG_CTRL1,
					DW_CL37_BP | DW_EN_VSMMD1);
	}

	switch (compat->an_mode) {
	case DW_10GBASER:
		break;
	case DW_AN_C73:
		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
			ret = xpcs_config_aneg_c73(xpcs, compat);
			if (ret)
				return ret;
		}
		break;
	case DW_AN_C37_SGMII:
		ret = xpcs_config_aneg_c37_sgmii(xpcs, neg_mode);
		if (ret)
			return ret;
		break;
	case DW_AN_C37_1000BASEX:
		ret = xpcs_config_aneg_c37_1000basex(xpcs, neg_mode,
						     advertising);
		if (ret)
			return ret;
		break;
	case DW_2500BASEX:
		ret = xpcs_config_2500basex(xpcs);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	if (compat->pma_config) {
		ret = compat->pma_config(xpcs);
		if (ret)
			return ret;
	}

	return 0;
}

static int xpcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
		       phy_interface_t interface,
		       const unsigned long *advertising,
		       bool permit_pause_to_mac)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	return xpcs_do_config(xpcs, interface, advertising, neg_mode);
}

static int xpcs_get_state_c73(struct dw_xpcs *xpcs,
			      struct phylink_link_state *state,
			      const struct dw_xpcs_compat *compat)
{
	bool an_enabled;
	int pcs_stat1;
	int an_stat1;
	int ret;

	/* The link status bit is latching-low, so it is important to
	 * avoid unnecessary re-reads of this register to avoid missing
	 * a link-down event.
	 */
	pcs_stat1 = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT1);
	if (pcs_stat1 < 0) {
		state->link = false;
		return pcs_stat1;
	}

	/* Link needs to be read first ... */
	state->link = !!(pcs_stat1 & MDIO_STAT1_LSTATUS);

	/* ... and then we check the faults. */
	ret = xpcs_read_fault_c73(xpcs, state, pcs_stat1);
	if (ret) {
		ret = xpcs_soft_reset(xpcs, compat);
		if (ret)
			return ret;

		state->link = 0;

		return xpcs_do_config(xpcs, state->interface, NULL,
				      PHYLINK_PCS_NEG_INBAND_ENABLED);
	}

	/* There is no point doing anything else if the link is down. */
	if (!state->link)
		return 0;

	an_enabled = linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				       state->advertising);
	if (an_enabled) {
		/* The link status bit is latching-low, so it is important to
		 * avoid unnecessary re-reads of this register to avoid missing
		 * a link-down event.
		 */
		an_stat1 = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_STAT1);
		if (an_stat1 < 0) {
			state->link = false;
			return an_stat1;
		}

		state->an_complete = xpcs_aneg_done_c73(xpcs, state, compat,
							an_stat1);
		if (!state->an_complete) {
			state->link = false;
			return 0;
		}

		ret = xpcs_read_lpa_c73(xpcs, state, an_stat1);
		if (ret < 0) {
			state->link = false;
			return ret;
		}

		phylink_resolve_c73(state);
	} else {
		xpcs_resolve_pma(xpcs, state);
	}

	return 0;
}

static int xpcs_get_state_c37_sgmii(struct dw_xpcs *xpcs,
				    struct phylink_link_state *state)
{
	int ret;

	/* Reset link_state */
	state->link = false;
	state->speed = SPEED_UNKNOWN;
	state->duplex = DUPLEX_UNKNOWN;
	state->pause = 0;

	/* For C37 SGMII mode, we check DW_VR_MII_AN_INTR_STS for link
	 * status, speed and duplex.
	 */
	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS);
	if (ret < 0)
		return ret;

	if (ret & DW_VR_MII_C37_ANSGM_SP_LNKSTS) {
		int speed_value;

		state->link = true;

		speed_value = FIELD_GET(DW_VR_MII_AN_STS_C37_ANSGM_SP, ret);
		if (speed_value == DW_VR_MII_C37_ANSGM_SP_1000)
			state->speed = SPEED_1000;
		else if (speed_value == DW_VR_MII_C37_ANSGM_SP_100)
			state->speed = SPEED_100;
		else
			state->speed = SPEED_10;

		if (ret & DW_VR_MII_AN_STS_C37_ANSGM_FD)
			state->duplex = DUPLEX_FULL;
		else
			state->duplex = DUPLEX_HALF;
	} else if (ret == DW_VR_MII_AN_STS_C37_ANCMPLT_INTR) {
		int speed, duplex;

		state->link = true;

		speed = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_BMCR);
		if (speed < 0)
			return speed;

		speed &= BMCR_SPEED100 | BMCR_SPEED1000;
		if (speed == BMCR_SPEED1000)
			state->speed = SPEED_1000;
		else if (speed == BMCR_SPEED100)
			state->speed = SPEED_100;
		else if (speed == 0)
			state->speed = SPEED_10;

		duplex = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_ADVERTISE);
		if (duplex < 0)
			return duplex;

		if (duplex & ADVERTISE_1000XFULL)
			state->duplex = DUPLEX_FULL;
		else if (duplex & ADVERTISE_1000XHALF)
			state->duplex = DUPLEX_HALF;

		xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS, 0);
	}

	return 0;
}

static int xpcs_get_state_c37_1000basex(struct dw_xpcs *xpcs,
					unsigned int neg_mode,
					struct phylink_link_state *state)
{
	int lpa, bmsr;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			      state->advertising)) {
		/* Reset link state */
		state->link = false;

		lpa = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_LPA);
		if (lpa < 0 || lpa & LPA_RFAULT)
			return lpa;

		bmsr = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_BMSR);
		if (bmsr < 0)
			return bmsr;

		/* Clear AN complete interrupt */
		if (!xpcs->pcs.poll) {
			int an_intr;

			an_intr = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS);
			if (an_intr & DW_VR_MII_AN_STS_C37_ANCMPLT_INTR) {
				an_intr &= ~DW_VR_MII_AN_STS_C37_ANCMPLT_INTR;
				xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS, an_intr);
			}
		}

		phylink_mii_c22_pcs_decode_state(state, neg_mode, bmsr, lpa);
	}

	return 0;
}

static int xpcs_get_state_2500basex(struct dw_xpcs *xpcs,
				    struct phylink_link_state *state)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_BMSR);
	if (ret < 0) {
		state->link = 0;
		return ret;
	}

	state->link = !!(ret & BMSR_LSTATUS);
	if (!state->link)
		return 0;

	state->speed = SPEED_2500;
	state->pause |= MLO_PAUSE_TX | MLO_PAUSE_RX;
	state->duplex = DUPLEX_FULL;

	return 0;
}

static void xpcs_get_state(struct phylink_pcs *pcs, unsigned int neg_mode,
			   struct phylink_link_state *state)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);
	const struct dw_xpcs_compat *compat;
	int ret;

	compat = xpcs_find_compat(xpcs, state->interface);
	if (!compat)
		return;

	switch (compat->an_mode) {
	case DW_10GBASER:
		phylink_mii_c45_pcs_get_state(xpcs->mdiodev, state);
		break;
	case DW_AN_C73:
		ret = xpcs_get_state_c73(xpcs, state, compat);
		if (ret)
			dev_err(&xpcs->mdiodev->dev, "%s returned %pe\n",
				"xpcs_get_state_c73", ERR_PTR(ret));
		break;
	case DW_AN_C37_SGMII:
		ret = xpcs_get_state_c37_sgmii(xpcs, state);
		if (ret)
			dev_err(&xpcs->mdiodev->dev, "%s returned %pe\n",
				"xpcs_get_state_c37_sgmii", ERR_PTR(ret));
		break;
	case DW_AN_C37_1000BASEX:
		ret = xpcs_get_state_c37_1000basex(xpcs, neg_mode, state);
		if (ret)
			dev_err(&xpcs->mdiodev->dev, "%s returned %pe\n",
				"xpcs_get_state_c37_1000basex", ERR_PTR(ret));
		break;
	case DW_2500BASEX:
		ret = xpcs_get_state_2500basex(xpcs, state);
		if (ret)
			dev_err(&xpcs->mdiodev->dev, "%s returned %pe\n",
				"xpcs_get_state_2500basex", ERR_PTR(ret));
		break;
	default:
		return;
	}
}

static void xpcs_link_up_sgmii_1000basex(struct dw_xpcs *xpcs,
					 unsigned int neg_mode,
					 phy_interface_t interface,
					 int speed, int duplex)
{
	int ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	if (interface == PHY_INTERFACE_MODE_1000BASEX) {
		if (speed != SPEED_1000) {
			dev_err(&xpcs->mdiodev->dev,
				"%s: speed %dMbps not supported\n",
				__func__, speed);
			return;
		}

		if (duplex != DUPLEX_FULL)
			dev_err(&xpcs->mdiodev->dev,
				"%s: half duplex not supported\n",
				__func__);
	}

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MII_BMCR,
			 mii_bmcr_encode_fixed(speed, duplex));
	if (ret)
		dev_err(&xpcs->mdiodev->dev, "%s: xpcs_write returned %pe\n",
			__func__, ERR_PTR(ret));
}

static void xpcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			 phy_interface_t interface, int speed, int duplex)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	switch (interface) {
	case PHY_INTERFACE_MODE_USXGMII:
		xpcs_link_up_usxgmii(xpcs, speed);
		break;

	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		xpcs_link_up_sgmii_1000basex(xpcs, neg_mode, interface, speed,
					     duplex);
		break;

	default:
		break;
	}
}

static void xpcs_an_restart(struct phylink_pcs *pcs)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	xpcs_modify(xpcs, MDIO_MMD_VEND2, MII_BMCR, BMCR_ANRESTART,
		    BMCR_ANRESTART);
}

static int xpcs_config_eee(struct dw_xpcs *xpcs, bool enable)
{
	u16 mask, val;
	int ret;

	mask = DW_VR_MII_EEE_LTX_EN | DW_VR_MII_EEE_LRX_EN |
	       DW_VR_MII_EEE_TX_QUIET_EN | DW_VR_MII_EEE_RX_QUIET_EN |
	       DW_VR_MII_EEE_TX_EN_CTRL | DW_VR_MII_EEE_RX_EN_CTRL |
	       DW_VR_MII_EEE_MULT_FACT_100NS;

	if (enable)
		val = DW_VR_MII_EEE_LTX_EN | DW_VR_MII_EEE_LRX_EN |
		      DW_VR_MII_EEE_TX_QUIET_EN | DW_VR_MII_EEE_RX_QUIET_EN |
		      DW_VR_MII_EEE_TX_EN_CTRL | DW_VR_MII_EEE_RX_EN_CTRL |
		      FIELD_PREP(DW_VR_MII_EEE_MULT_FACT_100NS,
				 xpcs->eee_mult_fact);
	else
		val = 0;

	ret = xpcs_modify(xpcs, MDIO_MMD_VEND2, DW_VR_MII_EEE_MCTRL0, mask,
			  val);
	if (ret < 0)
		return ret;

	return xpcs_modify(xpcs, MDIO_MMD_VEND2, DW_VR_MII_EEE_MCTRL1,
			   DW_VR_MII_EEE_TRN_LPI,
			   enable ? DW_VR_MII_EEE_TRN_LPI : 0);
}

static void xpcs_disable_eee(struct phylink_pcs *pcs)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	xpcs_config_eee(xpcs, false);
}

static void xpcs_enable_eee(struct phylink_pcs *pcs)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	xpcs_config_eee(xpcs, true);
}

/**
 * xpcs_config_eee_mult_fact() - set the EEE clock multiplying factor
 * @xpcs: pointer to a &struct dw_xpcs instance
 * @mult_fact: the multiplying factor
 *
 * Configure the EEE clock multiplying factor. This value should be such that
 * clk_eee_time_period * (mult_fact + 1) is within the range 80 to 120ns.
 */
void xpcs_config_eee_mult_fact(struct dw_xpcs *xpcs, u8 mult_fact)
{
	xpcs->eee_mult_fact = mult_fact;
}
EXPORT_SYMBOL_GPL(xpcs_config_eee_mult_fact);

static int xpcs_read_ids(struct dw_xpcs *xpcs)
{
	int ret;
	u32 id;

	/* First, search C73 PCS using PCS MMD 3. Return ENODEV if communication
	 * failed indicating that device couldn't be reached.
	 */
	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MII_PHYSID1);
	if (ret < 0)
		return -ENODEV;

	id = ret << 16;

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MII_PHYSID2);
	if (ret < 0)
		return ret;

	id |= ret;

	/* If Device IDs are not all zeros or ones, then 10GBase-X/R or C73
	 * KR/KX4 PCS found. Otherwise fallback to detecting 1000Base-X or C37
	 * PCS in MII MMD 31.
	 */
	if (!id || id == 0xffffffff) {
		ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_PHYSID1);
		if (ret < 0)
			return ret;

		id = ret << 16;

		ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_PHYSID2);
		if (ret < 0)
			return ret;

		id |= ret;
	}

	/* Set the PCS ID if it hasn't been pre-initialized */
	if (xpcs->info.pcs == DW_XPCS_ID_NATIVE)
		xpcs->info.pcs = id;

	/* Find out PMA/PMD ID from MMD 1 device ID registers */
	ret = xpcs_read(xpcs, MDIO_MMD_PMAPMD, MDIO_DEVID1);
	if (ret < 0)
		return ret;

	id = ret;

	ret = xpcs_read(xpcs, MDIO_MMD_PMAPMD, MDIO_DEVID2);
	if (ret < 0)
		return ret;

	/* Note the inverted dword order and masked out Model/Revision numbers
	 * with respect to what is done with the PCS ID...
	 */
	ret = (ret >> 10) & 0x3F;
	id |= ret << 16;

	/* Set the PMA ID if it hasn't been pre-initialized */
	if (xpcs->info.pma == DW_XPCS_PMA_ID_NATIVE)
		xpcs->info.pma = id;

	return 0;
}

static const struct dw_xpcs_compat synopsys_xpcs_compat[] = {
	{
		.interface = PHY_INTERFACE_MODE_USXGMII,
		.supported = xpcs_usxgmii_features,
		.an_mode = DW_AN_C73,
	}, {
		.interface = PHY_INTERFACE_MODE_10GKR,
		.supported = xpcs_10gkr_features,
		.an_mode = DW_AN_C73,
	}, {
		.interface = PHY_INTERFACE_MODE_XLGMII,
		.supported = xpcs_xlgmii_features,
		.an_mode = DW_AN_C73,
	}, {
		.interface = PHY_INTERFACE_MODE_10GBASER,
		.supported = xpcs_10gbaser_features,
		.an_mode = DW_10GBASER,
	}, {
		.interface = PHY_INTERFACE_MODE_SGMII,
		.supported = xpcs_sgmii_features,
		.an_mode = DW_AN_C37_SGMII,
	}, {
		.interface = PHY_INTERFACE_MODE_1000BASEX,
		.supported = xpcs_1000basex_features,
		.an_mode = DW_AN_C37_1000BASEX,
	}, {
		.interface = PHY_INTERFACE_MODE_2500BASEX,
		.supported = xpcs_2500basex_features,
		.an_mode = DW_2500BASEX,
	}, {
	}
};

static const struct dw_xpcs_compat nxp_sja1105_xpcs_compat[] = {
	{
		.interface = PHY_INTERFACE_MODE_SGMII,
		.supported = xpcs_sgmii_features,
		.an_mode = DW_AN_C37_SGMII,
		.pma_config = nxp_sja1105_sgmii_pma_config,
	}, {
	}
};

static const struct dw_xpcs_compat nxp_sja1110_xpcs_compat[] = {
	{
		.interface = PHY_INTERFACE_MODE_SGMII,
		.supported = xpcs_sgmii_features,
		.an_mode = DW_AN_C37_SGMII,
		.pma_config = nxp_sja1110_sgmii_pma_config,
	}, {
		.interface = PHY_INTERFACE_MODE_2500BASEX,
		.supported = xpcs_2500basex_features,
		.an_mode = DW_2500BASEX,
		.pma_config = nxp_sja1110_2500basex_pma_config,
	}, {
	}
};

static const struct dw_xpcs_desc xpcs_desc_list[] = {
	{
		.id = DW_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = synopsys_xpcs_compat,
	}, {
		.id = NXP_SJA1105_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = nxp_sja1105_xpcs_compat,
	}, {
		.id = NXP_SJA1110_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = nxp_sja1110_xpcs_compat,
	},
};

static const struct phylink_pcs_ops xpcs_phylink_ops = {
	.pcs_validate = xpcs_validate,
	.pcs_inband_caps = xpcs_inband_caps,
	.pcs_pre_config = xpcs_pre_config,
	.pcs_config = xpcs_config,
	.pcs_get_state = xpcs_get_state,
	.pcs_an_restart = xpcs_an_restart,
	.pcs_link_up = xpcs_link_up,
	.pcs_disable_eee = xpcs_disable_eee,
	.pcs_enable_eee = xpcs_enable_eee,
};

static int xpcs_identify(struct dw_xpcs *xpcs)
{
	int i, ret;

	ret = xpcs_read_ids(xpcs);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(xpcs_desc_list); i++) {
		const struct dw_xpcs_desc *entry = &xpcs_desc_list[i];

		if ((xpcs->info.pcs & entry->mask) == entry->id) {
			xpcs->desc = entry;
			return 0;
		}
	}

	return -ENODEV;
}

static struct dw_xpcs *xpcs_create_data(struct mdio_device *mdiodev)
{
	struct dw_xpcs *xpcs;

	xpcs = kzalloc(sizeof(*xpcs), GFP_KERNEL);
	if (!xpcs)
		return ERR_PTR(-ENOMEM);

	mdio_device_get(mdiodev);
	xpcs->mdiodev = mdiodev;
	xpcs->pcs.ops = &xpcs_phylink_ops;
	xpcs->pcs.poll = true;

	return xpcs;
}

static void xpcs_free_data(struct dw_xpcs *xpcs)
{
	mdio_device_put(xpcs->mdiodev);
	kfree(xpcs);
}

static int xpcs_init_clks(struct dw_xpcs *xpcs)
{
	static const char *ids[DW_XPCS_NUM_CLKS] = {
		[DW_XPCS_CORE_CLK] = "core",
		[DW_XPCS_PAD_CLK] = "pad",
	};
	struct device *dev = &xpcs->mdiodev->dev;
	int ret, i;

	for (i = 0; i < DW_XPCS_NUM_CLKS; ++i)
		xpcs->clks[i].id = ids[i];

	ret = clk_bulk_get_optional(dev, DW_XPCS_NUM_CLKS, xpcs->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	ret = clk_bulk_prepare_enable(DW_XPCS_NUM_CLKS, xpcs->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable clocks\n");

	return 0;
}

static void xpcs_clear_clks(struct dw_xpcs *xpcs)
{
	clk_bulk_disable_unprepare(DW_XPCS_NUM_CLKS, xpcs->clks);

	clk_bulk_put(DW_XPCS_NUM_CLKS, xpcs->clks);
}

static int xpcs_init_id(struct dw_xpcs *xpcs)
{
	const struct dw_xpcs_info *info;

	info = dev_get_platdata(&xpcs->mdiodev->dev);
	if (!info) {
		xpcs->info.pcs = DW_XPCS_ID_NATIVE;
		xpcs->info.pma = DW_XPCS_PMA_ID_NATIVE;
	} else {
		xpcs->info = *info;
	}

	return xpcs_identify(xpcs);
}

static struct dw_xpcs *xpcs_create(struct mdio_device *mdiodev)
{
	struct dw_xpcs *xpcs;
	int ret;

	xpcs = xpcs_create_data(mdiodev);
	if (IS_ERR(xpcs))
		return xpcs;

	ret = xpcs_init_clks(xpcs);
	if (ret)
		goto out_free_data;

	ret = xpcs_init_id(xpcs);
	if (ret)
		goto out_clear_clks;

	xpcs_get_interfaces(xpcs, xpcs->pcs.supported_interfaces);

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID)
		xpcs->pcs.poll = false;
	else
		xpcs->need_reset = true;

	return xpcs;

out_clear_clks:
	xpcs_clear_clks(xpcs);

out_free_data:
	xpcs_free_data(xpcs);

	return ERR_PTR(ret);
}

/**
 * xpcs_create_mdiodev() - create a DW xPCS instance with the MDIO @addr
 * @bus: pointer to the MDIO-bus descriptor for the device to be looked at
 * @addr: device MDIO-bus ID
 *
 * Return: a pointer to the DW XPCS handle if successful, otherwise -ENODEV if
 * the PCS device couldn't be found on the bus and other negative errno related
 * to the data allocation and MDIO-bus communications.
 */
struct dw_xpcs *xpcs_create_mdiodev(struct mii_bus *bus, int addr)
{
	struct mdio_device *mdiodev;
	struct dw_xpcs *xpcs;

	mdiodev = mdio_device_create(bus, addr);
	if (IS_ERR(mdiodev))
		return ERR_CAST(mdiodev);

	xpcs = xpcs_create(mdiodev);

	/* xpcs_create() has taken a refcount on the mdiodev if it was
	 * successful. If xpcs_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * xpcs_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdiodev);

	return xpcs;
}
EXPORT_SYMBOL_GPL(xpcs_create_mdiodev);

struct phylink_pcs *xpcs_create_pcs_mdiodev(struct mii_bus *bus, int addr)
{
	struct dw_xpcs *xpcs;

	xpcs = xpcs_create_mdiodev(bus, addr);
	if (IS_ERR(xpcs))
		return ERR_CAST(xpcs);

	return &xpcs->pcs;
}
EXPORT_SYMBOL_GPL(xpcs_create_pcs_mdiodev);

/**
 * xpcs_create_fwnode() - Create a DW xPCS instance from @fwnode
 * @fwnode: fwnode handle poining to the DW XPCS device
 *
 * Return: a pointer to the DW XPCS handle if successful, otherwise -ENODEV if
 * the fwnode device is unavailable or the PCS device couldn't be found on the
 * bus, -EPROBE_DEFER if the respective MDIO-device instance couldn't be found,
 * other negative errno related to the data allocations and MDIO-bus
 * communications.
 */
struct dw_xpcs *xpcs_create_fwnode(struct fwnode_handle *fwnode)
{
	struct mdio_device *mdiodev;
	struct dw_xpcs *xpcs;

	if (!fwnode_device_is_available(fwnode))
		return ERR_PTR(-ENODEV);

	mdiodev = fwnode_mdio_find_device(fwnode);
	if (!mdiodev)
		return ERR_PTR(-EPROBE_DEFER);

	xpcs = xpcs_create(mdiodev);

	/* xpcs_create() has taken a refcount on the mdiodev if it was
	 * successful. If xpcs_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * xpcs_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdiodev);

	return xpcs;
}
EXPORT_SYMBOL_GPL(xpcs_create_fwnode);

void xpcs_destroy(struct dw_xpcs *xpcs)
{
	if (!xpcs)
		return;

	xpcs_clear_clks(xpcs);

	xpcs_free_data(xpcs);
}
EXPORT_SYMBOL_GPL(xpcs_destroy);

void xpcs_destroy_pcs(struct phylink_pcs *pcs)
{
	xpcs_destroy(phylink_pcs_to_xpcs(pcs));
}
EXPORT_SYMBOL_GPL(xpcs_destroy_pcs);

MODULE_DESCRIPTION("Synopsys DesignWare XPCS library");
MODULE_LICENSE("GPL v2");
