#ifndef __NVKM_DEVINIT_NV50_H__
#define __NVKM_DEVINIT_NV50_H__

#include "priv.h"

struct nv50_devinit_priv {
	struct nouveau_devinit base;
	u32 r001540;
};

int  nv50_devinit_ctor(struct nouveau_object *, struct nouveau_object *,
		       struct nouveau_oclass *, void *, u32,
		       struct nouveau_object **);
int  nv50_devinit_init(struct nouveau_object *);
int  nv50_devinit_pll_set(struct nouveau_devinit *, u32, u32);

int  nva3_devinit_pll_set(struct nouveau_devinit *, u32, u32);

int  nvc0_devinit_pll_set(struct nouveau_devinit *, u32, u32);

#endif
