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

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/sfi.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/irq.h>

#include <asm/fixmap.h>
#include <asm/apb_timer.h>

#define APBT_MASK			CLOCKSOURCE_MASK(32)
#define APBT_SHIFT			22
#define APBT_CLOCKEVENT_RATING		150
#define APBT_CLOCKSOURCE_RATING		250
#define APBT_MIN_DELTA_USEC		200

#define EVT_TO_APBT_DEV(evt) container_of(evt, struct apbt_dev, evt)
#define APBT_CLOCKEVENT0_NUM   (0)
#define APBT_CLOCKEVENT1_NUM   (1)
#define APBT_CLOCKSOURCE_NUM   (2)

static unsigned long apbt_address;
static int apb_timer_block_enabled;
static void __iomem *apbt_virt_address;
static int phy_cs_timer_id;

/*
 * Common DW APB timer info
 */
static uint64_t apbt_freq;

static void apbt_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt);
static int apbt_next_event(unsigned long delta,
			   struct clock_event_device *evt);
static cycle_t apbt_read_clocksource(struct clocksource *cs);
static void apbt_restart_clocksource(void);

struct apbt_dev {
	struct clock_event_device evt;
	unsigned int num;
	int cpu;
	unsigned int irq;
	unsigned int tick;
	unsigned int count;
	unsigned int flags;
	char name[10];
};

int disable_apbt_percpu __cpuinitdata;

static DEFINE_PER_CPU(struct apbt_dev, cpu_apbt_dev);

#ifdef CONFIG_SMP
static unsigned int apbt_num_timers_used;
static struct apbt_dev *apbt_devs;
#endif

static	inline unsigned long apbt_readl_reg(unsigned long a)
{
	return readl(apbt_virt_address + a);
}

static inline void apbt_writel_reg(unsigned long d, unsigned long a)
{
	writel(d, apbt_virt_address + a);
}

static inline unsigned long apbt_readl(int n, unsigned long a)
{
	return readl(apbt_virt_address + a + n * APBTMRS_REG_SIZE);
}

static inline void apbt_writel(int n, unsigned long d, unsigned long a)
{
	writel(d, apbt_virt_address + a + n * APBTMRS_REG_SIZE);
}

static inline void apbt_set_mapping(void)
{
	struct sfi_timer_table_entry *mtmr;

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
	apbt_address = (unsigned long)mtmr->phys_addr;
	if (!apbt_address) {
		printk(KERN_WARNING "No timer base from SFI, use default\n");
		apbt_address = APBT_DEFAULT_BASE;
	}
	apbt_virt_address = ioremap_nocache(apbt_address, APBT_MMAP_SIZE);
	if (apbt_virt_address) {
		pr_debug("Mapped APBT physical addr %p at virtual addr %p\n",\
			 (void *)apbt_address, (void *)apbt_virt_address);
	} else {
		pr_debug("Failed mapping APBT phy address at %p\n",\
			 (void *)apbt_address);
		goto panic_noapbt;
	}
	apbt_freq = mtmr->freq_hz / USEC_PER_SEC;
	sfi_free_mtmr(mtmr);

	/* Now figure out the physical timer id for clocksource device */
	mtmr = sfi_get_mtmr(APBT_CLOCKSOURCE_NUM);
	if (mtmr == NULL)
		goto panic_noapbt;

	/* Now figure out the physical timer id */
	phy_cs_timer_id = (unsigned int)(mtmr->phys_addr & 0xff)
		/ APBTMRS_REG_SIZE;
	pr_debug("Use timer %d for clocksource\n", phy_cs_timer_id);
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

static struct clocksource clocksource_apbt = {
	.name		= "apbt",
	.rating		= APBT_CLOCKSOURCE_RATING,
	.read		= apbt_read_clocksource,
	.mask		= APBT_MASK,
	.shift		= APBT_SHIFT,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume		= apbt_restart_clocksource,
};

/* boot APB clock event device */
static struct clock_event_device apbt_clockevent = {
	.name		= "apbt0",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= apbt_set_mode,
	.set_next_event = apbt_next_event,
	.shift		= APBT_SHIFT,
	.irq		= 0,
	.rating		= APBT_CLOCKEVENT_RATING,
};

/*
 * if user does not want to use per CPU apb timer, just give it a lower rating
 * than local apic timer and skip the late per cpu timer init.
 */
static inline int __init setup_x86_mrst_timer(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp("apbt_only", arg) == 0)
		disable_apbt_percpu = 0;
	else if (strcmp("lapic_and_apbt", arg) == 0)
		disable_apbt_percpu = 1;
	else {
		pr_warning("X86 MRST timer option %s not recognised"
			   " use x86_mrst_timer=apbt_only or lapic_and_apbt\n",
			   arg);
		return -EINVAL;
	}
	return 0;
}
__setup("x86_mrst_timer=", setup_x86_mrst_timer);

