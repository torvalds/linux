/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IRIS_PLATFORM_MILOS_H__
#define __IRIS_PLATFORM_MILOS_H__

#define MILOS_MAXIMUM_FPS	240

static const struct icc_info iris_icc_info_milos[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 10000000 },
};

static const char * const milos_opp_pd_table[] = { "cx", "mx" };

static struct platform_inst_caps platform_inst_cap_milos = {
	.min_frame_width = 96,
	.max_frame_width = 4096,
	.min_frame_height = 96,
	.max_frame_height = 4096,
	.max_mbpf = (4096 * 2176) / 256,
	.mb_cycles_vsp = 25,
	.mb_cycles_vpp = 200,
	.max_frame_rate = MILOS_MAXIMUM_FPS,
	.max_operating_rate = MILOS_MAXIMUM_FPS,
};

#endif
