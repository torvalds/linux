#ifndef __NVKM_DEVINIT_NV04_H__
#define __NVKM_DEVINIT_NV04_H__

#include "priv.h"

struct nv04_devinit_priv {
	struct nouveau_devinit base;
	u8 owner;
};

int  nv04_devinit_ctor(struct nouveau_object *, struct nouveau_object *,
		       struct nouveau_oclass *, void *, u32,
		       struct nouveau_object **);
void nv04_devinit_dtor(struct nouveau_object *);
int  nv04_devinit_init(struct nouveau_object *);
int  nv04_devinit_fini(struct nouveau_object *, bool);
int  nv04_devinit_pll_set(struct nouveau_devinit *, u32, u32);

void setPLL_single(struct nouveau_devinit *, u32, struct nouveau_pll_vals *);
void setPLL_double_highregs(struct nouveau_devinit *, u32, struct nouveau_pll_vals *);
void setPLL_double_lowregs(struct nouveau_devinit *, u32, struct nouveau_pll_vals *);

#endif
