/*
 * x86_pkg_temp_thermal driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.
 *
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
#include <asm/mce.h>

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
/* Limit number of package temp zones */
#define MAX_PKG_TEMP_ZONE_IDS	256

struct pkg_device {
	struct list_head		list;
	u16				phys_proc_id;
	u16				cpu;
	u32				tj_max;
	u32				msr_pkg_therm_low;
	u32				msr_pkg_therm_high;
	struct thermal_zone_device	*tzone;
	struct cpumask			cpumask;
};

static struct thermal_zone_params pkg_temp_tz_params = {
	.no_hwmon	= true,
};

/* List maintaining number of package instances */
static LIST_HEAD(phy_dev_list);
/* Serializes interrupt notification, work and hotplug */
static DEFINE_SPINLOCK(pkg_temp_lock);
/* Protects zone operation in the work function against hotplug removal */
static DEFINE_MUTEX(thermal_zone_mutex);

/* Interrupt to work function schedule queue */
static DEFINE_PER_CPU(struct delayed_work, pkg_temp_thermal_threshold_work);

/* To track if the work is already scheduled on a package */
static u8 *pkg_work_scheduled;

static u16 max_phy_id;

/* Debug counters to show using debugfs */
static struct dentry *debugfs;
static unsigned int pkg_interrupt_cnt;
static unsigned int pkg_work_cnt;

static int pkg_temp_debugfs_init(void)
{
	struct dentry *d;

	debugfs = debugfs_create_dir("pkg_temp_thermal", NULL);
	if (!debugfs)
		return -ENOENT;

	d = debugfs_create_u32("pkg_thres_interrupt", S_IRUGO, debugfs,
				(u32 *)&pkg_interrupt_cnt);
	if (!d)
		goto err_out;

	d = debugfs_create_u32("pkg_thres_work", S_IRUGO, debugfs,
				(u32 *)&pkg_work_cnt);
	if (!d)
		goto err_out;

	return 0;

err_out:
	debugfs_remove_recursive(debugfs);
	return -ENOENT;
}

/*
 * Protection:
 *
 * - cpu hotplug: Read serialized by cpu hotplug lock
 *		  Write must hold pkg_temp_lock
 *
 * - Other callsites: Must hold pkg_temp_lock
 */
static struct pkg_device *pkg_temp_thermal_get_dev(unsigned int cpu)
{
	u16 phys_proc_id = topology_physical_package_id(cpu);
	struct pkg_device *pkgdev;

	list_for_each_entry(pkgdev, &phy_dev_list, list) {
		if (pkgdev->phys_proc_id == phys_proc_id)
			return pkgdev;
	}
	return NULL;
}

/*
* tj-max is is interesting because threshold is set relative to this
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
	struct pkg_device *pkgdev = tzd->devdata;
	u32 eax, edx;

	rdmsr_on_cpu(pkgdev->cpu, MSR_IA32_PACKAGE_THERM_STATUS, &eax, &edx);
	if (eax & 0x80000000) {
		*temp = pkgdev->tj_max - ((eax >> 16) & 0x7f) * 1000;
		pr_debug("sys_get_curr_temp %d\n", *temp);
		return 0;
	}
	return -EINVAL;
}

static int sys_get_trip_temp(struct thermal_zone_device *tzd,
			     int trip, int *temp)
{
	struct pkg_device *pkgdev = tzd->devdata;
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

	ret = rdmsr_on_cpu(pkgdev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			   &eax, &edx);
	if (ret < 0)
		return ret;

	thres_reg_value = (eax & mask) >> shift;
	if (thres_reg_value)
		*temp = pkgdev->tj_max - thres_reg_value * 1000;
	else
		*temp = 0;
	pr_debug("sys_get_trip_temp %d\n", *temp);

	return 0;
}

static int
sys_set_trip_temp(struct thermal_zone_device *tzd, int trip, int temp)
{
	struct pkg_device *pkgdev = tzd->devdata;
	u32 l, h, mask, shift, intr;
	int ret;

	if (trip >= MAX_NUMBER_OF_TRIPS || temp >= pkgdev->tj_max)
		return -EINVAL;

	ret = rdmsr_on_cpu(pkgdev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
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
		l |= (pkgdev->tj_max - temp)/1000 << shift;
		l |= intr;
	}

	return wrmsr_on_cpu(pkgdev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
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
	int phy_id, cpu = smp_processor_id();
	struct pkg_device *pkgdev;
	u64 msr_val, wr_val;

	mutex_lock(&thermal_zone_mutex);
	spin_lock_irq(&pkg_temp_lock);
	++pkg_work_cnt;

	pkgdev = pkg_temp_thermal_get_dev(cpu);
	if (!pkgdev) {
		spin_unlock_irq(&pkg_temp_lock);
		mutex_unlock(&thermal_zone_mutex);
		return;
	}

	pkg_work_scheduled[phy_id] = 0;

	rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr_val);
	wr_val = msr_val & ~(THERM_LOG_THRESHOLD0 | THERM_LOG_THRESHOLD1);
	if (wr_val != msr_val) {
		wrmsrl(MSR_IA32_PACKAGE_THERM_STATUS, wr_val);
		tzone = pkgdev->tzone;
	}

	enable_pkg_thres_interrupt();
	spin_unlock_irq(&pkg_temp_lock);

	/*
	 * If tzone is not NULL, then thermal_zone_mutex will prevent the
	 * concurrent removal in the cpu offline callback.
	 */
	if (tzone)
		thermal_zone_device_update(tzone, THERMAL_EVENT_UNSPECIFIED);

	mutex_unlock(&thermal_zone_mutex);
}

