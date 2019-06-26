/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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

#include "radeon.h"
#include "radeon_asic.h"
#include "evergreend.h"
#include "evergreen_reg_safe.h"
#include "cayman_reg_safe.h"

#define MAX(a,b)                   (((a)>(b))?(a):(b))
#define MIN(a,b)                   (((a)<(b))?(a):(b))

#define REG_SAFE_BM_SIZE ARRAY_SIZE(evergreen_reg_safe_bm)

int r600_dma_cs_next_reloc(struct radeon_cs_parser *p,
			   struct radeon_bo_list **cs_reloc);
struct evergreen_cs_track {
	u32			group_size;
	u32			nbanks;
	u32			npipes;
	u32			row_size;
	/* value we track */
	u32			nsamples;		/* unused */
	struct radeon_bo	*cb_color_bo[12];
	u32			cb_color_bo_offset[12];
	struct radeon_bo	*cb_color_fmask_bo[8];	/* unused */
	struct radeon_bo	*cb_color_cmask_bo[8];	/* unused */
	u32			cb_color_info[12];
	u32			cb_color_view[12];
	u32			cb_color_pitch[12];
	u32			cb_color_slice[12];
	u32			cb_color_slice_idx[12];
	u32			cb_color_attrib[12];
	u32			cb_color_cmask_slice[8];/* unused */
	u32			cb_color_fmask_slice[8];/* unused */
	u32			cb_target_mask;
	u32			cb_shader_mask; /* unused */
	u32			vgt_strmout_config;
	u32			vgt_strmout_buffer_config;
	struct radeon_bo	*vgt_strmout_bo[4];
	u32			vgt_strmout_bo_offset[4];
	u32			vgt_strmout_size[4];
	u32			db_depth_control;
	u32			db_depth_view;
	u32			db_depth_slice;
	u32			db_depth_size;
	u32			db_z_info;
	u32			db_z_read_offset;
	u32			db_z_write_offset;
	struct radeon_bo	*db_z_read_bo;
	struct radeon_bo	*db_z_write_bo;
	u32			db_s_info;
	u32			db_s_read_offset;
	u32			db_s_write_offset;
	struct radeon_bo	*db_s_read_bo;
	struct radeon_bo	*db_s_write_bo;
	bool			sx_misc_kill_all_prims;
	bool			cb_dirty;
	bool			db_dirty;
	bool			streamout_dirty;
	u32			htile_offset;
	u32			htile_surface;
	struct radeon_bo	*htile_bo;
	unsigned long		indirect_draw_buffer_size;
	const unsigned		*reg_safe_bm;
};

static u32 evergreen_cs_get_aray_mode(u32 tiling_flags)
{
	if (tiling_flags & RADEON_TILING_MACRO)
		return ARRAY_2D_TILED_THIN1;
	else if (tiling_flags & RADEON_TILING_MICRO)
		return ARRAY_1D_TILED_THIN1;
	else
		return ARRAY_LINEAR_GENERAL;
}

static u32 evergreen_cs_get_num_banks(u32 nbanks)
{
	switch (nbanks) {
	case 2:
		return ADDR_SURF_2_BANK;
	case 4:
		return ADDR_SURF_4_BANK;
	case 8:
	default:
		return ADDR_SURF_8_BANK;
	case 16:
		return ADDR_SURF_16_BANK;
	}
}

static void evergreen_cs_track_init(struct evergreen_cs_track *track)
{
	int i;

	for (i = 0; i < 8; i++) {
		track->cb_color_fmask_bo[i] = NULL;
		track->cb_color_cmask_bo[i] = NULL;
		track->cb_color_cmask_slice[i] = 0;
		track->cb_color_fmask_slice[i] = 0;
	}

	for (i = 0; i < 12; i++) {
		track->cb_color_bo[i] = NULL;
		track->cb_color_bo_offset[i] = 0xFFFFFFFF;
		track->cb_color_info[i] = 0;
		track->cb_color_view[i] = 0xFFFFFFFF;
		track->cb_color_pitch[i] = 0;
		track->cb_color_slice[i] = 0xfffffff;
		track->cb_color_slice_idx[i] = 0;
	}
	track->cb_target_mask = 0xFFFFFFFF;
	track->cb_shader_mask = 0xFFFFFFFF;
	track->cb_dirty = true;

	track->db_depth_slice = 0xffffffff;
	track->db_depth_view = 0xFFFFC000;
	track->db_depth_size = 0xFFFFFFFF;
	track->db_depth_control = 0xFFFFFFFF;
	track->db_z_info = 0xFFFFFFFF;
	track->db_z_read_offset = 0xFFFFFFFF;
	track->db_z_write_offset = 0xFFFFFFFF;
	track->db_z_read_bo = NULL;
	track->db_z_write_bo = NULL;
	track->db_s_info = 0xFFFFFFFF;
	track->db_s_read_offset = 0xFFFFFFFF;
	track->db_s_write_offset = 0xFFFFFFFF;
	track->db_s_read_bo = NULL;
	track->db_s_write_bo = NULL;
	track->db_dirty = true;
	track->htile_bo = NULL;
	track->htile_offset = 0xFFFFFFFF;
	track->htile_surface = 0;

	for (i = 0; i < 4; i++) {
		track->vgt_strmout_size[i] = 0;
		track->vgt_strmout_bo[i] = NULL;
		track->vgt_strmout_bo_offset[i] = 0xFFFFFFFF;
	}
	track->streamout_dirty = true;
	track->sx_misc_kill_all_prims = false;
}

struct eg_surface {
	/* value gathered from cs */
	unsigned	nbx;
	unsigned	nby;
	unsigned	format;
	unsigned	mode;
	unsigned	nbanks;
	unsigned	bankw;
	unsigned	bankh;
	unsigned	tsplit;
	unsigned	mtilea;
	unsigned	nsamples;
	/* output value */
	unsigned	bpe;
	unsigned	layer_size;
	unsigned	palign;
	unsigned	halign;
	unsigned long	base_align;
};

static int evergreen_surface_check_linear(struct radeon_cs_parser *p,
					  struct eg_surface *surf,
					  const char *prefix)
{
	surf->layer_size = surf->nbx * surf->nby * surf->bpe * surf->nsamples;
	surf->base_align = surf->bpe;
	surf->palign = 1;
	surf->halign = 1;
	return 0;
}

static int evergreen_surface_check_linear_aligned(struct radeon_cs_parser *p,
						  struct eg_surface *surf,
						  const char *prefix)
{
	struct evergreen_cs_track *track = p->track;
	unsigned palign;

	palign = MAX(64, track->group_size / surf->bpe);
	surf->layer_size = surf->nbx * surf->nby * surf->bpe * surf->nsamples;
	surf->base_align = track->group_size;
	surf->palign = palign;
	surf->halign = 1;
	if (surf->nbx & (palign - 1)) {
		if (prefix) {
			dev_warn(p->dev, "%s:%d %s pitch %d invalid must be aligned with %d\n",
				 __func__, __LINE__, prefix, surf->nbx, palign);
		}
		return -EINVAL;
	}
	return 0;
}

static int evergreen_surface_check_1d(struct radeon_cs_parser *p,
				      struct eg_surface *surf,
				      const char *prefix)
{
	struct evergreen_cs_track *track = p->track;
	unsigned palign;

	palign = track->group_size / (8 * surf->bpe * surf->nsamples);
	palign = MAX(8, palign);
	surf->layer_size = surf->nbx * surf->nby * surf->bpe;
	surf->base_align = track->group_size;
	surf->palign = palign;
	surf->halign = 8;
	if ((surf->nbx & (palign - 1))) {
		if (prefix) {
			dev_warn(p->dev, "%s:%d %s pitch %d invalid must be aligned with %d (%d %d %d)\n",
				 __func__, __LINE__, prefix, surf->nbx, palign,
				 track->group_size, surf->bpe, surf->nsamples);
		}
		return -EINVAL;
	}
	if ((surf->nby & (8 - 1))) {
		if (prefix) {
			dev_warn(p->dev, "%s:%d %s height %d invalid must be aligned with 8\n",
				 __func__, __LINE__, prefix, surf->nby);
		}
		return -EINVAL;
	}
	return 0;
}

static int evergreen_surface_check_2d(struct radeon_cs_parser *p,
				      struct eg_surface *surf,
				      const char *prefix)
{
	struct evergreen_cs_track *track = p->track;
	unsigned palign, halign, tileb, slice_pt;
	unsigned mtile_pr, mtile_ps, mtileb;

	tileb = 64 * surf->bpe * surf->nsamples;
	slice_pt = 1;
	if (tileb > surf->tsplit) {
		slice_pt = tileb / surf->tsplit;
	}
	tileb = tileb / slice_pt;
	/* macro tile width & height */
	palign = (8 * surf->bankw * track->npipes) * surf->mtilea;
	halign = (8 * surf->bankh * surf->nbanks) / surf->mtilea;
	mtileb = (palign / 8) * (halign / 8) * tileb;
	mtile_pr = surf->nbx / palign;
	mtile_ps = (mtile_pr * surf->nby) / halign;
	surf->layer_size = mtile_ps * mtileb * slice_pt;
	surf->base_align = (palign / 8) * (halign / 8) * tileb;
	surf->palign = palign;
	surf->halign = halign;

	if ((surf->nbx & (palign - 1))) {
		if (prefix) {
			dev_warn(p->dev, "%s:%d %s pitch %d invalid must be aligned with %d\n",
				 __func__, __LINE__, prefix, surf->nbx, palign);
		}
		return -EINVAL;
	}
	if ((surf->nby & (halign - 1))) {
		if (prefix) {
			dev_warn(p->dev, "%s:%d %s height %d invalid must be aligned with %d\n",
				 __func__, __LINE__, prefix, surf->nby, halign);
		}
		return -EINVAL;
	}

	return 0;
}

static int evergreen_surface_check(struct radeon_cs_parser *p,
				   struct eg_surface *surf,
				   const char *prefix)
{
	/* some common value computed here */
	surf->bpe = r600_fmt_get_blocksize(surf->format);

	switch (surf->mode) {
	case ARRAY_LINEAR_GENERAL:
		return evergreen_surface_check_linear(p, surf, prefix);
	case ARRAY_LINEAR_ALIGNED:
		return evergreen_surface_check_linear_aligned(p, surf, prefix);
	case ARRAY_1D_TILED_THIN1:
		return evergreen_surface_check_1d(p, surf, prefix);
	case ARRAY_2D_TILED_THIN1:
		return evergreen_surface_check_2d(p, surf, prefix);
	default:
		dev_warn(p->dev, "%s:%d %s invalid array mode %d\n",
				__func__, __LINE__, prefix, surf->mode);
		return -EINVAL;
	}
	return -EINVAL;
}

static int evergreen_surface_value_conv_check(struct radeon_cs_parser *p,
					      struct eg_surface *surf,
					      const char *prefix)
{
	switch (surf->mode) {
	case ARRAY_2D_TILED_THIN1:
		break;
	case ARRAY_LINEAR_GENERAL:
	case ARRAY_LINEAR_ALIGNED:
	case ARRAY_1D_TILED_THIN1:
		return 0;
	default:
		dev_warn(p->dev, "%s:%d %s invalid array mode %d\n",
				__func__, __LINE__, prefix, surf->mode);
		return -EINVAL;
	}

	switch (surf->nbanks) {
	case 0: surf->nbanks = 2; break;
	case 1: surf->nbanks = 4; break;
	case 2: surf->nbanks = 8; break;
	case 3: surf->nbanks = 16; break;
	default:
		dev_warn(p->dev, "%s:%d %s invalid number of banks %d\n",
			 __func__, __LINE__, prefix, surf->nbanks);
		return -EINVAL;
	}
	switch (surf->bankw) {
	case 0: surf->bankw = 1; break;
	case 1: surf->bankw = 2; break;
	case 2: surf->bankw = 4; break;
	case 3: surf->bankw = 8; break;
	default:
		dev_warn(p->dev, "%s:%d %s invalid bankw %d\n",
			 __func__, __LINE__, prefix, surf->bankw);
		return -EINVAL;
	}
	switch (surf->bankh) {
	case 0: surf->bankh = 1; break;
	case 1: surf->bankh = 2; break;
	case 2: surf->bankh = 4; break;
	case 3: surf->bankh = 8; break;
	default:
		dev_warn(p->dev, "%s:%d %s invalid bankh %d\n",
			 __func__, __LINE__, prefix, surf->bankh);
		return -EINVAL;
	}
	switch (surf->mtilea) {
	case 0: surf->mtilea = 1; break;
	case 1: surf->mtilea = 2; break;
	case 2: surf->mtilea = 4; break;
	case 3: surf->mtilea = 8; break;
	default:
		dev_warn(p->dev, "%s:%d %s invalid macro tile aspect %d\n",
			 __func__, __LINE__, prefix, surf->mtilea);
		return -EINVAL;
	}
	switch (surf->tsplit) {
	case 0: surf->tsplit = 64; break;
	case 1: surf->tsplit = 128; break;
	case 2: surf->tsplit = 256; break;
	case 3: surf->tsplit = 512; break;
	case 4: surf->tsplit = 1024; break;
	case 5: surf->tsplit = 2048; break;
	case 6: surf->tsplit = 4096; break;
	default:
		dev_warn(p->dev, "%s:%d %s invalid tile split %d\n",
			 __func__, __LINE__, prefix, surf->tsplit);
		return -EINVAL;
	}
	return 0;
}

