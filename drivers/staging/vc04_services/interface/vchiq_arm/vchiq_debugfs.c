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

#include <linux/defs.h>
#include "vchiq_core.h"
#include "vchiq_arm.h"
#include "vchiq_defs.h"

#ifdef CONFIG_DE_FS

/****************************************************************************
*
*   log category entries
*
***************************************************************************/
#define DEFS_WRITE_BUF_SIZE 256

#define VCHIQ_LOG_ERROR_STR   "error"
#define VCHIQ_LOG_WARNING_STR "warning"
#define VCHIQ_LOG_INFO_STR    "info"
#define VCHIQ_LOG_TRACE_STR   "trace"

/* Global 'vchiq' defs and clients entry used by all instances */
static struct dentry *vchiq_dbg_dir;
static struct dentry *vchiq_dbg_clients;

/* Log category defs entries */
struct vchiq_defs_log_entry {
	const char *name;
	void *plevel;
};

static struct vchiq_defs_log_entry vchiq_defs_log_entries[] = {
	{ "core", &vchiq_core_log_level },
	{ "msg",  &vchiq_core_msg_log_level },
	{ "sync", &vchiq_sync_log_level },
	{ "susp", &vchiq_susp_log_level },
	{ "arm",  &vchiq_arm_log_level },
};
static int n_log_entries = ARRAY_SIZE(vchiq_defs_log_entries);

static int defs_log_show(struct seq_file *f, void *offset)
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

static int defs_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, defs_log_show, inode->i_private);
}

static ssize_t defs_log_write(struct file *file,
	const char __user *buffer,
	size_t count, loff_t *ppos)
{
	struct seq_file *f = (struct seq_file *)file->private_data;
	int *levp = f->private;
	char kbuf[DEFS_WRITE_BUF_SIZE + 1];

	memset(kbuf, 0, DEFS_WRITE_BUF_SIZE + 1);
	if (count >= DEFS_WRITE_BUF_SIZE)
		count = DEFS_WRITE_BUF_SIZE;

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

static const struct file_operations defs_log_fops = {
	.owner		= THIS_MODULE,
	.open		= defs_log_open,
	.write		= defs_log_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int defs_usecount_show(struct seq_file *f, void *offset)
{
	VCHIQ_INSTANCE_T instance = f->private;
	int use_count;

	use_count = vchiq_instance_get_use_count(instance);
	seq_printf(f, "%d\n", use_count);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(defs_usecount);

static int defs_trace_show(struct seq_file *f, void *offset)
{
	VCHIQ_INSTANCE_T instance = f->private;
	int trace;

	trace = vchiq_instance_get_trace(instance);
	seq_printf(f, "%s\n", trace ? "Y" : "N");

	return 0;
}

static int defs_trace_open(struct inode *inode, struct file *file)
{
	return single_open(file, defs_trace_show, inode->i_private);
}

static ssize_t defs_trace_write(struct file *file,
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

static const struct file_operations defs_trace_fops = {
	.owner		= THIS_MODULE,
	.open		= defs_trace_open,
	.write		= defs_trace_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* add an instance (process) to the defs entries */
void vchiq_defs_add_instance(VCHIQ_INSTANCE_T instance)
{
	char pidstr[16];
	struct dentry *top;

	snprintf(pidstr, sizeof(pidstr), "%d",
		 vchiq_instance_get_pid(instance));

	top = defs_create_dir(pidstr, vchiq_dbg_clients);

	defs_create_file("use_count", 0444, top, instance,
			    &defs_usecount_fops);
	defs_create_file("trace", 0644, top, instance, &defs_trace_fops);

	vchiq_instance_get_defs_node(instance)->dentry = top;
}

void vchiq_defs_remove_instance(VCHIQ_INSTANCE_T instance)
{
	struct vchiq_defs_node *node =
				vchiq_instance_get_defs_node(instance);

	defs_remove_recursive(node->dentry);
}

void vchiq_defs_init(void)
{
	struct dentry *dir;
	int i;

	vchiq_dbg_dir = defs_create_dir("vchiq", NULL);
	vchiq_dbg_clients = defs_create_dir("clients", vchiq_dbg_dir);

	/* create an entry under <defs>/vchiq/log for each log category */
	dir = defs_create_dir("log", vchiq_dbg_dir);

	for (i = 0; i < n_log_entries; i++)
		defs_create_file(vchiq_defs_log_entries[i].name, 0644,
				    dir, vchiq_defs_log_entries[i].plevel,
				    &defs_log_fops);
}

/* remove all the defs entries */
void vchiq_defs_deinit(void)
{
	defs_remove_recursive(vchiq_dbg_dir);
}

#else /* CONFIG_DE_FS */

void vchiq_defs_init(void)
{
}

void vchiq_defs_deinit(void)
{
}

void vchiq_defs_add_instance(VCHIQ_INSTANCE_T instance)
{
}

void vchiq_defs_remove_instance(VCHIQ_INSTANCE_T instance)
{
}

#endif /* CONFIG_DE_FS */
