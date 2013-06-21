/*
 * arch\arm\mach-sun7i\timer.c
 * (C) Copyright 2010-2016
 * Allwinner Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang<liugang@reuuimllatech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <asm/sched_clock.h>
#include <mach/includes.h>

#define TIMER0_VALUE   (AW_CLOCK_SRC / (AW_CLOCK_DIV * 100))

static void __iomem *timer_cpu_base = 0;
static spinlock_t timer0_spin_lock;
static DEFINE_SPINLOCK(clksrc_lock);

static cycle_t aw_clksrc_read(struct clocksource *cs)
{
	unsigned long flags;
	u32 lower, upper, temp, cnt = 0x0fffff;

	pr_info("%s(%d)\n", __func__, __LINE__);
	spin_lock_irqsave(&clksrc_lock, flags);

	/* latch 64bit counter and wait ready for read */
	temp = readl(SW_HSTMR_CTRL_REG);
	temp |= (1<<1);
	writel(temp, SW_HSTMR_CTRL_REG);
	while(cnt-- && (readl(SW_HSTMR_CTRL_REG) & (1<<1)));
	if(unlikely(0 == cnt))
		pr_err("%s(%d): wait read latched timeout\n", __func__, __LINE__);

	/* read the 64bits counter */
	lower = readl(SW_HSTMR_LOW_REG);
	upper = readl(SW_HSTMR_HIGH_REG);

	spin_unlock_irqrestore(&clksrc_lock, flags);
	pr_info("%s(%d)\n", __func__, __LINE__);
	return (((u64)upper)<<32) | lower;
}