static int evergreen_cs_track_validate_cb(struct radeon_cs_parser *p, unsigned id)
{
	struct evergreen_cs_track *track = p->track;
	struct eg_surface surf;
	unsigned pitch, slice, mslice;
	unsigned long offset;
	int r;

	mslice = G_028C6C_SLICE_MAX(track->cb_color_view[id]) + 1;
	pitch = track->cb_color_pitch[id];
	slice = track->cb_color_slice[id];
	surf.nbx = (pitch + 1) * 8;
	surf.nby = ((slice + 1) * 64) / surf.nbx;
	surf.mode = G_028C70_ARRAY_MODE(track->cb_color_info[id]);
	surf.format = G_028C70_FORMAT(track->cb_color_info[id]);
	surf.tsplit = G_028C74_TILE_SPLIT(track->cb_color_attrib[id]);
	surf.nbanks = G_028C74_NUM_BANKS(track->cb_color_attrib[id]);
	surf.bankw = G_028C74_BANK_WIDTH(track->cb_color_attrib[id]);
	surf.bankh = G_028C74_BANK_HEIGHT(track->cb_color_attrib[id]);
	surf.mtilea = G_028C74_MACRO_TILE_ASPECT(track->cb_color_attrib[id]);
	surf.nsamples = 1;

	if (!r600_fmt_is_valid_color(surf.format)) {
		dev_warn(p->dev, "%s:%d cb invalid format %d for %d (0x%08x)\n",
			 __func__, __LINE__, surf.format,
			id, track->cb_color_info[id]);
		return -EINVAL;
	}

	r = evergreen_surface_value_conv_check(p, &surf, "cb");
	if (r) {
		return r;
	}

	r = evergreen_surface_check(p, &surf, "cb");
	if (r) {
		dev_warn(p->dev, "%s:%d cb[%d] invalid (0x%08x 0x%08x 0x%08x 0x%08x)\n",
			 __func__, __LINE__, id, track->cb_color_pitch[id],
			 track->cb_color_slice[id], track->cb_color_attrib[id],
			 track->cb_color_info[id]);
		return r;
	}

	offset = track->cb_color_bo_offset[id] << 8;
	if (offset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d cb[%d] bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, id, offset, surf.base_align);
		return -EINVAL;
	}

	offset += surf.layer_size * mslice;
	if (offset > radeon_bo_size(track->cb_color_bo[id])) {
		/* old ddx are broken they allocate bo with w*h*bpp but
		 * program slice with ALIGN(h, 8), catch this and patch
		 * command stream.
		 */
		if (!surf.mode) {
			uint32_t *ib = p->ib.ptr;
			unsigned long tmp, nby, bsize, size, min = 0;

			/* find the height the ddx wants */
			if (surf.nby > 8) {
				min = surf.nby - 8;
			}
			bsize = radeon_bo_size(track->cb_color_bo[id]);
			tmp = track->cb_color_bo_offset[id] << 8;
			for (nby = surf.nby; nby > min; nby--) {
				size = nby * surf.nbx * surf.bpe * surf.nsamples;
				if ((tmp + size * mslice) <= bsize) {
					break;
				}
			}
			if (nby > min) {
				surf.nby = nby;
				slice = ((nby * surf.nbx) / 64) - 1;
				if (!evergreen_surface_check(p, &surf, "cb")) {
					/* check if this one works */
					tmp += surf.layer_size * mslice;
					if (tmp <= bsize) {
						ib[track->cb_color_slice_idx[id]] = slice;
						goto old_ddx_ok;
					}
				}
			}
		}
		dev_warn(p->dev, "%s:%d cb[%d] bo too small (layer size %d, "
			 "offset %d, max layer %d, bo size %ld, slice %d)\n",
			 __func__, __LINE__, id, surf.layer_size,
			track->cb_color_bo_offset[id] << 8, mslice,
			radeon_bo_size(track->cb_color_bo[id]), slice);
		dev_warn(p->dev, "%s:%d problematic surf: (%d %d) (%d %d %d %d %d %d %d)\n",
			 __func__, __LINE__, surf.nbx, surf.nby,
			surf.mode, surf.bpe, surf.nsamples,
			surf.bankw, surf.bankh,
			surf.tsplit, surf.mtilea);
		return -EINVAL;
	}
old_ddx_ok:

	return 0;
}

static int evergreen_cs_track_validate_htile(struct radeon_cs_parser *p,
						unsigned nbx, unsigned nby)
{
	struct evergreen_cs_track *track = p->track;
	unsigned long size;

	if (track->htile_bo == NULL) {
		dev_warn(p->dev, "%s:%d htile enabled without htile surface 0x%08x\n",
				__func__, __LINE__, track->db_z_info);
		return -EINVAL;
	}

	if (G_028ABC_LINEAR(track->htile_surface)) {
		/* pitch must be 16 htiles aligned == 16 * 8 pixel aligned */
		nbx = round_up(nbx, 16 * 8);
		/* height is npipes htiles aligned == npipes * 8 pixel aligned */
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
	return 0;
}

static int evergreen_cs_track_validate_stencil(struct radeon_cs_parser *p)
{
	struct evergreen_cs_track *track = p->track;
	struct eg_surface surf;
	unsigned pitch, slice, mslice;
	unsigned long offset;
	int r;

	mslice = G_028008_SLICE_MAX(track->db_depth_view) + 1;
	pitch = G_028058_PITCH_TILE_MAX(track->db_depth_size);
	slice = track->db_depth_slice;
	surf.nbx = (pitch + 1) * 8;
	surf.nby = ((slice + 1) * 64) / surf.nbx;
	surf.mode = G_028040_ARRAY_MODE(track->db_z_info);
	surf.format = G_028044_FORMAT(track->db_s_info);
	surf.tsplit = G_028044_TILE_SPLIT(track->db_s_info);
	surf.nbanks = G_028040_NUM_BANKS(track->db_z_info);
	surf.bankw = G_028040_BANK_WIDTH(track->db_z_info);
	surf.bankh = G_028040_BANK_HEIGHT(track->db_z_info);
	surf.mtilea = G_028040_MACRO_TILE_ASPECT(track->db_z_info);
	surf.nsamples = 1;

	if (surf.format != 1) {
		dev_warn(p->dev, "%s:%d stencil invalid format %d\n",
			 __func__, __LINE__, surf.format);
		return -EINVAL;
	}
	/* replace by color format so we can use same code */
	surf.format = V_028C70_COLOR_8;

	r = evergreen_surface_value_conv_check(p, &surf, "stencil");
	if (r) {
		return r;
	}

	r = evergreen_surface_check(p, &surf, NULL);
	if (r) {
		/* old userspace doesn't compute proper depth/stencil alignment
		 * check that alignment against a bigger byte per elements and
		 * only report if that alignment is wrong too.
		 */
		surf.format = V_028C70_COLOR_8_8_8_8;
		r = evergreen_surface_check(p, &surf, "stencil");
		if (r) {
			dev_warn(p->dev, "%s:%d stencil invalid (0x%08x 0x%08x 0x%08x 0x%08x)\n",
				 __func__, __LINE__, track->db_depth_size,
				 track->db_depth_slice, track->db_s_info, track->db_z_info);
		}
		return r;
	}

	offset = track->db_s_read_offset << 8;
	if (offset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d stencil read bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, offset, surf.base_align);
		return -EINVAL;
	}
	offset += surf.layer_size * mslice;
	if (offset > radeon_bo_size(track->db_s_read_bo)) {
		dev_warn(p->dev, "%s:%d stencil read bo too small (layer size %d, "
			 "offset %ld, max layer %d, bo size %ld)\n",
			 __func__, __LINE__, surf.layer_size,
			(unsigned long)track->db_s_read_offset << 8, mslice,
			radeon_bo_size(track->db_s_read_bo));
		dev_warn(p->dev, "%s:%d stencil invalid (0x%08x 0x%08x 0x%08x 0x%08x)\n",
			 __func__, __LINE__, track->db_depth_size,
			 track->db_depth_slice, track->db_s_info, track->db_z_info);
		return -EINVAL;
	}

	offset = track->db_s_write_offset << 8;
	if (offset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d stencil write bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, offset, surf.base_align);
		return -EINVAL;
	}
	offset += surf.layer_size * mslice;
	if (offset > radeon_bo_size(track->db_s_write_bo)) {
		dev_warn(p->dev, "%s:%d stencil write bo too small (layer size %d, "
			 "offset %ld, max layer %d, bo size %ld)\n",
			 __func__, __LINE__, surf.layer_size,
			(unsigned long)track->db_s_write_offset << 8, mslice,
			radeon_bo_size(track->db_s_write_bo));
		return -EINVAL;
	}

	/* hyperz */
	if (G_028040_TILE_SURFACE_ENABLE(track->db_z_info)) {
		r = evergreen_cs_track_validate_htile(p, surf.nbx, surf.nby);
		if (r) {
			return r;
		}
	}

	return 0;
}

static int evergreen_cs_track_validate_depth(struct radeon_cs_parser *p)
{
	struct evergreen_cs_track *track = p->track;
	struct eg_surface surf;
	unsigned pitch, slice, mslice;
	unsigned long offset;
	int r;

	mslice = G_028008_SLICE_MAX(track->db_depth_view) + 1;
	pitch = G_028058_PITCH_TILE_MAX(track->db_depth_size);
	slice = track->db_depth_slice;
	surf.nbx = (pitch + 1) * 8;
	surf.nby = ((slice + 1) * 64) / surf.nbx;
	surf.mode = G_028040_ARRAY_MODE(track->db_z_info);
	surf.format = G_028040_FORMAT(track->db_z_info);
	surf.tsplit = G_028040_TILE_SPLIT(track->db_z_info);
	surf.nbanks = G_028040_NUM_BANKS(track->db_z_info);
	surf.bankw = G_028040_BANK_WIDTH(track->db_z_info);
	surf.bankh = G_028040_BANK_HEIGHT(track->db_z_info);
	surf.mtilea = G_028040_MACRO_TILE_ASPECT(track->db_z_info);
	surf.nsamples = 1;

	switch (surf.format) {
	case V_028040_Z_16:
		surf.format = V_028C70_COLOR_16;
		break;
	case V_028040_Z_24:
	case V_028040_Z_32_FLOAT:
		surf.format = V_028C70_COLOR_8_8_8_8;
		break;
	default:
		dev_warn(p->dev, "%s:%d depth invalid format %d\n",
			 __func__, __LINE__, surf.format);
		return -EINVAL;
	}

	r = evergreen_surface_value_conv_check(p, &surf, "depth");
	if (r) {
		dev_warn(p->dev, "%s:%d depth invalid (0x%08x 0x%08x 0x%08x)\n",
			 __func__, __LINE__, track->db_depth_size,
			 track->db_depth_slice, track->db_z_info);
		return r;
	}

	r = evergreen_surface_check(p, &surf, "depth");
	if (r) {
		dev_warn(p->dev, "%s:%d depth invalid (0x%08x 0x%08x 0x%08x)\n",
			 __func__, __LINE__, track->db_depth_size,
			 track->db_depth_slice, track->db_z_info);
		return r;
	}

	offset = track->db_z_read_offset << 8;
	if (offset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d stencil read bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, offset, surf.base_align);
		return -EINVAL;
	}
	offset += surf.layer_size * mslice;
	if (offset > radeon_bo_size(track->db_z_read_bo)) {
		dev_warn(p->dev, "%s:%d depth read bo too small (layer size %d, "
			 "offset %ld, max layer %d, bo size %ld)\n",
			 __func__, __LINE__, surf.layer_size,
			(unsigned long)track->db_z_read_offset << 8, mslice,
			radeon_bo_size(track->db_z_read_bo));
		return -EINVAL;
	}

	offset = track->db_z_write_offset << 8;
	if (offset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d stencil write bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, offset, surf.base_align);
		return -EINVAL;
	}
	offset += surf.layer_size * mslice;
	if (offset > radeon_bo_size(track->db_z_write_bo)) {
		dev_warn(p->dev, "%s:%d depth write bo too small (layer size %d, "
			 "offset %ld, max layer %d, bo size %ld)\n",
			 __func__, __LINE__, surf.layer_size,
			(unsigned long)track->db_z_write_offset << 8, mslice,
			radeon_bo_size(track->db_z_write_bo));
		return -EINVAL;
	}

	/* hyperz */
	if (G_028040_TILE_SURFACE_ENABLE(track->db_z_info)) {
		r = evergreen_cs_track_validate_htile(p, surf.nbx, surf.nby);
		if (r) {
			return r;
		}
	}

	return 0;
}

