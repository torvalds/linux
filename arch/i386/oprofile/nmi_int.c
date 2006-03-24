/**
 * @file nmi_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/sysdev.h>
#include <linux/slab.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/apic.h>
 
#include "op_counter.h"
#include "op_x86_model.h"
 
static struct op_x86_model_spec const * model;
static struct op_msrs cpu_msrs[NR_CPUS];
static unsigned long saved_lvtpc[NR_CPUS];
 
static int nmi_start(void);
static void nmi_stop(void);

/* 0 == registered but off, 1 == registered and on */
static int nmi_enabled = 0;

#ifdef CONFIG_PM

static int nmi_suspend(struct sys_device *dev, pm_message_t state)
{
	if (nmi_enabled == 1)
		nmi_stop();
	return 0;
}


static int nmi_resume(struct sys_device *dev)
{
	if (nmi_enabled == 1)
		nmi_start();
	return 0;
}


static struct sysdev_class oprofile_sysclass = {
	set_kset_name("oprofile"),
	.resume		= nmi_resume,
	.suspend	= nmi_suspend,
};


static struct sys_device device_oprofile = {
	.id	= 0,
	.cls	= &oprofile_sysclass,
};


static int __init init_driverfs(void)
{
	int error;
	if (!(error = sysdev_class_register(&oprofile_sysclass)))
		error = sysdev_register(&device_oprofile);
	return error;
}


static void exit_driverfs(void)
{
	sysdev_unregister(&device_oprofile);
	sysdev_class_unregister(&oprofile_sysclass);
}

#else
#define init_driverfs() do { } while (0)
#define exit_driverfs() do { } while (0)
#endif /* CONFIG_PM */


static int nmi_callback(struct pt_regs * regs, int cpu)
{
	return model->check_ctrs(regs, &cpu_msrs[cpu]);
}
 
 
static void nmi_cpu_save_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr * counters = msrs->counters;
	struct op_msr * controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < nr_ctrs; ++i) {
		rdmsr(counters[i].addr,
			counters[i].saved.low,
			counters[i].saved.high);
	}
 
	for (i = 0; i < nr_ctrls; ++i) {
		rdmsr(controls[i].addr,
			controls[i].saved.low,
			controls[i].saved.high);
	}
}


static void nmi_save_registers(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	model->fill_in_addresses(msrs);
	nmi_cpu_save_registers(msrs);
}


static void free_msrs(void)
{
	int i;
	for_each_cpu(i) {
		kfree(cpu_msrs[i].counters);
		cpu_msrs[i].counters = NULL;
		kfree(cpu_msrs[i].controls);
		cpu_msrs[i].controls = NULL;
	}
}


static int allocate_msrs(void)
{
	int success = 1;
	size_t controls_size = sizeof(struct op_msr) * model->num_controls;
	size_t counters_size = sizeof(struct op_msr) * model->num_counters;

	int i;
	for_each_online_cpu(i) {
		cpu_msrs[i].counters = kmalloc(counters_size, GFP_KERNEL);
		if (!cpu_msrs[i].counters) {
			success = 0;
			break;
		}
		cpu_msrs[i].controls = kmalloc(controls_size, GFP_KERNEL);
		if (!cpu_msrs[i].controls) {
			success = 0;
			break;
		}
	}

	if (!success)
		free_msrs();

	return success;
}


static void nmi_cpu_setup(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(msrs);
	spin_unlock(&oprofilefs_lock);
	saved_lvtpc[cpu] = apic_read(APIC_LVTPC);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}


static int nmi_setup(void)
{
	if (!allocate_msrs())
		return -ENOMEM;

	/* We walk a thin line between law and rape here.
	 * We need to be careful to install our NMI handler
	 * without actually triggering any NMIs as this will
	 * break the core code horrifically.
	 */
	if (reserve_lapic_nmi() < 0) {
		free_msrs();
		return -EBUSY;
	}
	/* We need to serialize save and setup for HT because the subset
	 * of msrs are distinct for save and setup operations
	 */
	on_each_cpu(nmi_save_registers, NULL, 0, 1);
	on_each_cpu(nmi_cpu_setup, NULL, 0, 1);
	set_nmi_callback(nmi_callback);
	nmi_enabled = 1;
	return 0;
}


