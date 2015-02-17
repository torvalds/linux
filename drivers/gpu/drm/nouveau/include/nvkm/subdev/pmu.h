#ifndef __NVKM_PMU_H__
#define __NVKM_PMU_H__
#include <core/subdev.h>

struct nvkm_pmu {
	struct nvkm_subdev base;

	struct {
		u32 base;
		u32 size;
	} send;

	struct {
		u32 base;
		u32 size;

		struct work_struct work;
		wait_queue_head_t wait;
		u32 process;
		u32 message;
		u32 data[2];
	} recv;

	int  (*message)(struct nvkm_pmu *, u32[2], u32, u32, u32, u32);
	void (*pgob)(struct nvkm_pmu *, bool);
};

static inline struct nvkm_pmu *
nvkm_pmu(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_PMU);
}

extern struct nvkm_oclass *gt215_pmu_oclass;
extern struct nvkm_oclass *gf100_pmu_oclass;
extern struct nvkm_oclass *gf110_pmu_oclass;
extern struct nvkm_oclass *gk104_pmu_oclass;
extern struct nvkm_oclass *gk208_pmu_oclass;
extern struct nvkm_oclass *gk20a_pmu_oclass;

/* interface to MEMX process running on PMU */
struct nvkm_memx;
int  nvkm_memx_init(struct nvkm_pmu *, struct nvkm_memx **);
int  nvkm_memx_fini(struct nvkm_memx **, bool exec);
void nvkm_memx_wr32(struct nvkm_memx *, u32 addr, u32 data);
void nvkm_memx_wait(struct nvkm_memx *, u32 addr, u32 mask, u32 data, u32 nsec);
void nvkm_memx_nsec(struct nvkm_memx *, u32 nsec);
void nvkm_memx_wait_vblank(struct nvkm_memx *);
void nvkm_memx_train(struct nvkm_memx *);
int  nvkm_memx_train_result(struct nvkm_pmu *, u32 *, int);
void nvkm_memx_block(struct nvkm_memx *);
void nvkm_memx_unblock(struct nvkm_memx *);
#endif
