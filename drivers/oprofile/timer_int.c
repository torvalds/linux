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
#include <linux/cpu.h>
#include <linux/hrtimer.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>

#include "oprof.h"

static DEFINE_PER_CPU(struct hrtimer, oprofile_hrtimer);

static enum hrtimer_restart oprofile_hrtimer_notify(struct hrtimer *hrtimer)
{
	oprofile_add_sample(get_irq_regs(), 0);
	hrtimer_forward_now(hrtimer, ns_to_ktime(TICK_NSEC));
	return HRTIMER_RESTART;
}

static void __oprofile_hrtimer_start(void *unused)
{
	struct hrtimer *hrtimer = &__get_cpu_var(oprofile_hrtimer);

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = oprofile_hrtimer_notify;

	hrtimer_start(hrtimer, ns_to_ktime(TICK_NSEC),
		      HRTIMER_MODE_REL_PINNED);
}

static int oprofile_hrtimer_start(void)
{
	on_each_cpu(__oprofile_hrtimer_start, NULL, 1);
	return 0;
}

static void __oprofile_hrtimer_stop(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(oprofile_hrtimer, cpu);

	hrtimer_cancel(hrtimer);
}

static void oprofile_hrtimer_stop(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		__oprofile_hrtimer_stop(cpu);
}

static int __cpuinit oprofile_cpu_notify(struct notifier_block *self,
					 unsigned long action, void *hcpu)
{
	long cpu = (long) hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		smp_call_function_single(cpu, __oprofile_hrtimer_start,
					 NULL, 1);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		__oprofile_hrtimer_stop(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata oprofile_cpu_notifier = {
	.notifier_call = oprofile_cpu_notify,
};

int __init oprofile_timer_init(struct oprofile_operations *ops)
{
	int rc;

	rc = register_hotcpu_notifier(&oprofile_cpu_notifier);
	if (rc)
		return rc;
	ops->create_files = NULL;
	ops->setup = NULL;
	ops->shutdown = NULL;
	ops->start = oprofile_hrtimer_start;
	ops->stop = oprofile_hrtimer_stop;
	ops->cpu_type = "timer";
	return 0;
}

void __exit oprofile_timer_exit(void)
{
	unregister_hotcpu_notifier(&oprofile_cpu_notifier);
}
