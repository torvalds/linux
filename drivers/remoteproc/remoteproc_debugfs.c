// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote Processor Framework
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Mark Grosen <mgrosen@ti.com>
 * Brian Swetland <swetland@google.com>
 * Fernando Guzman Lugo <fernando.lugo@ti.com>
 * Suman Anna <s-anna@ti.com>
 * Robert Tivy <rtivy@ti.com>
 * Armando Uribe De Leon <x0095078@ti.com>
 */

#define pr_fmt(fmt)    "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/remoteproc.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#include "remoteproc_internal.h"

/* remoteproc debugfs parent dir */
static struct dentry *rproc_dbg;

/*
 * A coredump-configuration-to-string lookup table, for exposing a
 * human readable configuration via debugfs. Always keep in sync with
 * enum rproc_coredump_mechanism
 */
static const char * const rproc_coredump_str[] = {
	[RPROC_COREDUMP_DISABLED]	= "disabled",
	[RPROC_COREDUMP_ENABLED]	= "enabled",
	[RPROC_COREDUMP_INLINE]		= "inline",
};

/* Expose the current coredump configuration via debugfs */
static ssize_t rproc_coredump_read(struct file *filp, char __user *userbuf,
				   size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	char buf[20];
	int len;

	len = scnprintf(buf, sizeof(buf), "%s\n",
			rproc_coredump_str[rproc->dump_conf]);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

/*
 * By writing to the 'coredump' debugfs entry, we control the behavior of the
 * coredump mechanism dynamically. The default value of this entry is "disabled".
 *
 * The 'coredump' debugfs entry supports these commands:
 *
 * disabled:	By default coredump collection is disabled. Recovery will
 *		proceed without collecting any dump.
 *
 * enabled:	When the remoteproc crashes the entire coredump will be copied
 *		to a separate buffer and exposed to userspace.
 *
 * inline:	The coredump will not be copied to a separate buffer and the
 *		recovery process will have to wait until data is read by
 *		userspace. But this avoid usage of extra memory.
 */
static ssize_t rproc_coredump_write(struct file *filp,
				    const char __user *user_buf, size_t count,
				    loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	int ret, err = 0;
	char buf[20];

	if (count < 1 || count > sizeof(buf))
		return -EINVAL;

	ret = copy_from_user(buf, user_buf, count);
	if (ret)
		return -EFAULT;

	/* remove end of line */
	if (buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (rproc->state == RPROC_CRASHED) {
		dev_err(&rproc->dev, "can't change coredump configuration\n");
		err = -EBUSY;
		goto out;
	}

	if (!strncmp(buf, "disabled", count)) {
		rproc->dump_conf = RPROC_COREDUMP_DISABLED;
	} else if (!strncmp(buf, "enabled", count)) {
		rproc->dump_conf = RPROC_COREDUMP_ENABLED;
	} else if (!strncmp(buf, "inline", count)) {
		rproc->dump_conf = RPROC_COREDUMP_INLINE;
	} else {
		dev_err(&rproc->dev, "Invalid coredump configuration\n");
		err = -EINVAL;
	}
out:
	return err ? err : count;
}

static const struct file_operations rproc_coredump_fops = {
	.read = rproc_coredump_read,
	.write = rproc_coredump_write,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

/*
 * Some remote processors may support dumping trace logs into a shared
 * memory buffer. We expose this trace buffer using debugfs, so users
 * can easily tell what's going on remotely.
 *
 * We will most probably improve the rproc tracing facilities later on,
 * but this kind of lightweight and simple mechanism is always good to have,
 * as it provides very early tracing with little to no dependencies at all.
 */
static ssize_t rproc_trace_read(struct file *filp, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct rproc_debug_trace *data = filp->private_data;
	struct rproc_mem_entry *trace = &data->trace_mem;
	void *va;
	char buf[100];
	int len;

	va = rproc_da_to_va(data->rproc, trace->da, trace->len, NULL);

	if (!va) {
		len = scnprintf(buf, sizeof(buf), "Trace %s not available\n",
				trace->name);
		va = buf;
	} else {
		len = strnlen(va, trace->len);
	}

	return simple_read_from_buffer(userbuf, count, ppos, va, len);
}

static const struct file_operations trace_rproc_ops = {
	.read = rproc_trace_read,
	.open = simple_open,
	.llseek	= generic_file_llseek,
};

/* expose the name of the remote processor via debugfs */
static ssize_t rproc_name_read(struct file *filp, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	/* need room for the name, a newline and a terminating null */
	char buf[100];
	int i;

	i = scnprintf(buf, sizeof(buf), "%.98s\n", rproc->name);

	return simple_read_from_buffer(userbuf, count, ppos, buf, i);
}

static const struct file_operations rproc_name_ops = {
	.read = rproc_name_read,
	.open = simple_open,
	.llseek	= generic_file_llseek,
};

/* expose recovery flag via debugfs */
static ssize_t rproc_recovery_read(struct file *filp, char __user *userbuf,
				   size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	char *buf = rproc->recovery_disabled ? "disabled\n" : "enabled\n";

	return simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
}

/*
 * By writing to the 'recovery' debugfs entry, we control the behavior of the
 * recovery mechanism dynamically. The default value of this entry is "enabled".
 *
 * The 'recovery' debugfs entry supports these commands:
 *
 * enabled:	When enabled, the remote processor will be automatically
 *		recovered whenever it crashes. Moreover, if the remote
 *		processor crashes while recovery is disabled, it will
 *		be automatically recovered too as soon as recovery is enabled.
 *
 * disabled:	When disabled, a remote processor will remain in a crashed
 *		state if it crashes. This is useful for debugging purposes;
 *		without it, debugging a crash is substantially harder.
 *
 * recover:	This function will trigger an immediate recovery if the
 *		remote processor is in a crashed state, without changing
 *		or checking the recovery state (enabled/disabled).
 *		This is useful during debugging sessions, when one expects
 *		additional crashes to happen after enabling recovery. In this
 *		case, enabling recovery will make it hard to debug subsequent
 *		crashes, so it's recommended to keep recovery disabled, and
 *		instead use the "recover" command as needed.
 */
static ssize_t
rproc_recovery_write(struct file *filp, const char __user *user_buf,
		     size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	char buf[10];
	int ret;

	if (count < 1 || count > sizeof(buf))
		return -EINVAL;

	ret = copy_from_user(buf, user_buf, count);
	if (ret)
		return -EFAULT;

	/* remove end of line */
	if (buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (!strncmp(buf, "enabled", count)) {
		/* change the flag and begin the recovery process if needed */
		rproc->recovery_disabled = false;
		rproc_trigger_recovery(rproc);
	} else if (!strncmp(buf, "disabled", count)) {
		rproc->recovery_disabled = true;
	} else if (!strncmp(buf, "recover", count)) {
		/* begin the recovery process without changing the flag */
		rproc_trigger_recovery(rproc);
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations rproc_recovery_ops = {
	.read = rproc_recovery_read,
	.write = rproc_recovery_write,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

/* expose the crash trigger via debugfs */
static ssize_t
rproc_crash_write(struct file *filp, const char __user *user_buf,
		  size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	unsigned int type;
	int ret;

	ret = kstrtouint_from_user(user_buf, count, 0, &type);
	if (ret < 0)
		return ret;

	rproc_report_crash(rproc, type);

	return count;
}

static const struct file_operations rproc_crash_ops = {
	.write = rproc_crash_write,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

/* Expose resource table content via debugfs */
static int rproc_rsc_table_show(struct seq_file *seq, void *p)
{
	static const char * const types[] = {"carveout", "devmem", "trace", "vdev"};
	struct rproc *rproc = seq->private;
	struct resource_table *table = rproc->table_ptr;
	struct fw_rsc_carveout *c;
	struct fw_rsc_devmem *d;
	struct fw_rsc_trace *t;
	struct fw_rsc_vdev *v;
	int i, j;

	if (!table) {
		seq_puts(seq, "No resource table found\n");
		return 0;
	}

	for (i = 0; i < table->num; i++) {
		int offset = table->offset[i];
		struct fw_rsc_hdr *hdr = (void *)table + offset;
		void *rsc = (void *)hdr + sizeof(*hdr);

		switch (hdr->type) {
		case RSC_CARVEOUT:
			c = rsc;
			seq_printf(seq, "Entry %d is of type %s\n", i, types[hdr->type]);
			seq_printf(seq, "  Device Address 0x%x\n", c->da);
			seq_printf(seq, "  Physical Address 0x%x\n", c->pa);
			seq_printf(seq, "  Length 0x%x Bytes\n", c->len);
			seq_printf(seq, "  Flags 0x%x\n", c->flags);
			seq_printf(seq, "  Reserved (should be zero) [%d]\n", c->reserved);
			seq_printf(seq, "  Name %s\n\n", c->name);
			break;
		case RSC_DEVMEM:
			d = rsc;
			seq_printf(seq, "Entry %d is of type %s\n", i, types[hdr->type]);
			seq_printf(seq, "  Device Address 0x%x\n", d->da);
			seq_printf(seq, "  Physical Address 0x%x\n", d->pa);
			seq_printf(seq, "  Length 0x%x Bytes\n", d->len);
			seq_printf(seq, "  Flags 0x%x\n", d->flags);
			seq_printf(seq, "  Reserved (should be zero) [%d]\n", d->reserved);
			seq_printf(seq, "  Name %s\n\n", d->name);
			break;
		case RSC_TRACE:
			t = rsc;
			seq_printf(seq, "Entry %d is of type %s\n", i, types[hdr->type]);
			seq_printf(seq, "  Device Address 0x%x\n", t->da);
			seq_printf(seq, "  Length 0x%x Bytes\n", t->len);
			seq_printf(seq, "  Reserved (should be zero) [%d]\n", t->reserved);
			seq_printf(seq, "  Name %s\n\n", t->name);
			break;
		case RSC_VDEV:
			v = rsc;
			seq_printf(seq, "Entry %d is of type %s\n", i, types[hdr->type]);

			seq_printf(seq, "  ID %d\n", v->id);
			seq_printf(seq, "  Notify ID %d\n", v->notifyid);
			seq_printf(seq, "  Device features 0x%x\n", v->dfeatures);
			seq_printf(seq, "  Guest features 0x%x\n", v->gfeatures);
			seq_printf(seq, "  Config length 0x%x\n", v->config_len);
			seq_printf(seq, "  Status 0x%x\n", v->status);
			seq_printf(seq, "  Number of vrings %d\n", v->num_of_vrings);
			seq_printf(seq, "  Reserved (should be zero) [%d][%d]\n\n",
				   v->reserved[0], v->reserved[1]);

			for (j = 0; j < v->num_of_vrings; j++) {
				seq_printf(seq, "  Vring %d\n", j);
				seq_printf(seq, "    Device Address 0x%x\n", v->vring[j].da);
				seq_printf(seq, "    Alignment %d\n", v->vring[j].align);
				seq_printf(seq, "    Number of buffers %d\n", v->vring[j].num);
				seq_printf(seq, "    Notify ID %d\n", v->vring[j].notifyid);
				seq_printf(seq, "    Physical Address 0x%x\n\n",
					   v->vring[j].pa);
			}
			break;
		default:
			seq_printf(seq, "Unknown resource type found: %d [hdr: %pK]\n",
				   hdr->type, hdr);
			break;
		}
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(rproc_rsc_table);

/* Expose carveout content via debugfs */
static int rproc_carveouts_show(struct seq_file *seq, void *p)
{
	struct rproc *rproc = seq->private;
	struct rproc_mem_entry *carveout;

	list_for_each_entry(carveout, &rproc->carveouts, node) {
		seq_puts(seq, "Carveout memory entry:\n");
		seq_printf(seq, "\tName: %s\n", carveout->name);
		seq_printf(seq, "\tVirtual address: %pK\n", carveout->va);
		seq_printf(seq, "\tDMA address: %pad\n", &carveout->dma);
		seq_printf(seq, "\tDevice address: 0x%x\n", carveout->da);
		seq_printf(seq, "\tLength: 0x%zx Bytes\n\n", carveout->len);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(rproc_carveouts);

void rproc_remove_trace_file(struct dentry *tfile)
{
	debugfs_remove(tfile);
}

struct dentry *rproc_create_trace_file(const char *name, struct rproc *rproc,
				       struct rproc_debug_trace *trace)
{
	struct dentry *tfile;

	tfile = debugfs_create_file(name, 0400, rproc->dbg_dir, trace,
				    &trace_rproc_ops);
	if (!tfile) {
		dev_err(&rproc->dev, "failed to create debugfs trace entry\n");
		return NULL;
	}

	return tfile;
}

void rproc_delete_debug_dir(struct rproc *rproc)
{
	debugfs_remove_recursive(rproc->dbg_dir);
}

void rproc_create_debug_dir(struct rproc *rproc)
{
	struct device *dev = &rproc->dev;

	if (!rproc_dbg)
		return;

	rproc->dbg_dir = debugfs_create_dir(dev_name(dev), rproc_dbg);
	if (!rproc->dbg_dir)
		return;

	debugfs_create_file("name", 0400, rproc->dbg_dir,
			    rproc, &rproc_name_ops);
	debugfs_create_file("recovery", 0600, rproc->dbg_dir,
			    rproc, &rproc_recovery_ops);
	debugfs_create_file("crash", 0200, rproc->dbg_dir,
			    rproc, &rproc_crash_ops);
	debugfs_create_file("resource_table", 0400, rproc->dbg_dir,
			    rproc, &rproc_rsc_table_fops);
	debugfs_create_file("carveout_memories", 0400, rproc->dbg_dir,
			    rproc, &rproc_carveouts_fops);
	debugfs_create_file("coredump", 0600, rproc->dbg_dir,
			    rproc, &rproc_coredump_fops);
}

void __init rproc_init_debugfs(void)
{
	if (debugfs_initialized()) {
		rproc_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
		if (!rproc_dbg)
			pr_err("can't create debugfs dir\n");
	}
}

void __exit rproc_exit_debugfs(void)
{
	debugfs_remove(rproc_dbg);
}