static int pkg_thermal_notify(u64 msr_val)
{
	int cpu = smp_processor_id();
	int phy_id = topology_physical_package_id(cpu);
	struct pkg_device *pkgdev;
	unsigned long flags;

	spin_lock_irqsave(&pkg_temp_lock, flags);
	++pkg_interrupt_cnt;

	disable_pkg_thres_interrupt();

	/* Work is per package, so scheduling it once is enough. */
	pkgdev = pkg_temp_thermal_get_dev(cpu);
	if (pkgdev && pkg_work_scheduled && !pkg_work_scheduled[phy_id]) {
		pkg_work_scheduled[phy_id] = 1;
		schedule_delayed_work_on(cpu,
				&per_cpu(pkg_temp_thermal_threshold_work, cpu),
				msecs_to_jiffies(notify_delay_ms));
	}

	spin_unlock_irqrestore(&pkg_temp_lock, flags);
	return 0;
}

static int pkg_temp_thermal_device_add(unsigned int cpu)
{
	u32 tj_max, eax, ebx, ecx, edx;
	struct pkg_device *pkgdev;
	int thres_count, err;
	unsigned long flags;
	u8 *temp;

	cpuid(6, &eax, &ebx, &ecx, &edx);
	thres_count = ebx & 0x07;
	if (!thres_count)
		return -ENODEV;

	if (topology_physical_package_id(cpu) > MAX_PKG_TEMP_ZONE_IDS)
		return -ENODEV;

	thres_count = clamp_val(thres_count, 0, MAX_NUMBER_OF_TRIPS);

	err = get_tj_max(cpu, &tj_max);
	if (err)
		return err;

	pkgdev = kzalloc(sizeof(*pkgdev), GFP_KERNEL);
	if (!pkgdev)
		return -ENOMEM;

	spin_lock_irqsave(&pkg_temp_lock, flags);
	if (topology_physical_package_id(cpu) > max_phy_id)
		max_phy_id = topology_physical_package_id(cpu);
	temp = krealloc(pkg_work_scheduled,
			(max_phy_id+1) * sizeof(u8), GFP_ATOMIC);
	if (!temp) {
		spin_unlock_irqrestore(&pkg_temp_lock, flags);
		kfree(pkgdev);
		return -ENOMEM;
	}
	pkg_work_scheduled = temp;
	pkg_work_scheduled[topology_physical_package_id(cpu)] = 0;
	spin_unlock_irqrestore(&pkg_temp_lock, flags);

	pkgdev->phys_proc_id = topology_physical_package_id(cpu);
	pkgdev->cpu = cpu;
	pkgdev->tj_max = tj_max;
	pkgdev->tzone = thermal_zone_device_register("x86_pkg_temp",
			thres_count,
			(thres_count == MAX_NUMBER_OF_TRIPS) ? 0x03 : 0x01,
			pkgdev, &tzone_ops, &pkg_temp_tz_params, 0, 0);
	if (IS_ERR(pkgdev->tzone)) {
		err = PTR_ERR(pkgdev->tzone);
		kfree(pkgdev);
		return err;
	}
	/* Store MSR value for package thermal interrupt, to restore at exit */
	rdmsr_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
		     &pkgdev->msr_pkg_therm_low,
		     &pkgdev->msr_pkg_therm_high);

	cpumask_set_cpu(cpu, &pkgdev->cpumask);
	spin_lock_irq(&pkg_temp_lock);
	list_add_tail(&pkgdev->list, &phy_dev_list);
	spin_unlock_irq(&pkg_temp_lock);
	return 0;
}

