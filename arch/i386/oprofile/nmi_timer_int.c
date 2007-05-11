/**
 * @file nmi_timer_int.c
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo <zwane@linuxpower.ca>
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/oprofile.h>
#include <linux/rcupdate.h>
#include <linux/kdebug.h>

#include <asm/nmi.h>
#include <asm/apic.h>
#include <asm/ptrace.h>
 
static int profile_timer_exceptions_notify(struct notifier_block *self,
					   unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	switch(val) {
	case DIE_NMI:
		oprofile_add_sample(args->regs, 0);
		ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	return ret;
}

static struct notifier_block profile_timer_exceptions_nb = {
	.notifier_call = profile_timer_exceptions_notify,
	.next = NULL,
	.priority = 0
};

static int timer_start(void)
{
	if (register_die_notifier(&profile_timer_exceptions_nb))
		return 1;
	return 0;
}


static void timer_stop(void)
{
	unregister_die_notifier(&profile_timer_exceptions_nb);
	synchronize_sched();  /* Allow already-started NMIs to complete. */
}


int __init op_nmi_timer_init(struct oprofile_operations * ops)
{
	if ((nmi_watchdog != NMI_IO_APIC) || (atomic_read(&nmi_active) <= 0))
		return -ENODEV;

	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
	printk(KERN_INFO "oprofile: using NMI timer interrupt.\n");
	return 0;
}
