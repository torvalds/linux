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

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/time.h>
#include <asm/arch/netx-regs.h>

/*
 * Returns number of us since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long netx_gettimeoffset(void)
{
	return readl(NETX_GPIO_COUNTER_CURRENT(0)) / 100;
}

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
}

struct sys_timer netx_timer = {
	.init           = netx_timer_init,
	.offset         = netx_gettimeoffset,
};
