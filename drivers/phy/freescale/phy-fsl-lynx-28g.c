// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2021-2022 NXP. */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#define LYNX_28G_NUM_LANE			8
#define LYNX_28G_NUM_PLL			2

/* General registers per SerDes block */
#define LYNX_28G_PCC8				0x10a0
#define LYNX_28G_PCC8_SGMII			0x1
#define LYNX_28G_PCC8_SGMII_DIS			0x0

#define LYNX_28G_PCCC				0x10b0
#define LYNX_28G_PCCC_10GBASER			0x9
#define LYNX_28G_PCCC_USXGMII			0x1
#define LYNX_28G_PCCC_SXGMII_DIS		0x0

#define LYNX_28G_LNa_PCC_OFFSET(lane)		(4 * (LYNX_28G_NUM_LANE - (lane->id) - 1))

/* Per PLL registers */
#define LYNX_28G_PLLnRSTCTL(pll)		(0x400 + (pll) * 0x100 + 0x0)
#define LYNX_28G_PLLnRSTCTL_DIS(rstctl)		(((rstctl) & BIT(24)) >> 24)
#define LYNX_28G_PLLnRSTCTL_LOCK(rstctl)	(((rstctl) & BIT(23)) >> 23)

#define LYNX_28G_PLLnCR0(pll)			(0x400 + (pll) * 0x100 + 0x4)
#define LYNX_28G_PLLnCR0_REFCLK_SEL(cr0)	(((cr0) & GENMASK(20, 16)))
#define LYNX_28G_PLLnCR0_REFCLK_SEL_100MHZ	0x0
#define LYNX_28G_PLLnCR0_REFCLK_SEL_125MHZ	0x10000
#define LYNX_28G_PLLnCR0_REFCLK_SEL_156MHZ	0x20000
#define LYNX_28G_PLLnCR0_REFCLK_SEL_150MHZ	0x30000
#define LYNX_28G_PLLnCR0_REFCLK_SEL_161MHZ	0x40000

#define LYNX_28G_PLLnCR1(pll)			(0x400 + (pll) * 0x100 + 0x8)
#define LYNX_28G_PLLnCR1_FRATE_SEL(cr1)		(((cr1) & GENMASK(28, 24)))
#define LYNX_28G_PLLnCR1_FRATE_5G_10GVCO	0x0
#define LYNX_28G_PLLnCR1_FRATE_5G_25GVCO	0x10000000
#define LYNX_28G_PLLnCR1_FRATE_10G_20GVCO	0x6000000

/* Per SerDes lane registers */
/* Lane a General Control Register */
#define LYNX_28G_LNaGCR0(lane)			(0x800 + (lane) * 0x100 + 0x0)
#define LYNX_28G_LNaGCR0_PROTO_SEL_MSK		GENMASK(7, 3)
#define LYNX_28G_LNaGCR0_PROTO_SEL_SGMII	0x8
#define LYNX_28G_LNaGCR0_PROTO_SEL_XFI		0x50
#define LYNX_28G_LNaGCR0_IF_WIDTH_MSK		GENMASK(2, 0)
#define LYNX_28G_LNaGCR0_IF_WIDTH_10_BIT	0x0
#define LYNX_28G_LNaGCR0_IF_WIDTH_20_BIT	0x2

/* Lane a Tx Reset Control Register */
#define LYNX_28G_LNaTRSTCTL(lane)		(0x800 + (lane) * 0x100 + 0x20)
#define LYNX_28G_LNaTRSTCTL_HLT_REQ		BIT(27)
#define LYNX_28G_LNaTRSTCTL_RST_DONE		BIT(30)
#define LYNX_28G_LNaTRSTCTL_RST_REQ		BIT(31)

/* Lane a Tx General Control Register */
#define LYNX_28G_LNaTGCR0(lane)			(0x800 + (lane) * 0x100 + 0x24)
#define LYNX_28G_LNaTGCR0_USE_PLLF		0x0
#define LYNX_28G_LNaTGCR0_USE_PLLS		BIT(28)
#define LYNX_28G_LNaTGCR0_USE_PLL_MSK		BIT(28)
#define LYNX_28G_LNaTGCR0_N_RATE_FULL		0x0
#define LYNX_28G_LNaTGCR0_N_RATE_HALF		0x1000000
#define LYNX_28G_LNaTGCR0_N_RATE_QUARTER	0x2000000
#define LYNX_28G_LNaTGCR0_N_RATE_MSK		GENMASK(26, 24)

