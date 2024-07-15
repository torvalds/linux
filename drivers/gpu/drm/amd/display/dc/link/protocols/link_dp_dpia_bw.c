
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
//				USB4 DPIA BANDWIDTH ALLOCATION LOGIC
/*********************************************************************/
#include "link_dp_dpia_bw.h"
#include "link_dpcd.h"
#include "dc_dmub_srv.h"

#define DC_LOGGER \
	link->ctx->logger

#define Kbps_TO_Gbps (1000 * 1000)

// ------------------------------------------------------------------
//					PRIVATE FUNCTIONS
// ------------------------------------------------------------------
/*
 * Always Check the following:
 *  - Is it USB4 link?
 *  - Is HPD HIGH?
 *  - Is BW Allocation Support Mode enabled on DP-Tx?
 */
static bool get_bw_alloc_proceed_flag(struct dc_link *tmp)
{
	return (tmp && DISPLAY_ENDPOINT_USB4_DPIA == tmp->ep_type
			&& tmp->hpd_status
			&& tmp->dpia_bw_alloc_config.bw_alloc_enabled);
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
	link->dpia_bw_alloc_config.response_ready = false;
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
static void init_usb4_bw_struct(struct dc_link *link)
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

static uint8_t get_lowest_dpia_index(struct dc_link *link)
{
	const struct dc *dc_struct = link->dc;
	uint8_t idx = 0xFF;
	int i;

	for (i = 0; i < MAX_LINKS; ++i) {

		if (!dc_struct->links[i] ||
				dc_struct->links[i]->ep_type != DISPLAY_ENDPOINT_USB4_DPIA)
			continue;

		if (idx > dc_struct->links[i]->link_index) {
			idx = dc_struct->links[i]->link_index;
			break;
		}
	}

	return idx;
}

/*
 * Get the maximum dp tunnel banwidth of host router
 *
 * @dc: pointer to the dc struct instance
 * @hr_index: host router index
 *
 * return: host router maximum dp tunnel bandwidth
 */
static int get_host_router_total_dp_tunnel_bw(const struct dc *dc, uint8_t hr_index)
{
	uint8_t lowest_dpia_index = get_lowest_dpia_index(dc->links[0]);
	uint8_t hr_index_temp = 0;
	struct dc_link *link_dpia_primary, *link_dpia_secondary;
	int total_bw = 0;

	for (uint8_t i = 0; i < MAX_LINKS - 1; ++i) {

		if (!dc->links[i] || dc->links[i]->ep_type != DISPLAY_ENDPOINT_USB4_DPIA)
			continue;

		hr_index_temp = (dc->links[i]->link_index - lowest_dpia_index) / 2;

		if (hr_index_temp == hr_index) {
			link_dpia_primary = dc->links[i];
			link_dpia_secondary = dc->links[i + 1];

			/**
			 * If BW allocation enabled on both DPIAs, then
			 * HR BW = Estimated(dpia_primary) + Allocated(dpia_secondary)
			 * otherwise HR BW = Estimated(bw alloc enabled dpia)
			 */
			if ((link_dpia_primary->hpd_status &&
				link_dpia_primary->dpia_bw_alloc_config.bw_alloc_enabled) &&
				(link_dpia_secondary->hpd_status &&
				link_dpia_secondary->dpia_bw_alloc_config.bw_alloc_enabled)) {
					total_bw += link_dpia_primary->dpia_bw_alloc_config.estimated_bw +
						link_dpia_secondary->dpia_bw_alloc_config.allocated_bw;
			} else if (link_dpia_primary->hpd_status &&
					link_dpia_primary->dpia_bw_alloc_config.bw_alloc_enabled) {
				total_bw = link_dpia_primary->dpia_bw_alloc_config.estimated_bw;
			} else if (link_dpia_secondary->hpd_status &&
				link_dpia_secondary->dpia_bw_alloc_config.bw_alloc_enabled) {
				total_bw += link_dpia_secondary->dpia_bw_alloc_config.estimated_bw;
			}
			break;
		}
	}

	return total_bw;
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
		DC_LOG_DEBUG("%s: resetting bw alloc config for link(%d)\n",
			__func__, link->link_index);
		reset_bw_alloc_struct(link);
	}
}

