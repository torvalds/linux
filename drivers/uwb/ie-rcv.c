/*
 * Ultra Wide Band
 * IE Received notification handling.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
	size_t iesize;

	/* Is there enough data to decode it? */
	if (evt->notif.size < sizeof(*iercv)) {
		dev_err(dev, "IE Received notification: Not enough data to "
			"decode (%zu vs %zu bytes needed)\n",
			evt->notif.size, sizeof(*iercv));
		goto error;
	}
	iercv = container_of(evt->notif.rceb, struct uwb_rc_evt_ie_rcv, rceb);
	iesize = le16_to_cpu(iercv->wIELength);

	dev_dbg(dev, "IE received, element ID=%d\n", iercv->IEData[0]);

	if (iercv->IEData[0] == UWB_RELINQUISH_REQUEST_IE) {
		dev_warn(dev, "unhandled Relinquish Request IE\n");
	}

	return 0;
error:
	return result;
}
