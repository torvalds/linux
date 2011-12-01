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

static int profile_timer_exceptions_notify(unsigned int val, struct pt_regs *regs)
{
	oprofile_add_sample(regs, 0);
	return NMI_HANDLED;
}

static int timer_start(void)
{
	if (register_nmi_handler(NMI_LOCAL, profile_timer_exceptions_notify,
					0, "oprofile-timer"))
		return 1;
	return 0;
}


static void timer_stop(void)
{
	unregister_nmi_handler(NMI_LOCAL, "oprofile-timer");
	synchronize_sched();  /* Allow already-started NMIs to complete. */
}


int __init op_nmi_timer_init(struct oprofile_operations *ops)
{
	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
	printk(KERN_INFO "oprofile: using NMI timer interrupt.\n");
	return 0;
}
