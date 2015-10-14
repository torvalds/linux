/* time.c: UltraSparc timer and TOD clock support.
 *
 * Copyright (C) 1997, 2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998 Eddie C. Dost   (ecd@skynet.be)
 *
 * Based largely on code which is:
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>
#include <linux/profile.h>
#include <linux/bcd.h>
#include <linux/jiffies.h>
#include <linux/cpufreq.h>
#include <linux/percpu.h>
#include <linux/miscdevice.h>
#include <linux/rtc/m48t59.h>
#include <linux/kernel_stat.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/ftrace.h>

#include <asm/oplib.h>
#include <asm/timer.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/starfire.h>
#include <asm/smp.h>
#include <asm/sections.h>
#include <asm/cpudata.h>
#include <asm/uaccess.h>
#include <asm/irq_regs.h>

#include "entry.h"

DEFINE_SPINLOCK(rtc_lock);

#define TICK_PRIV_BIT	(1UL << 63)
#define TICKCMP_IRQ_BIT	(1UL << 63)

#ifdef CONFIG_SMP
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (in_lock_functions(pc))
		return regs->u_regs[UREG_RETPC];
	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

static void tick_disable_protection(void)
{
	/* Set things up so user can access tick register for profiling
	 * purposes.  Also workaround BB_ERRATA_1 by doing a dummy
	 * read back of %tick after writing it.
	 */
	__asm__ __volatile__(
	"	ba,pt	%%xcc, 1f\n"
	"	 nop\n"
	"	.align	64\n"
	"1:	rd	%%tick, %%g2\n"
	"	add	%%g2, 6, %%g2\n"
	"	andn	%%g2, %0, %%g2\n"
	"	wrpr	%%g2, 0, %%tick\n"
	"	rdpr	%%tick, %%g0"
	: /* no outputs */
	: "r" (TICK_PRIV_BIT)
	: "g2");
}

static void tick_disable_irq(void)
{
	__asm__ __volatile__(
	"	ba,pt	%%xcc, 1f\n"
	"	 nop\n"
	"	.align	64\n"
	"1:	wr	%0, 0x0, %%tick_cmpr\n"
	"	rd	%%tick_cmpr, %%g0"
	: /* no outputs */
	: "r" (TICKCMP_IRQ_BIT));
}

static void tick_init_tick(void)
{
	tick_disable_protection();
	tick_disable_irq();
}

static unsigned long long tick_get_tick(void)
{
	unsigned long ret;

	__asm__ __volatile__("rd	%%tick, %0\n\t"
			     "mov	%0, %0"
			     : "=r" (ret));

	return ret & ~TICK_PRIV_BIT;
}

static int tick_add_compare(unsigned long adj)
{
	unsigned long orig_tick, new_tick, new_compare;

	__asm__ __volatile__("rd	%%tick, %0"
			     : "=r" (orig_tick));

	orig_tick &= ~TICKCMP_IRQ_BIT;

	/* Workaround for Spitfire Errata (#54 I think??), I discovered
	 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
	 * number 103640.
	 *
	 * On Blackbird writes to %tick_cmpr can fail, the
	 * workaround seems to be to execute the wr instruction
	 * at the start of an I-cache line, and perform a dummy
	 * read back from %tick_cmpr right after writing to it. -DaveM
	 */
	__asm__ __volatile__("ba,pt	%%xcc, 1f\n\t"
			     " add	%1, %2, %0\n\t"
			     ".align	64\n"
			     "1:\n\t"
			     "wr	%0, 0, %%tick_cmpr\n\t"
			     "rd	%%tick_cmpr, %%g0\n\t"
			     : "=r" (new_compare)
			     : "r" (orig_tick), "r" (adj));

	__asm__ __volatile__("rd	%%tick, %0"
			     : "=r" (new_tick));
	new_tick &= ~TICKCMP_IRQ_BIT;

	return ((long)(new_tick - (orig_tick+adj))) > 0L;
}

