// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * Author: Cerf Yu <cerf.yu@rock-chips.com>
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rga2.h"
#include "RGA2_API.h"
#include "rga2_mmu_info.h"
#include "rga2_debugger.h"

#define RGA_DEBUGGER_ROOT_NAME  "rkrga"

#define STR_ENABLE(en) (en ? "EN" : "DIS")

extern struct rga2_drvdata_t *rga2_drvdata;

void rga2_slt(void);

int RGA2_TEST_REG;
int RGA2_TEST_MSG;
int RGA2_TEST_TIME;
int RGA2_CHECK_MODE;
int RGA2_NONUSE;
int RGA2_INT_FLAG;

static int rga_debug_show(struct seq_file *m, void *data)
{
	seq_printf(m, "REG   [%s]\n"
		      "MSG   [%s]\n"
		      "TIME  [%s]\n"
		      "INT   [%s]\n"
		      "CHECK [%s]\n"
		      "STOP  [%s]\n",
		   STR_ENABLE(RGA2_TEST_REG), STR_ENABLE(RGA2_TEST_MSG),
		   STR_ENABLE(RGA2_TEST_TIME), STR_ENABLE(RGA2_CHECK_MODE),
		   STR_ENABLE(RGA2_NONUSE), STR_ENABLE(RGA2_INT_FLAG));

	seq_puts(m, "\nhelp:\n");
	seq_puts(m, "  'echo reg   > debug' to enable/disable register log printing.\n");
	seq_puts(m, "  'echo msg   > debug' to enable/disable message log printing.\n");
	seq_puts(m, "  'echo time  > debug' to enable/disable time log printing.\n");
	seq_puts(m, "  'echo int   > debug' to enable/disable interruppt log printing.\n");
	seq_puts(m, "  'echo check > debug' to enable/disable check mode.\n");
	seq_puts(m, "  'echo stop  > debug' to enable/disable stop using hardware\n");

	return 0;
}

static ssize_t rga_debug_write(struct file *file, const char __user *ubuf,
			      size_t len, loff_t *offp)
{
	char buf[14];

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	if (strncmp(buf, "reg", 4) == 0) {
		if (RGA2_TEST_REG) {
			RGA2_TEST_REG = 0;
			INFO("close rga2 reg!\n");
		} else {
			RGA2_TEST_REG = 1;
			INFO("open rga2 reg!\n");
		}
	} else if (strncmp(buf, "msg", 3) == 0) {
		if (RGA2_TEST_MSG) {
			RGA2_TEST_MSG = 0;
			INFO("close rga2 test MSG!\n");
		} else {
			RGA2_TEST_MSG = 1;
			INFO("open rga2 test MSG!\n");
		}
	} else if (strncmp(buf, "time", 4) == 0) {
		if (RGA2_TEST_TIME) {
			RGA2_TEST_TIME = 0;
			INFO("close rga2 test time!\n");
		} else {
			RGA2_TEST_TIME = 1;
			INFO("open rga2 test time!\n");
		}
	} else if (strncmp(buf, "check", 5) == 0) {
		if (RGA2_CHECK_MODE) {
			RGA2_CHECK_MODE = 0;
			INFO("close rga2 check flag!\n");
		} else {
			RGA2_CHECK_MODE = 1;
			INFO("open rga2 check flag!\n");
		}
	} else if (strncmp(buf, "stop", 4) == 0) {
		if (RGA2_NONUSE) {
			RGA2_NONUSE = 0;
			INFO("stop using rga hardware!\n");
		} else {
			RGA2_NONUSE = 1;
			INFO("use rga hardware!\n");
		}
	} else if (strncmp(buf, "int", 3) == 0) {
		if (RGA2_INT_FLAG) {
			RGA2_INT_FLAG = 0;
			INFO("close inturrupt MSG!\n");
		} else {
			RGA2_INT_FLAG = 1;
			INFO("open inturrupt MSG!\n");
		}
	} else if (strncmp(buf, "slt", 3) == 0) {
		rga2_slt();
	}

	return len;
}

static int rga_version_show(struct seq_file *m, void *data)
{
	seq_printf(m, "%s: v%s\n", DRIVER_DESC, DRIVER_VERSION);

	return 0;
}

struct rga_debugger_list rga_root_list[] = {
	{ "debug", rga_debug_show, rga_debug_write, NULL },
	{ "driver_version", rga_version_show, NULL, NULL },
};

static ssize_t rga_debugger_write(struct file *file, const char __user *ubuf,
				  size_t len, loff_t *offp)
{
	struct seq_file *priv = file->private_data;
	struct rga_debugger_node *node = priv->private;

	if (node->info_ent->write)
		return node->info_ent->write(file, ubuf, len, offp);
	else
		return len;
}

#ifdef CONFIG_ROCKCHIP_RGA2_DEBUG_FS
static int rga_debugfs_open(struct inode *inode, struct file *file)
{
	struct rga_debugger_node *node = inode->i_private;

	return single_open(file, node->info_ent->show, node);
}