#define LYNX_28G_LNaTECR0(lane)			(0x800 + (lane) * 0x100 + 0x30)

/* Lane a Rx Reset Control Register */
#define LYNX_28G_LNaRRSTCTL(lane)		(0x800 + (lane) * 0x100 + 0x40)
#define LYNX_28G_LNaRRSTCTL_HLT_REQ		BIT(27)
#define LYNX_28G_LNaRRSTCTL_RST_DONE		BIT(30)
#define LYNX_28G_LNaRRSTCTL_RST_REQ		BIT(31)
#define LYNX_28G_LNaRRSTCTL_CDR_LOCK		BIT(12)

/* Lane a Rx General Control Register */
#define LYNX_28G_LNaRGCR0(lane)			(0x800 + (lane) * 0x100 + 0x44)
#define LYNX_28G_LNaRGCR0_USE_PLLF		0x0
#define LYNX_28G_LNaRGCR0_USE_PLLS		BIT(28)
#define LYNX_28G_LNaRGCR0_USE_PLL_MSK		BIT(28)
#define LYNX_28G_LNaRGCR0_N_RATE_MSK		GENMASK(26, 24)
#define LYNX_28G_LNaRGCR0_N_RATE_FULL		0x0
#define LYNX_28G_LNaRGCR0_N_RATE_HALF		0x1000000
#define LYNX_28G_LNaRGCR0_N_RATE_QUARTER	0x2000000
#define LYNX_28G_LNaRGCR0_N_RATE_MSK		GENMASK(26, 24)

#define LYNX_28G_LNaRGCR1(lane)			(0x800 + (lane) * 0x100 + 0x48)

#define LYNX_28G_LNaRECR0(lane)			(0x800 + (lane) * 0x100 + 0x50)
#define LYNX_28G_LNaRECR1(lane)			(0x800 + (lane) * 0x100 + 0x54)
#define LYNX_28G_LNaRECR2(lane)			(0x800 + (lane) * 0x100 + 0x58)

#define LYNX_28G_LNaRSCCR0(lane)		(0x800 + (lane) * 0x100 + 0x74)

#define LYNX_28G_LNaPSS(lane)			(0x1000 + (lane) * 0x4)
#define LYNX_28G_LNaPSS_TYPE(pss)		(((pss) & GENMASK(30, 24)) >> 24)
#define LYNX_28G_LNaPSS_TYPE_SGMII		0x4
#define LYNX_28G_LNaPSS_TYPE_XFI		0x28

#define LYNX_28G_SGMIIaCR1(lane)		(0x1804 + (lane) * 0x10)
#define LYNX_28G_SGMIIaCR1_SGPCS_EN		BIT(11)
#define LYNX_28G_SGMIIaCR1_SGPCS_DIS		0x0
#define LYNX_28G_SGMIIaCR1_SGPCS_MSK		BIT(11)

struct lynx_28g_priv;

struct lynx_28g_pll {
	struct lynx_28g_priv *priv;
	u32 rstctl, cr0, cr1;
	int id;
	DECLARE_PHY_INTERFACE_MASK(supported);
};

struct lynx_28g_lane {
	struct lynx_28g_priv *priv;
	struct phy *phy;
	bool powered_up;
	bool init;
	unsigned int id;
	phy_interface_t interface;
};

struct lynx_28g_priv {
	void __iomem *base;
	struct device *dev;
	/* Serialize concurrent access to registers shared between lanes,
	 * like PCCn
	 */
	spinlock_t pcc_lock;
	struct lynx_28g_pll pll[LYNX_28G_NUM_PLL];
	struct lynx_28g_lane lane[LYNX_28G_NUM_LANE];

	struct delayed_work cdr_check;
};

static void lynx_28g_rmw(struct lynx_28g_priv *priv, unsigned long off,
			 u32 val, u32 mask)
{
	void __iomem *reg = priv->base + off;
	u32 orig, tmp;

	orig = ioread32(reg);
	tmp = orig & ~mask;
	tmp |= val;
	iowrite32(tmp, reg);
}