static void set_usb4_req_bw_req(struct dc_link *link, int req_bw)
{
	uint8_t requested_bw;
	uint32_t temp;

	/* Error check whether request bw greater than allocated */
	if (req_bw > link->dpia_bw_alloc_config.estimated_bw) {
		DC_LOG_ERROR("%s: Request bw greater than estimated bw for link(%d)\n",
			__func__, link->link_index);
		req_bw = link->dpia_bw_alloc_config.estimated_bw;
	}

	temp = req_bw * link->dpia_bw_alloc_config.bw_granularity;
	requested_bw = temp / Kbps_TO_Gbps;

	/* Always make sure to add more to account for floating points */
	if (temp % Kbps_TO_Gbps)
		++requested_bw;

	/* Error check whether requested and allocated are equal */
	req_bw = requested_bw * (Kbps_TO_Gbps / link->dpia_bw_alloc_config.bw_granularity);
	if (req_bw && (req_bw == link->dpia_bw_alloc_config.allocated_bw)) {
		DC_LOG_ERROR("%s: Request bw equals to allocated bw for link(%d)\n",
			__func__, link->link_index);
	}

	link->dpia_bw_alloc_config.response_ready = false; // Reset flag
	core_link_write_dpcd(
		link,
		REQUESTED_BW,
		&requested_bw,
		sizeof(uint8_t));
}

/*
 * Return the response_ready flag from dc_link struct
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: response_ready flag from dc_link struct
 */
static bool get_cm_response_ready_flag(struct dc_link *link)
{
	return link->dpia_bw_alloc_config.response_ready;
}

// ------------------------------------------------------------------
//					PUBLIC FUNCTIONS
// ------------------------------------------------------------------
bool link_dp_dpia_set_dptx_usb4_bw_alloc_support(struct dc_link *link)
{
	bool ret = false;
	uint8_t response = 0,
			bw_support_dpia = 0,
			bw_support_cm = 0;

	if (!(link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA && link->hpd_status))
		goto out;

	if (core_link_read_dpcd(
			link,
			DP_TUNNELING_CAPABILITIES,
			&response,
			sizeof(uint8_t)) == DC_OK)
		bw_support_dpia = (response >> 7) & 1;

	if (core_link_read_dpcd(
		link,
		USB4_DRIVER_BW_CAPABILITY,
		&response,
		sizeof(uint8_t)) == DC_OK)
		bw_support_cm = (response >> 7) & 1;

	/* Send request acknowledgment to Turn ON DPTX support */
	if (bw_support_cm && bw_support_dpia) {

		response = 0x80;
		if (core_link_write_dpcd(
				link,
				DPTX_BW_ALLOCATION_MODE_CONTROL,
				&response,
				sizeof(uint8_t)) != DC_OK) {
			DC_LOG_DEBUG("%s: FAILURE Enabling DPtx BW Allocation Mode Support for link(%d)\n",
				__func__, link->link_index);
		} else {
			// SUCCESS Enabled DPtx BW Allocation Mode Support
			DC_LOG_DEBUG("%s: SUCCESS Enabling DPtx BW Allocation Mode Support for link(%d)\n",
				__func__, link->link_index);

			ret = true;
			init_usb4_bw_struct(link);
			link->dpia_bw_alloc_config.bw_alloc_enabled = true;

			/*
			 * During DP tunnel creation, CM preallocates BW and reduces estimated BW of other
			 * DPIA. CM release preallocation only when allocation is complete. Do zero alloc
			 * to make the CM to release preallocation and update estimated BW correctly for
			 * all DPIAs per host router
			 */
			link_dp_dpia_allocate_usb4_bandwidth_for_stream(link, 0);
		}
	}

out:
	return ret;
}

