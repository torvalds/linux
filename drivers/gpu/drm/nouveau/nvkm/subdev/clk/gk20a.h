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

#define GK20A_CLK_GPC_MDIV 1000

#define SYS_GPCPLL_CFG_BASE	0x00137000

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

int _gk20a_clk_ctor(struct nvkm_device *, int, const struct nvkm_clk_func *,
		    const struct gk20a_clk_pllg_params *, struct gk20a_clk *);
void gk20a_clk_fini(struct nvkm_clk *);
int gk20a_clk_read(struct nvkm_clk *, enum nv_clk_src);
int gk20a_clk_calc(struct nvkm_clk *, struct nvkm_cstate *);
int gk20a_clk_prog(struct nvkm_clk *);
void gk20a_clk_tidy(struct nvkm_clk *);

#endif
