#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hpet.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/pm.h>

#include <asm/fixmap.h>
#include <asm/hpet.h>
#include <asm/i8253.h>
#include <asm/io.h>

#define HPET_MASK	CLOCKSOURCE_MASK(32)
#define HPET_SHIFT	22

/* FSEC = 10^-15
   NSEC = 10^-9 */
#define FSEC_PER_NSEC	1000000L

/*
 * HPET address is set in acpi/boot.c, when an ACPI entry exists
 */
unsigned long hpet_address;
static void __iomem *hpet_virt_address;

unsigned long hpet_readl(unsigned long a)
{
	return readl(hpet_virt_address + a);
}

static inline void hpet_writel(unsigned long d, unsigned long a)
{
	writel(d, hpet_virt_address + a);
}

#ifdef CONFIG_X86_64
#include <asm/pgtable.h>
#endif

static inline void hpet_set_mapping(void)
{
	hpet_virt_address = ioremap_nocache(hpet_address, HPET_MMAP_SIZE);
#ifdef CONFIG_X86_64
	__set_fixmap(VSYSCALL_HPET, hpet_address, PAGE_KERNEL_VSYSCALL_NOCACHE);
#endif
}

static inline void hpet_clear_mapping(void)
{
	iounmap(hpet_virt_address);
	hpet_virt_address = NULL;
}

/*
 * HPET command line enable / disable
 */
static int boot_hpet_disable;
int hpet_force_user;

static int __init hpet_setup(char* str)
{
	if (str) {
		if (!strncmp("disable", str, 7))
			boot_hpet_disable = 1;
		if (!strncmp("force", str, 5))
			hpet_force_user = 1;
	}
	return 1;
}
__setup("hpet=", hpet_setup);

static int __init disable_hpet(char *str)
{
	boot_hpet_disable = 1;
	return 1;
}
__setup("nohpet", disable_hpet);

static inline int is_hpet_capable(void)
{
	return (!boot_hpet_disable && hpet_address);
}

/*
 * HPET timer interrupt enable / disable
 */
static int hpet_legacy_int_enabled;

/**
 * is_hpet_enabled - check whether the hpet timer interrupt is enabled
 */
int is_hpet_enabled(void)
{
	return is_hpet_capable() && hpet_legacy_int_enabled;
}
EXPORT_SYMBOL_GPL(is_hpet_enabled);

/*
 * When the hpet driver (/dev/hpet) is enabled, we need to reserve
 * timer 0 and timer 1 in case of RTC emulation.
 */
#ifdef CONFIG_HPET
static void hpet_reserve_platform_timers(unsigned long id)
{
	struct hpet __iomem *hpet = hpet_virt_address;
	struct hpet_timer __iomem *timer = &hpet->hpet_timers[2];
	unsigned int nrtimers, i;
	struct hpet_data hd;

	nrtimers = ((id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT) + 1;

	memset(&hd, 0, sizeof (hd));
	hd.hd_phys_address = hpet_address;
	hd.hd_address = hpet;
	hd.hd_nirqs = nrtimers;
	hd.hd_flags = HPET_DATA_PLATFORM;
	hpet_reserve_timer(&hd, 0);

#ifdef CONFIG_HPET_EMULATE_RTC
	hpet_reserve_timer(&hd, 1);
#endif

	hd.hd_irq[0] = HPET_LEGACY_8254;
	hd.hd_irq[1] = HPET_LEGACY_RTC;

	for (i = 2; i < nrtimers; timer++, i++) {
		hd.hd_irq[i] = (readl(&timer->hpet_config) & Tn_INT_ROUTE_CNF_MASK) >>
			Tn_INT_ROUTE_CNF_SHIFT;
	}

	hpet_alloc(&hd);

}
#else
static void hpet_reserve_platform_timers(unsigned long id) { }
#endif

/*
 * Common hpet info
 */
static unsigned long hpet_period;

static void hpet_legacy_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt);
static int hpet_legacy_next_event(unsigned long delta,
			   struct clock_event_device *evt);

/*
 * The hpet clock event device
 */
static struct clock_event_device hpet_clockevent = {
	.name		= "hpet",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= hpet_legacy_set_mode,
	.set_next_event = hpet_legacy_next_event,
	.shift		= 32,
	.irq		= 0,
	.rating		= 50,
};

