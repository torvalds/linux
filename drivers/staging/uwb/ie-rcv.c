// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ultra Wide Band
 * IE Received notification handling.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/bitmap.h>
#include "uwb-internal.h"

/*
 * Process an incoming IE Received notification.
 */
int uwbd_evt_handle_rc_ie_rcv(struct uwb_event *evt)
{
	int result = -EINVAL;
	struct device *dev = &evt->rc->uwb_dev.dev;
	struct uwb_rc_evt_ie_rcv *iercv;

	/* Is there enough data to decode it? */
	if (evt->notif.size < sizeof(*iercv)) {
		dev_err(dev, "IE Received notification: Not enough data to "
			"decode (%zu vs %zu bytes needed)\n",
			evt->notif.size, sizeof(*iercv));
		goto error;
	}
	iercv = container_of(evt->notif.rceb, struct uwb_rc_evt_ie_rcv, rceb);

	dev_dbg(dev, "IE received, element ID=%d\n", iercv->IEData[0]);

	if (iercv->IEData[0] == UWB_RELINQUISH_REQUEST_IE) {
		dev_warn(dev, "unhandled Relinquish Request IE\n");
	}

	return 0;
error:
	return result;
}
