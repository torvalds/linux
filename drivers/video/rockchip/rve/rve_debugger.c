// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *	Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rve_debugger: " fmt

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rve.h"
#include "rve_debugger.h"
#include "rve_drv.h"

#define RVE_DEBUGGER_ROOT_NAME "rve"

#define STR_ENABLE(en) (en ? "EN" : "DIS")

int RVE_DEBUG_REG;
int RVE_DEBUG_MSG;
int RVE_DEBUG_TIME;
int RVE_DEBUG_CHECK_MODE;
int RVE_DEBUG_NONUSE;
int RVE_DEBUG_INT_FLAG;
int RVE_DEBUG_MONITOR;

static int rve_debug_show(struct seq_file *m, void *data)
{
	seq_printf(m, "REG [%s]\n"
		 "MSG [%s]\n"
		 "TIME [%s]\n"
		 "INT [%s]\n"
		 "CHECK [%s]\n"
		 "STOP [%s]\n"
		 "MONITOR [%s]",
		 STR_ENABLE(RVE_DEBUG_REG),
		 STR_ENABLE(RVE_DEBUG_MSG),
		 STR_ENABLE(RVE_DEBUG_TIME),
		 STR_ENABLE(RVE_DEBUG_INT_FLAG),
		 STR_ENABLE(RVE_DEBUG_CHECK_MODE),
		 STR_ENABLE(RVE_DEBUG_NONUSE),
		 STR_ENABLE(RVE_DEBUG_MONITOR));

	seq_puts(m, "\nhelp:\n");
	seq_puts(m,
		 " 'echo reg > debug' to enable/disable register log printing.\n");
	seq_puts(m,
		 " 'echo msg > debug' to enable/disable message log printing.\n");
	seq_puts(m,
		 " 'echo time > debug' to enable/disable time log printing.\n");
	seq_puts(m,
		 " 'echo int > debug' to enable/disable interruppt log printing.\n");
	seq_puts(m, " 'echo check > debug' to enable/disable check mode.\n");
	seq_puts(m,
		 " 'echo stop > debug' to enable/disable stop using hardware\n");
	seq_puts(m, " 'echo mon > debug' to enable/disable monitor");

	return 0;
}

static ssize_t rve_debug_write(struct file *file, const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	char buf[14];

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	if (strncmp(buf, "reg", 4) == 0) {
		if (RVE_DEBUG_REG) {
			RVE_DEBUG_REG = 0;
			pr_err("close rve reg!\n");
		} else {
			RVE_DEBUG_REG = 1;
			pr_err("open rve reg!\n");
		}
	} else if (strncmp(buf, "msg", 3) == 0) {
		if (RVE_DEBUG_MSG) {
			RVE_DEBUG_MSG = 0;
			pr_err("close rve test MSG!\n");
		} else {
			RVE_DEBUG_MSG = 1;
			pr_err("open rve test MSG!\n");
		}
	} else if (strncmp(buf, "time", 4) == 0) {
		if (RVE_DEBUG_TIME) {
			RVE_DEBUG_TIME = 0;
			pr_err("close rve test time!\n");
		} else {
			RVE_DEBUG_TIME = 1;
			pr_err("open rve test time!\n");
		}
	} else if (strncmp(buf, "check", 5) == 0) {
		if (RVE_DEBUG_CHECK_MODE) {
			RVE_DEBUG_CHECK_MODE = 0;
			pr_err("close rve check flag!\n");
		} else {
			RVE_DEBUG_CHECK_MODE = 1;
			pr_err("open rve check flag!\n");
		}
	} else if (strncmp(buf, "stop", 4) == 0) {
		if (RVE_DEBUG_NONUSE) {
			RVE_DEBUG_NONUSE = 0;
			pr_err("using rve hardware!\n");
		} else {
			RVE_DEBUG_NONUSE = 1;
			pr_err("stop using rve hardware!\n");
		}
	} else if (strncmp(buf, "int", 3) == 0) {
		if (RVE_DEBUG_INT_FLAG) {
			RVE_DEBUG_INT_FLAG = 0;
			pr_err("close inturrupt MSG!\n");
		} else {
			RVE_DEBUG_INT_FLAG = 1;
			pr_err("open inturrupt MSG!\n");
		}
	} else if (strncmp(buf, "mon", 3) == 0) {
		if (RVE_DEBUG_MONITOR) {
			RVE_DEBUG_MONITOR = 0;
			pr_err("close monitor!\n");
		} else {
			RVE_DEBUG_MONITOR = 1;
			pr_err("open monitor!\n");
		}
	} else if (strncmp(buf, "slt", 3) == 0) {
		pr_err("Null");
	}

	return len;
}

