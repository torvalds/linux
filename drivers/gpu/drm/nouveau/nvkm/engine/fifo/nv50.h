/* SPDX-License-Identifier: MIT */
#ifndef __NV50_FIFO_H__
#define __NV50_FIFO_H__
#define nv50_fifo(p) container_of((p), struct nv50_fifo, base)
#include "priv.h"

struct nv50_fifo {
	struct nvkm_fifo base;
	struct nvkm_memory *runlist[2];
	int cur_runlist;
};

int nv50_fifo_new_(const struct nvkm_fifo_func *, struct nvkm_device *,
		   int index, struct nvkm_fifo **);

void *nv50_fifo_dtor(struct nvkm_fifo *);
int nv50_fifo_oneinit(struct nvkm_fifo *);
void nv50_fifo_init(struct nvkm_fifo *);
void nv50_fifo_runlist_update(struct nv50_fifo *);
#endif
