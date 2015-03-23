#ifndef __NVKM_MC_H__
#define __NVKM_MC_H__
#include <core/subdev.h>

struct nvkm_mc {
	struct nvkm_subdev base;
	bool use_msi;
	unsigned int irq;
	void (*unk260)(struct nvkm_mc *, u32);
};

static inline struct nvkm_mc *
nvkm_mc(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_MC);
}

extern struct nvkm_oclass *nv04_mc_oclass;
extern struct nvkm_oclass *nv40_mc_oclass;
extern struct nvkm_oclass *nv44_mc_oclass;
extern struct nvkm_oclass *nv4c_mc_oclass;
extern struct nvkm_oclass *nv50_mc_oclass;
extern struct nvkm_oclass *g94_mc_oclass;
extern struct nvkm_oclass *g98_mc_oclass;
extern struct nvkm_oclass *gf100_mc_oclass;
extern struct nvkm_oclass *gf106_mc_oclass;
extern struct nvkm_oclass *gk20a_mc_oclass;
#endif
