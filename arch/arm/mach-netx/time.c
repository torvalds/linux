/*
 * arch/arm/mach-netx/time.c
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clocksource.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/time.h>
#include <asm/arch/netx-regs.h>

/*
 * IRQ handler for the timer
 */
static irqreturn_t
netx_timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);

	timer_tick();

	write_sequnlock(&xtime_lock);

	/* acknowledge interrupt */
	writel(COUNTER_BIT(0), NETX_GPIO_IRQ);

	return IRQ_HANDLED;
}

static struct irqaction netx_timer_irq = {
	.name           = "NetX Timer Tick",
	.flags          = IRQF_DISABLED | IRQF_TIMER,
	.handler        = netx_timer_interrupt,
};

cycle_t netx_get_cycles(void)
{
	return readl(NETX_GPIO_COUNTER_CURRENT(1));
}

static struct clocksource clocksource_netx = {
	.name 		= "netx_timer",
	.rating		= 200,
	.read		= netx_get_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift 		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Set up timer interrupt
 */
static void __init netx_timer_init(void)
{
	/* disable timer initially */
	writel(0, NETX_GPIO_COUNTER_CTRL(0));

	/* Reset the timer value to zero */
	writel(0, NETX_GPIO_COUNTER_CURRENT(0));

	writel(LATCH, NETX_GPIO_COUNTER_MAX(0));

	/* acknowledge interrupt */
	writel(COUNTER_BIT(0), NETX_GPIO_IRQ);

	/* Enable the interrupt in the specific timer register and start timer */
	writel(COUNTER_BIT(0), NETX_GPIO_IRQ_ENABLE);
	writel(NETX_GPIO_COUNTER_CTRL_IRQ_EN | NETX_GPIO_COUNTER_CTRL_RUN,
		NETX_GPIO_COUNTER_CTRL(0));

	setup_irq(NETX_IRQ_TIMER0, &netx_timer_irq);

	/* Setup timer one for clocksource */
        writel(0, NETX_GPIO_COUNTER_CTRL(1));
        writel(0, NETX_GPIO_COUNTER_CURRENT(1));
        writel(0xFFFFFFFF, NETX_GPIO_COUNTER_MAX(1));

        writel(NETX_GPIO_COUNTER_CTRL_RUN,
                NETX_GPIO_COUNTER_CTRL(1));

	clocksource_netx.mult =
		clocksource_hz2mult(CLOCK_TICK_RATE, clocksource_netx.shift);
	clocksource_register(&clocksource_netx);
}

struct sys_timer netx_timer = {
	.init		= netx_timer_init,
};
