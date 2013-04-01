/* linux/arch/arm/mach-exynos4/mct.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 MCT(Multi-Core Timer) support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/percpu.h>
#include <linux/of.h>

#include <asm/arch_timer.h>
#include <asm/localtimer.h>

#include <plat/cpu.h>

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/regs-mct.h>
#include <asm/mach/time.h>

#define TICK_BASE_CNT	1

enum {
	MCT_INT_SPI,
	MCT_INT_PPI
};

static unsigned long clk_rate;
static unsigned int mct_int_type;

struct mct_clock_event_device {
	struct clock_event_device *evt;
	void __iomem *base;
	char name[10];
};

static void exynos4_mct_write(unsigned int value, void *addr)
{
	void __iomem *stat_addr;
	u32 mask;
	u32 i;

	__raw_writel(value, addr);

	if (likely(addr >= EXYNOS4_MCT_L_BASE(0))) {
		u32 base = (u32) addr & EXYNOS4_MCT_L_MASK;
		switch ((u32) addr & ~EXYNOS4_MCT_L_MASK) {
		case (u32) MCT_L_TCON_OFFSET:
			stat_addr = (void __iomem *) base + MCT_L_WSTAT_OFFSET;
			mask = 1 << 3;		/* L_TCON write status */
			break;
		case (u32) MCT_L_ICNTB_OFFSET:
			stat_addr = (void __iomem *) base + MCT_L_WSTAT_OFFSET;
			mask = 1 << 1;		/* L_ICNTB write status */
			break;
		case (u32) MCT_L_TCNTB_OFFSET:
			stat_addr = (void __iomem *) base + MCT_L_WSTAT_OFFSET;
			mask = 1 << 0;		/* L_TCNTB write status */
			break;
		default:
			return;
		}
	} else {
		switch ((u32) addr) {
		case (u32) EXYNOS4_MCT_G_TCON:
			stat_addr = EXYNOS4_MCT_G_WSTAT;
			mask = 1 << 16;		/* G_TCON write status */
			break;
		case (u32) EXYNOS4_MCT_G_COMP0_L:
			stat_addr = EXYNOS4_MCT_G_WSTAT;
			mask = 1 << 0;		/* G_COMP0_L write status */
			break;
		case (u32) EXYNOS4_MCT_G_COMP0_U:
			stat_addr = EXYNOS4_MCT_G_WSTAT;
			mask = 1 << 1;		/* G_COMP0_U write status */
			break;
		case (u32) EXYNOS4_MCT_G_COMP0_ADD_INCR:
			stat_addr = EXYNOS4_MCT_G_WSTAT;
			mask = 1 << 2;		/* G_COMP0_ADD_INCR w status */
			break;
		case (u32) EXYNOS4_MCT_G_CNT_L:
			stat_addr = EXYNOS4_MCT_G_CNT_WSTAT;
			mask = 1 << 0;		/* G_CNT_L write status */
			break;
		case (u32) EXYNOS4_MCT_G_CNT_U:
			stat_addr = EXYNOS4_MCT_G_CNT_WSTAT;
			mask = 1 << 1;		/* G_CNT_U write status */
			break;
		default:
			return;
		}
	}

	/* Wait maximum 1 ms until written values are applied */
	for (i = 0; i < loops_per_jiffy / 1000 * HZ; i++)
		if (__raw_readl(stat_addr) & mask) {
			__raw_writel(mask, stat_addr);
			return;
		}

	panic("MCT hangs after writing %d (addr:0x%08x)\n", value, (u32)addr);
}

/* Clocksource handling */
static void exynos4_mct_frc_start(u32 hi, u32 lo)
{
	u32 reg;

	exynos4_mct_write(lo, EXYNOS4_MCT_G_CNT_L);
	exynos4_mct_write(hi, EXYNOS4_MCT_G_CNT_U);

	reg = __raw_readl(EXYNOS4_MCT_G_TCON);
	reg |= MCT_G_TCON_START;
	exynos4_mct_write(reg, EXYNOS4_MCT_G_TCON);
}

