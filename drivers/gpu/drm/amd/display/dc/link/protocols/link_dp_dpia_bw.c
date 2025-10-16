
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
/*********************************************************************/
// USB4 DPIA BANDWIDTH ALLOCATION LOGIC
/*********************************************************************/
#include "link_dp_dpia_bw.h"
#include "link_dpcd.h"
#include "dc_dmub_srv.h"

#define DC_LOGGER \
	link->ctx->logger

#define Kbps_TO_Gbps (1000 * 1000)

#define MST_TIME_SLOT_COUNT 64

// ------------------------------------------------------------------
// PRIVATE FUNCTIONS
// ------------------------------------------------------------------
/*
 * Always Check the following:
 *  - Is it USB4 link?
 *  - Is HPD HIGH?
 *  - Is BW Allocation Support Mode enabled on DP-Tx?
 */
static bool link_dp_is_bw_alloc_available(struct dc_link *link)
{
	return (link && link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dp_tunneling
		&& link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dpia_bw_alloc
		&& link->dpcd_caps.usb4_dp_tun_info.driver_bw_cap.bits.driver_bw_alloc_support);
}

static void reset_bw_alloc_struct(struct dc_link *link)
{
	link->dpia_bw_alloc_config.bw_alloc_enabled = false;
	link->dpia_bw_alloc_config.link_verified_bw = 0;
	link->dpia_bw_alloc_config.link_max_bw = 0;
	link->dpia_bw_alloc_config.allocated_bw = 0;
	link->dpia_bw_alloc_config.estimated_bw = 0;
	link->dpia_bw_alloc_config.bw_granularity = 0;
	link->dpia_bw_alloc_config.dp_overhead = 0;
	link->dpia_bw_alloc_config.nrd_max_lane_count = 0;
	link->dpia_bw_alloc_config.nrd_max_link_rate = 0;
	for (int i = 0; i < MAX_SINKS_PER_LINK; i++)
		link->dpia_bw_alloc_config.remote_sink_req_bw[i] = 0;
	DC_LOG_DEBUG("reset usb4 bw alloc of link(%d)\n", link->link_index);
}

#define BW_GRANULARITY_0 4 // 0.25 Gbps
#define BW_GRANULARITY_1 2 // 0.5 Gbps
#define BW_GRANULARITY_2 1 // 1 Gbps

static uint8_t get_bw_granularity(struct dc_link *link)
{
	uint8_t bw_granularity = 0;

	core_link_read_dpcd(
			link,
			DP_BW_GRANULALITY,
			&bw_granularity,
			sizeof(uint8_t));

	switch (bw_granularity & 0x3) {
	case 0:
		bw_granularity = BW_GRANULARITY_0;
		break;
	case 1:
		bw_granularity = BW_GRANULARITY_1;
		break;
	case 2:
	default:
		bw_granularity = BW_GRANULARITY_2;
		break;
	}

	return bw_granularity;
}

static int get_estimated_bw(struct dc_link *link)
{
	uint8_t bw_estimated_bw = 0;

	core_link_read_dpcd(
			link,
			ESTIMATED_BW,
			&bw_estimated_bw,
			sizeof(uint8_t));

	return bw_estimated_bw * (Kbps_TO_Gbps / link->dpia_bw_alloc_config.bw_granularity);
}

static int get_non_reduced_max_link_rate(struct dc_link *link)
{
	uint8_t nrd_max_link_rate = 0;

	core_link_read_dpcd(
			link,
			DP_TUNNELING_MAX_LINK_RATE,
			&nrd_max_link_rate,
			sizeof(uint8_t));

	return nrd_max_link_rate;
}

static int get_non_reduced_max_lane_count(struct dc_link *link)
{
	uint8_t nrd_max_lane_count = 0;

	core_link_read_dpcd(
			link,
			DP_TUNNELING_MAX_LANE_COUNT,
			&nrd_max_lane_count,
			sizeof(uint8_t));

	return nrd_max_lane_count;
}

/*
 * Read all New BW alloc configuration ex: estimated_bw, allocated_bw,
 * granuality, Driver_ID, CM_Group, & populate the BW allocation structs
 * for host router and dpia
 */
