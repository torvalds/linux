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
};

static struct thermal_zone_params pkg_temp_tz_params = {
	.no_hwmon	= true,
};

/* List maintaining number of package instances */
static LIST_HEAD(phy_dev_list);
static DEFINE_MUTEX(phy_dev_list_mutex);

/* Interrupt to work function schedule queue */
static DEFINE_PER_CPU(struct delayed_work, pkg_temp_thermal_threshold_work);

/* To track if the work is already scheduled on a package */
static u8 *pkg_work_scheduled;

/* Spin lock to prevent races with pkg_work_scheduled */
static spinlock_t pkg_work_lock;
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

static struct pkg_device *pkg_temp_thermal_get_dev(unsigned int cpu)
{
	u16 phys_proc_id = topology_physical_package_id(cpu);
	struct pkg_device *pkgdev;

	mutex_lock(&phy_dev_list_mutex);

	list_for_each_entry(pkgdev, &phy_dev_list, list)
		if (pkgdev->phys_proc_id == phys_proc_id) {
			mutex_unlock(&phy_dev_list_mutex);
			return pkgdev;
		}

	mutex_unlock(&phy_dev_list_mutex);

	return NULL;
}

/*
* tj-max is is interesting because threshold is set relative to this
* temperature.
*/
static int get_tj_max(int cpu, u32 *tj_max)
{
	u32 eax, edx;
	u32 val;
	int err;

	err = rdmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, &eax, &edx);
	if (err)
		goto err_ret;
	else {
		val = (eax >> 16) & 0xff;
		if (val)
			*tj_max = val * 1000;
		else {
			err = -EINVAL;
			goto err_ret;
		}
	}

	return 0;
err_ret:
	*tj_max = 0;
	return err;
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
		return -EINVAL;

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
		return -EINVAL;

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
	u32 l, h;
	u8 thres_0, thres_1;

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
	wrmsr(MSR_IA32_PACKAGE_THERM_INTERRUPT,
			l & (~THERM_INT_THRESHOLD0_ENABLE) &
				(~THERM_INT_THRESHOLD1_ENABLE), h);
}

static void pkg_temp_thermal_threshold_work_fn(struct work_struct *work)
{
	__u64 msr_val;
	int cpu = smp_processor_id();
	int phy_id = topology_physical_package_id(cpu);
	struct pkg_device *pkgdev = pkg_temp_thermal_get_dev(cpu);
	bool notify = false;
	unsigned long flags;

	if (!pkgdev)
		return;

	spin_lock_irqsave(&pkg_work_lock, flags);
	++pkg_work_cnt;
	if (unlikely(phy_id > max_phy_id)) {
		spin_unlock_irqrestore(&pkg_work_lock, flags);
		return;
	}
	pkg_work_scheduled[phy_id] = 0;
	spin_unlock_irqrestore(&pkg_work_lock, flags);

	rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr_val);
	if (msr_val & THERM_LOG_THRESHOLD0) {
		wrmsrl(MSR_IA32_PACKAGE_THERM_STATUS,
				msr_val & ~THERM_LOG_THRESHOLD0);
		notify = true;
	}
	if (msr_val & THERM_LOG_THRESHOLD1) {
		wrmsrl(MSR_IA32_PACKAGE_THERM_STATUS,
				msr_val & ~THERM_LOG_THRESHOLD1);
		notify = true;
	}

	enable_pkg_thres_interrupt();

	if (notify) {
		pr_debug("thermal_zone_device_update\n");
		thermal_zone_device_update(pkgdev->tzone,
					   THERMAL_EVENT_UNSPECIFIED);
	}
}

static int pkg_thermal_notify(__u64 msr_val)
{
	unsigned long flags;
	int cpu = smp_processor_id();
	int phy_id = topology_physical_package_id(cpu);

	/*
	* When a package is in interrupted state, all CPU's in that package
	* are in the same interrupt state. So scheduling on any one CPU in
	* the package is enough and simply return for others.
	*/
	spin_lock_irqsave(&pkg_work_lock, flags);
	++pkg_interrupt_cnt;
	if (unlikely(phy_id > max_phy_id) || unlikely(!pkg_work_scheduled) ||
			pkg_work_scheduled[phy_id]) {
		disable_pkg_thres_interrupt();
		spin_unlock_irqrestore(&pkg_work_lock, flags);
		return -EINVAL;
	}
	pkg_work_scheduled[phy_id] = 1;
	spin_unlock_irqrestore(&pkg_work_lock, flags);

	disable_pkg_thres_interrupt();
	schedule_delayed_work_on(cpu,
				&per_cpu(pkg_temp_thermal_threshold_work, cpu),
				msecs_to_jiffies(notify_delay_ms));
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
		goto err_ret;

	mutex_lock(&phy_dev_list_mutex);

	pkgdev = kzalloc(sizeof(*pkgdev), GFP_KERNEL);
	if (!pkgdev) {
		err = -ENOMEM;
		goto err_ret_unlock;
	}

	spin_lock_irqsave(&pkg_work_lock, flags);
	if (topology_physical_package_id(cpu) > max_phy_id)
		max_phy_id = topology_physical_package_id(cpu);
	temp = krealloc(pkg_work_scheduled,
			(max_phy_id+1) * sizeof(u8), GFP_ATOMIC);
	if (!temp) {
		spin_unlock_irqrestore(&pkg_work_lock, flags);
		err = -ENOMEM;
		goto err_ret_free;
	}
	pkg_work_scheduled = temp;
	pkg_work_scheduled[topology_physical_package_id(cpu)] = 0;
	spin_unlock_irqrestore(&pkg_work_lock, flags);

	pkgdev->phys_proc_id = topology_physical_package_id(cpu);
	pkgdev->cpu = cpu;
	pkgdev->tj_max = tj_max;
	pkgdev->tzone = thermal_zone_device_register("x86_pkg_temp",
			thres_count,
			(thres_count == MAX_NUMBER_OF_TRIPS) ? 0x03 : 0x01,
			pkgdev, &tzone_ops, &pkg_temp_tz_params, 0, 0);
	if (IS_ERR(pkgdev->tzone)) {
		err = PTR_ERR(pkgdev->tzone);
		goto err_ret_free;
	}
	/* Store MSR value for package thermal interrupt, to restore at exit */
	rdmsr_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
		     &pkgdev->msr_pkg_therm_low,
		     &pkgdev->msr_pkg_therm_high);

	list_add_tail(&pkgdev->list, &phy_dev_list);
	pr_debug("pkg_temp_thermal_device_add :phy_id %d cpu %d\n",
		 pkgdev->phys_proc_id, cpu);

	mutex_unlock(&phy_dev_list_mutex);

	return 0;

