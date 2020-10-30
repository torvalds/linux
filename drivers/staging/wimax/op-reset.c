// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux WiMAX
 * Implement and export a method for resetting a WiMAX device
 *
 * Copyright (C) 2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This implements a simple synchronous call to reset a WiMAX device.
 *
 * Resets aim at being warm, keeping the device handles active;
 * however, when that fails, it falls back to a cold reset (that will
 * disconnect and reconnect the device).
 */

#include "net-wimax.h"
#include <net/genetlink.h>
#include "linux-wimax.h"
#include <linux/security.h>
#include <linux/export.h>
#include "wimax-internal.h"

#define D_SUBMODULE op_reset
#include "debug-levels.h"


/**
 * wimax_reset - Reset a WiMAX device
 *
 * @wimax_dev: WiMAX device descriptor
 *
 * Returns:
 *
 * %0 if ok and a warm reset was done (the device still exists in
 * the system).
 *
 * -%ENODEV if a cold/bus reset had to be done (device has
 * disconnected and reconnected, so current handle is not valid
 * any more).
 *
 * -%EINVAL if the device is not even registered.
 *
 * Any other negative error code shall be considered as
 * non-recoverable.
 *
 * Description:
 *
 * Called when wanting to reset the device for any reason. Device is
 * taken back to power on status.
 *
 * This call blocks; on successful return, the device has completed the
 * reset process and is ready to operate.
 */
int wimax_reset(struct wimax_dev *wimax_dev)
{
	int result = -EINVAL;
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	enum wimax_st state;

	might_sleep();
	d_fnstart(3, dev, "(wimax_dev %p)\n", wimax_dev);
	mutex_lock(&wimax_dev->mutex);
	dev_hold(wimax_dev->net_dev);
	state = wimax_dev->state;
	mutex_unlock(&wimax_dev->mutex);

	if (state >= WIMAX_ST_DOWN) {
		mutex_lock(&wimax_dev->mutex_reset);
		result = wimax_dev->op_reset(wimax_dev);
		mutex_unlock(&wimax_dev->mutex_reset);
	}
	dev_put(wimax_dev->net_dev);

	d_fnend(3, dev, "(wimax_dev %p) = %d\n", wimax_dev, result);
	return result;
}
EXPORT_SYMBOL(wimax_reset);


/*
 * Exporting to user space over generic netlink
 *
 * Parse the reset command from user space, return error code.
 *
 * No attributes.
 */
int wimax_gnl_doit_reset(struct sk_buff *skb, struct genl_info *info)
{
	int result, ifindex;
	struct wimax_dev *wimax_dev;

	d_fnstart(3, NULL, "(skb %p info %p)\n", skb, info);
	result = -ENODEV;
	if (info->attrs[WIMAX_GNL_RESET_IFIDX] == NULL) {
		pr_err("WIMAX_GNL_OP_RFKILL: can't find IFIDX attribute\n");
		goto error_no_wimax_dev;
	}
	ifindex = nla_get_u32(info->attrs[WIMAX_GNL_RESET_IFIDX]);
	wimax_dev = wimax_dev_get_by_genl_info(info, ifindex);
	if (wimax_dev == NULL)
		goto error_no_wimax_dev;
	/* Execute the operation and send the result back to user space */
	result = wimax_reset(wimax_dev);
	dev_put(wimax_dev->net_dev);
error_no_wimax_dev:
	d_fnend(3, NULL, "(skb %p info %p) = %d\n", skb, info, result);
	return result;
}