static int rve_version_show(struct seq_file *m, void *data)
{
	seq_printf(m, "%s: v%s\n", DRIVER_DESC, DRIVER_VERSION);

	return 0;
}

static int rve_load_show(struct seq_file *m, void *data)
{
	struct rve_scheduler_t *scheduler = NULL;
	unsigned long flags;
	int i;
	int load;
	u32 busy_time_total;

	seq_printf(m, "num of scheduler = %d\n", rve_drvdata->num_of_scheduler);
	seq_printf(m, "================= load ==================\n");

	for (i = 0; i < rve_drvdata->num_of_scheduler; i++) {
		scheduler = rve_drvdata->scheduler[i];

		seq_printf(m, "scheduler[%d]: %s\n",
			i, dev_driver_string(scheduler->dev));

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		busy_time_total = scheduler->timer.busy_time_record;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		load = (busy_time_total * 100000 / RVE_LOAD_INTERVAL);
		seq_printf(m, "\t load = %d\n", load);
		seq_printf(m, "-----------------------------------\n");
	}
	return 0;
}

static int rve_scheduler_show(struct seq_file *m, void *data)
{
	struct rve_scheduler_t *scheduler = NULL;
	int i;

	seq_printf(m, "num of scheduler = %d\n", rve_drvdata->num_of_scheduler);
	seq_printf(m, "===================================\n");

	for (i = 0; i < rve_drvdata->num_of_scheduler; i++) {
		scheduler = rve_drvdata->scheduler[i];

		seq_printf(m, "scheduler[%d]: %s\n", i, dev_driver_string(scheduler->dev));
		seq_printf(m, "-----------------------------------\n");
		seq_printf(m, "pd_ref = %d\n", scheduler->pd_refcount);
		seq_printf(m, "total_int_cnt = %llu\n", scheduler->total_int_cnt);
	}

	return 0;
}

static int rve_ctx_manager_show(struct seq_file *m, void *data)
{
	int id;
	struct rve_pending_ctx_manager *ctx_manager;
	struct rve_internal_ctx_t *ctx;
	unsigned long flags;
	int cmd_num = 0;
	int finished_job_count = 0;
	bool status = false;
	pid_t pid;

	ktime_t last_job_hw_use_time;
	ktime_t last_job_use_time;
	ktime_t hw_time_total;
	ktime_t max_cost_time_per_sec;
	uint32_t rd_bandwidth, wr_bandwidth, cycle_cnt;

	ctx_manager = rve_drvdata->pend_ctx_manager;

	seq_puts(m, "rve internal ctx dump:\n");
	seq_printf(m, "ctx count = %d\n", ctx_manager->ctx_count);

	mutex_lock(&ctx_manager->lock);

	idr_for_each_entry(&ctx_manager->ctx_id_idr, ctx, id) {
		seq_printf(m, "================= ctx id: %d =================\n", ctx->id);

		spin_lock_irqsave(&ctx->lock, flags);

		cmd_num = ctx->cmd_num;
		finished_job_count = ctx->finished_job_count;
		status = ctx->is_running;
		pid = ctx->debug_info.pid;
		last_job_hw_use_time = ctx->debug_info.last_job_hw_use_time;
		last_job_use_time = ctx->debug_info.last_job_use_time;
		hw_time_total = ctx->debug_info.hw_time_total;
		max_cost_time_per_sec = ctx->debug_info.max_cost_time_per_sec;
		rd_bandwidth = ctx->debug_info.rd_bandwidth;
		wr_bandwidth = ctx->debug_info.wr_bandwidth;
		cycle_cnt = ctx->debug_info.cycle_cnt;

		spin_unlock_irqrestore(&ctx->lock, flags);

		seq_printf(m, "----------------- RVE CTX INFO -----------------\n");
		seq_printf(m, "\t [pid: %d] status: %s\n", pid, status ? "active" : "pending");
		seq_printf(m, "\t set cmd num: %d\t finish job sum: %d\n",
				cmd_num, finished_job_count);
		seq_printf(m, "\t last_job_use_time: %llu us\t last_job_hw_use_time: %llu us",
				ktime_to_us(last_job_use_time), ktime_to_us(last_job_hw_use_time));
		seq_printf(m, "\t hw_time_total: %llu us\t max_cost_time_per_sec: %llu us",
				ktime_to_us(hw_time_total), ktime_to_us(max_cost_time_per_sec));
		seq_printf(m, "\t rd_bandwidth: %u bytes\t wr_bandwidth: %u bytes",
				rd_bandwidth, wr_bandwidth);
		seq_printf(m, "\t cycle_cnt: %u\t", cycle_cnt);

		seq_printf(m, "----------------- RVE INVOKE INFO -----------------\n");
		/* TODO: */
	}

	mutex_unlock(&ctx_manager->lock);

	return 0;
}


