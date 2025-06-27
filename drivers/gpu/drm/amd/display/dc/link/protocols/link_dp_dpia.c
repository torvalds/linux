// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "dc.h"
#include "inc/core_status.h"
#include "dpcd_defs.h"

#include "link_dp_dpia.h"
#include "link_hwss.h"
#include "dm_helpers.h"
#include "dmub/inc/dmub_cmd.h"
#include "link_dpcd.h"
#include "link_dp_training.h"
#include "dc_dmub_srv.h"

#define DC_LOGGER \
	link->ctx->logger

/** @note Can remove once DP tunneling registers in upstream include/drm/drm_dp_helper.h */
/* DPCD DP Tunneling over USB4 */
#define DP_TUNNELING_CAPABILITIES_SUPPORT 0xe000d
#define DP_IN_ADAPTER_INFO                0xe000e
#define DP_USB4_DRIVER_ID                 0xe000f
#define DP_USB4_ROUTER_TOPOLOGY_ID        0xe001b

enum dc_status dpcd_get_tunneling_device_data(struct dc_link *link)
{
	enum dc_status status = DC_OK;
	uint8_t dpcd_dp_tun_data[3] = {0};
	uint8_t dpcd_topology_data[DPCD_USB4_TOPOLOGY_ID_LEN] = {0};
	uint8_t i = 0;

	status = core_link_read_dpcd(
			link,
			DP_TUNNELING_CAPABILITIES_SUPPORT,
			dpcd_dp_tun_data,
			sizeof(dpcd_dp_tun_data));

	if (status != DC_OK)
		goto err;

	link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.raw =
			dpcd_dp_tun_data[DP_TUNNELING_CAPABILITIES_SUPPORT - DP_TUNNELING_CAPABILITIES_SUPPORT];

	if (link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dp_tunneling == false)
		goto err;

	link->dpcd_caps.usb4_dp_tun_info.dpia_info.raw =
			dpcd_dp_tun_data[DP_IN_ADAPTER_INFO - DP_TUNNELING_CAPABILITIES_SUPPORT];
	link->dpcd_caps.usb4_dp_tun_info.usb4_driver_id =
			dpcd_dp_tun_data[DP_USB4_DRIVER_ID - DP_TUNNELING_CAPABILITIES_SUPPORT];

	if (link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dpia_bw_alloc) {
		status = core_link_read_dpcd(link, USB4_DRIVER_BW_CAPABILITY,
				dpcd_dp_tun_data, 2);

		if (status != DC_OK)
			goto err;

		link->dpcd_caps.usb4_dp_tun_info.driver_bw_cap.raw =
			dpcd_dp_tun_data[USB4_DRIVER_BW_CAPABILITY - USB4_DRIVER_BW_CAPABILITY];
		link->dpcd_caps.usb4_dp_tun_info.dpia_tunnel_info.raw =
			dpcd_dp_tun_data[DP_IN_ADAPTER_TUNNEL_INFO - USB4_DRIVER_BW_CAPABILITY];
	}

	DC_LOG_DEBUG("%s: Link[%d]  DP tunneling support  (RouterId=%d  AdapterId=%d)  "
			"DPIA_BW_Alloc_support=%d "
			"CM_BW_Alloc_support=%d ",
			__func__, link->link_index,
			link->dpcd_caps.usb4_dp_tun_info.usb4_driver_id,
			link->dpcd_caps.usb4_dp_tun_info.dpia_info.bits.dpia_num,
			link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dpia_bw_alloc,
			link->dpcd_caps.usb4_dp_tun_info.driver_bw_cap.bits.driver_bw_alloc_support);

	status = core_link_read_dpcd(
			link,
			DP_USB4_ROUTER_TOPOLOGY_ID,
			dpcd_topology_data,
			sizeof(dpcd_topology_data));

	if (status != DC_OK)
		goto err;

	for (i = 0; i < DPCD_USB4_TOPOLOGY_ID_LEN; i++)
		link->dpcd_caps.usb4_dp_tun_info.usb4_topology_id[i] = dpcd_topology_data[i];

err:
	return status;
}

bool dpia_query_hpd_status(struct dc_link *link)
{
	union dmub_rb_cmd cmd = {0};
	struct dc_dmub_srv *dmub_srv = link->ctx->dmub_srv;

	/* prepare QUERY_HPD command */
	cmd.query_hpd.header.type = DMUB_CMD__QUERY_HPD_STATE;
	cmd.query_hpd.header.payload_bytes = sizeof(cmd.query_hpd.data);
	cmd.query_hpd.data.instance = link->link_id.enum_id - ENUM_ID_1;
	cmd.query_hpd.data.ch_type = AUX_CHANNEL_DPIA;

	/* Query dpia hpd status from dmub */
	if (dc_wake_and_execute_dmub_cmd(dmub_srv->ctx, &cmd,
		DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY) &&
	    cmd.query_hpd.data.status == AUX_RET_SUCCESS) {
		DC_LOG_DEBUG("%s: for link(%d) dpia(%d) success, current_hpd_status(%d) new_hpd_status(%d)\n",
			__func__,
			link->link_index,
			link->link_id.enum_id - ENUM_ID_1,
			link->hpd_status,
			cmd.query_hpd.data.result);
		link->hpd_status = cmd.query_hpd.data.result;
	} else {
		DC_LOG_ERROR("%s: for link(%d) dpia(%d) failed with status(%d), current_hpd_status(%d) new_hpd_status(0)\n",
			__func__,
			link->link_index,
			link->link_id.enum_id - ENUM_ID_1,
			cmd.query_hpd.data.status,
			link->hpd_status);
		link->hpd_status = false;
	}

	return link->hpd_status;
}

void link_decide_dp_tunnel_settings(struct dc_stream_state *stream,
			struct dc_tunnel_settings *dp_tunnel_setting)
{
	struct dc_link *link = stream->link;

	memset(dp_tunnel_setting, 0, sizeof(*dp_tunnel_setting));

	if ((stream->signal == SIGNAL_TYPE_DISPLAY_PORT) || (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)) {
		dp_tunnel_setting->should_enable_dp_tunneling =
					link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dp_tunneling;

		if (link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dpia_bw_alloc
				&& link->dpcd_caps.usb4_dp_tun_info.driver_bw_cap.bits.driver_bw_alloc_support) {
			dp_tunnel_setting->should_use_dp_bw_allocation = true;
			dp_tunnel_setting->cm_id = link->dpcd_caps.usb4_dp_tun_info.usb4_driver_id & 0x0F;
			dp_tunnel_setting->group_id = link->dpcd_caps.usb4_dp_tun_info.dpia_tunnel_info.bits.group_id;
			dp_tunnel_setting->estimated_bw = link->dpia_bw_alloc_config.estimated_bw;
			dp_tunnel_setting->allocated_bw = link->dpia_bw_alloc_config.allocated_bw;
			dp_tunnel_setting->bw_granularity = link->dpia_bw_alloc_config.bw_granularity;
		}
	}
}

