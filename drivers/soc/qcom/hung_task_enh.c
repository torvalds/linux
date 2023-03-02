// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/syscore_ops.h>
#include <linux/sched/walt.h>

#include <trace/hooks/hung_task.h>

#define DEFAULT_MAX_IOWAIT_TASK 5

enum {
	TASK_DEFAULT = 0,
	TASK_IN_WHITELIST,
	TASK_IN_BLACKLIST,
};

enum {
	HUNG_TASK_MODE_WHITELIST = 0,
	HUNG_TASK_MODE_BLACKLIST,
};

/**
 * struct hung_task_enh_data
 * @read_pid: PID of the process will be read
 * @global_detect_mode: The global detect mode switch, 0-Whitelist/1-Blacklist
 * @curr_iowait_task_cnt: Count the number of iowait processes in one scan cycle
 * @max_iowait_task_cnt: Max number of iowait processes
 * @curr_iowait_timeout_cnt: Count how many times the iowait condition is met
 * @max_iowait_timeout_cnt: Max times of the iowait condition is met
 * @ctl_table_hdr: hung_task_enh ctl table header
 */
struct hung_task_enh_data {
	int read_pid;
	int global_detect_mode;
	int curr_iowait_task_cnt;
	int max_iowait_task_cnt;
	int curr_iowait_timeout_cnt;
	int max_iowait_timeout_cnt;
	struct ctl_table_header *ctl_table_hdr;
};

static struct hung_task_enh_data hung_task_enh;

void qcom_before_check_tasks(void *ignore, struct task_struct *t, unsigned long timeout,
							bool *need_check)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) t->android_vendor_data1;

	if ((hung_task_enh.global_detect_mode == HUNG_TASK_MODE_WHITELIST &&
			wts->hung_detect_status != TASK_IN_WHITELIST) ||
			(hung_task_enh.global_detect_mode == HUNG_TASK_MODE_BLACKLIST &&
			wts->hung_detect_status == TASK_IN_BLACKLIST)) {
		*need_check = false;
		return;
	}

	*need_check = true;

	if (unlikely(t->in_iowait) && (t->__state == TASK_UNINTERRUPTIBLE ||
			t->__state == TASK_STOPPED || t->__state == TASK_TRACED) &&
			t->last_switch_time != 0 &&
			time_is_before_jiffies(t->last_switch_time + timeout * HZ) &&
			(t->mm != NULL && t == t->group_leader))
		hung_task_enh.curr_iowait_task_cnt++;
}

void qcom_check_tasks_done(void *ignore, void *extra)
{
	if (hung_task_enh.curr_iowait_task_cnt >= hung_task_enh.max_iowait_task_cnt)
		hung_task_enh.curr_iowait_timeout_cnt++;
	else
		hung_task_enh.curr_iowait_timeout_cnt = 0;

	if (hung_task_enh.max_iowait_timeout_cnt != 0 &&
	   hung_task_enh.curr_iowait_timeout_cnt >= hung_task_enh.max_iowait_timeout_cnt)
		panic("Detect IO wait too long time for multiple tasks!\n");

	hung_task_enh.curr_iowait_task_cnt = 0;
}

static DEFINE_MUTEX(readpid_mutex);
static int read_pid_handler(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	mutex_lock(&readpid_mutex);
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	mutex_unlock(&readpid_mutex);

	return ret;
}

