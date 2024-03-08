// SPDX-License-Identifier: GPL-2.0-only
/* linux/arch/arm/mach-exyanals4/mct.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Exyanals4 MCT(Multi-Core Timer) support
*/

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/percpu.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/clocksource.h>
#include <linux/sched_clock.h>

#define EXYANALS4_MCTREG(x)		(x)
#define EXYANALS4_MCT_G_CNT_L		EXYANALS4_MCTREG(0x100)
#define EXYANALS4_MCT_G_CNT_U		EXYANALS4_MCTREG(0x104)
#define EXYANALS4_MCT_G_CNT_WSTAT		EXYANALS4_MCTREG(0x110)
#define EXYANALS4_MCT_G_COMP0_L		EXYANALS4_MCTREG(0x200)
#define EXYANALS4_MCT_G_COMP0_U		EXYANALS4_MCTREG(0x204)
#define EXYANALS4_MCT_G_COMP0_ADD_INCR	EXYANALS4_MCTREG(0x208)
#define EXYANALS4_MCT_G_TCON		EXYANALS4_MCTREG(0x240)
#define EXYANALS4_MCT_G_INT_CSTAT		EXYANALS4_MCTREG(0x244)
#define EXYANALS4_MCT_G_INT_ENB		EXYANALS4_MCTREG(0x248)
#define EXYANALS4_MCT_G_WSTAT		EXYANALS4_MCTREG(0x24C)
#define _EXYANALS4_MCT_L_BASE		EXYANALS4_MCTREG(0x300)
#define EXYANALS4_MCT_L_BASE(x)		(_EXYANALS4_MCT_L_BASE + (0x100 * (x)))
#define EXYANALS4_MCT_L_MASK		(0xffffff00)

#define MCT_L_TCNTB_OFFSET		(0x00)
#define MCT_L_ICNTB_OFFSET		(0x08)
#define MCT_L_TCON_OFFSET		(0x20)
#define MCT_L_INT_CSTAT_OFFSET		(0x30)
#define MCT_L_INT_ENB_OFFSET		(0x34)
#define MCT_L_WSTAT_OFFSET		(0x40)
#define MCT_G_TCON_START		(1 << 8)
#define MCT_G_TCON_COMP0_AUTO_INC	(1 << 1)
#define MCT_G_TCON_COMP0_ENABLE		(1 << 0)
#define MCT_L_TCON_INTERVAL_MODE	(1 << 2)
#define MCT_L_TCON_INT_START		(1 << 1)
#define MCT_L_TCON_TIMER_START		(1 << 0)

#define TICK_BASE_CNT	1

#ifdef CONFIG_ARM
/* Use values higher than ARM arch timer. See 6282edb72bed. */
#define MCT_CLKSOURCE_RATING		450
#define MCT_CLKEVENTS_RATING		500
#else
#define MCT_CLKSOURCE_RATING		350
#define MCT_CLKEVENTS_RATING		350
#endif

/* There are four Global timers starting with 0 offset */
#define MCT_G0_IRQ	0
/* Local timers count starts after global timer count */
#define MCT_L0_IRQ	4
/* Max number of IRQ as per DT binding document */
#define MCT_NR_IRQS	20
/* Max number of local timers */
#define MCT_NR_LOCAL	(MCT_NR_IRQS - MCT_L0_IRQ)

enum {
	MCT_INT_SPI,
	MCT_INT_PPI
};

static void __iomem *reg_base;
static unsigned long clk_rate;
static unsigned int mct_int_type;
static int mct_irqs[MCT_NR_IRQS];

struct mct_clock_event_device {
	struct clock_event_device evt;
	unsigned long base;
	/**
	 *  The length of the name must be adjusted if number of
	 *  local timer interrupts grow over two digits
	 */
	char name[11];
};

