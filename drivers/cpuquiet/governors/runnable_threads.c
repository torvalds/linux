/*
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/cpuquiet.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sched.h>

typedef enum {
	DISABLED,
	IDLE,
	DOWN,
	UP,
} RUNNABLES_STATE;

static struct delayed_work runnables_work;
static struct kobject *runnables_kobject;

/* configurable parameters */
static unsigned int sample_rate = 20;		/* msec */

static RUNNABLES_STATE runnables_state;
static struct workqueue_struct *runnables_wq;

#define NR_FSHIFT_EXP	3
#define NR_FSHIFT	(1 << NR_FSHIFT_EXP)
/* avg run threads * 8 (e.g., 11 = 1.375 threads) */
static unsigned int default_thresholds[] = {
	9, 17, 25, UINT_MAX
};

static unsigned int nr_run_last;
static unsigned int nr_run_hysteresis = 4;		/* 1 / 4 thread */
static unsigned int default_threshold_level = 4;	/* 1 / 4 thread */
static unsigned int nr_run_thresholds[NR_CPUS];

DEFINE_MUTEX(runnables_work_lock);

static void update_runnables_state(void)
{
	unsigned int nr_cpus = num_online_cpus();
	int max_cpus = pm_qos_request(PM_QOS_MAX_ONLINE_CPUS) ? : 4;
	int min_cpus = pm_qos_request(PM_QOS_MIN_ONLINE_CPUS);
	unsigned int avg_nr_run = avg_nr_running();
	unsigned int nr_run;

	if (runnables_state == DISABLED)
		return;

	for (nr_run = 1; nr_run < ARRAY_SIZE(nr_run_thresholds); nr_run++) {
		unsigned int nr_threshold = nr_run_thresholds[nr_run - 1];
		if (nr_run_last <= nr_run)
			nr_threshold += NR_FSHIFT / nr_run_hysteresis;
		if (avg_nr_run <= (nr_threshold << (FSHIFT - NR_FSHIFT_EXP)))
			break;
	}
	nr_run_last = nr_run;

	if ((nr_cpus > max_cpus || nr_run < nr_cpus) && nr_cpus >= min_cpus) {
		runnables_state = DOWN;
	} else if (nr_cpus < min_cpus || nr_run > nr_cpus) {
		runnables_state =  UP;
	} else {
		runnables_state = IDLE;
	}
}

static unsigned int get_lightest_loaded_cpu_n(void)
{
	unsigned long min_avg_runnables = ULONG_MAX;
	unsigned int cpu = nr_cpu_ids;
	int i;

	for_each_online_cpu(i) {
		unsigned int nr_runnables = get_avg_nr_running(i);

		if (i > 0 && min_avg_runnables > nr_runnables) {
			cpu = i;
			min_avg_runnables = nr_runnables;
		}
	}

	return cpu;
}

static void runnables_work_func(struct work_struct *work)
{
	bool up = false;
	bool sample = false;
	unsigned int cpu = nr_cpu_ids;

	mutex_lock(&runnables_work_lock);

	update_runnables_state();

	switch (runnables_state) {
	case DISABLED:
		break;
	case IDLE:
		sample = true;
		break;
	case UP:
		cpu = cpumask_next_zero(0, cpu_online_mask);
		up = true;
		sample = true;
		break;
	case DOWN:
		cpu = get_lightest_loaded_cpu_n();
		sample = true;
		break;
	default:
		pr_err("%s: invalid cpuquiet runnable governor state %d\n",
			__func__, runnables_state);
		break;
	}

	if (sample)
		queue_delayed_work(runnables_wq, &runnables_work,
					msecs_to_jiffies(sample_rate));

	if (cpu < nr_cpu_ids) {
		if (up)
			cpuquiet_wake_cpu(cpu);
		else
			cpuquiet_quiesence_cpu(cpu);
	}

	mutex_unlock(&runnables_work_lock);
}

CPQ_BASIC_ATTRIBUTE(sample_rate, 0644, uint);
CPQ_BASIC_ATTRIBUTE(nr_run_hysteresis, 0644, uint);

static struct attribute *runnables_attributes[] = {
	&sample_rate_attr.attr,
	&nr_run_hysteresis_attr.attr,
	NULL,
};

static const struct sysfs_ops runnables_sysfs_ops = {
	.show = cpuquiet_auto_sysfs_show,
	.store = cpuquiet_auto_sysfs_store,
};

static struct kobj_type ktype_runnables = {
	.sysfs_ops = &runnables_sysfs_ops,
	.default_attrs = runnables_attributes,
};

static int runnables_sysfs(void)
{
	int err;

	runnables_kobject = kzalloc(sizeof(*runnables_kobject),
				GFP_KERNEL);

	if (!runnables_kobject)
		return -ENOMEM;

	err = cpuquiet_kobject_init(runnables_kobject, &ktype_runnables,
				"runnable_threads");

	if (err)
		kfree(runnables_kobject);

	return err;
}

static void runnables_device_busy(void)
{
	if (runnables_state != DISABLED) {
		runnables_state = DISABLED;
		cancel_delayed_work_sync(&runnables_work);
	}
}

static void runnables_device_free(void)
{
	if (runnables_state == DISABLED) {
		runnables_state = IDLE;
		runnables_work_func(NULL);
	}
}

static void runnables_stop(void)
{
	runnables_state = DISABLED;
	cancel_delayed_work_sync(&runnables_work);
	destroy_workqueue(runnables_wq);
	kobject_put(runnables_kobject);
}

static int runnables_start(void)
{
	int err, i;

	err = runnables_sysfs();
	if (err)
		return err;

	runnables_wq = alloc_workqueue("cpuquiet-runnables",
			WQ_UNBOUND | WQ_RESCUER | WQ_FREEZABLE, 1);
	if (!runnables_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&runnables_work, runnables_work_func);

	for(i = 0; i < ARRAY_SIZE(nr_run_thresholds); ++i) {
		if (i < ARRAY_SIZE(default_thresholds))
			nr_run_thresholds[i] = default_thresholds[i];
		else if (i == (ARRAY_SIZE(nr_run_thresholds) - 1))
			nr_run_thresholds[i] = UINT_MAX;
		else
			nr_run_thresholds[i] = i + 1 +
				NR_FSHIFT / default_threshold_level;
	}

	runnables_state = IDLE;
	runnables_work_func(NULL);

	return 0;
}

struct cpuquiet_governor runnables_governor = {
	.name		   	  = "runnable",
	.start			  = runnables_start,
	.device_free_notification = runnables_device_free,
	.device_busy_notification = runnables_device_busy,
	.stop			  = runnables_stop,
	.owner		   	  = THIS_MODULE,
};

static int __init init_runnables(void)
{
	return cpuquiet_register_governor(&runnables_governor);
}

static void __exit exit_runnables(void)
{
	cpuquiet_unregister_governor(&runnables_governor);
}

MODULE_LICENSE("GPL");
module_init(init_runnables);
module_exit(exit_runnables);
