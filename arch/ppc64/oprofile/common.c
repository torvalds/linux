/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Based on alpha version.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pmc.h>
#include <asm/cputable.h>
#include <asm/oprofile_impl.h>

static struct op_ppc64_model *model;

static struct op_counter_config ctr[OP_MAX_COUNTER];
static struct op_system_config sys;

static void op_handle_interrupt(struct pt_regs *regs)
{
	model->handle_interrupt(regs, ctr);
}

static int op_ppc64_setup(void)
{
	int err;

	/* Grab the hardware */
	err = reserve_pmc_hardware(op_handle_interrupt);
	if (err)
		return err;

	/* Pre-compute the values to stuff in the hardware registers.  */
	model->reg_setup(ctr, &sys, model->num_counters);

	/* Configure the registers on all cpus.  */
	on_each_cpu(model->cpu_setup, NULL, 0, 1);

	return 0;
}

static void op_ppc64_shutdown(void)
{
	release_pmc_hardware();
}

static void op_ppc64_cpu_start(void *dummy)
{
	model->start(ctr);
}

static int op_ppc64_start(void)
{
	on_each_cpu(op_ppc64_cpu_start, NULL, 0, 1);
	return 0;
}

static inline void op_ppc64_cpu_stop(void *dummy)
{
	model->stop();
}

static void op_ppc64_stop(void)
{
	on_each_cpu(op_ppc64_cpu_stop, NULL, 0, 1);
}

static int op_ppc64_create_files(struct super_block *sb, struct dentry *root)
{
	int i;

	/*
	 * There is one mmcr0, mmcr1 and mmcra for setting the events for
	 * all of the counters.
	 */
	oprofilefs_create_ulong(sb, root, "mmcr0", &sys.mmcr0);
	oprofilefs_create_ulong(sb, root, "mmcr1", &sys.mmcr1);
	oprofilefs_create_ulong(sb, root, "mmcra", &sys.mmcra);

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
		char buf[3];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);

		oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &ctr[i].count);
		/*
		 * We dont support per counter user/kernel selection, but
		 * we leave the entries because userspace expects them
		 */
		oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);
		oprofilefs_create_ulong(sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

	oprofilefs_create_ulong(sb, root, "enable_kernel", &sys.enable_kernel);
	oprofilefs_create_ulong(sb, root, "enable_user", &sys.enable_user);
	oprofilefs_create_ulong(sb, root, "backtrace_spinlocks",
				&sys.backtrace_spinlocks);

	/* Default to tracing both kernel and user */
	sys.enable_kernel = 1;
	sys.enable_user = 1;

	/* Turn on backtracing through spinlocks by default */
	sys.backtrace_spinlocks = 1;

	return 0;
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	if (!cur_cpu_spec->oprofile_model || !cur_cpu_spec->oprofile_cpu_type)
		return -ENODEV;

	model = cur_cpu_spec->oprofile_model;
	model->num_counters = cur_cpu_spec->num_pmcs;

	ops->cpu_type = cur_cpu_spec->oprofile_cpu_type;
	ops->create_files = op_ppc64_create_files;
	ops->setup = op_ppc64_setup;
	ops->shutdown = op_ppc64_shutdown;
	ops->start = op_ppc64_start;
	ops->stop = op_ppc64_stop;

	printk(KERN_INFO "oprofile: using %s performance monitoring.\n",
	       ops->cpu_type);

	return 0;
}

void oprofile_arch_exit(void)
{
}
