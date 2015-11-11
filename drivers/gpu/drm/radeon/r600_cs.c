/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <linux/kernel.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "r600d.h"
#include "r600_reg_safe.h"

static int r600_nomm;
extern void r600_cs_legacy_get_tiling_conf(struct drm_device *dev, u32 *npipes, u32 *nbanks, u32 *group_size);


struct r600_cs_track {
	/* configuration we miror so that we use same code btw kms/ums */
	u32			group_size;
	u32			nbanks;
	u32			npipes;
	/* value we track */
	u32			sq_config;
	u32			log_nsamples;
	u32			nsamples;
	u32			cb_color_base_last[8];
	struct radeon_bo	*cb_color_bo[8];
	u64			cb_color_bo_mc[8];
	u64			cb_color_bo_offset[8];
	struct radeon_bo	*cb_color_frag_bo[8];
	u64			cb_color_frag_offset[8];
	struct radeon_bo	*cb_color_tile_bo[8];
	u64			cb_color_tile_offset[8];
	u32			cb_color_mask[8];
	u32			cb_color_info[8];
	u32			cb_color_view[8];
	u32			cb_color_size_idx[8]; /* unused */
	u32			cb_target_mask;
	u32			cb_shader_mask;  /* unused */
	bool			is_resolve;
	u32			cb_color_size[8];
	u32			vgt_strmout_en;
	u32			vgt_strmout_buffer_en;
	struct radeon_bo	*vgt_strmout_bo[4];
	u64			vgt_strmout_bo_mc[4]; /* unused */
	u32			vgt_strmout_bo_offset[4];
	u32			vgt_strmout_size[4];
	u32			db_depth_control;
	u32			db_depth_info;
	u32			db_depth_size_idx;
	u32			db_depth_view;
	u32			db_depth_size;
	u32			db_offset;
	struct radeon_bo	*db_bo;
	u64			db_bo_mc;
	bool			sx_misc_kill_all_prims;
	bool			cb_dirty;
	bool			db_dirty;
	bool			streamout_dirty;
	struct radeon_bo	*htile_bo;
	u64			htile_offset;
	u32			htile_surface;
};

#define FMT_8_BIT(fmt, vc)   [fmt] = { 1, 1, 1, vc, CHIP_R600 }
#define FMT_16_BIT(fmt, vc)  [fmt] = { 1, 1, 2, vc, CHIP_R600 }
#define FMT_24_BIT(fmt)      [fmt] = { 1, 1, 4,  0, CHIP_R600 }
#define FMT_32_BIT(fmt, vc)  [fmt] = { 1, 1, 4, vc, CHIP_R600 }
#define FMT_48_BIT(fmt)      [fmt] = { 1, 1, 8,  0, CHIP_R600 }
#define FMT_64_BIT(fmt, vc)  [fmt] = { 1, 1, 8, vc, CHIP_R600 }
#define FMT_96_BIT(fmt)      [fmt] = { 1, 1, 12, 0, CHIP_R600 }
#define FMT_128_BIT(fmt, vc) [fmt] = { 1, 1, 16,vc, CHIP_R600 }

struct gpu_formats {
	unsigned blockwidth;
	unsigned blockheight;
	unsigned blocksize;
	unsigned valid_color;
	enum radeon_family min_family;
};

static const struct gpu_formats color_formats_table[] = {
	/* 8 bit */
	FMT_8_BIT(V_038004_COLOR_8, 1),
	FMT_8_BIT(V_038004_COLOR_4_4, 1),
	FMT_8_BIT(V_038004_COLOR_3_3_2, 1),
	FMT_8_BIT(V_038004_FMT_1, 0),

	/* 16-bit */
	FMT_16_BIT(V_038004_COLOR_16, 1),
	FMT_16_BIT(V_038004_COLOR_16_FLOAT, 1),
	FMT_16_BIT(V_038004_COLOR_8_8, 1),
	FMT_16_BIT(V_038004_COLOR_5_6_5, 1),
	FMT_16_BIT(V_038004_COLOR_6_5_5, 1),
	FMT_16_BIT(V_038004_COLOR_1_5_5_5, 1),
	FMT_16_BIT(V_038004_COLOR_4_4_4_4, 1),
	FMT_16_BIT(V_038004_COLOR_5_5_5_1, 1),

	/* 24-bit */
	FMT_24_BIT(V_038004_FMT_8_8_8),

	/* 32-bit */
	FMT_32_BIT(V_038004_COLOR_32, 1),
	FMT_32_BIT(V_038004_COLOR_32_FLOAT, 1),
	FMT_32_BIT(V_038004_COLOR_16_16, 1),
	FMT_32_BIT(V_038004_COLOR_16_16_FLOAT, 1),
	FMT_32_BIT(V_038004_COLOR_8_24, 1),
	FMT_32_BIT(V_038004_COLOR_8_24_FLOAT, 1),
	FMT_32_BIT(V_038004_COLOR_24_8, 1),
	FMT_32_BIT(V_038004_COLOR_24_8_FLOAT, 1),
	FMT_32_BIT(V_038004_COLOR_10_11_11, 1),
	FMT_32_BIT(V_038004_COLOR_10_11_11_FLOAT, 1),
	FMT_32_BIT(V_038004_COLOR_11_11_10, 1),
	FMT_32_BIT(V_038004_COLOR_11_11_10_FLOAT, 1),
	FMT_32_BIT(V_038004_COLOR_2_10_10_10, 1),
	FMT_32_BIT(V_038004_COLOR_8_8_8_8, 1),
	FMT_32_BIT(V_038004_COLOR_10_10_10_2, 1),
	FMT_32_BIT(V_038004_FMT_5_9_9_9_SHAREDEXP, 0),
	FMT_32_BIT(V_038004_FMT_32_AS_8, 0),
	FMT_32_BIT(V_038004_FMT_32_AS_8_8, 0),

	/* 48-bit */
	FMT_48_BIT(V_038004_FMT_16_16_16),
	FMT_48_BIT(V_038004_FMT_16_16_16_FLOAT),

	/* 64-bit */
	FMT_64_BIT(V_038004_COLOR_X24_8_32_FLOAT, 1),
	FMT_64_BIT(V_038004_COLOR_32_32, 1),
	FMT_64_BIT(V_038004_COLOR_32_32_FLOAT, 1),
	FMT_64_BIT(V_038004_COLOR_16_16_16_16, 1),
	FMT_64_BIT(V_038004_COLOR_16_16_16_16_FLOAT, 1),

	FMT_96_BIT(V_038004_FMT_32_32_32),
	FMT_96_BIT(V_038004_FMT_32_32_32_FLOAT),

	/* 128-bit */
	FMT_128_BIT(V_038004_COLOR_32_32_32_32, 1),
	FMT_128_BIT(V_038004_COLOR_32_32_32_32_FLOAT, 1),

	[V_038004_FMT_GB_GR] = { 2, 1, 4, 0 },
	[V_038004_FMT_BG_RG] = { 2, 1, 4, 0 },

	/* block compressed formats */
	[V_038004_FMT_BC1] = { 4, 4, 8, 0 },
	[V_038004_FMT_BC2] = { 4, 4, 16, 0 },
	[V_038004_FMT_BC3] = { 4, 4, 16, 0 },
	[V_038004_FMT_BC4] = { 4, 4, 8, 0 },
	[V_038004_FMT_BC5] = { 4, 4, 16, 0},
	[V_038004_FMT_BC6] = { 4, 4, 16, 0, CHIP_CEDAR}, /* Evergreen-only */
	[V_038004_FMT_BC7] = { 4, 4, 16, 0, CHIP_CEDAR}, /* Evergreen-only */

	/* The other Evergreen formats */
	[V_038004_FMT_32_AS_32_32_32_32] = { 1, 1, 4, 0, CHIP_CEDAR},
};

bool r600_fmt_is_valid_color(u32 format)
{
	if (format >= ARRAY_SIZE(color_formats_table))
		return false;

	if (color_formats_table[format].valid_color)
		return true;

	return false;
}

bool r600_fmt_is_valid_texture(u32 format, enum radeon_family family)
{
	if (format >= ARRAY_SIZE(color_formats_table))
		return false;

	if (family < color_formats_table[format].min_family)
		return false;

	if (color_formats_table[format].blockwidth > 0)
		return true;

	return false;
}

int r600_fmt_get_blocksize(u32 format)
{
	if (format >= ARRAY_SIZE(color_formats_table))
		return 0;

	return color_formats_table[format].blocksize;
}

int r600_fmt_get_nblocksx(u32 format, u32 w)
{
	unsigned bw;

	if (format >= ARRAY_SIZE(color_formats_table))
		return 0;

	bw = color_formats_table[format].blockwidth;
	if (bw == 0)
		return 0;

	return (w + bw - 1) / bw;
}

int r600_fmt_get_nblocksy(u32 format, u32 h)
{
	unsigned bh;

	if (format >= ARRAY_SIZE(color_formats_table))
		return 0;

	bh = color_formats_table[format].blockheight;
	if (bh == 0)
		return 0;

	return (h + bh - 1) / bh;
}

struct array_mode_checker {
	int array_mode;
	u32 group_size;
	u32 nbanks;
	u32 npipes;
	u32 nsamples;
	u32 blocksize;
};

