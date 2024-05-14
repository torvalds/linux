#ifndef __NVIF_TIMER_H__
#define __NVIF_TIMER_H__
#include <nvif/os.h>

struct nvif_timer_wait {
	struct nvif_device *device;
	u64 limit;
	u64 time0;
	u64 time1;
	int reads;
};

void nvif_timer_wait_init(struct nvif_device *, u64 nsec,
			  struct nvif_timer_wait *);
s64 nvif_timer_wait_test(struct nvif_timer_wait *);

/* Delay based on GPU time (ie. PTIMER).
 *
 * Will return -ETIMEDOUT unless the loop was terminated with 'break',
 * where it will return the number of nanoseconds taken instead.
 */
#define nvif_nsec(d,n,cond...) ({                                              \
	struct nvif_timer_wait _wait;                                          \
	s64 _taken = 0;                                                        \
                                                                               \
	nvif_timer_wait_init((d), (n), &_wait);                                \
	do {                                                                   \
		cond                                                           \
	} while ((_taken = nvif_timer_wait_test(&_wait)) >= 0);                \
                                                                               \
	_taken;                                                                \
})
#define nvif_usec(d,u,cond...) nvif_nsec((d), (u) * 1000, ##cond)
#define nvif_msec(d,m,cond...) nvif_usec((d), (m) * 1000, ##cond)
#endif
