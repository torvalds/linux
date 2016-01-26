/*
 * SVC Greybus "watchdog" driver.
 *
 * Copyright 2016 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/delay.h>
#include <linux/workqueue.h>
#include "greybus.h"

#define SVC_WATCHDOG_PERIOD	(2*HZ)

struct gb_svc_watchdog {
	struct delayed_work	work;
	struct gb_svc		*svc;
	bool			enabled;
};

static struct delayed_work reset_work;

static void greybus_reset(struct work_struct *work)
{
	static char start_path[256] = "/system/bin/start";
	static char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin",
		NULL,
	};
	static char *argv[] = {
		start_path,
		"unipro_reset",
		NULL,
	};

	printk(KERN_ERR "svc_watchdog: calling \"%s %s\" to reset greybus network!\n",
	       argv[0], argv[1]);
	call_usermodehelper(start_path, argv, envp, UMH_WAIT_EXEC);
}

static void do_work(struct work_struct *work)
{
	struct gb_svc_watchdog *watchdog;
	struct gb_svc *svc;
	int retval;

	watchdog = container_of(work, struct gb_svc_watchdog, work.work);
	svc = watchdog->svc;

	dev_dbg(&svc->dev, "%s: ping.\n", __func__);
	retval = gb_svc_ping(svc);
	if (retval) {
		/*
		 * Something went really wrong, let's warn userspace and then
		 * pull the plug and reset the whole greybus network.
		 * We need to do this outside of this workqueue as we will be
		 * tearing down the svc device itself.  So queue up
		 * yet-another-callback to do that.
		 */
		dev_err(&svc->dev,
			"SVC ping has returned %d, something is wrong!!!\n",
			retval);
		dev_err(&svc->dev, "Resetting the greybus network, watch out!!!\n");

		INIT_DELAYED_WORK(&reset_work, greybus_reset);
		queue_delayed_work(system_wq, &reset_work, HZ/2);

		/*
		 * Disable ourselves, we don't want to trip again unless
		 * userspace wants us to.
		 */
		watchdog->enabled = false;
	}

	/* resubmit our work to happen again, if we are still "alive" */
	if (watchdog->enabled)
		queue_delayed_work(system_wq, &watchdog->work,
				   SVC_WATCHDOG_PERIOD);
}

int gb_svc_watchdog_create(struct gb_svc *svc)
{
	struct gb_svc_watchdog *watchdog;

	if (svc->watchdog)
		return 0;

	watchdog = kmalloc(sizeof(*watchdog), GFP_KERNEL);
	if (!watchdog)
		return -ENOMEM;

	watchdog->enabled = false;
	watchdog->svc = svc;
	INIT_DELAYED_WORK(&watchdog->work, do_work);
	svc->watchdog = watchdog;

	return gb_svc_watchdog_enable(svc);
}

void gb_svc_watchdog_destroy(struct gb_svc *svc)
{
	struct gb_svc_watchdog *watchdog = svc->watchdog;

	if (!watchdog)
		return;

	gb_svc_watchdog_disable(svc);
	svc->watchdog = NULL;
	kfree(watchdog);
}

bool gb_svc_watchdog_enabled(struct gb_svc *svc)
{
	if (!svc || !svc->watchdog)
		return false;
	return svc->watchdog->enabled;
}

int gb_svc_watchdog_enable(struct gb_svc *svc)
{
	struct gb_svc_watchdog *watchdog;

	if (!svc->watchdog)
		return -ENODEV;

	watchdog = svc->watchdog;
	if (watchdog->enabled)
		return 0;

	watchdog->enabled = true;
	queue_delayed_work(system_wq, &watchdog->work,
			   SVC_WATCHDOG_PERIOD);
	return 0;
}

int gb_svc_watchdog_disable(struct gb_svc *svc)
{
	struct gb_svc_watchdog *watchdog;

	if (!svc->watchdog)
		return -ENODEV;

	watchdog = svc->watchdog;
	if (!watchdog->enabled)
		return 0;

	watchdog->enabled = false;
	cancel_delayed_work_sync(&watchdog->work);
	return 0;
}
