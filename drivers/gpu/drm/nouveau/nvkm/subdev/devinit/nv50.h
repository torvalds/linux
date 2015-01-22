#ifndef __NVKM_DEVINIT_NV50_H__
#define __NVKM_DEVINIT_NV50_H__
#include "priv.h"

struct nv50_devinit_priv {
	struct nvkm_devinit base;
	u32 r001540;
};

int  nv50_devinit_ctor(struct nvkm_object *, struct nvkm_object *,
		       struct nvkm_oclass *, void *, u32,
		       struct nvkm_object **);
int  nv50_devinit_init(struct nvkm_object *);
int  nv50_devinit_pll_set(struct nvkm_devinit *, u32, u32);

int  gt215_devinit_pll_set(struct nvkm_devinit *, u32, u32);

int  gf100_devinit_pll_set(struct nvkm_devinit *, u32, u32);

u64  gm107_devinit_disable(struct nvkm_devinit *);
#endif
