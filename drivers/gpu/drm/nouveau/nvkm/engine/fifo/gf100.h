/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GF100_FIFO_H__
#define __GF100_FIFO_H__
#define gf100_fifo(p) container_of((p), struct gf100_fifo, base)
#include "priv.h"

#include <subdev/mmu.h>

struct gf100_fifo_chan;
struct gf100_fifo {
	struct nvkm_fifo base;

	struct list_head chan;

	struct {
		struct work_struct work;
		u64 mask;
	} recover;

	int pbdma_nr;

	struct {
		struct nvkm_memory *mem[2];
		int active;
		wait_queue_head_t wait;
	} runlist;

	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma *bar;
	} user;
};

void gf100_fifo_intr_engine(struct gf100_fifo *);
void gf100_fifo_runlist_insert(struct gf100_fifo *, struct gf100_fifo_chan *);
void gf100_fifo_runlist_remove(struct gf100_fifo *, struct gf100_fifo_chan *);
void gf100_fifo_runlist_commit(struct gf100_fifo *);
#endif
