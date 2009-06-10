/* MN10300 Architecture time management specifications
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#include <asm/hardirq.h>
#include <unit/timex.h>

#define TICK_SIZE (tick_nsec / 1000)

#define CLOCK_TICK_RATE 1193180 /* Underlying HZ - this should probably be set
				 * to something appropriate, but what? */

extern cycles_t cacheflush_time;

#ifdef __KERNEL__

static inline cycles_t get_cycles(void)
{
	return read_timestamp_counter();
}

#endif /* __KERNEL__ */

#endif /* _ASM_TIMEX_H */
