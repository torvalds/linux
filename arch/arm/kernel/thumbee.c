// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/kernel/thumbee.c
 *
 * Copyright (C) 2008 ARM Limited
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/cputype.h>
#include <asm/system_info.h>
#include <asm/thread_notify.h>

/*
 * Access to the ThumbEE Handler Base register
 */
static inline unsigned long teehbr_read(void)
{
	unsigned long v;
	asm("mrc	p14, 6, %0, c1, c0, 0\n" : "=r" (v));
	return v;
}

static inline void teehbr_write(unsigned long v)
{
	asm("mcr	p14, 6, %0, c1, c0, 0\n" : : "r" (v));
}

static int thumbee_notifier(struct notifier_block *self, unsigned long cmd, void *t)
{
	struct thread_info *thread = t;

	switch (cmd) {
	case THREAD_NOTIFY_FLUSH:
		teehbr_write(0);
		break;
	case THREAD_NOTIFY_SWITCH:
		current_thread_info()->thumbee_state = teehbr_read();
		teehbr_write(thread->thumbee_state);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block thumbee_notifier_block = {
	.notifier_call	= thumbee_notifier,
};

static int __init thumbee_init(void)
{
	unsigned long pfr0;
	unsigned int cpu_arch = cpu_architecture();

	if (cpu_arch < CPU_ARCH_ARMv7)
		return 0;

	pfr0 = read_cpuid_ext(CPUID_EXT_PFR0);
	if ((pfr0 & 0x0000f000) != 0x00001000)
		return 0;

	pr_info("ThumbEE CPU extension supported.\n");
	elf_hwcap |= HWCAP_THUMBEE;
	thread_register_notifier(&thumbee_notifier_block);

	return 0;
}

late_initcall(thumbee_init);
