/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <subdev/clk.h>
#include <subdev/volt.h>
#include <subdev/timer.h>
#include <core/device.h>
#include <core/tegra.h>

#include "priv.h"
#include "gk20a.h"

#define GPCPLL_CFG_SYNC_MODE	BIT(2)

#define BYPASSCTRL_SYS	(SYS_GPCPLL_CFG_BASE + 0x340)
#define BYPASSCTRL_SYS_GPCPLL_SHIFT	0
#define BYPASSCTRL_SYS_GPCPLL_WIDTH	1

#define GPCPLL_CFG2_SDM_DIN_SHIFT	0
#define GPCPLL_CFG2_SDM_DIN_WIDTH	8
#define GPCPLL_CFG2_SDM_DIN_MASK	\
	(MASK(GPCPLL_CFG2_SDM_DIN_WIDTH) << GPCPLL_CFG2_SDM_DIN_SHIFT)
#define GPCPLL_CFG2_SDM_DIN_NEW_SHIFT	8
#define GPCPLL_CFG2_SDM_DIN_NEW_WIDTH	15
#define GPCPLL_CFG2_SDM_DIN_NEW_MASK	\
	(MASK(GPCPLL_CFG2_SDM_DIN_NEW_WIDTH) << GPCPLL_CFG2_SDM_DIN_NEW_SHIFT)
#define GPCPLL_CFG2_SETUP2_SHIFT	16
#define GPCPLL_CFG2_PLL_STEPA_SHIFT	24

#define GPCPLL_DVFS0	(SYS_GPCPLL_CFG_BASE + 0x10)
#define GPCPLL_DVFS0_DFS_COEFF_SHIFT	0
#define GPCPLL_DVFS0_DFS_COEFF_WIDTH	7
#define GPCPLL_DVFS0_DFS_COEFF_MASK	\
	(MASK(GPCPLL_DVFS0_DFS_COEFF_WIDTH) << GPCPLL_DVFS0_DFS_COEFF_SHIFT)
#define GPCPLL_DVFS0_DFS_DET_MAX_SHIFT	8
#define GPCPLL_DVFS0_DFS_DET_MAX_WIDTH	7
#define GPCPLL_DVFS0_DFS_DET_MAX_MASK	\
	(MASK(GPCPLL_DVFS0_DFS_DET_MAX_WIDTH) << GPCPLL_DVFS0_DFS_DET_MAX_SHIFT)

#define GPCPLL_DVFS1		(SYS_GPCPLL_CFG_BASE + 0x14)
#define GPCPLL_DVFS1_DFS_EXT_DET_SHIFT		0
#define GPCPLL_DVFS1_DFS_EXT_DET_WIDTH		7
#define GPCPLL_DVFS1_DFS_EXT_STRB_SHIFT		7
#define GPCPLL_DVFS1_DFS_EXT_STRB_WIDTH		1
#define GPCPLL_DVFS1_DFS_EXT_CAL_SHIFT		8
#define GPCPLL_DVFS1_DFS_EXT_CAL_WIDTH		7
#define GPCPLL_DVFS1_DFS_EXT_SEL_SHIFT		15
#define GPCPLL_DVFS1_DFS_EXT_SEL_WIDTH		1
#define GPCPLL_DVFS1_DFS_CTRL_SHIFT		16
#define GPCPLL_DVFS1_DFS_CTRL_WIDTH		12
#define GPCPLL_DVFS1_EN_SDM_SHIFT		28
#define GPCPLL_DVFS1_EN_SDM_WIDTH		1
#define GPCPLL_DVFS1_EN_SDM_BIT			BIT(28)
#define GPCPLL_DVFS1_EN_DFS_SHIFT		29
#define GPCPLL_DVFS1_EN_DFS_WIDTH		1
#define GPCPLL_DVFS1_EN_DFS_BIT			BIT(29)
#define GPCPLL_DVFS1_EN_DFS_CAL_SHIFT		30
#define GPCPLL_DVFS1_EN_DFS_CAL_WIDTH		1
#define GPCPLL_DVFS1_EN_DFS_CAL_BIT		BIT(30)
#define GPCPLL_DVFS1_DFS_CAL_DONE_SHIFT		31
#define GPCPLL_DVFS1_DFS_CAL_DONE_WIDTH		1
#define GPCPLL_DVFS1_DFS_CAL_DONE_BIT		BIT(31)

#define GPC_BCAST_GPCPLL_DVFS2	(GPC_BCAST_GPCPLL_CFG_BASE + 0x20)
#define GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT	BIT(16)

#define GPCPLL_CFG3_PLL_DFS_TESTOUT_SHIFT	24
#define GPCPLL_CFG3_PLL_DFS_TESTOUT_WIDTH	7

#define DFS_DET_RANGE	6	/* -2^6 ... 2^6-1 */
#define SDM_DIN_RANGE	12	/* -2^12 ... 2^12-1 */

struct gm20b_clk_dvfs_params {
	s32 coeff_slope;
	s32 coeff_offs;
	u32 vco_ctrl;
};

