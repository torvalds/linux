/*
 * arch/s390/appldata/appldata_base.c
 *
 * Base infrastructure for Linux-z/VM Monitor Stream, Stage 1.
 * Exports appldata_register_ops() and appldata_unregister_ops() for the
 * data gathering modules.
 *
 * Copyright IBM Corp. 2003, 2008
 *
 * Author: Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#define KMSG_COMPONENT	"appldata"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
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
#include <asm/appldata.h>
#include <asm/timer.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/smp.h>

#include "appldata.h"


#define APPLDATA_CPU_INTERVAL	10000		/* default (CPU) time for
						   sampling interval in
						   milliseconds */

#define TOD_MICRO	0x01000			/* nr. of TOD clock units
						   for 1 microsecond */
/*
 * /proc entries (sysctl)
 */
static const char appldata_proc_name[APPLDATA_PROC_NAME_LENGTH] = "appldata";
static int appldata_timer_handler(ctl_table *ctl, int write, struct file *filp,
				  void __user *buffer, size_t *lenp, loff_t *ppos);
static int appldata_interval_handler(ctl_table *ctl, int write,
					 struct file *filp,
					 void __user *buffer,
					 size_t *lenp, loff_t *ppos);

static struct ctl_table_header *appldata_sysctl_header;
static struct ctl_table appldata_table[] = {
	{
		.procname	= "timer",
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= &appldata_timer_handler,
	},
	{
		.procname	= "interval",
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= &appldata_interval_handler,
	},
	{ },
};

static struct ctl_table appldata_dir_table[] = {
	{
		.procname	= appldata_proc_name,
		.maxlen		= 0,
		.mode		= S_IRUGO | S_IXUGO,
		.child		= appldata_table,
	},
	{ },
};

/*
 * Timer
 */
static DEFINE_PER_CPU(struct vtimer_list, appldata_timer);
static atomic_t appldata_expire_count = ATOMIC_INIT(0);

static DEFINE_SPINLOCK(appldata_timer_lock);
static int appldata_interval = APPLDATA_CPU_INTERVAL;
static int appldata_timer_active;

/*
 * Work queue
 */
static struct workqueue_struct *appldata_wq;
static void appldata_work_fn(struct work_struct *work);
static DECLARE_WORK(appldata_work, appldata_work_fn);


/*
 * Ops list
 */
static DEFINE_SPINLOCK(appldata_ops_lock);
static LIST_HEAD(appldata_ops_list);


/*************************** timer, work, DIAG *******************************/
/*
 * appldata_timer_function()
 *
 * schedule work and reschedule timer
 */
static void appldata_timer_function(unsigned long data)
{
	if (atomic_dec_and_test(&appldata_expire_count)) {
		atomic_set(&appldata_expire_count, num_online_cpus());
		queue_work(appldata_wq, (struct work_struct *) data);
	}
}

/*
 * appldata_work_fn()
 *
 * call data gathering function for each (active) module
 */
static void appldata_work_fn(struct work_struct *work)
{
	struct list_head *lh;
	struct appldata_ops *ops;
	int i;

	i = 0;
	get_online_cpus();
	spin_lock(&appldata_ops_lock);
	list_for_each(lh, &appldata_ops_list) {
		ops = list_entry(lh, struct appldata_ops, list);
		if (ops->active == 1) {
			ops->callback(ops->data);
		}
	}
	spin_unlock(&appldata_ops_lock);
	put_online_cpus();
}

/*
 * appldata_diag()
 *
 * prepare parameter list, issue DIAG 0xDC
 */
int appldata_diag(char record_nr, u16 function, unsigned long buffer,
			u16 length, char *mod_lvl)
{
	struct appldata_product_id id = {
		.prod_nr    = {0xD3, 0xC9, 0xD5, 0xE4,
			       0xE7, 0xD2, 0xD9},	/* "LINUXKR" */
		.prod_fn    = 0xD5D3,			/* "NL" */
		.version_nr = 0xF2F6,			/* "26" */
		.release_nr = 0xF0F1,			/* "01" */
	};

	id.record_nr = record_nr;
	id.mod_lvl = (mod_lvl[0]) << 8 | mod_lvl[1];
	return appldata_asm(&id, function, (void *) buffer, length);
}
/************************ timer, work, DIAG <END> ****************************/


/****************************** /proc stuff **********************************/

/*
 * appldata_mod_vtimer_wrap()
 *
 * wrapper function for mod_virt_timer(), because smp_call_function_single()
 * accepts only one parameter.
 */
static void __appldata_mod_vtimer_wrap(void *p) {
	struct {
		struct vtimer_list *timer;
		u64    expires;
	} *args = p;
	mod_virt_timer_periodic(args->timer, args->expires);
}