/* returns alignment in pixels for pitch/height/depth and bytes for base */
static int r600_get_array_mode_alignment(struct array_mode_checker *values,
						u32 *pitch_align,
						u32 *height_align,
						u32 *depth_align,
						u64 *base_align)
{
	u32 tile_width = 8;
	u32 tile_height = 8;
	u32 macro_tile_width = values->nbanks;
	u32 macro_tile_height = values->npipes;
	u32 tile_bytes = tile_width * tile_height * values->blocksize * values->nsamples;
	u32 macro_tile_bytes = macro_tile_width * macro_tile_height * tile_bytes;

	switch (values->array_mode) {
	case ARRAY_LINEAR_GENERAL:
		/* technically tile_width/_height for pitch/height */
		*pitch_align = 1; /* tile_width */
		*height_align = 1; /* tile_height */
		*depth_align = 1;
		*base_align = 1;
		break;
	case ARRAY_LINEAR_ALIGNED:
		*pitch_align = max((u32)64, (u32)(values->group_size / values->blocksize));
		*height_align = 1;
		*depth_align = 1;
		*base_align = values->group_size;
		break;
	case ARRAY_1D_TILED_THIN1:
		*pitch_align = max((u32)tile_width,
				   (u32)(values->group_size /
					 (tile_height * values->blocksize * values->nsamples)));
		*height_align = tile_height;
		*depth_align = 1;
		*base_align = values->group_size;
		break;
	case ARRAY_2D_TILED_THIN1:
		*pitch_align = max((u32)macro_tile_width * tile_width,
				(u32)((values->group_size * values->nbanks) /
				(values->blocksize * values->nsamples * tile_width)));
		*height_align = macro_tile_height * tile_height;
		*depth_align = 1;
		*base_align = max(macro_tile_bytes,
				  (*pitch_align) * values->blocksize * (*height_align) * values->nsamples);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void r600_cs_track_init(struct r600_cs_track *track)
{
	int i;

	/* assume DX9 mode */
	track->sq_config = DX9_CONSTS;
	for (i = 0; i < 8; i++) {
		track->cb_color_base_last[i] = 0;
		track->cb_color_size[i] = 0;
		track->cb_color_size_idx[i] = 0;
		track->cb_color_info[i] = 0;
		track->cb_color_view[i] = 0xFFFFFFFF;
		track->cb_color_bo[i] = NULL;
		track->cb_color_bo_offset[i] = 0xFFFFFFFF;
		track->cb_color_bo_mc[i] = 0xFFFFFFFF;
		track->cb_color_frag_bo[i] = NULL;
		track->cb_color_frag_offset[i] = 0xFFFFFFFF;
		track->cb_color_tile_bo[i] = NULL;
		track->cb_color_tile_offset[i] = 0xFFFFFFFF;
		track->cb_color_mask[i] = 0xFFFFFFFF;
	}
	track->is_resolve = false;
	track->nsamples = 16;
	track->log_nsamples = 4;
	track->cb_target_mask = 0xFFFFFFFF;
	track->cb_shader_mask = 0xFFFFFFFF;
	track->cb_dirty = true;
	track->db_bo = NULL;
	track->db_bo_mc = 0xFFFFFFFF;
	/* assume the biggest format and that htile is enabled */
	track->db_depth_info = 7 | (1 << 25);
	track->db_depth_view = 0xFFFFC000;
	track->db_depth_size = 0xFFFFFFFF;
	track->db_depth_size_idx = 0;
	track->db_depth_control = 0xFFFFFFFF;
	track->db_dirty = true;
	track->htile_bo = NULL;
	track->htile_offset = 0xFFFFFFFF;
	track->htile_surface = 0;

	for (i = 0; i < 4; i++) {
		track->vgt_strmout_size[i] = 0;
		track->vgt_strmout_bo[i] = NULL;
		track->vgt_strmout_bo_offset[i] = 0xFFFFFFFF;
		track->vgt_strmout_bo_mc[i] = 0xFFFFFFFF;
	}
	track->streamout_dirty = true;
	track->sx_misc_kill_all_prims = false;
}

static int r600_cs_track_validate_cb(struct radeon_cs_parser *p, int i)
{
	struct r600_cs_track *track = p->track;
	u32 slice_tile_max, size, tmp;
	u32 height, height_align, pitch, pitch_align, depth_align;
	u64 base_offset, base_align;
	struct array_mode_checker array_check;
	volatile u32 *ib = p->ib.ptr;
	unsigned array_mode;
	u32 format;
	/* When resolve is used, the second colorbuffer has always 1 sample. */
	unsigned nsamples = track->is_resolve && i == 1 ? 1 : track->nsamples;

	size = radeon_bo_size(track->cb_color_bo[i]) - track->cb_color_bo_offset[i];
	format = G_0280A0_FORMAT(track->cb_color_info[i]);
	if (!r600_fmt_is_valid_color(format)) {
		dev_warn(p->dev, "%s:%d cb invalid format %d for %d (0x%08X)\n",
			 __func__, __LINE__, format,
			i, track->cb_color_info[i]);
		return -EINVAL;
	}
	/* pitch in pixels */
	pitch = (G_028060_PITCH_TILE_MAX(track->cb_color_size[i]) + 1) * 8;
	slice_tile_max = G_028060_SLICE_TILE_MAX(track->cb_color_size[i]) + 1;
	slice_tile_max *= 64;
	height = slice_tile_max / pitch;
	if (height > 8192)
		height = 8192;
	array_mode = G_0280A0_ARRAY_MODE(track->cb_color_info[i]);

	base_offset = track->cb_color_bo_mc[i] + track->cb_color_bo_offset[i];
	array_check.array_mode = array_mode;
	array_check.group_size = track->group_size;
	array_check.nbanks = track->nbanks;
	array_check.npipes = track->npipes;
	array_check.nsamples = nsamples;
	array_check.blocksize = r600_fmt_get_blocksize(format);
	if (r600_get_array_mode_alignment(&array_check,
					  &pitch_align, &height_align, &depth_align, &base_align)) {
		dev_warn(p->dev, "%s invalid tiling %d for %d (0x%08X)\n", __func__,
			 G_0280A0_ARRAY_MODE(track->cb_color_info[i]), i,
			 track->cb_color_info[i]);
		return -EINVAL;
	}
	switch (array_mode) {
	case V_0280A0_ARRAY_LINEAR_GENERAL:
		break;
	case V_0280A0_ARRAY_LINEAR_ALIGNED:
		break;
	case V_0280A0_ARRAY_1D_TILED_THIN1:
		/* avoid breaking userspace */
		if (height > 7)
			height &= ~0x7;
		break;
	case V_0280A0_ARRAY_2D_TILED_THIN1:
		break;
	default:
		dev_warn(p->dev, "%s invalid tiling %d for %d (0x%08X)\n", __func__,
			G_0280A0_ARRAY_MODE(track->cb_color_info[i]), i,
			track->cb_color_info[i]);
		return -EINVAL;
	}

	if (!IS_ALIGNED(pitch, pitch_align)) {
		dev_warn(p->dev, "%s:%d cb pitch (%d, 0x%x, %d) invalid\n",
			 __func__, __LINE__, pitch, pitch_align, array_mode);
		return -EINVAL;
	}
	if (!IS_ALIGNED(height, height_align)) {
		dev_warn(p->dev, "%s:%d cb height (%d, 0x%x, %d) invalid\n",
			 __func__, __LINE__, height, height_align, array_mode);
		return -EINVAL;
	}
	if (!IS_ALIGNED(base_offset, base_align)) {
		dev_warn(p->dev, "%s offset[%d] 0x%llx 0x%llx, %d not aligned\n", __func__, i,
			 base_offset, base_align, array_mode);
		return -EINVAL;
	}

	/* check offset */
	tmp = r600_fmt_get_nblocksy(format, height) * r600_fmt_get_nblocksx(format, pitch) *
	      r600_fmt_get_blocksize(format) * nsamples;
	switch (array_mode) {
	default:
	case V_0280A0_ARRAY_LINEAR_GENERAL:
	case V_0280A0_ARRAY_LINEAR_ALIGNED:
		tmp += track->cb_color_view[i] & 0xFF;
		break;
	case V_0280A0_ARRAY_1D_TILED_THIN1:
	case V_0280A0_ARRAY_2D_TILED_THIN1:
		tmp += G_028080_SLICE_MAX(track->cb_color_view[i]) * tmp;
		break;
	}
	if ((tmp + track->cb_color_bo_offset[i]) > radeon_bo_size(track->cb_color_bo[i])) {
		if (array_mode == V_0280A0_ARRAY_LINEAR_GENERAL) {
			/* the initial DDX does bad things with the CB size occasionally */
			/* it rounds up height too far for slice tile max but the BO is smaller */
			/* r600c,g also seem to flush at bad times in some apps resulting in
			 * bogus values here. So for linear just allow anything to avoid breaking
			 * broken userspace.
			 */
		} else {
			dev_warn(p->dev, "%s offset[%d] %d %llu %d %lu too big (%d %d) (%d %d %d)\n",
				 __func__, i, array_mode,
				 track->cb_color_bo_offset[i], tmp,
				 radeon_bo_size(track->cb_color_bo[i]),
				 pitch, height, r600_fmt_get_nblocksx(format, pitch),
				 r600_fmt_get_nblocksy(format, height),
				 r600_fmt_get_blocksize(format));
			return -EINVAL;
		}
	}
	/* limit max tile */
	tmp = (height * pitch) >> 6;
	if (tmp < slice_tile_max)
		slice_tile_max = tmp;
	tmp = S_028060_PITCH_TILE_MAX((pitch / 8) - 1) |
		S_028060_SLICE_TILE_MAX(slice_tile_max - 1);
	ib[track->cb_color_size_idx[i]] = tmp;

	/* FMASK/CMASK */
	switch (G_0280A0_TILE_MODE(track->cb_color_info[i])) {
	case V_0280A0_TILE_DISABLE:
		break;
	case V_0280A0_FRAG_ENABLE:
		if (track->nsamples > 1) {
			uint32_t tile_max = G_028100_FMASK_TILE_MAX(track->cb_color_mask[i]);
			/* the tile size is 8x8, but the size is in units of bits.
			 * for bytes, do just * 8. */
			uint32_t bytes = track->nsamples * track->log_nsamples * 8 * (tile_max + 1);

			if (bytes + track->cb_color_frag_offset[i] >
			    radeon_bo_size(track->cb_color_frag_bo[i])) {
				dev_warn(p->dev, "%s FMASK_TILE_MAX too large "
					 "(tile_max=%u, bytes=%u, offset=%llu, bo_size=%lu)\n",
					 __func__, tile_max, bytes,
					 track->cb_color_frag_offset[i],
					 radeon_bo_size(track->cb_color_frag_bo[i]));
				return -EINVAL;
			}
		}
		/* fall through */
	case V_0280A0_CLEAR_ENABLE:
	{
		uint32_t block_max = G_028100_CMASK_BLOCK_MAX(track->cb_color_mask[i]);
		/* One block = 128x128 pixels, one 8x8 tile has 4 bits..
		 * (128*128) / (8*8) / 2 = 128 bytes per block. */
		uint32_t bytes = (block_max + 1) * 128;

		if (bytes + track->cb_color_tile_offset[i] >
		    radeon_bo_size(track->cb_color_tile_bo[i])) {
			dev_warn(p->dev, "%s CMASK_BLOCK_MAX too large "
				 "(block_max=%u, bytes=%u, offset=%llu, bo_size=%lu)\n",
				 __func__, block_max, bytes,
				 track->cb_color_tile_offset[i],
				 radeon_bo_size(track->cb_color_tile_bo[i]));
			return -EINVAL;
		}
		break;
	}
	default:
		dev_warn(p->dev, "%s invalid tile mode\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int r600_cs_track_validate_db(struct radeon_cs_parser *p)
{
	struct r600_cs_track *track = p->track;
	u32 nviews, bpe, ntiles, size, slice_tile_max, tmp;
	u32 height_align, pitch_align, depth_align;
	u32 pitch = 8192;
	u32 height = 8192;
	u64 base_offset, base_align;
	struct array_mode_checker array_check;
	int array_mode;
	volatile u32 *ib = p->ib.ptr;


	if (track->db_bo == NULL) {
		dev_warn(p->dev, "z/stencil with no depth buffer\n");
		return -EINVAL;
	}
	switch (G_028010_FORMAT(track->db_depth_info)) {
	case V_028010_DEPTH_16:
		bpe = 2;
		break;
	case V_028010_DEPTH_X8_24:
	case V_028010_DEPTH_8_24:
	case V_028010_DEPTH_X8_24_FLOAT:
	case V_028010_DEPTH_8_24_FLOAT:
	case V_028010_DEPTH_32_FLOAT:
		bpe = 4;
		break;
	case V_028010_DEPTH_X24_8_32_FLOAT:
		bpe = 8;
		break;
	default:
		dev_warn(p->dev, "z/stencil with invalid format %d\n", G_028010_FORMAT(track->db_depth_info));
		return -EINVAL;
	}
	if ((track->db_depth_size & 0xFFFFFC00) == 0xFFFFFC00) {
		if (!track->db_depth_size_idx) {
			dev_warn(p->dev, "z/stencil buffer size not set\n");
			return -EINVAL;
		}
		tmp = radeon_bo_size(track->db_bo) - track->db_offset;
		tmp = (tmp / bpe) >> 6;
		if (!tmp) {
			dev_warn(p->dev, "z/stencil buffer too small (0x%08X %d %d %ld)\n",
					track->db_depth_size, bpe, track->db_offset,
					radeon_bo_size(track->db_bo));
			return -EINVAL;
		}
		ib[track->db_depth_size_idx] = S_028000_SLICE_TILE_MAX(tmp - 1) | (track->db_depth_size & 0x3FF);
	} else {
		size = radeon_bo_size(track->db_bo);
		/* pitch in pixels */
		pitch = (G_028000_PITCH_TILE_MAX(track->db_depth_size) + 1) * 8;
		slice_tile_max = G_028000_SLICE_TILE_MAX(track->db_depth_size) + 1;
		slice_tile_max *= 64;
		height = slice_tile_max / pitch;
		if (height > 8192)
			height = 8192;
		base_offset = track->db_bo_mc + track->db_offset;
		array_mode = G_028010_ARRAY_MODE(track->db_depth_info);
		array_check.array_mode = array_mode;
		array_check.group_size = track->group_size;
		array_check.nbanks = track->nbanks;
		array_check.npipes = track->npipes;
		array_check.nsamples = track->nsamples;
		array_check.blocksize = bpe;
		if (r600_get_array_mode_alignment(&array_check,
					&pitch_align, &height_align, &depth_align, &base_align)) {
			dev_warn(p->dev, "%s invalid tiling %d (0x%08X)\n", __func__,
					G_028010_ARRAY_MODE(track->db_depth_info),
					track->db_depth_info);
			return -EINVAL;
		}
		switch (array_mode) {
		case V_028010_ARRAY_1D_TILED_THIN1:
			/* don't break userspace */
			height &= ~0x7;
			break;
		case V_028010_ARRAY_2D_TILED_THIN1:
			break;
		default:
			dev_warn(p->dev, "%s invalid tiling %d (0x%08X)\n", __func__,
					G_028010_ARRAY_MODE(track->db_depth_info),
					track->db_depth_info);
			return -EINVAL;
		}

		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d db pitch (%d, 0x%x, %d) invalid\n",
					__func__, __LINE__, pitch, pitch_align, array_mode);
			return -EINVAL;
		}
		if (!IS_ALIGNED(height, height_align)) {
			dev_warn(p->dev, "%s:%d db height (%d, 0x%x, %d) invalid\n",
					__func__, __LINE__, height, height_align, array_mode);
			return -EINVAL;
		}
		if (!IS_ALIGNED(base_offset, base_align)) {
			dev_warn(p->dev, "%s offset 0x%llx, 0x%llx, %d not aligned\n", __func__,
					base_offset, base_align, array_mode);
			return -EINVAL;
		}

		ntiles = G_028000_SLICE_TILE_MAX(track->db_depth_size) + 1;
		nviews = G_028004_SLICE_MAX(track->db_depth_view) + 1;
		tmp = ntiles * bpe * 64 * nviews * track->nsamples;
		if ((tmp + track->db_offset) > radeon_bo_size(track->db_bo)) {
			dev_warn(p->dev, "z/stencil buffer (%d) too small (0x%08X %d %d %d -> %u have %lu)\n",
					array_mode,
					track->db_depth_size, ntiles, nviews, bpe, tmp + track->db_offset,
					radeon_bo_size(track->db_bo));
			return -EINVAL;
		}
	}

	/* hyperz */
	if (G_028010_TILE_SURFACE_ENABLE(track->db_depth_info)) {
		unsigned long size;
		unsigned nbx, nby;

		if (track->htile_bo == NULL) {
			dev_warn(p->dev, "%s:%d htile enabled without htile surface 0x%08x\n",
				 __func__, __LINE__, track->db_depth_info);
			return -EINVAL;
		}
		if ((track->db_depth_size & 0xFFFFFC00) == 0xFFFFFC00) {
			dev_warn(p->dev, "%s:%d htile can't be enabled with bogus db_depth_size 0x%08x\n",
				 __func__, __LINE__, track->db_depth_size);
			return -EINVAL;
		}

		nbx = pitch;
		nby = height;
		if (G_028D24_LINEAR(track->htile_surface)) {
			/* nbx must be 16 htiles aligned == 16 * 8 pixel aligned */
			nbx = round_up(nbx, 16 * 8);
			/* nby is npipes htiles aligned == npipes * 8 pixel aligned */
			nby = round_up(nby, track->npipes * 8);
		} else {
			/* always assume 8x8 htile */
			/* align is htile align * 8, htile align vary according to
			 * number of pipe and tile width and nby
			 */
			switch (track->npipes) {
			case 8:
				/* HTILE_WIDTH = 8 & HTILE_HEIGHT = 8*/
				nbx = round_up(nbx, 64 * 8);
				nby = round_up(nby, 64 * 8);
				break;
			case 4:
				/* HTILE_WIDTH = 8 & HTILE_HEIGHT = 8*/
				nbx = round_up(nbx, 64 * 8);
				nby = round_up(nby, 32 * 8);
				break;
			case 2:
				/* HTILE_WIDTH = 8 & HTILE_HEIGHT = 8*/
				nbx = round_up(nbx, 32 * 8);
				nby = round_up(nby, 32 * 8);
				break;
			case 1:
				/* HTILE_WIDTH = 8 & HTILE_HEIGHT = 8*/
				nbx = round_up(nbx, 32 * 8);
				nby = round_up(nby, 16 * 8);
				break;
			default:
				dev_warn(p->dev, "%s:%d invalid num pipes %d\n",
					 __func__, __LINE__, track->npipes);
				return -EINVAL;
			}
		}
		/* compute number of htile */
		nbx = nbx >> 3;
		nby = nby >> 3;
		/* size must be aligned on npipes * 2K boundary */
		size = roundup(nbx * nby * 4, track->npipes * (2 << 10));
		size += track->htile_offset;

		if (size > radeon_bo_size(track->htile_bo)) {
			dev_warn(p->dev, "%s:%d htile surface too small %ld for %ld (%d %d)\n",
				 __func__, __LINE__, radeon_bo_size(track->htile_bo),
				 size, nbx, nby);
			return -EINVAL;
		}
	}

	track->db_dirty = false;
	return 0;
}

static int r600_cs_track_check(struct radeon_cs_parser *p)
{
	struct r600_cs_track *track = p->track;
	u32 tmp;
	int r, i;

	/* on legacy kernel we don't perform advanced check */
	if (p->rdev == NULL)
		return 0;

	/* check streamout */
	if (track->streamout_dirty && track->vgt_strmout_en) {
		for (i = 0; i < 4; i++) {
			if (track->vgt_strmout_buffer_en & (1 << i)) {
				if (track->vgt_strmout_bo[i]) {
					u64 offset = (u64)track->vgt_strmout_bo_offset[i] +
						(u64)track->vgt_strmout_size[i];
					if (offset > radeon_bo_size(track->vgt_strmout_bo[i])) {
						DRM_ERROR("streamout %d bo too small: 0x%llx, 0x%lx\n",
							  i, offset,
							  radeon_bo_size(track->vgt_strmout_bo[i]));
						return -EINVAL;
					}
				} else {
					dev_warn(p->dev, "No buffer for streamout %d\n", i);
					return -EINVAL;
				}
			}
		}
		track->streamout_dirty = false;
	}

	if (track->sx_misc_kill_all_prims)
		return 0;

	/* check that we have a cb for each enabled target, we don't check
	 * shader_mask because it seems mesa isn't always setting it :(
	 */
	if (track->cb_dirty) {
		tmp = track->cb_target_mask;

		/* We must check both colorbuffers for RESOLVE. */
		if (track->is_resolve) {
			tmp |= 0xff;
		}

		for (i = 0; i < 8; i++) {
			u32 format = G_0280A0_FORMAT(track->cb_color_info[i]);

			if (format != V_0280A0_COLOR_INVALID &&
			    (tmp >> (i * 4)) & 0xF) {
				/* at least one component is enabled */
				if (track->cb_color_bo[i] == NULL) {
					dev_warn(p->dev, "%s:%d mask 0x%08X | 0x%08X no cb for %d\n",
						__func__, __LINE__, track->cb_target_mask, track->cb_shader_mask, i);
					return -EINVAL;
				}
				/* perform rewrite of CB_COLOR[0-7]_SIZE */
				r = r600_cs_track_validate_cb(p, i);
				if (r)
					return r;
			}
		}
		track->cb_dirty = false;
	}

	/* Check depth buffer */
	if (track->db_dirty &&
	    G_028010_FORMAT(track->db_depth_info) != V_028010_DEPTH_INVALID &&
	    (G_028800_STENCIL_ENABLE(track->db_depth_control) ||
	     G_028800_Z_ENABLE(track->db_depth_control))) {
		r = r600_cs_track_validate_db(p);
		if (r)
			return r;
	}

	return 0;
}

/**
 * r600_cs_packet_parse_vline() - parse userspace VLINE packet
 * @parser:		parser structure holding parsing context.
 *
 * This is an R600-specific function for parsing VLINE packets.
 * Real work is done by r600_cs_common_vline_parse function.
 * Here we just set up ASIC-specific register table and call
 * the common implementation function.
 */
static int r600_cs_packet_parse_vline(struct radeon_cs_parser *p)
{
	static uint32_t vline_start_end[2] = {AVIVO_D1MODE_VLINE_START_END,
					      AVIVO_D2MODE_VLINE_START_END};
	static uint32_t vline_status[2] = {AVIVO_D1MODE_VLINE_STATUS,
					   AVIVO_D2MODE_VLINE_STATUS};

	return r600_cs_common_vline_parse(p, vline_start_end, vline_status);
}

/**
 * r600_cs_common_vline_parse() - common vline parser
 * @parser:		parser structure holding parsing context.
 * @vline_start_end:    table of vline_start_end registers
 * @vline_status:       table of vline_status registers
 *
 * Userspace sends a special sequence for VLINE waits.
 * PACKET0 - VLINE_START_END + value
 * PACKET3 - WAIT_REG_MEM poll vline status reg
 * RELOC (P3) - crtc_id in reloc.
 *
 * This function parses this and relocates the VLINE START END
 * and WAIT_REG_MEM packets to the correct crtc.
 * It also detects a switched off crtc and nulls out the
 * wait in that case. This function is common for all ASICs that
 * are R600 and newer. The parsing algorithm is the same, and only
 * differs in which registers are used.
 *
 * Caller is the ASIC-specific function which passes the parser
 * context and ASIC-specific register table
 */
int r600_cs_common_vline_parse(struct radeon_cs_parser *p,
			       uint32_t *vline_start_end,
			       uint32_t *vline_status)
{
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	struct radeon_cs_packet p3reloc, wait_reg_mem;
	int crtc_id;
	int r;
	uint32_t header, h_idx, reg, wait_reg_mem_info;
	volatile uint32_t *ib;

	ib = p->ib.ptr;

	/* parse the WAIT_REG_MEM */
	r = radeon_cs_packet_parse(p, &wait_reg_mem, p->idx);
	if (r)
		return r;

	/* check its a WAIT_REG_MEM */
	if (wait_reg_mem.type != RADEON_PACKET_TYPE3 ||
	    wait_reg_mem.opcode != PACKET3_WAIT_REG_MEM) {
		DRM_ERROR("vline wait missing WAIT_REG_MEM segment\n");
		return -EINVAL;
	}

	wait_reg_mem_info = radeon_get_ib_value(p, wait_reg_mem.idx + 1);
	/* bit 4 is reg (0) or mem (1) */
	if (wait_reg_mem_info & 0x10) {
		DRM_ERROR("vline WAIT_REG_MEM waiting on MEM instead of REG\n");
		return -EINVAL;
	}
	/* bit 8 is me (0) or pfp (1) */
	if (wait_reg_mem_info & 0x100) {
		DRM_ERROR("vline WAIT_REG_MEM waiting on PFP instead of ME\n");
		return -EINVAL;
	}
	/* waiting for value to be equal */
	if ((wait_reg_mem_info & 0x7) != 0x3) {
		DRM_ERROR("vline WAIT_REG_MEM function not equal\n");
		return -EINVAL;
	}
	if ((radeon_get_ib_value(p, wait_reg_mem.idx + 2) << 2) != vline_status[0]) {
		DRM_ERROR("vline WAIT_REG_MEM bad reg\n");
		return -EINVAL;
	}

	if (radeon_get_ib_value(p, wait_reg_mem.idx + 5) != RADEON_VLINE_STAT) {
		DRM_ERROR("vline WAIT_REG_MEM bad bit mask\n");
		return -EINVAL;
	}

	/* jump over the NOP */
	r = radeon_cs_packet_parse(p, &p3reloc, p->idx + wait_reg_mem.count + 2);
	if (r)
		return r;

	h_idx = p->idx - 2;
	p->idx += wait_reg_mem.count + 2;
	p->idx += p3reloc.count + 2;

	header = radeon_get_ib_value(p, h_idx);
	crtc_id = radeon_get_ib_value(p, h_idx + 2 + 7 + 1);
	reg = R600_CP_PACKET0_GET_REG(header);

	crtc = drm_crtc_find(p->rdev->ddev, crtc_id);
	if (!crtc) {
		DRM_ERROR("cannot find crtc %d\n", crtc_id);
		return -ENOENT;
	}
	radeon_crtc = to_radeon_crtc(crtc);
	crtc_id = radeon_crtc->crtc_id;

	if (!crtc->enabled) {
		/* CRTC isn't enabled - we need to nop out the WAIT_REG_MEM */
		ib[h_idx + 2] = PACKET2(0);
		ib[h_idx + 3] = PACKET2(0);
		ib[h_idx + 4] = PACKET2(0);
		ib[h_idx + 5] = PACKET2(0);
		ib[h_idx + 6] = PACKET2(0);
		ib[h_idx + 7] = PACKET2(0);
		ib[h_idx + 8] = PACKET2(0);
	} else if (reg == vline_start_end[0]) {
		header &= ~R600_CP_PACKET0_REG_MASK;
		header |= vline_start_end[crtc_id] >> 2;
		ib[h_idx] = header;
		ib[h_idx + 4] = vline_status[crtc_id] >> 2;
	} else {
		DRM_ERROR("unknown crtc reloc\n");
		return -EINVAL;
	}
	return 0;
}

static int r600_packet0_check(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt,
				unsigned idx, unsigned reg)
{
	int r;

	switch (reg) {
	case AVIVO_D1MODE_VLINE_START_END:
		r = r600_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			return r;
		}
		break;
	default:
		printk(KERN_ERR "Forbidden register 0x%04X in cs at %d\n",
		       reg, idx);
		return -EINVAL;
	}
	return 0;
}

static int r600_cs_parse_packet0(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt)
{
	unsigned reg, i;
	unsigned idx;
	int r;

	idx = pkt->idx + 1;
	reg = pkt->reg;
	for (i = 0; i <= pkt->count; i++, idx++, reg += 4) {
		r = r600_packet0_check(p, pkt, idx, reg);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * r600_cs_check_reg() - check if register is authorized or not
 * @parser: parser structure holding parsing context
 * @reg: register we are testing
 * @idx: index into the cs buffer
 *
 * This function will test against r600_reg_safe_bm and return 0
 * if register is safe. If register is not flag as safe this function
 * will test it against a list of register needind special handling.
 */
static int r600_cs_check_reg(struct radeon_cs_parser *p, u32 reg, u32 idx)
{
	struct r600_cs_track *track = (struct r600_cs_track *)p->track;
	struct radeon_bo_list *reloc;
	u32 m, i, tmp, *ib;
	int r;

	i = (reg >> 7);
	if (i >= ARRAY_SIZE(r600_reg_safe_bm)) {
		dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
		return -EINVAL;
	}
	m = 1 << ((reg >> 2) & 31);
	if (!(r600_reg_safe_bm[i] & m))
		return 0;
	ib = p->ib.ptr;
	switch (reg) {
	/* force following reg to 0 in an attempt to disable out buffer
	 * which will need us to better understand how it works to perform
	 * security check on it (Jerome)
	 */
	case R_0288A8_SQ_ESGS_RING_ITEMSIZE:
	case R_008C44_SQ_ESGS_RING_SIZE:
	case R_0288B0_SQ_ESTMP_RING_ITEMSIZE:
	case R_008C54_SQ_ESTMP_RING_SIZE:
	case R_0288C0_SQ_FBUF_RING_ITEMSIZE:
	case R_008C74_SQ_FBUF_RING_SIZE:
	case R_0288B4_SQ_GSTMP_RING_ITEMSIZE:
	case R_008C5C_SQ_GSTMP_RING_SIZE:
	case R_0288AC_SQ_GSVS_RING_ITEMSIZE:
	case R_008C4C_SQ_GSVS_RING_SIZE:
	case R_0288BC_SQ_PSTMP_RING_ITEMSIZE:
	case R_008C6C_SQ_PSTMP_RING_SIZE:
	case R_0288C4_SQ_REDUC_RING_ITEMSIZE:
	case R_008C7C_SQ_REDUC_RING_SIZE:
	case R_0288B8_SQ_VSTMP_RING_ITEMSIZE:
	case R_008C64_SQ_VSTMP_RING_SIZE:
	case R_0288C8_SQ_GS_VERT_ITEMSIZE:
		/* get value to populate the IB don't remove */
		/*tmp =radeon_get_ib_value(p, idx);
		  ib[idx] = 0;*/
		break;
	case SQ_ESGS_RING_BASE:
	case SQ_GSVS_RING_BASE:
	case SQ_ESTMP_RING_BASE:
	case SQ_GSTMP_RING_BASE:
	case SQ_PSTMP_RING_BASE:
	case SQ_VSTMP_RING_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case SQ_CONFIG:
		track->sq_config = radeon_get_ib_value(p, idx);
		break;
	case R_028800_DB_DEPTH_CONTROL:
		track->db_depth_control = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case R_028010_DB_DEPTH_INFO:
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS) &&
		    radeon_cs_packet_next_is_pkt3_nop(p)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				dev_warn(p->dev, "bad SET_CONTEXT_REG "
					 "0x%04X\n", reg);
				return -EINVAL;
			}
			track->db_depth_info = radeon_get_ib_value(p, idx);
			ib[idx] &= C_028010_ARRAY_MODE;
			track->db_depth_info &= C_028010_ARRAY_MODE;
			if (reloc->tiling_flags & RADEON_TILING_MACRO) {
				ib[idx] |= S_028010_ARRAY_MODE(V_028010_ARRAY_2D_TILED_THIN1);
				track->db_depth_info |= S_028010_ARRAY_MODE(V_028010_ARRAY_2D_TILED_THIN1);
			} else {
				ib[idx] |= S_028010_ARRAY_MODE(V_028010_ARRAY_1D_TILED_THIN1);
				track->db_depth_info |= S_028010_ARRAY_MODE(V_028010_ARRAY_1D_TILED_THIN1);
			}
		} else {
			track->db_depth_info = radeon_get_ib_value(p, idx);
		}
		track->db_dirty = true;
		break;
	case R_028004_DB_DEPTH_VIEW:
		track->db_depth_view = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case R_028000_DB_DEPTH_SIZE:
		track->db_depth_size = radeon_get_ib_value(p, idx);
		track->db_depth_size_idx = idx;
		track->db_dirty = true;
		break;
	case R_028AB0_VGT_STRMOUT_EN:
		track->vgt_strmout_en = radeon_get_ib_value(p, idx);
		track->streamout_dirty = true;
		break;
	case R_028B20_VGT_STRMOUT_BUFFER_EN:
		track->vgt_strmout_buffer_en = radeon_get_ib_value(p, idx);
		track->streamout_dirty = true;
		break;
	case VGT_STRMOUT_BUFFER_BASE_0:
	case VGT_STRMOUT_BUFFER_BASE_1:
	case VGT_STRMOUT_BUFFER_BASE_2:
	case VGT_STRMOUT_BUFFER_BASE_3:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - VGT_STRMOUT_BUFFER_BASE_0) / 16;
		track->vgt_strmout_bo_offset[tmp] = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->vgt_strmout_bo[tmp] = reloc->robj;
		track->vgt_strmout_bo_mc[tmp] = reloc->gpu_offset;
		track->streamout_dirty = true;
		break;
	case VGT_STRMOUT_BUFFER_SIZE_0:
	case VGT_STRMOUT_BUFFER_SIZE_1:
	case VGT_STRMOUT_BUFFER_SIZE_2:
	case VGT_STRMOUT_BUFFER_SIZE_3:
		tmp = (reg - VGT_STRMOUT_BUFFER_SIZE_0) / 16;
		/* size in register is DWs, convert to bytes */
		track->vgt_strmout_size[tmp] = radeon_get_ib_value(p, idx) * 4;
		track->streamout_dirty = true;
		break;
	case CP_COHER_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "missing reloc for CP_COHER_BASE "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case R_028238_CB_TARGET_MASK:
		track->cb_target_mask = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case R_02823C_CB_SHADER_MASK:
		track->cb_shader_mask = radeon_get_ib_value(p, idx);
		break;
	case R_028C04_PA_SC_AA_CONFIG:
		tmp = G_028C04_MSAA_NUM_SAMPLES(radeon_get_ib_value(p, idx));
		track->log_nsamples = tmp;
		track->nsamples = 1 << tmp;
		track->cb_dirty = true;
		break;
	case R_028808_CB_COLOR_CONTROL:
		tmp = G_028808_SPECIAL_OP(radeon_get_ib_value(p, idx));
		track->is_resolve = tmp == V_028808_SPECIAL_RESOLVE_BOX;
		track->cb_dirty = true;
		break;
	case R_0280A0_CB_COLOR0_INFO:
	case R_0280A4_CB_COLOR1_INFO:
	case R_0280A8_CB_COLOR2_INFO:
	case R_0280AC_CB_COLOR3_INFO:
	case R_0280B0_CB_COLOR4_INFO:
	case R_0280B4_CB_COLOR5_INFO:
	case R_0280B8_CB_COLOR6_INFO:
	case R_0280BC_CB_COLOR7_INFO:
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS) &&
		     radeon_cs_packet_next_is_pkt3_nop(p)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
				return -EINVAL;
			}
			tmp = (reg - R_0280A0_CB_COLOR0_INFO) / 4;
			track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
			if (reloc->tiling_flags & RADEON_TILING_MACRO) {
				ib[idx] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_2D_TILED_THIN1);
				track->cb_color_info[tmp] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_2D_TILED_THIN1);
			} else if (reloc->tiling_flags & RADEON_TILING_MICRO) {
				ib[idx] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_1D_TILED_THIN1);
				track->cb_color_info[tmp] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_1D_TILED_THIN1);
			}
		} else {
			tmp = (reg - R_0280A0_CB_COLOR0_INFO) / 4;
			track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
		}
		track->cb_dirty = true;
		break;
	case R_028080_CB_COLOR0_VIEW:
	case R_028084_CB_COLOR1_VIEW:
	case R_028088_CB_COLOR2_VIEW:
	case R_02808C_CB_COLOR3_VIEW:
	case R_028090_CB_COLOR4_VIEW:
	case R_028094_CB_COLOR5_VIEW:
	case R_028098_CB_COLOR6_VIEW:
	case R_02809C_CB_COLOR7_VIEW:
		tmp = (reg - R_028080_CB_COLOR0_VIEW) / 4;
		track->cb_color_view[tmp] = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case R_028060_CB_COLOR0_SIZE:
	case R_028064_CB_COLOR1_SIZE:
	case R_028068_CB_COLOR2_SIZE:
	case R_02806C_CB_COLOR3_SIZE:
	case R_028070_CB_COLOR4_SIZE:
	case R_028074_CB_COLOR5_SIZE:
	case R_028078_CB_COLOR6_SIZE:
	case R_02807C_CB_COLOR7_SIZE:
		tmp = (reg - R_028060_CB_COLOR0_SIZE) / 4;
		track->cb_color_size[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_size_idx[tmp] = idx;
		track->cb_dirty = true;
		break;
		/* This register were added late, there is userspace
		 * which does provide relocation for those but set
		 * 0 offset. In order to avoid breaking old userspace
		 * we detect this and set address to point to last
		 * CB_COLOR0_BASE, note that if userspace doesn't set
		 * CB_COLOR0_BASE before this register we will report
		 * error. Old userspace always set CB_COLOR0_BASE
		 * before any of this.
		 */
	case R_0280E0_CB_COLOR0_FRAG:
	case R_0280E4_CB_COLOR1_FRAG:
	case R_0280E8_CB_COLOR2_FRAG:
	case R_0280EC_CB_COLOR3_FRAG:
	case R_0280F0_CB_COLOR4_FRAG:
	case R_0280F4_CB_COLOR5_FRAG:
	case R_0280F8_CB_COLOR6_FRAG:
	case R_0280FC_CB_COLOR7_FRAG:
		tmp = (reg - R_0280E0_CB_COLOR0_FRAG) / 4;
		if (!radeon_cs_packet_next_is_pkt3_nop(p)) {
			if (!track->cb_color_base_last[tmp]) {
				dev_err(p->dev, "Broken old userspace ? no cb_color0_base supplied before trying to write 0x%08X\n", reg);
				return -EINVAL;
			}
			track->cb_color_frag_bo[tmp] = track->cb_color_bo[tmp];
			track->cb_color_frag_offset[tmp] = track->cb_color_bo_offset[tmp];
			ib[idx] = track->cb_color_base_last[tmp];
		} else {
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
				return -EINVAL;
			}
			track->cb_color_frag_bo[tmp] = reloc->robj;
			track->cb_color_frag_offset[tmp] = (u64)ib[idx] << 8;
			ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		}
		if (G_0280A0_TILE_MODE(track->cb_color_info[tmp])) {
			track->cb_dirty = true;
		}
		break;
	case R_0280C0_CB_COLOR0_TILE:
	case R_0280C4_CB_COLOR1_TILE:
	case R_0280C8_CB_COLOR2_TILE:
	case R_0280CC_CB_COLOR3_TILE:
	case R_0280D0_CB_COLOR4_TILE:
	case R_0280D4_CB_COLOR5_TILE:
	case R_0280D8_CB_COLOR6_TILE:
	case R_0280DC_CB_COLOR7_TILE:
		tmp = (reg - R_0280C0_CB_COLOR0_TILE) / 4;
		if (!radeon_cs_packet_next_is_pkt3_nop(p)) {
			if (!track->cb_color_base_last[tmp]) {
				dev_err(p->dev, "Broken old userspace ? no cb_color0_base supplied before trying to write 0x%08X\n", reg);
				return -EINVAL;
			}
			track->cb_color_tile_bo[tmp] = track->cb_color_bo[tmp];
			track->cb_color_tile_offset[tmp] = track->cb_color_bo_offset[tmp];
			ib[idx] = track->cb_color_base_last[tmp];
		} else {
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
				return -EINVAL;
			}
			track->cb_color_tile_bo[tmp] = reloc->robj;
			track->cb_color_tile_offset[tmp] = (u64)ib[idx] << 8;
			ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		}
		if (G_0280A0_TILE_MODE(track->cb_color_info[tmp])) {
			track->cb_dirty = true;
		}
		break;
	case R_028100_CB_COLOR0_MASK:
	case R_028104_CB_COLOR1_MASK:
	case R_028108_CB_COLOR2_MASK:
	case R_02810C_CB_COLOR3_MASK:
	case R_028110_CB_COLOR4_MASK:
	case R_028114_CB_COLOR5_MASK:
	case R_028118_CB_COLOR6_MASK:
	case R_02811C_CB_COLOR7_MASK:
		tmp = (reg - R_028100_CB_COLOR0_MASK) / 4;
		track->cb_color_mask[tmp] = radeon_get_ib_value(p, idx);
		if (G_0280A0_TILE_MODE(track->cb_color_info[tmp])) {
			track->cb_dirty = true;
		}
		break;
	case CB_COLOR0_BASE:
	case CB_COLOR1_BASE:
	case CB_COLOR2_BASE:
	case CB_COLOR3_BASE:
	case CB_COLOR4_BASE:
	case CB_COLOR5_BASE:
	case CB_COLOR6_BASE:
	case CB_COLOR7_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - CB_COLOR0_BASE) / 4;
		track->cb_color_bo_offset[tmp] = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->cb_color_base_last[tmp] = ib[idx];
		track->cb_color_bo[tmp] = reloc->robj;
		track->cb_color_bo_mc[tmp] = reloc->gpu_offset;
		track->cb_dirty = true;
		break;
	case DB_DEPTH_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_offset = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->db_bo = reloc->robj;
		track->db_bo_mc = reloc->gpu_offset;
		track->db_dirty = true;
		break;
	case DB_HTILE_DATA_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->htile_offset = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->htile_bo = reloc->robj;
		track->db_dirty = true;
		break;
	case DB_HTILE_SURFACE:
		track->htile_surface = radeon_get_ib_value(p, idx);
		/* force 8x8 htile width and height */
		ib[idx] |= 3;
		track->db_dirty = true;
		break;
	case SQ_PGM_START_FS:
	case SQ_PGM_START_ES:
	case SQ_PGM_START_VS:
	case SQ_PGM_START_GS:
	case SQ_PGM_START_PS:
	case SQ_ALU_CONST_CACHE_GS_0:
	case SQ_ALU_CONST_CACHE_GS_1:
	case SQ_ALU_CONST_CACHE_GS_2:
	case SQ_ALU_CONST_CACHE_GS_3:
	case SQ_ALU_CONST_CACHE_GS_4:
	case SQ_ALU_CONST_CACHE_GS_5:
	case SQ_ALU_CONST_CACHE_GS_6:
	case SQ_ALU_CONST_CACHE_GS_7:
	case SQ_ALU_CONST_CACHE_GS_8:
	case SQ_ALU_CONST_CACHE_GS_9:
	case SQ_ALU_CONST_CACHE_GS_10:
	case SQ_ALU_CONST_CACHE_GS_11:
	case SQ_ALU_CONST_CACHE_GS_12:
	case SQ_ALU_CONST_CACHE_GS_13:
	case SQ_ALU_CONST_CACHE_GS_14:
	case SQ_ALU_CONST_CACHE_GS_15:
	case SQ_ALU_CONST_CACHE_PS_0:
	case SQ_ALU_CONST_CACHE_PS_1:
	case SQ_ALU_CONST_CACHE_PS_2:
	case SQ_ALU_CONST_CACHE_PS_3:
	case SQ_ALU_CONST_CACHE_PS_4:
	case SQ_ALU_CONST_CACHE_PS_5:
	case SQ_ALU_CONST_CACHE_PS_6:
	case SQ_ALU_CONST_CACHE_PS_7:
	case SQ_ALU_CONST_CACHE_PS_8:
	case SQ_ALU_CONST_CACHE_PS_9:
	case SQ_ALU_CONST_CACHE_PS_10:
	case SQ_ALU_CONST_CACHE_PS_11:
	case SQ_ALU_CONST_CACHE_PS_12:
	case SQ_ALU_CONST_CACHE_PS_13:
	case SQ_ALU_CONST_CACHE_PS_14:
	case SQ_ALU_CONST_CACHE_PS_15:
	case SQ_ALU_CONST_CACHE_VS_0:
	case SQ_ALU_CONST_CACHE_VS_1:
	case SQ_ALU_CONST_CACHE_VS_2:
	case SQ_ALU_CONST_CACHE_VS_3:
	case SQ_ALU_CONST_CACHE_VS_4:
	case SQ_ALU_CONST_CACHE_VS_5:
	case SQ_ALU_CONST_CACHE_VS_6:
	case SQ_ALU_CONST_CACHE_VS_7:
	case SQ_ALU_CONST_CACHE_VS_8:
	case SQ_ALU_CONST_CACHE_VS_9:
	case SQ_ALU_CONST_CACHE_VS_10:
	case SQ_ALU_CONST_CACHE_VS_11:
	case SQ_ALU_CONST_CACHE_VS_12:
	case SQ_ALU_CONST_CACHE_VS_13:
	case SQ_ALU_CONST_CACHE_VS_14:
	case SQ_ALU_CONST_CACHE_VS_15:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case SX_MEMORY_EXPORT_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			dev_warn(p->dev, "bad SET_CONFIG_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case SX_MISC:
		track->sx_misc_kill_all_prims = (radeon_get_ib_value(p, idx) & 0x1) != 0;
		break;
	default:
		dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
		return -EINVAL;
	}
	return 0;
}