static cycle_t exynos4_frc_read(struct clocksource *cs)
{
	unsigned int lo, hi;
	u32 hi2 = __raw_readl(EXYNOS4_MCT_G_CNT_U);

	do {
		hi = hi2;
		lo = __raw_readl(EXYNOS4_MCT_G_CNT_L);
		hi2 = __raw_readl(EXYNOS4_MCT_G_CNT_U);
	} while (hi != hi2);

	return ((cycle_t)hi << 32) | lo;
}

static void exynos4_frc_resume(struct clocksource *cs)
{
	exynos4_mct_frc_start(0, 0);
}

struct clocksource mct_frc = {
	.name		= "mct-frc",
	.rating		= 400,
	.read		= exynos4_frc_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume		= exynos4_frc_resume,
};

static void __init exynos4_clocksource_init(void)
{
	exynos4_mct_frc_start(0, 0);

	if (clocksource_register_hz(&mct_frc, clk_rate))
		panic("%s: can't register clocksource\n", mct_frc.name);
}

static void exynos4_mct_comp0_stop(void)
{
	unsigned int tcon;

	tcon = __raw_readl(EXYNOS4_MCT_G_TCON);
	tcon &= ~(MCT_G_TCON_COMP0_ENABLE | MCT_G_TCON_COMP0_AUTO_INC);

	exynos4_mct_write(tcon, EXYNOS4_MCT_G_TCON);
	exynos4_mct_write(0, EXYNOS4_MCT_G_INT_ENB);
}

static void exynos4_mct_comp0_start(enum clock_event_mode mode,
				    unsigned long cycles)
{
	unsigned int tcon;
	cycle_t comp_cycle;

	tcon = __raw_readl(EXYNOS4_MCT_G_TCON);

	if (mode == CLOCK_EVT_MODE_PERIODIC) {
		tcon |= MCT_G_TCON_COMP0_AUTO_INC;
		exynos4_mct_write(cycles, EXYNOS4_MCT_G_COMP0_ADD_INCR);
	}

	comp_cycle = exynos4_frc_read(&mct_frc) + cycles;
	exynos4_mct_write((u32)comp_cycle, EXYNOS4_MCT_G_COMP0_L);
	exynos4_mct_write((u32)(comp_cycle >> 32), EXYNOS4_MCT_G_COMP0_U);

	exynos4_mct_write(0x1, EXYNOS4_MCT_G_INT_ENB);

	tcon |= MCT_G_TCON_COMP0_ENABLE;
	exynos4_mct_write(tcon , EXYNOS4_MCT_G_TCON);
}

static int exynos4_comp_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	exynos4_mct_comp0_start(evt->mode, cycles);

	return 0;
}

