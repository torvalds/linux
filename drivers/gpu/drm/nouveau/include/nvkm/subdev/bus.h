#ifndef __NVKM_BUS_H__
#define __NVKM_BUS_H__
#include <core/subdev.h>

struct nvkm_bus {
	const struct nvkm_bus_func *func;
	struct nvkm_subdev subdev;
};

/* interface to sequencer */
struct nvkm_hwsq;
int  nvkm_hwsq_init(struct nvkm_subdev *, struct nvkm_hwsq **);
int  nvkm_hwsq_fini(struct nvkm_hwsq **, bool exec);
void nvkm_hwsq_wr32(struct nvkm_hwsq *, u32 addr, u32 data);
void nvkm_hwsq_setf(struct nvkm_hwsq *, u8 flag, int data);
void nvkm_hwsq_wait(struct nvkm_hwsq *, u8 flag, u8 data);
void nvkm_hwsq_wait_vblank(struct nvkm_hwsq *);
void nvkm_hwsq_nsec(struct nvkm_hwsq *, u32 nsec);

int nv04_bus_new(struct nvkm_device *, int, struct nvkm_bus **);
int nv31_bus_new(struct nvkm_device *, int, struct nvkm_bus **);
int nv50_bus_new(struct nvkm_device *, int, struct nvkm_bus **);
int g94_bus_new(struct nvkm_device *, int, struct nvkm_bus **);
int gf100_bus_new(struct nvkm_device *, int, struct nvkm_bus **);
#endif
