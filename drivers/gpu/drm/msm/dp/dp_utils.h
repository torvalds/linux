/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_UTILS_H_
#define _DP_UTILS_H_

#define HEADER_BYTE_0_BIT	 0
#define PARITY_BYTE_0_BIT	 8
#define HEADER_BYTE_1_BIT	16
#define PARITY_BYTE_1_BIT	24
#define HEADER_BYTE_2_BIT	 0
#define PARITY_BYTE_2_BIT	 8
#define HEADER_BYTE_3_BIT	16
#define PARITY_BYTE_3_BIT	24

u8 dp_utils_get_g0_value(u8 data);
u8 dp_utils_get_g1_value(u8 data);
u8 dp_utils_calculate_parity(u32 data);

#endif /* _DP_UTILS_H_ */
