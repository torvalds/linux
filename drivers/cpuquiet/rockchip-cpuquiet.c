/*
 * Cpuquiet driver for Rockchip SoCs
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/cpuquiet.h>
#include <linux/pm_qos.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#define INITIAL_STATE		CPQ_DISABLED
#define HOTPLUG_DELAY_MS	100

static DEFINE_MUTEX(rockchip_cpuquiet_lock);
static DEFINE_MUTEX(rockchip_cpq_lock_stats);

static struct workqueue_struct *cpuquiet_wq;
static struct work_struct cpuquiet_work;

static wait_queue_head_t wait_enable;
static wait_queue_head_t wait_cpu;

static bool enable;
static unsigned long hotplug_timeout_jiffies;

static struct cpumask cpumask_online_requests;
static struct cpumask cpumask_offline_requests;

enum {
	CPQ_DISABLED = 0,
	CPQ_ENABLED,
	CPQ_IDLE,
};

static int cpq_target_state;

static int cpq_state;

static struct {
	cputime64_t time_up_total;
	u64 last_update;
	unsigned int up_down_count;
} hp_stats[CONFIG_NR_CPUS];

static void hp_init_stats(void)
{
	int i;
	u64 cur_jiffies = get_jiffies_64();

	mutex_lock(&rockchip_cpq_lock_stats);

	for (i = 0; i < nr_cpu_ids; i++) {
		hp_stats[i].time_up_total = 0;
		hp_stats[i].last_update = cur_jiffies;

		hp_stats[i].up_down_count = 0;
		if (cpu_online(i))
			hp_stats[i].up_down_count = 1;
	}

	mutex_unlock(&rockchip_cpq_lock_stats);
}

/* must be called with rockchip_cpq_lock_stats held */
static void __hp_stats_update(unsigned int cpu, bool up)
{
	u64 cur_jiffies = get_jiffies_64();
	bool was_up;

	was_up = hp_stats[cpu].up_down_count & 0x1;

	if (was_up)
		hp_stats[cpu].time_up_total =
			hp_stats[cpu].time_up_total +
			(cur_jiffies - hp_stats[cpu].last_update);

	if (was_up != up) {
		hp_stats[cpu].up_down_count++;
		if ((hp_stats[cpu].up_down_count & 0x1) != up) {
			/* FIXME: sysfs user space CPU control breaks stats */
			pr_err("hotplug stats out of sync with CPU%d", cpu);
			hp_stats[cpu].up_down_count ^=  0x1;
		}
	}
	hp_stats[cpu].last_update = cur_jiffies;
}

static void hp_stats_update(unsigned int cpu, bool up)
{
	mutex_lock(&rockchip_cpq_lock_stats);

	__hp_stats_update(cpu, up);

	mutex_unlock(&rockchip_cpq_lock_stats);
}

static int update_core_config(unsigned int cpunumber, bool up)
{
	int ret = 0;

	mutex_lock(&rockchip_cpuquiet_lock);

	if (cpq_state == CPQ_DISABLED || cpunumber >= nr_cpu_ids) {
		mutex_unlock(&rockchip_cpuquiet_lock);
		return -EINVAL;
	}

	if (up) {
		cpumask_set_cpu(cpunumber, &cpumask_online_requests);
		cpumask_clear_cpu(cpunumber, &cpumask_offline_requests);
		queue_work(cpuquiet_wq, &cpuquiet_work);
	} else {
		cpumask_set_cpu(cpunumber, &cpumask_offline_requests);
		cpumask_clear_cpu(cpunumber, &cpumask_online_requests);
		queue_work(cpuquiet_wq, &cpuquiet_work);
	}

	mutex_unlock(&rockchip_cpuquiet_lock);

	return ret;
}

static int rockchip_quiesence_cpu(unsigned int cpunumber, bool sync)
{
	int err = 0;

	err = update_core_config(cpunumber, false);
	if (err || !sync)
		return err;

	err = wait_event_interruptible_timeout(wait_cpu,
					       !cpu_online(cpunumber),
					       hotplug_timeout_jiffies);

	if (err < 0)
		return err;

	if (err > 0)
		return 0;
	else
		return -ETIMEDOUT;
}

