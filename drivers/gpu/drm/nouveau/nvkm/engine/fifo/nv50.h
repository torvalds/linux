#ifndef __NV50_FIFO_H__
#define __NV50_FIFO_H__
#define nv50_fifo(p) container_of((p), struct nv50_fifo, base)
#include "priv.h"

struct nv50_fifo {
	struct nvkm_fifo base;
	struct nvkm_memory *runlist[2];
	int cur_runlist;
};

void nv50_fifo_dtor(struct nvkm_object *);
int  nv50_fifo_init(struct nvkm_object *);
void nv50_fifo_runlist_update(struct nv50_fifo *);
#endif