struct rve_debugger_list rve_debugger_root_list[] = {
	{"debug", rve_debug_show, rve_debug_write, NULL},
	{"driver_version", rve_version_show, NULL, NULL},
	{"load", rve_load_show, NULL, NULL},
	{"scheduler_status", rve_scheduler_show, NULL, NULL},
	{"ctx_manager", rve_ctx_manager_show, NULL, NULL},
};

#ifdef CONFIG_ROCKCHIP_RVE_DEBUG_FS

static ssize_t rve_debugger_write(struct file *file, const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	struct seq_file *priv = file->private_data;
	struct rve_debugger_node *node = priv->private;

	if (node->info_ent->write)
		return node->info_ent->write(file, ubuf, len, offp);
	else
		return len;
}

static int rve_debugfs_open(struct inode *inode, struct file *file)
{
	struct rve_debugger_node *node = inode->i_private;

	return single_open(file, node->info_ent->show, node);
}

static const struct file_operations rve_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rve_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rve_debugger_write,
};

static int rve_debugfs_remove_files(struct rve_debugger *debugger)
{
	struct rve_debugger_node *pos, *q;
	struct list_head *entry_list;

	mutex_lock(&debugger->debugfs_lock);

	/* Delete debugfs entry list */
	entry_list = &debugger->debugfs_entry_list;
	list_for_each_entry_safe(pos, q, entry_list, list) {
		if (pos->dent == NULL)
			continue;
		list_del(&pos->list);
		kfree(pos);
		pos = NULL;
	}

	/* Delete all debugfs node in this directory */
	debugfs_remove_recursive(debugger->debugfs_dir);
	debugger->debugfs_dir = NULL;

	mutex_unlock(&debugger->debugfs_lock);

	return 0;
}

static int rve_debugfs_create_files(const struct rve_debugger_list *files,
					int count, struct dentry *root,
					struct rve_debugger *debugger)
{
	int i;
	struct dentry *ent;
	struct rve_debugger_node *tmp;

	for (i = 0; i < count; i++) {
		tmp = kmalloc(sizeof(struct rve_debugger_node), GFP_KERNEL);
		if (tmp == NULL) {
			pr_err("Cannot alloc node path /sys/kernel/debug/%pd/%s\n",
				 root, files[i].name);
			goto MALLOC_FAIL;
		}

		tmp->info_ent = &files[i];
		tmp->debugger = debugger;

		ent = debugfs_create_file(files[i].name, S_IFREG | S_IRUGO,
					 root, tmp, &rve_debugfs_fops);
		if (!ent) {
			pr_err("Cannot create /sys/kernel/debug/%pd/%s\n", root,
				 files[i].name);
			goto CREATE_FAIL;
		}

		tmp->dent = ent;

		mutex_lock(&debugger->debugfs_lock);
		list_add_tail(&tmp->list, &debugger->debugfs_entry_list);
		mutex_unlock(&debugger->debugfs_lock);
	}

	return 0;

CREATE_FAIL:
	kfree(tmp);
MALLOC_FAIL:
	rve_debugfs_remove_files(debugger);

	return -1;
}

int rve_debugfs_remove(void)
{
	struct rve_debugger *debugger;

	debugger = rve_drvdata->debugger;

	rve_debugfs_remove_files(debugger);

	return 0;
}

