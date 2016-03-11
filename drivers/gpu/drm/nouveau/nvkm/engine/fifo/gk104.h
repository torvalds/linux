#ifndef __GK104_FIFO_H__
#define __GK104_FIFO_H__
#define gk104_fifo(p) container_of((p), struct gk104_fifo, base)
#include "priv.h"

#include <subdev/mmu.h>

struct gk104_fifo_chan;
struct gk104_fifo {
	struct nvkm_fifo base;

	struct {
		struct work_struct work;
		u32 engm;
		u32 runm;
	} recover;

	int pbdma_nr;

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
		struct list_head chan;
		u32 engm;
	} runlist[16];
	int runlist_nr;

	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma bar;
	} user;
};

int gk104_fifo_new_(const struct nvkm_fifo_func *, struct nvkm_device *,
		    int index, int nr, struct nvkm_fifo **);
void *gk104_fifo_dtor(struct nvkm_fifo *);
int gk104_fifo_oneinit(struct nvkm_fifo *);
void gk104_fifo_init(struct nvkm_fifo *);
void gk104_fifo_fini(struct nvkm_fifo *);
void gk104_fifo_intr(struct nvkm_fifo *);
void gk104_fifo_uevent_init(struct nvkm_fifo *);
void gk104_fifo_uevent_fini(struct nvkm_fifo *);
void gk104_fifo_runlist_insert(struct gk104_fifo *, struct gk104_fifo_chan *);
void gk104_fifo_runlist_remove(struct gk104_fifo *, struct gk104_fifo_chan *);
void gk104_fifo_runlist_commit(struct gk104_fifo *, int runl);

static inline u64
gk104_fifo_engine_subdev(int engine)
{
	switch (engine) {
	case 0: return (1ULL << NVKM_ENGINE_GR) |
		       (1ULL << NVKM_ENGINE_SW) |
		       (1ULL << NVKM_ENGINE_CE2);
	case 1: return (1ULL << NVKM_ENGINE_MSPDEC);
	case 2: return (1ULL << NVKM_ENGINE_MSPPP);
	case 3: return (1ULL << NVKM_ENGINE_MSVLD);
	case 4: return (1ULL << NVKM_ENGINE_CE0);
	case 5: return (1ULL << NVKM_ENGINE_CE1);
	case 6: return (1ULL << NVKM_ENGINE_MSENC);
	default:
		WARN_ON(1);
		return 0;
	}
}
#endif
