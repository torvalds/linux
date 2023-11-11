// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 NVIDIA Corporation. All rights reserved.
 *
 * NVIDIA USB Type-C Alt Mode Driver
 */
#include <linux/module.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_dp.h>
#include "displayport.h"

static int nvidia_altmode_probe(struct typec_altmode *alt)
{
	if (alt->svid == USB_TYPEC_NVIDIA_VLINK_SID)
		return dp_altmode_probe(alt);
	else
		return -ENOTSUPP;
}

static void nvidia_altmode_remove(struct typec_altmode *alt)
{
	if (alt->svid == USB_TYPEC_NVIDIA_VLINK_SID)
		dp_altmode_remove(alt);
}

static const struct typec_device_id nvidia_typec_id[] = {
	{ USB_TYPEC_NVIDIA_VLINK_SID, TYPEC_ANY_MODE },
	{ },
};
MODULE_DEVICE_TABLE(typec, nvidia_typec_id);

static struct typec_altmode_driver nvidia_altmode_driver = {
	.id_table = nvidia_typec_id,
	.probe = nvidia_altmode_probe,
	.remove = nvidia_altmode_remove,
	.driver = {
		.name = "typec_nvidia",
		.owner = THIS_MODULE,
	},
};
module_typec_altmode_driver(nvidia_altmode_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NVIDIA USB Type-C Alt Mode Driver");
