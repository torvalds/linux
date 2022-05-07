// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/types.h>
#include <linux/errno.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include "vs_dc_dec.h"

#define fourcc_mod_vs_get_tile_mode(val) ((u8)((val) & \
				DRM_FORMAT_MOD_VS_DEC_TILE_MODE_MASK))

static inline bool _is_stream_changed(struct dc_dec400l *dec400l, u8 stream_id)
{
	return dec400l->stream[stream_id].dirty;
}

static inline bool _is_stream_valid(struct dc_dec400l *dec400l, u8 stream_id)
{
	return !!(dec400l->stream[stream_id].main_base_addr);
}

static void _enable_stream(struct dc_dec400l *dec400l, struct dc_hw *hw,
						   u8 stream_id)
{
	if (!(dec400l->stream_status & (1 << stream_id))) {
		if (!(dec400l->stream_status))
			/* the first enabled steram */
			dc_hw_dec_init(hw);

		dec400l->stream_status |= 1 << stream_id;
	}
}
static void _disable_stream(struct dc_dec400l *dec400l, struct dc_hw *hw,
							u8 stream_id)
{
	if ((dec400l->stream_status & (1 << stream_id)))
		dec400l->stream_status &= ~(1 << stream_id);
}

static u16 get_dec_tile_size(u8 tile_mode, u8 cpp)
{
	u16 multi = 0;

	switch (tile_mode) {
	case DRM_FORMAT_MOD_VS_DEC_RASTER_16X1:
		multi = 16;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X4:
	case DRM_FORMAT_MOD_VS_DEC_TILE_4X8:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_32X1:
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X4_S:
		multi = 32;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X8_XMAJOR:
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X8_YMAJOR:
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_16X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_64X1:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_32X2:
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X4_S:
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X4_LSB:
		multi = 64;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_128X1:
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X8:
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X16:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_32X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_64X2:
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X4_S:
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X4_LSB:
		multi = 128;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_64X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_256X1:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_64X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_128X2:
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X16:
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X8:
		multi = 256;
		break;
	case DRM_FORMAT_MOD_VS_DEC_RASTER_256X2:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_128X4:
	case DRM_FORMAT_MOD_VS_DEC_RASTER_512X1:
	case DRM_FORMAT_MOD_VS_DEC_TILE_128X4:
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X16:
		multi = 512;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_256X4:
	case DRM_FORMAT_MOD_VS_DEC_TILE_64X16:
	case DRM_FORMAT_MOD_VS_DEC_TILE_128X8:
		multi = 1024;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_512X4:
		multi = 2048;
		break;
	default:
		break;
	}

	return multi * cpp;
}

static void update_fb_format(struct dc_dec_fb *dec_fb)
{
	struct drm_framebuffer *drm_fb = dec_fb->fb;
	u8 tile_mod = fourcc_mod_vs_get_tile_mode(drm_fb->modifier);
	u8 norm_mod = DRM_FORMAT_MOD_VS_LINEAR;

	switch (tile_mod) {
	case DRM_FORMAT_MOD_VS_DEC_RASTER_32X1:
		norm_mod = DRM_FORMAT_MOD_VS_TILE_32X1;
		break;
	case DRM_FORMAT_MOD_VS_DEC_RASTER_64X1:
		norm_mod = DRM_FORMAT_MOD_VS_TILE_64X1;
		break;
	case DRM_FORMAT_MOD_VS_DEC_RASTER_128X1:
		norm_mod = DRM_FORMAT_MOD_VS_TILE_128X1;
		break;
	case DRM_FORMAT_MOD_VS_DEC_RASTER_256X1:
		norm_mod = DRM_FORMAT_MOD_VS_TILE_256X1;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X4:
		norm_mod = DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR_8X4;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_4X8:
		norm_mod = DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR_4X8;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X8_XMAJOR:
		if (drm_fb->format->format == DRM_FORMAT_YUYV ||
			drm_fb->format->format == DRM_FORMAT_UYVY ||
			drm_fb->format->format == DRM_FORMAT_P010)
			norm_mod = DRM_FORMAT_MOD_VS_TILE_8X8;
		else
			norm_mod = DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X8:
		if (drm_fb->format->format == DRM_FORMAT_NV12 ||
			drm_fb->format->format == DRM_FORMAT_P010)
			norm_mod = DRM_FORMAT_MOD_VS_TILE_8X8;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X8:
		if (drm_fb->format->format == DRM_FORMAT_NV12)
			norm_mod = DRM_FORMAT_MOD_VS_TILE_8X8;
		break;
	default:
		break;
	}

	drm_fb->modifier = fourcc_mod_vs_norm_code(norm_mod);
}

