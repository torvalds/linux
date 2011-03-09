/*
 * linux/arch/mips/include/asm/perf_event.h
 *
 * Copyright (C) 2010 MIPS Technologies, Inc.
 * Author: Deng-Cheng Zhu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MIPS_PERF_EVENT_H__
#define __MIPS_PERF_EVENT_H__

/*
 * MIPS performance counters do not raise NMI upon overflow, a regular
 * interrupt will be signaled. Hence we can do the pending perf event
 * work at the tail of the irq handler.
 */
static inline void
set_perf_event_pending(void)
{
}

#endif /* __MIPS_PERF_EVENT_H__ */