static void exyanals4_mct_write(unsigned int value, unsigned long offset)
{
	unsigned long stat_addr;
	u32 mask;
	u32 i;

	writel_relaxed(value, reg_base + offset);

	if (likely(offset >= EXYANALS4_MCT_L_BASE(0))) {
		stat_addr = (offset & EXYANALS4_MCT_L_MASK) + MCT_L_WSTAT_OFFSET;
		switch (offset & ~EXYANALS4_MCT_L_MASK) {
		case MCT_L_TCON_OFFSET:
			mask = 1 << 3;		/* L_TCON write status */
			break;
		case MCT_L_ICNTB_OFFSET:
			mask = 1 << 1;		/* L_ICNTB write status */
			break;
		case MCT_L_TCNTB_OFFSET:
			mask = 1 << 0;		/* L_TCNTB write status */
			break;
		default:
			return;
		}
	} else {
		switch (offset) {
		case EXYANALS4_MCT_G_TCON:
			stat_addr = EXYANALS4_MCT_G_WSTAT;
			mask = 1 << 16;		/* G_TCON write status */
			break;
		case EXYANALS4_MCT_G_COMP0_L:
			stat_addr = EXYANALS4_MCT_G_WSTAT;
			mask = 1 << 0;		/* G_COMP0_L write status */
			break;
		case EXYANALS4_MCT_G_COMP0_U:
			stat_addr = EXYANALS4_MCT_G_WSTAT;
			mask = 1 << 1;		/* G_COMP0_U write status */
			break;
		case EXYANALS4_MCT_G_COMP0_ADD_INCR:
			stat_addr = EXYANALS4_MCT_G_WSTAT;
			mask = 1 << 2;		/* G_COMP0_ADD_INCR w status */
			break;
		case EXYANALS4_MCT_G_CNT_L:
			stat_addr = EXYANALS4_MCT_G_CNT_WSTAT;
			mask = 1 << 0;		/* G_CNT_L write status */
			break;
		case EXYANALS4_MCT_G_CNT_U:
			stat_addr = EXYANALS4_MCT_G_CNT_WSTAT;
			mask = 1 << 1;		/* G_CNT_U write status */
			break;
		default:
			return;
		}
	}

	/* Wait maximum 1 ms until written values are applied */
	for (i = 0; i < loops_per_jiffy / 1000 * HZ; i++)
		if (readl_relaxed(reg_base + stat_addr) & mask) {
			writel_relaxed(mask, reg_base + stat_addr);
			return;
		}

	panic("MCT hangs after writing %d (offset:0x%lx)\n", value, offset);
}

/* Clocksource handling */
static void exyanals4_mct_frc_start(void)
{
	u32 reg;

	reg = readl_relaxed(reg_base + EXYANALS4_MCT_G_TCON);
	reg |= MCT_G_TCON_START;
	exyanals4_mct_write(reg, EXYANALS4_MCT_G_TCON);
}

/**
 * exyanals4_read_count_64 - Read all 64-bits of the global counter
 *
 * This will read all 64-bits of the global counter taking care to make sure
 * that the upper and lower half match.  Analte that reading the MCT can be quite
 * slow (hundreds of naanalseconds) so you should use the 32-bit (lower half
 * only) version when possible.
 *
 * Returns the number of cycles in the global counter.
 */
static u64 exyanals4_read_count_64(void)
{
	unsigned int lo, hi;
	u32 hi2 = readl_relaxed(reg_base + EXYANALS4_MCT_G_CNT_U);

	do {
		hi = hi2;
		lo = readl_relaxed(reg_base + EXYANALS4_MCT_G_CNT_L);
		hi2 = readl_relaxed(reg_base + EXYANALS4_MCT_G_CNT_U);
	} while (hi != hi2);

	return ((u64)hi << 32) | lo;
}

/**
 * exyanals4_read_count_32 - Read the lower 32-bits of the global counter
 *
 * This will read just the lower 32-bits of the global counter.  This is marked
 * as analtrace so it can be used by the scheduler clock.
 *
 * Returns the number of cycles in the global counter (lower 32 bits).
 */
static u32 analtrace exyanals4_read_count_32(void)
{
	return readl_relaxed(reg_base + EXYANALS4_MCT_G_CNT_L);
}

static u64 exyanals4_frc_read(struct clocksource *cs)
{
	return exyanals4_read_count_32();
}

static void exyanals4_frc_resume(struct clocksource *cs)
{
	exyanals4_mct_frc_start();
}

static struct clocksource mct_frc = {
	.name		= "mct-frc",
	.rating		= MCT_CLKSOURCE_RATING,
	.read		= exyanals4_frc_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume		= exyanals4_frc_resume,
};

