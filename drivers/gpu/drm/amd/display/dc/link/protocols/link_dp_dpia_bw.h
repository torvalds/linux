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

#include "link_service.h"


/*
 * Host Router BW type
 */
enum bw_type {
	HOST_ROUTER_BW_ESTIMATED,
	HOST_ROUTER_BW_ALLOCATED,
	HOST_ROUTER_BW_INVALID,
};

struct usb4_router_validation_set {
	bool is_valid;
	uint8_t cm_id;
	uint8_t dpia_count;
	uint32_t required_bw;
	uint32_t allocated_bw;
	uint32_t estimated_bw;
	uint32_t remaining_bw;
};

/*
 * Enable USB4 DP BW allocation mode
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: SUCCESS or FAILURE
 */
bool link_dpia_enable_usb4_dp_bw_alloc_mode(struct dc_link *link);

/*
 * Allocates only what the stream needs for bw, so if:
 * If (stream_req_bw < or > already_allocated_bw_at_HPD)
 * => Deallocate Max Bw & then allocate only what the stream needs
 *
 * @link: pointer to the dc_link struct instance
 * @req_bw: Bw requested by the stream
 *
 */
void link_dp_dpia_allocate_usb4_bandwidth_for_stream(struct dc_link *link, int req_bw);

/*
 * Handle the USB4 BW Allocation related functionality here:
 * Plug => Try to allocate max bw from timing parameters supported by the sink
 * Unplug => de-allocate bw
 *
 * @link: pointer to the dc_link struct instance
 * @peak_bw: Peak bw used by the link/sink
 *
 */
void dpia_handle_usb4_bandwidth_allocation_for_link(struct dc_link *link, int peak_bw);

/*
 * Obtain all the DP overheads in dp tunneling for the dpia link
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: DP overheads in DP tunneling
 */
uint32_t link_dpia_get_dp_overhead(const struct dc_link *link);

/*
 * Handle DP BW allocation status register
 *
 * @link: pointer to the dc_link struct instance
 * @status: content of DP tunneling status register
 *
 * return: none
 */
void link_dp_dpia_handle_bw_alloc_status(struct dc_link *link, uint8_t status);

/*
 * Aggregates the DPIA bandwidth usage for the respective USB4 Router.
 *
 * @dc_validation_dpia_set: pointer to the dc_validation_dpia_set
 * @count: number of DPIA validation sets
 *
 * return: true if validation is succeeded
 */
bool link_dpia_validate_dp_tunnel_bandwidth(const struct dc_validation_dpia_set *dpia_link_sets, uint8_t count);

#endif /* DC_INC_LINK_DP_DPIA_BW_H_ */

