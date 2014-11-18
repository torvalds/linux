/*
 * apb_timer.c: Driver for Langwell APB timers
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Note:
 * Langwell is the south complex of Intel Moorestown MID platform. There are
 * eight external timers in total that can be used by the operating system.
 * The timer information, such as frequency and addresses, is provided to the
 * OS via SFI tables.
 * Timer interrupts are routed via FW/HW emulated IOAPIC independently via
 * individual redirection table entries (RTE).
 * Unlike HPET, there is no master counter, therefore one of the timers are
 * used as clocksource. The overall allocation looks like:
 *  - timer 0 - NR_CPUs for per cpu timer
 *  - one timer for clocksource
 *  - one timer for watchdog driver.
 * It is also worth notice that APB timer does not support true one-shot mode,
 * free-running mode will be used here to emulate one-shot mode.
 * APB timer can also be used as broadcast timer along with per cpu local APIC
 * timer, but by default APB timer has higher rating than local APIC timers.
 */

#include <linux/delay.h>
#include <linux/dw_apb_timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/sfi.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/irq.h>

#include <asm/fixmap.h>
#include <asm/apb_timer.h>
#include <asm/intel-mid.h>
#include <asm/time.h>

#define APBT_CLOCKEVENT_RATING		110
#define APBT_CLOCKSOURCE_RATING		250

#define APBT_CLOCKEVENT0_NUM   (0)
#define APBT_CLOCKSOURCE_NUM   (2)

static phys_addr_t apbt_address;
static int apb_timer_block_enabled;
static void __iomem *apbt_virt_address;

/*
 * Common DW APB timer info
 */
static unsigned long apbt_freq;

struct apbt_dev {
	struct dw_apb_clock_event_device	*timer;
	unsigned int				num;
	int					cpu;
	unsigned int				irq;
	char					name[10];
};

static struct dw_apb_clocksource *clocksource_apbt;

static inline void __iomem *adev_virt_addr(struct apbt_dev *adev)
{
	return apbt_virt_address + adev->num * APBTMRS_REG_SIZE;
}

static DEFINE_PER_CPU(struct apbt_dev, cpu_apbt_dev);

#ifdef CONFIG_SMP
static unsigned int apbt_num_timers_used;
#endif

static inline void apbt_set_mapping(void)
{
	struct sfi_timer_table_entry *mtmr;
	int phy_cs_timer_id = 0;

	if (apbt_virt_address) {
		pr_debug("APBT base already mapped\n");
		return;
	}
	mtmr = sfi_get_mtmr(APBT_CLOCKEVENT0_NUM);
	if (mtmr == NULL) {
		printk(KERN_ERR "Failed to get MTMR %d from SFI\n",
		       APBT_CLOCKEVENT0_NUM);
		return;
	}
	apbt_address = (phys_addr_t)mtmr->phys_addr;
	if (!apbt_address) {
		printk(KERN_WARNING "No timer base from SFI, use default\n");
		apbt_address = APBT_DEFAULT_BASE;
	}
	apbt_virt_address = ioremap_nocache(apbt_address, APBT_MMAP_SIZE);
	if (!apbt_virt_address) {
		pr_debug("Failed mapping APBT phy address at %lu\n",\
			 (unsigned long)apbt_address);
		goto panic_noapbt;
	}
	apbt_freq = mtmr->freq_hz;
	sfi_free_mtmr(mtmr);

	/* Now figure out the physical timer id for clocksource device */
	mtmr = sfi_get_mtmr(APBT_CLOCKSOURCE_NUM);
	if (mtmr == NULL)
		goto panic_noapbt;

	/* Now figure out the physical timer id */
	pr_debug("Use timer %d for clocksource\n",
		 (int)(mtmr->phys_addr & 0xff) / APBTMRS_REG_SIZE);
	phy_cs_timer_id = (unsigned int)(mtmr->phys_addr & 0xff) /
		APBTMRS_REG_SIZE;

	clocksource_apbt = dw_apb_clocksource_init(APBT_CLOCKSOURCE_RATING,
		"apbt0", apbt_virt_address + phy_cs_timer_id *
		APBTMRS_REG_SIZE, apbt_freq);
	return;

panic_noapbt:
	panic("Failed to setup APB system timer\n");

}

static inline void apbt_clear_mapping(void)
{
	iounmap(apbt_virt_address);
	apbt_virt_address = NULL;
}

/*
 * APBT timer interrupt enable / disable
 */
static inline int is_apbt_capable(void)
{
	return apbt_virt_address ? 1 : 0;
}

