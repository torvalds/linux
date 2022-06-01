/* SPDX-License-Identifier: MIT */
#ifndef __NV04_FIFO_H__
#define __NV04_FIFO_H__
#define nv04_fifo(p) container_of((p), struct nv04_fifo, base)
#include "priv.h"

#define nv04_fifo_ramfc nvkm_ramfc_layout

struct nv04_fifo {
	struct nvkm_fifo base;
};

int nv04_fifo_new_(const struct nvkm_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   int nr, const struct nv04_fifo_ramfc *, struct nvkm_fifo **);
#endif
