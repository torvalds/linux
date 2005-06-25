/*
 *  linux/arch/i386/kernel/time_hpet.c
 *  This code largely copied from arch/x86_64/kernel/time.c
 *  See that file for credits.
 *
 *  2003-06-30    Venkatesh Pallipadi - Additional changes for HPET support
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/timer.h>
#include <asm/fixmap.h>
#include <asm/apic.h>

#include <linux/timex.h>
#include <linux/config.h>

#include <asm/hpet.h>
#include <linux/hpet.h>

static unsigned long hpet_period;	/* fsecs / HPET clock */
unsigned long hpet_tick;		/* hpet clks count per tick */
unsigned long hpet_address;		/* hpet memory map physical address */
int hpet_use_timer;

static int use_hpet; 		/* can be used for runtime check of hpet */
static int boot_hpet_disable; 	/* boottime override for HPET timer */
static void __iomem * hpet_virt_address;	/* hpet kernel virtual address */

#define FSEC_TO_USEC (1000000000UL)

int hpet_readl(unsigned long a)
{
	return readl(hpet_virt_address + a);
}

static void hpet_writel(unsigned long d, unsigned long a)
{
	writel(d, hpet_virt_address + a);
}

#ifdef CONFIG_X86_LOCAL_APIC
/*
 * HPET counters dont wrap around on every tick. They just change the
 * comparator value and continue. Next tick can be caught by checking
 * for a change in the comparator value. Used in apic.c.
 */
static void __devinit wait_hpet_tick(void)
{
	unsigned int start_cmp_val, end_cmp_val;

	start_cmp_val = hpet_readl(HPET_T0_CMP);
	do {
		end_cmp_val = hpet_readl(HPET_T0_CMP);
	} while (start_cmp_val == end_cmp_val);
}
#endif

static int hpet_timer_stop_set_go(unsigned long tick)
{
	unsigned int cfg;

	/*
	 * Stop the timers and reset the main counter.
	 */
	cfg = hpet_readl(HPET_CFG);
	cfg &= ~HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);

	if (hpet_use_timer) {
		/*
		 * Set up timer 0, as periodic with first interrupt to happen at
		 * hpet_tick, and period also hpet_tick.
		 */
		cfg = hpet_readl(HPET_T0_CFG);
		cfg |= HPET_TN_ENABLE | HPET_TN_PERIODIC |
		       HPET_TN_SETVAL | HPET_TN_32BIT;
		hpet_writel(cfg, HPET_T0_CFG);

		/*
		 * The first write after writing TN_SETVAL to the config register sets
		 * the counter value, the second write sets the threshold.
		 */
		hpet_writel(tick, HPET_T0_CMP);
		hpet_writel(tick, HPET_T0_CMP);
	}
	/*
 	 * Go!
 	 */
	cfg = hpet_readl(HPET_CFG);
	if (hpet_use_timer)
		cfg |= HPET_CFG_LEGACY;
	cfg |= HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);

	return 0;
}

/*
 * Check whether HPET was found by ACPI boot parse. If yes setup HPET
 * counter 0 for kernel base timer.
 */
