// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pps-ktimer.c -- kernel timer test client
 *
 * Copyright (C) 2005-2006   Rodolfo Giometti <giometti@linux.it>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/pps_kernel.h>

/*
 * Global variables
 */

static struct pps_device *pps;
static struct timer_list ktimer;

/*
 * The kernel timer
 */

static void pps_ktimer_event(struct timer_list *unused)
{
	struct pps_event_time ts;

	/* First of all we get the time stamp... */
	pps_get_ts(&ts);

	pps_event(pps, &ts, PPS_CAPTUREASSERT, NULL);

	mod_timer(&ktimer, jiffies + HZ);
}

/*
 * The PPS info struct
 */

static struct pps_source_info pps_ktimer_info = {
	.name		= "ktimer",
	.path		= "",
	.mode		= PPS_CAPTUREASSERT | PPS_OFFSETASSERT |
			  PPS_ECHOASSERT |
			  PPS_CANWAIT | PPS_TSFMT_TSPEC,
	.owner		= THIS_MODULE,
};

/*
 * Module staff
 */

static void __exit pps_ktimer_exit(void)
{
	dev_info(pps->dev, "ktimer PPS source unregistered\n");

	del_timer_sync(&ktimer);
	pps_unregister_source(pps);
}

static int __init pps_ktimer_init(void)
{
	pps = pps_register_source(&pps_ktimer_info,
				PPS_CAPTUREASSERT | PPS_OFFSETASSERT);
	if (IS_ERR(pps)) {
		pr_err("cannot register PPS source\n");
		return PTR_ERR(pps);
	}

	timer_setup(&ktimer, pps_ktimer_event, 0);
	mod_timer(&ktimer, jiffies + HZ);

	dev_info(pps->dev, "ktimer PPS source registered\n");

	return 0;
}

module_init(pps_ktimer_init);
module_exit(pps_ktimer_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("dummy PPS source by using a kernel timer (just for debug)");
MODULE_LICENSE("GPL");
