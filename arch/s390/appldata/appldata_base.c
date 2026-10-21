// SPDX-License-Identifier: GPL-2.0
/*
 * Base infrastructure for Linux-z/VM Monitor Stream, Stage 1.
 * Exports appldata_register_ops() and appldata_unregister_ops() for the
 * data gathering modules.
 *
 * Copyright IBM Corp. 2003, 2009
 *
 * Author: Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#define pr_fmt(fmt) "appldata: " fmt

#include <linux/export.h>
#include <linux/module.h>
#include <linux/sched/stat.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/kernel_stat.h>
#include <asm/appldata.h>
#include <asm/smp.h>

#include "appldata.h"

/* Default CPU time for sampling interval */
#define APPLDATA_CPU_INTERVAL	(10 * NSEC_PER_SEC)

/*
 * /proc entries (sysctl)
 */
static const char appldata_proc_name[APPLDATA_PROC_NAME_LENGTH] = "appldata";
static int appldata_timer_handler(const struct ctl_table *ctl, int write,
				  void *buffer, size_t *lenp, loff_t *ppos);
static int appldata_interval_handler(const struct ctl_table *ctl, int write,
				     void *buffer, size_t *lenp, loff_t *ppos);

static const struct ctl_table appldata_table[] = {
	{
		.procname	= "timer",
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= appldata_timer_handler,
	},
	{
		.procname	= "interval",
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= appldata_interval_handler,
	},
};

static void appldata_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(appldata_work, appldata_work_fn);

static DEFINE_MUTEX(appldata_timer_lock);
static u64 appldata_interval = APPLDATA_CPU_INTERVAL;
static int appldata_timer_active;

static u64 appldata_cputime_start;

/*
 * Ops list
 */
static DEFINE_MUTEX(appldata_ops_mutex);
static LIST_HEAD(appldata_ops_list);


/*************************** timer, work, DIAG *******************************/
static u64 appldata_total_cpu_time_ns(void)
{
	u64 total = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		total += kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
		total += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];
		total += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
		total += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
		total += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	}
	return total;
}

static void appldata_schedule_work(u64 remaining)
{
	unsigned int ncpus = num_online_cpus();
	unsigned long delay = HZ / 10;

	/*
	 * At most ncpus CPUs consume CPU time simultaneously, so the
	 * minimum wall-clock time until the remaining CPU time elapses
	 * is remaining / ncpus.
	 * Make sure the work is not scheduled more than once per 100ms.
	 */
	delay = max(delay, nsecs_to_jiffies(remaining / ncpus));
	mod_delayed_work(system_percpu_wq, &appldata_work, delay);
}

/*
 * appldata_work_fn()
 *
 * call data gathering function for each (active) module
 */
static void appldata_work_fn(struct work_struct *work)
{
	u64 now, elapsed, interval;
	struct appldata_ops *ops;
	struct list_head *lh;
	bool expired = false;

	now = appldata_total_cpu_time_ns();
	mutex_lock(&appldata_timer_lock);
	if (appldata_timer_active) {
		interval = appldata_interval;
		elapsed = now - appldata_cputime_start;
		if (elapsed < interval) {
			appldata_schedule_work(interval - elapsed);
		} else {
			appldata_cputime_start = now;
			appldata_schedule_work(interval);
			expired = true;
		}
	}
	mutex_unlock(&appldata_timer_lock);
	if (!expired)
		return;
	mutex_lock(&appldata_ops_mutex);
	list_for_each(lh, &appldata_ops_list) {
		ops = list_entry(lh, struct appldata_ops, list);
		if (ops->active == 1) {
			ops->callback(ops->data);
		}
	}
	mutex_unlock(&appldata_ops_mutex);
}

static struct appldata_product_id appldata_id = {
	.prod_nr    = {0xD3, 0xC9, 0xD5, 0xE4,
		       0xE7, 0xD2, 0xD9},	/* "LINUXKR" */
	.prod_fn    = 0xD5D3,			/* "NL" */
	.version_nr = 0xF2F6,			/* "26" */
	.release_nr = 0xF0F1,			/* "01" */
};

/*
 * appldata_diag()
 *
 * prepare parameter list, issue DIAG 0xDC
 */
int appldata_diag(char record_nr, u16 function, unsigned long buffer,
			u16 length, char *mod_lvl)
{
	struct appldata_parameter_list *parm_list;
	struct appldata_product_id *id;
	int rc;

	parm_list = kmalloc_obj(*parm_list);
	id = kmemdup(&appldata_id, sizeof(appldata_id), GFP_KERNEL);
	rc = -ENOMEM;
	if (parm_list && id) {
		id->record_nr = record_nr;
		id->mod_lvl = (mod_lvl[0]) << 8 | mod_lvl[1];
		rc = appldata_asm(parm_list, id, function,
				  (void *) buffer, length);
	}
	kfree(id);
	kfree(parm_list);
	return rc;
}
/************************ timer, work, DIAG <END> ****************************/