static void retrieve_usb4_dp_bw_allocation_info(struct dc_link *link)
{
	reset_bw_alloc_struct(link);

	/* init the known values */
	link->dpia_bw_alloc_config.bw_granularity = get_bw_granularity(link);
	link->dpia_bw_alloc_config.estimated_bw = get_estimated_bw(link);
	link->dpia_bw_alloc_config.nrd_max_link_rate = get_non_reduced_max_link_rate(link);
	link->dpia_bw_alloc_config.nrd_max_lane_count = get_non_reduced_max_lane_count(link);

	DC_LOG_DEBUG("%s: bw_granularity(%d), estimated_bw(%d)\n",
		__func__, link->dpia_bw_alloc_config.bw_granularity,
		link->dpia_bw_alloc_config.estimated_bw);
	DC_LOG_DEBUG("%s: nrd_max_link_rate(%d), nrd_max_lane_count(%d)\n",
		__func__, link->dpia_bw_alloc_config.nrd_max_link_rate,
		link->dpia_bw_alloc_config.nrd_max_lane_count);
}

/*
 * Cleanup function for when the dpia is unplugged to reset struct
 * and perform any required clean up
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: none
 */
static void dpia_bw_alloc_unplug(struct dc_link *link)
{
	if (link) {
		DC_LOG_DEBUG("%s: resetting BW alloc config for link(%d)\n",
			__func__, link->link_index);
		reset_bw_alloc_struct(link);
	}
}

static void link_dpia_send_bw_alloc_request(struct dc_link *link, int req_bw)
{
	uint8_t request_reg_val;
	uint32_t temp, request_bw;

	if (link->dpia_bw_alloc_config.bw_granularity == 0) {
		DC_LOG_ERROR("%s:  Link[%d]:  bw_granularity is zero!", __func__, link->link_index);
		return;
	}

	temp = req_bw * link->dpia_bw_alloc_config.bw_granularity;
	request_reg_val = temp / Kbps_TO_Gbps;

	/* Always make sure to add more to account for floating points */
	if (temp % Kbps_TO_Gbps)
		++request_reg_val;

	request_bw = request_reg_val * (Kbps_TO_Gbps / link->dpia_bw_alloc_config.bw_granularity);

	if (request_bw > link->dpia_bw_alloc_config.estimated_bw) {
		DC_LOG_ERROR("%s:  Link[%d]:  Request BW (%d --> %d) > Estimated BW (%d)... Set to Estimated BW!",
				__func__, link->link_index,
				req_bw, request_bw, link->dpia_bw_alloc_config.estimated_bw);
		req_bw = link->dpia_bw_alloc_config.estimated_bw;

		temp = req_bw * link->dpia_bw_alloc_config.bw_granularity;
		request_reg_val = temp / Kbps_TO_Gbps;
		if (temp % Kbps_TO_Gbps)
			++request_reg_val;
	}

	link->dpia_bw_alloc_config.allocated_bw = request_bw;
	DC_LOG_DC("%s:  Link[%d]:  Request BW:  %d", __func__, link->link_index, request_bw);

	core_link_write_dpcd(link, REQUESTED_BW,
		&request_reg_val,
		sizeof(uint8_t));
}

