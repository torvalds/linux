/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2022, Google LLC
 */

#ifndef _USB_MISC_ONBOARD_USB_HUB_H
#define _USB_MISC_ONBOARD_USB_HUB_H

static const struct of_device_id onboard_hub_match[] = {
	{ .compatible = "usbbda,411" },
	{ .compatible = "usbbda,5411" },
	{ .compatible = "usbbda,414" },
	{ .compatible = "usbbda,5414" },
	{}
};

#endif /* _USB_MISC_ONBOARD_USB_HUB_H */