static const struct gm20b_clk_dvfs_params gm20b_dvfs_params = {
	.coeff_slope = -165230,
	.coeff_offs = 214007,
	.vco_ctrl = 0x7 << 3,
};

/*
 * base.n is now the *integer* part of the N factor.
 * sdm_din contains n's decimal part.
 */
struct gm20b_pll {
	struct gk20a_pll base;
	u32 sdm_din;
};

struct gm20b_clk_dvfs {
	u32 dfs_coeff;
	s32 dfs_det_max;
	s32 dfs_ext_cal;
};

struct gm20b_clk {
	/* currently applied parameters */
	struct gk20a_clk base;
	struct gm20b_clk_dvfs dvfs;
	u32 uv;

	/* new parameters to apply */
	struct gk20a_pll new_pll;
	struct gm20b_clk_dvfs new_dvfs;
	u32 new_uv;

	const struct gm20b_clk_dvfs_params *dvfs_params;

	/* fused parameters */
	s32 uvdet_slope;
	s32 uvdet_offs;

	/* safe frequency we can use at minimum voltage */
	u32 safe_fmax_vmin;
};
#define gm20b_clk(p) container_of((gk20a_clk(p)), struct gm20b_clk, base)

static u32 pl_to_div(u32 pl)
{
	return pl;
}

static u32 div_to_pl(u32 div)
{
	return div;
}

static const struct gk20a_clk_pllg_params gm20b_pllg_params = {
	.min_vco = 1300000, .max_vco = 2600000,
	.min_u = 12000, .max_u = 38400,
	.min_m = 1, .max_m = 255,
	.min_n = 8, .max_n = 255,
	.min_pl = 1, .max_pl = 31,
};

static void
gm20b_pllg_read_mnp(struct gm20b_clk *clk, struct gm20b_pll *pll)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 val;

	gk20a_pllg_read_mnp(&clk->base, &pll->base);
	val = nvkm_rd32(device, GPCPLL_CFG2);
	pll->sdm_din = (val >> GPCPLL_CFG2_SDM_DIN_SHIFT) &
		       MASK(GPCPLL_CFG2_SDM_DIN_WIDTH);
}

static void
gm20b_pllg_write_mnp(struct gm20b_clk *clk, const struct gm20b_pll *pll)
{
	struct nvkm_device *device = clk->base.base.subdev.device;

	nvkm_mask(device, GPCPLL_CFG2, GPCPLL_CFG2_SDM_DIN_MASK,
		  pll->sdm_din << GPCPLL_CFG2_SDM_DIN_SHIFT);
	gk20a_pllg_write_mnp(&clk->base, &pll->base);
}

/*
 * Determine DFS_COEFF for the requested voltage. Always select external
 * calibration override equal to the voltage, and set maximum detection
 * limit "0" (to make sure that PLL output remains under F/V curve when
 * voltage increases).
 */
static void
gm20b_dvfs_calc_det_coeff(struct gm20b_clk *clk, s32 uv,
			  struct gm20b_clk_dvfs *dvfs)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	const struct gm20b_clk_dvfs_params *p = clk->dvfs_params;
	u32 coeff;
	/* Work with mv as uv would likely trigger an overflow */
	s32 mv = DIV_ROUND_CLOSEST(uv, 1000);

	/* coeff = slope * voltage + offset */
	coeff = DIV_ROUND_CLOSEST(mv * p->coeff_slope, 1000) + p->coeff_offs;
	coeff = DIV_ROUND_CLOSEST(coeff, 1000);
	dvfs->dfs_coeff = min_t(u32, coeff, MASK(GPCPLL_DVFS0_DFS_COEFF_WIDTH));

	dvfs->dfs_ext_cal = DIV_ROUND_CLOSEST(uv - clk->uvdet_offs,
					     clk->uvdet_slope);
	/* should never happen */
	if (abs(dvfs->dfs_ext_cal) >= BIT(DFS_DET_RANGE))
		nvkm_error(subdev, "dfs_ext_cal overflow!\n");

	dvfs->dfs_det_max = 0;

	nvkm_debug(subdev, "%s uv: %d coeff: %x, ext_cal: %d, det_max: %d\n",
		   __func__, uv, dvfs->dfs_coeff, dvfs->dfs_ext_cal,
		   dvfs->dfs_det_max);
}

/*
 * Solve equation for integer and fractional part of the effective NDIV:
 *
 * n_eff = n_int + 1/2 + (SDM_DIN / 2^(SDM_DIN_RANGE + 1)) +
 *         (DVFS_COEFF * DVFS_DET_DELTA) / 2^DFS_DET_RANGE
 *
 * The SDM_DIN LSB is finally shifted out, since it is not accessible by sw.
 */
