/*
 * Ultra Wide Band
 * Dynamic Reservation Protocol handling
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
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
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include "uwb-internal.h"

/**
 * Construct and send the SET DRP IE
 *
 * @rc:         UWB Host controller
 * @returns:    >= 0 number of bytes still available in the beacon
 *              < 0 errno code on error.
 *
 * See WUSB[8.6.2.7]: The host must set all the DRP IEs that it wants the
 * device to include in its beacon at the same time. We thus have to
 * traverse all reservations and include the DRP IEs of all PENDING
 * and NEGOTIATED reservations in a SET DRP command for transmission.
 *
 * A DRP Availability IE is appended.
 *
 * rc->uwb_dev.mutex is held
 *
 * FIXME We currently ignore the returned value indicating the remaining space
 * in beacon. This could be used to deny reservation requests earlier if
 * determined that they would cause the beacon space to be exceeded.
 */
static
int uwb_rc_gen_send_drp_ie(struct uwb_rc *rc)
{
	int result;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_rc_cmd_set_drp_ie *cmd;
	struct uwb_rc_evt_set_drp_ie reply;
	struct uwb_rsv *rsv;
	int num_bytes = 0;
	u8 *IEDataptr;

	result = -ENOMEM;
	/* First traverse all reservations to determine memory needed. */
	list_for_each_entry(rsv, &rc->reservations, rc_node) {
		if (rsv->drp_ie != NULL)
			num_bytes += rsv->drp_ie->hdr.length + 2;
	}
	num_bytes += sizeof(rc->drp_avail.ie);
	cmd = kzalloc(sizeof(*cmd) + num_bytes, GFP_KERNEL);
	if (cmd == NULL)
		goto error;
	cmd->rccb.bCommandType = UWB_RC_CET_GENERAL;
	cmd->rccb.wCommand = cpu_to_le16(UWB_RC_CMD_SET_DRP_IE);
	cmd->wIELength = num_bytes;
	IEDataptr = (u8 *)&cmd->IEData[0];

	/* Next traverse all reservations to place IEs in allocated memory. */
	list_for_each_entry(rsv, &rc->reservations, rc_node) {
		if (rsv->drp_ie != NULL) {
			memcpy(IEDataptr, rsv->drp_ie,
			       rsv->drp_ie->hdr.length + 2);
			IEDataptr += rsv->drp_ie->hdr.length + 2;
		}
	}
	memcpy(IEDataptr, &rc->drp_avail.ie, sizeof(rc->drp_avail.ie));

	reply.rceb.bEventType = UWB_RC_CET_GENERAL;
	reply.rceb.wEvent = UWB_RC_CMD_SET_DRP_IE;
	result = uwb_rc_cmd(rc, "SET-DRP-IE", &cmd->rccb,
			sizeof(*cmd) + num_bytes, &reply.rceb,
			sizeof(reply));
	if (result < 0)
		goto error_cmd;
	result = le16_to_cpu(reply.wRemainingSpace);
	if (reply.bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(&rc->uwb_dev.dev, "SET-DRP-IE: command execution "
				"failed: %s (%d). RemainingSpace in beacon "
				"= %d\n", uwb_rc_strerror(reply.bResultCode),
				reply.bResultCode, result);
		result = -EIO;
	} else {
		dev_dbg(dev, "SET-DRP-IE sent. RemainingSpace in beacon "
			     "= %d.\n", result);
		result = 0;
	}
error_cmd:
	kfree(cmd);
error:
	return result;

}
/**
 * Send all DRP IEs associated with this host
 *
 * @returns:    >= 0 number of bytes still available in the beacon
 *              < 0 errno code on error.
 *
 * As per the protocol we obtain the host controller device lock to access
 * bandwidth structures.
 */
int uwb_rc_send_all_drp_ie(struct uwb_rc *rc)
{
	int result;

	mutex_lock(&rc->uwb_dev.mutex);
	result = uwb_rc_gen_send_drp_ie(rc);
	mutex_unlock(&rc->uwb_dev.mutex);
	return result;
}

