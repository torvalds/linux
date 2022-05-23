/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __DRV_CLK_MTK_RESET_H
#define __DRV_CLK_MTK_RESET_H

#include <linux/reset-controller.h>
#include <linux/types.h>

/**
 * enum mtk_reset_version - Version of MediaTek clock reset controller.
 * @MTK_RST_SIMPLE: Use the same registers for bit set and clear.
 * @MTK_RST_SET_CLR: Use separate registers for bit set and clear.
 * @MTK_RST_MAX: Total quantity of version for MediaTek clock reset controller.
 */
enum mtk_reset_version {
	MTK_RST_SIMPLE = 0,
	MTK_RST_SET_CLR,
	MTK_RST_MAX,
};

struct mtk_reset {
	struct regmap *regmap;
	int regofs;
	struct reset_controller_dev rcdev;
};

/**
 * mtk_register_reset_controller - Register MediaTek clock reset controller
 * @np: Pointer to device node.
 * @rst_bank_nr: Quantity of reset bank.
 * @reg_ofs: Base offset of the reset register.
 * @version: Version of MediaTek clock reset controller.
 */
void mtk_register_reset_controller(struct device_node *np,
				   u32 rst_bank_nr, u16 reg_ofs,
				   enum mtk_reset_version version);

#endif /* __DRV_CLK_MTK_RESET_H */