/*
 * start count down from 0xffff_ffff. this is done by toggling the enable bit
 * then load initial load count to ~0.
 */
static void apbt_start_counter(int n)
{
	unsigned long ctrl = apbt_readl(n, APBTMR_N_CONTROL);

	ctrl &= ~APBTMR_CONTROL_ENABLE;
	apbt_writel(n, ctrl, APBTMR_N_CONTROL);
	apbt_writel(n, ~0, APBTMR_N_LOAD_COUNT);
	/* enable, mask interrupt */
	ctrl &= ~APBTMR_CONTROL_MODE_PERIODIC;
	ctrl |= (APBTMR_CONTROL_ENABLE | APBTMR_CONTROL_INT);
	apbt_writel(n, ctrl, APBTMR_N_CONTROL);
	/* read it once to get cached counter value initialized */
	apbt_read_clocksource(&clocksource_apbt);
}

static irqreturn_t apbt_interrupt_handler(int irq, void *data)
{
	struct apbt_dev *dev = (struct apbt_dev *)data;
	struct clock_event_device *aevt = &dev->evt;

	if (!aevt->event_handler) {
		printk(KERN_INFO "Spurious APBT timer interrupt on %d\n",
		       dev->num);
		return IRQ_NONE;
	}
	aevt->event_handler(aevt);
	return IRQ_HANDLED;
}

static void apbt_restart_clocksource(void)
{
	apbt_start_counter(phy_cs_timer_id);
}

/* Setup IRQ routing via IOAPIC */
#ifdef CONFIG_SMP
static void apbt_setup_irq(struct apbt_dev *adev)
{
	struct irq_chip *chip;
	struct irq_desc *desc;

	/* timer0 irq has been setup early */
	if (adev->irq == 0)
		return;
	desc = irq_to_desc(adev->irq);
	chip = get_irq_chip(adev->irq);
	disable_irq(adev->irq);
	desc->status |= IRQ_MOVE_PCNTXT;
	irq_set_affinity(adev->irq, cpumask_of(adev->cpu));
	/* APB timer irqs are set up as mp_irqs, timer is edge triggerred */
	set_irq_chip_and_handler_name(adev->irq, chip, handle_edge_irq, "edge");
	enable_irq(adev->irq);
	if (system_state == SYSTEM_BOOTING)
		if (request_irq(adev->irq, apbt_interrupt_handler,
				IRQF_TIMER | IRQF_DISABLED | IRQF_NOBALANCING,
				adev->name, adev)) {
			printk(KERN_ERR "Failed request IRQ for APBT%d\n",
			       adev->num);
		}
}
#endif

static void apbt_enable_int(int n)
{
	unsigned long ctrl = apbt_readl(n, APBTMR_N_CONTROL);
	/* clear pending intr */
	apbt_readl(n, APBTMR_N_EOI);
	ctrl &= ~APBTMR_CONTROL_INT;
	apbt_writel(n, ctrl, APBTMR_N_CONTROL);
}

static void apbt_disable_int(int n)
{
	unsigned long ctrl = apbt_readl(n, APBTMR_N_CONTROL);

	ctrl |= APBTMR_CONTROL_INT;
	apbt_writel(n, ctrl, APBTMR_N_CONTROL);
}


