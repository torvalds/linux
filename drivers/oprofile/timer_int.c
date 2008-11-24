/**
 * @file timer_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/profile.h>
#include <linux/init.h>
#include <asm/ptrace.h>

#include "oprof.h"

static int timer_notify(struct pt_regs *regs)
{
	oprofile_add_sample(regs, 0);
	return 0;
}

static int timer_start(void)
{
	return register_timer_hook(timer_notify);
}


static void timer_stop(void)
{
	unregister_timer_hook(timer_notify);
}


void __init oprofile_timer_init(struct oprofile_operations *ops)
{
	ops->create_files = NULL;
	ops->setup = NULL;
	ops->shutdown = NULL;
	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
}
