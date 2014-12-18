/*
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

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#include <mach/clk.h>

#include "proc_comm.h"
#include "clock.h"
#include "clock-pcom.h"

struct clk_pcom {
	unsigned id;
	unsigned long flags;
	struct msm_clk msm_clk;
};

static inline struct clk_pcom *to_clk_pcom(struct clk_hw *hw)
{
	return container_of(to_msm_clk(hw), struct clk_pcom, msm_clk);
}

static int pc_clk_enable(struct clk_hw *hw)
{
	unsigned id = to_clk_pcom(hw)->id;
	int rc = msm_proc_comm(PCOM_CLKCTL_RPC_ENABLE, &id, NULL);
	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

static void pc_clk_disable(struct clk_hw *hw)
{
	unsigned id = to_clk_pcom(hw)->id;
	msm_proc_comm(PCOM_CLKCTL_RPC_DISABLE, &id, NULL);
}

static int pc_clk_reset(struct clk_hw *hw, enum clk_reset_action action)
{
	int rc;
	unsigned id = to_clk_pcom(hw)->id;

	if (action == CLK_RESET_ASSERT)
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_RESET_ASSERT, &id, NULL);
	else
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_RESET_DEASSERT, &id, NULL);

	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

static int pc_clk_set_rate(struct clk_hw *hw, unsigned long new_rate,
			   unsigned long p_rate)
{
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id, rate = new_rate;
	int rc;

	/*
	 * The rate _might_ be rounded off to the nearest KHz value by the
	 * remote function. So a return value of 0 doesn't necessarily mean
	 * that the exact rate was set successfully.
	 */
	if (p->flags & CLKFLAG_MIN)
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_MIN_RATE, &id, &rate);
	else
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_SET_RATE, &id, &rate);
	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

static unsigned long pc_clk_recalc_rate(struct clk_hw *hw, unsigned long p_rate)
{
	unsigned id = to_clk_pcom(hw)->id;
	if (msm_proc_comm(PCOM_CLKCTL_RPC_RATE, &id, NULL))
		return 0;
	else
		return id;
}

static int pc_clk_is_enabled(struct clk_hw *hw)
{
	unsigned id = to_clk_pcom(hw)->id;
	if (msm_proc_comm(PCOM_CLKCTL_RPC_ENABLED, &id, NULL))
		return 0;
	else
		return id;
}

static long pc_clk_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *p_rate)
{
	/* Not really supported; pc_clk_set_rate() does rounding on it's own. */
	return rate;
}

static struct clk_ops clk_ops_pcom = {
	.enable = pc_clk_enable,
	.disable = pc_clk_disable,
	.set_rate = pc_clk_set_rate,
	.recalc_rate = pc_clk_recalc_rate,
	.is_enabled = pc_clk_is_enabled,
	.round_rate = pc_clk_round_rate,
};

static int msm_clock_pcom_probe(struct platform_device *pdev)
{
	const struct pcom_clk_pdata *pdata = pdev->dev.platform_data;
	int i, ret;

	for (i = 0; i < pdata->num_lookups; i++) {
		const struct clk_pcom_desc *desc = &pdata->lookup[i];
		struct clk *c;
		struct clk_pcom *p;
		struct clk_hw *hw;
		struct clk_init_data init;

		p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		p->id = desc->id;
		p->flags = desc->flags;
		p->msm_clk.reset = pc_clk_reset;

		hw = &p->msm_clk.hw;
		hw->init = &init;

		init.name = desc->name;
		init.ops = &clk_ops_pcom;
		init.num_parents = 0;
		init.flags = CLK_IS_ROOT;

		if (!(p->flags & CLKFLAG_AUTO_OFF))
			init.flags |= CLK_IGNORE_UNUSED;

		c = devm_clk_register(&pdev->dev, hw);
		ret = clk_register_clkdev(c, desc->con, desc->dev);
		if (ret)
			return ret;
	}

	return 0;
}

static struct platform_driver msm_clock_pcom_driver = {
	.probe		= msm_clock_pcom_probe,
	.driver		= {
		.name	= "msm-clock-pcom",
	},
};
module_platform_driver(msm_clock_pcom_driver);

MODULE_LICENSE("GPL v2");
