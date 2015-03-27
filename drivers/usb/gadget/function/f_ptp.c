/*
 * Gadget Function Driver for PTP
 *
 * Copyright (C) 2014 Google, Inc.
 * Author: Badhri Jagan Sridharan <badhri@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/types.h>

#include <linux/configfs.h>
#include <linux/usb/composite.h>

#include "f_mtp.h"

static struct usb_function_instance *ptp_alloc_inst(void)
{
	return alloc_inst_mtp_ptp(false);
}

static struct usb_function *ptp_alloc(struct usb_function_instance *fi)
{
	return function_alloc_mtp_ptp(fi, false);
}

DECLARE_USB_FUNCTION_INIT(ptp, ptp_alloc_inst, ptp_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Badhri Jagan Sridharan");
