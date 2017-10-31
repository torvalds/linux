#ifndef __NV50_FIFO_CHAN_H__
#define __NV50_FIFO_CHAN_H__
#define nv50_fifo_chan(p) container_of((p), struct nv50_fifo_chan, base)
#include "chan.h"
#include "nv50.h"

struct nv50_fifo_chan {
	struct nv50_fifo *fifo;
	struct nvkm_fifo_chan base;

	struct nvkm_gpuobj *ramfc;
	struct nvkm_gpuobj *cache;
	struct nvkm_gpuobj *eng;
	struct nvkm_gpuobj *pgd;
	struct nvkm_ramht *ramht;

	struct nvkm_gpuobj *engn[NVKM_SUBDEV_NR];
};

int nv50_fifo_chan_ctor(struct nv50_fifo *, u64 vmm, u64 push,
			const struct nvkm_oclass *, struct nv50_fifo_chan *);
void *nv50_fifo_chan_dtor(struct nvkm_fifo_chan *);
void nv50_fifo_chan_fini(struct nvkm_fifo_chan *);
void nv50_fifo_chan_engine_dtor(struct nvkm_fifo_chan *, struct nvkm_engine *);
void nv50_fifo_chan_object_dtor(struct nvkm_fifo_chan *, int);

int g84_fifo_chan_ctor(struct nv50_fifo *, u64 vmm, u64 push,
		       const struct nvkm_oclass *, struct nv50_fifo_chan *);

extern const struct nvkm_fifo_chan_oclass nv50_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv50_fifo_gpfifo_oclass;
extern const struct nvkm_fifo_chan_oclass g84_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass g84_fifo_gpfifo_oclass;
#endif
