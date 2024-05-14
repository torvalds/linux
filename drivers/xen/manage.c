// SPDX-License-Identifier: GPL-2.0-only
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
#include <xen/page.h>
#include <xen/xen-ops.h>

#include <asm/xen/hypercall.h>
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
	xen_manage_runstate_time(-1);
	xen_arch_pre_suspend();

	si->cancelled = HYPERVISOR_suspend(xen_pv_domain()
                                           ? virt_to_gfn(xen_start_info)
                                           : 0);

	xen_arch_post_suspend(si->cancelled);
	xen_manage_runstate_time(si->cancelled ? 1 : 0);
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
		pr_err("%s: freeze processes failed %d\n", __func__, err);
		goto out;
	}

	err = freeze_kernel_threads();
	if (err) {
		pr_err("%s: freeze kernel threads failed %d\n", __func__, err);
		goto out_thaw;
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

	xen_arch_suspend();

	si.cancelled = 1;

	err = stop_machine(xen_suspend, &si, cpumask_of(0));

	/* Resume console as early as possible. */
	if (!si.cancelled)
		xen_console_resume();

	raw_notifier_call_chain(&xen_resume_notifier, 0, NULL);

	xen_arch_resume();

	dpm_resume_start(si.cancelled ? PMSG_THAW : PMSG_RESTORE);

	if (err) {
		pr_err("failed to start xen_suspend: %d\n", err);
		si.cancelled = 1;
	}

out_resume:
	if (!si.cancelled)
		xs_resume();
	else
		xs_suspend_cancel();

	dpm_resume_end(si.cancelled ? PMSG_THAW : PMSG_RESTORE);

out_thaw:
	thaw_processes();
out:
	shutting_down = SHUTDOWN_INVALID;
}
#endif	/* CONFIG_HIBERNATE_CALLBACKS */

struct shutdown_handler {
#define SHUTDOWN_CMD_SIZE 11
	const char command[SHUTDOWN_CMD_SIZE];
	bool flag;
	void (*cb)(void);
};

static int poweroff_nb(struct notifier_block *cb, unsigned long code, void *unused)
{
	switch (code) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		shutting_down = SHUTDOWN_POWEROFF;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
static void do_poweroff(void)
{
	switch (system_state) {
	case SYSTEM_BOOTING:
	case SYSTEM_SCHEDULING:
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
	orderly_reboot();
}

static struct shutdown_handler shutdown_handlers[] = {
	{ "poweroff",	true,	do_poweroff },
	{ "halt",	false,	do_poweroff },
	{ "reboot",	true,	do_reboot   },
#ifdef CONFIG_HIBERNATE_CALLBACKS
	{ "suspend",	true,	do_suspend  },
#endif
};

static void shutdown_handler(struct xenbus_watch *watch,
			     const char *path, const char *token)
{
	char *str;
	struct xenbus_transaction xbt;
	int err;
	int idx;

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

	for (idx = 0; idx < ARRAY_SIZE(shutdown_handlers); idx++) {
		if (strcmp(str, shutdown_handlers[idx].command) == 0)
			break;
	}

	/* Only acknowledge commands which we are prepared to handle. */
	if (idx < ARRAY_SIZE(shutdown_handlers))
		xenbus_write(xbt, "control", "shutdown", "");

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN) {
		kfree(str);
		goto again;
	}

	if (idx < ARRAY_SIZE(shutdown_handlers)) {
		shutdown_handlers[idx].cb();
	} else {
		pr_info("Ignoring shutdown request: %s\n", str);
		shutting_down = SHUTDOWN_INVALID;
	}

	kfree(str);
}

#ifdef CONFIG_MAGIC_SYSRQ
static void sysrq_handler(struct xenbus_watch *watch, const char *path,
			  const char *token)
{
	char sysrq_key = '\0';
	struct xenbus_transaction xbt;
	int err;

 again:
	err = xenbus_transaction_start(&xbt);
	if (err)
		return;
	err = xenbus_scanf(xbt, "control", "sysrq", "%c", &sysrq_key);
	if (err < 0) {
		/*
		 * The Xenstore watch fires directly after registering it and
		 * after a suspend/resume cycle. So ENOENT is no error but
		 * might happen in those cases. ERANGE is observed when we get
		 * an empty value (''), this happens when we acknowledge the
		 * request by writing '\0' below.
		 */
		if (err != -ENOENT && err != -ERANGE)
			pr_err("Error %d reading sysrq code in control/sysrq\n",
			       err);
		xenbus_transaction_end(xbt, 1);
		return;
	}

	if (sysrq_key != '\0') {
		err = xenbus_printf(xbt, "control", "sysrq", "%c", '\0');
		if (err) {
			pr_err("%s: Error %d writing sysrq in control/sysrq\n",
			       __func__, err);
			xenbus_transaction_end(xbt, 1);
			return;
		}
	}

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
	int idx;
#define FEATURE_PATH_SIZE (SHUTDOWN_CMD_SIZE + sizeof("feature-"))
	char node[FEATURE_PATH_SIZE];

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

	for (idx = 0; idx < ARRAY_SIZE(shutdown_handlers); idx++) {
		if (!shutdown_handlers[idx].flag)
			continue;
		snprintf(node, FEATURE_PATH_SIZE, "feature-%s",
			 shutdown_handlers[idx].command);
		err = xenbus_printf(XBT_NIL, "control", node, "%u", 1);
		if (err) {
			pr_err("%s: Error %d writing %s\n", __func__,
				err, node);
			return err;
		}
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
