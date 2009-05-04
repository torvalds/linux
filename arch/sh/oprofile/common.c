/*
 * arch/sh/oprofile/init.c
 *
 * Copyright (C) 2003 - 2008  Paul Mundt
 *
 * Based on arch/mips/oprofile/common.c:
 *
 *	Copyright (C) 2004, 2005 Ralf Baechle
 *	Copyright (C) 2005 MIPS Technologies, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <asm/processor.h>
#include "op_impl.h"

extern struct op_sh_model op_model_sh7750_ops __weak;
extern struct op_sh_model op_model_sh4a_ops __weak;

static struct op_sh_model *model;

static struct op_counter_config ctr[20];

extern void sh_backtrace(struct pt_regs * const regs, unsigned int depth);

static int op_sh_setup(void)
{
	/* Pre-compute the values to stuff in the hardware registers.  */
	model->reg_setup(ctr);

	/* Configure the registers on all cpus.  */
	on_each_cpu(model->cpu_setup, NULL, 1);

        return 0;
}

static int op_sh_create_files(struct super_block *sb, struct dentry *root)
{
	int i, ret = 0;

	for (i = 0; i < model->num_counters; i++) {
		struct dentry *dir;
		char buf[4];

		snprintf(buf, sizeof(buf), "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);

		ret |= oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		ret |= oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		ret |= oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		ret |= oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);

		if (model->create_files)
			ret |= model->create_files(sb, dir);
		else
			ret |= oprofilefs_create_ulong(sb, dir, "count", &ctr[i].count);

		/* Dummy entries */
		ret |= oprofilefs_create_ulong(sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

	return ret;
}

static int op_sh_start(void)
{
	/* Enable performance monitoring for all counters.  */
	on_each_cpu(model->cpu_start, NULL, 1);

	return 0;
}

static void op_sh_stop(void)
{
	/* Disable performance monitoring for all counters.  */
	on_each_cpu(model->cpu_stop, NULL, 1);
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	struct op_sh_model *lmodel = NULL;
	int ret;

	/*
	 * Always assign the backtrace op. If the counter initialization
	 * fails, we fall back to the timer which will still make use of
	 * this.
	 */
	ops->backtrace = sh_backtrace;

	switch (current_cpu_data.type) {
	/* SH-4 types */
	case CPU_SH7750:
	case CPU_SH7750S:
		lmodel = &op_model_sh7750_ops;
		break;

        /* SH-4A types */
	case CPU_SH7763:
	case CPU_SH7770:
	case CPU_SH7780:
	case CPU_SH7781:
	case CPU_SH7785:
	case CPU_SH7786:
	case CPU_SH7723:
	case CPU_SHX3:
		lmodel = &op_model_sh4a_ops;
		break;

	/* SH4AL-DSP types */
	case CPU_SH7343:
	case CPU_SH7722:
	case CPU_SH7366:
		lmodel = &op_model_sh4a_ops;
		break;
	}

	if (!lmodel)
		return -ENODEV;
	if (!(current_cpu_data.flags & CPU_HAS_PERF_COUNTER))
		return -ENODEV;

	ret = lmodel->init();
	if (unlikely(ret != 0))
		return ret;

	model = lmodel;

	ops->setup		= op_sh_setup;
	ops->create_files	= op_sh_create_files;
	ops->start		= op_sh_start;
	ops->stop		= op_sh_stop;
	ops->cpu_type		= lmodel->cpu_type;

	printk(KERN_INFO "oprofile: using %s performance monitoring.\n",
	       lmodel->cpu_type);

	return 0;
}

void oprofile_arch_exit(void)
{
	if (model && model->exit)
		model->exit();
}