static unsigned long tick_add_tick(unsigned long adj)
{
	unsigned long new_tick;

	/* Also need to handle Blackbird bug here too. */
	__asm__ __volatile__("rd	%%tick, %0\n\t"
			     "add	%0, %1, %0\n\t"
			     "wrpr	%0, 0, %%tick\n\t"
			     : "=&r" (new_tick)
			     : "r" (adj));

	return new_tick;
}

static struct sparc64_tick_ops tick_operations __read_mostly = {
	.name		=	"tick",
	.init_tick	=	tick_init_tick,
	.disable_irq	=	tick_disable_irq,
	.get_tick	=	tick_get_tick,
	.add_tick	=	tick_add_tick,
	.add_compare	=	tick_add_compare,
	.softint_mask	=	1UL << 0,
};

struct sparc64_tick_ops *tick_ops __read_mostly = &tick_operations;
EXPORT_SYMBOL(tick_ops);

static void stick_disable_irq(void)
{
	__asm__ __volatile__(
	"wr	%0, 0x0, %%asr25"
	: /* no outputs */
	: "r" (TICKCMP_IRQ_BIT));
}

static void stick_init_tick(void)
{
	/* Writes to the %tick and %stick register are not
	 * allowed on sun4v.  The Hypervisor controls that
	 * bit, per-strand.
	 */
	if (tlb_type != hypervisor) {
		tick_disable_protection();
		tick_disable_irq();

		/* Let the user get at STICK too. */
		__asm__ __volatile__(
		"	rd	%%asr24, %%g2\n"
		"	andn	%%g2, %0, %%g2\n"
		"	wr	%%g2, 0, %%asr24"
		: /* no outputs */
		: "r" (TICK_PRIV_BIT)
		: "g1", "g2");
	}

	stick_disable_irq();
}

static unsigned long long stick_get_tick(void)
{
	unsigned long ret;

	__asm__ __volatile__("rd	%%asr24, %0"
			     : "=r" (ret));

	return ret & ~TICK_PRIV_BIT;
}

static unsigned long stick_add_tick(unsigned long adj)
{
	unsigned long new_tick;

	__asm__ __volatile__("rd	%%asr24, %0\n\t"
			     "add	%0, %1, %0\n\t"
			     "wr	%0, 0, %%asr24\n\t"
			     : "=&r" (new_tick)
			     : "r" (adj));

	return new_tick;
}

static int stick_add_compare(unsigned long adj)
{
	unsigned long orig_tick, new_tick;

	__asm__ __volatile__("rd	%%asr24, %0"
			     : "=r" (orig_tick));
	orig_tick &= ~TICKCMP_IRQ_BIT;

	__asm__ __volatile__("wr	%0, 0, %%asr25"
			     : /* no outputs */
			     : "r" (orig_tick + adj));

	__asm__ __volatile__("rd	%%asr24, %0"
			     : "=r" (new_tick));
	new_tick &= ~TICKCMP_IRQ_BIT;

	return ((long)(new_tick - (orig_tick+adj))) > 0L;
}

static struct sparc64_tick_ops stick_operations __read_mostly = {
	.name		=	"stick",
	.init_tick	=	stick_init_tick,
	.disable_irq	=	stick_disable_irq,
	.get_tick	=	stick_get_tick,
	.add_tick	=	stick_add_tick,
	.add_compare	=	stick_add_compare,
	.softint_mask	=	1UL << 16,
};

/* On Hummingbird the STICK/STICK_CMPR register is implemented
 * in I/O space.  There are two 64-bit registers each, the
 * first holds the low 32-bits of the value and the second holds
 * the high 32-bits.
 *
 * Since STICK is constantly updating, we have to access it carefully.
 *
 * The sequence we use to read is:
 * 1) read high
 * 2) read low
 * 3) read high again, if it rolled re-read both low and high again.
 *
 * Writing STICK safely is also tricky:
 * 1) write low to zero
 * 2) write high
 * 3) write low
 */