static int rockchip_wake_cpu(unsigned int cpunumber, bool sync)
{
	int err = 0;

	err = update_core_config(cpunumber, true);
	if (err || !sync)
		return err;

	err = wait_event_interruptible_timeout(wait_cpu, cpu_online(cpunumber),
					       hotplug_timeout_jiffies);

	if (err < 0)
		return err;

	if (err > 0)
		return 0;
	else
		return -ETIMEDOUT;
}

static struct cpuquiet_driver rockchip_cpuquiet_driver = {
	.name		= "rockchip",
	.quiesence_cpu	= rockchip_quiesence_cpu,
	.wake_cpu	= rockchip_wake_cpu,
};

/* must be called from worker function */
static void __cpuinit __apply_core_config(void)
{
	int count = -1;
	unsigned int cpu;
	int nr_cpus;
	struct cpumask online, offline, cpu_online;
	int max_cpus = pm_qos_request(PM_QOS_MAX_ONLINE_CPUS);
	int min_cpus = pm_qos_request(PM_QOS_MIN_ONLINE_CPUS);

	if (min_cpus > num_possible_cpus())
		min_cpus = 0;
	if (max_cpus <= 0)
		max_cpus = num_present_cpus();

	mutex_lock(&rockchip_cpuquiet_lock);

	online = cpumask_online_requests;
	offline = cpumask_offline_requests;

	mutex_unlock(&rockchip_cpuquiet_lock);

	/* always keep CPU0 online */
	cpumask_set_cpu(0, &online);
	cpu_online = *cpu_online_mask;

	if (max_cpus < min_cpus)
		max_cpus = min_cpus;

	nr_cpus = cpumask_weight(&online);
	if (nr_cpus < min_cpus) {
		cpu = 0;
		count = min_cpus - nr_cpus;
		for (; count > 0; count--) {
			cpu = cpumask_next_zero(cpu, &online);
			cpumask_set_cpu(cpu, &online);
			cpumask_clear_cpu(cpu, &offline);
		}
	} else if (nr_cpus > max_cpus) {
		count = nr_cpus - max_cpus;
		cpu = 0;
		for (; count > 0; count--) {
			/* CPU0 should always be online */
			cpu = cpumask_next(cpu, &online);
			cpumask_set_cpu(cpu, &offline);
			cpumask_clear_cpu(cpu, &online);
		}
	}

	cpumask_andnot(&online, &online, &cpu_online);
	for_each_cpu(cpu, &online) {
		cpu_up(cpu);
		hp_stats_update(cpu, true);
	}

	cpumask_and(&offline, &offline, &cpu_online);
	for_each_cpu(cpu, &offline) {
		cpu_down(cpu);
		hp_stats_update(cpu, false);
	}
	wake_up_interruptible(&wait_cpu);
}

static void __cpuinit rockchip_cpuquiet_work_func(struct work_struct *work)
{
	int action;

	mutex_lock(&rockchip_cpuquiet_lock);

	action = cpq_target_state;

	if (action == CPQ_ENABLED) {
		hp_init_stats();
		cpuquiet_device_free();
		pr_info("cpuquiet enabled\n");
		cpq_state = CPQ_ENABLED;
		cpq_target_state = CPQ_IDLE;
		wake_up_interruptible(&wait_enable);
	}

	if (cpq_state == CPQ_DISABLED) {
		mutex_unlock(&rockchip_cpuquiet_lock);
		return;
	}

	if (action == CPQ_DISABLED) {
		cpq_state = CPQ_DISABLED;
		mutex_unlock(&rockchip_cpuquiet_lock);
		cpuquiet_device_busy();
		pr_info("cpuquiet disabled\n");
		wake_up_interruptible(&wait_enable);
		return;
	}

	mutex_unlock(&rockchip_cpuquiet_lock);
	__apply_core_config();
}

