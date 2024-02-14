// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86_pkg_temp_thermal driver
 * Copyright (c) 2013, Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/param.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/thermal.h>
#include <linux/debugfs.h>

#include <asm/cpu_device_id.h>

#include "thermal_interrupt.h"

/*
* Rate control delay: Idea is to introduce denounce effect
* This should be long enough to avoid reduce events, when
* threshold is set to a temperature, which is constantly
* violated, but at the short enough to take any action.
* The action can be remove threshold or change it to next
* interesting setting. Based on experiments, in around
* every 5 seconds under load will give us a significant
* temperature change.
*/
#define PKG_TEMP_THERMAL_NOTIFY_DELAY	5000
static int notify_delay_ms = PKG_TEMP_THERMAL_NOTIFY_DELAY;
module_param(notify_delay_ms, int, 0644);
MODULE_PARM_DESC(notify_delay_ms,
	"User space notification delay in milli seconds.");

/* Number of trip points in thermal zone. Currently it can't
* be more than 2. MSR can allow setting and getting notifications
* for only 2 thresholds. This define enforces this, if there
* is some wrong values returned by cpuid for number of thresholds.
*/
#define MAX_NUMBER_OF_TRIPS	2

struct zone_device {
	int				cpu;
	bool				work_scheduled;
	u32				tj_max;
	u32				msr_pkg_therm_low;
	u32				msr_pkg_therm_high;
	struct delayed_work		work;
	struct thermal_zone_device	*tzone;
	struct cpumask			cpumask;
};

static struct thermal_zone_params pkg_temp_tz_params = {
	.no_hwmon	= true,
};

/* Keep track of how many zone pointers we allocated in init() */
static int max_id __read_mostly;
/* Array of zone pointers */
static struct zone_device **zones;
/* Serializes interrupt notification, work and hotplug */
static DEFINE_RAW_SPINLOCK(pkg_temp_lock);
/* Protects zone operation in the work function against hotplug removal */
static DEFINE_MUTEX(thermal_zone_mutex);

/* The dynamically assigned cpu hotplug state for module_exit() */
static enum cpuhp_state pkg_thermal_hp_state __read_mostly;

/* Debug counters to show using debugfs */
static struct dentry *debugfs;
static unsigned int pkg_interrupt_cnt;
static unsigned int pkg_work_cnt;

static void pkg_temp_debugfs_init(void)
{
	debugfs = debugfs_create_dir("pkg_temp_thermal", NULL);

	debugfs_create_u32("pkg_thres_interrupt", S_IRUGO, debugfs,
			   &pkg_interrupt_cnt);
	debugfs_create_u32("pkg_thres_work", S_IRUGO, debugfs,
			   &pkg_work_cnt);
}

/*
 * Protection:
 *
 * - cpu hotplug: Read serialized by cpu hotplug lock
 *		  Write must hold pkg_temp_lock
 *
 * - Other callsites: Must hold pkg_temp_lock
 */
static struct zone_device *pkg_temp_thermal_get_dev(unsigned int cpu)
{
	int id = topology_logical_die_id(cpu);

	if (id >= 0 && id < max_id)
		return zones[id];
	return NULL;
}

/*
* tj-max is interesting because threshold is set relative to this
* temperature.
*/
static int get_tj_max(int cpu, u32 *tj_max)
{
	u32 eax, edx, val;
	int err;

	err = rdmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, &eax, &edx);
	if (err)
		return err;

	val = (eax >> 16) & 0xff;
	*tj_max = val * 1000;

	return val ? 0 : -EINVAL;
}

static int sys_get_curr_temp(struct thermal_zone_device *tzd, int *temp)
{
	struct zone_device *zonedev = tzd->devdata;
	u32 eax, edx;

	rdmsr_on_cpu(zonedev->cpu, MSR_IA32_PACKAGE_THERM_STATUS,
			&eax, &edx);
	if (eax & 0x80000000) {
		*temp = zonedev->tj_max - ((eax >> 16) & 0x7f) * 1000;
		pr_debug("sys_get_curr_temp %d\n", *temp);
		return 0;
	}
	return -EINVAL;
}

