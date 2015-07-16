#ifndef __NVKM_DEVINIT_H__
#define __NVKM_DEVINIT_H__
#include <core/subdev.h>

struct nvkm_devinit {
	struct nvkm_subdev base;
	bool post;
	void (*meminit)(struct nvkm_devinit *);
	int  (*pll_set)(struct nvkm_devinit *, u32 type, u32 freq);
	u32  (*mmio)(struct nvkm_devinit *, u32 addr);
};

static inline struct nvkm_devinit *
nvkm_devinit(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_DEVINIT);
}

extern struct nvkm_oclass *nv04_devinit_oclass;
extern struct nvkm_oclass *nv05_devinit_oclass;
extern struct nvkm_oclass *nv10_devinit_oclass;
extern struct nvkm_oclass *nv1a_devinit_oclass;
extern struct nvkm_oclass *nv20_devinit_oclass;
extern struct nvkm_oclass *nv50_devinit_oclass;
extern struct nvkm_oclass *g84_devinit_oclass;
extern struct nvkm_oclass *g98_devinit_oclass;
extern struct nvkm_oclass *gt215_devinit_oclass;
extern struct nvkm_oclass *mcp89_devinit_oclass;
extern struct nvkm_oclass *gf100_devinit_oclass;
extern struct nvkm_oclass *gm107_devinit_oclass;
extern struct nvkm_oclass *gm204_devinit_oclass;
#endif