static int min_cpus_notify(struct notifier_block *nb, unsigned long n, void *p)
{
	mutex_lock(&rockchip_cpuquiet_lock);

	if (cpq_state != CPQ_DISABLED)
		queue_work(cpuquiet_wq, &cpuquiet_work);

	mutex_unlock(&rockchip_cpuquiet_lock);

	return NOTIFY_OK;
}

static int max_cpus_notify(struct notifier_block *nb, unsigned long n, void *p)
{
	mutex_lock(&rockchip_cpuquiet_lock);

	if (cpq_state != CPQ_DISABLED)
		queue_work(cpuquiet_wq, &cpuquiet_work);

	mutex_unlock(&rockchip_cpuquiet_lock);

	return NOTIFY_OK;
}

/* Must be called with rockchip_cpuquiet_lock held */
static void __idle_stop_governor(void)
{
	if (cpq_state == CPQ_DISABLED)
		return;

	if (num_online_cpus() == 1)
		cpuquiet_device_busy();
	else
		cpuquiet_device_free();
}

static int __cpuinit cpu_online_notify(struct notifier_block *nfb,
				       unsigned long action, void *hcpu)
{
	switch (action) {
	case CPU_POST_DEAD:
		if (num_online_cpus() == 1) {
			mutex_lock(&rockchip_cpuquiet_lock);
			__idle_stop_governor();
			mutex_unlock(&rockchip_cpuquiet_lock);
		}
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		mutex_lock(&rockchip_cpuquiet_lock);
		__idle_stop_governor();
		mutex_unlock(&rockchip_cpuquiet_lock);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block cpu_online_notifier __cpuinitdata = {
	.notifier_call = cpu_online_notify,
};

static struct notifier_block min_cpus_notifier = {
	.notifier_call = min_cpus_notify,
};

static struct notifier_block max_cpus_notifier = {
	.notifier_call = max_cpus_notify,
};

static void delay_callback(struct cpuquiet_attribute *attr)
{
	unsigned long val;

	if (attr) {
		val = (*((unsigned long *)(attr->param)));
		(*((unsigned long *)(attr->param))) = msecs_to_jiffies(val);
	}
}

static void enable_callback(struct cpuquiet_attribute *attr)
{
	int target_state = enable ? CPQ_ENABLED : CPQ_DISABLED;

	mutex_lock(&rockchip_cpuquiet_lock);

	if (cpq_state != target_state) {
		cpq_target_state = target_state;
		queue_work(cpuquiet_wq, &cpuquiet_work);
	}

	mutex_unlock(&rockchip_cpuquiet_lock);

	wait_event_interruptible(wait_enable, cpq_state == target_state);
}

CPQ_ATTRIBUTE(hotplug_timeout_jiffies, 0644, ulong, delay_callback);
CPQ_ATTRIBUTE(enable, 0644, bool, enable_callback);

static struct attribute *rockchip_cpuquiet_attributes[] = {
	&enable_attr.attr,
	&hotplug_timeout_jiffies_attr.attr,
	NULL,
};

static const struct sysfs_ops rockchip_cpuquiet_sysfs_ops = {
	.show = cpuquiet_auto_sysfs_show,
	.store = cpuquiet_auto_sysfs_store,
};

static struct kobj_type ktype_sysfs = {
	.sysfs_ops = &rockchip_cpuquiet_sysfs_ops,
	.default_attrs = rockchip_cpuquiet_attributes,
};

static int rockchip_cpuquiet_sysfs_init(void)
{
	int err;

	struct kobject *kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);

	if (!kobj)
		return -ENOMEM;

	err = cpuquiet_kobject_init(kobj, &ktype_sysfs, "rockchip_cpuquiet");

	if (err)
		kfree(kobj);

	return err;
}

#ifdef CONFIG_DEBUG_FS
static int hp_stats_show(struct seq_file *s, void *data)
{
	int i;
	u64 cur_jiffies = get_jiffies_64();

	mutex_lock(&rockchip_cpuquiet_lock);

	mutex_lock(&rockchip_cpq_lock_stats);

	if (cpq_state != CPQ_DISABLED) {
		for (i = 0; i < nr_cpu_ids; i++) {
			bool was_up;

			was_up = (hp_stats[i].up_down_count & 0x1);
			__hp_stats_update(i, was_up);
		}
	}
	mutex_unlock(&rockchip_cpq_lock_stats);

	mutex_unlock(&rockchip_cpuquiet_lock);

	seq_printf(s, "%-15s ", "cpu:");
	for (i = 0; i < nr_cpu_ids; i++)
		seq_printf(s, "G%-9d ", i);

	seq_printf(s, "%-15s ", "transitions:");
	for (i = 0; i < nr_cpu_ids; i++)
		seq_printf(s, "%-10u ", hp_stats[i].up_down_count);
	seq_puts(s, "\n");

	seq_printf(s, "%-15s ", "time plugged:");
	for (i = 0; i < nr_cpu_ids; i++) {
		seq_printf(s, "%-10llu ",
			   cputime64_to_clock_t(hp_stats[i].time_up_total));
	}
	seq_puts(s, "\n");

	seq_printf(s, "%-15s %llu\n", "time-stamp:",
		   cputime64_to_clock_t(cur_jiffies));

	return 0;
}

static int hp_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, hp_stats_show, inode->i_private);
}

