/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIFO_CHAN_H__
#define __NVKM_FIFO_CHAN_H__
#define nvkm_fifo_chan(p) container_of((p), struct nvkm_fifo_chan, object)
#include "priv.h"

struct nvkm_fifo_chan_func {
	void *(*dtor)(struct nvkm_fifo_chan *);
	void (*init)(struct nvkm_fifo_chan *);
	void (*fini)(struct nvkm_fifo_chan *);
	int (*ntfy)(struct nvkm_fifo_chan *, u32 type, struct nvkm_event **);
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
			u64 engines, int bar, u32 base, u32 user,
			const struct nvkm_oclass *, struct nvkm_fifo_chan *);

struct nvkm_fifo_chan_oclass {
	int (*ctor)(struct nvkm_fifo *, const struct nvkm_oclass *,
		    void *data, u32 size, struct nvkm_object **);
	struct nvkm_sclass base;
};

int gf100_fifo_chan_ntfy(struct nvkm_fifo_chan *, u32, struct nvkm_event **);
#endif
