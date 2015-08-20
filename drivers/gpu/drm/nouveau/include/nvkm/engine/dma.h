#ifndef __NVKM_DMA_H__
#define __NVKM_DMA_H__
#include <core/engine.h>
struct nvkm_client;
struct nvkm_gpuobj;

struct nvkm_dmaobj {
	const struct nvkm_dmaobj_func *func;
	struct nvkm_dma *dma;

	struct nvkm_object object;
	u32 target;
	u32 access;
	u64 start;
	u64 limit;

	struct rb_node rb;
	u64 handle; /*XXX HANDLE MERGE */
};

struct nvkm_dmaobj_func {
	int (*bind)(struct nvkm_dmaobj *, struct nvkm_gpuobj *, int align,
		    struct nvkm_gpuobj **);
};

struct nvkm_dma {
	struct nvkm_engine engine;
};

struct nvkm_dmaobj *
nvkm_dma_search(struct nvkm_dma *, struct nvkm_client *, u64 object);

extern struct nvkm_oclass *nv04_dmaeng_oclass;
extern struct nvkm_oclass *nv50_dmaeng_oclass;
extern struct nvkm_oclass *gf100_dmaeng_oclass;
extern struct nvkm_oclass *gf110_dmaeng_oclass;
#endif