#define HBIRD_STICKCMP_ADDR	0x1fe0000f060UL
#define HBIRD_STICK_ADDR	0x1fe0000f070UL

static unsigned long __hbird_read_stick(void)
{
	unsigned long ret, tmp1, tmp2, tmp3;
	unsigned long addr = HBIRD_STICK_ADDR+8;

	__asm__ __volatile__("ldxa	[%1] %5, %2\n"
			     "1:\n\t"
			     "sub	%1, 0x8, %1\n\t"
			     "ldxa	[%1] %5, %3\n\t"
			     "add	%1, 0x8, %1\n\t"
			     "ldxa	[%1] %5, %4\n\t"
			     "cmp	%4, %2\n\t"
			     "bne,a,pn	%%xcc, 1b\n\t"
			     " mov	%4, %2\n\t"
			     "sllx	%4, 32, %4\n\t"
			     "or	%3, %4, %0\n\t"
			     : "=&r" (ret), "=&r" (addr),
			       "=&r" (tmp1), "=&r" (tmp2), "=&r" (tmp3)
			     : "i" (ASI_PHYS_BYPASS_EC_E), "1" (addr));

	return ret;
}

static void __hbird_write_stick(unsigned long val)
{
	unsigned long low = (val & 0xffffffffUL);
	unsigned long high = (val >> 32UL);
	unsigned long addr = HBIRD_STICK_ADDR;

	__asm__ __volatile__("stxa	%%g0, [%0] %4\n\t"
			     "add	%0, 0x8, %0\n\t"
			     "stxa	%3, [%0] %4\n\t"
			     "sub	%0, 0x8, %0\n\t"
			     "stxa	%2, [%0] %4"
			     : "=&r" (addr)
			     : "0" (addr), "r" (low), "r" (high),
			       "i" (ASI_PHYS_BYPASS_EC_E));
}

static void __hbird_write_compare(unsigned long val)
{
	unsigned long low = (val & 0xffffffffUL);
	unsigned long high = (val >> 32UL);
	unsigned long addr = HBIRD_STICKCMP_ADDR + 0x8UL;

	__asm__ __volatile__("stxa	%3, [%0] %4\n\t"
			     "sub	%0, 0x8, %0\n\t"
			     "stxa	%2, [%0] %4"
			     : "=&r" (addr)
			     : "0" (addr), "r" (low), "r" (high),
			       "i" (ASI_PHYS_BYPASS_EC_E));
}

static void hbtick_disable_irq(void)
{
	__hbird_write_compare(TICKCMP_IRQ_BIT);
}

static void hbtick_init_tick(void)
{
	tick_disable_protection();

	/* XXX This seems to be necessary to 'jumpstart' Hummingbird
	 * XXX into actually sending STICK interrupts.  I think because
	 * XXX of how we store %tick_cmpr in head.S this somehow resets the
	 * XXX {TICK + STICK} interrupt mux.  -DaveM
	 */
	__hbird_write_stick(__hbird_read_stick());

	hbtick_disable_irq();
}

static unsigned long long hbtick_get_tick(void)
{
	return __hbird_read_stick() & ~TICK_PRIV_BIT;
}

static unsigned long hbtick_add_tick(unsigned long adj)
{
	unsigned long val;

	val = __hbird_read_stick() + adj;
	__hbird_write_stick(val);

	return val;
}

static int hbtick_add_compare(unsigned long adj)
{
	unsigned long val = __hbird_read_stick();
	unsigned long val2;

	val &= ~TICKCMP_IRQ_BIT;
	val += adj;
	__hbird_write_compare(val);

	val2 = __hbird_read_stick() & ~TICKCMP_IRQ_BIT;

	return ((long)(val2 - val)) > 0L;
}

