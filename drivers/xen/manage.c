/*
 * Handle extern requests for shutdown, reboot and sysrq
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/stop_machine.h>
#include <linux/freezer.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/events.h>
#include <xen/hvc-console.h>
#include <xen/xen-ops.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>
#include <asm/xen/hypervisor.h>

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
static int xen_hvm_suspend(void *data)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_suspend };
	int *cancelled = data;

	BUG_ON(!irqs_disabled());

	*cancelled = HYPERVISOR_sched_op(SCHEDOP_shutdown, &r);

	xen_hvm_post_suspend(*cancelled);
	gnttab_resume();

	if (!*cancelled) {
		xen_irq_resume();
		xen_console_resume();
		xen_timer_resume();
	}

	return 0;
}

static int xen_suspend(void *data)
{
	int err;
	int *cancelled = data;

	BUG_ON(!irqs_disabled());

	err = sysdev_suspend(PMSG_SUSPEND);
	if (err) {
		printk(KERN_ERR "xen_suspend: sysdev_suspend failed: %d\n",
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

	if (!*cancelled) {
		xen_irq_resume();
		xen_console_resume();
		xen_timer_resume();
	}

	sysdev_resume();

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
		goto out;
	}
#endif

	err = dpm_suspend_start(PMSG_SUSPEND);
	if (err) {
		printk(KERN_ERR "xen suspend: dpm_suspend_start %d\n", err);
		goto out_thaw;
	}

	printk(KERN_DEBUG "suspending xenstore...\n");
	xs_suspend();

	err = dpm_suspend_noirq(PMSG_SUSPEND);
	if (err) {
		printk(KERN_ERR "dpm_suspend_noirq failed: %d\n", err);
		goto out_resume;
	}

	if (xen_hvm_domain())
		err = stop_machine(xen_hvm_suspend, &cancelled, cpumask_of(0));
	else
		err = stop_machine(xen_suspend, &cancelled, cpumask_of(0));

	dpm_resume_noirq(PMSG_RESUME);

	if (err) {
		printk(KERN_ERR "failed to start xen_suspend: %d\n", err);
		cancelled = 1;
	}

out_resume:
	if (!cancelled) {
		xen_arch_resume();
		xs_resume();
	} else
		xs_suspend_cancel();

	dpm_resume_end(PMSG_RESUME);

	/* Make sure timer events get retriggered on all CPUs */
	clock_was_set();

out_thaw:
#ifdef CONFIG_PREEMPT
	thaw_processes();
out:
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

#ifdef CONFIG_MAGIC_SYSRQ
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
		handle_sysrq(sysrq_key);
}

static struct xenbus_watch sysrq_watch = {
	.node = "control/sysrq",
	.callback = sysrq_handler
};
#endif

static struct xenbus_watch shutdown_watch = {
	.node = "control/shutdown",
	.callback = shutdown_handler
};

static int setup_shutdown_watcher(void)
{
	int err;

	err = register_xenbus_watch(&shutdown_watch);
	if (err) {
		printk(KERN_ERR "Failed to set shutdown watcher\n");
		return err;
	}

#ifdef CONFIG_MAGIC_SYSRQ
	err = register_xenbus_watch(&sysrq_watch);
	if (err) {
		printk(KERN_ERR "Failed to set sysrq watcher\n");
		return err;
	}
#endif

	return 0;
}

static int shutdown_event(struct notifier_block *notifier,
			  unsigned long event,
			  void *data)
{
	setup_shutdown_watcher();
	return NOTIFY_DONE;
}

static int __init __setup_shutdown_event(void)
{
	/* Delay initialization in the PV on HVM case */
	if (xen_hvm_domain())
		return 0;

	if (!xen_pv_domain())
		return -ENODEV;

	return xen_setup_shutdown_event();
}

int xen_setup_shutdown_event(void)
{
	static struct notifier_block xenstore_notifier = {
		.notifier_call = shutdown_event
	};
	register_xenstore_notifier(&xenstore_notifier);

	return 0;
}
EXPORT_SYMBOL_GPL(xen_setup_shutdown_event);

subsys_initcall(__setup_shutdown_event);
