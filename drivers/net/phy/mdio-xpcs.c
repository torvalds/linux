// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare XPCS helpers
 *
 * Author: Jose Abreu <Jose.Abreu@synopsys.com>
 */

#include <linux/delay.h>
#include <linux/mdio.h>
#include <linux/mdio-xpcs.h>
#include <linux/phylink.h>
#include <linux/workqueue.h>

#define SYNOPSYS_XPCS_USXGMII_ID	0x7996ced0
#define SYNOPSYS_XPCS_10GKR_ID		0x7996ced0
#define SYNOPSYS_XPCS_XLGMII_ID		0x7996ced0
#define SYNOPSYS_XPCS_MASK		0xffffffff

/* Vendor regs access */
#define DW_VENDOR			BIT(15)

/* VR_XS_PCS */
#define DW_USXGMII_RST			BIT(10)
#define DW_USXGMII_EN			BIT(9)
#define DW_VR_XS_PCS_DIG_STS		0x0010
#define DW_RXFIFO_ERR			GENMASK(6, 5)

/* SR_MII */
#define DW_USXGMII_FULL			BIT(8)
#define DW_USXGMII_SS_MASK		(BIT(13) | BIT(6) | BIT(5))
#define DW_USXGMII_10000		(BIT(13) | BIT(6))
#define DW_USXGMII_5000			(BIT(13) | BIT(5))
#define DW_USXGMII_2500			(BIT(5))
#define DW_USXGMII_1000			(BIT(6))
#define DW_USXGMII_100			(BIT(13))
#define DW_USXGMII_10			(0)

/* SR_AN */
#define DW_SR_AN_ADV1			0x10
#define DW_SR_AN_ADV2			0x11
#define DW_SR_AN_ADV3			0x12
#define DW_SR_AN_LP_ABL1		0x13
#define DW_SR_AN_LP_ABL2		0x14
#define DW_SR_AN_LP_ABL3		0x15

/* Clause 73 Defines */
/* AN_LP_ABL1 */
#define DW_C73_PAUSE			BIT(10)
#define DW_C73_ASYM_PAUSE		BIT(11)
#define DW_C73_AN_ADV_SF		0x1
/* AN_LP_ABL2 */
#define DW_C73_1000KX			BIT(5)
#define DW_C73_10000KX4			BIT(6)
#define DW_C73_10000KR			BIT(7)
/* AN_LP_ABL3 */
#define DW_C73_2500KX			BIT(0)
#define DW_C73_5000KR			BIT(1)

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

static const phy_interface_t xpcs_usxgmii_interfaces[] = {
	PHY_INTERFACE_MODE_USXGMII,
	PHY_INTERFACE_MODE_MAX,
};

static const phy_interface_t xpcs_10gkr_interfaces[] = {
	PHY_INTERFACE_MODE_10GKR,
	PHY_INTERFACE_MODE_MAX,
};

static const phy_interface_t xpcs_xlgmii_interfaces[] = {
	PHY_INTERFACE_MODE_XLGMII,
	PHY_INTERFACE_MODE_MAX,
};

static struct xpcs_id {
	u32 id;
	u32 mask;
	const int *supported;
	const phy_interface_t *interface;
} xpcs_id_list[] = {
	{
		.id = SYNOPSYS_XPCS_USXGMII_ID,
		.mask = SYNOPSYS_XPCS_MASK,
		.supported = xpcs_usxgmii_features,
		.interface = xpcs_usxgmii_interfaces,
	}, {
		.id = SYNOPSYS_XPCS_10GKR_ID,
		.mask = SYNOPSYS_XPCS_MASK,
		.supported = xpcs_10gkr_features,
		.interface = xpcs_10gkr_interfaces,
	}, {
		.id = SYNOPSYS_XPCS_XLGMII_ID,
		.mask = SYNOPSYS_XPCS_MASK,
		.supported = xpcs_xlgmii_features,
		.interface = xpcs_xlgmii_interfaces,
	},
};

static int xpcs_read(struct mdio_xpcs_args *xpcs, int dev, u32 reg)
{
	u32 reg_addr = MII_ADDR_C45 | dev << 16 | reg;

	return mdiobus_read(xpcs->bus, xpcs->addr, reg_addr);
}

