// SPDX-License-Identifier: GPL-2.0-only
/*
 * acpi_pad.c ACPI Processor Aggregator Driver
 *
 * Copyright (c) 2009, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/tick.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <asm/mwait.h>
#include <xen/xen.h>

#define ACPI_PROCESSOR_AGGREGATOR_CLASS	"acpi_pad"
#define ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME "Processor Aggregator"
#define ACPI_PROCESSOR_AGGREGATOR_NOTIFY 0x80

#define ACPI_PROCESSOR_AGGREGATOR_STATUS_SUCCESS	0
#define ACPI_PROCESSOR_AGGREGATOR_STATUS_NO_ACTION	1

static DEFINE_MUTEX(isolated_cpus_lock);
static DEFINE_MUTEX(round_robin_lock);

static unsigned long power_saving_mwait_eax;

static unsigned char tsc_detected_unstable;
static unsigned char tsc_marked_unstable;

static void power_saving_mwait_init(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int highest_cstate = 0;
	unsigned int highest_subcstate = 0;
	int i;

	if (!boot_cpu_has(X86_FEATURE_MWAIT))
		return;
	if (boot_cpu_data.cpuid_level < CPUID_MWAIT_LEAF)
		return;

	cpuid(CPUID_MWAIT_LEAF, &eax, &ebx, &ecx, &edx);

	if (!(ecx & CPUID5_ECX_EXTENSIONS_SUPPORTED) ||
	    !(ecx & CPUID5_ECX_INTERRUPT_BREAK))
		return;

	edx >>= MWAIT_SUBSTATE_SIZE;
	for (i = 0; i < 7 && edx; i++, edx >>= MWAIT_SUBSTATE_SIZE) {
		if (edx & MWAIT_SUBSTATE_MASK) {
			highest_cstate = i;
			highest_subcstate = edx & MWAIT_SUBSTATE_MASK;
		}
	}
	power_saving_mwait_eax = (highest_cstate << MWAIT_SUBSTATE_SIZE) |
		(highest_subcstate - 1);

#if defined(CONFIG_X86)
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_HYGON:
	case X86_VENDOR_AMD:
	case X86_VENDOR_INTEL:
	case X86_VENDOR_ZHAOXIN:
	case X86_VENDOR_CENTAUR:
		/*
		 * AMD Fam10h TSC will tick in all
		 * C/P/S0/S1 states when this bit is set.
		 */
		if (!boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
			tsc_detected_unstable = 1;
		break;
	default:
		/* TSC could halt in idle */
		tsc_detected_unstable = 1;
	}
#endif
}

static unsigned long cpu_weight[NR_CPUS];
static int tsk_in_cpu[NR_CPUS] = {[0 ... NR_CPUS-1] = -1};
static DECLARE_BITMAP(pad_busy_cpus_bits, NR_CPUS);
static void round_robin_cpu(unsigned int tsk_index)
{
	struct cpumask *pad_busy_cpus = to_cpumask(pad_busy_cpus_bits);
	cpumask_var_t tmp;
	int cpu;
	unsigned long min_weight = -1;
	unsigned long preferred_cpu;

	if (!alloc_cpumask_var(&tmp, GFP_KERNEL))
		return;

	mutex_lock(&round_robin_lock);
	cpumask_clear(tmp);
	for_each_cpu(cpu, pad_busy_cpus)
		cpumask_or(tmp, tmp, topology_sibling_cpumask(cpu));
	cpumask_andnot(tmp, cpu_online_mask, tmp);
	/* avoid HT siblings if possible */
	if (cpumask_empty(tmp))
		cpumask_andnot(tmp, cpu_online_mask, pad_busy_cpus);
	if (cpumask_empty(tmp)) {
		mutex_unlock(&round_robin_lock);
		free_cpumask_var(tmp);
		return;
	}
	for_each_cpu(cpu, tmp) {
		if (cpu_weight[cpu] < min_weight) {
			min_weight = cpu_weight[cpu];
			preferred_cpu = cpu;
		}
	}

	if (tsk_in_cpu[tsk_index] != -1)
		cpumask_clear_cpu(tsk_in_cpu[tsk_index], pad_busy_cpus);
	tsk_in_cpu[tsk_index] = preferred_cpu;
	cpumask_set_cpu(preferred_cpu, pad_busy_cpus);
	cpu_weight[preferred_cpu]++;
	mutex_unlock(&round_robin_lock);

	set_cpus_allowed_ptr(current, cpumask_of(preferred_cpu));

	free_cpumask_var(tmp);
}

static void exit_round_robin(unsigned int tsk_index)
{
	struct cpumask *pad_busy_cpus = to_cpumask(pad_busy_cpus_bits);

	if (tsk_in_cpu[tsk_index] != -1) {
		cpumask_clear_cpu(tsk_in_cpu[tsk_index], pad_busy_cpus);
		tsk_in_cpu[tsk_index] = -1;
	}
}