void uwb_drp_handle_timeout(struct uwb_rsv *rsv)
{
	struct device *dev = &rsv->rc->uwb_dev.dev;

	dev_dbg(dev, "reservation timeout in state %s (%d)\n",
		uwb_rsv_state_str(rsv->state), rsv->state);

	switch (rsv->state) {
	case UWB_RSV_STATE_O_INITIATED:
		if (rsv->is_multicast) {
			uwb_rsv_set_state(rsv, UWB_RSV_STATE_O_ESTABLISHED);
			return;
		}
		break;
	case UWB_RSV_STATE_O_ESTABLISHED:
		if (rsv->is_multicast)
			return;
		break;
	default:
		break;
	}
	uwb_rsv_remove(rsv);
}

/*
 * Based on the DRP IE, transition a target reservation to a new
 * state.
 */
static void uwb_drp_process_target(struct uwb_rc *rc, struct uwb_rsv *rsv,
				   struct uwb_ie_drp *drp_ie)
{
	struct device *dev = &rc->uwb_dev.dev;
	int status;
	enum uwb_drp_reason reason_code;

	status = uwb_ie_drp_status(drp_ie);
	reason_code = uwb_ie_drp_reason_code(drp_ie);

	if (status) {
		switch (reason_code) {
		case UWB_DRP_REASON_ACCEPTED:
			uwb_rsv_set_state(rsv, UWB_RSV_STATE_T_ACCEPTED);
			break;
		case UWB_DRP_REASON_MODIFIED:
			dev_err(dev, "FIXME: unhandled reason code (%d/%d)\n",
				reason_code, status);
			break;
		default:
			dev_warn(dev, "ignoring invalid DRP IE state (%d/%d)\n",
				 reason_code, status);
		}
	} else {
		switch (reason_code) {
		case UWB_DRP_REASON_ACCEPTED:
			/* New reservations are handled in uwb_rsv_find(). */
			break;
		case UWB_DRP_REASON_DENIED:
			uwb_rsv_set_state(rsv, UWB_RSV_STATE_NONE);
			break;
		case UWB_DRP_REASON_CONFLICT:
		case UWB_DRP_REASON_MODIFIED:
			dev_err(dev, "FIXME: unhandled reason code (%d/%d)\n",
				reason_code, status);
			break;
		default:
			dev_warn(dev, "ignoring invalid DRP IE state (%d/%d)\n",
				 reason_code, status);
		}
	}
}

/*
 * Based on the DRP IE, transition an owner reservation to a new
 * state.
 */
static void uwb_drp_process_owner(struct uwb_rc *rc, struct uwb_rsv *rsv,
				  struct uwb_ie_drp *drp_ie)
{
	struct device *dev = &rc->uwb_dev.dev;
	int status;
	enum uwb_drp_reason reason_code;

	status = uwb_ie_drp_status(drp_ie);
	reason_code = uwb_ie_drp_reason_code(drp_ie);

	if (status) {
		switch (reason_code) {
		case UWB_DRP_REASON_ACCEPTED:
			uwb_rsv_set_state(rsv, UWB_RSV_STATE_O_ESTABLISHED);
			break;
		case UWB_DRP_REASON_MODIFIED:
			dev_err(dev, "FIXME: unhandled reason code (%d/%d)\n",
				reason_code, status);
			break;
		default:
			dev_warn(dev, "ignoring invalid DRP IE state (%d/%d)\n",
				 reason_code, status);
		}
	} else {
		switch (reason_code) {
		case UWB_DRP_REASON_PENDING:
			uwb_rsv_set_state(rsv, UWB_RSV_STATE_O_PENDING);
			break;
		case UWB_DRP_REASON_DENIED:
			uwb_rsv_set_state(rsv, UWB_RSV_STATE_NONE);
			break;
		case UWB_DRP_REASON_CONFLICT:
		case UWB_DRP_REASON_MODIFIED:
			dev_err(dev, "FIXME: unhandled reason code (%d/%d)\n",
				reason_code, status);
			break;
		default:
			dev_warn(dev, "ignoring invalid DRP IE state (%d/%d)\n",
				 reason_code, status);
		}
	}
}

