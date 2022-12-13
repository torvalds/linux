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

// XXX: TODO: Re-add for Phase 2
/* Number of Host Routers per motherboard is 2 and 2 DPIA per host router */
#define MAX_HR_NUM 2

struct dc_host_router_bw_alloc {
	int max_bw[MAX_HR_NUM];             // The Max BW that each Host Router has available to be shared btw DPIAs
	int total_estimated_bw[MAX_HR_NUM]; // The Total Verified and available BW that Host Router has
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
 *
 * return: none
 */
void get_usb4_req_bw_resp(struct dc_link *link);

#endif /* DC_INC_LINK_DP_DPIA_BW_H_ */