static void
gm20b_dvfs_calc_ndiv(struct gm20b_clk *clk, u32 n_eff, u32 *n_int, u32 *sdm_din)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	const struct gk20a_clk_pllg_params *p = clk->base.params;
	u32 n;
	s32 det_delta;
	u32 rem, rem_range;

	/* calculate current ext_cal and subtract previous one */
	det_delta = DIV_ROUND_CLOSEST(((s32)clk->uv) - clk->uvdet_offs,
				      clk->uvdet_slope);
	det_delta -= clk->dvfs.dfs_ext_cal;
	det_delta = min(det_delta, clk->dvfs.dfs_det_max);
	det_delta *= clk->dvfs.dfs_coeff;

	/* integer part of n */
	n = (n_eff << DFS_DET_RANGE) - det_delta;
	/* should never happen! */
	if (n <= 0) {
		nvkm_error(subdev, "ndiv <= 0 - setting to 1...\n");
		n = 1 << DFS_DET_RANGE;
	}
	if (n >> DFS_DET_RANGE > p->max_n) {
		nvkm_error(subdev, "ndiv > max_n - setting to max_n...\n");
		n = p->max_n << DFS_DET_RANGE;
	}
	*n_int = n >> DFS_DET_RANGE;

	/* fractional part of n */
	rem = ((u32)n) & MASK(DFS_DET_RANGE);
	rem_range = SDM_DIN_RANGE + 1 - DFS_DET_RANGE;
	/* subtract 2^SDM_DIN_RANGE to account for the 1/2 of the equation */
	rem = (rem << rem_range) - BIT(SDM_DIN_RANGE);
	/* lose 8 LSB and clip - sdm_din only keeps the most significant byte */
	*sdm_din = (rem >> BITS_PER_BYTE) & MASK(GPCPLL_CFG2_SDM_DIN_WIDTH);

	nvkm_debug(subdev, "%s n_eff: %d, n_int: %d, sdm_din: %d\n", __func__,
		   n_eff, *n_int, *sdm_din);
}

static int
gm20b_pllg_slide(struct gm20b_clk *clk, u32 n)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	struct nvkm_device *device = subdev->device;
	struct gm20b_pll pll;
	u32 n_int, sdm_din;
	int ret = 0;

	/* calculate the new n_int/sdm_din for this n/uv */
	gm20b_dvfs_calc_ndiv(clk, n, &n_int, &sdm_din);

	/* get old coefficients */
	gm20b_pllg_read_mnp(clk, &pll);
	/* do nothing if NDIV is the same */
	if (n_int == pll.base.n && sdm_din == pll.sdm_din)
		return 0;

	/* pll slowdown mode */
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT),
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT));

	/* new ndiv ready for ramp */
	/* in DVFS mode SDM is updated via "new" field */
	nvkm_mask(device, GPCPLL_CFG2, GPCPLL_CFG2_SDM_DIN_NEW_MASK,
		  sdm_din << GPCPLL_CFG2_SDM_DIN_NEW_SHIFT);
	pll.base.n = n_int;
	udelay(1);
	gk20a_pllg_write_mnp(&clk->base, &pll.base);

	/* dynamic ramp to new ndiv */
	udelay(1);
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		  BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT),
		  BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT));

	/* wait for ramping to complete */
	if (nvkm_wait_usec(device, 500, GPC_BCAST_NDIV_SLOWDOWN_DEBUG,
		GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK,
		GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK) < 0)
		ret = -ETIMEDOUT;

	/* in DVFS mode complete SDM update */
	nvkm_mask(device, GPCPLL_CFG2, GPCPLL_CFG2_SDM_DIN_MASK,
		  sdm_din << GPCPLL_CFG2_SDM_DIN_SHIFT);

	/* exit slowdown mode */
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT) |
		BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT), 0);
	nvkm_rd32(device, GPCPLL_NDIV_SLOWDOWN);

	return ret;
}

static int
gm20b_pllg_enable(struct gm20b_clk *clk)
{
	struct nvkm_device *device = clk->base.base.subdev.device;

	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_ENABLE, GPCPLL_CFG_ENABLE);
	nvkm_rd32(device, GPCPLL_CFG);

	/* In DVFS mode lock cannot be used - so just delay */
	udelay(40);

	/* set SYNC_MODE for glitchless switch out of bypass */
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_SYNC_MODE,
		       GPCPLL_CFG_SYNC_MODE);
	nvkm_rd32(device, GPCPLL_CFG);

	/* switch to VCO mode */
	nvkm_mask(device, SEL_VCO, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT),
		  BIT(SEL_VCO_GPC2CLK_OUT_SHIFT));

	return 0;
}

static void
gm20b_pllg_disable(struct gm20b_clk *clk)
{
	struct nvkm_device *device = clk->base.base.subdev.device;

	/* put PLL in bypass before disabling it */
	nvkm_mask(device, SEL_VCO, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT), 0);

	/* clear SYNC_MODE before disabling PLL */
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_SYNC_MODE, 0);

	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_ENABLE, 0);
	nvkm_rd32(device, GPCPLL_CFG);
}

