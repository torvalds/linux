/* arch/arm/mach-msm/clock.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007-2012, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk-provider.h>
#include <linux/module.h>

#include "clock.h"

int clk_reset(struct clk *clk, enum clk_reset_action action)
{
	struct clk_hw *hw = __clk_get_hw(clk);
	struct msm_clk *m = to_msm_clk(hw);
	return m->reset(hw, action);
}
EXPORT_SYMBOL(clk_reset);
