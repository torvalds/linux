/*
 * arch/v850/kernel/highres_timer.c -- High resolution timing routines
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <asm/system.h>
#include <asm/v850e_timer_d.h>
#include <asm/highres_timer.h>

#define HIGHRES_TIMER_USEC_SHIFT   12

/* Pre-calculated constant used for converting ticks to real time
   units.  We initialize it to prevent it being put into BSS.  */
static u32 highres_timer_usec_prescale = 1;

void highres_timer_slow_tick_irq (void) __attribute__ ((noreturn));
void highres_timer_slow_tick_irq (void)
{
	/* This is an interrupt handler, so it must be very careful to
	   not to trash any registers.  At this point, the stack-pointer
	   (r3) has been saved in the chip ram location ENTRY_SP by the
	   interrupt vector, so we can use it as a scratch register; we
	   must also restore it before returning.  */
	asm ("ld.w	%0[r0], sp;"
	     "add	1, sp;"
	     "st.w	sp, %0[r0];"
	     "ld.w	%1[r0], sp;" /* restore pre-irq stack-pointer */
	     "reti"
	     ::
	      "i" (HIGHRES_TIMER_SLOW_TICKS_ADDR),
	      "i" (ENTRY_SP_ADDR)
	     : "memory");
}

void highres_timer_reset (void)
{
	V850E_TIMER_D_TMD (HIGHRES_TIMER_TIMER_D_UNIT) = 0;
	HIGHRES_TIMER_SLOW_TICKS = 0;
}

void highres_timer_start (void)
{
	u32 fast_tick_rate;

	/* Start hardware timer.  */
	v850e_timer_d_configure (HIGHRES_TIMER_TIMER_D_UNIT,
				 HIGHRES_TIMER_SLOW_TICK_RATE);

	fast_tick_rate =
		(V850E_TIMER_D_BASE_FREQ
		 >> V850E_TIMER_D_DIVLOG2 (HIGHRES_TIMER_TIMER_D_UNIT));

	/* The obvious way of calculating microseconds from fast ticks
	   is to do:

	     usec = fast_ticks * 10^6 / fast_tick_rate

	   However, divisions are much slower than multiplications, and
	   the above calculation can overflow, so we do this instead:

	     usec = fast_ticks * (10^6 * 2^12 / fast_tick_rate) / 2^12

           since we can pre-calculate (10^6 * (2^12 / fast_tick_rate))
	   and use a shift for dividing by 2^12, this avoids division,
	   and is almost as accurate (it differs by about 2 microseconds
	   at the extreme value of the fast-tick counter's ranger).  */
	highres_timer_usec_prescale = ((1000000 << HIGHRES_TIMER_USEC_SHIFT)
				       / fast_tick_rate);

	/* Enable the interrupt (which is hardwired to this use), and
	   give it the highest priority.  */
	V850E_INTC_IC (IRQ_INTCMD (HIGHRES_TIMER_TIMER_D_UNIT)) = 0;
}

void highres_timer_stop (void)
{
	/* Stop the timer.  */
	V850E_TIMER_D_TMCD (HIGHRES_TIMER_TIMER_D_UNIT) =
		V850E_TIMER_D_TMCD_CAE;
	/* Disable its interrupt, just in case.  */
	v850e_intc_disable_irq (IRQ_INTCMD (HIGHRES_TIMER_TIMER_D_UNIT));
}

inline void highres_timer_read_ticks (u32 *slow_ticks, u32 *fast_ticks)
{
	int flags;
	u32 fast_ticks_1, fast_ticks_2, _slow_ticks;

	local_irq_save (flags);
	fast_ticks_1 = V850E_TIMER_D_TMD (HIGHRES_TIMER_TIMER_D_UNIT);
	_slow_ticks = HIGHRES_TIMER_SLOW_TICKS;
	fast_ticks_2 = V850E_TIMER_D_TMD (HIGHRES_TIMER_TIMER_D_UNIT);
	local_irq_restore (flags);

	if (fast_ticks_2 < fast_ticks_1)
		_slow_ticks++;

	*slow_ticks = _slow_ticks;
	*fast_ticks = fast_ticks_2;
}

inline void highres_timer_ticks_to_timeval (u32 slow_ticks, u32 fast_ticks,
					    struct timeval *tv)
{
	unsigned long sec, sec_rem, usec;

	usec = ((fast_ticks * highres_timer_usec_prescale)
		>> HIGHRES_TIMER_USEC_SHIFT);

	sec = slow_ticks / HIGHRES_TIMER_SLOW_TICK_RATE;
	sec_rem = slow_ticks % HIGHRES_TIMER_SLOW_TICK_RATE;

	usec += sec_rem * (1000000 / HIGHRES_TIMER_SLOW_TICK_RATE);

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void highres_timer_read (struct timeval *tv)
{
	u32 fast_ticks, slow_ticks;
	highres_timer_read_ticks (&slow_ticks, &fast_ticks);
	highres_timer_ticks_to_timeval (slow_ticks, fast_ticks, tv);
}