int __init hpet_enable(void)
{
	unsigned int id;
	unsigned long tick_fsec_low, tick_fsec_high; /* tick in femto sec */
	unsigned long hpet_tick_rem;

	if (boot_hpet_disable)
		return -1;

	if (!hpet_address) {
		return -1;
	}
	hpet_virt_address = ioremap_nocache(hpet_address, HPET_MMAP_SIZE);
	/*
	 * Read the period, compute tick and quotient.
	 */
	id = hpet_readl(HPET_ID);

	/*
	 * We are checking for value '1' or more in number field if
	 * CONFIG_HPET_EMULATE_RTC is set because we will need an
	 * additional timer for RTC emulation.
	 * However, we can do with one timer otherwise using the
	 * the single HPET timer for system time.
	 */
#ifdef CONFIG_HPET_EMULATE_RTC
	if (!(id & HPET_ID_NUMBER))
		return -1;
#endif


	hpet_period = hpet_readl(HPET_PERIOD);
	if ((hpet_period < HPET_MIN_PERIOD) || (hpet_period > HPET_MAX_PERIOD))
		return -1;

	/*
	 * 64 bit math
	 * First changing tick into fsec
	 * Then 64 bit div to find number of hpet clk per tick
	 */
	ASM_MUL64_REG(tick_fsec_low, tick_fsec_high,
			KERNEL_TICK_USEC, FSEC_TO_USEC);
	ASM_DIV64_REG(hpet_tick, hpet_tick_rem,
			hpet_period, tick_fsec_low, tick_fsec_high);

	if (hpet_tick_rem > (hpet_period >> 1))
		hpet_tick++; /* rounding the result */

	hpet_use_timer = id & HPET_ID_LEGSUP;

	if (hpet_timer_stop_set_go(hpet_tick))
		return -1;

	use_hpet = 1;

#ifdef	CONFIG_HPET
	{
		struct hpet_data	hd;
		unsigned int 		ntimer;

		memset(&hd, 0, sizeof (hd));

		ntimer = hpet_readl(HPET_ID);
		ntimer = (ntimer & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT;
		ntimer++;

		/*
		 * Register with driver.
		 * Timer0 and Timer1 is used by platform.
		 */
		hd.hd_phys_address = hpet_address;
		hd.hd_address = hpet_virt_address;
		hd.hd_nirqs = ntimer;
		hd.hd_flags = HPET_DATA_PLATFORM;
		hpet_reserve_timer(&hd, 0);
#ifdef	CONFIG_HPET_EMULATE_RTC
		hpet_reserve_timer(&hd, 1);
#endif
		hd.hd_irq[0] = HPET_LEGACY_8254;
		hd.hd_irq[1] = HPET_LEGACY_RTC;
		if (ntimer > 2) {
			struct hpet __iomem	*hpet;
			struct hpet_timer __iomem *timer;
			int			i;

			hpet = hpet_virt_address;

			for (i = 2, timer = &hpet->hpet_timers[2]; i < ntimer;
				timer++, i++)
				hd.hd_irq[i] = (timer->hpet_config &
					Tn_INT_ROUTE_CNF_MASK) >>
					Tn_INT_ROUTE_CNF_SHIFT;

		}

		hpet_alloc(&hd);
	}
#endif

#ifdef CONFIG_X86_LOCAL_APIC
	if (hpet_use_timer)
		wait_timer_tick = wait_hpet_tick;
#endif
	return 0;
}

int hpet_reenable(void)
{
	return hpet_timer_stop_set_go(hpet_tick);
}

int is_hpet_enabled(void)
{
	return use_hpet;
}

int is_hpet_capable(void)
{
	if (!boot_hpet_disable && hpet_address)
		return 1;
	return 0;
}

static int __init hpet_setup(char* str)
{
	if (str) {
		if (!strncmp("disable", str, 7))
			boot_hpet_disable = 1;
	}
	return 1;
}

__setup("hpet=", hpet_setup);

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

extern irqreturn_t rtc_interrupt(int irq, void *dev_id, struct pt_regs *regs);

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
	local_irq_restore(flags);

	cfg = hpet_readl(HPET_T1_CFG);
	cfg |= HPET_TN_ENABLE | HPET_TN_SETVAL | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	return 1;
}

static void hpet_rtc_timer_reinit(void)
{
	unsigned int cfg, cnt;

	if (!(PIE_on | AIE_on | UIE_on))
		return;

	if (PIE_on && (PIE_freq > DEFAULT_RTC_INT_FREQ))
		hpet_rtc_int_freq = PIE_freq;
	else
		hpet_rtc_int_freq = DEFAULT_RTC_INT_FREQ;

	/* It is more accurate to use the comparator value than current count.*/
	cnt = hpet_readl(HPET_T1_CMP);
	cnt += hpet_tick*HZ/hpet_rtc_int_freq;
	hpet_writel(cnt, HPET_T1_CMP);

	cfg = hpet_readl(HPET_T1_CFG);
	cfg |= HPET_TN_ENABLE | HPET_TN_SETVAL | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	return;
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
		rtc_interrupt(rtc_int_flag, dev_id, regs);
	}
	return IRQ_HANDLED;
}
#endif