void dpia_handle_bw_alloc_response(struct dc_link *link, uint8_t bw, uint8_t result)
{
	int bw_needed = 0;
	int estimated = 0;

	if (!get_bw_alloc_proceed_flag((link)))
		return;

	switch (result) {

	case DPIA_BW_REQ_FAILED:

		/*
		 * Ideally, we shouldn't run into this case as we always validate available
		 * bandwidth and request within that limit
		 */
		estimated = bw * (Kbps_TO_Gbps / link->dpia_bw_alloc_config.bw_granularity);

		DC_LOG_ERROR("%s: BW REQ FAILURE for DP-TX Request for link(%d)\n",
			__func__, link->link_index);
		DC_LOG_ERROR("%s: current estimated_bw(%d), new estimated_bw(%d)\n",
			__func__, link->dpia_bw_alloc_config.estimated_bw, estimated);

		/* Update the new Estimated BW value updated by CM */
		link->dpia_bw_alloc_config.estimated_bw = estimated;

		/* Allocate the previously requested bandwidth */
		set_usb4_req_bw_req(link, link->dpia_bw_alloc_config.estimated_bw);

		/*
		 * If FAIL then it is either:
		 * 1. Due to DP-Tx trying to allocate more than available i.e. it failed locally
		 *    => get estimated and allocate that
		 * 2. Due to the fact that DP-Tx tried to allocated ESTIMATED BW and failed then
		 *    CM will have to update 0xE0023 with new ESTIMATED BW value.
		 */
		break;

	case DPIA_BW_REQ_SUCCESS:

		bw_needed = bw * (Kbps_TO_Gbps / link->dpia_bw_alloc_config.bw_granularity);

		DC_LOG_DEBUG("%s: BW REQ SUCCESS for DP-TX Request for link(%d)\n",
			__func__, link->link_index);
		DC_LOG_DEBUG("%s: current allocated_bw(%d), new allocated_bw(%d)\n",
			__func__, link->dpia_bw_alloc_config.allocated_bw, bw_needed);

		link->dpia_bw_alloc_config.allocated_bw = bw_needed;

		link->dpia_bw_alloc_config.response_ready = true;
		break;

	case DPIA_EST_BW_CHANGED:

		estimated = bw * (Kbps_TO_Gbps / link->dpia_bw_alloc_config.bw_granularity);

		DC_LOG_DEBUG("%s: ESTIMATED BW CHANGED for link(%d)\n",
			__func__, link->link_index);
		DC_LOG_DEBUG("%s: current estimated_bw(%d), new estimated_bw(%d)\n",
			__func__, link->dpia_bw_alloc_config.estimated_bw, estimated);

		link->dpia_bw_alloc_config.estimated_bw = estimated;
		break;

	case DPIA_BW_ALLOC_CAPS_CHANGED:

		DC_LOG_ERROR("%s: BW ALLOC CAPABILITY CHANGED to Disabled for link(%d)\n",
			__func__, link->link_index);
		link->dpia_bw_alloc_config.bw_alloc_enabled = false;
		break;
	}
}
int dpia_handle_usb4_bandwidth_allocation_for_link(struct dc_link *link, int peak_bw)
{
	int ret = 0;
	uint8_t timeout = 10;

	if (!(link && DISPLAY_ENDPOINT_USB4_DPIA == link->ep_type
			&& link->dpia_bw_alloc_config.bw_alloc_enabled))
		goto out;

	//1. Hot Plug
	if (link->hpd_status && peak_bw > 0) {

		// If DP over USB4 then we need to check BW allocation
		link->dpia_bw_alloc_config.link_max_bw = peak_bw;
		set_usb4_req_bw_req(link, link->dpia_bw_alloc_config.link_max_bw);

		do {
			if (timeout > 0)
				timeout--;
			else
				break;
			msleep(10);
		} while (!get_cm_response_ready_flag(link));

		if (!timeout)
			ret = 0;// ERROR TIMEOUT waiting for response for allocating bw
		else if (link->dpia_bw_alloc_config.allocated_bw > 0)
			ret = link->dpia_bw_alloc_config.allocated_bw;
	}
	//2. Cold Unplug
	else if (!link->hpd_status)
		dpia_bw_alloc_unplug(link);

out:
	return ret;
}
bool link_dp_dpia_allocate_usb4_bandwidth_for_stream(struct dc_link *link, int req_bw)
{
	bool ret = false;
	uint8_t timeout = 10;

	DC_LOG_DEBUG("%s: ENTER: link(%d), hpd_status(%d), current allocated_bw(%d), req_bw(%d)\n",
		__func__, link->link_index, link->hpd_status,
		link->dpia_bw_alloc_config.allocated_bw, req_bw);

	if (!get_bw_alloc_proceed_flag(link))
		goto out;

	set_usb4_req_bw_req(link, req_bw);
	do {
		if (timeout > 0)
			timeout--;
		else
			break;
		msleep(10);
	} while (!get_cm_response_ready_flag(link));

	if (timeout)
		ret = true;

out:
	DC_LOG_DEBUG("%s: EXIT: timeout(%d), ret(%d)\n", __func__, timeout, ret);
	return ret;
}