#define APPLDATA_ADD_TIMER	0
#define APPLDATA_DEL_TIMER	1
#define APPLDATA_MOD_TIMER	2

/*
 * __appldata_vtimer_setup()
 *
 * Add, delete or modify virtual timers on all online cpus.
 * The caller needs to get the appldata_timer_lock spinlock.
 */
static void
__appldata_vtimer_setup(int cmd)
{
	u64 per_cpu_interval;
	int i;

	switch (cmd) {
	case APPLDATA_ADD_TIMER:
		if (appldata_timer_active)
			break;
		per_cpu_interval = (u64) (appldata_interval*1000 /
					  num_online_cpus()) * TOD_MICRO;
		for_each_online_cpu(i) {
			per_cpu(appldata_timer, i).expires = per_cpu_interval;
			smp_call_function_single(i, add_virt_timer_periodic,
						 &per_cpu(appldata_timer, i),
						 1);
		}
		appldata_timer_active = 1;
		break;
	case APPLDATA_DEL_TIMER:
		for_each_online_cpu(i)
			del_virt_timer(&per_cpu(appldata_timer, i));
		if (!appldata_timer_active)
			break;
		appldata_timer_active = 0;
		atomic_set(&appldata_expire_count, num_online_cpus());
		break;
	case APPLDATA_MOD_TIMER:
		per_cpu_interval = (u64) (appldata_interval*1000 /
					  num_online_cpus()) * TOD_MICRO;
		if (!appldata_timer_active)
			break;
		for_each_online_cpu(i) {
			struct {
				struct vtimer_list *timer;
				u64    expires;
			} args;
			args.timer = &per_cpu(appldata_timer, i);
			args.expires = per_cpu_interval;
			smp_call_function_single(i, __appldata_mod_vtimer_wrap,
						 &args, 1);
		}
	}
}

/*
 * appldata_timer_handler()
 *
 * Start/Stop timer, show status of timer (0 = not active, 1 = active)
 */
static int
appldata_timer_handler(ctl_table *ctl, int write, struct file *filp,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int len;
	char buf[2];

	if (!*lenp || *ppos) {
		*lenp = 0;
		return 0;
	}
	if (!write) {
		len = sprintf(buf, appldata_timer_active ? "1\n" : "0\n");
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		goto out;
	}
	len = *lenp;
	if (copy_from_user(buf, buffer, len > sizeof(buf) ? sizeof(buf) : len))
		return -EFAULT;
	get_online_cpus();
	spin_lock(&appldata_timer_lock);
	if (buf[0] == '1')
		__appldata_vtimer_setup(APPLDATA_ADD_TIMER);
	else if (buf[0] == '0')
		__appldata_vtimer_setup(APPLDATA_DEL_TIMER);
	spin_unlock(&appldata_timer_lock);
	put_online_cpus();
out:
	*lenp = len;
	*ppos += len;
	return 0;
}

/*
 * appldata_interval_handler()
 *
 * Set (CPU) timer interval for collection of data (in milliseconds), show
 * current timer interval.
 */
static int
appldata_interval_handler(ctl_table *ctl, int write, struct file *filp,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int len, interval;
	char buf[16];

	if (!*lenp || *ppos) {
		*lenp = 0;
		return 0;
	}
	if (!write) {
		len = sprintf(buf, "%i\n", appldata_interval);
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		goto out;
	}
	len = *lenp;
	if (copy_from_user(buf, buffer, len > sizeof(buf) ? sizeof(buf) : len)) {
		return -EFAULT;
	}
	interval = 0;
	sscanf(buf, "%i", &interval);
	if (interval <= 0)
		return -EINVAL;

	get_online_cpus();
	spin_lock(&appldata_timer_lock);
	appldata_interval = interval;
	__appldata_vtimer_setup(APPLDATA_MOD_TIMER);
	spin_unlock(&appldata_timer_lock);
	put_online_cpus();
out:
	*lenp = len;
	*ppos += len;
	return 0;
}

/*
 * appldata_generic_handler()
 *
 * Generic start/stop monitoring and DIAG, show status of
 * monitoring (0 = not in process, 1 = in process)
 */