unsigned r600_mip_minify(unsigned size, unsigned level)
{
	unsigned val;

	val = max(1U, size >> level);
	if (level > 0)
		val = roundup_pow_of_two(val);
	return val;
}

static void r600_texture_size(unsigned nfaces, unsigned blevel, unsigned llevel,
			      unsigned w0, unsigned h0, unsigned d0, unsigned nsamples, unsigned format,
			      unsigned block_align, unsigned height_align, unsigned base_align,
			      unsigned *l0_size, unsigned *mipmap_size)
{
	unsigned offset, i, level;
	unsigned width, height, depth, size;
	unsigned blocksize;
	unsigned nbx, nby;
	unsigned nlevels = llevel - blevel + 1;

	*l0_size = -1;
	blocksize = r600_fmt_get_blocksize(format);

	w0 = r600_mip_minify(w0, 0);
	h0 = r600_mip_minify(h0, 0);
	d0 = r600_mip_minify(d0, 0);
	for(i = 0, offset = 0, level = blevel; i < nlevels; i++, level++) {
		width = r600_mip_minify(w0, i);
		nbx = r600_fmt_get_nblocksx(format, width);

		nbx = round_up(nbx, block_align);

		height = r600_mip_minify(h0, i);
		nby = r600_fmt_get_nblocksy(format, height);
		nby = round_up(nby, height_align);

		depth = r600_mip_minify(d0, i);

		size = nbx * nby * blocksize * nsamples;
		if (nfaces)
			size *= nfaces;
		else
			size *= depth;

		if (i == 0)
			*l0_size = size;

		if (i == 0 || i == 1)
			offset = round_up(offset, base_align);

		offset += size;
	}
	*mipmap_size = offset;
	if (llevel == 0)
		*mipmap_size = *l0_size;
	if (!blevel)
		*mipmap_size -= *l0_size;
}

