#ifndef __NV04_FIFO_CHAN_H__
#define __NV04_FIFO_CHAN_H__
#define nv04_fifo_chan(p) container_of((p), struct nv04_fifo_chan, base)
#include "chan.h"
#include "nv04.h"

struct nv04_fifo_chan {
	struct nvkm_fifo_chan base;
	struct nv04_fifo *fifo;
	u32 ramfc;
	struct nvkm_gpuobj *engn[NVKM_SUBDEV_NR];
};

extern const struct nvkm_fifo_chan_func nv04_fifo_dma_func;
void *nv04_fifo_dma_dtor(struct nvkm_fifo_chan *);
void nv04_fifo_dma_init(struct nvkm_fifo_chan *);
void nv04_fifo_dma_fini(struct nvkm_fifo_chan *);
void nv04_fifo_dma_object_dtor(struct nvkm_fifo_chan *, int);

extern const struct nvkm_fifo_chan_oclass nv04_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv10_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv17_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv40_fifo_dma_oclass;
#endif
