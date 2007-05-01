#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/hpet.h>
#include <asm/pgtable.h>
#include <asm/vsyscall.h>
#include <asm/timex.h>
#include <asm/hpet.h>

#define HPET_MASK	0xFFFFFFFF
#define HPET_SHIFT	22

/* FSEC = 10^-15 NSEC = 10^-9 */
#define FSEC_PER_NSEC	1000000

int nohpet __initdata;

unsigned long hpet_address;
unsigned long hpet_period;	/* fsecs / HPET clock */
unsigned long hpet_tick;	/* HPET clocks / interrupt */

int hpet_use_timer;		/* Use counter of hpet for time keeping,
				 * otherwise PIT
				 */

#ifdef	CONFIG_HPET
static __init int late_hpet_init(void)
{
	struct hpet_data	hd;
	unsigned int 		ntimer;

	if (!hpet_address)
        	return 0;

	memset(&hd, 0, sizeof(hd));

	ntimer = hpet_readl(HPET_ID);
	ntimer = (ntimer & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT;
	ntimer++;

	/*
	 * Register with driver.
	 * Timer0 and Timer1 is used by platform.
	 */
	hd.hd_phys_address = hpet_address;
	hd.hd_address = (void __iomem *)fix_to_virt(FIX_HPET_BASE);
	hd.hd_nirqs = ntimer;
	hd.hd_flags = HPET_DATA_PLATFORM;
	hpet_reserve_timer(&hd, 0);
#ifdef	CONFIG_HPET_EMULATE_RTC
	hpet_reserve_timer(&hd, 1);
#endif
	hd.hd_irq[0] = HPET_LEGACY_8254;
	hd.hd_irq[1] = HPET_LEGACY_RTC;
	if (ntimer > 2) {
		struct hpet		*hpet;
		struct hpet_timer	*timer;
		int			i;

		hpet = (struct hpet *) fix_to_virt(FIX_HPET_BASE);
		timer = &hpet->hpet_timers[2];
		for (i = 2; i < ntimer; timer++, i++)
			hd.hd_irq[i] = (timer->hpet_config &
					Tn_INT_ROUTE_CNF_MASK) >>
				Tn_INT_ROUTE_CNF_SHIFT;

	}

	hpet_alloc(&hd);
	return 0;
}
fs_initcall(late_hpet_init);
#endif

int hpet_timer_stop_set_go(unsigned long tick)
{
	unsigned int cfg;

/*
 * Stop the timers and reset the main counter.
 */

	cfg = hpet_readl(HPET_CFG);
	cfg &= ~(HPET_CFG_ENABLE | HPET_CFG_LEGACY);
	hpet_writel(cfg, HPET_CFG);
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);

/*
 * Set up timer 0, as periodic with first interrupt to happen at hpet_tick,
 * and period also hpet_tick.
 */
	if (hpet_use_timer) {
		hpet_writel(HPET_TN_ENABLE | HPET_TN_PERIODIC | HPET_TN_SETVAL |
		    HPET_TN_32BIT, HPET_T0_CFG);
		hpet_writel(hpet_tick, HPET_T0_CMP); /* next interrupt */
		hpet_writel(hpet_tick, HPET_T0_CMP); /* period */
		cfg |= HPET_CFG_LEGACY;
	}
/*
 * Go!
 */

	cfg |= HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);

	return 0;
}

static cycle_t read_hpet(void)
{
	return (cycle_t)hpet_readl(HPET_COUNTER);
}

static cycle_t __vsyscall_fn vread_hpet(void)
{
	return readl((void __iomem *)fix_to_virt(VSYSCALL_HPET) + 0xf0);
}

struct clocksource clocksource_hpet = {
	.name		= "hpet",
	.rating		= 250,
	.read		= read_hpet,
	.mask		= (cycle_t)HPET_MASK,
	.mult		= 0, /* set below */
	.shift		= HPET_SHIFT,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.vread		= vread_hpet,
};

int hpet_arch_init(void)
{
	unsigned int id;
	u64 tmp;

	if (!hpet_address)
		return -1;
	set_fixmap_nocache(FIX_HPET_BASE, hpet_address);
	__set_fixmap(VSYSCALL_HPET, hpet_address, PAGE_KERNEL_VSYSCALL_NOCACHE);

/*
 * Read the period, compute tick and quotient.
 */

	id = hpet_readl(HPET_ID);

	if (!(id & HPET_ID_VENDOR) || !(id & HPET_ID_NUMBER))
		return -1;

	hpet_period = hpet_readl(HPET_PERIOD);
	if (hpet_period < 100000 || hpet_period > 100000000)
		return -1;

	hpet_tick = (FSEC_PER_TICK + hpet_period / 2) / hpet_period;

	hpet_use_timer = (id & HPET_ID_LEGSUP);

	/*
	 * hpet period is in femto seconds per cycle
	 * so we need to convert this to ns/cyc units
	 * aproximated by mult/2^shift
	 *
	 *  fsec/cyc * 1nsec/1000000fsec = nsec/cyc = mult/2^shift
	 *  fsec/cyc * 1ns/1000000fsec * 2^shift = mult
	 *  fsec/cyc * 2^shift * 1nsec/1000000fsec = mult
	 *  (fsec/cyc << shift)/1000000 = mult
	 *  (hpet_period << shift)/FSEC_PER_NSEC = mult
	 */
	tmp = (u64)hpet_period << HPET_SHIFT;
	do_div(tmp, FSEC_PER_NSEC);
	clocksource_hpet.mult = (u32)tmp;
	clocksource_register(&clocksource_hpet);

	return hpet_timer_stop_set_go(hpet_tick);
}