static void nmi_restore_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr * counters = msrs->counters;
	struct op_msr * controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < nr_ctrls; ++i) {
		wrmsr(controls[i].addr,
			controls[i].saved.low,
			controls[i].saved.high);
	}
 
	for (i = 0; i < nr_ctrs; ++i) {
		wrmsr(counters[i].addr,
			counters[i].saved.low,
			counters[i].saved.high);
	}
}
 

static void nmi_cpu_shutdown(void * dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
 
	/* restoring APIC_LVTPC can trigger an apic error because the delivery
	 * mode and vector nr combination can be illegal. That's by design: on
	 * power on apic lvt contain a zero vector nr which are legal only for
	 * NMI delivery mode. So inhibit apic err before restoring lvtpc
	 */
	v = apic_read(APIC_LVTERR);
	apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	apic_write(APIC_LVTPC, saved_lvtpc[cpu]);
	apic_write(APIC_LVTERR, v);
	nmi_restore_registers(msrs);
}

 
static void nmi_shutdown(void)
{
	nmi_enabled = 0;
	on_each_cpu(nmi_cpu_shutdown, NULL, 0, 1);
	unset_nmi_callback();
	release_lapic_nmi();
	free_msrs();
}

 
static void nmi_cpu_start(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->start(msrs);
}
 

static int nmi_start(void)
{
	on_each_cpu(nmi_cpu_start, NULL, 0, 1);
	return 0;
}
 
 
static void nmi_cpu_stop(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->stop(msrs);
}
 
 
static void nmi_stop(void)
{
	on_each_cpu(nmi_cpu_stop, NULL, 0, 1);
}


struct op_counter_config counter_config[OP_MAX_COUNTER];

static int nmi_create_files(struct super_block * sb, struct dentry * root)
{
	unsigned int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry * dir;
		char buf[2];
 
		snprintf(buf, 2, "%d", i);
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
 
 
static int __init p4_init(char ** cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;

	if (cpu_model > 4)
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


static int __init ppro_init(char ** cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;

	if (cpu_model > 0xd)
		return 0;

	if (cpu_model == 9) {
		*cpu_type = "i386/p6_mobile";
	} else if (cpu_model > 5) {
		*cpu_type = "i386/piii";
	} else if (cpu_model > 2) {
		*cpu_type = "i386/pii";
	} else {
		*cpu_type = "i386/ppro";
	}

	model = &op_ppro_spec;
	return 1;
}

/* in order to get driverfs right */
static int using_nmi;

int __init op_nmi_init(struct oprofile_operations *ops)
{
	__u8 vendor = boot_cpu_data.x86_vendor;
	__u8 family = boot_cpu_data.x86;
	char *cpu_type;

	if (!cpu_has_apic)
		return -ENODEV;
 
	switch (vendor) {
		case X86_VENDOR_AMD:
			/* Needs to be at least an Athlon (or hammer in 32bit mode) */

			switch (family) {
			default:
				return -ENODEV;
			case 6:
				model = &op_athlon_spec;
				cpu_type = "i386/athlon";
				break;
			case 0xf:
				model = &op_athlon_spec;
				/* Actually it could be i386/hammer too, but give
				   user space an consistent name. */
				cpu_type = "x86-64/hammer";
				break;
			}
			break;
 
		case X86_VENDOR_INTEL:
			switch (family) {
				/* Pentium IV */
				case 0xf:
					if (!p4_init(&cpu_type))
						return -ENODEV;
					break;

				/* A P6-class processor */
				case 6:
					if (!ppro_init(&cpu_type))
						return -ENODEV;
					break;

				default:
					return -ENODEV;
			}
			break;

		default:
			return -ENODEV;
	}

	init_driverfs();
	using_nmi = 1;
	ops->create_files = nmi_create_files;
	ops->setup = nmi_setup;
	ops->shutdown = nmi_shutdown;
	ops->start = nmi_start;
	ops->stop = nmi_stop;
	ops->cpu_type = cpu_type;
	printk(KERN_INFO "oprofile: using NMI interrupt.\n");
	return 0;
}


void op_nmi_exit(void)
{
	if (using_nmi)
		exit_driverfs();
}
