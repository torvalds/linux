// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022 International Business Machines, Inc.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/time64.h>
#include <linux/watchdog.h>

#define DRV_NAME "pseries-wdt"

/*
 * H_WATCHDOG Input
 *
 * R4: "flags":
 *
 *         Bits 48-55: "operation"
 */
#define PSERIES_WDTF_OP_START	0x100UL		/* start timer */
#define PSERIES_WDTF_OP_STOP	0x200UL		/* stop timer */
#define PSERIES_WDTF_OP_QUERY	0x300UL		/* query timer capabilities */

/*
 *         Bits 56-63: "timeoutAction" (for "Start Watchdog" only)
 */
#define PSERIES_WDTF_ACTION_HARD_POWEROFF	0x1UL	/* poweroff */
#define PSERIES_WDTF_ACTION_HARD_RESTART	0x2UL	/* restart */
#define PSERIES_WDTF_ACTION_DUMP_RESTART	0x3UL	/* dump + restart */

/*
 * H_WATCHDOG Output
 *
 * R3: Return code
 *
 *     H_SUCCESS    The operation completed.
 *
 *     H_BUSY	    The hypervisor is too busy; retry the operation.
 *
 *     H_PARAMETER  The given "flags" are somehow invalid.  Either the
 *                  "operation" or "timeoutAction" is invalid, or a
 *                  reserved bit is set.
 *
 *     H_P2         The given "watchdogNumber" is zero or exceeds the
 *                  supported maximum value.
 *
 *     H_P3         The given "timeoutInMs" is below the supported
 *                  minimum value.
 *
 *     H_NOOP       The given "watchdogNumber" is already stopped.
 *
 *     H_HARDWARE   The operation failed for ineffable reasons.
 *
 *     H_FUNCTION   The H_WATCHDOG hypercall is not supported by this
 *                  hypervisor.
 *
 * R4:
 *
 * - For the "Query Watchdog Capabilities" operation, a 64-bit
 *   structure:
 */
#define PSERIES_WDTQ_MIN_TIMEOUT(cap)	(((cap) >> 48) & 0xffff)
#define PSERIES_WDTQ_MAX_NUMBER(cap)	(((cap) >> 32) & 0xffff)

static const unsigned long pseries_wdt_action[] = {
	[0] = PSERIES_WDTF_ACTION_HARD_POWEROFF,
	[1] = PSERIES_WDTF_ACTION_HARD_RESTART,
	[2] = PSERIES_WDTF_ACTION_DUMP_RESTART,
};

#define WATCHDOG_ACTION 1
static unsigned int action = WATCHDOG_ACTION;
module_param(action, uint, 0444);
MODULE_PARM_DESC(action, "Action taken when watchdog expires (default="
		 __MODULE_STRING(WATCHDOG_ACTION) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0444);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define WATCHDOG_TIMEOUT 60
static unsigned int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, uint, 0444);
MODULE_PARM_DESC(timeout, "Initial watchdog timeout in seconds (default="
		 __MODULE_STRING(WATCHDOG_TIMEOUT) ")");

struct pseries_wdt {
	struct watchdog_device wd;
	unsigned long action;
	unsigned long num;		/* Watchdog numbers are 1-based */
};

static int pseries_wdt_start(struct watchdog_device *wdd)
{
	struct pseries_wdt *pw = watchdog_get_drvdata(wdd);
	struct device *dev = wdd->parent;
	unsigned long flags, msecs;
	long rc;

	flags = pw->action | PSERIES_WDTF_OP_START;
	msecs = wdd->timeout * MSEC_PER_SEC;
	rc = plpar_hcall_norets(H_WATCHDOG, flags, pw->num, msecs);
	if (rc != H_SUCCESS) {
		dev_crit(dev, "H_WATCHDOG: %ld: failed to start timer %lu",
			 rc, pw->num);
		return -EIO;
	}
	return 0;
}

