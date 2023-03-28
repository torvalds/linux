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

static char *cmd_buf;

static int diag288(unsigned int func, unsigned int timeout,
		   unsigned long action, unsigned int len)
{
	union register_pair r1 = { .even = func, .odd = timeout, };
	union register_pair r3 = { .even = action, .odd = len, };
	int err;

	diag_stat_inc(DIAG_STAT_X288);

	err = -EINVAL;
	asm volatile(
		"	diag	%[r1],%[r3],0x288\n"
		"0:	la	%[err],0\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [err] "+d" (err)
		: [r1] "d" (r1.pair), [r3] "d" (r3.pair)
		: "cc", "memory");
	return err;
}

static int diag288_str(unsigned int func, unsigned int timeout, char *cmd)
{
	ssize_t len;

	len = strscpy(cmd_buf, cmd, MAX_CMDLEN);
	if (len < 0)
		return len;
	ASCEBC(cmd_buf, MAX_CMDLEN);
	EBC_TOUPPER(cmd_buf, MAX_CMDLEN);

	return diag288(func, timeout, virt_to_phys(cmd_buf), len);
}

static int wdt_start(struct watchdog_device *dev)
{
	int ret;
	unsigned int func;

	if (MACHINE_IS_VM) {
		func = conceal_on ? (WDT_FUNC_INIT | WDT_FUNC_CONCEAL)
			: WDT_FUNC_INIT;
		ret = diag288_str(func, dev->timeout, wdt_cmd);
		WARN_ON(ret != 0);
	} else {
		ret = diag288(WDT_FUNC_INIT, dev->timeout, LPARWDT_RESTART, 0);
	}

	if (ret) {
		pr_err("The watchdog cannot be activated\n");
		return ret;
	}
	return 0;
}

static int wdt_stop(struct watchdog_device *dev)
{
	return diag288(WDT_FUNC_CANCEL, 0, 0, 0);
}

static int wdt_ping(struct watchdog_device *dev)
{
	int ret;
	unsigned int func;

	if (MACHINE_IS_VM) {
		/*
		 * It seems to be ok to z/VM to use the init function to
		 * retrigger the watchdog. On LPAR WDT_FUNC_CHANGE must
		 * be used when the watchdog is running.
		 */
		func = conceal_on ? (WDT_FUNC_INIT | WDT_FUNC_CONCEAL)
			: WDT_FUNC_INIT;

		ret = diag288_str(func, dev->timeout, wdt_cmd);
		WARN_ON(ret != 0);
	} else {
		ret = diag288(WDT_FUNC_CHANGE, dev->timeout, 0, 0);
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

static int __init diag288_init(void)
{
	int ret;

	watchdog_set_nowayout(&wdt_dev, nowayout_info);

	if (MACHINE_IS_VM) {
		cmd_buf = kmalloc(MAX_CMDLEN, GFP_KERNEL);
		if (!cmd_buf) {
			pr_err("The watchdog cannot be initialized\n");
			return -ENOMEM;
		}

		ret = diag288_str(WDT_FUNC_INIT, MIN_INTERVAL, "BEGIN");
		if (ret != 0) {
			pr_err("The watchdog cannot be initialized\n");
			kfree(cmd_buf);
			return -EINVAL;
		}
	} else {
		if (diag288(WDT_FUNC_INIT, WDT_DEFAULT_TIMEOUT,
			    LPARWDT_RESTART, 0)) {
			pr_err("The watchdog cannot be initialized\n");
			return -EINVAL;
		}
	}

	if (diag288(WDT_FUNC_CANCEL, 0, 0, 0)) {
		pr_err("The watchdog cannot be deactivated\n");
		return -EINVAL;
	}

	return watchdog_register_device(&wdt_dev);
}

static void __exit diag288_exit(void)
{
	watchdog_unregister_device(&wdt_dev);
	kfree(cmd_buf);
}

module_init(diag288_init);
module_exit(diag288_exit);
