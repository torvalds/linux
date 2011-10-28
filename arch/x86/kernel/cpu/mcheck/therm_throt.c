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

#define THERMAL_THROTTLING_EVENT	0
#define POWER_LIMIT_EVENT		1

/*
 * Current thermal event state:
 */
struct _thermal_state {
	bool			new_event;
	int			event;
	u64			next_check;
	unsigned long		count;
	unsigned long		last_count;
};

struct thermal_state {
	struct _thermal_state core_throttle;
	struct _thermal_state core_power_limit;
	struct _thermal_state package_throttle;
	struct _thermal_state package_power_limit;
	struct _thermal_state core_thresh0;
	struct _thermal_state core_thresh1;
};

/* Callback to handle core threshold interrupts */
int (*platform_thermal_notify)(__u64 msr_val);
EXPORT_SYMBOL(platform_thermal_notify);

static DEFINE_PER_CPU(struct thermal_state, thermal_state);

static atomic_t therm_throt_en	= ATOMIC_INIT(0);

static u32 lvtthmr_init __read_mostly;

#ifdef CONFIG_SYSFS
#define define_therm_throt_sysdev_one_ro(_name)				\
	static SYSDEV_ATTR(_name, 0444,					\
			   therm_throt_sysdev_show_##_name,		\
				   NULL)				\

#define define_therm_throt_sysdev_show_func(event, name)		\
									\
static ssize_t therm_throt_sysdev_show_##event##_##name(		\
			struct sys_device *dev,				\
			struct sysdev_attribute *attr,			\
			char *buf)					\
{									\
	unsigned int cpu = dev->id;					\
	ssize_t ret;							\
									\
	preempt_disable();	/* CPU hotplug */			\
	if (cpu_online(cpu)) {						\
		ret = sprintf(buf, "%lu\n",				\
			      per_cpu(thermal_state, cpu).event.name);	\
	} else								\
		ret = 0;						\
	preempt_enable();						\
									\
	return ret;							\
}

define_therm_throt_sysdev_show_func(core_throttle, count);
define_therm_throt_sysdev_one_ro(core_throttle_count);

define_therm_throt_sysdev_show_func(core_power_limit, count);
define_therm_throt_sysdev_one_ro(core_power_limit_count);

define_therm_throt_sysdev_show_func(package_throttle, count);
define_therm_throt_sysdev_one_ro(package_throttle_count);

define_therm_throt_sysdev_show_func(package_power_limit, count);
define_therm_throt_sysdev_one_ro(package_power_limit_count);

static struct attribute *thermal_throttle_attrs[] = {
	&attr_core_throttle_count.attr,
	NULL
};

static struct attribute_group thermal_attr_group = {
	.attrs	= thermal_throttle_attrs,
	.name	= "thermal_throttle"
};
#endif /* CONFIG_SYSFS */

#define CORE_LEVEL	0
#define PACKAGE_LEVEL	1

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
static int therm_throt_process(bool new_event, int event, int level)
{
	struct _thermal_state *state;
	unsigned int this_cpu = smp_processor_id();
	bool old_event;
	u64 now;
	struct thermal_state *pstate = &per_cpu(thermal_state, this_cpu);

	now = get_jiffies_64();
	if (level == CORE_LEVEL) {
		if (event == THERMAL_THROTTLING_EVENT)
			state = &pstate->core_throttle;
		else if (event == POWER_LIMIT_EVENT)
			state = &pstate->core_power_limit;
		else
			 return 0;
	} else if (level == PACKAGE_LEVEL) {
		if (event == THERMAL_THROTTLING_EVENT)
			state = &pstate->package_throttle;
		else if (event == POWER_LIMIT_EVENT)
			state = &pstate->package_power_limit;
		else
			return 0;
	} else
		return 0;

	old_event = state->new_event;
	state->new_event = new_event;

	if (new_event)
		state->count++;

	if (time_before64(now, state->next_check) &&
			state->count != state->last_count)
		return 0;

	state->next_check = now + CHECK_INTERVAL;
	state->last_count = state->count;

	/* if we just entered the thermal event */
	if (new_event) {
		if (event == THERMAL_THROTTLING_EVENT)
			printk(KERN_CRIT "CPU%d: %s temperature above threshold, cpu clock throttled (total events = %lu)\n",
				this_cpu,
				level == CORE_LEVEL ? "Core" : "Package",
				state->count);
		else
			printk(KERN_CRIT "CPU%d: %s power limit notification (total events = %lu)\n",
				this_cpu,
				level == CORE_LEVEL ? "Core" : "Package",
				state->count);
		return 1;
	}
	if (old_event) {
		if (event == THERMAL_THROTTLING_EVENT)
			printk(KERN_INFO "CPU%d: %s temperature/speed normal\n",
				this_cpu,
				level == CORE_LEVEL ? "Core" : "Package");
		else
			printk(KERN_INFO "CPU%d: %s power limit normal\n",
				this_cpu,
				level == CORE_LEVEL ? "Core" : "Package");
		return 1;
	}

	return 0;
}

