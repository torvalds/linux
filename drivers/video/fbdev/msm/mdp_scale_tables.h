/* drivers/video/msm_fb/mdp_scale_tables.h
 *
 * Copyright (C) 2007 QUALCOMM Incorporated
 * Copyright (C) 2007 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _MDP_SCALE_TABLES_H_
#define _MDP_SCALE_TABLES_H_

#include <linux/types.h>
struct mdp_table_entry {
	uint32_t reg;
	uint32_t val;
};

extern struct mdp_table_entry mdp_upscale_table[64];

enum {
	MDP_DOWNSCALE_PT2TOPT4,
	MDP_DOWNSCALE_PT4TOPT6,
	MDP_DOWNSCALE_PT6TOPT8,
	MDP_DOWNSCALE_PT8TO1,
	MDP_DOWNSCALE_MAX,
};

extern struct mdp_table_entry *mdp_downscale_x_table[MDP_DOWNSCALE_MAX];
extern struct mdp_table_entry *mdp_downscale_y_table[MDP_DOWNSCALE_MAX];
extern struct mdp_table_entry mdp_gaussian_blur_table[];

#endif
