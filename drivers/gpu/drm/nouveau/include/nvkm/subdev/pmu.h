#ifndef __NOUVEAU_PMU_H__
#define __NOUVEAU_PMU_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_pmu {
	struct nouveau_subdev base;

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

	int  (*message)(struct nouveau_pmu *, u32[2], u32, u32, u32, u32);
	void (*pgob)(struct nouveau_pmu *, bool);
};

static inline struct nouveau_pmu *
nouveau_pmu(void *obj)
{
	return (void *)nouveau_subdev(obj, NVDEV_SUBDEV_PMU);
}

extern struct nouveau_oclass *nva3_pmu_oclass;
extern struct nouveau_oclass *nvc0_pmu_oclass;
extern struct nouveau_oclass *nvd0_pmu_oclass;
extern struct nouveau_oclass *gk104_pmu_oclass;
extern struct nouveau_oclass *nv108_pmu_oclass;
extern struct nouveau_oclass *gk20a_pmu_oclass;

/* interface to MEMX process running on PMU */
struct nouveau_memx;
int  nouveau_memx_init(struct nouveau_pmu *, struct nouveau_memx **);
int  nouveau_memx_fini(struct nouveau_memx **, bool exec);
void nouveau_memx_wr32(struct nouveau_memx *, u32 addr, u32 data);
void nouveau_memx_wait(struct nouveau_memx *,
		       u32 addr, u32 mask, u32 data, u32 nsec);
void nouveau_memx_nsec(struct nouveau_memx *, u32 nsec);
void nouveau_memx_wait_vblank(struct nouveau_memx *);
void nouveau_memx_train(struct nouveau_memx *);
int  nouveau_memx_train_result(struct nouveau_pmu *, u32 *, int);
void nouveau_memx_block(struct nouveau_memx *);
void nouveau_memx_unblock(struct nouveau_memx *);

#endif
