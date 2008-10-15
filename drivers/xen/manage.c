/*
 * Handle extern requests for shutdown, reboot and sysrq
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/stop_machine.h>
#include <linux/freezer.h>

#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/events.h>
#include <xen/hvc-console.h>
#include <xen/xen-ops.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>

enum shutdown_state {
	SHUTDOWN_INVALID = -1,
	SHUTDOWN_POWEROFF = 0,
	SHUTDOWN_SUSPEND = 2,
	/* Code 3 is SHUTDOWN_CRASH, which we don't use because the domain can only
	   report a crash, not be instructed to crash!
	   HALT is the same as POWEROFF, as far as we're concerned.  The tools use
	   the distinction when we return the reason code to them.  */
	 SHUTDOWN_HALT = 4,
};

/* Ignore multiple shutdown requests. */
static enum shutdown_state shutting_down = SHUTDOWN_INVALID;

#ifdef CONFIG_PM_SLEEP
static int xen_suspend(void *data)
{
	int *cancelled = data;
	int err;

	BUG_ON(!irqs_disabled());

	load_cr3(swapper_pg_dir);

	err = device_power_down(PMSG_SUSPEND);
	if (err) {
		printk(KERN_ERR "xen_suspend: device_power_down failed: %d\n",
		       err);
		return err;
	}

	xen_mm_pin_all();
	gnttab_suspend();
	xen_pre_suspend();

	/*
	 * This hypercall returns 1 if suspend was cancelled
	 * or the domain was merely checkpointed, and 0 if it
	 * is resuming in a new domain.
	 */
	*cancelled = HYPERVISOR_suspend(virt_to_mfn(xen_start_info));

	xen_post_suspend(*cancelled);
	gnttab_resume();
	xen_mm_unpin_all();

	device_power_up(PMSG_RESUME);

	if (!*cancelled) {
		xen_irq_resume();
		xen_console_resume();
		xen_timer_resume();
	}

	return 0;
}

static void do_suspend(void)
{
	int err;
	int cancelled = 1;

	shutting_down = SHUTDOWN_SUSPEND;

#ifdef CONFIG_PREEMPT
	/* If the kernel is preemptible, we need to freeze all the processes
	   to prevent them from being in the middle of a pagetable update
	   during suspend. */
	err = freeze_processes();
	if (err) {
		printk(KERN_ERR "xen suspend: freeze failed %d\n", err);
		return;
	}
#endif

	err = device_suspend(PMSG_SUSPEND);
	if (err) {
		printk(KERN_ERR "xen suspend: device_suspend %d\n", err);
		goto out;
	}

	printk("suspending xenbus...\n");
	/* XXX use normal device tree? */
	xenbus_suspend();

	err = stop_machine(xen_suspend, &cancelled, &cpumask_of_cpu(0));
	if (err) {
		printk(KERN_ERR "failed to start xen_suspend: %d\n", err);
		goto out;
	}

	if (!cancelled) {
		xen_arch_resume();
		xenbus_resume();
	} else
		xenbus_suspend_cancel();

	device_resume(PMSG_RESUME);

	/* Make sure timer events get retriggered on all CPUs */
	clock_was_set();
out:
#ifdef CONFIG_PREEMPT
	thaw_processes();
#endif
	shutting_down = SHUTDOWN_INVALID;
}
#endif	/* CONFIG_PM_SLEEP */

static void shutdown_handler(struct xenbus_watch *watch,
			     const char **vec, unsigned int len)
{
	char *str;
	struct xenbus_transaction xbt;
	int err;

	if (shutting_down != SHUTDOWN_INVALID)
		return;

 again:
	err = xenbus_transaction_start(&xbt);
	if (err)
		return;

	str = (char *)xenbus_read(xbt, "control", "shutdown", NULL);
	/* Ignore read errors and empty reads. */
	if (XENBUS_IS_ERR_READ(str)) {
		xenbus_transaction_end(xbt, 1);
		return;
	}

	xenbus_write(xbt, "control", "shutdown", "");

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN) {
		kfree(str);
		goto again;
	}

	if (strcmp(str, "poweroff") == 0 ||
	    strcmp(str, "halt") == 0) {
		shutting_down = SHUTDOWN_POWEROFF;
		orderly_poweroff(false);
	} else if (strcmp(str, "reboot") == 0) {
		shutting_down = SHUTDOWN_POWEROFF; /* ? */
		ctrl_alt_del();
#ifdef CONFIG_PM_SLEEP
	} else if (strcmp(str, "suspend") == 0) {
		do_suspend();
#endif
	} else {
		printk(KERN_INFO "Ignoring shutdown request: %s\n", str);
		shutting_down = SHUTDOWN_INVALID;
	}

	kfree(str);
}

static void sysrq_handler(struct xenbus_watch *watch, const char **vec,
			  unsigned int len)
{
	char sysrq_key = '\0';
	struct xenbus_transaction xbt;
	int err;

 again:
	err = xenbus_transaction_start(&xbt);
	if (err)
		return;
	if (!xenbus_scanf(xbt, "control", "sysrq", "%c", &sysrq_key)) {
		printk(KERN_ERR "Unable to read sysrq code in "
		       "control/sysrq\n");
		xenbus_transaction_end(xbt, 1);
		return;
	}

	if (sysrq_key != '\0')
		xenbus_printf(xbt, "control", "sysrq", "%c", '\0');

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;

	if (sysrq_key != '\0')
		handle_sysrq(sysrq_key, NULL);
}

static struct xenbus_watch shutdown_watch = {
	.node = "control/shutdown",
	.callback = shutdown_handler
};

static struct xenbus_watch sysrq_watch = {
	.node = "control/sysrq",
	.callback = sysrq_handler
};

static int setup_shutdown_watcher(void)
{
	int err;

	err = register_xenbus_watch(&shutdown_watch);
	if (err) {
		printk(KERN_ERR "Failed to set shutdown watcher\n");
		return err;
	}

	err = register_xenbus_watch(&sysrq_watch);
	if (err) {
		printk(KERN_ERR "Failed to set sysrq watcher\n");
		return err;
	}

	return 0;
}

static int shutdown_event(struct notifier_block *notifier,
			  unsigned long event,
			  void *data)
{
	setup_shutdown_watcher();
	return NOTIFY_DONE;
}

static int __init setup_shutdown_event(void)
{
	static struct notifier_block xenstore_notifier = {
		.notifier_call = shutdown_event
	};
	register_xenstore_notifier(&xenstore_notifier);

	return 0;
}

subsys_initcall(setup_shutdown_event);
