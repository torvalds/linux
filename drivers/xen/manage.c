/*
 * Handle extern requests for shutdown, reboot and sysrq
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/stop_machine.h>
#include <linux/freezer.h>
#include <linux/syscore_ops.h>
#include <linux/export.h>

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

struct suspend_info {
	int cancelled;
};

static RAW_NOTIFIER_HEAD(xen_resume_notifier);

void xen_resume_notifier_register(struct notifier_block *nb)
{
	raw_notifier_chain_register(&xen_resume_notifier, nb);
}
EXPORT_SYMBOL_GPL(xen_resume_notifier_register);

void xen_resume_notifier_unregister(struct notifier_block *nb)
{
	raw_notifier_chain_unregister(&xen_resume_notifier, nb);
}
EXPORT_SYMBOL_GPL(xen_resume_notifier_unregister);

#ifdef CONFIG_HIBERNATE_CALLBACKS
static int xen_suspend(void *data)
{
	struct suspend_info *si = data;
	int err;

	BUG_ON(!irqs_disabled());

	err = syscore_suspend();
	if (err) {
		pr_err("%s: system core suspend failed: %d\n", __func__, err);
		return err;
	}

	gnttab_suspend();
	xen_arch_pre_suspend();

	/*
	 * This hypercall returns 1 if suspend was cancelled
	 * or the domain was merely checkpointed, and 0 if it
	 * is resuming in a new domain.
	 */
	si->cancelled = HYPERVISOR_suspend(xen_pv_domain()
                                           ? virt_to_mfn(xen_start_info)
                                           : 0);

	xen_arch_post_suspend(si->cancelled);
	gnttab_resume();

	if (!si->cancelled) {
		xen_irq_resume();
		xen_timer_resume();
	}

	syscore_resume();

	return 0;
}

static void do_suspend(void)
{
	int err;
	struct suspend_info si;

	shutting_down = SHUTDOWN_SUSPEND;

	err = freeze_processes();
	if (err) {
		pr_err("%s: freeze failed %d\n", __func__, err);
		goto out;
	}

	err = dpm_suspend_start(PMSG_FREEZE);
	if (err) {
		pr_err("%s: dpm_suspend_start %d\n", __func__, err);
		goto out_thaw;
	}

	printk(KERN_DEBUG "suspending xenstore...\n");
	xs_suspend();

	err = dpm_suspend_end(PMSG_FREEZE);
	if (err) {
		pr_err("dpm_suspend_end failed: %d\n", err);
		si.cancelled = 0;
		goto out_resume;
	}

	si.cancelled = 1;

	err = stop_machine(xen_suspend, &si, cpumask_of(0));

	/* Resume console as early as possible. */
	if (!si.cancelled)
		xen_console_resume();

	raw_notifier_call_chain(&xen_resume_notifier, 0, NULL);

	dpm_resume_start(si.cancelled ? PMSG_THAW : PMSG_RESTORE);

	if (err) {
		pr_err("failed to start xen_suspend: %d\n", err);
		si.cancelled = 1;
	}

out_resume:
	if (!si.cancelled) {
		xen_arch_resume();
		xs_resume();
	} else
		xs_suspend_cancel();

	dpm_resume_end(si.cancelled ? PMSG_THAW : PMSG_RESTORE);

out_thaw:
	thaw_processes();
out:
	shutting_down = SHUTDOWN_INVALID;
}
#endif	/* CONFIG_HIBERNATE_CALLBACKS */

struct shutdown_handler {
	const char *command;
	void (*cb)(void);
};

static int poweroff_nb(struct notifier_block *cb, unsigned long code, void *unused)
{
	switch (code) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		shutting_down = SHUTDOWN_POWEROFF;
	default:
		break;
	}
	return NOTIFY_DONE;
}
static void do_poweroff(void)
{
	switch (system_state) {
	case SYSTEM_BOOTING:
		orderly_poweroff(true);
		break;
	case SYSTEM_RUNNING:
		orderly_poweroff(false);
		break;
	default:
		/* Don't do it when we are halting/rebooting. */
		pr_info("Ignoring Xen toolstack shutdown.\n");
		break;
	}
}

static void do_reboot(void)
{
	shutting_down = SHUTDOWN_POWEROFF; /* ? */
	ctrl_alt_del();
}

static void shutdown_handler(struct xenbus_watch *watch,
			     const char **vec, unsigned int len)
{
	char *str;
	struct xenbus_transaction xbt;
	int err;
	static struct shutdown_handler handlers[] = {
		{ "poweroff",	do_poweroff },
		{ "halt",	do_poweroff },
		{ "reboot",	do_reboot   },
#ifdef CONFIG_HIBERNATE_CALLBACKS
		{ "suspend",	do_suspend  },
#endif
		{NULL, NULL},
	};
	static struct shutdown_handler *handler;

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

	for (handler = &handlers[0]; handler->command; handler++) {
		if (strcmp(str, handler->command) == 0)
			break;
	}

	/* Only acknowledge commands which we are prepared to handle. */
	if (handler->cb)
		xenbus_write(xbt, "control", "shutdown", "");

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN) {
		kfree(str);
		goto again;
	}

	if (handler->cb) {
		handler->cb();
	} else {
		pr_info("Ignoring shutdown request: %s\n", str);
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
		pr_err("Unable to read sysrq code in control/sysrq\n");
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

static struct notifier_block xen_reboot_nb = {
	.notifier_call = poweroff_nb,
};

static int setup_shutdown_watcher(void)
{
	int err;

	err = register_xenbus_watch(&shutdown_watch);
	if (err) {
		pr_err("Failed to set shutdown watcher\n");
		return err;
	}


#ifdef CONFIG_MAGIC_SYSRQ
	err = register_xenbus_watch(&sysrq_watch);
	if (err) {
		pr_err("Failed to set sysrq watcher\n");
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

int xen_setup_shutdown_event(void)
{
	static struct notifier_block xenstore_notifier = {
		.notifier_call = shutdown_event
	};

	if (!xen_domain())
		return -ENODEV;
	register_xenstore_notifier(&xenstore_notifier);
	register_reboot_notifier(&xen_reboot_nb);

	return 0;
}
EXPORT_SYMBOL_GPL(xen_setup_shutdown_event);

subsys_initcall(xen_setup_shutdown_event);
