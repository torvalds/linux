/* SPDX-License-Identifier: MIT */
#ifndef __GK104_FIFO_H__
#define __GK104_FIFO_H__
#define gk104_fifo(p) container_of((p), struct gk104_fifo, base)
#include "priv.h"
struct nvkm_fifo_cgrp;

#include <subdev/mmu.h>

#define gk104_fifo_func nvkm_fifo_func

struct gk104_fifo_chan;
struct gk104_fifo {
	const struct gk104_fifo_func *func;
	struct nvkm_fifo base;

	struct {
		u32 engm;
		u32 engm_sw;
	} runlist[16];
	int runlist_nr;
};

int gk104_fifo_new_(const struct gk104_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type,
		    int index, int nr, struct nvkm_fifo **);
void *gk104_fifo_dtor(struct nvkm_fifo *base);
int gk104_fifo_oneinit(struct nvkm_fifo *);
#endif
