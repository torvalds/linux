/**
 * @file arch/alpha/oprofile/common.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Richard Henderson <rth@twiddle.net>
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/special_insns.h>

#include "op_impl.h"

extern struct op_axp_model op_model_ev4 __attribute__((weak));
extern struct op_axp_model op_model_ev5 __attribute__((weak));
extern struct op_axp_model op_model_pca56 __attribute__((weak));
extern struct op_axp_model op_model_ev6 __attribute__((weak));
extern struct op_axp_model op_model_ev67 __attribute__((weak));

static struct op_axp_model *model;

extern void (*perf_irq)(unsigned long, struct pt_regs *);
static void (*save_perf_irq)(unsigned long, struct pt_regs *);

static struct op_counter_config ctr[20];
static struct op_system_config sys;
static struct op_register_config reg;

/* Called from do_entInt to handle the performance monitor interrupt.  */

static void
op_handle_interrupt(unsigned long which, struct pt_regs *regs)
{
	model->handle_interrupt(which, regs, ctr);

	/* If the user has selected an interrupt frequency that is
	   not exactly the width of the counter, write a new value
	   into the counter such that it'll overflow after N more
	   events.  */
	if ((reg.need_reset >> which) & 1)
		model->reset_ctr(&reg, which);
}
 
static int
op_axp_setup(void)
{
	unsigned long i, e;

	/* Install our interrupt handler into the existing hook.  */
	save_perf_irq = perf_irq;
	perf_irq = op_handle_interrupt;

	/* Compute the mask of enabled counters.  */
	for (i = e = 0; i < model->num_counters; ++i)
		if (ctr[i].enabled)
			e |= 1 << i;
	reg.enable = e;

	/* Pre-compute the values to stuff in the hardware registers.  */
	model->reg_setup(&reg, ctr, &sys);

	/* Configure the registers on all cpus.  */
	(void)smp_call_function(model->cpu_setup, &reg, 1);
	model->cpu_setup(&reg);
	return 0;
}

static void
op_axp_shutdown(void)
{
	/* Remove our interrupt handler.  We may be removing this module.  */
	perf_irq = save_perf_irq;
}

static void
op_axp_cpu_start(void *dummy)
{
	wrperfmon(1, reg.enable);
}

static int
op_axp_start(void)
{
	(void)smp_call_function(op_axp_cpu_start, NULL, 1);
	op_axp_cpu_start(NULL);
	return 0;
}

static inline void
op_axp_cpu_stop(void *dummy)
{
	/* Disable performance monitoring for all counters.  */
	wrperfmon(0, -1);
}

static void
op_axp_stop(void)
{
	(void)smp_call_function(op_axp_cpu_stop, NULL, 1);
	op_axp_cpu_stop(NULL);
}

static int
op_axp_create_files(struct dentry *root)
{
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
		char buf[4];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(root, buf);

		oprofilefs_create_ulong(root->d_sb, dir, "enabled", &ctr[i].enabled);
                oprofilefs_create_ulong(root->d_sb, dir, "event", &ctr[i].event);
		oprofilefs_create_ulong(root->d_sb, dir, "count", &ctr[i].count);
		/* Dummies.  */
		oprofilefs_create_ulong(root->d_sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(root->d_sb, dir, "user", &ctr[i].user);
		oprofilefs_create_ulong(root->d_sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

	if (model->can_set_proc_mode) {
		oprofilefs_create_ulong(root->d_sb, root, "enable_pal",
					&sys.enable_pal);
		oprofilefs_create_ulong(root->d_sb, root, "enable_kernel",
					&sys.enable_kernel);
		oprofilefs_create_ulong(root->d_sb, root, "enable_user",
					&sys.enable_user);
	}

	return 0;
}

int __init
oprofile_arch_init(struct oprofile_operations *ops)
{
	struct op_axp_model *lmodel = NULL;

	switch (implver()) {
	case IMPLVER_EV4:
		lmodel = &op_model_ev4;
		break;
	case IMPLVER_EV5:
		/* 21164PC has a slightly different set of events.
		   Recognize the chip by the presence of the MAX insns.  */
		if (!amask(AMASK_MAX))
			lmodel = &op_model_pca56;
		else
			lmodel = &op_model_ev5;
		break;
	case IMPLVER_EV6:
		/* 21264A supports ProfileMe.
		   Recognize the chip by the presence of the CIX insns.  */
		if (!amask(AMASK_CIX))
			lmodel = &op_model_ev67;
		else
			lmodel = &op_model_ev6;
		break;
	}

	if (!lmodel)
		return -ENODEV;
	model = lmodel;

	ops->create_files = op_axp_create_files;
	ops->setup = op_axp_setup;
	ops->shutdown = op_axp_shutdown;
	ops->start = op_axp_start;
	ops->stop = op_axp_stop;
	ops->cpu_type = lmodel->cpu_type;

	printk(KERN_INFO "oprofile: using %s performance monitoring.\n",
	       lmodel->cpu_type);

	return 0;
}


void
oprofile_arch_exit(void)
{
}