static unsigned int idle_pct = 5; /* percentage */
static unsigned int round_robin_time = 1; /* second */
static int power_saving_thread(void *data)
{
	int do_sleep;
	unsigned int tsk_index = (unsigned long)data;
	u64 last_jiffies = 0;

	sched_set_fifo_low(current);

	while (!kthread_should_stop()) {
		unsigned long expire_time;

		/* round robin to cpus */
		expire_time = last_jiffies + round_robin_time * HZ;
		if (time_before(expire_time, jiffies)) {
			last_jiffies = jiffies;
			round_robin_cpu(tsk_index);
		}

		do_sleep = 0;

		expire_time = jiffies + HZ * (100 - idle_pct) / 100;

		while (!need_resched()) {
			if (tsc_detected_unstable && !tsc_marked_unstable) {
				/* TSC could halt in idle, so notify users */
				mark_tsc_unstable("TSC halts in idle");
				tsc_marked_unstable = 1;
			}
			local_irq_disable();

			perf_lopwr_cb(true);

			tick_broadcast_enable();
			tick_broadcast_enter();
			stop_critical_timings();

			mwait_idle_with_hints(power_saving_mwait_eax, 1);

			start_critical_timings();
			tick_broadcast_exit();

			perf_lopwr_cb(false);

			local_irq_enable();

			if (time_before(expire_time, jiffies)) {
				do_sleep = 1;
				break;
			}
		}

		/*
		 * current sched_rt has threshold for rt task running time.
		 * When a rt task uses 95% CPU time, the rt thread will be
		 * scheduled out for 5% CPU time to not starve other tasks. But
		 * the mechanism only works when all CPUs have RT task running,
		 * as if one CPU hasn't RT task, RT task from other CPUs will
		 * borrow CPU time from this CPU and cause RT task use > 95%
		 * CPU time. To make 'avoid starvation' work, takes a nap here.
		 */
		if (unlikely(do_sleep))
			schedule_timeout_killable(HZ * idle_pct / 100);

		/* If an external event has set the need_resched flag, then
		 * we need to deal with it, or this loop will continue to
		 * spin without calling __mwait().
		 */
		if (unlikely(need_resched()))
			schedule();
	}

	exit_round_robin(tsk_index);
	return 0;
}

static struct task_struct *ps_tsks[NR_CPUS];
static unsigned int ps_tsk_num;
static int create_power_saving_task(void)
{
	int rc;

	ps_tsks[ps_tsk_num] = kthread_run(power_saving_thread,
		(void *)(unsigned long)ps_tsk_num,
		"acpi_pad/%d", ps_tsk_num);

	if (IS_ERR(ps_tsks[ps_tsk_num])) {
		rc = PTR_ERR(ps_tsks[ps_tsk_num]);
		ps_tsks[ps_tsk_num] = NULL;
	} else {
		rc = 0;
		ps_tsk_num++;
	}

	return rc;
}

static void destroy_power_saving_task(void)
{
	if (ps_tsk_num > 0) {
		ps_tsk_num--;
		kthread_stop(ps_tsks[ps_tsk_num]);
		ps_tsks[ps_tsk_num] = NULL;
	}
}

static void set_power_saving_task_num(unsigned int num)
{
	if (num > ps_tsk_num) {
		while (ps_tsk_num < num) {
			if (create_power_saving_task())
				return;
		}
	} else if (num < ps_tsk_num) {
		while (ps_tsk_num > num)
			destroy_power_saving_task();
	}
}

static void acpi_pad_idle_cpus(unsigned int num_cpus)
{
	cpus_read_lock();

	num_cpus = min_t(unsigned int, num_cpus, num_online_cpus());
	set_power_saving_task_num(num_cpus);

	cpus_read_unlock();
}

static uint32_t acpi_pad_idle_cpus_num(void)
{
	return ps_tsk_num;
}

static ssize_t rrtime_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long num;

	if (kstrtoul(buf, 0, &num))
		return -EINVAL;
	if (num < 1 || num >= 100)
		return -EINVAL;
	mutex_lock(&isolated_cpus_lock);
	round_robin_time = num;
	mutex_unlock(&isolated_cpus_lock);
	return count;
}

static ssize_t rrtime_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", round_robin_time);
}
static DEVICE_ATTR_RW(rrtime);

static ssize_t idlepct_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long num;

	if (kstrtoul(buf, 0, &num))
		return -EINVAL;
	if (num < 1 || num >= 100)
		return -EINVAL;
	mutex_lock(&isolated_cpus_lock);
	idle_pct = num;
	mutex_unlock(&isolated_cpus_lock);
	return count;
}