static int
gm20b_pllg_program_mnp(struct gm20b_clk *clk, const struct gk20a_pll *pll)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	struct nvkm_device *device = subdev->device;
	struct gm20b_pll cur_pll;
	u32 n_int, sdm_din;
	/* if we only change pdiv, we can do a glitchless transition */
	bool pdiv_only;
	int ret;

	gm20b_dvfs_calc_ndiv(clk, pll->n, &n_int, &sdm_din);
	gm20b_pllg_read_mnp(clk, &cur_pll);
	pdiv_only = cur_pll.base.n == n_int && cur_pll.sdm_din == sdm_din &&
		    cur_pll.base.m == pll->m;

	/* need full sequence if clock not enabled yet */
	if (!gk20a_pllg_is_enabled(&clk->base))
		pdiv_only = false;

	/* split VCO-to-bypass jump in half by setting out divider 1:2 */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV2 << GPC2CLK_OUT_VCODIV_SHIFT);
	/* Intentional 2nd write to assure linear divider operation */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV2 << GPC2CLK_OUT_VCODIV_SHIFT);
	nvkm_rd32(device, GPC2CLK_OUT);
	udelay(2);

	if (pdiv_only) {
		u32 old = cur_pll.base.pl;
		u32 new = pll->pl;

		/*
		 * we can do a glitchless transition only if the old and new PL
		 * parameters share at least one bit set to 1. If this is not
		 * the case, calculate and program an interim PL that will allow
		 * us to respect that rule.
		 */
		if ((old & new) == 0) {
			cur_pll.base.pl = min(old | BIT(ffs(new) - 1),
					      new | BIT(ffs(old) - 1));
			gk20a_pllg_write_mnp(&clk->base, &cur_pll.base);
		}

		cur_pll.base.pl = new;
		gk20a_pllg_write_mnp(&clk->base, &cur_pll.base);
	} else {
		/* disable before programming if more than pdiv changes */
		gm20b_pllg_disable(clk);

		cur_pll.base = *pll;
		cur_pll.base.n = n_int;
		cur_pll.sdm_din = sdm_din;
		gm20b_pllg_write_mnp(clk, &cur_pll);

		ret = gm20b_pllg_enable(clk);
		if (ret)
			return ret;
	}

	/* restore out divider 1:1 */
	udelay(2);
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV1 << GPC2CLK_OUT_VCODIV_SHIFT);
	/* Intentional 2nd write to assure linear divider operation */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV1 << GPC2CLK_OUT_VCODIV_SHIFT);
	nvkm_rd32(device, GPC2CLK_OUT);

	return 0;
}

static int
gm20b_pllg_program_mnp_slide(struct gm20b_clk *clk, const struct gk20a_pll *pll)
{
	struct gk20a_pll cur_pll;
	int ret;

	if (gk20a_pllg_is_enabled(&clk->base)) {
		gk20a_pllg_read_mnp(&clk->base, &cur_pll);

		/* just do NDIV slide if there is no change to M and PL */
		if (pll->m == cur_pll.m && pll->pl == cur_pll.pl)
			return gm20b_pllg_slide(clk, pll->n);

		/* slide down to current NDIV_LO */
		cur_pll.n = gk20a_pllg_n_lo(&clk->base, &cur_pll);
		ret = gm20b_pllg_slide(clk, cur_pll.n);
		if (ret)
			return ret;
	}

	/* program MNP with the new clock parameters and new NDIV_LO */
	cur_pll = *pll;
	cur_pll.n = gk20a_pllg_n_lo(&clk->base, &cur_pll);
	ret = gm20b_pllg_program_mnp(clk, &cur_pll);
	if (ret)
		return ret;

	/* slide up to new NDIV */
	return gm20b_pllg_slide(clk, pll->n);
}

static int
gm20b_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct gm20b_clk *clk = gm20b_clk(base);
	struct nvkm_subdev *subdev = &base->subdev;
	struct nvkm_volt *volt = base->subdev.device->volt;
	int ret;

	ret = gk20a_pllg_calc_mnp(&clk->base, cstate->domain[nv_clk_src_gpc] *
					     GK20A_CLK_GPC_MDIV, &clk->new_pll);
	if (ret)
		return ret;

	clk->new_uv = volt->vid[cstate->voltage].uv;
	gm20b_dvfs_calc_det_coeff(clk, clk->new_uv, &clk->new_dvfs);

	nvkm_debug(subdev, "%s uv: %d uv\n", __func__, clk->new_uv);

	return 0;
}

/*
 * Compute PLL parameters that are always safe for the current voltage
 */
static void
gm20b_dvfs_calc_safe_pll(struct gm20b_clk *clk, struct gk20a_pll *pll)
{
	u32 rate = gk20a_pllg_calc_rate(&clk->base, pll) / KHZ;
	u32 parent_rate = clk->base.parent_rate / KHZ;
	u32 nmin, nsafe;

	/* remove a safe margin of 10% */
	if (rate > clk->safe_fmax_vmin)
		rate = rate * (100 - 10) / 100;

	/* gpc2clk */
	rate *= 2;

	nmin = DIV_ROUND_UP(pll->m * clk->base.params->min_vco, parent_rate);
	nsafe = pll->m * rate / (clk->base.parent_rate);

	if (nsafe < nmin) {
		pll->pl = DIV_ROUND_UP(nmin * parent_rate, pll->m * rate);
		nsafe = nmin;
	}

	pll->n = nsafe;
}

