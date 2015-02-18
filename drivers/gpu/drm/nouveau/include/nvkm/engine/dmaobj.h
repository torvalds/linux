#ifndef __NVKM_DMAOBJ_H__
#define __NVKM_DMAOBJ_H__
#include <core/engine.h>
struct nvkm_gpuobj;

struct nvkm_dmaobj {
	struct nvkm_object base;
	u32 target;
	u32 access;
	u64 start;
	u64 limit;
};

struct nvkm_dmaeng {
	struct nvkm_engine base;

	/* creates a "physical" dma object from a struct nvkm_dmaobj */
	int (*bind)(struct nvkm_dmaobj *dmaobj, struct nvkm_object *parent,
		    struct nvkm_gpuobj **);
};

extern struct nvkm_oclass *nv04_dmaeng_oclass;
extern struct nvkm_oclass *nv50_dmaeng_oclass;
extern struct nvkm_oclass *gf100_dmaeng_oclass;
extern struct nvkm_oclass *gf110_dmaeng_oclass;
#endif
