// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2021-2022 NXP. */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#define LYNX_28G_NUM_LANE			8
#define LYNX_28G_NUM_PLL			2

#define LNa_PCC_OFFSET(lane)			(4 * (LYNX_28G_NUM_LANE - (lane->id) - 1))

/* General registers per SerDes block */
#define PCC8					0x10a0
#define PCC8_SGMIInCFG(lane, x)			(((x) & GENMASK(2, 0)) << LNa_PCC_OFFSET(lane))
#define PCC8_SGMIInCFG_EN(lane)			PCC8_SGMIInCFG(lane, 1)
#define PCC8_SGMIInCFG_MSK(lane)		PCC8_SGMIInCFG(lane, GENMASK(2, 0))
#define PCC8_SGMIIn_KX(lane, x)			((((x) << 3) & BIT(3)) << LNa_PCC_OFFSET(lane))
#define PCC8_SGMIIn_KX_MSK(lane)		PCC8_SGMIIn_KX(lane, 1)
#define PCC8_MSK(lane)				PCC8_SGMIInCFG_MSK(lane) | \
						PCC8_SGMIIn_KX_MSK(lane)

#define PCCC					0x10b0
#define PCCC_SXGMIInCFG(lane, x)		(((x) & GENMASK(2, 0)) << LNa_PCC_OFFSET(lane))
#define PCCC_SXGMIInCFG_EN(lane)		PCCC_SXGMIInCFG(lane, 1)
#define PCCC_SXGMIInCFG_MSK(lane)		PCCC_SXGMIInCFG(lane, GENMASK(2, 0))
#define PCCC_SXGMIInCFG_XFI(lane, x)		((((x) << 3) & BIT(3)) << LNa_PCC_OFFSET(lane))
#define PCCC_SXGMIInCFG_XFI_MSK(lane)		PCCC_SXGMIInCFG_XFI(lane, 1)
#define PCCC_MSK(lane)				PCCC_SXGMIInCFG_MSK(lane) | \
						PCCC_SXGMIInCFG_XFI_MSK(lane)

#define PCCD					0x10b4
#define PCCD_E25GnCFG(lane, x)			(((x) & GENMASK(2, 0)) << LNa_PCCD_OFFSET(lane))
#define PCCD_E25GnCFG_EN(lane)			PCCD_E25GnCFG(lane, 1)
#define PCCD_E25GnCFG_MSK(lane)			PCCD_E25GnCFG(lane, GENMASK(2, 0))
#define PCCD_MSK(lane)				PCCD_E25GnCFG_MSK(lane)

/* Per PLL registers */
#define PLLnRSTCTL(pll)				(0x400 + (pll) * 0x100 + 0x0)
#define PLLnRSTCTL_DIS(rstctl)			(((rstctl) & BIT(24)) >> 24)
#define PLLnRSTCTL_LOCK(rstctl)			(((rstctl) & BIT(23)) >> 23)

#define PLLnCR0(pll)				(0x400 + (pll) * 0x100 + 0x4)
#define PLLnCR0_REFCLK_SEL			GENMASK(20, 16)
#define PLLnCR0_REFCLK_SEL_100MHZ		0x0
#define PLLnCR0_REFCLK_SEL_125MHZ		0x1
#define PLLnCR0_REFCLK_SEL_156MHZ		0x2
#define PLLnCR0_REFCLK_SEL_150MHZ		0x3
#define PLLnCR0_REFCLK_SEL_161MHZ		0x4

#define PLLnCR1(pll)				(0x400 + (pll) * 0x100 + 0x8)
#define PLLnCR1_FRATE_SEL			GENMASK(28, 24)
#define PLLnCR1_FRATE_5G_10GVCO			0x0
#define PLLnCR1_FRATE_5G_25GVCO			0x10
#define PLLnCR1_FRATE_10G_20GVCO		0x6

/* Per SerDes lane registers */
/* Lane a General Control Register */
#define LNaGCR0(lane)				(0x800 + (lane) * 0x100 + 0x0)
#define LNaGCR0_PROTO_SEL			GENMASK(7, 3)
#define LNaGCR0_PROTO_SEL_SGMII			0x1
#define LNaGCR0_PROTO_SEL_XFI			0xa
#define LNaGCR0_IF_WIDTH			GENMASK(2, 0)
#define LNaGCR0_IF_WIDTH_10_BIT			0x0
#define LNaGCR0_IF_WIDTH_20_BIT			0x2

