/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Chris Zhong<zyw@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

#include "pm.h"
#include "embedded/rk3288_ddr.h"
#include "embedded/rk3288_resume.h"

void __iomem *rk3288_regulator_pwm_addr;
void __iomem *rk3288_ddr_ctrl_addr[RK3288_NUM_DDR_PORTS];
void __iomem *rk3288_phy_addr[RK3288_NUM_DDR_PORTS];
void __iomem *rk3288_msch_addr[RK3288_NUM_DDR_PORTS];

static const char * const rk3288_ddr_clk_names[] = {
	"pclk_ddrupctl0",
	"pclk_publ0",
	"pclk_ddrupctl1",
	"pclk_publ1",
	"aclk_dmac1",
};
#define NUM_DDR_CLK_NAMES ARRAY_SIZE(rk3288_ddr_clk_names)

static struct clk *rk3288_ddr_clks[NUM_DDR_CLK_NAMES];

static const u32 rk3288_pwm_reg[] = {
	0x4,	/* PERIOD */
	0x8,	/* DUTY */
	0xc,	/* CTRL */
};
#define NUM_PWM_REGS ARRAY_SIZE(rk3288_pwm_reg)

static const u32 rk3288_ddr_phy_dll_reg[] = {
	DDR_PUBL_DLLGCR,
	DDR_PUBL_ACDLLCR,
	DDR_PUBL_DX0DLLCR,
	DDR_PUBL_DX1DLLCR,
	DDR_PUBL_DX2DLLCR,
	DDR_PUBL_DX3DLLCR,
	DDR_PUBL_PIR,
};
#define NUM_DDR_PHY_DLL_REGS ARRAY_SIZE(rk3288_ddr_phy_dll_reg)

static const u32 rk3288_ddr_ctrl_reg[] = {
	DDR_PCTL_TOGCNT1U,
	DDR_PCTL_TINIT,
	DDR_PCTL_TEXSR,
	DDR_PCTL_TINIT,
	DDR_PCTL_TRSTH,
	DDR_PCTL_TOGCNT100N,
	DDR_PCTL_TREFI,
	DDR_PCTL_TMRD,
	DDR_PCTL_TRFC,
	DDR_PCTL_TRP,
	DDR_PCTL_TRTW,
	DDR_PCTL_TAL,
	DDR_PCTL_TCL,
	DDR_PCTL_TCWL,
	DDR_PCTL_TRAS,
	DDR_PCTL_TRC,
	DDR_PCTL_TRCD,
	DDR_PCTL_TRRD,
	DDR_PCTL_TRTP,
	DDR_PCTL_TWR,
	DDR_PCTL_TWTR,
	DDR_PCTL_TEXSR,
	DDR_PCTL_TXP,
	DDR_PCTL_TXPDLL,
	DDR_PCTL_TZQCS,
	DDR_PCTL_TZQCSI,
	DDR_PCTL_TDQS,
	DDR_PCTL_TCKSRE,
	DDR_PCTL_TCKSRX,
	DDR_PCTL_TCKE,
	DDR_PCTL_TMOD,
	DDR_PCTL_TRSTL,
	DDR_PCTL_TZQCL,
	DDR_PCTL_TMRR,
	DDR_PCTL_TCKESR,
	DDR_PCTL_TDPD,
	DDR_PCTL_SCFG,
	DDR_PCTL_CMDTSTATEN,
	DDR_PCTL_MCFG1,
	DDR_PCTL_MCFG,
	DDR_PCTL_DFITCTRLDELAY,
	DDR_PCTL_DFIODTCFG,
	DDR_PCTL_DFIODTCFG1,
	DDR_PCTL_DFIODTRANKMAP,
	DDR_PCTL_DFITPHYWRDATA,
	DDR_PCTL_DFITPHYWRLAT,
	DDR_PCTL_DFITRDDATAEN,
	DDR_PCTL_DFITPHYRDLAT,
	DDR_PCTL_DFITPHYUPDTYPE0,
	DDR_PCTL_DFITPHYUPDTYPE1,
	DDR_PCTL_DFITPHYUPDTYPE2,
	DDR_PCTL_DFITPHYUPDTYPE3,
	DDR_PCTL_DFITCTRLUPDMIN,
	DDR_PCTL_DFITCTRLUPDMAX,
	DDR_PCTL_DFITCTRLUPDDLY,
	DDR_PCTL_DFIUPDCFG,
	DDR_PCTL_DFITREFMSKI,
	DDR_PCTL_DFITCTRLUPDI,
	DDR_PCTL_DFISTCFG0,
	DDR_PCTL_DFISTCFG1,
	DDR_PCTL_DFITDRAMCLKEN,
	DDR_PCTL_DFITDRAMCLKDIS,
	DDR_PCTL_DFISTCFG2,
	DDR_PCTL_DFILPCFG0,
};
#define NUM_DDR_CTRL_REGS ARRAY_SIZE(rk3288_ddr_ctrl_reg)

