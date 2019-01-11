// SPDX-License-Identifier: GPL-2.0
/*
 * The idle loop for all SuperH platforms.
 *
 *  Copyright (C) 2002 - 2009  Paul Mundt
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/preempt.h>
#include <linux/thread_info.h>
#include <linux/irqflags.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <asm/pgalloc.h>
#include <asm/smp.h>
#include <asm/bl_bit.h>

static void (*sh_idle)(void);

void default_idle(void)
{
	set_bl_bit();
	local_irq_enable();
	/* Isn't this racy ? */
	cpu_sleep();
	clear_bl_bit();
}

void arch_cpu_idle_dead(void)
{
	play_dead();
}

void arch_cpu_idle(void)
{
	sh_idle();
}

void __init select_idle_routine(void)
{
	/*
	 * If a platform has set its own idle routine, leave it alone.
	 */
	if (!sh_idle)
		sh_idle = default_idle;
}

void stop_this_cpu(void *unused)
{
	local_irq_disable();
	set_cpu_online(smp_processor_id(), false);

	for (;;)
		cpu_sleep();
}
