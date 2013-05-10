/*
 *  linux/arch/arm/mach-clps711x/time.c
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/hardware/clps7111.h>

#include <asm/mach/time.h>


/*
 * gettimeoffset() returns time since last timer tick, in usecs.
 *
 * 'LATCH' is hwclock ticks (see CLOCK_TICK_RATE in timex.h) per jiffy.
 * 'tick' is usecs per jiffy.
 */
static unsigned long clps711x_gettimeoffset(void)
{
	unsigned long hwticks;
	hwticks = LATCH - (clps_readl(TC2D) & 0xffff);	/* since last underflow */
	return (hwticks * (tick_nsec / 1000)) / LATCH;
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t
p720t_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();
	return IRQ_HANDLED;
}

static struct irqaction clps711x_timer_irq = {
	.name		= "CLPS711x Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= p720t_timer_interrupt,
};

static void __init clps711x_timer_init(void)
{
	struct timespec tv;
	unsigned int syscon;

	syscon = clps_readl(SYSCON1);
	syscon |= SYSCON1_TC2S | SYSCON1_TC2M;
	clps_writel(syscon, SYSCON1);

	clps_writel(LATCH-1, TC2D); /* 512kHz / 100Hz - 1 */

	setup_irq(IRQ_TC2OI, &clps711x_timer_irq);

	tv.tv_nsec = 0;
	tv.tv_sec = clps_readl(RTCDR);
	do_settimeofday(&tv);
}

struct sys_timer clps711x_timer = {
	.init		= clps711x_timer_init,
	.offset		= clps711x_gettimeoffset,
};