/****************************** /proc stuff **********************************/

#define APPLDATA_ADD_TIMER	0
#define APPLDATA_DEL_TIMER	1
#define APPLDATA_MOD_TIMER	2

/*
 * __appldata_timer_setup()
 *
 * Add, delete or modify the appldata delayed work.
 */
static void __appldata_timer_setup(int cmd)
{
	static DEFINE_MUTEX(appldata_cancel_lock);
	u64 now, elapsed, remaining;

	switch (cmd) {
	case APPLDATA_ADD_TIMER:
		mutex_lock(&appldata_cancel_lock);
		mutex_lock(&appldata_timer_lock);
		if (!appldata_timer_active) {
			appldata_cputime_start = appldata_total_cpu_time_ns();
			appldata_schedule_work(appldata_interval);
			appldata_timer_active = 1;
		}
		mutex_unlock(&appldata_timer_lock);
		mutex_unlock(&appldata_cancel_lock);
		break;
	case APPLDATA_DEL_TIMER:
		mutex_lock(&appldata_cancel_lock);
		mutex_lock(&appldata_timer_lock);
		if (!appldata_timer_active) {
			mutex_unlock(&appldata_timer_lock);
			mutex_unlock(&appldata_cancel_lock);
			break;
		}
		appldata_timer_active = 0;
		mutex_unlock(&appldata_timer_lock);
		cancel_delayed_work_sync(&appldata_work);
		mutex_unlock(&appldata_cancel_lock);
		break;
	case APPLDATA_MOD_TIMER:
		mutex_lock(&appldata_timer_lock);
		if (appldata_timer_active) {
			now = appldata_total_cpu_time_ns();
			elapsed = now - appldata_cputime_start;
			remaining = appldata_interval - elapsed;
			if (elapsed >= appldata_interval)
				remaining = 1;
			appldata_schedule_work(remaining);
		}
		mutex_unlock(&appldata_timer_lock);
		break;
	}
}

/*
 * appldata_timer_handler()
 *
 * Start/Stop timer, show status of timer (0 = not active, 1 = active)
 */
static int
appldata_timer_handler(const struct ctl_table *ctl, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	int timer_active = appldata_timer_active;
	int rc;
	struct ctl_table ctl_entry = {
		.procname	= ctl->procname,
		.data		= &timer_active,
		.maxlen		= sizeof(int),
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	};

	rc = proc_douintvec_minmax(&ctl_entry, write, buffer, lenp, ppos);
	if (rc < 0 || !write)
		return rc;

	if (timer_active)
		__appldata_timer_setup(APPLDATA_ADD_TIMER);
	else
		__appldata_timer_setup(APPLDATA_DEL_TIMER);
	return 0;
}

/*
 * appldata_interval_handler()
 *
 * Set (CPU) timer interval for collection of data (in milliseconds), show
 * current timer interval.
 */
static int
appldata_interval_handler(const struct ctl_table *ctl, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	int interval = appldata_interval / NSEC_PER_MSEC;
	int rc;
	struct ctl_table ctl_entry = {
		.procname	= ctl->procname,
		.data		= &interval,
		.maxlen		= sizeof(int),
		.extra1		= SYSCTL_ONE,
	};

	rc = proc_dointvec_minmax(&ctl_entry, write, buffer, lenp, ppos);
	if (rc < 0 || !write)
		return rc;

	mutex_lock(&appldata_timer_lock);
	appldata_interval = interval * NSEC_PER_MSEC;
	mutex_unlock(&appldata_timer_lock);
	__appldata_timer_setup(APPLDATA_MOD_TIMER);
	return 0;
}

/*
 * appldata_generic_handler()
 *
 * Generic start/stop monitoring and DIAG, show status of
 * monitoring (0 = not in process, 1 = in process)
 */