static void put_core_offline(unsigned int cpu)
{
	struct pkg_device *pkgdev = pkg_temp_thermal_get_dev(cpu);
	bool lastcpu;
	int target;

	if (!pkgdev)
		return;

	target = cpumask_any_but(&pkgdev->cpumask, cpu);
	cpumask_clear_cpu(cpu, &pkgdev->cpumask);
	lastcpu = target >= nr_cpu_ids;
	/*
	 * Remove the sysfs files, if this is the last cpu in the package
	 * before doing further cleanups.
	 */
	if (lastcpu) {
		struct thermal_zone_device *tzone = pkgdev->tzone;

		/*
		 * We must protect against a work function calling
		 * thermal_zone_update, after/while unregister. We null out
		 * the pointer under the zone mutex, so the worker function
		 * won't try to call.
		 */
		mutex_lock(&thermal_zone_mutex);
		pkgdev->tzone = NULL;
		mutex_unlock(&thermal_zone_mutex);

		thermal_zone_device_unregister(tzone);
	}

	/*
	 * If this is the last CPU in the package, restore the interrupt
	 * MSR and remove the package reference from the array.
	 */
	if (lastcpu) {
		/* Protect against work and interrupts */
		spin_lock_irq(&pkg_temp_lock);
		list_del(&pkgdev->list);
		/*
		 * After this point nothing touches the MSR anymore. We
		 * must drop the lock to make the cross cpu call. This goes
		 * away once we move that code to the hotplug state machine.
		 */
		spin_unlock_irq(&pkg_temp_lock);
		wrmsr_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			     pkgdev->msr_pkg_therm_low,
			     pkgdev->msr_pkg_therm_high);
		kfree(pkgdev);
	}

	/*
	 * Note, this is broken when work was really scheduled on the
	 * outgoing cpu because this will leave the work_scheduled flag set
	 * and the thermal interrupts disabled. Will be fixed in the next
	 * step as there is no way to fix it in a sane way with the per cpu
	 * work nonsense.
	 */
	cancel_delayed_work_sync(&per_cpu(pkg_temp_thermal_threshold_work, cpu));
}

static int get_core_online(unsigned int cpu)
{
	struct pkg_device *pkgdev = pkg_temp_thermal_get_dev(cpu);
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	/* Paranoia check */
	if (!cpu_has(c, X86_FEATURE_DTHERM) || !cpu_has(c, X86_FEATURE_PTS))
		return -ENODEV;

	INIT_DELAYED_WORK(&per_cpu(pkg_temp_thermal_threshold_work, cpu),
			  pkg_temp_thermal_threshold_work_fn);

	/* If the package exists, nothing to do */
	if (pkgdev) {
		cpumask_set_cpu(cpu, &pkgdev->cpumask);
		return 0;
	}
	return pkg_temp_thermal_device_add(cpu);
}

static int pkg_temp_thermal_cpu_callback(struct notifier_block *nfb,
				 unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		get_core_online(cpu);
		break;
	case CPU_DOWN_PREPARE:
		put_core_offline(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block pkg_temp_thermal_notifier __refdata = {
	.notifier_call = pkg_temp_thermal_cpu_callback,
};

static const struct x86_cpu_id __initconst pkg_temp_thermal_ids[] = {
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, X86_FEATURE_PTS },
	{}
};
MODULE_DEVICE_TABLE(x86cpu, pkg_temp_thermal_ids);

static int __init pkg_temp_thermal_init(void)
{
	int i;

	if (!x86_match_cpu(pkg_temp_thermal_ids))
		return -ENODEV;

	cpu_notifier_register_begin();
	for_each_online_cpu(i)
		if (get_core_online(i))
			goto err_ret;
	__register_hotcpu_notifier(&pkg_temp_thermal_notifier);
	cpu_notifier_register_done();

	platform_thermal_package_notify = pkg_thermal_notify;
	platform_thermal_package_rate_control = pkg_thermal_rate_control;

	 /* Don't care if it fails */
	pkg_temp_debugfs_init();
	return 0;

err_ret:
	for_each_online_cpu(i)
		put_core_offline(i);
	cpu_notifier_register_done();
	kfree(pkg_work_scheduled);
	return -ENODEV;
}
module_init(pkg_temp_thermal_init)

static void __exit pkg_temp_thermal_exit(void)
{
	int i;

	platform_thermal_package_notify = NULL;
	platform_thermal_package_rate_control = NULL;

	cpu_notifier_register_begin();
	__unregister_hotcpu_notifier(&pkg_temp_thermal_notifier);
	for_each_online_cpu(i)
		put_core_offline(i);
	cpu_notifier_register_done();

	kfree(pkg_work_scheduled);

	debugfs_remove_recursive(debugfs);
}
module_exit(pkg_temp_thermal_exit)

MODULE_DESCRIPTION("X86 PKG TEMP Thermal Driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
