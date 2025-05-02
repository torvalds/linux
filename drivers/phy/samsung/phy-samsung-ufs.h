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

#include <linux/phy/phy.h>
#include <linux/regmap.h>

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

#define PHY_TRSV_REG_CFG_OFFSET(o, v, d, c) {	\
	.off_0 = PHY_APB_ADDR((o)),	\
	.off_1 = PHY_APB_ADDR((o) + (c)),	\
	.val = (v),		\
	.desc = (d),		\
	.id = PHY_TRSV_BLK,	\
}

#define PHY_TRSV_REG_CFG(o, v, d)	\
	PHY_TRSV_REG_CFG_OFFSET(o, v, d, PHY_TRSV_CH_OFFSET)

/* UFS PHY registers */
#define PHY_PLL_LOCK_STATUS	0x1e

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

struct samsung_ufs_phy_pmu_isol {
	u32 offset;
	u32 mask;
	u32 en;
};

struct samsung_ufs_phy_drvdata {
	const struct samsung_ufs_phy_cfg **cfgs;
	struct samsung_ufs_phy_pmu_isol isol;
	const char * const *clk_list;
	int num_clks;
	u32 cdr_lock_status_offset;
	/* SoC's specific operations */
	int (*wait_for_cal)(struct phy *phy, u8 lane);
	int (*wait_for_cdr)(struct phy *phy, u8 lane);
};

struct samsung_ufs_phy {
	struct device *dev;
	void __iomem *reg_pma;
	struct regmap *reg_pmu;
	struct clk_bulk_data *clks;
	const struct samsung_ufs_phy_drvdata *drvdata;
	const struct samsung_ufs_phy_cfg * const *cfgs;
	struct samsung_ufs_phy_pmu_isol isol;
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
	regmap_update_bits(phy->reg_pmu, phy->isol.offset,
			   phy->isol.mask, isol ? 0 : phy->isol.en);
}

int samsung_ufs_phy_wait_for_lock_acq(struct phy *phy, u8 lane);
int exynosautov920_ufs_phy_wait_cdr_lock(struct phy *phy, u8 lane);
void samsung_ufs_phy_config(struct samsung_ufs_phy *phy,
			    const struct samsung_ufs_phy_cfg *cfg, u8 lane);

extern const struct samsung_ufs_phy_drvdata exynos7_ufs_phy;
extern const struct samsung_ufs_phy_drvdata exynosautov9_ufs_phy;
extern const struct samsung_ufs_phy_drvdata exynosautov920_ufs_phy;
extern const struct samsung_ufs_phy_drvdata fsd_ufs_phy;
extern const struct samsung_ufs_phy_drvdata tensor_gs101_ufs_phy;

#endif /* _PHY_SAMSUNG_UFS_ */
