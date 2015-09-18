#ifndef __NV04_DEVINIT_H__
#define __NV04_DEVINIT_H__
#define nv04_devinit(p) container_of((p), struct nv04_devinit, base)
#include "priv.h"
struct nvkm_pll_vals;

struct nv04_devinit {
	struct nvkm_devinit base;
	int owner;
};

int nv04_devinit_new_(const struct nvkm_devinit_func *, struct nvkm_device *,
		      int, struct nvkm_devinit **);
void *nv04_devinit_dtor(struct nvkm_devinit *);
void nv04_devinit_preinit(struct nvkm_devinit *);
void nv04_devinit_fini(struct nvkm_devinit *);
int  nv04_devinit_pll_set(struct nvkm_devinit *, u32, u32);

void setPLL_single(struct nvkm_devinit *, u32, struct nvkm_pll_vals *);
void setPLL_double_highregs(struct nvkm_devinit *, u32, struct nvkm_pll_vals *);
void setPLL_double_lowregs(struct nvkm_devinit *, u32, struct nvkm_pll_vals *);
#endif
