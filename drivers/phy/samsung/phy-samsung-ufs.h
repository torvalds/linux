/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UFS PHY driver for Samsung EXYNOS SoC
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd.
 * Author: Seungwon Jeon <essuuj@gmail.com>
 * Author: Alim Akhtar <alim.akhtar@samsung.com>
 *
 */
#ifndef _PHY_SAMSUNG_UFS_
#define _PHY_SAMSUNG_UFS_

#define PHY_COMN_BLK	1
#define PHY_TRSV_BLK	2
#define END_UFS_PHY_CFG { 0 }
#define PHY_TRSV_CH_OFFSET	0x30
#define PHY_APB_ADDR(off)	((off) << 2)

#define PHY_COMN_REG_CFG(o, v, d) {	\
	.off_0 = PHY_APB_ADDR((o)),	\
	.off_1 = 0,		\
	.val = (v),		\
	.desc = (d),		\
	.id = PHY_COMN_BLK,	\
}

#define PHY_TRSV_REG_CFG(o, v, d) {	\
	.off_0 = PHY_APB_ADDR((o)),	\
	.off_1 = PHY_APB_ADDR((o) + PHY_TRSV_CH_OFFSET),	\
	.val = (v),		\
	.desc = (d),		\
	.id = PHY_TRSV_BLK,	\
}

/* UFS PHY registers */
#define PHY_PLL_LOCK_STATUS	0x1e
#define PHY_CDR_LOCK_STATUS	0x5e

#define PHY_PLL_LOCK_BIT	BIT(5)
#define PHY_CDR_LOCK_BIT	BIT(4)

/* description for PHY calibration */
enum {
	/* applicable to any */
	PWR_DESC_ANY	= 0,
	/* mode */
	PWR_DESC_PWM	= 1,
	PWR_DESC_HS	= 2,
	/* series */
	PWR_DESC_SER_A	= 1,
	PWR_DESC_SER_B	= 2,
	/* gear */
	PWR_DESC_G1	= 1,
	PWR_DESC_G2	= 2,
	PWR_DESC_G3	= 3,
	/* field mask */
	MD_MASK		= 0x3,
	SR_MASK		= 0x3,
	GR_MASK		= 0x7,
};

#define PWR_MODE_HS_G1_ANY	PWR_MODE_HS(PWR_DESC_G1, PWR_DESC_ANY)
#define PWR_MODE_HS_G1_SER_A	PWR_MODE_HS(PWR_DESC_G1, PWR_DESC_SER_A)
#define PWR_MODE_HS_G1_SER_B	PWR_MODE_HS(PWR_DESC_G1, PWR_DESC_SER_B)
#define PWR_MODE_HS_G2_ANY	PWR_MODE_HS(PWR_DESC_G2, PWR_DESC_ANY)
#define PWR_MODE_HS_G2_SER_A	PWR_MODE_HS(PWR_DESC_G2, PWR_DESC_SER_A)
#define PWR_MODE_HS_G2_SER_B	PWR_MODE_HS(PWR_DESC_G2, PWR_DESC_SER_B)
#define PWR_MODE_HS_G3_ANY	PWR_MODE_HS(PWR_DESC_G3, PWR_DESC_ANY)
#define PWR_MODE_HS_G3_SER_A	PWR_MODE_HS(PWR_DESC_G3, PWR_DESC_SER_A)
#define PWR_MODE_HS_G3_SER_B	PWR_MODE_HS(PWR_DESC_G3, PWR_DESC_SER_B)
#define PWR_MODE(g, s, m)	((((g) & GR_MASK) << 4) |\
				 (((s) & SR_MASK) << 2) | ((m) & MD_MASK))
#define PWR_MODE_PWM_ANY	PWR_MODE(PWR_DESC_ANY,\
					 PWR_DESC_ANY, PWR_DESC_PWM)
#define PWR_MODE_HS(g, s)	((((g) & GR_MASK) << 4) |\
				 (((s) & SR_MASK) << 2) | PWR_DESC_HS)
#define PWR_MODE_HS_ANY		PWR_MODE(PWR_DESC_ANY,\
					 PWR_DESC_ANY, PWR_DESC_HS)
#define PWR_MODE_ANY		PWR_MODE(PWR_DESC_ANY,\
					 PWR_DESC_ANY, PWR_DESC_ANY)
/* PHY calibration point/state */
enum {
	CFG_PRE_INIT,
	CFG_POST_INIT,
	CFG_PRE_PWR_HS,
	CFG_POST_PWR_HS,
	CFG_TAG_MAX,
};

struct samsung_ufs_phy_cfg {
	u32 off_0;
	u32 off_1;
	u32 val;
	u8 desc;
	u8 id;
};

struct samsung_ufs_phy_drvdata {
	const struct samsung_ufs_phy_cfg **cfg;
	struct pmu_isol {
		u32 offset;
		u32 mask;
		u32 en;
	} isol;
	bool has_symbol_clk;
};

struct samsung_ufs_phy {
	struct device *dev;
	void __iomem *reg_pma;
	struct regmap *reg_pmu;
	struct clk *ref_clk;
	struct clk *ref_clk_parent;
	struct clk *tx0_symbol_clk;
	struct clk *rx0_symbol_clk;
	struct clk *rx1_symbol_clk;
	const struct samsung_ufs_phy_drvdata *drvdata;
	struct samsung_ufs_phy_cfg **cfg;
	const struct pmu_isol *isol;
	u8 lane_cnt;
	int ufs_phy_state;
	enum phy_mode mode;
};

static inline struct samsung_ufs_phy *get_samsung_ufs_phy(struct phy *phy)
{
	return (struct samsung_ufs_phy *)phy_get_drvdata(phy);
}

static inline void samsung_ufs_phy_ctrl_isol(
		struct samsung_ufs_phy *phy, u32 isol)
{
	regmap_update_bits(phy->reg_pmu, phy->isol->offset,
			   phy->isol->mask, isol ? 0 : phy->isol->en);
}

#include "phy-exynos7-ufs.h"

#endif /* _PHY_SAMSUNG_UFS_ */