#define lynx_28g_lane_rmw(lane, reg, val, mask)	\
	lynx_28g_rmw((lane)->priv, LYNX_28G_##reg(lane->id), \
		     LYNX_28G_##reg##_##val, LYNX_28G_##reg##_##mask)
#define lynx_28g_lane_read(lane, reg)			\
	ioread32((lane)->priv->base + LYNX_28G_##reg((lane)->id))
#define lynx_28g_pll_read(pll, reg)			\
	ioread32((pll)->priv->base + LYNX_28G_##reg((pll)->id))

static bool lynx_28g_supports_interface(struct lynx_28g_priv *priv, int intf)
{
	int i;

	for (i = 0; i < LYNX_28G_NUM_PLL; i++) {
		if (LYNX_28G_PLLnRSTCTL_DIS(priv->pll[i].rstctl))
			continue;

		if (test_bit(intf, priv->pll[i].supported))
			return true;
	}

	return false;
}

static struct lynx_28g_pll *lynx_28g_pll_get(struct lynx_28g_priv *priv,
					     phy_interface_t intf)
{
	struct lynx_28g_pll *pll;
	int i;

	for (i = 0; i < LYNX_28G_NUM_PLL; i++) {
		pll = &priv->pll[i];

		if (LYNX_28G_PLLnRSTCTL_DIS(pll->rstctl))
			continue;

		if (test_bit(intf, pll->supported))
			return pll;
	}

	return NULL;
}

static void lynx_28g_lane_set_nrate(struct lynx_28g_lane *lane,
				    struct lynx_28g_pll *pll,
				    phy_interface_t intf)
{
	switch (LYNX_28G_PLLnCR1_FRATE_SEL(pll->cr1)) {
	case LYNX_28G_PLLnCR1_FRATE_5G_10GVCO:
	case LYNX_28G_PLLnCR1_FRATE_5G_25GVCO:
		switch (intf) {
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_1000BASEX:
			lynx_28g_lane_rmw(lane, LNaTGCR0, N_RATE_QUARTER, N_RATE_MSK);
			lynx_28g_lane_rmw(lane, LNaRGCR0, N_RATE_QUARTER, N_RATE_MSK);
			break;
		default:
			break;
		}
		break;
	case LYNX_28G_PLLnCR1_FRATE_10G_20GVCO:
		switch (intf) {
		case PHY_INTERFACE_MODE_10GBASER:
		case PHY_INTERFACE_MODE_USXGMII:
			lynx_28g_lane_rmw(lane, LNaTGCR0, N_RATE_FULL, N_RATE_MSK);
			lynx_28g_lane_rmw(lane, LNaRGCR0, N_RATE_FULL, N_RATE_MSK);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void lynx_28g_lane_set_pll(struct lynx_28g_lane *lane,
				  struct lynx_28g_pll *pll)
{
	if (pll->id == 0) {
		lynx_28g_lane_rmw(lane, LNaTGCR0, USE_PLLF, USE_PLL_MSK);
		lynx_28g_lane_rmw(lane, LNaRGCR0, USE_PLLF, USE_PLL_MSK);
	} else {
		lynx_28g_lane_rmw(lane, LNaTGCR0, USE_PLLS, USE_PLL_MSK);
		lynx_28g_lane_rmw(lane, LNaRGCR0, USE_PLLS, USE_PLL_MSK);
	}
}

static void lynx_28g_cleanup_lane(struct lynx_28g_lane *lane)
{
	u32 lane_offset = LYNX_28G_LNa_PCC_OFFSET(lane);
	struct lynx_28g_priv *priv = lane->priv;

	/* Cleanup the protocol configuration registers of the current protocol */
	switch (lane->interface) {
	case PHY_INTERFACE_MODE_10GBASER:
		lynx_28g_rmw(priv, LYNX_28G_PCCC,
			     LYNX_28G_PCCC_SXGMII_DIS << lane_offset,
			     GENMASK(3, 0) << lane_offset);
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		lynx_28g_rmw(priv, LYNX_28G_PCC8,
			     LYNX_28G_PCC8_SGMII_DIS << lane_offset,
			     GENMASK(3, 0) << lane_offset);
		break;
	default:
		break;
	}
}

static void lynx_28g_lane_set_sgmii(struct lynx_28g_lane *lane)
{
	u32 lane_offset = LYNX_28G_LNa_PCC_OFFSET(lane);
	struct lynx_28g_priv *priv = lane->priv;
	struct lynx_28g_pll *pll;

	lynx_28g_cleanup_lane(lane);

	/* Setup the lane to run in SGMII */
	lynx_28g_rmw(priv, LYNX_28G_PCC8,
		     LYNX_28G_PCC8_SGMII << lane_offset,
		     GENMASK(3, 0) << lane_offset);

	/* Setup the protocol select and SerDes parallel interface width */
	lynx_28g_lane_rmw(lane, LNaGCR0, PROTO_SEL_SGMII, PROTO_SEL_MSK);
	lynx_28g_lane_rmw(lane, LNaGCR0, IF_WIDTH_10_BIT, IF_WIDTH_MSK);

	/* Switch to the PLL that works with this interface type */
	pll = lynx_28g_pll_get(priv, PHY_INTERFACE_MODE_SGMII);
	lynx_28g_lane_set_pll(lane, pll);

	/* Choose the portion of clock net to be used on this lane */
	lynx_28g_lane_set_nrate(lane, pll, PHY_INTERFACE_MODE_SGMII);

	/* Enable the SGMII PCS */
	lynx_28g_lane_rmw(lane, SGMIIaCR1, SGPCS_EN, SGPCS_MSK);

	/* Configure the appropriate equalization parameters for the protocol */
	iowrite32(0x00808006, priv->base + LYNX_28G_LNaTECR0(lane->id));
	iowrite32(0x04310000, priv->base + LYNX_28G_LNaRGCR1(lane->id));
	iowrite32(0x9f800000, priv->base + LYNX_28G_LNaRECR0(lane->id));
	iowrite32(0x001f0000, priv->base + LYNX_28G_LNaRECR1(lane->id));
	iowrite32(0x00000000, priv->base + LYNX_28G_LNaRECR2(lane->id));
	iowrite32(0x00000000, priv->base + LYNX_28G_LNaRSCCR0(lane->id));
}

static void lynx_28g_lane_set_10gbaser(struct lynx_28g_lane *lane)
{
	u32 lane_offset = LYNX_28G_LNa_PCC_OFFSET(lane);
	struct lynx_28g_priv *priv = lane->priv;
	struct lynx_28g_pll *pll;

	lynx_28g_cleanup_lane(lane);

	/* Enable the SXGMII lane */
	lynx_28g_rmw(priv, LYNX_28G_PCCC,
		     LYNX_28G_PCCC_10GBASER << lane_offset,
		     GENMASK(3, 0) << lane_offset);

	/* Setup the protocol select and SerDes parallel interface width */
	lynx_28g_lane_rmw(lane, LNaGCR0, PROTO_SEL_XFI, PROTO_SEL_MSK);
	lynx_28g_lane_rmw(lane, LNaGCR0, IF_WIDTH_20_BIT, IF_WIDTH_MSK);

	/* Switch to the PLL that works with this interface type */
	pll = lynx_28g_pll_get(priv, PHY_INTERFACE_MODE_10GBASER);
	lynx_28g_lane_set_pll(lane, pll);

	/* Choose the portion of clock net to be used on this lane */
	lynx_28g_lane_set_nrate(lane, pll, PHY_INTERFACE_MODE_10GBASER);

	/* Disable the SGMII PCS */
	lynx_28g_lane_rmw(lane, SGMIIaCR1, SGPCS_DIS, SGPCS_MSK);

	/* Configure the appropriate equalization parameters for the protocol */
	iowrite32(0x10808307, priv->base + LYNX_28G_LNaTECR0(lane->id));
	iowrite32(0x10000000, priv->base + LYNX_28G_LNaRGCR1(lane->id));
	iowrite32(0x00000000, priv->base + LYNX_28G_LNaRECR0(lane->id));
	iowrite32(0x001f0000, priv->base + LYNX_28G_LNaRECR1(lane->id));
	iowrite32(0x81000020, priv->base + LYNX_28G_LNaRECR2(lane->id));
	iowrite32(0x00002000, priv->base + LYNX_28G_LNaRSCCR0(lane->id));
}

static int lynx_28g_power_off(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	u32 trstctl, rrstctl;

	if (!lane->powered_up)
		return 0;

	/* Issue a halt request */
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, HLT_REQ, HLT_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, HLT_REQ, HLT_REQ);

	/* Wait until the halting process is complete */
	do {
		trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
		rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
	} while ((trstctl & LYNX_28G_LNaTRSTCTL_HLT_REQ) ||
		 (rrstctl & LYNX_28G_LNaRRSTCTL_HLT_REQ));

	lane->powered_up = false;

	return 0;
}

static int lynx_28g_power_on(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	u32 trstctl, rrstctl;

	if (lane->powered_up)
		return 0;

	/* Issue a reset request on the lane */
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, RST_REQ, RST_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, RST_REQ, RST_REQ);

	/* Wait until the reset sequence is completed */
	do {
		trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
		rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
	} while (!(trstctl & LYNX_28G_LNaTRSTCTL_RST_DONE) ||
		 !(rrstctl & LYNX_28G_LNaRRSTCTL_RST_DONE));

	lane->powered_up = true;

	return 0;
}

static int lynx_28g_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	struct lynx_28g_priv *priv = lane->priv;
	int powered_up = lane->powered_up;
	int err = 0;

	if (mode != PHY_MODE_ETHERNET)
		return -EOPNOTSUPP;

	if (lane->interface == PHY_INTERFACE_MODE_NA)
		return -EOPNOTSUPP;

	if (!lynx_28g_supports_interface(priv, submode))
		return -EOPNOTSUPP;

	/* If the lane is powered up, put the lane into the halt state while
	 * the reconfiguration is being done.
	 */
	if (powered_up)
		lynx_28g_power_off(phy);

	spin_lock(&priv->pcc_lock);

	switch (submode) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		lynx_28g_lane_set_sgmii(lane);
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		lynx_28g_lane_set_10gbaser(lane);
		break;
	default:
		err = -EOPNOTSUPP;
		goto out;
	}

	lane->interface = submode;

out:
	spin_unlock(&priv->pcc_lock);

	/* Power up the lane if necessary */
	if (powered_up)
		lynx_28g_power_on(phy);

	return err;
}

static int lynx_28g_validate(struct phy *phy, enum phy_mode mode, int submode,
			     union phy_configure_opts *opts __always_unused)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	struct lynx_28g_priv *priv = lane->priv;

	if (mode != PHY_MODE_ETHERNET)
		return -EOPNOTSUPP;

	if (!lynx_28g_supports_interface(priv, submode))
		return -EOPNOTSUPP;

	return 0;
}

static int lynx_28g_init(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);

	/* Mark the fact that the lane was init */
	lane->init = true;

	/* SerDes lanes are powered on at boot time.  Any lane that is managed
	 * by this driver will get powered down at init time aka at dpaa2-eth
	 * probe time.
	 */
	lane->powered_up = true;
	lynx_28g_power_off(phy);

	return 0;
}

