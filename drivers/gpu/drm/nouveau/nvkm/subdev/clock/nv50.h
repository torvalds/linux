#ifndef __NVKM_CLK_NV50_H__
#define __NVKM_CLK_NV50_H__

#include <subdev/bus.h>
#include <subdev/bus/hwsq.h>
#include <subdev/clock.h>

struct nv50_clock_hwsq {
	struct hwsq base;
	struct hwsq_reg r_fifo;
	struct hwsq_reg r_spll[2];
	struct hwsq_reg r_nvpll[2];
	struct hwsq_reg r_divs;
	struct hwsq_reg r_mast;
};

struct nv50_clock_priv {
	struct nouveau_clock base;
	struct nv50_clock_hwsq hwsq;
};

int  nv50_clock_ctor(struct nouveau_object *, struct nouveau_object *,
		     struct nouveau_oclass *, void *, u32,
		     struct nouveau_object **);

struct nv50_clock_oclass {
	struct nouveau_oclass base;
	struct nouveau_clocks *domains;
};

#endif
