#ifndef __NVKM_DEVINIT_PRIV_H__
#define __NVKM_DEVINIT_PRIV_H__

#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/clock/pll.h>
#include <subdev/devinit.h>

void nv04_devinit_dtor(struct nouveau_object *);
int  nv04_devinit_init(struct nouveau_object *);
int  nv04_devinit_fini(struct nouveau_object *, bool);
int  nv04_devinit_pll_set(struct nouveau_devinit *, u32, u32);

void setPLL_single(struct nouveau_devinit *, u32, struct nouveau_pll_vals *);
void setPLL_double_highregs(struct nouveau_devinit *, u32, struct nouveau_pll_vals *);
void setPLL_double_lowregs(struct nouveau_devinit *, u32, struct nouveau_pll_vals *);


struct nv50_devinit_priv {
	struct nouveau_devinit base;
};

int  nv50_devinit_init(struct nouveau_object *);

#endif