static int xpcs_write(struct mdio_xpcs_args *xpcs, int dev, u32 reg, u16 val)
{
	u32 reg_addr = MII_ADDR_C45 | dev << 16 | reg;

	return mdiobus_write(xpcs->bus, xpcs->addr, reg_addr, val);
}

static int xpcs_read_vendor(struct mdio_xpcs_args *xpcs, int dev, u32 reg)
{
	return xpcs_read(xpcs, dev, DW_VENDOR | reg);
}

static int xpcs_write_vendor(struct mdio_xpcs_args *xpcs, int dev, int reg,
			     u16 val)
{
	return xpcs_write(xpcs, dev, DW_VENDOR | reg, val);
}

static int xpcs_read_vpcs(struct mdio_xpcs_args *xpcs, int reg)
{
	return xpcs_read_vendor(xpcs, MDIO_MMD_PCS, reg);
}

static int xpcs_write_vpcs(struct mdio_xpcs_args *xpcs, int reg, u16 val)
{
	return xpcs_write_vendor(xpcs, MDIO_MMD_PCS, reg, val);
}

static int xpcs_poll_reset(struct mdio_xpcs_args *xpcs, int dev)
{
	/* Poll until the reset bit clears (50ms per retry == 0.6 sec) */
	unsigned int retries = 12;
	int ret;

	do {
		msleep(50);
		ret = xpcs_read(xpcs, dev, MDIO_CTRL1);
		if (ret < 0)
			return ret;
	} while (ret & MDIO_CTRL1_RESET && --retries);

	return (ret & MDIO_CTRL1_RESET) ? -ETIMEDOUT : 0;
}

static int xpcs_soft_reset(struct mdio_xpcs_args *xpcs, int dev)
{
	int ret;

	ret = xpcs_write(xpcs, dev, MDIO_CTRL1, MDIO_CTRL1_RESET);
	if (ret < 0)
		return ret;

	return xpcs_poll_reset(xpcs, dev);
}

#define xpcs_warn(__xpcs, __state, __args...) \
({ \
	if ((__state)->link) \
		dev_warn(&(__xpcs)->bus->dev, ##__args); \
})

static int xpcs_read_fault(struct mdio_xpcs_args *xpcs,
			   struct phylink_link_state *state)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT1);
	if (ret < 0)
		return ret;

	if (ret & MDIO_STAT1_FAULT) {
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

static int xpcs_read_link(struct mdio_xpcs_args *xpcs, bool an)
{
	bool link = true;
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT1);
	if (ret < 0)
		return ret;

	if (!(ret & MDIO_STAT1_LSTATUS))
		link = false;

	if (an) {
		ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_STAT1);
		if (ret < 0)
			return ret;

		if (!(ret & MDIO_STAT1_LSTATUS))
			link = false;
	}

	return link;
}

static int xpcs_get_max_usxgmii_speed(const unsigned long *supported)
{
	int max = SPEED_UNKNOWN;

	if (phylink_test(supported, 1000baseKX_Full))
		max = SPEED_1000;
	if (phylink_test(supported, 2500baseX_Full))
		max = SPEED_2500;
	if (phylink_test(supported, 10000baseKX4_Full))
		max = SPEED_10000;
	if (phylink_test(supported, 10000baseKR_Full))
		max = SPEED_10000;

	return max;
}

static int xpcs_config_usxgmii(struct mdio_xpcs_args *xpcs, int speed)
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
		return -EINVAL;
	}

	ret = xpcs_read_vpcs(xpcs, MDIO_CTRL1);
	if (ret < 0)
		return ret;

	ret = xpcs_write_vpcs(xpcs, MDIO_CTRL1, ret | DW_USXGMII_EN);
	if (ret < 0)
		return ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1);
	if (ret < 0)
		return ret;

	ret &= ~DW_USXGMII_SS_MASK;
	ret |= speed_sel | DW_USXGMII_FULL;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1, ret);
	if (ret < 0)
		return ret;

	ret = xpcs_read_vpcs(xpcs, MDIO_CTRL1);
	if (ret < 0)
		return ret;

	return xpcs_write_vpcs(xpcs, MDIO_CTRL1, ret | DW_USXGMII_RST);
}