static void exynos4_comp_set_mode(enum clock_event_mode mode,
				  struct clock_event_device *evt)
{
	unsigned long cycles_per_jiffy;
	exynos4_mct_comp0_stop();

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		cycles_per_jiffy =
			(((unsigned long long) NSEC_PER_SEC / HZ * evt->mult) >> evt->shift);
		exynos4_mct_comp0_start(mode, cycles_per_jiffy);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device mct_comp_device = {
	.name		= "mct-comp",
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 250,
	.set_next_event	= exynos4_comp_set_next_event,
	.set_mode	= exynos4_comp_set_mode,
};

static irqreturn_t exynos4_mct_comp_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	exynos4_mct_write(0x1, EXYNOS4_MCT_G_INT_CSTAT);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction mct_comp_event_irq = {
	.name		= "mct_comp_irq",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= exynos4_mct_comp_isr,
	.dev_id		= &mct_comp_device,
};

static void exynos4_clockevent_init(void)
{
	mct_comp_device.cpumask = cpumask_of(0);
	clockevents_config_and_register(&mct_comp_device, clk_rate,
					0xf, 0xffffffff);

	if (soc_is_exynos5250())
		setup_irq(EXYNOS5_IRQ_MCT_G0, &mct_comp_event_irq);
	else
		setup_irq(EXYNOS4_IRQ_MCT_G0, &mct_comp_event_irq);
}

#ifdef CONFIG_LOCAL_TIMERS

static DEFINE_PER_CPU(struct mct_clock_event_device, percpu_mct_tick);

/* Clock event handling */
static void exynos4_mct_tick_stop(struct mct_clock_event_device *mevt)
{
	unsigned long tmp;
	unsigned long mask = MCT_L_TCON_INT_START | MCT_L_TCON_TIMER_START;
	void __iomem *addr = mevt->base + MCT_L_TCON_OFFSET;

	tmp = __raw_readl(addr);
	if (tmp & mask) {
		tmp &= ~mask;
		exynos4_mct_write(tmp, addr);
	}
}

static void exynos4_mct_tick_start(unsigned long cycles,
				   struct mct_clock_event_device *mevt)
{
	unsigned long tmp;

	exynos4_mct_tick_stop(mevt);

	tmp = (1 << 31) | cycles;	/* MCT_L_UPDATE_ICNTB */

	/* update interrupt count buffer */
	exynos4_mct_write(tmp, mevt->base + MCT_L_ICNTB_OFFSET);

	/* enable MCT tick interrupt */
	exynos4_mct_write(0x1, mevt->base + MCT_L_INT_ENB_OFFSET);

	tmp = __raw_readl(mevt->base + MCT_L_TCON_OFFSET);
	tmp |= MCT_L_TCON_INT_START | MCT_L_TCON_TIMER_START |
	       MCT_L_TCON_INTERVAL_MODE;
	exynos4_mct_write(tmp, mevt->base + MCT_L_TCON_OFFSET);
}

static int exynos4_tick_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	struct mct_clock_event_device *mevt = this_cpu_ptr(&percpu_mct_tick);

	exynos4_mct_tick_start(cycles, mevt);

	return 0;
}

static inline void exynos4_tick_set_mode(enum clock_event_mode mode,
					 struct clock_event_device *evt)
{
	struct mct_clock_event_device *mevt = this_cpu_ptr(&percpu_mct_tick);
	unsigned long cycles_per_jiffy;

