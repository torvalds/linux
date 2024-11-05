/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>

#include "dp_utils.h"

#define DP_SDP_HEADER_SIZE		8

u8 msm_dp_utils_get_g0_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[3];
	g[1] = c[0] ^ c[3];
	g[2] = c[1];
	g[3] = c[2];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

u8 msm_dp_utils_get_g1_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[0] ^ c[3];
	g[1] = c[0] ^ c[1] ^ c[3];
	g[2] = c[1] ^ c[2];
	g[3] = c[2] ^ c[3];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

u8 msm_dp_utils_calculate_parity(u32 data)
{
	u8 x0 = 0;
	u8 x1 = 0;
	u8 ci = 0;
	u8 iData = 0;
	u8 i = 0;
	u8 parity_byte;
	u8 num_byte = (data & 0xFF00) > 0 ? 8 : 2;

	for (i = 0; i < num_byte; i++) {
		iData = (data >> i * 4) & 0xF;

		ci = iData ^ x1;
		x1 = x0 ^ msm_dp_utils_get_g1_value(ci);
		x0 = msm_dp_utils_get_g0_value(ci);
	}

	parity_byte = x1 | (x0 << 4);

	return parity_byte;
}

ssize_t msm_dp_utils_pack_sdp_header(struct dp_sdp_header *sdp_header, u32 *header_buff)
{
	size_t length;

	length = sizeof(header_buff);
	if (length < DP_SDP_HEADER_SIZE)
		return -ENOSPC;

	header_buff[0] = FIELD_PREP(HEADER_0_MASK, sdp_header->HB0) |
		FIELD_PREP(PARITY_0_MASK, msm_dp_utils_calculate_parity(sdp_header->HB0)) |
		FIELD_PREP(HEADER_1_MASK, sdp_header->HB1) |
		FIELD_PREP(PARITY_1_MASK, msm_dp_utils_calculate_parity(sdp_header->HB1));

	header_buff[1] = FIELD_PREP(HEADER_2_MASK, sdp_header->HB2) |
		FIELD_PREP(PARITY_2_MASK, msm_dp_utils_calculate_parity(sdp_header->HB2)) |
		FIELD_PREP(HEADER_3_MASK, sdp_header->HB3) |
		FIELD_PREP(PARITY_3_MASK, msm_dp_utils_calculate_parity(sdp_header->HB3));

	return length;
}