/*
 * Process a received DRP IE, it's either for a reservation owned by
 * the RC or targeted at it (or it's for a WUSB cluster reservation).
 */
static void uwb_drp_process(struct uwb_rc *rc, struct uwb_dev *src,
		     struct uwb_ie_drp *drp_ie)
{
	struct uwb_rsv *rsv;

	rsv = uwb_rsv_find(rc, src, drp_ie);
	if (!rsv) {
		/*
		 * No reservation? It's either for a recently
		 * terminated reservation; or the DRP IE couldn't be
		 * processed (e.g., an invalid IE or out of memory).
		 */
		return;
	}

	/*
	 * Do nothing with DRP IEs for reservations that have been
	 * terminated.
	 */
	if (rsv->state == UWB_RSV_STATE_NONE) {
		uwb_rsv_set_state(rsv, UWB_RSV_STATE_NONE);
		return;
	}

	if (uwb_ie_drp_owner(drp_ie))
		uwb_drp_process_target(rc, rsv, drp_ie);
	else
		uwb_drp_process_owner(rc, rsv, drp_ie);
}


/*
 * Process all the DRP IEs (both DRP IEs and the DRP Availability IE)
 * from a device.
 */
static
void uwb_drp_process_all(struct uwb_rc *rc, struct uwb_rc_evt_drp *drp_evt,
			 size_t ielen, struct uwb_dev *src_dev)
{
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_ie_hdr *ie_hdr;
	void *ptr;

	ptr = drp_evt->ie_data;
	for (;;) {
		ie_hdr = uwb_ie_next(&ptr, &ielen);
		if (!ie_hdr)
			break;

		switch (ie_hdr->element_id) {
		case UWB_IE_DRP_AVAILABILITY:
			/* FIXME: does something need to be done with this? */
			break;
		case UWB_IE_DRP:
			uwb_drp_process(rc, src_dev, (struct uwb_ie_drp *)ie_hdr);
			break;
		default:
			dev_warn(dev, "unexpected IE in DRP notification\n");
			break;
		}
	}

	if (ielen > 0)
		dev_warn(dev, "%d octets remaining in DRP notification\n",
			 (int)ielen);
}


/*
 * Go through all the DRP IEs and find the ones that conflict with our
 * reservations.
 *
 * FIXME: must resolve the conflict according the the rules in
 * [ECMA-368].
 */
static
void uwb_drp_process_conflict_all(struct uwb_rc *rc, struct uwb_rc_evt_drp *drp_evt,
				  size_t ielen, struct uwb_dev *src_dev)
{
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_ie_hdr *ie_hdr;
	struct uwb_ie_drp *drp_ie;
	void *ptr;

	ptr = drp_evt->ie_data;
	for (;;) {
		ie_hdr = uwb_ie_next(&ptr, &ielen);
		if (!ie_hdr)
			break;

		drp_ie = container_of(ie_hdr, struct uwb_ie_drp, hdr);

		/* FIXME: check if this DRP IE conflicts. */
	}

	if (ielen > 0)
		dev_warn(dev, "%d octets remaining in DRP notification\n",
			 (int)ielen);
}


/*
 * Terminate all reservations owned by, or targeted at, 'uwb_dev'.
 */
static void uwb_drp_terminate_all(struct uwb_rc *rc, struct uwb_dev *uwb_dev)
{
	struct uwb_rsv *rsv;

	list_for_each_entry(rsv, &rc->reservations, rc_node) {
		if (rsv->owner == uwb_dev
		    || (rsv->target.type == UWB_RSV_TARGET_DEV && rsv->target.dev == uwb_dev))
			uwb_rsv_remove(rsv);
	}
}


