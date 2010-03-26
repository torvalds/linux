/*
 *  linux/arch/arm/include/asm/perf_event.h
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ARM_PERF_EVENT_H__
#define __ARM_PERF_EVENT_H__

/*
 * NOP: on *most* (read: all supported) ARM platforms, the performance
 * counter interrupts are regular interrupts and not an NMI. This
 * means that when we receive the interrupt we can call
 * perf_event_do_pending() that handles all of the work with
 * interrupts enabled.
 */
static inline void
set_perf_event_pending(void)
{
}

/* ARM performance counters start from 1 (in the cp15 accesses) so use the
 * same indexes here for consistency. */
#define PERF_EVENT_INDEX_OFFSET 1

#endif /* __ARM_PERF_EVENT_H__ */
