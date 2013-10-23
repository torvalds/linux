#ifndef __NVKM_BUS_NV04_H__
#define __NVKM_BUS_NV04_H__

#include <subdev/bus.h>

struct nv04_bus_priv {
	struct nouveau_bus base;
};

int nv04_bus_ctor(struct nouveau_object *, struct nouveau_object *,
		  struct nouveau_oclass *, void *, u32,
		  struct nouveau_object **);

struct nv04_bus_impl {
	struct nouveau_oclass base;
	void (*intr)(struct nouveau_subdev *);
};

#endif
