/*
 * arch/arm/plat-sunxi/clocksrc.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <mach/clock.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <plat/system.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include "clocksrc.h"

#undef CLKSRC_DBG
#undef CLKSRC_ERR
#if (0)
    #define CLKSRC_DBG(format,args...)  printk("[CLKSRC] "format,##args)
    #define CLKSRC_ERR(format,args...)  printk("[CLKSRC] "format,##args)
#else
    #define CLKSRC_DBG(...)
    #define CLKSRC_ERR(...)
#endif

static DEFINE_SPINLOCK(clksrc_lock);
static spinlock_t tmr_spin_lock[2];
static const int tmr_div[2] = { SYS_TIMER_SCAL, 1 };

static irqreturn_t aw_clkevt_irq(int irq, void *handle);
static void aw_set_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev);
static int aw_set_next_clkevt(unsigned long delta, struct clock_event_device *dev);

static struct clocksource aw_clocksrc =
{
    .name = "aw_64bits_counter",
    .list = {NULL, NULL},
    .rating = 300,                  /* perfect clock source             */
    .read = aw_clksrc_read,         /* read clock counter               */
    .enable = 0,                    /* not define                       */
    .disable = 0,                   /* not define                       */
    .mask = CLOCKSOURCE_MASK(64),   /* 64bits mask                      */
    .mult = 0,                      /* it will be calculated by shift   */
    .shift = 10,                    /* 32bit shift for                  */
    .max_idle_ns = 1000000000000ULL,
    .flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct clock_event_device timer0_clockevent = {
	.name = "timer0",
	.shift = 32,
	.rating = 100,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = aw_set_clkevt_mode,
	.set_next_event = aw_set_next_clkevt,
	.irq = SW_INT_IRQNO_TIMER0,
};

static struct irqaction sw_timer_irq = {
	.name = "timer0",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = aw_clkevt_irq,
	.dev_id = &timer0_clockevent,
	.irq = SW_INT_IRQNO_TIMER0,
};

#ifdef CONFIG_HIGH_RES_TIMERS
static struct clock_event_device aw_clock_event =
{
    .name = "aw_clock_event",
    .features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
    .max_delta_ns = 100000000000ULL,
    .min_delta_ns = (1000000000 + AW_HPET_CLOCK_EVENT_HZ - 1) / AW_HPET_CLOCK_EVENT_HZ,
    .mult = 100,                    /* will be calculate when init      */
    .shift = 32,
    .rating = 300,                  /* clock event is perfect           */
    .irq = SW_INT_IRQNO_TIMER1,
    .cpumask = 0,                   /* will be set when init            */
    .set_next_event = aw_set_next_clkevt,
    .set_mode = aw_set_clkevt_mode, /* set clock event mode             */
    .event_handler = 0,             /* be alloced by system framework   */
};

static struct irqaction aw_clkevt_irqact =
{
    .handler = aw_clkevt_irq,
    .flags = IRQF_TIMER | IRQF_DISABLED,
    .name = "aw_clock_event",
    .dev_id = &aw_clock_event,
    .irq = SW_INT_IRQNO_TIMER1,
};
#endif


/*
*********************************************************************************************************
*                           aw_clksrc_read
*
*Description: read cycle count of the clock source;
*
*Arguments  : cs    clock source handle.
*
*Return     : cycle count;
*
*Notes      :
*
*********************************************************************************************************
*/
cycle_t aw_clksrc_read(struct clocksource *cs)
{
    unsigned long   flags;
    __u32           lower, upper;

	spin_lock_irqsave(&clksrc_lock, flags);

    /* latch 64bit counter and wait ready for read */
    TMR_REG_CNT64_CTL |= (1<<1);
    while(TMR_REG_CNT64_CTL & (1<<1));

    /* read the 64bits counter */
    lower = TMR_REG_CNT64_LO;
    upper = TMR_REG_CNT64_HI;

	spin_unlock_irqrestore(&clksrc_lock, flags);

    return (((__u64)upper)<<32) | lower;
}
EXPORT_SYMBOL(aw_clksrc_read);

u32 aw_sched_clock_read(void)
{
	u32 lower;
	unsigned long flags;

	spin_lock_irqsave(&clksrc_lock, flags);

	/* latch 64bit counter and wait ready for read */
	TMR_REG_CNT64_CTL |= (1 << 1);
	while (TMR_REG_CNT64_CTL & (1 << 1)) {}

	/* read the low 32bits counter */
	lower = TMR_REG_CNT64_LO;
	spin_unlock_irqrestore(&clksrc_lock, flags);

	return lower;
}

