/* SPDX-License-Identifier: MIT */
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

#define NV50_FIFO_ENGN_SW   0
#define NV50_FIFO_ENGN_GR   1
#define NV50_FIFO_ENGN_MPEG 2
#define NV50_FIFO_ENGN_DMA  3

#define G84_FIFO_ENGN_SW     0
#define G84_FIFO_ENGN_GR     1
#define G84_FIFO_ENGN_MPEG   2
#define G84_FIFO_ENGN_MSPPP  2
#define G84_FIFO_ENGN_ME     3
#define G84_FIFO_ENGN_CE0    3
#define G84_FIFO_ENGN_VP     4
#define G84_FIFO_ENGN_MSPDEC 4
#define G84_FIFO_ENGN_CIPHER 5
#define G84_FIFO_ENGN_SEC    5
#define G84_FIFO_ENGN_VIC    5
#define G84_FIFO_ENGN_BSP    6
#define G84_FIFO_ENGN_MSVLD  6
#define G84_FIFO_ENGN_DMA    7
	struct nvkm_gpuobj *engn[NVKM_FIFO_ENGN_NR];
};

int nv50_fifo_chan_ctor(struct nv50_fifo *, u64 vmm, u64 push,
			const struct nvkm_oclass *, struct nv50_fifo_chan *);
void *nv50_fifo_chan_dtor(struct nvkm_fifo_chan *);
void nv50_fifo_chan_fini(struct nvkm_fifo_chan *);
struct nvkm_gpuobj **nv50_fifo_chan_engine(struct nv50_fifo_chan *, struct nvkm_engine *);
void nv50_fifo_chan_engine_dtor(struct nvkm_fifo_chan *, struct nvkm_engine *);
void nv50_fifo_chan_object_dtor(struct nvkm_fifo_chan *, int);

int g84_fifo_chan_ctor(struct nv50_fifo *, u64 vmm, u64 push,
		       const struct nvkm_oclass *, struct nv50_fifo_chan *);

extern const struct nvkm_fifo_chan_oclass nv50_fifo_gpfifo_oclass;
extern const struct nvkm_fifo_chan_oclass g84_fifo_gpfifo_oclass;
#endif
