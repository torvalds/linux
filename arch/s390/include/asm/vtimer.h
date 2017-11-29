/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2003, 2012
 *  Virtual CPU timer
 *
 *  Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 */

#ifndef _ASM_S390_TIMER_H
#define _ASM_S390_TIMER_H

#define VTIMER_MAX_SLICE (0x7fffffffffffffffULL)

struct vtimer_list {
	struct list_head entry;
	u64 expires;
	u64 interval;
	void (*function)(unsigned long);
	unsigned long data;
};

extern void init_virt_timer(struct vtimer_list *timer);
extern void add_virt_timer(struct vtimer_list *timer);
extern void add_virt_timer_periodic(struct vtimer_list *timer);
extern int mod_virt_timer(struct vtimer_list *timer, u64 expires);
extern int mod_virt_timer_periodic(struct vtimer_list *timer, u64 expires);
extern int del_virt_timer(struct vtimer_list *timer);

extern void init_cpu_vtimer(void);
extern void vtime_init(void);

#endif /* _ASM_S390_TIMER_H */
