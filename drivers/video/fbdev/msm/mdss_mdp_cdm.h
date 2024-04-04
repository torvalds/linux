/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014,2016, 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef MDSS_MDP_CDM_H
#define MDSS_MDP_CDM_H

#include <linux/msm_mdp.h>
#include <linux/kref.h>

enum mdp_cdm_cdwn_method_type {
	MDP_CDM_CDWN_DISABLE,
	MDP_CDM_CDWN_PIXEL_DROP,
	MDP_CDM_CDWN_AVG,
	MDP_CDM_CDWN_COSITE,
	MDP_CDM_CDWN_OFFSITE,
};

enum mdp_cdm_cdwn_output_type {
	MDP_CDM_CDWN_OUTPUT_HDMI,
	MDP_CDM_CDWN_OUTPUT_WB,
};

enum mdp_cdm_csc_bit_depth {
	MDP_CDM_CSC_8BIT,
	MDP_CDM_CSC_10BIT,
};

struct mdp_cdm_cfg {
	/* CSC block configuration */
	u32 mdp_csc_bit_depth;
	u32 csc_type;
	/* CDWN block configuration */
	u32 horz_downsampling_type;
	u32 vert_downsampling_type;
	/* Output packer configuration */
	u32 output_width;
	u32 output_height;
	u32 out_format;
};

struct mdss_mdp_cdm {
	u32 num;
	char __iomem *base;
	struct kref kref;
	struct mutex lock;

	struct mdss_data_type *mdata;
	u32 out_intf;
	bool is_bypassed;
	struct mdp_cdm_cfg setup;
	struct completion free_comp;
};

struct mdss_mdp_cdm *mdss_mdp_cdm_init(struct mdss_mdp_ctl *ctl,
				       u32 intf_type);
int mdss_mdp_cdm_destroy(struct mdss_mdp_cdm *cdm);
int mdss_mdp_cdm_setup(struct mdss_mdp_cdm *cdm, struct mdp_cdm_cfg *data);

#endif /* MDSS_MDP_CDM_H */