static ssize_t idlepct_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", idle_pct);
}
static DEVICE_ATTR_RW(idlepct);

static ssize_t idlecpus_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long num;

	if (kstrtoul(buf, 0, &num))
		return -EINVAL;
	mutex_lock(&isolated_cpus_lock);
	acpi_pad_idle_cpus(num);
	mutex_unlock(&isolated_cpus_lock);
	return count;
}

static ssize_t idlecpus_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(false, buf,
				       to_cpumask(pad_busy_cpus_bits));
}

static DEVICE_ATTR_RW(idlecpus);

static struct attribute *acpi_pad_attrs[] = {
	&dev_attr_idlecpus.attr,
	&dev_attr_idlepct.attr,
	&dev_attr_rrtime.attr,
	NULL
};

ATTRIBUTE_GROUPS(acpi_pad);

/*
 * Query firmware how many CPUs should be idle
 * return -1 on failure
 */
static int acpi_pad_pur(acpi_handle handle)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *package;
	int num = -1;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_PUR", NULL, &buffer)))
		return num;

	if (!buffer.length || !buffer.pointer)
		return num;

	package = buffer.pointer;

	if (package->type == ACPI_TYPE_PACKAGE &&
		package->package.count == 2 &&
		package->package.elements[0].integer.value == 1) /* rev 1 */

		num = package->package.elements[1].integer.value;

	kfree(buffer.pointer);
	return num;
}

static void acpi_pad_handle_notify(acpi_handle handle)
{
	int num_cpus;
	uint32_t idle_cpus;
	struct acpi_buffer param = {
		.length = 4,
		.pointer = (void *)&idle_cpus,
	};
	u32 status;

	mutex_lock(&isolated_cpus_lock);
	num_cpus = acpi_pad_pur(handle);
	if (num_cpus < 0) {
		/* The ACPI specification says that if no action was performed when
		 * processing the _PUR object, _OST should still be evaluated, albeit
		 * with a different status code.
		 */
		status = ACPI_PROCESSOR_AGGREGATOR_STATUS_NO_ACTION;
	} else {
		status = ACPI_PROCESSOR_AGGREGATOR_STATUS_SUCCESS;
		acpi_pad_idle_cpus(num_cpus);
	}

	idle_cpus = acpi_pad_idle_cpus_num();
	acpi_evaluate_ost(handle, ACPI_PROCESSOR_AGGREGATOR_NOTIFY, status, &param);
	mutex_unlock(&isolated_cpus_lock);
}

static void acpi_pad_notify(acpi_handle handle, u32 event,
	void *data)
{
	struct acpi_device *adev = data;

	switch (event) {
	case ACPI_PROCESSOR_AGGREGATOR_NOTIFY:
		acpi_pad_handle_notify(handle);
		acpi_bus_generate_netlink_event(adev->pnp.device_class,
			dev_name(&adev->dev), event, 0);
		break;
	default:
		pr_warn("Unsupported event [0x%x]\n", event);
		break;
	}
}

static int acpi_pad_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	acpi_status status;

	strscpy(acpi_device_name(adev), ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME);
	strscpy(acpi_device_class(adev), ACPI_PROCESSOR_AGGREGATOR_CLASS);

	status = acpi_install_notify_handler(adev->handle,
		ACPI_DEVICE_NOTIFY, acpi_pad_notify, adev);

	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static void acpi_pad_remove(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);

	mutex_lock(&isolated_cpus_lock);
	acpi_pad_idle_cpus(0);
	mutex_unlock(&isolated_cpus_lock);

	acpi_remove_notify_handler(adev->handle,
		ACPI_DEVICE_NOTIFY, acpi_pad_notify);
}

static const struct acpi_device_id pad_device_ids[] = {
	{"ACPI000C", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, pad_device_ids);

static struct platform_driver acpi_pad_driver = {
	.probe = acpi_pad_probe,
	.remove_new = acpi_pad_remove,
	.driver = {
		.dev_groups = acpi_pad_groups,
		.name = "processor_aggregator",
		.acpi_match_table = pad_device_ids,
	},
};

static int __init acpi_pad_init(void)
{
	/* Xen ACPI PAD is used when running as Xen Dom0. */
	if (xen_initial_domain())
		return -ENODEV;

	power_saving_mwait_init();
	if (power_saving_mwait_eax == 0)
		return -EINVAL;

	return platform_driver_register(&acpi_pad_driver);
}

static void __exit acpi_pad_exit(void)
{
	platform_driver_unregister(&acpi_pad_driver);
}

module_init(acpi_pad_init);
module_exit(acpi_pad_exit);
MODULE_AUTHOR("Shaohua Li<shaohua.li@intel.com>");
MODULE_DESCRIPTION("ACPI Processor Aggregator Driver");
MODULE_LICENSE("GPL");
