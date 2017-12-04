// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/cris/arch-v32/kernel/time.c
 *
 *  Copyright (C) 2003-2010 Axis Communications AB
 *
 */

#include <linux/timex.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/swap.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/cpufreq.h>
#include <linux/sched_clock.h>
#include <linux/mm.h>
#include <asm/types.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>

#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/timer_defs.h>
#include <hwregs/intr_vect_defs.h>
#ifdef CONFIG_CRIS_MACH_ARTPEC3
#include <hwregs/clkgen_defs.h>
#endif

/* Watchdog defines */
#define ETRAX_WD_KEY_MASK	0x7F /* key is 7 bit */
#define ETRAX_WD_HZ		763 /* watchdog counts at 763 Hz */
/* Number of 763 counts before watchdog bites */
#define ETRAX_WD_CNT		((2*ETRAX_WD_HZ)/HZ + 1)

#define CRISV32_TIMER_FREQ	(100000000lu)

unsigned long timer_regs[NR_CPUS] =
{
	regi_timer0,
};

extern int set_rtc_mmss(unsigned long nowtime);

#ifdef CONFIG_CPU_FREQ
static int cris_time_freq_notifier(struct notifier_block *nb,
				   unsigned long val, void *data);

static struct notifier_block cris_time_freq_notifier_block = {
	.notifier_call = cris_time_freq_notifier,
};
#endif

unsigned long get_ns_in_jiffie(void)
{
	reg_timer_r_tmr0_data data;
	unsigned long ns;

	data = REG_RD(timer, regi_timer0, r_tmr0_data);
	ns = (TIMER0_DIV - data) * 10;
	return ns;
}

/* From timer MDS describing the hardware watchdog:
 * 4.3.1 Watchdog Operation
 * The watchdog timer is an 8-bit timer with a configurable start value.
 * Once started the watchdog counts downwards with a frequency of 763 Hz
 * (100/131072 MHz). When the watchdog counts down to 1, it generates an
 * NMI (Non Maskable Interrupt), and when it counts down to 0, it resets the
 * chip.
 */
/* This gives us 1.3 ms to do something useful when the NMI comes */

/* Right now, starting the watchdog is the same as resetting it */
#define start_watchdog reset_watchdog

#if defined(CONFIG_ETRAX_WATCHDOG)
static short int watchdog_key = 42;  /* arbitrary 7 bit number */
#endif

/* Number of pages to consider "out of memory". It is normal that the memory
 * is used though, so set this really low. */
#define WATCHDOG_MIN_FREE_PAGES 8

#if defined(CONFIG_ETRAX_WATCHDOG_NICE_DOGGY)
/* for reliable NICE_DOGGY behaviour */
static int bite_in_progress;
#endif

void reset_watchdog(void)
{
#if defined(CONFIG_ETRAX_WATCHDOG)
	reg_timer_rw_wd_ctrl wd_ctrl = { 0 };

#if defined(CONFIG_ETRAX_WATCHDOG_NICE_DOGGY)
	if (unlikely(bite_in_progress))
		return;
#endif
	/* Only keep watchdog happy as long as we have memory left! */
	if(nr_free_pages() > WATCHDOG_MIN_FREE_PAGES) {
		/* Reset the watchdog with the inverse of the old key */
		/* Invert key, which is 7 bits */
		watchdog_key ^= ETRAX_WD_KEY_MASK;
		wd_ctrl.cnt = ETRAX_WD_CNT;
		wd_ctrl.cmd = regk_timer_start;
		wd_ctrl.key = watchdog_key;
		REG_WR(timer, regi_timer0, rw_wd_ctrl, wd_ctrl);
	}
#endif
}

/* stop the watchdog - we still need the correct key */

void stop_watchdog(void)
{
#if defined(CONFIG_ETRAX_WATCHDOG)
	reg_timer_rw_wd_ctrl wd_ctrl = { 0 };
	watchdog_key ^= ETRAX_WD_KEY_MASK; /* invert key, which is 7 bits */
	wd_ctrl.cnt = ETRAX_WD_CNT;
	wd_ctrl.cmd = regk_timer_stop;
	wd_ctrl.key = watchdog_key;
	REG_WR(timer, regi_timer0, rw_wd_ctrl, wd_ctrl);
#endif
}

extern void show_registers(struct pt_regs *regs);

void handle_watchdog_bite(struct pt_regs *regs)
{
#if defined(CONFIG_ETRAX_WATCHDOG)
	extern int cause_of_death;

	nmi_enter();
	oops_in_progress = 1;
#if defined(CONFIG_ETRAX_WATCHDOG_NICE_DOGGY)
	bite_in_progress = 1;
#endif
	printk(KERN_WARNING "Watchdog bite\n");

	/* Check if forced restart or unexpected watchdog */
	if (cause_of_death == 0xbedead) {
#ifdef CONFIG_CRIS_MACH_ARTPEC3
		/* There is a bug in Artpec-3 (voodoo TR 78) that requires
		 * us to go to lower frequency for the reset to be reliable
		 */
		reg_clkgen_rw_clk_ctrl ctrl =
			REG_RD(clkgen, regi_clkgen, rw_clk_ctrl);
		ctrl.pll = 0;
		REG_WR(clkgen, regi_clkgen, rw_clk_ctrl, ctrl);
#endif
		while(1);
	}

	/* Unexpected watchdog, stop the watchdog and dump registers. */
	stop_watchdog();
	printk(KERN_WARNING "Oops: bitten by watchdog\n");
	show_registers(regs);
	oops_in_progress = 0;
	printk("\n"); /* Flush mtdoops.  */
#ifndef CONFIG_ETRAX_WATCHDOG_NICE_DOGGY
	reset_watchdog();
#endif
	while(1) /* nothing */;
#endif
}