int hpet_reenable(void)
{
	return hpet_timer_stop_set_go(hpet_tick);
}

/*
 * calibrate_tsc() calibrates the processor TSC in a very simple way, comparing
 * it to the HPET timer of known frequency.
 */

#define TICK_COUNT 100000000
#define TICK_MIN   5000
#define MAX_TRIES  5

/*
 * Some platforms take periodic SMI interrupts with 5ms duration. Make sure none
 * occurs between the reads of the hpet & TSC.
 */
static void __init read_hpet_tsc(int *hpet, int *tsc)
{
	int tsc1, tsc2, hpet1, i;

	for (i = 0; i < MAX_TRIES; i++) {
		tsc1 = get_cycles_sync();
		hpet1 = hpet_readl(HPET_COUNTER);
		tsc2 = get_cycles_sync();
		if (tsc2 - tsc1 > TICK_MIN)
			break;
	}
	*hpet = hpet1;
	*tsc = tsc2;
}

unsigned int __init hpet_calibrate_tsc(void)
{
	int tsc_start, hpet_start;
	int tsc_now, hpet_now;
	unsigned long flags;

	local_irq_save(flags);

	read_hpet_tsc(&hpet_start, &tsc_start);

	do {
		local_irq_disable();
		read_hpet_tsc(&hpet_now, &tsc_now);
		local_irq_restore(flags);
	} while ((tsc_now - tsc_start) < TICK_COUNT &&
		(hpet_now - hpet_start) < TICK_COUNT);

	return (tsc_now - tsc_start) * 1000000000L
		/ ((hpet_now - hpet_start) * hpet_period / 1000);
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
#include <linux/rtc.h>

#define DEFAULT_RTC_INT_FREQ 	64
#define RTC_NUM_INTS 		1

static unsigned long UIE_on;
static unsigned long prev_update_sec;

static unsigned long AIE_on;
static struct rtc_time alarm_time;

static unsigned long PIE_on;
static unsigned long PIE_freq = DEFAULT_RTC_INT_FREQ;
static unsigned long PIE_count;

static unsigned long hpet_rtc_int_freq; /* RTC interrupt frequency */
static unsigned int hpet_t1_cmp; /* cached comparator register */

int is_hpet_enabled(void)
{
	return hpet_address != 0;
}

/*
 * Timer 1 for RTC, we do not use periodic interrupt feature,
 * even if HPET supports periodic interrupts on Timer 1.
 * The reason being, to set up a periodic interrupt in HPET, we need to
 * stop the main counter. And if we do that everytime someone diables/enables
 * RTC, we will have adverse effect on main kernel timer running on Timer 0.
 * So, for the time being, simulate the periodic interrupt in software.
 *
 * hpet_rtc_timer_init() is called for the first time and during subsequent
 * interuppts reinit happens through hpet_rtc_timer_reinit().
 */
int hpet_rtc_timer_init(void)
{
	unsigned int cfg, cnt;
	unsigned long flags;

	if (!is_hpet_enabled())
		return 0;
	/*
	 * Set the counter 1 and enable the interrupts.
	 */
	if (PIE_on && (PIE_freq > DEFAULT_RTC_INT_FREQ))
		hpet_rtc_int_freq = PIE_freq;
	else
		hpet_rtc_int_freq = DEFAULT_RTC_INT_FREQ;

	local_irq_save(flags);

	cnt = hpet_readl(HPET_COUNTER);
	cnt += ((hpet_tick*HZ)/hpet_rtc_int_freq);
	hpet_writel(cnt, HPET_T1_CMP);
	hpet_t1_cmp = cnt;

	cfg = hpet_readl(HPET_T1_CFG);
	cfg &= ~HPET_TN_PERIODIC;
	cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	local_irq_restore(flags);

	return 1;
}

static void hpet_rtc_timer_reinit(void)
{
	unsigned int cfg, cnt, ticks_per_int, lost_ints;

	if (unlikely(!(PIE_on | AIE_on | UIE_on))) {
		cfg = hpet_readl(HPET_T1_CFG);
		cfg &= ~HPET_TN_ENABLE;
		hpet_writel(cfg, HPET_T1_CFG);
		return;
	}

	if (PIE_on && (PIE_freq > DEFAULT_RTC_INT_FREQ))
		hpet_rtc_int_freq = PIE_freq;
	else
		hpet_rtc_int_freq = DEFAULT_RTC_INT_FREQ;

	/* It is more accurate to use the comparator value than current count.*/
	ticks_per_int = hpet_tick * HZ / hpet_rtc_int_freq;
	hpet_t1_cmp += ticks_per_int;
	hpet_writel(hpet_t1_cmp, HPET_T1_CMP);

	/*
	 * If the interrupt handler was delayed too long, the write above tries
	 * to schedule the next interrupt in the past and the hardware would
	 * not interrupt until the counter had wrapped around.
	 * So we have to check that the comparator wasn't set to a past time.
	 */
	cnt = hpet_readl(HPET_COUNTER);
	if (unlikely((int)(cnt - hpet_t1_cmp) > 0)) {
		lost_ints = (cnt - hpet_t1_cmp) / ticks_per_int + 1;
		/* Make sure that, even with the time needed to execute
		 * this code, the next scheduled interrupt has been moved
		 * back to the future: */
		lost_ints++;

		hpet_t1_cmp += lost_ints * ticks_per_int;
		hpet_writel(hpet_t1_cmp, HPET_T1_CMP);

		if (PIE_on)
			PIE_count += lost_ints;

		if (printk_ratelimit())
			printk(KERN_WARNING "rtc: lost some interrupts at %ldHz.\n",
			       hpet_rtc_int_freq);
	}
}

/*
 * The functions below are called from rtc driver.
 * Return 0 if HPET is not being used.
 * Otherwise do the necessary changes and return 1.
 */
int hpet_mask_rtc_irq_bit(unsigned long bit_mask)
{
	if (!is_hpet_enabled())
		return 0;

	if (bit_mask & RTC_UIE)
		UIE_on = 0;
	if (bit_mask & RTC_PIE)
		PIE_on = 0;
	if (bit_mask & RTC_AIE)
		AIE_on = 0;

	return 1;
}

int hpet_set_rtc_irq_bit(unsigned long bit_mask)
{
	int timer_init_reqd = 0;

	if (!is_hpet_enabled())
		return 0;

	if (!(PIE_on | AIE_on | UIE_on))
		timer_init_reqd = 1;

	if (bit_mask & RTC_UIE) {
		UIE_on = 1;
	}
	if (bit_mask & RTC_PIE) {
		PIE_on = 1;
		PIE_count = 0;
	}
	if (bit_mask & RTC_AIE) {
		AIE_on = 1;
	}

	if (timer_init_reqd)
		hpet_rtc_timer_init();

	return 1;
}

int hpet_set_alarm_time(unsigned char hrs, unsigned char min, unsigned char sec)
{
	if (!is_hpet_enabled())
		return 0;

	alarm_time.tm_hour = hrs;
	alarm_time.tm_min = min;
	alarm_time.tm_sec = sec;

	return 1;
}

int hpet_set_periodic_freq(unsigned long freq)
{
	if (!is_hpet_enabled())
		return 0;

	PIE_freq = freq;
	PIE_count = 0;

	return 1;
}

int hpet_rtc_dropped_irq(void)
{
	if (!is_hpet_enabled())
		return 0;

	return 1;
}

irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct rtc_time curr_time;
	unsigned long rtc_int_flag = 0;
	int call_rtc_interrupt = 0;

	hpet_rtc_timer_reinit();

	if (UIE_on | AIE_on) {
		rtc_get_rtc_time(&curr_time);
	}
	if (UIE_on) {
		if (curr_time.tm_sec != prev_update_sec) {
			/* Set update int info, call real rtc int routine */
			call_rtc_interrupt = 1;
			rtc_int_flag = RTC_UF;
			prev_update_sec = curr_time.tm_sec;
		}
	}
	if (PIE_on) {
		PIE_count++;
		if (PIE_count >= hpet_rtc_int_freq/PIE_freq) {
			/* Set periodic int info, call real rtc int routine */
			call_rtc_interrupt = 1;
			rtc_int_flag |= RTC_PF;
			PIE_count = 0;
		}
	}
	if (AIE_on) {
		if ((curr_time.tm_sec == alarm_time.tm_sec) &&
		    (curr_time.tm_min == alarm_time.tm_min) &&
		    (curr_time.tm_hour == alarm_time.tm_hour)) {
			/* Set alarm int info, call real rtc int routine */
			call_rtc_interrupt = 1;
			rtc_int_flag |= RTC_AF;
		}
	}
	if (call_rtc_interrupt) {
		rtc_int_flag |= (RTC_IRQF | (RTC_NUM_INTS << 8));
		rtc_interrupt(rtc_int_flag, dev_id);
	}
	return IRQ_HANDLED;
}
#endif

static int __init nohpet_setup(char *s)
{
	nohpet = 1;
	return 1;
}

__setup("nohpet", nohpet_setup);
