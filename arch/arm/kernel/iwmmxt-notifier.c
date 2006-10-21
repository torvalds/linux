/*
 *  linux/arch/arm/kernel/iwmmxt-notifier.c
 *
 *  XScale iWMMXt (Concan) context switching and handling
 *
 *  Initial code:
 *  Copyright (c) 2003, Intel Corporation
 *
 *  Full lazy switching support, optimizations and more, by Nicolas Pitre
 *  Copyright (c) 2003-2004, MontaVista Software, Inc.
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
#include <asm/thread_notify.h>
#include <asm/io.h>

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

	case THREAD_NOTIFY_RELEASE:
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

static int __init iwmmxt_init(void)
{
	thread_register_notifier(&iwmmxt_notifier_block);

	return 0;
}

late_initcall(iwmmxt_init);
