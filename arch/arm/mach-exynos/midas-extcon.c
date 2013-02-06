/*
 * midas-extcon.c - EXTCON (External Connector)
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/host_notify.h>
#include <linux/power_supply.h>
#include <linux/regulator/machine.h>

#include <plat/devs.h>
#include <plat/udc-hs.h>

#ifdef CONFIG_JACK_MON
#include <linux/jack.h>
#endif

#define EXTCON_DEV_NAME		"max77693-muic"

static struct extcon_dev *midas_extcon;
static struct notifier_block extcon_nb;
static struct work_struct extcon_notifier_work;
static unsigned long prev_value;

static void midas_extcon_notifier_work(struct work_struct *work)
{
	/* TODO */
}

static int midas_extcon_notifier(struct notifier_block *self,
		unsigned long event, void *ptr)
{
	/* Store the previous cable state of extcon device */
	prev_value = event;

	schedule_work(&extcon_notifier_work);

	return NOTIFY_DONE;
}

static int __init midas_extcon_init(void)
{
	int ret;

	midas_extcon = extcon_get_extcon_dev(EXTCON_DEV_NAME);
	if (!midas_extcon) {
		printk(KERN_ERR "Failed to get extcon device of %s\n",
				EXTCON_DEV_NAME);
		ret = -EINVAL;
		goto err_extcon;
	}

	INIT_WORK(&extcon_notifier_work, midas_extcon_notifier_work);
	extcon_nb.notifier_call = midas_extcon_notifier;
	ret = extcon_register_notifier(midas_extcon, &extcon_nb);
	if (ret < 0) {
		pr_err("Failed to register extcon device for mms_ts\n");
		ret = -EINVAL;
		goto err_extcon;
	}

	/* TODO */

	return 0;

err_extcon:
	midas_extcon = NULL;
	return ret;
}
late_initcall(midas_extcon_init);
