#ifndef __NVKM_MC_NV04_H__
#define __NVKM_MC_NV04_H__
#include "priv.h"

struct nv04_mc_priv {
	struct nvkm_mc base;
};

int  nv04_mc_ctor(struct nvkm_object *, struct nvkm_object *,
		  struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);

extern const struct nvkm_mc_intr nv04_mc_intr[];
int  nv04_mc_init(struct nvkm_object *);
void nv40_mc_msi_rearm(struct nvkm_mc *);
int  nv44_mc_init(struct nvkm_object *object);
int  nv50_mc_init(struct nvkm_object *);
extern const struct nvkm_mc_intr nv50_mc_intr[];
extern const struct nvkm_mc_intr gf100_mc_intr[];
#endif