static void hpet_start_counter(void)
{
	unsigned long cfg = hpet_readl(HPET_CFG);

	cfg &= ~HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);
	cfg |= HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
}

static void hpet_resume_device(void)
{
	force_hpet_resume();
}

static void hpet_restart_counter(void)
{
	hpet_resume_device();
	hpet_start_counter();
}

static void hpet_enable_legacy_int(void)
{
	unsigned long cfg = hpet_readl(HPET_CFG);

	cfg |= HPET_CFG_LEGACY;
	hpet_writel(cfg, HPET_CFG);
	hpet_legacy_int_enabled = 1;
}

static void hpet_legacy_clockevent_register(void)
{
	/* Start HPET legacy interrupts */
	hpet_enable_legacy_int();

	/*
	 * The mult factor is defined as (include/linux/clockchips.h)
	 *  mult/2^shift = cyc/ns (in contrast to ns/cyc in clocksource.h)
	 * hpet_period is in units of femtoseconds (per cycle), so
	 *  mult/2^shift = cyc/ns = 10^6/hpet_period
	 *  mult = (10^6 * 2^shift)/hpet_period
	 *  mult = (FSEC_PER_NSEC << hpet_clockevent.shift)/hpet_period
	 */
	hpet_clockevent.mult = div_sc((unsigned long) FSEC_PER_NSEC,
				      hpet_period, hpet_clockevent.shift);
	/* Calculate the min / max delta */
	hpet_clockevent.max_delta_ns = clockevent_delta2ns(0x7FFFFFFF,
							   &hpet_clockevent);
	/* 5 usec minimum reprogramming delta. */
	hpet_clockevent.min_delta_ns = 5000;

	/*
	 * Start hpet with the boot cpu mask and make it
	 * global after the IO_APIC has been initialized.
	 */
	hpet_clockevent.cpumask = cpumask_of_cpu(smp_processor_id());
	clockevents_register_device(&hpet_clockevent);
	global_clock_event = &hpet_clockevent;
	printk(KERN_DEBUG "hpet clockevent registered\n");
}

static void hpet_legacy_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt)
{
	unsigned long cfg, cmp, now;
	uint64_t delta;

	switch(mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		delta = ((uint64_t)(NSEC_PER_SEC/HZ)) * hpet_clockevent.mult;
		delta >>= hpet_clockevent.shift;
		now = hpet_readl(HPET_COUNTER);
		cmp = now + (unsigned long) delta;
		cfg = hpet_readl(HPET_T0_CFG);
		cfg |= HPET_TN_ENABLE | HPET_TN_PERIODIC |
		       HPET_TN_SETVAL | HPET_TN_32BIT;
		hpet_writel(cfg, HPET_T0_CFG);
		/*
		 * The first write after writing TN_SETVAL to the
		 * config register sets the counter value, the second
		 * write sets the period.
		 */
		hpet_writel(cmp, HPET_T0_CMP);
		udelay(1);
		hpet_writel((unsigned long) delta, HPET_T0_CMP);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		cfg = hpet_readl(HPET_T0_CFG);
		cfg &= ~HPET_TN_PERIODIC;
		cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
		hpet_writel(cfg, HPET_T0_CFG);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		cfg = hpet_readl(HPET_T0_CFG);
		cfg &= ~HPET_TN_ENABLE;
		hpet_writel(cfg, HPET_T0_CFG);
		break;

	case CLOCK_EVT_MODE_RESUME:
		hpet_enable_legacy_int();
		break;
	}
}

static int hpet_legacy_next_event(unsigned long delta,
				  struct clock_event_device *evt)
{
	u32 cnt;

	cnt = hpet_readl(HPET_COUNTER);
	cnt += (u32) delta;
	hpet_writel(cnt, HPET_T0_CMP);

	/*
	 * We need to read back the CMP register to make sure that
	 * what we wrote hit the chip before we compare it to the
	 * counter.
	 */
	WARN_ON((u32)hpet_readl(HPET_T0_CMP) != cnt);

	return (s32)((u32)hpet_readl(HPET_COUNTER) - cnt) >= 0 ? -ETIME : 0;
}

/*
 * Clock source related code
 */
static cycle_t read_hpet(void)
{
	return (cycle_t)hpet_readl(HPET_COUNTER);
}

#ifdef CONFIG_X86_64
static cycle_t __vsyscall_fn vread_hpet(void)
{
	return readl((const void __iomem *)fix_to_virt(VSYSCALL_HPET) + 0xf0);
}
#endif

