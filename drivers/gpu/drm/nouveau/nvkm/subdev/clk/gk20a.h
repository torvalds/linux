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
 *
 */

#ifndef __NVKM_CLK_GK20A_H__
#define __NVKM_CLK_GK20A_H__

#define KHZ (1000)
#define MHZ (KHZ * 1000)

#define MASK(w)	((1 << (w)) - 1)

#define GK20A_CLK_GPC_MDIV 1000

#define SYS_GPCPLL_CFG_BASE	0x00137000
#define GPCPLL_CFG		(SYS_GPCPLL_CFG_BASE + 0)
#define GPCPLL_CFG_ENABLE	BIT(0)
#define GPCPLL_CFG_IDDQ		BIT(1)
#define GPCPLL_CFG_LOCK_DET_OFF	BIT(4)
#define GPCPLL_CFG_LOCK		BIT(17)

#define GPCPLL_CFG2		(SYS_GPCPLL_CFG_BASE + 0xc)
#define GPCPLL_CFG2_SETUP2_SHIFT	16
#define GPCPLL_CFG2_PLL_STEPA_SHIFT	24

#define GPCPLL_CFG3			(SYS_GPCPLL_CFG_BASE + 0x18)
#define GPCPLL_CFG3_VCO_CTRL_SHIFT		0
#define GPCPLL_CFG3_VCO_CTRL_WIDTH		9
#define GPCPLL_CFG3_VCO_CTRL_MASK		\
	(MASK(GPCPLL_CFG3_VCO_CTRL_WIDTH) << GPCPLL_CFG3_VCO_CTRL_SHIFT)
#define GPCPLL_CFG3_PLL_STEPB_SHIFT		16
#define GPCPLL_CFG3_PLL_STEPB_WIDTH		8

#define GPCPLL_COEFF		(SYS_GPCPLL_CFG_BASE + 4)
#define GPCPLL_COEFF_M_SHIFT	0
#define GPCPLL_COEFF_M_WIDTH	8
#define GPCPLL_COEFF_N_SHIFT	8
#define GPCPLL_COEFF_N_WIDTH	8
#define GPCPLL_COEFF_N_MASK	\
	(MASK(GPCPLL_COEFF_N_WIDTH) << GPCPLL_COEFF_N_SHIFT)
#define GPCPLL_COEFF_P_SHIFT	16
#define GPCPLL_COEFF_P_WIDTH	6

#define GPCPLL_NDIV_SLOWDOWN			(SYS_GPCPLL_CFG_BASE + 0x1c)
#define GPCPLL_NDIV_SLOWDOWN_NDIV_LO_SHIFT	0
#define GPCPLL_NDIV_SLOWDOWN_NDIV_MID_SHIFT	8
#define GPCPLL_NDIV_SLOWDOWN_STEP_SIZE_LO2MID_SHIFT	16
#define GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT	22
#define GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT	31

#define GPC_BCAST_GPCPLL_CFG_BASE		0x00132800
#define GPC_BCAST_NDIV_SLOWDOWN_DEBUG	(GPC_BCAST_GPCPLL_CFG_BASE + 0xa0)
#define GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_SHIFT	24
#define GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK \
	(0x1 << GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_SHIFT)

#define SEL_VCO				(SYS_GPCPLL_CFG_BASE + 0x100)
#define SEL_VCO_GPC2CLK_OUT_SHIFT	0

#define GPC2CLK_OUT			(SYS_GPCPLL_CFG_BASE + 0x250)
#define GPC2CLK_OUT_SDIV14_INDIV4_WIDTH	1
#define GPC2CLK_OUT_SDIV14_INDIV4_SHIFT	31
#define GPC2CLK_OUT_SDIV14_INDIV4_MODE	1
#define GPC2CLK_OUT_VCODIV_WIDTH	6
#define GPC2CLK_OUT_VCODIV_SHIFT	8
#define GPC2CLK_OUT_VCODIV1		0
#define GPC2CLK_OUT_VCODIV2		2
#define GPC2CLK_OUT_VCODIV_MASK		(MASK(GPC2CLK_OUT_VCODIV_WIDTH) << \
					GPC2CLK_OUT_VCODIV_SHIFT)
#define GPC2CLK_OUT_BYPDIV_WIDTH	6
#define GPC2CLK_OUT_BYPDIV_SHIFT	0
#define GPC2CLK_OUT_BYPDIV31		0x3c
#define GPC2CLK_OUT_INIT_MASK	((MASK(GPC2CLK_OUT_SDIV14_INDIV4_WIDTH) << \
		GPC2CLK_OUT_SDIV14_INDIV4_SHIFT)\
		| (MASK(GPC2CLK_OUT_VCODIV_WIDTH) << GPC2CLK_OUT_VCODIV_SHIFT)\
		| (MASK(GPC2CLK_OUT_BYPDIV_WIDTH) << GPC2CLK_OUT_BYPDIV_SHIFT))
#define GPC2CLK_OUT_INIT_VAL	((GPC2CLK_OUT_SDIV14_INDIV4_MODE << \
		GPC2CLK_OUT_SDIV14_INDIV4_SHIFT) \
		| (GPC2CLK_OUT_VCODIV1 << GPC2CLK_OUT_VCODIV_SHIFT) \
		| (GPC2CLK_OUT_BYPDIV31 << GPC2CLK_OUT_BYPDIV_SHIFT))

/* All frequencies in Khz */
struct gk20a_clk_pllg_params {
	u32 min_vco, max_vco;
	u32 min_u, max_u;
	u32 min_m, max_m;
	u32 min_n, max_n;
	u32 min_pl, max_pl;
};

struct gk20a_pll {
	u32 m;
	u32 n;
	u32 pl;
};

struct gk20a_clk {
	struct nvkm_clk base;
	const struct gk20a_clk_pllg_params *params;
	struct gk20a_pll pll;
	u32 parent_rate;

	u32 (*div_to_pl)(u32);
	u32 (*pl_to_div)(u32);
};
#define gk20a_clk(p) container_of((p), struct gk20a_clk, base)

u32 gk20a_pllg_calc_rate(struct gk20a_clk *, struct gk20a_pll *);
int gk20a_pllg_calc_mnp(struct gk20a_clk *, unsigned long, struct gk20a_pll *);
void gk20a_pllg_read_mnp(struct gk20a_clk *, struct gk20a_pll *);
void gk20a_pllg_write_mnp(struct gk20a_clk *, const struct gk20a_pll *);

static inline bool
gk20a_pllg_is_enabled(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 val;

	val = nvkm_rd32(device, GPCPLL_CFG);
	return val & GPCPLL_CFG_ENABLE;
}

static inline u32
gk20a_pllg_n_lo(struct gk20a_clk *clk, struct gk20a_pll *pll)
{
	return DIV_ROUND_UP(pll->m * clk->params->min_vco,
			    clk->parent_rate / KHZ);
}

int gk20a_clk_ctor(struct nvkm_device *, enum nvkm_subdev_type, int, const struct nvkm_clk_func *,
		   const struct gk20a_clk_pllg_params *, struct gk20a_clk *);
void gk20a_clk_fini(struct nvkm_clk *);
int gk20a_clk_read(struct nvkm_clk *, enum nv_clk_src);
int gk20a_clk_calc(struct nvkm_clk *, struct nvkm_cstate *);
int gk20a_clk_prog(struct nvkm_clk *);
void gk20a_clk_tidy(struct nvkm_clk *);

int gk20a_clk_setup_slide(struct gk20a_clk *);

#endif
