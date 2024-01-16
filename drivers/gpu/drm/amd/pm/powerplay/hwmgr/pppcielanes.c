/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#include <linux/types.h>
#include "atom-types.h"
#include "atombios.h"
#include "pppcielanes.h"

/** \file
 * Functions related to PCIe lane changes.
 */

/* For converting from number of lanes to lane bits.  */
static const unsigned char pp_r600_encode_lanes[] = {
	0,          /*  0 Not Supported  */
	1,          /*  1 Lane  */
	2,          /*  2 Lanes  */
	0,          /*  3 Not Supported  */
	3,          /*  4 Lanes  */
	0,          /*  5 Not Supported  */
	0,          /*  6 Not Supported  */
	0,          /*  7 Not Supported  */
	4,          /*  8 Lanes  */
	0,          /*  9 Not Supported  */
	0,          /* 10 Not Supported  */
	0,          /* 11 Not Supported  */
	5,          /* 12 Lanes (Not actually supported)  */
	0,          /* 13 Not Supported  */
	0,          /* 14 Not Supported  */
	0,          /* 15 Not Supported  */
	6           /* 16 Lanes  */
};

static const unsigned char pp_r600_decoded_lanes[8] = { 16, 1, 2, 4, 8, 12, 16, };

uint8_t encode_pcie_lane_width(uint32_t num_lanes)
{
	return pp_r600_encode_lanes[num_lanes];
}

uint8_t decode_pcie_lane_width(uint32_t num_lanes)
{
	return pp_r600_decoded_lanes[num_lanes];
}