static void _stream_config(struct dc_dec_fb *dec_fb,
						   struct dc_dec_stream *stream, u8 index)
{
	struct drm_framebuffer *drm_fb = dec_fb->fb;
	const struct drm_format_info *info = drm_fb->format;
	u32 plane_height = drm_format_info_plane_height(info,
						   drm_fb->height, index);
	u16 tile_size;

	stream->main_base_addr = dec_fb->addr[index];
	stream->tile_mode = fourcc_mod_vs_get_tile_mode(drm_fb->modifier);
	if (drm_fb->modifier & DRM_FORMAT_MOD_VS_DEC_ALIGN_32)
		stream->align_mode = DEC_ALIGN_32;
	else
		stream->align_mode = DEC_ALIGN_64;

	switch (drm_fb->format->format) {
	case DRM_FORMAT_ARGB8888:
		stream->format = DEC_FORMAT_ARGB8;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_XRGB8888:
		stream->format = DEC_FORMAT_XRGB8;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_RGB565:
		stream->format = DEC_FORMAT_R5G6B5;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_ARGB1555:
		stream->format = DEC_FORMAT_A1RGB5;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_XRGB1555:
		stream->format = DEC_FORMAT_X1RGB5;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_ARGB4444:
		stream->format = DEC_FORMAT_ARGB4;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_XRGB4444:
		stream->format = DEC_FORMAT_XRGB4;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_ARGB2101010:
		stream->format = DEC_FORMAT_A2R10G10B10;
		stream->depth = DEC_DEPTH_10;
		break;
	case DRM_FORMAT_YUYV:
		stream->format = DEC_FORMAT_YUY2;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_UYVY:
		stream->format = DEC_FORMAT_UYVY;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_YVU420:
		stream->format = DEC_FORMAT_YUV_ONLY;
		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_NV12:
		WARN_ON(stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_RASTER_256X1 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_RASTER_128X1 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_TILE_32X8 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_TILE_16X8);
		if (index) {
			stream->format = DEC_FORMAT_UV_MIX;
			switch (stream->tile_mode) {
			case DRM_FORMAT_MOD_VS_DEC_RASTER_256X1:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_RASTER_128X1;
				break;
			case DRM_FORMAT_MOD_VS_DEC_RASTER_128X1:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_RASTER_64X1;
				break;
			case DRM_FORMAT_MOD_VS_DEC_TILE_32X8:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_TILE_32X4;
				break;
			case DRM_FORMAT_MOD_VS_DEC_TILE_16X8:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_TILE_16X4;
				break;
			default:
				break;
			}
		} else {
			stream->format = DEC_FORMAT_YUV_ONLY;
		}

		stream->depth = DEC_DEPTH_8;
		break;
	case DRM_FORMAT_P010:
		WARN_ON(stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_RASTER_128X1 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_RASTER_64X1 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_TILE_16X8 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_TILE_8X8_XMAJOR);
		if (index) {
			stream->format = DEC_FORMAT_UV_MIX;
			switch (stream->tile_mode) {
			case DRM_FORMAT_MOD_VS_DEC_RASTER_128X1:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_RASTER_64X1;
				break;
			case DRM_FORMAT_MOD_VS_DEC_RASTER_64X1:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_RASTER_32X1;
				break;
			case DRM_FORMAT_MOD_VS_DEC_TILE_16X8:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_TILE_16X4;
				break;
			case DRM_FORMAT_MOD_VS_DEC_TILE_8X8_XMAJOR:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_TILE_8X4;
				break;
			default:
				break;
			}
		} else {
			stream->format = DEC_FORMAT_YUV_ONLY;
		}

		stream->depth = DEC_DEPTH_10;
		break;
	case DRM_FORMAT_NV16:
		WARN_ON(stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_RASTER_256X1 &&
				stream->tile_mode != DRM_FORMAT_MOD_VS_DEC_RASTER_128X1);
		if (index) {
			stream->format = DEC_FORMAT_UV_MIX;
			switch (stream->tile_mode) {
			case DRM_FORMAT_MOD_VS_DEC_RASTER_256X1:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_RASTER_128X1;
				break;
			case DRM_FORMAT_MOD_VS_DEC_RASTER_128X1:
				stream->tile_mode =
					DRM_FORMAT_MOD_VS_DEC_RASTER_64X1;
				break;
			default:
				break;
			}
		} else {
			stream->format = DEC_FORMAT_YUV_ONLY;
		}

		stream->depth = DEC_DEPTH_8;
		break;
	}

