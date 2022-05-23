/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __DRV_CLK_MTK_RESET_H
#define __DRV_CLK_MTK_RESET_H

#include <linux/reset-controller.h>
#include <linux/types.h>

struct mtk_reset {
	struct regmap *regmap;
	int regofs;
	struct reset_controller_dev rcdev;
};

void mtk_register_reset_controller(struct device_node *np,
				   unsigned int num_regs, int regofs);

void mtk_register_reset_controller_set_clr(struct device_node *np,
					   unsigned int num_regs, int regofs);

#endif /* __DRV_CLK_MTK_RESET_H */
