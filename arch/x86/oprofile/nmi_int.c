/**
 * @file nmi_int.c
 *
 * @remark Copyright 2002-2008 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Robert Richter <robert.richter@amd.com>
 */

#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/sysdev.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/kdebug.h>
#include <linux/cpu.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/apic.h>

#include "op_counter.h"
#include "op_x86_model.h"

static struct op_x86_model_spec const *model;
static DEFINE_PER_CPU(struct op_msrs, cpu_msrs);
static DEFINE_PER_CPU(unsigned long, saved_lvtpc);

/* 0 == registered but off, 1 == registered and on */
static int nmi_enabled = 0;

static int profile_exceptions_notify(struct notifier_block *self,
				     unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;
	int cpu = smp_processor_id();

	switch (val) {
	case DIE_NMI:
	case DIE_NMI_IPI:
		model->check_ctrs(args->regs, &per_cpu(cpu_msrs, cpu));
		ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	return ret;
}

static void nmi_cpu_save_registers(struct op_msrs *msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls;
	struct op_msr *counters = msrs->counters;
	struct op_msr *controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < nr_ctrs; ++i) {
		if (counters[i].addr) {
			rdmsr(counters[i].addr,
				counters[i].saved.low,
				counters[i].saved.high);
		}
	}

	for (i = 0; i < nr_ctrls; ++i) {
		if (controls[i].addr) {
			rdmsr(controls[i].addr,
				controls[i].saved.low,
				controls[i].saved.high);
		}
	}
}

static void nmi_save_registers(void *dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs *msrs = &per_cpu(cpu_msrs, cpu);
	nmi_cpu_save_registers(msrs);
}

static void free_msrs(void)
{
	int i;
	for_each_possible_cpu(i) {
		kfree(per_cpu(cpu_msrs, i).counters);
		per_cpu(cpu_msrs, i).counters = NULL;
		kfree(per_cpu(cpu_msrs, i).controls);
		per_cpu(cpu_msrs, i).controls = NULL;
	}
}

static int allocate_msrs(void)
{
	int success = 1;
	size_t controls_size = sizeof(struct op_msr) * model->num_controls;
	size_t counters_size = sizeof(struct op_msr) * model->num_counters;

	int i;
	for_each_possible_cpu(i) {
		per_cpu(cpu_msrs, i).counters = kmalloc(counters_size,
								GFP_KERNEL);
		if (!per_cpu(cpu_msrs, i).counters) {
			success = 0;
			break;
		}
		per_cpu(cpu_msrs, i).controls = kmalloc(controls_size,
								GFP_KERNEL);
		if (!per_cpu(cpu_msrs, i).controls) {
			success = 0;
			break;
		}
	}

	if (!success)
		free_msrs();

	return success;
}

static void nmi_cpu_setup(void *dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs *msrs = &per_cpu(cpu_msrs, cpu);
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(msrs);
	spin_unlock(&oprofilefs_lock);
	per_cpu(saved_lvtpc, cpu) = apic_read(APIC_LVTPC);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

static struct notifier_block profile_exceptions_nb = {
	.notifier_call = profile_exceptions_notify,
	.next = NULL,
	.priority = 2
};

static int nmi_setup(void)
{
	int err = 0;
	int cpu;

	if (!allocate_msrs())
		return -ENOMEM;

	err = register_die_notifier(&profile_exceptions_nb);
	if (err) {
		free_msrs();
		return err;
	}

	/* We need to serialize save and setup for HT because the subset
	 * of msrs are distinct for save and setup operations
	 */

	/* Assume saved/restored counters are the same on all CPUs */
	model->fill_in_addresses(&per_cpu(cpu_msrs, 0));
	for_each_possible_cpu(cpu) {
		if (cpu != 0) {
			memcpy(per_cpu(cpu_msrs, cpu).counters,
				per_cpu(cpu_msrs, 0).counters,
				sizeof(struct op_msr) * model->num_counters);

			memcpy(per_cpu(cpu_msrs, cpu).controls,
				per_cpu(cpu_msrs, 0).controls,
				sizeof(struct op_msr) * model->num_controls);
		}

	}
	on_each_cpu(nmi_save_registers, NULL, 1);
	on_each_cpu(nmi_cpu_setup, NULL, 1);
	nmi_enabled = 1;
	return 0;
}

static void nmi_restore_registers(struct op_msrs *msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls;
	struct op_msr *counters = msrs->counters;
	struct op_msr *controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < nr_ctrls; ++i) {
		if (controls[i].addr) {
			wrmsr(controls[i].addr,
				controls[i].saved.low,
				controls[i].saved.high);
		}
	}

	for (i = 0; i < nr_ctrs; ++i) {
		if (counters[i].addr) {
			wrmsr(counters[i].addr,
				counters[i].saved.low,
				counters[i].saved.high);
		}
	}
}

