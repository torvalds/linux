/*
 * Blackfin nmi_watchdog Driver
 *
 * Originally based on bfin_wdt.c
 * Copyright 2010-2010 Analog Devices Inc.
 *		Graff Yang <graf.yang@analog.com>
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/bitops.h>
#include <linux/hardirq.h>
#include <linux/syscore_ops.h>
#include <linux/pm.h>
#include <linux/nmi.h>
#include <linux/smp.h>
#include <linux/timer.h>
#include <asm/blackfin.h>
#include <linux/atomic.h>
#include <asm/cacheflush.h>
#include <asm/bfin_watchdog.h>

#define DRV_NAME "nmi-wdt"

#define NMI_WDT_TIMEOUT 5          /* 5 seconds */
#define NMI_CHECK_TIMEOUT (4 * HZ) /* 4 seconds in jiffies */
static int nmi_wdt_cpu = 1;

static unsigned int timeout = NMI_WDT_TIMEOUT;
static int nmi_active;

static unsigned short wdoga_ctl;
static unsigned int wdoga_cnt;
static struct corelock_slot saved_corelock;
static atomic_t nmi_touched[NR_CPUS];
static struct timer_list ntimer;

enum {
	COREA_ENTER_NMI = 0,
	COREA_EXIT_NMI,
	COREB_EXIT_NMI,

	NMI_EVENT_NR,
};
static unsigned long nmi_event __attribute__ ((__section__(".l2.bss")));

/* we are in nmi, non-atomic bit ops is safe */
static inline void set_nmi_event(int event)
{
	__set_bit(event, &nmi_event);
}

static inline void wait_nmi_event(int event)
{
	while (!test_bit(event, &nmi_event))
		barrier();
	__clear_bit(event, &nmi_event);
}

static inline void send_corea_nmi(void)
{
	wdoga_ctl = bfin_read_WDOGA_CTL();
	wdoga_cnt = bfin_read_WDOGA_CNT();

	bfin_write_WDOGA_CTL(WDEN_DISABLE);
	bfin_write_WDOGA_CNT(0);
	bfin_write_WDOGA_CTL(WDEN_ENABLE | ICTL_NMI);
}

static inline void restore_corea_nmi(void)
{
	bfin_write_WDOGA_CTL(WDEN_DISABLE);
	bfin_write_WDOGA_CTL(WDOG_EXPIRED | WDEN_DISABLE | ICTL_NONE);

	bfin_write_WDOGA_CNT(wdoga_cnt);
	bfin_write_WDOGA_CTL(wdoga_ctl);
}

static inline void save_corelock(void)
{
	saved_corelock = corelock;
	corelock.lock = 0;
}

static inline void restore_corelock(void)
{
	corelock = saved_corelock;
}


static inline void nmi_wdt_keepalive(void)
{
	bfin_write_WDOGB_STAT(0);
}

static inline void nmi_wdt_stop(void)
{
	bfin_write_WDOGB_CTL(WDEN_DISABLE);
}

/* before calling this function, you must stop the WDT */
static inline void nmi_wdt_clear(void)
{
	/* clear TRO bit, disable event generation */
	bfin_write_WDOGB_CTL(WDOG_EXPIRED | WDEN_DISABLE | ICTL_NONE);
}

static inline void nmi_wdt_start(void)
{
	bfin_write_WDOGB_CTL(WDEN_ENABLE | ICTL_NMI);
}

static inline int nmi_wdt_running(void)
{
	return ((bfin_read_WDOGB_CTL() & WDEN_MASK) != WDEN_DISABLE);
}

static inline int nmi_wdt_set_timeout(unsigned long t)
{
	u32 cnt, max_t, sclk;
	int run;

	sclk = get_sclk();
	max_t = -1 / sclk;
	cnt = t * sclk;
	if (t > max_t) {
		pr_warning("NMI: timeout value is too large\n");
		return -EINVAL;
	}

	run = nmi_wdt_running();
	nmi_wdt_stop();
	bfin_write_WDOGB_CNT(cnt);
	if (run)
		nmi_wdt_start();

	timeout = t;

	return 0;
}

