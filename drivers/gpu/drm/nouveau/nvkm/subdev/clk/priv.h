/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CLK_PRIV_H__
#define __NVKM_CLK_PRIV_H__
#define nvkm_clk(p) container_of((p), struct nvkm_clk, subdev)
#include <subdev/clk.h>

struct nvkm_clk_func {
	int (*init)(struct nvkm_clk *);
	void (*fini)(struct nvkm_clk *);
	int (*read)(struct nvkm_clk *, enum nv_clk_src);
	int (*calc)(struct nvkm_clk *, struct nvkm_cstate *);
	int (*prog)(struct nvkm_clk *);
	void (*tidy)(struct nvkm_clk *);
	struct nvkm_pstate *pstates;
	int nr_pstates;
	struct nvkm_domain domains[];
};

int nvkm_clk_ctor(const struct nvkm_clk_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  bool allow_reclock, struct nvkm_clk *);
int nvkm_clk_new_(const struct nvkm_clk_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  bool allow_reclock, struct nvkm_clk **);

int nv04_clk_pll_calc(struct nvkm_clk *, struct nvbios_pll *, int clk,
		      struct nvkm_pll_vals *);
int nv04_clk_pll_prog(struct nvkm_clk *, u32 reg1, struct nvkm_pll_vals *);
#endif