err_ret_free:
	kfree(pkgdev);
err_ret_unlock:
	mutex_unlock(&phy_dev_list_mutex);

err_ret:
	return err;
}

static int pkg_temp_thermal_device_remove(unsigned int cpu)
{
	struct pkg_device *pkgdev = pkg_temp_thermal_get_dev(cpu);
	int target;

	if (!pkgdev)
		return -ENODEV;

	mutex_lock(&phy_dev_list_mutex);

	target = cpumask_any_but(topology_core_cpumask(cpu), cpu);
	/* This might be the last cpu in this package */
	if (target >= nr_cpu_ids) {
		thermal_zone_device_unregister(pkgdev->tzone);
		/*
		 * Restore original MSR value for package thermal
		 * interrupt.
		 */
		wrmsr_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			     pkgdev->msr_pkg_therm_low,
			     pkgdev->msr_pkg_therm_high);
		list_del(&pkgdev->list);
		kfree(pkgdev);
	} else if (pkgdev->cpu == cpu) {
		pkgdev->cpu = target;
	}

	mutex_unlock(&phy_dev_list_mutex);

	return 0;
}

static int get_core_online(unsigned int cpu)
{
	struct pkg_device *pkgdev = pkg_temp_thermal_get_dev(cpu);
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	/* Check if there is already an instance for this package */
	if (!pkgdev) {
		if (!cpu_has(c, X86_FEATURE_DTHERM) ||
					!cpu_has(c, X86_FEATURE_PTS))
			return -ENODEV;
		if (pkg_temp_thermal_device_add(cpu))
			return -ENODEV;
	}

	INIT_DELAYED_WORK(&per_cpu(pkg_temp_thermal_threshold_work, cpu),
			  pkg_temp_thermal_threshold_work_fn);

	pr_debug("get_core_online: cpu %d successful\n", cpu);

	return 0;
}

static void put_core_offline(unsigned int cpu)
{
	if (!pkg_temp_thermal_device_remove(cpu))
		cancel_delayed_work_sync(
			&per_cpu(pkg_temp_thermal_threshold_work, cpu));

	pr_debug("put_core_offline: cpu %d\n", cpu);
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

	spin_lock_init(&pkg_work_lock);

	cpu_notifier_register_begin();
	for_each_online_cpu(i)
		if (get_core_online(i))
			goto err_ret;
	__register_hotcpu_notifier(&pkg_temp_thermal_notifier);
	cpu_notifier_register_done();

	platform_thermal_package_notify = pkg_thermal_notify;
	platform_thermal_package_rate_control = pkg_thermal_rate_control;

	pkg_temp_debugfs_init(); /* Don't care if fails */

	return 0;

err_ret:
	for_each_online_cpu(i)
		put_core_offline(i);
	cpu_notifier_register_done();
	kfree(pkg_work_scheduled);
	return -ENODEV;
}

static void __exit pkg_temp_thermal_exit(void)
{
	struct pkg_device *pkgdev, *n;
	int i;

	platform_thermal_package_notify = NULL;
	platform_thermal_package_rate_control = NULL;

	cpu_notifier_register_begin();
	__unregister_hotcpu_notifier(&pkg_temp_thermal_notifier);
	mutex_lock(&phy_dev_list_mutex);
	list_for_each_entry_safe(pkgdev, n, &phy_dev_list, list) {
		/* Retore old MSR value for package thermal interrupt */
		wrmsr_on_cpu(pkgdev->cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT,
			     pkgdev->msr_pkg_therm_low,
			     pkgdev->msr_pkg_therm_high);
		thermal_zone_device_unregister(pkgdev->tzone);
		list_del(&pkgdev->list);
		kfree(pkgdev);
	}
	mutex_unlock(&phy_dev_list_mutex);
	for_each_online_cpu(i)
		cancel_delayed_work_sync(
			&per_cpu(pkg_temp_thermal_threshold_work, i));
	cpu_notifier_register_done();

	kfree(pkg_work_scheduled);

	debugfs_remove_recursive(debugfs);
}

module_init(pkg_temp_thermal_init)
module_exit(pkg_temp_thermal_exit)

MODULE_DESCRIPTION("X86 PKG TEMP Thermal Driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
