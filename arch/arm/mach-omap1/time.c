/*
 * linux/arch/arm/mach-omap1/time.c
 *
 * OMAP Timers
 *
 * Copyright (C) 2004 Nokia Corporation
 * Partial timer rewrite and additional dynamic tick timer support by
 * Tony Lindgen <tony@atomide.com> and
 * Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 * MPU timer code based on the older MPU timer code for OMAP
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/sched_clock.h>

#include <mach/hardware.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include "iomap.h"
#include "common.h"

#ifdef CONFIG_OMAP_MPU_TIMER

#define OMAP_MPU_TIMER_BASE		OMAP_MPU_TIMER1_BASE
#define OMAP_MPU_TIMER_OFFSET		0x100

typedef struct {
	u32 cntl;			/* CNTL_TIMER, R/W */
	u32 load_tim;			/* LOAD_TIM,   W */
	u32 read_tim;			/* READ_TIM,   R */
} omap_mpu_timer_regs_t;

#define omap_mpu_timer_base(n)							\
((omap_mpu_timer_regs_t __iomem *)OMAP1_IO_ADDRESS(OMAP_MPU_TIMER_BASE +	\
				 (n)*OMAP_MPU_TIMER_OFFSET))

static inline unsigned long notrace omap_mpu_timer_read(int nr)
{
	omap_mpu_timer_regs_t __iomem *timer = omap_mpu_timer_base(nr);
	return readl(&timer->read_tim);
}

static inline void omap_mpu_set_autoreset(int nr)
{
	omap_mpu_timer_regs_t __iomem *timer = omap_mpu_timer_base(nr);

	writel(readl(&timer->cntl) | MPU_TIMER_AR, &timer->cntl);
}

static inline void omap_mpu_remove_autoreset(int nr)
{
	omap_mpu_timer_regs_t __iomem *timer = omap_mpu_timer_base(nr);

	writel(readl(&timer->cntl) & ~MPU_TIMER_AR, &timer->cntl);
}

static inline void omap_mpu_timer_start(int nr, unsigned long load_val,
					int autoreset)
{
	omap_mpu_timer_regs_t __iomem *timer = omap_mpu_timer_base(nr);
	unsigned int timerflags = MPU_TIMER_CLOCK_ENABLE | MPU_TIMER_ST;

	if (autoreset)
		timerflags |= MPU_TIMER_AR;

	writel(MPU_TIMER_CLOCK_ENABLE, &timer->cntl);
	udelay(1);
	writel(load_val, &timer->load_tim);
        udelay(1);
	writel(timerflags, &timer->cntl);
}

static inline void omap_mpu_timer_stop(int nr)
{
	omap_mpu_timer_regs_t __iomem *timer = omap_mpu_timer_base(nr);

	writel(readl(&timer->cntl) & ~MPU_TIMER_ST, &timer->cntl);
}

/*
 * ---------------------------------------------------------------------------
 * MPU timer 1 ... count down to zero, interrupt, reload
 * ---------------------------------------------------------------------------
 */
static int omap_mpu_set_next_event(unsigned long cycles,
				   struct clock_event_device *evt)
{
	omap_mpu_timer_start(0, cycles, 0);
	return 0;
}

static void omap_mpu_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		omap_mpu_set_autoreset(0);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		omap_mpu_timer_stop(0);
		omap_mpu_remove_autoreset(0);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device clockevent_mpu_timer1 = {
	.name		= "mpu_timer1",
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event	= omap_mpu_set_next_event,
	.set_mode	= omap_mpu_set_mode,
};

static irqreturn_t omap_mpu_timer1_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_mpu_timer1;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction omap_mpu_timer1_irq = {
	.name		= "mpu_timer1",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= omap_mpu_timer1_interrupt,
};

static __init void omap_init_mpu_timer(unsigned long rate)
{
	setup_irq(INT_TIMER1, &omap_mpu_timer1_irq);
	omap_mpu_timer_start(0, (rate / HZ) - 1, 1);

	clockevent_mpu_timer1.cpumask = cpumask_of(0);
	clockevents_config_and_register(&clockevent_mpu_timer1, rate,
					1, -1);
}


/*
 * ---------------------------------------------------------------------------
 * MPU timer 2 ... free running 32-bit clock source and scheduler clock
 * ---------------------------------------------------------------------------
 */

static u32 notrace omap_mpu_read_sched_clock(void)
{
	return ~omap_mpu_timer_read(1);
}

static void __init omap_init_clocksource(unsigned long rate)
{
	omap_mpu_timer_regs_t __iomem *timer = omap_mpu_timer_base(1);
	static char err[] __initdata = KERN_ERR
			"%s: can't register clocksource!\n";

	omap_mpu_timer_start(1, ~0, 1);
	setup_sched_clock(omap_mpu_read_sched_clock, 32, rate);

	if (clocksource_mmio_init(&timer->read_tim, "mpu_timer2", rate,
			300, 32, clocksource_mmio_readl_down))
		printk(err, "mpu_timer2");
}

static void __init omap_mpu_timer_init(void)
{
	struct clk	*ck_ref = clk_get(NULL, "ck_ref");
	unsigned long	rate;

	BUG_ON(IS_ERR(ck_ref));

	rate = clk_get_rate(ck_ref);
	clk_put(ck_ref);

	/* PTV = 0 */
	rate /= 2;

	omap_init_mpu_timer(rate);
	omap_init_clocksource(rate);
}

#else
static inline void omap_mpu_timer_init(void)
{
	pr_err("Bogus timer, should not happen\n");
}
#endif	/* CONFIG_OMAP_MPU_TIMER */

/*
 * ---------------------------------------------------------------------------
 * Timer initialization
 * ---------------------------------------------------------------------------
 */
void __init omap1_timer_init(void)
{
	if (omap_32k_timer_init() != 0)
		omap_mpu_timer_init();
}
