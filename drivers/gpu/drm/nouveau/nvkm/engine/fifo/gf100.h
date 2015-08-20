#ifndef __GF100_FIFO_H__
#define __GF100_FIFO_H__
#include "priv.h"

struct gf100_fifo {
	struct nvkm_fifo base;

	struct work_struct fault;
	u64 mask;

	struct {
		struct nvkm_memory *mem[2];
		int active;
		wait_queue_head_t wait;
	} runlist;

	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma bar;
	} user;
	int spoon_nr;
};

void gf100_fifo_intr_engine(struct gf100_fifo *);
void gf100_fifo_runlist_update(struct gf100_fifo *);
#endif