/**
 * r600_check_texture_resource() - check if register is authorized or not
 * @p: parser structure holding parsing context
 * @idx: index into the cs buffer
 * @texture: texture's bo structure
 * @mipmap: mipmap's bo structure
 *
 * This function will check that the resource has valid field and that
 * the texture and mipmap bo object are big enough to cover this resource.
 */
static int r600_check_texture_resource(struct radeon_cs_parser *p,  u32 idx,
					      struct radeon_bo *texture,
					      struct radeon_bo *mipmap,
					      u64 base_offset,
					      u64 mip_offset,
					      u32 tiling_flags)
{
	struct r600_cs_track *track = p->track;
	u32 dim, nfaces, llevel, blevel, w0, h0, d0;
	u32 word0, word1, l0_size, mipmap_size, word2, word3, word4, word5;
	u32 height_align, pitch, pitch_align, depth_align;
	u32 barray, larray;
	u64 base_align;
	struct array_mode_checker array_check;
	u32 format;
	bool is_array;

	/* on legacy kernel we don't perform advanced check */
	if (p->rdev == NULL)
		return 0;

	/* convert to bytes */
	base_offset <<= 8;
	mip_offset <<= 8;

	word0 = radeon_get_ib_value(p, idx + 0);
	if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
		if (tiling_flags & RADEON_TILING_MACRO)
			word0 |= S_038000_TILE_MODE(V_038000_ARRAY_2D_TILED_THIN1);
		else if (tiling_flags & RADEON_TILING_MICRO)
			word0 |= S_038000_TILE_MODE(V_038000_ARRAY_1D_TILED_THIN1);
	}
	word1 = radeon_get_ib_value(p, idx + 1);
	word2 = radeon_get_ib_value(p, idx + 2) << 8;
	word3 = radeon_get_ib_value(p, idx + 3) << 8;
	word4 = radeon_get_ib_value(p, idx + 4);
	word5 = radeon_get_ib_value(p, idx + 5);
	dim = G_038000_DIM(word0);
	w0 = G_038000_TEX_WIDTH(word0) + 1;
	pitch = (G_038000_PITCH(word0) + 1) * 8;
	h0 = G_038004_TEX_HEIGHT(word1) + 1;
	d0 = G_038004_TEX_DEPTH(word1);
	format = G_038004_DATA_FORMAT(word1);
	blevel = G_038010_BASE_LEVEL(word4);
	llevel = G_038014_LAST_LEVEL(word5);
	/* pitch in texels */
	array_check.array_mode = G_038000_TILE_MODE(word0);
	array_check.group_size = track->group_size;
	array_check.nbanks = track->nbanks;
	array_check.npipes = track->npipes;
	array_check.nsamples = 1;
	array_check.blocksize = r600_fmt_get_blocksize(format);
	nfaces = 1;
	is_array = false;
	switch (dim) {
	case V_038000_SQ_TEX_DIM_1D:
	case V_038000_SQ_TEX_DIM_2D:
	case V_038000_SQ_TEX_DIM_3D:
		break;
	case V_038000_SQ_TEX_DIM_CUBEMAP:
		if (p->family >= CHIP_RV770)
			nfaces = 8;
		else
			nfaces = 6;
		break;
	case V_038000_SQ_TEX_DIM_1D_ARRAY:
	case V_038000_SQ_TEX_DIM_2D_ARRAY:
		is_array = true;
		break;
	case V_038000_SQ_TEX_DIM_2D_ARRAY_MSAA:
		is_array = true;
		/* fall through */
	case V_038000_SQ_TEX_DIM_2D_MSAA:
		array_check.nsamples = 1 << llevel;
		llevel = 0;
		break;
	default:
		dev_warn(p->dev, "this kernel doesn't support %d texture dim\n", G_038000_DIM(word0));
		return -EINVAL;
	}
	if (!r600_fmt_is_valid_texture(format, p->family)) {
		dev_warn(p->dev, "%s:%d texture invalid format %d\n",
			 __func__, __LINE__, format);
		return -EINVAL;
	}

	if (r600_get_array_mode_alignment(&array_check,
					  &pitch_align, &height_align, &depth_align, &base_align)) {
		dev_warn(p->dev, "%s:%d tex array mode (%d) invalid\n",
			 __func__, __LINE__, G_038000_TILE_MODE(word0));
		return -EINVAL;
	}

	/* XXX check height as well... */

	if (!IS_ALIGNED(pitch, pitch_align)) {
		dev_warn(p->dev, "%s:%d tex pitch (%d, 0x%x, %d) invalid\n",
			 __func__, __LINE__, pitch, pitch_align, G_038000_TILE_MODE(word0));
		return -EINVAL;
	}
	if (!IS_ALIGNED(base_offset, base_align)) {
		dev_warn(p->dev, "%s:%d tex base offset (0x%llx, 0x%llx, %d) invalid\n",
			 __func__, __LINE__, base_offset, base_align, G_038000_TILE_MODE(word0));
		return -EINVAL;
	}
	if (!IS_ALIGNED(mip_offset, base_align)) {
		dev_warn(p->dev, "%s:%d tex mip offset (0x%llx, 0x%llx, %d) invalid\n",
			 __func__, __LINE__, mip_offset, base_align, G_038000_TILE_MODE(word0));
		return -EINVAL;
	}

	if (blevel > llevel) {
		dev_warn(p->dev, "texture blevel %d > llevel %d\n",
			 blevel, llevel);
	}
	if (is_array) {
		barray = G_038014_BASE_ARRAY(word5);
		larray = G_038014_LAST_ARRAY(word5);

		nfaces = larray - barray + 1;
	}
	r600_texture_size(nfaces, blevel, llevel, w0, h0, d0, array_check.nsamples, format,
			  pitch_align, height_align, base_align,
			  &l0_size, &mipmap_size);
	/* using get ib will give us the offset into the texture bo */
	if ((l0_size + word2) > radeon_bo_size(texture)) {
		dev_warn(p->dev, "texture bo too small ((%d %d) (%d %d) %d %d %d -> %d have %ld)\n",
			 w0, h0, pitch_align, height_align,
			 array_check.array_mode, format, word2,
			 l0_size, radeon_bo_size(texture));
		dev_warn(p->dev, "alignments %d %d %d %lld\n", pitch, pitch_align, height_align, base_align);
		return -EINVAL;
	}
	/* using get ib will give us the offset into the mipmap bo */
	if ((mipmap_size + word3) > radeon_bo_size(mipmap)) {
		/*dev_warn(p->dev, "mipmap bo too small (%d %d %d %d %d %d -> %d have %ld)\n",
		  w0, h0, format, blevel, nlevels, word3, mipmap_size, radeon_bo_size(texture));*/
	}
	return 0;
}