static int xpcs_config_aneg_c73(struct mdio_xpcs_args *xpcs)
{
	int ret, adv;

	/* By default, in USXGMII mode XPCS operates at 10G baud and
	 * replicates data to achieve lower speeds. Hereby, in this
	 * default configuration we need to advertise all supported
	 * modes and not only the ones we want to use.
	 */

	/* SR_AN_ADV3 */
	adv = 0;
	if (phylink_test(xpcs->supported, 2500baseX_Full))
		adv |= DW_C73_2500KX;

	/* TODO: 5000baseKR */

	ret = xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV3, adv);
	if (ret < 0)
		return ret;

	/* SR_AN_ADV2 */
	adv = 0;
	if (phylink_test(xpcs->supported, 1000baseKX_Full))
		adv |= DW_C73_1000KX;
	if (phylink_test(xpcs->supported, 10000baseKX4_Full))
		adv |= DW_C73_10000KX4;
	if (phylink_test(xpcs->supported, 10000baseKR_Full))
		adv |= DW_C73_10000KR;

	ret = xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV2, adv);
	if (ret < 0)
		return ret;

	/* SR_AN_ADV1 */
	adv = DW_C73_AN_ADV_SF;
	if (phylink_test(xpcs->supported, Pause))
		adv |= DW_C73_PAUSE;
	if (phylink_test(xpcs->supported, Asym_Pause))
		adv |= DW_C73_ASYM_PAUSE;

	return xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV1, adv);
}

static int xpcs_config_aneg(struct mdio_xpcs_args *xpcs)
{
	int ret;

	ret = xpcs_config_aneg_c73(xpcs);
	if (ret < 0)
		return ret;

	ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_CTRL1);
	if (ret < 0)
		return ret;

	ret |= MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART;

	return xpcs_write(xpcs, MDIO_MMD_AN, MDIO_CTRL1, ret);
}

static int xpcs_aneg_done(struct mdio_xpcs_args *xpcs,
			  struct phylink_link_state *state)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_STAT1);
	if (ret < 0)
		return ret;

	if (ret & MDIO_AN_STAT1_COMPLETE) {
		ret = xpcs_read(xpcs, MDIO_MMD_AN, DW_SR_AN_LP_ABL1);
		if (ret < 0)
			return ret;

		/* Check if Aneg outcome is valid */
		if (!(ret & DW_C73_AN_ADV_SF)) {
			xpcs_config_aneg(xpcs);
			return 0;
		}

		return 1;
	}

	return 0;
}

static int xpcs_read_lpa(struct mdio_xpcs_args *xpcs,
			 struct phylink_link_state *state)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_STAT1);
	if (ret < 0)
		return ret;

	if (!(ret & MDIO_AN_STAT1_LPABLE)) {
		phylink_clear(state->lp_advertising, Autoneg);
		return 0;
	}

	phylink_set(state->lp_advertising, Autoneg);

	/* Clause 73 outcome */
	ret = xpcs_read(xpcs, MDIO_MMD_AN, DW_SR_AN_LP_ABL3);
	if (ret < 0)
		return ret;

	if (ret & DW_C73_2500KX)
		phylink_set(state->lp_advertising, 2500baseX_Full);

	ret = xpcs_read(xpcs, MDIO_MMD_AN, DW_SR_AN_LP_ABL2);
	if (ret < 0)
		return ret;

	if (ret & DW_C73_1000KX)
		phylink_set(state->lp_advertising, 1000baseKX_Full);
	if (ret & DW_C73_10000KX4)
		phylink_set(state->lp_advertising, 10000baseKX4_Full);
	if (ret & DW_C73_10000KR)
		phylink_set(state->lp_advertising, 10000baseKR_Full);

	ret = xpcs_read(xpcs, MDIO_MMD_AN, DW_SR_AN_LP_ABL1);
	if (ret < 0)
		return ret;

	if (ret & DW_C73_PAUSE)
		phylink_set(state->lp_advertising, Pause);
	if (ret & DW_C73_ASYM_PAUSE)
		phylink_set(state->lp_advertising, Asym_Pause);

	linkmode_and(state->lp_advertising, state->lp_advertising,
		     state->advertising);
	return 0;
}

static void xpcs_resolve_lpa(struct mdio_xpcs_args *xpcs,
			     struct phylink_link_state *state)
{
	int max_speed = xpcs_get_max_usxgmii_speed(state->lp_advertising);

