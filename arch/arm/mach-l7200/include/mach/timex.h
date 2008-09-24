/*
 * arch/arm/mach-l7200/include/mach/timex.h
 *
 * Copyright (C) 2000 Rob Scott (rscott@mtrob.fdns.net)
 *                    Steve Hill (sjhill@cotw.com)
 *
 * 04-21-2000  RS Created file
 * 05-03-2000 SJH Tick rate was wrong
 *
 */

/*
 * On the ARM720T, clock ticks are set to 128 Hz.
 *
 * NOTE: The actual RTC value is set in 'time.h' which
 *       must be changed when choosing a different tick
 *       rate. The value of HZ in 'param.h' must also
 *       be changed to match below.
 */
#define CLOCK_TICK_RATE		128