/* Lane a Tx Reset Control Register */
#define LNaTRSTCTL(lane)			(0x800 + (lane) * 0x100 + 0x20)
#define LNaTRSTCTL_HLT_REQ			BIT(27)
#define LNaTRSTCTL_RST_DONE			BIT(30)
#define LNaTRSTCTL_RST_REQ			BIT(31)

/* Lane a Tx General Control Register */
#define LNaTGCR0(lane)				(0x800 + (lane) * 0x100 + 0x24)
#define LNaTGCR0_USE_PLL			BIT(28)
#define LNaTGCR0_USE_PLLF			0x0
#define LNaTGCR0_USE_PLLS			0x1
#define LNaTGCR0_N_RATE				GENMASK(26, 24)
#define LNaTGCR0_N_RATE_FULL			0x0
#define LNaTGCR0_N_RATE_HALF			0x1
#define LNaTGCR0_N_RATE_QUARTER			0x2

#define LNaTECR0(lane)				(0x800 + (lane) * 0x100 + 0x30)
#define LNaTECR0_EQ_TYPE			GENMASK(30, 28)
#define LNaTECR0_EQ_SGN_PREQ			BIT(23)
#define LNaTECR0_EQ_PREQ			GENMASK(19, 16)
#define LNaTECR0_EQ_SGN_POST1Q			BIT(15)
#define LNaTECR0_EQ_POST1Q			GENMASK(12, 8)
#define LNaTECR0_EQ_AMP_RED			GENMASK(5, 0)

/* Lane a Rx Reset Control Register */
#define LNaRRSTCTL(lane)			(0x800 + (lane) * 0x100 + 0x40)
#define LNaRRSTCTL_HLT_REQ			BIT(27)
#define LNaRRSTCTL_RST_DONE			BIT(30)
#define LNaRRSTCTL_RST_REQ			BIT(31)
#define LNaRRSTCTL_CDR_LOCK			BIT(12)

/* Lane a Rx General Control Register */
#define LNaRGCR0(lane)				(0x800 + (lane) * 0x100 + 0x44)
#define LNaRGCR0_USE_PLL			BIT(28)
#define LNaRGCR0_USE_PLLF			0x0
#define LNaRGCR0_USE_PLLS			0x1
#define LNaRGCR0_N_RATE				GENMASK(26, 24)
#define LNaRGCR0_N_RATE_FULL			0x0
#define LNaRGCR0_N_RATE_HALF			0x1
#define LNaRGCR0_N_RATE_QUARTER			0x2

#define LNaRGCR1(lane)				(0x800 + (lane) * 0x100 + 0x48)
#define LNaRGCR1_RX_ORD_ELECIDLE		BIT(31)
#define LNaRGCR1_DATA_LOST_FLT			BIT(30)
#define LNaRGCR1_DATA_LOST			BIT(29)
#define LNaRGCR1_IDLE_CONFIG			BIT(28)
#define LNaRGCR1_ENTER_IDLE_FLT_SEL		GENMASK(26, 24)
#define LNaRGCR1_EXIT_IDLE_FLT_SEL		GENMASK(22, 20)
#define LNaRGCR1_DATA_LOST_TH_SEL		GENMASK(18, 16)
#define LNaRGCR1_EXT_REC_CLK_SEL		GENMASK(10, 8)
#define LNaRGCR1_WAKE_TX_DIS			BIT(5)
#define LNaRGCR1_PHY_RDY			BIT(4)
#define LNaRGCR1_CHANGE_RX_CLK			BIT(3)
#define LNaRGCR1_PWR_MGT			GENMASK(2, 0)

#define LNaRECR0(lane)				(0x800 + (lane) * 0x100 + 0x50)
#define LNaRECR0_EQ_GAINK2_HF_OV_EN		BIT(31)
#define LNaRECR0_EQ_GAINK2_HF_OV		GENMASK(28, 24)
#define LNaRECR0_EQ_GAINK3_MF_OV_EN		BIT(23)
#define LNaRECR0_EQ_GAINK3_MF_OV		GENMASK(20, 16)
#define LNaRECR0_EQ_GAINK4_LF_OV_EN		BIT(7)
#define LNaRECR0_EQ_GAINK4_LF_DIS		BIT(6)
#define LNaRECR0_EQ_GAINK4_LF_OV		GENMASK(4, 0)

