// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2010 ARM Ltd.
 * Copyright 2012 Advanced Micro Devices, Inc., Robert Richter
 *
 * Perf-events backend for OProfile.
 */
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/oprofile.h>
#include <linux/slab.h>

/*
 * Per performance monitor configuration as set via oprofilefs.
 */
struct op_counter_config {
	unsigned long count;
	unsigned long enabled;
	unsigned long event;
	unsigned long unit_mask;
	unsigned long kernel;
	unsigned long user;
	struct perf_event_attr attr;
};

static int oprofile_perf_enabled;
static DEFINE_MUTEX(oprofile_perf_mutex);

static struct op_counter_config *counter_config;
static DEFINE_PER_CPU(struct perf_event **, perf_events);
static int num_counters;

/*
 * Overflow callback for oprofile.
 */
static void op_overflow_handler(struct perf_event *event,
			struct perf_sample_data *data, struct pt_regs *regs)
{
	int id;
	u32 cpu = smp_processor_id();

	for (id = 0; id < num_counters; ++id)
		if (per_cpu(perf_events, cpu)[id] == event)
			break;

	if (id != num_counters)
		oprofile_add_sample(regs, id);
	else
		pr_warning("oprofile: ignoring spurious overflow "
				"on cpu %u\n", cpu);
}

/*
 * Called by oprofile_perf_setup to create perf attributes to mirror the oprofile
 * settings in counter_config. Attributes are created as `pinned' events and
 * so are permanently scheduled on the PMU.
 */
static void op_perf_setup(void)
{
	int i;
	u32 size = sizeof(struct perf_event_attr);
	struct perf_event_attr *attr;

	for (i = 0; i < num_counters; ++i) {
		attr = &counter_config[i].attr;
		memset(attr, 0, size);
		attr->type		= PERF_TYPE_RAW;
		attr->size		= size;
		attr->config		= counter_config[i].event;
		attr->sample_period	= counter_config[i].count;
		attr->pinned		= 1;
	}
}

static int op_create_counter(int cpu, int event)
{
	struct perf_event *pevent;

	if (!counter_config[event].enabled || per_cpu(perf_events, cpu)[event])
		return 0;

	pevent = perf_event_create_kernel_counter(&counter_config[event].attr,
						  cpu, NULL,
						  op_overflow_handler, NULL);

	if (IS_ERR(pevent))
		return PTR_ERR(pevent);

	if (pevent->state != PERF_EVENT_STATE_ACTIVE) {
		perf_event_release_kernel(pevent);
		pr_warning("oprofile: failed to enable event %d "
				"on CPU %d\n", event, cpu);
		return -EBUSY;
	}

	per_cpu(perf_events, cpu)[event] = pevent;

	return 0;
}

static void op_destroy_counter(int cpu, int event)
{
	struct perf_event *pevent = per_cpu(perf_events, cpu)[event];

	if (pevent) {
		perf_event_release_kernel(pevent);
		per_cpu(perf_events, cpu)[event] = NULL;
	}
}

/*
 * Called by oprofile_perf_start to create active perf events based on the
 * perviously configured attributes.
 */
static int op_perf_start(void)
{
	int cpu, event, ret = 0;

	for_each_online_cpu(cpu) {
		for (event = 0; event < num_counters; ++event) {
			ret = op_create_counter(cpu, event);
			if (ret)
				return ret;
		}
	}

	return ret;
}

/*
 * Called by oprofile_perf_stop at the end of a profiling run.
 */
static void op_perf_stop(void)
{
	int cpu, event;

	for_each_online_cpu(cpu)
		for (event = 0; event < num_counters; ++event)
			op_destroy_counter(cpu, event);
}

static int oprofile_perf_create_files(struct dentry *root)
{
	unsigned int i;

	for (i = 0; i < num_counters; i++) {
		struct dentry *dir;
		char buf[4];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(root, buf);
		oprofilefs_create_ulong(dir, "enabled", &counter_config[i].enabled);
		oprofilefs_create_ulong(dir, "event", &counter_config[i].event);
		oprofilefs_create_ulong(dir, "count", &counter_config[i].count);
		oprofilefs_create_ulong(dir, "unit_mask", &counter_config[i].unit_mask);
		oprofilefs_create_ulong(dir, "kernel", &counter_config[i].kernel);
		oprofilefs_create_ulong(dir, "user", &counter_config[i].user);
	}

	return 0;
}