// ------------------------------------------------------------------
// PUBLIC FUNCTIONS
// ------------------------------------------------------------------
bool link_dpia_enable_usb4_dp_bw_alloc_mode(struct dc_link *link)
{
	bool ret = false;
	uint8_t val;

	if (link->dc->debug.dpia_debug.bits.enable_bw_allocation_mode == false) {
		DC_LOG_DEBUG("%s:  link[%d] DPTX BW allocation mode disabled", __func__, link->link_index);
		return false;
	}

	val = DPTX_BW_ALLOC_MODE_ENABLE | DPTX_BW_ALLOC_UNMASK_IRQ;

	if (core_link_write_dpcd(link, DPTX_BW_ALLOCATION_MODE_CONTROL, &val, sizeof(uint8_t)) == DC_OK) {
		DC_LOG_DEBUG("%s:  link[%d] DPTX BW allocation mode enabled", __func__, link->link_index);

		retrieve_usb4_dp_bw_allocation_info(link);

		if (
				link->dpia_bw_alloc_config.nrd_max_link_rate
				&& link->dpia_bw_alloc_config.nrd_max_lane_count) {
			link->reported_link_cap.link_rate = link->dpia_bw_alloc_config.nrd_max_link_rate;
			link->reported_link_cap.lane_count = link->dpia_bw_alloc_config.nrd_max_lane_count;
		}

		link->dpia_bw_alloc_config.bw_alloc_enabled = true;
		ret = true;

		if (link->dc->debug.dpia_debug.bits.enable_usb4_bw_zero_alloc_patch) {
			/*
			 * During DP tunnel creation, the CM preallocates BW
			 * and reduces the estimated BW of other DPIAs.
			 * The CM releases the preallocation only when the allocation is complete.
			 * Perform a zero allocation to make the CM release the preallocation
			 * and correctly update the estimated BW for all DPIAs per host router.
			 */
			link_dp_dpia_allocate_usb4_bandwidth_for_stream(link, 0);
		}
	} else
		DC_LOG_DEBUG("%s:  link[%d] failed to enable DPTX BW allocation mode", __func__, link->link_index);

	return ret;
}

/*
 * Handle DP BW allocation status register
 *
 * @link: pointer to the dc_link struct instance
 * @status: content of DP tunneling status DPCD register
 *
 * return: none
 */
void link_dp_dpia_handle_bw_alloc_status(struct dc_link *link, uint8_t status)
{
	link->dpia_bw_alloc_config.estimated_bw = get_estimated_bw(link);

	if (status & DP_TUNNELING_BW_REQUEST_SUCCEEDED) {
		DC_LOG_DEBUG("%s: BW Allocation request succeeded on link(%d)",
				__func__, link->link_index);
	} else if (status & DP_TUNNELING_BW_REQUEST_FAILED) {
		DC_LOG_DEBUG("%s: BW Allocation request failed on link(%d)  allocated/estimated BW=%d",
				__func__, link->link_index, link->dpia_bw_alloc_config.estimated_bw);

		link_dpia_send_bw_alloc_request(link, link->dpia_bw_alloc_config.estimated_bw);
	} else if (status & DP_TUNNELING_ESTIMATED_BW_CHANGED) {
		DC_LOG_DEBUG("%s: Estimated BW changed on link(%d)  new estimated BW=%d",
				__func__, link->link_index, link->dpia_bw_alloc_config.estimated_bw);
	}

	core_link_write_dpcd(
		link, DP_TUNNELING_STATUS,
		&status, sizeof(status));
}

/*
 * Handle the DP Bandwidth allocation for DPIA
 *
 */
void dpia_handle_usb4_bandwidth_allocation_for_link(struct dc_link *link, int peak_bw)
{
	if (link && link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.bits.dp_tunneling
			&& link->dpia_bw_alloc_config.bw_alloc_enabled) {
		if (peak_bw > 0) {
			// If DP over USB4 then we need to check BW allocation
			link->dpia_bw_alloc_config.link_max_bw = peak_bw;

			link_dpia_send_bw_alloc_request(link, peak_bw);
		} else
			dpia_bw_alloc_unplug(link);
	}
}

void link_dp_dpia_allocate_usb4_bandwidth_for_stream(struct dc_link *link, int req_bw)
{
	link->dpia_bw_alloc_config.estimated_bw = get_estimated_bw(link);

	DC_LOG_DEBUG("%s: ENTER: link[%d] hpd(%d)  Allocated_BW: %d  Estimated_BW: %d  Req_BW: %d",
		__func__, link->link_index, link->hpd_status,
		link->dpia_bw_alloc_config.allocated_bw,
		link->dpia_bw_alloc_config.estimated_bw,
		req_bw);

	if (link_dp_is_bw_alloc_available(link))
		link_dpia_send_bw_alloc_request(link, req_bw);
	else
		DC_LOG_DEBUG("%s:  BW Allocation mode not available", __func__);
}