static void
gm20b_dvfs_program_coeff(struct gm20b_clk *clk, u32 coeff)
{
	struct nvkm_device *device = clk->base.base.subdev.device;

	/* strobe to read external DFS coefficient */
	nvkm_mask(device, GPC_BCAST_GPCPLL_DVFS2,
		  GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT,
		  GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT);

	nvkm_mask(device, GPCPLL_DVFS0, GPCPLL_DVFS0_DFS_COEFF_MASK,
		  coeff << GPCPLL_DVFS0_DFS_COEFF_SHIFT);

	udelay(1);
	nvkm_mask(device, GPC_BCAST_GPCPLL_DVFS2,
		  GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT, 0);
}

static void
gm20b_dvfs_program_ext_cal(struct gm20b_clk *clk, u32 dfs_det_cal)
{
	struct nvkm_device *device = clk->base.base.subdev.device;
	u32 val;

	nvkm_mask(device, GPC_BCAST_GPCPLL_DVFS2, MASK(DFS_DET_RANGE + 1),
		  dfs_det_cal);
	udelay(1);

	val = nvkm_rd32(device, GPCPLL_DVFS1);
	if (!(val & BIT(25))) {
		/* Use external value to overwrite calibration value */
		val |= BIT(25) | BIT(16);
		nvkm_wr32(device, GPCPLL_DVFS1, val);
	}
}

static void
gm20b_dvfs_program_dfs_detection(struct gm20b_clk *clk,
				 struct gm20b_clk_dvfs *dvfs)
{
	struct nvkm_device *device = clk->base.base.subdev.device;

	/* strobe to read external DFS coefficient */
	nvkm_mask(device, GPC_BCAST_GPCPLL_DVFS2,
		  GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT,
		  GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT);

	nvkm_mask(device, GPCPLL_DVFS0,
		  GPCPLL_DVFS0_DFS_COEFF_MASK | GPCPLL_DVFS0_DFS_DET_MAX_MASK,
		  dvfs->dfs_coeff << GPCPLL_DVFS0_DFS_COEFF_SHIFT |
		  dvfs->dfs_det_max << GPCPLL_DVFS0_DFS_DET_MAX_SHIFT);

	udelay(1);
	nvkm_mask(device, GPC_BCAST_GPCPLL_DVFS2,
		  GPC_BCAST_GPCPLL_DVFS2_DFS_EXT_STROBE_BIT, 0);

	gm20b_dvfs_program_ext_cal(clk, dvfs->dfs_ext_cal);
}

static int
gm20b_clk_prog(struct nvkm_clk *base)
{
	struct gm20b_clk *clk = gm20b_clk(base);
	u32 cur_freq;
	int ret;

	/* No change in DVFS settings? */
	if (clk->uv == clk->new_uv)
		goto prog;

	/*
	 * Interim step for changing DVFS detection settings: low enough
	 * frequency to be safe at at DVFS coeff = 0.
	 *
	 * 1. If voltage is increasing:
	 * - safe frequency target matches the lowest - old - frequency
	 * - DVFS settings are still old
	 * - Voltage already increased to new level by volt, but maximum
	 *   detection limit assures PLL output remains under F/V curve
	 *
	 * 2. If voltage is decreasing:
	 * - safe frequency target matches the lowest - new - frequency
	 * - DVFS settings are still old
	 * - Voltage is also old, it will be lowered by volt afterwards
	 *
	 * Interim step can be skipped if old frequency is below safe minimum,
	 * i.e., it is low enough to be safe at any voltage in operating range
	 * with zero DVFS coefficient.
	 */
	cur_freq = nvkm_clk_read(&clk->base.base, nv_clk_src_gpc);
	if (cur_freq > clk->safe_fmax_vmin) {
		struct gk20a_pll pll_safe;

		if (clk->uv < clk->new_uv)
			/* voltage will raise: safe frequency is current one */
			pll_safe = clk->base.pll;
		else
			/* voltage will drop: safe frequency is new one */
			pll_safe = clk->new_pll;

		gm20b_dvfs_calc_safe_pll(clk, &pll_safe);
		ret = gm20b_pllg_program_mnp_slide(clk, &pll_safe);
		if (ret)
			return ret;
	}

	/*
	 * DVFS detection settings transition:
	 * - Set DVFS coefficient zero
	 * - Set calibration level to new voltage
	 * - Set DVFS coefficient to match new voltage
	 */
	gm20b_dvfs_program_coeff(clk, 0);
	gm20b_dvfs_program_ext_cal(clk, clk->new_dvfs.dfs_ext_cal);
	gm20b_dvfs_program_coeff(clk, clk->new_dvfs.dfs_coeff);
	gm20b_dvfs_program_dfs_detection(clk, &clk->new_dvfs);

prog:
	clk->uv = clk->new_uv;
	clk->dvfs = clk->new_dvfs;
	clk->base.pll = clk->new_pll;

	return gm20b_pllg_program_mnp_slide(clk, &clk->base.pll);
}

