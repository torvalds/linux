#ifndef __NVKM_DMA_PRIV_H__
#define __NVKM_DMA_PRIV_H__
#define nvkm_dma(p) container_of((p), struct nvkm_dma, engine)
#include <engine/dma.h>

int _nvkm_dma_ctor(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, void *, u32,
		      struct nvkm_object **);
#define _nvkm_dma_dtor _nvkm_engine_dtor
#define _nvkm_dma_init _nvkm_engine_init
#define _nvkm_dma_fini _nvkm_engine_fini

struct nvkm_dma_impl {
	struct nvkm_oclass base;
	struct nvkm_oclass *sclass;
	int (*bind)(struct nvkm_dmaobj *, struct nvkm_gpuobj *,
		    struct nvkm_gpuobj **);
	int (*class_new)(struct nvkm_dma *, const struct nvkm_oclass *,
			 void *data, u32 size, struct nvkm_dmaobj **);
};
#endif