static int __init apbt_clockevent_register(void)
{
	struct sfi_timer_table_entry *mtmr;
	struct apbt_dev *adev = this_cpu_ptr(&cpu_apbt_dev);

	mtmr = sfi_get_mtmr(APBT_CLOCKEVENT0_NUM);
	if (mtmr == NULL) {
		printk(KERN_ERR "Failed to get MTMR %d from SFI\n",
		       APBT_CLOCKEVENT0_NUM);
		return -ENODEV;
	}

	adev->num = smp_processor_id();
	adev->timer = dw_apb_clockevent_init(smp_processor_id(), "apbt0",
		intel_mid_timer_options == INTEL_MID_TIMER_LAPIC_APBT ?
		APBT_CLOCKEVENT_RATING - 100 : APBT_CLOCKEVENT_RATING,
		adev_virt_addr(adev), 0, apbt_freq);
	/* Firmware does EOI handling for us. */
	adev->timer->eoi = NULL;

	if (intel_mid_timer_options == INTEL_MID_TIMER_LAPIC_APBT) {
		global_clock_event = &adev->timer->ced;
		printk(KERN_DEBUG "%s clockevent registered as global\n",
		       global_clock_event->name);
	}

	dw_apb_clockevent_register(adev->timer);

	sfi_free_mtmr(mtmr);
	return 0;
}

#ifdef CONFIG_SMP

static void apbt_setup_irq(struct apbt_dev *adev)
{
	/* timer0 irq has been setup early */
	if (adev->irq == 0)
		return;

	irq_modify_status(adev->irq, 0, IRQ_MOVE_PCNTXT);
	irq_set_affinity(adev->irq, cpumask_of(adev->cpu));
	/* APB timer irqs are set up as mp_irqs, timer is edge type */
	__irq_set_handler(adev->irq, handle_edge_irq, 0, "edge");
}

/* Should be called with per cpu */
void apbt_setup_secondary_clock(void)
{
	struct apbt_dev *adev;
	int cpu;

	/* Don't register boot CPU clockevent */
	cpu = smp_processor_id();
	if (!cpu)
		return;

	adev = this_cpu_ptr(&cpu_apbt_dev);
	if (!adev->timer) {
		adev->timer = dw_apb_clockevent_init(cpu, adev->name,
			APBT_CLOCKEVENT_RATING, adev_virt_addr(adev),
			adev->irq, apbt_freq);
		adev->timer->eoi = NULL;
	} else {
		dw_apb_clockevent_resume(adev->timer);
	}

	printk(KERN_INFO "Registering CPU %d clockevent device %s, cpu %08x\n",
	       cpu, adev->name, adev->cpu);

	apbt_setup_irq(adev);
	dw_apb_clockevent_register(adev->timer);

	return;
}

/*
 * this notify handler process CPU hotplug events. in case of S0i3, nonboot
 * cpus are disabled/enabled frequently, for performance reasons, we keep the
 * per cpu timer irq registered so that we do need to do free_irq/request_irq.
 *
 * TODO: it might be more reliable to directly disable percpu clockevent device
 * without the notifier chain. currently, cpu 0 may get interrupts from other
 * cpu timers during the offline process due to the ordering of notification.
 * the extra interrupt is harmless.
 */
static int apbt_cpuhp_notify(struct notifier_block *n,
			     unsigned long action, void *hcpu)
{
	unsigned long cpu = (unsigned long)hcpu;
	struct apbt_dev *adev = &per_cpu(cpu_apbt_dev, cpu);

	switch (action & 0xf) {
	case CPU_DEAD:
		dw_apb_clockevent_pause(adev->timer);
		if (system_state == SYSTEM_RUNNING) {
			pr_debug("skipping APBT CPU %lu offline\n", cpu);
		} else {
			pr_debug("APBT clockevent for cpu %lu offline\n", cpu);
			dw_apb_clockevent_stop(adev->timer);
		}
		break;
	default:
		pr_debug("APBT notified %lu, no action\n", action);
	}
	return NOTIFY_OK;
}

static __init int apbt_late_init(void)
{
	if (intel_mid_timer_options == INTEL_MID_TIMER_LAPIC_APBT ||
		!apb_timer_block_enabled)
		return 0;
	/* This notifier should be called after workqueue is ready */
	hotcpu_notifier(apbt_cpuhp_notify, -20);
	return 0;
}
fs_initcall(apbt_late_init);
#else

void apbt_setup_secondary_clock(void) {}

#endif /* CONFIG_SMP */

