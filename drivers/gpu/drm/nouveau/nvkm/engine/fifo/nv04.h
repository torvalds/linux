#ifndef __NV04_FIFO_H__
#define __NV04_FIFO_H__
#define nv04_fifo(p) container_of((p), struct nv04_fifo, base)
#include "priv.h"

struct ramfc_desc {
	unsigned bits:6;
	unsigned ctxs:5;
	unsigned ctxp:8;
	unsigned regs:5;
	unsigned regp;
};

struct nv04_fifo {
	struct nvkm_fifo base;
	struct ramfc_desc *ramfc_desc;
};

void nv04_fifo_dtor(struct nvkm_object *);
int  nv04_fifo_init(struct nvkm_object *);
#endif
