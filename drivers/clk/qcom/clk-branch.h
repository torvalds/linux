/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCOM_CLK_BRANCH_H__
#define __QCOM_CLK_BRANCH_H__

#include <linux/clk-provider.h>

#include "clk-regmap.h"

/**
 * struct clk_branch - gating clock with status bit and dynamic hardware gating
 *
 * @hwcg_reg: dynamic hardware clock gating register
 * @hwcg_bit: ORed with @hwcg_reg to enable dynamic hardware clock gating
 * @halt_reg: halt register
 * @halt_bit: ANDed with @halt_reg to test for clock halted
 * @halt_check: type of halt checking to perform
 * @clkr: handle between common and hardware-specific interfaces
 *
 * Clock which can gate its output.
 */
struct clk_branch {
	u32	hwcg_reg;
	u32	halt_reg;
	u8	hwcg_bit;
	u8	halt_bit;
	u8	halt_check;
#define BRANCH_VOTED			BIT(7) /* Delay on disable */
#define BRANCH_HALT			0 /* pol: 1 = halt */
#define BRANCH_HALT_VOTED		(BRANCH_HALT | BRANCH_VOTED)
#define BRANCH_HALT_ENABLE		1 /* pol: 0 = halt */
#define BRANCH_HALT_ENABLE_VOTED	(BRANCH_HALT_ENABLE | BRANCH_VOTED)
#define BRANCH_HALT_DELAY		2 /* No bit to check; just delay */
#define BRANCH_HALT_SKIP		3 /* Don't check halt bit */

	struct clk_regmap clkr;
};

extern const struct clk_ops clk_branch_ops;
extern const struct clk_ops clk_branch2_ops;
extern const struct clk_ops clk_branch_simple_ops;

#define to_clk_branch(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_branch, clkr)

#endif