static int evergreen_cs_track_validate_texture(struct radeon_cs_parser *p,
					       struct radeon_bo *texture,
					       struct radeon_bo *mipmap,
					       unsigned idx)
{
	struct eg_surface surf;
	unsigned long toffset, moffset;
	unsigned dim, llevel, mslice, width, height, depth, i;
	u32 texdw[8];
	int r;

	texdw[0] = radeon_get_ib_value(p, idx + 0);
	texdw[1] = radeon_get_ib_value(p, idx + 1);
	texdw[2] = radeon_get_ib_value(p, idx + 2);
	texdw[3] = radeon_get_ib_value(p, idx + 3);
	texdw[4] = radeon_get_ib_value(p, idx + 4);
	texdw[5] = radeon_get_ib_value(p, idx + 5);
	texdw[6] = radeon_get_ib_value(p, idx + 6);
	texdw[7] = radeon_get_ib_value(p, idx + 7);
	dim = G_030000_DIM(texdw[0]);
	llevel = G_030014_LAST_LEVEL(texdw[5]);
	mslice = G_030014_LAST_ARRAY(texdw[5]) + 1;
	width = G_030000_TEX_WIDTH(texdw[0]) + 1;
	height =  G_030004_TEX_HEIGHT(texdw[1]) + 1;
	depth = G_030004_TEX_DEPTH(texdw[1]) + 1;
	surf.format = G_03001C_DATA_FORMAT(texdw[7]);
	surf.nbx = (G_030000_PITCH(texdw[0]) + 1) * 8;
	surf.nbx = r600_fmt_get_nblocksx(surf.format, surf.nbx);
	surf.nby = r600_fmt_get_nblocksy(surf.format, height);
	surf.mode = G_030004_ARRAY_MODE(texdw[1]);
	surf.tsplit = G_030018_TILE_SPLIT(texdw[6]);
	surf.nbanks = G_03001C_NUM_BANKS(texdw[7]);
	surf.bankw = G_03001C_BANK_WIDTH(texdw[7]);
	surf.bankh = G_03001C_BANK_HEIGHT(texdw[7]);
	surf.mtilea = G_03001C_MACRO_TILE_ASPECT(texdw[7]);
	surf.nsamples = 1;
	toffset = texdw[2] << 8;
	moffset = texdw[3] << 8;

	if (!r600_fmt_is_valid_texture(surf.format, p->family)) {
		dev_warn(p->dev, "%s:%d texture invalid format %d\n",
			 __func__, __LINE__, surf.format);
		return -EINVAL;
	}
	switch (dim) {
	case V_030000_SQ_TEX_DIM_1D:
	case V_030000_SQ_TEX_DIM_2D:
	case V_030000_SQ_TEX_DIM_CUBEMAP:
	case V_030000_SQ_TEX_DIM_1D_ARRAY:
	case V_030000_SQ_TEX_DIM_2D_ARRAY:
		depth = 1;
		break;
	case V_030000_SQ_TEX_DIM_2D_MSAA:
	case V_030000_SQ_TEX_DIM_2D_ARRAY_MSAA:
		surf.nsamples = 1 << llevel;
		llevel = 0;
		depth = 1;
		break;
	case V_030000_SQ_TEX_DIM_3D:
		break;
	default:
		dev_warn(p->dev, "%s:%d texture invalid dimension %d\n",
			 __func__, __LINE__, dim);
		return -EINVAL;
	}

	r = evergreen_surface_value_conv_check(p, &surf, "texture");
	if (r) {
		return r;
	}

	/* align height */
	evergreen_surface_check(p, &surf, NULL);
	surf.nby = ALIGN(surf.nby, surf.halign);

	r = evergreen_surface_check(p, &surf, "texture");
	if (r) {
		dev_warn(p->dev, "%s:%d texture invalid 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 __func__, __LINE__, texdw[0], texdw[1], texdw[4],
			 texdw[5], texdw[6], texdw[7]);
		return r;
	}

	/* check texture size */
	if (toffset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d texture bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, toffset, surf.base_align);
		return -EINVAL;
	}
	if (surf.nsamples <= 1 && moffset & (surf.base_align - 1)) {
		dev_warn(p->dev, "%s:%d mipmap bo base %ld not aligned with %ld\n",
			 __func__, __LINE__, moffset, surf.base_align);
		return -EINVAL;
	}
	if (dim == SQ_TEX_DIM_3D) {
		toffset += surf.layer_size * depth;
	} else {
		toffset += surf.layer_size * mslice;
	}
	if (toffset > radeon_bo_size(texture)) {
		dev_warn(p->dev, "%s:%d texture bo too small (layer size %d, "
			 "offset %ld, max layer %d, depth %d, bo size %ld) (%d %d)\n",
			 __func__, __LINE__, surf.layer_size,
			(unsigned long)texdw[2] << 8, mslice,
			depth, radeon_bo_size(texture),
			surf.nbx, surf.nby);
		return -EINVAL;
	}

	if (!mipmap) {
		if (llevel) {
			dev_warn(p->dev, "%s:%i got NULL MIP_ADDRESS relocation\n",
				 __func__, __LINE__);
			return -EINVAL;
		} else {
			return 0; /* everything's ok */
		}
	}

	/* check mipmap size */
	for (i = 1; i <= llevel; i++) {
		unsigned w, h, d;

		w = r600_mip_minify(width, i);
		h = r600_mip_minify(height, i);
		d = r600_mip_minify(depth, i);
		surf.nbx = r600_fmt_get_nblocksx(surf.format, w);
		surf.nby = r600_fmt_get_nblocksy(surf.format, h);

		switch (surf.mode) {
		case ARRAY_2D_TILED_THIN1:
			if (surf.nbx < surf.palign || surf.nby < surf.halign) {
				surf.mode = ARRAY_1D_TILED_THIN1;
			}
			/* recompute alignment */
			evergreen_surface_check(p, &surf, NULL);
			break;
		case ARRAY_LINEAR_GENERAL:
		case ARRAY_LINEAR_ALIGNED:
		case ARRAY_1D_TILED_THIN1:
			break;
		default:
			dev_warn(p->dev, "%s:%d invalid array mode %d\n",
				 __func__, __LINE__, surf.mode);
			return -EINVAL;
		}
		surf.nbx = ALIGN(surf.nbx, surf.palign);
		surf.nby = ALIGN(surf.nby, surf.halign);

		r = evergreen_surface_check(p, &surf, "mipmap");
		if (r) {
			return r;
		}

		if (dim == SQ_TEX_DIM_3D) {
			moffset += surf.layer_size * d;
		} else {
			moffset += surf.layer_size * mslice;
		}
		if (moffset > radeon_bo_size(mipmap)) {
			dev_warn(p->dev, "%s:%d mipmap [%d] bo too small (layer size %d, "
					"offset %ld, coffset %ld, max layer %d, depth %d, "
					"bo size %ld) level0 (%d %d %d)\n",
					__func__, __LINE__, i, surf.layer_size,
					(unsigned long)texdw[3] << 8, moffset, mslice,
					d, radeon_bo_size(mipmap),
					width, height, depth);
			dev_warn(p->dev, "%s:%d problematic surf: (%d %d) (%d %d %d %d %d %d %d)\n",
				 __func__, __LINE__, surf.nbx, surf.nby,
				surf.mode, surf.bpe, surf.nsamples,
				surf.bankw, surf.bankh,
				surf.tsplit, surf.mtilea);
			return -EINVAL;
		}
	}

	return 0;
}

