/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Imagination Technologies Ltd.
 */
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/mips_mt.h>
#include <asm/vpe.h>
#include <asm/rtlx.h>

static int major;

static void rtlx_dispatch(void)
{
	if (read_c0_cause() & read_c0_status() & C_SW0)
		do_IRQ(MIPS_CPU_IRQ_BASE + MIPS_CPU_RTLX_IRQ);
}

/*
 * Interrupt handler may be called before rtlx_init has otherwise had
 * a chance to run.
 */
static irqreturn_t rtlx_interrupt(int irq, void *dev_id)
{
	unsigned int vpeflags;
	unsigned long flags;
	int i;

	/* Ought not to be strictly necessary for SMTC builds */
	local_irq_save(flags);
	vpeflags = dvpe();
	set_c0_status(0x100 << MIPS_CPU_RTLX_IRQ);
	irq_enable_hazard();
	evpe(vpeflags);
	local_irq_restore(flags);

	for (i = 0; i < RTLX_CHANNELS; i++) {
		wake_up(&channel_wqs[i].lx_queue);
		wake_up(&channel_wqs[i].rt_queue);
	}

	return IRQ_HANDLED;
}

static struct irqaction rtlx_irq = {
	.handler	= rtlx_interrupt,
	.name		= "RTLX",
};

static int rtlx_irq_num = MIPS_CPU_IRQ_BASE + MIPS_CPU_RTLX_IRQ;

void _interrupt_sp(void)
{
	unsigned long flags;

	local_irq_save(flags);
	dvpe();
	settc(1);
	write_vpe_c0_cause(read_vpe_c0_cause() | C_SW0);
	evpe(EVPE_ENABLE);
	local_irq_restore(flags);
}

int __init rtlx_module_init(void)
{
	struct device *dev;
	int i, err;

	if (!cpu_has_mipsmt) {
		pr_warn("VPE loader: not a MIPS MT capable processor\n");
		return -ENODEV;
	}

	if (aprp_cpu_index() == 0) {
		pr_warn("No TCs reserved for AP/SP, not initializing RTLX.\n"
			"Pass maxtcs=<n> argument as kernel argument\n");

		return -ENODEV;
	}

	major = register_chrdev(0, RTLX_MODULE_NAME, &rtlx_fops);
	if (major < 0) {
		pr_err("rtlx_module_init: unable to register device\n");
		return major;
	}

	/* initialise the wait queues */
	for (i = 0; i < RTLX_CHANNELS; i++) {
		init_waitqueue_head(&channel_wqs[i].rt_queue);
		init_waitqueue_head(&channel_wqs[i].lx_queue);
		atomic_set(&channel_wqs[i].in_open, 0);
		mutex_init(&channel_wqs[i].mutex);

		dev = device_create(mt_class, NULL, MKDEV(major, i), NULL,
				    "%s%d", RTLX_MODULE_NAME, i);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			goto out_chrdev;
		}
	}

	/* set up notifiers */
	rtlx_notify.start = rtlx_starting;
	rtlx_notify.stop = rtlx_stopping;
	vpe_notify(aprp_cpu_index(), &rtlx_notify);

	if (cpu_has_vint) {
		aprp_hook = rtlx_dispatch;
	} else {
		pr_err("APRP RTLX init on non-vectored-interrupt processor\n");
		err = -ENODEV;
		goto out_class;
	}

	rtlx_irq.dev_id = rtlx;
	err = setup_irq(rtlx_irq_num, &rtlx_irq);
	if (err)
		goto out_class;

	return 0;

out_class:
	for (i = 0; i < RTLX_CHANNELS; i++)
		device_destroy(mt_class, MKDEV(major, i));
out_chrdev:
	unregister_chrdev(major, RTLX_MODULE_NAME);

	return err;
}

void __exit rtlx_module_exit(void)
{
	int i;

	for (i = 0; i < RTLX_CHANNELS; i++)
		device_destroy(mt_class, MKDEV(major, i));
	unregister_chrdev(major, RTLX_MODULE_NAME);
}