static const struct phy_ops lynx_28g_ops = {
	.init		= lynx_28g_init,
	.power_on	= lynx_28g_power_on,
	.power_off	= lynx_28g_power_off,
	.set_mode	= lynx_28g_set_mode,
	.validate	= lynx_28g_validate,
	.owner		= THIS_MODULE,
};

static void lynx_28g_pll_read_configuration(struct lynx_28g_priv *priv)
{
	struct lynx_28g_pll *pll;
	int i;

	for (i = 0; i < LYNX_28G_NUM_PLL; i++) {
		pll = &priv->pll[i];
		pll->priv = priv;
		pll->id = i;

		pll->rstctl = lynx_28g_pll_read(pll, PLLnRSTCTL);
		pll->cr0 = lynx_28g_pll_read(pll, PLLnCR0);
		pll->cr1 = lynx_28g_pll_read(pll, PLLnCR1);

		if (LYNX_28G_PLLnRSTCTL_DIS(pll->rstctl))
			continue;

		switch (LYNX_28G_PLLnCR1_FRATE_SEL(pll->cr1)) {
		case LYNX_28G_PLLnCR1_FRATE_5G_10GVCO:
		case LYNX_28G_PLLnCR1_FRATE_5G_25GVCO:
			/* 5GHz clock net */
			__set_bit(PHY_INTERFACE_MODE_1000BASEX, pll->supported);
			__set_bit(PHY_INTERFACE_MODE_SGMII, pll->supported);
			break;
		case LYNX_28G_PLLnCR1_FRATE_10G_20GVCO:
			/* 10.3125GHz clock net */
			__set_bit(PHY_INTERFACE_MODE_10GBASER, pll->supported);
			break;
		default:
			/* 6GHz, 12.890625GHz, 8GHz */
			break;
		}
	}
}

