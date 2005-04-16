/*
 * include/asm-v850/v850e_timer_d.c -- `Timer D' component often used
 *	with V850E CPUs
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

#include <linux/kernel.h>

#include <asm/v850e_utils.h>
#include <asm/v850e_timer_d.h>

/* Start interval timer TIMER (0-3).  The timer will issue the
   corresponding INTCMD interrupt RATE times per second.
   This function does not enable the interrupt.  */
void v850e_timer_d_configure (unsigned timer, unsigned rate)
{
	unsigned divlog2, count;

	/* Calculate params for timer.  */
	if (! calc_counter_params (
		    V850E_TIMER_D_BASE_FREQ, rate,
		    V850E_TIMER_D_TMCD_CS_MIN, V850E_TIMER_D_TMCD_CS_MAX, 16,
		    &divlog2, &count))
		printk (KERN_WARNING
			"Cannot find interval timer %d setting suitable"
			" for rate of %dHz.\n"
			"Using rate of %dHz instead.\n",
			timer, rate,
			(V850E_TIMER_D_BASE_FREQ >> divlog2) >> 16);

	/* Do the actual hardware timer initialization:  */

	/* Enable timer.  */
	V850E_TIMER_D_TMCD(timer) = V850E_TIMER_D_TMCD_CAE;
	/* Set clock divider.  */
	V850E_TIMER_D_TMCD(timer)
		= V850E_TIMER_D_TMCD_CAE
		| V850E_TIMER_D_TMCD_CS(divlog2);
	/* Set timer compare register.  */
	V850E_TIMER_D_CMD(timer) = count;
	/* Start counting.  */
	V850E_TIMER_D_TMCD(timer)
		= V850E_TIMER_D_TMCD_CAE
		| V850E_TIMER_D_TMCD_CS(divlog2)
		| V850E_TIMER_D_TMCD_CE;
}