static bool r600_is_safe_reg(struct radeon_cs_parser *p, u32 reg, u32 idx)
{
	u32 m, i;

	i = (reg >> 7);
	if (i >= ARRAY_SIZE(r600_reg_safe_bm)) {
		dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
		return false;
	}
	m = 1 << ((reg >> 2) & 31);
	if (!(r600_reg_safe_bm[i] & m))
		return true;
	dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
	return false;
}

static int r600_packet3_check(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt)
{
	struct radeon_bo_list *reloc;
	struct r600_cs_track *track;
	volatile u32 *ib;
	unsigned idx;
	unsigned i;
	unsigned start_reg, end_reg, reg;
	int r;
	u32 idx_value;

	track = (struct r600_cs_track *)p->track;
	ib = p->ib.ptr;
	idx = pkt->idx + 1;
	idx_value = radeon_get_ib_value(p, idx);

	switch (pkt->opcode) {
	case PACKET3_SET_PREDICATION:
	{
		int pred_op;
		int tmp;
		uint64_t offset;

		if (pkt->count != 1) {
			DRM_ERROR("bad SET PREDICATION\n");
			return -EINVAL;
		}

		tmp = radeon_get_ib_value(p, idx + 1);
		pred_op = (tmp >> 16) & 0x7;

		/* for the clear predicate operation */
		if (pred_op == 0)
			return 0;

		if (pred_op > 2) {
			DRM_ERROR("bad SET PREDICATION operation %d\n", pred_op);
			return -EINVAL;
		}

		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			DRM_ERROR("bad SET PREDICATION\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
		         (idx_value & 0xfffffff0) +
		         ((u64)(tmp & 0xff) << 32);

		ib[idx + 0] = offset;
		ib[idx + 1] = (tmp & 0xffffff00) | (upper_32_bits(offset) & 0xff);
	}
	break;

	case PACKET3_START_3D_CMDBUF:
		if (p->family >= CHIP_RV770 || pkt->count) {
			DRM_ERROR("bad START_3D\n");
			return -EINVAL;
		}
		break;
	case PACKET3_CONTEXT_CONTROL:
		if (pkt->count != 1) {
			DRM_ERROR("bad CONTEXT_CONTROL\n");
			return -EINVAL;
		}
		break;
	case PACKET3_INDEX_TYPE:
	case PACKET3_NUM_INSTANCES:
		if (pkt->count) {
			DRM_ERROR("bad INDEX_TYPE/NUM_INSTANCES\n");
			return -EINVAL;
		}
		break;
	case PACKET3_DRAW_INDEX:
	{
		uint64_t offset;
		if (pkt->count != 3) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
		         idx_value +
		         ((u64)(radeon_get_ib_value(p, idx+1) & 0xff) << 32);

		ib[idx+0] = offset;
		ib[idx+1] = upper_32_bits(offset) & 0xff;

		r = r600_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	}
	case PACKET3_DRAW_INDEX_AUTO:
		if (pkt->count != 1) {
			DRM_ERROR("bad DRAW_INDEX_AUTO\n");
			return -EINVAL;
		}
		r = r600_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_IMMD_BE:
	case PACKET3_DRAW_INDEX_IMMD:
		if (pkt->count < 2) {
			DRM_ERROR("bad DRAW_INDEX_IMMD\n");
			return -EINVAL;
		}
		r = r600_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_WAIT_REG_MEM:
		if (pkt->count != 5) {
			DRM_ERROR("bad WAIT_REG_MEM\n");
			return -EINVAL;
		}
		/* bit 4 is reg (0) or mem (1) */
		if (idx_value & 0x10) {
			uint64_t offset;

			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad WAIT_REG_MEM\n");
				return -EINVAL;
			}

			offset = reloc->gpu_offset +
			         (radeon_get_ib_value(p, idx+1) & 0xfffffff0) +
			         ((u64)(radeon_get_ib_value(p, idx+2) & 0xff) << 32);

			ib[idx+1] = (ib[idx+1] & 0x3) | (offset & 0xfffffff0);
			ib[idx+2] = upper_32_bits(offset) & 0xff;
		} else if (idx_value & 0x100) {
			DRM_ERROR("cannot use PFP on REG wait\n");
			return -EINVAL;
		}
		break;
	case PACKET3_CP_DMA:
	{
		u32 command, size;
		u64 offset, tmp;
		if (pkt->count != 4) {
			DRM_ERROR("bad CP DMA\n");
			return -EINVAL;
		}
		command = radeon_get_ib_value(p, idx+4);
		size = command & 0x1fffff;
		if (command & PACKET3_CP_DMA_CMD_SAS) {
			/* src address space is register */
			DRM_ERROR("CP DMA SAS not supported\n");
			return -EINVAL;
		} else {
			if (command & PACKET3_CP_DMA_CMD_SAIC) {
				DRM_ERROR("CP DMA SAIC only supported for registers\n");
				return -EINVAL;
			}
			/* src address space is memory */
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad CP DMA SRC\n");
				return -EINVAL;
			}

			tmp = radeon_get_ib_value(p, idx) +
				((u64)(radeon_get_ib_value(p, idx+1) & 0xff) << 32);

			offset = reloc->gpu_offset + tmp;

			if ((tmp + size) > radeon_bo_size(reloc->robj)) {
				dev_warn(p->dev, "CP DMA src buffer too small (%llu %lu)\n",
					 tmp + size, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}

			ib[idx] = offset;
			ib[idx+1] = (ib[idx+1] & 0xffffff00) | (upper_32_bits(offset) & 0xff);
		}
		if (command & PACKET3_CP_DMA_CMD_DAS) {
			/* dst address space is register */
			DRM_ERROR("CP DMA DAS not supported\n");
			return -EINVAL;
		} else {
			/* dst address space is memory */
			if (command & PACKET3_CP_DMA_CMD_DAIC) {
				DRM_ERROR("CP DMA DAIC only supported for registers\n");
				return -EINVAL;
			}
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad CP DMA DST\n");
				return -EINVAL;
			}

			tmp = radeon_get_ib_value(p, idx+2) +
				((u64)(radeon_get_ib_value(p, idx+3) & 0xff) << 32);

			offset = reloc->gpu_offset + tmp;

			if ((tmp + size) > radeon_bo_size(reloc->robj)) {
				dev_warn(p->dev, "CP DMA dst buffer too small (%llu %lu)\n",
					 tmp + size, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}

			ib[idx+2] = offset;
			ib[idx+3] = upper_32_bits(offset) & 0xff;
		}
		break;
	}
	case PACKET3_SURFACE_SYNC:
		if (pkt->count != 3) {
			DRM_ERROR("bad SURFACE_SYNC\n");
			return -EINVAL;
		}
		/* 0xffffffff/0x0 is flush all cache flag */
		if (radeon_get_ib_value(p, idx + 1) != 0xffffffff ||
		    radeon_get_ib_value(p, idx + 2) != 0) {
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad SURFACE_SYNC\n");
				return -EINVAL;
			}
			ib[idx+2] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		}
		break;
	case PACKET3_EVENT_WRITE:
		if (pkt->count != 2 && pkt->count != 0) {
			DRM_ERROR("bad EVENT_WRITE\n");
			return -EINVAL;
		}
		if (pkt->count) {
			uint64_t offset;

			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad EVENT_WRITE\n");
				return -EINVAL;
			}
			offset = reloc->gpu_offset +
			         (radeon_get_ib_value(p, idx+1) & 0xfffffff8) +
			         ((u64)(radeon_get_ib_value(p, idx+2) & 0xff) << 32);

			ib[idx+1] = offset & 0xfffffff8;
			ib[idx+2] = upper_32_bits(offset) & 0xff;
		}
		break;
	case PACKET3_EVENT_WRITE_EOP:
	{
		uint64_t offset;

		if (pkt->count != 4) {
			DRM_ERROR("bad EVENT_WRITE_EOP\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			DRM_ERROR("bad EVENT_WRITE\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
		         (radeon_get_ib_value(p, idx+1) & 0xfffffffc) +
		         ((u64)(radeon_get_ib_value(p, idx+2) & 0xff) << 32);

		ib[idx+1] = offset & 0xfffffffc;
		ib[idx+2] = (ib[idx+2] & 0xffffff00) | (upper_32_bits(offset) & 0xff);
		break;
	}
	case PACKET3_SET_CONFIG_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_OFFSET) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			r = r600_cs_check_reg(p, reg, idx+1+i);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_CONTEXT_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONTEXT_REG_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONTEXT_REG_OFFSET) ||
		    (start_reg >= PACKET3_SET_CONTEXT_REG_END) ||
		    (end_reg >= PACKET3_SET_CONTEXT_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONTEXT_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			r = r600_cs_check_reg(p, reg, idx+1+i);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_RESOURCE:
		if (pkt->count % 7) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		start_reg = (idx_value << 2) + PACKET3_SET_RESOURCE_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_RESOURCE_OFFSET) ||
		    (start_reg >= PACKET3_SET_RESOURCE_END) ||
		    (end_reg >= PACKET3_SET_RESOURCE_END)) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		for (i = 0; i < (pkt->count / 7); i++) {
			struct radeon_bo *texture, *mipmap;
			u32 size, offset, base_offset, mip_offset;

			switch (G__SQ_VTX_CONSTANT_TYPE(radeon_get_ib_value(p, idx+(i*7)+6+1))) {
			case SQ_TEX_VTX_VALID_TEXTURE:
				/* tex base */
				r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				base_offset = (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
				if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
					if (reloc->tiling_flags & RADEON_TILING_MACRO)
						ib[idx+1+(i*7)+0] |= S_038000_TILE_MODE(V_038000_ARRAY_2D_TILED_THIN1);
					else if (reloc->tiling_flags & RADEON_TILING_MICRO)
						ib[idx+1+(i*7)+0] |= S_038000_TILE_MODE(V_038000_ARRAY_1D_TILED_THIN1);
				}
				texture = reloc->robj;
				/* tex mip base */
				r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				mip_offset = (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
				mipmap = reloc->robj;
				r = r600_check_texture_resource(p,  idx+(i*7)+1,
								texture, mipmap,
								base_offset + radeon_get_ib_value(p, idx+1+(i*7)+2),
								mip_offset + radeon_get_ib_value(p, idx+1+(i*7)+3),
								reloc->tiling_flags);
				if (r)
					return r;
				ib[idx+1+(i*7)+2] += base_offset;
				ib[idx+1+(i*7)+3] += mip_offset;
				break;
			case SQ_TEX_VTX_VALID_BUFFER:
			{
				uint64_t offset64;
				/* vtx base */
				r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				offset = radeon_get_ib_value(p, idx+1+(i*7)+0);
				size = radeon_get_ib_value(p, idx+1+(i*7)+1) + 1;
				if (p->rdev && (size + offset) > radeon_bo_size(reloc->robj)) {
					/* force size to size of the buffer */
					dev_warn(p->dev, "vbo resource seems too big (%d) for the bo (%ld)\n",
						 size + offset, radeon_bo_size(reloc->robj));
					ib[idx+1+(i*7)+1] = radeon_bo_size(reloc->robj) - offset;
				}

				offset64 = reloc->gpu_offset + offset;
				ib[idx+1+(i*8)+0] = offset64;
				ib[idx+1+(i*8)+2] = (ib[idx+1+(i*8)+2] & 0xffffff00) |
						    (upper_32_bits(offset64) & 0xff);
				break;
			}
			case SQ_TEX_VTX_INVALID_TEXTURE:
			case SQ_TEX_VTX_INVALID_BUFFER:
			default:
				DRM_ERROR("bad SET_RESOURCE\n");
				return -EINVAL;
			}
		}
		break;
	case PACKET3_SET_ALU_CONST:
		if (track->sq_config & DX9_CONSTS) {
			start_reg = (idx_value << 2) + PACKET3_SET_ALU_CONST_OFFSET;
			end_reg = 4 * pkt->count + start_reg - 4;
			if ((start_reg < PACKET3_SET_ALU_CONST_OFFSET) ||
			    (start_reg >= PACKET3_SET_ALU_CONST_END) ||
			    (end_reg >= PACKET3_SET_ALU_CONST_END)) {
				DRM_ERROR("bad SET_ALU_CONST\n");
				return -EINVAL;
			}
		}
		break;
	case PACKET3_SET_BOOL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_BOOL_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_BOOL_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_BOOL_CONST_END) ||
		    (end_reg >= PACKET3_SET_BOOL_CONST_END)) {
			DRM_ERROR("bad SET_BOOL_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_LOOP_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_LOOP_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_LOOP_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_LOOP_CONST_END) ||
		    (end_reg >= PACKET3_SET_LOOP_CONST_END)) {
			DRM_ERROR("bad SET_LOOP_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_CTL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_CTL_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CTL_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_CTL_CONST_END) ||
		    (end_reg >= PACKET3_SET_CTL_CONST_END)) {
			DRM_ERROR("bad SET_CTL_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_SAMPLER:
		if (pkt->count % 3) {
			DRM_ERROR("bad SET_SAMPLER\n");
			return -EINVAL;
		}
		start_reg = (idx_value << 2) + PACKET3_SET_SAMPLER_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_SAMPLER_OFFSET) ||
		    (start_reg >= PACKET3_SET_SAMPLER_END) ||
		    (end_reg >= PACKET3_SET_SAMPLER_END)) {
			DRM_ERROR("bad SET_SAMPLER\n");
			return -EINVAL;
		}
		break;
	case PACKET3_STRMOUT_BASE_UPDATE:
		/* RS780 and RS880 also need this */
		if (p->family < CHIP_RS780) {
			DRM_ERROR("STRMOUT_BASE_UPDATE only supported on 7xx\n");
			return -EINVAL;
		}
		if (pkt->count != 1) {
			DRM_ERROR("bad STRMOUT_BASE_UPDATE packet count\n");
			return -EINVAL;
		}
		if (idx_value > 3) {
			DRM_ERROR("bad STRMOUT_BASE_UPDATE index\n");
			return -EINVAL;
		}
		{
			u64 offset;

			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad STRMOUT_BASE_UPDATE reloc\n");
				return -EINVAL;
			}

			if (reloc->robj != track->vgt_strmout_bo[idx_value]) {
				DRM_ERROR("bad STRMOUT_BASE_UPDATE, bo does not match\n");
				return -EINVAL;
			}

			offset = radeon_get_ib_value(p, idx+1) << 8;
			if (offset != track->vgt_strmout_bo_offset[idx_value]) {
				DRM_ERROR("bad STRMOUT_BASE_UPDATE, bo offset does not match: 0x%llx, 0x%x\n",
					  offset, track->vgt_strmout_bo_offset[idx_value]);
				return -EINVAL;
			}

			if ((offset + 4) > radeon_bo_size(reloc->robj)) {
				DRM_ERROR("bad STRMOUT_BASE_UPDATE bo too small: 0x%llx, 0x%lx\n",
					  offset + 4, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}
			ib[idx+1] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		}
		break;
	case PACKET3_SURFACE_BASE_UPDATE:
		if (p->family >= CHIP_RV770 || p->family == CHIP_R600) {
			DRM_ERROR("bad SURFACE_BASE_UPDATE\n");
			return -EINVAL;
		}
		if (pkt->count) {
			DRM_ERROR("bad SURFACE_BASE_UPDATE\n");
			return -EINVAL;
		}
		break;
	case PACKET3_STRMOUT_BUFFER_UPDATE:
		if (pkt->count != 4) {
			DRM_ERROR("bad STRMOUT_BUFFER_UPDATE (invalid count)\n");
			return -EINVAL;
		}
		/* Updating memory at DST_ADDRESS. */
		if (idx_value & 0x1) {
			u64 offset;
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad STRMOUT_BUFFER_UPDATE (missing dst reloc)\n");
				return -EINVAL;
			}
			offset = radeon_get_ib_value(p, idx+1);
			offset += ((u64)(radeon_get_ib_value(p, idx+2) & 0xff)) << 32;
			if ((offset + 4) > radeon_bo_size(reloc->robj)) {
				DRM_ERROR("bad STRMOUT_BUFFER_UPDATE dst bo too small: 0x%llx, 0x%lx\n",
					  offset + 4, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}
			offset += reloc->gpu_offset;
			ib[idx+1] = offset;
			ib[idx+2] = upper_32_bits(offset) & 0xff;
		}
		/* Reading data from SRC_ADDRESS. */
		if (((idx_value >> 1) & 0x3) == 2) {
			u64 offset;
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad STRMOUT_BUFFER_UPDATE (missing src reloc)\n");
				return -EINVAL;
			}
			offset = radeon_get_ib_value(p, idx+3);
			offset += ((u64)(radeon_get_ib_value(p, idx+4) & 0xff)) << 32;
			if ((offset + 4) > radeon_bo_size(reloc->robj)) {
				DRM_ERROR("bad STRMOUT_BUFFER_UPDATE src bo too small: 0x%llx, 0x%lx\n",
					  offset + 4, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}
			offset += reloc->gpu_offset;
			ib[idx+3] = offset;
			ib[idx+4] = upper_32_bits(offset) & 0xff;
		}
		break;
	case PACKET3_MEM_WRITE:
	{
		u64 offset;

		if (pkt->count != 3) {
			DRM_ERROR("bad MEM_WRITE (invalid count)\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
		if (r) {
			DRM_ERROR("bad MEM_WRITE (missing reloc)\n");
			return -EINVAL;
		}
		offset = radeon_get_ib_value(p, idx+0);
		offset += ((u64)(radeon_get_ib_value(p, idx+1) & 0xff)) << 32UL;
		if (offset & 0x7) {
			DRM_ERROR("bad MEM_WRITE (address not qwords aligned)\n");
			return -EINVAL;
		}
		if ((offset + 8) > radeon_bo_size(reloc->robj)) {
			DRM_ERROR("bad MEM_WRITE bo too small: 0x%llx, 0x%lx\n",
				  offset + 8, radeon_bo_size(reloc->robj));
			return -EINVAL;
		}
		offset += reloc->gpu_offset;
		ib[idx+0] = offset;
		ib[idx+1] = upper_32_bits(offset) & 0xff;
		break;
	}
	case PACKET3_COPY_DW:
		if (pkt->count != 4) {
			DRM_ERROR("bad COPY_DW (invalid count)\n");
			return -EINVAL;
		}
		if (idx_value & 0x1) {
			u64 offset;
			/* SRC is memory. */
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad COPY_DW (missing src reloc)\n");
				return -EINVAL;
			}
			offset = radeon_get_ib_value(p, idx+1);
			offset += ((u64)(radeon_get_ib_value(p, idx+2) & 0xff)) << 32;
			if ((offset + 4) > radeon_bo_size(reloc->robj)) {
				DRM_ERROR("bad COPY_DW src bo too small: 0x%llx, 0x%lx\n",
					  offset + 4, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}
			offset += reloc->gpu_offset;
			ib[idx+1] = offset;
			ib[idx+2] = upper_32_bits(offset) & 0xff;
		} else {
			/* SRC is a reg. */
			reg = radeon_get_ib_value(p, idx+1) << 2;
			if (!r600_is_safe_reg(p, reg, idx+1))
				return -EINVAL;
		}
		if (idx_value & 0x2) {
			u64 offset;
			/* DST is memory. */
			r = radeon_cs_packet_next_reloc(p, &reloc, r600_nomm);
			if (r) {
				DRM_ERROR("bad COPY_DW (missing dst reloc)\n");
				return -EINVAL;
			}
			offset = radeon_get_ib_value(p, idx+3);
			offset += ((u64)(radeon_get_ib_value(p, idx+4) & 0xff)) << 32;
			if ((offset + 4) > radeon_bo_size(reloc->robj)) {
				DRM_ERROR("bad COPY_DW dst bo too small: 0x%llx, 0x%lx\n",
					  offset + 4, radeon_bo_size(reloc->robj));
				return -EINVAL;
			}
			offset += reloc->gpu_offset;
			ib[idx+3] = offset;
			ib[idx+4] = upper_32_bits(offset) & 0xff;
		} else {
			/* DST is a reg. */
			reg = radeon_get_ib_value(p, idx+3) << 2;
			if (!r600_is_safe_reg(p, reg, idx+3))
				return -EINVAL;
		}
		break;
	case PACKET3_NOP:
		break;
	default:
		DRM_ERROR("Packet3 opcode %x not supported\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int r600_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	struct r600_cs_track *track;
	int r;

	if (p->track == NULL) {
		/* initialize tracker, we are in kms */
		track = kzalloc(sizeof(*track), GFP_KERNEL);
		if (track == NULL)
			return -ENOMEM;
		r600_cs_track_init(track);
		if (p->rdev->family < CHIP_RV770) {
			track->npipes = p->rdev->config.r600.tiling_npipes;
			track->nbanks = p->rdev->config.r600.tiling_nbanks;
			track->group_size = p->rdev->config.r600.tiling_group_size;
		} else if (p->rdev->family <= CHIP_RV740) {
			track->npipes = p->rdev->config.rv770.tiling_npipes;
			track->nbanks = p->rdev->config.rv770.tiling_nbanks;
			track->group_size = p->rdev->config.rv770.tiling_group_size;
		}
		p->track = track;
	}
	do {
		r = radeon_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			kfree(p->track);
			p->track = NULL;
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			r = r600_cs_parse_packet0(p, &pkt);
			break;
		case RADEON_PACKET_TYPE2:
			break;
		case RADEON_PACKET_TYPE3:
			r = r600_packet3_check(p, &pkt);
			break;
		default:
			DRM_ERROR("Unknown packet type %d !\n", pkt.type);
			kfree(p->track);
			p->track = NULL;
			return -EINVAL;
		}
		if (r) {
			kfree(p->track);
			p->track = NULL;
			return r;
		}
	} while (p->idx < p->chunk_ib->length_dw);
#if 0
	for (r = 0; r < p->ib.length_dw; r++) {
		printk(KERN_INFO "%05d  0x%08X\n", r, p->ib.ptr[r]);
		mdelay(1);
	}
#endif
	kfree(p->track);
	p->track = NULL;
	return 0;
}

#ifdef CONFIG_DRM_RADEON_UMS

/**
 * cs_parser_fini() - clean parser states
 * @parser:	parser structure holding parsing context.
 * @error:	error number
 *
 * If error is set than unvalidate buffer, otherwise just free memory
 * used by parsing context.
 **/
static void r600_cs_parser_fini(struct radeon_cs_parser *parser, int error)
{
	unsigned i;

	kfree(parser->relocs);
	for (i = 0; i < parser->nchunks; i++)
		drm_free_large(parser->chunks[i].kdata);
	kfree(parser->chunks);
	kfree(parser->chunks_array);
}

static int r600_cs_parser_relocs_legacy(struct radeon_cs_parser *p)
{
	if (p->chunk_relocs == NULL) {
		return 0;
	}
	p->relocs = kzalloc(sizeof(struct radeon_bo_list), GFP_KERNEL);
	if (p->relocs == NULL) {
		return -ENOMEM;
	}
	return 0;
}

int r600_cs_legacy(struct drm_device *dev, void *data, struct drm_file *filp,
			unsigned family, u32 *ib, int *l)
{
	struct radeon_cs_parser parser;
	struct radeon_cs_chunk *ib_chunk;
	struct r600_cs_track *track;
	int r;

	/* initialize tracker */
	track = kzalloc(sizeof(*track), GFP_KERNEL);
	if (track == NULL)
		return -ENOMEM;
	r600_cs_track_init(track);
	r600_cs_legacy_get_tiling_conf(dev, &track->npipes, &track->nbanks, &track->group_size);
	/* initialize parser */
	memset(&parser, 0, sizeof(struct radeon_cs_parser));
	parser.filp = filp;
	parser.dev = &dev->pdev->dev;
	parser.rdev = NULL;
	parser.family = family;
	parser.track = track;
	parser.ib.ptr = ib;
	r = radeon_cs_parser_init(&parser, data);
	if (r) {
		DRM_ERROR("Failed to initialize parser !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	r = r600_cs_parser_relocs_legacy(&parser);
	if (r) {
		DRM_ERROR("Failed to parse relocation !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	/* Copy the packet into the IB, the parser will read from the
	 * input memory (cached) and write to the IB (which can be
	 * uncached). */
	ib_chunk = parser.chunk_ib;
	parser.ib.length_dw = ib_chunk->length_dw;
	*l = parser.ib.length_dw;
	if (copy_from_user(ib, ib_chunk->user_ptr, ib_chunk->length_dw * 4)) {
		r = -EFAULT;
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	r = r600_cs_parse(&parser);
	if (r) {
		DRM_ERROR("Invalid command stream !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	r600_cs_parser_fini(&parser, r);
	return r;
}

void r600_cs_legacy_init(void)
{
	r600_nomm = 1;
}

#endif

/*
 *  DMA
 */
/**
 * r600_dma_cs_next_reloc() - parse next reloc
 * @p:		parser structure holding parsing context.
 * @cs_reloc:		reloc informations
 *
 * Return the next reloc, do bo validation and compute
 * GPU offset using the provided start.
 **/
int r600_dma_cs_next_reloc(struct radeon_cs_parser *p,
			   struct radeon_bo_list **cs_reloc)
{
	struct radeon_cs_chunk *relocs_chunk;
	unsigned idx;

	*cs_reloc = NULL;
	if (p->chunk_relocs == NULL) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}
	relocs_chunk = p->chunk_relocs;
	idx = p->dma_reloc_idx;
	if (idx >= p->nrelocs) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, p->nrelocs);
		return -EINVAL;
	}
	*cs_reloc = &p->relocs[idx];
	p->dma_reloc_idx++;
	return 0;
}

#define GET_DMA_CMD(h) (((h) & 0xf0000000) >> 28)
#define GET_DMA_COUNT(h) ((h) & 0x0000ffff)
#define GET_DMA_T(h) (((h) & 0x00800000) >> 23)

/**
 * r600_dma_cs_parse() - parse the DMA IB
 * @p:		parser structure holding parsing context.
 *
 * Parses the DMA IB from the CS ioctl and updates
 * the GPU addresses based on the reloc information and
 * checks for errors. (R6xx-R7xx)
 * Returns 0 for success and an error on failure.
 **/
int r600_dma_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_chunk *ib_chunk = p->chunk_ib;
	struct radeon_bo_list *src_reloc, *dst_reloc;
	u32 header, cmd, count, tiled;
	volatile u32 *ib = p->ib.ptr;
	u32 idx, idx_value;
	u64 src_offset, dst_offset;
	int r;

	do {
		if (p->idx >= ib_chunk->length_dw) {
			DRM_ERROR("Can not parse packet at %d after CS end %d !\n",
				  p->idx, ib_chunk->length_dw);
			return -EINVAL;
		}
		idx = p->idx;
		header = radeon_get_ib_value(p, idx);
		cmd = GET_DMA_CMD(header);
		count = GET_DMA_COUNT(header);
		tiled = GET_DMA_T(header);

		switch (cmd) {
		case DMA_PACKET_WRITE:
			r = r600_dma_cs_next_reloc(p, &dst_reloc);
			if (r) {
				DRM_ERROR("bad DMA_PACKET_WRITE\n");
				return -EINVAL;
			}
			if (tiled) {
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset <<= 8;

				ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				p->idx += count + 5;
			} else {
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset |= ((u64)(radeon_get_ib_value(p, idx+2) & 0xff)) << 32;

				ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
				ib[idx+2] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				p->idx += count + 3;
			}
			if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
				dev_warn(p->dev, "DMA write buffer too small (%llu %lu)\n",
					 dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
				return -EINVAL;
			}
			break;
		case DMA_PACKET_COPY:
			r = r600_dma_cs_next_reloc(p, &src_reloc);
			if (r) {
				DRM_ERROR("bad DMA_PACKET_COPY\n");
				return -EINVAL;
			}
			r = r600_dma_cs_next_reloc(p, &dst_reloc);
			if (r) {
				DRM_ERROR("bad DMA_PACKET_COPY\n");
				return -EINVAL;
			}
			if (tiled) {
				idx_value = radeon_get_ib_value(p, idx + 2);
				/* detile bit */
				if (idx_value & (1 << 31)) {
					/* tiled src, linear dst */
					src_offset = radeon_get_ib_value(p, idx+1);
					src_offset <<= 8;
					ib[idx+1] += (u32)(src_reloc->gpu_offset >> 8);

					dst_offset = radeon_get_ib_value(p, idx+5);
					dst_offset |= ((u64)(radeon_get_ib_value(p, idx+6) & 0xff)) << 32;
					ib[idx+5] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
					ib[idx+6] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				} else {
					/* linear src, tiled dst */
					src_offset = radeon_get_ib_value(p, idx+5);
					src_offset |= ((u64)(radeon_get_ib_value(p, idx+6) & 0xff)) << 32;
					ib[idx+5] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
					ib[idx+6] += upper_32_bits(src_reloc->gpu_offset) & 0xff;

					dst_offset = radeon_get_ib_value(p, idx+1);
					dst_offset <<= 8;
					ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				}
				p->idx += 7;
			} else {
				if (p->family >= CHIP_RV770) {
					src_offset = radeon_get_ib_value(p, idx+2);
					src_offset |= ((u64)(radeon_get_ib_value(p, idx+4) & 0xff)) << 32;
					dst_offset = radeon_get_ib_value(p, idx+1);
					dst_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0xff)) << 32;

					ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
					ib[idx+2] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
					ib[idx+3] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
					ib[idx+4] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
					p->idx += 5;
				} else {
					src_offset = radeon_get_ib_value(p, idx+2);
					src_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0xff)) << 32;
					dst_offset = radeon_get_ib_value(p, idx+1);
					dst_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0xff0000)) << 16;

					ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
					ib[idx+2] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
					ib[idx+3] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
					ib[idx+3] += (upper_32_bits(dst_reloc->gpu_offset) & 0xff) << 16;
					p->idx += 4;
				}
			}
			if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
				dev_warn(p->dev, "DMA copy src buffer too small (%llu %lu)\n",
					 src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
				return -EINVAL;
			}
			if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
				dev_warn(p->dev, "DMA write dst buffer too small (%llu %lu)\n",
					 dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
				return -EINVAL;
			}
			break;
		case DMA_PACKET_CONSTANT_FILL:
			if (p->family < CHIP_RV770) {
				DRM_ERROR("Constant Fill is 7xx only !\n");
				return -EINVAL;
			}
			r = r600_dma_cs_next_reloc(p, &dst_reloc);
			if (r) {
				DRM_ERROR("bad DMA_PACKET_WRITE\n");
				return -EINVAL;
			}
			dst_offset = radeon_get_ib_value(p, idx+1);
			dst_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0x00ff0000)) << 16;
			if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
				dev_warn(p->dev, "DMA constant fill buffer too small (%llu %lu)\n",
					 dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
				return -EINVAL;
			}
			ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
			ib[idx+3] += (upper_32_bits(dst_reloc->gpu_offset) << 16) & 0x00ff0000;
			p->idx += 4;
			break;
		case DMA_PACKET_NOP:
			p->idx += 1;
			break;
		default:
			DRM_ERROR("Unknown packet type %d at %d !\n", cmd, idx);
			return -EINVAL;
		}
	} while (p->idx < p->chunk_ib->length_dw);
#if 0
	for (r = 0; r < p->ib->length_dw; r++) {
		printk(KERN_INFO "%05d  0x%08X\n", r, p->ib.ptr[r]);
		mdelay(1);
	}
#endif
	return 0;
}
