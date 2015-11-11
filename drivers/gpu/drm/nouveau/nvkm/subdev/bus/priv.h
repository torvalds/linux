#ifndef __NVKM_BUS_PRIV_H__
#define __NVKM_BUS_PRIV_H__
#define nvkm_bus(p) container_of((p), struct nvkm_bus, subdev)
#include <subdev/bus.h>

struct nvkm_bus_func {
	void (*init)(struct nvkm_bus *);
	void (*intr)(struct nvkm_bus *);
	int (*hwsq_exec)(struct nvkm_bus *, u32 *, u32);
	u32 hwsq_size;
};

int nvkm_bus_new_(const struct nvkm_bus_func *, struct nvkm_device *, int,
		  struct nvkm_bus **);

void nv50_bus_init(struct nvkm_bus *);
void nv50_bus_intr(struct nvkm_bus *);
#endif