static const u32 rk3288_ddr_phy_reg[] = {
	DDR_PUBL_DTPR0,
	DDR_PUBL_DTPR1,
	DDR_PUBL_DTPR2,
	DDR_PUBL_MR0,
	DDR_PUBL_MR1,
	DDR_PUBL_MR2,
	DDR_PUBL_MR3,
	DDR_PUBL_PGCR,
	DDR_PUBL_PTR0,
	DDR_PUBL_PTR1,
	DDR_PUBL_PTR2,
	DDR_PUBL_ACIOCR,
	DDR_PUBL_DXCCR,
	DDR_PUBL_DSGCR,
	DDR_PUBL_DCR,
	DDR_PUBL_ODTCR,
	DDR_PUBL_DTAR,
	DDR_PUBL_DX0GCR,
	DDR_PUBL_DX1GCR,
	DDR_PUBL_DX2GCR,
	DDR_PUBL_DX3GCR,
	DDR_PUBL_DX0DQTR,
	DDR_PUBL_DX0DQSTR,
	DDR_PUBL_DX1DQTR,
	DDR_PUBL_DX1DQSTR,
	DDR_PUBL_DX2DQTR,
	DDR_PUBL_DX2DQSTR,
	DDR_PUBL_DX3DQTR,
	DDR_PUBL_DX3DQSTR,
};
#define NUM_DDR_PHY_REGS ARRAY_SIZE(rk3288_ddr_phy_reg)

static const u32 rk3288_ddr_msch_reg[] = {
	DDR_MSCH_DDRCONF,
	DDR_MSCH_DDRTIMING,
	DDR_MSCH_DDRMODE,
	DDR_MSCH_READLATENCY,
	DDR_MSCH_ACTIVATE,
	DDR_MSCH_DEVTODEV,
};
#define NUM_DDR_MSCH_REGS ARRAY_SIZE(rk3288_ddr_msch_reg)

static const u32 rk3288_ddr_phy_zqcr_reg[] = {
	DDR_PUBL_ZQ0CR0,
	DDR_PUBL_ZQ1CR0,
};
#define NUM_DDR_PHY_ZQCR_REGS ARRAY_SIZE(rk3288_ddr_phy_zqcr_reg)

static void rk3288_ddr_reg_save(void __iomem *regbase, const u32 reg_list[],
				int num_reg, u32 *vals)
{
	int i;

	for (i = 0; i < num_reg; i++)
		vals[i] = readl_relaxed(regbase + reg_list[i]);
}

static void rk3288_ddr_save_offsets(u32 *dst_offsets, const u32 *src_offsets,
				    int num_offsets, int max_offsets)
{
	memcpy(dst_offsets, src_offsets, sizeof(*dst_offsets) * num_offsets);

	/*
	 * Bytes are precious in the restore code, so we don't actually store
	 * a count.  We just put a 0xffffffff if num >= max.
	 */
	if (num_offsets < max_offsets)
		dst_offsets[num_offsets] = RK3288_BOGUS_OFFSET;
}

int rk3288_ddr_suspend(struct rk3288_ddr_save_data *ddr_save)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(rk3288_ddr_clk_names); i++) {
		ret = clk_enable(rk3288_ddr_clks[i]);
		if (ret) {
			pr_err("%s: Couldn't enable clock %s\n", __func__,
			       rk3288_ddr_clk_names[i]);
			goto err;
		}
	}

	if (rk3288_regulator_pwm_addr)
		rk3288_ddr_reg_save(rk3288_regulator_pwm_addr, rk3288_pwm_reg,
				NUM_PWM_REGS, ddr_save->pwm_vals);

	rk3288_ddr_reg_save(rk3288_ddr_ctrl_addr[0], rk3288_ddr_ctrl_reg,
			    NUM_DDR_CTRL_REGS, ddr_save->ctrl_vals);

	/* TODO: need to support only one channel of DDR? */
	for (i = 0; i < RK3288_NUM_DDR_PORTS; i++) {
		rk3288_ddr_reg_save(rk3288_phy_addr[i], rk3288_ddr_phy_reg,
				    NUM_DDR_PHY_REGS, ddr_save->phy_vals[i]);
		rk3288_ddr_reg_save(rk3288_phy_addr[i], rk3288_ddr_phy_dll_reg,
				    NUM_DDR_PHY_DLL_REGS,
				    ddr_save->phy_dll_vals[i]);
		rk3288_ddr_reg_save(rk3288_msch_addr[i], rk3288_ddr_msch_reg,
				    NUM_DDR_MSCH_REGS, ddr_save->msch_vals[i]);
	}

	rk3288_ddr_reg_save(rk3288_phy_addr[0], rk3288_ddr_phy_zqcr_reg,
			    NUM_DDR_PHY_ZQCR_REGS, ddr_save->phy_zqcr_vals);

	return 0;