	state->pause = MLO_PAUSE_TX | MLO_PAUSE_RX;
	state->speed = max_speed;
	state->duplex = DUPLEX_FULL;
}

static int xpcs_get_max_xlgmii_speed(struct mdio_xpcs_args *xpcs,
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

static void xpcs_resolve_pma(struct mdio_xpcs_args *xpcs,
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

static int xpcs_validate(struct mdio_xpcs_args *xpcs,
			 unsigned long *supported,
			 struct phylink_link_state *state)
{
	linkmode_and(supported, supported, xpcs->supported);
	linkmode_and(state->advertising, state->advertising, xpcs->supported);
	return 0;
}

static int xpcs_config(struct mdio_xpcs_args *xpcs,
		       const struct phylink_link_state *state)
{
	int ret;

	if (state->an_enabled) {
		ret = xpcs_config_aneg(xpcs);
		if (ret)
			return ret;
	}

	return 0;
}

static int xpcs_get_state(struct mdio_xpcs_args *xpcs,
			  struct phylink_link_state *state)
{
	int ret;

	/* Link needs to be read first ... */
	state->link = xpcs_read_link(xpcs, state->an_enabled) > 0 ? 1 : 0;

	/* ... and then we check the faults. */
	ret = xpcs_read_fault(xpcs, state);
	if (ret) {
		ret = xpcs_soft_reset(xpcs, MDIO_MMD_PCS);
		if (ret)
			return ret;

		state->link = 0;

		return xpcs_config(xpcs, state);
	}

	if (state->an_enabled && xpcs_aneg_done(xpcs, state)) {
		state->an_complete = true;
		xpcs_read_lpa(xpcs, state);
		xpcs_resolve_lpa(xpcs, state);
	} else if (state->an_enabled) {
		state->link = 0;
	} else if (state->link) {
		xpcs_resolve_pma(xpcs, state);
	}

	return 0;
}

static int xpcs_link_up(struct mdio_xpcs_args *xpcs, int speed,
			phy_interface_t interface)
{
	if (interface == PHY_INTERFACE_MODE_USXGMII)
		return xpcs_config_usxgmii(xpcs, speed);

	return 0;
}

static u32 xpcs_get_id(struct mdio_xpcs_args *xpcs)
{
	int ret;
	u32 id;

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MII_PHYSID1);
	if (ret < 0)
		return 0xffffffff;

	id = ret << 16;

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MII_PHYSID2);
	if (ret < 0)
		return 0xffffffff;

	return id | ret;
}

static bool xpcs_check_features(struct mdio_xpcs_args *xpcs,
				struct xpcs_id *match,
				phy_interface_t interface)
{
	int i;

	for (i = 0; match->interface[i] != PHY_INTERFACE_MODE_MAX; i++) {
		if (match->interface[i] == interface)
			break;
	}

	if (match->interface[i] == PHY_INTERFACE_MODE_MAX)
		return false;

	for (i = 0; match->supported[i] != __ETHTOOL_LINK_MODE_MASK_NBITS; i++)
		set_bit(match->supported[i], xpcs->supported);

	return true;
}

static int xpcs_probe(struct mdio_xpcs_args *xpcs, phy_interface_t interface)
{
	u32 xpcs_id = xpcs_get_id(xpcs);
	struct xpcs_id *match = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(xpcs_id_list); i++) {
		struct xpcs_id *entry = &xpcs_id_list[i];

		if ((xpcs_id & entry->mask) == entry->id) {
			match = entry;

			if (xpcs_check_features(xpcs, match, interface))
				return xpcs_soft_reset(xpcs, MDIO_MMD_PCS);
		}
	}

	return -ENODEV;
}

static struct mdio_xpcs_ops xpcs_ops = {
	.validate = xpcs_validate,
	.config = xpcs_config,
	.get_state = xpcs_get_state,
	.link_up = xpcs_link_up,
	.probe = xpcs_probe,
};

struct mdio_xpcs_ops *mdio_xpcs_get_ops(void)
{
	return &xpcs_ops;
}
EXPORT_SYMBOL_GPL(mdio_xpcs_get_ops);

MODULE_LICENSE("GPL v2");