static int sys_get_trip_temp(struct thermal_zone_device *tzd,
			     int trip, int *temp)
{
	struct zone_device *zonedev = tzd->devdata;
	unsigned long thres_reg_value;
	u32 mask, shift, eax, edx;
	int ret;

	if (trip >= MAX_NUMBER_OF_TRIPS)
		return -EINVAL;

	if (trip) {
		mask = THERM_MASK_THRESHOLD1;
		shift = THERM_SHIFT_THRESHOLD1;
	} else {
		mask = THERM_MASK_THRESHOLD0;
		shift = THERM_SHIFT_THRESHOLD0;
	}

	ret = rdmsr_on_cpu(zonedev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			   &eax, &edx);
	if (ret < 0)
		return ret;

	thres_reg_value = (eax & mask) >> shift;
	if (thres_reg_value)
		*temp = zonedev->tj_max - thres_reg_value * 1000;
	else
		*temp = THERMAL_TEMP_INVALID;
	pr_debug("sys_get_trip_temp %d\n", *temp);

	return 0;
}

static int
sys_set_trip_temp(struct thermal_zone_device *tzd, int trip, int temp)
{
	struct zone_device *zonedev = tzd->devdata;
	u32 l, h, mask, shift, intr;
	int ret;

	if (trip >= MAX_NUMBER_OF_TRIPS || temp >= zonedev->tj_max)
		return -EINVAL;

	ret = rdmsr_on_cpu(zonedev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			   &l, &h);
	if (ret < 0)
		return ret;

	if (trip) {
		mask = THERM_MASK_THRESHOLD1;
		shift = THERM_SHIFT_THRESHOLD1;
		intr = THERM_INT_THRESHOLD1_ENABLE;
	} else {
		mask = THERM_MASK_THRESHOLD0;
		shift = THERM_SHIFT_THRESHOLD0;
		intr = THERM_INT_THRESHOLD0_ENABLE;
	}
	l &= ~mask;
	/*
	* When users space sets a trip temperature == 0, which is indication
	* that, it is no longer interested in receiving notifications.
	*/
	if (!temp) {
		l &= ~intr;
	} else {
		l |= (zonedev->tj_max - temp)/1000 << shift;
		l |= intr;
	}

	return wrmsr_on_cpu(zonedev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			l, h);
}

static int sys_get_trip_type(struct thermal_zone_device *thermal, int trip,
			     enum thermal_trip_type *type)
{
	*type = THERMAL_TRIP_PASSIVE;
	return 0;
}

/* Thermal zone callback registry */
static struct thermal_zone_device_ops tzone_ops = {
	.get_temp = sys_get_curr_temp,
	.get_trip_temp = sys_get_trip_temp,
	.get_trip_type = sys_get_trip_type,
	.set_trip_temp = sys_set_trip_temp,
};

static bool pkg_thermal_rate_control(void)
{
	return true;
}

/* Enable threshold interrupt on local package/cpu */
static inline void enable_pkg_thres_interrupt(void)
{
	u8 thres_0, thres_1;
	u32 l, h;

	rdmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
	/* only enable/disable if it had valid threshold value */
	thres_0 = (l & THERM_MASK_THRESHOLD0) >> THERM_SHIFT_THRESHOLD0;
	thres_1 = (l & THERM_MASK_THRESHOLD1) >> THERM_SHIFT_THRESHOLD1;
	if (thres_0)
		l |= THERM_INT_THRESHOLD0_ENABLE;
	if (thres_1)
		l |= THERM_INT_THRESHOLD1_ENABLE;
	wrmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
}

/* Disable threshold interrupt on local package/cpu */
static inline void disable_pkg_thres_interrupt(void)
{
	u32 l, h;

	rdmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);

	l &= ~(THERM_INT_THRESHOLD0_ENABLE | THERM_INT_THRESHOLD1_ENABLE);
	wrmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
}

static void pkg_temp_thermal_threshold_work_fn(struct work_struct *work)
{
	struct thermal_zone_device *tzone = NULL;
	int cpu = smp_processor_id();
	struct zone_device *zonedev;
	u64 msr_val, wr_val;

	mutex_lock(&thermal_zone_mutex);
	raw_spin_lock_irq(&pkg_temp_lock);
	++pkg_work_cnt;

	zonedev = pkg_temp_thermal_get_dev(cpu);
	if (!zonedev) {
		raw_spin_unlock_irq(&pkg_temp_lock);
		mutex_unlock(&thermal_zone_mutex);
		return;
	}
	zonedev->work_scheduled = false;

	rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr_val);
	wr_val = msr_val & ~(THERM_LOG_THRESHOLD0 | THERM_LOG_THRESHOLD1);
	if (wr_val != msr_val) {
		wrmsrl(MSR_IA32_PACKAGE_THERM_STATUS, wr_val);
		tzone = zonedev->tzone;
	}

	enable_pkg_thres_interrupt();
	raw_spin_unlock_irq(&pkg_temp_lock);

	/*
	 * If tzone is not NULL, then thermal_zone_mutex will prevent the
	 * concurrent removal in the cpu offline callback.
	 */
	if (tzone)
		thermal_zone_device_update(tzone, THERMAL_EVENT_UNSPECIFIED);

	mutex_unlock(&thermal_zone_mutex);
}

