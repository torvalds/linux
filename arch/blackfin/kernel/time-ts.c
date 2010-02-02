/*
 * Based on arm clockevents implementation and old bfin time tick.
 *
 * Copyright 2008-2009 Analog Devics Inc.
 *                2008 GeoTechnologies
 *                     Vitja Makarov
 *
 * Licensed under the GPL-2
 */

#include <linux/module.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cpufreq.h>

#include <asm/blackfin.h>
#include <asm/time.h>
#include <asm/gptimers.h>

/* Accelerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better precision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */

#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

#if defined(CONFIG_CYCLES_CLOCKSOURCE)

static notrace cycle_t bfin_read_cycles(struct clocksource *cs)
{
	return __bfin_cycles_off + (get_cycles() << __bfin_cycles_mod);
}

static struct clocksource bfin_cs_cycles = {
	.name		= "bfin_cs_cycles",
	.rating		= 400,
	.read		= bfin_read_cycles,
	.mask		= CLOCKSOURCE_MASK(64),
	.shift		= CYC2NS_SCALE_FACTOR,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static inline unsigned long long bfin_cs_cycles_sched_clock(void)
{
	return clocksource_cyc2ns(bfin_read_cycles(&bfin_cs_cycles),
		bfin_cs_cycles.mult, bfin_cs_cycles.shift);
}

static int __init bfin_cs_cycles_init(void)
{
	bfin_cs_cycles.mult = \
		clocksource_hz2mult(get_cclk(), bfin_cs_cycles.shift);

	if (clocksource_register(&bfin_cs_cycles))
		panic("failed to register clocksource");

	return 0;
}
#else
# define bfin_cs_cycles_init()
#endif

#ifdef CONFIG_GPTMR0_CLOCKSOURCE

void __init setup_gptimer0(void)
{
	disable_gptimers(TIMER0bit);

	set_gptimer_config(TIMER0_id, \
		TIMER_OUT_DIS | TIMER_PERIOD_CNT | TIMER_MODE_PWM);
	set_gptimer_period(TIMER0_id, -1);
	set_gptimer_pwidth(TIMER0_id, -2);
	SSYNC();
	enable_gptimers(TIMER0bit);
}

static cycle_t bfin_read_gptimer0(struct clocksource *cs)
{
	return bfin_read_TIMER0_COUNTER();
}

static struct clocksource bfin_cs_gptimer0 = {
	.name		= "bfin_cs_gptimer0",
	.rating		= 350,
	.read		= bfin_read_gptimer0,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= CYC2NS_SCALE_FACTOR,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static inline unsigned long long bfin_cs_gptimer0_sched_clock(void)
{
	return clocksource_cyc2ns(bfin_read_TIMER0_COUNTER(),
		bfin_cs_gptimer0.mult, bfin_cs_gptimer0.shift);
}

static int __init bfin_cs_gptimer0_init(void)
{
	setup_gptimer0();

	bfin_cs_gptimer0.mult = \
		clocksource_hz2mult(get_sclk(), bfin_cs_gptimer0.shift);

	if (clocksource_register(&bfin_cs_gptimer0))
		panic("failed to register clocksource");

	return 0;
}
#else
# define bfin_cs_gptimer0_init()
#endif


#if defined(CONFIG_GPTMR0_CLOCKSOURCE) || defined(CONFIG_CYCLES_CLOCKSOURCE)
/* prefer to use cycles since it has higher rating */
notrace unsigned long long sched_clock(void)
{
#if defined(CONFIG_CYCLES_CLOCKSOURCE)
	return bfin_cs_cycles_sched_clock();
#else
	return bfin_cs_gptimer0_sched_clock();
#endif
}
#endif

#ifdef CONFIG_CORE_TIMER_IRQ_L1
__attribute__((l1_text))
#endif
irqreturn_t timer_interrupt(int irq, void *dev_id);

static int bfin_timer_set_next_event(unsigned long, \
		struct clock_event_device *);

static void bfin_timer_set_mode(enum clock_event_mode, \
		struct clock_event_device *);

static struct clock_event_device clockevent_bfin = {
#if defined(CONFIG_TICKSOURCE_GPTMR0)
	.name		= "bfin_gptimer0",
	.rating		= 300,
	.irq		= IRQ_TIMER0,
#else
	.name		= "bfin_core_timer",
	.rating		= 350,
	.irq		= IRQ_CORETMR,
#endif
	.shift		= 32,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event = bfin_timer_set_next_event,
	.set_mode	= bfin_timer_set_mode,
};

static struct irqaction bfin_timer_irq = {
#if defined(CONFIG_TICKSOURCE_GPTMR0)
	.name		= "Blackfin GPTimer0",
#else
	.name		= "Blackfin CoreTimer",
#endif
	.flags		= IRQF_DISABLED | IRQF_TIMER | \
			  IRQF_IRQPOLL | IRQF_PERCPU,
	.handler	= timer_interrupt,
	.dev_id		= &clockevent_bfin,
};

#if defined(CONFIG_TICKSOURCE_GPTMR0)
static int bfin_timer_set_next_event(unsigned long cycles,
                                     struct clock_event_device *evt)
{
	disable_gptimers(TIMER0bit);

