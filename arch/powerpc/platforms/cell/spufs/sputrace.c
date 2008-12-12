/*
 * Copyright (C) 2007 IBM Deutschland Entwicklung GmbH
 *	Released under GPL v2.
 *
 * Partially based on net/ipv4/tcp_probe.c.
 *
 * Simple tracing facility for spu contexts.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/marker.h>
#include <linux/proc_fs.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include "spufs.h"

struct spu_probe {
	const char *name;
	const char *format;
	marker_probe_func *probe_func;
};

struct sputrace {
	ktime_t tstamp;
	int owner_tid; /* owner */
	int curr_tid;
	const char *name;
	int number;
};

static int bufsize __read_mostly = 16384;
MODULE_PARM_DESC(bufsize, "Log buffer size (number of records)");
module_param(bufsize, int, 0);


static DEFINE_SPINLOCK(sputrace_lock);
static DECLARE_WAIT_QUEUE_HEAD(sputrace_wait);
static ktime_t sputrace_start;
static unsigned long sputrace_head, sputrace_tail;
static struct sputrace *sputrace_log;
static int sputrace_logging;

static int sputrace_used(void)
{
	return (sputrace_head - sputrace_tail) % bufsize;
}

static inline int sputrace_avail(void)
{
	return bufsize - sputrace_used();
}

static int sputrace_sprint(char *tbuf, int n)
{
	const struct sputrace *t = sputrace_log + sputrace_tail % bufsize;
	struct timespec tv =
		ktime_to_timespec(ktime_sub(t->tstamp, sputrace_start));

	return snprintf(tbuf, n,
		"[%lu.%09lu] %d: %s (ctxthread = %d, spu = %d)\n",
		(unsigned long) tv.tv_sec,
		(unsigned long) tv.tv_nsec,
		t->curr_tid,
		t->name,
		t->owner_tid,
		t->number);
}

static ssize_t sputrace_read(struct file *file, char __user *buf,
		size_t len, loff_t *ppos)
{
	int error = 0, cnt = 0;

	if (!buf || len < 0)
		return -EINVAL;

	while (cnt < len) {
		char tbuf[128];
		int width;

		/* If we have data ready to return, don't block waiting
		 * for more */
		if (cnt > 0 && sputrace_used() == 0)
			break;

		error = wait_event_interruptible(sputrace_wait,
						 sputrace_used() > 0);
		if (error)
			break;

		spin_lock(&sputrace_lock);
		if (sputrace_head == sputrace_tail) {
			spin_unlock(&sputrace_lock);
			continue;
		}

		width = sputrace_sprint(tbuf, sizeof(tbuf));
		if (width < len)
			sputrace_tail = (sputrace_tail + 1) % bufsize;
		spin_unlock(&sputrace_lock);

		if (width >= len)
			break;

		error = copy_to_user(buf + cnt, tbuf, width);
		if (error)
			break;
		cnt += width;
	}

	return cnt == 0 ? error : cnt;
}

static int sputrace_open(struct inode *inode, struct file *file)
{
	int rc;

	spin_lock(&sputrace_lock);
	if (sputrace_logging) {
		rc = -EBUSY;
		goto out;
	}

	sputrace_logging = 1;
	sputrace_head = sputrace_tail = 0;
	sputrace_start = ktime_get();
	rc = 0;

out:
	spin_unlock(&sputrace_lock);
	return rc;
}

static int sputrace_release(struct inode *inode, struct file *file)
{
	spin_lock(&sputrace_lock);
	sputrace_logging = 0;
	spin_unlock(&sputrace_lock);
	return 0;
}

static const struct file_operations sputrace_fops = {
	.owner   = THIS_MODULE,
	.open    = sputrace_open,
	.read    = sputrace_read,
	.release = sputrace_release,
};

static void sputrace_log_item(const char *name, struct spu_context *ctx,
		struct spu *spu)
{
	spin_lock(&sputrace_lock);

	if (!sputrace_logging) {
		spin_unlock(&sputrace_lock);
		return;
	}