bool dpia_validate_usb4_bw(struct dc_link **link, int *bw_needed_per_dpia, const unsigned int num_dpias)
{
	bool ret = true;
	int bw_needed_per_hr[MAX_HR_NUM] = { 0, 0 }, host_router_total_dp_bw = 0;
	uint8_t lowest_dpia_index, i, hr_index;

	if (!num_dpias || num_dpias > MAX_DPIA_NUM)
		return ret;

	lowest_dpia_index = get_lowest_dpia_index(link[0]);

	/* get total Host Router BW with granularity for the given modes */
	for (i = 0; i < num_dpias; ++i) {
		int granularity_Gbps = 0;
		int bw_granularity = 0;

		if (!link[i]->dpia_bw_alloc_config.bw_alloc_enabled)
			continue;

		if (link[i]->link_index < lowest_dpia_index)
			continue;

		granularity_Gbps = (Kbps_TO_Gbps / link[i]->dpia_bw_alloc_config.bw_granularity);
		bw_granularity = (bw_needed_per_dpia[i] / granularity_Gbps) * granularity_Gbps +
				((bw_needed_per_dpia[i] % granularity_Gbps) ? granularity_Gbps : 0);

		hr_index = (link[i]->link_index - lowest_dpia_index) / 2;
		bw_needed_per_hr[hr_index] += bw_granularity;
	}

	/* validate against each Host Router max BW */
	for (hr_index = 0; hr_index < MAX_HR_NUM; ++hr_index) {
		if (bw_needed_per_hr[hr_index]) {
			host_router_total_dp_bw = get_host_router_total_dp_tunnel_bw(link[0]->dc, hr_index);
			if (bw_needed_per_hr[hr_index] > host_router_total_dp_bw) {
				ret = false;
				break;
			}
		}
	}

	return ret;
}

int link_dp_dpia_get_dp_overhead_in_dp_tunneling(struct dc_link *link)
{
	int dp_overhead = 0, link_mst_overhead = 0;

	if (!get_bw_alloc_proceed_flag((link)))
		return dp_overhead;

	/* if its mst link, add MTPH overhead */
	if ((link->type == dc_connection_mst_branch) &&
		!link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED) {
		/* For 8b/10b encoding: MTP is 64 time slots long, slot 0 is used for MTPH
		 * MST overhead is 1/64 of link bandwidth (excluding any overhead)
		 */
		const struct dc_link_settings *link_cap =
			dc_link_get_link_cap(link);
		uint32_t link_bw_in_kbps = (uint32_t)link_cap->link_rate *
					   (uint32_t)link_cap->lane_count *
					   LINK_RATE_REF_FREQ_IN_KHZ * 8;
		link_mst_overhead = (link_bw_in_kbps / 64) + ((link_bw_in_kbps % 64) ? 1 : 0);
	}

	/* add all the overheads */
	dp_overhead = link_mst_overhead;

	return dp_overhead;
}