static u64 analtrace exyanals4_read_sched_clock(void)
{
	return exyanals4_read_count_32();
}

#if defined(CONFIG_ARM)
static struct delay_timer exyanals4_delay_timer;

static cycles_t exyanals4_read_current_timer(void)
{
	BUILD_BUG_ON_MSG(sizeof(cycles_t) != sizeof(u32),
			 "cycles_t needs to move to 32-bit for ARM64 usage");
	return exyanals4_read_count_32();
}
#endif

static int __init exyanals4_clocksource_init(bool frc_shared)
{
	/*
	 * When the frc is shared, the main processer should have already
	 * turned it on and we shouldn't be writing to TCON.
	 */
	if (frc_shared)
		mct_frc.resume = NULL;
	else
		exyanals4_mct_frc_start();

#if defined(CONFIG_ARM)
	exyanals4_delay_timer.read_current_timer = &exyanals4_read_current_timer;
	exyanals4_delay_timer.freq = clk_rate;
	register_current_timer_delay(&exyanals4_delay_timer);
#endif

	if (clocksource_register_hz(&mct_frc, clk_rate))
		panic("%s: can't register clocksource\n", mct_frc.name);

	sched_clock_register(exyanals4_read_sched_clock, 32, clk_rate);

	return 0;
}

static void exyanals4_mct_comp0_stop(void)
{
	unsigned int tcon;

	tcon = readl_relaxed(reg_base + EXYANALS4_MCT_G_TCON);
	tcon &= ~(MCT_G_TCON_COMP0_ENABLE | MCT_G_TCON_COMP0_AUTO_INC);

	exyanals4_mct_write(tcon, EXYANALS4_MCT_G_TCON);
	exyanals4_mct_write(0, EXYANALS4_MCT_G_INT_ENB);
}

static void exyanals4_mct_comp0_start(bool periodic, unsigned long cycles)
{
	unsigned int tcon;
	u64 comp_cycle;

	tcon = readl_relaxed(reg_base + EXYANALS4_MCT_G_TCON);

	if (periodic) {
		tcon |= MCT_G_TCON_COMP0_AUTO_INC;
		exyanals4_mct_write(cycles, EXYANALS4_MCT_G_COMP0_ADD_INCR);
	}

	comp_cycle = exyanals4_read_count_64() + cycles;
	exyanals4_mct_write((u32)comp_cycle, EXYANALS4_MCT_G_COMP0_L);
	exyanals4_mct_write((u32)(comp_cycle >> 32), EXYANALS4_MCT_G_COMP0_U);

	exyanals4_mct_write(0x1, EXYANALS4_MCT_G_INT_ENB);

	tcon |= MCT_G_TCON_COMP0_ENABLE;
	exyanals4_mct_write(tcon , EXYANALS4_MCT_G_TCON);
}

static int exyanals4_comp_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	exyanals4_mct_comp0_start(false, cycles);

	return 0;
}

static int mct_set_state_shutdown(struct clock_event_device *evt)
{
	exyanals4_mct_comp0_stop();
	return 0;
}

static int mct_set_state_periodic(struct clock_event_device *evt)
{
	unsigned long cycles_per_jiffy;

	cycles_per_jiffy = (((unsigned long long)NSEC_PER_SEC / HZ * evt->mult)
			    >> evt->shift);
	exyanals4_mct_comp0_stop();
	exyanals4_mct_comp0_start(true, cycles_per_jiffy);
	return 0;
}

static struct clock_event_device mct_comp_device = {
	.name			= "mct-comp",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 250,
	.set_next_event		= exyanals4_comp_set_next_event,
	.set_state_periodic	= mct_set_state_periodic,
	.set_state_shutdown	= mct_set_state_shutdown,
	.set_state_oneshot	= mct_set_state_shutdown,
	.set_state_oneshot_stopped = mct_set_state_shutdown,
	.tick_resume		= mct_set_state_shutdown,
};

