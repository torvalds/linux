/*
 * PPC 32 oprofile support
 * Based on PPC64 oprofile support
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Copyright (C) Freescale Semiconductor, Inc 2004
 *
 * Author: Andy Fleming
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/oprofile.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/perfmon.h>
#include <asm/cputable.h>

#include "op_impl.h"

static struct op_ppc32_model *model;

static struct op_counter_config ctr[OP_MAX_COUNTER];
static struct op_system_config sys;

static void op_handle_interrupt(struct pt_regs *regs)
{
	model->handle_interrupt(regs, ctr);
}

static int op_ppc32_setup(void)
{
	/* Install our interrupt handler into the existing hook.  */
	if(request_perfmon_irq(&op_handle_interrupt))
		return -EBUSY;

	mb();

	/* Pre-compute the values to stuff in the hardware registers.  */
	model->reg_setup(ctr, &sys, model->num_counters);

#if 0
	/* FIXME: Make multi-cpu work */
	/* Configure the registers on all cpus.  */
	on_each_cpu(model->reg_setup, NULL, 0, 1);
#endif

	return 0;
}

static void op_ppc32_shutdown(void)
{
	mb();

	/* Remove our interrupt handler. We may be removing this module. */
	free_perfmon_irq();
}

static void op_ppc32_cpu_start(void *dummy)
{
	model->start(ctr);
}

static int op_ppc32_start(void)
{
	on_each_cpu(op_ppc32_cpu_start, NULL, 0, 1);
	return 0;
}

static inline void op_ppc32_cpu_stop(void *dummy)
{
	model->stop();
}

static void op_ppc32_stop(void)
{
	on_each_cpu(op_ppc32_cpu_stop, NULL, 0, 1);
}

static int op_ppc32_create_files(struct super_block *sb, struct dentry *root)
{
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
		char buf[3];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);

		oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &ctr[i].count);
		oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);

		/* FIXME: Not sure if this is used */
		oprofilefs_create_ulong(sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

	oprofilefs_create_ulong(sb, root, "enable_kernel", &sys.enable_kernel);
	oprofilefs_create_ulong(sb, root, "enable_user", &sys.enable_user);

	/* Default to tracing both kernel and user */
	sys.enable_kernel = 1;
	sys.enable_user = 1;

	return 0;
}

static struct oprofile_operations oprof_ppc32_ops = {
	.create_files	= op_ppc32_create_files,
	.setup		= op_ppc32_setup,
	.shutdown	= op_ppc32_shutdown,
	.start		= op_ppc32_start,
	.stop		= op_ppc32_stop,
	.cpu_type	= NULL		/* To be filled in below. */
};

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	char *name;
	int cpu_id = smp_processor_id();

#ifdef CONFIG_FSL_BOOKE
	model = &op_model_fsl_booke;
#else
	return -ENODEV;
#endif

	name = kmalloc(32, GFP_KERNEL);

	if (NULL == name)
		return -ENOMEM;

	sprintf(name, "ppc/%s", cur_cpu_spec[cpu_id]->cpu_name);

	oprof_ppc32_ops.cpu_type = name;

	model->num_counters = cur_cpu_spec[cpu_id]->num_pmcs;

	*ops = oprof_ppc32_ops;

	printk(KERN_INFO "oprofile: using %s performance monitoring.\n",
	       oprof_ppc32_ops.cpu_type);

	return 0;
}

void oprofile_arch_exit(void)
{
	kfree(oprof_ppc32_ops.cpu_type);
	oprof_ppc32_ops.cpu_type = NULL;
}