static int
appldata_generic_handler(ctl_table *ctl, int write, struct file *filp,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct appldata_ops *ops = NULL, *tmp_ops;
	int rc, len, found;
	char buf[2];
	struct list_head *lh;

	found = 0;
	spin_lock(&appldata_ops_lock);
	list_for_each(lh, &appldata_ops_list) {
		tmp_ops = list_entry(lh, struct appldata_ops, list);
		if (&tmp_ops->ctl_table[2] == ctl) {
			found = 1;
		}
	}
	if (!found) {
		spin_unlock(&appldata_ops_lock);
		return -ENODEV;
	}
	ops = ctl->data;
	if (!try_module_get(ops->owner)) {	// protect this function
		spin_unlock(&appldata_ops_lock);
		return -ENODEV;
	}
	spin_unlock(&appldata_ops_lock);

	if (!*lenp || *ppos) {
		*lenp = 0;
		module_put(ops->owner);
		return 0;
	}
	if (!write) {
		len = sprintf(buf, ops->active ? "1\n" : "0\n");
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len)) {
			module_put(ops->owner);
			return -EFAULT;
		}
		goto out;
	}
	len = *lenp;
	if (copy_from_user(buf, buffer,
			   len > sizeof(buf) ? sizeof(buf) : len)) {
		module_put(ops->owner);
		return -EFAULT;
	}

	spin_lock(&appldata_ops_lock);
	if ((buf[0] == '1') && (ops->active == 0)) {
		// protect work queue callback
		if (!try_module_get(ops->owner)) {
			spin_unlock(&appldata_ops_lock);
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
	} else if ((buf[0] == '0') && (ops->active == 1)) {
		ops->active = 0;
		rc = appldata_diag(ops->record_nr, APPLDATA_STOP_REC,
				(unsigned long) ops->data, ops->size,
				ops->mod_lvl);
		if (rc != 0)
			pr_err("Stopping the data collection for %s "
			       "failed with rc=%d\n", ops->name, rc);
		module_put(ops->owner);
	}
	spin_unlock(&appldata_ops_lock);
out:
	*lenp = len;
	*ppos += len;
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

	ops->ctl_table = kzalloc(4 * sizeof(struct ctl_table), GFP_KERNEL);
	if (!ops->ctl_table)
		return -ENOMEM;

	spin_lock(&appldata_ops_lock);
	list_add(&ops->list, &appldata_ops_list);
	spin_unlock(&appldata_ops_lock);

	ops->ctl_table[0].procname = appldata_proc_name;
	ops->ctl_table[0].maxlen   = 0;
	ops->ctl_table[0].mode     = S_IRUGO | S_IXUGO;
	ops->ctl_table[0].child    = &ops->ctl_table[2];

	ops->ctl_table[2].procname = ops->name;
	ops->ctl_table[2].mode     = S_IRUGO | S_IWUSR;
	ops->ctl_table[2].proc_handler = appldata_generic_handler;
	ops->ctl_table[2].data = ops;

	ops->sysctl_header = register_sysctl_table(ops->ctl_table);
	if (!ops->sysctl_header)
		goto out;
	return 0;
out:
	spin_lock(&appldata_ops_lock);
	list_del(&ops->list);
	spin_unlock(&appldata_ops_lock);
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
	spin_lock(&appldata_ops_lock);
	list_del(&ops->list);
	spin_unlock(&appldata_ops_lock);
	unregister_sysctl_table(ops->sysctl_header);
	kfree(ops->ctl_table);
}
/********************** module-ops management <END> **************************/


/******************************* init / exit *********************************/

static void __cpuinit appldata_online_cpu(int cpu)
{
	init_virt_timer(&per_cpu(appldata_timer, cpu));
	per_cpu(appldata_timer, cpu).function = appldata_timer_function;
	per_cpu(appldata_timer, cpu).data = (unsigned long)
		&appldata_work;
	atomic_inc(&appldata_expire_count);
	spin_lock(&appldata_timer_lock);
	__appldata_vtimer_setup(APPLDATA_MOD_TIMER);
	spin_unlock(&appldata_timer_lock);
}

static void __cpuinit appldata_offline_cpu(int cpu)
{
	del_virt_timer(&per_cpu(appldata_timer, cpu));
	if (atomic_dec_and_test(&appldata_expire_count)) {
		atomic_set(&appldata_expire_count, num_online_cpus());
		queue_work(appldata_wq, &appldata_work);
	}
	spin_lock(&appldata_timer_lock);
	__appldata_vtimer_setup(APPLDATA_MOD_TIMER);
	spin_unlock(&appldata_timer_lock);
}

static int __cpuinit appldata_cpu_notify(struct notifier_block *self,
					 unsigned long action,
					 void *hcpu)
{
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		appldata_online_cpu((long) hcpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		appldata_offline_cpu((long) hcpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata appldata_nb = {
	.notifier_call = appldata_cpu_notify,
};

/*
 * appldata_init()
 *
 * init timer, register /proc entries
 */
static int __init appldata_init(void)
{
	int i;

	appldata_wq = create_singlethread_workqueue("appldata");
	if (!appldata_wq)
		return -ENOMEM;

	get_online_cpus();
	for_each_online_cpu(i)
		appldata_online_cpu(i);
	put_online_cpus();

	/* Register cpu hotplug notifier */
	register_hotcpu_notifier(&appldata_nb);

	appldata_sysctl_header = register_sysctl_table(appldata_dir_table);
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