static const struct file_operations hp_stats_fops = {
	.open		= hp_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init rockchip_cpuquiet_debug_init(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("rockchip_cpuquiet", NULL);
	if (!dir)
		return -ENOMEM;

	if (!debugfs_create_file("stats", S_IRUGO, dir, NULL, &hp_stats_fops))
		goto err_out;

	return 0;

err_out:
	debugfs_remove_recursive(dir);
	return -ENOMEM;
}

late_initcall(rockchip_cpuquiet_debug_init);
#endif /* CONFIG_DEBUG_FS */

static int __init rockchip_cpuquiet_init(void)
{
	int err;

	init_waitqueue_head(&wait_enable);
	init_waitqueue_head(&wait_cpu);

	/*
	 * Not bound to the issuer CPU (=> high-priority), has rescue worker
	 * task, single-threaded, freezable.
	 */
	cpuquiet_wq = alloc_workqueue(
		"cpuquiet", WQ_NON_REENTRANT | WQ_FREEZABLE, 1);

	if (!cpuquiet_wq)
		return -ENOMEM;

	INIT_WORK(&cpuquiet_work, rockchip_cpuquiet_work_func);
	hotplug_timeout_jiffies = msecs_to_jiffies(HOTPLUG_DELAY_MS);
	cpumask_clear(&cpumask_online_requests);
	cpumask_clear(&cpumask_offline_requests);

	cpq_state = INITIAL_STATE;
	enable = cpq_state == CPQ_DISABLED ? false : true;
	hp_init_stats();

	pr_info("cpuquiet initialized: %s\n",
		(cpq_state == CPQ_DISABLED) ? "disabled" : "enabled");

	if (pm_qos_add_notifier(PM_QOS_MIN_ONLINE_CPUS, &min_cpus_notifier))
		pr_err("Failed to register min cpus PM QoS notifier\n");
	if (pm_qos_add_notifier(PM_QOS_MAX_ONLINE_CPUS, &max_cpus_notifier))
		pr_err("Failed to register max cpus PM QoS notifier\n");

	register_hotcpu_notifier(&cpu_online_notifier);

	err = cpuquiet_register_driver(&rockchip_cpuquiet_driver);
	if (err) {
		destroy_workqueue(cpuquiet_wq);
		return err;
	}

	err = rockchip_cpuquiet_sysfs_init();
	if (err) {
		cpuquiet_unregister_driver(&rockchip_cpuquiet_driver);
		destroy_workqueue(cpuquiet_wq);
	}

	return err;
}
device_initcall(rockchip_cpuquiet_init);