static int __init apbt_clockevent_register(void)
{
	struct sfi_timer_table_entry *mtmr;
	struct apbt_dev *adev = &__get_cpu_var(cpu_apbt_dev);

	mtmr = sfi_get_mtmr(APBT_CLOCKEVENT0_NUM);
	if (mtmr == NULL) {
		printk(KERN_ERR "Failed to get MTMR %d from SFI\n",
		       APBT_CLOCKEVENT0_NUM);
		return -ENODEV;
	}

	/*
	 * We need to calculate the scaled math multiplication factor for
	 * nanosecond to apbt tick conversion.
	 * mult = (nsec/cycle)*2^APBT_SHIFT
	 */
	apbt_clockevent.mult = div_sc((unsigned long) mtmr->freq_hz
				      , NSEC_PER_SEC, APBT_SHIFT);

	/* Calculate the min / max delta */
	apbt_clockevent.max_delta_ns = clockevent_delta2ns(0x7FFFFFFF,
							   &apbt_clockevent);
	apbt_clockevent.min_delta_ns = clockevent_delta2ns(
		APBT_MIN_DELTA_USEC*apbt_freq,
		&apbt_clockevent);
	/*
	 * Start apbt with the boot cpu mask and make it
	 * global if not used for per cpu timer.
	 */
	apbt_clockevent.cpumask = cpumask_of(smp_processor_id());
	adev->num = smp_processor_id();
	memcpy(&adev->evt, &apbt_clockevent, sizeof(struct clock_event_device));

	if (disable_apbt_percpu) {
		apbt_clockevent.rating = APBT_CLOCKEVENT_RATING - 100;
		global_clock_event = &adev->evt;
		printk(KERN_DEBUG "%s clockevent registered as global\n",
		       global_clock_event->name);
	}

	if (request_irq(apbt_clockevent.irq, apbt_interrupt_handler,
			IRQF_TIMER | IRQF_DISABLED | IRQF_NOBALANCING,
			apbt_clockevent.name, adev)) {
		printk(KERN_ERR "Failed request IRQ for APBT%d\n",
		       apbt_clockevent.irq);
	}

	clockevents_register_device(&adev->evt);
	/* Start APBT 0 interrupts */
	apbt_enable_int(APBT_CLOCKEVENT0_NUM);

	sfi_free_mtmr(mtmr);
	return 0;
}

#ifdef CONFIG_SMP
/* Should be called with per cpu */
void apbt_setup_secondary_clock(void)
{
	struct apbt_dev *adev;
	struct clock_event_device *aevt;
	int cpu;

	/* Don't register boot CPU clockevent */
	cpu = smp_processor_id();
	if (cpu == boot_cpu_id)
		return;
	/*
	 * We need to calculate the scaled math multiplication factor for
	 * nanosecond to apbt tick conversion.
	 * mult = (nsec/cycle)*2^APBT_SHIFT
	 */
	printk(KERN_INFO "Init per CPU clockevent %d\n", cpu);
	adev = &per_cpu(cpu_apbt_dev, cpu);
	aevt = &adev->evt;

	memcpy(aevt, &apbt_clockevent, sizeof(*aevt));
	aevt->cpumask = cpumask_of(cpu);
	aevt->name = adev->name;
	aevt->mode = CLOCK_EVT_MODE_UNUSED;

	printk(KERN_INFO "Registering CPU %d clockevent device %s, mask %08x\n",
	       cpu, aevt->name, *(u32 *)aevt->cpumask);

	apbt_setup_irq(adev);

	clockevents_register_device(aevt);

	apbt_enable_int(cpu);

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
		apbt_disable_int(cpu);
		if (system_state == SYSTEM_RUNNING)
			pr_debug("skipping APBT CPU %lu offline\n", cpu);
		else if (adev) {
			pr_debug("APBT clockevent for cpu %lu offline\n", cpu);
			free_irq(adev->irq, adev);
		}
		break;
	default:
		pr_debug(KERN_INFO "APBT notified %lu, no action\n", action);
	}
	return NOTIFY_OK;
}

static __init int apbt_late_init(void)
{
	if (disable_apbt_percpu)
		return 0;
	/* This notifier should be called after workqueue is ready */
	hotcpu_notifier(apbt_cpuhp_notify, -20);
	return 0;
}
fs_initcall(apbt_late_init);
#else

void apbt_setup_secondary_clock(void) {}

#endif /* CONFIG_SMP */

static void apbt_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt)
{
	unsigned long ctrl;
	uint64_t delta;
	int timer_num;
	struct apbt_dev *adev = EVT_TO_APBT_DEV(evt);

	timer_num = adev->num;
	pr_debug("%s CPU %d timer %d mode=%d\n",
		 __func__, first_cpu(*evt->cpumask), timer_num, mode);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		delta = ((uint64_t)(NSEC_PER_SEC/HZ)) * apbt_clockevent.mult;
		delta >>= apbt_clockevent.shift;
		ctrl = apbt_readl(timer_num, APBTMR_N_CONTROL);
		ctrl |= APBTMR_CONTROL_MODE_PERIODIC;
		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
		/*
		 * DW APB p. 46, have to disable timer before load counter,
		 * may cause sync problem.
		 */
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
		udelay(1);
		pr_debug("Setting clock period %d for HZ %d\n", (int)delta, HZ);
		apbt_writel(timer_num, delta, APBTMR_N_LOAD_COUNT);
		ctrl |= APBTMR_CONTROL_ENABLE;
		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
		break;
		/* APB timer does not have one-shot mode, use free running mode */
	case CLOCK_EVT_MODE_ONESHOT:
		ctrl = apbt_readl(timer_num, APBTMR_N_CONTROL);
		/*
		 * set free running mode, this mode will let timer reload max
		 * timeout which will give time (3min on 25MHz clock) to rearm
		 * the next event, therefore emulate the one-shot mode.
		 */
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		ctrl &= ~APBTMR_CONTROL_MODE_PERIODIC;

		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
		/* write again to set free running mode */
		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);

		/*
		 * DW APB p. 46, load counter with all 1s before starting free
		 * running mode.
		 */
		apbt_writel(timer_num, ~0, APBTMR_N_LOAD_COUNT);
		ctrl &= ~APBTMR_CONTROL_INT;
		ctrl |= APBTMR_CONTROL_ENABLE;
		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		apbt_disable_int(timer_num);
		ctrl = apbt_readl(timer_num, APBTMR_N_CONTROL);
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
		break;

	case CLOCK_EVT_MODE_RESUME:
		apbt_enable_int(timer_num);
		break;
	}
}

