/*
 * PPC 64 oprofile support:
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 * PPC 32 oprofile support: (based on PPC 64 support)
 * Copyright (C) Freescale Semiconductor, Inc 2004
 *	Author: Andy Fleming
 *
 * Based on alpha version.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/oprofile.h>
#ifndef __powerpc64__
#include <linux/slab.h>
#endif /* ! __powerpc64__ */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pmc.h>
#include <asm/cputable.h>
#include <asm/oprofile_impl.h>

static struct op_powerpc_model *model;

static struct op_counter_config ctr[OP_MAX_COUNTER];
static struct op_system_config sys;

#ifndef __powerpc64__
static char *cpu_type;
#endif /* ! __powerpc64__ */

static void op_handle_interrupt(struct pt_regs *regs)
{
	model->handle_interrupt(regs, ctr);
}

static int op_powerpc_setup(void)
{
	int err;

	/* Grab the hardware */
	err = reserve_pmc_hardware(op_handle_interrupt);
	if (err)
		return err;

	/* Pre-compute the values to stuff in the hardware registers.  */
	model->reg_setup(ctr, &sys, model->num_counters);

	/* Configure the registers on all cpus.  */
#ifdef __powerpc64__
	on_each_cpu(model->cpu_setup, NULL, 0, 1);
#else /* __powerpc64__ */
#if 0
	/* FIXME: Make multi-cpu work */
	on_each_cpu(model->reg_setup, NULL, 0, 1);
#endif
#endif /* __powerpc64__ */

	return 0;
}

static void op_powerpc_shutdown(void)
{
	release_pmc_hardware();
}

static void op_powerpc_cpu_start(void *dummy)
{
	model->start(ctr);
}

static int op_powerpc_start(void)
{
	on_each_cpu(op_powerpc_cpu_start, NULL, 0, 1);
	return 0;
}

static inline void op_powerpc_cpu_stop(void *dummy)
{
	model->stop();
}

static void op_powerpc_stop(void)
{
	on_each_cpu(op_powerpc_cpu_stop, NULL, 0, 1);
}

static int op_powerpc_create_files(struct super_block *sb, struct dentry *root)
{
	int i;

#ifdef __powerpc64__
	/*
	 * There is one mmcr0, mmcr1 and mmcra for setting the events for
	 * all of the counters.
	 */
	oprofilefs_create_ulong(sb, root, "mmcr0", &sys.mmcr0);
	oprofilefs_create_ulong(sb, root, "mmcr1", &sys.mmcr1);
	oprofilefs_create_ulong(sb, root, "mmcra", &sys.mmcra);
#endif /* __powerpc64__ */

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
		char buf[3];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);

		oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &ctr[i].count);
#ifdef __powerpc64__
		/*
		 * We dont support per counter user/kernel selection, but
		 * we leave the entries because userspace expects them
		 */
#endif /* __powerpc64__ */
		oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);

#ifndef __powerpc64__
		/* FIXME: Not sure if this is used */
#endif /* ! __powerpc64__ */
		oprofilefs_create_ulong(sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

	oprofilefs_create_ulong(sb, root, "enable_kernel", &sys.enable_kernel);
	oprofilefs_create_ulong(sb, root, "enable_user", &sys.enable_user);
#ifdef __powerpc64__
	oprofilefs_create_ulong(sb, root, "backtrace_spinlocks",
				&sys.backtrace_spinlocks);
#endif /* __powerpc64__ */

	/* Default to tracing both kernel and user */
	sys.enable_kernel = 1;
	sys.enable_user = 1;
#ifdef __powerpc64__
	/* Turn on backtracing through spinlocks by default */
	sys.backtrace_spinlocks = 1;
#endif /* __powerpc64__ */

	return 0;
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
#ifndef __powerpc64__
#ifdef CONFIG_FSL_BOOKE
	model = &op_model_fsl_booke;
#else
	return -ENODEV;
#endif

	cpu_type = kmalloc(32, GFP_KERNEL);
	if (NULL == cpu_type)
		return -ENOMEM;

	sprintf(cpu_type, "ppc/%s", cur_cpu_spec->cpu_name);

	model->num_counters = cur_cpu_spec->num_pmcs;

	ops->cpu_type = cpu_type;
#else /* __powerpc64__ */
	if (!cur_cpu_spec->oprofile_model || !cur_cpu_spec->oprofile_cpu_type)
		return -ENODEV;
	model = cur_cpu_spec->oprofile_model;
	model->num_counters = cur_cpu_spec->num_pmcs;

	ops->cpu_type = cur_cpu_spec->oprofile_cpu_type;
#endif /* __powerpc64__ */
	ops->create_files = op_powerpc_create_files;
	ops->setup = op_powerpc_setup;
	ops->shutdown = op_powerpc_shutdown;
	ops->start = op_powerpc_start;
	ops->stop = op_powerpc_stop;

	printk(KERN_INFO "oprofile: using %s performance monitoring.\n",
	       ops->cpu_type);

	return 0;
}

void oprofile_arch_exit(void)
{
#ifndef __powerpc64__
	kfree(cpu_type);
	cpu_type = NULL;
#endif /* ! __powerpc64__ */
}