	exynos4_mct_tick_stop(mevt);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		cycles_per_jiffy =
			(((unsigned long long) NSEC_PER_SEC / HZ * evt->mult) >> evt->shift);
		exynos4_mct_tick_start(cycles_per_jiffy, mevt);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int exynos4_mct_tick_clear(struct mct_clock_event_device *mevt)
{
	struct clock_event_device *evt = mevt->evt;

	/*
	 * This is for supporting oneshot mode.
	 * Mct would generate interrupt periodically
	 * without explicit stopping.
	 */
	if (evt->mode != CLOCK_EVT_MODE_PERIODIC)
		exynos4_mct_tick_stop(mevt);

	/* Clear the MCT tick interrupt */
	if (__raw_readl(mevt->base + MCT_L_INT_CSTAT_OFFSET) & 1) {
		exynos4_mct_write(0x1, mevt->base + MCT_L_INT_CSTAT_OFFSET);
		return 1;
	} else {
		return 0;
	}
}

static irqreturn_t exynos4_mct_tick_isr(int irq, void *dev_id)
{
	struct mct_clock_event_device *mevt = dev_id;
	struct clock_event_device *evt = mevt->evt;

	exynos4_mct_tick_clear(mevt);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction mct_tick0_event_irq = {
	.name		= "mct_tick0_irq",
	.flags		= IRQF_TIMER | IRQF_NOBALANCING,
	.handler	= exynos4_mct_tick_isr,
};

static struct irqaction mct_tick1_event_irq = {
	.name		= "mct_tick1_irq",
	.flags		= IRQF_TIMER | IRQF_NOBALANCING,
	.handler	= exynos4_mct_tick_isr,
};

static int __cpuinit exynos4_local_timer_setup(struct clock_event_device *evt)
{
	struct mct_clock_event_device *mevt;
	unsigned int cpu = smp_processor_id();
	int mct_lx_irq;

	mevt = this_cpu_ptr(&percpu_mct_tick);
	mevt->evt = evt;

	mevt->base = EXYNOS4_MCT_L_BASE(cpu);
	sprintf(mevt->name, "mct_tick%d", cpu);

	evt->name = mevt->name;
	evt->cpumask = cpumask_of(cpu);
	evt->set_next_event = exynos4_tick_set_next_event;
	evt->set_mode = exynos4_tick_set_mode;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->rating = 450;
	clockevents_config_and_register(evt, clk_rate / (TICK_BASE_CNT + 1),
					0xf, 0x7fffffff);

	exynos4_mct_write(TICK_BASE_CNT, mevt->base + MCT_L_TCNTB_OFFSET);

	if (mct_int_type == MCT_INT_SPI) {
		if (cpu == 0) {
			mct_lx_irq = soc_is_exynos4210() ? EXYNOS4_IRQ_MCT_L0 :
						EXYNOS5_IRQ_MCT_L0;
			mct_tick0_event_irq.dev_id = mevt;
			evt->irq = mct_lx_irq;
			setup_irq(mct_lx_irq, &mct_tick0_event_irq);
		} else {
			mct_lx_irq = soc_is_exynos4210() ? EXYNOS4_IRQ_MCT_L1 :
						EXYNOS5_IRQ_MCT_L1;
			mct_tick1_event_irq.dev_id = mevt;
			evt->irq = mct_lx_irq;
			setup_irq(mct_lx_irq, &mct_tick1_event_irq);
			irq_set_affinity(mct_lx_irq, cpumask_of(1));
		}
	} else {
		enable_percpu_irq(EXYNOS_IRQ_MCT_LOCALTIMER, 0);
	}

	return 0;
}

static void exynos4_local_timer_stop(struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	if (mct_int_type == MCT_INT_SPI)
		if (cpu == 0)
			remove_irq(evt->irq, &mct_tick0_event_irq);
		else
			remove_irq(evt->irq, &mct_tick1_event_irq);
	else
		disable_percpu_irq(EXYNOS_IRQ_MCT_LOCALTIMER);
}

static struct local_timer_ops exynos4_mct_tick_ops __cpuinitdata = {
	.setup	= exynos4_local_timer_setup,
	.stop	= exynos4_local_timer_stop,
};
#endif /* CONFIG_LOCAL_TIMERS */

static void __init exynos4_timer_resources(void)
{
	struct clk *mct_clk;
	mct_clk = clk_get(NULL, "xtal");

	clk_rate = clk_get_rate(mct_clk);

#ifdef CONFIG_LOCAL_TIMERS
	if (mct_int_type == MCT_INT_PPI) {
		int err;

		err = request_percpu_irq(EXYNOS_IRQ_MCT_LOCALTIMER,
					 exynos4_mct_tick_isr, "MCT",
					 &percpu_mct_tick);
		WARN(err, "MCT: can't request IRQ %d (%d)\n",
		     EXYNOS_IRQ_MCT_LOCALTIMER, err);
	}

	local_timer_register(&exynos4_mct_tick_ops);
#endif /* CONFIG_LOCAL_TIMERS */
}

void __init exynos4_timer_init(void)
{
	if (soc_is_exynos5440()) {
		arch_timer_of_register();
		return;
	}

	if ((soc_is_exynos4210()) || (soc_is_exynos5250()))
		mct_int_type = MCT_INT_SPI;
	else
		mct_int_type = MCT_INT_PPI;

	exynos4_timer_resources();
	exynos4_clocksource_init();
	exynos4_clockevent_init();
}
