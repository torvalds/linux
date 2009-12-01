/*
 * Thermal throttle event support code (such as syslog messaging and rate
 * limiting) that was factored out from x86_64 (mce_intel.c) and i386 (p4.c).
 *
 * This allows consistent reporting of CPU thermal throttle events.
 *
 * Maintains a counter in /sys that keeps track of the number of thermal
 * events, such that the user knows how bad the thermal problem might be
 * (since the logging to syslog and mcelog is rate limited).
 *
 * Author: Dmitriy Zavin (dmitriyz@google.com)
 *
 * Credits: Adapted from Zwane Mwaikambo's original code in mce_intel.c.
 *          Inspired by Ross Biro's and Al Borchers' counter code.
 */
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/sysdev.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpu.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/apic.h>
#include <asm/idle.h>
#include <asm/mce.h>
#include <asm/msr.h>

/* How long to wait between reporting thermal events */
#define CHECK_INTERVAL		(300 * HZ)

/*
 * Current thermal throttling state:
 */
struct thermal_state {
	bool			is_throttled;

	u64			next_check;
	unsigned long		throttle_count;
	unsigned long		last_throttle_count;
};

static DEFINE_PER_CPU(struct thermal_state, thermal_state);

static atomic_t therm_throt_en	= ATOMIC_INIT(0);

#ifdef CONFIG_SYSFS
#define define_therm_throt_sysdev_one_ro(_name)				\
	static SYSDEV_ATTR(_name, 0444, therm_throt_sysdev_show_##_name, NULL)

#define define_therm_throt_sysdev_show_func(name)			\
									\
static ssize_t therm_throt_sysdev_show_##name(				\
			struct sys_device *dev,				\
			struct sysdev_attribute *attr,			\
			char *buf)					\
{									\
	unsigned int cpu = dev->id;					\
	ssize_t ret;							\
									\
	preempt_disable();	/* CPU hotplug */			\
	if (cpu_online(cpu))						\
		ret = sprintf(buf, "%lu\n",				\
			      per_cpu(thermal_state, cpu).name);	\
	else								\
		ret = 0;						\
	preempt_enable();						\
									\
	return ret;							\
}

define_therm_throt_sysdev_show_func(throttle_count);
define_therm_throt_sysdev_one_ro(throttle_count);

static struct attribute *thermal_throttle_attrs[] = {
	&attr_throttle_count.attr,
	NULL
};

static struct attribute_group thermal_throttle_attr_group = {
	.attrs	= thermal_throttle_attrs,
	.name	= "thermal_throttle"
};
#endif /* CONFIG_SYSFS */

/***
 * therm_throt_process - Process thermal throttling event from interrupt
 * @curr: Whether the condition is current or not (boolean), since the
 *        thermal interrupt normally gets called both when the thermal
 *        event begins and once the event has ended.
 *
 * This function is called by the thermal interrupt after the
 * IRQ has been acknowledged.
 *
 * It will take care of rate limiting and printing messages to the syslog.
 *
 * Returns: 0 : Event should NOT be further logged, i.e. still in
 *              "timeout" from previous log message.
 *          1 : Event should be logged further, and a message has been
 *              printed to the syslog.
 */
static int therm_throt_process(bool is_throttled)
{
	struct thermal_state *state;
	unsigned int this_cpu;
	bool was_throttled;
	u64 now;

	this_cpu = smp_processor_id();
	now = get_jiffies_64();
	state = &per_cpu(thermal_state, this_cpu);

	was_throttled = state->is_throttled;
	state->is_throttled = is_throttled;

	if (is_throttled)
		state->throttle_count++;

	if (time_before64(now, state->next_check) &&
			state->throttle_count != state->last_throttle_count)
		return 0;

	state->next_check = now + CHECK_INTERVAL;
	state->last_throttle_count = state->throttle_count;

	/* if we just entered the thermal event */
	if (is_throttled) {
		printk(KERN_CRIT "CPU%d: Temperature above threshold, cpu clock throttled (total events = %lu)\n", this_cpu, state->throttle_count);

		add_taint(TAINT_MACHINE_CHECK);
		return 1;
	}
	if (was_throttled) {
		printk(KERN_INFO "CPU%d: Temperature/speed normal\n", this_cpu);
		return 1;
	}

	return 0;
}

#ifdef CONFIG_SYSFS
/* Add/Remove thermal_throttle interface for CPU device: */
static __cpuinit int thermal_throttle_add_dev(struct sys_device *sys_dev)
{
	return sysfs_create_group(&sys_dev->kobj,
				  &thermal_throttle_attr_group);
}

static __cpuinit void thermal_throttle_remove_dev(struct sys_device *sys_dev)
{
	sysfs_remove_group(&sys_dev->kobj, &thermal_throttle_attr_group);
}

/* Mutex protecting device creation against CPU hotplug: */
static DEFINE_MUTEX(therm_cpu_lock);