static const struct file_operations rga_debugfs_fops = {
	.owner	 = THIS_MODULE,
	.open	 = rga_debugfs_open,
	.read	 = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.write	 = rga_debugger_write,
};

static int rga_debugfs_remove_files(struct rga_debugger *debugger)
{
	struct rga_debugger_node *pos, *q;
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

static int rga_debugfs_create_files(const struct rga_debugger_list *files, int count,
			     struct dentry *root, struct rga_debugger *debugger)
{
	int i;
	struct dentry *ent;
	struct rga_debugger_node *tmp;

	for (i = 0; i < count; i++) {
		tmp = kmalloc(sizeof(struct rga_debugger_node), GFP_KERNEL);
		if (tmp == NULL) {
			ERR("Cannot alloc rga_debugger_node for /sys/kernel/debug/%pd/%s\n",
			    root, files[i].name);
			goto MALLOC_FAIL;
		}

		tmp->info_ent = &files[i];
		tmp->debugger = debugger;

		ent = debugfs_create_file(files[i].name, S_IFREG | S_IRUGO,
					  root, tmp, &rga_debugfs_fops);
		if (!ent) {
			ERR("Cannot create /sys/kernel/debug/%pd/%s\n", root, files[i].name);
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
	rga_debugfs_remove_files(debugger);

	return -1;
}

int rga2_debugfs_remove(void)
{
	struct rga_debugger *debugger;

	debugger = rga2_drvdata->debugger;

	rga_debugfs_remove_files(debugger);

	return 0;
}

int rga2_debugfs_init(void)
{
	int ret;
	struct rga_debugger *debugger;

	debugger = rga2_drvdata->debugger;

	debugger->debugfs_dir = debugfs_create_dir(RGA_DEBUGGER_ROOT_NAME, NULL);
	if (IS_ERR_OR_NULL(debugger->debugfs_dir)) {
		ERR("failed on mkdir /sys/kernel/debug/%s\n", RGA_DEBUGGER_ROOT_NAME);
		debugger->debugfs_dir = NULL;
		return -EIO;
	}

	ret = rga_debugfs_create_files(rga_root_list, ARRAY_SIZE(rga_root_list),
				       debugger->debugfs_dir, debugger);
	if (ret) {
		ERR("Could not install rga_root_list debugfs\n");
		goto CREATE_FAIL;
	}

	return 0;

CREATE_FAIL:
	rga2_debugfs_remove();

	return ret;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA2_DEBUG_FS */

#ifdef CONFIG_ROCKCHIP_RGA2_PROC_FS
static int rga_procfs_open(struct inode *inode, struct file *file)
{
	struct rga_debugger_node *node = PDE_DATA(inode);

	return single_open(file, node->info_ent->show, node);
}

static const struct file_operations rga_procfs_fops = {
	.owner   = THIS_MODULE,
	.open    = rga_procfs_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.write   = rga_debugger_write,
};

static int rga_procfs_remove_files(struct rga_debugger *debugger)
{
	struct rga_debugger_node *pos, *q;
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

static int rga_procfs_create_files(const struct rga_debugger_list *files, int count,
			    struct proc_dir_entry *root, struct rga_debugger *debugger)
{
	int i;
	struct proc_dir_entry *ent;
	struct rga_debugger_node *tmp;

	for (i = 0; i < count; i++) {
		tmp = kmalloc(sizeof(struct rga_debugger_node), GFP_KERNEL);
		if (tmp == NULL) {
			ERR("Cannot alloc rga_debugger_node for /proc/%s/%s\n",
			    RGA_DEBUGGER_ROOT_NAME, files[i].name);
			goto MALLOC_FAIL;
		}

		tmp->info_ent = &files[i];
		tmp->debugger = debugger;

		ent = proc_create_data(files[i].name, S_IFREG | S_IRUGO,
				       root, &rga_procfs_fops, tmp);
		if (!ent) {
			ERR("Cannot create /proc/%s/%s\n", RGA_DEBUGGER_ROOT_NAME, files[i].name);
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
	rga_procfs_remove_files(debugger);
	return -1;
}

int rga2_procfs_remove(void)
{
	struct rga_debugger *debugger;

	debugger = rga2_drvdata->debugger;

	rga_procfs_remove_files(debugger);

	return 0;
}

int rga2_procfs_init(void)
{
	int ret;
	struct rga_debugger *debugger;

	debugger = rga2_drvdata->debugger;

	debugger->procfs_dir = proc_mkdir(RGA_DEBUGGER_ROOT_NAME, NULL);
	if (IS_ERR_OR_NULL(debugger->procfs_dir)) {
		ERR("failed on mkdir /proc/%s\n", RGA_DEBUGGER_ROOT_NAME);
		debugger->procfs_dir = NULL;
		return -EIO;
	}

	ret = rga_procfs_create_files(rga_root_list, ARRAY_SIZE(rga_root_list),
				      debugger->procfs_dir, debugger);
	if (ret) {
		ERR("Could not install rga_root_list procfs\n");
		goto CREATE_FAIL;
	}

	return 0;

CREATE_FAIL:
	rga2_procfs_remove();

	return ret;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA2_PROC_FS */
