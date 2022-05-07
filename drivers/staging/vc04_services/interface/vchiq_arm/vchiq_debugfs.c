// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 */

#include <linux/debugfs.h>
#include "vchiq_core.h"
#include "vchiq_arm.h"
#include "vchiq_debugfs.h"

#ifdef CONFIG_DEBUG_FS

/****************************************************************************
*
*   log category entries
*
***************************************************************************/
#define DEBUGFS_WRITE_BUF_SIZE 256

#define VCHIQ_LOG_ERROR_STR   "error"
#define VCHIQ_LOG_WARNING_STR "warning"
#define VCHIQ_LOG_INFO_STR    "info"
#define VCHIQ_LOG_TRACE_STR   "trace"

/* Global 'vchiq' debugfs and clients entry used by all instances */
static struct dentry *vchiq_dbg_dir;
static struct dentry *vchiq_dbg_clients;

/* Log category debugfs entries */
struct vchiq_debugfs_log_entry {
	const char *name;
	void *plevel;
};

static struct vchiq_debugfs_log_entry vchiq_debugfs_log_entries[] = {
	{ "core", &vchiq_core_log_level },
	{ "msg",  &vchiq_core_msg_log_level },
	{ "sync", &vchiq_sync_log_level },
	{ "susp", &vchiq_susp_log_level },
	{ "arm",  &vchiq_arm_log_level },
};
static int n_log_entries = ARRAY_SIZE(vchiq_debugfs_log_entries);

static int debugfs_log_show(struct seq_file *f, void *offset)
{
	int *levp = f->private;
	char *log_value = NULL;

	switch (*levp) {
	case VCHIQ_LOG_ERROR:
		log_value = VCHIQ_LOG_ERROR_STR;
		break;
	case VCHIQ_LOG_WARNING:
		log_value = VCHIQ_LOG_WARNING_STR;
		break;
	case VCHIQ_LOG_INFO:
		log_value = VCHIQ_LOG_INFO_STR;
		break;
	case VCHIQ_LOG_TRACE:
		log_value = VCHIQ_LOG_TRACE_STR;
		break;
	default:
		break;
	}

	seq_printf(f, "%s\n", log_value ? log_value : "(null)");

	return 0;
}

static int debugfs_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_log_show, inode->i_private);
}

static ssize_t debugfs_log_write(struct file *file,
	const char __user *buffer,
	size_t count, loff_t *ppos)
{
	struct seq_file *f = (struct seq_file *)file->private_data;
	int *levp = f->private;
	char kbuf[DEBUGFS_WRITE_BUF_SIZE + 1];

	memset(kbuf, 0, DEBUGFS_WRITE_BUF_SIZE + 1);
	if (count >= DEBUGFS_WRITE_BUF_SIZE)
		count = DEBUGFS_WRITE_BUF_SIZE;

	if (copy_from_user(kbuf, buffer, count))
		return -EFAULT;
	kbuf[count - 1] = 0;

	if (strncmp("error", kbuf, strlen("error")) == 0)
		*levp = VCHIQ_LOG_ERROR;
	else if (strncmp("warning", kbuf, strlen("warning")) == 0)
		*levp = VCHIQ_LOG_WARNING;
	else if (strncmp("info", kbuf, strlen("info")) == 0)
		*levp = VCHIQ_LOG_INFO;
	else if (strncmp("trace", kbuf, strlen("trace")) == 0)
		*levp = VCHIQ_LOG_TRACE;
	else
		*levp = VCHIQ_LOG_DEFAULT;

	*ppos += count;

	return count;
}

static const struct file_operations debugfs_log_fops = {
	.owner		= THIS_MODULE,
	.open		= debugfs_log_open,
	.write		= debugfs_log_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int debugfs_usecount_show(struct seq_file *f, void *offset)
{
	struct vchiq_instance *instance = f->private;
	int use_count;

	use_count = vchiq_instance_get_use_count(instance);
	seq_printf(f, "%d\n", use_count);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(debugfs_usecount);

static int debugfs_trace_show(struct seq_file *f, void *offset)
{
	struct vchiq_instance *instance = f->private;
	int trace;

	trace = vchiq_instance_get_trace(instance);
	seq_printf(f, "%s\n", trace ? "Y" : "N");

	return 0;
}

static int debugfs_trace_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_trace_show, inode->i_private);
}

static ssize_t debugfs_trace_write(struct file *file,
	const char __user *buffer,
	size_t count, loff_t *ppos)
{
	struct seq_file *f = (struct seq_file *)file->private_data;
	struct vchiq_instance *instance = f->private;
	char firstchar;

	if (copy_from_user(&firstchar, buffer, 1))
		return -EFAULT;

	switch (firstchar) {
	case 'Y':
	case 'y':
	case '1':
		vchiq_instance_set_trace(instance, 1);
		break;
	case 'N':
	case 'n':
	case '0':
		vchiq_instance_set_trace(instance, 0);
		break;
	default:
		break;
	}

	*ppos += count;

	return count;
}

static const struct file_operations debugfs_trace_fops = {
	.owner		= THIS_MODULE,
	.open		= debugfs_trace_open,
	.write		= debugfs_trace_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* add an instance (process) to the debugfs entries */
void vchiq_debugfs_add_instance(struct vchiq_instance *instance)
{
	char pidstr[16];
	struct dentry *top;

	snprintf(pidstr, sizeof(pidstr), "%d",
		 vchiq_instance_get_pid(instance));

	top = debugfs_create_dir(pidstr, vchiq_dbg_clients);

	debugfs_create_file("use_count", 0444, top, instance,
			    &debugfs_usecount_fops);
	debugfs_create_file("trace", 0644, top, instance, &debugfs_trace_fops);

	vchiq_instance_get_debugfs_node(instance)->dentry = top;
}

void vchiq_debugfs_remove_instance(struct vchiq_instance *instance)
{
	struct vchiq_debugfs_node *node =
				vchiq_instance_get_debugfs_node(instance);

	debugfs_remove_recursive(node->dentry);
}

void vchiq_debugfs_init(void)
{
	struct dentry *dir;
	int i;

	vchiq_dbg_dir = debugfs_create_dir("vchiq", NULL);
	vchiq_dbg_clients = debugfs_create_dir("clients", vchiq_dbg_dir);

	/* create an entry under <debugfs>/vchiq/log for each log category */
	dir = debugfs_create_dir("log", vchiq_dbg_dir);

	for (i = 0; i < n_log_entries; i++)
		debugfs_create_file(vchiq_debugfs_log_entries[i].name, 0644,
				    dir, vchiq_debugfs_log_entries[i].plevel,
				    &debugfs_log_fops);
}

/* remove all the debugfs entries */
void vchiq_debugfs_deinit(void)
{
	debugfs_remove_recursive(vchiq_dbg_dir);
}

#else /* CONFIG_DEBUG_FS */

void vchiq_debugfs_init(void)
{
}

void vchiq_debugfs_deinit(void)
{
}

void vchiq_debugfs_add_instance(struct vchiq_instance *instance)
{
}

void vchiq_debugfs_remove_instance(struct vchiq_instance *instance)
{
}

#endif /* CONFIG_DEBUG_FS */
