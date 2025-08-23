/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CLK_GP10B_H__
#define __NVKM_CLK_GP10B_H__

struct gp10b_clk {
	/* currently applied parameters */
	struct nvkm_clk base;
	struct clk *clk;
	u32 rate;

	/* new parameters to apply */
	u32 new_rate;
};

#define gp10b_clk(p) container_of((p), struct gp10b_clk, base)

#endif
