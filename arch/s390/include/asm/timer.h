/*
 *  include/asm-s390/timer.h
 *
 *  (C) Copyright IBM Corp. 2003,2006
 *  Virtual CPU timer
 *
 *  Author: Jan Glauber (jang@de.ibm.com)
 */

#ifndef _ASM_S390_TIMER_H
#define _ASM_S390_TIMER_H

#ifdef __KERNEL__

#include <linux/timer.h>

#define VTIMER_MAX_SLICE (0x7ffffffffffff000LL)

struct vtimer_list {
	struct list_head entry;

	int cpu;
	__u64 expires;
	__u64 interval;

	void (*function)(unsigned long);
	unsigned long data;
};

/* the vtimer value will wrap after ca. 71 years */
struct vtimer_queue {
	struct list_head list;
	spinlock_t lock;
	__u64 timer;		/* last programmed timer */
	__u64 elapsed;		/* elapsed time of timer expire values */
	__u64 idle_enter;	/* cpu timer on idle enter */
	__u64 idle_exit;	/* cpu timer on idle exit */
};

extern void init_virt_timer(struct vtimer_list *timer);
extern void add_virt_timer(void *new);
extern void add_virt_timer_periodic(void *new);
extern int mod_virt_timer(struct vtimer_list *timer, __u64 expires);
extern int mod_virt_timer_periodic(struct vtimer_list *timer, __u64 expires);
extern int del_virt_timer(struct vtimer_list *timer);

extern void init_cpu_vtimer(void);
extern void vtime_init(void);

extern void vtime_stop_cpu(void);
extern void vtime_start_leave(void);

#endif /* __KERNEL__ */

#endif /* _ASM_S390_TIMER_H */
