/*
 * WUSB cluster reservation management
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
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
#include <linux/kernel.h>
#include <linux/uwb.h>

#include "wusbhc.h"

/*
 * WUSB cluster reservations are multicast reservations with the
 * broadcast cluster ID (BCID) as the target DevAddr.
 *
 * FIXME: consider adjusting the reservation depending on what devices
 * are attached.
 */

static int wusbhc_bwa_set(struct wusbhc *wusbhc, u8 stream,
	const struct uwb_mas_bm *mas)
{
	if (mas == NULL)
		mas = &uwb_mas_bm_zero;
	return wusbhc->bwa_set(wusbhc, stream, mas);
}

/**
 * wusbhc_rsv_complete_cb - WUSB HC reservation complete callback
 * @rsv:    the reservation
 *
 * Either set or clear the HC's view of the reservation.
 *
 * FIXME: when a reservation is denied the HC should be stopped.
 */
static void wusbhc_rsv_complete_cb(struct uwb_rsv *rsv)
{
	struct wusbhc *wusbhc = rsv->pal_priv;
	struct device *dev = wusbhc->dev;
	struct uwb_mas_bm mas;

	dev_dbg(dev, "%s: state = %d\n", __func__, rsv->state);
	switch (rsv->state) {
	case UWB_RSV_STATE_O_ESTABLISHED:
		uwb_rsv_get_usable_mas(rsv, &mas);
		dev_dbg(dev, "established reservation: %*pb\n",
			UWB_NUM_MAS, mas.bm);
		wusbhc_bwa_set(wusbhc, rsv->stream, &mas);
		break;
	case UWB_RSV_STATE_NONE:
		dev_dbg(dev, "removed reservation\n");
		wusbhc_bwa_set(wusbhc, 0, NULL);
		break;
	default:
		dev_dbg(dev, "unexpected reservation state: %d\n", rsv->state);
		break;
	}
}


/**
 * wusbhc_rsv_establish - establish a reservation for the cluster
 * @wusbhc: the WUSB HC requesting a bandwidth reservation
 */
int wusbhc_rsv_establish(struct wusbhc *wusbhc)
{
	struct uwb_rc *rc = wusbhc->uwb_rc;
	struct uwb_rsv *rsv;
	struct uwb_dev_addr bcid;
	int ret;

	if (rc == NULL)
		return -ENODEV;

	rsv = uwb_rsv_create(rc, wusbhc_rsv_complete_cb, wusbhc);
	if (rsv == NULL)
		return -ENOMEM;

	bcid.data[0] = wusbhc->cluster_id;
	bcid.data[1] = 0;

	rsv->target.type = UWB_RSV_TARGET_DEVADDR;
	rsv->target.devaddr = bcid;
	rsv->type = UWB_DRP_TYPE_PRIVATE;
	rsv->max_mas = 256; /* try to get as much as possible */
	rsv->min_mas = 15;  /* one MAS per zone */
	rsv->max_interval = 1; /* max latency is one zone */
	rsv->is_multicast = true;

	ret = uwb_rsv_establish(rsv);
	if (ret == 0)
		wusbhc->rsv = rsv;
	else
		uwb_rsv_destroy(rsv);
	return ret;
}


/**
 * wusbhc_rsv_terminate - terminate the cluster reservation
 * @wusbhc: the WUSB host whose reservation is to be terminated
 */
void wusbhc_rsv_terminate(struct wusbhc *wusbhc)
{
	if (wusbhc->rsv) {
		uwb_rsv_terminate(wusbhc->rsv);
		uwb_rsv_destroy(wusbhc->rsv);
		wusbhc->rsv = NULL;
	}
}
