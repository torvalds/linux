#ifndef __NVKM_BUS_NV04_H__
#define __NVKM_BUS_NV04_H__

#include <subdev/bus.h>

struct nv04_bus_priv {
	struct nouveau_bus base;
};

int  nv04_bus_ctor(struct nouveau_object *, struct nouveau_object *,
		   struct nouveau_oclass *, void *, u32,
		   struct nouveau_object **);
int  nv50_bus_init(struct nouveau_object *);
void nv50_bus_intr(struct nouveau_subdev *);

struct nv04_bus_impl {
	struct nouveau_oclass base;
	void (*intr)(struct nouveau_subdev *);
	int  (*hwsq_exec)(struct nouveau_bus *, u32 *, u32);
	u32  hwsq_size;
};

#endif