static void pkg_thermal_schedule_work(int cpu, struct delayed_work *work)
{
	unsigned long ms = msecs_to_jiffies(notify_delay_ms);

	schedule_delayed_work_on(cpu, work, ms);
}

static int pkg_thermal_notify(u64 msr_val)
{
	int cpu = smp_processor_id();
	struct zone_device *zonedev;
	unsigned long flags;

	raw_spin_lock_irqsave(&pkg_temp_lock, flags);
	++pkg_interrupt_cnt;

	disable_pkg_thres_interrupt();

	/* Work is per package, so scheduling it once is enough. */
	zonedev = pkg_temp_thermal_get_dev(cpu);
	if (zonedev && !zonedev->work_scheduled) {
		zonedev->work_scheduled = true;
		pkg_thermal_schedule_work(zonedev->cpu, &zonedev->work);
	}

	raw_spin_unlock_irqrestore(&pkg_temp_lock, flags);
	return 0;
}

static int pkg_temp_thermal_device_add(unsigned int cpu)
{
	int id = topology_logical_die_id(cpu);
	u32 tj_max, eax, ebx, ecx, edx;
	struct zone_device *zonedev;
	int thres_count, err;

	if (id >= max_id)
		return -ENOMEM;

	cpuid(6, &eax, &ebx, &ecx, &edx);
	thres_count = ebx & 0x07;
	if (!thres_count)
		return -ENODEV;

	thres_count = clamp_val(thres_count, 0, MAX_NUMBER_OF_TRIPS);

	err = get_tj_max(cpu, &tj_max);
	if (err)
		return err;

	zonedev = kzalloc(sizeof(*zonedev), GFP_KERNEL);
	if (!zonedev)
		return -ENOMEM;

	INIT_DELAYED_WORK(&zonedev->work, pkg_temp_thermal_threshold_work_fn);
	zonedev->cpu = cpu;
	zonedev->tj_max = tj_max;
	zonedev->tzone = thermal_zone_device_register("x86_pkg_temp",
			thres_count,
			(thres_count == MAX_NUMBER_OF_TRIPS) ? 0x03 : 0x01,
			zonedev, &tzone_ops, &pkg_temp_tz_params, 0, 0);
	if (IS_ERR(zonedev->tzone)) {
		err = PTR_ERR(zonedev->tzone);
		kfree(zonedev);
		return err;
	}
	err = thermal_zone_device_enable(zonedev->tzone);
	if (err) {
		thermal_zone_device_unregister(zonedev->tzone);
		kfree(zonedev);
		return err;
	}
	/* Store MSR value for package thermal interrupt, to restore at exit */
	rdmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT, zonedev->msr_pkg_therm_low,
	      zonedev->msr_pkg_therm_high);

	cpumask_set_cpu(cpu, &zonedev->cpumask);
	raw_spin_lock_irq(&pkg_temp_lock);
	zones[id] = zonedev;
	raw_spin_unlock_irq(&pkg_temp_lock);
	return 0;
}

