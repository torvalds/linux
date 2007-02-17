/*
 * arch/arm/kernel/crunch.c
 * Cirrus MaverickCrunch context switching and handling
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
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
#include <asm/arch/ep93xx-regs.h>
#include <asm/thread_notify.h>
#include <asm/io.h>

struct crunch_state *crunch_owner;

void crunch_task_release(struct thread_info *thread)
{
	local_irq_disable();
	if (crunch_owner == &thread->crunchstate)
		crunch_owner = NULL;
	local_irq_enable();
}

static int crunch_enabled(u32 devcfg)
{
	return !!(devcfg & EP93XX_SYSCON_DEVICE_CONFIG_CRUNCH_ENABLE);
}

static int crunch_do(struct notifier_block *self, unsigned long cmd, void *t)
{
	struct thread_info *thread = (struct thread_info *)t;
	struct crunch_state *crunch_state;
	u32 devcfg;

	crunch_state = &thread->crunchstate;

	switch (cmd) {
	case THREAD_NOTIFY_FLUSH:
		memset(crunch_state, 0, sizeof(*crunch_state));

		/*
		 * FALLTHROUGH: Ensure we don't try to overwrite our newly
		 * initialised state information on the first fault.
		 */

	case THREAD_NOTIFY_RELEASE:
		crunch_task_release(thread);
		break;

	case THREAD_NOTIFY_SWITCH:
		devcfg = __raw_readl(EP93XX_SYSCON_DEVICE_CONFIG);
		if (crunch_enabled(devcfg) || crunch_owner == crunch_state) {
			devcfg ^= EP93XX_SYSCON_DEVICE_CONFIG_CRUNCH_ENABLE;
			__raw_writel(0xaa, EP93XX_SYSCON_SWLOCK);
			__raw_writel(devcfg, EP93XX_SYSCON_DEVICE_CONFIG);
		}
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block crunch_notifier_block = {
	.notifier_call	= crunch_do,
};

static int __init crunch_init(void)
{
	thread_register_notifier(&crunch_notifier_block);
	elf_hwcap |= HWCAP_CRUNCH;

	return 0;
}

late_initcall(crunch_init);
