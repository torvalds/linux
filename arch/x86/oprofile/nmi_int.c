/**
 * @file nmi_int.c
 *
 * @remark Copyright 2002-2009 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Robert Richter <robert.richter@amd.com>
 * @author Barry Kasindorf <barry.kasindorf@amd.com>
 * @author Jason Yeh <jason.yeh@amd.com>
 * @author Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
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

static struct op_x86_model_spec *model;
static DEFINE_PER_CPU(struct op_msrs, cpu_msrs);
static DEFINE_PER_CPU(unsigned long, saved_lvtpc);

/* 0 == registered but off, 1 == registered and on */
static int nmi_enabled = 0;

struct op_counter_config counter_config[OP_MAX_COUNTER];

/* common functions */

u64 op_x86_get_ctrl(struct op_x86_model_spec const *model,
		    struct op_counter_config *counter_config)
{
	u64 val = 0;
	u16 event = (u16)counter_config->event;

	val |= ARCH_PERFMON_EVENTSEL_INT;
	val |= counter_config->user ? ARCH_PERFMON_EVENTSEL_USR : 0;
	val |= counter_config->kernel ? ARCH_PERFMON_EVENTSEL_OS : 0;
	val |= (counter_config->unit_mask & 0xFF) << 8;
	event &= model->event_mask ? model->event_mask : 0xFF;
	val |= event & 0xFF;
	val |= (event & 0x0F00) << 24;

	return val;
}


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
	struct op_msr *counters = msrs->counters;
	struct op_msr *controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < model->num_counters; ++i) {
		if (counters[i].addr)
			rdmsrl(counters[i].addr, counters[i].saved);
	}

	for (i = 0; i < model->num_controls; ++i) {
		if (controls[i].addr)
			rdmsrl(controls[i].addr, controls[i].saved);
	}
}

static void nmi_cpu_start(void *dummy)
{
	struct op_msrs const *msrs = &__get_cpu_var(cpu_msrs);
	if (!msrs->controls)
		WARN_ON_ONCE(1);
	else
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
	if (!msrs->controls)
		WARN_ON_ONCE(1);
	else
		model->stop(msrs);
}

static void nmi_stop(void)
{
	on_each_cpu(nmi_cpu_stop, NULL, 1);
}

#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX

static DEFINE_PER_CPU(int, switch_index);

static inline int has_mux(void)
{
	return !!model->switch_ctrl;
}

inline int op_x86_phys_to_virt(int phys)
{
	return __get_cpu_var(switch_index) + phys;
}

inline int op_x86_virt_to_phys(int virt)
{
	return virt % model->num_counters;
}

static void nmi_shutdown_mux(void)
{
	int i;

	if (!has_mux())
		return;

	for_each_possible_cpu(i) {
		kfree(per_cpu(cpu_msrs, i).multiplex);
		per_cpu(cpu_msrs, i).multiplex = NULL;
		per_cpu(switch_index, i) = 0;
	}
}

static int nmi_setup_mux(void)
{
	size_t multiplex_size =
		sizeof(struct op_msr) * model->num_virt_counters;
	int i;

	if (!has_mux())
		return 1;

	for_each_possible_cpu(i) {
		per_cpu(cpu_msrs, i).multiplex =
			kzalloc(multiplex_size, GFP_KERNEL);
		if (!per_cpu(cpu_msrs, i).multiplex)
			return 0;
	}

	return 1;
}

static void nmi_cpu_setup_mux(int cpu, struct op_msrs const * const msrs)
{
	int i;
	struct op_msr *multiplex = msrs->multiplex;

	if (!has_mux())
		return;

	for (i = 0; i < model->num_virt_counters; ++i) {
		if (counter_config[i].enabled) {
			multiplex[i].saved = -(u64)counter_config[i].count;
		} else {
			multiplex[i].saved = 0;
		}
	}

	per_cpu(switch_index, cpu) = 0;
}

static void nmi_cpu_save_mpx_registers(struct op_msrs *msrs)
{
	struct op_msr *counters = msrs->counters;
	struct op_msr *multiplex = msrs->multiplex;
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		int virt = op_x86_phys_to_virt(i);
		if (counters[i].addr)
			rdmsrl(counters[i].addr, multiplex[virt].saved);
	}
}