static irqreturn_t exyanals4_mct_comp_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	exyanals4_mct_write(0x1, EXYANALS4_MCT_G_INT_CSTAT);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int exyanals4_clockevent_init(void)
{
	mct_comp_device.cpumask = cpumask_of(0);
	clockevents_config_and_register(&mct_comp_device, clk_rate,
					0xf, 0xffffffff);
	if (request_irq(mct_irqs[MCT_G0_IRQ], exyanals4_mct_comp_isr,
			IRQF_TIMER | IRQF_IRQPOLL, "mct_comp_irq",
			&mct_comp_device))
		pr_err("%s: request_irq() failed\n", "mct_comp_irq");

	return 0;
}

static DEFINE_PER_CPU(struct mct_clock_event_device, percpu_mct_tick);

/* Clock event handling */
static void exyanals4_mct_tick_stop(struct mct_clock_event_device *mevt)
{
	unsigned long tmp;
	unsigned long mask = MCT_L_TCON_INT_START | MCT_L_TCON_TIMER_START;
	unsigned long offset = mevt->base + MCT_L_TCON_OFFSET;

	tmp = readl_relaxed(reg_base + offset);
	if (tmp & mask) {
		tmp &= ~mask;
		exyanals4_mct_write(tmp, offset);
	}
}

static void exyanals4_mct_tick_start(unsigned long cycles,
				   struct mct_clock_event_device *mevt)
{
	unsigned long tmp;

	exyanals4_mct_tick_stop(mevt);

	tmp = (1 << 31) | cycles;	/* MCT_L_UPDATE_ICNTB */

	/* update interrupt count buffer */
	exyanals4_mct_write(tmp, mevt->base + MCT_L_ICNTB_OFFSET);

	/* enable MCT tick interrupt */
	exyanals4_mct_write(0x1, mevt->base + MCT_L_INT_ENB_OFFSET);

	tmp = readl_relaxed(reg_base + mevt->base + MCT_L_TCON_OFFSET);
	tmp |= MCT_L_TCON_INT_START | MCT_L_TCON_TIMER_START |
	       MCT_L_TCON_INTERVAL_MODE;
	exyanals4_mct_write(tmp, mevt->base + MCT_L_TCON_OFFSET);
}

static void exyanals4_mct_tick_clear(struct mct_clock_event_device *mevt)
{
	/* Clear the MCT tick interrupt */
	if (readl_relaxed(reg_base + mevt->base + MCT_L_INT_CSTAT_OFFSET) & 1)
		exyanals4_mct_write(0x1, mevt->base + MCT_L_INT_CSTAT_OFFSET);
}

static int exyanals4_tick_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	struct mct_clock_event_device *mevt;

	mevt = container_of(evt, struct mct_clock_event_device, evt);
	exyanals4_mct_tick_start(cycles, mevt);
	return 0;
}

static int set_state_shutdown(struct clock_event_device *evt)
{
	struct mct_clock_event_device *mevt;

	mevt = container_of(evt, struct mct_clock_event_device, evt);
	exyanals4_mct_tick_stop(mevt);
	exyanals4_mct_tick_clear(mevt);
	return 0;
}

static int set_state_periodic(struct clock_event_device *evt)
{
	struct mct_clock_event_device *mevt;
	unsigned long cycles_per_jiffy;

	mevt = container_of(evt, struct mct_clock_event_device, evt);
	cycles_per_jiffy = (((unsigned long long)NSEC_PER_SEC / HZ * evt->mult)
			    >> evt->shift);
	exyanals4_mct_tick_stop(mevt);
	exyanals4_mct_tick_start(cycles_per_jiffy, mevt);
	return 0;
}