static int pkg_thermal_cpu_offline(unsigned int cpu)
{
	struct zone_device *zonedev = pkg_temp_thermal_get_dev(cpu);
	bool lastcpu, was_target;
	int target;

	if (!zonedev)
		return 0;

	target = cpumask_any_but(&zonedev->cpumask, cpu);
	cpumask_clear_cpu(cpu, &zonedev->cpumask);
	lastcpu = target >= nr_cpu_ids;
	/*
	 * Remove the sysfs files, if this is the last cpu in the package
	 * before doing further cleanups.
	 */
	if (lastcpu) {
		struct thermal_zone_device *tzone = zonedev->tzone;

		/*
		 * We must protect against a work function calling
		 * thermal_zone_update, after/while unregister. We null out
		 * the pointer under the zone mutex, so the worker function
		 * won't try to call.
		 */
		mutex_lock(&thermal_zone_mutex);
		zonedev->tzone = NULL;
		mutex_unlock(&thermal_zone_mutex);

		thermal_zone_device_unregister(tzone);
	}

	/* Protect against work and interrupts */
	raw_spin_lock_irq(&pkg_temp_lock);

	/*
	 * Check whether this cpu was the current target and store the new
	 * one. When we drop the lock, then the interrupt notify function
	 * will see the new target.
	 */
	was_target = zonedev->cpu == cpu;
	zonedev->cpu = target;

	/*
	 * If this is the last CPU in the package remove the package
	 * reference from the array and restore the interrupt MSR. When we
	 * drop the lock neither the interrupt notify function nor the
	 * worker will see the package anymore.
	 */
	if (lastcpu) {
		zones[topology_logical_die_id(cpu)] = NULL;
		/* After this point nothing touches the MSR anymore. */
		wrmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT,
		      zonedev->msr_pkg_therm_low, zonedev->msr_pkg_therm_high);
	}

	/*
	 * Check whether there is work scheduled and whether the work is
	 * targeted at the outgoing CPU.
	 */
	if (zonedev->work_scheduled && was_target) {
		/*
		 * To cancel the work we need to drop the lock, otherwise
		 * we might deadlock if the work needs to be flushed.
		 */
		raw_spin_unlock_irq(&pkg_temp_lock);
		cancel_delayed_work_sync(&zonedev->work);
		raw_spin_lock_irq(&pkg_temp_lock);
		/*
		 * If this is not the last cpu in the package and the work
		 * did not run after we dropped the lock above, then we
		 * need to reschedule the work, otherwise the interrupt
		 * stays disabled forever.
		 */
		if (!lastcpu && zonedev->work_scheduled)
			pkg_thermal_schedule_work(target, &zonedev->work);
	}

	raw_spin_unlock_irq(&pkg_temp_lock);

	/* Final cleanup if this is the last cpu */
	if (lastcpu)
		kfree(zonedev);
	return 0;
}

static int pkg_thermal_cpu_online(unsigned int cpu)
{
	struct zone_device *zonedev = pkg_temp_thermal_get_dev(cpu);
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	/* Paranoia check */
	if (!cpu_has(c, X86_FEATURE_DTHERM) || !cpu_has(c, X86_FEATURE_PTS))
		return -ENODEV;

	/* If the package exists, nothing to do */
	if (zonedev) {
		cpumask_set_cpu(cpu, &zonedev->cpumask);
		return 0;
	}
	return pkg_temp_thermal_device_add(cpu);
}

static const struct x86_cpu_id __initconst pkg_temp_thermal_ids[] = {
	X86_MATCH_VENDOR_FEATURE(INTEL, X86_FEATURE_PTS, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, pkg_temp_thermal_ids);

static int __init pkg_temp_thermal_init(void)
{
	int ret;

	if (!x86_match_cpu(pkg_temp_thermal_ids))
		return -ENODEV;

	max_id = topology_max_packages() * topology_max_die_per_package();
	zones = kcalloc(max_id, sizeof(struct zone_device *),
			   GFP_KERNEL);
	if (!zones)
		return -ENOMEM;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "thermal/x86_pkg:online",
				pkg_thermal_cpu_online,	pkg_thermal_cpu_offline);
	if (ret < 0)
		goto err;

	/* Store the state for module exit */
	pkg_thermal_hp_state = ret;

	platform_thermal_package_notify = pkg_thermal_notify;
	platform_thermal_package_rate_control = pkg_thermal_rate_control;

	 /* Don't care if it fails */
	pkg_temp_debugfs_init();
	return 0;

err:
	kfree(zones);
	return ret;
}
module_init(pkg_temp_thermal_init)

static void __exit pkg_temp_thermal_exit(void)
{
	platform_thermal_package_notify = NULL;
	platform_thermal_package_rate_control = NULL;

	cpuhp_remove_state(pkg_thermal_hp_state);
	debugfs_remove_recursive(debugfs);
	kfree(zones);
}
module_exit(pkg_temp_thermal_exit)

MODULE_DESCRIPTION("X86 PKG TEMP Thermal Driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
