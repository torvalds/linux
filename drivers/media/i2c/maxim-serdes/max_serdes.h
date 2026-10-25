/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Analog Devices Inc.
 */

#ifndef MAX_SERDES_H
#define MAX_SERDES_H

#include <linux/bitfield.h>
#include <linux/types.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-subdev.h>

#define REG_SEQUENCE_2(reg, val) \
	{ (reg),     ((val) >> 8) & 0xff }, \
	{ (reg) + 1, ((val) >> 0) & 0xff }

#define REG_SEQUENCE_3(reg, val) \
	{ (reg),     ((val) >> 16) & 0xff }, \
	{ (reg) + 1, ((val) >> 8)  & 0xff }, \
	{ (reg) + 2, ((val) >> 0)  & 0xff }

#define REG_SEQUENCE_3_LE(reg, val) \
	{ (reg),     ((val) >> 0) & 0xff }, \
	{ (reg) + 1, ((val) >> 8)  & 0xff }, \
	{ (reg) + 2, ((val) >> 16)  & 0xff }

#define MAX_SERDES_PHYS_MAX		4
#define MAX_SERDES_STREAMS_NUM		4
#define MAX_SERDES_VC_ID_NUM		4
#define MAX_SERDES_TPG_STREAM		0

#define MAX_SERDES_GRAD_INCR		4
#define MAX_SERDES_CHECKER_COLOR_A	0x00ccfe
#define MAX_SERDES_CHECKER_COLOR_B	0xa76a00
#define MAX_SERDES_CHECKER_SIZE		60

extern const char * const max_serdes_tpg_patterns[];

enum max_serdes_gmsl_version {
	MAX_SERDES_GMSL_MIN,
	MAX_SERDES_GMSL_2_3GBPS = MAX_SERDES_GMSL_MIN,
	MAX_SERDES_GMSL_2_6GBPS,
	MAX_SERDES_GMSL_3_12GBPS,
	MAX_SERDES_GMSL_MAX = MAX_SERDES_GMSL_3_12GBPS,
};

enum max_serdes_gmsl_mode {
	MAX_SERDES_GMSL_PIXEL_MODE,
	MAX_SERDES_GMSL_TUNNEL_MODE,
};

enum max_serdes_tpg_pattern {
	MAX_SERDES_TPG_PATTERN_MIN,
	MAX_SERDES_TPG_PATTERN_CHECKERBOARD = MAX_SERDES_TPG_PATTERN_MIN,
	MAX_SERDES_TPG_PATTERN_GRADIENT,
	MAX_SERDES_TPG_PATTERN_MAX = MAX_SERDES_TPG_PATTERN_GRADIENT,
};

struct max_serdes_phys_config {
	unsigned int lanes[MAX_SERDES_PHYS_MAX];
	unsigned int clock_lane[MAX_SERDES_PHYS_MAX];
};

struct max_serdes_phys_configs {
	const struct max_serdes_phys_config *configs;
	unsigned int num_configs;
};

struct max_serdes_i2c_xlate {
	u8 src;
	u8 dst;
	bool en;
};

struct max_serdes_mipi_format {
	u8 dt;
	u8 bpp;
};

struct max_serdes_vc_remap {
	u8 src;
	u8 dst;
};

struct max_serdes_source {
	struct v4l2_subdev *sd;
	u16 pad;
	struct fwnode_handle *ep_fwnode;

	unsigned int index;
};

struct max_serdes_asc {
	struct v4l2_async_connection base;
	struct max_serdes_source *source;
};

struct max_serdes_tpg_entry {
	u32 width;
	u32 height;
	struct v4l2_fract interval;
	u32 code;
	u8 dt;
	u8 bpp;
};

#define MAX_TPG_ENTRY_640X480P60_RGB888 \
	{ 640, 480, { 1, 60 }, MEDIA_BUS_FMT_RGB888_1X24, MIPI_CSI2_DT_RGB888, 24 }

#define MAX_TPG_ENTRY_1920X1080P30_RGB888 \
	{ 1920, 1080, { 1, 30 }, MEDIA_BUS_FMT_RGB888_1X24, MIPI_CSI2_DT_RGB888, 24 }

#define MAX_TPG_ENTRY_1920X1080P60_RGB888 \
	{ 1920, 1080, { 1, 60 }, MEDIA_BUS_FMT_RGB888_1X24, MIPI_CSI2_DT_RGB888, 24 }

struct max_serdes_tpg_entries {
	const struct max_serdes_tpg_entry *entries;
	unsigned int num_entries;
};

struct max_serdes_tpg_timings {
	bool gen_vs;
	bool gen_hs;
	bool gen_de;
	bool vs_inv;
	bool hs_inv;
	bool de_inv;
	u32 vs_dly;
	u32 vs_high;
	u32 vs_low;
	u32 v2h;
	u32 hs_high;
	u32 hs_low;
	u32 hs_cnt;
	u32 v2d;
	u32 de_high;
	u32 de_low;
	u32 de_cnt;
	u32 clock;
	u32 fps;
};

static inline struct max_serdes_asc *asc_to_max(struct v4l2_async_connection *asc)
{
	return container_of(asc, struct max_serdes_asc, base);
}

const char *max_serdes_gmsl_version_str(enum max_serdes_gmsl_version version);
const char *max_serdes_gmsl_mode_str(enum max_serdes_gmsl_mode mode);

const struct max_serdes_mipi_format *max_serdes_mipi_format_by_dt(u8 dt);

int max_serdes_get_fd_stream_entry(struct v4l2_subdev *sd, u32 pad, u32 stream,
				   struct v4l2_mbus_frame_desc_entry *entry);

int max_serdes_get_fd_bpp(struct v4l2_mbus_frame_desc_entry *entry,
			  unsigned int *bpp);
int max_serdes_process_bpps(struct device *dev, u32 bpps,
			    u32 allowed_double_bpps, unsigned int *doubled_bpp);

int max_serdes_xlate_enable_disable_streams(struct max_serdes_source *sources,
					    u32 source_sink_pad_offset,
					    const struct v4l2_subdev_state *state,
					    u32 pad, u64 updated_streams_mask,
					    u32 sink_pad_start, u32 num_sink_pads,
					    bool enable);

int max_serdes_get_streams_masks(struct device *dev,
				 const struct v4l2_subdev_state *state,
				 u32 pad, u64 updated_streams_mask,
				 u32 num_pads, u64 *old_streams_masks,
				 u64 **new_streams_masks, bool enable);

int max_serdes_get_tpg_timings(const struct max_serdes_tpg_entry *entry,
			       struct max_serdes_tpg_timings *timings);

int max_serdes_validate_tpg_routing(struct v4l2_subdev_krouting *routing);

#endif // MAX_SERDES_H
