/*
 * Machine check injection support.
 * Copyright 2008 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Authors:
 * Andi Kleen
 * Ying Huang
 */
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/notifier.h>
#include <linux/kdebug.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <asm/mce.h>
#include <asm/apic.h>

/* Update fake mce registers on current CPU. */
static void inject_mce(struct mce *m)
{
	struct mce *i = &per_cpu(injectm, m->extcpu);

	/* Make sure noone reads partially written injectm */
	i->finished = 0;
	mb();
	m->finished = 0;
	/* First set the fields after finished */
	i->extcpu = m->extcpu;
	mb();
	/* Now write record in order, finished last (except above) */
	memcpy(i, m, sizeof(struct mce));
	/* Finally activate it */
	mb();
	i->finished = 1;
}

static void raise_poll(struct mce *m)
{
	unsigned long flags;
	mce_banks_t b;

	memset(&b, 0xff, sizeof(mce_banks_t));
	local_irq_save(flags);
	machine_check_poll(0, &b);
	local_irq_restore(flags);
	m->finished = 0;
}

static void raise_exception(struct mce *m, struct pt_regs *pregs)
{
	struct pt_regs regs;
	unsigned long flags;

	if (!pregs) {
		memset(&regs, 0, sizeof(struct pt_regs));
		regs.ip = m->ip;
		regs.cs = m->cs;
		pregs = &regs;
	}
	/* in mcheck exeception handler, irq will be disabled */
	local_irq_save(flags);
	do_machine_check(pregs, 0);
	local_irq_restore(flags);
	m->finished = 0;
}

static cpumask_var_t mce_inject_cpumask;

static int mce_raise_notify(struct notifier_block *self,
			    unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int cpu = smp_processor_id();
	struct mce *m = &__get_cpu_var(injectm);
	if (val != DIE_NMI_IPI || !cpumask_test_cpu(cpu, mce_inject_cpumask))
		return NOTIFY_DONE;
	cpumask_clear_cpu(cpu, mce_inject_cpumask);
	if (m->inject_flags & MCJ_EXCEPTION)
		raise_exception(m, args->regs);
	else if (m->status)
		raise_poll(m);
	return NOTIFY_STOP;
}

static struct notifier_block mce_raise_nb = {
	.notifier_call = mce_raise_notify,
	.priority = 1000,
};

/* Inject mce on current CPU */
static int raise_local(void)
{
	struct mce *m = &__get_cpu_var(injectm);
	int context = MCJ_CTX(m->inject_flags);
	int ret = 0;
	int cpu = m->extcpu;

	if (m->inject_flags & MCJ_EXCEPTION) {
		printk(KERN_INFO "Triggering MCE exception on CPU %d\n", cpu);
		switch (context) {
		case MCJ_CTX_IRQ:
			/*
			 * Could do more to fake interrupts like
			 * calling irq_enter, but the necessary
			 * machinery isn't exported currently.
			 */
			/*FALL THROUGH*/
		case MCJ_CTX_PROCESS:
			raise_exception(m, NULL);
			break;
		default:
			printk(KERN_INFO "Invalid MCE context\n");
			ret = -EINVAL;
		}
		printk(KERN_INFO "MCE exception done on CPU %d\n", cpu);
	} else if (m->status) {
		printk(KERN_INFO "Starting machine check poll CPU %d\n", cpu);
		raise_poll(m);
		mce_notify_irq();
		printk(KERN_INFO "Machine check poll done on CPU %d\n", cpu);
	} else
		m->finished = 0;

	return ret;
}

static void raise_mce(struct mce *m)
{
	int context = MCJ_CTX(m->inject_flags);

	inject_mce(m);

	if (context == MCJ_CTX_RANDOM)
		return;

#ifdef CONFIG_X86_LOCAL_APIC
	if (m->inject_flags & MCJ_NMI_BROADCAST) {
		unsigned long start;
		int cpu;
		get_online_cpus();
		cpumask_copy(mce_inject_cpumask, cpu_online_mask);
		cpumask_clear_cpu(get_cpu(), mce_inject_cpumask);
		for_each_online_cpu(cpu) {
			struct mce *mcpu = &per_cpu(injectm, cpu);
			if (!mcpu->finished ||
			    MCJ_CTX(mcpu->inject_flags) != MCJ_CTX_RANDOM)
				cpumask_clear_cpu(cpu, mce_inject_cpumask);
		}
		if (!cpumask_empty(mce_inject_cpumask))
			apic->send_IPI_mask(mce_inject_cpumask, NMI_VECTOR);
		start = jiffies;
		while (!cpumask_empty(mce_inject_cpumask)) {
			if (!time_before(jiffies, start + 2*HZ)) {
				printk(KERN_ERR
				"Timeout waiting for mce inject NMI %lx\n",
					*cpumask_bits(mce_inject_cpumask));
				break;
			}
			cpu_relax();
		}
		raise_local();
		put_cpu();
		put_online_cpus();
	} else
#endif
		raise_local();
}

/* Error injection interface */
static ssize_t mce_write(struct file *filp, const char __user *ubuf,
			 size_t usize, loff_t *off)
{
	struct mce m;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*
	 * There are some cases where real MSR reads could slip
	 * through.
	 */
	if (!boot_cpu_has(X86_FEATURE_MCE) || !boot_cpu_has(X86_FEATURE_MCA))
		return -EIO;

	if ((unsigned long)usize > sizeof(struct mce))
		usize = sizeof(struct mce);
	if (copy_from_user(&m, ubuf, usize))
		return -EFAULT;

	if (m.extcpu >= num_possible_cpus() || !cpu_online(m.extcpu))
		return -EINVAL;

	/*
	 * Need to give user space some time to set everything up,
	 * so do it a jiffie or two later everywhere.
	 */
	schedule_timeout(2);
	raise_mce(&m);
	return usize;
}

static int inject_init(void)
{
	if (!alloc_cpumask_var(&mce_inject_cpumask, GFP_KERNEL))
		return -ENOMEM;
	printk(KERN_INFO "Machine check injector initialized\n");
	mce_chrdev_ops.write = mce_write;
	register_die_notifier(&mce_raise_nb);
	return 0;
}

module_init(inject_init);
/*
 * Cannot tolerate unloading currently because we cannot
 * guarantee all openers of mce_chrdev will get a reference to us.
 */
MODULE_LICENSE("GPL");