	tile_size = get_dec_tile_size(stream->tile_mode, info->cpp[index]);
	stream->aligned_stride = ALIGN(dec_fb->stride[index], tile_size);
	stream->ts_base_addr = stream->main_base_addr +
						   stream->aligned_stride * plane_height;
}

static int _dec400l_config(struct dc_dec400l *dec400l,
						  struct dc_dec_fb *dec_fb, u8 stream_base)
{
	struct dc_dec_stream stream;
	u8 i, stream_id, num_planes = 0;

	if (dec_fb) {
		const struct drm_format_info *info = dec_fb->fb->format;

		num_planes = info->num_planes;
	}

	for (i = 0; i < STREAM_COUNT; i++) {
		stream_id = stream_base + i;

		memset(&dec400l->stream[stream_id], 0, sizeof(struct dc_dec_stream));

		if (i < num_planes) {
			memset(&stream, 0, sizeof(struct dc_dec_stream));
			_stream_config(dec_fb, &stream, i);
			memcpy(&dec400l->stream[stream_id], &stream,
				   sizeof(struct dc_dec_stream));
		}
		dec400l->stream[stream_id].dirty = true;
	}

	if (dec_fb)
		update_fb_format(dec_fb);

	return 0;
}

int dc_dec_config(struct dc_dec400l *dec400l, struct dc_dec_fb *dec_fb,
				  u8 stream_base)
{
	if (dec_fb && !dec_fb->fb)
		return -EINVAL;

	if (dec_fb && (fourcc_mod_vs_get_type(dec_fb->fb->modifier) !=
				   DRM_FORMAT_MOD_VS_TYPE_COMPRESSED))
		_dec400l_config(dec400l, NULL, stream_base);
	else
		_dec400l_config(dec400l, dec_fb, stream_base);

	return 0;
}

int dc_dec_commit(struct dc_dec400l *dec400l, struct dc_hw *hw)
{
	u8 i;

	for (i = 0; i < STREAM_TOTAL; i++) {
		if (!_is_stream_changed(dec400l, i))
			continue;

		if (_is_stream_valid(dec400l, i)) {
			_enable_stream(dec400l, hw, i);
			dc_hw_dec_stream_set(hw, dec400l->stream[i].main_base_addr,
				dec400l->stream[i].ts_base_addr, dec400l->stream[i].tile_mode,
				dec400l->stream[i].align_mode, dec400l->stream[i].format,
				dec400l->stream[i].depth, i);
		} else {
			dc_hw_dec_stream_disable(hw, i);
			_disable_stream(dec400l, hw, i);
		}

		dec400l->stream[i].dirty = false;
	}

	return 0;
}