	if (sputrace_avail() > 1) {
		struct sputrace *t = sputrace_log + sputrace_head;

		t->tstamp = ktime_get();
		t->owner_tid = ctx->tid;
		t->name = name;
		t->curr_tid = current->pid;
		t->number = spu ? spu->number : -1;

		sputrace_head = (sputrace_head + 1) % bufsize;
	} else {
		printk(KERN_WARNING
		       "sputrace: lost samples due to full buffer.\n");
	}
	spin_unlock(&sputrace_lock);

	wake_up(&sputrace_wait);
}

static void spu_context_event(void *probe_private, void *call_data,
		const char *format, va_list *args)
{
	struct spu_probe *p = probe_private;
	struct spu_context *ctx;
	struct spu *spu;

	ctx = va_arg(*args, struct spu_context *);
	spu = va_arg(*args, struct spu *);

	sputrace_log_item(p->name, ctx, spu);
}

static void spu_context_nospu_event(void *probe_private, void *call_data,
		const char *format, va_list *args)
{
	struct spu_probe *p = probe_private;
	struct spu_context *ctx;

	ctx = va_arg(*args, struct spu_context *);

	sputrace_log_item(p->name, ctx, NULL);
}

struct spu_probe spu_probes[] = {
	{ "spu_bind_context__enter", "ctx %p spu %p", spu_context_event },
	{ "spu_unbind_context__enter", "ctx %p spu %p", spu_context_event },
	{ "spu_get_idle__enter", "ctx %p", spu_context_nospu_event },
	{ "spu_get_idle__found", "ctx %p spu %p", spu_context_event },
	{ "spu_get_idle__not_found", "ctx %p", spu_context_nospu_event },
	{ "spu_find_victim__enter", "ctx %p", spu_context_nospu_event },
	{ "spusched_tick__preempt", "ctx %p spu %p", spu_context_event },
	{ "spusched_tick__newslice", "ctx %p", spu_context_nospu_event },
	{ "spu_yield__enter", "ctx %p", spu_context_nospu_event },
	{ "spu_deactivate__enter", "ctx %p", spu_context_nospu_event },
	{ "__spu_deactivate__unload", "ctx %p spu %p", spu_context_event },
	{ "spufs_ps_fault__enter", "ctx %p", spu_context_nospu_event },
	{ "spufs_ps_fault__sleep", "ctx %p", spu_context_nospu_event },
	{ "spufs_ps_fault__wake", "ctx %p spu %p", spu_context_event },
	{ "spufs_ps_fault__insert", "ctx %p spu %p", spu_context_event },
	{ "spu_acquire_saved__enter", "ctx %p", spu_context_nospu_event },
	{ "destroy_spu_context__enter", "ctx %p", spu_context_nospu_event },
	{ "spufs_stop_callback__enter", "ctx %p spu %p", spu_context_event },
};

static int __init sputrace_init(void)
{
	struct proc_dir_entry *entry;
	int i, error = -ENOMEM;

	sputrace_log = kcalloc(bufsize, sizeof(struct sputrace), GFP_KERNEL);
	if (!sputrace_log)
		goto out;

	entry = proc_create("sputrace", S_IRUSR, NULL, &sputrace_fops);
	if (!entry)
		goto out_free_log;

	for (i = 0; i < ARRAY_SIZE(spu_probes); i++) {
		struct spu_probe *p = &spu_probes[i];

		error = marker_probe_register(p->name, p->format,
					      p->probe_func, p);
		if (error)
			printk(KERN_INFO "Unable to register probe %s\n",
					p->name);
	}

	return 0;

out_free_log:
	kfree(sputrace_log);
out:
	return -ENOMEM;
}

static void __exit sputrace_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spu_probes); i++)
		marker_probe_unregister(spu_probes[i].name,
			spu_probes[i].probe_func, &spu_probes[i]);

	remove_proc_entry("sputrace", NULL);
	kfree(sputrace_log);
	marker_synchronize_unregister();
}

module_init(sputrace_init);
module_exit(sputrace_exit);

MODULE_LICENSE("GPL");