static int evergreen_cs_track_check(struct radeon_cs_parser *p)
{
	struct evergreen_cs_track *track = p->track;
	unsigned tmp, i;
	int r;
	unsigned buffer_mask = 0;

	/* check streamout */
	if (track->streamout_dirty && track->vgt_strmout_config) {
		for (i = 0; i < 4; i++) {
			if (track->vgt_strmout_config & (1 << i)) {
				buffer_mask |= (track->vgt_strmout_buffer_config >> (i * 4)) & 0xf;
			}
		}

		for (i = 0; i < 4; i++) {
			if (buffer_mask & (1 << i)) {
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

	/* check that we have a cb for each enabled target
	 */
	if (track->cb_dirty) {
		tmp = track->cb_target_mask;
		for (i = 0; i < 8; i++) {
			u32 format = G_028C70_FORMAT(track->cb_color_info[i]);

			if (format != V_028C70_COLOR_INVALID &&
			    (tmp >> (i * 4)) & 0xF) {
				/* at least one component is enabled */
				if (track->cb_color_bo[i] == NULL) {
					dev_warn(p->dev, "%s:%d mask 0x%08X | 0x%08X no cb for %d\n",
						__func__, __LINE__, track->cb_target_mask, track->cb_shader_mask, i);
					return -EINVAL;
				}
				/* check cb */
				r = evergreen_cs_track_validate_cb(p, i);
				if (r) {
					return r;
				}
			}
		}
		track->cb_dirty = false;
	}

	if (track->db_dirty) {
		/* Check stencil buffer */
		if (G_028044_FORMAT(track->db_s_info) != V_028044_STENCIL_INVALID &&
		    G_028800_STENCIL_ENABLE(track->db_depth_control)) {
			r = evergreen_cs_track_validate_stencil(p);
			if (r)
				return r;
		}
		/* Check depth buffer */
		if (G_028040_FORMAT(track->db_z_info) != V_028040_Z_INVALID &&
		    G_028800_Z_ENABLE(track->db_depth_control)) {
			r = evergreen_cs_track_validate_depth(p);
			if (r)
				return r;
		}
		track->db_dirty = false;
	}

	return 0;
}

/**
 * evergreen_cs_packet_parse_vline() - parse userspace VLINE packet
 * @parser:		parser structure holding parsing context.
 *
 * This is an Evergreen(+)-specific function for parsing VLINE packets.
 * Real work is done by r600_cs_common_vline_parse function.
 * Here we just set up ASIC-specific register table and call
 * the common implementation function.
 */
static int evergreen_cs_packet_parse_vline(struct radeon_cs_parser *p)
{

	static uint32_t vline_start_end[6] = {
		EVERGREEN_VLINE_START_END + EVERGREEN_CRTC0_REGISTER_OFFSET,
		EVERGREEN_VLINE_START_END + EVERGREEN_CRTC1_REGISTER_OFFSET,
		EVERGREEN_VLINE_START_END + EVERGREEN_CRTC2_REGISTER_OFFSET,
		EVERGREEN_VLINE_START_END + EVERGREEN_CRTC3_REGISTER_OFFSET,
		EVERGREEN_VLINE_START_END + EVERGREEN_CRTC4_REGISTER_OFFSET,
		EVERGREEN_VLINE_START_END + EVERGREEN_CRTC5_REGISTER_OFFSET
	};
	static uint32_t vline_status[6] = {
		EVERGREEN_VLINE_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET,
		EVERGREEN_VLINE_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET,
		EVERGREEN_VLINE_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET,
		EVERGREEN_VLINE_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET,
		EVERGREEN_VLINE_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET,
		EVERGREEN_VLINE_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET
	};

	return r600_cs_common_vline_parse(p, vline_start_end, vline_status);
}

static int evergreen_packet0_check(struct radeon_cs_parser *p,
				   struct radeon_cs_packet *pkt,
				   unsigned idx, unsigned reg)
{
	int r;

	switch (reg) {
	case EVERGREEN_VLINE_START_END:
		r = evergreen_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			return r;
		}
		break;
	default:
		pr_err("Forbidden register 0x%04X in cs at %d\n", reg, idx);
		return -EINVAL;
	}
	return 0;
}

static int evergreen_cs_parse_packet0(struct radeon_cs_parser *p,
				      struct radeon_cs_packet *pkt)
{
	unsigned reg, i;
	unsigned idx;
	int r;

	idx = pkt->idx + 1;
	reg = pkt->reg;
	for (i = 0; i <= pkt->count; i++, idx++, reg += 4) {
		r = evergreen_packet0_check(p, pkt, idx, reg);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * evergreen_cs_handle_reg() - process registers that need special handling.
 * @parser: parser structure holding parsing context
 * @reg: register we are testing
 * @idx: index into the cs buffer
 */
static int evergreen_cs_handle_reg(struct radeon_cs_parser *p, u32 reg, u32 idx)
{
	struct evergreen_cs_track *track = (struct evergreen_cs_track *)p->track;
	struct radeon_bo_list *reloc;
	u32 tmp, *ib;
	int r;

	ib = p->ib.ptr;
	switch (reg) {
	/* force following reg to 0 in an attempt to disable out buffer
	 * which will need us to better understand how it works to perform
	 * security check on it (Jerome)
	 */
	case SQ_ESGS_RING_SIZE:
	case SQ_GSVS_RING_SIZE:
	case SQ_ESTMP_RING_SIZE:
	case SQ_GSTMP_RING_SIZE:
	case SQ_HSTMP_RING_SIZE:
	case SQ_LSTMP_RING_SIZE:
	case SQ_PSTMP_RING_SIZE:
	case SQ_VSTMP_RING_SIZE:
	case SQ_ESGS_RING_ITEMSIZE:
	case SQ_ESTMP_RING_ITEMSIZE:
	case SQ_GSTMP_RING_ITEMSIZE:
	case SQ_GSVS_RING_ITEMSIZE:
	case SQ_GS_VERT_ITEMSIZE:
	case SQ_GS_VERT_ITEMSIZE_1:
	case SQ_GS_VERT_ITEMSIZE_2:
	case SQ_GS_VERT_ITEMSIZE_3:
	case SQ_GSVS_RING_OFFSET_1:
	case SQ_GSVS_RING_OFFSET_2:
	case SQ_GSVS_RING_OFFSET_3:
	case SQ_HSTMP_RING_ITEMSIZE:
	case SQ_LSTMP_RING_ITEMSIZE:
	case SQ_PSTMP_RING_ITEMSIZE:
	case SQ_VSTMP_RING_ITEMSIZE:
	case VGT_TF_RING_SIZE:
		/* get value to populate the IB don't remove */
		/*tmp =radeon_get_ib_value(p, idx);
		  ib[idx] = 0;*/
		break;
	case SQ_ESGS_RING_BASE:
	case SQ_GSVS_RING_BASE:
	case SQ_ESTMP_RING_BASE:
	case SQ_GSTMP_RING_BASE:
	case SQ_HSTMP_RING_BASE:
	case SQ_LSTMP_RING_BASE:
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
	case DB_DEPTH_CONTROL:
		track->db_depth_control = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case CAYMAN_DB_EQAA:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		break;
	case CAYMAN_DB_DEPTH_INFO:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		break;
	case DB_Z_INFO:
		track->db_z_info = radeon_get_ib_value(p, idx);
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				dev_warn(p->dev, "bad SET_CONTEXT_REG "
						"0x%04X\n", reg);
				return -EINVAL;
			}
			ib[idx] &= ~Z_ARRAY_MODE(0xf);
			track->db_z_info &= ~Z_ARRAY_MODE(0xf);
			ib[idx] |= Z_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
			track->db_z_info |= Z_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
			if (reloc->tiling_flags & RADEON_TILING_MACRO) {
				unsigned bankw, bankh, mtaspect, tile_split;

				evergreen_tiling_fields(reloc->tiling_flags,
							&bankw, &bankh, &mtaspect,
							&tile_split);
				ib[idx] |= DB_NUM_BANKS(evergreen_cs_get_num_banks(track->nbanks));
				ib[idx] |= DB_TILE_SPLIT(tile_split) |
						DB_BANK_WIDTH(bankw) |
						DB_BANK_HEIGHT(bankh) |
						DB_MACRO_TILE_ASPECT(mtaspect);
			}
		}
		track->db_dirty = true;
		break;
	case DB_STENCIL_INFO:
		track->db_s_info = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case DB_DEPTH_VIEW:
		track->db_depth_view = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case DB_DEPTH_SIZE:
		track->db_depth_size = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case R_02805C_DB_DEPTH_SLICE:
		track->db_depth_slice = radeon_get_ib_value(p, idx);
		track->db_dirty = true;
		break;
	case DB_Z_READ_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_z_read_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->db_z_read_bo = reloc->robj;
		track->db_dirty = true;
		break;
	case DB_Z_WRITE_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_z_write_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->db_z_write_bo = reloc->robj;
		track->db_dirty = true;
		break;
	case DB_STENCIL_READ_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_s_read_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->db_s_read_bo = reloc->robj;
		track->db_dirty = true;
		break;
	case DB_STENCIL_WRITE_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_s_write_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->db_s_write_bo = reloc->robj;
		track->db_dirty = true;
		break;
	case VGT_STRMOUT_CONFIG:
		track->vgt_strmout_config = radeon_get_ib_value(p, idx);
		track->streamout_dirty = true;
		break;
	case VGT_STRMOUT_BUFFER_CONFIG:
		track->vgt_strmout_buffer_config = radeon_get_ib_value(p, idx);
		track->streamout_dirty = true;
		break;
	case VGT_STRMOUT_BUFFER_BASE_0:
	case VGT_STRMOUT_BUFFER_BASE_1:
	case VGT_STRMOUT_BUFFER_BASE_2:
	case VGT_STRMOUT_BUFFER_BASE_3:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - VGT_STRMOUT_BUFFER_BASE_0) / 16;
		track->vgt_strmout_bo_offset[tmp] = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->vgt_strmout_bo[tmp] = reloc->robj;
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
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "missing reloc for CP_COHER_BASE "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case CB_TARGET_MASK:
		track->cb_target_mask = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case CB_SHADER_MASK:
		track->cb_shader_mask = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case PA_SC_AA_CONFIG:
		if (p->rdev->family >= CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = radeon_get_ib_value(p, idx) & MSAA_NUM_SAMPLES_MASK;
		track->nsamples = 1 << tmp;
		break;
	case CAYMAN_PA_SC_AA_CONFIG:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = radeon_get_ib_value(p, idx) & CAYMAN_MSAA_NUM_SAMPLES_MASK;
		track->nsamples = 1 << tmp;
		break;
	case CB_COLOR0_VIEW:
	case CB_COLOR1_VIEW:
	case CB_COLOR2_VIEW:
	case CB_COLOR3_VIEW:
	case CB_COLOR4_VIEW:
	case CB_COLOR5_VIEW:
	case CB_COLOR6_VIEW:
	case CB_COLOR7_VIEW:
		tmp = (reg - CB_COLOR0_VIEW) / 0x3c;
		track->cb_color_view[tmp] = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case CB_COLOR8_VIEW:
	case CB_COLOR9_VIEW:
	case CB_COLOR10_VIEW:
	case CB_COLOR11_VIEW:
		tmp = ((reg - CB_COLOR8_VIEW) / 0x1c) + 8;
		track->cb_color_view[tmp] = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case CB_COLOR0_INFO:
	case CB_COLOR1_INFO:
	case CB_COLOR2_INFO:
	case CB_COLOR3_INFO:
	case CB_COLOR4_INFO:
	case CB_COLOR5_INFO:
	case CB_COLOR6_INFO:
	case CB_COLOR7_INFO:
		tmp = (reg - CB_COLOR0_INFO) / 0x3c;
		track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				dev_warn(p->dev, "bad SET_CONTEXT_REG "
						"0x%04X\n", reg);
				return -EINVAL;
			}
			ib[idx] |= CB_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
			track->cb_color_info[tmp] |= CB_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
		}
		track->cb_dirty = true;
		break;
	case CB_COLOR8_INFO:
	case CB_COLOR9_INFO:
	case CB_COLOR10_INFO:
	case CB_COLOR11_INFO:
		tmp = ((reg - CB_COLOR8_INFO) / 0x1c) + 8;
		track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				dev_warn(p->dev, "bad SET_CONTEXT_REG "
						"0x%04X\n", reg);
				return -EINVAL;
			}
			ib[idx] |= CB_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
			track->cb_color_info[tmp] |= CB_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
		}
		track->cb_dirty = true;
		break;
	case CB_COLOR0_PITCH:
	case CB_COLOR1_PITCH:
	case CB_COLOR2_PITCH:
	case CB_COLOR3_PITCH:
	case CB_COLOR4_PITCH:
	case CB_COLOR5_PITCH:
	case CB_COLOR6_PITCH:
	case CB_COLOR7_PITCH:
		tmp = (reg - CB_COLOR0_PITCH) / 0x3c;
		track->cb_color_pitch[tmp] = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case CB_COLOR8_PITCH:
	case CB_COLOR9_PITCH:
	case CB_COLOR10_PITCH:
	case CB_COLOR11_PITCH:
		tmp = ((reg - CB_COLOR8_PITCH) / 0x1c) + 8;
		track->cb_color_pitch[tmp] = radeon_get_ib_value(p, idx);
		track->cb_dirty = true;
		break;
	case CB_COLOR0_SLICE:
	case CB_COLOR1_SLICE:
	case CB_COLOR2_SLICE:
	case CB_COLOR3_SLICE:
	case CB_COLOR4_SLICE:
	case CB_COLOR5_SLICE:
	case CB_COLOR6_SLICE:
	case CB_COLOR7_SLICE:
		tmp = (reg - CB_COLOR0_SLICE) / 0x3c;
		track->cb_color_slice[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_slice_idx[tmp] = idx;
		track->cb_dirty = true;
		break;
	case CB_COLOR8_SLICE:
	case CB_COLOR9_SLICE:
	case CB_COLOR10_SLICE:
	case CB_COLOR11_SLICE:
		tmp = ((reg - CB_COLOR8_SLICE) / 0x1c) + 8;
		track->cb_color_slice[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_slice_idx[tmp] = idx;
		track->cb_dirty = true;
		break;
	case CB_COLOR0_ATTRIB:
	case CB_COLOR1_ATTRIB:
	case CB_COLOR2_ATTRIB:
	case CB_COLOR3_ATTRIB:
	case CB_COLOR4_ATTRIB:
	case CB_COLOR5_ATTRIB:
	case CB_COLOR6_ATTRIB:
	case CB_COLOR7_ATTRIB:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			if (reloc->tiling_flags & RADEON_TILING_MACRO) {
				unsigned bankw, bankh, mtaspect, tile_split;

				evergreen_tiling_fields(reloc->tiling_flags,
							&bankw, &bankh, &mtaspect,
							&tile_split);
				ib[idx] |= CB_NUM_BANKS(evergreen_cs_get_num_banks(track->nbanks));
				ib[idx] |= CB_TILE_SPLIT(tile_split) |
					   CB_BANK_WIDTH(bankw) |
					   CB_BANK_HEIGHT(bankh) |
					   CB_MACRO_TILE_ASPECT(mtaspect);
			}
		}
		tmp = ((reg - CB_COLOR0_ATTRIB) / 0x3c);
		track->cb_color_attrib[tmp] = ib[idx];
		track->cb_dirty = true;
		break;
	case CB_COLOR8_ATTRIB:
	case CB_COLOR9_ATTRIB:
	case CB_COLOR10_ATTRIB:
	case CB_COLOR11_ATTRIB:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			if (reloc->tiling_flags & RADEON_TILING_MACRO) {
				unsigned bankw, bankh, mtaspect, tile_split;

				evergreen_tiling_fields(reloc->tiling_flags,
							&bankw, &bankh, &mtaspect,
							&tile_split);
				ib[idx] |= CB_NUM_BANKS(evergreen_cs_get_num_banks(track->nbanks));
				ib[idx] |= CB_TILE_SPLIT(tile_split) |
					   CB_BANK_WIDTH(bankw) |
					   CB_BANK_HEIGHT(bankh) |
					   CB_MACRO_TILE_ASPECT(mtaspect);
			}
		}
		tmp = ((reg - CB_COLOR8_ATTRIB) / 0x1c) + 8;
		track->cb_color_attrib[tmp] = ib[idx];
		track->cb_dirty = true;
		break;
	case CB_COLOR0_FMASK:
	case CB_COLOR1_FMASK:
	case CB_COLOR2_FMASK:
	case CB_COLOR3_FMASK:
	case CB_COLOR4_FMASK:
	case CB_COLOR5_FMASK:
	case CB_COLOR6_FMASK:
	case CB_COLOR7_FMASK:
		tmp = (reg - CB_COLOR0_FMASK) / 0x3c;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->cb_color_fmask_bo[tmp] = reloc->robj;
		break;
	case CB_COLOR0_CMASK:
	case CB_COLOR1_CMASK:
	case CB_COLOR2_CMASK:
	case CB_COLOR3_CMASK:
	case CB_COLOR4_CMASK:
	case CB_COLOR5_CMASK:
	case CB_COLOR6_CMASK:
	case CB_COLOR7_CMASK:
		tmp = (reg - CB_COLOR0_CMASK) / 0x3c;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->cb_color_cmask_bo[tmp] = reloc->robj;
		break;
	case CB_COLOR0_FMASK_SLICE:
	case CB_COLOR1_FMASK_SLICE:
	case CB_COLOR2_FMASK_SLICE:
	case CB_COLOR3_FMASK_SLICE:
	case CB_COLOR4_FMASK_SLICE:
	case CB_COLOR5_FMASK_SLICE:
	case CB_COLOR6_FMASK_SLICE:
	case CB_COLOR7_FMASK_SLICE:
		tmp = (reg - CB_COLOR0_FMASK_SLICE) / 0x3c;
		track->cb_color_fmask_slice[tmp] = radeon_get_ib_value(p, idx);
		break;
	case CB_COLOR0_CMASK_SLICE:
	case CB_COLOR1_CMASK_SLICE:
	case CB_COLOR2_CMASK_SLICE:
	case CB_COLOR3_CMASK_SLICE:
	case CB_COLOR4_CMASK_SLICE:
	case CB_COLOR5_CMASK_SLICE:
	case CB_COLOR6_CMASK_SLICE:
	case CB_COLOR7_CMASK_SLICE:
		tmp = (reg - CB_COLOR0_CMASK_SLICE) / 0x3c;
		track->cb_color_cmask_slice[tmp] = radeon_get_ib_value(p, idx);
		break;
	case CB_COLOR0_BASE:
	case CB_COLOR1_BASE:
	case CB_COLOR2_BASE:
	case CB_COLOR3_BASE:
	case CB_COLOR4_BASE:
	case CB_COLOR5_BASE:
	case CB_COLOR6_BASE:
	case CB_COLOR7_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - CB_COLOR0_BASE) / 0x3c;
		track->cb_color_bo_offset[tmp] = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->cb_color_bo[tmp] = reloc->robj;
		track->cb_dirty = true;
		break;
	case CB_COLOR8_BASE:
	case CB_COLOR9_BASE:
	case CB_COLOR10_BASE:
	case CB_COLOR11_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = ((reg - CB_COLOR8_BASE) / 0x1c) + 8;
		track->cb_color_bo_offset[tmp] = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->cb_color_bo[tmp] = reloc->robj;
		track->cb_dirty = true;
		break;
	case DB_HTILE_DATA_BASE:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->htile_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		track->htile_bo = reloc->robj;
		track->db_dirty = true;
		break;
	case DB_HTILE_SURFACE:
		/* 8x8 only */
		track->htile_surface = radeon_get_ib_value(p, idx);
		/* force 8x8 htile width and height */
		ib[idx] |= 3;
		track->db_dirty = true;
		break;
	case CB_IMMED0_BASE:
	case CB_IMMED1_BASE:
	case CB_IMMED2_BASE:
	case CB_IMMED3_BASE:
	case CB_IMMED4_BASE:
	case CB_IMMED5_BASE:
	case CB_IMMED6_BASE:
	case CB_IMMED7_BASE:
	case CB_IMMED8_BASE:
	case CB_IMMED9_BASE:
	case CB_IMMED10_BASE:
	case CB_IMMED11_BASE:
	case SQ_PGM_START_FS:
	case SQ_PGM_START_ES:
	case SQ_PGM_START_VS:
	case SQ_PGM_START_GS:
	case SQ_PGM_START_PS:
	case SQ_PGM_START_HS:
	case SQ_PGM_START_LS:
	case SQ_CONST_MEM_BASE:
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
	case SQ_ALU_CONST_CACHE_HS_0:
	case SQ_ALU_CONST_CACHE_HS_1:
	case SQ_ALU_CONST_CACHE_HS_2:
	case SQ_ALU_CONST_CACHE_HS_3:
	case SQ_ALU_CONST_CACHE_HS_4:
	case SQ_ALU_CONST_CACHE_HS_5:
	case SQ_ALU_CONST_CACHE_HS_6:
	case SQ_ALU_CONST_CACHE_HS_7:
	case SQ_ALU_CONST_CACHE_HS_8:
	case SQ_ALU_CONST_CACHE_HS_9:
	case SQ_ALU_CONST_CACHE_HS_10:
	case SQ_ALU_CONST_CACHE_HS_11:
	case SQ_ALU_CONST_CACHE_HS_12:
	case SQ_ALU_CONST_CACHE_HS_13:
	case SQ_ALU_CONST_CACHE_HS_14:
	case SQ_ALU_CONST_CACHE_HS_15:
	case SQ_ALU_CONST_CACHE_LS_0:
	case SQ_ALU_CONST_CACHE_LS_1:
	case SQ_ALU_CONST_CACHE_LS_2:
	case SQ_ALU_CONST_CACHE_LS_3:
	case SQ_ALU_CONST_CACHE_LS_4:
	case SQ_ALU_CONST_CACHE_LS_5:
	case SQ_ALU_CONST_CACHE_LS_6:
	case SQ_ALU_CONST_CACHE_LS_7:
	case SQ_ALU_CONST_CACHE_LS_8:
	case SQ_ALU_CONST_CACHE_LS_9:
	case SQ_ALU_CONST_CACHE_LS_10:
	case SQ_ALU_CONST_CACHE_LS_11:
	case SQ_ALU_CONST_CACHE_LS_12:
	case SQ_ALU_CONST_CACHE_LS_13:
	case SQ_ALU_CONST_CACHE_LS_14:
	case SQ_ALU_CONST_CACHE_LS_15:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case SX_MEMORY_EXPORT_BASE:
		if (p->rdev->family >= CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONFIG_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONFIG_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
		break;
	case CAYMAN_SX_SCATTER_EXPORT_BASE:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
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

/**
 * evergreen_is_safe_reg() - check if register is authorized or not
 * @parser: parser structure holding parsing context
 * @reg: register we are testing
 *
 * This function will test against reg_safe_bm and return true
 * if register is safe or false otherwise.
 */
static inline bool evergreen_is_safe_reg(struct radeon_cs_parser *p, u32 reg)
{
	struct evergreen_cs_track *track = p->track;
	u32 m, i;

	i = (reg >> 7);
	if (unlikely(i >= REG_SAFE_BM_SIZE)) {
		return false;
	}
	m = 1 << ((reg >> 2) & 31);
	if (!(track->reg_safe_bm[i] & m))
		return true;

	return false;
}

static int evergreen_packet3_check(struct radeon_cs_parser *p,
				   struct radeon_cs_packet *pkt)
{
	struct radeon_bo_list *reloc;
	struct evergreen_cs_track *track;
	uint32_t *ib;
	unsigned idx;
	unsigned i;
	unsigned start_reg, end_reg, reg;
	int r;
	u32 idx_value;

	track = (struct evergreen_cs_track *)p->track;
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

		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
	case PACKET3_CONTEXT_CONTROL:
		if (pkt->count != 1) {
			DRM_ERROR("bad CONTEXT_CONTROL\n");
			return -EINVAL;
		}
		break;
	case PACKET3_INDEX_TYPE:
	case PACKET3_NUM_INSTANCES:
	case PACKET3_CLEAR_STATE:
		if (pkt->count) {
			DRM_ERROR("bad INDEX_TYPE/NUM_INSTANCES/CLEAR_STATE\n");
			return -EINVAL;
		}
		break;
	case CAYMAN_PACKET3_DEALLOC_STATE:
		if (p->rdev->family < CHIP_CAYMAN) {
			DRM_ERROR("bad PACKET3_DEALLOC_STATE\n");
			return -EINVAL;
		}
		if (pkt->count) {
			DRM_ERROR("bad INDEX_TYPE/NUM_INSTANCES/CLEAR_STATE\n");
			return -EINVAL;
		}
		break;
	case PACKET3_INDEX_BASE:
	{
		uint64_t offset;

		if (pkt->count != 1) {
			DRM_ERROR("bad INDEX_BASE\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad INDEX_BASE\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
			 idx_value +
			 ((u64)(radeon_get_ib_value(p, idx+1) & 0xff) << 32);

		ib[idx+0] = offset;
		ib[idx+1] = upper_32_bits(offset) & 0xff;

		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	}
	case PACKET3_INDEX_BUFFER_SIZE:
	{
		if (pkt->count != 0) {
			DRM_ERROR("bad INDEX_BUFFER_SIZE\n");
			return -EINVAL;
		}
		break;
	}
	case PACKET3_DRAW_INDEX:
	{
		uint64_t offset;
		if (pkt->count != 3) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
			 idx_value +
			 ((u64)(radeon_get_ib_value(p, idx+1) & 0xff) << 32);

		ib[idx+0] = offset;
		ib[idx+1] = upper_32_bits(offset) & 0xff;

		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	}
	case PACKET3_DRAW_INDEX_2:
	{
		uint64_t offset;

		if (pkt->count != 4) {
			DRM_ERROR("bad DRAW_INDEX_2\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad DRAW_INDEX_2\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
			 radeon_get_ib_value(p, idx+1) +
			 ((u64)(radeon_get_ib_value(p, idx+2) & 0xff) << 32);

		ib[idx+1] = offset;
		ib[idx+2] = upper_32_bits(offset) & 0xff;

		r = evergreen_cs_track_check(p);
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
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_MULTI_AUTO:
		if (pkt->count != 2) {
			DRM_ERROR("bad DRAW_INDEX_MULTI_AUTO\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_IMMD:
		if (pkt->count < 2) {
			DRM_ERROR("bad DRAW_INDEX_IMMD\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_OFFSET:
		if (pkt->count != 2) {
			DRM_ERROR("bad DRAW_INDEX_OFFSET\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_OFFSET_2:
		if (pkt->count != 3) {
			DRM_ERROR("bad DRAW_INDEX_OFFSET_2\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_SET_BASE:
	{
		/*
		DW 1 HEADER Header of the packet. Shader_Type in bit 1 of the Header will correspond to the shader type of the Load, see Type-3 Packet.
		   2 BASE_INDEX Bits [3:0] BASE_INDEX - Base Index specifies which base address is specified in the last two DWs.
		     0001: DX11 Draw_Index_Indirect Patch Table Base: Base address for Draw_Index_Indirect data.
		   3 ADDRESS_LO Bits [31:3] - Lower bits of QWORD-Aligned Address. Bits [2:0] - Reserved
		   4 ADDRESS_HI Bits [31:8] - Reserved. Bits [7:0] - Upper bits of Address [47:32]
		*/
		if (pkt->count != 2) {
			DRM_ERROR("bad SET_BASE\n");
			return -EINVAL;
		}

		/* currently only supporting setting indirect draw buffer base address */
		if (idx_value != 1) {
			DRM_ERROR("bad SET_BASE\n");
			return -EINVAL;
		}

		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad SET_BASE\n");
			return -EINVAL;
		}

		track->indirect_draw_buffer_size = radeon_bo_size(reloc->robj);

		ib[idx+1] = reloc->gpu_offset;
		ib[idx+2] = upper_32_bits(reloc->gpu_offset) & 0xff;

		break;
	}
	case PACKET3_DRAW_INDIRECT:
	case PACKET3_DRAW_INDEX_INDIRECT:
	{
		u64 size = pkt->opcode == PACKET3_DRAW_INDIRECT ? 16 : 20;

		/*
		DW 1 HEADER
		   2 DATA_OFFSET Bits [31:0] + byte aligned offset where the required data structure starts. Bits 1:0 are zero
		   3 DRAW_INITIATOR Draw Initiator Register. Written to the VGT_DRAW_INITIATOR register for the assigned context
		*/
		if (pkt->count != 1) {
			DRM_ERROR("bad DRAW_INDIRECT\n");
			return -EINVAL;
		}

		if (idx_value + size > track->indirect_draw_buffer_size) {
			dev_warn(p->dev, "DRAW_INDIRECT buffer too small %u + %llu > %lu\n",
				idx_value, size, track->indirect_draw_buffer_size);
			return -EINVAL;
		}

		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	}
	case PACKET3_DISPATCH_DIRECT:
		if (pkt->count != 3) {
			DRM_ERROR("bad DISPATCH_DIRECT\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DISPATCH_INDIRECT:
		if (pkt->count != 1) {
			DRM_ERROR("bad DISPATCH_INDIRECT\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad DISPATCH_INDIRECT\n");
			return -EINVAL;
		}
		ib[idx+0] = idx_value + (u32)(reloc->gpu_offset & 0xffffffff);
		r = evergreen_cs_track_check(p);
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

			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				DRM_ERROR("bad WAIT_REG_MEM\n");
				return -EINVAL;
			}

			offset = reloc->gpu_offset +
				 (radeon_get_ib_value(p, idx+1) & 0xfffffffc) +
				 ((u64)(radeon_get_ib_value(p, idx+2) & 0xff) << 32);

			ib[idx+1] = (ib[idx+1] & 0x3) | (offset & 0xfffffffc);
			ib[idx+2] = upper_32_bits(offset) & 0xff;
		} else if (idx_value & 0x100) {
			DRM_ERROR("cannot use PFP on REG wait\n");
			return -EINVAL;
		}
		break;
	case PACKET3_CP_DMA:
	{
		u32 command, size, info;
		u64 offset, tmp;
		if (pkt->count != 4) {
			DRM_ERROR("bad CP DMA\n");
			return -EINVAL;
		}
		command = radeon_get_ib_value(p, idx+4);
		size = command & 0x1fffff;
		info = radeon_get_ib_value(p, idx+1);
		if ((((info & 0x60000000) >> 29) != 0) || /* src = GDS or DATA */
		    (((info & 0x00300000) >> 20) != 0) || /* dst = GDS */
		    ((((info & 0x00300000) >> 20) == 0) &&
		     (command & PACKET3_CP_DMA_CMD_DAS)) || /* dst = register */
		    ((((info & 0x60000000) >> 29) == 0) &&
		     (command & PACKET3_CP_DMA_CMD_SAS))) { /* src = register */
			/* non mem to mem copies requires dw aligned count */
			if (size % 4) {
				DRM_ERROR("CP DMA command requires dw count alignment\n");
				return -EINVAL;
			}
		}
		if (command & PACKET3_CP_DMA_CMD_SAS) {
			/* src address space is register */
			/* GDS is ok */
			if (((info & 0x60000000) >> 29) != 1) {
				DRM_ERROR("CP DMA SAS not supported\n");
				return -EINVAL;
			}
		} else {
			if (command & PACKET3_CP_DMA_CMD_SAIC) {
				DRM_ERROR("CP DMA SAIC only supported for registers\n");
				return -EINVAL;
			}
			/* src address space is memory */
			if (((info & 0x60000000) >> 29) == 0) {
				r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
			} else if (((info & 0x60000000) >> 29) != 2) {
				DRM_ERROR("bad CP DMA SRC_SEL\n");
				return -EINVAL;
			}
		}
		if (command & PACKET3_CP_DMA_CMD_DAS) {
			/* dst address space is register */
			/* GDS is ok */
			if (((info & 0x00300000) >> 20) != 1) {
				DRM_ERROR("CP DMA DAS not supported\n");
				return -EINVAL;
			}
		} else {
			/* dst address space is memory */
			if (command & PACKET3_CP_DMA_CMD_DAIC) {
				DRM_ERROR("CP DMA DAIC only supported for registers\n");
				return -EINVAL;
			}
			if (((info & 0x00300000) >> 20) == 0) {
				r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
			} else {
				DRM_ERROR("bad CP DMA DST_SEL\n");
				return -EINVAL;
			}
		}
		break;
	}
	case PACKET3_PFP_SYNC_ME:
		if (pkt->count) {
			DRM_ERROR("bad PFP_SYNC_ME\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SURFACE_SYNC:
		if (pkt->count != 3) {
			DRM_ERROR("bad SURFACE_SYNC\n");
			return -EINVAL;
		}
		/* 0xffffffff/0x0 is flush all cache flag */
		if (radeon_get_ib_value(p, idx + 1) != 0xffffffff ||
		    radeon_get_ib_value(p, idx + 2) != 0) {
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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

			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad EVENT_WRITE_EOP\n");
			return -EINVAL;
		}

		offset = reloc->gpu_offset +
			 (radeon_get_ib_value(p, idx+1) & 0xfffffffc) +
			 ((u64)(radeon_get_ib_value(p, idx+2) & 0xff) << 32);

		ib[idx+1] = offset & 0xfffffffc;
		ib[idx+2] = (ib[idx+2] & 0xffffff00) | (upper_32_bits(offset) & 0xff);
		break;
	}
	case PACKET3_EVENT_WRITE_EOS:
	{
		uint64_t offset;

		if (pkt->count != 3) {
			DRM_ERROR("bad EVENT_WRITE_EOS\n");
			return -EINVAL;
		}
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("bad EVENT_WRITE_EOS\n");
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
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_START) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (reg = start_reg, idx++; reg <= end_reg; reg += 4, idx++) {
			if (evergreen_is_safe_reg(p, reg))
				continue;
			r = evergreen_cs_handle_reg(p, reg, idx);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_CONTEXT_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONTEXT_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONTEXT_REG_START) ||
		    (start_reg >= PACKET3_SET_CONTEXT_REG_END) ||
		    (end_reg >= PACKET3_SET_CONTEXT_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONTEXT_REG\n");
			return -EINVAL;
		}
		for (reg = start_reg, idx++; reg <= end_reg; reg += 4, idx++) {
			if (evergreen_is_safe_reg(p, reg))
				continue;
			r = evergreen_cs_handle_reg(p, reg, idx);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_RESOURCE:
		if (pkt->count % 8) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		start_reg = (idx_value << 2) + PACKET3_SET_RESOURCE_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_RESOURCE_START) ||
		    (start_reg >= PACKET3_SET_RESOURCE_END) ||
		    (end_reg >= PACKET3_SET_RESOURCE_END)) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		for (i = 0; i < (pkt->count / 8); i++) {
			struct radeon_bo *texture, *mipmap;
			u32 toffset, moffset;
			u32 size, offset, mip_address, tex_dim;

			switch (G__SQ_CONSTANT_TYPE(radeon_get_ib_value(p, idx+1+(i*8)+7))) {
			case SQ_TEX_VTX_VALID_TEXTURE:
				/* tex base */
				r = radeon_cs_packet_next_reloc(p, &reloc, 0);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE (tex)\n");
					return -EINVAL;
				}
				if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
					ib[idx+1+(i*8)+1] |=
						TEX_ARRAY_MODE(evergreen_cs_get_aray_mode(reloc->tiling_flags));
					if (reloc->tiling_flags & RADEON_TILING_MACRO) {
						unsigned bankw, bankh, mtaspect, tile_split;

						evergreen_tiling_fields(reloc->tiling_flags,
									&bankw, &bankh, &mtaspect,
									&tile_split);
						ib[idx+1+(i*8)+6] |= TEX_TILE_SPLIT(tile_split);
						ib[idx+1+(i*8)+7] |=
							TEX_BANK_WIDTH(bankw) |
							TEX_BANK_HEIGHT(bankh) |
							MACRO_TILE_ASPECT(mtaspect) |
							TEX_NUM_BANKS(evergreen_cs_get_num_banks(track->nbanks));
					}
				}
				texture = reloc->robj;
				toffset = (u32)((reloc->gpu_offset >> 8) & 0xffffffff);

				/* tex mip base */
				tex_dim = ib[idx+1+(i*8)+0] & 0x7;
				mip_address = ib[idx+1+(i*8)+3];

				if ((tex_dim == SQ_TEX_DIM_2D_MSAA || tex_dim == SQ_TEX_DIM_2D_ARRAY_MSAA) &&
				    !mip_address &&
				    !radeon_cs_packet_next_is_pkt3_nop(p)) {
					/* MIP_ADDRESS should point to FMASK for an MSAA texture.
					 * It should be 0 if FMASK is disabled. */
					moffset = 0;
					mipmap = NULL;
				} else {
					r = radeon_cs_packet_next_reloc(p, &reloc, 0);
					if (r) {
						DRM_ERROR("bad SET_RESOURCE (tex)\n");
						return -EINVAL;
					}
					moffset = (u32)((reloc->gpu_offset >> 8) & 0xffffffff);
					mipmap = reloc->robj;
				}

				r = evergreen_cs_track_validate_texture(p, texture, mipmap, idx+1+(i*8));
				if (r)
					return r;
				ib[idx+1+(i*8)+2] += toffset;
				ib[idx+1+(i*8)+3] += moffset;
				break;
			case SQ_TEX_VTX_VALID_BUFFER:
			{
				uint64_t offset64;
				/* vtx base */
				r = radeon_cs_packet_next_reloc(p, &reloc, 0);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE (vtx)\n");
					return -EINVAL;
				}
				offset = radeon_get_ib_value(p, idx+1+(i*8)+0);
				size = radeon_get_ib_value(p, idx+1+(i*8)+1);
				if (p->rdev && (size + offset) > radeon_bo_size(reloc->robj)) {
					/* force size to size of the buffer */
					dev_warn_ratelimited(p->dev, "vbo resource seems too big for the bo\n");
					ib[idx+1+(i*8)+1] = radeon_bo_size(reloc->robj) - offset;
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
		/* XXX fix me ALU const buffers only */
		break;
	case PACKET3_SET_BOOL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_BOOL_CONST_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_BOOL_CONST_START) ||
		    (start_reg >= PACKET3_SET_BOOL_CONST_END) ||
		    (end_reg >= PACKET3_SET_BOOL_CONST_END)) {
			DRM_ERROR("bad SET_BOOL_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_LOOP_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_LOOP_CONST_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_LOOP_CONST_START) ||
		    (start_reg >= PACKET3_SET_LOOP_CONST_END) ||
		    (end_reg >= PACKET3_SET_LOOP_CONST_END)) {
			DRM_ERROR("bad SET_LOOP_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_CTL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_CTL_CONST_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CTL_CONST_START) ||
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
		start_reg = (idx_value << 2) + PACKET3_SET_SAMPLER_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_SAMPLER_START) ||
		    (start_reg >= PACKET3_SET_SAMPLER_END) ||
		    (end_reg >= PACKET3_SET_SAMPLER_END)) {
			DRM_ERROR("bad SET_SAMPLER\n");
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
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
			if (!evergreen_is_safe_reg(p, reg)) {
				dev_warn(p->dev, "forbidden register 0x%08x at %d\n",
					 reg, idx + 1);
				return -EINVAL;
			}
		}
		if (idx_value & 0x2) {
			u64 offset;
			/* DST is memory. */
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
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
			if (!evergreen_is_safe_reg(p, reg)) {
				dev_warn(p->dev, "forbidden register 0x%08x at %d\n",
					 reg, idx + 3);
				return -EINVAL;
			}
		}
		break;
	case PACKET3_SET_APPEND_CNT:
	{
		uint32_t areg;
		uint32_t allowed_reg_base;
		uint32_t source_sel;
		if (pkt->count != 2) {
			DRM_ERROR("bad SET_APPEND_CNT (invalid count)\n");
			return -EINVAL;
		}

		allowed_reg_base = GDS_APPEND_COUNT_0;
		allowed_reg_base -= PACKET3_SET_CONTEXT_REG_START;
		allowed_reg_base >>= 2;

		areg = idx_value >> 16;
		if (areg < allowed_reg_base || areg > (allowed_reg_base + 11)) {
			dev_warn(p->dev, "forbidden register for append cnt 0x%08x at %d\n",
				 areg, idx);
			return -EINVAL;
		}

		source_sel = G_PACKET3_SET_APPEND_CNT_SRC_SELECT(idx_value);
		if (source_sel == PACKET3_SAC_SRC_SEL_MEM) {
			uint64_t offset;
			uint32_t swap;
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				DRM_ERROR("bad SET_APPEND_CNT (missing reloc)\n");
				return -EINVAL;
			}
			offset = radeon_get_ib_value(p, idx + 1);
			swap = offset & 0x3;
			offset &= ~0x3;

			offset += ((u64)(radeon_get_ib_value(p, idx + 2) & 0xff)) << 32;

			offset += reloc->gpu_offset;
			ib[idx+1] = (offset & 0xfffffffc) | swap;
			ib[idx+2] = upper_32_bits(offset) & 0xff;
		} else {
			DRM_ERROR("bad SET_APPEND_CNT (unsupported operation)\n");
			return -EINVAL;
		}
		break;
	}
	case PACKET3_NOP:
		break;
	default:
		DRM_ERROR("Packet3 opcode %x not supported\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int evergreen_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	struct evergreen_cs_track *track;
	u32 tmp;
	int r;

	if (p->track == NULL) {
		/* initialize tracker, we are in kms */
		track = kzalloc(sizeof(*track), GFP_KERNEL);
		if (track == NULL)
			return -ENOMEM;
		evergreen_cs_track_init(track);
		if (p->rdev->family >= CHIP_CAYMAN) {
			tmp = p->rdev->config.cayman.tile_config;
			track->reg_safe_bm = cayman_reg_safe_bm;
		} else {
			tmp = p->rdev->config.evergreen.tile_config;
			track->reg_safe_bm = evergreen_reg_safe_bm;
		}
		BUILD_BUG_ON(ARRAY_SIZE(cayman_reg_safe_bm) != REG_SAFE_BM_SIZE);
		BUILD_BUG_ON(ARRAY_SIZE(evergreen_reg_safe_bm) != REG_SAFE_BM_SIZE);
		switch (tmp & 0xf) {
		case 0:
			track->npipes = 1;
			break;
		case 1:
		default:
			track->npipes = 2;
			break;
		case 2:
			track->npipes = 4;
			break;
		case 3:
			track->npipes = 8;
			break;
		}

		switch ((tmp & 0xf0) >> 4) {
		case 0:
			track->nbanks = 4;
			break;
		case 1:
		default:
			track->nbanks = 8;
			break;
		case 2:
			track->nbanks = 16;
			break;
		}

		switch ((tmp & 0xf00) >> 8) {
		case 0:
			track->group_size = 256;
			break;
		case 1:
		default:
			track->group_size = 512;
			break;
		}

		switch ((tmp & 0xf000) >> 12) {
		case 0:
			track->row_size = 1;
			break;
		case 1:
		default:
			track->row_size = 2;
			break;
		case 2:
			track->row_size = 4;
			break;
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
			r = evergreen_cs_parse_packet0(p, &pkt);
			break;
		case RADEON_PACKET_TYPE2:
			break;
		case RADEON_PACKET_TYPE3:
			r = evergreen_packet3_check(p, &pkt);
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
		pr_info("%05d  0x%08X\n", r, p->ib.ptr[r]);
		mdelay(1);
	}
#endif
	kfree(p->track);
	p->track = NULL;
	return 0;
}

/**
 * evergreen_dma_cs_parse() - parse the DMA IB
 * @p:		parser structure holding parsing context.
 *
 * Parses the DMA IB from the CS ioctl and updates
 * the GPU addresses based on the reloc information and
 * checks for errors. (Evergreen-Cayman)
 * Returns 0 for success and an error on failure.
 **/
int evergreen_dma_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_chunk *ib_chunk = p->chunk_ib;
	struct radeon_bo_list *src_reloc, *dst_reloc, *dst2_reloc;
	u32 header, cmd, count, sub_cmd;
	uint32_t *ib = p->ib.ptr;
	u32 idx;
	u64 src_offset, dst_offset, dst2_offset;
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
		sub_cmd = GET_DMA_SUB_CMD(header);

		switch (cmd) {
		case DMA_PACKET_WRITE:
			r = r600_dma_cs_next_reloc(p, &dst_reloc);
			if (r) {
				DRM_ERROR("bad DMA_PACKET_WRITE\n");
				return -EINVAL;
			}
			switch (sub_cmd) {
			/* tiled */
			case 8:
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset <<= 8;

				ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				p->idx += count + 7;
				break;
			/* linear */
			case 0:
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset |= ((u64)(radeon_get_ib_value(p, idx+2) & 0xff)) << 32;

				ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
				ib[idx+2] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				p->idx += count + 3;
				break;
			default:
				DRM_ERROR("bad DMA_PACKET_WRITE [%6d] 0x%08x sub cmd is not 0 or 8\n", idx, header);
				return -EINVAL;
			}
			if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
				dev_warn(p->dev, "DMA write buffer too small (%llu %lu)\n",
					 dst_offset, radeon_bo_size(dst_reloc->robj));
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
			switch (sub_cmd) {
			/* Copy L2L, DW aligned */
			case 0x00:
				/* L2L, dw */
				src_offset = radeon_get_ib_value(p, idx+2);
				src_offset |= ((u64)(radeon_get_ib_value(p, idx+4) & 0xff)) << 32;
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0xff)) << 32;
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, dw src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, dw dst buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
				ib[idx+2] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
				ib[idx+3] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				ib[idx+4] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				p->idx += 5;
				break;
			/* Copy L2T/T2L */
			case 0x08:
				/* detile bit */
				if (radeon_get_ib_value(p, idx + 2) & (1 << 31)) {
					/* tiled src, linear dst */
					src_offset = radeon_get_ib_value(p, idx+1);
					src_offset <<= 8;
					ib[idx+1] += (u32)(src_reloc->gpu_offset >> 8);

					dst_offset = radeon_get_ib_value(p, idx + 7);
					dst_offset |= ((u64)(radeon_get_ib_value(p, idx+8) & 0xff)) << 32;
					ib[idx+7] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
					ib[idx+8] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				} else {
					/* linear src, tiled dst */
					src_offset = radeon_get_ib_value(p, idx+7);
					src_offset |= ((u64)(radeon_get_ib_value(p, idx+8) & 0xff)) << 32;
					ib[idx+7] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
					ib[idx+8] += upper_32_bits(src_reloc->gpu_offset) & 0xff;

					dst_offset = radeon_get_ib_value(p, idx+1);
					dst_offset <<= 8;
					ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				}
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, dst buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				p->idx += 9;
				break;
			/* Copy L2L, byte aligned */
			case 0x40:
				/* L2L, byte */
				src_offset = radeon_get_ib_value(p, idx+2);
				src_offset |= ((u64)(radeon_get_ib_value(p, idx+4) & 0xff)) << 32;
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0xff)) << 32;
				if ((src_offset + count) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, byte src buffer too small (%llu %lu)\n",
							src_offset + count, radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + count) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, byte dst buffer too small (%llu %lu)\n",
							dst_offset + count, radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xffffffff);
				ib[idx+2] += (u32)(src_reloc->gpu_offset & 0xffffffff);
				ib[idx+3] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				ib[idx+4] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				p->idx += 5;
				break;
			/* Copy L2L, partial */
			case 0x41:
				/* L2L, partial */
				if (p->family < CHIP_CAYMAN) {
					DRM_ERROR("L2L Partial is cayman only !\n");
					return -EINVAL;
				}
				ib[idx+1] += (u32)(src_reloc->gpu_offset & 0xffffffff);
				ib[idx+2] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				ib[idx+4] += (u32)(dst_reloc->gpu_offset & 0xffffffff);
				ib[idx+5] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;

				p->idx += 9;
				break;
			/* Copy L2L, DW aligned, broadcast */
			case 0x44:
				/* L2L, dw, broadcast */
				r = r600_dma_cs_next_reloc(p, &dst2_reloc);
				if (r) {
					DRM_ERROR("bad L2L, dw, broadcast DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset |= ((u64)(radeon_get_ib_value(p, idx+4) & 0xff)) << 32;
				dst2_offset = radeon_get_ib_value(p, idx+2);
				dst2_offset |= ((u64)(radeon_get_ib_value(p, idx+5) & 0xff)) << 32;
				src_offset = radeon_get_ib_value(p, idx+3);
				src_offset |= ((u64)(radeon_get_ib_value(p, idx+6) & 0xff)) << 32;
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, dw, broadcast src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, dw, broadcast dst buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				if ((dst2_offset + (count * 4)) > radeon_bo_size(dst2_reloc->robj)) {
					dev_warn(p->dev, "DMA L2L, dw, broadcast dst2 buffer too small (%llu %lu)\n",
							dst2_offset + (count * 4), radeon_bo_size(dst2_reloc->robj));
					return -EINVAL;
				}
				ib[idx+1] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
				ib[idx+2] += (u32)(dst2_reloc->gpu_offset & 0xfffffffc);
				ib[idx+3] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
				ib[idx+4] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				ib[idx+5] += upper_32_bits(dst2_reloc->gpu_offset) & 0xff;
				ib[idx+6] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				p->idx += 7;
				break;
			/* Copy L2T Frame to Field */
			case 0x48:
				if (radeon_get_ib_value(p, idx + 2) & (1 << 31)) {
					DRM_ERROR("bad L2T, frame to fields DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				r = r600_dma_cs_next_reloc(p, &dst2_reloc);
				if (r) {
					DRM_ERROR("bad L2T, frame to fields DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset <<= 8;
				dst2_offset = radeon_get_ib_value(p, idx+2);
				dst2_offset <<= 8;
				src_offset = radeon_get_ib_value(p, idx+8);
				src_offset |= ((u64)(radeon_get_ib_value(p, idx+9) & 0xff)) << 32;
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, frame to fields src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, frame to fields buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				if ((dst2_offset + (count * 4)) > radeon_bo_size(dst2_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, frame to fields buffer too small (%llu %lu)\n",
							dst2_offset + (count * 4), radeon_bo_size(dst2_reloc->robj));
					return -EINVAL;
				}
				ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				ib[idx+2] += (u32)(dst2_reloc->gpu_offset >> 8);
				ib[idx+8] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
				ib[idx+9] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				p->idx += 10;
				break;
			/* Copy L2T/T2L, partial */
			case 0x49:
				/* L2T, T2L partial */
				if (p->family < CHIP_CAYMAN) {
					DRM_ERROR("L2T, T2L Partial is cayman only !\n");
					return -EINVAL;
				}
				/* detile bit */
				if (radeon_get_ib_value(p, idx + 2) & (1 << 31)) {
					/* tiled src, linear dst */
					ib[idx+1] += (u32)(src_reloc->gpu_offset >> 8);

					ib[idx+7] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
					ib[idx+8] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				} else {
					/* linear src, tiled dst */
					ib[idx+7] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
					ib[idx+8] += upper_32_bits(src_reloc->gpu_offset) & 0xff;

					ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				}
				p->idx += 12;
				break;
			/* Copy L2T broadcast */
			case 0x4b:
				/* L2T, broadcast */
				if (radeon_get_ib_value(p, idx + 2) & (1 << 31)) {
					DRM_ERROR("bad L2T, broadcast DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				r = r600_dma_cs_next_reloc(p, &dst2_reloc);
				if (r) {
					DRM_ERROR("bad L2T, broadcast DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset <<= 8;
				dst2_offset = radeon_get_ib_value(p, idx+2);
				dst2_offset <<= 8;
				src_offset = radeon_get_ib_value(p, idx+8);
				src_offset |= ((u64)(radeon_get_ib_value(p, idx+9) & 0xff)) << 32;
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, broadcast src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, broadcast dst buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				if ((dst2_offset + (count * 4)) > radeon_bo_size(dst2_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, broadcast dst2 buffer too small (%llu %lu)\n",
							dst2_offset + (count * 4), radeon_bo_size(dst2_reloc->robj));
					return -EINVAL;
				}
				ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				ib[idx+2] += (u32)(dst2_reloc->gpu_offset >> 8);
				ib[idx+8] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
				ib[idx+9] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				p->idx += 10;
				break;
			/* Copy L2T/T2L (tile units) */
			case 0x4c:
				/* L2T, T2L */
				/* detile bit */
				if (radeon_get_ib_value(p, idx + 2) & (1 << 31)) {
					/* tiled src, linear dst */
					src_offset = radeon_get_ib_value(p, idx+1);
					src_offset <<= 8;
					ib[idx+1] += (u32)(src_reloc->gpu_offset >> 8);

					dst_offset = radeon_get_ib_value(p, idx+7);
					dst_offset |= ((u64)(radeon_get_ib_value(p, idx+8) & 0xff)) << 32;
					ib[idx+7] += (u32)(dst_reloc->gpu_offset & 0xfffffffc);
					ib[idx+8] += upper_32_bits(dst_reloc->gpu_offset) & 0xff;
				} else {
					/* linear src, tiled dst */
					src_offset = radeon_get_ib_value(p, idx+7);
					src_offset |= ((u64)(radeon_get_ib_value(p, idx+8) & 0xff)) << 32;
					ib[idx+7] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
					ib[idx+8] += upper_32_bits(src_reloc->gpu_offset) & 0xff;

					dst_offset = radeon_get_ib_value(p, idx+1);
					dst_offset <<= 8;
					ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				}
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, T2L src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, T2L dst buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				p->idx += 9;
				break;
			/* Copy T2T, partial (tile units) */
			case 0x4d:
				/* T2T partial */
				if (p->family < CHIP_CAYMAN) {
					DRM_ERROR("L2T, T2L Partial is cayman only !\n");
					return -EINVAL;
				}
				ib[idx+1] += (u32)(src_reloc->gpu_offset >> 8);
				ib[idx+4] += (u32)(dst_reloc->gpu_offset >> 8);
				p->idx += 13;
				break;
			/* Copy L2T broadcast (tile units) */
			case 0x4f:
				/* L2T, broadcast */
				if (radeon_get_ib_value(p, idx + 2) & (1 << 31)) {
					DRM_ERROR("bad L2T, broadcast DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				r = r600_dma_cs_next_reloc(p, &dst2_reloc);
				if (r) {
					DRM_ERROR("bad L2T, broadcast DMA_PACKET_COPY\n");
					return -EINVAL;
				}
				dst_offset = radeon_get_ib_value(p, idx+1);
				dst_offset <<= 8;
				dst2_offset = radeon_get_ib_value(p, idx+2);
				dst2_offset <<= 8;
				src_offset = radeon_get_ib_value(p, idx+8);
				src_offset |= ((u64)(radeon_get_ib_value(p, idx+9) & 0xff)) << 32;
				if ((src_offset + (count * 4)) > radeon_bo_size(src_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, broadcast src buffer too small (%llu %lu)\n",
							src_offset + (count * 4), radeon_bo_size(src_reloc->robj));
					return -EINVAL;
				}
				if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, broadcast dst buffer too small (%llu %lu)\n",
							dst_offset + (count * 4), radeon_bo_size(dst_reloc->robj));
					return -EINVAL;
				}
				if ((dst2_offset + (count * 4)) > radeon_bo_size(dst2_reloc->robj)) {
					dev_warn(p->dev, "DMA L2T, broadcast dst2 buffer too small (%llu %lu)\n",
							dst2_offset + (count * 4), radeon_bo_size(dst2_reloc->robj));
					return -EINVAL;
				}
				ib[idx+1] += (u32)(dst_reloc->gpu_offset >> 8);
				ib[idx+2] += (u32)(dst2_reloc->gpu_offset >> 8);
				ib[idx+8] += (u32)(src_reloc->gpu_offset & 0xfffffffc);
				ib[idx+9] += upper_32_bits(src_reloc->gpu_offset) & 0xff;
				p->idx += 10;
				break;
			default:
				DRM_ERROR("bad DMA_PACKET_COPY [%6d] 0x%08x invalid sub cmd\n", idx, header);
				return -EINVAL;
			}
			break;
		case DMA_PACKET_CONSTANT_FILL:
			r = r600_dma_cs_next_reloc(p, &dst_reloc);
			if (r) {
				DRM_ERROR("bad DMA_PACKET_CONSTANT_FILL\n");
				return -EINVAL;
			}
			dst_offset = radeon_get_ib_value(p, idx+1);
			dst_offset |= ((u64)(radeon_get_ib_value(p, idx+3) & 0x00ff0000)) << 16;
			if ((dst_offset + (count * 4)) > radeon_bo_size(dst_reloc->robj)) {
				dev_warn(p->dev, "DMA constant fill buffer too small (%llu %lu)\n",
					 dst_offset, radeon_bo_size(dst_reloc->robj));
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
		pr_info("%05d  0x%08X\n", r, p->ib.ptr[r]);
		mdelay(1);
	}
#endif
	return 0;
}

/* vm parser */
static bool evergreen_vm_reg_valid(u32 reg)
{
	/* context regs are fine */
	if (reg >= 0x28000)
		return true;

	/* check config regs */
	switch (reg) {
	case WAIT_UNTIL:
	case GRBM_GFX_INDEX:
	case CP_STRMOUT_CNTL:
	case CP_COHER_CNTL:
	case CP_COHER_SIZE:
	case VGT_VTX_VECT_EJECT_REG:
	case VGT_CACHE_INVALIDATION:
	case VGT_GS_VERTEX_REUSE:
	case VGT_PRIMITIVE_TYPE:
	case VGT_INDEX_TYPE:
	case VGT_NUM_INDICES:
	case VGT_NUM_INSTANCES:
	case VGT_COMPUTE_DIM_X:
	case VGT_COMPUTE_DIM_Y:
	case VGT_COMPUTE_DIM_Z:
	case VGT_COMPUTE_START_X:
	case VGT_COMPUTE_START_Y:
	case VGT_COMPUTE_START_Z:
	case VGT_COMPUTE_INDEX:
	case VGT_COMPUTE_THREAD_GROUP_SIZE:
	case VGT_HS_OFFCHIP_PARAM:
	case PA_CL_ENHANCE:
	case PA_SU_LINE_STIPPLE_VALUE:
	case PA_SC_LINE_STIPPLE_STATE:
	case PA_SC_ENHANCE:
	case SQ_DYN_GPR_CNTL_PS_FLUSH_REQ:
	case SQ_DYN_GPR_SIMD_LOCK_EN:
	case SQ_CONFIG:
	case SQ_GPR_RESOURCE_MGMT_1:
	case SQ_GLOBAL_GPR_RESOURCE_MGMT_1:
	case SQ_GLOBAL_GPR_RESOURCE_MGMT_2:
	case SQ_CONST_MEM_BASE:
	case SQ_STATIC_THREAD_MGMT_1:
	case SQ_STATIC_THREAD_MGMT_2:
	case SQ_STATIC_THREAD_MGMT_3:
	case SPI_CONFIG_CNTL:
	case SPI_CONFIG_CNTL_1:
	case TA_CNTL_AUX:
	case DB_DEBUG:
	case DB_DEBUG2:
	case DB_DEBUG3:
	case DB_DEBUG4:
	case DB_WATERMARKS:
	case TD_PS_BORDER_COLOR_INDEX:
	case TD_PS_BORDER_COLOR_RED:
	case TD_PS_BORDER_COLOR_GREEN:
	case TD_PS_BORDER_COLOR_BLUE:
	case TD_PS_BORDER_COLOR_ALPHA:
	case TD_VS_BORDER_COLOR_INDEX:
	case TD_VS_BORDER_COLOR_RED:
	case TD_VS_BORDER_COLOR_GREEN:
	case TD_VS_BORDER_COLOR_BLUE:
	case TD_VS_BORDER_COLOR_ALPHA:
	case TD_GS_BORDER_COLOR_INDEX:
	case TD_GS_BORDER_COLOR_RED:
	case TD_GS_BORDER_COLOR_GREEN:
	case TD_GS_BORDER_COLOR_BLUE:
	case TD_GS_BORDER_COLOR_ALPHA:
	case TD_HS_BORDER_COLOR_INDEX:
	case TD_HS_BORDER_COLOR_RED:
	case TD_HS_BORDER_COLOR_GREEN:
	case TD_HS_BORDER_COLOR_BLUE:
	case TD_HS_BORDER_COLOR_ALPHA:
	case TD_LS_BORDER_COLOR_INDEX:
	case TD_LS_BORDER_COLOR_RED:
	case TD_LS_BORDER_COLOR_GREEN:
	case TD_LS_BORDER_COLOR_BLUE:
	case TD_LS_BORDER_COLOR_ALPHA:
	case TD_CS_BORDER_COLOR_INDEX:
	case TD_CS_BORDER_COLOR_RED:
	case TD_CS_BORDER_COLOR_GREEN:
	case TD_CS_BORDER_COLOR_BLUE:
	case TD_CS_BORDER_COLOR_ALPHA:
	case SQ_ESGS_RING_SIZE:
	case SQ_GSVS_RING_SIZE:
	case SQ_ESTMP_RING_SIZE:
	case SQ_GSTMP_RING_SIZE:
	case SQ_HSTMP_RING_SIZE:
	case SQ_LSTMP_RING_SIZE:
	case SQ_PSTMP_RING_SIZE:
	case SQ_VSTMP_RING_SIZE:
	case SQ_ESGS_RING_ITEMSIZE:
	case SQ_ESTMP_RING_ITEMSIZE:
	case SQ_GSTMP_RING_ITEMSIZE:
	case SQ_GSVS_RING_ITEMSIZE:
	case SQ_GS_VERT_ITEMSIZE:
	case SQ_GS_VERT_ITEMSIZE_1:
	case SQ_GS_VERT_ITEMSIZE_2:
	case SQ_GS_VERT_ITEMSIZE_3:
	case SQ_GSVS_RING_OFFSET_1:
	case SQ_GSVS_RING_OFFSET_2:
	case SQ_GSVS_RING_OFFSET_3:
	case SQ_HSTMP_RING_ITEMSIZE:
	case SQ_LSTMP_RING_ITEMSIZE:
	case SQ_PSTMP_RING_ITEMSIZE:
	case SQ_VSTMP_RING_ITEMSIZE:
	case VGT_TF_RING_SIZE:
	case SQ_ESGS_RING_BASE:
	case SQ_GSVS_RING_BASE:
	case SQ_ESTMP_RING_BASE:
	case SQ_GSTMP_RING_BASE:
	case SQ_HSTMP_RING_BASE:
	case SQ_LSTMP_RING_BASE:
	case SQ_PSTMP_RING_BASE:
	case SQ_VSTMP_RING_BASE:
	case CAYMAN_VGT_OFFCHIP_LDS_BASE:
	case CAYMAN_SQ_EX_ALLOC_TABLE_SLOTS:
		return true;
	default:
		DRM_ERROR("Invalid register 0x%x in CS\n", reg);
		return false;
	}
}

static int evergreen_vm_packet3_check(struct radeon_device *rdev,
				      u32 *ib, struct radeon_cs_packet *pkt)
{
	u32 idx = pkt->idx + 1;
	u32 idx_value = ib[idx];
	u32 start_reg, end_reg, reg, i;
	u32 command, info;

	switch (pkt->opcode) {
	case PACKET3_NOP:
		break;
	case PACKET3_SET_BASE:
		if (idx_value != 1) {
			DRM_ERROR("bad SET_BASE");
			return -EINVAL;
		}
		break;
	case PACKET3_CLEAR_STATE:
	case PACKET3_INDEX_BUFFER_SIZE:
	case PACKET3_DISPATCH_DIRECT:
	case PACKET3_DISPATCH_INDIRECT:
	case PACKET3_MODE_CONTROL:
	case PACKET3_SET_PREDICATION:
	case PACKET3_COND_EXEC:
	case PACKET3_PRED_EXEC:
	case PACKET3_DRAW_INDIRECT:
	case PACKET3_DRAW_INDEX_INDIRECT:
	case PACKET3_INDEX_BASE:
	case PACKET3_DRAW_INDEX_2:
	case PACKET3_CONTEXT_CONTROL:
	case PACKET3_DRAW_INDEX_OFFSET:
	case PACKET3_INDEX_TYPE:
	case PACKET3_DRAW_INDEX:
	case PACKET3_DRAW_INDEX_AUTO:
	case PACKET3_DRAW_INDEX_IMMD:
	case PACKET3_NUM_INSTANCES:
	case PACKET3_DRAW_INDEX_MULTI_AUTO:
	case PACKET3_STRMOUT_BUFFER_UPDATE:
	case PACKET3_DRAW_INDEX_OFFSET_2:
	case PACKET3_DRAW_INDEX_MULTI_ELEMENT:
	case PACKET3_MPEG_INDEX:
	case PACKET3_WAIT_REG_MEM:
	case PACKET3_MEM_WRITE:
	case PACKET3_PFP_SYNC_ME:
	case PACKET3_SURFACE_SYNC:
	case PACKET3_EVENT_WRITE:
	case PACKET3_EVENT_WRITE_EOP:
	case PACKET3_EVENT_WRITE_EOS:
	case PACKET3_SET_CONTEXT_REG:
	case PACKET3_SET_BOOL_CONST:
	case PACKET3_SET_LOOP_CONST:
	case PACKET3_SET_RESOURCE:
	case PACKET3_SET_SAMPLER:
	case PACKET3_SET_CTL_CONST:
	case PACKET3_SET_RESOURCE_OFFSET:
	case PACKET3_SET_CONTEXT_REG_INDIRECT:
	case PACKET3_SET_RESOURCE_INDIRECT:
	case CAYMAN_PACKET3_DEALLOC_STATE:
		break;
	case PACKET3_COND_WRITE:
		if (idx_value & 0x100) {
			reg = ib[idx + 5] * 4;
			if (!evergreen_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_COPY_DW:
		if (idx_value & 0x2) {
			reg = ib[idx + 3] * 4;
			if (!evergreen_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_SET_CONFIG_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_START) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			if (!evergreen_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_CP_DMA:
		command = ib[idx + 4];
		info = ib[idx + 1];
		if ((((info & 0x60000000) >> 29) != 0) || /* src = GDS or DATA */
		    (((info & 0x00300000) >> 20) != 0) || /* dst = GDS */
		    ((((info & 0x00300000) >> 20) == 0) &&
		     (command & PACKET3_CP_DMA_CMD_DAS)) || /* dst = register */
		    ((((info & 0x60000000) >> 29) == 0) &&
		     (command & PACKET3_CP_DMA_CMD_SAS))) { /* src = register */
			/* non mem to mem copies requires dw aligned count */
			if ((command & 0x1fffff) % 4) {
				DRM_ERROR("CP DMA command requires dw count alignment\n");
				return -EINVAL;
			}
		}
		if (command & PACKET3_CP_DMA_CMD_SAS) {
			/* src address space is register */
			if (((info & 0x60000000) >> 29) == 0) {
				start_reg = idx_value << 2;
				if (command & PACKET3_CP_DMA_CMD_SAIC) {
					reg = start_reg;
					if (!evergreen_vm_reg_valid(reg)) {
						DRM_ERROR("CP DMA Bad SRC register\n");
						return -EINVAL;
					}
				} else {
					for (i = 0; i < (command & 0x1fffff); i++) {
						reg = start_reg + (4 * i);
						if (!evergreen_vm_reg_valid(reg)) {
							DRM_ERROR("CP DMA Bad SRC register\n");
							return -EINVAL;
						}
					}
				}
			}
		}
		if (command & PACKET3_CP_DMA_CMD_DAS) {
			/* dst address space is register */
			if (((info & 0x00300000) >> 20) == 0) {
				start_reg = ib[idx + 2];
				if (command & PACKET3_CP_DMA_CMD_DAIC) {
					reg = start_reg;
					if (!evergreen_vm_reg_valid(reg)) {
						DRM_ERROR("CP DMA Bad DST register\n");
						return -EINVAL;
					}
				} else {
					for (i = 0; i < (command & 0x1fffff); i++) {
						reg = start_reg + (4 * i);
						if (!evergreen_vm_reg_valid(reg)) {
							DRM_ERROR("CP DMA Bad DST register\n");
							return -EINVAL;
						}
					}
				}
			}
		}
		break;
	case PACKET3_SET_APPEND_CNT: {
		uint32_t areg;
		uint32_t allowed_reg_base;

		if (pkt->count != 2) {
			DRM_ERROR("bad SET_APPEND_CNT (invalid count)\n");
			return -EINVAL;
		}

		allowed_reg_base = GDS_APPEND_COUNT_0;
		allowed_reg_base -= PACKET3_SET_CONTEXT_REG_START;
		allowed_reg_base >>= 2;

		areg = idx_value >> 16;
		if (areg < allowed_reg_base || areg > (allowed_reg_base + 11)) {
			DRM_ERROR("forbidden register for append cnt 0x%08x at %d\n",
				  areg, idx);
			return -EINVAL;
		}
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

int evergreen_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib)
{
	int ret = 0;
	u32 idx = 0;
	struct radeon_cs_packet pkt;

	do {
		pkt.idx = idx;
		pkt.type = RADEON_CP_PACKET_GET_TYPE(ib->ptr[idx]);
		pkt.count = RADEON_CP_PACKET_GET_COUNT(ib->ptr[idx]);
		pkt.one_reg_wr = 0;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			dev_err(rdev->dev, "Packet0 not allowed!\n");
			ret = -EINVAL;
			break;
		case RADEON_PACKET_TYPE2:
			idx += 1;
			break;
		case RADEON_PACKET_TYPE3:
			pkt.opcode = RADEON_CP_PACKET3_GET_OPCODE(ib->ptr[idx]);
			ret = evergreen_vm_packet3_check(rdev, ib->ptr, &pkt);
			idx += pkt.count + 2;
			break;
		default:
			dev_err(rdev->dev, "Unknown packet type %d !\n", pkt.type);
			ret = -EINVAL;
			break;
		}
		if (ret)
			break;
	} while (idx < ib->length_dw);

	return ret;
}

/**
 * evergreen_dma_ib_parse() - parse the DMA IB for VM
 * @rdev: radeon_device pointer
 * @ib:	radeon_ib pointer
 *
 * Parses the DMA IB from the VM CS ioctl
 * checks for errors. (Cayman-SI)
 * Returns 0 for success and an error on failure.
 **/
int evergreen_dma_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib)
{
	u32 idx = 0;
	u32 header, cmd, count, sub_cmd;

	do {
		header = ib->ptr[idx];
		cmd = GET_DMA_CMD(header);
		count = GET_DMA_COUNT(header);
		sub_cmd = GET_DMA_SUB_CMD(header);

		switch (cmd) {
		case DMA_PACKET_WRITE:
			switch (sub_cmd) {
			/* tiled */
			case 8:
				idx += count + 7;
				break;
			/* linear */
			case 0:
				idx += count + 3;
				break;
			default:
				DRM_ERROR("bad DMA_PACKET_WRITE [%6d] 0x%08x sub cmd is not 0 or 8\n", idx, ib->ptr[idx]);
				return -EINVAL;
			}
			break;
		case DMA_PACKET_COPY:
			switch (sub_cmd) {
			/* Copy L2L, DW aligned */
			case 0x00:
				idx += 5;
				break;
			/* Copy L2T/T2L */
			case 0x08:
				idx += 9;
				break;
			/* Copy L2L, byte aligned */
			case 0x40:
				idx += 5;
				break;
			/* Copy L2L, partial */
			case 0x41:
				idx += 9;
				break;
			/* Copy L2L, DW aligned, broadcast */
			case 0x44:
				idx += 7;
				break;
			/* Copy L2T Frame to Field */
			case 0x48:
				idx += 10;
				break;
			/* Copy L2T/T2L, partial */
			case 0x49:
				idx += 12;
				break;
			/* Copy L2T broadcast */
			case 0x4b:
				idx += 10;
				break;
			/* Copy L2T/T2L (tile units) */
			case 0x4c:
				idx += 9;
				break;
			/* Copy T2T, partial (tile units) */
			case 0x4d:
				idx += 13;
				break;
			/* Copy L2T broadcast (tile units) */
			case 0x4f:
				idx += 10;
				break;
			default:
				DRM_ERROR("bad DMA_PACKET_COPY [%6d] 0x%08x invalid sub cmd\n", idx, ib->ptr[idx]);
				return -EINVAL;
			}
			break;
		case DMA_PACKET_CONSTANT_FILL:
			idx += 4;
			break;
		case DMA_PACKET_NOP:
			idx += 1;
			break;
		default:
			DRM_ERROR("Unknown packet type %d at %d !\n", cmd, idx);
			return -EINVAL;
		}
	} while (idx < ib->length_dw);

	return 0;
}
