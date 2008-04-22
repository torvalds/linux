/*
 * Copytight (C) 1999, 2000, 05, 06 Ralf Baechle (ralf@linux-mips.org)
 * Copytight (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/bcd.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/mm.h>

#include <asm/time.h>
#include <asm/pgtable.h>
#include <asm/sgialib.h>
#include <asm/sn/ioc3.h>
#include <asm/m48t35.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/arch.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/sn0/hub.h>

#define TICK_SIZE (tick_nsec / 1000)

#if 0
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	struct m48t35_rtc *rtc;
	nasid_t nid;

	nid = get_nasid();
	rtc = (struct m48t35_rtc *)(KL_CONFIG_CH_CONS_INFO(nid)->memory_base +
							IOC3_BYTEBUS_DEV0);

	rtc->control |= M48T35_RTC_READ;
	cmos_minutes = BCD2BIN(rtc->min);
	rtc->control &= ~M48T35_RTC_READ;

	/*
	 * Since we're only adjusting minutes and seconds, don't interfere with
	 * hour overflow. This avoids messing with unknown time zones but
	 * requires your RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		real_seconds = BIN2BCD(real_seconds);
		real_minutes = BIN2BCD(real_minutes);
		rtc->control |= M48T35_RTC_SET;
		rtc->sec = real_seconds;
		rtc->min = real_minutes;
		rtc->control &= ~M48T35_RTC_SET;
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	return retval;
}
#endif

/* Includes for ioc3_init().  */
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/pci/bridge.h>

unsigned long read_persistent_clock(void)
{
        unsigned int year, month, date, hour, min, sec;
	struct m48t35_rtc *rtc;
	nasid_t nid;

	nid = get_nasid();
	rtc = (struct m48t35_rtc *)(KL_CONFIG_CH_CONS_INFO(nid)->memory_base +
							IOC3_BYTEBUS_DEV0);

	rtc->control |= M48T35_RTC_READ;
	sec = rtc->sec;
	min = rtc->min;
	hour = rtc->hour;
	date = rtc->date;
	month = rtc->month;
	year = rtc->year;
	rtc->control &= ~M48T35_RTC_READ;

        sec = BCD2BIN(sec);
        min = BCD2BIN(min);
        hour = BCD2BIN(hour);
        date = BCD2BIN(date);
        month = BCD2BIN(month);
        year = BCD2BIN(year);

        year += 1970;

        return mktime(year, month, date, hour, min, sec);
}

static void enable_rt_irq(unsigned int irq)
{
}

static void disable_rt_irq(unsigned int irq)
{
}

static struct irq_chip rt_irq_type = {
	.name		= "SN HUB RT timer",
	.ack		= disable_rt_irq,
	.mask		= disable_rt_irq,
	.mask_ack	= disable_rt_irq,
	.unmask		= enable_rt_irq,
	.eoi		= enable_rt_irq,
};

static int rt_next_event(unsigned long delta, struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	int slice = cputoslice(cpu);
	unsigned long cnt;

	cnt = LOCAL_HUB_L(PI_RT_COUNT);
	cnt += delta;
	LOCAL_HUB_S(PI_RT_COMPARE_A + PI_COUNT_OFFSET * slice, cnt);

	return LOCAL_HUB_L(PI_RT_COUNT) >= cnt ? -ETIME : 0;
}

static void rt_set_mode(enum clock_event_mode mode,
		struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
		/* The only mode supported */
		break;

	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		/* Nothing to do  */
		break;
	}
}

unsigned int rt_timer_irq;

static irqreturn_t hub_rt_counter_handler(int irq, void *dev_id)
{
	struct clock_event_device *cd = dev_id;
	unsigned int cpu = smp_processor_id();
	int slice = cputoslice(cpu);

	/*
	 * Ack
	 */
	LOCAL_HUB_S(PI_RT_PEND_A + PI_COUNT_OFFSET * slice, 0);
	cd->event_handler(cd);

	return IRQ_HANDLED;
}

struct irqaction hub_rt_irqaction = {
	.handler	= hub_rt_counter_handler,
	.flags		= IRQF_DISABLED | IRQF_PERCPU,
	.name		= "hub-rt",
};