static void nmi_cpu_restore_mpx_registers(struct op_msrs *msrs)
{
	struct op_msr *counters = msrs->counters;
	struct op_msr *multiplex = msrs->multiplex;
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		int virt = op_x86_phys_to_virt(i);
		if (counters[i].addr)
			wrmsrl(counters[i].addr, multiplex[virt].saved);
	}
}

static void nmi_cpu_switch(void *dummy)
{
	int cpu = smp_processor_id();
	int si = per_cpu(switch_index, cpu);
	struct op_msrs *msrs = &per_cpu(cpu_msrs, cpu);

	nmi_cpu_stop(NULL);
	nmi_cpu_save_mpx_registers(msrs);

	/* move to next set */
	si += model->num_counters;
	if ((si >= model->num_virt_counters) || (counter_config[si].count == 0))
		per_cpu(switch_index, cpu) = 0;
	else
		per_cpu(switch_index, cpu) = si;

	model->switch_ctrl(model, msrs);
	nmi_cpu_restore_mpx_registers(msrs);

	nmi_cpu_start(NULL);
}


/*
 * Quick check to see if multiplexing is necessary.
 * The check should be sufficient since counters are used
 * in ordre.
 */
static int nmi_multiplex_on(void)
{
	return counter_config[model->num_counters].count ? 0 : -EINVAL;
}

static int nmi_switch_event(void)
{
	if (!has_mux())
		return -ENOSYS;		/* not implemented */
	if (nmi_multiplex_on() < 0)
		return -EINVAL;		/* not necessary */

	on_each_cpu(nmi_cpu_switch, NULL, 1);

	return 0;
}

static inline void mux_init(struct oprofile_operations *ops)
{
	if (has_mux())
		ops->switch_events = nmi_switch_event;
}

static void mux_clone(int cpu)
{
	if (!has_mux())
		return;

	memcpy(per_cpu(cpu_msrs, cpu).multiplex,
	       per_cpu(cpu_msrs, 0).multiplex,
	       sizeof(struct op_msr) * model->num_virt_counters);
}

#else

inline int op_x86_phys_to_virt(int phys) { return phys; }
inline int op_x86_virt_to_phys(int virt) { return virt; }
static inline void nmi_shutdown_mux(void) { }
static inline int nmi_setup_mux(void) { return 1; }
static inline void
nmi_cpu_setup_mux(int cpu, struct op_msrs const * const msrs) { }
static inline void mux_init(struct oprofile_operations *ops) { }
static void mux_clone(int cpu) { }

#endif

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
	size_t controls_size = sizeof(struct op_msr) * model->num_controls;
	size_t counters_size = sizeof(struct op_msr) * model->num_counters;

	int i;
	for_each_possible_cpu(i) {
		per_cpu(cpu_msrs, i).counters = kzalloc(counters_size,
							GFP_KERNEL);
		if (!per_cpu(cpu_msrs, i).counters)
			return 0;
		per_cpu(cpu_msrs, i).controls = kzalloc(controls_size,
							GFP_KERNEL);
		if (!per_cpu(cpu_msrs, i).controls)
			return 0;
	}

	return 1;
}

static void nmi_cpu_setup(void *dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs *msrs = &per_cpu(cpu_msrs, cpu);
	nmi_cpu_save_registers(msrs);
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(model, msrs);
	nmi_cpu_setup_mux(cpu, msrs);
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
		err = -ENOMEM;
	else if (!nmi_setup_mux())
		err = -ENOMEM;
	else
		err = register_die_notifier(&profile_exceptions_nb);

	if (err) {
		free_msrs();
		nmi_shutdown_mux();
		return err;
	}

	/* We need to serialize save and setup for HT because the subset
	 * of msrs are distinct for save and setup operations
	 */

	/* Assume saved/restored counters are the same on all CPUs */
	model->fill_in_addresses(&per_cpu(cpu_msrs, 0));
	for_each_possible_cpu(cpu) {
		if (!cpu)
			continue;

		memcpy(per_cpu(cpu_msrs, cpu).counters,
		       per_cpu(cpu_msrs, 0).counters,
		       sizeof(struct op_msr) * model->num_counters);

		memcpy(per_cpu(cpu_msrs, cpu).controls,
		       per_cpu(cpu_msrs, 0).controls,
		       sizeof(struct op_msr) * model->num_controls);

		mux_clone(cpu);
	}
	on_each_cpu(nmi_cpu_setup, NULL, 1);
	nmi_enabled = 1;
	return 0;
}

