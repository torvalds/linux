/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dmub/dmub_srv_stat.h"
#include "dmub/inc/dmub_cmd.h"

/**
 * DOC: DMUB_SRV STAT Interface
 *
 * These interfaces are called without acquiring DAL and DC locks.
 * Hence, there is limitations on whese interfaces can access. Only
 * variables exclusively defined for these interfaces can be modified.
 */

/**
 * dmub_srv_stat_get_notification - Retrieves a dmub outbox notification, set up dmub notification
 *                                  structure with message information. Also a pending bit if queue
 *                                  is having more notifications
 *  @dmub: dmub srv structure
 *  @notify: dmub notification structure to be filled up
 *
 *  Returns: dmub_status
 */
enum dmub_status dmub_srv_stat_get_notification(struct dmub_srv *dmub,
						struct dmub_notification *notify)
{
	/**
	 * This function is called without dal and dc locks, so
	 * we shall not modify any dmub variables, only dmub->outbox1_rb
	 * is exempted as it is exclusively accessed by this function
	 */
	union dmub_rb_out_cmd cmd = {0};

	if (!dmub->hw_init) {
		notify->type = DMUB_NOTIFICATION_NO_DATA;
		notify->pending_notification = false;
		return DMUB_STATUS_INVALID;
	}

	/* Get write pointer which is updated by dmub */
	dmub->outbox1_rb.wrpt = dmub->hw_funcs.get_outbox1_wptr(dmub);

	if (!dmub_rb_out_front(&dmub->outbox1_rb, &cmd)) {
		notify->type = DMUB_NOTIFICATION_NO_DATA;
		notify->pending_notification = false;
		return DMUB_STATUS_OK;
	}

	switch (cmd.cmd_common.header.type) {
	case DMUB_OUT_CMD__DP_AUX_REPLY:
		notify->type = DMUB_NOTIFICATION_AUX_REPLY;
		notify->link_index = cmd.dp_aux_reply.control.instance;
		notify->result = cmd.dp_aux_reply.control.result;
		dmub_memcpy((void *)&notify->aux_reply,
			(void *)&cmd.dp_aux_reply.reply_data, sizeof(struct aux_reply_data));
		break;
	case DMUB_OUT_CMD__DP_HPD_NOTIFY:
		if (cmd.dp_hpd_notify.hpd_data.hpd_type == DP_HPD) {
			notify->type = DMUB_NOTIFICATION_HPD;
			notify->hpd_status = cmd.dp_hpd_notify.hpd_data.hpd_status;
		} else {
			notify->type = DMUB_NOTIFICATION_HPD_IRQ;
		}

		notify->link_index = cmd.dp_hpd_notify.hpd_data.instance;
		notify->result = AUX_RET_SUCCESS;
		break;
	case DMUB_OUT_CMD__SET_CONFIG_REPLY:
		notify->type = DMUB_NOTIFICATION_SET_CONFIG_REPLY;
		notify->link_index = cmd.set_config_reply.set_config_reply_control.instance;
		notify->sc_status = cmd.set_config_reply.set_config_reply_control.status;
		break;
	case DMUB_OUT_CMD__DPIA_NOTIFICATION:
		notify->type = DMUB_NOTIFICATION_DPIA_NOTIFICATION;
		notify->link_index = cmd.dpia_notification.payload.header.instance;

		if (cmd.dpia_notification.payload.header.type == DPIA_NOTIFY__BW_ALLOCATION) {

			notify->dpia_notification.payload.data.dpia_bw_alloc.estimated_bw =
					cmd.dpia_notification.payload.data.dpia_bw_alloc.estimated_bw;
			notify->dpia_notification.payload.data.dpia_bw_alloc.allocated_bw =
					cmd.dpia_notification.payload.data.dpia_bw_alloc.allocated_bw;

			if (cmd.dpia_notification.payload.data.dpia_bw_alloc.bits.bw_request_failed)
				notify->result = DPIA_BW_REQ_FAILED;
			else if (cmd.dpia_notification.payload.data.dpia_bw_alloc.bits.bw_request_succeeded)
				notify->result = DPIA_BW_REQ_SUCCESS;
			else if (cmd.dpia_notification.payload.data.dpia_bw_alloc.bits.est_bw_changed)
				notify->result = DPIA_EST_BW_CHANGED;
			else if (cmd.dpia_notification.payload.data.dpia_bw_alloc.bits.bw_alloc_cap_changed)
				notify->result = DPIA_BW_ALLOC_CAPS_CHANGED;
		}
		break;
	default:
		notify->type = DMUB_NOTIFICATION_NO_DATA;
		break;
	}

	/* Pop outbox1 ringbuffer and update read pointer */
	dmub_rb_pop_front(&dmub->outbox1_rb);
	dmub->hw_funcs.set_outbox1_rptr(dmub, dmub->outbox1_rb.rptr);

	/**
	 * Notify dc whether dmub has a pending outbox message,
	 * this is to avoid one more call to dmub_srv_stat_get_notification
	 */
	if (dmub_rb_empty(&dmub->outbox1_rb))
		notify->pending_notification = false;
	else
		notify->pending_notification = true;

	return DMUB_STATUS_OK;
}