#define LNaRECR1(lane)				(0x800 + (lane) * 0x100 + 0x54)
#define LNaRECR1_EQ_BLW_OV_EN			BIT(31)
#define LNaRECR1_EQ_BLW_OV			GENMASK(28, 24)
#define LNaRECR1_EQ_OFFSET_OV_EN		BIT(23)
#define LNaRECR1_EQ_OFFSET_OV			GENMASK(21, 16)

#define LNaRECR2(lane)				(0x800 + (lane) * 0x100 + 0x58)
#define LNaRECR2_EQ_OFFSET_RNG_DBL		BIT(31)
#define LNaRECR2_EQ_BOOST			GENMASK(29, 28)
#define LNaRECR2_EQ_BLW_SEL			GENMASK(25, 24)
#define LNaRECR2_EQ_ZERO			GENMASK(17, 16)
#define LNaRECR2_EQ_IND				GENMASK(13, 12)
#define LNaRECR2_EQ_BIN_DATA_AVG_TC		GENMASK(5, 4)
#define LNaRECR2_SPARE_IN			GENMASK(1, 0)

#define LNaRSCCR0(lane)				(0x800 + (lane) * 0x100 + 0x74)
#define LNaRSCCR0_SMP_OFF_EN			BIT(31)
#define LNaRSCCR0_SMP_OFF_OV_EN			BIT(30)
#define LNaRSCCR0_SMP_MAN_OFF_EN		BIT(29)
#define LNaRSCCR0_SMP_OFF_RNG_OV_EN		BIT(27)
#define LNaRSCCR0_SMP_OFF_RNG_4X_OV		BIT(25)
#define LNaRSCCR0_SMP_OFF_RNG_2X_OV		BIT(24)
#define LNaRSCCR0_SMP_AUTOZ_PD			BIT(23)
#define LNaRSCCR0_SMP_AUTOZ_CTRL		GENMASK(19, 16)
#define LNaRSCCR0_SMP_AUTOZ_D1R			GENMASK(13, 12)
#define LNaRSCCR0_SMP_AUTOZ_D1F			GENMASK(9, 8)
#define LNaRSCCR0_SMP_AUTOZ_EG1R		GENMASK(5, 4)
#define LNaRSCCR0_SMP_AUTOZ_EG1F		GENMASK(1, 0)

#define LNaPSS(lane)				(0x1000 + (lane) * 0x4)
#define LNaPSS_TYPE				GENMASK(30, 24)
#define LNaPSS_TYPE_SGMII			0x4
#define LNaPSS_TYPE_XFI				0x28

#define SGMIIaCR1(lane)				(0x1804 + (lane) * 0x10)
#define SGMIIaCR1_SGPCS_EN			BIT(11)

enum lynx_28g_eq_type {
	EQ_TYPE_NO_EQ = 0,
	EQ_TYPE_2TAP = 1,
	EQ_TYPE_3TAP = 2,
};

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
	lynx_28g_rmw((lane)->priv, reg(lane->id), val, mask)
#define lynx_28g_lane_read(lane, reg)			\
	ioread32((lane)->priv->base + reg((lane)->id))
#define lynx_28g_lane_write(lane, reg, val)		\
	iowrite32(val, (lane)->priv->base + reg((lane)->id))
#define lynx_28g_pll_read(pll, reg)			\
	ioread32((pll)->priv->base + reg((pll)->id))

static bool lynx_28g_supports_interface(struct lynx_28g_priv *priv, int intf)
{
	int i;

	for (i = 0; i < LYNX_28G_NUM_PLL; i++) {
		if (PLLnRSTCTL_DIS(priv->pll[i].rstctl))
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

		if (PLLnRSTCTL_DIS(pll->rstctl))
			continue;

		if (test_bit(intf, pll->supported))
			return pll;
	}

	/* no pll supports requested mode, either caller forgot to check
	 * lynx_28g_supports_lane_mode, or this is a bug.
	 */
	dev_WARN_ONCE(priv->dev, 1, "no pll for interface %s\n", phy_modes(intf));
	return NULL;
}

