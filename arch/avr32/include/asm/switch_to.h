/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_SWITCH_TO_H
#define __ASM_AVR32_SWITCH_TO_H

/*
 * Help PathFinder and other Nexus-compliant debuggers keep track of
 * the current PID by emitting an Ownership Trace Message each time we
 * switch task.
 */
#ifdef CONFIG_OWNERSHIP_TRACE
#include <asm/ocd.h>
#define ocd_switch(prev, next)				\
	do {						\
		ocd_write(PID, prev->pid);		\
		ocd_write(PID, next->pid);		\
	} while(0)
#else
#define ocd_switch(prev, next)
#endif

/*
 * switch_to(prev, next, last) should switch from task `prev' to task
 * `next'. `prev' will never be the same as `next'.
 *
 * We just delegate everything to the __switch_to assembly function,
 * which is implemented in arch/avr32/kernel/switch_to.S
 *
 * mb() tells GCC not to cache `current' across this call.
 */
struct cpu_context;
struct task_struct;
extern struct task_struct *__switch_to(struct task_struct *,
				       struct cpu_context *,
				       struct cpu_context *);
#define switch_to(prev, next, last)					\
	do {								\
		ocd_switch(prev, next);					\
		last = __switch_to(prev, &prev->thread.cpu_context + 1,	\
				   &next->thread.cpu_context);		\
	} while (0)


#endif /* __ASM_AVR32_SWITCH_TO_H */
