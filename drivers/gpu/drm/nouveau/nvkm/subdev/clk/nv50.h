#ifndef __NVKM_CLK_NV50_H__
#define __NVKM_CLK_NV50_H__
#include <subdev/bus/hwsq.h>
#include <subdev/clk.h>

struct nv50_clk_hwsq {
	struct hwsq base;
	struct hwsq_reg r_fifo;
	struct hwsq_reg r_spll[2];
	struct hwsq_reg r_nvpll[2];
	struct hwsq_reg r_divs;
	struct hwsq_reg r_mast;
};

struct nv50_clk_priv {
	struct nvkm_clk base;
	struct nv50_clk_hwsq hwsq;
};

int  nv50_clk_ctor(struct nvkm_object *, struct nvkm_object *,
		     struct nvkm_oclass *, void *, u32,
		     struct nvkm_object **);

struct nv50_clk_oclass {
	struct nvkm_oclass base;
	struct nvkm_domain *domains;
};
#endif