static struct clocksource clocksource_hpet = {
	.name		= "hpet",
	.rating		= 250,
	.read		= read_hpet,
	.mask		= HPET_MASK,
	.shift		= HPET_SHIFT,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume		= hpet_restart_counter,
#ifdef CONFIG_X86_64
	.vread		= vread_hpet,
#endif
};

static int hpet_clocksource_register(void)
{
	u64 start, now;
	cycle_t t1;

	/* Start the counter */
	hpet_start_counter();

	/* Verify whether hpet counter works */
	t1 = read_hpet();
	rdtscll(start);

	/*
	 * We don't know the TSC frequency yet, but waiting for
	 * 200000 TSC cycles is safe:
	 * 4 GHz == 50us
	 * 1 GHz == 200us
	 */
	do {
		rep_nop();
		rdtscll(now);
	} while ((now - start) < 200000UL);

	if (t1 == read_hpet()) {
		printk(KERN_WARNING
		       "HPET counter not counting. HPET disabled\n");
		return -ENODEV;
	}

	/*
	 * The definition of mult is (include/linux/clocksource.h)
	 * mult/2^shift = ns/cyc and hpet_period is in units of fsec/cyc
	 * so we first need to convert hpet_period to ns/cyc units:
	 *  mult/2^shift = ns/cyc = hpet_period/10^6
	 *  mult = (hpet_period * 2^shift)/10^6
	 *  mult = (hpet_period << shift)/FSEC_PER_NSEC
	 */
	clocksource_hpet.mult = div_sc(hpet_period, FSEC_PER_NSEC, HPET_SHIFT);

	clocksource_register(&clocksource_hpet);

	return 0;
}

/**
 * hpet_enable - Try to setup the HPET timer. Returns 1 on success.
 */
int __init hpet_enable(void)
{
	unsigned long id;
	int i;

	if (!is_hpet_capable())
		return 0;

	hpet_set_mapping();

	/*
	 * Read the period and check for a sane value:
	 */
	hpet_period = hpet_readl(HPET_PERIOD);

	/*
	 * AMD SB700 based systems with spread spectrum enabled use a
	 * SMM based HPET emulation to provide proper frequency
	 * setting. The SMM code is initialized with the first HPET
	 * register access and takes some time to complete. During
	 * this time the config register reads 0xffffffff. We check
	 * for max. 1000 loops whether the config register reads a non
	 * 0xffffffff value to make sure that HPET is up and running
	 * before we go further. A counting loop is safe, as the HPET
	 * access takes thousands of CPU cycles. On non SB700 based
	 * machines this check is only done once and has no side
	 * effects.
	 */
	for (i = 0; hpet_readl(HPET_CFG) == 0xFFFFFFFF; i++) {
		if (i == 1000) {
			printk(KERN_WARNING
			       "HPET config register value = 0xFFFFFFFF. "
			       "Disabling HPET\n");
			goto out_nohpet;
		}
	}

	if (hpet_period < HPET_MIN_PERIOD || hpet_period > HPET_MAX_PERIOD)
		goto out_nohpet;

	/*
	 * Read the HPET ID register to retrieve the IRQ routing
	 * information and the number of channels
	 */
	id = hpet_readl(HPET_ID);

#ifdef CONFIG_HPET_EMULATE_RTC
	/*
	 * The legacy routing mode needs at least two channels, tick timer
	 * and the rtc emulation channel.
	 */
	if (!(id & HPET_ID_NUMBER))
		goto out_nohpet;
#endif

	if (hpet_clocksource_register())
		goto out_nohpet;

	if (id & HPET_ID_LEGSUP) {
		hpet_legacy_clockevent_register();
		return 1;
	}
	return 0;

out_nohpet:
	hpet_clear_mapping();
	boot_hpet_disable = 1;
	return 0;
}

/*
 * Needs to be late, as the reserve_timer code calls kalloc !
 *
 * Not a problem on i386 as hpet_enable is called from late_time_init,
 * but on x86_64 it is necessary !
 */
static __init int hpet_late_init(void)
{
	if (boot_hpet_disable)
		return -ENODEV;

	if (!hpet_address) {
		if (!force_hpet_address)
			return -ENODEV;

		hpet_address = force_hpet_address;
		hpet_enable();
		if (!hpet_virt_address)
			return -ENODEV;
	}

	hpet_reserve_platform_timers(hpet_readl(HPET_ID));

	return 0;
}
fs_initcall(hpet_late_init);

