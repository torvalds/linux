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
#include <asm/mce.h>

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

struct delayed_mce {
	struct timer_list timer;
	struct mce m;
};

/* Inject mce on current CPU */
static void raise_mce(unsigned long data)
{
	struct delayed_mce *dm = (struct delayed_mce *)data;
	struct mce *m = &dm->m;
	int cpu = m->extcpu;

	inject_mce(m);
	if (m->status & MCI_STATUS_UC) {
		struct pt_regs regs;
		memset(&regs, 0, sizeof(struct pt_regs));
		regs.ip = m->ip;
		regs.cs = m->cs;
		printk(KERN_INFO "Triggering MCE exception on CPU %d\n", cpu);
		do_machine_check(&regs, 0);
		printk(KERN_INFO "MCE exception done on CPU %d\n", cpu);
	} else {
		mce_banks_t b;
		memset(&b, 0xff, sizeof(mce_banks_t));
		printk(KERN_INFO "Starting machine check poll CPU %d\n", cpu);
		machine_check_poll(0, &b);
		mce_notify_irq();
		printk(KERN_INFO "Finished machine check poll on CPU %d\n",
		       cpu);
	}
	kfree(dm);
}

/* Error injection interface */
static ssize_t mce_write(struct file *filp, const char __user *ubuf,
			 size_t usize, loff_t *off)
{
	struct delayed_mce *dm;
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

	dm = kmalloc(sizeof(struct delayed_mce), GFP_KERNEL);
	if (!dm)
		return -ENOMEM;

	/*
	 * Need to give user space some time to set everything up,
	 * so do it a jiffie or two later everywhere.
	 * Should we use a hrtimer here for better synchronization?
	 */
	memcpy(&dm->m, &m, sizeof(struct mce));
	setup_timer(&dm->timer, raise_mce, (unsigned long)dm);
	dm->timer.expires = jiffies + 2;
	add_timer_on(&dm->timer, m.extcpu);
	return usize;
}

static int inject_init(void)
{
	printk(KERN_INFO "Machine check injector initialized\n");
	mce_chrdev_ops.write = mce_write;
	return 0;
}

module_init(inject_init);
/*
 * Cannot tolerate unloading currently because we cannot
 * guarantee all openers of mce_chrdev will get a reference to us.
 */
MODULE_LICENSE("GPL");
