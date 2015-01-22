#ifndef __NVKM_BUS_NV04_H__
#define __NVKM_BUS_NV04_H__
#include <subdev/bus.h>

struct nv04_bus_priv {
	struct nvkm_bus base;
};

int  nv04_bus_ctor(struct nvkm_object *, struct nvkm_object *,
		   struct nvkm_oclass *, void *, u32,
		   struct nvkm_object **);
int  nv50_bus_init(struct nvkm_object *);
void nv50_bus_intr(struct nvkm_subdev *);

struct nv04_bus_impl {
	struct nvkm_oclass base;
	void (*intr)(struct nvkm_subdev *);
	int  (*hwsq_exec)(struct nvkm_bus *, u32 *, u32);
	u32  hwsq_size;
};
#endif