void hpet_disable(void)
{
	if (is_hpet_capable()) {
		unsigned long cfg = hpet_readl(HPET_CFG);

		if (hpet_legacy_int_enabled) {
			cfg &= ~HPET_CFG_LEGACY;
			hpet_legacy_int_enabled = 0;
		}
		cfg &= ~HPET_CFG_ENABLE;
		hpet_writel(cfg, HPET_CFG);
	}
}

#ifdef CONFIG_HPET_EMULATE_RTC

/* HPET in LegacyReplacement Mode eats up RTC interrupt line. When, HPET
 * is enabled, we support RTC interrupt functionality in software.
 * RTC has 3 kinds of interrupts:
 * 1) Update Interrupt - generate an interrupt, every sec, when RTC clock
 *    is updated
 * 2) Alarm Interrupt - generate an interrupt at a specific time of day
 * 3) Periodic Interrupt - generate periodic interrupt, with frequencies
 *    2Hz-8192Hz (2Hz-64Hz for non-root user) (all freqs in powers of 2)
 * (1) and (2) above are implemented using polling at a frequency of
 * 64 Hz. The exact frequency is a tradeoff between accuracy and interrupt
 * overhead. (DEFAULT_RTC_INT_FREQ)
 * For (3), we use interrupts at 64Hz or user specified periodic
 * frequency, whichever is higher.
 */
#include <linux/mc146818rtc.h>
#include <linux/rtc.h>
#include <asm/rtc.h>

#define DEFAULT_RTC_INT_FREQ	64
#define DEFAULT_RTC_SHIFT	6
#define RTC_NUM_INTS		1

static unsigned long hpet_rtc_flags;
static int hpet_prev_update_sec;
static struct rtc_time hpet_alarm_time;
static unsigned long hpet_pie_count;
static unsigned long hpet_t1_cmp;
static unsigned long hpet_default_delta;
static unsigned long hpet_pie_delta;
static unsigned long hpet_pie_limit;

static rtc_irq_handler irq_handler;

/*
 * Registers a IRQ handler.
 */
int hpet_register_irq_handler(rtc_irq_handler handler)
{
	if (!is_hpet_enabled())
		return -ENODEV;
	if (irq_handler)
		return -EBUSY;

	irq_handler = handler;

	return 0;
}
EXPORT_SYMBOL_GPL(hpet_register_irq_handler);

/*
 * Deregisters the IRQ handler registered with hpet_register_irq_handler()
 * and does cleanup.
 */
void hpet_unregister_irq_handler(rtc_irq_handler handler)
{
	if (!is_hpet_enabled())
		return;

	irq_handler = NULL;
	hpet_rtc_flags = 0;
}
EXPORT_SYMBOL_GPL(hpet_unregister_irq_handler);

/*
 * Timer 1 for RTC emulation. We use one shot mode, as periodic mode
 * is not supported by all HPET implementations for timer 1.
 *
 * hpet_rtc_timer_init() is called when the rtc is initialized.
 */
int hpet_rtc_timer_init(void)
{
	unsigned long cfg, cnt, delta, flags;

	if (!is_hpet_enabled())
		return 0;

	if (!hpet_default_delta) {
		uint64_t clc;

		clc = (uint64_t) hpet_clockevent.mult * NSEC_PER_SEC;
		clc >>= hpet_clockevent.shift + DEFAULT_RTC_SHIFT;
		hpet_default_delta = (unsigned long) clc;
	}

	if (!(hpet_rtc_flags & RTC_PIE) || hpet_pie_limit)
		delta = hpet_default_delta;
	else
		delta = hpet_pie_delta;

	local_irq_save(flags);

	cnt = delta + hpet_readl(HPET_COUNTER);
	hpet_writel(cnt, HPET_T1_CMP);
	hpet_t1_cmp = cnt;

	cfg = hpet_readl(HPET_T1_CFG);
	cfg &= ~HPET_TN_PERIODIC;
	cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	local_irq_restore(flags);

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_rtc_timer_init);

/*
 * The functions below are called from rtc driver.
 * Return 0 if HPET is not being used.
 * Otherwise do the necessary changes and return 1.
 */
