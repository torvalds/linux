/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CHAN_H__
#define __NVKM_CHAN_H__
#define nvkm_chan(p) container_of((p), struct nvkm_chan, object) /*FIXME: remove later */
#include <engine/fifo.h>
struct nvkm_engn;

extern const struct nvkm_event_func nvkm_chan_event;

struct nvkm_cctx {
	struct nvkm_vctx *vctx;
	refcount_t refs;
	refcount_t uses;

	struct list_head head;
};

struct nvkm_chan_func {
	const struct nvkm_chan_func_inst {
		u32 size;
		bool zero;
		bool vmm;
	} *inst;

	const struct nvkm_chan_func_userd {
		int bar;
		u32 base;
		u32 size;
		void (*clear)(struct nvkm_chan *);
	} *userd;

	const struct nvkm_chan_func_ramfc {
		const struct nvkm_ramfc_layout {
			unsigned bits:6;
			unsigned ctxs:5;
			unsigned ctxp:8;
			unsigned regs:5;
			unsigned regp;
		} *layout;
		int (*write)(struct nvkm_chan *, u64 offset, u64 length, u32 devm, bool priv);
		void (*clear)(struct nvkm_chan *);
		bool ctxdma;
		u32 devm;
		bool priv;
	} *ramfc;

	void (*bind)(struct nvkm_chan *);
	void (*unbind)(struct nvkm_chan *);
	void (*start)(struct nvkm_chan *);
	void (*stop)(struct nvkm_chan *);
	void (*preempt)(struct nvkm_chan *);
	u32 (*doorbell_handle)(struct nvkm_chan *);

	void *(*dtor)(struct nvkm_fifo_chan *);
	int  (*engine_ctor)(struct nvkm_fifo_chan *, struct nvkm_engine *,
			    struct nvkm_object *);
	void (*engine_dtor)(struct nvkm_fifo_chan *, struct nvkm_engine *);
	int  (*engine_init)(struct nvkm_fifo_chan *, struct nvkm_engine *);
	int  (*engine_fini)(struct nvkm_fifo_chan *, struct nvkm_engine *,
			    bool suspend);
	int  (*object_ctor)(struct nvkm_fifo_chan *, struct nvkm_object *);
	void (*object_dtor)(struct nvkm_fifo_chan *, int);
};

int nvkm_fifo_chan_ctor(const struct nvkm_fifo_chan_func *, struct nvkm_fifo *,
			u32 size, u32 align, bool zero, u64 vm, u64 push,
			u32 engm, int bar, u32 base, u32 user,
			const struct nvkm_oclass *, struct nvkm_fifo_chan *);
void nvkm_chan_del(struct nvkm_chan **);
void nvkm_chan_allow(struct nvkm_chan *);
void nvkm_chan_block(struct nvkm_chan *);
void nvkm_chan_error(struct nvkm_chan *, bool preempt);
void nvkm_chan_insert(struct nvkm_chan *);
void nvkm_chan_remove(struct nvkm_chan *, bool preempt);
void nvkm_chan_remove_locked(struct nvkm_chan *);
int nvkm_chan_preempt(struct nvkm_chan *, bool wait);
int nvkm_chan_preempt_locked(struct nvkm_chan *, bool wait);
int nvkm_chan_cctx_get(struct nvkm_chan *, struct nvkm_engn *, struct nvkm_cctx **,
		       struct nvkm_client * /*TODO: remove need for this */);
void nvkm_chan_cctx_put(struct nvkm_chan *, struct nvkm_cctx **);
struct nvkm_oproxy;
void nvkm_chan_cctx_bind(struct nvkm_chan *, struct nvkm_oproxy *, struct nvkm_cctx *);

#define CHAN_PRCLI(c,l,p,f,a...) CGRP_PRINT((c)->cgrp, l, p, "%04x:[%s]"f, (c)->id, (c)->name, ##a)
#define CHAN_PRINT(c,l,p,f,a...) CGRP_PRINT((c)->cgrp, l, p, "%04x:"f, (c)->id, ##a)
#define CHAN_ERROR(c,f,a...) CHAN_PRCLI((c), ERROR,    err, " "f"\n", ##a)
#define CHAN_TRACE(c,f,a...) CHAN_PRINT((c), TRACE,   info, " "f"\n", ##a)

int nvkm_fifo_chan_child_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
#endif
