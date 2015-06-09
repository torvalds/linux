#ifndef __NVKM_TIMER_H__
#define __NVKM_TIMER_H__
#include <core/subdev.h>

struct nvkm_alarm {
	struct list_head head;
	u64 timestamp;
	void (*func)(struct nvkm_alarm *);
};

static inline void
nvkm_alarm_init(struct nvkm_alarm *alarm,
		   void (*func)(struct nvkm_alarm *))
{
	INIT_LIST_HEAD(&alarm->head);
	alarm->func = func;
}

bool nvkm_timer_wait_eq(void *, u64 nsec, u32 addr, u32 mask, u32 data);
bool nvkm_timer_wait_ne(void *, u64 nsec, u32 addr, u32 mask, u32 data);
bool nvkm_timer_wait_cb(void *, u64 nsec, bool (*func)(void *), void *data);
void nvkm_timer_alarm(void *, u32 nsec, struct nvkm_alarm *);
void nvkm_timer_alarm_cancel(void *, struct nvkm_alarm *);

#define NV_WAIT_DEFAULT 2000000000ULL
#define nv_wait(o,a,m,v)                                                       \
	nvkm_timer_wait_eq((o), NV_WAIT_DEFAULT, (a), (m), (v))
#define nv_wait_ne(o,a,m,v)                                                    \
	nvkm_timer_wait_ne((o), NV_WAIT_DEFAULT, (a), (m), (v))
#define nv_wait_cb(o,c,d)                                                      \
	nvkm_timer_wait_cb((o), NV_WAIT_DEFAULT, (c), (d))

struct nvkm_timer {
	struct nvkm_subdev base;
	u64  (*read)(struct nvkm_timer *);
	void (*alarm)(struct nvkm_timer *, u64 time, struct nvkm_alarm *);
	void (*alarm_cancel)(struct nvkm_timer *, struct nvkm_alarm *);
};

static inline struct nvkm_timer *
nvkm_timer(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_TIMER);
}

#define nvkm_timer_create(p,e,o,d)                                          \
	nvkm_subdev_create_((p), (e), (o), 0, "PTIMER", "timer",            \
			       sizeof(**d), (void **)d)
#define nvkm_timer_destroy(p)                                               \
	nvkm_subdev_destroy(&(p)->base)
#define nvkm_timer_init(p)                                                  \
	nvkm_subdev_init(&(p)->base)
#define nvkm_timer_fini(p,s)                                                \
	nvkm_subdev_fini(&(p)->base, (s))

int nvkm_timer_create_(struct nvkm_object *, struct nvkm_engine *,
			  struct nvkm_oclass *, int size, void **);

extern struct nvkm_oclass nv04_timer_oclass;
extern struct nvkm_oclass gk20a_timer_oclass;
#endif
