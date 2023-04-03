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

#ifndef DC_INC_LINK_DP_DPIA_BW_H_
#define DC_INC_LINK_DP_DPIA_BW_H_

#include "link.h"

/* Number of Host Routers per motherboard is 2 */
#define MAX_HR_NUM			2
/* Number of DPIA per host router is 2 */
#define MAX_DPIA_NUM		(MAX_HR_NUM * 2)

/*
 * Host Router BW type
 */
enum bw_type {
	HOST_ROUTER_BW_ESTIMATED,
	HOST_ROUTER_BW_ALLOCATED,
	HOST_ROUTER_BW_INVALID,
};

/*
 * Enable BW Allocation Mode Support from the DP-Tx side
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: SUCCESS or FAILURE
 */
bool link_dp_dpia_set_dptx_usb4_bw_alloc_support(struct dc_link *link);

/*
 * Allocates only what the stream needs for bw, so if:
 * If (stream_req_bw < or > already_allocated_bw_at_HPD)
 * => Deallocate Max Bw & then allocate only what the stream needs
 *
 * @link: pointer to the dc_link struct instance
 * @req_bw: Bw requested by the stream
 *
 * return: allocated bw else return 0
 */
int link_dp_dpia_allocate_usb4_bandwidth_for_stream(struct dc_link *link, int req_bw);

/*
 * Handle the USB4 BW Allocation related functionality here:
 * Plug => Try to allocate max bw from timing parameters supported by the sink
 * Unplug => de-allocate bw
 *
 * @link: pointer to the dc_link struct instance
 * @peak_bw: Peak bw used by the link/sink
 *
 * return: allocated bw else return 0
 */
int dpia_handle_usb4_bandwidth_allocation_for_link(struct dc_link *link, int peak_bw);

/*
 * Handle function for when the status of the Request above is complete.
 * We will find out the result of allocating on CM and update structs.
 *
 * @link: pointer to the dc_link struct instance
 * @bw: Allocated or Estimated BW depending on the result
 * @result: Response type
 *
 * return: none
 */
void dpia_handle_bw_alloc_response(struct dc_link *link, uint8_t bw, uint8_t result);

/*
 * Handle the validation of total BW here and confirm that the bw used by each
 * DPIA doesn't exceed available BW for each host router (HR)
 *
 * @link[]: array of link pointer to all possible DPIA links
 * @bw_needed[]: bw needed for each DPIA link based on timing
 * @num_dpias: Number of DPIAs for the above 2 arrays. Should always be <= MAX_DPIA_NUM
 *
 * return: TRUE if bw used by DPIAs doesn't exceed available BW else return FALSE
 */
bool dpia_validate_usb4_bw(struct dc_link **link, int *bw_needed, const unsigned int num_dpias);

#endif /* DC_INC_LINK_DP_DPIA_BW_H_ */