/*
 * This is a hack; we really need to figure these values out dynamically
 *
 * Since 800 ns works very well with various HUB frequencies, such as
 * 360, 380, 390 and 400 MHZ, we use 800 ns rtc cycle time.
 *
 * Ralf: which clock rate is used to feed the counter?
 */
#define NSEC_PER_CYCLE		800
#define CYCLES_PER_SEC		(NSEC_PER_SEC / NSEC_PER_CYCLE)

static DEFINE_PER_CPU(struct clock_event_device, hub_rt_clockevent);
static DEFINE_PER_CPU(char [11], hub_rt_name);

static void __cpuinit hub_rt_clock_event_init(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd = &per_cpu(hub_rt_clockevent, cpu);
	unsigned char *name = per_cpu(hub_rt_name, cpu);
	int irq = rt_timer_irq;

	sprintf(name, "hub-rt %d", cpu);
	cd->name		= "HUB-RT",
	cd->features		= CLOCK_EVT_FEAT_ONESHOT,
	clockevent_set_clock(cd, CYCLES_PER_SEC);
	cd->max_delta_ns        = clockevent_delta2ns(0xfffffffffffff, cd);
	cd->min_delta_ns        = clockevent_delta2ns(0x300, cd);
	cd->rating		= 200,
	cd->irq			= irq,
	cd->cpumask		= cpumask_of_cpu(cpu),
	cd->rating		= 300,
	cd->set_next_event	= rt_next_event,
	cd->set_mode		= rt_set_mode,
	clockevents_register_device(cd);
}

static void __init hub_rt_clock_event_global_init(void)
{
	unsigned int irq;

	do {
		smp_wmb();
		irq = rt_timer_irq;
		if (irq)
			break;

		irq = allocate_irqno();
		if (irq < 0)
			panic("Allocation of irq number for timer failed");
	} while (xchg(&rt_timer_irq, irq));

	set_irq_chip_and_handler(irq, &rt_irq_type, handle_percpu_irq);
	setup_irq(irq, &hub_rt_irqaction);
}

static cycle_t hub_rt_read(void)
{
	return REMOTE_HUB_L(cputonasid(0), PI_RT_COUNT);
}

struct clocksource hub_rt_clocksource = {
	.name	= "HUB-RT",
	.rating	= 200,
	.read	= hub_rt_read,
	.mask	= CLOCKSOURCE_MASK(52),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init hub_rt_clocksource_init(void)
{
	struct clocksource *cs = &hub_rt_clocksource;

	clocksource_set_clock(cs, CYCLES_PER_SEC);
	clocksource_register(cs);
}

void __init plat_time_init(void)
{
	hub_rt_clocksource_init();
	hub_rt_clock_event_global_init();
}

void __cpuinit cpu_time_init(void)
{
	lboard_t *board;
	klcpu_t *cpu;
	int cpuid;

	/* Don't use ARCS.  ARCS is fragile.  Klconfig is simple and sane.  */
	board = find_lboard(KL_CONFIG_INFO(get_nasid()), KLTYPE_IP27);
	if (!board)
		panic("Can't find board info for myself.");

	cpuid = LOCAL_HUB_L(PI_CPU_NUM) ? IP27_CPU0_INDEX : IP27_CPU1_INDEX;
	cpu = (klcpu_t *) KLCF_COMP(board, cpuid);
	if (!cpu)
		panic("No information about myself?");

	printk("CPU %d clock is %dMHz.\n", smp_processor_id(), cpu->cpu_speed);

	hub_rt_clock_event_init();
	set_c0_status(SRB_TIMOCLK);
}

void __cpuinit hub_rtc_init(cnodeid_t cnode)
{
	/*
	 * We only need to initialize the current node.
	 * If this is not the current node then it is a cpuless
	 * node and timeouts will not happen there.
	 */
	if (get_compact_nodeid() == cnode) {
		LOCAL_HUB_S(PI_RT_EN_A, 1);
		LOCAL_HUB_S(PI_RT_EN_B, 1);
		LOCAL_HUB_S(PI_PROF_EN_A, 0);
		LOCAL_HUB_S(PI_PROF_EN_B, 0);
		LOCAL_HUB_S(PI_RT_COUNT, 0);
		LOCAL_HUB_S(PI_RT_PEND_A, 0);
		LOCAL_HUB_S(PI_RT_PEND_B, 0);
	}
}