static struct sparc64_tick_ops hbtick_operations __read_mostly = {
	.name		=	"hbtick",
	.init_tick	=	hbtick_init_tick,
	.disable_irq	=	hbtick_disable_irq,
	.get_tick	=	hbtick_get_tick,
	.add_tick	=	hbtick_add_tick,
	.add_compare	=	hbtick_add_compare,
	.softint_mask	=	1UL << 0,
};

static unsigned long timer_ticks_per_nsec_quotient __read_mostly;

unsigned long cmos_regs;
EXPORT_SYMBOL(cmos_regs);

static struct resource rtc_cmos_resource;

static struct platform_device rtc_cmos_device = {
	.name		= "rtc_cmos",
	.id		= -1,
	.resource	= &rtc_cmos_resource,
	.num_resources	= 1,
};

static int rtc_probe(struct platform_device *op)
{
	struct resource *r;

	printk(KERN_INFO "%s: RTC regs at 0x%llx\n",
	       op->dev.of_node->full_name, op->resource[0].start);

	/* The CMOS RTC driver only accepts IORESOURCE_IO, so cons
	 * up a fake resource so that the probe works for all cases.
	 * When the RTC is behind an ISA bus it will have IORESOURCE_IO
	 * already, whereas when it's behind EBUS is will be IORESOURCE_MEM.
	 */

	r = &rtc_cmos_resource;
	r->flags = IORESOURCE_IO;
	r->name = op->resource[0].name;
	r->start = op->resource[0].start;
	r->end = op->resource[0].end;

	cmos_regs = op->resource[0].start;
	return platform_device_register(&rtc_cmos_device);
}

static const struct of_device_id rtc_match[] = {
	{
		.name = "rtc",
		.compatible = "m5819",
	},
	{
		.name = "rtc",
		.compatible = "isa-m5819p",
	},
	{
		.name = "rtc",
		.compatible = "isa-m5823p",
	},
	{
		.name = "rtc",
		.compatible = "ds1287",
	},
	{},
};

static struct platform_driver rtc_driver = {
	.probe		= rtc_probe,
	.driver = {
		.name = "rtc",
		.of_match_table = rtc_match,
	},
};

static struct platform_device rtc_bq4802_device = {
	.name		= "rtc-bq4802",
	.id		= -1,
	.num_resources	= 1,
};

static int bq4802_probe(struct platform_device *op)
{

	printk(KERN_INFO "%s: BQ4802 regs at 0x%llx\n",
	       op->dev.of_node->full_name, op->resource[0].start);

	rtc_bq4802_device.resource = &op->resource[0];
	return platform_device_register(&rtc_bq4802_device);
}

static const struct of_device_id bq4802_match[] = {
	{
		.name = "rtc",
		.compatible = "bq4802",
	},
	{},
};

static struct platform_driver bq4802_driver = {
	.probe		= bq4802_probe,
	.driver = {
		.name = "bq4802",
		.of_match_table = bq4802_match,
	},
};

static unsigned char mostek_read_byte(struct device *dev, u32 ofs)
{
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *regs = (void __iomem *) pdev->resource[0].start;

	return readb(regs + ofs);
}

static void mostek_write_byte(struct device *dev, u32 ofs, u8 val)
{
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *regs = (void __iomem *) pdev->resource[0].start;

	writeb(val, regs + ofs);
}

static struct m48t59_plat_data m48t59_data = {
	.read_byte	= mostek_read_byte,
	.write_byte	= mostek_write_byte,
};

static struct platform_device m48t59_rtc = {
	.name		= "rtc-m48t59",
	.id		= 0,
	.num_resources	= 1,
	.dev	= {
		.platform_data = &m48t59_data,
	},
};

