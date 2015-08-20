#ifndef __GK104_FIFO_H__
#define __GK104_FIFO_H__
#define gk104_fifo(p) container_of((p), struct gk104_fifo, base)
#include "priv.h"

#include <subdev/mmu.h>

struct gk104_fifo_engn {
	struct nvkm_memory *runlist[2];
	int cur_runlist;
	wait_queue_head_t wait;
	struct list_head chan;
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

static inline u64
gk104_fifo_engine_subdev(int engine)
{
	switch (engine) {
	case 0: return (1ULL << NVDEV_ENGINE_GR) |
		       (1ULL << NVDEV_ENGINE_SW) |
		       (1ULL << NVDEV_ENGINE_CE2);
	case 1: return (1ULL << NVDEV_ENGINE_MSPDEC);
	case 2: return (1ULL << NVDEV_ENGINE_MSPPP);
	case 3: return (1ULL << NVDEV_ENGINE_MSVLD);
	case 4: return (1ULL << NVDEV_ENGINE_CE0);
	case 5: return (1ULL << NVDEV_ENGINE_CE1);
	case 6: return (1ULL << NVDEV_ENGINE_MSENC);
	default:
		WARN_ON(1);
		return 0;
	}
}

static inline int
gk104_fifo_subdev_engine(int subdev)
{
	switch (subdev) {
	case NVDEV_ENGINE_GR:
	case NVDEV_ENGINE_SW:
	case NVDEV_ENGINE_CE2   : return 0;
	case NVDEV_ENGINE_MSPDEC: return 1;
	case NVDEV_ENGINE_MSPPP : return 2;
	case NVDEV_ENGINE_MSVLD : return 3;
	case NVDEV_ENGINE_CE0   : return 4;
	case NVDEV_ENGINE_CE1   : return 5;
	case NVDEV_ENGINE_MSENC : return 6;
	default:
		WARN_ON(1);
		return 0;
	}
}
#endif