static int oprofile_perf_setup(void)
{
	raw_spin_lock(&oprofilefs_lock);
	op_perf_setup();
	raw_spin_unlock(&oprofilefs_lock);
	return 0;
}

static int oprofile_perf_start(void)
{
	int ret = -EBUSY;

	mutex_lock(&oprofile_perf_mutex);
	if (!oprofile_perf_enabled) {
		ret = 0;
		op_perf_start();
		oprofile_perf_enabled = 1;
	}
	mutex_unlock(&oprofile_perf_mutex);
	return ret;
}

static void oprofile_perf_stop(void)
{
	mutex_lock(&oprofile_perf_mutex);
	if (oprofile_perf_enabled)
		op_perf_stop();
	oprofile_perf_enabled = 0;
	mutex_unlock(&oprofile_perf_mutex);
}

#ifdef CONFIG_PM

static int oprofile_perf_suspend(struct platform_device *dev, pm_message_t state)
{
	mutex_lock(&oprofile_perf_mutex);
	if (oprofile_perf_enabled)
		op_perf_stop();
	mutex_unlock(&oprofile_perf_mutex);
	return 0;
}

static int oprofile_perf_resume(struct platform_device *dev)
{
	mutex_lock(&oprofile_perf_mutex);
	if (oprofile_perf_enabled && op_perf_start())
		oprofile_perf_enabled = 0;
	mutex_unlock(&oprofile_perf_mutex);
	return 0;
}

static struct platform_driver oprofile_driver = {
	.driver		= {
		.name		= "oprofile-perf",
	},
	.resume		= oprofile_perf_resume,
	.suspend	= oprofile_perf_suspend,
};

static struct platform_device *oprofile_pdev;

static int __init init_driverfs(void)
{
	int ret;

	ret = platform_driver_register(&oprofile_driver);
	if (ret)
		return ret;

	oprofile_pdev =	platform_device_register_simple(
				oprofile_driver.driver.name, 0, NULL, 0);
	if (IS_ERR(oprofile_pdev)) {
		ret = PTR_ERR(oprofile_pdev);
		platform_driver_unregister(&oprofile_driver);
	}

	return ret;
}

static void exit_driverfs(void)
{
	platform_device_unregister(oprofile_pdev);
	platform_driver_unregister(&oprofile_driver);
}

#else

static inline int  init_driverfs(void) { return 0; }
static inline void exit_driverfs(void) { }

#endif /* CONFIG_PM */

void oprofile_perf_exit(void)
{
	int cpu, id;
	struct perf_event *event;

	for_each_possible_cpu(cpu) {
		for (id = 0; id < num_counters; ++id) {
			event = per_cpu(perf_events, cpu)[id];
			if (event)
				perf_event_release_kernel(event);
		}

		kfree(per_cpu(perf_events, cpu));
	}

	kfree(counter_config);
	exit_driverfs();
}

int __init oprofile_perf_init(struct oprofile_operations *ops)
{
	int cpu, ret = 0;

	ret = init_driverfs();
	if (ret)
		return ret;

	num_counters = perf_num_counters();
	if (num_counters <= 0) {
		pr_info("oprofile: no performance counters\n");
		ret = -ENODEV;
		goto out;
	}

	counter_config = kcalloc(num_counters,
			sizeof(struct op_counter_config), GFP_KERNEL);

	if (!counter_config) {
		pr_info("oprofile: failed to allocate %d "
				"counters\n", num_counters);
		ret = -ENOMEM;
		num_counters = 0;
		goto out;
	}

	for_each_possible_cpu(cpu) {
		per_cpu(perf_events, cpu) = kcalloc(num_counters,
				sizeof(struct perf_event *), GFP_KERNEL);
		if (!per_cpu(perf_events, cpu)) {
			pr_info("oprofile: failed to allocate %d perf events "
					"for cpu %d\n", num_counters, cpu);
			ret = -ENOMEM;
			goto out;
		}
	}

	ops->create_files	= oprofile_perf_create_files;
	ops->setup		= oprofile_perf_setup;
	ops->start		= oprofile_perf_start;
	ops->stop		= oprofile_perf_stop;
	ops->shutdown		= oprofile_perf_stop;
	ops->cpu_type		= op_name_from_perf_id();

	if (!ops->cpu_type)
		ret = -ENODEV;
	else
		pr_info("oprofile: using %s\n", ops->cpu_type);

out:
	if (ret)
		oprofile_perf_exit();

	return ret;
}