static int mostek_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;

	/* On an Enterprise system there can be multiple mostek clocks.
	 * We should only match the one that is on the central FHC bus.
	 */
	if (!strcmp(dp->parent->name, "fhc") &&
	    strcmp(dp->parent->parent->name, "central") != 0)
		return -ENODEV;

	printk(KERN_INFO "%s: Mostek regs at 0x%llx\n",
	       dp->full_name, op->resource[0].start);

	m48t59_rtc.resource = &op->resource[0];
	return platform_device_register(&m48t59_rtc);
}

static const struct of_device_id mostek_match[] = {
	{
		.name = "eeprom",
	},
	{},
};

static struct platform_driver mostek_driver = {
	.probe		= mostek_probe,
	.driver = {
		.name = "mostek",
		.of_match_table = mostek_match,
	},
};

static struct platform_device rtc_sun4v_device = {
	.name		= "rtc-sun4v",
	.id		= -1,
};

static struct platform_device rtc_starfire_device = {
	.name		= "rtc-starfire",
	.id		= -1,
};

static int __init clock_init(void)
{
	if (this_is_starfire)
		return platform_device_register(&rtc_starfire_device);

	if (tlb_type == hypervisor)
		return platform_device_register(&rtc_sun4v_device);

	(void) platform_driver_register(&rtc_driver);
	(void) platform_driver_register(&mostek_driver);
	(void) platform_driver_register(&bq4802_driver);

	return 0;
}

/* Must be after subsys_initcall() so that busses are probed.  Must
 * be before device_initcall() because things like the RTC driver
 * need to see the clock registers.
 */
fs_initcall(clock_init);

/* This is gets the master TICK_INT timer going. */
static unsigned long sparc64_init_timers(void)
{
	struct device_node *dp;
	unsigned long freq;

	dp = of_find_node_by_path("/");
	if (tlb_type == spitfire) {
		unsigned long ver, manuf, impl;

		__asm__ __volatile__ ("rdpr %%ver, %0"
				      : "=&r" (ver));
		manuf = ((ver >> 48) & 0xffff);
		impl = ((ver >> 32) & 0xffff);
		if (manuf == 0x17 && impl == 0x13) {
			/* Hummingbird, aka Ultra-IIe */
			tick_ops = &hbtick_operations;
			freq = of_getintprop_default(dp, "stick-frequency", 0);
		} else {
			tick_ops = &tick_operations;
			freq = local_cpu_data().clock_tick;
		}
	} else {
		tick_ops = &stick_operations;
		freq = of_getintprop_default(dp, "stick-frequency", 0);
	}

	return freq;
}

struct freq_table {
	unsigned long clock_tick_ref;
	unsigned int ref_freq;
};
static DEFINE_PER_CPU(struct freq_table, sparc64_freq_table) = { 0, 0 };

unsigned long sparc64_get_clock_tick(unsigned int cpu)
{
	struct freq_table *ft = &per_cpu(sparc64_freq_table, cpu);

	if (ft->clock_tick_ref)
		return ft->clock_tick_ref;
	return cpu_data(cpu).clock_tick;
}
EXPORT_SYMBOL(sparc64_get_clock_tick);

#ifdef CONFIG_CPU_FREQ

static int sparc64_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				    void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned int cpu = freq->cpu;
	struct freq_table *ft = &per_cpu(sparc64_freq_table, cpu);

	if (!ft->ref_freq) {
		ft->ref_freq = freq->old;
		ft->clock_tick_ref = cpu_data(cpu).clock_tick;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new)) {
		cpu_data(cpu).clock_tick =
			cpufreq_scale(ft->clock_tick_ref,
				      ft->ref_freq,
				      freq->new);
	}

	return 0;
}

static struct notifier_block sparc64_cpufreq_notifier_block = {
	.notifier_call	= sparc64_cpufreq_notifier
};

static int __init register_sparc64_cpufreq_notifier(void)
{

	cpufreq_register_notifier(&sparc64_cpufreq_notifier_block,
				  CPUFREQ_TRANSITION_NOTIFIER);
	return 0;
}

core_initcall(register_sparc64_cpufreq_notifier);

