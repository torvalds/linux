#ifndef _PERF_PERF_H
#define _PERF_PERF_H

/*
 * prctl(PR_TASK_PERF_COUNTERS_DISABLE) will (cheaply) disable all
 * counters in the current task.
 */
#define PR_TASK_PERF_COUNTERS_DISABLE   31
#define PR_TASK_PERF_COUNTERS_ENABLE    32

#define rdclock()					\
({							\
	struct timespec ts;				\
							\
	clock_gettime(CLOCK_MONOTONIC, &ts);		\
	ts.tv_sec * 1000000000ULL + ts.tv_nsec;		\
})

/*
 * Pick up some kernel type conventions:
 */
#define __user
#define asmlinkage

#ifdef __x86_64__
#define __NR_perf_counter_open 298
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __i386__
#define __NR_perf_counter_open 336
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __powerpc__
#define __NR_perf_counter_open 319
#define rmb()		asm volatile ("sync" ::: "memory")
#define cpu_relax()	asm volatile ("" ::: "memory");
#endif

#define unlikely(x)	__builtin_expect(!!(x), 0)
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

static inline int
sys_perf_counter_open(struct perf_counter_hw_event *hw_event_uptr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	return syscall(__NR_perf_counter_open, hw_event_uptr, pid, cpu,
		       group_fd, flags);
}

#define MAX_COUNTERS			64
#define MAX_NR_CPUS			256

#define EID(type, id) (((__u64)(type) << PERF_COUNTER_TYPE_SHIFT) | (id))

#endif