static int pseries_wdt_stop(struct watchdog_device *wdd)
{
	struct pseries_wdt *pw = watchdog_get_drvdata(wdd);
	struct device *dev = wdd->parent;
	long rc;

	rc = plpar_hcall_norets(H_WATCHDOG, PSERIES_WDTF_OP_STOP, pw->num);
	if (rc != H_SUCCESS && rc != H_NOOP) {
		dev_crit(dev, "H_WATCHDOG: %ld: failed to stop timer %lu",
			 rc, pw->num);
		return -EIO;
	}
	return 0;
}

static struct watchdog_info pseries_wdt_info = {
	.identity = DRV_NAME,
	.options = WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT
	    | WDIOF_PRETIMEOUT,
};

static const struct watchdog_ops pseries_wdt_ops = {
	.owner = THIS_MODULE,
	.start = pseries_wdt_start,
	.stop = pseries_wdt_stop,
};

static int pseries_wdt_probe(struct platform_device *pdev)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE] = { 0 };
	struct pseries_wdt *pw;
	unsigned long cap;
	long msecs, rc;
	int err;

	rc = plpar_hcall(H_WATCHDOG, ret, PSERIES_WDTF_OP_QUERY);
	if (rc == H_FUNCTION)
		return -ENODEV;
	if (rc != H_SUCCESS)
		return -EIO;
	cap = ret[0];

	pw = devm_kzalloc(&pdev->dev, sizeof(*pw), GFP_KERNEL);
	if (!pw)
		return -ENOMEM;

	/*
	 * Assume watchdogNumber 1 for now.  If we ever support
	 * multiple timers we will need to devise a way to choose a
	 * distinct watchdogNumber for each platform device at device
	 * registration time.
	 */
	pw->num = 1;
	if (PSERIES_WDTQ_MAX_NUMBER(cap) < pw->num)
		return -ENODEV;

	if (action >= ARRAY_SIZE(pseries_wdt_action))
		return -EINVAL;
	pw->action = pseries_wdt_action[action];

	pw->wd.parent = &pdev->dev;
	pw->wd.info = &pseries_wdt_info;
	pw->wd.ops = &pseries_wdt_ops;
	msecs = PSERIES_WDTQ_MIN_TIMEOUT(cap);
	pw->wd.min_timeout = DIV_ROUND_UP(msecs, MSEC_PER_SEC);
	pw->wd.max_timeout = UINT_MAX / 1000;	/* from linux/watchdog.h */
	pw->wd.timeout = timeout;
	if (watchdog_init_timeout(&pw->wd, 0, NULL))
		return -EINVAL;
	watchdog_set_nowayout(&pw->wd, nowayout);
	watchdog_stop_on_reboot(&pw->wd);
	watchdog_stop_on_unregister(&pw->wd);
	watchdog_set_drvdata(&pw->wd, pw);

	err = devm_watchdog_register_device(&pdev->dev, &pw->wd);
	if (err)
		return err;

	platform_set_drvdata(pdev, &pw->wd);

	return 0;
}

static int pseries_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct watchdog_device *wd = platform_get_drvdata(pdev);

	if (watchdog_active(wd))
		return pseries_wdt_stop(wd);
	return 0;
}

static int pseries_wdt_resume(struct platform_device *pdev)
{
	struct watchdog_device *wd = platform_get_drvdata(pdev);

	if (watchdog_active(wd))
		return pseries_wdt_start(wd);
	return 0;
}

static const struct platform_device_id pseries_wdt_id[] = {
	{ .name = "pseries-wdt" },
	{}
};
MODULE_DEVICE_TABLE(platform, pseries_wdt_id);

static struct platform_driver pseries_wdt_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.id_table = pseries_wdt_id,
	.probe = pseries_wdt_probe,
	.resume = pseries_wdt_resume,
	.suspend = pseries_wdt_suspend,
};
module_platform_driver(pseries_wdt_driver);

MODULE_AUTHOR("Alexey Kardashevskiy");
MODULE_AUTHOR("Scott Cheloha");
MODULE_DESCRIPTION("POWER Architecture Platform Watchdog Driver");
MODULE_LICENSE("GPL");
