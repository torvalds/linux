/* SPDX-License-Identifier: MIT */
#ifndef __NV50_CLK_H__
#define __NV50_CLK_H__
#define nv50_clk(p) container_of((p), struct nv50_clk, base)
#include "priv.h"

#include <subdev/bus/hwsq.h>

struct nv50_clk_hwsq {
	struct hwsq base;
	struct hwsq_reg r_fifo;
	struct hwsq_reg r_spll[2];
	struct hwsq_reg r_nvpll[2];
	struct hwsq_reg r_divs;
	struct hwsq_reg r_mast;
};

struct nv50_clk {
	struct nvkm_clk base;
	struct nv50_clk_hwsq hwsq;
};

int nv50_clk_new_(const struct nvkm_clk_func *, struct nvkm_device *, int,
		  bool, struct nvkm_clk **);
int nv50_clk_read(struct nvkm_clk *, enum nv_clk_src);
int nv50_clk_calc(struct nvkm_clk *, struct nvkm_cstate *);
int nv50_clk_prog(struct nvkm_clk *);
void nv50_clk_tidy(struct nvkm_clk *);
#endif