static void nmi_cpu_shutdown(void *dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();
	struct op_msrs *msrs = &__get_cpu_var(cpu_msrs);

	/* restoring APIC_LVTPC can trigger an apic error because the delivery
	 * mode and vector nr combination can be illegal. That's by design: on
	 * power on apic lvt contain a zero vector nr which are legal only for
	 * NMI delivery mode. So inhibit apic err before restoring lvtpc
	 */
	v = apic_read(APIC_LVTERR);
	apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	apic_write(APIC_LVTPC, per_cpu(saved_lvtpc, cpu));
	apic_write(APIC_LVTERR, v);
	nmi_restore_registers(msrs);
}

static void nmi_shutdown(void)
{
	struct op_msrs *msrs;

	nmi_enabled = 0;
	on_each_cpu(nmi_cpu_shutdown, NULL, 1);
	unregister_die_notifier(&profile_exceptions_nb);
	msrs = &get_cpu_var(cpu_msrs);
	model->shutdown(msrs);
	free_msrs();
	put_cpu_var(cpu_msrs);
}

static void nmi_cpu_start(void *dummy)
{
	struct op_msrs const *msrs = &__get_cpu_var(cpu_msrs);
	model->start(msrs);
}

static int nmi_start(void)
{
	on_each_cpu(nmi_cpu_start, NULL, 1);
	return 0;
}

static void nmi_cpu_stop(void *dummy)
{
	struct op_msrs const *msrs = &__get_cpu_var(cpu_msrs);
	model->stop(msrs);
}

static void nmi_stop(void)
{
	on_each_cpu(nmi_cpu_stop, NULL, 1);
}

struct op_counter_config counter_config[OP_MAX_COUNTER];