err:
	for (; i > 0; i--)
		clk_disable(rk3288_ddr_clks[i - 1]);

	return ret;
}

void rk3288_ddr_resume(void)
{
	int i;

	for (i = NUM_DDR_CLK_NAMES; i > 0; i--)
		clk_disable(rk3288_ddr_clks[i - 1]);
}

/**
 * rk3288_get_regulator_pwm_np - Get the PWM regulator node, if present
 *
 * It's common (but not required) that an rk3288 board uses one of the
 * PWMs on the rk3288 as a regulator.  We'll see if we're in that case.  If we
 * are we'll return a "np" for the PWM to save.  If not, we'll return NULL.
 * If we have an unexpected error we'll return an ERR_PTR.
 *
 * NOTE: this whole concept of needing to restore the voltage immediately after
 * resume only makes sense for the PWMs built into rk3288.  Any external
 * regulators or external PWMs ought to keep their state.
 */
static struct device_node * __init rk3288_get_regulator_pwm_np(
	struct device_node *dmc_np)
{
	struct device_node *np;
	struct of_phandle_args args;
	int ret;

	/* Look for the supply to the memory controller; OK if not there */
	np = of_parse_phandle(dmc_np, "logic-supply", 0);
	if (!np)
		return NULL;

	/* Check to see if it's a PWM regulator; OK if it's not */
	if (!of_device_is_compatible(np, "pwm-regulator")) {
		of_node_put(np);
		return NULL;
	}

	/* If it's a PWM regulator, we'd better be able to get the PWM */
	ret = of_parse_phandle_with_args(np, "pwms", "#pwm-cells", 0, &args);
	of_node_put(np);
	if (ret) {
		pr_err("%s(): can't parse \"pwms\" property\n", __func__);
		return ERR_PTR(ret);
	}

	/*
	 * It seems highly unlikely that we'd have a PWM supplying a PWM
	 * regulator on an rk3288 that isn't a rk3288 PWM.  In such a case
	 * it's unlikely that the PWM will lose its state.  We'll throw up a
	 * warning just because this is so strange, but we won't treat it as
	 * an error.
	 */
	if (!of_device_is_compatible(args.np, "rockchip,rk3288-pwm")) {
		pr_warn("%s(): unexpected PWM for regulator\n", __func__);
		of_node_put(args.np);
		return NULL;
	}

	return args.np;
}

