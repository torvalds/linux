/*
 *  syscore.c - Execution of system core operations.
 *
 *  Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 *  This file is released under the GPLv2.
 */

#include <linux/syscore_ops.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <trace/events/power.h>
#include <linux/wakeup_reason.h>

static LIST_HEAD(syscore_ops_list);
static DEFINE_MUTEX(syscore_ops_lock);

/**
 * register_syscore_ops - Register a set of system core operations.
 * @ops: System core operations to register.
 */
void register_syscore_ops(struct syscore_ops *ops)
{
	mutex_lock(&syscore_ops_lock);
	list_add_tail(&ops->node, &syscore_ops_list);
	mutex_unlock(&syscore_ops_lock);
}
EXPORT_SYMBOL_GPL(register_syscore_ops);

/**
 * unregister_syscore_ops - Unregister a set of system core operations.
 * @ops: System core operations to unregister.
 */
void unregister_syscore_ops(struct syscore_ops *ops)
{
	mutex_lock(&syscore_ops_lock);
	list_del(&ops->node);
	mutex_unlock(&syscore_ops_lock);
}
EXPORT_SYMBOL_GPL(unregister_syscore_ops);

#ifdef CONFIG_PM_SLEEP
/**
 * syscore_suspend - Execute all the registered system core suspend callbacks.
 *
 * This function is executed with one CPU on-line and disabled interrupts.
 */
int syscore_suspend(void)
{
	struct syscore_ops *ops;
	int ret = 0;

	trace_suspend_resume(TPS("syscore_suspend"), 0, true);
	pr_debug("Checking wakeup interrupts\n");

	/* Return error code if there are any wakeup interrupts pending. */
	if (pm_wakeup_pending())
		return -EBUSY;

	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled before system core suspend.\n");

	list_for_each_entry_reverse(ops, &syscore_ops_list, node)
		if (ops->suspend) {
			if (initcall_debug)
				pr_info("PM: Calling %pF\n", ops->suspend);
			ret = ops->suspend();
			if (ret)
				goto err_out;
			WARN_ONCE(!irqs_disabled(),
				"Interrupts enabled after %pF\n", ops->suspend);
		}

	trace_suspend_resume(TPS("syscore_suspend"), 0, false);
	return 0;

 err_out:
	log_suspend_abort_reason("System core suspend callback %pF failed",
		ops->suspend);
	pr_err("PM: System core suspend callback %pF failed.\n", ops->suspend);

	list_for_each_entry_continue(ops, &syscore_ops_list, node)
		if (ops->resume)
			ops->resume();

	return ret;
}
EXPORT_SYMBOL_GPL(syscore_suspend);

/**
 * syscore_resume - Execute all the registered system core resume callbacks.
 *
 * This function is executed with one CPU on-line and disabled interrupts.
 */
void syscore_resume(void)
{
	struct syscore_ops *ops;

	trace_suspend_resume(TPS("syscore_resume"), 0, true);
	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled before system core resume.\n");

	list_for_each_entry(ops, &syscore_ops_list, node)
		if (ops->resume) {
			if (initcall_debug)
				pr_info("PM: Calling %pF\n", ops->resume);
			ops->resume();
			WARN_ONCE(!irqs_disabled(),
				"Interrupts enabled after %pF\n", ops->resume);
		}
	trace_suspend_resume(TPS("syscore_resume"), 0, false);
}
EXPORT_SYMBOL_GPL(syscore_resume);
#endif /* CONFIG_PM_SLEEP */

/**
 * syscore_shutdown - Execute all the registered system core shutdown callbacks.
 */
void syscore_shutdown(void)
{
	struct syscore_ops *ops;

	mutex_lock(&syscore_ops_lock);

	list_for_each_entry_reverse(ops, &syscore_ops_list, node)
		if (ops->shutdown) {
			if (initcall_debug)
				pr_info("PM: Calling %pF\n", ops->shutdown);
			ops->shutdown();
		}

	mutex_unlock(&syscore_ops_lock);
}