int hpet_mask_rtc_irq_bit(unsigned long bit_mask)
{
	if (!is_hpet_enabled())
		return 0;

	hpet_rtc_flags &= ~bit_mask;
	return 1;
}
EXPORT_SYMBOL_GPL(hpet_mask_rtc_irq_bit);

int hpet_set_rtc_irq_bit(unsigned long bit_mask)
{
	unsigned long oldbits = hpet_rtc_flags;

	if (!is_hpet_enabled())
		return 0;

	hpet_rtc_flags |= bit_mask;

	if ((bit_mask & RTC_UIE) && !(oldbits & RTC_UIE))
		hpet_prev_update_sec = -1;

	if (!oldbits)
		hpet_rtc_timer_init();

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_rtc_irq_bit);

int hpet_set_alarm_time(unsigned char hrs, unsigned char min,
			unsigned char sec)
{
	if (!is_hpet_enabled())
		return 0;

	hpet_alarm_time.tm_hour = hrs;
	hpet_alarm_time.tm_min = min;
	hpet_alarm_time.tm_sec = sec;

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_alarm_time);

int hpet_set_periodic_freq(unsigned long freq)
{
	uint64_t clc;

	if (!is_hpet_enabled())
		return 0;

	if (freq <= DEFAULT_RTC_INT_FREQ)
		hpet_pie_limit = DEFAULT_RTC_INT_FREQ / freq;
	else {
		clc = (uint64_t) hpet_clockevent.mult * NSEC_PER_SEC;
		do_div(clc, freq);
		clc >>= hpet_clockevent.shift;
		hpet_pie_delta = (unsigned long) clc;
	}
	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_periodic_freq);

int hpet_rtc_dropped_irq(void)
{
	return is_hpet_enabled();
}
EXPORT_SYMBOL_GPL(hpet_rtc_dropped_irq);

static void hpet_rtc_timer_reinit(void)
{
	unsigned long cfg, delta;
	int lost_ints = -1;

	if (unlikely(!hpet_rtc_flags)) {
		cfg = hpet_readl(HPET_T1_CFG);
		cfg &= ~HPET_TN_ENABLE;
		hpet_writel(cfg, HPET_T1_CFG);
		return;
	}

	if (!(hpet_rtc_flags & RTC_PIE) || hpet_pie_limit)
		delta = hpet_default_delta;
	else
		delta = hpet_pie_delta;

	/*
	 * Increment the comparator value until we are ahead of the
	 * current count.
	 */
	do {
		hpet_t1_cmp += delta;
		hpet_writel(hpet_t1_cmp, HPET_T1_CMP);
		lost_ints++;
	} while ((long)(hpet_readl(HPET_COUNTER) - hpet_t1_cmp) > 0);

	if (lost_ints) {
		if (hpet_rtc_flags & RTC_PIE)
			hpet_pie_count += lost_ints;
		if (printk_ratelimit())
			printk(KERN_WARNING "hpet1: lost %d rtc interrupts\n",
				lost_ints);
	}
}

irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id)
{
	struct rtc_time curr_time;
	unsigned long rtc_int_flag = 0;

	hpet_rtc_timer_reinit();
	memset(&curr_time, 0, sizeof(struct rtc_time));

	if (hpet_rtc_flags & (RTC_UIE | RTC_AIE))
		get_rtc_time(&curr_time);

	if (hpet_rtc_flags & RTC_UIE &&
	    curr_time.tm_sec != hpet_prev_update_sec) {
		if (hpet_prev_update_sec >= 0)
			rtc_int_flag = RTC_UF;
		hpet_prev_update_sec = curr_time.tm_sec;
	}

	if (hpet_rtc_flags & RTC_PIE &&
	    ++hpet_pie_count >= hpet_pie_limit) {
		rtc_int_flag |= RTC_PF;
		hpet_pie_count = 0;
	}

	if (hpet_rtc_flags & RTC_AIE &&
	    (curr_time.tm_sec == hpet_alarm_time.tm_sec) &&
	    (curr_time.tm_min == hpet_alarm_time.tm_min) &&
	    (curr_time.tm_hour == hpet_alarm_time.tm_hour))
			rtc_int_flag |= RTC_AF;

	if (rtc_int_flag) {
		rtc_int_flag |= (RTC_IRQF | (RTC_NUM_INTS << 8));
		if (irq_handler)
			irq_handler(rtc_int_flag, dev_id);
	}
	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(hpet_rtc_interrupt);
#endif
