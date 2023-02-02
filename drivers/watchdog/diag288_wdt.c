// SPDX-License-Identifier: GPL-2.0-only
/*
 * Watchdog driver for z/VM and LPAR using the diag 288 interface.
 *
 * Under z/VM, expiration of the watchdog will send a "system restart" command
 * to CP.
 *
 * The command can be altered using the module parameter "cmd". This is
 * not recommended because it's only supported on z/VM but not whith LPAR.
 *
 * On LPAR, the watchdog will always trigger a system restart. the module
 * paramter cmd is meaningless here.
 *
 *
 * Copyright IBM Corp. 2004, 2013
 * Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *	      Philipp Hachtmann (phacht@de.ibm.com)
 *
 */

#define KMSG_COMPONENT "diag288_wdt"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/watchdog.h>
#include <linux/suspend.h>
#include <asm/ebcdic.h>
#include <asm/diag.h>
#include <linux/io.h>

#define MAX_CMDLEN 240
#define DEFAULT_CMD "SYSTEM RESTART"

#define MIN_INTERVAL 15     /* Minimal time supported by diag88 */
#define MAX_INTERVAL 3600   /* One hour should be enough - pure estimation */

#define WDT_DEFAULT_TIMEOUT 30

/* Function codes - init, change, cancel */
#define WDT_FUNC_INIT 0
#define WDT_FUNC_CHANGE 1
#define WDT_FUNC_CANCEL 2
#define WDT_FUNC_CONCEAL 0x80000000

/* Action codes for LPAR watchdog */
#define LPARWDT_RESTART 0

static char wdt_cmd[MAX_CMDLEN] = DEFAULT_CMD;
static bool conceal_on;
static bool nowayout_info = WATCHDOG_NOWAYOUT;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
MODULE_AUTHOR("Philipp Hachtmann <phacht@de.ibm.com>");

MODULE_DESCRIPTION("System z diag288  Watchdog Timer");

module_param_string(cmd, wdt_cmd, MAX_CMDLEN, 0644);
MODULE_PARM_DESC(cmd, "CP command that is run when the watchdog triggers (z/VM only)");

module_param_named(conceal, conceal_on, bool, 0644);
MODULE_PARM_DESC(conceal, "Enable the CONCEAL CP option while the watchdog is active (z/VM only)");

module_param_named(nowayout, nowayout_info, bool, 0444);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default = CONFIG_WATCHDOG_NOWAYOUT)");

MODULE_ALIAS("vmwatchdog");

static int __diag288(unsigned int func, unsigned int timeout,
		     unsigned long action, unsigned int len)
{
	register unsigned long __func asm("2") = func;
	register unsigned long __timeout asm("3") = timeout;
	register unsigned long __action asm("4") = action;
	register unsigned long __len asm("5") = len;
	int err;

	err = -EINVAL;
	asm volatile(
		"	diag	%1, %3, 0x288\n"
		"0:	la	%0, 0\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (err) : "d"(__func), "d"(__timeout),
		  "d"(__action), "d"(__len) : "1", "cc", "memory");
	return err;
}

static int __diag288_vm(unsigned int  func, unsigned int timeout,
			char *cmd, size_t len)
{
	diag_stat_inc(DIAG_STAT_X288);
	return __diag288(func, timeout, virt_to_phys(cmd), len);
}

static int __diag288_lpar(unsigned int func, unsigned int timeout,
			  unsigned long action)
{
	diag_stat_inc(DIAG_STAT_X288);
	return __diag288(func, timeout, action, 0);
}

static unsigned long wdt_status;

#define DIAG_WDOG_BUSY	0

static int wdt_start(struct watchdog_device *dev)
{
	char *ebc_cmd;
	size_t len;
	int ret;
	unsigned int func;

	if (test_and_set_bit(DIAG_WDOG_BUSY, &wdt_status))
		return -EBUSY;

	if (MACHINE_IS_VM) {
		ebc_cmd = kmalloc(MAX_CMDLEN, GFP_KERNEL);
		if (!ebc_cmd) {
			clear_bit(DIAG_WDOG_BUSY, &wdt_status);
			return -ENOMEM;
		}
		len = strlcpy(ebc_cmd, wdt_cmd, MAX_CMDLEN);
		ASCEBC(ebc_cmd, MAX_CMDLEN);
		EBC_TOUPPER(ebc_cmd, MAX_CMDLEN);

		func = conceal_on ? (WDT_FUNC_INIT | WDT_FUNC_CONCEAL)
			: WDT_FUNC_INIT;
		ret = __diag288_vm(func, dev->timeout, ebc_cmd, len);
		WARN_ON(ret != 0);
		kfree(ebc_cmd);
	} else {
		ret = __diag288_lpar(WDT_FUNC_INIT,
				     dev->timeout, LPARWDT_RESTART);
	}

	if (ret) {
		pr_err("The watchdog cannot be activated\n");
		clear_bit(DIAG_WDOG_BUSY, &wdt_status);
		return ret;
	}
	return 0;
}