static int apbt_clocksource_register(void)
{
	u64 start, now;
	cycle_t t1;

	/* Start the counter, use timer 2 as source, timer 0/1 for event */
	dw_apb_clocksource_start(clocksource_apbt);

	/* Verify whether apbt counter works */
	t1 = dw_apb_clocksource_read(clocksource_apbt);
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

	/* APBT is the only always on clocksource, it has to work! */
	if (t1 == dw_apb_clocksource_read(clocksource_apbt))
		panic("APBT counter not counting. APBT disabled\n");

	dw_apb_clocksource_register(clocksource_apbt);

	return 0;
}

/*
 * Early setup the APBT timer, only use timer 0 for booting then switch to
 * per CPU timer if possible.
 * returns 1 if per cpu apbt is setup
 * returns 0 if no per cpu apbt is chosen
 * panic if set up failed, this is the only platform timer on Moorestown.
 */
void __init apbt_time_init(void)
{
#ifdef CONFIG_SMP
	int i;
	struct sfi_timer_table_entry *p_mtmr;
	struct apbt_dev *adev;
#endif

	if (apb_timer_block_enabled)
		return;
	apbt_set_mapping();
	if (!apbt_virt_address)
		goto out_noapbt;
	/*
	 * Read the frequency and check for a sane value, for ESL model
	 * we extend the possible clock range to allow time scaling.
	 */

	if (apbt_freq < APBT_MIN_FREQ || apbt_freq > APBT_MAX_FREQ) {
		pr_debug("APBT has invalid freq 0x%lx\n", apbt_freq);
		goto out_noapbt;
	}
	if (apbt_clocksource_register()) {
		pr_debug("APBT has failed to register clocksource\n");
		goto out_noapbt;
	}
	if (!apbt_clockevent_register())
		apb_timer_block_enabled = 1;
	else {
		pr_debug("APBT has failed to register clockevent\n");
		goto out_noapbt;
	}
#ifdef CONFIG_SMP
	/* kernel cmdline disable apb timer, so we will use lapic timers */
	if (intel_mid_timer_options == INTEL_MID_TIMER_LAPIC_APBT) {
		printk(KERN_INFO "apbt: disabled per cpu timer\n");
		return;
	}
	pr_debug("%s: %d CPUs online\n", __func__, num_online_cpus());
	if (num_possible_cpus() <= sfi_mtimer_num)
		apbt_num_timers_used = num_possible_cpus();
	else
		apbt_num_timers_used = 1;
	pr_debug("%s: %d APB timers used\n", __func__, apbt_num_timers_used);

	/* here we set up per CPU timer data structure */
	for (i = 0; i < apbt_num_timers_used; i++) {
		adev = &per_cpu(cpu_apbt_dev, i);
		adev->num = i;
		adev->cpu = i;
		p_mtmr = sfi_get_mtmr(i);
		if (p_mtmr)
			adev->irq = p_mtmr->irq;
		else
			printk(KERN_ERR "Failed to get timer for cpu %d\n", i);
		snprintf(adev->name, sizeof(adev->name) - 1, "apbt%d", i);
	}
#endif

	return;

out_noapbt:
	apbt_clear_mapping();
	apb_timer_block_enabled = 0;
	panic("failed to enable APB timer\n");
}

/* called before apb_timer_enable, use early map */
unsigned long apbt_quick_calibrate(void)
{
	int i, scale;
	u64 old, new;
	cycle_t t1, t2;
	unsigned long khz = 0;
	u32 loop, shift;

	apbt_set_mapping();
	dw_apb_clocksource_start(clocksource_apbt);

	/* check if the timer can count down, otherwise return */
	old = dw_apb_clocksource_read(clocksource_apbt);
	i = 10000;
	while (--i) {
		if (old != dw_apb_clocksource_read(clocksource_apbt))
			break;
	}
	if (!i)
		goto failed;

	/* count 16 ms */
	loop = (apbt_freq / 1000) << 4;

	/* restart the timer to ensure it won't get to 0 in the calibration */
	dw_apb_clocksource_start(clocksource_apbt);

	old = dw_apb_clocksource_read(clocksource_apbt);
	old += loop;

	t1 = __native_read_tsc();

	do {
		new = dw_apb_clocksource_read(clocksource_apbt);
	} while (new < old);

	t2 = __native_read_tsc();

	shift = 5;
	if (unlikely(loop >> shift == 0)) {
		printk(KERN_INFO
		       "APBT TSC calibration failed, not enough resolution\n");
		return 0;
	}
	scale = (int)div_u64((t2 - t1), loop >> shift);
	khz = (scale * (apbt_freq / 1000)) >> shift;
	printk(KERN_INFO "TSC freq calculated by APB timer is %lu khz\n", khz);
	return khz;
failed:
	return 0;
}