static struct nvkm_pstate
gm20b_pstates[] = {
	{
		.base = {
			.domain[nv_clk_src_gpc] = 76800,
			.voltage = 0,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 153600,
			.voltage = 1,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 230400,
			.voltage = 2,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 307200,
			.voltage = 3,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 384000,
			.voltage = 4,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 460800,
			.voltage = 5,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 537600,
			.voltage = 6,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 614400,
			.voltage = 7,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 691200,
			.voltage = 8,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 768000,
			.voltage = 9,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 844800,
			.voltage = 10,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 921600,
			.voltage = 11,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 998400,
			.voltage = 12,
		},
	},
};

static void
gm20b_clk_fini(struct nvkm_clk *base)
{
	struct nvkm_device *device = base->subdev.device;
	struct gm20b_clk *clk = gm20b_clk(base);

	/* slide to VCO min */
	if (gk20a_pllg_is_enabled(&clk->base)) {
		struct gk20a_pll pll;
		u32 n_lo;

		gk20a_pllg_read_mnp(&clk->base, &pll);
		n_lo = gk20a_pllg_n_lo(&clk->base, &pll);
		gm20b_pllg_slide(clk, n_lo);
	}

	gm20b_pllg_disable(clk);

	/* set IDDQ */
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_IDDQ, 1);
}

static int
gm20b_clk_init_dvfs(struct gm20b_clk *clk)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	struct nvkm_device *device = subdev->device;
	bool fused = clk->uvdet_offs && clk->uvdet_slope;
	static const s32 ADC_SLOPE_UV = 10000; /* default ADC detection slope */
	u32 data;
	int ret;

	/* Enable NA DVFS */
	nvkm_mask(device, GPCPLL_DVFS1, GPCPLL_DVFS1_EN_DFS_BIT,
		  GPCPLL_DVFS1_EN_DFS_BIT);

	/* Set VCO_CTRL */
	if (clk->dvfs_params->vco_ctrl)
		nvkm_mask(device, GPCPLL_CFG3, GPCPLL_CFG3_VCO_CTRL_MASK,
		      clk->dvfs_params->vco_ctrl << GPCPLL_CFG3_VCO_CTRL_SHIFT);

	if (fused) {
		/* Start internal calibration, but ignore results */
		nvkm_mask(device, GPCPLL_DVFS1, GPCPLL_DVFS1_EN_DFS_CAL_BIT,
			  GPCPLL_DVFS1_EN_DFS_CAL_BIT);

		/* got uvdev parameters from fuse, skip calibration */
		goto calibrated;
	}

	/*
	 * If calibration parameters are not fused, start internal calibration,
	 * wait for completion, and use results along with default slope to
	 * calculate ADC offset during boot.
	 */
	nvkm_mask(device, GPCPLL_DVFS1, GPCPLL_DVFS1_EN_DFS_CAL_BIT,
			  GPCPLL_DVFS1_EN_DFS_CAL_BIT);

	/* Wait for internal calibration done (spec < 2us). */
	ret = nvkm_wait_usec(device, 10, GPCPLL_DVFS1,
			     GPCPLL_DVFS1_DFS_CAL_DONE_BIT,
			     GPCPLL_DVFS1_DFS_CAL_DONE_BIT);
	if (ret < 0) {
		nvkm_error(subdev, "GPCPLL calibration timeout\n");
		return -ETIMEDOUT;
	}

	data = nvkm_rd32(device, GPCPLL_CFG3) >>
			 GPCPLL_CFG3_PLL_DFS_TESTOUT_SHIFT;
	data &= MASK(GPCPLL_CFG3_PLL_DFS_TESTOUT_WIDTH);

	clk->uvdet_slope = ADC_SLOPE_UV;
	clk->uvdet_offs = ((s32)clk->uv) - data * ADC_SLOPE_UV;

	nvkm_debug(subdev, "calibrated DVFS parameters: offs %d, slope %d\n",
		   clk->uvdet_offs, clk->uvdet_slope);

calibrated:
	/* Compute and apply initial DVFS parameters */
	gm20b_dvfs_calc_det_coeff(clk, clk->uv, &clk->dvfs);
	gm20b_dvfs_program_coeff(clk, 0);
	gm20b_dvfs_program_ext_cal(clk, clk->dvfs.dfs_ext_cal);
	gm20b_dvfs_program_coeff(clk, clk->dvfs.dfs_coeff);
	gm20b_dvfs_program_dfs_detection(clk, &clk->new_dvfs);

	return 0;
}

/* Forward declaration to detect speedo >=1 in gm20b_clk_init() */
static const struct nvkm_clk_func gm20b_clk;