static int wdt_stop(struct watchdog_device *dev)
{
	int ret;

	diag_stat_inc(DIAG_STAT_X288);
	ret = __diag288(WDT_FUNC_CANCEL, 0, 0, 0);

	clear_bit(DIAG_WDOG_BUSY, &wdt_status);

	return ret;
}

static int wdt_ping(struct watchdog_device *dev)
{
	char *ebc_cmd;
	size_t len;
	int ret;
	unsigned int func;

	if (MACHINE_IS_VM) {
		ebc_cmd = kmalloc(MAX_CMDLEN, GFP_KERNEL);
		if (!ebc_cmd)
			return -ENOMEM;
		len = strlcpy(ebc_cmd, wdt_cmd, MAX_CMDLEN);
		ASCEBC(ebc_cmd, MAX_CMDLEN);
		EBC_TOUPPER(ebc_cmd, MAX_CMDLEN);

		/*
		 * It seems to be ok to z/VM to use the init function to
		 * retrigger the watchdog. On LPAR WDT_FUNC_CHANGE must
		 * be used when the watchdog is running.
		 */
		func = conceal_on ? (WDT_FUNC_INIT | WDT_FUNC_CONCEAL)
			: WDT_FUNC_INIT;

		ret = __diag288_vm(func, dev->timeout, ebc_cmd, len);
		WARN_ON(ret != 0);
		kfree(ebc_cmd);
	} else {
		ret = __diag288_lpar(WDT_FUNC_CHANGE, dev->timeout, 0);
	}

	if (ret)
		pr_err("The watchdog timer cannot be started or reset\n");
	return ret;
}

static int wdt_set_timeout(struct watchdog_device * dev, unsigned int new_to)
{
	dev->timeout = new_to;
	return wdt_ping(dev);
}

static const struct watchdog_ops wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdt_start,
	.stop = wdt_stop,
	.ping = wdt_ping,
	.set_timeout = wdt_set_timeout,
};

static const struct watchdog_info wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.firmware_version = 0,
	.identity = "z Watchdog",
};

static struct watchdog_device wdt_dev = {
	.parent = NULL,
	.info = &wdt_info,
	.ops = &wdt_ops,
	.bootstatus = 0,
	.timeout = WDT_DEFAULT_TIMEOUT,
	.min_timeout = MIN_INTERVAL,
	.max_timeout = MAX_INTERVAL,
};

/*
 * It makes no sense to go into suspend while the watchdog is running.
 * Depending on the memory size, the watchdog might trigger, while we
 * are still saving the memory.
 */
static int wdt_suspend(void)
{
	if (test_and_set_bit(DIAG_WDOG_BUSY, &wdt_status)) {
		pr_err("Linux cannot be suspended while the watchdog is in use\n");
		return notifier_from_errno(-EBUSY);
	}
	return NOTIFY_DONE;
}

static int wdt_resume(void)
{
	clear_bit(DIAG_WDOG_BUSY, &wdt_status);
	return NOTIFY_DONE;
}

static int wdt_power_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	switch (event) {
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		return wdt_resume();
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		return wdt_suspend();
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block wdt_power_notifier = {
	.notifier_call = wdt_power_event,
};

static int __init diag288_init(void)
{
	int ret;
	char ebc_begin[] = {
		194, 197, 199, 201, 213
	};
	char *ebc_cmd;

	watchdog_set_nowayout(&wdt_dev, nowayout_info);

	if (MACHINE_IS_VM) {
		ebc_cmd = kmalloc(sizeof(ebc_begin), GFP_KERNEL);
		if (!ebc_cmd) {
			pr_err("The watchdog cannot be initialized\n");
			return -ENOMEM;
		}
		memcpy(ebc_cmd, ebc_begin, sizeof(ebc_begin));
		ret = __diag288_vm(WDT_FUNC_INIT, 15,
				   ebc_cmd, sizeof(ebc_begin));
		kfree(ebc_cmd);
		if (ret != 0) {
			pr_err("The watchdog cannot be initialized\n");
			return -EINVAL;
		}
	} else {
		if (__diag288_lpar(WDT_FUNC_INIT, 30, LPARWDT_RESTART)) {
			pr_err("The watchdog cannot be initialized\n");
			return -EINVAL;
		}
	}

	if (__diag288_lpar(WDT_FUNC_CANCEL, 0, 0)) {
		pr_err("The watchdog cannot be deactivated\n");
		return -EINVAL;
	}

	ret = register_pm_notifier(&wdt_power_notifier);
	if (ret)
		return ret;

	ret = watchdog_register_device(&wdt_dev);
	if (ret)
		unregister_pm_notifier(&wdt_power_notifier);

	return ret;
}

static void __exit diag288_exit(void)
{
	watchdog_unregister_device(&wdt_dev);
	unregister_pm_notifier(&wdt_power_notifier);
}

module_init(diag288_init);
module_exit(diag288_exit);
