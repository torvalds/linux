/* SPDX-License-Identifier: MIT */
#ifndef __NV50_FIFO_H__
#define __NV50_FIFO_H__
#define nv50_fifo(p) container_of((p), struct nv50_fifo, base)
#include "priv.h"

struct nv50_fifo {
	struct nvkm_fifo base;
};

int nv50_fifo_new_(const struct nvkm_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_fifo **);

void *nv50_fifo_dtor(struct nvkm_fifo *);
#endif