/**
 * uwbd_evt_handle_rc_drp - handle a DRP_IE event
 * @evt: the DRP_IE event from the radio controller
 *
 * This processes DRP notifications from the radio controller, either
 * initiating a new reservation or transitioning an existing
 * reservation into a different state.
 *
 * DRP notifications can occur for three different reasons:
 *
 * - UWB_DRP_NOTIF_DRP_IE_RECVD: one or more DRP IEs with the RC as
 *   the target or source have been recieved.
 *
 *   These DRP IEs could be new or for an existing reservation.
 *
 *   If the DRP IE for an existing reservation ceases to be to
 *   recieved for at least mMaxLostBeacons, the reservation should be
 *   considered to be terminated.  Note that the TERMINATE reason (see
 *   below) may not always be signalled (e.g., the remote device has
 *   two or more reservations established with the RC).
 *
 * - UWB_DRP_NOTIF_CONFLICT: DRP IEs from any device in the beacon
 *   group conflict with the RC's reservations.
 *
 * - UWB_DRP_NOTIF_TERMINATE: DRP IEs are no longer being received
 *   from a device (i.e., it's terminated all reservations).
 *
 * Only the software state of the reservations is changed; the setting
 * of the radio controller's DRP IEs is done after all the events in
 * an event buffer are processed.  This saves waiting multiple times
 * for the SET_DRP_IE command to complete.
 */
int uwbd_evt_handle_rc_drp(struct uwb_event *evt)
{
	struct device *dev = &evt->rc->uwb_dev.dev;
	struct uwb_rc *rc = evt->rc;
	struct uwb_rc_evt_drp *drp_evt;
	size_t ielength, bytes_left;
	struct uwb_dev_addr src_addr;
	struct uwb_dev *src_dev;
	int reason;

	/* Is there enough data to decode the event (and any IEs in
	   its payload)? */
	if (evt->notif.size < sizeof(*drp_evt)) {
		dev_err(dev, "DRP event: Not enough data to decode event "
			"[%zu bytes left, %zu needed]\n",
			evt->notif.size, sizeof(*drp_evt));
		return 0;
	}
	bytes_left = evt->notif.size - sizeof(*drp_evt);
	drp_evt = container_of(evt->notif.rceb, struct uwb_rc_evt_drp, rceb);
	ielength = le16_to_cpu(drp_evt->ie_length);
	if (bytes_left != ielength) {
		dev_err(dev, "DRP event: Not enough data in payload [%zu"
			"bytes left, %zu declared in the event]\n",
			bytes_left, ielength);
		return 0;
	}

	memcpy(src_addr.data, &drp_evt->src_addr, sizeof(src_addr));
	src_dev = uwb_dev_get_by_devaddr(rc, &src_addr);
	if (!src_dev) {
		/*
		 * A DRP notification from an unrecognized device.
		 *
		 * This is probably from a WUSB device that doesn't
		 * have an EUI-48 and therefore doesn't show up in the
		 * UWB device database.  It's safe to simply ignore
		 * these.
		 */
		return 0;
	}

	mutex_lock(&rc->rsvs_mutex);

	reason = uwb_rc_evt_drp_reason(drp_evt);

	switch (reason) {
	case UWB_DRP_NOTIF_DRP_IE_RCVD:
		uwb_drp_process_all(rc, drp_evt, ielength, src_dev);
		break;
	case UWB_DRP_NOTIF_CONFLICT:
		uwb_drp_process_conflict_all(rc, drp_evt, ielength, src_dev);
		break;
	case UWB_DRP_NOTIF_TERMINATE:
		uwb_drp_terminate_all(rc, src_dev);
		break;
	default:
		dev_warn(dev, "ignored DRP event with reason code: %d\n", reason);
		break;
	}

	mutex_unlock(&rc->rsvs_mutex);

	uwb_dev_put(src_dev);
	return 0;
}