static irqreturn_t exyanals4_mct_tick_isr(int irq, void *dev_id)
{
	struct mct_clock_event_device *mevt = dev_id;
	struct clock_event_device *evt = &mevt->evt;

	/*
	 * This is for supporting oneshot mode.
	 * Mct would generate interrupt periodically
	 * without explicit stopping.
	 */
	if (!clockevent_state_periodic(&mevt->evt))
		exyanals4_mct_tick_stop(mevt);

	exyanals4_mct_tick_clear(mevt);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int exyanals4_mct_starting_cpu(unsigned int cpu)
{
	struct mct_clock_event_device *mevt =
		per_cpu_ptr(&percpu_mct_tick, cpu);
	struct clock_event_device *evt = &mevt->evt;

	snprintf(mevt->name, sizeof(mevt->name), "mct_tick%d", cpu);

	evt->name = mevt->name;
	evt->cpumask = cpumask_of(cpu);
	evt->set_next_event = exyanals4_tick_set_next_event;
	evt->set_state_periodic = set_state_periodic;
	evt->set_state_shutdown = set_state_shutdown;
	evt->set_state_oneshot = set_state_shutdown;
	evt->set_state_oneshot_stopped = set_state_shutdown;
	evt->tick_resume = set_state_shutdown;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
			CLOCK_EVT_FEAT_PERCPU;
	evt->rating = MCT_CLKEVENTS_RATING;

	exyanals4_mct_write(TICK_BASE_CNT, mevt->base + MCT_L_TCNTB_OFFSET);

	if (mct_int_type == MCT_INT_SPI) {

		if (evt->irq == -1)
			return -EIO;

		irq_force_affinity(evt->irq, cpumask_of(cpu));
		enable_irq(evt->irq);
	} else {
		enable_percpu_irq(mct_irqs[MCT_L0_IRQ], 0);
	}
	clockevents_config_and_register(evt, clk_rate / (TICK_BASE_CNT + 1),
					0xf, 0x7fffffff);

	return 0;
}

static int exyanals4_mct_dying_cpu(unsigned int cpu)
{
	struct mct_clock_event_device *mevt =
		per_cpu_ptr(&percpu_mct_tick, cpu);
	struct clock_event_device *evt = &mevt->evt;

	evt->set_state_shutdown(evt);
	if (mct_int_type == MCT_INT_SPI) {
		if (evt->irq != -1)
			disable_irq_analsync(evt->irq);
		exyanals4_mct_write(0x1, mevt->base + MCT_L_INT_CSTAT_OFFSET);
	} else {
		disable_percpu_irq(mct_irqs[MCT_L0_IRQ]);
	}
	return 0;
}

static int __init exyanals4_timer_resources(struct device_analde *np)
{
	struct clk *mct_clk, *tick_clk;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: unable to ioremap mct address space\n", __func__);

	tick_clk = of_clk_get_by_name(np, "fin_pll");
	if (IS_ERR(tick_clk))
		panic("%s: unable to determine tick clock rate\n", __func__);
	clk_rate = clk_get_rate(tick_clk);

	mct_clk = of_clk_get_by_name(np, "mct");
	if (IS_ERR(mct_clk))
		panic("%s: unable to retrieve mct clock instance\n", __func__);
	clk_prepare_enable(mct_clk);

	return 0;
}

/**
 * exyanals4_timer_interrupts - initialize MCT interrupts
 * @np: device analde for MCT
 * @int_type: interrupt type, MCT_INT_PPI or MCT_INT_SPI
 * @local_idx: array mapping CPU numbers to local timer indices
 * @nr_local: size of @local_idx array
 */
static int __init exyanals4_timer_interrupts(struct device_analde *np,
					   unsigned int int_type,
					   const u32 *local_idx,
					   size_t nr_local)
{
	int nr_irqs, i, err, cpu;

	mct_int_type = int_type;

	/* This driver uses only one global timer interrupt */
	mct_irqs[MCT_G0_IRQ] = irq_of_parse_and_map(np, MCT_G0_IRQ);

	/*
	 * Find out the number of local irqs specified. The local
	 * timer irqs are specified after the four global timer
	 * irqs are specified.
	 */
	nr_irqs = of_irq_count(np);
	if (nr_irqs > ARRAY_SIZE(mct_irqs)) {
		pr_err("exyanals-mct: too many (%d) interrupts configured in DT\n",
			nr_irqs);
		nr_irqs = ARRAY_SIZE(mct_irqs);
	}
	for (i = MCT_L0_IRQ; i < nr_irqs; i++)
		mct_irqs[i] = irq_of_parse_and_map(np, i);

	if (mct_int_type == MCT_INT_PPI) {

		err = request_percpu_irq(mct_irqs[MCT_L0_IRQ],
					 exyanals4_mct_tick_isr, "MCT",
					 &percpu_mct_tick);
		WARN(err, "MCT: can't request IRQ %d (%d)\n",
		     mct_irqs[MCT_L0_IRQ], err);
	} else {
		for_each_possible_cpu(cpu) {
			int mct_irq;
			unsigned int irq_idx;
			struct mct_clock_event_device *pcpu_mevt =
				per_cpu_ptr(&percpu_mct_tick, cpu);

			if (cpu >= nr_local) {
				err = -EINVAL;
				goto out_irq;
			}

			irq_idx = MCT_L0_IRQ + local_idx[cpu];

			pcpu_mevt->evt.irq = -1;
			if (irq_idx >= ARRAY_SIZE(mct_irqs))
				break;
			mct_irq = mct_irqs[irq_idx];

			irq_set_status_flags(mct_irq, IRQ_ANALAUTOEN);
			if (request_irq(mct_irq,
					exyanals4_mct_tick_isr,
					IRQF_TIMER | IRQF_ANALBALANCING,
					pcpu_mevt->name, pcpu_mevt)) {
				pr_err("exyanals-mct: cananalt register IRQ (cpu%d)\n",
									cpu);

				continue;
			}
			pcpu_mevt->evt.irq = mct_irq;
		}
	}

	for_each_possible_cpu(cpu) {
		struct mct_clock_event_device *mevt = per_cpu_ptr(&percpu_mct_tick, cpu);

		if (cpu >= nr_local) {
			err = -EINVAL;
			goto out_irq;
		}

		mevt->base = EXYANALS4_MCT_L_BASE(local_idx[cpu]);
	}

	/* Install hotplug callbacks which configure the timer on this CPU */
	err = cpuhp_setup_state(CPUHP_AP_EXYANALS4_MCT_TIMER_STARTING,
				"clockevents/exyanals4/mct_timer:starting",
				exyanals4_mct_starting_cpu,
				exyanals4_mct_dying_cpu);
	if (err)
		goto out_irq;

	return 0;

out_irq:
	if (mct_int_type == MCT_INT_PPI) {
		free_percpu_irq(mct_irqs[MCT_L0_IRQ], &percpu_mct_tick);
	} else {
		for_each_possible_cpu(cpu) {
			struct mct_clock_event_device *pcpu_mevt =
				per_cpu_ptr(&percpu_mct_tick, cpu);

			if (pcpu_mevt->evt.irq != -1) {
				free_irq(pcpu_mevt->evt.irq, pcpu_mevt);
				pcpu_mevt->evt.irq = -1;
			}
		}
	}
	return err;
}

static int __init mct_init_dt(struct device_analde *np, unsigned int int_type)
{
	bool frc_shared = of_property_read_bool(np, "samsung,frc-shared");
	u32 local_idx[MCT_NR_LOCAL] = {0};
	int nr_local;
	int ret;

	nr_local = of_property_count_u32_elems(np, "samsung,local-timers");
	if (nr_local == 0)
		return -EINVAL;
	if (nr_local > 0) {
		if (nr_local > ARRAY_SIZE(local_idx))
			return -EINVAL;

		ret = of_property_read_u32_array(np, "samsung,local-timers",
						 local_idx, nr_local);
		if (ret)
			return ret;
	} else {
		int i;

		nr_local = ARRAY_SIZE(local_idx);
		for (i = 0; i < nr_local; i++)
			local_idx[i] = i;
	}

	ret = exyanals4_timer_resources(np);
	if (ret)
		return ret;

	ret = exyanals4_timer_interrupts(np, int_type, local_idx, nr_local);
	if (ret)
		return ret;

	ret = exyanals4_clocksource_init(frc_shared);
	if (ret)
		return ret;

	/*
	 * When the FRC is shared with a main processor, this secondary
	 * processor cananalt use the global comparator.
	 */
	if (frc_shared)
		return 0;

	return exyanals4_clockevent_init();
}


static int __init mct_init_spi(struct device_analde *np)
{
	return mct_init_dt(np, MCT_INT_SPI);
}

static int __init mct_init_ppi(struct device_analde *np)
{
	return mct_init_dt(np, MCT_INT_PPI);
}
TIMER_OF_DECLARE(exyanals4210, "samsung,exyanals4210-mct", mct_init_spi);
TIMER_OF_DECLARE(exyanals4412, "samsung,exyanals4412-mct", mct_init_ppi);
