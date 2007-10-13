/*
 * Handle extern requests for shutdown, reboot and sysrq
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>

#include <xen/xenbus.h>

#define SHUTDOWN_INVALID  -1
#define SHUTDOWN_POWEROFF  0
#define SHUTDOWN_SUSPEND   2
/* Code 3 is SHUTDOWN_CRASH, which we don't use because the domain can only
 * report a crash, not be instructed to crash!
 * HALT is the same as POWEROFF, as far as we're concerned.  The tools use
 * the distinction when we return the reason code to them.
 */
#define SHUTDOWN_HALT      4

/* Ignore multiple shutdown requests. */
static int shutting_down = SHUTDOWN_INVALID;

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
	    strcmp(str, "halt") == 0)
		orderly_poweroff(false);
	else if (strcmp(str, "reboot") == 0)
		ctrl_alt_del();
	else {
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