#endif /* CONFIG_CPU_FREQ */

static int sparc64_next_event(unsigned long delta,
			      struct clock_event_device *evt)
{
	return tick_ops->add_compare(delta) ? -ETIME : 0;
}

static int sparc64_timer_shutdown(struct clock_event_device *evt)
{
	tick_ops->disable_irq();
	return 0;
}

static struct clock_event_device sparc64_clockevent = {
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= sparc64_timer_shutdown,
	.set_next_event		= sparc64_next_event,
	.rating			= 100,
	.shift			= 30,
	.irq			= -1,
};
static DEFINE_PER_CPU(struct clock_event_device, sparc64_events);

void __irq_entry timer_interrupt(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned long tick_mask = tick_ops->softint_mask;
	int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(sparc64_events, cpu);

	clear_softint(tick_mask);

	irq_enter();

	local_cpu_data().irq0_irqs++;
	kstat_incr_irq_this_cpu(0);

	if (unlikely(!evt->event_handler)) {
		printk(KERN_WARNING
		       "Spurious SPARC64 timer interrupt on cpu %d\n", cpu);
	} else
		evt->event_handler(evt);

	irq_exit();

	set_irq_regs(old_regs);
}

void setup_sparc64_timer(void)
{
	struct clock_event_device *sevt;
	unsigned long pstate;

	/* Guarantee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	tick_ops->init_tick();

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));

	sevt = this_cpu_ptr(&sparc64_events);

	memcpy(sevt, &sparc64_clockevent, sizeof(*sevt));
	sevt->cpumask = cpumask_of(smp_processor_id());

	clockevents_register_device(sevt);
}

#define SPARC64_NSEC_PER_CYC_SHIFT	10UL

static struct clocksource clocksource_tick = {
	.rating		= 100,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static unsigned long tb_ticks_per_usec __read_mostly;

void __delay(unsigned long loops)
{
	unsigned long bclock, now;

	bclock = tick_ops->get_tick();
	do {
		now = tick_ops->get_tick();
	} while ((now-bclock) < loops);
}
EXPORT_SYMBOL(__delay);

void udelay(unsigned long usecs)
{
	__delay(tb_ticks_per_usec * usecs);
}
EXPORT_SYMBOL(udelay);

static cycle_t clocksource_tick_read(struct clocksource *cs)
{
	return tick_ops->get_tick();
}

void __init time_init(void)
{
	unsigned long freq = sparc64_init_timers();

	tb_ticks_per_usec = freq / USEC_PER_SEC;

	timer_ticks_per_nsec_quotient =
		clocksource_hz2mult(freq, SPARC64_NSEC_PER_CYC_SHIFT);

	clocksource_tick.name = tick_ops->name;
	clocksource_tick.read = clocksource_tick_read;

	clocksource_register_hz(&clocksource_tick, freq);
	printk("clocksource: mult[%x] shift[%d]\n",
	       clocksource_tick.mult, clocksource_tick.shift);

	sparc64_clockevent.name = tick_ops->name;
	clockevents_calc_mult_shift(&sparc64_clockevent, freq, 4);

	sparc64_clockevent.max_delta_ns =
		clockevent_delta2ns(0x7fffffffffffffffUL, &sparc64_clockevent);
	sparc64_clockevent.min_delta_ns =
		clockevent_delta2ns(0xF, &sparc64_clockevent);

	printk("clockevent: mult[%x] shift[%d]\n",
	       sparc64_clockevent.mult, sparc64_clockevent.shift);

	setup_sparc64_timer();
}

unsigned long long sched_clock(void)
{
	unsigned long ticks = tick_ops->get_tick();

	return (ticks * timer_ticks_per_nsec_quotient)
		>> SPARC64_NSEC_PER_CYC_SHIFT;
}

int read_current_timer(unsigned long *timer_val)
{
	*timer_val = tick_ops->get_tick();
	return 0;
}