int rve_debugfs_init(void)
{
	int ret;
	struct rve_debugger *debugger;

	debugger = rve_drvdata->debugger;

	debugger->debugfs_dir =
		debugfs_create_dir(RVE_DEBUGGER_ROOT_NAME, NULL);
	if (IS_ERR_OR_NULL(debugger->debugfs_dir)) {
		pr_err("failed on mkdir /sys/kernel/debug/%s\n",
			 RVE_DEBUGGER_ROOT_NAME);
		debugger->debugfs_dir = NULL;
		return -EIO;
	}

	ret = rve_debugfs_create_files(rve_debugger_root_list, ARRAY_SIZE(rve_debugger_root_list),
					 debugger->debugfs_dir, debugger);
	if (ret) {
		pr_err("Could not install rve_debugger_root_list debugfs\n");
		goto CREATE_FAIL;
	}

	return 0;

CREATE_FAIL:
	rve_debugfs_remove();

	return ret;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RVE_DEBUG_FS */

#ifdef CONFIG_ROCKCHIP_RVE_PROC_FS
static int rve_procfs_open(struct inode *inode, struct file *file)
{
	struct rve_debugger_node *node = PDE_DATA(inode);

	return single_open(file, node->info_ent->show, node);
}

static ssize_t rve_fops_write_u32(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int rc;
	struct seq_file *priv = file->private_data;

	rc = kstrtou32_from_user(buf, count, 0, priv->private);
	if (rc)
		return rc;

	return count;
}

static const struct proc_ops rve_procfs_fops = {
	.proc_open = rve_procfs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = rve_fops_write_u32,
};

static int rve_procfs_remove_files(struct rve_debugger *debugger)
{
	struct rve_debugger_node *pos, *q;
	struct list_head *entry_list;

	mutex_lock(&debugger->procfs_lock);

	/* Delete procfs entry list */
	entry_list = &debugger->procfs_entry_list;
	list_for_each_entry_safe(pos, q, entry_list, list) {
		if (pos->pent == NULL)
			continue;
		list_del(&pos->list);
		kfree(pos);
		pos = NULL;
	}

	/* Delete all procfs node in this directory */
	proc_remove(debugger->procfs_dir);
	debugger->procfs_dir = NULL;

	mutex_unlock(&debugger->procfs_lock);

	return 0;
}

static int rve_procfs_create_files(const struct rve_debugger_list *files,
				 int count, struct proc_dir_entry *root,
				 struct rve_debugger *debugger)
{
	int i;
	struct proc_dir_entry *ent;
	struct rve_debugger_node *tmp;

	for (i = 0; i < count; i++) {
		tmp = kmalloc(sizeof(struct rve_debugger_node), GFP_KERNEL);
		if (tmp == NULL) {
			pr_err("Cannot alloc node path for /proc/%s/%s\n",
				 RVE_DEBUGGER_ROOT_NAME, files[i].name);
			goto MALLOC_FAIL;
		}

		tmp->info_ent = &files[i];
		tmp->debugger = debugger;

		ent = proc_create_data(files[i].name, S_IFREG | S_IRUGO,
					 root, &rve_procfs_fops, tmp);
		if (!ent) {
			pr_err("Cannot create /proc/%s/%s\n",
				 RVE_DEBUGGER_ROOT_NAME, files[i].name);
			goto CREATE_FAIL;
		}

		tmp->pent = ent;

		mutex_lock(&debugger->procfs_lock);
		list_add_tail(&tmp->list, &debugger->procfs_entry_list);
		mutex_unlock(&debugger->procfs_lock);
	}

	return 0;

CREATE_FAIL:
	kfree(tmp);
MALLOC_FAIL:
	rve_procfs_remove_files(debugger);
	return -1;
}

int rve_procfs_remove(void)
{
	struct rve_debugger *debugger;

	debugger = rve_drvdata->debugger;

	rve_procfs_remove_files(debugger);

	return 0;
}

int rve_procfs_init(void)
{
	int ret;
	struct rve_debugger *debugger;

	debugger = rve_drvdata->debugger;

	debugger->procfs_dir = proc_mkdir(RVE_DEBUGGER_ROOT_NAME, NULL);
	if (IS_ERR_OR_NULL(debugger->procfs_dir)) {
		pr_err("failed on mkdir /proc/%s\n", RVE_DEBUGGER_ROOT_NAME);
		debugger->procfs_dir = NULL;
		return -EIO;
	}

	ret = rve_procfs_create_files(rve_debugger_root_list, ARRAY_SIZE(rve_debugger_root_list),
					 debugger->procfs_dir, debugger);
	if (ret) {
		pr_err("Could not install rve_debugger_root_list procfs\n");
		goto CREATE_FAIL;
	}

	return 0;

CREATE_FAIL:
	rve_procfs_remove();

	return ret;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RVE_PROC_FS */

