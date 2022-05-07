/* SPDX-License-Identifier: MIT */
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

struct nvkm_timer_wait {
	struct nvkm_timer *tmr;
	u64 limit;
	u64 time0;
	u64 time1;
	int reads;
};

void nvkm_timer_wait_init(struct nvkm_device *, u64 nsec,
			  struct nvkm_timer_wait *);
s64 nvkm_timer_wait_test(struct nvkm_timer_wait *);

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
	struct nvkm_timer_wait _wait;                                          \
	bool _warn = true;                                                     \
	s64 _taken = 0;                                                        \
                                                                               \
	nvkm_timer_wait_init((d), (n), &_wait);                                \
	do {                                                                   \
		cond                                                           \
	} while ((_taken = nvkm_timer_wait_test(&_wait)) >= 0);                \
                                                                               \
	if (_warn && _taken < 0)                                               \
		dev_WARN(_wait.tmr->subdev.device->dev, "timeout\n");          \
	_taken;                                                                \
})
#define nvkm_usec(d, u, cond...) nvkm_nsec((d), (u) * 1000ULL, ##cond)
#define nvkm_msec(d, m, cond...) nvkm_usec((d), (m) * 1000ULL, ##cond)

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
