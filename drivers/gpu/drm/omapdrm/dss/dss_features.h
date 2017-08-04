/*
 * linux/drivers/video/omap2/dss/dss_features.h
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Archit Taneja <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAP2_DSS_FEATURES_H
#define __OMAP2_DSS_FEATURES_H

#define MAX_DSS_MANAGERS	4
#define MAX_DSS_OVERLAYS	4
#define MAX_DSS_LCD_MANAGERS	3
#define MAX_NUM_DSI		2

enum dss_range_param {
	FEAT_PARAM_DSS_FCK,
	FEAT_PARAM_DSS_PCD,
	FEAT_PARAM_DSIPLL_LPDIV,
	FEAT_PARAM_DSI_FCK,
	FEAT_PARAM_DOWNSCALE,
	FEAT_PARAM_LINEWIDTH,
};

/* DSS Feature Functions */
unsigned long dss_feat_get_param_min(enum dss_range_param param);
unsigned long dss_feat_get_param_max(enum dss_range_param param);

void dss_features_init(enum omapdss_version version);

enum omap_dss_output_id dss_feat_get_supported_outputs(enum omap_channel channel);

#endif