int check_nmi_wdt_touched(void)
{
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;
	cpumask_t mask;

	cpumask_copy(&mask, cpu_online_mask);
	if (!atomic_read(&nmi_touched[this_cpu]))
		return 0;

	atomic_set(&nmi_touched[this_cpu], 0);

	cpumask_clear_cpu(this_cpu, &mask);
	for_each_cpu(cpu, &mask) {
		invalidate_dcache_range((unsigned long)(&nmi_touched[cpu]),
				(unsigned long)(&nmi_touched[cpu]));
		if (!atomic_read(&nmi_touched[cpu]))
			return 0;
		atomic_set(&nmi_touched[cpu], 0);
	}

	return 1;
}

static void nmi_wdt_timer(unsigned long data)
{
	if (check_nmi_wdt_touched())
		nmi_wdt_keepalive();

	mod_timer(&ntimer, jiffies + NMI_CHECK_TIMEOUT);
}

static int __init init_nmi_wdt(void)
{
	nmi_wdt_set_timeout(timeout);
	nmi_wdt_start();
	nmi_active = true;

	init_timer(&ntimer);
	ntimer.function = nmi_wdt_timer;
	ntimer.expires = jiffies + NMI_CHECK_TIMEOUT;
	add_timer(&ntimer);

	pr_info("nmi_wdt: initialized: timeout=%d sec\n", timeout);
	return 0;
}
device_initcall(init_nmi_wdt);

void touch_nmi_watchdog(void)
{
	atomic_set(&nmi_touched[smp_processor_id()], 1);
}

/* Suspend/resume support */
#ifdef CONFIG_PM
static int nmi_wdt_suspend(void)
{
	nmi_wdt_stop();
	return 0;
}

static void nmi_wdt_resume(void)
{
	if (nmi_active)
		nmi_wdt_start();
}

static struct syscore_ops nmi_syscore_ops = {
	.resume		= nmi_wdt_resume,
	.suspend	= nmi_wdt_suspend,
};

static int __init init_nmi_wdt_syscore(void)
{
	if (nmi_active)
		register_syscore_ops(&nmi_syscore_ops);

	return 0;
}
late_initcall(init_nmi_wdt_syscore);

#endif	/* CONFIG_PM */


asmlinkage notrace void do_nmi(struct pt_regs *fp)
{
	unsigned int cpu = smp_processor_id();
	nmi_enter();

	cpu_pda[cpu].__nmi_count += 1;

	if (cpu == nmi_wdt_cpu) {
		/* CoreB goes here first */

		/* reload the WDOG_STAT */
		nmi_wdt_keepalive();

		/* clear nmi interrupt for CoreB */
		nmi_wdt_stop();
		nmi_wdt_clear();

		/* trigger NMI interrupt of CoreA */
		send_corea_nmi();

		/* waiting CoreB to enter NMI */
		wait_nmi_event(COREA_ENTER_NMI);

		/* recover WDOGA's settings */
		restore_corea_nmi();

		save_corelock();

		/* corelock is save/cleared, CoreA is dummping messages */

		wait_nmi_event(COREA_EXIT_NMI);
	} else {
		/* OK, CoreA entered NMI */
		set_nmi_event(COREA_ENTER_NMI);
	}

	pr_emerg("\nNMI Watchdog detected LOCKUP, dump for CPU %d\n", cpu);
	dump_bfin_process(fp);
	dump_bfin_mem(fp);
	show_regs(fp);
	dump_bfin_trace_buffer();
	show_stack(current, (unsigned long *)fp);

	if (cpu == nmi_wdt_cpu) {
		pr_emerg("This fault is not recoverable, sorry!\n");

		/* CoreA dump finished, restore the corelock */
		restore_corelock();

		set_nmi_event(COREB_EXIT_NMI);
	} else {
		/* CoreB dump finished, notice the CoreA we are done */
		set_nmi_event(COREA_EXIT_NMI);

		/* synchronize with CoreA */
		wait_nmi_event(COREB_EXIT_NMI);
	}

	nmi_exit();
}