static int nmi_create_files(struct super_block *sb, struct dentry *root)
{
	unsigned int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
		char buf[4];

		/* quick little hack to _not_ expose a counter if it is not
		 * available for use.  This should protect userspace app.
		 * NOTE:  assumes 1:1 mapping here (that counters are organized
		 *        sequentially in their struct assignment).
		 */
		if (unlikely(!avail_to_resrv_perfctr_nmi_bit(i)))
			continue;

		snprintf(buf,  sizeof(buf), "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);
		oprofilefs_create_ulong(sb, dir, "enabled", &counter_config[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &counter_config[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &counter_config[i].count);
		oprofilefs_create_ulong(sb, dir, "unit_mask", &counter_config[i].unit_mask);
		oprofilefs_create_ulong(sb, dir, "kernel", &counter_config[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &counter_config[i].user);
	}

	return 0;
}

#ifdef CONFIG_SMP
static int oprofile_cpu_notifier(struct notifier_block *b, unsigned long action,
				 void *data)
{
	int cpu = (unsigned long)data;
	switch (action) {
	case CPU_DOWN_FAILED:
	case CPU_ONLINE:
		smp_call_function_single(cpu, nmi_cpu_start, NULL, 0);
		break;
	case CPU_DOWN_PREPARE:
		smp_call_function_single(cpu, nmi_cpu_stop, NULL, 1);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block oprofile_cpu_nb = {
	.notifier_call = oprofile_cpu_notifier
};
#endif

#ifdef CONFIG_PM

static int nmi_suspend(struct sys_device *dev, pm_message_t state)
{
	/* Only one CPU left, just stop that one */
	if (nmi_enabled == 1)
		nmi_cpu_stop(NULL);
	return 0;
}

static int nmi_resume(struct sys_device *dev)
{
	if (nmi_enabled == 1)
		nmi_cpu_start(NULL);
	return 0;
}

static struct sysdev_class oprofile_sysclass = {
	.name		= "oprofile",
	.resume		= nmi_resume,
	.suspend	= nmi_suspend,
};

static struct sys_device device_oprofile = {
	.id	= 0,
	.cls	= &oprofile_sysclass,
};

static int __init init_sysfs(void)
{
	int error;

	error = sysdev_class_register(&oprofile_sysclass);
	if (!error)
		error = sysdev_register(&device_oprofile);
	return error;
}

static void exit_sysfs(void)
{
	sysdev_unregister(&device_oprofile);
	sysdev_class_unregister(&oprofile_sysclass);
}

#else
#define init_sysfs() do { } while (0)
#define exit_sysfs() do { } while (0)
#endif /* CONFIG_PM */

static int __init p4_init(char **cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;

	if (cpu_model > 6 || cpu_model == 5)
		return 0;

#ifndef CONFIG_SMP
	*cpu_type = "i386/p4";
	model = &op_p4_spec;
	return 1;
#else
	switch (smp_num_siblings) {
	case 1:
		*cpu_type = "i386/p4";
		model = &op_p4_spec;
		return 1;

	case 2:
		*cpu_type = "i386/p4-ht";
		model = &op_p4_ht2_spec;
		return 1;
	}
#endif

	printk(KERN_INFO "oprofile: P4 HyperThreading detected with > 2 threads\n");
	printk(KERN_INFO "oprofile: Reverting to timer mode.\n");
	return 0;
}

static int force_arch_perfmon;
static int force_cpu_type(const char *str, struct kernel_param *kp)
{
	if (!strcmp(str, "archperfmon")) {
		force_arch_perfmon = 1;
		printk(KERN_INFO "oprofile: forcing architectural perfmon\n");
	}

	return 0;
}
module_param_call(cpu_type, force_cpu_type, NULL, NULL, 0);

static int __init ppro_init(char **cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;

	if (force_arch_perfmon && cpu_has_arch_perfmon)
		return 0;

	switch (cpu_model) {
	case 0 ... 2:
		*cpu_type = "i386/ppro";
		break;
	case 3 ... 5:
		*cpu_type = "i386/pii";
		break;
	case 6 ... 8:
	case 10 ... 11:
		*cpu_type = "i386/piii";
		break;
	case 9:
	case 13:
		*cpu_type = "i386/p6_mobile";
		break;
	case 14:
		*cpu_type = "i386/core";
		break;
	case 15: case 23:
		*cpu_type = "i386/core_2";
		break;
	case 26:
		arch_perfmon_setup_counters();
		*cpu_type = "i386/core_i7";
		break;
	case 28:
		*cpu_type = "i386/atom";
		break;
	default:
		/* Unknown */
		return 0;
	}

	model = &op_ppro_spec;
	return 1;
}

static int __init arch_perfmon_init(char **cpu_type)
{
	if (!cpu_has_arch_perfmon)
		return 0;
	*cpu_type = "i386/arch_perfmon";
	model = &op_arch_perfmon_spec;
	arch_perfmon_setup_counters();
	return 1;
}

/* in order to get sysfs right */
static int using_nmi;

int __init op_nmi_init(struct oprofile_operations *ops)
{
	__u8 vendor = boot_cpu_data.x86_vendor;
	__u8 family = boot_cpu_data.x86;
	char *cpu_type = NULL;
	int ret = 0;

	if (!cpu_has_apic)
		return -ENODEV;

	switch (vendor) {
	case X86_VENDOR_AMD:
		/* Needs to be at least an Athlon (or hammer in 32bit mode) */

		switch (family) {
		default:
			return -ENODEV;
		case 6:
			model = &op_amd_spec;
			cpu_type = "i386/athlon";
			break;
		case 0xf:
			model = &op_amd_spec;
			/* Actually it could be i386/hammer too, but give
			 user space an consistent name. */
			cpu_type = "x86-64/hammer";
			break;
		case 0x10:
			model = &op_amd_spec;
			cpu_type = "x86-64/family10";
			break;
		case 0x11:
			model = &op_amd_spec;
			cpu_type = "x86-64/family11h";
			break;
		}
		break;

	case X86_VENDOR_INTEL:
		switch (family) {
			/* Pentium IV */
		case 0xf:
			p4_init(&cpu_type);
			break;

			/* A P6-class processor */
		case 6:
			ppro_init(&cpu_type);
			break;

		default:
			break;
		}

		if (!cpu_type && !arch_perfmon_init(&cpu_type))
			return -ENODEV;
		break;

	default:
		return -ENODEV;
	}

#ifdef CONFIG_SMP
	register_cpu_notifier(&oprofile_cpu_nb);
#endif
	/* default values, can be overwritten by model */
	ops->create_files = nmi_create_files;
	ops->setup = nmi_setup;
	ops->shutdown = nmi_shutdown;
	ops->start = nmi_start;
	ops->stop = nmi_stop;
	ops->cpu_type = cpu_type;

	if (model->init)
		ret = model->init(ops);
	if (ret)
		return ret;

	init_sysfs();
	using_nmi = 1;
	printk(KERN_INFO "oprofile: using NMI interrupt.\n");
	return 0;
}

void op_nmi_exit(void)
{
	if (using_nmi) {
		exit_sysfs();
#ifdef CONFIG_SMP
		unregister_cpu_notifier(&oprofile_cpu_nb);
#endif
	}
	if (model->exit)
		model->exit();
}