/*
*********************************************************************************************************
*                           aw_set_clkevt_mode
*
*Description: set clock event work mode.
*
*Arguments  : mode  mode for clock event work;
*             dev   clock event device;
*
*Return     : none
*
*Notes      :
*
*********************************************************************************************************
*/
static void aw_set_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	int nr = dev->irq - SW_INT_IRQNO_TIMER0;
	unsigned long flags;

	CLKSRC_DBG("aw_set_clkevt_mode(%d): %u\n", nr, mode);

	spin_lock_irqsave(&tmr_spin_lock[nr], flags);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* set timer work with continueous mode */
		TMR_REG_TMR_CTL(nr) &= ~(1<<0);
		/* wait hardware synchronization, 2 clock cycles at least */
		__delay(50 * tmr_div[nr]);
		TMR_REG_TMR_CTL(nr) &= ~(1<<7);
		TMR_REG_TMR_CTL(nr) |= (1<<0);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* set timer work with onshot mode */
		TMR_REG_TMR_CTL(nr) &= ~(1<<0);
		/* wait hardware synchronization, 2 clock cycles at least */
		__delay(50 * tmr_div[nr]);
		TMR_REG_TMR_CTL(nr) |= (1<<7);
		TMR_REG_TMR_CTL(nr) |= (1<<0);
		break;
	default:
		/* disable clock event device */
		TMR_REG_TMR_CTL(nr) &= ~(1<<0);
		/* wait hardware synchronization, 2 clock cycles at least */
		__delay(50 * tmr_div[nr]);
	}
	spin_unlock_irqrestore(&tmr_spin_lock[nr], flags);
}


/*
*********************************************************************************************************
*                           aw_set_next_clkevt
*
*Description: set next clock event.
*
*Arguments  : delta     cycle count for next clock event.
*             dev       clock event device.
*
*Return     : result,
*               0,  set next event successed;
*              !0,  set next event failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static int aw_set_next_clkevt(unsigned long delta, struct clock_event_device *dev)
{
	int nr = dev->irq - SW_INT_IRQNO_TIMER0;
	unsigned long flags;

	CLKSRC_DBG("aw_set_next_clkevt(%d): %u\n", nr, (unsigned int)delta);

	spin_lock_irqsave(&tmr_spin_lock[nr], flags);
	/* disable timer and clear pending first */
	TMR_REG_TMR_CTL(nr) &= ~(1<<0);
	/* wait hardware synchronization, 2 cycles of the hardware work clock at least  */
	udelay(1);

	/* set timer intervalue */
	TMR_REG_TMR_INTV(nr) = delta;
	/* reload the timer intervalue  */
	TMR_REG_TMR_CTL(nr) |= (1<<1);

	/* enable timer */
	TMR_REG_TMR_CTL(nr) |= (1<<0);
	spin_unlock_irqrestore(&tmr_spin_lock[nr], flags);
	return 0;
}

/*
*********************************************************************************************************
*                                   aw_clkevt_irq
*
*Description: clock event interrupt handler.
*
*Arguments  : irq       interrupt number of current processed;
*             handle    device handle registered when setup irq;
*
*Return     : result,
*               IRQ_HANDLED,    irq is processed successed;
*               IRQ_NONE,       irq is not setup by us;
*
*Notes      :
*
*********************************************************************************************************
*/
static irqreturn_t aw_clkevt_irq(int irq, void *handle)
{
	int nr = irq - SW_INT_IRQNO_TIMER0;
	struct clock_event_device *clk_dev = handle;

	if (TMR_REG_IRQ_STAT & (1 << nr)) {
		CLKSRC_DBG("aw_clkevt_irq!\n");

		/* clear pending */
		TMR_REG_IRQ_STAT = (1 << nr);

		/* clock event interrupt handled */
		if (likely(clk_dev->event_handler != NULL))
			clk_dev->event_handler(clk_dev);

		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}


/*
*********************************************************************************************************
*                           aw_clksrc_init
*
*Description: clock source initialise.
*
*Arguments  : none
*
*Return     : result,
*               0,  initiate successed;
*              !0,  initiate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
int __init aw_clksrc_init(void)
{
	CLKSRC_DBG("all-winners clock source init!\n");

	/*
	 * The sun7i has separate 64 bits counters per osc, see clocksrc.h, so
	 * there is no need to set the clock source for the counter on sun7i.
	 * Moreover these counters share some logic with the arch_timer.c
	 * counters and mucking with them makes arch_timer.c unhappy.
	 */
	if (!sunxi_is_sun7i()) {
		TMR_REG_CNT64_CTL = 0;
		__delay(50);

		/* config clock source for 64bits counter */
#if(AW_HPET_CLK_SRC == TMR_CLK_SRC_24MHOSC)
		TMR_REG_CNT64_CTL |= (0 << 2);
#else	
		TMR_REG_CNT64_CTL |= (1 << 2);
#endif
		__delay(50);

		/* clear 64bits counter */
		TMR_REG_CNT64_CTL |= (1 << 0);
		while (TMR_REG_CNT64_CTL & (1 << 0)) {}
	}

	CLKSRC_DBG("register all-winners clock source!\n");
	/* calculate the mult by shift  */
	aw_clocksrc.mult = clocksource_hz2mult(AW_HPET_CLOCK_SOURCE_HZ,
					       aw_clocksrc.shift);
	/* register clock source */
	clocksource_register(&aw_clocksrc);

	return 0;
}