	/* it starts counting three SCLK cycles after the TIMENx bit is set */
	set_gptimer_pwidth(TIMER0_id, cycles - 3);
	enable_gptimers(TIMER0bit);
	return 0;
}

static void bfin_timer_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC: {
		set_gptimer_config(TIMER0_id, \
			TIMER_OUT_DIS | TIMER_IRQ_ENA | \
			TIMER_PERIOD_CNT | TIMER_MODE_PWM);
		set_gptimer_period(TIMER0_id, get_sclk() / HZ);
		set_gptimer_pwidth(TIMER0_id, get_sclk() / HZ - 1);
		enable_gptimers(TIMER0bit);
		break;
	}
	case CLOCK_EVT_MODE_ONESHOT:
		disable_gptimers(TIMER0bit);
		set_gptimer_config(TIMER0_id, \
			TIMER_OUT_DIS | TIMER_IRQ_ENA | TIMER_MODE_PWM);
		set_gptimer_period(TIMER0_id, 0);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		disable_gptimers(TIMER0bit);
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static void bfin_timer_ack(void)
{
	set_gptimer_status(TIMER_GROUP1, TIMER_STATUS_TIMIL0);
}

static void __init bfin_timer_init(void)
{
	disable_gptimers(TIMER0bit);
}

static unsigned long  __init bfin_clockevent_check(void)
{
	setup_irq(IRQ_TIMER0, &bfin_timer_irq);
	return get_sclk();
}

#else /* CONFIG_TICKSOURCE_CORETMR */

static int bfin_timer_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TCOUNT(cycles);
	CSYNC();
	bfin_write_TCNTL(TMPWR | TMREN);
	return 0;
}

static void bfin_timer_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC: {
		unsigned long tcount = ((get_cclk() / (HZ * TIME_SCALE)) - 1);
		bfin_write_TCNTL(TMPWR);
		CSYNC();
		bfin_write_TSCALE(TIME_SCALE - 1);
		bfin_write_TPERIOD(tcount);
		bfin_write_TCOUNT(tcount);
		CSYNC();
		bfin_write_TCNTL(TMPWR | TMREN | TAUTORLD);
		break;
	}
	case CLOCK_EVT_MODE_ONESHOT:
		bfin_write_TCNTL(TMPWR);
		CSYNC();
		bfin_write_TSCALE(TIME_SCALE - 1);
		bfin_write_TPERIOD(0);
		bfin_write_TCOUNT(0);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		bfin_write_TCNTL(0);
		CSYNC();
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static void bfin_timer_ack(void)
{
}

static void __init bfin_timer_init(void)
{
	/* power up the timer, but don't enable it just yet */
	bfin_write_TCNTL(TMPWR);
	CSYNC();

	/*
	 * the TSCALE prescaler counter.
	 */
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(0);
	bfin_write_TCOUNT(0);

	CSYNC();
}

static unsigned long  __init bfin_clockevent_check(void)
{
	setup_irq(IRQ_CORETMR, &bfin_timer_irq);
	return get_cclk() / TIME_SCALE;
}

void __init setup_core_timer(void)
{
	bfin_timer_init();
	bfin_timer_set_mode(CLOCK_EVT_MODE_PERIODIC, NULL);
}
#endif /* CONFIG_TICKSOURCE_GPTMR0 */

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	smp_mb();
	evt->event_handler(evt);
	bfin_timer_ack();
	return IRQ_HANDLED;
}

static int __init bfin_clockevent_init(void)
{
	unsigned long timer_clk;

	timer_clk = bfin_clockevent_check();

	bfin_timer_init();

	clockevent_bfin.mult = div_sc(timer_clk, NSEC_PER_SEC, clockevent_bfin.shift);
	clockevent_bfin.max_delta_ns = clockevent_delta2ns(-1, &clockevent_bfin);
	clockevent_bfin.min_delta_ns = clockevent_delta2ns(100, &clockevent_bfin);
	clockevent_bfin.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_bfin);

	return 0;
}

void __init time_init(void)
{
	time_t secs_since_1970 = (365 * 37 + 9) * 24 * 60 * 60;	/* 1 Jan 2007 */

#ifdef CONFIG_RTC_DRV_BFIN
	/* [#2663] hack to filter junk RTC values that would cause
	 * userspace to have to deal with time values greater than
	 * 2^31 seconds (which uClibc cannot cope with yet)
	 */
	if ((bfin_read_RTC_STAT() & 0xC0000000) == 0xC0000000) {
		printk(KERN_NOTICE "bfin-rtc: invalid date; resetting\n");
		bfin_write_RTC_STAT(0);
	}
#endif

	/* Initialize xtime. From now on, xtime is updated with timer interrupts */
	xtime.tv_sec = secs_since_1970;
	xtime.tv_nsec = 0;
	set_normalized_timespec(&wall_to_monotonic, -xtime.tv_sec, -xtime.tv_nsec);

	bfin_cs_cycles_init();
	bfin_cs_gptimer0_init();
	bfin_clockevent_init();
}
