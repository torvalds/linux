/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CHAN_H__
#define __NVKM_CHAN_H__
#define nvkm_chan(p) container_of((p), struct nvkm_chan, object) /*FIXME: remove later */
#include <engine/fifo.h>

struct nvkm_chan_func {
	void *(*dtor)(struct nvkm_fifo_chan *);
	void (*init)(struct nvkm_fifo_chan *);
	void (*fini)(struct nvkm_fifo_chan *);
	int  (*engine_ctor)(struct nvkm_fifo_chan *, struct nvkm_engine *,
			    struct nvkm_object *);
	void (*engine_dtor)(struct nvkm_fifo_chan *, struct nvkm_engine *);
	int  (*engine_init)(struct nvkm_fifo_chan *, struct nvkm_engine *);
	int  (*engine_fini)(struct nvkm_fifo_chan *, struct nvkm_engine *,
			    bool suspend);
	int  (*object_ctor)(struct nvkm_fifo_chan *, struct nvkm_object *);
	void (*object_dtor)(struct nvkm_fifo_chan *, int);
	u32 (*submit_token)(struct nvkm_fifo_chan *);
};

int nvkm_fifo_chan_ctor(const struct nvkm_fifo_chan_func *, struct nvkm_fifo *,
			u32 size, u32 align, bool zero, u64 vm, u64 push,
			u32 engm, int bar, u32 base, u32 user,
			const struct nvkm_oclass *, struct nvkm_fifo_chan *);
void nvkm_chan_del(struct nvkm_chan **);

int nvkm_fifo_chan_child_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
#endif
