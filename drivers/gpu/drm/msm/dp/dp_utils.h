/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_UTILS_H_
#define _DP_UTILS_H_

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <drm/display/drm_dp_helper.h>

#define HEADER_BYTE_0_BIT	 0
#define PARITY_BYTE_0_BIT	 8
#define HEADER_BYTE_1_BIT	16
#define PARITY_BYTE_1_BIT	24
#define HEADER_BYTE_2_BIT	 0
#define PARITY_BYTE_2_BIT	 8
#define HEADER_BYTE_3_BIT	16
#define PARITY_BYTE_3_BIT	24

#define HEADER_0_MASK	    GENMASK(7, 0)
#define PARITY_0_MASK	    GENMASK(15, 8)
#define HEADER_1_MASK	    GENMASK(23, 16)
#define PARITY_1_MASK	    GENMASK(31, 24)
#define HEADER_2_MASK	    GENMASK(7, 0)
#define PARITY_2_MASK	    GENMASK(15, 8)
#define HEADER_3_MASK	    GENMASK(23, 16)
#define PARITY_3_MASK	    GENMASK(31, 24)

u8 dp_utils_get_g0_value(u8 data);
u8 dp_utils_get_g1_value(u8 data);
u8 dp_utils_calculate_parity(u32 data);
ssize_t dp_utils_pack_sdp_header(struct dp_sdp_header *sdp_header, u32 *header_buff);

#endif /* _DP_UTILS_H_ */