static struct clocksource aw_clocksrc =
{
	.name = "sun7i high-res couter",
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

u32 sched_clock_read(void)
{
	u32   reg, lower, cnt = 0x0fffff;
	unsigned long   flags;

	spin_lock_irqsave(&clksrc_lock, flags);
	/* latch 64bit counter and wait ready for read */
	reg = readl(SW_HSTMR_CTRL_REG);
	reg |= (1<<1);
	writel(reg, SW_HSTMR_CTRL_REG);
	while(cnt-- && (readl(SW_HSTMR_CTRL_REG) & (1<<1)));
	if(unlikely(0 == cnt))
		pr_err("%s(%d): wait latched timeout\n", __func__, __LINE__);

	/* read the low 32bits counter */
	lower = readl(SW_HSTMR_LOW_REG);
	spin_unlock_irqrestore(&clksrc_lock, flags);

	return lower;
}

void __init aw_clksrc_init(void)
{
	pr_info("%s(%d)\n", __func__, __LINE__);

#if 0	/* start counting on booting, so cannot clear it, otherwise systime will be err */
	/* we use 64bits counter as HPET(High Precision Event Timer) */
	writel(0, SW_HSTMR_CTRL_REG);
	/* config clock source for 64bits counter */
	temp = readl(SW_HSTMR_CTRL_REG);
	temp &= ~(1<<2);
	/* clear 64bits counter */
	temp |= (1<<0);
	writel(temp, SW_HSTMR_CTRL_REG);
	/* wait clear complete */
	while(cnt-- && (readl(SW_HSTMR_CTRL_REG) & (1<<0)));
	if(unlikely(0 == cnt))
		pr_err("%s(%d): wait cleared timeout\n", __func__, __LINE__);
#endif

	/* calculate the mult by shift  */
	aw_clocksrc.mult = clocksource_hz2mult(AW_HPET_CLOCK_SOURCE_HZ, aw_clocksrc.shift);

	/* register clock source */
	clocksource_register(&aw_clocksrc);
	/* set sched clock */
	setup_sched_clock(sched_clock_read, 32, AW_HPET_CLOCK_SOURCE_HZ);
	pr_info("%s(%d)\n", __func__, __LINE__);
}

static void timer_set_mode(enum clock_event_mode mode, struct clock_event_device *clk)
{
	u32 ctrl = 0;

	if(clk) {
		switch (mode) {
		case CLOCK_EVT_MODE_PERIODIC:
			writel(TIMER0_VALUE, timer_cpu_base + TMR0_INTV_VALUE_REG_OFF); /* interval (999+1) */
			ctrl = readl(timer_cpu_base + TMR0_CTRL_REG_OFF);
			ctrl &= ~(1<<7);	/* Continuous mode */
			ctrl |= 1;  		/* Enable this timer */
			break;
		case CLOCK_EVT_MODE_ONESHOT:
			writel(TIMER0_VALUE, timer_cpu_base + TMR0_INTV_VALUE_REG_OFF); /* interval (999+1) */
			ctrl = readl(timer_cpu_base + TMR0_CTRL_REG_OFF);
			ctrl &= (1<<7);    	/* single mode */
			ctrl |= 1;  		/* Enable this timer */
			break;
		case CLOCK_EVT_MODE_UNUSED:
		case CLOCK_EVT_MODE_SHUTDOWN:
		default:
			ctrl = readl(timer_cpu_base + TMR0_CTRL_REG_OFF);
			ctrl &= ~(1<<0);    /* Disable timer0 */
			break;
		}

		writel(ctrl, timer_cpu_base + TMR0_CTRL_REG_OFF);
	} else {
		pr_warning("error:%s,line:%d\n", __func__, __LINE__);
		BUG();
	}
}

static int timer_set_next_clkevt(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long flags;
	u32 temp = 0;

	if(dev) {
		spin_lock_irqsave(&timer0_spin_lock, flags);

		/* disable timer and clear pending first    */
		temp = readl(timer_cpu_base + TMR0_CTRL_REG_OFF);
		temp &= ~(1<<0);
		writel(temp, timer_cpu_base + TMR0_CTRL_REG_OFF);
		udelay(1);
		/* set timer intervalue         */
		writel(delta, timer_cpu_base + TMR0_INTV_VALUE_REG_OFF);

		/* reload the timer intervalue  */
		temp = readl(timer_cpu_base + TMR0_CTRL_REG_OFF);
		temp |= (1<<1);
		writel(temp, timer_cpu_base + TMR0_CTRL_REG_OFF);

		/* enable timer */
		temp = readl(timer_cpu_base + TMR0_CTRL_REG_OFF);
		temp |= (1<<0);
		writel(temp, timer_cpu_base + TMR0_CTRL_REG_OFF);

		spin_unlock_irqrestore(&timer0_spin_lock, flags);
	} else {
	        pr_warning("error:%s,line:%d\n", __func__, __LINE__);
	        BUG();
	        return -EINVAL;
	}
	return 0;
}

static struct clock_event_device sun7i_timer0_clockevent = {
	.name = "timer0",
	.shift = 32,
	//.rating = 450, /* will lead to msleep NOT accurate,eg msleep(5) last 2 seconds, liugang */
	.rating = 100,
	.features = CLOCK_EVT_FEAT_PERIODIC,
	.set_mode = timer_set_mode,
	.set_next_event = timer_set_next_clkevt,
};

static irqreturn_t sun7i_timer_interrupt(int irq, void *dev_id)
{
	if (dev_id) {
		struct clock_event_device *evt = (struct clock_event_device *)dev_id;

		/* clear interrupt */
		writel(0x1, timer_cpu_base + TMR_IRQ_STA_REG_OFF);

		/* timer_set_next_event will be called only in ONESHOT mode */
		evt->event_handler(evt);
	} else {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		BUG();
		return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

static struct irqaction sun7i_timer_irq = {
	.name = "timer0",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = sun7i_timer_interrupt,
	.dev_id = &sun7i_timer0_clockevent,
	.irq = AW_IRQ_TIMER0,
};

void __init aw_clkevt_init(void)
{
	int ret;
	u32 val = 0;

	timer_cpu_base = ioremap_nocache(SW_PA_TIMERC_IO_BASE, 0x1000);
	pr_info("%s: timer base 0x%08x\n", __func__, (int)timer_cpu_base);

	/* disable & clear all timers */
	writel(0x0, timer_cpu_base + TMR_IRQ_EN_REG_OFF);
	writel(0x1ff, timer_cpu_base + TMR_IRQ_STA_REG_OFF); /* diff from 33 */

	/* init timer0 */
	writel(TIMER0_VALUE, timer_cpu_base + TMR0_INTV_VALUE_REG_OFF);
	val = 0 << 7; /* continuous mode */
	val |= 0b100 << 4; /* pre-scale: 16 */
	val |= 0b01 << 2; /* src: osc24M */
	val |= 1 << 1; /* reload interval value */
	writel(val, timer_cpu_base + TMR0_CTRL_REG_OFF);

	/* register timer0 interrupt */
	ret = setup_irq(AW_IRQ_TIMER0, &sun7i_timer_irq);
	if (ret)
		early_printk("failed to setup irq %d\n", 36);

	/* enable timer0 */
	writel(0x1, timer_cpu_base + TMR_IRQ_EN_REG_OFF);

	/* register clock event */
	sun7i_timer0_clockevent.mult = div_sc(AW_CLOCK_SRC/AW_CLOCK_DIV, NSEC_PER_SEC, sun7i_timer0_clockevent.shift);
	sun7i_timer0_clockevent.max_delta_ns = clockevent_delta2ns(0xff, &sun7i_timer0_clockevent);
	//sun7i_timer0_clockevent.min_delta_ns = clockevent_delta2ns(0x1, &sun7i_timer0_clockevent)+100000;
	sun7i_timer0_clockevent.min_delta_ns = clockevent_delta2ns(0x1, &sun7i_timer0_clockevent); /* liugang */
	sun7i_timer0_clockevent.cpumask = cpu_all_mask;
	sun7i_timer0_clockevent.irq = sun7i_timer_irq.irq;
	early_printk("%s: sun7i_timer0_clockevent mult %d, max_delta_ns %d, min_delta_ns %d, cpumask 0x%08x, irq %d\n",
		__func__, (int)sun7i_timer0_clockevent.mult, (int)sun7i_timer0_clockevent.max_delta_ns,
		(int)sun7i_timer0_clockevent.min_delta_ns, (int)sun7i_timer0_clockevent.cpumask,
		(int)sun7i_timer0_clockevent.irq);
	clockevents_register_device(&sun7i_timer0_clockevent);

#ifdef CONFIG_AW_TIME_DELAY
	use_time_delay();
#endif
}