#define work_to_lynx(w) container_of((w), struct lynx_28g_priv, cdr_check.work)

static void lynx_28g_cdr_lock_check(struct work_struct *work)
{
	struct lynx_28g_priv *priv = work_to_lynx(work);
	struct lynx_28g_lane *lane;
	u32 rrstctl;
	int i;

	for (i = 0; i < LYNX_28G_NUM_LANE; i++) {
		lane = &priv->lane[i];

		mutex_lock(&lane->phy->mutex);

		if (!lane->init || !lane->powered_up) {
			mutex_unlock(&lane->phy->mutex);
			continue;
		}

		rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
		if (!(rrstctl & LYNX_28G_LNaRRSTCTL_CDR_LOCK)) {
			lynx_28g_lane_rmw(lane, LNaRRSTCTL, RST_REQ, RST_REQ);
			do {
				rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
			} while (!(rrstctl & LYNX_28G_LNaRRSTCTL_RST_DONE));
		}

		mutex_unlock(&lane->phy->mutex);
	}
	queue_delayed_work(system_power_efficient_wq, &priv->cdr_check,
			   msecs_to_jiffies(1000));
}

static void lynx_28g_lane_read_configuration(struct lynx_28g_lane *lane)
{
	u32 pss, protocol;

	pss = lynx_28g_lane_read(lane, LNaPSS);
	protocol = LYNX_28G_LNaPSS_TYPE(pss);
	switch (protocol) {
	case LYNX_28G_LNaPSS_TYPE_SGMII:
		lane->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	case LYNX_28G_LNaPSS_TYPE_XFI:
		lane->interface = PHY_INTERFACE_MODE_10GBASER;
		break;
	default:
		lane->interface = PHY_INTERFACE_MODE_NA;
	}
}

