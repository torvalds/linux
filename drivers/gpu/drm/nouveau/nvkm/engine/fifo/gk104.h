#ifndef __GK104_FIFO_H__
#define __GK104_FIFO_H__
#include "priv.h"

struct gk104_fifo_engn {
	struct nvkm_memory *runlist[2];
	int cur_runlist;
	wait_queue_head_t wait;
};

struct gk104_fifo {
	struct nvkm_fifo base;

	struct work_struct fault;
	u64 mask;

	struct gk104_fifo_engn engine[7];
	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma bar;
	} user;
	int spoon_nr;
};

struct gk104_fifo_impl {
	struct nvkm_oclass base;
	u32 channels;
};

int  gk104_fifo_ctor(struct nvkm_object *, struct nvkm_object *,
		    struct nvkm_oclass *, void *, u32,
		    struct nvkm_object **);
void gk104_fifo_dtor(struct nvkm_object *);
int  gk104_fifo_init(struct nvkm_object *);
int  gk104_fifo_fini(struct nvkm_object *, bool);
void gk104_fifo_runlist_update(struct gk104_fifo *, u32 engine);

int  gm204_fifo_ctor(struct nvkm_object *, struct nvkm_object *,
		    struct nvkm_oclass *, void *, u32,
		    struct nvkm_object **);
#endif
