/*
 * arch/arm/mach-pnx4008/include/mach/timex.h
 *
 * PNX4008 timers header file
 *
 * Author: Dmitry Chigirev <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __PNX4008_TIMEX_H
#define __PNX4008_TIMEX_H

#include <linux/io.h>
#include <mach/hardware.h>

#define CLOCK_TICK_RATE		1000000

#define TICKS2USECS(x)	(x)

/* MilliSecond Timer - Chapter 21 Page 202 */

#define MSTIM_INT     IO_ADDRESS((PNX4008_MSTIMER_BASE + 0x0))
#define MSTIM_CTRL    IO_ADDRESS((PNX4008_MSTIMER_BASE + 0x4))
#define MSTIM_COUNTER IO_ADDRESS((PNX4008_MSTIMER_BASE + 0x8))
#define MSTIM_MCTRL   IO_ADDRESS((PNX4008_MSTIMER_BASE + 0x14))
#define MSTIM_MATCH0  IO_ADDRESS((PNX4008_MSTIMER_BASE + 0x18))
#define MSTIM_MATCH1  IO_ADDRESS((PNX4008_MSTIMER_BASE + 0x1c))

/* High Speed Timer - Chpater 22, Page 205 */

#define HSTIM_INT     IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x0))
#define HSTIM_CTRL    IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x4))
#define HSTIM_COUNTER IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x8))
#define HSTIM_PMATCH  IO_ADDRESS((PNX4008_HSTIMER_BASE + 0xC))
#define HSTIM_PCOUNT  IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x10))
#define HSTIM_MCTRL   IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x14))
#define HSTIM_MATCH0  IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x18))
#define HSTIM_MATCH1  IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x1c))
#define HSTIM_MATCH2  IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x20))
#define HSTIM_CCR     IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x28))
#define HSTIM_CR0     IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x2C))
#define HSTIM_CR1     IO_ADDRESS((PNX4008_HSTIMER_BASE + 0x30))

/* IMPORTANT: both timers are UPCOUNTING */

/* xSTIM_MCTRL bit definitions */
#define MR0_INT        1
#define RESET_COUNT0   (1<<1)
#define STOP_COUNT0    (1<<2)
#define MR1_INT        (1<<3)
#define RESET_COUNT1   (1<<4)
#define STOP_COUNT1    (1<<5)
#define MR2_INT        (1<<6)
#define RESET_COUNT2   (1<<7)
#define STOP_COUNT2    (1<<8)

/* xSTIM_CTRL bit definitions */
#define COUNT_ENAB     1
#define RESET_COUNT    (1<<1)
#define DEBUG_EN       (1<<2)

/* xSTIM_INT bit definitions */
#define MATCH0_INT     1
#define MATCH1_INT     (1<<1)
#define MATCH2_INT     (1<<2)
#define RTC_TICK0      (1<<4)
#define RTC_TICK1      (1<<5)

#endif
