// SPDX-License-Identifier: MIT
#include <subdev/clk.h>
#include <subdev/timer.h>
#include <core/device.h>
#include <core/tegra.h>

#include "priv.h"
#include "gk20a_devfreq.h"
#include "gk20a.h"
#include "gp10b.h"

static int
gp10b_clk_init(struct nvkm_clk *base)
{
	struct gp10b_clk *clk = gp10b_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	int ret;

	/* Start with the highest frequency, matching the BPMP default */
	base->func->calc(base, &base->func->pstates[base->func->nr_pstates - 1].base);
	ret = base->func->prog(base);
	if (ret) {
		nvkm_error(subdev, "cannot initialize clock\n");
		return ret;
	}

	ret = gk20a_devfreq_init(base, &clk->devfreq);
	if (ret)
		return ret;

	return 0;
}

static int
gp10b_clk_read(struct nvkm_clk *base, enum nv_clk_src src)
{
	struct gp10b_clk *clk = gp10b_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;

	switch (src) {
	case nv_clk_src_gpc:
		return clk_get_rate(clk->clk) / GK20A_CLK_GPC_MDIV;
	default:
		nvkm_error(subdev, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

static int
gp10b_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct gp10b_clk *clk = gp10b_clk(base);
	u32 target_rate = cstate->domain[nv_clk_src_gpc] * GK20A_CLK_GPC_MDIV;

	clk->new_rate = clk_round_rate(clk->clk, target_rate) / GK20A_CLK_GPC_MDIV;

	return 0;
}

static int
gp10b_clk_prog(struct nvkm_clk *base)
{
	struct gp10b_clk *clk = gp10b_clk(base);
	int ret;

	ret = clk_set_rate(clk->clk, clk->new_rate * GK20A_CLK_GPC_MDIV);
	if (ret < 0)
		return ret;

	clk->rate = clk_get_rate(clk->clk) / GK20A_CLK_GPC_MDIV;

	return 0;
}

static struct nvkm_pstate
gp10b_pstates[] = {
	{
		.base = {
			.domain[nv_clk_src_gpc] = 114750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 216750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 318750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 420750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 522750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 624750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 726750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 828750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 930750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 1032750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 1134750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 1236750,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 1300500,
		},
	},
};

static const struct nvkm_clk_func
gp10b_clk = {
	.init = gp10b_clk_init,
	.read = gp10b_clk_read,
	.calc = gp10b_clk_calc,
	.prog = gp10b_clk_prog,
	.tidy = gk20a_clk_tidy,
	.pstates = gp10b_pstates,
	.nr_pstates = ARRAY_SIZE(gp10b_pstates),
	.domains = {
		{ nv_clk_src_gpc, 0xff, 0, "core", GK20A_CLK_GPC_MDIV },
		{ nv_clk_src_max }
	}
};

int
gp10b_clk_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_clk **pclk)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	const struct nvkm_clk_func *func = &gp10b_clk;
	struct gp10b_clk *clk;
	int ret, i;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;
	*pclk = &clk->base;
	clk->clk = tdev->clk;

	/* Finish initializing the pstates */
	for (i = 0; i < func->nr_pstates; i++) {
		INIT_LIST_HEAD(&func->pstates[i].list);
		func->pstates[i].pstate = i + 1;
	}

	ret = nvkm_clk_ctor(func, device, type, inst, true, &clk->base);
	if (ret)
		return ret;

	return 0;
}