static int hung_task_handler(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	struct task_struct *task;
	struct walt_task_struct *wts;
	int pid_and_val[2] = {-1, -1};

	struct ctl_table tmp = {
		.data	= &pid_and_val,
		.maxlen	= sizeof(pid_and_val),
		.mode	= table->mode,
	};

	mutex_lock(&readpid_mutex);

	if (!write) {
		task = get_pid_task(find_vpid(hung_task_enh.read_pid),
				PIDTYPE_PID);
		if (!task) {
			ret = -ENOENT;
			goto unlock_mutex;
		}
		wts = (struct walt_task_struct *) task->android_vendor_data1;
		pid_and_val[0] = hung_task_enh.read_pid;
		pid_and_val[1] = wts->hung_detect_status;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		goto put_task;
	}

	ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	if (ret)
		goto unlock_mutex;

	if (pid_and_val[0] <= 0 || pid_and_val[1] < 0 || pid_and_val[1] > 1) {
		ret = -ENOENT;
		goto unlock_mutex;
	}

	task = get_pid_task(find_vpid(pid_and_val[0]), PIDTYPE_PID);
	if (!task) {
		ret = -ENOENT;
		goto unlock_mutex;
	}

	wts = (struct walt_task_struct *) task->android_vendor_data1;
	if (pid_and_val[1] == 1) {
		if (hung_task_enh.global_detect_mode == HUNG_TASK_MODE_WHITELIST)
			wts->hung_detect_status = TASK_IN_WHITELIST;
		else
			wts->hung_detect_status = TASK_IN_BLACKLIST;
	} else {
		wts->hung_detect_status = TASK_DEFAULT;
	}

put_task:
	put_task_struct(task);
unlock_mutex:
	mutex_unlock(&readpid_mutex);

	return ret;
}

struct ctl_table hung_task_table[] = {
	{
		.procname	= "global_detect_mode",
		.data		= &hung_task_enh.global_detect_mode,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "per_task_detect_mode",
		.data		= (int *) 0,
		.maxlen		= sizeof(unsigned int) * 2,
		.mode		= 0644,
		.proc_handler	= hung_task_handler,
	},
	{
		.procname	= "max_iowait_timeout_cnt",
		.data		= &hung_task_enh.max_iowait_timeout_cnt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "max_iowait_task_cnt",
		.data		= &hung_task_enh.max_iowait_task_cnt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "read_pid",
		.data		= &hung_task_enh.read_pid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= read_pid_handler,
		.extra1		= SYSCTL_ONE,
		.extra2		= SYSCTL_INT_MAX,
	},
	{ }
};

struct ctl_table hung_task_base_table[] = {
	{
		.procname	= "hung_task_enh",
		.mode		= 0555,
		.child		= hung_task_table,
	},
	{ }
};

static int __init hung_task_enh_init(void)
{
	int ret;

	hung_task_enh.max_iowait_task_cnt = DEFAULT_MAX_IOWAIT_TASK;

	ret = register_trace_android_vh_check_uninterrupt_tasks(
						qcom_before_check_tasks, NULL);
	if (ret)
		return ret;

	ret = register_trace_android_vh_check_uninterrupt_tasks_done(
						qcom_check_tasks_done, NULL);
	if (ret) {
		unregister_trace_android_vh_check_uninterrupt_tasks(
						qcom_before_check_tasks, NULL);
		return ret;
	}

	hung_task_enh.ctl_table_hdr = register_sysctl_table(
						hung_task_base_table);
	if (!hung_task_enh.ctl_table_hdr) {
		unregister_trace_android_vh_check_uninterrupt_tasks(
						qcom_before_check_tasks, NULL);
		unregister_trace_android_vh_check_uninterrupt_tasks_done(
						qcom_check_tasks_done, NULL);
		return -ENOMEM;
	}

	return ret;
}
late_initcall(hung_task_enh_init);

static void __exit hung_task_enh_exit(void)
{
	unregister_sysctl_table(hung_task_enh.ctl_table_hdr);
	unregister_trace_android_vh_check_uninterrupt_tasks(
						qcom_before_check_tasks, NULL);
	unregister_trace_android_vh_check_uninterrupt_tasks_done(
						qcom_check_tasks_done, NULL);
}
module_exit(hung_task_enh_exit);

MODULE_DESCRIPTION("QCOM Hung Task Enhancement");
MODULE_LICENSE("GPL");
