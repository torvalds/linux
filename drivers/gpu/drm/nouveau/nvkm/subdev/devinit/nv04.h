#ifndef __NVKM_DEVINIT_NV04_H__
#define __NVKM_DEVINIT_NV04_H__
#include "priv.h"
struct nvkm_pll_vals;

struct nv04_devinit_priv {
	struct nvkm_devinit base;
	int owner;
};

int  nv04_devinit_ctor(struct nvkm_object *, struct nvkm_object *,
		       struct nvkm_oclass *, void *, u32,
		       struct nvkm_object **);
void nv04_devinit_dtor(struct nvkm_object *);
int  nv04_devinit_init(struct nvkm_object *);
int  nv04_devinit_fini(struct nvkm_object *, bool);
int  nv04_devinit_pll_set(struct nvkm_devinit *, u32, u32);

void setPLL_single(struct nvkm_devinit *, u32, struct nvkm_pll_vals *);
void setPLL_double_highregs(struct nvkm_devinit *, u32, struct nvkm_pll_vals *);
void setPLL_double_lowregs(struct nvkm_devinit *, u32, struct nvkm_pll_vals *);
#endif
