/* SPDX-License-Identifier: MIT */
#ifndef __NV04_FIFO_CHAN_H__
#define __NV04_FIFO_CHAN_H__
#define nv04_fifo_chan(p) container_of((p), struct nv04_fifo_chan, base)
#include "chan.h"
#include "nv04.h"

struct nv04_fifo_chan {
	struct nvkm_fifo_chan base;
#define NV04_FIFO_ENGN_SW   0
#define NV04_FIFO_ENGN_GR   1
#define NV04_FIFO_ENGN_MPEG 2
#define NV04_FIFO_ENGN_DMA  3
};

extern const struct nvkm_fifo_chan_func nv04_fifo_dma_func;
void *nv04_fifo_dma_dtor(struct nvkm_fifo_chan *);

extern const struct nvkm_fifo_chan_oclass nv04_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv10_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv17_fifo_dma_oclass;
extern const struct nvkm_fifo_chan_oclass nv40_fifo_dma_oclass;
#endif
