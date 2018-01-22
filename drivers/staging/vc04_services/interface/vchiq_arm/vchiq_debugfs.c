/**
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

/* Top-level debug info */
struct vchiq_debugfs_info {
	/* Global 'vchiq' debugfs entry used by all instances */
	struct dentry *vchiq_cfg_dir;

	/* one entry per client process */
	struct dentry *clients;

	/* log categories */
	struct dentry *log_categories;
};

static struct vchiq_debugfs_info debugfs_info;

/* Log category debugfs entries */
struct vchiq_debugfs_log_entry {
	const char *name;
	int *plevel;
	struct dentry *dir;
};

static struct vchiq_debugfs_log_entry vchiq_debugfs_log_entries[] = {
	{ "core", &vchiq_core_log_level },
	{ "msg",  &vchiq_core_msg_log_level },
	{ "sync", &vchiq_sync_log_level },
	{ "susp", &vchiq_susp_log_level },
	{ "arm",  &vchiq_arm_log_level },
};
static int n_log_entries = ARRAY_SIZE(vchiq_debugfs_log_entries);

static struct dentry *vchiq_clients_top(void);
static struct dentry *vchiq_debugfs_top(void);

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

	if (copy_from_user(kbuf, buffer, count) != 0)
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

/* create an entry under <debugfs>/vchiq/log for each log category */
static int vchiq_debugfs_create_log_entries(struct dentry *top)
{
	struct dentry *dir;
	size_t i;
	int ret = 0;

	dir = debugfs_create_dir("log", vchiq_debugfs_top());
	if (!dir)
		return -ENOMEM;
	debugfs_info.log_categories = dir;

	for (i = 0; i < n_log_entries; i++) {
		void *levp = (void *)vchiq_debugfs_log_entries[i].plevel;

		dir = debugfs_create_file(vchiq_debugfs_log_entries[i].name,
					  0644,
					  debugfs_info.log_categories,
					  levp,
					  &debugfs_log_fops);
		if (!dir) {
			ret = -ENOMEM;
			break;
		}

		vchiq_debugfs_log_entries[i].dir = dir;
	}
	return ret;
}

static int debugfs_usecount_show(struct seq_file *f, void *offset)
{
	VCHIQ_INSTANCE_T instance = f->private;
	int use_count;

	use_count = vchiq_instance_get_use_count(instance);
	seq_printf(f, "%d\n", use_count);

	return 0;
}

static int debugfs_usecount_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_usecount_show, inode->i_private);
}

static const struct file_operations debugfs_usecount_fops = {
	.owner		= THIS_MODULE,
	.open		= debugfs_usecount_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int debugfs_trace_show(struct seq_file *f, void *offset)
{
	VCHIQ_INSTANCE_T instance = f->private;
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
	VCHIQ_INSTANCE_T instance = f->private;
	char firstchar;

	if (copy_from_user(&firstchar, buffer, 1) != 0)
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
int vchiq_debugfs_add_instance(VCHIQ_INSTANCE_T instance)
{
	char pidstr[16];
	struct dentry *top, *use_count, *trace;
	struct dentry *clients = vchiq_clients_top();

	snprintf(pidstr, sizeof(pidstr), "%d",
		 vchiq_instance_get_pid(instance));

	top = debugfs_create_dir(pidstr, clients);
	if (!top)
		goto fail_top;

	use_count = debugfs_create_file("use_count",
					0444, top,
					instance,
					&debugfs_usecount_fops);
	if (!use_count)
		goto fail_use_count;

	trace = debugfs_create_file("trace",
				    0644, top,
				    instance,
				    &debugfs_trace_fops);
	if (!trace)
		goto fail_trace;

	vchiq_instance_get_debugfs_node(instance)->dentry = top;

	return 0;

fail_trace:
	debugfs_remove(use_count);
fail_use_count:
	debugfs_remove(top);
fail_top:
	return -ENOMEM;
}

void vchiq_debugfs_remove_instance(VCHIQ_INSTANCE_T instance)
{
	VCHIQ_DEBUGFS_NODE_T *node = vchiq_instance_get_debugfs_node(instance);

	debugfs_remove_recursive(node->dentry);
}

int vchiq_debugfs_init(void)
{
	BUG_ON(debugfs_info.vchiq_cfg_dir != NULL);

	debugfs_info.vchiq_cfg_dir = debugfs_create_dir("vchiq", NULL);
	if (debugfs_info.vchiq_cfg_dir == NULL)
		goto fail;

	debugfs_info.clients = debugfs_create_dir("clients",
				vchiq_debugfs_top());
	if (!debugfs_info.clients)
		goto fail;

	if (vchiq_debugfs_create_log_entries(vchiq_debugfs_top()) != 0)
		goto fail;

	return 0;

fail:
	vchiq_debugfs_deinit();
	vchiq_log_error(vchiq_arm_log_level,
		"%s: failed to create debugfs directory",
		__func__);

	return -ENOMEM;
}

/* remove all the debugfs entries */
void vchiq_debugfs_deinit(void)
{
	debugfs_remove_recursive(vchiq_debugfs_top());
}

static struct dentry *vchiq_clients_top(void)
{
	return debugfs_info.clients;
}

static struct dentry *vchiq_debugfs_top(void)
{
	BUG_ON(debugfs_info.vchiq_cfg_dir == NULL);
	return debugfs_info.vchiq_cfg_dir;
}

#else /* CONFIG_DEBUG_FS */

int vchiq_debugfs_init(void)
{
	return 0;
}

void vchiq_debugfs_deinit(void)
{
}

int vchiq_debugfs_add_instance(VCHIQ_INSTANCE_T instance)
{
	return 0;
}

void vchiq_debugfs_remove_instance(VCHIQ_INSTANCE_T instance)
{
}

#endif /* CONFIG_DEBUG_FS */
