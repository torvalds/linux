/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_TIMER_H__
#define __NVKM_TIMER_H__
#include <core/subdev.h>

struct nvkm_alarm {
	struct list_head head;
	struct list_head exec;
	u64 timestamp;
	void (*func)(struct nvkm_alarm *);
};

static inline void
nvkm_alarm_init(struct nvkm_alarm *alarm, void (*func)(struct nvkm_alarm *))
{
	INIT_LIST_HEAD(&alarm->head);
	alarm->func = func;
}

struct nvkm_timer {
	const struct nvkm_timer_func *func;
	struct nvkm_subdev subdev;

	struct list_head alarms;
	spinlock_t lock;
};

u64 nvkm_timer_read(struct nvkm_timer *);
void nvkm_timer_alarm(struct nvkm_timer *, u32 nsec, struct nvkm_alarm *);

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
	u64 _nsecs = (n), _time0 = nvkm_timer_read(_tmr);                      \
	s64 _taken = 0;                                                        \
	bool _warn = true;                                                     \
                                                                               \
	do {                                                                   \
		cond                                                           \
	} while (_taken = nvkm_timer_read(_tmr) - _time0, _taken < _nsecs);    \
                                                                               \
	if (_taken >= _nsecs) {                                                \
		if (_warn)                                                     \
			dev_WARN(_device->dev, "timeout\n");                   \
		_taken = -ETIMEDOUT;                                           \
	}                                                                      \
	_taken;                                                                \
})
#define nvkm_usec(d,u,cond...) nvkm_nsec((d), (u) * 1000, ##cond)
#define nvkm_msec(d,m,cond...) nvkm_usec((d), (m) * 1000, ##cond)

#define nvkm_wait_nsec(d,n,addr,mask,data)                                     \
	nvkm_nsec(d, n,                                                        \
		if ((nvkm_rd32(d, (addr)) & (mask)) == (data))                 \
			break;                                                 \
		)
#define nvkm_wait_usec(d,u,addr,mask,data)                                     \
	nvkm_wait_nsec((d), (u) * 1000, (addr), (mask), (data))
#define nvkm_wait_msec(d,m,addr,mask,data)                                     \
	nvkm_wait_usec((d), (m) * 1000, (addr), (mask), (data))

int nv04_timer_new(struct nvkm_device *, int, struct nvkm_timer **);
int nv40_timer_new(struct nvkm_device *, int, struct nvkm_timer **);
int nv41_timer_new(struct nvkm_device *, int, struct nvkm_timer **);
int gk20a_timer_new(struct nvkm_device *, int, struct nvkm_timer **);
#endif
