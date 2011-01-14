/*
 * linux/arch/arm/kernel/pj4-cp0.c
 *
 * PJ4 iWMMXt coprocessor context switching and handling
 *
 * Copyright (c) 2010 Marvell International Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/thread_notify.h>

static int iwmmxt_do(struct notifier_block *self, unsigned long cmd, void *t)
{
	struct thread_info *thread = t;

	switch (cmd) {
	case THREAD_NOTIFY_FLUSH:
		/*
		 * flush_thread() zeroes thread->fpstate, so no need
		 * to do anything here.
		 *
		 * FALLTHROUGH: Ensure we don't try to overwrite our newly
		 * initialised state information on the first fault.
		 */

	case THREAD_NOTIFY_EXIT:
		iwmmxt_task_release(thread);
		break;

	case THREAD_NOTIFY_SWITCH:
		iwmmxt_task_switch(thread);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block iwmmxt_notifier_block = {
	.notifier_call	= iwmmxt_do,
};


static u32 __init pj4_cp_access_read(void)
{
	u32 value;

	__asm__ __volatile__ (
		"mrc	p15, 0, %0, c1, c0, 2\n\t"
		: "=r" (value));
	return value;
}

static void __init pj4_cp_access_write(u32 value)
{
	u32 temp;

	__asm__ __volatile__ (
		"mcr	p15, 0, %1, c1, c0, 2\n\t"
		"mrc	p15, 0, %0, c1, c0, 2\n\t"
		"mov	%0, %0\n\t"
		"sub	pc, pc, #4\n\t"
		: "=r" (temp) : "r" (value));
}


/*
 * Disable CP0/CP1 on boot, and let call_fpe() and the iWMMXt lazy
 * switch code handle iWMMXt context switching.
 */
static int __init pj4_cp0_init(void)
{
	u32 cp_access;

	cp_access = pj4_cp_access_read() & ~0xf;
	pj4_cp_access_write(cp_access);

	printk(KERN_INFO "PJ4 iWMMXt coprocessor enabled.\n");
	elf_hwcap |= HWCAP_IWMMXT;
	thread_register_notifier(&iwmmxt_notifier_block);

	return 0;
}

late_initcall(pj4_cp0_init);