extern void cris_profile_sample(struct pt_regs *regs);
static void __iomem *timer_base;

static int crisv32_clkevt_switch_state(struct clock_event_device *dev)
{
	reg_timer_rw_tmr0_ctrl ctrl = {
		.op = regk_timer_hold,
		.freq = regk_timer_f100,
	};

	REG_WR(timer, timer_base, rw_tmr0_ctrl, ctrl);
	return 0;
}

static int crisv32_clkevt_next_event(unsigned long evt,
				     struct clock_event_device *dev)
{
	reg_timer_rw_tmr0_ctrl ctrl = {
		.op = regk_timer_ld,
		.freq = regk_timer_f100,
	};

	REG_WR(timer, timer_base, rw_tmr0_div, evt);
	REG_WR(timer, timer_base, rw_tmr0_ctrl, ctrl);

	ctrl.op = regk_timer_run;
	REG_WR(timer, timer_base, rw_tmr0_ctrl, ctrl);

	return 0;
}

static irqreturn_t crisv32_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	reg_timer_rw_tmr0_ctrl ctrl = {
		.op = regk_timer_hold,
		.freq = regk_timer_f100,
	};
	reg_timer_rw_ack_intr ack = { .tmr0 = 1 };
	reg_timer_r_masked_intr intr;

	intr = REG_RD(timer, timer_base, r_masked_intr);
	if (!intr.tmr0)
		return IRQ_NONE;

	REG_WR(timer, timer_base, rw_tmr0_ctrl, ctrl);
	REG_WR(timer, timer_base, rw_ack_intr, ack);

	reset_watchdog();
#ifdef CONFIG_SYSTEM_PROFILER
	cris_profile_sample(get_irq_regs());
#endif

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct clock_event_device crisv32_clockevent = {
	.name = "crisv32-timer",
	.rating = 300,
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_state_oneshot = crisv32_clkevt_switch_state,
	.set_state_shutdown = crisv32_clkevt_switch_state,
	.tick_resume = crisv32_clkevt_switch_state,
	.set_next_event = crisv32_clkevt_next_event,
};

/* Timer is IRQF_SHARED so drivers can add stuff to the timer irq chain. */
static struct irqaction irq_timer = {
	.handler = crisv32_timer_interrupt,
	.flags = IRQF_TIMER | IRQF_SHARED,
	.name = "crisv32-timer",
	.dev_id = &crisv32_clockevent,
};

static u64 notrace crisv32_timer_sched_clock(void)
{
	return REG_RD(timer, timer_base, r_time);
}

static void __init crisv32_timer_init(void)
{
	reg_timer_rw_intr_mask timer_intr_mask;
	reg_timer_rw_tmr0_ctrl ctrl = {
		.op = regk_timer_hold,
		.freq = regk_timer_f100,
	};

	REG_WR(timer, timer_base, rw_tmr0_ctrl, ctrl);

	timer_intr_mask = REG_RD(timer, timer_base, rw_intr_mask);
	timer_intr_mask.tmr0 = 1;
	REG_WR(timer, timer_base, rw_intr_mask, timer_intr_mask);
}

void __init time_init(void)
{
	int irq;
	int ret;

	/* Probe for the RTC and read it if it exists.
	 * Before the RTC can be probed the loops_per_usec variable needs
	 * to be initialized to make usleep work. A better value for
	 * loops_per_usec is calculated by the kernel later once the
	 * clock has started.
	 */
	loops_per_usec = 50;

	irq = TIMER0_INTR_VECT;
	timer_base = (void __iomem *) regi_timer0;

	crisv32_timer_init();

	sched_clock_register(crisv32_timer_sched_clock, 32,
			     CRISV32_TIMER_FREQ);

	clocksource_mmio_init(timer_base + REG_RD_ADDR_timer_r_time,
			      "crisv32-timer", CRISV32_TIMER_FREQ,
			      300, 32, clocksource_mmio_readl_up);

	crisv32_clockevent.cpumask = cpu_possible_mask;
	crisv32_clockevent.irq = irq;

	ret = setup_irq(irq, &irq_timer);
	if (ret)
		pr_warn("failed to setup irq %d\n", irq);

	clockevents_config_and_register(&crisv32_clockevent,
					CRISV32_TIMER_FREQ,
					2, 0xffffffff);

	/* Enable watchdog if we should use one. */

#if defined(CONFIG_ETRAX_WATCHDOG)
	printk(KERN_INFO "Enabling watchdog...\n");
	start_watchdog();

	/* If we use the hardware watchdog, we want to trap it as an NMI
	 * and dump registers before it resets us.  For this to happen, we
	 * must set the "m" NMI enable flag (which once set, is unset only
	 * when an NMI is taken). */
	{
		unsigned long flags;
		local_save_flags(flags);
		flags |= (1<<30); /* NMI M flag is at bit 30 */
		local_irq_restore(flags);
	}
#endif

#ifdef CONFIG_CPU_FREQ
	cpufreq_register_notifier(&cris_time_freq_notifier_block,
				  CPUFREQ_TRANSITION_NOTIFIER);
#endif
}

#ifdef CONFIG_CPU_FREQ
static int cris_time_freq_notifier(struct notifier_block *nb,
				   unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = data;
	if (val == CPUFREQ_POSTCHANGE) {
		reg_timer_r_tmr0_data data;
		reg_timer_rw_tmr0_div div = (freqs->new * 500) / HZ;
		do {
			data = REG_RD(timer, timer_regs[freqs->cpu],
				r_tmr0_data);
		} while (data > 20);
		REG_WR(timer, timer_regs[freqs->cpu], rw_tmr0_div, div);
	}
	return 0;
}
#endif