static struct phy *lynx_28g_xlate(struct device *dev,
				  const struct of_phandle_args *args)
{
	struct lynx_28g_priv *priv = dev_get_drvdata(dev);
	int idx = args->args[0];

	if (WARN_ON(idx >= LYNX_28G_NUM_LANE))
		return ERR_PTR(-EINVAL);

	return priv->lane[idx].phy;
}

static int lynx_28g_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct lynx_28g_priv *priv;
	int i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	lynx_28g_pll_read_configuration(priv);

	for (i = 0; i < LYNX_28G_NUM_LANE; i++) {
		struct lynx_28g_lane *lane = &priv->lane[i];
		struct phy *phy;

		memset(lane, 0, sizeof(*lane));

		phy = devm_phy_create(&pdev->dev, NULL, &lynx_28g_ops);
		if (IS_ERR(phy))
			return PTR_ERR(phy);

		lane->priv = priv;
		lane->phy = phy;
		lane->id = i;
		phy_set_drvdata(phy, lane);
		lynx_28g_lane_read_configuration(lane);
	}

	dev_set_drvdata(dev, priv);

	spin_lock_init(&priv->pcc_lock);
	INIT_DELAYED_WORK(&priv->cdr_check, lynx_28g_cdr_lock_check);

	queue_delayed_work(system_power_efficient_wq, &priv->cdr_check,
			   msecs_to_jiffies(1000));

	dev_set_drvdata(&pdev->dev, priv);
	provider = devm_of_phy_provider_register(&pdev->dev, lynx_28g_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static void lynx_28g_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lynx_28g_priv *priv = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&priv->cdr_check);
}

static const struct of_device_id lynx_28g_of_match_table[] = {
	{ .compatible = "fsl,lynx-28g" },
	{ },
};
MODULE_DEVICE_TABLE(of, lynx_28g_of_match_table);

static struct platform_driver lynx_28g_driver = {
	.probe = lynx_28g_probe,
	.remove = lynx_28g_remove,
	.driver = {
		.name = "lynx-28g",
		.of_match_table = lynx_28g_of_match_table,
	},
};
module_platform_driver(lynx_28g_driver);

MODULE_AUTHOR("Ioana Ciornei <ioana.ciornei@nxp.com>");
MODULE_DESCRIPTION("Lynx 28G SerDes PHY driver for Layerscape SoCs");
MODULE_LICENSE("GPL v2");
