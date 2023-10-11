/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, The Linux Foundation
 */

#ifndef __MSM_MDSS_H__
#define __MSM_MDSS_H__

struct msm_mdss_data {
	u32 ubwc_enc_version;
	/* can be read from register 0x58 */
	u32 ubwc_dec_version;
	u32 ubwc_swizzle;
	u32 ubwc_static;
	u32 highest_bank_bit;
	u32 macrotile_mode;
};

#define UBWC_1_0 0x10000000
#define UBWC_2_0 0x20000000
#define UBWC_3_0 0x30000000
#define UBWC_4_0 0x40000000
#define UBWC_4_3 0x40030000

const struct msm_mdss_data *msm_mdss_get_mdss_data(struct device *dev);

#endif /* __MSM_MDSS_H__ */