static void lynx_28g_lane_set_nrate(struct lynx_28g_lane *lane,
				    struct lynx_28g_pll *pll,
				    phy_interface_t intf)
{
	switch (FIELD_GET(PLLnCR1_FRATE_SEL, pll->cr1)) {
	case PLLnCR1_FRATE_5G_10GVCO:
	case PLLnCR1_FRATE_5G_25GVCO:
		switch (intf) {
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_1000BASEX:
			lynx_28g_lane_rmw(lane, LNaTGCR0,
					  FIELD_PREP(LNaTGCR0_N_RATE, LNaTGCR0_N_RATE_QUARTER),
					  LNaTGCR0_N_RATE);
			lynx_28g_lane_rmw(lane, LNaRGCR0,
					  FIELD_PREP(LNaRGCR0_N_RATE, LNaRGCR0_N_RATE_QUARTER),
					  LNaRGCR0_N_RATE);
			break;
		default:
			break;
		}
		break;
	case PLLnCR1_FRATE_10G_20GVCO:
		switch (intf) {
		case PHY_INTERFACE_MODE_10GBASER:
		case PHY_INTERFACE_MODE_USXGMII:
			lynx_28g_lane_rmw(lane, LNaTGCR0,
					  FIELD_PREP(LNaTGCR0_N_RATE, LNaTGCR0_N_RATE_FULL),
					  LNaTGCR0_N_RATE);
			lynx_28g_lane_rmw(lane, LNaRGCR0,
					  FIELD_PREP(LNaRGCR0_N_RATE, LNaRGCR0_N_RATE_FULL),
					  LNaRGCR0_N_RATE);
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
		lynx_28g_lane_rmw(lane, LNaTGCR0,
				  FIELD_PREP(LNaTGCR0_USE_PLL, LNaTGCR0_USE_PLLF),
				  LNaTGCR0_USE_PLL);
		lynx_28g_lane_rmw(lane, LNaRGCR0,
				  FIELD_PREP(LNaRGCR0_USE_PLL, LNaRGCR0_USE_PLLF),
				  LNaRGCR0_USE_PLL);
	} else {
		lynx_28g_lane_rmw(lane, LNaTGCR0,
				  FIELD_PREP(LNaTGCR0_USE_PLL, LNaTGCR0_USE_PLLS),
				  LNaTGCR0_USE_PLL);
		lynx_28g_lane_rmw(lane, LNaRGCR0,
				  FIELD_PREP(LNaRGCR0_USE_PLL, LNaRGCR0_USE_PLLS),
				  LNaRGCR0_USE_PLL);
	}
}

static void lynx_28g_cleanup_lane(struct lynx_28g_lane *lane)
{
	struct lynx_28g_priv *priv = lane->priv;

	/* Cleanup the protocol configuration registers of the current protocol */
	switch (lane->interface) {
	case PHY_INTERFACE_MODE_10GBASER:
		/* Cleanup the protocol configuration registers */
		lynx_28g_rmw(priv, PCCC, 0, PCCC_MSK(lane));
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		/* Cleanup the protocol configuration registers */
		lynx_28g_rmw(priv, PCC8, 0, PCC8_MSK(lane));

		/* Disable the SGMII PCS */
		lynx_28g_lane_rmw(lane, SGMIIaCR1, 0, SGMIIaCR1_SGPCS_EN);

		break;
	default:
		break;
	}
}

static void lynx_28g_lane_set_sgmii(struct lynx_28g_lane *lane)
{
	struct lynx_28g_priv *priv = lane->priv;
	struct lynx_28g_pll *pll;

	lynx_28g_cleanup_lane(lane);

	/* Setup the lane to run in SGMII */
	lynx_28g_rmw(priv, PCC8, PCC8_SGMIInCFG_EN(lane), PCC8_MSK(lane));

	/* Setup the protocol select and SerDes parallel interface width */
	lynx_28g_lane_rmw(lane, LNaGCR0,
			  FIELD_PREP(LNaGCR0_PROTO_SEL, LNaGCR0_PROTO_SEL_SGMII) |
			  FIELD_PREP(LNaGCR0_IF_WIDTH, LNaGCR0_IF_WIDTH_10_BIT),
			  LNaGCR0_PROTO_SEL | LNaGCR0_IF_WIDTH);

	/* Find the PLL that works with this interface type */
	pll = lynx_28g_pll_get(priv, PHY_INTERFACE_MODE_SGMII);
	if (unlikely(pll == NULL))
		return;

	/* Switch to the PLL that works with this interface type */
	lynx_28g_lane_set_pll(lane, pll);

	/* Choose the portion of clock net to be used on this lane */
	lynx_28g_lane_set_nrate(lane, pll, PHY_INTERFACE_MODE_SGMII);

	/* Enable the SGMII PCS */
	lynx_28g_lane_rmw(lane, SGMIIaCR1, SGMIIaCR1_SGPCS_EN,
			  SGMIIaCR1_SGPCS_EN);

	/* Configure the appropriate equalization parameters for the protocol */
	lynx_28g_lane_write(lane, LNaTECR0,
			    LNaTECR0_EQ_SGN_PREQ | LNaTECR0_EQ_SGN_POST1Q |
			    FIELD_PREP(LNaTECR0_EQ_AMP_RED, 6));
	lynx_28g_lane_write(lane, LNaRGCR1,
			    FIELD_PREP(LNaRGCR1_ENTER_IDLE_FLT_SEL, 4) |
			    FIELD_PREP(LNaRGCR1_EXIT_IDLE_FLT_SEL, 3) |
			    LNaRGCR1_DATA_LOST_FLT);
	lynx_28g_lane_write(lane, LNaRECR0,
			    LNaRECR0_EQ_GAINK2_HF_OV_EN |
			    FIELD_PREP(LNaRECR0_EQ_GAINK2_HF_OV, 31) |
			    LNaRECR0_EQ_GAINK3_MF_OV_EN |
			    FIELD_PREP(LNaRECR0_EQ_GAINK3_MF_OV, 0));
	lynx_28g_lane_write(lane, LNaRECR1,
			    FIELD_PREP(LNaRECR1_EQ_OFFSET_OV, 31));
	lynx_28g_lane_write(lane, LNaRECR2, 0);
	lynx_28g_lane_write(lane, LNaRSCCR0, 0);
}

static void lynx_28g_lane_set_10gbaser(struct lynx_28g_lane *lane)
{
	struct lynx_28g_priv *priv = lane->priv;
	struct lynx_28g_pll *pll;

	lynx_28g_cleanup_lane(lane);

	/* Enable the SXGMII lane */
	lynx_28g_rmw(priv, PCCC, PCCC_SXGMIInCFG_EN(lane) |
		     PCCC_SXGMIInCFG_XFI(lane, 1), PCCC_MSK(lane));

	/* Setup the protocol select and SerDes parallel interface width */
	lynx_28g_lane_rmw(lane, LNaGCR0,
			  FIELD_PREP(LNaGCR0_PROTO_SEL, LNaGCR0_PROTO_SEL_XFI) |
			  FIELD_PREP(LNaGCR0_IF_WIDTH, LNaGCR0_IF_WIDTH_20_BIT),
			  LNaGCR0_PROTO_SEL | LNaGCR0_IF_WIDTH);

	/* Find the PLL that works with this interface type */
	pll = lynx_28g_pll_get(priv, PHY_INTERFACE_MODE_10GBASER);
	if (unlikely(pll == NULL))
		return;

	/* Switch to the PLL that works with this interface type */
	lynx_28g_lane_set_pll(lane, pll);

	/* Choose the portion of clock net to be used on this lane */
	lynx_28g_lane_set_nrate(lane, pll, PHY_INTERFACE_MODE_10GBASER);

	/* Disable the SGMII PCS */
	lynx_28g_lane_rmw(lane, SGMIIaCR1, 0, SGMIIaCR1_SGPCS_EN);

	/* Configure the appropriate equalization parameters for the protocol */
	lynx_28g_lane_write(lane, LNaTECR0,
			    FIELD_PREP(LNaTECR0_EQ_TYPE, EQ_TYPE_2TAP) |
			    LNaTECR0_EQ_SGN_PREQ |
			    FIELD_PREP(LNaTECR0_EQ_PREQ, 0) |
			    LNaTECR0_EQ_SGN_POST1Q |
			    FIELD_PREP(LNaTECR0_EQ_POST1Q, 3) |
			    FIELD_PREP(LNaTECR0_EQ_AMP_RED, 7));
	lynx_28g_lane_write(lane, LNaRGCR1, LNaRGCR1_IDLE_CONFIG);
	lynx_28g_lane_write(lane, LNaRECR0, 0);
	lynx_28g_lane_write(lane, LNaRECR1, FIELD_PREP(LNaRECR1_EQ_OFFSET_OV, 31));
	lynx_28g_lane_write(lane, LNaRECR2,
			    LNaRECR2_EQ_OFFSET_RNG_DBL |
			    FIELD_PREP(LNaRECR2_EQ_BLW_SEL, 1) |
			    FIELD_PREP(LNaRECR2_EQ_BIN_DATA_AVG_TC, 2));
	lynx_28g_lane_write(lane, LNaRSCCR0,
			    FIELD_PREP(LNaRSCCR0_SMP_AUTOZ_D1R, 2));
}

static int lynx_28g_power_off(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	u32 trstctl, rrstctl;

	if (!lane->powered_up)
		return 0;

	/* Issue a halt request */
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, LNaTRSTCTL_HLT_REQ,
			  LNaTRSTCTL_HLT_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_HLT_REQ,
			  LNaRRSTCTL_HLT_REQ);

	/* Wait until the halting process is complete */
	do {
		trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
		rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
	} while ((trstctl & LNaTRSTCTL_HLT_REQ) ||
		 (rrstctl & LNaRRSTCTL_HLT_REQ));

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
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, LNaTRSTCTL_RST_REQ,
			  LNaTRSTCTL_RST_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_RST_REQ,
			  LNaRRSTCTL_RST_REQ);

	/* Wait until the reset sequence is completed */
	do {
		trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
		rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
	} while (!(trstctl & LNaTRSTCTL_RST_DONE) ||
		 !(rrstctl & LNaRRSTCTL_RST_DONE));

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

		if (PLLnRSTCTL_DIS(pll->rstctl))
			continue;

		switch (FIELD_GET(PLLnCR1_FRATE_SEL, pll->cr1)) {
		case PLLnCR1_FRATE_5G_10GVCO:
		case PLLnCR1_FRATE_5G_25GVCO:
			/* 5GHz clock net */
			__set_bit(PHY_INTERFACE_MODE_1000BASEX, pll->supported);
			__set_bit(PHY_INTERFACE_MODE_SGMII, pll->supported);
			break;
		case PLLnCR1_FRATE_10G_20GVCO:
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
		if (!(rrstctl & LNaRRSTCTL_CDR_LOCK)) {
			lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_RST_REQ,
					  LNaRRSTCTL_RST_REQ);
			do {
				rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
			} while (!(rrstctl & LNaRRSTCTL_RST_DONE));
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
	protocol = FIELD_GET(LNaPSS_TYPE, pss);
	switch (protocol) {
	case LNaPSS_TYPE_SGMII:
		lane->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	case LNaPSS_TYPE_XFI:
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
	int idx;

	if (args->args_count == 0)
		return of_phy_simple_xlate(dev, args);
	else if (args->args_count != 1)
		return ERR_PTR(-ENODEV);

	idx = args->args[0];

	if (WARN_ON(idx >= LYNX_28G_NUM_LANE))
		return ERR_PTR(-EINVAL);

	return priv->lane[idx].phy;
}

static int lynx_28g_probe_lane(struct lynx_28g_priv *priv, int id,
			       struct device_node *dn)
{
	struct lynx_28g_lane *lane = &priv->lane[id];
	struct phy *phy;

	phy = devm_phy_create(priv->dev, dn, &lynx_28g_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	lane->priv = priv;
	lane->phy = phy;
	lane->id = id;
	phy_set_drvdata(phy, lane);
	lynx_28g_lane_read_configuration(lane);

	return 0;
}

static int lynx_28g_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct lynx_28g_priv *priv;
	struct device_node *dn;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	lynx_28g_pll_read_configuration(priv);

	dn = dev_of_node(dev);
	if (of_get_child_count(dn)) {
		struct device_node *child;

		for_each_available_child_of_node(dn, child) {
			u32 reg;

			/* PHY subnode name must be 'phy'. */
			if (!(of_node_name_eq(child, "phy")))
				continue;

			if (of_property_read_u32(child, "reg", &reg)) {
				dev_err(dev, "No \"reg\" property for %pOF\n", child);
				of_node_put(child);
				return -EINVAL;
			}

			if (reg >= LYNX_28G_NUM_LANE) {
				dev_err(dev, "\"reg\" property out of range for %pOF\n", child);
				of_node_put(child);
				return -EINVAL;
			}

			err = lynx_28g_probe_lane(priv, reg, child);
			if (err) {
				of_node_put(child);
				return err;
			}
		}
	} else {
		for (int i = 0; i < LYNX_28G_NUM_LANE; i++) {
			err = lynx_28g_probe_lane(priv, i, NULL);
			if (err)
				return err;
		}
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
