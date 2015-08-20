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

void nvkm_timer_alarm(void *, u32 nsec, struct nvkm_alarm *);
void nvkm_timer_alarm_cancel(void *, struct nvkm_alarm *);

/* Delay based on GPU time (ie. PTIMER).
 *
 * Will return -ETIMEDOUT unless the loop was terminated with 'break',
 * where it will return the number of nanoseconds taken instead.
 *
 * NVKM_DELAY can be passed for 'cond' to disable the timeout warning,
 * which is useful for unconditional delay loops.
 */
#define NVKM_DELAY _warn = false;
#define nvkm_nsec(d,n,cond...) ({                                              \
	struct nvkm_device *_device = (d);                                     \
	struct nvkm_timer *_tmr = _device->timer;                              \
	u64 _nsecs = (n), _time0 = _tmr->read(_tmr);                           \
	s64 _taken = 0;                                                        \
	bool _warn = true;                                                    \
                                                                               \
	do {                                                                   \
		cond                                                           \
	} while (_taken = _tmr->read(_tmr) - _time0, _taken < _nsecs);         \
                                                                               \
	if (_taken >= _nsecs) {                                                \
		if (_warn) {                                                   \
			dev_warn(_device->dev, "timeout at %s:%d/%s()!\n",     \
				 __FILE__, __LINE__, __func__);                \
		}                                                              \
		_taken = -ETIMEDOUT;                                           \
	}                                                                      \
	_taken;                                                                \
})
#define nvkm_usec(d,u,cond...) nvkm_nsec((d), (u) * 1000, ##cond)
#define nvkm_msec(d,m,cond...) nvkm_usec((d), (m) * 1000, ##cond)

struct nvkm_timer {
	struct nvkm_subdev subdev;
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
	nvkm_subdev_destroy(&(p)->subdev)
#define nvkm_timer_init(p)                                                  \
	nvkm_subdev_init(&(p)->subdev)
#define nvkm_timer_fini(p,s)                                                \
	nvkm_subdev_fini(&(p)->subdev, (s))

int nvkm_timer_create_(struct nvkm_object *, struct nvkm_engine *,
			  struct nvkm_oclass *, int size, void **);

extern struct nvkm_oclass nv04_timer_oclass;
extern struct nvkm_oclass gk20a_timer_oclass;
#endif