static int thresh_event_valid(int event)
{
	struct _thermal_state *state;
	unsigned int this_cpu = smp_processor_id();
	struct thermal_state *pstate = &per_cpu(thermal_state, this_cpu);
	u64 now = get_jiffies_64();

	state = (event == 0) ? &pstate->core_thresh0 : &pstate->core_thresh1;

	if (time_before64(now, state->next_check))
		return 0;

	state->next_check = now + CHECK_INTERVAL;
	return 1;
}

#ifdef CONFIG_SYSFS
/* Add/Remove thermal_throttle interface for CPU device: */
static __cpuinit int thermal_throttle_add_dev(struct sys_device *sys_dev,
				unsigned int cpu)
{
	int err;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	err = sysfs_create_group(&sys_dev->kobj, &thermal_attr_group);
	if (err)
		return err;

	if (cpu_has(c, X86_FEATURE_PLN))
		err = sysfs_add_file_to_group(&sys_dev->kobj,
					      &attr_core_power_limit_count.attr,
					      thermal_attr_group.name);
	if (cpu_has(c, X86_FEATURE_PTS)) {
		err = sysfs_add_file_to_group(&sys_dev->kobj,
					      &attr_package_throttle_count.attr,
					      thermal_attr_group.name);
		if (cpu_has(c, X86_FEATURE_PLN))
			err = sysfs_add_file_to_group(&sys_dev->kobj,
					&attr_package_power_limit_count.attr,
					thermal_attr_group.name);
	}

	return err;
}

static __cpuinit void thermal_throttle_remove_dev(struct sys_device *sys_dev)
{
	sysfs_remove_group(&sys_dev->kobj, &thermal_attr_group);
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
		err = thermal_throttle_add_dev(sys_dev, cpu);
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
	return notifier_from_errno(err);
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
		err = thermal_throttle_add_dev(get_cpu_sysdev(cpu), cpu);
		WARN_ON(err);
	}
#ifdef CONFIG_HOTPLUG_CPU
	mutex_unlock(&therm_cpu_lock);
#endif

	return 0;
}
device_initcall(thermal_throttle_init_device);

#endif /* CONFIG_SYSFS */

/*
 * Set up the most two significant bit to notify mce log that this thermal
 * event type.
 * This is a temp solution. May be changed in the future with mce log
 * infrasture.
 */
#define CORE_THROTTLED		(0)
#define CORE_POWER_LIMIT	((__u64)1 << 62)
#define PACKAGE_THROTTLED	((__u64)2 << 62)
#define PACKAGE_POWER_LIMIT	((__u64)3 << 62)

static void notify_thresholds(__u64 msr_val)
{
	/* check whether the interrupt handler is defined;
	 * otherwise simply return
	 */
	if (!platform_thermal_notify)
		return;

	/* lower threshold reached */
	if ((msr_val & THERM_LOG_THRESHOLD0) &&	thresh_event_valid(0))
		platform_thermal_notify(msr_val);
	/* higher threshold reached */
	if ((msr_val & THERM_LOG_THRESHOLD1) && thresh_event_valid(1))
		platform_thermal_notify(msr_val);
}