static void nmi_cpu_restore_registers(struct op_msrs *msrs)
{
	struct op_msr *counters = msrs->counters;
	struct op_msr *controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < model->num_controls; ++i) {
		if (controls[i].addr)
			wrmsrl(controls[i].addr, controls[i].saved);
	}

	for (i = 0; i < model->num_counters; ++i) {
		if (counters[i].addr)
			wrmsrl(counters[i].addr, counters[i].saved);
	}
}

static void nmi_cpu_shutdown(void *dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();
	struct op_msrs *msrs = &per_cpu(cpu_msrs, cpu);

	/* restoring APIC_LVTPC can trigger an apic error because the delivery
	 * mode and vector nr combination can be illegal. That's by design: on
	 * power on apic lvt contain a zero vector nr which are legal only for
	 * NMI delivery mode. So inhibit apic err before restoring lvtpc
	 */
	v = apic_read(APIC_LVTERR);
	apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	apic_write(APIC_LVTPC, per_cpu(saved_lvtpc, cpu));
	apic_write(APIC_LVTERR, v);
	nmi_cpu_restore_registers(msrs);
}

static void nmi_shutdown(void)
{
	struct op_msrs *msrs;

	nmi_enabled = 0;
	on_each_cpu(nmi_cpu_shutdown, NULL, 1);
	unregister_die_notifier(&profile_exceptions_nb);
	nmi_shutdown_mux();
	msrs = &get_cpu_var(cpu_msrs);
	model->shutdown(msrs);
	free_msrs();
	put_cpu_var(cpu_msrs);
}

static int nmi_create_files(struct super_block *sb, struct dentry *root)
{
	unsigned int i;

	for (i = 0; i < model->num_virt_counters; ++i) {
		struct dentry *dir;
		char buf[4];

		/* quick little hack to _not_ expose a counter if it is not
		 * available for use.  This should protect userspace app.
		 * NOTE:  assumes 1:1 mapping here (that counters are organized
		 *        sequentially in their struct assignment).
		 */
		if (!avail_to_resrv_perfctr_nmi_bit(op_x86_virt_to_phys(i)))
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
	if (!strcmp(str, "arch_perfmon")) {
		force_arch_perfmon = 1;
		printk(KERN_INFO "oprofile: forcing architectural perfmon\n");
	}

	return 0;
}
module_param_call(cpu_type, force_cpu_type, NULL, NULL, 0);

static int __init ppro_init(char **cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;
	struct op_x86_model_spec *spec = &op_ppro_spec;	/* default */

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
	case 0x2e:
	case 26:
		spec = &op_arch_perfmon_spec;
		*cpu_type = "i386/core_i7";
		break;
	case 28:
		*cpu_type = "i386/atom";
		break;
	default:
		/* Unknown */
		return 0;
	}

	model = spec;
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
		case 6:
			cpu_type = "i386/athlon";
			break;
		case 0xf:
			/*
			 * Actually it could be i386/hammer too, but
			 * give user space an consistent name.
			 */
			cpu_type = "x86-64/hammer";
			break;
		case 0x10:
			cpu_type = "x86-64/family10";
			break;
		case 0x11:
			cpu_type = "x86-64/family11h";
			break;
		default:
			return -ENODEV;
		}
		model = &op_amd_spec;
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

		if (cpu_type)
			break;

		if (!cpu_has_arch_perfmon)
			return -ENODEV;

		/* use arch perfmon as fallback */
		cpu_type = "i386/arch_perfmon";
		model = &op_arch_perfmon_spec;
		break;

	default:
		return -ENODEV;
	}

#ifdef CONFIG_SMP
	register_cpu_notifier(&oprofile_cpu_nb);
#endif
	/* default values, can be overwritten by model */
	ops->create_files	= nmi_create_files;
	ops->setup		= nmi_setup;
	ops->shutdown		= nmi_shutdown;
	ops->start		= nmi_start;
	ops->stop		= nmi_stop;
	ops->cpu_type		= cpu_type;

	if (model->init)
		ret = model->init(ops);
	if (ret)
		return ret;

	if (!model->num_virt_counters)
		model->num_virt_counters = model->num_counters;

	mux_init(ops);

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
