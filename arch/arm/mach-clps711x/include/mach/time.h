/*
 *  arch/arm/mach-clps711x/include/mach/time.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <asm/leds.h>
#include <mach/hardware.h>

extern void clps711x_setup_timer(void);

/*
 * IRQ handler for the timer
 */
static irqreturn_t
p720t_timer_interrupt(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();
	do_leds();
	xtime_update(1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	do_profile(regs);
	return IRQ_HANDLED;
}

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init time_init(void)
{
	clps711x_setup_timer();
	timer_irq.handler = p720t_timer_interrupt;
	setup_irq(IRQ_TC2OI, &timer_irq);
}