/* Get notified when a cpu comes on/off. Be hotplug friendly. */
static __cpuinit int
thermal_throttle_cpu_callback(struct notifier_block *nfb,
			      unsigned long action,
			      void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct sys_device *sys_dev;
	int err = 0;

	sys_dev = get_cpu_sysdev(cpu);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		mutex_lock(&therm_cpu_lock);
		err = thermal_throttle_add_dev(sys_dev);
		mutex_unlock(&therm_cpu_lock);
		WARN_ON(err);
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		mutex_lock(&therm_cpu_lock);
		thermal_throttle_remove_dev(sys_dev);
		mutex_unlock(&therm_cpu_lock);
		break;
	}
	return err ? NOTIFY_BAD : NOTIFY_OK;
}

static struct notifier_block thermal_throttle_cpu_notifier __cpuinitdata =
{
	.notifier_call = thermal_throttle_cpu_callback,
};

static __init int thermal_throttle_init_device(void)
{
	unsigned int cpu = 0;
	int err;

	if (!atomic_read(&therm_throt_en))
		return 0;

	register_hotcpu_notifier(&thermal_throttle_cpu_notifier);

#ifdef CONFIG_HOTPLUG_CPU
	mutex_lock(&therm_cpu_lock);
#endif
	/* connect live CPUs to sysfs */
	for_each_online_cpu(cpu) {
		err = thermal_throttle_add_dev(get_cpu_sysdev(cpu));
		WARN_ON(err);
	}
#ifdef CONFIG_HOTPLUG_CPU
	mutex_unlock(&therm_cpu_lock);
#endif

	return 0;
}
device_initcall(thermal_throttle_init_device);

#endif /* CONFIG_SYSFS */

/* Thermal transition interrupt handler */
static void intel_thermal_interrupt(void)
{
	__u64 msr_val;

	rdmsrl(MSR_IA32_THERM_STATUS, msr_val);
	if (therm_throt_process((msr_val & THERM_STATUS_PROCHOT) != 0))
		mce_log_therm_throt_event(msr_val);
}

static void unexpected_thermal_interrupt(void)
{
	printk(KERN_ERR "CPU%d: Unexpected LVT TMR interrupt!\n",
			smp_processor_id());
	add_taint(TAINT_MACHINE_CHECK);
}

static void (*smp_thermal_vector)(void) = unexpected_thermal_interrupt;

asmlinkage void smp_thermal_interrupt(struct pt_regs *regs)
{
	exit_idle();
	irq_enter();
	inc_irq_stat(irq_thermal_count);
	smp_thermal_vector();
	irq_exit();
	/* Ack only at the end to avoid potential reentry */
	ack_APIC_irq();
}

void intel_init_thermal(struct cpuinfo_x86 *c)
{
	unsigned int cpu = smp_processor_id();
	int tm2 = 0;
	u32 l, h;

	/* Thermal monitoring depends on ACPI and clock modulation*/
	if (!cpu_has(c, X86_FEATURE_ACPI) || !cpu_has(c, X86_FEATURE_ACC))
		return;

	/*
	 * First check if its enabled already, in which case there might
	 * be some SMM goo which handles it, so we can't even put a handler
	 * since it might be delivered via SMI already:
	 */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	h = apic_read(APIC_LVTTHMR);
	if ((l & MSR_IA32_MISC_ENABLE_TM1) && (h & APIC_DM_SMI)) {
		printk(KERN_DEBUG
		       "CPU%d: Thermal monitoring handled by SMI\n", cpu);
		return;
	}

	/* Check whether a vector already exists */
	if (h & APIC_VECTOR_MASK) {
		printk(KERN_DEBUG
		       "CPU%d: Thermal LVT vector (%#x) already installed\n",
		       cpu, (h & APIC_VECTOR_MASK));
		return;
	}

	/* early Pentium M models use different method for enabling TM2 */
	if (cpu_has(c, X86_FEATURE_TM2)) {
		if (c->x86 == 6 && (c->x86_model == 9 || c->x86_model == 13)) {
			rdmsr(MSR_THERM2_CTL, l, h);
			if (l & MSR_THERM2_CTL_TM_SELECT)
				tm2 = 1;
		} else if (l & MSR_IA32_MISC_ENABLE_TM2)
			tm2 = 1;
	}

	/* We'll mask the thermal vector in the lapic till we're ready: */
	h = THERMAL_APIC_VECTOR | APIC_DM_FIXED | APIC_LVT_MASKED;
	apic_write(APIC_LVTTHMR, h);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT,
		l | (THERM_INT_LOW_ENABLE | THERM_INT_HIGH_ENABLE), h);

	smp_thermal_vector = intel_thermal_interrupt;

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | MSR_IA32_MISC_ENABLE_TM1, h);

	/* Unmask the thermal vector: */
	l = apic_read(APIC_LVTTHMR);
	apic_write(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);

	printk(KERN_INFO "CPU%d: Thermal monitoring enabled (%s)\n",
	       cpu, tm2 ? "TM2" : "TM1");

	/* enable thermal throttle processing */
	atomic_set(&therm_throt_en, 1);
}