/* Thermal transition interrupt handler */
static void intel_thermal_interrupt(void)
{
	__u64 msr_val;

	rdmsrl(MSR_IA32_THERM_STATUS, msr_val);

	/* Check for violation of core thermal thresholds*/
	notify_thresholds(msr_val);

	if (therm_throt_process(msr_val & THERM_STATUS_PROCHOT,
				THERMAL_THROTTLING_EVENT,
				CORE_LEVEL) != 0)
		mce_log_therm_throt_event(CORE_THROTTLED | msr_val);

	if (this_cpu_has(X86_FEATURE_PLN))
		if (therm_throt_process(msr_val & THERM_STATUS_POWER_LIMIT,
					POWER_LIMIT_EVENT,
					CORE_LEVEL) != 0)
			mce_log_therm_throt_event(CORE_POWER_LIMIT | msr_val);

	if (this_cpu_has(X86_FEATURE_PTS)) {
		rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr_val);
		if (therm_throt_process(msr_val & PACKAGE_THERM_STATUS_PROCHOT,
					THERMAL_THROTTLING_EVENT,
					PACKAGE_LEVEL) != 0)
			mce_log_therm_throt_event(PACKAGE_THROTTLED | msr_val);
		if (this_cpu_has(X86_FEATURE_PLN))
			if (therm_throt_process(msr_val &
					PACKAGE_THERM_STATUS_POWER_LIMIT,
					POWER_LIMIT_EVENT,
					PACKAGE_LEVEL) != 0)
				mce_log_therm_throt_event(PACKAGE_POWER_LIMIT
							  | msr_val);
	}
}

static void unexpected_thermal_interrupt(void)
{
	printk(KERN_ERR "CPU%d: Unexpected LVT thermal interrupt!\n",
			smp_processor_id());
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

/* Thermal monitoring depends on APIC, ACPI and clock modulation */
static int intel_thermal_supported(struct cpuinfo_x86 *c)
{
	if (!cpu_has_apic)
		return 0;
	if (!cpu_has(c, X86_FEATURE_ACPI) || !cpu_has(c, X86_FEATURE_ACC))
		return 0;
	return 1;
}

void __init mcheck_intel_therm_init(void)
{
	/*
	 * This function is only called on boot CPU. Save the init thermal
	 * LVT value on BSP and use that value to restore APs' thermal LVT
	 * entry BIOS programmed later
	 */
	if (intel_thermal_supported(&boot_cpu_data))
		lvtthmr_init = apic_read(APIC_LVTTHMR);
}

void intel_init_thermal(struct cpuinfo_x86 *c)
{
	unsigned int cpu = smp_processor_id();
	int tm2 = 0;
	u32 l, h;

	if (!intel_thermal_supported(c))
		return;

	/*
	 * First check if its enabled already, in which case there might
	 * be some SMM goo which handles it, so we can't even put a handler
	 * since it might be delivered via SMI already:
	 */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);

	h = lvtthmr_init;
	/*
	 * The initial value of thermal LVT entries on all APs always reads
	 * 0x10000 because APs are woken up by BSP issuing INIT-SIPI-SIPI
	 * sequence to them and LVT registers are reset to 0s except for
	 * the mask bits which are set to 1s when APs receive INIT IPI.
	 * If BIOS takes over the thermal interrupt and sets its interrupt
	 * delivery mode to SMI (not fixed), it restores the value that the
	 * BIOS has programmed on AP based on BSP's info we saved since BIOS
	 * is always setting the same value for all threads/cores.
	 */
	if ((h & APIC_DM_FIXED_MASK) != APIC_DM_FIXED)
		apic_write(APIC_LVTTHMR, lvtthmr_init);


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
	if (cpu_has(c, X86_FEATURE_PLN))
		wrmsr(MSR_IA32_THERM_INTERRUPT,
		      l | (THERM_INT_LOW_ENABLE
			| THERM_INT_HIGH_ENABLE | THERM_INT_PLN_ENABLE), h);
	else
		wrmsr(MSR_IA32_THERM_INTERRUPT,
		      l | (THERM_INT_LOW_ENABLE | THERM_INT_HIGH_ENABLE), h);

	if (cpu_has(c, X86_FEATURE_PTS)) {
		rdmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
		if (cpu_has(c, X86_FEATURE_PLN))
			wrmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT,
			      l | (PACKAGE_THERM_INT_LOW_ENABLE
				| PACKAGE_THERM_INT_HIGH_ENABLE
				| PACKAGE_THERM_INT_PLN_ENABLE), h);
		else
			wrmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT,
			      l | (PACKAGE_THERM_INT_LOW_ENABLE
				| PACKAGE_THERM_INT_HIGH_ENABLE), h);
	}

	smp_thermal_vector = intel_thermal_interrupt;

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | MSR_IA32_MISC_ENABLE_TM1, h);

	/* Unmask the thermal vector: */
	l = apic_read(APIC_LVTTHMR);
	apic_write(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);

	printk_once(KERN_INFO "CPU0: Thermal monitoring enabled (%s)\n",
		       tm2 ? "TM2" : "TM1");

	/* enable thermal throttle processing */
	atomic_set(&therm_throt_en, 1);
}