static int
gm20b_clk_init(struct nvkm_clk *base)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	int ret;
	u32 data;

	/* get out from IDDQ */
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_IDDQ, 0);
	nvkm_rd32(device, GPCPLL_CFG);
	udelay(5);

	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_INIT_MASK,
		  GPC2CLK_OUT_INIT_VAL);

	/* Set the global bypass control to VCO */
	nvkm_mask(device, BYPASSCTRL_SYS,
	       MASK(BYPASSCTRL_SYS_GPCPLL_WIDTH) << BYPASSCTRL_SYS_GPCPLL_SHIFT,
	       0);

	ret = gk20a_clk_setup_slide(clk);
	if (ret)
		return ret;

	/* If not fused, set RAM SVOP PDP data 0x2, and enable fuse override */
	data = nvkm_rd32(device, 0x021944);
	if (!(data & 0x3)) {
		data |= 0x2;
		nvkm_wr32(device, 0x021944, data);

		data = nvkm_rd32(device, 0x021948);
		data |=  0x1;
		nvkm_wr32(device, 0x021948, data);
	}

	/* Disable idle slow down  */
	nvkm_mask(device, 0x20160, 0x003f0000, 0x0);

	/* speedo >= 1? */
	if (clk->base.func == &gm20b_clk) {
		struct gm20b_clk *_clk = gm20b_clk(base);
		struct nvkm_volt *volt = device->volt;

		/* Get current voltage */
		_clk->uv = nvkm_volt_get(volt);

		/* Initialize DVFS */
		ret = gm20b_clk_init_dvfs(_clk);
		if (ret)
			return ret;
	}

	/* Start with lowest frequency */
	base->func->calc(base, &base->func->pstates[0].base);
	ret = base->func->prog(base);
	if (ret) {
		nvkm_error(subdev, "cannot initialize clock\n");
		return ret;
	}

	return 0;
}

static const struct nvkm_clk_func
gm20b_clk_speedo0 = {
	.init = gm20b_clk_init,
	.fini = gk20a_clk_fini,
	.read = gk20a_clk_read,
	.calc = gk20a_clk_calc,
	.prog = gk20a_clk_prog,
	.tidy = gk20a_clk_tidy,
	.pstates = gm20b_pstates,
	/* Speedo 0 only supports 12 voltages */
	.nr_pstates = ARRAY_SIZE(gm20b_pstates) - 1,
	.domains = {
		{ nv_clk_src_crystal, 0xff },
		{ nv_clk_src_gpc, 0xff, 0, "core", GK20A_CLK_GPC_MDIV },
		{ nv_clk_src_max },
	},
};

static const struct nvkm_clk_func
gm20b_clk = {
	.init = gm20b_clk_init,
	.fini = gm20b_clk_fini,
	.read = gk20a_clk_read,
	.calc = gm20b_clk_calc,
	.prog = gm20b_clk_prog,
	.tidy = gk20a_clk_tidy,
	.pstates = gm20b_pstates,
	.nr_pstates = ARRAY_SIZE(gm20b_pstates),
	.domains = {
		{ nv_clk_src_crystal, 0xff },
		{ nv_clk_src_gpc, 0xff, 0, "core", GK20A_CLK_GPC_MDIV },
		{ nv_clk_src_max },
	},
};

static int
gm20b_clk_new_speedo0(struct nvkm_device *device, int index,
		      struct nvkm_clk **pclk)
{
	struct gk20a_clk *clk;
	int ret;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;
	*pclk = &clk->base;

	ret = gk20a_clk_ctor(device, index, &gm20b_clk_speedo0,
			     &gm20b_pllg_params, clk);

	clk->pl_to_div = pl_to_div;
	clk->div_to_pl = div_to_pl;

	return ret;
}

/* FUSE register */
#define FUSE_RESERVED_CALIB0	0x204
#define FUSE_RESERVED_CALIB0_INTERCEPT_FRAC_SHIFT	0
#define FUSE_RESERVED_CALIB0_INTERCEPT_FRAC_WIDTH	4
#define FUSE_RESERVED_CALIB0_INTERCEPT_INT_SHIFT	4
#define FUSE_RESERVED_CALIB0_INTERCEPT_INT_WIDTH	10
#define FUSE_RESERVED_CALIB0_SLOPE_FRAC_SHIFT		14
#define FUSE_RESERVED_CALIB0_SLOPE_FRAC_WIDTH		10
#define FUSE_RESERVED_CALIB0_SLOPE_INT_SHIFT		24
#define FUSE_RESERVED_CALIB0_SLOPE_INT_WIDTH		6
#define FUSE_RESERVED_CALIB0_FUSE_REV_SHIFT		30
#define FUSE_RESERVED_CALIB0_FUSE_REV_WIDTH		2

static int
gm20b_clk_init_fused_params(struct gm20b_clk *clk)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	u32 val = 0;
	u32 rev = 0;

