/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DMA_H__
#define __NVKM_DMA_H__
#include <core/engine.h>
#include <core/object.h>
struct nvkm_client;

struct nvkm_dmaobj {
	const struct nvkm_dmaobj_func *func;
	struct nvkm_dma *dma;

	struct nvkm_object object;
	u32 target;
	u32 access;
	u64 start;
	u64 limit;
};

struct nvkm_dma {
	const struct nvkm_dma_func *func;
	struct nvkm_engine engine;
};

struct nvkm_dmaobj *nvkm_dmaobj_search(struct nvkm_client *, u64 object);

int nv04_dma_new(struct nvkm_device *, int, struct nvkm_dma **);
int nv50_dma_new(struct nvkm_device *, int, struct nvkm_dma **);
int gf100_dma_new(struct nvkm_device *, int, struct nvkm_dma **);
int gf119_dma_new(struct nvkm_device *, int, struct nvkm_dma **);
int gv100_dma_new(struct nvkm_device *, int, struct nvkm_dma **);
#endif
