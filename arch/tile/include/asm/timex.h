/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_TIMEX_H
#define _ASM_TILE_TIMEX_H

/*
 * This rate should be a multiple of the possible HZ values (100, 250, 1000)
 * and a fraction of the possible hardware timer frequencies.  Our timer
 * frequency is highly tunable but also quite precise, so for the primary use
 * of this value (setting ACT_HZ from HZ) we just pick a value that causes
 * ACT_HZ to be set to HZ.  We make the value somewhat large just to be
 * more robust in case someone tries out a new value of HZ.
 */
#define CLOCK_TICK_RATE	1000000

typedef unsigned long long cycles_t;

#if CHIP_HAS_SPLIT_CYCLE()
cycles_t get_cycles(void);
#else
static inline cycles_t get_cycles(void)
{
	return __insn_mfspr(SPR_CYCLE);
}
#endif

cycles_t get_clock_rate(void);

/* Called at cpu initialization to set some low-level constants. */
void setup_clock(void);

/* Called at cpu initialization to start the tile-timer clock device. */
void setup_tile_timer(void);

#endif /* _ASM_TILE_TIMEX_H */
