/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GK104_FIFO_CHAN_H__
#define __GK104_FIFO_CHAN_H__
#define gk104_fifo_chan(p) container_of((p), struct gk104_fifo_chan, base)
#include "chan.h"
#include "gk104.h"

struct gk104_fifo_chan {
	struct nvkm_fifo_chan base;
	struct gk104_fifo *fifo;
	int runl;

	struct nvkm_fifo_cgrp *cgrp;
	struct list_head head;
	bool killed;

	struct nvkm_memory *mthd;

	struct {
		struct nvkm_gpuobj *inst;
		struct nvkm_vma *vma;
	} engn[NVKM_SUBDEV_NR];
};

extern const struct nvkm_fifo_chan_func gk104_fifo_gpfifo_func;

int gk104_fifo_gpfifo_new(struct gk104_fifo *, const struct nvkm_oclass *,
			  void *data, u32 size, struct nvkm_object **);
void *gk104_fifo_gpfifo_dtor(struct nvkm_fifo_chan *);
void gk104_fifo_gpfifo_init(struct nvkm_fifo_chan *);
void gk104_fifo_gpfifo_fini(struct nvkm_fifo_chan *);
int gk104_fifo_gpfifo_engine_ctor(struct nvkm_fifo_chan *, struct nvkm_engine *,
				  struct nvkm_object *);
void gk104_fifo_gpfifo_engine_dtor(struct nvkm_fifo_chan *,
				   struct nvkm_engine *);
int gk104_fifo_gpfifo_kick(struct gk104_fifo_chan *);
int gk104_fifo_gpfifo_kick_locked(struct gk104_fifo_chan *);

int gv100_fifo_gpfifo_new(struct gk104_fifo *, const struct nvkm_oclass *,
			  void *data, u32 size, struct nvkm_object **);
int gv100_fifo_gpfifo_new_(const struct nvkm_fifo_chan_func *,
			   struct gk104_fifo *, u64 *, u16 *, u64, u64, u64,
			   u64 *, bool, u32 *, const struct nvkm_oclass *,
			   struct nvkm_object **);
int gv100_fifo_gpfifo_engine_init(struct nvkm_fifo_chan *,
				  struct nvkm_engine *);
int gv100_fifo_gpfifo_engine_fini(struct nvkm_fifo_chan *,
				  struct nvkm_engine *, bool);

int tu104_fifo_gpfifo_new(struct gk104_fifo *, const struct nvkm_oclass *,
			  void *data, u32 size, struct nvkm_object **);
#endif