#if IS_ENABLED(CONFIG_ARCH_TEGRA)
	tegra_fuse_readl(FUSE_RESERVED_CALIB0, &val);
	rev = (val >> FUSE_RESERVED_CALIB0_FUSE_REV_SHIFT) &
	      MASK(FUSE_RESERVED_CALIB0_FUSE_REV_WIDTH);
#endif

	/* No fused parameters, we will calibrate later */
	if (rev == 0)
		return -EINVAL;

	/* Integer part in mV + fractional part in uV */
	clk->uvdet_slope = ((val >> FUSE_RESERVED_CALIB0_SLOPE_INT_SHIFT) &
			MASK(FUSE_RESERVED_CALIB0_SLOPE_INT_WIDTH)) * 1000 +
			((val >> FUSE_RESERVED_CALIB0_SLOPE_FRAC_SHIFT) &
			MASK(FUSE_RESERVED_CALIB0_SLOPE_FRAC_WIDTH));

	/* Integer part in mV + fractional part in 100uV */
	clk->uvdet_offs = ((val >> FUSE_RESERVED_CALIB0_INTERCEPT_INT_SHIFT) &
			MASK(FUSE_RESERVED_CALIB0_INTERCEPT_INT_WIDTH)) * 1000 +
			((val >> FUSE_RESERVED_CALIB0_INTERCEPT_FRAC_SHIFT) &
			 MASK(FUSE_RESERVED_CALIB0_INTERCEPT_FRAC_WIDTH)) * 100;

	nvkm_debug(subdev, "fused calibration data: slope %d, offs %d\n",
		   clk->uvdet_slope, clk->uvdet_offs);
	return 0;
}

static int
gm20b_clk_init_safe_fmax(struct gm20b_clk *clk)
{
	struct nvkm_subdev *subdev = &clk->base.base.subdev;
	struct nvkm_volt *volt = subdev->device->volt;
	struct nvkm_pstate *pstates = clk->base.base.func->pstates;
	int nr_pstates = clk->base.base.func->nr_pstates;
	int vmin, id = 0;
	u32 fmax = 0;
	int i;

	/* find lowest voltage we can use */
	vmin = volt->vid[0].uv;
	for (i = 1; i < volt->vid_nr; i++) {
		if (volt->vid[i].uv <= vmin) {
			vmin = volt->vid[i].uv;
			id = volt->vid[i].vid;
		}
	}

	/* find max frequency at this voltage */
	for (i = 0; i < nr_pstates; i++)
		if (pstates[i].base.voltage == id)
			fmax = max(fmax,
				   pstates[i].base.domain[nv_clk_src_gpc]);

	if (!fmax) {
		nvkm_error(subdev, "failed to evaluate safe fmax\n");
		return -EINVAL;
	}

	/* we are safe at 90% of the max frequency */
	clk->safe_fmax_vmin = fmax * (100 - 10) / 100;
	nvkm_debug(subdev, "safe fmax @ vmin = %u Khz\n", clk->safe_fmax_vmin);

	return 0;
}

int
gm20b_clk_new(struct nvkm_device *device, int index, struct nvkm_clk **pclk)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	struct gm20b_clk *clk;
	struct nvkm_subdev *subdev;
	struct gk20a_clk_pllg_params *clk_params;
	int ret;

	/* Speedo 0 GPUs cannot use noise-aware PLL */
	if (tdev->gpu_speedo_id == 0)
		return gm20b_clk_new_speedo0(device, index, pclk);

	/* Speedo >= 1, use NAPLL */
	clk = kzalloc(sizeof(*clk) + sizeof(*clk_params), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;
	*pclk = &clk->base.base;
	subdev = &clk->base.base.subdev;

	/* duplicate the clock parameters since we will patch them below */
	clk_params = (void *) (clk + 1);
	*clk_params = gm20b_pllg_params;
	ret = gk20a_clk_ctor(device, index, &gm20b_clk, clk_params,
			     &clk->base);
	if (ret)
		return ret;

	/*
	 * NAPLL can only work with max_u, clamp the m range so
	 * gk20a_pllg_calc_mnp always uses it
	 */
	clk_params->max_m = clk_params->min_m = DIV_ROUND_UP(clk_params->max_u,
						(clk->base.parent_rate / KHZ));
	if (clk_params->max_m == 0) {
		nvkm_warn(subdev, "cannot use NAPLL, using legacy clock...\n");
		kfree(clk);
		return gm20b_clk_new_speedo0(device, index, pclk);
	}

	clk->base.pl_to_div = pl_to_div;
	clk->base.div_to_pl = div_to_pl;

	clk->dvfs_params = &gm20b_dvfs_params;

	ret = gm20b_clk_init_fused_params(clk);
	/*
	 * we will calibrate during init - should never happen on
	 * prod parts
	 */
	if (ret)
		nvkm_warn(subdev, "no fused calibration parameters\n");

	ret = gm20b_clk_init_safe_fmax(clk);
	if (ret)
		return ret;

	return 0;
}
