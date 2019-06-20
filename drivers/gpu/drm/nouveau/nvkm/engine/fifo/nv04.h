/* SPDX-License-Identifier: MIT */
#ifndef __NV04_FIFO_H__
#define __NV04_FIFO_H__
#define nv04_fifo(p) container_of((p), struct nv04_fifo, base)
#include "priv.h"

struct nv04_fifo_ramfc {
	unsigned bits:6;
	unsigned ctxs:5;
	unsigned ctxp:8;
	unsigned regs:5;
	unsigned regp;
};

struct nv04_fifo {
	struct nvkm_fifo base;
	const struct nv04_fifo_ramfc *ramfc;
};

int nv04_fifo_new_(const struct nvkm_fifo_func *, struct nvkm_device *,
		   int index, int nr, const struct nv04_fifo_ramfc *,
		   struct nvkm_fifo **);
void nv04_fifo_init(struct nvkm_fifo *);
#endif
