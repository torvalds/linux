#ifndef __NVKM_TIMER_NV04_H__
#define __NVKM_TIMER_NV04_H__
#include "priv.h"

#define NV04_PTIMER_INTR_0      0x009100
#define NV04_PTIMER_INTR_EN_0   0x009140
#define NV04_PTIMER_NUMERATOR   0x009200
#define NV04_PTIMER_DENOMINATOR 0x009210
#define NV04_PTIMER_TIME_0      0x009400
#define NV04_PTIMER_TIME_1      0x009410
#define NV04_PTIMER_ALARM_0     0x009420

struct nv04_timer_priv {
	struct nvkm_timer base;
	struct list_head alarms;
	spinlock_t lock;
	u64 suspend_time;
};

int  nv04_timer_ctor(struct nvkm_object *, struct nvkm_object *,
		     struct nvkm_oclass *, void *, u32,
		     struct nvkm_object **);
void nv04_timer_dtor(struct nvkm_object *);
int  nv04_timer_fini(struct nvkm_object *, bool);
#endif