static int apbt_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	unsigned long ctrl;
	int timer_num;

	struct apbt_dev *adev = EVT_TO_APBT_DEV(evt);

	timer_num = adev->num;
	/* Disable timer */
	ctrl = apbt_readl(timer_num, APBTMR_N_CONTROL);
	ctrl &= ~APBTMR_CONTROL_ENABLE;
	apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
	/* write new count */
	apbt_writel(timer_num, delta, APBTMR_N_LOAD_COUNT);
	ctrl |= APBTMR_CONTROL_ENABLE;
	apbt_writel(timer_num, ctrl, APBTMR_N_CONTROL);
	return 0;
}

/*
 * APB timer clock is not in sync with pclk on Langwell, which translates to
 * unreliable read value caused by sampling error. the error does not add up
 * overtime and only happens when sampling a 0 as a 1 by mistake. so the time
 * would go backwards. the following code is trying to prevent time traveling
 * backwards. little bit paranoid.
 */
static cycle_t apbt_read_clocksource(struct clocksource *cs)
{
	unsigned long t0, t1, t2;
	static unsigned long last_read;

bad_count:
	t1 = apbt_readl(phy_cs_timer_id,
			APBTMR_N_CURRENT_VALUE);
	t2 = apbt_readl(phy_cs_timer_id,
			APBTMR_N_CURRENT_VALUE);
	if (unlikely(t1 < t2)) {
		pr_debug("APBT: read current count error %lx:%lx:%lx\n",
			 t1, t2, t2 - t1);
		goto bad_count;
	}
	/*
	 * check against cached last read, makes sure time does not go back.
	 * it could be a normal rollover but we will do tripple check anyway
	 */
	if (unlikely(t2 > last_read)) {
		/* check if we have a normal rollover */
		unsigned long raw_intr_status =
			apbt_readl_reg(APBTMRS_RAW_INT_STATUS);
		/*
		 * cs timer interrupt is masked but raw intr bit is set if
		 * rollover occurs. then we read EOI reg to clear it.
		 */
		if (raw_intr_status & (1 << phy_cs_timer_id)) {
			apbt_readl(phy_cs_timer_id, APBTMR_N_EOI);
			goto out;
		}
		pr_debug("APB CS going back %lx:%lx:%lx ",
			 t2, last_read, t2 - last_read);
bad_count_x3:
		pr_debug(KERN_INFO "tripple check enforced\n");
		t0 = apbt_readl(phy_cs_timer_id,
				APBTMR_N_CURRENT_VALUE);
		udelay(1);
		t1 = apbt_readl(phy_cs_timer_id,
				APBTMR_N_CURRENT_VALUE);
		udelay(1);
		t2 = apbt_readl(phy_cs_timer_id,
				APBTMR_N_CURRENT_VALUE);
		if ((t2 > t1) || (t1 > t0)) {
			printk(KERN_ERR "Error: APB CS tripple check failed\n");
			goto bad_count_x3;
		}
	}
out:
	last_read = t2;
	return (cycle_t)~t2;
}

