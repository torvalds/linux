// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS PHY driver data for Samsung EXYNOSAUTO v9 SoC
 *
 * Copyright (C) 2021 Samsung Electronics Co., Ltd.
 */

#include "phy-samsung-ufs.h"

#define EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CTRL		0x728
#define EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CTRL_MASK	0x1
#define EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CTRL_EN		BIT(0)
#define EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CDR_LOCK_STATUS	0x5e

#define PHY_TRSV_REG_CFG_AUTOV9(o, v, d) \
	PHY_TRSV_REG_CFG_OFFSET(o, v, d, 0x50)

/* Calibration for phy initialization */
static const struct samsung_ufs_phy_cfg exynosautov9_pre_init_cfg[] = {
	PHY_COMN_REG_CFG(0x023, 0x80, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x01d, 0x10, PWR_MODE_ANY),

	PHY_TRSV_REG_CFG_AUTOV9(0x044, 0xb5, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x04d, 0x43, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x05b, 0x20, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x05e, 0xc0, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x038, 0x12, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x059, 0x58, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x06c, 0x18, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x06d, 0x02, PWR_MODE_ANY),

	PHY_COMN_REG_CFG(0x023, 0xc0, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x023, 0x00, PWR_MODE_ANY),

	PHY_TRSV_REG_CFG_AUTOV9(0x042, 0x5d, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x043, 0x80, PWR_MODE_ANY),

	END_UFS_PHY_CFG,
};

/* Calibration for HS mode series A/B */
static const struct samsung_ufs_phy_cfg exynosautov9_pre_pwr_hs_cfg[] = {
	PHY_TRSV_REG_CFG_AUTOV9(0x032, 0xbc, PWR_MODE_HS_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x03c, 0x7f, PWR_MODE_HS_ANY),
	PHY_TRSV_REG_CFG_AUTOV9(0x048, 0xc0, PWR_MODE_HS_ANY),

	PHY_TRSV_REG_CFG_AUTOV9(0x04a, 0x00, PWR_MODE_HS_G3_SER_B),
	PHY_TRSV_REG_CFG_AUTOV9(0x04b, 0x10, PWR_MODE_HS_G1_SER_B |
				PWR_MODE_HS_G3_SER_B),
	PHY_TRSV_REG_CFG_AUTOV9(0x04d, 0x63, PWR_MODE_HS_G3_SER_B),

	END_UFS_PHY_CFG,
};

static const struct samsung_ufs_phy_cfg *exynosautov9_ufs_phy_cfgs[CFG_TAG_MAX] = {
	[CFG_PRE_INIT]		= exynosautov9_pre_init_cfg,
	[CFG_PRE_PWR_HS]	= exynosautov9_pre_pwr_hs_cfg,
};

static const char * const exynosautov9_ufs_phy_clks[] = {
	"ref_clk",
};

const struct samsung_ufs_phy_drvdata exynosautov9_ufs_phy = {
	.cfgs = exynosautov9_ufs_phy_cfgs,
	.isol = {
		.offset = EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CTRL,
		.mask = EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CTRL_MASK,
		.en = EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CTRL_EN,
	},
	.clk_list = exynosautov9_ufs_phy_clks,
	.num_clks = ARRAY_SIZE(exynosautov9_ufs_phy_clks),
	.cdr_lock_status_offset = EXYNOSAUTOV9_EMBEDDED_COMBO_PHY_CDR_LOCK_STATUS,
	.wait_for_cdr = samsung_ufs_phy_wait_for_lock_acq,
};
