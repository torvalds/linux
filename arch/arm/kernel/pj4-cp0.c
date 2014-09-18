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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/thread_notify.h>
#include <asm/cputype.h>

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

static struct notifier_block __maybe_unused iwmmxt_notifier_block = {
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

static int __init pj4_get_iwmmxt_version(void)
{
	u32 cp_access, wcid;

	cp_access = pj4_cp_access_read();
	pj4_cp_access_write(cp_access | 0xf);

	/* check if coprocessor 0 and 1 are available */
	if ((pj4_cp_access_read() & 0xf) != 0xf) {
		pj4_cp_access_write(cp_access);
		return -ENODEV;
	}

	/* read iWMMXt coprocessor id register p1, c0 */
	__asm__ __volatile__ ("mrc    p1, 0, %0, c0, c0, 0\n" : "=r" (wcid));

	pj4_cp_access_write(cp_access);

	/* iWMMXt v1 */
	if ((wcid & 0xffffff00) == 0x56051000)
		return 1;
	/* iWMMXt v2 */
	if ((wcid & 0xffffff00) == 0x56052000)
		return 2;

	return -EINVAL;
}

/*
 * Disable CP0/CP1 on boot, and let call_fpe() and the iWMMXt lazy
 * switch code handle iWMMXt context switching.
 */
static int __init pj4_cp0_init(void)
{
	u32 __maybe_unused cp_access;
	int vers;

	if (!cpu_is_pj4())
		return 0;

	vers = pj4_get_iwmmxt_version();
	if (vers < 0)
		return 0;

#ifndef CONFIG_IWMMXT
	pr_info("PJ4 iWMMXt coprocessor detected, but kernel support is missing.\n");
#else
	cp_access = pj4_cp_access_read() & ~0xf;
	pj4_cp_access_write(cp_access);

	pr_info("PJ4 iWMMXt v%d coprocessor enabled.\n", vers);
	elf_hwcap |= HWCAP_IWMMXT;
	thread_register_notifier(&iwmmxt_notifier_block);
#endif

	return 0;
}

late_initcall(pj4_cp0_init);
