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
		struct work_struct work;
		u32 engm;
		u32 runm;
	} recover;

	struct {
		struct nvkm_engine *engine;
		int runl;
		int pbid;
	} engine[16];
	int engine_nr;

	struct {
		struct nvkm_memory *mem[2];
		int next;
		wait_queue_head_t wait;
		struct list_head cgrp;
		struct list_head chan;
		u32 engm;
		u32 engm_sw;
	} runlist[16];
	int runlist_nr;

	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma *bar;
	} user;
};

struct gk104_fifo_engine_status {
	bool busy;
	bool faulted;
	bool chsw;
	bool save;
	bool load;
	struct {
		bool tsg;
		u32 id;
	} prev, next, *chan;
};

int gk104_fifo_new_(const struct gk104_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type,
		    int index, int nr, struct nvkm_fifo **);
void gk104_fifo_runlist_insert(struct gk104_fifo *, struct gk104_fifo_chan *);
void gk104_fifo_runlist_remove(struct gk104_fifo *, struct gk104_fifo_chan *);
void gk104_fifo_runlist_update(struct gk104_fifo *, int runl);
void gk104_fifo_engine_status(struct gk104_fifo *fifo, int engn,
			      struct gk104_fifo_engine_status *status);
void gk104_fifo_intr_runlist(struct gk104_fifo *fifo);
void *gk104_fifo_dtor(struct nvkm_fifo *base);
int gk104_fifo_oneinit(struct nvkm_fifo *);
void gk104_fifo_init(struct nvkm_fifo *base);
void gk104_fifo_fini(struct nvkm_fifo *base);

extern const struct nvkm_enum gk104_fifo_fault_access[];
extern const struct nvkm_enum gk104_fifo_fault_engine[];
extern const struct nvkm_enum gk104_fifo_fault_reason[];
extern const struct nvkm_enum gk104_fifo_fault_hubclient[];
extern const struct nvkm_enum gk104_fifo_fault_gpcclient[];
extern const struct gk104_fifo_runlist_func gk104_fifo_runlist;
void gk104_fifo_runlist_chan(struct gk104_fifo_chan *,
			     struct nvkm_memory *, u32);
void gk104_fifo_runlist_commit(struct gk104_fifo *, int runl,
			       struct nvkm_memory *, int);

extern const struct gk104_fifo_runlist_func gk110_fifo_runlist;
void gk110_fifo_runlist_cgrp(struct nvkm_fifo_cgrp *,
			     struct nvkm_memory *, u32);

void gk208_fifo_pbdma_init_timeout(struct gk104_fifo *);

extern const struct nvkm_enum gm107_fifo_fault_engine[];
extern const struct gk104_fifo_runlist_func gm107_fifo_runlist;

extern const struct nvkm_enum gp100_fifo_fault_engine[];

extern const struct nvkm_enum gv100_fifo_fault_access[];
extern const struct nvkm_enum gv100_fifo_fault_reason[];
extern const struct nvkm_enum gv100_fifo_fault_hubclient[];
extern const struct nvkm_enum gv100_fifo_fault_gpcclient[];
void gv100_fifo_runlist_cgrp(struct nvkm_fifo_cgrp *,
			     struct nvkm_memory *, u32);
void gv100_fifo_runlist_chan(struct gk104_fifo_chan *,
			     struct nvkm_memory *, u32);
#endif
