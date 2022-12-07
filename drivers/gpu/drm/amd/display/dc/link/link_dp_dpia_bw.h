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
bool set_dptx_usb4_bw_alloc_support(struct dc_link *link);

/*
 * Send a request from DP-Tx requesting to allocate BW remotely after
 * allocating it locally. This will get processed by CM and a CB function
 * will be called.
 *
 * @link: pointer to the dc_link struct instance
 * @req_bw: The requested bw in Kbyte to allocated
 *
 * return: none
 */
void set_usb4_req_bw_req(struct dc_link *link, int req_bw);

/*
 * CB function for when the status of the Req above is complete. We will
 * find out the result of allocating on CM and update structs accordingly
 *
 * @link: pointer to the dc_link struct instance
 * @bw: Allocated or Estimated BW depending on the result
 * @result: Response type
 *
 * return: none
 */
void get_usb4_req_bw_resp(struct dc_link *link, uint8_t bw, uint8_t result);

/*
 * Return the response_ready flag from dc_link struct
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: response_ready flag from dc_link struct
 */
bool get_cm_response_ready_flag(struct dc_link *link);

/*
 * Get the Max Available BW or Max Estimated BW for each Host Router
 *
 * @link: pointer to the dc_link struct instance
 * @type: ESTIMATD BW or MAX AVAILABLE BW
 *
 * return: response_ready flag from dc_link struct
 */
int get_host_router_total_bw(struct dc_link *link, uint8_t type);

/*
 * Cleanup function for when the dpia is unplugged to reset struct
 * and perform any required clean up
 *
 * @link: pointer to the dc_link struct instance
 *
 * return: none
 */
bool dpia_bw_alloc_unplug(struct dc_link *link);

#endif /* DC_INC_LINK_DP_DPIA_BW_H_ */