/*
*********************************************************************************************************
*                           aw_clkevt_init
*
*Description: clock event initialise.
*
*Arguments  : none
*
*Return     : result,
*               0,  initiate successed;
*              !0,  initiate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static void register_clk_dev(struct clock_event_device *clk_dev, int freq)
{
	clk_dev->mult = div_sc(freq, NSEC_PER_SEC, clk_dev->shift);
	/* time value timer must larger than 50 cycles at least,
	   suggested by david 2011-5-25 11:41 */
	clk_dev->min_delta_ns = clockevent_delta2ns(0x1, clk_dev) + 100000;
	clk_dev->max_delta_ns = clockevent_delta2ns(0x80000000, clk_dev);
	clk_dev->cpumask = cpu_all_mask;
	clockevents_register_device(clk_dev);
}

int __init aw_clkevt_init(void)
{
	u32 val = 0;

	/* disable & clear all timers */
	TMR_REG_IRQ_EN = 0x00;
	TMR_REG_IRQ_STAT = 0x1ff;

	/* init timer0 */
	CLKSRC_DBG("set up timer0\n");
	spin_lock_init(&tmr_spin_lock[0]);
	/* clear timer0 setting */
	TMR_REG_TMR_CTL(0) = 0;
	/* initialise timer0 interval value */
	TMR_REG_TMR_INTV(0) = TMR_INTER_VAL;
	/* set clock source to HOSC, 16 pre-division, auto-reload */
	val = 0 << 7; /* continuous mode */
	val |= 0b100 << 4; /* pre-scale: 16 */
	val |= 0b01 << 2; /* src: osc24M */
	val |= 1 << 1; /* auto-reload interval value */
	TMR_REG_TMR_CTL(0) = val;

	/* install timer0 irq */
	setup_irq(SW_INT_IRQNO_TIMER0, &sw_timer_irq);
	/* enable timer0 irq */
	TMR_REG_IRQ_EN |= (1 << 0);

	/* register timer0 */
	CLKSRC_DBG("register timer0\n");
	register_clk_dev(&timer0_clockevent, SYS_TIMER_CLKSRC/SYS_TIMER_SCAL);

#ifdef CONFIG_HIGH_RES_TIMERS
	CLKSRC_DBG("set up timer1 / aw clock event device (high res timer)\n");
	spin_lock_init(&tmr_spin_lock[1]);
	/* clear timer1 setting */
	TMR_REG_TMR_CTL(1) = 0;
	/* initialise timer1 interval value to 1 tick */
	TMR_REG_TMR_INTV(1) = AW_HPET_CLOCK_EVENT_HZ/HZ;

	/* config clock source for timer1 */
#if(AW_HPET_CLK_EVT == TMR_CLK_SRC_24MHOSC)
	TMR_REG_TMR_CTL(1) |= (1<<2);
#else
	TMR_REG_TMR_CTL(1) |= (0<<2);
#endif
	/* reload inter value */
	TMR_REG_TMR_CTL(1) |= (1<<1);
	/* install timer irq */
	setup_irq(SW_INT_IRQNO_TIMER1, &aw_clkevt_irqact);
	/* enable timer1 irq */
	TMR_REG_IRQ_EN |= (1<<1);

	/* register clock event device  */
	CLKSRC_DBG("register all-winners clock event device!\n");
	register_clk_dev(&aw_clock_event, AW_HPET_CLOCK_EVENT_HZ);
#endif

	return 0;
}
