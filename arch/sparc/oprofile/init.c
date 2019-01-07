/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/param.h>	/* for HZ */
 
#ifdef CONFIG_SPARC64
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/kdebug.h>
#include <asm/nmi.h>

static int profile_timer_exceptions_notify(struct notifier_block *self,
					   unsigned long val, void *data)
{
	struct die_args *args = data;
	int ret = NOTIFY_DONE;

	switch (val) {
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
	.notifier_call	= profile_timer_exceptions_notify,
};

static int timer_start(void)
{
	if (register_die_notifier(&profile_timer_exceptions_nb))
		return 1;
	nmi_adjust_hz(HZ);
	return 0;
}


static void timer_stop(void)
{
	nmi_adjust_hz(1);
	unregister_die_notifier(&profile_timer_exceptions_nb);
	synchronize_rcu();  /* Allow already-started NMIs to complete. */
}

static int op_nmi_timer_init(struct oprofile_operations *ops)
{
	if (atomic_read(&nmi_active) <= 0)
		return -ENODEV;

	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
	printk(KERN_INFO "oprofile: Using perfctr NMI timer interrupt.\n");
	return 0;
}
#endif

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	int ret = -ENODEV;

#ifdef CONFIG_SPARC64
	ret = op_nmi_timer_init(ops);
	if (!ret)
		return ret;
#endif

	return ret;
}

void oprofile_arch_exit(void)
{
}