static int apbt_clocksource_register(void)
{
	u64 start, now;
	cycle_t t1;

	/* Start the counter, use timer 2 as source, timer 0/1 for event */
	apbt_start_counter(phy_cs_timer_id);

	/* Verify whether apbt counter works */
	t1 = apbt_read_clocksource(&clocksource_apbt);
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
	if (t1 == apbt_read_clocksource(&clocksource_apbt))
		panic("APBT counter not counting. APBT disabled\n");

	/*
	 * initialize and register APBT clocksource
	 * convert that to ns/clock cycle
	 * mult = (ns/c) * 2^APBT_SHIFT
	 */
	clocksource_apbt.mult = div_sc(MSEC_PER_SEC,
				       (unsigned long) apbt_freq, APBT_SHIFT);
	clocksource_register(&clocksource_apbt);

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
	unsigned int percpu_timer;
	struct apbt_dev *adev;
#endif

	if (apb_timer_block_enabled)
		return;
	apbt_set_mapping();
	if (apbt_virt_address) {
		pr_debug("Found APBT version 0x%lx\n",\
			 apbt_readl_reg(APBTMRS_COMP_VERSION));
	} else
		goto out_noapbt;
	/*
	 * Read the frequency and check for a sane value, for ESL model
	 * we extend the possible clock range to allow time scaling.
	 */

	if (apbt_freq < APBT_MIN_FREQ || apbt_freq > APBT_MAX_FREQ) {
		pr_debug("APBT has invalid freq 0x%llx\n", apbt_freq);
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
	if (disable_apbt_percpu) {
		printk(KERN_INFO "apbt: disabled per cpu timer\n");
		return;
	}
	pr_debug("%s: %d CPUs online\n", __func__, num_online_cpus());
	if (num_possible_cpus() <= sfi_mtimer_num) {
		percpu_timer = 1;
		apbt_num_timers_used = num_possible_cpus();
	} else {
		percpu_timer = 0;
		apbt_num_timers_used = 1;
		adev = &per_cpu(cpu_apbt_dev, 0);
		adev->flags &= ~APBT_DEV_USED;
	}
	pr_debug("%s: %d APB timers used\n", __func__, apbt_num_timers_used);

	/* here we set up per CPU timer data structure */
	apbt_devs = kzalloc(sizeof(struct apbt_dev) * apbt_num_timers_used,
			    GFP_KERNEL);
	if (!apbt_devs) {
		printk(KERN_ERR "Failed to allocate APB timer devices\n");
		return;
	}
	for (i = 0; i < apbt_num_timers_used; i++) {
		adev = &per_cpu(cpu_apbt_dev, i);
		adev->num = i;
		adev->cpu = i;
		p_mtmr = sfi_get_mtmr(i);
		if (p_mtmr) {
			adev->tick = p_mtmr->freq_hz;
			adev->irq = p_mtmr->irq;
		} else
			printk(KERN_ERR "Failed to get timer for cpu %d\n", i);
		adev->count = 0;
		sprintf(adev->name, "apbt%d", i);
	}
#endif

	return;

out_noapbt:
	apbt_clear_mapping();
	apb_timer_block_enabled = 0;
	panic("failed to enable APB timer\n");
}

static inline void apbt_disable(int n)
{
	if (is_apbt_capable()) {
		unsigned long ctrl =  apbt_readl(n, APBTMR_N_CONTROL);
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		apbt_writel(n, ctrl, APBTMR_N_CONTROL);
	}
}

/* called before apb_timer_enable, use early map */
unsigned long apbt_quick_calibrate()
{
	int i, scale;
	u64 old, new;
	cycle_t t1, t2;
	unsigned long khz = 0;
	u32 loop, shift;

	apbt_set_mapping();
	apbt_start_counter(phy_cs_timer_id);

	/* check if the timer can count down, otherwise return */
	old = apbt_read_clocksource(&clocksource_apbt);
	i = 10000;
	while (--i) {
		if (old != apbt_read_clocksource(&clocksource_apbt))
			break;
	}
	if (!i)
		goto failed;

	/* count 16 ms */
	loop = (apbt_freq * 1000) << 4;

	/* restart the timer to ensure it won't get to 0 in the calibration */
	apbt_start_counter(phy_cs_timer_id);

	old = apbt_read_clocksource(&clocksource_apbt);
	old += loop;

	t1 = __native_read_tsc();

	do {
		new = apbt_read_clocksource(&clocksource_apbt);
	} while (new < old);

	t2 = __native_read_tsc();

	shift = 5;
	if (unlikely(loop >> shift == 0)) {
		printk(KERN_INFO
		       "APBT TSC calibration failed, not enough resolution\n");
		return 0;
	}
	scale = (int)div_u64((t2 - t1), loop >> shift);
	khz = (scale * apbt_freq * 1000) >> shift;
	printk(KERN_INFO "TSC freq calculated by APB timer is %lu khz\n", khz);
	return khz;
failed:
	return 0;
}
