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
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
	struct rproc_mem_entry *trace = filp->private_data;
	int len = strnlen(trace->va, trace->len);

	return simple_read_from_buffer(userbuf, count, ppos, trace->va, len);
}

static const struct file_operations trace_rproc_ops = {
	.read = rproc_trace_read,
	.open = simple_open,
	.llseek	= generic_file_llseek,
};

/*
 * A state-to-string lookup table, for exposing a human readable state
 * via debugfs. Always keep in sync with enum rproc_state
 */
static const char * const rproc_state_string[] = {
	"offline",
	"suspended",
	"running",
	"crashed",
	"invalid",
};

/* expose the state of the remote processor via debugfs */
static ssize_t rproc_state_read(struct file *filp, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	unsigned int state;
	char buf[30];
	int i;

	state = rproc->state > RPROC_LAST ? RPROC_LAST : rproc->state;

	i = scnprintf(buf, 30, "%.28s (%d)\n", rproc_state_string[state],
							rproc->state);

	return simple_read_from_buffer(userbuf, count, ppos, buf, i);
}

static ssize_t rproc_state_write(struct file *filp, const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	char buf[10];
	int ret;

	if (count > sizeof(buf) || count <= 0)
		return -EINVAL;

	ret = copy_from_user(buf, userbuf, count);
	if (ret)
		return -EFAULT;

	if (buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (!strncmp(buf, "start", count)) {
		ret = rproc_boot(rproc);
		if (ret) {
			dev_err(&rproc->dev, "Boot failed: %d\n", ret);
			return ret;
		}
	} else if (!strncmp(buf, "stop", count)) {
		rproc_shutdown(rproc);
	} else {
		dev_err(&rproc->dev, "Unrecognised option: %s\n", buf);
		return -EINVAL;
	}

	return count;
}

static const struct file_operations rproc_state_ops = {
	.read = rproc_state_read,
	.write = rproc_state_write,
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
		rproc->recovery_disabled = false;
		/* if rproc has crashed, trigger recovery */
		if (rproc->state == RPROC_CRASHED)
			rproc_trigger_recovery(rproc);
	} else if (!strncmp(buf, "disabled", count)) {
		rproc->recovery_disabled = true;
	} else if (!strncmp(buf, "recover", count)) {
		/* if rproc has crashed, trigger recovery */
		if (rproc->state == RPROC_CRASHED)
			rproc_trigger_recovery(rproc);
	}

	return count;
}

static const struct file_operations rproc_recovery_ops = {
	.read = rproc_recovery_read,
	.write = rproc_recovery_write,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

void rproc_remove_trace_file(struct dentry *tfile)
{
	debugfs_remove(tfile);
}

struct dentry *rproc_create_trace_file(const char *name, struct rproc *rproc,
					struct rproc_mem_entry *trace)
{
	struct dentry *tfile;

	tfile = debugfs_create_file(name, 0400, rproc->dbg_dir,
						trace, &trace_rproc_ops);
	if (!tfile) {
		dev_err(&rproc->dev, "failed to create debugfs trace entry\n");
		return NULL;
	}

	return tfile;
}

void rproc_delete_debug_dir(struct rproc *rproc)
{
	if (!rproc->dbg_dir)
		return;

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
	debugfs_create_file("state", 0400, rproc->dbg_dir,
					rproc, &rproc_state_ops);
	debugfs_create_file("recovery", 0400, rproc->dbg_dir,
					rproc, &rproc_recovery_ops);
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
