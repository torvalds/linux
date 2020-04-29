// SPDX-License-Identifier: GPL-2.0-only
/*
 * Picvue PVC160206 display driver
 *
 * Brian Murphy <brian.murphy@eicon.com>
 *
 */
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>

#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "picvue.h"

static DEFINE_MUTEX(pvc_mutex);
static char pvc_lines[PVC_NLINES][PVC_LINELEN+1];
static int pvc_linedata[PVC_NLINES];
static char *pvc_linename[PVC_NLINES] = {"line1", "line2"};
#define DISPLAY_DIR_NAME "display"
static int scroll_dir, scroll_interval;

static struct timer_list timer;

static void pvc_display(unsigned long data)
{
	int i;

	pvc_clear();
	for (i = 0; i < PVC_NLINES; i++)
		pvc_write_string(pvc_lines[i], 0, i);
}

static DECLARE_TASKLET(pvc_display_tasklet, &pvc_display, 0);

static int pvc_line_proc_show(struct seq_file *m, void *v)
{
	int lineno = *(int *)m->private;

	if (lineno < 0 || lineno >= PVC_NLINES) {
		printk(KERN_WARNING "proc_read_line: invalid lineno %d\n", lineno);
		return 0;
	}

	mutex_lock(&pvc_mutex);
	seq_printf(m, "%s\n", pvc_lines[lineno]);
	mutex_unlock(&pvc_mutex);

	return 0;
}

static int pvc_line_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pvc_line_proc_show, PDE_DATA(inode));
}

static ssize_t pvc_line_proc_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *pos)
{
	int lineno = *(int *)PDE_DATA(file_inode(file));
	char kbuf[PVC_LINELEN];
	size_t len;

	BUG_ON(lineno < 0 || lineno >= PVC_NLINES);

	len = min(count, sizeof(kbuf) - 1);
	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	if (len > 0 && kbuf[len - 1] == '\n')
		len--;

	mutex_lock(&pvc_mutex);
	strncpy(pvc_lines[lineno], kbuf, len);
	pvc_lines[lineno][len] = '\0';
	mutex_unlock(&pvc_mutex);

	tasklet_schedule(&pvc_display_tasklet);

	return count;
}

static const struct proc_ops pvc_line_proc_ops = {
	.proc_open	= pvc_line_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= pvc_line_proc_write,
};

static ssize_t pvc_scroll_proc_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *pos)
{
	char kbuf[42];
	size_t len;
	int cmd;

	len = min(count, sizeof(kbuf) - 1);
	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	cmd = simple_strtol(kbuf, NULL, 10);

	mutex_lock(&pvc_mutex);
	if (scroll_interval != 0)
		del_timer(&timer);

	if (cmd == 0) {
		scroll_dir = 0;
		scroll_interval = 0;
	} else {
		if (cmd < 0) {
			scroll_dir = -1;
			scroll_interval = -cmd;
		} else {
			scroll_dir = 1;
			scroll_interval = cmd;
		}
		add_timer(&timer);
	}
	mutex_unlock(&pvc_mutex);

	return count;
}

static int pvc_scroll_proc_show(struct seq_file *m, void *v)
{
	mutex_lock(&pvc_mutex);
	seq_printf(m, "%d\n", scroll_dir * scroll_interval);
	mutex_unlock(&pvc_mutex);

	return 0;
}

static int pvc_scroll_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pvc_scroll_proc_show, NULL);
}

static const struct proc_ops pvc_scroll_proc_ops = {
	.proc_open	= pvc_scroll_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= pvc_scroll_proc_write,
};

void pvc_proc_timerfunc(struct timer_list *unused)
{
	if (scroll_dir < 0)
		pvc_move(DISPLAY|RIGHT);
	else if (scroll_dir > 0)
		pvc_move(DISPLAY|LEFT);

	timer.expires = jiffies + scroll_interval;
	add_timer(&timer);
}

static void pvc_proc_cleanup(void)
{
	remove_proc_subtree(DISPLAY_DIR_NAME, NULL);
	del_timer_sync(&timer);
}

static int __init pvc_proc_init(void)
{
	struct proc_dir_entry *dir, *proc_entry;
	int i;

	dir = proc_mkdir(DISPLAY_DIR_NAME, NULL);
	if (dir == NULL)
		goto error;

	for (i = 0; i < PVC_NLINES; i++) {
		strcpy(pvc_lines[i], "");
		pvc_linedata[i] = i;
	}
	for (i = 0; i < PVC_NLINES; i++) {
		proc_entry = proc_create_data(pvc_linename[i], 0644, dir,
					&pvc_line_proc_ops, &pvc_linedata[i]);
		if (proc_entry == NULL)
			goto error;
	}
	proc_entry = proc_create("scroll", 0644, dir, &pvc_scroll_proc_ops);
	if (proc_entry == NULL)
		goto error;

	timer_setup(&timer, pvc_proc_timerfunc, 0);

	return 0;
error:
	pvc_proc_cleanup();
	return -ENOMEM;
}

module_init(pvc_proc_init);
module_exit(pvc_proc_cleanup);
MODULE_LICENSE("GPL");