uint32_t link_dpia_get_dp_overhead(const struct dc_link *link)
{
	uint32_t link_dp_overhead = 0;

	if ((link->type == dc_connection_mst_branch) &&
				!link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED) {
		/* For 8b/10b encoding: MTP is 64 time slots long, slot 0 is used for MTPH
		 * MST overhead is 1/64 of link bandwidth (excluding any overhead)
		 */
		const struct dc_link_settings *link_cap = dc_link_get_link_cap(link);

		if (link_cap) {
			uint32_t link_bw_in_kbps = (uint32_t)link_cap->link_rate *
					   (uint32_t)link_cap->lane_count *
					   LINK_RATE_REF_FREQ_IN_KHZ * 8;
			link_dp_overhead = (link_bw_in_kbps / MST_TIME_SLOT_COUNT)
						+ ((link_bw_in_kbps % MST_TIME_SLOT_COUNT) ? 1 : 0);
		}
	}

	return link_dp_overhead;
}

/*
 * Aggregates the DPIA bandwidth usage for the respective USB4 Router.
 * And then validate if the required bandwidth is within the router's capacity.
 *
 * @dc_validation_dpia_set: pointer to the dc_validation_dpia_set
 * @count: number of DPIA validation sets
 *
 * return: true if validation is succeeded
 */
bool link_dpia_validate_dp_tunnel_bandwidth(const struct dc_validation_dpia_set *dpia_link_sets, uint8_t count)
{
	uint32_t granularity_Gbps;
	const struct dc_link *link;
	uint32_t link_bw_granularity;
	uint32_t link_required_bw;
	struct usb4_router_validation_set router_sets[MAX_HOST_ROUTERS_NUM] = { 0 };
	uint8_t i;
	bool is_success = true;
	uint8_t router_count = 0;

	if ((dpia_link_sets == NULL) || (count == 0))
		return is_success;

	// Iterate through each DP tunneling link (DPIA).
	// Aggregate its bandwidth requirements onto the respective USB4 router.
	for (i = 0; i < count; i++) {
		link = dpia_link_sets[i].link;
		link_required_bw = dpia_link_sets[i].required_bw;
		const struct dc_tunnel_settings *dp_tunnel_settings = dpia_link_sets[i].tunnel_settings;

		if ((link == NULL) || (dp_tunnel_settings == NULL) || dp_tunnel_settings->bw_granularity == 0)
			break;

		if (link->type == dc_connection_mst_branch)
			link_required_bw += link_dpia_get_dp_overhead(link);

		granularity_Gbps = (Kbps_TO_Gbps / dp_tunnel_settings->bw_granularity);
		link_bw_granularity = (link_required_bw / granularity_Gbps) * granularity_Gbps +
				((link_required_bw % granularity_Gbps) ? granularity_Gbps : 0);

		// Find or add the USB4 router associated with the current DPIA link
		for (uint8_t j = 0; j < MAX_HOST_ROUTERS_NUM; j++) {
			if (router_sets[j].is_valid == false) {
				router_sets[j].is_valid = true;
				router_sets[j].cm_id = dp_tunnel_settings->cm_id;
				router_count++;
			}

			if (router_sets[j].cm_id == dp_tunnel_settings->cm_id) {
				uint32_t remaining_bw =
					dp_tunnel_settings->estimated_bw - dp_tunnel_settings->allocated_bw;

				router_sets[j].allocated_bw += dp_tunnel_settings->allocated_bw;

				if (remaining_bw > router_sets[j].remaining_bw)
					router_sets[j].remaining_bw = remaining_bw;

				// Get the max estimated BW within the same CM_ID
				if (dp_tunnel_settings->estimated_bw > router_sets[j].estimated_bw)
					router_sets[j].estimated_bw = dp_tunnel_settings->estimated_bw;

				router_sets[j].required_bw += link_bw_granularity;
				router_sets[j].dpia_count++;
				break;
			}
		}
	}

	// Validate bandwidth for each unique router found.
	for (i = 0; i < router_count; i++) {
		uint32_t total_bw = 0;

		if (router_sets[i].is_valid == false)
			break;

		// Determine the total available bandwidth for the current router based on aggregated data
		if ((router_sets[i].dpia_count == 1) || (router_sets[i].allocated_bw == 0))
			total_bw = router_sets[i].estimated_bw;
		else
			total_bw = router_sets[i].allocated_bw + router_sets[i].remaining_bw;

		if (router_sets[i].required_bw > total_bw) {
			is_success = false;
			break;
		}
	}

	return is_success;
}