int __init rk3288_ddr_suspend_init(struct rk3288_ddr_save_data *ddr_save)
{
	struct device_node *dmc_np = NULL, *noc_np = NULL, *pwm_np = NULL;
	int ret;
	int i;

	dmc_np = of_find_compatible_node(NULL, NULL, "rockchip,rk3288-dmc");
	if (!dmc_np) {
		pr_err("%s: could not find dmc dt node\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	noc_np = of_find_compatible_node(NULL, NULL, "rockchip,rk3288-noc");
	if (!noc_np) {
		pr_err("%s: could not find noc node\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	pwm_np = rk3288_get_regulator_pwm_np(dmc_np);
	if (IS_ERR(pwm_np)) {
		ret = PTR_ERR(pwm_np);
		goto err;
	}

	/* Do the offsets and saving of the PWM together */
	if (pwm_np) {
		struct resource res;

		rk3288_regulator_pwm_addr = of_iomap(pwm_np, 0);
		if (!rk3288_regulator_pwm_addr) {
			pr_err("%s: could not map PWM\n", __func__);
			ret = -ENOMEM;
			goto err;
		}

		ret = of_address_to_resource(pwm_np, 0, &res);
		if (ret) {
			pr_err("%s: could not get PWM phy addr\n", __func__);
			goto err;
		}

		BUILD_BUG_ON(NUM_PWM_REGS > RK3288_MAX_PWM_REGS);
		rk3288_ddr_save_offsets(ddr_save->pwm_addrs,
					rk3288_pwm_reg,
					NUM_PWM_REGS,
					RK3288_MAX_PWM_REGS);

		/* Adjust to store full address, since there are many PWMs */
		for (i = 0; i < NUM_PWM_REGS; i++)
			ddr_save->pwm_addrs[i] += res.start;
	}

	/* Copy offsets in */
	BUILD_BUG_ON(NUM_DDR_PHY_DLL_REGS > RK3288_MAX_DDR_PHY_DLL_REGS);
	rk3288_ddr_save_offsets(ddr_save->phy_dll_offsets,
				rk3288_ddr_phy_dll_reg,
				NUM_DDR_PHY_DLL_REGS,
				RK3288_MAX_DDR_PHY_DLL_REGS);

	BUILD_BUG_ON(NUM_DDR_CTRL_REGS > RK3288_MAX_DDR_CTRL_REGS);
	rk3288_ddr_save_offsets(ddr_save->ctrl_offsets,
				rk3288_ddr_ctrl_reg,
				NUM_DDR_CTRL_REGS,
				RK3288_MAX_DDR_CTRL_REGS);

	BUILD_BUG_ON(NUM_DDR_PHY_REGS > RK3288_MAX_DDR_PHY_REGS);
	rk3288_ddr_save_offsets(ddr_save->phy_offsets,
				rk3288_ddr_phy_reg,
				NUM_DDR_PHY_REGS,
				RK3288_MAX_DDR_PHY_REGS);

	BUILD_BUG_ON(ARRAY_SIZE(rk3288_ddr_msch_reg) >
		     RK3288_MAX_DDR_MSCH_REGS);
	rk3288_ddr_save_offsets(ddr_save->msch_offsets,
				rk3288_ddr_msch_reg,
				NUM_DDR_MSCH_REGS,
				RK3288_MAX_DDR_MSCH_REGS);

	BUILD_BUG_ON(NUM_DDR_PHY_ZQCR_REGS > RK3288_MAX_DDR_PHY_ZQCR_REGS);
	rk3288_ddr_save_offsets(ddr_save->phy_zqcr_offsets,
				rk3288_ddr_phy_zqcr_reg,
				NUM_DDR_PHY_ZQCR_REGS,
				RK3288_MAX_DDR_PHY_ZQCR_REGS);

	for (i = 0; i < RK3288_NUM_DDR_PORTS; i++) {
		rk3288_ddr_ctrl_addr[i] = of_iomap(dmc_np, i * 2);
		if (!rk3288_ddr_ctrl_addr[i]) {
			pr_err("%s: could not map ddr ctrl\n", __func__);
			ret = -ENOMEM;
			goto err;
		}

		rk3288_phy_addr[i] = of_iomap(dmc_np, i * 2 + 1);
		if (!rk3288_phy_addr[i]) {
			pr_err("%s: could not map phy\n", __func__);
			ret = -ENOMEM;
			goto err;
		}
	}

	for (i = 0; i < NUM_DDR_CLK_NAMES; i++) {
		rk3288_ddr_clks[i] =
			of_clk_get_by_name(dmc_np, rk3288_ddr_clk_names[i]);

		if (IS_ERR(rk3288_ddr_clks[i])) {
			pr_err("%s: couldn't get clock %s\n", __func__,
			       rk3288_ddr_clk_names[i]);
			ret = PTR_ERR(rk3288_ddr_clks[i]);
			goto err;
		}

		ret = clk_prepare(rk3288_ddr_clks[i]);
		if (ret) {
			pr_err("%s: couldn't prepare clock %s\n", __func__,
			       rk3288_ddr_clk_names[i]);
			clk_put(rk3288_ddr_clks[i]);
			rk3288_ddr_clks[i] = NULL;
			goto err;
		}
	}

	rk3288_msch_addr[0] = of_iomap(noc_np, 0);
	if (!rk3288_msch_addr[0]) {
		pr_err("%s: could not map msch base\n", __func__);
		ret = -ENOMEM;
		goto err;
	}
	rk3288_msch_addr[1] = rk3288_msch_addr[0] + 0x80;

	ret = 0;
	goto exit_of;

err:
	if (rk3288_msch_addr[0]) {
		iounmap(rk3288_msch_addr[0]);
		rk3288_msch_addr[0] = NULL;
	}

	for (i = 0; i < NUM_DDR_CLK_NAMES; i++)
		if (!IS_ERR_OR_NULL(rk3288_ddr_clks[i])) {
			clk_unprepare(rk3288_ddr_clks[i]);
			clk_put(rk3288_ddr_clks[i]);
			rk3288_ddr_clks[i] = NULL;
		}

	for (i = 0; i < RK3288_NUM_DDR_PORTS; i++) {
		if (rk3288_phy_addr[i]) {
			iounmap(rk3288_phy_addr[i]);
			rk3288_phy_addr[i] = NULL;
		}
		if (rk3288_ddr_ctrl_addr[i]) {
			iounmap(rk3288_ddr_ctrl_addr[i]);
			rk3288_ddr_ctrl_addr[i] = NULL;
		}
	}

	if (rk3288_regulator_pwm_addr) {
		iounmap(rk3288_regulator_pwm_addr);
		rk3288_regulator_pwm_addr = NULL;
	}

exit_of:
	if (pwm_np)
		of_node_put(pwm_np);
	if (noc_np)
		of_node_put(noc_np);
	if (dmc_np)
		of_node_put(dmc_np);

	return ret;
}