static int
appldata_generic_handler(const struct ctl_table *ctl, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	struct appldata_ops *ops = NULL, *tmp_ops;
	struct list_head *lh;
	int rc, found;
	int active;
	struct ctl_table ctl_entry = {
		.data		= &active,
		.maxlen		= sizeof(int),
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	};

	found = 0;
	mutex_lock(&appldata_ops_mutex);
	list_for_each(lh, &appldata_ops_list) {
		tmp_ops = list_entry(lh, struct appldata_ops, list);
		if (&tmp_ops->ctl_table[0] == ctl) {
			found = 1;
		}
	}
	if (!found) {
		mutex_unlock(&appldata_ops_mutex);
		return -ENODEV;
	}
	ops = ctl->data;
	if (!try_module_get(ops->owner)) {	// protect this function
		mutex_unlock(&appldata_ops_mutex);
		return -ENODEV;
	}
	mutex_unlock(&appldata_ops_mutex);

	active = ops->active;
	rc = proc_douintvec_minmax(&ctl_entry, write, buffer, lenp, ppos);
	if (rc < 0 || !write) {
		module_put(ops->owner);
		return rc;
	}

	mutex_lock(&appldata_ops_mutex);
	if (active && (ops->active == 0)) {
		// protect work queue callback
		if (!try_module_get(ops->owner)) {
			mutex_unlock(&appldata_ops_mutex);
			module_put(ops->owner);
			return -ENODEV;
		}
		ops->callback(ops->data);	// init record
		rc = appldata_diag(ops->record_nr,
					APPLDATA_START_INTERVAL_REC,
					(unsigned long) ops->data, ops->size,
					ops->mod_lvl);
		if (rc != 0) {
			pr_err("Starting the data collection for %s "
			       "failed with rc=%d\n", ops->name, rc);
			module_put(ops->owner);
		} else
			ops->active = 1;
	} else if (!active && (ops->active == 1)) {
		ops->active = 0;
		rc = appldata_diag(ops->record_nr, APPLDATA_STOP_REC,
				(unsigned long) ops->data, ops->size,
				ops->mod_lvl);
		if (rc != 0)
			pr_err("Stopping the data collection for %s "
			       "failed with rc=%d\n", ops->name, rc);
		module_put(ops->owner);
	}
	mutex_unlock(&appldata_ops_mutex);
	module_put(ops->owner);
	return 0;
}

/*************************** /proc stuff <END> *******************************/


/************************* module-ops management *****************************/
/*
 * appldata_register_ops()
 *
 * update ops list, register /proc/sys entries
 */
int appldata_register_ops(struct appldata_ops *ops)
{
	if (ops->size > APPLDATA_MAX_REC_SIZE)
		return -EINVAL;

	ops->ctl_table = kzalloc_objs(struct ctl_table, 1);
	if (!ops->ctl_table)
		return -ENOMEM;

	mutex_lock(&appldata_ops_mutex);
	list_add(&ops->list, &appldata_ops_list);
	mutex_unlock(&appldata_ops_mutex);

	ops->ctl_table[0].procname = ops->name;
	ops->ctl_table[0].mode = S_IRUGO | S_IWUSR;
	ops->ctl_table[0].proc_handler = appldata_generic_handler;
	ops->ctl_table[0].data = ops;

	ops->sysctl_header = register_sysctl_sz(appldata_proc_name, ops->ctl_table, 1);
	if (!ops->sysctl_header)
		goto out;
	return 0;
out:
	mutex_lock(&appldata_ops_mutex);
	list_del(&ops->list);
	mutex_unlock(&appldata_ops_mutex);
	kfree(ops->ctl_table);
	return -ENOMEM;
}

/*
 * appldata_unregister_ops()
 *
 * update ops list, unregister /proc entries, stop DIAG if necessary
 */
void appldata_unregister_ops(struct appldata_ops *ops)
{
	mutex_lock(&appldata_ops_mutex);
	list_del(&ops->list);
	mutex_unlock(&appldata_ops_mutex);
	unregister_sysctl_table(ops->sysctl_header);
	kfree(ops->ctl_table);
}
/********************** module-ops management <END> **************************/


/******************************* init / exit *********************************/

static int appldata_cpu_online(unsigned int cpu)
{
	__appldata_timer_setup(APPLDATA_MOD_TIMER);
	return 0;
}

static int __init appldata_init(void)
{
	int rc;

	rc = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "s390/appldata:online",
				       appldata_cpu_online, NULL);
	if (rc < 0)
		return rc;
	register_sysctl(appldata_proc_name, appldata_table);
	return 0;
}

__initcall(appldata_init);

/**************************** init / exit <END> ******************************/

EXPORT_SYMBOL_GPL(appldata_register_ops);
EXPORT_SYMBOL_GPL(appldata_unregister_ops);
EXPORT_SYMBOL_GPL(appldata_diag);

#ifdef CONFIG_SWAP
EXPORT_SYMBOL_GPL(si_swapinfo);
#endif
EXPORT_SYMBOL_GPL(nr_threads);
EXPORT_SYMBOL_GPL(nr_running);
EXPORT_SYMBOL_GPL(nr_iowait);
