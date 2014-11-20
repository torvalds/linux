#ifndef __NVKM_MC_NV04_H__
#define __NVKM_MC_NV04_H__

#include "priv.h"

struct nv04_mc_priv {
	struct nouveau_mc base;
};

int  nv04_mc_ctor(struct nouveau_object *, struct nouveau_object *,
		  struct nouveau_oclass *, void *, u32,
		  struct nouveau_object **);

extern const struct nouveau_mc_intr nv04_mc_intr[];
int  nv04_mc_init(struct nouveau_object *);
void nv40_mc_msi_rearm(struct nouveau_mc *);
int  nv44_mc_init(struct nouveau_object *object);
int  nv50_mc_init(struct nouveau_object *);
extern const struct nouveau_mc_intr nv50_mc_intr[];
extern const struct nouveau_mc_intr nvc0_mc_intr[];

#endif
