// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_core_dcn5_calcs_dchub.h"
#include "dml2_core_utils.h"

void dcn5_calculate_max_det_and_min_compressed_buffer_size(
		unsigned int ConfigReturnBufferSizeInKByte,
		unsigned int ConfigReturnBufferSegmentSizeInKByte,
		unsigned int ROBBufferSizeInKByte,
		unsigned int MaxNumDPP,
		unsigned int nomDETInKByteOverrideEnable, // VBA_DELTA, allow DV to override default DET size
		unsigned int nomDETInKByteOverrideValue, // VBA_DELTA
		bool is_mrq_present,

		// Output
		unsigned int *MaxTotalDETInKByte,
		unsigned int *nomDETInKByte,
		unsigned int *MinCompressedBufferSizeInKByte)
{
	if (is_mrq_present)
		*MaxTotalDETInKByte = (unsigned int) math_ceil2((double)(ConfigReturnBufferSizeInKByte + ROBBufferSizeInKByte)*4/5, 64);
	else
		*MaxTotalDETInKByte = ConfigReturnBufferSizeInKByte - ConfigReturnBufferSegmentSizeInKByte;

	*nomDETInKByte = (unsigned int)(math_floor2((double)*MaxTotalDETInKByte / (double)MaxNumDPP, ConfigReturnBufferSegmentSizeInKByte));
	*MinCompressedBufferSizeInKByte = ConfigReturnBufferSizeInKByte - *MaxTotalDETInKByte;

	DML_LOG_VERBOSE("DML::%s: is_mrq_present = %u\n", __func__, is_mrq_present);
	DML_LOG_VERBOSE("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, ConfigReturnBufferSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: ConfigReturnBufferSegmentSizeInKByte = %u\n", __func__, ConfigReturnBufferSegmentSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: ROBBufferSizeInKByte = %u\n", __func__, ROBBufferSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: MaxNumDPP = %u\n", __func__, MaxNumDPP);
	DML_LOG_VERBOSE("DML::%s: MaxTotalDETInKByte = %u\n", __func__, *MaxTotalDETInKByte);
	DML_LOG_VERBOSE("DML::%s: nomDETInKByte = %u\n", __func__, *nomDETInKByte);
	DML_LOG_VERBOSE("DML::%s: MinCompressedBufferSizeInKByte = %u\n", __func__, *MinCompressedBufferSizeInKByte);

	if (nomDETInKByteOverrideEnable) {
		*nomDETInKByte = nomDETInKByteOverrideValue;
		DML_LOG_VERBOSE("DML::%s: nomDETInKByte = %u (overrided)\n", __func__, *nomDETInKByte);
	}
}

void dcn5_calculate_byte_per_pixel_and_block_sizes(
	enum dml2_source_format_class SourcePixelFormat,
	enum dml2_swizzle_mode SurfaceTiling,
	unsigned int pitch_y,
	unsigned int pitch_c,

	// Output
	unsigned int *BytePerPixelY,
	unsigned int *BytePerPixelC,
	double *BytePerPixelDETY,
	double *BytePerPixelDETC,
	unsigned int *BlockHeight256BytesY,
	unsigned int *BlockHeight256BytesC,
	unsigned int *BlockWidth256BytesY,
	unsigned int *BlockWidth256BytesC,
	unsigned int *MacroTileHeightY,
	unsigned int *MacroTileHeightC,
	unsigned int *MacroTileWidthY,
	unsigned int *MacroTileWidthC,
	bool *surf_linear128_l,
	bool *surf_linear128_c)
{
	*BytePerPixelDETY = 0;
	*BytePerPixelDETC = 0;
	*BytePerPixelY = 0;
	*BytePerPixelC = 0;

	if (SourcePixelFormat == dml2_444_64) {
		*BytePerPixelDETY = 8;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 8;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_444_32 ||
		   SourcePixelFormat == dml2_rgbe ||
		   SourcePixelFormat == dml2_422_packed_12) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 4;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_422_packed_10) {
		*BytePerPixelDETY = (double)(8.0 / 3);
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 4;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_444_16 || SourcePixelFormat == dml2_mono_16 || SourcePixelFormat == dml2_422_packed_8) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 2;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_444_8 || SourcePixelFormat == dml2_mono_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 1;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_rgbe_alpha) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 1;
		*BytePerPixelY = 4;
		*BytePerPixelC = 1;
	} else if (SourcePixelFormat == dml2_420_8 || SourcePixelFormat == dml2_422_planar_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 2;
		*BytePerPixelY = 1;
		*BytePerPixelC = 2;
	} else if (SourcePixelFormat == dml2_420_12 || SourcePixelFormat == dml2_422_planar_12) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 4;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else if (SourcePixelFormat == dml2_420_10 || SourcePixelFormat == dml2_422_planar_10) {
		*BytePerPixelDETY = (double)(4.0 / 3);
		*BytePerPixelDETC = (double)(8.0 / 3);
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else {
		DML_LOG_VERBOSE("ERROR: DML::%s: SourcePixelFormat = %u not supported!\n", __func__, SourcePixelFormat);
		DML_ASSERT(0);
	}

	DML_LOG_VERBOSE("DML::%s: SourcePixelFormat = %u\n", __func__, SourcePixelFormat);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelDETY = %f\n", __func__, *BytePerPixelDETY);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelDETC = %f\n", __func__, *BytePerPixelDETC);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelY = %u\n", __func__, *BytePerPixelY);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelC = %u\n", __func__, *BytePerPixelC);
	DML_LOG_VERBOSE("DML::%s: pitch_y = %u\n", __func__, pitch_y);
	DML_LOG_VERBOSE("DML::%s: pitch_c = %u\n", __func__, pitch_c);

	unsigned int pixel_per_element = dml2_core_utils_is_422_packed(SourcePixelFormat) ? 2 : 1;
	if (dml2_core_utils_get_gfx_version(SurfaceTiling) == 11) {
		*surf_linear128_l = 0;
		*surf_linear128_c = 0;
	} else {
		if (SurfaceTiling == dml2_sw_linear) {
			*surf_linear128_l = (((pitch_y * pixel_per_element * *BytePerPixelY) % 256) != 0);

			if (dml2_core_utils_is_420(SourcePixelFormat) || dml2_core_utils_is_422_planar(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha)
				*surf_linear128_c = (((pitch_c * *BytePerPixelC) % 256) != 0);
		}
	}
	DML_LOG_VERBOSE("DML::%s: surf_linear128_l = %u\n", __func__, *surf_linear128_l);
	DML_LOG_VERBOSE("DML::%s: surf_linear128_c = %u\n", __func__, *surf_linear128_c);

	if (!(dml2_core_utils_is_420(SourcePixelFormat) || dml2_core_utils_is_422_planar(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha)) {
		if (SurfaceTiling == dml2_sw_linear) {
			*BlockHeight256BytesY = 1;
		} else if (SourcePixelFormat == dml2_444_64 || SourcePixelFormat == dml2_422_packed_10 || SourcePixelFormat == dml2_422_packed_12) {
			*BlockHeight256BytesY = 4;
		} else if (SourcePixelFormat == dml2_444_8) {
			*BlockHeight256BytesY = 16;
		} else {
			*BlockHeight256BytesY = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockHeight256BytesC = 0;
		*BlockWidth256BytesC = 0;
	} else { // dual plane
		if (SurfaceTiling == dml2_sw_linear) {
			*BlockHeight256BytesY = 1;
			*BlockHeight256BytesC = 1;
		} else if (SourcePixelFormat == dml2_rgbe_alpha) {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 16;
		} else if (SourcePixelFormat == dml2_420_8 || SourcePixelFormat == dml2_422_planar_8) {
			*BlockHeight256BytesY = 16;
			*BlockHeight256BytesC = 8;
		} else {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockWidth256BytesC = 256U / *BytePerPixelC / *BlockHeight256BytesC;
	}
	DML_LOG_VERBOSE("DML::%s: BlockWidth256BytesY = %u\n", __func__, *BlockWidth256BytesY);
	DML_LOG_VERBOSE("DML::%s: BlockHeight256BytesY = %u\n", __func__, *BlockHeight256BytesY);
	DML_LOG_VERBOSE("DML::%s: BlockWidth256BytesC = %u\n", __func__, *BlockWidth256BytesC);
	DML_LOG_VERBOSE("DML::%s: BlockHeight256BytesC = %u\n", __func__, *BlockHeight256BytesC);

	if (dml2_core_utils_get_gfx_version(SurfaceTiling) == 11) {
		if (SurfaceTiling == dml2_gfx11_sw_linear) {
			*MacroTileHeightY = *BlockHeight256BytesY;
			*MacroTileWidthY = 256 / *BytePerPixelY / *MacroTileHeightY;
			*MacroTileHeightC = *BlockHeight256BytesC;
			if (*MacroTileHeightC == 0) {
				*MacroTileWidthC = 0;
			} else {
				*MacroTileWidthC = 256 / *BytePerPixelC / *MacroTileHeightC;
			}
		} else if (SurfaceTiling == dml2_gfx11_sw_64kb_d || SurfaceTiling == dml2_gfx11_sw_64kb_d_t || SurfaceTiling == dml2_gfx11_sw_64kb_d_x || SurfaceTiling == dml2_gfx11_sw_64kb_r_x) {
			*MacroTileHeightY = 16 * *BlockHeight256BytesY;
			*MacroTileWidthY = 65536 / *BytePerPixelY / *MacroTileHeightY;
			*MacroTileHeightC = 16 * *BlockHeight256BytesC;
			if (*MacroTileHeightC == 0) {
				*MacroTileWidthC = 0;
			} else {
				*MacroTileWidthC = 65536 / *BytePerPixelC / *MacroTileHeightC;
			}
		} else {
			*MacroTileHeightY = 32 * *BlockHeight256BytesY;
			*MacroTileWidthY = 65536 * 4 / *BytePerPixelY / *MacroTileHeightY;
			*MacroTileHeightC = 32 * *BlockHeight256BytesC;
			if (*MacroTileHeightC == 0) {
				*MacroTileWidthC = 0;
			} else {
				*MacroTileWidthC = 65536 * 4 / *BytePerPixelC / *MacroTileHeightC;
			}
		}
	} else {
		unsigned int macro_tile_size_bytes_y = dml2_core_utils_get_tile_block_size_bytes(SurfaceTiling, *BytePerPixelY);
		unsigned int macro_tile_size_bytes_c = dml2_core_utils_get_tile_block_size_bytes(SurfaceTiling, *BytePerPixelY);
		unsigned int macro_tile_scale = 1; // macro tile to 256B req scaling

		if (SurfaceTiling == dml2_sw_linear) {
			macro_tile_scale = 1;
		} else if (SurfaceTiling == dml2_sw_4kb_2d) {
			macro_tile_scale = 4;
		} else if (SurfaceTiling == dml2_sw_64kb_2d) {
			macro_tile_scale = 16;
		} else if (SurfaceTiling == dml2_sw_256kb_2d) {
			macro_tile_scale = 32;
		} else {
			DML_LOG_VERBOSE("ERROR: Invalid SurfaceTiling setting! val=%u\n", SurfaceTiling);
			DML_ASSERT(0);
		}

		*MacroTileHeightY = macro_tile_scale * *BlockHeight256BytesY;
		*MacroTileWidthY = macro_tile_size_bytes_y / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = macro_tile_scale * *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0) {
			*MacroTileWidthC = 0;
		} else {
			*MacroTileWidthC = macro_tile_size_bytes_c / *BytePerPixelC / *MacroTileHeightC;
		}
	}

	DML_LOG_VERBOSE("DML::%s: MacroTileWidthY = %u\n", __func__, *MacroTileWidthY);
	DML_LOG_VERBOSE("DML::%s: MacroTileHeightY = %u\n", __func__, *MacroTileHeightY);
	DML_LOG_VERBOSE("DML::%s: MacroTileWidthC = %u\n", __func__, *MacroTileWidthC);
	DML_LOG_VERBOSE("DML::%s: MacroTileHeightC = %u\n", __func__, *MacroTileHeightC);
}

void dcn5_calculate_swath_width(
		const struct dml2_display_cfg *display_cfg,
		bool ForceSingleDPP,
		unsigned int NumberOfActiveSurfaces,
		enum dml2_odm_mode ODMMode[],
		unsigned int BytePerPixY[],
		unsigned int BytePerPixC[],
		unsigned int Read256BytesBlockHeightY[],
		unsigned int Read256BytesBlockHeightC[],
		unsigned int Read256BytesBlockWidthY[],
		unsigned int Read256BytesBlockWidthC[],
		bool surf_linear128_l[],
		bool surf_linear128_c[],
		unsigned int DPPPerSurface[],

		// Output
		unsigned int req_per_swath_ub_l[],
		unsigned int req_per_swath_ub_c[],
		unsigned int SwathWidthSingleDPPY[],
		unsigned int SwathWidthSingleDPPC[],
		unsigned int SwathWidthY[], // per-pipe
		unsigned int SwathWidthC[], // per-pipe
		unsigned int MaximumSwathHeightY[],
		unsigned int MaximumSwathHeightC[],
		unsigned int swath_width_luma_ub[], // per-pipe
		unsigned int swath_width_chroma_ub[], // per-pipe
		unsigned int swath_width_luma_ub_single_dpp[],
		unsigned int swath_width_chroma_ub_single_dpp[])
{
	(void)BytePerPixY;
	enum dml2_odm_mode MainSurfaceODMMode;
	double odm_hactive_factor = 1.0;
	unsigned int req_width_horz_y;
	unsigned int req_width_horz_c;
	unsigned int surface_width_ub_l;
	unsigned int surface_height_ub_l;
	unsigned int surface_width_ub_c;
	unsigned int surface_height_ub_c;

	DML_LOG_VERBOSE("DML::%s: ForceSingleDPP = %u\n", __func__, ForceSingleDPP);
	DML_LOG_VERBOSE("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (!dml2_core_utils_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle)) {
			SwathWidthSingleDPPY[k] = (unsigned int)display_cfg->plane_descriptors[k].composition.viewport.plane0.width;
		} else {
			SwathWidthSingleDPPY[k] = (unsigned int)display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
		}

		DML_LOG_VERBOSE("DML::%s: k=%u ViewportWidth=%lu\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.plane0.width);
		DML_LOG_VERBOSE("DML::%s: k=%u ViewportHeight=%lu\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.plane0.height);
		DML_LOG_VERBOSE("DML::%s: k=%u DPPPerSurface=%u\n", __func__, k, DPPPerSurface[k]);

		MainSurfaceODMMode = ODMMode[k];

		if (ForceSingleDPP) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
		} else {
			if (MainSurfaceODMMode == dml2_odm_mode_combine_4to1)
				odm_hactive_factor = 4.0;
			else if (MainSurfaceODMMode == dml2_odm_mode_combine_3to1)
				odm_hactive_factor = 3.0;
			else if (MainSurfaceODMMode == dml2_odm_mode_combine_2to1)
				odm_hactive_factor = 2.0;

			if (MainSurfaceODMMode == dml2_odm_mode_combine_4to1 || MainSurfaceODMMode == dml2_odm_mode_combine_3to1 || MainSurfaceODMMode == dml2_odm_mode_combine_2to1) {
				SwathWidthY[k] = (unsigned int)(math_min2((double)SwathWidthSingleDPPY[k], math_round((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active / odm_hactive_factor * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio)));
			} else if (DPPPerSurface[k] == 2) {
				SwathWidthY[k] = SwathWidthSingleDPPY[k] / 2;
			} else {
				SwathWidthY[k] = SwathWidthSingleDPPY[k];
			}
		}

		DML_LOG_VERBOSE("DML::%s: k=%u HActive=%lu\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active);
		DML_LOG_VERBOSE("DML::%s: k=%u HRatio=%f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u MainSurfaceODMMode=%u\n", __func__, k, MainSurfaceODMMode);
		DML_LOG_VERBOSE("DML::%s: k=%u SwathWidthSingleDPPY=%u\n", __func__, k, SwathWidthSingleDPPY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u SwathWidthY=%u\n", __func__, k, SwathWidthY[k]);

		if (dml2_core_utils_is_420(display_cfg->plane_descriptors[k].pixel_format) || dml2_core_utils_is_422_planar(display_cfg->plane_descriptors[k].pixel_format)) {
			SwathWidthC[k] = SwathWidthY[k] / 2;
			SwathWidthSingleDPPC[k] = SwathWidthSingleDPPY[k] / 2;
		} else {
			SwathWidthC[k] = SwathWidthY[k];
			SwathWidthSingleDPPC[k] = SwathWidthSingleDPPY[k];
		}

		if (ForceSingleDPP == true) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
			SwathWidthC[k] = SwathWidthSingleDPPC[k];
		}

		req_width_horz_y = Read256BytesBlockWidthY[k];
		req_width_horz_c = Read256BytesBlockWidthC[k];

		if (surf_linear128_l[k])
			req_width_horz_y = req_width_horz_y / 2;

		if (surf_linear128_c[k])
			req_width_horz_c = req_width_horz_c / 2;

		surface_width_ub_l = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane0.width, req_width_horz_y);
		surface_height_ub_l = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane0.height, Read256BytesBlockHeightY[k]);
		surface_width_ub_c = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane1.width, req_width_horz_c);
		surface_height_ub_c = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane1.height, Read256BytesBlockHeightC[k]);

		DML_LOG_VERBOSE("DML::%s: k=%u surface_width_ub_l=%u\n", __func__, k, surface_width_ub_l);
		DML_LOG_VERBOSE("DML::%s: k=%u surface_height_ub_l=%u\n", __func__, k, surface_height_ub_l);
		DML_LOG_VERBOSE("DML::%s: k=%u surface_width_ub_c=%u\n", __func__, k, surface_width_ub_c);
		DML_LOG_VERBOSE("DML::%s: k=%u surface_height_ub_c=%u\n", __func__, k, surface_height_ub_c);
		DML_LOG_VERBOSE("DML::%s: k=%u req_width_horz_y=%u\n", __func__, k, req_width_horz_y);
		DML_LOG_VERBOSE("DML::%s: k=%u req_width_horz_c=%u\n", __func__, k, req_width_horz_c);
		DML_LOG_VERBOSE("DML::%s: k=%u Read256BytesBlockWidthY=%u\n", __func__, k, Read256BytesBlockWidthY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u Read256BytesBlockHeightY=%u\n", __func__, k, Read256BytesBlockHeightY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u Read256BytesBlockWidthC=%u\n", __func__, k, Read256BytesBlockWidthC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u Read256BytesBlockHeightC=%u\n", __func__, k, Read256BytesBlockHeightC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u req_width_horz_y=%u\n", __func__, k, req_width_horz_y);
		DML_LOG_VERBOSE("DML::%s: k=%u req_width_horz_c=%u\n", __func__, k, req_width_horz_c);
		DML_LOG_VERBOSE("DML::%s: k=%u ViewportStationary=%u\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.stationary);
		DML_LOG_VERBOSE("DML::%s: k=%u DPPPerSurface=%u\n", __func__, k, DPPPerSurface[k]);

		req_per_swath_ub_l[k] = 0;
		req_per_swath_ub_c[k] = 0;
		if (!dml2_core_utils_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle)) {
			MaximumSwathHeightY[k] = Read256BytesBlockHeightY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockHeightC[k];
			if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_width_ub_l,
					math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start
						+ SwathWidthY[k] + req_width_horz_y - 1, req_width_horz_y)
					- math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start, req_width_horz_y)));
				swath_width_luma_ub_single_dpp[k] = swath_width_luma_ub[k];
			} else {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_width_ub_l, math_ceil2((double)SwathWidthY[k] - 1,
					req_width_horz_y) + req_width_horz_y));
				swath_width_luma_ub_single_dpp[k] = (unsigned int)(math_min2(surface_width_ub_l, math_ceil2((double)SwathWidthSingleDPPY[k] - 1,
					req_width_horz_y) + req_width_horz_y));
			}
			req_per_swath_ub_l[k] = swath_width_luma_ub[k] / req_width_horz_y;

			if (BytePerPixC[k] > 0) {
				if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_width_ub_c,
						math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start
							+ SwathWidthC[k] + req_width_horz_c - 1, req_width_horz_c)
						- math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start, req_width_horz_c)));
					swath_width_chroma_ub_single_dpp[k] = swath_width_chroma_ub[k];
				} else {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_width_ub_c, math_ceil2((double)SwathWidthC[k] - 1,
						req_width_horz_c) + req_width_horz_c));
					swath_width_chroma_ub_single_dpp[k] = (unsigned int)(math_min2(surface_width_ub_c, math_ceil2((double)SwathWidthSingleDPPC[k] - 1,
						req_width_horz_c) + req_width_horz_c));
				}
				req_per_swath_ub_c[k] = swath_width_chroma_ub[k] / req_width_horz_c;
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		} else {
			MaximumSwathHeightY[k] = Read256BytesBlockWidthY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockWidthC[k];

			if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_height_ub_l,
					math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start
						+ SwathWidthY[k] + Read256BytesBlockHeightY[k] - 1, Read256BytesBlockHeightY[k])
					- math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start, Read256BytesBlockHeightY[k])));
				swath_width_luma_ub_single_dpp[k] = swath_width_luma_ub[k];
			} else {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_height_ub_l, math_ceil2((double)SwathWidthY[k] - 1,
					Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]));
				swath_width_luma_ub_single_dpp[k] = (unsigned int)(math_min2(surface_height_ub_l, math_ceil2((double)SwathWidthSingleDPPY[k] - 1,
					Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]));
			}
			req_per_swath_ub_l[k] = swath_width_luma_ub[k] / Read256BytesBlockHeightY[k];
			if (BytePerPixC[k] > 0) {
				if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_height_ub_c,
						math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start
							+ SwathWidthC[k] + Read256BytesBlockHeightC[k] - 1, Read256BytesBlockHeightC[k])
						- math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start, Read256BytesBlockHeightC[k])));
					swath_width_chroma_ub_single_dpp[k] = swath_width_chroma_ub[k];
				} else {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_height_ub_c, math_ceil2((double)SwathWidthC[k] - 1,
						Read256BytesBlockHeightC[k]) + Read256BytesBlockHeightC[k]));
					swath_width_chroma_ub_single_dpp[k] = (unsigned int)(math_min2(surface_height_ub_c, math_ceil2((double)SwathWidthSingleDPPC[k] - 1,
						Read256BytesBlockHeightC[k]) + Read256BytesBlockHeightC[k]));
				}
				req_per_swath_ub_c[k] = swath_width_chroma_ub[k] / Read256BytesBlockHeightC[k];
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		}

		DML_LOG_VERBOSE("DML::%s: k=%u swath_width_luma_ub=%u\n", __func__, k, swath_width_luma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u swath_width_chroma_ub=%u\n", __func__, k, swath_width_chroma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathHeightY=%u\n", __func__, k, MaximumSwathHeightY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathHeightC=%u\n", __func__, k, MaximumSwathHeightC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u req_per_swath_ub_l=%u\n", __func__, k, req_per_swath_ub_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u req_per_swath_ub_c=%u\n", __func__, k, req_per_swath_ub_c[k]);
	}
}

static bool dcn5_is_unbounded_request(bool unb_req_force_en, bool unb_req_force_val, unsigned int TotalNumberOfActiveDPP, bool NoChromaOrLinear)
{
	bool unb_req_ok = false;
	bool unb_req_en = false;

	unb_req_ok = (TotalNumberOfActiveDPP == 1 && NoChromaOrLinear);
	unb_req_en = unb_req_ok;

	if (unb_req_force_en) {
		unb_req_en = unb_req_force_val && unb_req_ok;
	}
	DML_LOG_VERBOSE("DML::%s: unb_req_force_en = %u\n", __func__, unb_req_force_en);
	DML_LOG_VERBOSE("DML::%s: unb_req_force_val = %u\n", __func__, unb_req_force_val);
	DML_LOG_VERBOSE("DML::%s: unb_req_ok = %u\n", __func__, unb_req_ok);
	DML_LOG_VERBOSE("DML::%s: unb_req_en = %u\n", __func__, unb_req_en);
	return unb_req_en;
}

static void dcn5_calculate_det_buffer_size(
		struct dml2_core_shared_CalculateDETBufferSize_locals *l,
		const struct dml2_display_cfg *display_cfg,
		bool ForceSingleDPP,
		unsigned int NumberOfActiveSurfaces,
		bool UnboundedRequestEnabled,
		unsigned int nomDETInKByte,
		unsigned int MaxTotalDETInKByte,
		unsigned int ConfigReturnBufferSizeInKByte,
		unsigned int MinCompressedBufferSizeInKByte,
		unsigned int ConfigReturnBufferSegmentSizeInkByte,
		unsigned int CompressedBufferSegmentSizeInkByte,
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		unsigned int full_swath_bytes_l[],
		unsigned int full_swath_bytes_c[],
		unsigned int DPPPerSurface[],
		// Output
		unsigned int DETBufferSizeInKByte[],
		unsigned int *CompressedBufferSizeInkByte)
{
	memset(l, 0, sizeof(struct dml2_core_shared_CalculateDETBufferSize_locals));

	bool DETPieceAssignedToThisSurfaceAlready[DML2_MAX_PLANES];
	bool NextPotentialSurfaceToAssignDETPieceFound;
	bool MinimizeReallocationSuccess = false;

	DML_LOG_VERBOSE("DML::%s: ForceSingleDPP = %u\n", __func__, ForceSingleDPP);
	DML_LOG_VERBOSE("DML::%s: nomDETInKByte = %u\n", __func__, nomDETInKByte);
	DML_LOG_VERBOSE("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
	DML_LOG_VERBOSE("DML::%s: UnboundedRequestEnabled = %u\n", __func__, UnboundedRequestEnabled);
	DML_LOG_VERBOSE("DML::%s: MaxTotalDETInKByte = %u\n", __func__, MaxTotalDETInKByte);
	DML_LOG_VERBOSE("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, ConfigReturnBufferSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: MinCompressedBufferSizeInKByte = %u\n", __func__, MinCompressedBufferSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: CompressedBufferSegmentSizeInkByte = %u\n", __func__, CompressedBufferSegmentSizeInkByte);
	DML_LOG_VERBOSE("DML::%s: ConfigReturnBufferSegmentSizeInkByte = %u\n", __func__, ConfigReturnBufferSegmentSizeInkByte);

	// Note: Will use default det size if that fits 2 swaths
	if (UnboundedRequestEnabled) {
		if (display_cfg->plane_descriptors[0].overrides.det_size_override_kb > 0) {
			DETBufferSizeInKByte[0] = display_cfg->plane_descriptors[0].overrides.det_size_override_kb;
		} else {
			DETBufferSizeInKByte[0] = (unsigned int)math_max2(128.0, math_ceil2(2.0 * ((double)full_swath_bytes_l[0] + (double)full_swath_bytes_c[0]) / 1024.0, ConfigReturnBufferSegmentSizeInkByte));
		}
		*CompressedBufferSizeInkByte = ConfigReturnBufferSizeInKByte - DETBufferSizeInKByte[0];
	} else {
		l->DETBufferSizePoolInKByte = MaxTotalDETInKByte;
		for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
			DETBufferSizeInKByte[k] = 0;
			if (dml2_core_utils_is_420(display_cfg->plane_descriptors[k].pixel_format) || dml2_core_utils_is_422_planar(display_cfg->plane_descriptors[k].pixel_format)) {
				l->max_minDET = nomDETInKByte - ConfigReturnBufferSegmentSizeInkByte;
			} else {
				l->max_minDET = nomDETInKByte;
			}
			l->minDET = 128;
			l->minDET_pipe = 0;

			// add DET resource until can hold 2 full swaths
			while (l->minDET <= l->max_minDET && l->minDET_pipe == 0) {
				if (2.0 * ((double)full_swath_bytes_l[k] + (double)full_swath_bytes_c[k]) / 1024.0 <= l->minDET)
					l->minDET_pipe = l->minDET;
				l->minDET = l->minDET + ConfigReturnBufferSegmentSizeInkByte;
			}

			DML_LOG_VERBOSE("DML::%s: k=%u minDET = %u\n", __func__, k, l->minDET);
			DML_LOG_VERBOSE("DML::%s: k=%u max_minDET = %u\n", __func__, k, l->max_minDET);
			DML_LOG_VERBOSE("DML::%s: k=%u minDET_pipe = %u\n", __func__, k, l->minDET_pipe);
			DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_l = %u\n", __func__, k, full_swath_bytes_l[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_c = %u\n", __func__, k, full_swath_bytes_c[k]);
			if (l->minDET_pipe == 0) {
				l->minDET_pipe = (unsigned int)(math_max2(128, math_ceil2(((double)full_swath_bytes_l[k] + (double)full_swath_bytes_c[k]) / 1024.0, ConfigReturnBufferSegmentSizeInkByte)));
				DML_LOG_VERBOSE("DML::%s: k=%u minDET_pipe = %u (assume each plane take half DET)\n", __func__, k, l->minDET_pipe);
			}

			if (display_cfg->plane_descriptors[k].overrides.det_size_override_kb > 0) {
				DETBufferSizeInKByte[k] = display_cfg->plane_descriptors[k].overrides.det_size_override_kb;
				l->DETBufferSizePoolInKByte = l->DETBufferSizePoolInKByte - (ForceSingleDPP ? 1 : DPPPerSurface[k]) * display_cfg->plane_descriptors[k].overrides.det_size_override_kb;
			} else if ((ForceSingleDPP ? 1 : DPPPerSurface[k]) * l->minDET_pipe <= l->DETBufferSizePoolInKByte) {
				DETBufferSizeInKByte[k] = l->minDET_pipe;
				l->DETBufferSizePoolInKByte = l->DETBufferSizePoolInKByte - (ForceSingleDPP ? 1 : DPPPerSurface[k]) * l->minDET_pipe;
			}

			DML_LOG_VERBOSE("DML::%s: k=%u DPPPerSurface = %u\n", __func__, k, DPPPerSurface[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u DETSizeOverride = %u\n", __func__, k, display_cfg->plane_descriptors[k].overrides.det_size_override_kb);
			DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, DETBufferSizeInKByte[k]);
			DML_LOG_VERBOSE("DML::%s: DETBufferSizePoolInKByte = %u\n", __func__, l->DETBufferSizePoolInKByte);
		}

		if (display_cfg->minimize_det_reallocation) {
			MinimizeReallocationSuccess = true;
			// To minimize det reallocation, we don't distribute based on each surfaces bandwidth proportional to the global
			// but rather distribute DET across streams proportionally based on pixel rate, and only distribute based on
			// bandwidth between the planes on the same stream.  This ensures that large scale re-distribution only on a
			// stream count and/or pixel rate change, which is must less likely then general bandwidth changes per plane.

			// Calculate total pixel rate
			for (unsigned int k = 0; k < display_cfg->num_streams; ++k) {
				l->TotalPixelRate += display_cfg->stream_descriptors[k].timing.pixel_clock_khz;
			}

			// Calculate per stream DET budget
			for (unsigned int k = 0; k < display_cfg->num_streams; ++k) {
				l->DETBudgetPerStream[k] = (unsigned int)((double) display_cfg->stream_descriptors[k].timing.pixel_clock_khz * MaxTotalDETInKByte / l->TotalPixelRate);
				l->RemainingDETBudgetPerStream[k] = l->DETBudgetPerStream[k];
			}

			// Calculate the per stream total bandwidth
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				l->TotalBandwidthPerStream[display_cfg->plane_descriptors[k].stream_index] += (unsigned int)(ReadBandwidthLuma[k] + ReadBandwidthChroma[k]);

				// Check the minimum can be satisfied by budget
				if (l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index] >= DETBufferSizeInKByte[k] * (ForceSingleDPP ? 1 : DPPPerSurface[k])) {
					l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index] -= DETBufferSizeInKByte[k] * (ForceSingleDPP ? 1 : DPPPerSurface[k]);
				} else {
					MinimizeReallocationSuccess = false;
					break;
				}
			}

			if (MinimizeReallocationSuccess) {
				// Since a fixed budget per stream is sufficient to satisfy the minimums, just re-distribute each streams
				// budget proportionally across its planes
				l->ResidualDETAfterRounding = MaxTotalDETInKByte;

				for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
					l->IdealDETBudget = (unsigned int)(((ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) / l->TotalBandwidthPerStream[display_cfg->plane_descriptors[k].stream_index])
							* l->DETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index]);

					if (l->IdealDETBudget > DETBufferSizeInKByte[k]) {
						l->DeltaDETBudget = l->IdealDETBudget - DETBufferSizeInKByte[k];
						if (l->DeltaDETBudget > l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index])
							l->DeltaDETBudget = l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index];

						/* split the additional budgeted DET among the pipes per plane */
						DETBufferSizeInKByte[k] += (unsigned int)((double)l->DeltaDETBudget / (ForceSingleDPP ? 1 : DPPPerSurface[k]));
						l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index] -= l->DeltaDETBudget;
					}

					// Round down to segment size
					DETBufferSizeInKByte[k] = (DETBufferSizeInKByte[k] / ConfigReturnBufferSegmentSizeInkByte) * ConfigReturnBufferSegmentSizeInkByte;

					l->ResidualDETAfterRounding -= DETBufferSizeInKByte[k] * (ForceSingleDPP ? 1 : DPPPerSurface[k]);
				}
			}
		}

		if (!MinimizeReallocationSuccess) {
			l->TotalBandwidth = 0;
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				l->TotalBandwidth = l->TotalBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
			}
			DML_LOG_VERBOSE("DML::%s: --- Before bandwidth adjustment ---\n", __func__);
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, DETBufferSizeInKByte[k]);
			}
			DML_LOG_VERBOSE("DML::%s: --- DET allocation with bandwidth ---\n", __func__);
			DML_LOG_VERBOSE("DML::%s: TotalBandwidth = %f\n", __func__, l->TotalBandwidth);
			l->BandwidthOfSurfacesNotAssignedDETPiece = l->TotalBandwidth;
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				if (display_cfg->plane_descriptors[k].overrides.det_size_override_kb > 0 || (((double)(ForceSingleDPP ? 1 : DPPPerSurface[k]) * (double)DETBufferSizeInKByte[k] / (double)MaxTotalDETInKByte) >= ((ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) / l->TotalBandwidth))) {
					DETPieceAssignedToThisSurfaceAlready[k] = true;
					l->BandwidthOfSurfacesNotAssignedDETPiece = l->BandwidthOfSurfacesNotAssignedDETPiece - ReadBandwidthLuma[k] - ReadBandwidthChroma[k];
				} else {
					DETPieceAssignedToThisSurfaceAlready[k] = false;
				}
				DML_LOG_VERBOSE("DML::%s: k=%u DETPieceAssignedToThisSurfaceAlready = %u\n", __func__, k, DETPieceAssignedToThisSurfaceAlready[k]);
				DML_LOG_VERBOSE("DML::%s: k=%u BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, k, l->BandwidthOfSurfacesNotAssignedDETPiece);
			}

			for (unsigned int j = 0; j < NumberOfActiveSurfaces; ++j) {
				NextPotentialSurfaceToAssignDETPieceFound = false;
				l->NextSurfaceToAssignDETPiece = 0;

				for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, ReadBandwidthLuma[k] = %f\n", __func__, j, k, ReadBandwidthLuma[k]);
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, ReadBandwidthChroma[k] = %f\n", __func__, j, k, ReadBandwidthChroma[k]);
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, ReadBandwidthLuma[Next] = %f\n", __func__, j, k, ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece]);
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, ReadBandwidthChroma[Next] = %f\n", __func__, j, k, ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]);
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, NextSurfaceToAssignDETPiece = %u\n", __func__, j, k, l->NextSurfaceToAssignDETPiece);
					if (!DETPieceAssignedToThisSurfaceAlready[k] && (!NextPotentialSurfaceToAssignDETPieceFound ||
							ReadBandwidthLuma[k] + ReadBandwidthChroma[k] < ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece])) {
						l->NextSurfaceToAssignDETPiece = k;
						NextPotentialSurfaceToAssignDETPieceFound = true;
					}
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, DETPieceAssignedToThisSurfaceAlready = %u\n", __func__, j, k, DETPieceAssignedToThisSurfaceAlready[k]);
					DML_LOG_VERBOSE("DML::%s: j=%u k=%u, NextPotentialSurfaceToAssignDETPieceFound = %u\n", __func__, j, k, NextPotentialSurfaceToAssignDETPieceFound);
				}

				if (NextPotentialSurfaceToAssignDETPieceFound) {
					l->NextDETBufferPieceInKByte = (unsigned int)(math_min2(
							math_round((double)l->DETBufferSizePoolInKByte * (ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]) / l->BandwidthOfSurfacesNotAssignedDETPiece /
									((ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte))
									* (ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte,
									math_floor2((double)l->DETBufferSizePoolInKByte, (ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte)));

					DML_LOG_VERBOSE("DML::%s: j=%u, DETBufferSizePoolInKByte = %u\n", __func__, j, l->DETBufferSizePoolInKByte);
					DML_LOG_VERBOSE("DML::%s: j=%u, NextSurfaceToAssignDETPiece = %u\n", __func__, j, l->NextSurfaceToAssignDETPiece);
					DML_LOG_VERBOSE("DML::%s: j=%u, ReadBandwidthLuma[%u] = %f\n", __func__, j, l->NextSurfaceToAssignDETPiece, ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece]);
					DML_LOG_VERBOSE("DML::%s: j=%u, ReadBandwidthChroma[%u] = %f\n", __func__, j, l->NextSurfaceToAssignDETPiece, ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]);
					DML_LOG_VERBOSE("DML::%s: j=%u, BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, j, l->BandwidthOfSurfacesNotAssignedDETPiece);
					DML_LOG_VERBOSE("DML::%s: j=%u, NextDETBufferPieceInKByte = %u\n", __func__, j, l->NextDETBufferPieceInKByte);
					DML_LOG_VERBOSE("DML::%s: j=%u, DETBufferSizeInKByte[%u] increases from %u ", __func__, j, l->NextSurfaceToAssignDETPiece, DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece]);

					DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece] = DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece] + l->NextDETBufferPieceInKByte / (ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]);
					DML_LOG_VERBOSE("to %u\n", DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece]);

					l->DETBufferSizePoolInKByte = l->DETBufferSizePoolInKByte - l->NextDETBufferPieceInKByte;
					DETPieceAssignedToThisSurfaceAlready[l->NextSurfaceToAssignDETPiece] = true;
					l->BandwidthOfSurfacesNotAssignedDETPiece = l->BandwidthOfSurfacesNotAssignedDETPiece - (ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]);
				}
			}
		}
		*CompressedBufferSizeInkByte = MinCompressedBufferSizeInKByte;
	}
	*CompressedBufferSizeInkByte = *CompressedBufferSizeInkByte * CompressedBufferSegmentSizeInkByte / ConfigReturnBufferSegmentSizeInkByte;

	DML_LOG_VERBOSE("DML::%s: --- After bandwidth adjustment ---\n", __func__);
	DML_LOG_VERBOSE("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, *CompressedBufferSizeInkByte);
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeInKByte = %u (TotalReadBandWidth=%f)\n", __func__, k, DETBufferSizeInKByte[k], ReadBandwidthLuma[k] + ReadBandwidthChroma[k]);
	}
}

void dcn5_calculate_swath_and_det_configuration(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p)
{
	unsigned int MaximumSwathHeightY[DML2_MAX_PLANES] = { 0 };
	unsigned int MaximumSwathHeightC[DML2_MAX_PLANES] = { 0 };
	unsigned int RoundedUpSwathSizeBytesY[DML2_MAX_PLANES] = { 0 };
	unsigned int RoundedUpSwathSizeBytesC[DML2_MAX_PLANES] = { 0 };

	unsigned int TotalActiveDPP = 0;
	bool NoChromaOrLinear = true;
	unsigned int SurfaceDoingUnboundedRequest = 0;
	unsigned int DETBufferSizeInKByteForSwathCalculation;

	const long TTUFIFODEPTH = 8;
	const long MAXIMUMCOMPRESSION = 4;

	DML_LOG_VERBOSE("DML::%s: ForceSingleDPP = %u\n", __func__, p->ForceSingleDPP);
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		DML_LOG_VERBOSE("DML::%s: DPPPerSurface[%u] = %u\n", __func__, k, p->DPPPerSurface[k]);
	}
	dcn5_calculate_swath_width(
			p->display_cfg,
			p->ForceSingleDPP,
			p->NumberOfActiveSurfaces,
			p->ODMMode,
			p->BytePerPixY,
			p->BytePerPixC,
			p->Read256BytesBlockHeightY,
			p->Read256BytesBlockHeightC,
			p->Read256BytesBlockWidthY,
			p->Read256BytesBlockWidthC,
			p->surf_linear128_l,
			p->surf_linear128_c,
			p->DPPPerSurface,

			// Output
			p->req_per_swath_ub_l,
			p->req_per_swath_ub_c,
			p->dummy[0],
			p->dummy[1],
			p->SwathWidth,
			p->SwathWidthChroma,
			MaximumSwathHeightY,
			MaximumSwathHeightC,
			p->swath_width_luma_ub,
			p->swath_width_chroma_ub,
			p->swath_width_luma_ub_single_dpp,
			p->swath_width_chroma_ub_single_dpp);


	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->full_swath_bytes_single_dpp_l[k] = (unsigned int)(p->swath_width_luma_ub_single_dpp[k] * p->BytePerPixDETY[k] * MaximumSwathHeightY[k]);
		p->full_swath_bytes_single_dpp_c[k] = (unsigned int)(p->swath_width_chroma_ub_single_dpp[k] * p->BytePerPixDETC[k] * MaximumSwathHeightC[k]);
		p->full_swath_bytes_l[k] = (unsigned int)(p->swath_width_luma_ub[k] * p->BytePerPixDETY[k] * MaximumSwathHeightY[k]);
		p->full_swath_bytes_c[k] = (unsigned int)(p->swath_width_chroma_ub[k] * p->BytePerPixDETC[k] * MaximumSwathHeightC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u DPPPerSurface = %u\n", __func__, k, p->DPPPerSurface[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u swath_width_luma_ub = %u\n", __func__, k, p->swath_width_luma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u BytePerPixDETY = %f\n", __func__, k, p->BytePerPixDETY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathHeightY = %u\n", __func__, k, MaximumSwathHeightY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_l = %u\n", __func__, k, p->full_swath_bytes_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u swath_width_chroma_ub = %u\n", __func__, k, p->swath_width_chroma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u BytePerPixDETC = %f\n", __func__, k, p->BytePerPixDETC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathHeightC = %u\n", __func__, k, MaximumSwathHeightC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_c = %u\n", __func__, k, p->full_swath_bytes_c[k]);
		if (p->display_cfg->plane_descriptors[k].pixel_format == dml2_420_10
			|| p->display_cfg->plane_descriptors[k].pixel_format == dml2_422_planar_10
			|| p->display_cfg->plane_descriptors[k].pixel_format == dml2_422_packed_10) {
			p->full_swath_bytes_l[k] = (unsigned int)(math_ceil2((double)p->full_swath_bytes_l[k], 256));
			p->full_swath_bytes_c[k] = (unsigned int)(math_ceil2((double)p->full_swath_bytes_c[k], 256));
			p->full_swath_bytes_single_dpp_l[k] = (unsigned int)(math_ceil2((double)p->full_swath_bytes_single_dpp_l[k], 256));
			p->full_swath_bytes_single_dpp_c[k] = (unsigned int)(math_ceil2((double)p->full_swath_bytes_single_dpp_c[k], 256));
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		TotalActiveDPP = TotalActiveDPP + (p->ForceSingleDPP ? 1 : p->DPPPerSurface[k]);
		if (p->DPPPerSurface[k] > 0)
			SurfaceDoingUnboundedRequest = k;
		if (dml2_core_utils_is_420(p->display_cfg->plane_descriptors[k].pixel_format)
				|| dml2_core_utils_is_422_planar(p->display_cfg->plane_descriptors[k].pixel_format)
				|| p->display_cfg->plane_descriptors[k].pixel_format == dml2_rgbe_alpha
				|| dml2_core_utils_is_linear(p->display_cfg->plane_descriptors[k].surface.tiling)) {
			NoChromaOrLinear = false;
		}
	}

	*p->UnboundedRequestEnabled = dcn5_is_unbounded_request(p->display_cfg->overrides.hw.force_unbounded_requesting.enable, p->display_cfg->overrides.hw.force_unbounded_requesting.value, TotalActiveDPP, NoChromaOrLinear);

	dcn5_calculate_det_buffer_size(
			&scratch->CalculateDETBufferSize_locals,
			p->display_cfg,
			p->ForceSingleDPP,
			p->NumberOfActiveSurfaces,
			*p->UnboundedRequestEnabled,
			p->nomDETInKByte,
			p->MaxTotalDETInKByte,
			p->ConfigReturnBufferSizeInKByte,
			p->MinCompressedBufferSizeInKByte,
			p->ConfigReturnBufferSegmentSizeInkByte,
			p->CompressedBufferSegmentSizeInkByte,
			p->ReadBandwidthLuma,
			p->ReadBandwidthChroma,
			p->full_swath_bytes_l,
			p->full_swath_bytes_c,
			p->DPPPerSurface,

			// Output
			p->DETBufferSizeInKByte, // per hubp pipe
			p->CompressedBufferSizeInkByte);

	DML_LOG_VERBOSE("DML::%s: TotalActiveDPP = %u\n", __func__, TotalActiveDPP);
	DML_LOG_VERBOSE("DML::%s: nomDETInKByte = %u\n", __func__, p->nomDETInKByte);
	DML_LOG_VERBOSE("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, p->ConfigReturnBufferSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: UnboundedRequestEnabled = %u\n", __func__, *p->UnboundedRequestEnabled);
	DML_LOG_VERBOSE("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, *p->CompressedBufferSizeInkByte);

	*p->ViewportSizeSupport = true;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {

		DETBufferSizeInKByteForSwathCalculation = p->DETBufferSizeInKByte[k];
		DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeInKByteForSwathCalculation = %u\n", __func__, k, DETBufferSizeInKByteForSwathCalculation);
		if (dml2_core_utils_is_linear(p->display_cfg->plane_descriptors[k].surface.tiling)) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k];
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k];

			if (p->surf_linear128_l[k])
				p->request_size_bytes_luma[k] = 128;
			else
				p->request_size_bytes_luma[k] = 256;

			if (p->surf_linear128_c[k])
				p->request_size_bytes_chroma[k] = 128;
			else
				p->request_size_bytes_chroma[k] = 256;

		} else if (p->full_swath_bytes_l[k] + p->full_swath_bytes_c[k] <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k];
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k];
			p->request_size_bytes_luma[k] = 256;
			p->request_size_bytes_chroma[k] = 256;

		} else if (p->full_swath_bytes_l[k] >= 1.5 * p->full_swath_bytes_c[k] && p->full_swath_bytes_l[k] / 2 + p->full_swath_bytes_c[k] <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k] / 2;
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k];
			p->request_size_bytes_luma[k] = dml2_core_utils_get_segment_horizontal_contiguous(p->BytePerPixY[k], p->display_cfg->plane_descriptors[k].surface.tiling)
				== dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) ? 64 : 128;
			p->request_size_bytes_chroma[k] = 256;

		} else if (p->full_swath_bytes_l[k] < 1.5 * p->full_swath_bytes_c[k] && p->full_swath_bytes_l[k] + p->full_swath_bytes_c[k] / 2 <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k];
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k] / 2;
			p->request_size_bytes_luma[k] = 256;
			p->request_size_bytes_chroma[k] = dml2_core_utils_get_segment_horizontal_contiguous(p->BytePerPixC[k], p->display_cfg->plane_descriptors[k].surface.tiling)
				== dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) ? 64 : 128;

		} else {
			p->SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			p->SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k] / 2;
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k] / 2;
			p->request_size_bytes_luma[k] = dml2_core_utils_get_segment_horizontal_contiguous(p->BytePerPixY[k], p->display_cfg->plane_descriptors[k].surface.tiling)
				== dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) ? 64 : 128;
			p->request_size_bytes_chroma[k] = dml2_core_utils_get_segment_horizontal_contiguous(p->BytePerPixC[k], p->display_cfg->plane_descriptors[k].surface.tiling)
				== dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) ? 64 : 128;
		}

		if (p->SwathHeightC[k] == 0)
			p->request_size_bytes_chroma[k] = 0;

		if ((p->full_swath_bytes_l[k] / 2 + p->full_swath_bytes_c[k] / 2 > DETBufferSizeInKByteForSwathCalculation * 1024 / 2) ||
				p->SwathWidth[k] > p->MaximumSwathWidthLuma[k] || (p->SwathHeightC[k] > 0 && p->SwathWidthChroma[k] > p->MaximumSwathWidthChroma[k])) {
			*p->ViewportSizeSupport = false;
			DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_l=%u\n", __func__, k, p->full_swath_bytes_l[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_c=%u\n", __func__, k, p->full_swath_bytes_c[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeInKByteForSwathCalculation=%u\n", __func__, k, DETBufferSizeInKByteForSwathCalculation);
			DML_LOG_VERBOSE("DML::%s: k=%u SwathWidth=%u\n", __func__, k, p->SwathWidth[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthLuma=%f\n", __func__, k, p->MaximumSwathWidthLuma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u SwathWidthChroma=%d\n", __func__, k, p->SwathWidthChroma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthChroma=%f\n", __func__, k, p->MaximumSwathWidthChroma[k]);
			p->ViewportSizeSupportPerSurface[k] = false;
		} else {
			p->ViewportSizeSupportPerSurface[k] = true;
		}

		if (p->SwathHeightC[k] == 0) {
			DML_LOG_VERBOSE("DML::%s: k=%u, All DET will be used for plane0\n", __func__, k);
			p->DETBufferSizeY[k] = p->DETBufferSizeInKByte[k] * 1024;
			p->DETBufferSizeC[k] = 0;
		} else if (RoundedUpSwathSizeBytesY[k] <= 1.5 * RoundedUpSwathSizeBytesC[k]) {
			DML_LOG_VERBOSE("DML::%s: k=%u, Half DET will be used for plane0, and half for plane1\n", __func__, k);
			p->DETBufferSizeY[k] = p->DETBufferSizeInKByte[k] * 1024 / 2;
			p->DETBufferSizeC[k] = p->DETBufferSizeInKByte[k] * 1024 / 2;
		} else {
			DML_LOG_VERBOSE("DML::%s: k=%u, 2/3 DET will be used for plane0, and 1/3 for plane1\n", __func__, k);
			p->DETBufferSizeY[k] = (unsigned int)(math_floor2(p->DETBufferSizeInKByte[k] * 1024 * 2 / 3, 1024));
			p->DETBufferSizeC[k] = p->DETBufferSizeInKByte[k] * 1024 - p->DETBufferSizeY[k];
		}

		DML_LOG_VERBOSE("DML::%s: k=%u SwathHeightY = %u\n", __func__, k, p->SwathHeightY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u SwathHeightC = %u\n", __func__, k, p->SwathHeightC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_l = %u\n", __func__, k, p->full_swath_bytes_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u full_swath_bytes_c = %u\n", __func__, k, p->full_swath_bytes_c[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u RoundedUpSwathSizeBytesY = %u\n", __func__, k, RoundedUpSwathSizeBytesY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u RoundedUpSwathSizeBytesC = %u\n", __func__, k, RoundedUpSwathSizeBytesC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, p->DETBufferSizeInKByte[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u DETBufferSizeC = %u\n", __func__, k, p->DETBufferSizeC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u ViewportSizeSupportPerSurface = %u\n", __func__, k, p->ViewportSizeSupportPerSurface[k]);

	}

	*p->compbuf_reserved_space_64b = 2 * p->pixel_chunk_size_kbytes * 1024 / 64;
	if (*p->UnboundedRequestEnabled) {
		*p->compbuf_reserved_space_64b = (unsigned int)math_ceil2(math_max2(*p->compbuf_reserved_space_64b,
				(double)(p->rob_buffer_size_kbytes * 1024 / 64) - (double)(RoundedUpSwathSizeBytesY[SurfaceDoingUnboundedRequest] * TTUFIFODEPTH / (p->mrq_present ? MAXIMUMCOMPRESSION : 1) / 64)), 1.0);
		DML_LOG_VERBOSE("DML::%s: RoundedUpSwathSizeBytesY[%d] = %u\n", __func__, SurfaceDoingUnboundedRequest, RoundedUpSwathSizeBytesY[SurfaceDoingUnboundedRequest]);
		DML_LOG_VERBOSE("DML::%s: rob_buffer_size_kbytes = %u\n", __func__, p->rob_buffer_size_kbytes);
	}
	DML_LOG_VERBOSE("DML::%s: compbuf_reserved_space_64b = %u\n", __func__, *p->compbuf_reserved_space_64b);

	*p->hw_debug5 = false;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (!(p->mrq_present) && (!p->UnboundedRequestEnabled) && (TotalActiveDPP == 1)
				&& p->display_cfg->plane_descriptors[k].surface.dcc.enable
				&& ((p->rob_buffer_size_kbytes * 1024 * (p->mrq_present ? MAXIMUMCOMPRESSION : 1)
						+ *p->CompressedBufferSizeInkByte * MAXIMUMCOMPRESSION * 1024) > TTUFIFODEPTH * (RoundedUpSwathSizeBytesY[k] + RoundedUpSwathSizeBytesC[k])))
			*p->hw_debug5 = true;
		DML_LOG_VERBOSE("DML::%s: k=%u UnboundedRequestEnabled = %u\n", __func__, k, *p->UnboundedRequestEnabled);
		DML_LOG_VERBOSE("DML::%s: k=%u MAXIMUMCOMPRESSION = %lu\n", __func__, k, MAXIMUMCOMPRESSION);
		DML_LOG_VERBOSE("DML::%s: k=%u TTUFIFODEPTH = %lu\n", __func__, k, TTUFIFODEPTH);
		DML_LOG_VERBOSE("DML::%s: k=%u CompressedBufferSizeInkByte = %u\n", __func__, k, *p->CompressedBufferSizeInkByte);
		DML_LOG_VERBOSE("DML::%s: k=%u RoundedUpSwathSizeBytesC = %u\n", __func__, k, RoundedUpSwathSizeBytesC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u hw_debug5 = %u\n", __func__, k, *p->hw_debug5);
	}
}

static unsigned int dcn5_calculate_host_vm_dynamic_levels(
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMinPageSize,
		unsigned int HostVMMaxNonCachedPageTableLevels)
{
	unsigned int HostVMDynamicLevels = 0;

	if (GPUVMEnable && HostVMEnable) {
		if (HostVMMinPageSize < 2048)
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576)
			HostVMDynamicLevels = (unsigned int)math_max2(0, (double)HostVMMaxNonCachedPageTableLevels - 1);
		else
			HostVMDynamicLevels = (unsigned int)math_max2(0, (double)HostVMMaxNonCachedPageTableLevels - 2);
	} else {
		HostVMDynamicLevels = 0;
	}
	return HostVMDynamicLevels;
}

static unsigned int dcn5_calculate_prefetch_source_lines(
		double VRatio,
		unsigned int VTaps,
		bool UPSPEnabled,
		unsigned int UPSPVTaps,
		enum dml2_sample_positioning UPSPSamplePositioning,
		bool PixelFormatIs420,
		bool Interlace,
		bool ProgressiveToInterlaceUnitInOPP,
		unsigned int SwathHeight,
		enum dml2_rotation_angle RotationAngle,
		bool mirrored,
		bool ViewportStationary,
		unsigned int SwathWidth,
		unsigned int ViewportHeight,
		unsigned int ViewportXStart,
		unsigned int ViewportYStart,

		// Output
		unsigned int *VInitPreFill,
		unsigned int *MaxNumSwath)
{

	unsigned int vp_start_rot = 0;
	unsigned int sw0_tmp = 0;
	unsigned int MaxPartialSwath = 0;
	unsigned int VInitPreFillUPSP = 0;
	unsigned int VInitPreFillDSCL = 0;
	const float UPSPVratio = 0.5;
	double numLines = 0;

	DML_LOG_VERBOSE("DML::%s: VRatio = %f\n", __func__, VRatio);
	DML_LOG_VERBOSE("DML::%s: VTaps = %u\n", __func__, VTaps);
	DML_LOG_VERBOSE("DML::%s: ViewportXStart = %u\n", __func__, ViewportXStart);
	DML_LOG_VERBOSE("DML::%s: ViewportYStart = %u\n", __func__, ViewportYStart);
	DML_LOG_VERBOSE("DML::%s: ViewportStationary = %u\n", __func__, ViewportStationary);
	DML_LOG_VERBOSE("DML::%s: SwathHeight = %u\n", __func__, SwathHeight);

	if (UPSPEnabled && PixelFormatIs420) {
		//VRatio = (DSCL Vratio)/2
		VInitPreFillUPSP = (unsigned int)(math_floor2((UPSPVTaps + UPSPVratio + 1) / 2.0 + ((UPSPSamplePositioning == dml2_cosited) ? 0.25 : 0), 1));
		VInitPreFillDSCL = (unsigned int)(math_floor2((2 * VRatio + (double)VTaps + 1) / 2.0, 1)); // DSCL vratio is 2 * Vratio, so the total Vratio does not change
		*VInitPreFill 	 = (unsigned int)(math_floor2(VInitPreFillUPSP + (VInitPreFillDSCL - 1) * UPSPVratio, 1));
	} else if (ProgressiveToInterlaceUnitInOPP) {
		*VInitPreFill = (unsigned int)(math_floor2((VRatio + (double)VTaps + 1) / 2.0, 1));
	} else {
		*VInitPreFill = (unsigned int)(math_floor2((VRatio + (double)VTaps + 1 + (Interlace ? 1 : 0) * 0.5 * VRatio) / 2.0, 1));
	}

	if (ViewportStationary) {
		if (RotationAngle == dml2_rotation_180) {
			vp_start_rot = SwathHeight - (((unsigned int)(ViewportYStart + ViewportHeight - 1) % SwathHeight) + 1);
		} else if ((RotationAngle == dml2_rotation_270 && !mirrored) || (RotationAngle == dml2_rotation_90 && mirrored)) {
			vp_start_rot = ViewportXStart;
		} else if ((RotationAngle == dml2_rotation_90 && !mirrored) || (RotationAngle == dml2_rotation_270 && mirrored)) {
			vp_start_rot = SwathHeight - (((unsigned int)(ViewportYStart + SwathWidth - 1) % SwathHeight) + 1);
		} else {
			vp_start_rot = ViewportYStart;
		}
		sw0_tmp = SwathHeight - (vp_start_rot % SwathHeight);
		if (sw0_tmp < *VInitPreFill) {
			*MaxNumSwath = (unsigned int)(math_ceil2((*VInitPreFill - sw0_tmp) / (double)SwathHeight, 1) + 1);
		} else {
			*MaxNumSwath = 1;
		}
		MaxPartialSwath = (unsigned int)(math_max2(1, (unsigned int)(vp_start_rot + *VInitPreFill - 1) % SwathHeight));
	} else {
		*MaxNumSwath = (unsigned int)(math_ceil2((*VInitPreFill - 1.0) / (double)SwathHeight, 1) + 1);
		if (*VInitPreFill > 1) {
			MaxPartialSwath = (unsigned int)(math_max2(1, (unsigned int)(*VInitPreFill - 2) % SwathHeight));
		} else {
			MaxPartialSwath = (unsigned int)(math_max2(1, (unsigned int)(*VInitPreFill + SwathHeight - 2) % SwathHeight));
		}
	}
	numLines = *MaxNumSwath * SwathHeight + MaxPartialSwath;

	DML_LOG_VERBOSE("DML::%s: vp_start_rot = %u\n", __func__, vp_start_rot);
	DML_LOG_VERBOSE("DML::%s: VInitPreFill = %u\n", __func__, *VInitPreFill);
	DML_LOG_VERBOSE("DML::%s: MaxPartialSwath = %u\n", __func__, MaxPartialSwath);
	DML_LOG_VERBOSE("DML::%s: MaxNumSwath = %u\n", __func__, *MaxNumSwath);
	DML_LOG_VERBOSE("DML::%s: Prefetch source lines = %3.2f\n", __func__, numLines);
	return (unsigned int)(numLines);

}

static void dcn5_calculate_row_bandwidth(
		bool GPUVMEnable,
		bool use_one_row_for_frame,
		enum dml2_source_format_class SourcePixelFormat,
		double VRatio,
		double VRatioChroma,
		bool DCCEnable,
		double LineTime,
		unsigned int PixelPTEBytesPerRowLuma,
		unsigned int PixelPTEBytesPerRowChroma,
		unsigned int dpte_row_height_luma,
		unsigned int dpte_row_height_chroma,

		bool mrq_present,
		unsigned int meta_row_bytes_per_row_ub_l,
		unsigned int meta_row_bytes_per_row_ub_c,
		unsigned int meta_row_height_luma,
		unsigned int meta_row_height_chroma,

		// Output
		double *dpte_row_bw,
		double *meta_row_bw)
{
	if (!DCCEnable || !mrq_present) {
		*meta_row_bw = 0;
	} else if (dml2_core_utils_is_420(SourcePixelFormat) || dml2_core_utils_is_422_planar(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha) {
		*meta_row_bw = VRatio * meta_row_bytes_per_row_ub_l / (meta_row_height_luma * LineTime)
								+ VRatioChroma * meta_row_bytes_per_row_ub_c / (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * meta_row_bytes_per_row_ub_l / (meta_row_height_luma * LineTime);
	}

	if (GPUVMEnable != true || use_one_row_for_frame) {
		*dpte_row_bw = 0;
	} else if (dml2_core_utils_is_420(SourcePixelFormat) || dml2_core_utils_is_422_planar(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
							+ VRatioChroma * PixelPTEBytesPerRowChroma / (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}
}

unsigned int dcn5_calculate_vm_and_row_bytes(struct dml2_core_shared_calculate_vm_and_row_bytes_params *p)
{
	unsigned int extra_dpde_bytes;
	unsigned int extra_mpde_bytes;
	unsigned int MacroTileSizeBytes;
	unsigned int vp_height_dpte_ub;

	unsigned int meta_surface_bytes;
	unsigned int vm_bytes;
	unsigned int vp_height_meta_ub;
	unsigned int PixelPTEReqWidth_linear = 0; // VBA_DELTA. VBA doesn't calculate this

	*p->MetaRequestHeight = 8 * p->BlockHeight256Bytes;
	*p->MetaRequestWidth = 8 * p->BlockWidth256Bytes;
	if (dml2_core_utils_is_linear(p->SurfaceTiling)) {
		*p->meta_row_height = 32;
		*p->meta_row_width = (unsigned int)(math_floor2(p->ViewportXStart + p->SwathWidth + *p->MetaRequestWidth - 1, *p->MetaRequestWidth) - math_floor2(p->ViewportXStart, *p->MetaRequestWidth));
		*p->meta_row_bytes = (unsigned int)(*p->meta_row_width * *p->MetaRequestHeight * p->BytePerPixel / 256.0); // FIXME_DCN4SW missing in old code but no dcc for linear anyways?
	} else if (!dml2_core_utils_is_vertical_rotation(p->RotationAngle)) {
		*p->meta_row_height = *p->MetaRequestHeight;
		if (p->ViewportStationary && p->NumberOfDPPs == 1) {
			*p->meta_row_width = (unsigned int)(math_floor2(p->ViewportXStart + p->SwathWidth + *p->MetaRequestWidth - 1, *p->MetaRequestWidth) - math_floor2(p->ViewportXStart, *p->MetaRequestWidth));
		} else {
			*p->meta_row_width = (unsigned int)(math_ceil2(p->SwathWidth - 1, *p->MetaRequestWidth) + *p->MetaRequestWidth);
		}
		*p->meta_row_bytes = (unsigned int)(*p->meta_row_width * *p->MetaRequestHeight * p->BytePerPixel / 256.0);
	} else {
		*p->meta_row_height = *p->MetaRequestWidth;
		if (p->ViewportStationary && p->NumberOfDPPs == 1) {
			*p->meta_row_width = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + *p->MetaRequestHeight - 1, *p->MetaRequestHeight) - math_floor2(p->ViewportYStart, *p->MetaRequestHeight));
		} else {
			*p->meta_row_width = (unsigned int)(math_ceil2(p->SwathWidth - 1, *p->MetaRequestHeight) + *p->MetaRequestHeight);
		}
		*p->meta_row_bytes = (unsigned int)(*p->meta_row_width * *p->MetaRequestWidth * p->BytePerPixel / 256.0);
	}

	if (p->ViewportStationary && p->is_phantom && (p->NumberOfDPPs == 1 || !dml2_core_utils_is_vertical_rotation(p->RotationAngle))) {
		vp_height_meta_ub = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + 64 * p->BlockHeight256Bytes - 1, 64 * p->BlockHeight256Bytes) - math_floor2(p->ViewportYStart, 64 * p->BlockHeight256Bytes));
	} else if (!dml2_core_utils_is_vertical_rotation(p->RotationAngle)) {
		vp_height_meta_ub = (unsigned int)(math_ceil2(p->ViewportHeight - 1, 64 * p->BlockHeight256Bytes) + 64 * p->BlockHeight256Bytes);
	} else {
		vp_height_meta_ub = (unsigned int)(math_ceil2(p->SwathWidth - 1, 64 * p->BlockHeight256Bytes) + 64 * p->BlockHeight256Bytes);
	}

	meta_surface_bytes = (unsigned int)(p->DCCMetaPitch * vp_height_meta_ub * p->BytePerPixel / 256.0);
	DML_LOG_VERBOSE("DML::%s: DCCMetaPitch = %u\n", __func__, p->DCCMetaPitch);
	DML_LOG_VERBOSE("DML::%s: meta_surface_bytes = %u\n", __func__, meta_surface_bytes);
	if (p->GPUVMEnable == true) {
		double meta_vmpg_bytes = 4.0 * 1024.0;
		*p->meta_pte_bytes_per_frame_ub = (unsigned int)((math_ceil2((double) (meta_surface_bytes - meta_vmpg_bytes) / (8 * meta_vmpg_bytes), 1) + 1) * 64);
		extra_mpde_bytes = 128 * (p->GPUVMMaxPageTableLevels - 1);
	} else {
		*p->meta_pte_bytes_per_frame_ub = 0;
		extra_mpde_bytes = 0;
	}

	if (!p->DCCEnable || !p->mrq_present) {
		*p->meta_pte_bytes_per_frame_ub = 0;
		extra_mpde_bytes = 0;
		*p->meta_row_bytes = 0;
	}

	if (!p->GPUVMEnable) {
		*p->PixelPTEBytesPerRow = 0;
		*p->PixelPTEBytesPerRowStorage = 0;
		*p->dpte_row_width_ub = 0;
		*p->dpte_row_height = 0;
		*p->dpte_row_height_linear = 0;
		*p->PixelPTEBytesPerRow_one_row_per_frame = 0;
		*p->dpte_row_width_ub_one_row_per_frame = 0;
		*p->dpte_row_height_one_row_per_frame = 0;
		*p->vmpg_width = 0;
		*p->vmpg_height = 0;
		*p->PixelPTEReqWidth = 0;
		*p->PixelPTEReqHeight = 0;
		*p->PTERequestSize = 0;
		*p->dpde0_bytes_per_frame_ub = 0;
		return 0;
	}

	MacroTileSizeBytes = p->MacroTileWidth * p->BytePerPixel * p->MacroTileHeight;

	if (p->ViewportStationary && p->is_phantom && (p->NumberOfDPPs == 1 || !dml2_core_utils_is_vertical_rotation(p->RotationAngle))) {
		vp_height_dpte_ub = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + p->MacroTileHeight - 1, p->MacroTileHeight) - math_floor2(p->ViewportYStart, p->MacroTileHeight));
	} else if (!dml2_core_utils_is_vertical_rotation(p->RotationAngle)) {
		vp_height_dpte_ub = (unsigned int)(math_ceil2((double)p->ViewportHeight - 1, p->MacroTileHeight) + p->MacroTileHeight);
	} else {
		vp_height_dpte_ub = (unsigned int)(math_ceil2((double)p->SwathWidth - 1, p->MacroTileHeight) + p->MacroTileHeight);
	}

	unsigned int pixel_per_element = dml2_core_utils_is_422_packed(p->SourcePixelFormat) ? 2 : 1;
	if (p->GPUVMEnable == true && p->GPUVMMaxPageTableLevels > 1) {
		*p->dpde0_bytes_per_frame_ub = (unsigned int)(64 * (math_ceil2((double)(p->Pitch * pixel_per_element * vp_height_dpte_ub * p->BytePerPixel - MacroTileSizeBytes) / (double)(8 * 2097152), 1) + 1));
		extra_dpde_bytes = 128 * (p->GPUVMMaxPageTableLevels - 2);
	} else {
		*p->dpde0_bytes_per_frame_ub = 0;
		extra_dpde_bytes = 0;
	}

	vm_bytes = *p->meta_pte_bytes_per_frame_ub + extra_mpde_bytes + *p->dpde0_bytes_per_frame_ub + extra_dpde_bytes;

	DML_LOG_VERBOSE("DML::%s: DCCEnable = %u\n", __func__, p->DCCEnable);
	DML_LOG_VERBOSE("DML::%s: GPUVMEnable = %u\n", __func__, p->GPUVMEnable);
	DML_LOG_VERBOSE("DML::%s: SwModeLinear = %u\n", __func__, p->SurfaceTiling == dml2_sw_linear);
	DML_LOG_VERBOSE("DML::%s: BytePerPixel = %u\n", __func__, p->BytePerPixel);
	DML_LOG_VERBOSE("DML::%s: GPUVMMaxPageTableLevels = %u\n", __func__, p->GPUVMMaxPageTableLevels);
	DML_LOG_VERBOSE("DML::%s: BlockHeight256Bytes = %u\n", __func__, p->BlockHeight256Bytes);
	DML_LOG_VERBOSE("DML::%s: BlockWidth256Bytes = %u\n", __func__, p->BlockWidth256Bytes);
	DML_LOG_VERBOSE("DML::%s: MacroTileHeight = %u\n", __func__, p->MacroTileHeight);
	DML_LOG_VERBOSE("DML::%s: MacroTileWidth = %u\n", __func__, p->MacroTileWidth);
	DML_LOG_VERBOSE("DML::%s: meta_pte_bytes_per_frame_ub = %u\n", __func__, *p->meta_pte_bytes_per_frame_ub);
	DML_LOG_VERBOSE("DML::%s: dpde0_bytes_per_frame_ub = %u\n", __func__, *p->dpde0_bytes_per_frame_ub);
	DML_LOG_VERBOSE("DML::%s: extra_mpde_bytes = %u\n", __func__, extra_mpde_bytes);
	DML_LOG_VERBOSE("DML::%s: extra_dpde_bytes = %u\n", __func__, extra_dpde_bytes);
	DML_LOG_VERBOSE("DML::%s: vm_bytes = %u\n", __func__, vm_bytes);
	DML_LOG_VERBOSE("DML::%s: ViewportHeight = %u\n", __func__, p->ViewportHeight);
	DML_LOG_VERBOSE("DML::%s: SwathWidth = %u\n", __func__, p->SwathWidth);
	DML_LOG_VERBOSE("DML::%s: vp_height_dpte_ub = %u\n", __func__, vp_height_dpte_ub);

	if (dml2_core_utils_is_linear(p->SurfaceTiling)) {
		*p->PixelPTEReqHeight = 1;
		*p->PixelPTEReqWidth = p->GPUVMMinPageSizeKBytes * 1024 * 8 / p->BytePerPixel;
		PixelPTEReqWidth_linear = p->GPUVMMinPageSizeKBytes * 1024 * 8 / p->BytePerPixel;
		*p->PTERequestSize = 64;

		*p->vmpg_height = 1;
		*p->vmpg_width = p->GPUVMMinPageSizeKBytes * 1024 / p->BytePerPixel;
	} else if (p->GPUVMMinPageSizeKBytes * 1024 >= dml2_core_utils_get_tile_block_size_bytes(p->SurfaceTiling, p->BytePerPixel)) { // 1 64B 8x1 PTE
		*p->PixelPTEReqHeight = p->MacroTileHeight;
		*p->PixelPTEReqWidth = 8 * 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);
		*p->PTERequestSize = 64;

		*p->vmpg_height = p->MacroTileHeight;
		*p->vmpg_width = 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);

	} else if (p->GPUVMMinPageSizeKBytes == 4 && dml2_core_utils_get_tile_block_size_bytes(p->SurfaceTiling, p->BytePerPixel) == 65536) { // 2 64B PTE requests to get 16 PTEs to cover the 64K tile
		// one 64KB tile, is 16x16x256B req
		*p->PixelPTEReqHeight = 16 * p->BlockHeight256Bytes;
		*p->PixelPTEReqWidth = 16 * p->BlockWidth256Bytes;
		*p->PTERequestSize = 128;

		*p->vmpg_height = *p->PixelPTEReqHeight;
		*p->vmpg_width = *p->PixelPTEReqWidth;
	} else {
		// default for rest of calculation to go through, when vm is disable, the calulated pte related values shouldnt be used anyways
		*p->PixelPTEReqHeight = p->MacroTileHeight;
		*p->PixelPTEReqWidth = 8 * 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);
		*p->PTERequestSize = 64;

		*p->vmpg_height = p->MacroTileHeight;
		*p->vmpg_width = 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);

		if (p->GPUVMEnable == true) {
			DML_LOG_VERBOSE("DML::%s: GPUVMMinPageSizeKBytes=%u and sw_mode=%u (tile_size=%d) not supported!\n",
					__func__, p->GPUVMMinPageSizeKBytes, p->SurfaceTiling, dml2_core_utils_get_tile_block_size_bytes(p->SurfaceTiling, p->BytePerPixel));
			DML_ASSERT(0);
		}
	}

	DML_LOG_VERBOSE("DML::%s: GPUVMMinPageSizeKBytes = %u\n", __func__, p->GPUVMMinPageSizeKBytes);
	DML_LOG_VERBOSE("DML::%s: PixelPTEReqHeight = %u\n", __func__, *p->PixelPTEReqHeight);
	DML_LOG_VERBOSE("DML::%s: PixelPTEReqWidth = %u\n", __func__, *p->PixelPTEReqWidth);
	DML_LOG_VERBOSE("DML::%s: PixelPTEReqWidth_linear = %u\n", __func__, PixelPTEReqWidth_linear);
	DML_LOG_VERBOSE("DML::%s: PTERequestSize = %u\n", __func__, *p->PTERequestSize);
	DML_LOG_VERBOSE("DML::%s: Pitch = %u\n", __func__, p->Pitch);
	DML_LOG_VERBOSE("DML::%s: vmpg_width = %u\n", __func__, *p->vmpg_width);
	DML_LOG_VERBOSE("DML::%s: vmpg_height = %u\n", __func__, *p->vmpg_height);

	*p->dpte_row_height_one_row_per_frame = vp_height_dpte_ub;
	*p->dpte_row_width_ub_one_row_per_frame = (unsigned int)((math_ceil2(((double)p->Pitch * pixel_per_element * (double)*p->dpte_row_height_one_row_per_frame / (double)*p->PixelPTEReqHeight - 1) / (double)*p->PixelPTEReqWidth, 1) + 1) * (double)*p->PixelPTEReqWidth);
	*p->PixelPTEBytesPerRow_one_row_per_frame = (unsigned int)((double)*p->dpte_row_width_ub_one_row_per_frame / (double)*p->PixelPTEReqWidth * *p->PTERequestSize);
	*p->dpte_row_height_linear = 0;

	if (dml2_core_utils_is_linear(p->SurfaceTiling)) {
		*p->dpte_row_height = (unsigned int)(math_min2(128, (double)(1ULL << (unsigned int)math_floor2(math_log((float)(p->PTEBufferSizeInRequests * *p->PixelPTEReqWidth / pixel_per_element / p->Pitch), 2.0), 1))));
		*p->dpte_row_width_ub = (unsigned int)(math_ceil2(((double)p->Pitch * pixel_per_element * (double)*p->dpte_row_height - 1), (double)*p->PixelPTEReqWidth) + *p->PixelPTEReqWidth);
		*p->PixelPTEBytesPerRow = (unsigned int)((double)*p->dpte_row_width_ub / (double)*p->PixelPTEReqWidth * *p->PTERequestSize);

		// VBA_DELTA, VBA doesn't have programming value for pte row height linear.
		*p->dpte_row_height_linear = (unsigned int)1 << (unsigned int)math_floor2(math_log((float)(p->PTEBufferSizeInRequests * PixelPTEReqWidth_linear / pixel_per_element / p->Pitch), 2.0), 1);
		if (*p->dpte_row_height_linear > 128)
			*p->dpte_row_height_linear = 128;

		DML_LOG_VERBOSE("DML::%s: dpte_row_width_ub = %u (linear)\n", __func__, *p->dpte_row_width_ub);

	} else if (!dml2_core_utils_is_vertical_rotation(p->RotationAngle)) {
		*p->dpte_row_height = *p->PixelPTEReqHeight;

		if (p->GPUVMMinPageSizeKBytes > 64) {
			*p->dpte_row_width_ub = (unsigned int)((math_ceil2(((double)p->Pitch * pixel_per_element * (double)*p->dpte_row_height / (double)*p->PixelPTEReqHeight - 1) / (double)*p->PixelPTEReqWidth, 1) + 1) * *p->PixelPTEReqWidth);
		} else if (p->ViewportStationary && (p->NumberOfDPPs == 1)) {
			*p->dpte_row_width_ub = (unsigned int)(math_floor2(p->ViewportXStart + p->SwathWidth + *p->PixelPTEReqWidth - 1, *p->PixelPTEReqWidth) - math_floor2(p->ViewportXStart, *p->PixelPTEReqWidth));
		} else {
			*p->dpte_row_width_ub = (unsigned int)((math_ceil2((double)(p->SwathWidth - 1) / (double)*p->PixelPTEReqWidth, 1) + 1.0) * *p->PixelPTEReqWidth);
		}
		DML_LOG_VERBOSE("DML::%s: dpte_row_width_ub = %u (tiled horz)\n", __func__, *p->dpte_row_width_ub);
		*p->PixelPTEBytesPerRow = *p->dpte_row_width_ub / *p->PixelPTEReqWidth * *p->PTERequestSize;
	} else {
		*p->dpte_row_height = (unsigned int)(math_min2(*p->PixelPTEReqWidth, p->MacroTileWidth));

		if (p->ViewportStationary && (p->NumberOfDPPs == 1)) {
			*p->dpte_row_width_ub = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + *p->PixelPTEReqHeight - 1, *p->PixelPTEReqHeight) - math_floor2(p->ViewportYStart, *p->PixelPTEReqHeight));
		} else {
			*p->dpte_row_width_ub = (unsigned int)((math_ceil2((double)(p->SwathWidth - 1) / (double)*p->PixelPTEReqHeight, 1) + 1) * *p->PixelPTEReqHeight);
		}

		*p->PixelPTEBytesPerRow = (unsigned int)((double)*p->dpte_row_width_ub / (double)*p->PixelPTEReqHeight * *p->PTERequestSize);
		DML_LOG_VERBOSE("DML::%s: dpte_row_width_ub = %u (tiled vert)\n", __func__, *p->dpte_row_width_ub);
	}

	if (p->GPUVMEnable != true) {
		*p->PixelPTEBytesPerRow = 0;
		*p->PixelPTEBytesPerRow_one_row_per_frame = 0;
	}

	*p->PixelPTEBytesPerRowStorage = *p->PixelPTEBytesPerRow;

	DML_LOG_VERBOSE("DML::%s: GPUVMMinPageSizeKBytes = %u\n", __func__, p->GPUVMMinPageSizeKBytes);
	DML_LOG_VERBOSE("DML::%s: GPUVMEnable = %u\n", __func__, p->GPUVMEnable);
	DML_LOG_VERBOSE("DML::%s: meta_row_height = %u\n", __func__, *p->meta_row_height);
	DML_LOG_VERBOSE("DML::%s: dpte_row_height = %u\n", __func__, *p->dpte_row_height);
	DML_LOG_VERBOSE("DML::%s: dpte_row_height_linear = %u\n", __func__, *p->dpte_row_height_linear);
	DML_LOG_VERBOSE("DML::%s: dpte_row_width_ub = %u\n", __func__, *p->dpte_row_width_ub);
	DML_LOG_VERBOSE("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, *p->PixelPTEBytesPerRow);
	DML_LOG_VERBOSE("DML::%s: PixelPTEBytesPerRowStorage = %u\n", __func__, *p->PixelPTEBytesPerRowStorage);
	DML_LOG_VERBOSE("DML::%s: PTEBufferSizeInRequests = %u\n", __func__, p->PTEBufferSizeInRequests);
	DML_LOG_VERBOSE("DML::%s: dpte_row_height_one_row_per_frame = %u\n", __func__, *p->dpte_row_height_one_row_per_frame);
	DML_LOG_VERBOSE("DML::%s: dpte_row_width_ub_one_row_per_frame = %u\n", __func__, *p->dpte_row_width_ub_one_row_per_frame);
	DML_LOG_VERBOSE("DML::%s: PixelPTEBytesPerRow_one_row_per_frame = %u\n", __func__, *p->PixelPTEBytesPerRow_one_row_per_frame);

	return vm_bytes;
}

void dcn5_calculate_vm_row_and_swath(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateVMRowAndSwath_params *p)
{
	struct dml2_core_calcs_CalculateVMRowAndSwath_locals *s = &scratch->CalculateVMRowAndSwath_locals;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		s->HostVMDynamicLevels = dcn5_calculate_host_vm_dynamic_levels(p->display_cfg->gpuvm_enable, p->display_cfg->hostvm_enable, p->display_cfg->plane_descriptors[k].overrides.hostvm_min_page_size_kbytes,
			p->display_cfg->hostvm_max_non_cached_page_table_levels);

		if (p->display_cfg->gpuvm_enable == true) {
			p->vm_group_bytes[k] = 512;
			p->dpte_group_bytes[k] = 512;
		} else {
			p->vm_group_bytes[k] = 0;
			p->dpte_group_bytes[k] = 0;
		}

		if (dml2_core_utils_is_420(p->myPipe[k].SourcePixelFormat) || dml2_core_utils_is_422_planar(p->myPipe[k].SourcePixelFormat) || p->myPipe[k].SourcePixelFormat == dml2_rgbe_alpha) {
			if ((p->myPipe[k].SourcePixelFormat == dml2_420_10 || p->myPipe[k].SourcePixelFormat == dml2_420_12
				|| p->myPipe[k].SourcePixelFormat == dml2_422_planar_10 || p->myPipe[k].SourcePixelFormat == dml2_422_planar_12)
				&& !dml2_core_utils_is_vertical_rotation(p->myPipe[k].RotationAngle)) {
				s->PTEBufferSizeInRequestsForLuma[k] = (p->PTEBufferSizeInRequestsLuma + p->PTEBufferSizeInRequestsChroma) / 2;
				s->PTEBufferSizeInRequestsForChroma[k] = s->PTEBufferSizeInRequestsForLuma[k];
			} else {
				s->PTEBufferSizeInRequestsForLuma[k] = p->PTEBufferSizeInRequestsLuma;
				s->PTEBufferSizeInRequestsForChroma[k] = p->PTEBufferSizeInRequestsChroma;
			}

			scratch->calculate_vm_and_row_bytes_params.ViewportStationary = p->myPipe[k].ViewportStationary;
			scratch->calculate_vm_and_row_bytes_params.DCCEnable = p->myPipe[k].DCCEnable;
			scratch->calculate_vm_and_row_bytes_params.NumberOfDPPs = p->myPipe[k].DPPPerSurface;
			scratch->calculate_vm_and_row_bytes_params.BlockHeight256Bytes = p->myPipe[k].BlockHeight256BytesC;
			scratch->calculate_vm_and_row_bytes_params.BlockWidth256Bytes = p->myPipe[k].BlockWidth256BytesC;
			scratch->calculate_vm_and_row_bytes_params.SourcePixelFormat = p->myPipe[k].SourcePixelFormat;
			scratch->calculate_vm_and_row_bytes_params.SurfaceTiling = p->myPipe[k].SurfaceTiling;
			scratch->calculate_vm_and_row_bytes_params.BytePerPixel = p->myPipe[k].BytePerPixelC;
			scratch->calculate_vm_and_row_bytes_params.RotationAngle = p->myPipe[k].RotationAngle;
			scratch->calculate_vm_and_row_bytes_params.SwathWidth = p->SwathWidthC[k];
			scratch->calculate_vm_and_row_bytes_params.ViewportHeight = p->myPipe[k].ViewportHeightC;
			scratch->calculate_vm_and_row_bytes_params.ViewportXStart = p->myPipe[k].ViewportXStartC;
			scratch->calculate_vm_and_row_bytes_params.ViewportYStart = p->myPipe[k].ViewportYStartC;
			scratch->calculate_vm_and_row_bytes_params.GPUVMEnable = p->display_cfg->gpuvm_enable;
			scratch->calculate_vm_and_row_bytes_params.GPUVMMaxPageTableLevels = p->display_cfg->gpuvm_max_page_table_levels;
			scratch->calculate_vm_and_row_bytes_params.GPUVMMinPageSizeKBytes = p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;
			scratch->calculate_vm_and_row_bytes_params.PTEBufferSizeInRequests = s->PTEBufferSizeInRequestsForChroma[k];
			scratch->calculate_vm_and_row_bytes_params.Pitch = p->myPipe[k].PitchC;
			scratch->calculate_vm_and_row_bytes_params.MacroTileWidth = p->myPipe[k].BlockWidthC;
			scratch->calculate_vm_and_row_bytes_params.MacroTileHeight = p->myPipe[k].BlockHeightC;
			scratch->calculate_vm_and_row_bytes_params.DCCMetaPitch = p->myPipe[k].DCCMetaPitchC;
			scratch->calculate_vm_and_row_bytes_params.mrq_present = p->mrq_present;

			scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow = &s->PixelPTEBytesPerRowC[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRowStorage = &s->PixelPTEBytesPerRowStorageC[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub = &p->dpte_row_width_chroma_ub[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_height = &p->dpte_row_height_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_height_linear = &p->dpte_row_height_linear_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow_one_row_per_frame = &s->PixelPTEBytesPerRowC_one_row_per_frame[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub_one_row_per_frame = &s->dpte_row_width_chroma_ub_one_row_per_frame[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_height_one_row_per_frame = &s->dpte_row_height_chroma_one_row_per_frame[k];
			scratch->calculate_vm_and_row_bytes_params.vmpg_width = &p->vmpg_width_c[k];
			scratch->calculate_vm_and_row_bytes_params.vmpg_height = &p->vmpg_height_c[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEReqWidth = &p->PixelPTEReqWidthC[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEReqHeight = &p->PixelPTEReqHeightC[k];
			scratch->calculate_vm_and_row_bytes_params.PTERequestSize = &p->PTERequestSizeC[k];
			scratch->calculate_vm_and_row_bytes_params.dpde0_bytes_per_frame_ub = &p->dpde0_bytes_per_frame_ub_c[k];

			scratch->calculate_vm_and_row_bytes_params.meta_row_bytes = &s->meta_row_bytes_per_row_ub_c[k];
			scratch->calculate_vm_and_row_bytes_params.MetaRequestWidth = &p->meta_req_width_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.MetaRequestHeight = &p->meta_req_height_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.meta_row_width = &p->meta_row_width_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.meta_row_height = &p->meta_row_height_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.meta_pte_bytes_per_frame_ub = &p->meta_pte_bytes_per_frame_ub_c[k];

			s->vm_bytes_c = dcn5_calculate_vm_and_row_bytes(&scratch->calculate_vm_and_row_bytes_params);

			p->PrefetchSourceLinesC[k] = dcn5_calculate_prefetch_source_lines(
					p->myPipe[k].VRatioChroma,
					p->myPipe[k].VTapsChroma,
					p->myPipe[k].UPSPEnabled,
					p->myPipe[k].UPSPVTaps,
					p->myPipe[k].UPSPSamplePositioning,
					dml2_core_utils_is_420(p->myPipe[k].SourcePixelFormat),
					p->myPipe[k].InterlaceEnable,
					p->myPipe[k].ProgressiveToInterlaceUnitInOPP,
					p->myPipe[k].SwathHeightC,
					p->myPipe[k].RotationAngle,
					p->myPipe[k].mirrored,
					p->myPipe[k].ViewportStationary,
					p->SwathWidthC[k],
					p->myPipe[k].ViewportHeightC,
					p->myPipe[k].ViewportXStartC,
					p->myPipe[k].ViewportYStartC,

					// Output
					&p->VInitPreFillC[k],
					&p->MaxNumSwathC[k]);
		} else {
			s->PTEBufferSizeInRequestsForLuma[k] = p->PTEBufferSizeInRequestsLuma + p->PTEBufferSizeInRequestsChroma;
			s->PTEBufferSizeInRequestsForChroma[k] = 0;
			s->PixelPTEBytesPerRowC[k] = 0;
			s->PixelPTEBytesPerRowStorageC[k] = 0;
			s->vm_bytes_c = 0;
			p->MaxNumSwathC[k] = 0;
			p->PrefetchSourceLinesC[k] = 0;
			s->dpte_row_height_chroma_one_row_per_frame[k] = 0;
			s->dpte_row_width_chroma_ub_one_row_per_frame[k] = 0;
			s->PixelPTEBytesPerRowC_one_row_per_frame[k] = 0;
		}

		scratch->calculate_vm_and_row_bytes_params.ViewportStationary = p->myPipe[k].ViewportStationary;
		scratch->calculate_vm_and_row_bytes_params.DCCEnable = p->myPipe[k].DCCEnable;
		scratch->calculate_vm_and_row_bytes_params.NumberOfDPPs = p->myPipe[k].DPPPerSurface;
		scratch->calculate_vm_and_row_bytes_params.BlockHeight256Bytes = p->myPipe[k].BlockHeight256BytesY;
		scratch->calculate_vm_and_row_bytes_params.BlockWidth256Bytes = p->myPipe[k].BlockWidth256BytesY;
		scratch->calculate_vm_and_row_bytes_params.SourcePixelFormat = p->myPipe[k].SourcePixelFormat;
		scratch->calculate_vm_and_row_bytes_params.SurfaceTiling = p->myPipe[k].SurfaceTiling;
		scratch->calculate_vm_and_row_bytes_params.BytePerPixel = p->myPipe[k].BytePerPixelY;
		scratch->calculate_vm_and_row_bytes_params.RotationAngle = p->myPipe[k].RotationAngle;
		scratch->calculate_vm_and_row_bytes_params.SwathWidth = p->SwathWidthY[k];
		scratch->calculate_vm_and_row_bytes_params.ViewportHeight = p->myPipe[k].ViewportHeight;
		scratch->calculate_vm_and_row_bytes_params.ViewportXStart = p->myPipe[k].ViewportXStart;
		scratch->calculate_vm_and_row_bytes_params.ViewportYStart = p->myPipe[k].ViewportYStart;
		scratch->calculate_vm_and_row_bytes_params.GPUVMEnable = p->display_cfg->gpuvm_enable;
		scratch->calculate_vm_and_row_bytes_params.GPUVMMaxPageTableLevels = p->display_cfg->gpuvm_max_page_table_levels;
		scratch->calculate_vm_and_row_bytes_params.GPUVMMinPageSizeKBytes = p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;
		scratch->calculate_vm_and_row_bytes_params.PTEBufferSizeInRequests = s->PTEBufferSizeInRequestsForLuma[k];
		scratch->calculate_vm_and_row_bytes_params.Pitch = p->myPipe[k].PitchY;
		scratch->calculate_vm_and_row_bytes_params.MacroTileWidth = p->myPipe[k].BlockWidthY;
		scratch->calculate_vm_and_row_bytes_params.MacroTileHeight = p->myPipe[k].BlockHeightY;
		scratch->calculate_vm_and_row_bytes_params.DCCMetaPitch = p->myPipe[k].DCCMetaPitchY;
		scratch->calculate_vm_and_row_bytes_params.mrq_present = p->mrq_present;

		scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow = &s->PixelPTEBytesPerRowY[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRowStorage = &s->PixelPTEBytesPerRowStorageY[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub = &p->dpte_row_width_luma_ub[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_height = &p->dpte_row_height_luma[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_height_linear = &p->dpte_row_height_linear_luma[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow_one_row_per_frame = &s->PixelPTEBytesPerRowY_one_row_per_frame[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub_one_row_per_frame = &s->dpte_row_width_luma_ub_one_row_per_frame[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_height_one_row_per_frame = &s->dpte_row_height_luma_one_row_per_frame[k];
		scratch->calculate_vm_and_row_bytes_params.vmpg_width = &p->vmpg_width_y[k];
		scratch->calculate_vm_and_row_bytes_params.vmpg_height = &p->vmpg_height_y[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEReqWidth = &p->PixelPTEReqWidthY[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEReqHeight = &p->PixelPTEReqHeightY[k];
		scratch->calculate_vm_and_row_bytes_params.PTERequestSize = &p->PTERequestSizeY[k];
		scratch->calculate_vm_and_row_bytes_params.dpde0_bytes_per_frame_ub = &p->dpde0_bytes_per_frame_ub_l[k];

		scratch->calculate_vm_and_row_bytes_params.meta_row_bytes = &s->meta_row_bytes_per_row_ub_l[k];
		scratch->calculate_vm_and_row_bytes_params.MetaRequestWidth = &p->meta_req_width_luma[k];
		scratch->calculate_vm_and_row_bytes_params.MetaRequestHeight = &p->meta_req_height_luma[k];
		scratch->calculate_vm_and_row_bytes_params.meta_row_width = &p->meta_row_width_luma[k];
		scratch->calculate_vm_and_row_bytes_params.meta_row_height = &p->meta_row_height_luma[k];
		scratch->calculate_vm_and_row_bytes_params.meta_pte_bytes_per_frame_ub = &p->meta_pte_bytes_per_frame_ub_l[k];

		s->vm_bytes_l = dcn5_calculate_vm_and_row_bytes(&scratch->calculate_vm_and_row_bytes_params);

		p->PrefetchSourceLinesY[k] = dcn5_calculate_prefetch_source_lines(
				p->myPipe[k].VRatio,
				p->myPipe[k].VTaps,
				0, //No upsampler in Luma
				p->myPipe[k].UPSPVTaps,
				p->myPipe[k].UPSPSamplePositioning,
				dml2_core_utils_is_420(p->myPipe[k].SourcePixelFormat),
				p->myPipe[k].InterlaceEnable,
				p->myPipe[k].ProgressiveToInterlaceUnitInOPP,
				p->myPipe[k].SwathHeightY,
				p->myPipe[k].RotationAngle,
				p->myPipe[k].mirrored,
				p->myPipe[k].ViewportStationary,
				p->SwathWidthY[k],
				p->myPipe[k].ViewportHeight,
				p->myPipe[k].ViewportXStart,
				p->myPipe[k].ViewportYStart,

				// Output
				&p->VInitPreFillY[k],
				&p->MaxNumSwathY[k]);

		DML_LOG_VERBOSE("DML::%s: k=%u, vm_bytes_l = %u (before hvm level)\n", __func__, k, s->vm_bytes_l);
		DML_LOG_VERBOSE("DML::%s: k=%u, vm_bytes_c = %u (before hvm level)\n", __func__, k, s->vm_bytes_c);
		DML_LOG_VERBOSE("DML::%s: k=%u, meta_row_bytes_per_row_ub_l = %u\n", __func__, k, s->meta_row_bytes_per_row_ub_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, meta_row_bytes_per_row_ub_c = %u\n", __func__, k, s->meta_row_bytes_per_row_ub_c[k]);
		p->vm_bytes[k] = (s->vm_bytes_l + s->vm_bytes_c) * (1 + 8 * s->HostVMDynamicLevels);
		p->meta_row_bytes[k] = s->meta_row_bytes_per_row_ub_l[k] + s->meta_row_bytes_per_row_ub_c[k];
		p->meta_row_bytes_per_row_ub_l[k] = s->meta_row_bytes_per_row_ub_l[k];
		p->meta_row_bytes_per_row_ub_c[k] = s->meta_row_bytes_per_row_ub_c[k];

		DML_LOG_VERBOSE("DML::%s: k=%u, meta_row_bytes = %u\n", __func__, k, p->meta_row_bytes[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, vm_bytes = %u (after hvm level)\n", __func__, k, p->vm_bytes[k]);
		if (s->PixelPTEBytesPerRowStorageY[k] <= 64 * s->PTEBufferSizeInRequestsForLuma[k] && s->PixelPTEBytesPerRowStorageC[k] <= 64 * s->PTEBufferSizeInRequestsForChroma[k]) {
			p->PTEBufferSizeNotExceeded[k] = true;
		} else {
			p->PTEBufferSizeNotExceeded[k] = false;
		}

		s->one_row_per_frame_fits_in_buffer[k] = (s->PixelPTEBytesPerRowY_one_row_per_frame[k] <= 64 * 2 * s->PTEBufferSizeInRequestsForLuma[k] &&
				s->PixelPTEBytesPerRowC_one_row_per_frame[k] <= 64 * 2 * s->PTEBufferSizeInRequestsForChroma[k]);
		if (p->PTEBufferSizeNotExceeded[k] == 0 || s->one_row_per_frame_fits_in_buffer[k] == 0) {
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowY = %u (before hvm level)\n", __func__, k, s->PixelPTEBytesPerRowY[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowC = %u (before hvm level)\n", __func__, k, s->PixelPTEBytesPerRowC[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowStorageY = %u\n", __func__, k, s->PixelPTEBytesPerRowStorageY[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowStorageC = %u\n", __func__, k, s->PixelPTEBytesPerRowStorageC[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PTEBufferSizeInRequestsForLuma = %u\n", __func__, k, s->PTEBufferSizeInRequestsForLuma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PTEBufferSizeInRequestsForChroma = %u\n", __func__, k, s->PTEBufferSizeInRequestsForChroma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PTEBufferSizeNotExceeded (not one_row_per_frame) = %u\n", __func__, k, p->PTEBufferSizeNotExceeded[k]);

			DML_LOG_VERBOSE("DML::%s: k=%u, HostVMDynamicLevels = %u\n", __func__, k, s->HostVMDynamicLevels);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowY_one_row_per_frame = %u\n", __func__, k, s->PixelPTEBytesPerRowY_one_row_per_frame[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowC_one_row_per_frame = %u\n", __func__, k, s->PixelPTEBytesPerRowC_one_row_per_frame[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, one_row_per_frame_fits_in_buffer = %u\n", __func__, k, s->one_row_per_frame_fits_in_buffer[k]);
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->gpuvm_enable) {
			DML_LOG_VERBOSE("DML::%s: k=%u, force_pte_buffer_mode.enable = %u\n", __func__, k, p->display_cfg->plane_descriptors[k].overrides.hw.force_pte_buffer_mode.enable);
			DML_LOG_VERBOSE("DML::%s: k=%u, force_pte_buffer_mode.value = %u\n", __func__, k, p->display_cfg->plane_descriptors[k].overrides.hw.force_pte_buffer_mode.value);
			DML_LOG_VERBOSE("DML::%s: k=%u, gpuvm_min_page_size_kbytes = %u\n", __func__, k, p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes);
			DML_LOG_VERBOSE("DML::%s: k=%u, uclk_pstate_switch_modes = %u\n", __func__, k, p->uclk_pstate_switch_modes[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, FORCE_ONE_ROW_FOR_FRAME = %u\n", __func__, k, p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME);

			if (p->display_cfg->plane_descriptors[k].overrides.hw.force_pte_buffer_mode.enable == 1) {
				p->PTE_BUFFER_MODE[k] = p->display_cfg->plane_descriptors[k].overrides.hw.force_pte_buffer_mode.value;
			} else {
				p->PTE_BUFFER_MODE[k] = p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME
					|| (p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes > 64)
					|| (p->uclk_pstate_switch_modes[k] == dml2_pstate_method_alternate);
				p->BIGK_FRAGMENT_SIZE[k] = (unsigned int)(math_log((float)p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes * 1024, 2) - 12);
			}
		} else {
			p->PTE_BUFFER_MODE[k] = 0;
			p->BIGK_FRAGMENT_SIZE[k] = 0;
		}
		DML_LOG_VERBOSE("DML::%s: k=%u, PTE_BUFFER_MODE = %u\n", __func__, k, p->PTE_BUFFER_MODE[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, BIGK_FRAGMENT_SIZE = %u\n", __func__, k, p->BIGK_FRAGMENT_SIZE[k]);
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		s->HostVMDynamicLevels = dcn5_calculate_host_vm_dynamic_levels(p->display_cfg->gpuvm_enable, p->display_cfg->hostvm_enable, p->display_cfg->plane_descriptors[k].overrides.hostvm_min_page_size_kbytes,
			p->display_cfg->hostvm_max_non_cached_page_table_levels);

		p->DCCMetaBufferSizeNotExceeded[k] = true;
		if (p->display_cfg->gpuvm_enable) {
		    p->use_one_row_for_frame[k] = p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME
					|| (p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes > 64 && dml2_core_utils_is_vertical_rotation(p->myPipe[k].RotationAngle))
					|| (p->uclk_pstate_switch_modes[k] == dml2_pstate_method_alternate);
		}

		p->use_one_row_for_frame_flip[k] = p->use_one_row_for_frame[k];

		if (p->use_one_row_for_frame[k]) {
			p->dpte_row_height_luma[k] = s->dpte_row_height_luma_one_row_per_frame[k];
			p->dpte_row_width_luma_ub[k] = s->dpte_row_width_luma_ub_one_row_per_frame[k];
			s->PixelPTEBytesPerRowY[k] = s->PixelPTEBytesPerRowY_one_row_per_frame[k];
			p->dpte_row_height_chroma[k] = s->dpte_row_height_chroma_one_row_per_frame[k];
			p->dpte_row_width_chroma_ub[k] = s->dpte_row_width_chroma_ub_one_row_per_frame[k];
			s->PixelPTEBytesPerRowC[k] = s->PixelPTEBytesPerRowC_one_row_per_frame[k];
			p->PTEBufferSizeNotExceeded[k] = s->one_row_per_frame_fits_in_buffer[k];
		}

		if (p->meta_row_bytes[k] <= p->DCCMetaBufferSizeBytes) {
			p->DCCMetaBufferSizeNotExceeded[k] = true;
		} else {
			p->DCCMetaBufferSizeNotExceeded[k] = false;
			DML_LOG_VERBOSE("DML::%s: k=%d, meta_row_bytes = %d\n",  __func__, k, p->meta_row_bytes[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, DCCMetaBufferSizeBytes = %d\n",  __func__, k, p->DCCMetaBufferSizeBytes);
			DML_LOG_VERBOSE("DML::%s: k=%d, DCCMetaBufferSizeNotExceeded = %d\n",  __func__, k, p->DCCMetaBufferSizeNotExceeded[k]);
		}

		s->PixelPTEBytesPerRowY[k] = s->PixelPTEBytesPerRowY[k] * (1 + 8 * s->HostVMDynamicLevels);
		s->PixelPTEBytesPerRowC[k] = s->PixelPTEBytesPerRowC[k] * (1 + 8 * s->HostVMDynamicLevels);
		p->PixelPTEBytesPerRow[k] = s->PixelPTEBytesPerRowY[k] + s->PixelPTEBytesPerRowC[k];
		p->dpte_row_bytes_per_row_l[k] = s->PixelPTEBytesPerRowY[k];
		p->dpte_row_bytes_per_row_c[k] = s->PixelPTEBytesPerRowC[k];

		// if one row of dPTEs is meant to span the entire frame, then for these calculations, we will pretend like that one big row is fetched in two halfs
		if (p->use_one_row_for_frame[k])
			p->PixelPTEBytesPerRow[k] = p->PixelPTEBytesPerRow[k] / 2;

		dcn5_calculate_row_bandwidth(
				p->display_cfg->gpuvm_enable,
				p->use_one_row_for_frame[k],
				p->myPipe[k].SourcePixelFormat,
				p->myPipe[k].VRatio,
				p->myPipe[k].VRatioChroma,
				p->myPipe[k].DCCEnable,
				p->myPipe[k].HTotal / p->myPipe[k].PixelClock,
				s->PixelPTEBytesPerRowY[k],
				s->PixelPTEBytesPerRowC[k],
				p->dpte_row_height_luma[k],
				p->dpte_row_height_chroma[k],

				p->mrq_present,
				p->meta_row_bytes_per_row_ub_l[k],
				p->meta_row_bytes_per_row_ub_c[k],
				p->meta_row_height_luma[k],
				p->meta_row_height_chroma[k],

				// Output
				&p->dpte_row_bw[k],
				&p->meta_row_bw[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, use_one_row_for_frame = %u\n", __func__, k, p->use_one_row_for_frame[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, use_one_row_for_frame_flip = %u\n", __func__, k, p->use_one_row_for_frame_flip[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dpte_row_height_luma = %u\n", __func__, k, p->dpte_row_height_luma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dpte_row_width_luma_ub = %u\n", __func__, k, p->dpte_row_width_luma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowY = %u (after hvm level)\n", __func__, k, s->PixelPTEBytesPerRowY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dpte_row_height_chroma = %u\n", __func__, k, p->dpte_row_height_chroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dpte_row_width_chroma_ub = %u\n", __func__, k, p->dpte_row_width_chroma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRowC = %u (after hvm level)\n", __func__, k, s->PixelPTEBytesPerRowC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEBytesPerRow = %u\n", __func__, k, p->PixelPTEBytesPerRow[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, PTEBufferSizeNotExceeded = %u\n", __func__, k, p->PTEBufferSizeNotExceeded[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, gpuvm_enable = %u\n", __func__, k, p->display_cfg->gpuvm_enable);
	}
}

void dcn5_calculate_bytes_to_fetch_required_to_hide_latency(
		struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *p)
{
	unsigned int dst_lines_to_hide;
	unsigned int src_lines_to_hide_l;
	unsigned int src_lines_to_hide_c;
	unsigned int plane_index;
	unsigned int stream_index;

	for (plane_index = 0; plane_index < p->num_active_planes; plane_index++) {
		stream_index = p->display_cfg->plane_descriptors[plane_index].stream_index;

		dst_lines_to_hide = (unsigned int)math_ceil(p->latency_to_hide_us[plane_index] /
				((double)p->display_cfg->stream_descriptors[stream_index].timing.h_total /
						(double)p->display_cfg->stream_descriptors[stream_index].timing.pixel_clock_khz * 1000.0));

		src_lines_to_hide_l = (unsigned int)math_ceil2(p->display_cfg->plane_descriptors[plane_index].composition.scaler_info.plane0.v_ratio * dst_lines_to_hide,
				p->swath_height_l[plane_index]);
		p->bytes_required_l[plane_index] = src_lines_to_hide_l * p->num_of_dpp[plane_index] * p->swath_width_l[plane_index] * p->byte_per_pix_l[plane_index];

		src_lines_to_hide_c = (unsigned int)math_ceil2(p->display_cfg->plane_descriptors[plane_index].composition.scaler_info.plane1.v_ratio * dst_lines_to_hide,
				p->swath_height_c[plane_index]);
		p->bytes_required_c[plane_index] = src_lines_to_hide_c * p->num_of_dpp[plane_index] * p->swath_width_c[plane_index] * p->byte_per_pix_c[plane_index];

		if (p->display_cfg->plane_descriptors[plane_index].surface.dcc.enable && p->mrq_present) {
			p->bytes_required_l[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_l / p->meta_row_height_l[plane_index]) * p->meta_row_bytes_per_row_ub_l[plane_index];
			if (p->meta_row_height_c[plane_index]) {
				p->bytes_required_c[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_c / p->meta_row_height_c[plane_index]) * p->meta_row_bytes_per_row_ub_c[plane_index];
			}
		}

		if (p->display_cfg->gpuvm_enable == true) {
			p->bytes_required_l[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_l / p->dpte_row_height_l[plane_index]) * p->dpte_bytes_per_row_l[plane_index];
			if (p->dpte_row_height_c[plane_index]) {
				p->bytes_required_c[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_c / p->dpte_row_height_c[plane_index]) * p->dpte_bytes_per_row_c[plane_index];
			}
		}
	}
}

void dcn5_calculate_excess_vactive_bandwidth_required(
		const struct dml2_display_cfg *display_cfg,
		unsigned int num_active_planes,
		unsigned int bytes_required_l[],
		unsigned int bytes_required_c[],
		/* outputs */
		double excess_vactive_fill_bw_l[],
		double excess_vactive_fill_bw_c[])
{
	unsigned int plane_index;

	for (plane_index = 0; plane_index < num_active_planes; plane_index++) {
		excess_vactive_fill_bw_l[plane_index] = 0.0;
		excess_vactive_fill_bw_c[plane_index] = 0.0;

		if (display_cfg->plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_uclk] > 0) {
			excess_vactive_fill_bw_l[plane_index] = (double)bytes_required_l[plane_index] / (double)display_cfg->plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_uclk];
			excess_vactive_fill_bw_c[plane_index] = (double)bytes_required_c[plane_index] / (double)display_cfg->plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_uclk];
		}
	}
}

void dcn5_calculate_cursor_req_attributes(
		unsigned int cursor_width,
		unsigned int cursor_bpp,

		// output
		unsigned int *cursor_lines_per_chunk,
		unsigned int *cursor_bytes_per_line,
		unsigned int *cursor_bytes_per_chunk,
		unsigned int *cursor_bytes)
{
	unsigned int cursor_bytes_per_req = 0;
	unsigned int cursor_width_bytes = 0;

	//SW determines the cursor pitch to support the maximum cursor_width that will be used but the following restrictions apply.
	//- For 2bpp, cursor_pitch = 256 pixels due to min cursor request size of 64B
	//- For 32 or 64 bpp, cursor_pitch = 64, 128 or 256 pixels depending on the cursor width

	//The cursor requestor uses a cursor request size of 64B, 128B, or 256B depending on the cursor_width and cursor_bpp as follows.

	cursor_width_bytes = (unsigned int)math_ceil2((double)cursor_width * cursor_bpp / 8, 1);
	if (cursor_width_bytes <= 64)
		cursor_bytes_per_req = 64;
	else if (cursor_width_bytes <= 128)
		cursor_bytes_per_req = 128;
	else
		cursor_bytes_per_req = 256;

	//If cursor_width_bytes is greater than 256B, then multiple 256B requests are issued to fetch the entire cursor line.
	*cursor_bytes_per_line = (unsigned int)math_ceil2((double)cursor_width_bytes, cursor_bytes_per_req);

	//Nominally, the cursor chunk is 1KB or 2KB but it is restricted to a power of 2 number of lines with a maximum of 16 lines.
	if (cursor_bpp == 2) {
		*cursor_lines_per_chunk = 16;
	} else if (cursor_bpp == 32) {
		if (cursor_width <= 32)
			*cursor_lines_per_chunk = 16;
		else if (cursor_width <= 64)
			*cursor_lines_per_chunk = 8;
		else if (cursor_width <= 128)
			*cursor_lines_per_chunk = 4;
		else
			*cursor_lines_per_chunk = 2;
	} else if (cursor_bpp == 64) {
		if (cursor_width <= 16)
			*cursor_lines_per_chunk = 16;
		else if (cursor_width <= 32)
			*cursor_lines_per_chunk = 8;
		else if (cursor_width <= 64)
			*cursor_lines_per_chunk = 4;
		else if (cursor_width <= 128)
			*cursor_lines_per_chunk = 2;
		else
			*cursor_lines_per_chunk = 1;
	} else {
		if (cursor_width > 0) {
			DML_LOG_VERBOSE("DML::%s: Invalid cursor_bpp = %d\n", __func__, cursor_bpp);
			DML_ASSERT(0);
		}
	}

	*cursor_bytes_per_chunk = *cursor_bytes_per_line * *cursor_lines_per_chunk;

	// For the cursor implementation, all requested data is stored in the return buffer. Given this fact, the cursor_bytes can be directly compared with the CursorBufferSize.
	// Only cursor_width is provided for worst case sizing so assume that the cursor is square
	*cursor_bytes = *cursor_bytes_per_line * cursor_width;
	DML_LOG_VERBOSE("DML::%s: cursor_bpp = %d\n", __func__, cursor_bpp);
	DML_LOG_VERBOSE("DML::%s: cursor_width = %d\n", __func__, cursor_width);
	DML_LOG_VERBOSE("DML::%s: cursor_width_bytes = %d\n", __func__, cursor_width_bytes);
	DML_LOG_VERBOSE("DML::%s: cursor_bytes_per_req = %d\n", __func__, cursor_bytes_per_req);
	DML_LOG_VERBOSE("DML::%s: cursor_lines_per_chunk = %d\n", __func__, *cursor_lines_per_chunk);
	DML_LOG_VERBOSE("DML::%s: cursor_bytes_per_line = %d\n", __func__, *cursor_bytes_per_line);
	DML_LOG_VERBOSE("DML::%s: cursor_bytes_per_chunk = %d\n", __func__, *cursor_bytes_per_chunk);
	DML_LOG_VERBOSE("DML::%s: cursor_bytes = %d\n", __func__, *cursor_bytes);
	DML_LOG_VERBOSE("DML::%s: cursor_pitch = %d\n", __func__, cursor_bpp == 2 ? 256 : (unsigned int)1 << (unsigned int)math_ceil2(math_log((float)cursor_width, 2), 1));
}

void dcn5_calculate_cursor_urgent_burst_factor(
		unsigned int CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int cursor_bytes_per_chunk,
		unsigned int cursor_lines_per_chunk,
		double LineTime,
		double UrgentLatency,

		double *UrgentBurstFactorCursor,
		bool *NotEnoughUrgentLatencyHiding)
{
	unsigned int LinesInCursorBuffer = 0;
	double CursorBufferSizeInTime = 0;

	if (CursorWidth > 0) {
		LinesInCursorBuffer = (unsigned int)math_floor2(CursorBufferSize * 1024.0 / (double)cursor_bytes_per_chunk, 1) * cursor_lines_per_chunk;

		CursorBufferSizeInTime = LinesInCursorBuffer * LineTime;
		if (CursorBufferSizeInTime - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorCursor = 1;
		} else {
			*NotEnoughUrgentLatencyHiding = 0;
			*UrgentBurstFactorCursor = CursorBufferSizeInTime / (CursorBufferSizeInTime - UrgentLatency);
		}
		DML_LOG_VERBOSE("DML::%s: LinesInCursorBuffer = %u\n", __func__, LinesInCursorBuffer);
		DML_LOG_VERBOSE("DML::%s: CursorBufferSizeInTime = %f\n", __func__, CursorBufferSizeInTime);
		DML_LOG_VERBOSE("DML::%s: CursorBufferSize = %u (kbytes)\n", __func__, CursorBufferSize);
		DML_LOG_VERBOSE("DML::%s: cursor_bytes_per_chunk = %u\n", __func__, cursor_bytes_per_chunk);
		DML_LOG_VERBOSE("DML::%s: cursor_lines_per_chunk = %u\n", __func__, cursor_lines_per_chunk);
		DML_LOG_VERBOSE("DML::%s: UrgentBurstFactorCursor = %f\n", __func__, *UrgentBurstFactorCursor);
		DML_LOG_VERBOSE("DML::%s: NotEnoughUrgentLatencyHiding = %d\n", __func__, *NotEnoughUrgentLatencyHiding);
	}
}

void dcn5_calculate_urgent_burst_factor(
		const struct dml2_plane_parameters *plane_cfg,
		unsigned int swath_width_luma_ub,
		unsigned int swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double LineTime,
		double UrgentLatency,
		double VRatio,
		double VRatioC,
		double BytePerPixelInDETY,
		double BytePerPixelInDETC,
		unsigned int DETBufferSizeY,
		unsigned int DETBufferSizeC,
		// Output
		double *UrgentBurstFactorLuma,
		double *UrgentBurstFactorChroma,
		bool *NotEnoughUrgentLatencyHiding)
{
	(void)plane_cfg;
	double LinesInDETLuma;
	double LinesInDETChroma;
	double DETBufferSizeInTimeLuma;
	double DETBufferSizeInTimeChroma;

	*NotEnoughUrgentLatencyHiding = 0;
	*UrgentBurstFactorLuma = 0;
	*UrgentBurstFactorChroma = 0;

	DML_LOG_VERBOSE("DML::%s: VRatio = %f\n", __func__, VRatio);
	DML_LOG_VERBOSE("DML::%s: VRatioC = %f\n", __func__, VRatioC);
	DML_LOG_VERBOSE("DML::%s: DETBufferSizeY = %d\n", __func__, DETBufferSizeY);
	DML_LOG_VERBOSE("DML::%s: DETBufferSizeC = %d\n", __func__, DETBufferSizeC);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelInDETY = %f\n", __func__, BytePerPixelInDETY);
	DML_LOG_VERBOSE("DML::%s: swath_width_luma_ub = %d\n", __func__, swath_width_luma_ub);
	DML_LOG_VERBOSE("DML::%s: LineTime = %f\n", __func__, LineTime);
	DML_ASSERT(VRatio > 0);

	LinesInDETLuma = DETBufferSizeY / BytePerPixelInDETY / swath_width_luma_ub;

	DETBufferSizeInTimeLuma = math_floor2(LinesInDETLuma, SwathHeightY) * LineTime / VRatio;
	if (DETBufferSizeInTimeLuma - UrgentLatency <= 0) {
		*NotEnoughUrgentLatencyHiding = 1;
		*UrgentBurstFactorLuma = 1;
	} else {
		*UrgentBurstFactorLuma = DETBufferSizeInTimeLuma / (DETBufferSizeInTimeLuma - UrgentLatency);
	}

	if (BytePerPixelInDETC > 0) {
		LinesInDETChroma = DETBufferSizeC / BytePerPixelInDETC / swath_width_chroma_ub;

		DETBufferSizeInTimeChroma = math_floor2(LinesInDETChroma, SwathHeightC) * LineTime / VRatioC;
		if (DETBufferSizeInTimeChroma - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorChroma = 1;
		} else {
			*UrgentBurstFactorChroma = DETBufferSizeInTimeChroma / (DETBufferSizeInTimeChroma - UrgentLatency);
		}
	}

	DML_LOG_VERBOSE("DML::%s: LinesInDETLuma = %f\n", __func__, LinesInDETLuma);
	DML_LOG_VERBOSE("DML::%s: UrgentLatency = %f\n", __func__, UrgentLatency);
	DML_LOG_VERBOSE("DML::%s: DETBufferSizeInTimeLuma = %f\n", __func__, DETBufferSizeInTimeLuma);
	DML_LOG_VERBOSE("DML::%s: UrgentBurstFactorLuma = %f\n", __func__, *UrgentBurstFactorLuma);
	DML_LOG_VERBOSE("DML::%s: UrgentBurstFactorChroma = %f\n", __func__, *UrgentBurstFactorChroma);
	DML_LOG_VERBOSE("DML::%s: NotEnoughUrgentLatencyHiding = %d\n", __func__, *NotEnoughUrgentLatencyHiding);
}

void dcn5_calculate_dcfclk_deep_sleep(
		const struct dml2_display_cfg *display_cfg,
		unsigned int NumberOfActiveSurfaces,
		unsigned int BytePerPixelY[],
		unsigned int BytePerPixelC[],
		unsigned int SwathWidthY[],
		unsigned int SwathWidthC[],
		unsigned int DPPPerSurface[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double Dppclk[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		unsigned int ReturnBusWidth,

		// Output
		double *DCFClkDeepSleep)
{
	double DisplayPipeLineDeliveryTimeLuma;
	double DisplayPipeLineDeliveryTimeChroma;
	double DCFClkDeepSleepPerSurface[DML2_MAX_PLANES];
	double ReadBandwidth = 0.0;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		double pixel_rate_mhz = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

		if (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio <= 1) {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] * DPPPerSurface[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio / pixel_rate_mhz;
		} else {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}
		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma = 0;
		} else {
			if (display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio <= 1) {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] * DPPPerSurface[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio / pixel_rate_mhz;
			} else {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (BytePerPixelC[k] > 0) {
			DCFClkDeepSleepPerSurface[k] = math_max2(__DML2_CALCS_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 32.0 / DisplayPipeLineDeliveryTimeLuma,
					__DML2_CALCS_DCFCLK_FACTOR__ * SwathWidthC[k] * BytePerPixelC[k] / 32.0 / DisplayPipeLineDeliveryTimeChroma);
		} else {
			DCFClkDeepSleepPerSurface[k] = __DML2_CALCS_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 64.0 / DisplayPipeLineDeliveryTimeLuma;
		}
		DCFClkDeepSleepPerSurface[k] = math_max2(DCFClkDeepSleepPerSurface[k], pixel_rate_mhz / 16);

		DML_LOG_VERBOSE("DML::%s: k=%u, PixelClock = %f\n", __func__, k, pixel_rate_mhz);
		DML_LOG_VERBOSE("DML::%s: k=%u, DCFClkDeepSleepPerSurface = %f\n", __func__, k, DCFClkDeepSleepPerSurface[k]);
	}

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		ReadBandwidth = ReadBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
	}

	*DCFClkDeepSleep = math_max2(8.0, __DML2_CALCS_DCFCLK_FACTOR__ * ReadBandwidth / (double)ReturnBusWidth);

	DML_LOG_VERBOSE("DML::%s: __DML2_CALCS_DCFCLK_FACTOR__ = %f\n", __func__, __DML2_CALCS_DCFCLK_FACTOR__);
	DML_LOG_VERBOSE("DML::%s: ReadBandwidth = %f\n", __func__, ReadBandwidth);
	DML_LOG_VERBOSE("DML::%s: ReturnBusWidth = %u\n", __func__, ReturnBusWidth);
	DML_LOG_VERBOSE("DML::%s: DCFClkDeepSleep = %f\n", __func__, *DCFClkDeepSleep);

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		*DCFClkDeepSleep = math_max2(*DCFClkDeepSleep, DCFClkDeepSleepPerSurface[k]);
	}
	DML_LOG_VERBOSE("DML::%s: DCFClkDeepSleep = %f (final)\n", __func__, *DCFClkDeepSleep);
}

unsigned int dcn5_calculate_max_vstartup(
		bool ptoi_supported,
		unsigned int vblank_nom_default_us,
		const struct dml2_timing_cfg *timing,
		double write_back_delay_us)
{
	unsigned int vblank_size = 0;
	unsigned int max_vstartup_lines = 0;

	double line_time_us = (double)timing->h_total / ((double)timing->pixel_clock_khz / 1000);
	unsigned int vblank_actual = timing->v_total - timing->v_active;
	unsigned int vblank_nom_default_in_line = (unsigned int)math_floor2((double)vblank_nom_default_us / line_time_us, 1.0);
	unsigned int vblank_avail = (timing->vblank_nom == 0) ? vblank_nom_default_in_line : (unsigned int)timing->vblank_nom;

	vblank_size = (unsigned int)math_min2(vblank_actual, vblank_avail);

	if (timing->interlaced && !ptoi_supported)
		max_vstartup_lines = (unsigned int)(math_floor2((vblank_size - 1) / 2.0, 1.0));
	else
		max_vstartup_lines = vblank_size - (unsigned int)math_max2(1.0, math_ceil2(write_back_delay_us / line_time_us, 1.0));
	max_vstartup_lines = (unsigned int)math_min2(max_vstartup_lines, __DML2_CALCS_MAX_VSTARTUP__);

	DML_LOG_VERBOSE("DML::%s: VBlankNom = %lu\n", __func__, timing->vblank_nom);
	DML_LOG_VERBOSE("DML::%s: vblank_nom_default_us = %u\n", __func__, vblank_nom_default_us);
	DML_LOG_VERBOSE("DML::%s: line_time_us = %f\n", __func__, line_time_us);
	DML_LOG_VERBOSE("DML::%s: vblank_actual = %u\n", __func__, vblank_actual);
	DML_LOG_VERBOSE("DML::%s: vblank_avail = %u\n", __func__, vblank_avail);
	DML_LOG_VERBOSE("DML::%s: max_vstartup_lines = %u\n", __func__, max_vstartup_lines);
	return max_vstartup_lines;
}

static void dcn5_calculate_mcache_row_bytes(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_calculate_mcache_row_bytes_params *p)
{
	(void)scratch;
	unsigned int vmpg_bytes = 0;
	unsigned int blk_bytes = 0;
	float meta_per_mvmpg_per_channel = 0;
	unsigned int est_blk_per_vmpg = 2;
	unsigned int mvmpg_per_row_ub = 0;
	unsigned int full_vp_width_mvmpg_aligned = 0;
	unsigned int full_vp_height_mvmpg_aligned = 0;
	unsigned int meta_per_mvmpg_per_channel_ub = 0;
	unsigned int mvmpg_per_mcache;

	DML_LOG_VERBOSE("DML::%s: num_chans = %u\n", __func__, p->num_chans);
	DML_LOG_VERBOSE("DML::%s: mem_word_bytes = %u\n", __func__, p->mem_word_bytes);
	DML_LOG_VERBOSE("DML::%s: mcache_line_size_bytes = %u\n", __func__, p->mcache_line_size_bytes);
	DML_LOG_VERBOSE("DML::%s: mcache_size_bytes = %u\n", __func__, p->mcache_size_bytes);
	DML_LOG_VERBOSE("DML::%s: gpuvm_enable = %u\n", __func__, p->gpuvm_enable);
	DML_LOG_VERBOSE("DML::%s: gpuvm_page_size_kbytes = %u\n", __func__, p->gpuvm_page_size_kbytes);
	DML_LOG_VERBOSE("DML::%s: vp_stationary = %u\n", __func__, p->vp_stationary);
	DML_LOG_VERBOSE("DML::%s: tiling_mode = %u\n", __func__, p->tiling_mode);
	DML_LOG_VERBOSE("DML::%s: vp_start_x = %u\n", __func__, p->vp_start_x);
	DML_LOG_VERBOSE("DML::%s: vp_start_y = %u\n", __func__, p->vp_start_y);
	DML_LOG_VERBOSE("DML::%s: full_vp_width = %u\n", __func__, p->full_vp_width);
	DML_LOG_VERBOSE("DML::%s: full_vp_height = %u\n", __func__, p->full_vp_height);
	DML_LOG_VERBOSE("DML::%s: blk_width = %u\n", __func__, p->blk_width);
	DML_LOG_VERBOSE("DML::%s: blk_height = %u\n", __func__, p->blk_height);
	DML_LOG_VERBOSE("DML::%s: vmpg_width = %u\n", __func__, p->vmpg_width);
	DML_LOG_VERBOSE("DML::%s: vmpg_height = %u\n", __func__, p->vmpg_height);
	DML_LOG_VERBOSE("DML::%s: full_swath_bytes = %u\n", __func__, p->full_swath_bytes);
	DML_ASSERT(p->mcache_line_size_bytes != 0);
	DML_ASSERT(p->mcache_size_bytes != 0);

	*p->mvmpg_width = 0;
	*p->mvmpg_height = 0;

	if (p->full_vp_height == 0 && p->full_vp_width == 0) {
		*p->num_mcaches = 0;
		*p->mcache_row_bytes = 0;
		*p->mcache_row_bytes_per_channel = 0;
	} else {
		blk_bytes = dml2_core_utils_get_tile_block_size_bytes(p->tiling_mode, p->bytes_per_pixel);

		// if gpuvm is not enable, the alignment boundary should be in terms of tiling block size
		vmpg_bytes = p->gpuvm_page_size_kbytes * 1024;

		//With vmpg_bytes >= tile blk_bytes, the meta_row_width alignment equations are relative to the vmpg_width/height.
		// But for 4KB page with 64KB tile block, we need the meta for all pages in the tile block.
		// Therefore, the alignment is relative to the blk_width/height. The factor of 16 vmpg per 64KB tile block is applied at the end.
		*p->mvmpg_width = p->blk_width;
		*p->mvmpg_height = p->blk_height;
		if (p->gpuvm_enable) {
			if (vmpg_bytes >= blk_bytes) {
				*p->mvmpg_width = p->vmpg_width;
				*p->mvmpg_height = p->vmpg_height;
			} else if (!((blk_bytes == 65536) && (vmpg_bytes == 4096))) {
				DML_LOG_VERBOSE("ERROR: DML::%s: Tiling size and vm page size combination not supported\n", __func__);
				DML_ASSERT(0);
			}
		}

		//For plane0 & 1, first calculate full_vp_width/height_l/c aligned to vmpg_width/height_l/c
		full_vp_width_mvmpg_aligned = (unsigned int)(math_floor2((p->vp_start_x + p->full_vp_width) + *p->mvmpg_width - 1, *p->mvmpg_width) - math_floor2(p->vp_start_x, *p->mvmpg_width));
		full_vp_height_mvmpg_aligned = (unsigned int)(math_floor2((p->vp_start_y + p->full_vp_height) + *p->mvmpg_height - 1, *p->mvmpg_height) - math_floor2(p->vp_start_y, *p->mvmpg_height));

		*p->full_vp_access_width_mvmpg_aligned = p->surf_vert ? full_vp_height_mvmpg_aligned : full_vp_width_mvmpg_aligned;

		//Use the equation for the exact alignment when possible. Note that the exact alignment cannot be used for horizontal access if vmpg_bytes > blk_bytes.
		if (!p->surf_vert) { //horizontal access
			if (p->vp_stationary == 1 && vmpg_bytes <= blk_bytes)
				*p->meta_row_width_ub = full_vp_width_mvmpg_aligned;
			else
				*p->meta_row_width_ub = (unsigned int)math_ceil2((double)p->full_vp_width - 1, *p->mvmpg_width) + *p->mvmpg_width;
			mvmpg_per_row_ub = *p->meta_row_width_ub / *p->mvmpg_width;
		} else { //vertical access
			if (p->vp_stationary == 1)
				*p->meta_row_width_ub = full_vp_height_mvmpg_aligned;
			else
				*p->meta_row_width_ub = (unsigned int)math_ceil2((double)p->full_vp_height - 1, *p->mvmpg_height) + *p->mvmpg_height;
			mvmpg_per_row_ub = *p->meta_row_width_ub / *p->mvmpg_height;
		}

		if (p->gpuvm_enable) {
			meta_per_mvmpg_per_channel = (float)vmpg_bytes / (float)256 / p->num_chans;

			//but using the est_blk_per_vmpg between 2 and 4, to be not as pessimestic
			if (p->surf_vert && vmpg_bytes > blk_bytes) {
				meta_per_mvmpg_per_channel = (float)est_blk_per_vmpg * blk_bytes / (float)256 / p->num_chans;
			}

			*p->dcc_dram_bw_nom_overhead_factor = 1 + math_max2(1.0 / 256.0, math_ceil2(meta_per_mvmpg_per_channel, p->mem_word_bytes) / (256 * meta_per_mvmpg_per_channel)); // dcc_dr_oh_nom
		} else {
			meta_per_mvmpg_per_channel = (float) blk_bytes / (float)256 / p->num_chans;

			if (!p->surf_vert)
				*p->dcc_dram_bw_nom_overhead_factor = 1 + 1.0 / 256.0;
			else
				*p->dcc_dram_bw_nom_overhead_factor = 1 + math_max2(1.0 / 256.0, math_ceil2(meta_per_mvmpg_per_channel, p->mem_word_bytes) / (256 * meta_per_mvmpg_per_channel));
		}

		meta_per_mvmpg_per_channel_ub = (unsigned int)math_ceil2((double)meta_per_mvmpg_per_channel, p->mcache_line_size_bytes);

		//but for 4KB vmpg with 64KB tile blk
		if (p->gpuvm_enable && (blk_bytes == 65536) && (vmpg_bytes == 4096))
			meta_per_mvmpg_per_channel_ub = 16 * meta_per_mvmpg_per_channel_ub;

		// If this mcache_row_bytes for the full viewport of the surface is less than or equal to mcache_bytes,
		// then one mcache can be used for this request stream. If not, it is useful to know the width of the viewport that can be supported in the mcache_bytes.
		if (p->gpuvm_enable || p->surf_vert) {
			*p->mcache_row_bytes_per_channel = mvmpg_per_row_ub * meta_per_mvmpg_per_channel_ub;
			*p->mcache_row_bytes = *p->mcache_row_bytes_per_channel * p->num_chans;
		} else { // horizontal and gpuvm disable
			*p->mcache_row_bytes = *p->meta_row_width_ub * p->blk_height * p->bytes_per_pixel / 256;
			*p->mcache_row_bytes_per_channel = (unsigned int)math_ceil2((double)*p->mcache_row_bytes / p->num_chans, p->mcache_line_size_bytes);
		}

		*p->dcc_dram_bw_pref_overhead_factor = 1 + math_max2(1.0 / 256.0, (double)*p->mcache_row_bytes / (double)p->full_swath_bytes); // dcc_dr_oh_pref
		*p->num_mcaches = (unsigned int)math_ceil2((double)*p->mcache_row_bytes_per_channel / p->mcache_size_bytes, 1);

		mvmpg_per_mcache = p->mcache_size_bytes / meta_per_mvmpg_per_channel_ub;
		*p->mvmpg_per_mcache_lb = (unsigned int)math_floor2(mvmpg_per_mcache, 1);

		DML_LOG_VERBOSE("DML::%s: gpuvm_enable = %u\n", __func__, p->gpuvm_enable);
		DML_LOG_VERBOSE("DML::%s: vmpg_bytes = %u\n", __func__, vmpg_bytes);
		DML_LOG_VERBOSE("DML::%s: blk_bytes = %u\n", __func__, blk_bytes);
		DML_LOG_VERBOSE("DML::%s: meta_per_mvmpg_per_channel = %f\n", __func__, meta_per_mvmpg_per_channel);
		DML_LOG_VERBOSE("DML::%s: mvmpg_per_row_ub = %u\n", __func__, mvmpg_per_row_ub);
		DML_LOG_VERBOSE("DML::%s: meta_row_width_ub = %u\n", __func__, *p->meta_row_width_ub);
		DML_LOG_VERBOSE("DML::%s: mvmpg_width = %u\n", __func__, *p->mvmpg_width);
		DML_LOG_VERBOSE("DML::%s: mvmpg_height = %u\n", __func__, *p->mvmpg_height);
		DML_LOG_VERBOSE("DML::%s: dcc_dram_bw_nom_overhead_factor = %f\n", __func__, *p->dcc_dram_bw_nom_overhead_factor);
		DML_LOG_VERBOSE("DML::%s: dcc_dram_bw_pref_overhead_factor = %f\n", __func__, *p->dcc_dram_bw_pref_overhead_factor);
	}

	DML_LOG_VERBOSE("DML::%s: mcache_row_bytes = %u\n", __func__, *p->mcache_row_bytes);
	DML_LOG_VERBOSE("DML::%s: mcache_row_bytes_per_channel = %u\n", __func__, *p->mcache_row_bytes_per_channel);
	DML_LOG_VERBOSE("DML::%s: num_mcaches = %u\n", __func__, *p->num_mcaches);
	DML_ASSERT(*p->num_mcaches > 0);
}

void dcn5_calculate_mcache_setting(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_calculate_mcache_setting_params *p)
{
	unsigned int n;

	struct dml2_core_shared_calculate_mcache_setting_locals *l = &scratch->calculate_mcache_setting_locals;
	memset(l, 0, sizeof(struct dml2_core_shared_calculate_mcache_setting_locals));

	*p->num_mcaches_l = 0;
	*p->mcache_row_bytes_l = 0;
	*p->mcache_row_bytes_per_channel_l = 0;
	*p->dcc_dram_bw_nom_overhead_factor_l = 1.0;
	*p->dcc_dram_bw_pref_overhead_factor_l = 1.0;

	*p->num_mcaches_c = 0;
	*p->mcache_row_bytes_c = 0;
	*p->mcache_row_bytes_per_channel_c = 0;
	*p->dcc_dram_bw_nom_overhead_factor_c = 1.0;
	*p->dcc_dram_bw_pref_overhead_factor_c = 1.0;

	*p->mall_comb_mcache_l = 0;
	*p->mall_comb_mcache_c = 0;
	*p->lc_comb_mcache = 0;

	if (!p->dcc_enable)
		return;

	l->is_dual_plane =  dml2_core_utils_is_420(p->source_format) || dml2_core_utils_is_422_planar(p->source_format) || p->source_format == dml2_rgbe_alpha;

	l->l_p.num_chans = p->num_chans;
	l->l_p.mem_word_bytes = p->mem_word_bytes;
	l->l_p.mcache_size_bytes = p->mcache_size_bytes;
	l->l_p.mcache_line_size_bytes = p->mcache_line_size_bytes;
	l->l_p.gpuvm_enable = p->gpuvm_enable;
	l->l_p.gpuvm_page_size_kbytes = p->gpuvm_page_size_kbytes;
	l->l_p.surf_vert = p->surf_vert;
	l->l_p.vp_stationary = p->vp_stationary;
	l->l_p.tiling_mode = p->tiling_mode;
	l->l_p.vp_start_x = p->vp_start_x_l;
	l->l_p.vp_start_y = p->vp_start_y_l;
	l->l_p.full_vp_width = p->full_vp_width_l;
	l->l_p.full_vp_height = p->full_vp_height_l;
	l->l_p.blk_width = p->blk_width_l;
	l->l_p.blk_height = p->blk_height_l;
	l->l_p.vmpg_width = p->vmpg_width_l;
	l->l_p.vmpg_height = p->vmpg_height_l;
	l->l_p.full_swath_bytes = p->full_swath_bytes_l;
	l->l_p.bytes_per_pixel = p->bytes_per_pixel_l;

	// output
	l->l_p.num_mcaches = p->num_mcaches_l;
	l->l_p.mcache_row_bytes = p->mcache_row_bytes_l;
	l->l_p.mcache_row_bytes_per_channel = p->mcache_row_bytes_per_channel_l;
	l->l_p.dcc_dram_bw_nom_overhead_factor = p->dcc_dram_bw_nom_overhead_factor_l;
	l->l_p.dcc_dram_bw_pref_overhead_factor = p->dcc_dram_bw_pref_overhead_factor_l;
	l->l_p.mvmpg_width = &l->mvmpg_width_l;
	l->l_p.mvmpg_height = &l->mvmpg_height_l;
	l->l_p.full_vp_access_width_mvmpg_aligned = &l->full_vp_access_width_mvmpg_aligned_l;
	l->l_p.meta_row_width_ub = &l->meta_row_width_l;
	l->l_p.mvmpg_per_mcache_lb = &l->mvmpg_per_mcache_lb_l;

	dcn5_calculate_mcache_row_bytes(scratch, &l->l_p);
	DML_ASSERT(*p->num_mcaches_l > 0);

	if (l->is_dual_plane) {
		l->c_p.num_chans = p->num_chans;
		l->c_p.mem_word_bytes = p->mem_word_bytes;
		l->c_p.mcache_size_bytes = p->mcache_size_bytes;
		l->c_p.mcache_line_size_bytes = p->mcache_line_size_bytes;
		l->c_p.gpuvm_enable = p->gpuvm_enable;
		l->c_p.gpuvm_page_size_kbytes = p->gpuvm_page_size_kbytes;
		l->c_p.surf_vert = p->surf_vert;
		l->c_p.vp_stationary = p->vp_stationary;
		l->c_p.tiling_mode = p->tiling_mode;
		l->c_p.vp_start_x = p->vp_start_x_c;
		l->c_p.vp_start_y = p->vp_start_y_c;
		l->c_p.full_vp_width = p->full_vp_width_c;
		l->c_p.full_vp_height = p->full_vp_height_c;
		l->c_p.blk_width = p->blk_width_c;
		l->c_p.blk_height = p->blk_height_c;
		l->c_p.vmpg_width = p->vmpg_width_c;
		l->c_p.vmpg_height = p->vmpg_height_c;
		l->c_p.full_swath_bytes = p->full_swath_bytes_c;
		l->c_p.bytes_per_pixel = p->bytes_per_pixel_c;

		// output
		l->c_p.num_mcaches = p->num_mcaches_c;
		l->c_p.mcache_row_bytes = p->mcache_row_bytes_c;
		l->c_p.mcache_row_bytes_per_channel = p->mcache_row_bytes_per_channel_c;
		l->c_p.dcc_dram_bw_nom_overhead_factor = p->dcc_dram_bw_nom_overhead_factor_c;
		l->c_p.dcc_dram_bw_pref_overhead_factor = p->dcc_dram_bw_pref_overhead_factor_c;
		l->c_p.mvmpg_width = &l->mvmpg_width_c;
		l->c_p.mvmpg_height = &l->mvmpg_height_c;
		l->c_p.full_vp_access_width_mvmpg_aligned = &l->full_vp_access_width_mvmpg_aligned_c;
		l->c_p.meta_row_width_ub = &l->meta_row_width_c;
		l->c_p.mvmpg_per_mcache_lb = &l->mvmpg_per_mcache_lb_c;

		dcn5_calculate_mcache_row_bytes(scratch, &l->c_p);
		DML_ASSERT(*p->num_mcaches_c > 0);
	}

	// Sharing for iMALL access
	l->mcache_remainder_l = *p->mcache_row_bytes_per_channel_l % p->mcache_size_bytes;
	l->mcache_remainder_c = *p->mcache_row_bytes_per_channel_c % p->mcache_size_bytes;
	l->mvmpg_access_width_l = p->surf_vert ? l->mvmpg_height_l : l->mvmpg_width_l;
	l->mvmpg_access_width_c = p->surf_vert ? l->mvmpg_height_c : l->mvmpg_width_c;

	if (p->imall_enable) {
		*p->mall_comb_mcache_l = (2 * l->mcache_remainder_l <= p->mcache_size_bytes);

		if (l->is_dual_plane)
			*p->mall_comb_mcache_c = (2 * l->mcache_remainder_c <= p->mcache_size_bytes);
	}

	if (!p->surf_vert) // horizonatal access
		l->luma_time_factor = (double)l->mvmpg_height_c / l->mvmpg_height_l * 2;
	else // vertical access
		l->luma_time_factor = (double)l->mvmpg_width_c / l->mvmpg_width_l * 2;

	// The algorithm starts with computing a non-integer, avg_mcache_element_size_l/c:
	l->avg_mcache_element_size_l = l->meta_row_width_l / *p->num_mcaches_l;
	if (l->is_dual_plane) {
		l->avg_mcache_element_size_c = l->meta_row_width_c / *p->num_mcaches_c;

		/* if either remainder is 0, then mcache sharing is not needed or not possible due to full utilization */
		if (l->mcache_remainder_l && l->mcache_remainder_c) {
			if (!p->imall_enable || (*p->mall_comb_mcache_l == *p->mall_comb_mcache_c)) {
				l->lc_comb_last_mcache_size = (unsigned int)((l->mcache_remainder_l * (*p->mall_comb_mcache_l ? 2 : 1) * l->luma_time_factor) +
					(l->mcache_remainder_c * (*p->mall_comb_mcache_c ? 2 : 1)));
			}
			*p->lc_comb_mcache = (l->lc_comb_last_mcache_size <= p->mcache_size_bytes) && (*p->mall_comb_mcache_l == *p->mall_comb_mcache_c);
		}
	}

	DML_LOG_VERBOSE("DML::%s: imall_enable = %u\n", __func__, p->imall_enable);
	DML_LOG_VERBOSE("DML::%s: is_dual_plane = %u\n", __func__, l->is_dual_plane);
	DML_LOG_VERBOSE("DML::%s: surf_vert = %u\n", __func__, p->surf_vert);
	DML_LOG_VERBOSE("DML::%s: mvmpg_width_l = %u\n", __func__, l->mvmpg_width_l);
	DML_LOG_VERBOSE("DML::%s: mvmpg_height_l = %u\n", __func__, l->mvmpg_height_l);
	DML_LOG_VERBOSE("DML::%s: mcache_remainder_l = %f\n", __func__, l->mcache_remainder_l);
	DML_LOG_VERBOSE("DML::%s: num_mcaches_l = %u\n", __func__, *p->num_mcaches_l);
	DML_LOG_VERBOSE("DML::%s: avg_mcache_element_size_l = %u\n", __func__, l->avg_mcache_element_size_l);
	DML_LOG_VERBOSE("DML::%s: mvmpg_access_width_l = %u\n", __func__, l->mvmpg_access_width_l);
	DML_LOG_VERBOSE("DML::%s: mall_comb_mcache_l = %u\n", __func__, *p->mall_comb_mcache_l);

	if (l->is_dual_plane) {
		DML_LOG_VERBOSE("DML::%s: mvmpg_width_c = %u\n", __func__, l->mvmpg_width_c);
		DML_LOG_VERBOSE("DML::%s: mvmpg_height_c = %u\n", __func__, l->mvmpg_height_c);
		DML_LOG_VERBOSE("DML::%s: mcache_remainder_c = %f\n", __func__, l->mcache_remainder_c);
		DML_LOG_VERBOSE("DML::%s: luma_time_factor = %f\n", __func__, l->luma_time_factor);
		DML_LOG_VERBOSE("DML::%s: num_mcaches_c = %u\n", __func__, *p->num_mcaches_c);
		DML_LOG_VERBOSE("DML::%s: avg_mcache_element_size_c = %u\n", __func__, l->avg_mcache_element_size_c);
		DML_LOG_VERBOSE("DML::%s: mvmpg_access_width_c = %u\n", __func__, l->mvmpg_access_width_c);
		DML_LOG_VERBOSE("DML::%s: mall_comb_mcache_c = %u\n", __func__, *p->mall_comb_mcache_c);
		DML_LOG_VERBOSE("DML::%s: lc_comb_last_mcache_size = %u\n", __func__, l->lc_comb_last_mcache_size);
		DML_LOG_VERBOSE("DML::%s: lc_comb_mcache = %u\n", __func__, *p->lc_comb_mcache);
	}
	// calculate split_coordinate
	l->full_vp_access_width_l = p->surf_vert ? p->full_vp_height_l : p->full_vp_width_l;
	l->full_vp_access_width_c = p->surf_vert ? p->full_vp_height_c : p->full_vp_width_c;

	for (n = 0; n < *p->num_mcaches_l - 1; n++) {
		p->mcache_offsets_l[n] = (unsigned int)(math_floor2((n + 1) * l->avg_mcache_element_size_l / l->mvmpg_access_width_l, 1)) * l->mvmpg_access_width_l;
	}
	p->mcache_offsets_l[*p->num_mcaches_l - 1] = l->full_vp_access_width_l;

	if (l->is_dual_plane) {
		for (n = 0; n < *p->num_mcaches_c - 1; n++) {
			p->mcache_offsets_c[n] = (unsigned int)(math_floor2((n + 1) * l->avg_mcache_element_size_c / l->mvmpg_access_width_c, 1)) * l->mvmpg_access_width_c;
		}
		p->mcache_offsets_c[*p->num_mcaches_c - 1] = l->full_vp_access_width_c;
	}
	for (n = 0; n < *p->num_mcaches_l; n++)
		DML_LOG_VERBOSE("DML::%s: mcache_offsets_l[%u] = %u\n", __func__, n, p->mcache_offsets_l[n]);

	if (l->is_dual_plane) {
		for (n = 0; n < *p->num_mcaches_c; n++)
			DML_LOG_VERBOSE("DML::%s: mcache_offsets_c[%u] = %u\n", __func__, n, p->mcache_offsets_c[n]);
	}

	// Luma/Chroma combine in the last mcache
	// In the case of Luma/Chroma combine-mCache (with lc_comb_mcache==1), all mCaches except the last segment are filled as much as possible, when stay aligned to mvmpg boundary
	if (*p->lc_comb_mcache && l->is_dual_plane) {
		for (n = 0; n < *p->num_mcaches_l - 1; n++)
			p->mcache_offsets_l[n] = (n + 1) * l->mvmpg_per_mcache_lb_l * l->mvmpg_access_width_l;
		p->mcache_offsets_l[*p->num_mcaches_l - 1] = l->full_vp_access_width_l;

		for (n = 0; n < *p->num_mcaches_c - 1; n++)
			p->mcache_offsets_c[n] = (n + 1) * l->mvmpg_per_mcache_lb_c * l->mvmpg_access_width_c;
		p->mcache_offsets_c[*p->num_mcaches_c - 1] = l->full_vp_access_width_c;

		for (n = 0; n < *p->num_mcaches_l; n++)
			DML_LOG_VERBOSE("DML::%s: mcache_offsets_l[%u] = %u\n", __func__, n, p->mcache_offsets_l[n]);

		for (n = 0; n < *p->num_mcaches_c; n++)
			DML_LOG_VERBOSE("DML::%s: mcache_offsets_c[%u] = %u\n", __func__, n, p->mcache_offsets_c[n]);
	}

	*p->mcache_shift_granularity_l = l->mvmpg_access_width_l;
	*p->mcache_shift_granularity_c = l->mvmpg_access_width_c;
}

void dcn5_calculate_avg_bandwidth_required(
		double *avg_bandwidth_required,
		// input
		unsigned int num_active_planes,
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double cursor_bw[],
		double dcc_dram_bw_nom_overhead_factor_p0[],
		double dcc_dram_bw_nom_overhead_factor_p1[])
{
	unsigned int k;
	*avg_bandwidth_required = 0;
	for (k = 0; k < num_active_planes; ++k) {
		*avg_bandwidth_required += dcc_dram_bw_nom_overhead_factor_p0[k] * ReadBandwidthLuma[k]
			+ dcc_dram_bw_nom_overhead_factor_p1[k] * ReadBandwidthChroma[k]
			+ cursor_bw[k];
	}
}

void dcn5_calculate_hostvm_inefficiency_factor(
		double *HostVMInefficiencyFactor,
		double *HostVMInefficiencyFactorPrefetch,

		bool gpuvm_enable,
		bool hostvm_enable,
		unsigned int remote_iommu_outstanding_translations,
		unsigned int max_outstanding_reqs,
		double urg_bandwidth_avail_active_pixel_and_vm,
		double urg_bandwidth_avail_active_vm_only)
{
	*HostVMInefficiencyFactor = 1;
	*HostVMInefficiencyFactorPrefetch = 1;

	if (gpuvm_enable && hostvm_enable) {
		*HostVMInefficiencyFactor = urg_bandwidth_avail_active_pixel_and_vm / urg_bandwidth_avail_active_vm_only;
		*HostVMInefficiencyFactorPrefetch = *HostVMInefficiencyFactor;

		if ((*HostVMInefficiencyFactorPrefetch < 4) && (remote_iommu_outstanding_translations < max_outstanding_reqs))
			*HostVMInefficiencyFactorPrefetch = 4;
		DML_LOG_VERBOSE("DML::%s: urg_bandwidth_avail_active_pixel_and_vm = %f\n", __func__, urg_bandwidth_avail_active_pixel_and_vm);
		DML_LOG_VERBOSE("DML::%s: urg_bandwidth_avail_active_vm_only = %f\n", __func__, urg_bandwidth_avail_active_vm_only);
		DML_LOG_VERBOSE("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, *HostVMInefficiencyFactor);
		DML_LOG_VERBOSE("DML::%s: HostVMInefficiencyFactorPrefetch = %f\n", __func__, *HostVMInefficiencyFactorPrefetch);
	}
}

void dcn5_calculate_tdlut_setting(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_calculate_tdlut_setting_params *p)
{
	(void)scratch;
	// locals
	unsigned int tdlut_bpe = 8;
	unsigned int tdlut_width;
	unsigned int tdlut_pitch_bytes;
	unsigned int tdlut_footprint_bytes;
	unsigned int vmpg_bytes;
	unsigned int tdlut_vmpg_per_frame;
	unsigned int tdlut_pte_req_per_frame;
	unsigned int tdlut_bytes_per_line;
	double tdlut_drain_rate;
	unsigned int tdlut_mpc_width;
	unsigned int tdlut_bytes_per_group_simple;

	if (!p->setup_for_tdlut) {
		*p->tdlut_groups_per_2row_ub = 0;
		*p->tdlut_opt_time = 0;
		*p->tdlut_drain_time = 0;
		*p->tdlut_bytes_per_group = 0;
		*p->tdlut_pte_bytes_per_frame = 0;
		*p->tdlut_bytes_per_frame = 0;
		return;
	}

	if (p->tdlut_mpc_width_flag) {
		tdlut_mpc_width = 33;
		tdlut_bytes_per_group_simple = 39*256;
	} else {
		tdlut_mpc_width = 17;
		tdlut_bytes_per_group_simple = 10*256;
	}

	vmpg_bytes = p->gpuvm_page_size_kbytes * 1024;

	if (p->tdlut_addressing_mode == dml2_tdlut_simple_linear) {
		if (p->tdlut_width_mode == dml2_tdlut_width_17_cube)
			tdlut_width = 4916;
		else
			tdlut_width = 35940;
	} else {
		if (p->tdlut_width_mode == dml2_tdlut_width_17_cube)
			tdlut_width = 17;
		else // dml2_tdlut_width_33_cube
			tdlut_width = 33;
	}

	if (p->is_gfx11)
		tdlut_pitch_bytes = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 256); //256B alignment
	else
		tdlut_pitch_bytes = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 128); //128B alignment

	if (p->tdlut_addressing_mode == dml2_tdlut_sw_linear)
		tdlut_footprint_bytes = tdlut_pitch_bytes * tdlut_width * tdlut_width;
	else
		tdlut_footprint_bytes = tdlut_pitch_bytes;

	if (!p->gpuvm_enable) {
		tdlut_vmpg_per_frame = 0;
		tdlut_pte_req_per_frame = 0;
	} else {
		tdlut_vmpg_per_frame = (unsigned int)math_ceil2(tdlut_footprint_bytes - 1, vmpg_bytes) / vmpg_bytes + 1;
		tdlut_pte_req_per_frame = (unsigned int)math_ceil2(tdlut_vmpg_per_frame - 1, 8) / 8 + 1;
	}
	tdlut_bytes_per_line = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 64); //64b request
	*p->tdlut_pte_bytes_per_frame = tdlut_pte_req_per_frame * 64;

	if (p->tdlut_addressing_mode == dml2_tdlut_sw_linear) {
		//the tdlut_width is either 17 or 33 but the 33x33x33 is subsampled every other line/slice
		*p->tdlut_bytes_per_frame = tdlut_bytes_per_line * tdlut_mpc_width * tdlut_mpc_width;
		*p->tdlut_bytes_per_group = tdlut_bytes_per_line * tdlut_mpc_width;
		//the delivery cycles is DispClk cycles per line * number of lines * number of slices
		tdlut_drain_rate = tdlut_bytes_per_line * p->dispclk_mhz / math_ceil2(tdlut_mpc_width/2.0, 1);
	} else {
		//tdlut_addressing_mode = tdlut_simple_linear, 3dlut width should be 4*1229=4916 elements
		*p->tdlut_bytes_per_frame = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 256);
		*p->tdlut_bytes_per_group = tdlut_bytes_per_group_simple;
		tdlut_drain_rate = 2 * tdlut_bpe * p->dispclk_mhz;
	}

	//the tdlut is fetched during the 2 row times of prefetch.
	if (p->setup_for_tdlut) {
		*p->tdlut_groups_per_2row_ub = (unsigned int)math_ceil2((double) *p->tdlut_bytes_per_frame / *p->tdlut_bytes_per_group, 1);
		*p->tdlut_opt_time = (int) (*p->tdlut_bytes_per_frame - p->cursor_buffer_size * 1024) / tdlut_drain_rate;
		*p->tdlut_drain_time = p->cursor_buffer_size * 1024 / tdlut_drain_rate;
	}

	DML_LOG_VERBOSE("DML::%s: cursor_buffer_size = %d\n", __func__, p->cursor_buffer_size);
	DML_LOG_VERBOSE("DML::%s: gpuvm_enable = %d\n", __func__, p->gpuvm_enable);
	DML_LOG_VERBOSE("DML::%s: vmpg_bytes = %d\n", __func__, vmpg_bytes);
	DML_LOG_VERBOSE("DML::%s: tdlut_vmpg_per_frame = %d\n", __func__, tdlut_vmpg_per_frame);
	DML_LOG_VERBOSE("DML::%s: tdlut_pte_req_per_frame = %d\n", __func__, tdlut_pte_req_per_frame);

	DML_LOG_VERBOSE("DML::%s: dispclk_mhz = %f\n", __func__, p->dispclk_mhz);
	DML_LOG_VERBOSE("DML::%s: tdlut_width = %u\n", __func__, tdlut_width);
	DML_LOG_VERBOSE("DML::%s: tdlut_addressing_mode = %s\n", __func__, (p->tdlut_addressing_mode == dml2_tdlut_sw_linear) ? "sw_linear" : "simple_linear");
	DML_LOG_VERBOSE("DML::%s: tdlut_pitch_bytes = %u\n", __func__, tdlut_pitch_bytes);
	DML_LOG_VERBOSE("DML::%s: tdlut_footprint_bytes = %u\n", __func__, tdlut_footprint_bytes);
	DML_LOG_VERBOSE("DML::%s: tdlut_bytes_per_frame = %u\n", __func__, *p->tdlut_bytes_per_frame);
	DML_LOG_VERBOSE("DML::%s: tdlut_bytes_per_line = %u\n", __func__, tdlut_bytes_per_line);
	DML_LOG_VERBOSE("DML::%s: tdlut_bytes_per_group = %u\n", __func__, *p->tdlut_bytes_per_group);
	DML_LOG_VERBOSE("DML::%s: tdlut_drain_rate = %f\n", __func__, tdlut_drain_rate);
	DML_LOG_VERBOSE("DML::%s: tdlut_delivery_cycles = %u\n", __func__, p->tdlut_addressing_mode == dml2_tdlut_sw_linear ? (unsigned int)math_ceil2(tdlut_mpc_width/2.0, 1) * tdlut_mpc_width * tdlut_mpc_width : (unsigned int)math_ceil2(tdlut_width/2.0, 1));
	DML_LOG_VERBOSE("DML::%s: tdlut_opt_time = %f\n", __func__, *p->tdlut_opt_time);
	DML_LOG_VERBOSE("DML::%s: tdlut_drain_time = %f\n", __func__, *p->tdlut_drain_time);
	DML_LOG_VERBOSE("DML::%s: tdlut_groups_per_2row_ub = %d\n", __func__, *p->tdlut_groups_per_2row_ub);
}

static void dcn5_calculate_tarb(
		const struct dml2_display_cfg *display_cfg,
		unsigned int PixelChunkSizeInKByte,
		unsigned int NumberOfActiveSurfaces,
		unsigned int NumberOfDPP[],
		unsigned int dpte_group_bytes[],
		unsigned int tdlut_bytes_per_group[],
		double HostVMInefficiencyFactor,
		double HostVMInefficiencyFactorPrefetch,
		double ReturnBW,
		unsigned int MetaChunkSize,

		// output
		double *Tarb,
		double *Tarb_prefetch)
{
	double extra_bytes = 0;
	double extra_bytes_prefetch = 0;
	double HostVMDynamicLevels;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		extra_bytes = extra_bytes + (NumberOfDPP[k] * PixelChunkSizeInKByte * 1024);

		if (display_cfg->plane_descriptors[k].surface.dcc.enable)
			extra_bytes = extra_bytes + (MetaChunkSize * 1024);

		if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut)
			extra_bytes = extra_bytes + tdlut_bytes_per_group[k];
	}

	extra_bytes_prefetch = extra_bytes;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		HostVMDynamicLevels = dcn5_calculate_host_vm_dynamic_levels(display_cfg->gpuvm_enable, display_cfg->hostvm_enable, display_cfg->plane_descriptors[k].overrides.hostvm_min_page_size_kbytes, display_cfg->hostvm_max_non_cached_page_table_levels);

		if (display_cfg->gpuvm_enable == true) {
			extra_bytes = extra_bytes + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactor;
			extra_bytes_prefetch = extra_bytes_prefetch + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactorPrefetch;
		}
	}
	*Tarb = extra_bytes / ReturnBW;
	*Tarb_prefetch = extra_bytes_prefetch / ReturnBW;
	DML_LOG_VERBOSE("DML::%s: PixelChunkSizeInKByte = %d\n", __func__, PixelChunkSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: MetaChunkSize = %d\n", __func__, MetaChunkSize);
	DML_LOG_VERBOSE("DML::%s: extra_bytes = %f\n", __func__, extra_bytes);
	DML_LOG_VERBOSE("DML::%s: extra_bytes_prefetch = %f\n", __func__, extra_bytes_prefetch);
}

void dcn5_calculate_extra_latency(
		const struct dml2_display_cfg *display_cfg,
		unsigned int ROBBufferSizeInKByte,
		unsigned int RoundTripPingLatencyCycles,
		unsigned int ReorderingBytes,
		double DCFCLK,
		double FabricClock,
		unsigned int PixelChunkSizeInKByte,
		double ReturnBW,
		unsigned int NumberOfActiveSurfaces,
		unsigned int NumberOfDPP[],
		unsigned int dpte_group_bytes[],
		unsigned int tdlut_bytes_per_group[],
		double HostVMInefficiencyFactor,
		double HostVMInefficiencyFactorPrefetch,
		enum dml2_qos_param_type qos_type,
		bool max_outstanding_when_urgent_expected,
		unsigned int max_outstanding_requests,
		unsigned int request_size_bytes_luma[],
		unsigned int request_size_bytes_chroma[],
		unsigned int MetaChunkSize,
		unsigned int dchub_arb_to_ret_delay,
		double Ttrip,
		unsigned int hostvm_mode,

		// output
		double *ExtraLatency, // Tex
		double *ExtraLatency_sr, // Tex_sr
		double *ExtraLatencyPrefetch)

{
	double Tarb;
	double Tarb_prefetch;
	double Tex_trips;
	unsigned int max_request_size_bytes = 0;

	dcn5_calculate_tarb(
			display_cfg,
			PixelChunkSizeInKByte,
			NumberOfActiveSurfaces,
			NumberOfDPP,
			dpte_group_bytes,
			tdlut_bytes_per_group,
			HostVMInefficiencyFactor,
			HostVMInefficiencyFactorPrefetch,
			ReturnBW,
			MetaChunkSize,
			// output
			&Tarb,
			&Tarb_prefetch);

	Tex_trips = (display_cfg->hostvm_enable && hostvm_mode == 1) ? (2.0 * Ttrip) : 0.0;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (request_size_bytes_luma[k] > max_request_size_bytes)
			max_request_size_bytes = request_size_bytes_luma[k];
		if (request_size_bytes_chroma[k] > max_request_size_bytes)
			max_request_size_bytes = request_size_bytes_chroma[k];
	}

	if (qos_type == dml2_qos_param_type_dcn4x) {
		*ExtraLatency_sr = dchub_arb_to_ret_delay / DCFCLK;
		*ExtraLatency = *ExtraLatency_sr;
		if (max_outstanding_when_urgent_expected)
			*ExtraLatency = *ExtraLatency + (ROBBufferSizeInKByte * 1024 - max_outstanding_requests * max_request_size_bytes) / ReturnBW;
	} else {
		*ExtraLatency_sr = dchub_arb_to_ret_delay / DCFCLK + RoundTripPingLatencyCycles / FabricClock + ReorderingBytes / ReturnBW;
		*ExtraLatency = *ExtraLatency_sr;
	}
	*ExtraLatency = *ExtraLatency + Tex_trips;
	*ExtraLatencyPrefetch = *ExtraLatency + Tarb_prefetch;
	*ExtraLatency = *ExtraLatency + Tarb;
	*ExtraLatency_sr = *ExtraLatency_sr + Tarb;

	DML_LOG_VERBOSE("DML::%s: qos_type=%u\n", __func__, qos_type);
	DML_LOG_VERBOSE("DML::%s: hostvm_mode=%u\n", __func__, hostvm_mode);
	DML_LOG_VERBOSE("DML::%s: Tex_trips=%f\n", __func__, Tex_trips);
	DML_LOG_VERBOSE("DML::%s: DCFCLK=%f\n", __func__, DCFCLK);
	DML_LOG_VERBOSE("DML::%s: ReturnBW=%f\n", __func__, ReturnBW);
	if (qos_type == dml2_qos_param_type_dcn4x) {
		DML_LOG_VERBOSE("DML::%s: max_outstanding_when_urgent_expected=%u\n", __func__, max_outstanding_when_urgent_expected);
		DML_LOG_VERBOSE("DML::%s: max_outstanding_requests=%u\n", __func__, max_outstanding_requests);
		DML_LOG_VERBOSE("DML::%s: max_request_size_bytes=%u\n", __func__, max_request_size_bytes);
		DML_LOG_VERBOSE("DML::%s: ROBBufferSizeInKByte=%u\n", __func__, ROBBufferSizeInKByte);
	} else {
		DML_LOG_VERBOSE("DML::%s: FabricClock=%f\n", __func__, FabricClock);
		DML_LOG_VERBOSE("DML::%s: RoundTripPingLatencyCycles=%u\n", __func__, RoundTripPingLatencyCycles);
		DML_LOG_VERBOSE("DML::%s: ReorderingBytes=%u\n", __func__, ReorderingBytes);
	}
	DML_LOG_VERBOSE("DML::%s: Tarb=%f\n", __func__, Tarb);
	DML_LOG_VERBOSE("DML::%s: ExtraLatency=%f\n", __func__, *ExtraLatency);
	DML_LOG_VERBOSE("DML::%s: ExtraLatency_sr=%f\n", __func__, *ExtraLatency_sr);
	DML_LOG_VERBOSE("DML::%s: ExtraLatencyPrefetch=%f\n", __func__, *ExtraLatencyPrefetch);
}

double dcn5_calculate_t_wait(
		long reserved_vblank_time_ns,
		double UrgentLatency,
		double Ttrip,
		double temp_read_or_ppt_blackout_us,
		bool drr_enabled
	)
{
	double TWait;
	double t_urg_trip = math_max2(UrgentLatency, Ttrip);
	TWait = math_max2(reserved_vblank_time_ns / 1000.0, drr_enabled ? temp_read_or_ppt_blackout_us : 0.0) + t_urg_trip;

	DML_LOG_VERBOSE("DML::%s: reserved_vblank_time_ns = %ld\n", __func__, reserved_vblank_time_ns);
	DML_LOG_VERBOSE("DML::%s: UrgentLatency = %f\n", __func__, UrgentLatency);
	DML_LOG_VERBOSE("DML::%s: Ttrip = %f\n", __func__, Ttrip);
	DML_LOG_VERBOSE("DML::%s: TWait = %f\n", __func__, TWait);
	return TWait;
}

static void dcn5_calculate_v_update_and_dynamic_metadata_parameters(
		unsigned int MaxInterDCNTileRepeaters,
		double Dppclk,
		double Dispclk,
		double DCFClkDeepSleep,
		double PixelClock,
		unsigned int HTotal,
		unsigned int VBlank,
		unsigned int DynamicMetadataTransmittedBytes,
		unsigned int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,

		// Output
		double *TSetup,
		double *Tdmbf,
		double *Tdmec,
		double *Tdmsks,
		unsigned int *VUpdateOffsetPix,
		unsigned int *VUpdateWidthPix,
		unsigned int *VReadyOffsetPix)
{
	double TotalRepeaterDelayTime;
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2 / Dppclk + 3 / Dispclk);
	*VUpdateWidthPix = (unsigned int)(math_ceil2((14.0 / DCFClkDeepSleep + 12.0 / Dppclk + TotalRepeaterDelayTime) * PixelClock, 1.0));
	*VReadyOffsetPix = (unsigned int)(math_ceil2(math_max2(150.0 / Dppclk, TotalRepeaterDelayTime + 20.0 / DCFClkDeepSleep + 10.0 / Dppclk) * PixelClock, 1.0));
	*VUpdateOffsetPix = (unsigned int)(math_ceil2(HTotal / 4.0, 1.0));
	*TSetup = (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / PixelClock;
	*Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / Dispclk;
	*Tdmec = HTotal / PixelClock;

	if (DynamicMetadataLinesBeforeActiveRequired == 0) {
		*Tdmsks = VBlank * HTotal / PixelClock / 2.0;
	} else {
		*Tdmsks = DynamicMetadataLinesBeforeActiveRequired * HTotal / PixelClock;
	}
	if (InterlaceEnable == 1 && ProgressiveToInterlaceUnitInOPP == false) {
		*Tdmsks = *Tdmsks / 2;
	}
	DML_LOG_VERBOSE("DML::%s: DynamicMetadataLinesBeforeActiveRequired = %u\n", __func__, DynamicMetadataLinesBeforeActiveRequired);
	DML_LOG_VERBOSE("DML::%s: VBlank = %u\n", __func__, VBlank);
	DML_LOG_VERBOSE("DML::%s: HTotal = %u\n", __func__, HTotal);
	DML_LOG_VERBOSE("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	DML_LOG_VERBOSE("DML::%s: Dppclk = %f\n", __func__, Dppclk);
	DML_LOG_VERBOSE("DML::%s: DCFClkDeepSleep = %f\n", __func__, DCFClkDeepSleep);
	DML_LOG_VERBOSE("DML::%s: MaxInterDCNTileRepeaters = %u\n", __func__, MaxInterDCNTileRepeaters);
	DML_LOG_VERBOSE("DML::%s: TotalRepeaterDelayTime = %f\n", __func__, TotalRepeaterDelayTime);
	DML_LOG_VERBOSE("DML::%s: VUpdateWidthPix = %u\n", __func__, *VUpdateWidthPix);
	DML_LOG_VERBOSE("DML::%s: VReadyOffsetPix = %u\n", __func__, *VReadyOffsetPix);
	DML_LOG_VERBOSE("DML::%s: VUpdateOffsetPix = %u\n", __func__, *VUpdateOffsetPix);
	DML_LOG_VERBOSE("DML::%s: Tdmsks = %f\n", __func__, *Tdmsks);
}

bool dcn5_calculate_prefetch_schedule(struct dml2_core_internal_scratch *scratch, struct dml2_core_calcs_CalculatePrefetchSchedule_params *p)
{
	struct dml2_core_calcs_CalculatePrefetchSchedule_locals *s = &scratch->CalculatePrefetchSchedule_locals;
	bool dcc_mrq_enable;

	unsigned int vm_bytes;
	unsigned int extra_tdpe_bytes;
	unsigned int tdlut_row_bytes;
	unsigned int Lo;
	unsigned int cnvc_delay_subtotal;

	s->NoTimeToPrefetch = false;
	s->DPPCycles = 0;
	s->DISPCLKCycles = 0;
	s->DSTTotalPixelsAfterScaler = 0.0;
	s->LineTime = 0.0;
	s->dst_y_prefetch_equ = 0.0;
	s->prefetch_bw_oto = 0.0;
	s->Tvm_oto = 0.0;
	s->Tr0_oto = 0.0;
	s->Tvm_oto_lines = 0.0;
	s->Tr0_oto_lines = 0.0;
	s->dst_y_prefetch_oto = 0.0;
	s->TimeForFetchingVM = 0.0;
	s->TimeForFetchingRowInVBlank = 0.0;
	s->LinesToRequestPrefetchPixelData = 0.0;
	s->HostVMDynamicLevelsTrips = 0;
	s->trip_to_mem = 0.0;
	*p->Tvm_trips = 0.0;
	*p->Tr0_trips = 0.0;
	s->Tvm_trips_rounded = 0.0;
	s->Tr0_trips_rounded = 0.0;
	s->max_Tsw = 0.0;
	s->Lsw_oto = 0.0;
	*p->Tpre_rounded = 0.0;
	s->prefetch_bw_equ = 0.0;
	s->Tvm_equ = 0.0;
	s->Tr0_equ = 0.0;
	s->Tdmbf = 0.0;
	s->Tdmec = 0.0;
	s->Tdmsks = 0.0;
	*p->prefetch_sw_bytes = 0.0;
	s->prefetch_bw_pr = 0.0;
	s->bytes_pp = 0.0;
	s->dep_bytes = 0.0;
	s->min_Lsw_oto = 0.0;
	s->min_Lsw_equ = 0.0;
	s->Tsw_est1 = 0.0;
	s->Tsw_est2 = 0.0;
	s->Tsw_est3 = 0.0;
	s->cursor_prefetch_bytes = 0;
	*p->prefetch_cursor_bw = 0;

	dcc_mrq_enable = (p->dcc_enable && p->mrq_present);

	s->TWait_p = p->TWait - p->Ttrip; // TWait includes max(Turg, Ttrip) and Ttrip here is already max(Turg, Ttrip)

	if (p->display_cfg->gpuvm_enable == true && p->display_cfg->hostvm_enable == true) {
		s->HostVMDynamicLevelsTrips = p->display_cfg->hostvm_max_non_cached_page_table_levels;
	} else {
		s->HostVMDynamicLevelsTrips = 0;
	}
	DML_LOG_VERBOSE("DML::%s: dcc_enable = %u\n", __func__, p->dcc_enable);
	DML_LOG_VERBOSE("DML::%s: mrq_present = %u\n", __func__, p->mrq_present);
	DML_LOG_VERBOSE("DML::%s: dcc_mrq_enable = %u\n", __func__, dcc_mrq_enable);
	DML_LOG_VERBOSE("DML::%s: GPUVMEnable = %u\n", __func__, p->display_cfg->gpuvm_enable);
	DML_LOG_VERBOSE("DML::%s: GPUVMPageTableLevels = %u\n", __func__, p->display_cfg->gpuvm_max_page_table_levels);
	DML_LOG_VERBOSE("DML::%s: DCCEnable = %u\n", __func__, p->myPipe->DCCEnable);
	DML_LOG_VERBOSE("DML::%s: VStartup = %u\n", __func__, p->VStartup);
	DML_LOG_VERBOSE("DML::%s: HostVMEnable = %u\n", __func__, p->display_cfg->hostvm_enable);
	DML_LOG_VERBOSE("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
	DML_LOG_VERBOSE("DML::%s: TWait = %f\n", __func__, p->TWait);
	DML_LOG_VERBOSE("DML::%s: TWait_p = %f\n", __func__, s->TWait_p);
	DML_LOG_VERBOSE("DML::%s: Ttrip = %f\n", __func__, p->Ttrip);
	DML_LOG_VERBOSE("DML::%s: myPipe->Dppclk = %f\n", __func__, p->myPipe->Dppclk);
	DML_LOG_VERBOSE("DML::%s: myPipe->Dispclk = %f\n", __func__, p->myPipe->Dispclk);
	dcn5_calculate_v_update_and_dynamic_metadata_parameters(
			p->MaxInterDCNTileRepeaters,
			p->myPipe->Dppclk,
			p->myPipe->Dispclk,
			p->myPipe->DCFClkDeepSleep,
			p->myPipe->PixelClock,
			p->myPipe->HTotal,
			p->myPipe->VBlank,
			p->DynamicMetadataTransmittedBytes,
			p->DynamicMetadataLinesBeforeActiveRequired,
			p->myPipe->InterlaceEnable,
			p->myPipe->ProgressiveToInterlaceUnitInOPP,
			p->TSetup,

			// Output
			&s->Tdmbf,
			&s->Tdmec,
			&s->Tdmsks,
			p->VUpdateOffsetPix,
			p->VUpdateWidthPix,
			p->VReadyOffsetPix);

	s->LineTime = p->myPipe->HTotal / p->myPipe->PixelClock;
	s->trip_to_mem = p->Ttrip;
	*p->Tvm_trips = p->ExtraLatencyPrefetch + math_max2(s->trip_to_mem * (p->display_cfg->gpuvm_max_page_table_levels * (s->HostVMDynamicLevelsTrips + 1)), p->Turg);
	if (dcc_mrq_enable)
		*p->Tvm_trips_flip = *p->Tvm_trips;
	else
		*p->Tvm_trips_flip = *p->Tvm_trips - s->trip_to_mem;

	*p->Tr0_trips_flip = s->trip_to_mem * (s->HostVMDynamicLevelsTrips + 1);
	*p->Tr0_trips = math_max2(*p->Tr0_trips_flip, p->tdlut_opt_time / 2);

	if (p->DynamicMetadataVMEnabled == true) {
		*p->Tdmdl_vm = s->TWait_p + *p->Tvm_trips;
		*p->Tdmdl = *p->Tdmdl_vm + p->Ttrip;
	} else {
		*p->Tdmdl_vm = 0;
		*p->Tdmdl = s->TWait_p + p->ExtraLatencyPrefetch + p->Ttrip; // Tex
	}

	if (p->DynamicMetadataEnable == true) {
		if (p->VStartup * s->LineTime < *p->TSetup + *p->Tdmdl + s->Tdmbf + s->Tdmec + s->Tdmsks) {
			*p->NotEnoughTimeForDynamicMetadata = true;
			DML_LOG_VERBOSE("DML::%s: Not Enough Time for Dynamic Meta!\n", __func__);
			DML_LOG_VERBOSE("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, s->Tdmbf);
			DML_LOG_VERBOSE("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, s->Tdmec);
			DML_LOG_VERBOSE("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, s->Tdmsks);
			DML_LOG_VERBOSE("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", __func__, *p->Tdmdl);
		} else {
			*p->NotEnoughTimeForDynamicMetadata = false;
		}
	} else {
		*p->NotEnoughTimeForDynamicMetadata = false;
	}

	cnvc_delay_subtotal = (unsigned int)(p->DPPCLKDelaySubtotalPlusCNVCFormater);
	if (p->display_cfg->plane_descriptors->composition.scaler_info.upsp_enabled && dml2_core_utils_is_420(p->display_cfg->plane_descriptors->pixel_format))
		cnvc_delay_subtotal += 15;
	if (p->display_cfg->plane_descriptors->composition.scaler_info.upsp_enabled &&
		(dml2_core_utils_is_422_planar(p->display_cfg->plane_descriptors->pixel_format) || dml2_core_utils_is_422_packed(p->display_cfg->plane_descriptors->pixel_format)))
		cnvc_delay_subtotal += 6;

	if (!p->myPipe->ScalerEnabled)
		s->DPPCycles = cnvc_delay_subtotal + (unsigned int)(p->DPPCLKDelaySCLLBOnly);
	else if (!p->display_cfg->plane_descriptors->composition.scaler_info.easf_enabled && !p->display_cfg->plane_descriptors->composition.scaler_info.isharp_enabled)
		s->DPPCycles = cnvc_delay_subtotal + (unsigned int)(p->DPPCLKDelaySCL);
	else if (p->display_cfg->plane_descriptors->composition.scaler_info.easf_enabled && p->display_cfg->plane_descriptors->composition.scaler_info.isharp_enabled)
		s->DPPCycles = cnvc_delay_subtotal + 100;
	else // easf only
		s->DPPCycles = cnvc_delay_subtotal + 80;

	s->DPPCycles = (unsigned int)(s->DPPCycles + p->myPipe->NumberOfCursors * p->DPPCLKDelayCNVCCursor);

	s->DISPCLKCycles = (unsigned int)p->DISPCLKDelaySubtotal;

	if (p->display_cfg->plane_descriptors->tdlut.setup_for_tdlut && p->display_cfg->plane_descriptors->tdlut.tdlut_width_mode == dml2_tdlut_width_33_cube)
		s->DISPCLKCycles += 34;

	s->DISPCLKCycles += (p->myPipe->ODMMode != dml2_odm_mode_bypass ? 18 : 0);

	if (p->myPipe->Dppclk == 0.0 || p->myPipe->Dispclk == 0.0)
		return true;

	*p->DSTXAfterScaler = (unsigned int)math_round(s->DPPCycles * p->myPipe->PixelClock / p->myPipe->Dppclk + s->DISPCLKCycles * p->myPipe->PixelClock / p->myPipe->Dispclk + p->DSCDelay);

	if (p->myPipe->ODMMode == dml2_odm_mode_split_1to2 || p->myPipe->ODMMode == dml2_odm_mode_mso_1to2)
		*p->DSTXAfterScaler += p->myPipe->HActive / 2;
	else if (p->myPipe->ODMMode == dml2_odm_mode_mso_1to4)
		*p->DSTXAfterScaler += (p->myPipe->HActive * 3) / 4;
	else
		*p->DSTXAfterScaler += (p->myPipe->DPPPerSurface - 1) * p->DPP_RECOUT_WIDTH;

	DML_LOG_VERBOSE("DML::%s: DynamicMetadataVMEnabled = %u\n", __func__, p->DynamicMetadataVMEnabled);
	DML_LOG_VERBOSE("DML::%s: DPPCycles = %u\n", __func__, s->DPPCycles);
	DML_LOG_VERBOSE("DML::%s: PixelClock = %f\n", __func__, p->myPipe->PixelClock);
	DML_LOG_VERBOSE("DML::%s: Dppclk = %f\n", __func__, p->myPipe->Dppclk);
	DML_LOG_VERBOSE("DML::%s: DISPCLKCycles = %u\n", __func__, s->DISPCLKCycles);
	DML_LOG_VERBOSE("DML::%s: DISPCLK = %f\n", __func__, p->myPipe->Dispclk);
	DML_LOG_VERBOSE("DML::%s: DSCDelay = %u\n", __func__, p->DSCDelay);
	DML_LOG_VERBOSE("DML::%s: ODMMode = %u\n", __func__, p->myPipe->ODMMode);
	DML_LOG_VERBOSE("DML::%s: DPP_RECOUT_WIDTH = %u\n", __func__, p->DPP_RECOUT_WIDTH);
	DML_LOG_VERBOSE("DML::%s: DSTXAfterScaler = %u\n", __func__, *p->DSTXAfterScaler);

	DML_LOG_VERBOSE("DML::%s: setup_for_tdlut = %u\n", __func__, p->setup_for_tdlut);
	DML_LOG_VERBOSE("DML::%s: tdlut_opt_time = %f\n", __func__, p->tdlut_opt_time);
	DML_LOG_VERBOSE("DML::%s: tdlut_pte_bytes_per_frame = %u\n", __func__, p->tdlut_pte_bytes_per_frame);

	if (p->OutputFormat == dml2_420 || (p->myPipe->InterlaceEnable && p->myPipe->ProgressiveToInterlaceUnitInOPP))
		*p->DSTYAfterScaler = 1;
	else
		*p->DSTYAfterScaler = 0;

	s->DSTTotalPixelsAfterScaler = *p->DSTYAfterScaler * p->myPipe->HTotal + *p->DSTXAfterScaler;
	*p->DSTYAfterScaler = (unsigned int)(math_floor2(s->DSTTotalPixelsAfterScaler / p->myPipe->HTotal, 1));
	*p->DSTXAfterScaler = (unsigned int)(s->DSTTotalPixelsAfterScaler - ((double)(*p->DSTYAfterScaler * p->myPipe->HTotal)));
	DML_LOG_VERBOSE("DML::%s: DSTXAfterScaler = %u (final)\n", __func__, *p->DSTXAfterScaler);
	DML_LOG_VERBOSE("DML::%s: DSTYAfterScaler = %u (final)\n", __func__, *p->DSTYAfterScaler);

	s->NoTimeToPrefetch = false;
	DML_LOG_VERBOSE("DML::%s: Tr0_trips = %f\n", __func__, *p->Tr0_trips);
	DML_LOG_VERBOSE("DML::%s: Tvm_trips = %f\n", __func__, *p->Tvm_trips);
	DML_LOG_VERBOSE("DML::%s: trip_to_mem = %f\n", __func__, s->trip_to_mem);
	DML_LOG_VERBOSE("DML::%s: ExtraLatencyPrefetch = %f\n", __func__, p->ExtraLatencyPrefetch);
	DML_LOG_VERBOSE("DML::%s: GPUVMPageTableLevels = %u\n", __func__, p->display_cfg->gpuvm_max_page_table_levels);
	DML_LOG_VERBOSE("DML::%s: HostVMDynamicLevelsTrips = %u\n", __func__, s->HostVMDynamicLevelsTrips);
	if (p->display_cfg->gpuvm_enable) {
		s->Tvm_trips_rounded = math_ceil2(4.0 * *p->Tvm_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		*p->Tvm_trips_flip_rounded = math_ceil2(4.0 * *p->Tvm_trips_flip / s->LineTime, 1.0) / 4.0 * s->LineTime;
	} else {
		if (p->DynamicMetadataEnable || dcc_mrq_enable || p->setup_for_tdlut)
			s->Tvm_trips_rounded = math_max2(s->LineTime * math_ceil2(4.0*math_max3(p->ExtraLatencyPrefetch, p->Turg, s->trip_to_mem)/s->LineTime, 1)/4, s->LineTime/4.0);
		else
			s->Tvm_trips_rounded = s->LineTime / 4.0;
		*p->Tvm_trips_flip_rounded = s->LineTime / 4.0;
	}

	s->Tvm_trips_rounded = math_max2(s->Tvm_trips_rounded, s->LineTime / 4.0);
	*p->Tvm_trips_flip_rounded = math_max2(*p->Tvm_trips_flip_rounded, s->LineTime / 4.0);

	if (p->display_cfg->gpuvm_enable == true || p->setup_for_tdlut || dcc_mrq_enable) {
		s->Tr0_trips_rounded = math_ceil2(4.0 * *p->Tr0_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		*p->Tr0_trips_flip_rounded = math_ceil2(4.0 * *p->Tr0_trips_flip / s->LineTime, 1.0) / 4.0 * s->LineTime;
	} else {
		s->Tr0_trips_rounded = s->LineTime / 4.0;
		*p->Tr0_trips_flip_rounded = s->LineTime / 4.0;
	}
	s->Tr0_trips_rounded = math_max2(s->Tr0_trips_rounded, s->LineTime / 4.0);
	*p->Tr0_trips_flip_rounded = math_max2(*p->Tr0_trips_flip_rounded, s->LineTime / 4.0);

	if (p->display_cfg->gpuvm_enable == true) {
		if (p->display_cfg->gpuvm_max_page_table_levels >= 3) {
			*p->Tno_bw = p->ExtraLatencyPrefetch + s->trip_to_mem * (double)((p->display_cfg->gpuvm_max_page_table_levels - 2) * (s->HostVMDynamicLevelsTrips + 1));
		} else if (p->display_cfg->gpuvm_max_page_table_levels == 1 && !dcc_mrq_enable && !p->setup_for_tdlut) {
			*p->Tno_bw = p->ExtraLatencyPrefetch;
		} else {
			*p->Tno_bw = 0;
		}
	} else {
		*p->Tno_bw = 0;
	}

	if (p->mrq_present || p->display_cfg->gpuvm_max_page_table_levels >= 3)
		*p->Tno_bw_flip = *p->Tno_bw;
	else
		*p->Tno_bw_flip = 0; //because there is no 3DLUT for iFlip

	if (dml2_core_utils_is_420(p->myPipe->SourcePixelFormat)) {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC / 4.0;
	} else if (dml2_core_utils_is_422_planar(p->myPipe->SourcePixelFormat)) {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC / 2.0;
	} else {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC;
	}

	s->prefetch_bw_pr = s->bytes_pp * p->myPipe->PixelClock / (double)p->myPipe->DPPPerSurface;
	if (p->myPipe->VRatio < 1.0)
		s->prefetch_bw_pr = p->myPipe->VRatio * s->prefetch_bw_pr;
	s->max_Tsw = (math_max2(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) * s->LineTime);

	*p->prefetch_sw_bytes = p->PrefetchSourceLinesY * p->swath_width_luma_ub * p->myPipe->BytePerPixelY + p->PrefetchSourceLinesC * p->swath_width_chroma_ub * p->myPipe->BytePerPixelC;
	s->prefetch_bw_pr = s->prefetch_bw_pr;
	*p->prefetch_sw_bytes = *p->prefetch_sw_bytes;
	s->prefetch_bw_oto = math_max2(s->prefetch_bw_pr, *p->prefetch_sw_bytes / s->max_Tsw);

	s->min_Lsw_oto = math_max2(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) / __DML2_CALCS_MAX_VRATIO_PRE_OTO__;
	s->min_Lsw_oto = math_max2(s->min_Lsw_oto, 2.0);
	s->min_Lsw_oto = math_max2(s->min_Lsw_oto, p->tdlut_drain_time / s->LineTime);

	s->min_Lsw_equ = math_max2(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) / __DML2_CALCS_MAX_VRATIO_PRE_EQU__;
	s->min_Lsw_equ = math_max2(s->min_Lsw_equ, 2.0);
	s->min_Lsw_equ = math_max2(s->min_Lsw_equ, p->tdlut_drain_time / s->LineTime);

	vm_bytes = p->vm_bytes; // vm_bytes is dpde0_bytes_per_frame_ub_l + dpde0_bytes_per_frame_ub_c + 2*extra_dpde_bytes;
	extra_tdpe_bytes = (unsigned int)math_max2(0, (p->display_cfg->gpuvm_max_page_table_levels - 1) * 128);

	if (p->setup_for_tdlut)
		vm_bytes = vm_bytes + p->tdlut_pte_bytes_per_frame + (p->display_cfg->gpuvm_enable ? extra_tdpe_bytes : 0);

	tdlut_row_bytes = (unsigned long) math_ceil2(p->tdlut_bytes_per_frame/2.0, 1.0);

	s->prefetch_bw_oto = math_min2(s->prefetch_bw_oto, *p->prefetch_sw_bytes / (s->min_Lsw_oto * s->LineTime));

	s->Lsw_oto = math_ceil2(4.0 * *p->prefetch_sw_bytes / s->prefetch_bw_oto / s->LineTime, 1.0) / 4.0;
	s->prefetch_bw_oto = math_max3(s->prefetch_bw_oto,
		p->vm_bytes * p->HostVMInefficiencyFactor / (31 * s->LineTime) - *p->Tno_bw,
		(p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / (15 * s->LineTime));

	if (p->display_cfg->gpuvm_enable == true) {
		s->Tvm_oto = math_max3(
				*p->Tvm_trips,
				*p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw_oto,
				s->LineTime / 4.0);
		DML_LOG_VERBOSE("DML::%s: Tvm_oto max0 = %f\n", __func__, *p->Tvm_trips);
		DML_LOG_VERBOSE("DML::%s: Tvm_oto max1 = %f\n", __func__, *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw_oto);
		DML_LOG_VERBOSE("DML::%s: Tvm_oto max2 = %f\n", __func__, s->LineTime / 4.0);
	} else {
		s->Tvm_oto = s->Tvm_trips_rounded;
	}

	if ((p->display_cfg->gpuvm_enable == true || p->setup_for_tdlut || dcc_mrq_enable)) {
		s->Tr0_oto = math_max3(
				*p->Tr0_trips,
				(p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / s->prefetch_bw_oto,
				s->LineTime / 4.0);
		DML_LOG_VERBOSE("DML::%s: Tr0_oto max0 = %f\n", __func__, *p->Tr0_trips);
		DML_LOG_VERBOSE("DML::%s: Tr0_oto max1 = %f\n", __func__, (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / s->prefetch_bw_oto);
		DML_LOG_VERBOSE("DML::%s: Tr0_oto max2 = %f\n", __func__, s->LineTime / 4);
	} else
		s->Tr0_oto = s->LineTime / 4.0;

	s->Tvm_oto_lines = math_ceil2(4.0 * s->Tvm_oto / s->LineTime, 1) / 4.0;
	s->Tr0_oto_lines = math_ceil2(4.0 * s->Tr0_oto / s->LineTime, 1) / 4.0;
	s->dst_y_prefetch_oto = s->Tvm_oto_lines + 2 * s->Tr0_oto_lines + s->Lsw_oto;

	//To (time for delay after scaler) in line time
	Lo = (unsigned int)(*p->DSTYAfterScaler + (double)*p->DSTXAfterScaler / (double)p->myPipe->HTotal);

	//Tpre_equ in line time
	if (p->DynamicMetadataVMEnabled && p->DynamicMetadataEnable)
		s->dst_y_prefetch_equ = p->VStartup - (*p->TSetup + math_max2(p->TCalc, *p->Tvm_trips) + s->TWait_p) / s->LineTime - Lo;
	else
		s->dst_y_prefetch_equ = p->VStartup - (*p->TSetup + math_max2(p->TCalc, p->ExtraLatencyPrefetch) + s->TWait_p) / s->LineTime - Lo;
	s->dst_y_prefetch_equ = math_min2(s->dst_y_prefetch_equ, 63.75); // limit to the reg limit of U6.2 for DST_Y_PREFETCH

	DML_LOG_VERBOSE("DML::%s: HTotal = %u\n", __func__, p->myPipe->HTotal);
	DML_LOG_VERBOSE("DML::%s: min_Lsw_oto = %f\n", __func__, s->min_Lsw_oto);
	DML_LOG_VERBOSE("DML::%s: min_Lsw_equ = %f\n", __func__, s->min_Lsw_equ);
	DML_LOG_VERBOSE("DML::%s: Tno_bw = %f\n", __func__, *p->Tno_bw);
	DML_LOG_VERBOSE("DML::%s: Tno_bw_flip = %f\n", __func__, *p->Tno_bw_flip);
	DML_LOG_VERBOSE("DML::%s: ExtraLatencyPrefetch = %f\n", __func__, p->ExtraLatencyPrefetch);
	DML_LOG_VERBOSE("DML::%s: trip_to_mem = %f\n", __func__, s->trip_to_mem);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelY = %u\n", __func__, p->myPipe->BytePerPixelY);
	DML_LOG_VERBOSE("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
	DML_LOG_VERBOSE("DML::%s: swath_width_luma_ub = %u\n", __func__, p->swath_width_luma_ub);
	DML_LOG_VERBOSE("DML::%s: BytePerPixelC = %u\n", __func__, p->myPipe->BytePerPixelC);
	DML_LOG_VERBOSE("DML::%s: PrefetchSourceLinesC = %f\n", __func__, p->PrefetchSourceLinesC);
	DML_LOG_VERBOSE("DML::%s: swath_width_chroma_ub = %u\n", __func__, p->swath_width_chroma_ub);
	DML_LOG_VERBOSE("DML::%s: prefetch_sw_bytes = %f\n", __func__, *p->prefetch_sw_bytes);
	DML_LOG_VERBOSE("DML::%s: max_Tsw = %f\n", __func__, s->max_Tsw);
	DML_LOG_VERBOSE("DML::%s: bytes_pp = %f\n", __func__, s->bytes_pp);
	DML_LOG_VERBOSE("DML::%s: vm_bytes = %u\n", __func__, vm_bytes);
	DML_LOG_VERBOSE("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, p->PixelPTEBytesPerRow);
	DML_LOG_VERBOSE("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
	DML_LOG_VERBOSE("DML::%s: Tvm_trips = %f\n", __func__, *p->Tvm_trips);
	DML_LOG_VERBOSE("DML::%s: Tr0_trips = %f\n", __func__, *p->Tr0_trips);
	DML_LOG_VERBOSE("DML::%s: Tvm_trips_flip = %f\n", __func__, *p->Tvm_trips_flip);
	DML_LOG_VERBOSE("DML::%s: Tr0_trips_flip = %f\n", __func__, *p->Tr0_trips_flip);
	DML_LOG_VERBOSE("DML::%s: prefetch_bw_pr = %f\n", __func__, s->prefetch_bw_pr);
	DML_LOG_VERBOSE("DML::%s: prefetch_bw_oto = %f\n", __func__, s->prefetch_bw_oto);
	DML_LOG_VERBOSE("DML::%s: Tr0_oto = %f\n", __func__, s->Tr0_oto);
	DML_LOG_VERBOSE("DML::%s: Tvm_oto = %f\n", __func__, s->Tvm_oto);
	DML_LOG_VERBOSE("DML::%s: Tvm_oto_lines = %f\n", __func__, s->Tvm_oto_lines);
	DML_LOG_VERBOSE("DML::%s: Tr0_oto_lines = %f\n", __func__, s->Tr0_oto_lines);
	DML_LOG_VERBOSE("DML::%s: Lsw_oto = %f\n", __func__, s->Lsw_oto);
	DML_LOG_VERBOSE("DML::%s: dst_y_prefetch_oto = %f\n", __func__, s->dst_y_prefetch_oto);
	DML_LOG_VERBOSE("DML::%s: dst_y_prefetch_equ = %f\n", __func__, s->dst_y_prefetch_equ);
	DML_LOG_VERBOSE("DML::%s: tdlut_row_bytes = %d\n", __func__, tdlut_row_bytes);
	DML_LOG_VERBOSE("DML::%s: meta_row_bytes = %d\n", __func__, p->meta_row_bytes);
	s->dst_y_prefetch_equ = math_floor2(4.0 * (s->dst_y_prefetch_equ + 0.125), 1) / 4.0;
	*p->Tpre_rounded = s->dst_y_prefetch_equ * s->LineTime;

	DML_LOG_VERBOSE("DML::%s: dst_y_prefetch_equ: %f (after round)\n", __func__, s->dst_y_prefetch_equ);
	DML_LOG_VERBOSE("DML::%s: LineTime: %f\n", __func__, s->LineTime);
	DML_LOG_VERBOSE("DML::%s: VStartup: %u\n", __func__, p->VStartup);
	DML_LOG_VERBOSE("DML::%s: Tvstartup: %fus - time between vstartup and first pixel of active\n", __func__, p->VStartup * s->LineTime);
	DML_LOG_VERBOSE("DML::%s: TSetup: %fus - time from vstartup to vready\n", __func__, *p->TSetup);
	DML_LOG_VERBOSE("DML::%s: TCalc: %fus - time for calculations in dchub starting at vready\n", __func__, p->TCalc);
	DML_LOG_VERBOSE("DML::%s: TWait: %fus - time for fabric to become ready max(pstate exit,cstate enter/exit, urgent latency) after TCalc\n", __func__, p->TWait);
	DML_LOG_VERBOSE("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, s->Tdmbf);
	DML_LOG_VERBOSE("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, s->Tdmec);
	DML_LOG_VERBOSE("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, s->Tdmsks);
	DML_LOG_VERBOSE("DML::%s: TWait = %f\n", __func__, p->TWait);
	DML_LOG_VERBOSE("DML::%s: TWait_p = %f\n", __func__, s->TWait_p);
	DML_LOG_VERBOSE("DML::%s: Ttrip = %f\n", __func__, p->Ttrip);
	DML_LOG_VERBOSE("DML::%s: Tex = %f\n", __func__, p->ExtraLatencyPrefetch);
	DML_LOG_VERBOSE("DML::%s: Tdmdl_vm: %fus - time for vm stages of dmd \n", __func__, *p->Tdmdl_vm);
	DML_LOG_VERBOSE("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", __func__, *p->Tdmdl);
	DML_LOG_VERBOSE("DML::%s: TWait_p: %fus\n", __func__, s->TWait_p);
	DML_LOG_VERBOSE("DML::%s: Ttrip: %fus\n", __func__, p->Ttrip);
	DML_LOG_VERBOSE("DML::%s: DSTXAfterScaler: %u pixels - number of pixel clocks pipeline and buffer delay after scaler \n", __func__, *p->DSTXAfterScaler);
	DML_LOG_VERBOSE("DML::%s: DSTYAfterScaler: %u lines - number of lines of pipeline and buffer delay after scaler \n", __func__, *p->DSTYAfterScaler);
	DML_LOG_VERBOSE("DML::%s: vm_bytes: %f (hvm inefficiency scaled)\n", __func__, vm_bytes*p->HostVMInefficiencyFactor);
	DML_LOG_VERBOSE("DML::%s: row_bytes: %f (hvm inefficiency scaled, 1 row)\n", __func__, p->PixelPTEBytesPerRow*p->HostVMInefficiencyFactor+p->meta_row_bytes+tdlut_row_bytes);
	DML_LOG_VERBOSE("DML::%s: Tno_bw: %f\n", __func__, *p->Tno_bw);
	DML_LOG_VERBOSE("DML::%s: Tpre_rounded: %f\n", __func__, *p->Tpre_rounded);
	DML_LOG_VERBOSE("DML::%s: Tvm_trips=%f Tvm_trips_rounded: %f, delta=%f\n", __func__, *p->Tvm_trips, s->Tvm_trips_rounded, (s->Tvm_trips_rounded - *p->Tvm_trips));

	*p->dst_y_per_vm_vblank = 0;
	*p->dst_y_per_row_vblank = 0;
	*p->VRatioPrefetchY = 0;
	*p->VRatioPrefetchC = 0;
	*p->RequiredPrefetchPixelDataBWLuma = 0;

	// Derive bandwidth by finding how much data to move within the time constraint
	// Tpre_rounded is Tpre rounding to 2-bit fraction
	// Tvm_trips_rounded is Tvm_trips ceiling to 1/4 line time
	// Tr0_trips_rounded is Tr0_trips ceiling to 1/4 line time
	// So that means prefetch bw calculated can be higher since the total time availabe for prefetch is less
	bool min_Lsw_equ_ok = *p->Tpre_rounded >= s->Tvm_trips_rounded + 2.0*s->Tr0_trips_rounded + s->min_Lsw_equ*s->LineTime;

	if (s->dst_y_prefetch_equ > 1 && min_Lsw_equ_ok) {
		s->prefetch_bw1 = 0.;
		s->prefetch_bw2 = 0.;
		s->prefetch_bw3 = 0.;
		s->prefetch_bw4 = 0.;

		// prefetch_bw1: VM + 2*R0 + SW
		if (*p->Tpre_rounded - *p->Tno_bw > 0) {
			s->prefetch_bw1 = (vm_bytes * p->HostVMInefficiencyFactor
					+ 2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)
					+ *p->prefetch_sw_bytes)
								/ (*p->Tpre_rounded - *p->Tno_bw);
			s->Tsw_est1 = *p->prefetch_sw_bytes / s->prefetch_bw1;
		} else
			s->prefetch_bw1 = 0;

		DML_LOG_VERBOSE("DML::%s: prefetch_bw1: %f\n", __func__, s->prefetch_bw1);
		if ((s->Tsw_est1 < s->min_Lsw_equ * s->LineTime) && (*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw > 0)) {
			s->prefetch_bw1 = (vm_bytes * p->HostVMInefficiencyFactor + 2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)) /
					(*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw);
			DML_LOG_VERBOSE("DML::%s: vm and 2 rows bytes = %f\n", __func__, (vm_bytes * p->HostVMInefficiencyFactor + 2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)));
			DML_LOG_VERBOSE("DML::%s: Tpre_rounded = %f\n", __func__, *p->Tpre_rounded);
			DML_LOG_VERBOSE("DML::%s: minus term = %f\n", __func__, s->min_Lsw_equ * s->LineTime + 0.75 * s->LineTime + *p->Tno_bw);
			DML_LOG_VERBOSE("DML::%s: min_Lsw_equ = %f\n", __func__, s->min_Lsw_equ);
			DML_LOG_VERBOSE("DML::%s: LineTime = %f\n", __func__, s->LineTime);
			DML_LOG_VERBOSE("DML::%s: Tno_bw = %f\n", __func__, *p->Tno_bw);
			DML_LOG_VERBOSE("DML::%s: Time to fetch vm and 2 rows = %f\n", __func__, (*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw));
			DML_LOG_VERBOSE("DML::%s: prefetch_bw1: %f (updated)\n", __func__, s->prefetch_bw1);
		}

		// prefetch_bw2: VM + SW
		if (*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded > 0) {
			s->prefetch_bw2 = (vm_bytes * p->HostVMInefficiencyFactor + *p->prefetch_sw_bytes) /
					(*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded);
			s->Tsw_est2 = *p->prefetch_sw_bytes / s->prefetch_bw2;
		} else
			s->prefetch_bw2 = 0;

		DML_LOG_VERBOSE("DML::%s: prefetch_bw2: %f\n", __func__, s->prefetch_bw2);
		if ((s->Tsw_est2 < s->min_Lsw_equ * s->LineTime) && ((*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded - s->min_Lsw_equ * s->LineTime - 0.25 * s->LineTime) > 0)) {
			s->prefetch_bw2 = vm_bytes * p->HostVMInefficiencyFactor / (*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded - s->min_Lsw_equ * s->LineTime - 0.25 * s->LineTime);
			DML_LOG_VERBOSE("DML::%s: prefetch_bw2: %f (updated)\n", __func__, s->prefetch_bw2);
		}

		// prefetch_bw3: 2*R0 + SW
		if (*p->Tpre_rounded - s->Tvm_trips_rounded > 0) {
			s->prefetch_bw3 = (2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) + *p->prefetch_sw_bytes) /
					(*p->Tpre_rounded - s->Tvm_trips_rounded);
			s->Tsw_est3 = *p->prefetch_sw_bytes / s->prefetch_bw3;
		} else
			s->prefetch_bw3 = 0;

		DML_LOG_VERBOSE("DML::%s: prefetch_bw3: %f\n", __func__, s->prefetch_bw3);
		if ((s->Tsw_est3 < s->min_Lsw_equ * s->LineTime) && ((*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.5 * s->LineTime - s->Tvm_trips_rounded) > 0)) {
			s->prefetch_bw3 = (2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)) / (*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.5 * s->LineTime - s->Tvm_trips_rounded);
			DML_LOG_VERBOSE("DML::%s: prefetch_bw3: %f (updated)\n", __func__, s->prefetch_bw3);
		}

		// prefetch_bw4: SW
		if (*p->Tpre_rounded - s->Tvm_trips_rounded - 2 * s->Tr0_trips_rounded > 0)
			s->prefetch_bw4 = *p->prefetch_sw_bytes / (*p->Tpre_rounded - s->Tvm_trips_rounded - 2 * s->Tr0_trips_rounded);
		else
			s->prefetch_bw4 = 0;

		DML_LOG_VERBOSE("DML::%s: Tno_bw: %f\n", __func__, *p->Tno_bw);
		DML_LOG_VERBOSE("DML::%s: Tpre_rounded: %f\n", __func__, *p->Tpre_rounded);
		DML_LOG_VERBOSE("DML::%s: Tvm_trips=%f Tvm_trips_rounded: %f, delta=%f\n", __func__, *p->Tvm_trips, s->Tvm_trips_rounded, (s->Tvm_trips_rounded - *p->Tvm_trips));
		DML_LOG_VERBOSE("DML::%s: Tr0_trips=%f Tr0_trips_rounded: %f, delta=%f\n", __func__, *p->Tr0_trips, s->Tr0_trips_rounded, (s->Tr0_trips_rounded - *p->Tr0_trips));
		DML_LOG_VERBOSE("DML::%s: Tsw_est1: %f\n", __func__, s->Tsw_est1);
		DML_LOG_VERBOSE("DML::%s: Tsw_est2: %f\n", __func__, s->Tsw_est2);
		DML_LOG_VERBOSE("DML::%s: Tsw_est3: %f\n", __func__, s->Tsw_est3);
		DML_LOG_VERBOSE("DML::%s: prefetch_bw1: %f (final)\n", __func__, s->prefetch_bw1);
		DML_LOG_VERBOSE("DML::%s: prefetch_bw2: %f (final)\n", __func__, s->prefetch_bw2);
		DML_LOG_VERBOSE("DML::%s: prefetch_bw3: %f (final)\n", __func__, s->prefetch_bw3);
		DML_LOG_VERBOSE("DML::%s: prefetch_bw4: %f (final)\n", __func__, s->prefetch_bw4);
		{
			bool Case1OK = false;
			bool Case2OK = false;
			bool Case3OK = false;

			// get "equalized" bw among all stages (vm, r0, sw), so based is all 3 stages are just above the latency-based requirement
			// so it is not too dis-portionally favor a particular stage, next is either r0 more agressive and next is vm more agressive, the worst is all are agressive
			// vs the latency based number

			// prefetch_bw1: VM + 2*R0 + SW
			// so prefetch_bw1 will have enough bw to transfer the necessary data within Tpre_rounded - Tno_bw (Tpre is the the worst-case latency based time to fetch the data)
			// here is to make sure equ bw wont be more agressive than the latency-based requirement.
			// check vm time >= vm_trips
			// check r0 time >= r0_trips

			double total_row_bytes = (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes);

			DML_LOG_VERBOSE("DML::%s: Tvm_trips_rounded = %f\n", __func__, s->Tvm_trips_rounded);
			DML_LOG_VERBOSE("DML::%s: Tr0_trips_rounded = %f\n", __func__, s->Tr0_trips_rounded);

			if (s->prefetch_bw1 > 0) {
				double vm_transfer_time = *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw1;
				double row_transfer_time = total_row_bytes / s->prefetch_bw1;
				DML_LOG_VERBOSE("DML::%s: Case1: vm_transfer_time  = %f\n", __func__, vm_transfer_time);
				DML_LOG_VERBOSE("DML::%s: Case1: row_transfer_time = %f\n", __func__, row_transfer_time);
				if (vm_transfer_time >= s->Tvm_trips_rounded && row_transfer_time >= s->Tr0_trips_rounded) {
					Case1OK = true;
				}
			}

			// prefetch_bw2: VM + SW
			// prefetch_bw2 will be enough bw to transfer VM and SW data within (Tpre_rounded - Tr0_trips_rounded - Tno_bw)
			// check vm time >= vm_trips
			// check r0 time < r0_trips
			if (s->prefetch_bw2 > 0) {
				double vm_transfer_time = *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw2;
				double row_transfer_time = total_row_bytes / s->prefetch_bw2;
				DML_LOG_VERBOSE("DML::%s: Case2: vm_transfer_time  = %f\n", __func__, vm_transfer_time);
				DML_LOG_VERBOSE("DML::%s: Case2: row_transfer_time = %f\n", __func__, row_transfer_time);
				if (vm_transfer_time >= s->Tvm_trips_rounded && row_transfer_time < s->Tr0_trips_rounded) {
					Case2OK = true;
				}
			}

			// prefetch_bw3: VM + 2*R0
			// check vm time < vm_trips
			// check r0 time >= r0_trips
			if (s->prefetch_bw3 > 0) {
				double vm_transfer_time = *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw3;
				double row_transfer_time = total_row_bytes / s->prefetch_bw3;
				DML_LOG_VERBOSE("DML::%s: Case3: vm_transfer_time  = %f\n", __func__, vm_transfer_time);
				DML_LOG_VERBOSE("DML::%s: Case3: row_transfer_time = %f\n", __func__, row_transfer_time);
				if (vm_transfer_time < s->Tvm_trips_rounded && row_transfer_time >= s->Tr0_trips_rounded) {
					Case3OK = true;
				}
			}

			if (Case1OK) {
				s->prefetch_bw_equ = s->prefetch_bw1;
			} else if (Case2OK) {
				s->prefetch_bw_equ = s->prefetch_bw2;
			} else if (Case3OK) {
				s->prefetch_bw_equ = s->prefetch_bw3;
			} else {
				s->prefetch_bw_equ = s->prefetch_bw4;
			}

			s->prefetch_bw_equ = math_max3(s->prefetch_bw_equ,
					p->vm_bytes * p->HostVMInefficiencyFactor / (31 * s->LineTime) - *p->Tno_bw,
					(p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / (15 * s->LineTime));
			DML_LOG_VERBOSE("DML::%s: Case1OK: %u\n", __func__, Case1OK);
			DML_LOG_VERBOSE("DML::%s: Case2OK: %u\n", __func__, Case2OK);
			DML_LOG_VERBOSE("DML::%s: Case3OK: %u\n", __func__, Case3OK);
			DML_LOG_VERBOSE("DML::%s: prefetch_bw_equ: %f\n", __func__, s->prefetch_bw_equ);

			if (s->prefetch_bw_equ > 0) {
				if (p->display_cfg->gpuvm_enable == true) {
					s->Tvm_equ = math_max3(*p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw_equ, *p->Tvm_trips, s->LineTime / 4);
				} else {
					s->Tvm_equ = s->LineTime / 4;
				}

				if (p->display_cfg->gpuvm_enable == true || dcc_mrq_enable || p->setup_for_tdlut) {
					s->Tr0_equ = math_max3((p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / s->prefetch_bw_equ, // PixelPTEBytesPerRow is dpte_row_bytes
							*p->Tr0_trips,
							s->LineTime / 4);
				} else {
					s->Tr0_equ = s->LineTime / 4;
				}
			} else {
				s->Tvm_equ = 0;
				s->Tr0_equ = 0;
				DML_LOG_VERBOSE("DML::%s: prefetch_bw_equ equals 0!\n", __func__);
			}
		}
		DML_LOG_VERBOSE("DML::%s: Tvm_equ = %f\n", __func__, s->Tvm_equ);
		DML_LOG_VERBOSE("DML::%s: Tr0_equ = %f\n", __func__, s->Tr0_equ);

		s->LinesToRequestPrefetchPixelData = s->dst_y_prefetch_equ -
			(math_ceil2(4.0 * s->Tvm_equ / s->LineTime, 1.0) / 4.0) -
			2 * (math_ceil2(4.0 * s->Tr0_equ / s->LineTime, 1.0) / 4.0);
		if (s->dst_y_prefetch_oto < s->dst_y_prefetch_equ && !(p->use_max_lsw && s->min_Lsw_oto < s->LinesToRequestPrefetchPixelData)) {
			*p->dst_y_prefetch = s->dst_y_prefetch_oto;
			s->TimeForFetchingVM = s->Tvm_oto;
			s->TimeForFetchingRowInVBlank = s->Tr0_oto;
			DML_LOG_VERBOSE("DML::%s: Using oto scheduling for prefetch\n", __func__);
		} else {
			*p->dst_y_prefetch = s->dst_y_prefetch_equ;
			s->TimeForFetchingVM = s->Tvm_equ;
			s->TimeForFetchingRowInVBlank = s->Tr0_equ;
			DML_LOG_VERBOSE("DML::%s: Using equ scheduling for prefetch\n", __func__);
		}

		*p->dst_y_per_vm_vblank = math_ceil2(4.0 * s->TimeForFetchingVM / s->LineTime, 1.0) / 4.0;
		*p->dst_y_per_row_vblank = math_ceil2(4.0 * s->TimeForFetchingRowInVBlank / s->LineTime, 1.0) / 4.0;

		s->LinesToRequestPrefetchPixelData = *p->dst_y_prefetch - *p->dst_y_per_vm_vblank - 2 * *p->dst_y_per_row_vblank;

		s->cursor_prefetch_bytes = (unsigned int)math_max2(p->cursor_bytes_per_chunk, 4 * p->cursor_bytes_per_line);
		*p->prefetch_cursor_bw = p->num_cursors * s->cursor_prefetch_bytes / (s->LinesToRequestPrefetchPixelData * s->LineTime);

		DML_LOG_VERBOSE("DML::%s: TimeForFetchingVM = %f\n", __func__, s->TimeForFetchingVM);
		DML_LOG_VERBOSE("DML::%s: TimeForFetchingRowInVBlank = %f\n", __func__, s->TimeForFetchingRowInVBlank);
		DML_LOG_VERBOSE("DML::%s: LineTime = %f\n", __func__, s->LineTime);
		DML_LOG_VERBOSE("DML::%s: dst_y_prefetch = %f\n", __func__, *p->dst_y_prefetch);
		DML_LOG_VERBOSE("DML::%s: dst_y_per_vm_vblank = %f\n", __func__, *p->dst_y_per_vm_vblank);
		DML_LOG_VERBOSE("DML::%s: dst_y_per_row_vblank = %f\n", __func__, *p->dst_y_per_row_vblank);
		DML_LOG_VERBOSE("DML::%s: LinesToRequestPrefetchPixelData = %f\n", __func__, s->LinesToRequestPrefetchPixelData);
		DML_LOG_VERBOSE("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);

		DML_LOG_VERBOSE("DML::%s: cursor_bytes_per_chunk = %d\n", __func__, p->cursor_bytes_per_chunk);
		DML_LOG_VERBOSE("DML::%s: cursor_bytes_per_line = %d\n", __func__, p->cursor_bytes_per_line);
		DML_LOG_VERBOSE("DML::%s: cursor_prefetch_bytes = %d\n", __func__, s->cursor_prefetch_bytes);
		DML_LOG_VERBOSE("DML::%s: prefetch_cursor_bw = %f\n", __func__, *p->prefetch_cursor_bw);
		DML_ASSERT(*p->dst_y_prefetch < 64);

		unsigned int min_lsw_required = (unsigned int)math_max2(2, p->tdlut_drain_time / s->LineTime);
		if (s->LinesToRequestPrefetchPixelData >= min_lsw_required && s->prefetch_bw_equ > 0) {
			*p->VRatioPrefetchY = (double)p->PrefetchSourceLinesY / s->LinesToRequestPrefetchPixelData;
			*p->VRatioPrefetchY = math_max2(*p->VRatioPrefetchY, 1.0);
			DML_LOG_VERBOSE("DML::%s: VRatioPrefetchY = %f\n", __func__, *p->VRatioPrefetchY);
			DML_LOG_VERBOSE("DML::%s: SwathHeightY = %u\n", __func__, p->SwathHeightY);
			DML_LOG_VERBOSE("DML::%s: VInitPreFillY = %u\n", __func__, p->VInitPreFillY);
			if ((p->SwathHeightY > 4) && (p->VInitPreFillY > 3)) {
				if (s->LinesToRequestPrefetchPixelData > (p->VInitPreFillY - 3.0) / 2.0) {
					*p->VRatioPrefetchY = math_max2(*p->VRatioPrefetchY,
							(double)p->MaxNumSwathY * p->SwathHeightY / (s->LinesToRequestPrefetchPixelData - (p->VInitPreFillY - 3.0) / 2.0));
				} else {
					s->NoTimeToPrefetch = true;
					DML_LOG_VERBOSE("DML::%s: No time to prefetch!. LinesToRequestPrefetchPixelData=%f VinitPreFillY=%u\n", __func__, s->LinesToRequestPrefetchPixelData, p->VInitPreFillY);
					*p->VRatioPrefetchY = 0;
				}
				DML_LOG_VERBOSE("DML::%s: VRatioPrefetchY = %f\n", __func__, *p->VRatioPrefetchY);
				DML_LOG_VERBOSE("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
				DML_LOG_VERBOSE("DML::%s: MaxNumSwathY = %u\n", __func__, p->MaxNumSwathY);
			}

			*p->VRatioPrefetchC = (double)p->PrefetchSourceLinesC / s->LinesToRequestPrefetchPixelData;
			*p->VRatioPrefetchC = math_max2(*p->VRatioPrefetchC, 1.0);

			DML_LOG_VERBOSE("DML::%s: VRatioPrefetchC = %f\n", __func__, *p->VRatioPrefetchC);
			DML_LOG_VERBOSE("DML::%s: SwathHeightC = %u\n", __func__, p->SwathHeightC);
			DML_LOG_VERBOSE("DML::%s: VInitPreFillC = %u\n", __func__, p->VInitPreFillC);
			if ((p->SwathHeightC > 4) && (p->VInitPreFillC > 3)) {
				if (s->LinesToRequestPrefetchPixelData > (p->VInitPreFillC - 3.0) / 2.0) {
					*p->VRatioPrefetchC = math_max2(*p->VRatioPrefetchC, (double)p->MaxNumSwathC * p->SwathHeightC / (s->LinesToRequestPrefetchPixelData - (p->VInitPreFillC - 3.0) / 2.0));
				} else {
					s->NoTimeToPrefetch = true;
					DML_LOG_VERBOSE("DML::%s: No time to prefetch!. LinesToRequestPrefetchPixelData=%f VInitPreFillC=%u\n", __func__, s->LinesToRequestPrefetchPixelData, p->VInitPreFillC);
					*p->VRatioPrefetchC = 0;
				}
				DML_LOG_VERBOSE("DML::%s: VRatioPrefetchC = %f\n", __func__, *p->VRatioPrefetchC);
				DML_LOG_VERBOSE("DML::%s: PrefetchSourceLinesC = %f\n", __func__, p->PrefetchSourceLinesC);
				DML_LOG_VERBOSE("DML::%s: MaxNumSwathC = %u\n", __func__, p->MaxNumSwathC);
			}

			*p->RequiredPrefetchPixelDataBWLuma = (double)p->PrefetchSourceLinesY / s->LinesToRequestPrefetchPixelData * p->myPipe->BytePerPixelY * p->swath_width_luma_ub / s->LineTime;
			*p->RequiredPrefetchPixelDataBWChroma = (double)p->PrefetchSourceLinesC / s->LinesToRequestPrefetchPixelData * p->myPipe->BytePerPixelC * p->swath_width_chroma_ub / s->LineTime;

			DML_LOG_VERBOSE("DML::%s: BytePerPixelY = %u\n", __func__, p->myPipe->BytePerPixelY);
			DML_LOG_VERBOSE("DML::%s: swath_width_luma_ub = %u\n", __func__, p->swath_width_luma_ub);
			DML_LOG_VERBOSE("DML::%s: LineTime = %f\n", __func__, s->LineTime);
			DML_LOG_VERBOSE("DML::%s: RequiredPrefetchPixelDataBWLuma = %f\n", __func__, *p->RequiredPrefetchPixelDataBWLuma);
			DML_LOG_VERBOSE("DML::%s: RequiredPrefetchPixelDataBWChroma = %f\n", __func__, *p->RequiredPrefetchPixelDataBWChroma);
		} else {
			s->NoTimeToPrefetch = true;
			DML_LOG_VERBOSE("DML::%s: No time to prefetch!, LinesToRequestPrefetchPixelData: %f, should be >= %d\n", __func__, s->LinesToRequestPrefetchPixelData, min_lsw_required);
			DML_LOG_VERBOSE("DML::%s: No time to prefetch!, prefetch_bw_equ: %f, should be > 0\n", __func__, s->prefetch_bw_equ);
			*p->VRatioPrefetchY = 0;
			*p->VRatioPrefetchC = 0;
			*p->RequiredPrefetchPixelDataBWLuma = 0;
			*p->RequiredPrefetchPixelDataBWChroma = 0;
		}

		DML_LOG_VERBOSE("DML: Tpre: %fus - sum of time to request 2 x data pte, swaths\n", (double)s->LinesToRequestPrefetchPixelData * s->LineTime + 2.0 * s->TimeForFetchingRowInVBlank + s->TimeForFetchingVM);
		DML_LOG_VERBOSE("DML: Tvm: %fus - time to fetch vm\n", s->TimeForFetchingVM);
		DML_LOG_VERBOSE("DML: Tr0: %fus - time to fetch first row of data pagetables\n", s->TimeForFetchingRowInVBlank);
		DML_LOG_VERBOSE("DML: Tsw: %fus = time to fetch enough pixel data and cursor data to feed the scalers init position and detile\n", (double)s->LinesToRequestPrefetchPixelData * s->LineTime);
		DML_LOG_VERBOSE("DML: To: %fus - time for propagation from scaler to optc\n", (*p->DSTYAfterScaler + ((double)(*p->DSTXAfterScaler) / (double)p->myPipe->HTotal)) * s->LineTime);
		DML_LOG_VERBOSE("DML: Tvstartup - TSetup - Tcalc - TWait - Tpre - To > 0\n");
		DML_LOG_VERBOSE("DML: Tslack(pre): %fus - time left over in schedule\n", p->VStartup * s->LineTime - s->TimeForFetchingVM - 2 * s->TimeForFetchingRowInVBlank - (*p->DSTYAfterScaler + ((double)(*p->DSTXAfterScaler) / (double)p->myPipe->HTotal)) * s->LineTime - p->TWait - p->TCalc - *p->TSetup);
		DML_LOG_VERBOSE("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %u\n", p->PixelPTEBytesPerRow);

	} else {
		DML_LOG_VERBOSE("DML::%s: No time to prefetch! dst_y_prefetch_equ = %f (should be > 1)\n", __func__, s->dst_y_prefetch_equ);
		DML_LOG_VERBOSE("DML::%s: No time to prefetch! Tpre_rounded (%f) should be >= Tvm_trips_rounded (%f)  + 2.0*Tr0_trips_rounded (%f) + min_Tsw_equ (%f)\n",
				__func__, *p->Tpre_rounded, s->Tvm_trips_rounded, 2.0*s->Tr0_trips_rounded, s->min_Lsw_equ*s->LineTime);
		s->NoTimeToPrefetch = true;
		s->TimeForFetchingVM = 0;
		s->TimeForFetchingRowInVBlank = 0;
		*p->dst_y_per_vm_vblank = 0;
		*p->dst_y_per_row_vblank = 0;
		s->LinesToRequestPrefetchPixelData = 0;
		*p->VRatioPrefetchY = 0;
		*p->VRatioPrefetchC = 0;
		*p->RequiredPrefetchPixelDataBWLuma = 0;
		*p->RequiredPrefetchPixelDataBWChroma = 0;
	}

	{
		double prefetch_vm_bw;
		double prefetch_row_bw;

		if (vm_bytes == 0) {
			prefetch_vm_bw = 0;
		} else if (*p->dst_y_per_vm_vblank > 0) {
			DML_LOG_VERBOSE("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
			DML_LOG_VERBOSE("DML::%s: dst_y_per_vm_vblank = %f\n", __func__, *p->dst_y_per_vm_vblank);
			DML_LOG_VERBOSE("DML::%s: LineTime = %f\n", __func__, s->LineTime);
			prefetch_vm_bw = vm_bytes * p->HostVMInefficiencyFactor / (*p->dst_y_per_vm_vblank * s->LineTime);
			DML_LOG_VERBOSE("DML::%s: prefetch_vm_bw = %f\n", __func__, prefetch_vm_bw);
		} else {
			prefetch_vm_bw = 0;
			s->NoTimeToPrefetch = true;
			DML_LOG_VERBOSE("DML::%s: No time to prefetch!. dst_y_per_vm_vblank=%f (should be > 0)\n", __func__, *p->dst_y_per_vm_vblank);
		}

		if (p->PixelPTEBytesPerRow == 0 && tdlut_row_bytes == 0) {
			prefetch_row_bw = 0;
		} else if (*p->dst_y_per_row_vblank > 0) {
			prefetch_row_bw = (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + tdlut_row_bytes) / (*p->dst_y_per_row_vblank * s->LineTime);

			DML_LOG_VERBOSE("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, p->PixelPTEBytesPerRow);
			DML_LOG_VERBOSE("DML::%s: dst_y_per_row_vblank = %f\n", __func__, *p->dst_y_per_row_vblank);
			DML_LOG_VERBOSE("DML::%s: prefetch_row_bw = %f\n", __func__, prefetch_row_bw);
		} else {
			prefetch_row_bw = 0;
			s->NoTimeToPrefetch = true;
			DML_LOG_VERBOSE("DML::%s: No time to prefetch!. dst_y_per_row_vblank=%f (should be > 0)\n", __func__, *p->dst_y_per_row_vblank);
		}

		*p->prefetch_vmrow_bw = math_max2(prefetch_vm_bw, prefetch_row_bw);
	}

	if (s->NoTimeToPrefetch) {
		s->TimeForFetchingVM = 0;
		s->TimeForFetchingRowInVBlank = 0;
		*p->dst_y_per_vm_vblank = 0;
		*p->dst_y_per_row_vblank = 0;
		*p->dst_y_prefetch = 0;
		s->LinesToRequestPrefetchPixelData = 0;
		*p->VRatioPrefetchY = 0;
		*p->VRatioPrefetchC = 0;
		*p->RequiredPrefetchPixelDataBWLuma = 0;
		*p->RequiredPrefetchPixelDataBWChroma = 0;
		*p->prefetch_vmrow_bw = 0;
	}

	DML_LOG_VERBOSE("DML::%s: dst_y_per_vm_vblank = %f (final)\n", __func__, *p->dst_y_per_vm_vblank);
	DML_LOG_VERBOSE("DML::%s: dst_y_per_row_vblank = %f (final)\n", __func__, *p->dst_y_per_row_vblank);
	DML_LOG_VERBOSE("DML::%s: prefetch_vmrow_bw = %f (final)\n", __func__, *p->prefetch_vmrow_bw);
	DML_LOG_VERBOSE("DML::%s: RequiredPrefetchPixelDataBWLuma = %f (final)\n", __func__, *p->RequiredPrefetchPixelDataBWLuma);
	DML_LOG_VERBOSE("DML::%s: RequiredPrefetchPixelDataBWChroma = %f (final)\n", __func__, *p->RequiredPrefetchPixelDataBWChroma);
	DML_LOG_VERBOSE("DML::%s: NoTimeToPrefetch=%d\n", __func__, s->NoTimeToPrefetch);
	return s->NoTimeToPrefetch;
}

static double dcn5_calculate_urgent_bandwidth_required(
		struct dml2_core_shared_get_urgent_bandwidth_required_locals *l,
		const struct dml2_display_cfg *display_cfg,
		bool inc_flip_bw, // including flip bw
		bool use_qual_row_bw,
		unsigned int NumberOfActiveSurfaces,
		unsigned int NumberOfDPP[],
		double dcc_dram_bw_nom_overhead_factor_p0[],
		double dcc_dram_bw_nom_overhead_factor_p1[],
		double dcc_dram_bw_pref_overhead_factor_p0[],
		double dcc_dram_bw_pref_overhead_factor_p1[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double PrefetchBandwidthLuma[],
		double PrefetchBandwidthChroma[],
		double excess_vactive_fill_bw_l[],
		double excess_vactive_fill_bw_c[],
		double cursor_bw[],
		double dpte_row_bw[],
		double meta_row_bw[],
		double prefetch_cursor_bw[],
		double prefetch_vmrow_bw[],
		double flip_bw[],
		double UrgentBurstFactorLuma[],
		double UrgentBurstFactorChroma[],
		double UrgentBurstFactorCursor[],
		double UrgentBurstFactorLumaPre[],
		double UrgentBurstFactorChromaPre[],
		double UrgentBurstFactorCursorPre[],
		/* outputs */
		double surface_required_bw[],
		double surface_peak_required_bw[])
{
	// set inc_flip_bw = 0 for total_dchub_urgent_read_bw_noflip calculation, 1 for total_dchub_urgent_read_bw as described in the MAS
	// set use_qual_row_bw = 1 to calculate using qualified row bandwidth, used for total_flip_bw calculation

	memset(l, 0, sizeof(struct dml2_core_shared_get_urgent_bandwidth_required_locals));

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		l->adj_factor_p0 = UrgentBurstFactorLuma[k] * dcc_dram_bw_nom_overhead_factor_p0[k];
		l->adj_factor_p1 = UrgentBurstFactorChroma[k] * dcc_dram_bw_nom_overhead_factor_p1[k];
		l->adj_factor_cur = UrgentBurstFactorCursor[k];
		l->adj_factor_p0_pre = UrgentBurstFactorLumaPre[k] * dcc_dram_bw_pref_overhead_factor_p0[k];
		l->adj_factor_p1_pre = UrgentBurstFactorChromaPre[k] * dcc_dram_bw_pref_overhead_factor_p1[k];
		l->adj_factor_cur_pre = UrgentBurstFactorCursorPre[k];

		// The qualified row bandwidth, qual_row_bw, accounts for the regular non-flip row bandwidth when there is no possible immediate flip or HostVM invalidation flip.
		// The qual_row_bw is zero if HostVM is possible and only non-zero and equal to row_bw(i) if immediate flip is not allowed for that pipe.
		if (use_qual_row_bw) {
			if (display_cfg->hostvm_enable)
				l->per_plane_flip_bw[k] = 0; // qual_row_bw
			else if (!display_cfg->plane_descriptors[k].immediate_flip)
				l->per_plane_flip_bw[k] = NumberOfDPP[k] * (dpte_row_bw[k] + meta_row_bw[k]);
		} else {
			// the final_flip_bw includes the regular row_bw when immediate flip is disallowed (and no HostVM)
			if ((!display_cfg->plane_descriptors[k].immediate_flip && !display_cfg->hostvm_enable) || !inc_flip_bw)
				l->per_plane_flip_bw[k] = NumberOfDPP[k] * (dpte_row_bw[k] + meta_row_bw[k]);
			else
				l->per_plane_flip_bw[k] = NumberOfDPP[k] * flip_bw[k];
		}

		l->vm_row_bw = NumberOfDPP[k] * prefetch_vmrow_bw[k];
		l->flip_and_active_bw = l->per_plane_flip_bw[k]
			+ ReadBandwidthLuma[k] * l->adj_factor_p0
			+ ReadBandwidthChroma[k] * l->adj_factor_p1
			+ cursor_bw[k] * l->adj_factor_cur;
		l->flip_and_prefetch_bw = l->per_plane_flip_bw[k]
			+ NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * l->adj_factor_p0_pre + PrefetchBandwidthChroma[k] * l->adj_factor_p1_pre)
			+ prefetch_cursor_bw[k] * l->adj_factor_cur_pre;
		l->active_and_excess_bw = (ReadBandwidthLuma[k] + excess_vactive_fill_bw_l[k]) * dcc_dram_bw_nom_overhead_factor_p0[k]
			+ (ReadBandwidthChroma[k] + excess_vactive_fill_bw_c[k]) * dcc_dram_bw_nom_overhead_factor_p1[k]
			+ dpte_row_bw[k] + meta_row_bw[k];

		surface_required_bw[k] = math_max4(l->vm_row_bw, l->flip_and_active_bw, l->flip_and_prefetch_bw, l->active_and_excess_bw);

		/* export peak required bandwidth for the surface */
		surface_peak_required_bw[k] = math_max2(surface_required_bw[k], surface_peak_required_bw[k]);

		DML_LOG_VERBOSE("DML::%s: k=%d, max1: vm_row_bw=%f\n", __func__, k, l->vm_row_bw);
		DML_LOG_VERBOSE("DML::%s: k=%d, max2: flip_and_active_bw=%f\n", __func__, k, l->flip_and_active_bw);
		DML_LOG_VERBOSE("DML::%s: k=%d, max3: flip_and_prefetch_bw=%f\n", __func__, k, l->flip_and_prefetch_bw);
		DML_LOG_VERBOSE("DML::%s: k=%d, max4: active_and_excess_bw=%f\n", __func__, k, l->active_and_excess_bw);
		DML_LOG_VERBOSE("DML::%s: k=%d, surface_required_bw=%f\n", __func__, k, surface_required_bw[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, surface_peak_required_bw=%f\n", __func__, k, surface_peak_required_bw[k]);

		l->required_bandwidth_mbps += surface_required_bw[k];

		DML_LOG_VERBOSE("DML::%s: k=%d, NumberOfDPP=%d\n", __func__, k, NumberOfDPP[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, use_qual_row_bw=%d\n", __func__, k, use_qual_row_bw);
		DML_LOG_VERBOSE("DML::%s: k=%d, immediate_flip=%d\n", __func__, k, display_cfg->plane_descriptors[k].immediate_flip);
		DML_LOG_VERBOSE("DML::%s: k=%d, adj_factor_p0=%f\n", __func__, k, l->adj_factor_p0);
		DML_LOG_VERBOSE("DML::%s: k=%d, adj_factor_p1=%f\n", __func__, k, l->adj_factor_p1);
		DML_LOG_VERBOSE("DML::%s: k=%d, adj_factor_cur=%f\n", __func__, k, l->adj_factor_cur);

		DML_LOG_VERBOSE("DML::%s: k=%d, adj_factor_p0_pre=%f\n", __func__, k, l->adj_factor_p0_pre);
		DML_LOG_VERBOSE("DML::%s: k=%d, adj_factor_p1_pre=%f\n", __func__, k, l->adj_factor_p1_pre);
		DML_LOG_VERBOSE("DML::%s: k=%d, adj_factor_cur_pre=%f\n", __func__, k, l->adj_factor_cur_pre);

		DML_LOG_VERBOSE("DML::%s: k=%d, per_plane_flip_bw=%f\n", __func__, k, l->per_plane_flip_bw[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, prefetch_vmrow_bw=%f\n", __func__, k, prefetch_vmrow_bw[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, ReadBandwidthLuma=%f\n", __func__, k, ReadBandwidthLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, ReadBandwidthChroma=%f\n", __func__, k, ReadBandwidthChroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, excess_vactive_fill_bw_l=%f\n", __func__, k, excess_vactive_fill_bw_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, excess_vactive_fill_bw_c=%f\n", __func__, k, excess_vactive_fill_bw_c[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, cursor_bw=%f\n", __func__, k, cursor_bw[k]);

		DML_LOG_VERBOSE("DML::%s: k=%d, meta_row_bw=%f\n", __func__, k, meta_row_bw[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, dpte_row_bw=%f\n", __func__, k, dpte_row_bw[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, PrefetchBandwidthLuma=%f\n", __func__, k, PrefetchBandwidthLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, PrefetchBandwidthChroma=%f\n", __func__, k, PrefetchBandwidthChroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, prefetch_cursor_bw=%f\n", __func__, k, prefetch_cursor_bw[k]);
	}

	return l->required_bandwidth_mbps;
}

void dcn5_calculate_peak_bandwidth_required(
		struct dml2_core_internal_scratch *s,
		struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p)
{
	struct dml2_core_shared_calculate_peak_bandwidth_required_locals *l = &s->calculate_peak_bandwidth_required_locals;

	memset(l, 0, sizeof(struct dml2_core_shared_calculate_peak_bandwidth_required_locals));

	DML_LOG_VERBOSE("DML::%s: inc_flip_bw = %d\n", __func__, p->inc_flip_bw);
	DML_LOG_VERBOSE("DML::%s: NumberOfActiveSurfaces = %d\n", __func__, p->num_active_planes);

	for (unsigned int k = 0; k < p->num_active_planes; ++k) {
		l->unity_array[k] = 1.0;
		l->zero_array[k] = 0.0;
	}

	dcn5_calculate_urgent_bandwidth_required(
			&s->get_urgent_bandwidth_required_locals,
			p->display_cfg,
			0, //inc_flip_bw,
			0, //use_qual_row_bw
			p->num_active_planes,
			p->num_of_dpp,
			p->dcc_dram_bw_nom_overhead_factor_p0,
			p->dcc_dram_bw_nom_overhead_factor_p1,
			p->dcc_dram_bw_pref_overhead_factor_p0,
			p->dcc_dram_bw_pref_overhead_factor_p1,
			p->surface_read_bandwidth_l,
			p->surface_read_bandwidth_c,
			l->zero_array, //PrefetchBandwidthLuma,
			l->zero_array, //PrefetchBandwidthChroma,
			l->zero_array,
			l->zero_array,
			l->zero_array,
			p->dpte_row_bw,
			p->meta_row_bw,
			l->zero_array, //prefetch_cursor_bw,
			l->zero_array, //prefetch_vmrow_bw,
			l->zero_array, //flip_bw,
			l->zero_array,
			l->zero_array,
			l->zero_array,
			l->zero_array,
			l->zero_array,
			l->zero_array,
			**p->surface_avg_vactive_required_bw,
			l->surface_dummy_bw);

	**p->urg_bandwidth_required = dcn5_calculate_urgent_bandwidth_required(
			&s->get_urgent_bandwidth_required_locals,
			p->display_cfg,
			p->inc_flip_bw,
			0, //use_qual_row_bw
			p->num_active_planes,
			p->num_of_dpp,
			p->dcc_dram_bw_nom_overhead_factor_p0,
			p->dcc_dram_bw_nom_overhead_factor_p1,
			p->dcc_dram_bw_pref_overhead_factor_p0,
			p->dcc_dram_bw_pref_overhead_factor_p1,
			p->surface_read_bandwidth_l,
			p->surface_read_bandwidth_c,
			p->prefetch_bandwidth_l,
			p->prefetch_bandwidth_c,
			p->excess_vactive_fill_bw_l,
			p->excess_vactive_fill_bw_c,
			p->cursor_bw,
			p->dpte_row_bw,
			p->meta_row_bw,
			p->prefetch_cursor_bw,
			p->prefetch_vmrow_bw,
			p->flip_bw,
			p->urgent_burst_factor_l,
			p->urgent_burst_factor_c,
			p->urgent_burst_factor_cursor,
			p->urgent_burst_factor_prefetch_l,
			p->urgent_burst_factor_prefetch_c,
			p->urgent_burst_factor_prefetch_cursor,
			l->surface_dummy_bw,
			l->surface_dummy_bw);

	**p->urg_bandwidth_required_qual = dcn5_calculate_urgent_bandwidth_required(
			&s->get_urgent_bandwidth_required_locals,
			p->display_cfg,
			0, //inc_flip_bw
			1, //use_qual_row_bw
			p->num_active_planes,
			p->num_of_dpp,
			p->dcc_dram_bw_nom_overhead_factor_p0,
			p->dcc_dram_bw_nom_overhead_factor_p1,
			p->dcc_dram_bw_pref_overhead_factor_p0,
			p->dcc_dram_bw_pref_overhead_factor_p1,
			p->surface_read_bandwidth_l,
			p->surface_read_bandwidth_c,
			p->prefetch_bandwidth_l,
			p->prefetch_bandwidth_c,
			p->excess_vactive_fill_bw_l,
			p->excess_vactive_fill_bw_c,
			p->cursor_bw,
			p->dpte_row_bw,
			p->meta_row_bw,
			p->prefetch_cursor_bw,
			p->prefetch_vmrow_bw,
			p->flip_bw,
			p->urgent_burst_factor_l,
			p->urgent_burst_factor_c,
			p->urgent_burst_factor_cursor,
			p->urgent_burst_factor_prefetch_l,
			p->urgent_burst_factor_prefetch_c,
			p->urgent_burst_factor_prefetch_cursor,
			l->surface_dummy_bw,
			l->surface_dummy_bw);

	**p->non_urg_bandwidth_required = dcn5_calculate_urgent_bandwidth_required(
			&s->get_urgent_bandwidth_required_locals,
			p->display_cfg,
			p->inc_flip_bw,
			0, //use_qual_row_bw
			p->num_active_planes,
			p->num_of_dpp,
			p->dcc_dram_bw_nom_overhead_factor_p0,
			p->dcc_dram_bw_nom_overhead_factor_p1,
			p->dcc_dram_bw_pref_overhead_factor_p0,
			p->dcc_dram_bw_pref_overhead_factor_p1,
			p->surface_read_bandwidth_l,
			p->surface_read_bandwidth_c,
			p->prefetch_bandwidth_l,
			p->prefetch_bandwidth_c,
			p->excess_vactive_fill_bw_l,
			p->excess_vactive_fill_bw_c,
			p->cursor_bw,
			p->dpte_row_bw,
			p->meta_row_bw,
			p->prefetch_cursor_bw,
			p->prefetch_vmrow_bw,
			p->flip_bw,
			l->unity_array,
			l->unity_array,
			l->unity_array,
			l->unity_array,
			l->unity_array,
			l->unity_array,
			l->surface_dummy_bw,
			**p->surface_peak_required_bw);

	DML_LOG_VERBOSE("DML::%s: urg_bandwidth_required%s=%f\n", __func__, (p->inc_flip_bw ? "_flip" : ""), **p->urg_bandwidth_required);
	DML_LOG_VERBOSE("DML::%s: urg_bandwidth_required_qual=%f\n", __func__, **p->urg_bandwidth_required);
	DML_LOG_VERBOSE("DML::%s: non_urg_bandwidth_required%s=%f\n", __func__, (p->inc_flip_bw ? "_flip" : ""), **p->non_urg_bandwidth_required);
	DML_ASSERT(**p->urg_bandwidth_required >= **p->non_urg_bandwidth_required);

}

void dcn5_calculate_dcc_configuration(
		bool DCCEnabled,
		bool DCCProgrammingAssumesScanDirectionUnknown,
		enum dml2_source_format_class SourcePixelFormat,
		unsigned int SurfaceWidthLuma,
		unsigned int SurfaceWidthChroma,
		unsigned int SurfaceHeightLuma,
		unsigned int SurfaceHeightChroma,
		unsigned int nomDETInKByte,
		unsigned int RequestHeight256ByteLuma,
		unsigned int RequestHeight256ByteChroma,
		enum dml2_swizzle_mode TilingFormat,
		unsigned int BytePerPixelY,
		unsigned int BytePerPixelC,
		double BytePerPixelDETY,
		double BytePerPixelDETC,
		enum dml2_rotation_angle RotationAngle,

		// Output
		enum dml2_core_internal_request_type *RequestLuma,
		enum dml2_core_internal_request_type *RequestChroma,
		unsigned int *MaxUncompressedBlockLuma,
		unsigned int *MaxUncompressedBlockChroma,
		unsigned int *MaxCompressedBlockLuma,
		unsigned int *MaxCompressedBlockChroma,
		unsigned int *IndependentBlockLuma,
		unsigned int *IndependentBlockChroma)
{
	(void)SurfaceWidthChroma;
	(void)SurfaceHeightChroma;
	(void)TilingFormat;
	(void)BytePerPixelDETY;
	(void)BytePerPixelDETC;
	unsigned int DETBufferSizeForDCC = nomDETInKByte * 1024;

	unsigned int segment_order_horz_contiguous_luma;
	unsigned int segment_order_horz_contiguous_chroma;
	unsigned int segment_order_vert_contiguous_luma;
	unsigned int segment_order_vert_contiguous_chroma;

	unsigned int req128_horz_wc_l;
	unsigned int req128_horz_wc_c;
	unsigned int req128_vert_wc_l;
	unsigned int req128_vert_wc_c;

	bool yuv420_planar;
	bool yuv422_planar;
	unsigned int horz_subsample;
	unsigned int vert_subsample;
	unsigned int horz_div_l;
	unsigned int horz_div_c;
	unsigned int vert_div_l;
	unsigned int vert_div_c;

	unsigned int swath_buf_size;
	double detile_buf_vp_horz_limit;
	double detile_buf_vp_vert_limit;

	unsigned int MAS_vp_horz_limit;
	unsigned int MAS_vp_vert_limit;
	unsigned int max_vp_horz_width;
	unsigned int max_vp_vert_height;
	unsigned int eff_surf_width_l;
	unsigned int eff_surf_width_c;
	unsigned int eff_surf_height_l;
	unsigned int eff_surf_height_c;

	unsigned int full_swath_bytes_horz_wc_l;
	unsigned int full_swath_bytes_horz_wc_c;
	unsigned int full_swath_bytes_vert_wc_l;
	unsigned int full_swath_bytes_vert_wc_c;

	yuv420_planar = dml2_core_utils_is_420(SourcePixelFormat);
	yuv422_planar = dml2_core_utils_is_422_planar(SourcePixelFormat);
	horz_subsample = (yuv420_planar || yuv422_planar) ? 1 : 0;
	vert_subsample = yuv420_planar ? 1 : 0;
	horz_div_l = 1;
	horz_div_c = 1;
	vert_div_l = 1;
	vert_div_c = 1;

	if (BytePerPixelY == 1)
		vert_div_l = 0;
	if (BytePerPixelC == 1)
		vert_div_c = 0;

	if (BytePerPixelC == 0) {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 256;
		detile_buf_vp_horz_limit = (double)swath_buf_size / ((double)RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l));
		detile_buf_vp_vert_limit = (double)swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l));
	} else {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 2 * 256;
		detile_buf_vp_horz_limit = (double)swath_buf_size / ((double)RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l) + (double)RequestHeight256ByteChroma * BytePerPixelC / (1 + horz_div_c) / (1 + horz_subsample));
		detile_buf_vp_vert_limit = (double)swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l) + 256.0 / RequestHeight256ByteChroma / (1 + vert_div_c) / (1 + vert_subsample));
	}

	if (SourcePixelFormat == dml2_420_10 || SourcePixelFormat == dml2_422_planar_10 || SourcePixelFormat == dml2_422_packed_10) {
		detile_buf_vp_horz_limit = 1.5 * detile_buf_vp_horz_limit;
		detile_buf_vp_vert_limit = 1.5 * detile_buf_vp_vert_limit;
	}

	detile_buf_vp_horz_limit = math_floor2(detile_buf_vp_horz_limit - 1, 16);
	detile_buf_vp_vert_limit = math_floor2(detile_buf_vp_vert_limit - 1, 16);

	MAS_vp_horz_limit = SourcePixelFormat == dml2_rgbe_alpha ? 3840 : 6144;
	MAS_vp_vert_limit = SourcePixelFormat == dml2_rgbe_alpha ? 3840 : (BytePerPixelY == 8 ? 3072 : 6144);
	max_vp_horz_width = (unsigned int)(math_min2((double)MAS_vp_horz_limit, detile_buf_vp_horz_limit));
	max_vp_vert_height = (unsigned int)(math_min2((double)MAS_vp_vert_limit, detile_buf_vp_vert_limit));
	eff_surf_width_l = (SurfaceWidthLuma > max_vp_horz_width ? max_vp_horz_width : SurfaceWidthLuma);
	eff_surf_width_c = eff_surf_width_l / (1 + horz_subsample);
	eff_surf_height_l = (SurfaceHeightLuma > max_vp_vert_height ? max_vp_vert_height : SurfaceHeightLuma);
	eff_surf_height_c = eff_surf_height_l / (1 + vert_subsample);

	full_swath_bytes_horz_wc_l = eff_surf_width_l * RequestHeight256ByteLuma * BytePerPixelY;
	full_swath_bytes_vert_wc_l = eff_surf_height_l * 256 / RequestHeight256ByteLuma;
	if (BytePerPixelC > 0) {
		full_swath_bytes_horz_wc_c = eff_surf_width_c * RequestHeight256ByteChroma * BytePerPixelC;
		full_swath_bytes_vert_wc_c = eff_surf_height_c * 256 / RequestHeight256ByteChroma;
	} else {
		full_swath_bytes_horz_wc_c = 0;
		full_swath_bytes_vert_wc_c = 0;
	}

	if (SourcePixelFormat == dml2_420_10 || SourcePixelFormat == dml2_422_planar_10 || SourcePixelFormat == dml2_422_packed_10) {
		full_swath_bytes_horz_wc_l = (unsigned int)(math_ceil2((double)full_swath_bytes_horz_wc_l * 2.0 / 3.0, 256.0));
		full_swath_bytes_horz_wc_c = (unsigned int)(math_ceil2((double)full_swath_bytes_horz_wc_c * 2.0 / 3.0, 256.0));
		full_swath_bytes_vert_wc_l = (unsigned int)(math_ceil2((double)full_swath_bytes_vert_wc_l * 2.0 / 3.0, 256.0));
		full_swath_bytes_vert_wc_c = (unsigned int)(math_ceil2((double)full_swath_bytes_vert_wc_c * 2.0 / 3.0, 256.0));
	}

	if (2 * full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 0;
	} else if (full_swath_bytes_horz_wc_l < 1.5 * full_swath_bytes_horz_wc_c && 2 * full_swath_bytes_horz_wc_l + full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 1;
	} else if (full_swath_bytes_horz_wc_l >= 1.5 * full_swath_bytes_horz_wc_c && full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 0;
	} else {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 1;
	}

	if (2 * full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 0;
	} else if (full_swath_bytes_vert_wc_l < 1.5 * full_swath_bytes_vert_wc_c && 2 * full_swath_bytes_vert_wc_l + full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 1;
	} else if (full_swath_bytes_vert_wc_l >= 1.5 * full_swath_bytes_vert_wc_c && full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 0;
	} else {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 1;
	}

	if (BytePerPixelY == 2) {
		segment_order_horz_contiguous_luma = 0;
		segment_order_vert_contiguous_luma = 1;
	} else {
		segment_order_horz_contiguous_luma = 1;
		segment_order_vert_contiguous_luma = 0;
	}

	if (BytePerPixelC == 2) {
		segment_order_horz_contiguous_chroma = 0;
		segment_order_vert_contiguous_chroma = 1;
	} else {
		segment_order_horz_contiguous_chroma = 1;
		segment_order_vert_contiguous_chroma = 0;
	}
	DML_LOG_VERBOSE("DML::%s: DCCEnabled = %u\n", __func__, DCCEnabled);
	DML_LOG_VERBOSE("DML::%s: nomDETInKByte = %u\n", __func__, nomDETInKByte);
	DML_LOG_VERBOSE("DML::%s: DETBufferSizeForDCC = %u\n", __func__, DETBufferSizeForDCC);
	DML_LOG_VERBOSE("DML::%s: req128_horz_wc_l = %u\n", __func__, req128_horz_wc_l);
	DML_LOG_VERBOSE("DML::%s: req128_horz_wc_c = %u\n", __func__, req128_horz_wc_c);
	DML_LOG_VERBOSE("DML::%s: full_swath_bytes_horz_wc_l = %u\n", __func__, full_swath_bytes_horz_wc_l);
	DML_LOG_VERBOSE("DML::%s: full_swath_bytes_vert_wc_c = %u\n", __func__, full_swath_bytes_vert_wc_c);
	DML_LOG_VERBOSE("DML::%s: segment_order_horz_contiguous_luma = %u\n", __func__, segment_order_horz_contiguous_luma);
	DML_LOG_VERBOSE("DML::%s: segment_order_horz_contiguous_chroma = %u\n", __func__, segment_order_horz_contiguous_chroma);
	if (DCCProgrammingAssumesScanDirectionUnknown == true) {
		if (req128_horz_wc_l == 0 && req128_vert_wc_l == 0) {
			*RequestLuma = dml2_core_internal_request_type_256_bytes;
		} else if ((req128_horz_wc_l == 1 && segment_order_horz_contiguous_luma == 0) || (req128_vert_wc_l == 1 && segment_order_vert_contiguous_luma == 0)) {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
		if (req128_horz_wc_c == 0 && req128_vert_wc_c == 0) {
			*RequestChroma = dml2_core_internal_request_type_256_bytes;
		} else if ((req128_horz_wc_c == 1 && segment_order_horz_contiguous_chroma == 0) || (req128_vert_wc_c == 1 && segment_order_vert_contiguous_chroma == 0)) {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
	} else if (!dml2_core_utils_is_vertical_rotation(RotationAngle)) {
		if (req128_horz_wc_l == 0) {
			*RequestLuma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_horz_contiguous_luma == 0) {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
		if (req128_horz_wc_c == 0) {
			*RequestChroma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_horz_contiguous_chroma == 0) {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
	} else {
		if (req128_vert_wc_l == 0) {
			*RequestLuma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_vert_contiguous_luma == 0) {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
		if (req128_vert_wc_c == 0) {
			*RequestChroma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_vert_contiguous_chroma == 0) {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
	}

	if (*RequestLuma == dml2_core_internal_request_type_256_bytes) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 256;
		*IndependentBlockLuma = 0;
	} else if (*RequestLuma == dml2_core_internal_request_type_128_bytes_contiguous) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 128;
		*IndependentBlockLuma = 128;
	} else {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 64;
		*IndependentBlockLuma = 64;
	}

	if (*RequestChroma == dml2_core_internal_request_type_256_bytes) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 256;
		*IndependentBlockChroma = 0;
	} else if (*RequestChroma == dml2_core_internal_request_type_128_bytes_contiguous) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 128;
		*IndependentBlockChroma = 128;
	} else {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 64;
		*IndependentBlockChroma = 64;
	}

	if (DCCEnabled != true || BytePerPixelC == 0) {
		*MaxUncompressedBlockChroma = 0;
		*MaxCompressedBlockChroma = 0;
		*IndependentBlockChroma = 0;
	}

	if (DCCEnabled != true) {
		*MaxUncompressedBlockLuma = 0;
		*MaxCompressedBlockLuma = 0;
		*IndependentBlockLuma = 0;
	}

	DML_LOG_VERBOSE("DML::%s: MaxUncompressedBlockLuma = %u\n", __func__, *MaxUncompressedBlockLuma);
	DML_LOG_VERBOSE("DML::%s: MaxCompressedBlockLuma = %u\n", __func__, *MaxCompressedBlockLuma);
	DML_LOG_VERBOSE("DML::%s: IndependentBlockLuma = %u\n", __func__, *IndependentBlockLuma);
	DML_LOG_VERBOSE("DML::%s: MaxUncompressedBlockChroma = %u\n", __func__, *MaxUncompressedBlockChroma);
	DML_LOG_VERBOSE("DML::%s: MaxCompressedBlockChroma = %u\n", __func__, *MaxCompressedBlockChroma);
	DML_LOG_VERBOSE("DML::%s: IndependentBlockChroma = %u\n", __func__, *IndependentBlockChroma);
}

void dcn5_calculate_flip_schedule(
		struct dml2_core_internal_scratch *s,
		bool iflip_enable,
		bool use_lb_flip_bw,
		double HostVMInefficiencyFactor,
		double Tvm_trips_flip,
		double Tr0_trips_flip,
		double Tvm_trips_flip_rounded,
		double Tr0_trips_flip_rounded,
		bool GPUVMEnable,
		double vm_bytes, // vm_bytes
		double DPTEBytesPerRow, // dpte_row_bytes
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum dml2_source_format_class SourcePixelFormat,
		double LineTime,
		double VRatio,
		double VRatioChroma,
		double Tno_bw_flip,
		unsigned int dpte_row_height,
		unsigned int dpte_row_height_chroma,
		bool use_one_row_for_frame_flip,
		unsigned int max_flip_time_us,
		unsigned int max_flip_time_lines,
		unsigned int per_pipe_flip_bytes,
		unsigned int meta_row_bytes,
		unsigned int meta_row_height,
		unsigned int meta_row_height_chroma,
		bool dcc_mrq_enable,

		// Output
		double *dst_y_per_vm_flip,
		double *dst_y_per_row_flip,
		double *final_flip_bw,
		bool *ImmediateFlipSupportedForPipe)
{
	(void)use_one_row_for_frame_flip;
	struct dml2_core_shared_CalculateFlipSchedule_locals *l = &s->CalculateFlipSchedule_locals;

	l->dual_plane = dml2_core_utils_is_420(SourcePixelFormat) || dml2_core_utils_is_422_planar(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha;
	l->dpte_row_bytes = DPTEBytesPerRow;

	DML_LOG_VERBOSE("DML::%s: GPUVMEnable = %u\n", __func__, GPUVMEnable);
	DML_LOG_VERBOSE("DML::%s: ip.max_flip_time_us = %d\n", __func__, max_flip_time_us);
	DML_LOG_VERBOSE("DML::%s: ip.max_flip_time_lines = %d\n", __func__, max_flip_time_lines);
	DML_LOG_VERBOSE("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
	DML_LOG_VERBOSE("DML::%s: TotImmediateFlipBytes = %u\n", __func__, TotImmediateFlipBytes);
	DML_LOG_VERBOSE("DML::%s: use_lb_flip_bw = %u\n", __func__, use_lb_flip_bw);
	DML_LOG_VERBOSE("DML::%s: iflip_enable = %u\n", __func__, iflip_enable);
	DML_LOG_VERBOSE("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
	DML_LOG_VERBOSE("DML::%s: LineTime = %f\n", __func__, LineTime);
	DML_LOG_VERBOSE("DML::%s: Tno_bw_flip = %f\n", __func__, Tno_bw_flip);
	DML_LOG_VERBOSE("DML::%s: Tvm_trips_flip = %f\n", __func__, Tvm_trips_flip);
	DML_LOG_VERBOSE("DML::%s: Tr0_trips_flip = %f\n", __func__, Tr0_trips_flip);
	DML_LOG_VERBOSE("DML::%s: Tvm_trips_flip_rounded = %f\n", __func__, Tvm_trips_flip_rounded);
	DML_LOG_VERBOSE("DML::%s: Tr0_trips_flip_rounded = %f\n", __func__, Tr0_trips_flip_rounded);
	DML_LOG_VERBOSE("DML::%s: vm_bytes = %f\n", __func__, vm_bytes);
	DML_LOG_VERBOSE("DML::%s: DPTEBytesPerRow = %f\n", __func__, DPTEBytesPerRow);
	DML_LOG_VERBOSE("DML::%s: meta_row_bytes = %d\n", __func__, meta_row_bytes);
	DML_LOG_VERBOSE("DML::%s: dpte_row_bytes = %f\n", __func__, l->dpte_row_bytes);
	DML_LOG_VERBOSE("DML::%s: dpte_row_height = %d\n", __func__, dpte_row_height);
	DML_LOG_VERBOSE("DML::%s: meta_row_height = %d\n", __func__, meta_row_height);
	DML_LOG_VERBOSE("DML::%s: VRatio = %f\n", __func__, VRatio);

	if (TotImmediateFlipBytes > 0 && (GPUVMEnable || dcc_mrq_enable)) {
		if (l->dual_plane) {
			if (dcc_mrq_enable & GPUVMEnable) {
				l->min_row_height = math_min2(dpte_row_height, meta_row_height);
				l->min_row_height_chroma = math_min2(dpte_row_height_chroma, meta_row_height_chroma);
			} else if (GPUVMEnable) {
				l->min_row_height = dpte_row_height;
				l->min_row_height_chroma = dpte_row_height_chroma;
			} else {
				l->min_row_height = meta_row_height;
				l->min_row_height_chroma = meta_row_height_chroma;
			}
			l->min_row_time = math_min2(l->min_row_height * LineTime / VRatio, l->min_row_height_chroma * LineTime / VRatioChroma);
		} else {
			if (dcc_mrq_enable & GPUVMEnable)
				l->min_row_height = math_min2(dpte_row_height, meta_row_height);
			else if (GPUVMEnable)
				l->min_row_height = dpte_row_height;
			else
				l->min_row_height = meta_row_height;

			l->min_row_time = l->min_row_height * LineTime / VRatio;
		}
		DML_LOG_VERBOSE("DML::%s: min_row_time = %f\n", __func__, l->min_row_time);
		DML_ASSERT(l->min_row_time > 0);

		if (use_lb_flip_bw) {
			// For mode check, calculation the flip bw requirement with worst case flip time
			l->max_flip_time = math_min2(math_min2(l->min_row_time, (double)max_flip_time_lines * LineTime / VRatio),
					math_max2(Tvm_trips_flip_rounded + 2 * Tr0_trips_flip_rounded, (double)max_flip_time_us));

			//The lower bound on flip bandwidth
			// Note: The get_urgent_bandwidth_required already consider dpte_row_bw and meta_row_bw in bandwidth calculation, so leave final_flip_bw = 0 if iflip not required
			l->lb_flip_bw = 0;

			if (iflip_enable) {
				l->hvm_scaled_vm_bytes = vm_bytes * HostVMInefficiencyFactor;
				l->num_rows = 2;
				l->hvm_scaled_row_bytes = (l->num_rows * l->dpte_row_bytes * HostVMInefficiencyFactor + l->num_rows * meta_row_bytes);
				l->hvm_scaled_vm_row_bytes = l->hvm_scaled_vm_bytes + l->hvm_scaled_row_bytes;
				l->lb_flip_bw = math_max3(
						l->hvm_scaled_vm_row_bytes / (l->max_flip_time - Tno_bw_flip),
						l->hvm_scaled_vm_bytes / (l->max_flip_time - Tno_bw_flip - 2 * Tr0_trips_flip_rounded),
						l->hvm_scaled_row_bytes / (l->max_flip_time - Tvm_trips_flip_rounded));
				DML_LOG_VERBOSE("DML::%s: max_flip_time = %f\n", __func__, l->max_flip_time);
				DML_LOG_VERBOSE("DML::%s: total vm bytes (hvm ineff scaled) = %f\n", __func__, l->hvm_scaled_vm_bytes);
				DML_LOG_VERBOSE("DML::%s: total row bytes (%f row, hvm ineff scaled) = %f\n", __func__, l->num_rows, l->hvm_scaled_row_bytes);
				DML_LOG_VERBOSE("DML::%s: total vm+row bytes (hvm ineff scaled) = %f\n", __func__, l->hvm_scaled_vm_row_bytes);
				DML_LOG_VERBOSE("DML::%s: lb_flip_bw for vm and row = %f\n", __func__, l->hvm_scaled_vm_row_bytes / (l->max_flip_time - Tno_bw_flip));
				DML_LOG_VERBOSE("DML::%s: lb_flip_bw for vm = %f\n", __func__, l->hvm_scaled_vm_bytes / (l->max_flip_time - Tno_bw_flip - 2 * Tr0_trips_flip_rounded));
				DML_LOG_VERBOSE("DML::%s: lb_flip_bw for row = %f\n", __func__, l->hvm_scaled_row_bytes / (l->max_flip_time - Tvm_trips_flip_rounded));

				if (l->lb_flip_bw > 0) {
					DML_LOG_VERBOSE("DML::%s: mode_support est Tvm_flip = %f (bw-based)\n", __func__, Tno_bw_flip + l->hvm_scaled_vm_bytes / l->lb_flip_bw);
					DML_LOG_VERBOSE("DML::%s: mode_support est Tr0_flip = %f (bw-based)\n", __func__, l->hvm_scaled_row_bytes / l->lb_flip_bw / l->num_rows);
					DML_LOG_VERBOSE("DML::%s: mode_support est dst_y_per_vm_flip = %f (bw-based)\n", __func__, Tno_bw_flip + l->hvm_scaled_vm_bytes / l->lb_flip_bw / LineTime);
					DML_LOG_VERBOSE("DML::%s: mode_support est dst_y_per_row_flip = %f (bw-based)\n", __func__, l->hvm_scaled_row_bytes / l->lb_flip_bw / LineTime / l->num_rows);
					DML_LOG_VERBOSE("DML::%s: Tvm_trips_flip_rounded + 2*Tr0_trips_flip_rounded = %f\n", __func__, (Tvm_trips_flip_rounded + 2 * Tr0_trips_flip_rounded));
				}
				l->lb_flip_bw = math_max3(l->lb_flip_bw,
						l->hvm_scaled_vm_bytes / (31 * LineTime) - Tno_bw_flip,
						(l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / (15 * LineTime));

				DML_LOG_VERBOSE("DML::%s: lb_flip_bw for vm reg limit = %f\n", __func__, l->hvm_scaled_vm_bytes / (31 * LineTime) - Tno_bw_flip);
				DML_LOG_VERBOSE("DML::%s: lb_flip_bw for row reg limit = %f\n", __func__, (l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / (15 * LineTime));
			}

			*final_flip_bw = l->lb_flip_bw;

			*dst_y_per_vm_flip = 1; // not used
			*dst_y_per_row_flip = 1; // not used
			*ImmediateFlipSupportedForPipe = l->min_row_time >= (Tvm_trips_flip_rounded + 2 * Tr0_trips_flip_rounded);
		} else {
			if (iflip_enable) {
				l->ImmediateFlipBW = (double)per_pipe_flip_bytes * BandwidthAvailableForImmediateFlip / (double)TotImmediateFlipBytes; // flip_bw(i)
				DML_LOG_VERBOSE("DML::%s: per_pipe_flip_bytes = %d\n", __func__, per_pipe_flip_bytes);
				DML_LOG_VERBOSE("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
				DML_LOG_VERBOSE("DML::%s: ImmediateFlipBW = %f\n", __func__, l->ImmediateFlipBW);
				DML_LOG_VERBOSE("DML::%s: portion of flip bw = %f\n", __func__, (double)per_pipe_flip_bytes / (double)TotImmediateFlipBytes);
				if (l->ImmediateFlipBW == 0) {
					l->Tvm_flip = 0;
					l->Tr0_flip = 0;
				} else {
					l->Tvm_flip = math_max3(Tvm_trips_flip,
							Tno_bw_flip + vm_bytes * HostVMInefficiencyFactor / l->ImmediateFlipBW,
							LineTime / 4.0);

					l->Tr0_flip = math_max3(Tr0_trips_flip,
							(l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / l->ImmediateFlipBW,
							LineTime / 4.0);
				}
				DML_LOG_VERBOSE("DML::%s: total vm bytes (hvm ineff scaled) = %f\n", __func__, vm_bytes * HostVMInefficiencyFactor);
				DML_LOG_VERBOSE("DML::%s: total row bytes (hvm ineff scaled, one row) = %f\n", __func__, (l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes));
				DML_LOG_VERBOSE("DML::%s: Tvm_flip = %f (bw-based), Tvm_trips_flip = %f (latency-based)\n", __func__, Tno_bw_flip + vm_bytes * HostVMInefficiencyFactor / l->ImmediateFlipBW, Tvm_trips_flip);
				DML_LOG_VERBOSE("DML::%s: Tr0_flip = %f (bw-based), Tr0_trips_flip = %f (latency-based)\n", __func__, (l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / l->ImmediateFlipBW, Tr0_trips_flip);
				*dst_y_per_vm_flip = math_ceil2(4.0 * (l->Tvm_flip / LineTime), 1.0) / 4.0;
				*dst_y_per_row_flip = math_ceil2(4.0 * (l->Tr0_flip / LineTime), 1.0) / 4.0;

				*final_flip_bw = math_max2(vm_bytes * HostVMInefficiencyFactor / (*dst_y_per_vm_flip * LineTime),
						(l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / (*dst_y_per_row_flip * LineTime));

				if (*dst_y_per_vm_flip >= 32 || *dst_y_per_row_flip >= 16 || l->Tvm_flip + 2 * l->Tr0_flip > l->min_row_time) {
					*ImmediateFlipSupportedForPipe = false;
				} else {
					*ImmediateFlipSupportedForPipe = iflip_enable;
				}
			} else {
				l->Tvm_flip = 0;
				l->Tr0_flip = 0;
				*dst_y_per_vm_flip = 0;
				*dst_y_per_row_flip = 0;
				*final_flip_bw = 0;
				*ImmediateFlipSupportedForPipe = iflip_enable;
			}
		}
	} else {
		l->Tvm_flip = 0;
		l->Tr0_flip = 0;
		*dst_y_per_vm_flip = 0;
		*dst_y_per_row_flip = 0;
		*final_flip_bw = 0;
		*ImmediateFlipSupportedForPipe = iflip_enable;
	}

	if (!use_lb_flip_bw) {
		DML_LOG_VERBOSE("DML::%s: dst_y_per_vm_flip = %f (should be < 32)\n", __func__, *dst_y_per_vm_flip);
		DML_LOG_VERBOSE("DML::%s: dst_y_per_row_flip = %f (should be < 16)\n", __func__, *dst_y_per_row_flip);
		DML_LOG_VERBOSE("DML::%s: Tvm_flip = %f (final)\n", __func__, l->Tvm_flip);
		DML_LOG_VERBOSE("DML::%s: Tr0_flip = %f (final)\n", __func__, l->Tr0_flip);
		DML_LOG_VERBOSE("DML::%s: Tvm_flip + 2*Tr0_flip = %f (should be <= min_row_time=%f)\n", __func__, l->Tvm_flip + 2 * l->Tr0_flip, l->min_row_time);
	}
	DML_LOG_VERBOSE("DML::%s: final_flip_bw = %f\n", __func__, *final_flip_bw);
	DML_LOG_VERBOSE("DML::%s: ImmediateFlipSupportedForPipe = %u\n", __func__, *ImmediateFlipSupportedForPipe);
}

bool dcn5_calculate_pstate_support_method(
		enum dml2_pstate_method method,
		double vactive_margin_us,
		double reserved_vblank_us,
		double blackout_us,
		bool all_streams_blanked,
		/* output */
		enum dml2_pstate_change_support *surface_pstate_change_support)
{
	*surface_pstate_change_support = dml2_pstate_change_unsupported;
	if (method == dml2_pstate_method_na) {
		/* automatic */
		if (all_streams_blanked ||
				(vactive_margin_us > 0 && reserved_vblank_us >= blackout_us))
			*surface_pstate_change_support = dml2_pstate_change_vblank_and_vactive;
		else if (vactive_margin_us > 0)
			*surface_pstate_change_support = dml2_pstate_change_vactive;
		else if (reserved_vblank_us >= blackout_us)
			*surface_pstate_change_support = dml2_pstate_change_vblank;
	} else if (method == dml2_pstate_method_vactive || method == dml2_pstate_method_fw_vactive_drr) {
		/* vactive */
		if (all_streams_blanked ||
				(vactive_margin_us > 0 && reserved_vblank_us >= blackout_us))
			*surface_pstate_change_support = dml2_pstate_change_vblank_and_vactive;
		else if (vactive_margin_us > 0)
			*surface_pstate_change_support = dml2_pstate_change_vactive;
	} else if ((method == dml2_pstate_method_vblank || method == dml2_pstate_method_fw_vblank_drr) &&
			reserved_vblank_us >= blackout_us) {
		/* vblank */
		*surface_pstate_change_support = dml2_pstate_change_vblank;
	} else if (method == dml2_pstate_method_fw_drr) {
		/* drr */
		*surface_pstate_change_support = dml2_pstate_change_drr;
	} else if (method == dml2_pstate_method_alternate) {
		/* TODO - alternate */
		*surface_pstate_change_support = dml2_pstate_change_mall_svp;
	}

	return *surface_pstate_change_support != dml2_pstate_change_unsupported;
}

static double dcn5_calculate_writeback_latency_hiding_us(
		const struct dml2_display_cfg *display_cfg,
		unsigned int writeback_buffer_size_bytes,
		unsigned int stream_index,
		unsigned int dwb_index)
{
	double byte_per_pixel_luma_in_buffer = 1.0;
	double buffer_for_luma_bytes = (double)writeback_buffer_size_bytes * 1024.0;
	double line_time_us = (double)display_cfg->stream_descriptors[stream_index].timing.h_total /
			(double)display_cfg->stream_descriptors[stream_index].timing.pixel_clock_khz / 1000.0;

	if (display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].pixel_format == dml2_444_64) {
		byte_per_pixel_luma_in_buffer = 8.0;
	} else if (display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].pixel_format == dml2_444_32) {
		byte_per_pixel_luma_in_buffer = 4.0;
	} else if (display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].pixel_format == dml2_422_packed_8
		|| display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].pixel_format == dml2_420_8) {
		byte_per_pixel_luma_in_buffer = 1.0;
		buffer_for_luma_bytes = buffer_for_luma_bytes / 2.0;
	} else if (display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].pixel_format == dml2_422_packed_10
		|| display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].pixel_format == dml2_420_10) {
		byte_per_pixel_luma_in_buffer = 10.0 / 8.0;
	}

	return (double)buffer_for_luma_bytes /
			((double)display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].output_height *
			(double)display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].output_width /
			((double)display_cfg->stream_descriptors[stream_index].writeback.writeback_stream[dwb_index].input_height *
			line_time_us) * byte_per_pixel_luma_in_buffer);
}

void dcn5_calculate_watermarks_and_dram_speed_change_support(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p)
{
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals *s = &scratch->CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals;

	double reserved_vblank_time_us;
	bool FoundCriticalSurface = false;

	s->TotalActiveWriteback = 0;
	p->Watermark->UrgentWatermark = p->mmSOCParameters.UrgentLatency + p->mmSOCParameters.ExtraLatency;

	DML_LOG_VERBOSE("DML::%s: UrgentLatency = %f\n", __func__, p->mmSOCParameters.UrgentLatency);
	DML_LOG_VERBOSE("DML::%s: ExtraLatency = %f\n", __func__, p->mmSOCParameters.ExtraLatency);
	DML_LOG_VERBOSE("DML::%s: UrgentWatermark = %f\n", __func__, p->Watermark->UrgentWatermark);

	p->Watermark->USRRetrainingWatermark = p->mmSOCParameters.UrgentLatency + p->mmSOCParameters.ExtraLatency + p->mmSOCParameters.USRRetrainingLatency + p->mmSOCParameters.SMNLatency;
	p->Watermark->DRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->Watermark->UrgentWatermark;
	p->Watermark->FCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->Watermark->UrgentWatermark;
	p->Watermark->StutterExitWatermark = p->mmSOCParameters.SRExitTime + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	p->Watermark->StutterEnterPlusExitWatermark = p->mmSOCParameters.SREnterPlusExitTime + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	p->Watermark->Z8StutterExitWatermark = p->mmSOCParameters.SRExitZ8Time + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	p->Watermark->Z8StutterEnterPlusExitWatermark = p->mmSOCParameters.SREnterPlusExitZ8Time + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	if (p->mmSOCParameters.qos_type == dml2_qos_param_type_dcn4x) {
		p->Watermark->StutterExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
		p->Watermark->StutterEnterPlusExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
		p->Watermark->Z8StutterExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
		p->Watermark->Z8StutterEnterPlusExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
	}
	p->Watermark->temp_read_or_ppt_watermark_us = p->mmSOCParameters.temp_read_or_ppt_blackout_us + p->Watermark->UrgentWatermark;

	DML_LOG_VERBOSE("DML::%s: UrgentLatency = %f\n", __func__, p->mmSOCParameters.UrgentLatency);
	DML_LOG_VERBOSE("DML::%s: ExtraLatency = %f\n", __func__, p->mmSOCParameters.ExtraLatency);
	DML_LOG_VERBOSE("DML::%s: DRAMClockChangeLatency = %f\n", __func__, p->mmSOCParameters.DRAMClockChangeLatency);
	DML_LOG_VERBOSE("DML::%s: SREnterPlusExitZ8Time = %f\n", __func__, p->mmSOCParameters.SREnterPlusExitZ8Time);
	DML_LOG_VERBOSE("DML::%s: SREnterPlusExitTime = %f\n", __func__, p->mmSOCParameters.SREnterPlusExitTime);
	DML_LOG_VERBOSE("DML::%s: UrgentWatermark = %f\n", __func__, p->Watermark->UrgentWatermark);
	DML_LOG_VERBOSE("DML::%s: USRRetrainingWatermark = %f\n", __func__, p->Watermark->USRRetrainingWatermark);
	DML_LOG_VERBOSE("DML::%s: DRAMClockChangeWatermark = %f\n", __func__, p->Watermark->DRAMClockChangeWatermark);
	DML_LOG_VERBOSE("DML::%s: FCLKChangeWatermark = %f\n", __func__, p->Watermark->FCLKChangeWatermark);
	DML_LOG_VERBOSE("DML::%s: StutterExitWatermark = %f\n", __func__, p->Watermark->StutterExitWatermark);
	DML_LOG_VERBOSE("DML::%s: StutterEnterPlusExitWatermark = %f\n", __func__, p->Watermark->StutterEnterPlusExitWatermark);
	DML_LOG_VERBOSE("DML::%s: Z8StutterExitWatermark = %f\n", __func__, p->Watermark->Z8StutterExitWatermark);
	DML_LOG_VERBOSE("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, p->Watermark->Z8StutterEnterPlusExitWatermark);
	DML_LOG_VERBOSE("DML::%s: temp_read_or_ppt_watermark_us = %f\n", __func__, p->Watermark->temp_read_or_ppt_watermark_us);

	s->TotalActiveWriteback = 0;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k)
		if (p->display_cfg->plane_descriptors[k].stream_index == k)
			s->TotalActiveWriteback = s->TotalActiveWriteback + p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream;

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->WritebackUrgentWatermark = p->mmSOCParameters.WritebackLatency;
	} else {
		p->Watermark->WritebackUrgentWatermark = p->mmSOCParameters.WritebackLatency + (s->TotalActiveWriteback - 1) * p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
	}
	if (p->USRRetrainingRequired)
		p->Watermark->WritebackUrgentWatermark = p->Watermark->WritebackUrgentWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->WritebackDRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.WritebackLatency;
		p->Watermark->WritebackFCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->mmSOCParameters.WritebackLatency;
	} else {
		p->Watermark->WritebackDRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.WritebackLatency + (s->TotalActiveWriteback - 1) * p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
		p->Watermark->WritebackFCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->mmSOCParameters.WritebackLatency + (s->TotalActiveWriteback - 1) * p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
	}

	if (p->USRRetrainingRequired)
		p->Watermark->WritebackDRAMClockChangeWatermark = p->Watermark->WritebackDRAMClockChangeWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (p->USRRetrainingRequired)
		p->Watermark->WritebackFCLKChangeWatermark = p->Watermark->WritebackFCLKChangeWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->writeback_temp_read_or_ppt_watermark_us = p->mmSOCParameters.temp_read_or_ppt_blackout_us;
	} else {
		p->Watermark->writeback_temp_read_or_ppt_watermark_us = p->mmSOCParameters.temp_read_or_ppt_blackout_us + (s->TotalActiveWriteback - 1) * p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
	}

	DML_LOG_VERBOSE("DML::%s: WritebackDRAMClockChangeWatermark = %f\n", __func__, p->Watermark->WritebackDRAMClockChangeWatermark);
	DML_LOG_VERBOSE("DML::%s: WritebackFCLKChangeWatermark = %f\n", __func__, p->Watermark->WritebackFCLKChangeWatermark);
	DML_LOG_VERBOSE("DML::%s: writeback_temp_read_or_ppt_watermark_us = %f\n", __func__, p->Watermark->writeback_temp_read_or_ppt_watermark_us);
	DML_LOG_VERBOSE("DML::%s: WritebackUrgentWatermark = %f\n", __func__, p->Watermark->WritebackUrgentWatermark);
	DML_LOG_VERBOSE("DML::%s: USRRetrainingRequired = %u\n", __func__, p->USRRetrainingRequired);
	DML_LOG_VERBOSE("DML::%s: USRRetrainingLatency = %f\n", __func__, p->mmSOCParameters.USRRetrainingLatency);

	s->TotalPixelBW = 0.0;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		double h_total = (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		double pixel_clock_mhz = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000.0;
		double v_ratio = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		double v_ratio_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		s->TotalPixelBW = s->TotalPixelBW + p->DPPPerSurface[k]
								     * (p->SwathWidthY[k] * p->BytePerPixelDETY[k] * v_ratio + p->SwathWidthC[k] * p->BytePerPixelDETC[k] * v_ratio_c) / (h_total / pixel_clock_mhz);
	}

	*p->global_fclk_change_supported = true;
	*p->global_dram_clock_change_supported = true;
	*p->global_temp_read_or_ppt_supported = true;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		double h_total = (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		double pixel_clock_mhz = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000.0;
		double v_ratio = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		double v_ratio_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		double v_taps = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
		double v_taps_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
		double h_ratio = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio;
		double h_ratio_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio;
		double LBBitPerPixel = 57;

		s->LBLatencyHidingSourceLinesY[k] = (unsigned int)(math_min2((double)p->MaxLineBufferLines, math_floor2((double)p->LineBufferSize / LBBitPerPixel / ((double)p->SwathWidthY[k] / math_max2(h_ratio, 1.0)), 1)) - (v_taps - 1));
		s->LBLatencyHidingSourceLinesC[k] = (unsigned int)(math_min2((double)p->MaxLineBufferLines, math_floor2((double)p->LineBufferSize / LBBitPerPixel / ((double)p->SwathWidthC[k] / math_max2(h_ratio_c, 1.0)), 1)) - (v_taps_c - 1));

		DML_LOG_VERBOSE("DML::%s: k=%u, MaxLineBufferLines= %u\n", __func__, k, p->MaxLineBufferLines);
		DML_LOG_VERBOSE("DML::%s: k=%u, LineBufferSize = %u\n", __func__, k, p->LineBufferSize);
		DML_LOG_VERBOSE("DML::%s: k=%u, LBBitPerPixel = %f\n", __func__, k, LBBitPerPixel);
		DML_LOG_VERBOSE("DML::%s: k=%u, HRatio = %f\n", __func__, k, h_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u, VTaps = %f\n", __func__, k, v_taps);

		s->EffectiveLBLatencyHidingY = s->LBLatencyHidingSourceLinesY[k] / v_ratio * (h_total / pixel_clock_mhz);
		s->EffectiveLBLatencyHidingC = s->LBLatencyHidingSourceLinesC[k] / v_ratio_c * (h_total / pixel_clock_mhz);

		s->EffectiveDETBufferSizeY = p->DETBufferSizeY[k];
		if (p->UnboundedRequestEnabled) {
			s->EffectiveDETBufferSizeY = s->EffectiveDETBufferSizeY + p->CompressedBufferSizeInkByte * 1024 * (p->SwathWidthY[k] * p->BytePerPixelDETY[k] * v_ratio) / (h_total / pixel_clock_mhz) / s->TotalPixelBW;
		}

		s->LinesInDETY[k] = (double)s->EffectiveDETBufferSizeY / p->BytePerPixelDETY[k] / p->SwathWidthY[k];
		s->LinesInDETYRoundedDownToSwath[k] = (unsigned int)(math_floor2(s->LinesInDETY[k], p->SwathHeightY[k]));
		s->FullDETBufferingTimeY = s->LinesInDETYRoundedDownToSwath[k] * (h_total / pixel_clock_mhz) / v_ratio;

		s->ActiveClockChangeLatencyHidingY = s->EffectiveLBLatencyHidingY + s->FullDETBufferingTimeY - ((double)p->DSTXAfterScaler[k] / h_total + (double)p->DSTYAfterScaler[k]) * h_total / pixel_clock_mhz;

		if (p->NumberOfActiveSurfaces > 1) {
			s->ActiveClockChangeLatencyHidingY = s->ActiveClockChangeLatencyHidingY - (1.0 - 1.0 / (double)p->NumberOfActiveSurfaces) * (double)p->SwathHeightY[k] * (double)h_total / pixel_clock_mhz / v_ratio;
		}

		if (p->BytePerPixelDETC[k] > 0) {
			s->LinesInDETC[k] = p->DETBufferSizeC[k] / p->BytePerPixelDETC[k] / p->SwathWidthC[k];
			s->LinesInDETCRoundedDownToSwath[k] = (unsigned int)(math_floor2(s->LinesInDETC[k], p->SwathHeightC[k]));
			s->FullDETBufferingTimeC = s->LinesInDETCRoundedDownToSwath[k] * (h_total / pixel_clock_mhz) / v_ratio_c;
			s->ActiveClockChangeLatencyHidingC = s->EffectiveLBLatencyHidingC + s->FullDETBufferingTimeC - ((double)p->DSTXAfterScaler[k] / (double)h_total + (double)p->DSTYAfterScaler[k]) * (double)h_total / pixel_clock_mhz;
			if (p->NumberOfActiveSurfaces > 1) {
				s->ActiveClockChangeLatencyHidingC = s->ActiveClockChangeLatencyHidingC - (1.0 - 1.0 / (double)p->NumberOfActiveSurfaces) * (double)p->SwathHeightC[k] * (double)h_total / pixel_clock_mhz / v_ratio_c;
			}
			s->ActiveClockChangeLatencyHiding = math_min2(s->ActiveClockChangeLatencyHidingY, s->ActiveClockChangeLatencyHidingC);
		} else {
			s->ActiveClockChangeLatencyHiding = s->ActiveClockChangeLatencyHidingY;
		}

		DML_LOG_VERBOSE("DML::%s: ActiveClockChangeLatencyHidingY = %f\n", __func__, s->ActiveClockChangeLatencyHidingY);
		DML_LOG_VERBOSE("DML::%s: ActiveClockChangeLatencyHidingC = %f\n", __func__, s->ActiveClockChangeLatencyHidingC);
		DML_LOG_VERBOSE("DML::%s: ActiveClockChangeLatencyHiding = %f\n", __func__, s->ActiveClockChangeLatencyHiding);

		s->ActiveDRAMClockChangeLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->DRAMClockChangeWatermark;
		s->ActiveFCLKChangeLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->FCLKChangeWatermark;
		s->USRRetrainingLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->USRRetrainingWatermark;
		s->temp_read_or_ppt_latency_margin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->temp_read_or_ppt_watermark_us;

		DML_LOG_VERBOSE("DML::%s: k=%u, ActiveFCLKChangeLatencyMargin = %f\n", __func__, k, s->ActiveFCLKChangeLatencyMargin[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, ActiveDRAMClockChangeLatencyMargin = %f\n", __func__, k, s->ActiveDRAMClockChangeLatencyMargin[k]);

		if (p->VActiveLatencyHidingMargin) {
			p->VActiveLatencyHidingMargin[k] = s->ActiveDRAMClockChangeLatencyMargin[k];
			DML_LOG_VERBOSE("DML::%s: k=%u, VActiveLatencyHidingMargin = %f\n", __func__, k, p->VActiveLatencyHidingMargin[k]);
		}

		if (p->VActiveLatencyHidingUs) {
			p->VActiveLatencyHidingUs[k] = s->ActiveClockChangeLatencyHiding;
			DML_LOG_VERBOSE("DML::%s: k=%u, VActiveLatencyHidingUs = %f\n", __func__, k, p->VActiveLatencyHidingUs[k]);
		}

		for (unsigned int j = 0; j < p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream; ++j) {
			s->WritebackLatencyHiding = dcn5_calculate_writeback_latency_hiding_us(p->display_cfg,
					p->WritebackInterfaceBufferSize * 1024,
					p->display_cfg->plane_descriptors[k].stream_index,
					j);

			s->WritebackDRAMClockChangeLatencyMargin = s->WritebackLatencyHiding - p->Watermark->WritebackDRAMClockChangeWatermark;
			s->WritebackFCLKChangeLatencyMargin = s->WritebackLatencyHiding - p->Watermark->WritebackFCLKChangeWatermark;
			s->WritebackTempReadOrPptLatencyMargin = s->WritebackLatencyHiding - p->Watermark->writeback_temp_read_or_ppt_watermark_us;
			s->ActiveDRAMClockChangeLatencyMargin[k] = math_min2(s->ActiveDRAMClockChangeLatencyMargin[k], s->WritebackDRAMClockChangeLatencyMargin);
			s->ActiveFCLKChangeLatencyMargin[k] = math_min2(s->ActiveFCLKChangeLatencyMargin[k], s->WritebackFCLKChangeLatencyMargin);
			s->temp_read_or_ppt_latency_margin[k] = math_min2(s->temp_read_or_ppt_latency_margin[k], s->WritebackTempReadOrPptLatencyMargin);
			DML_LOG_VERBOSE("DML::%s: k=%u, ActiveFCLKChangeLatencyMargin = %f (WB)\n", __func__, k, s->ActiveFCLKChangeLatencyMargin[k]);
		}

		p->MaxActiveDRAMClockChangeLatencySupported[k] = s->ActiveDRAMClockChangeLatencyMargin[k] + p->mmSOCParameters.DRAMClockChangeLatency;

		reserved_vblank_time_us = (double)p->display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns / 1000;

		*p->global_fclk_change_supported &= dcn5_calculate_pstate_support_method(
				dml2_pstate_method_vactive,
				s->ActiveFCLKChangeLatencyMargin[k],
				reserved_vblank_time_us,
				p->mmSOCParameters.FCLKChangeLatency,
				p->display_cfg->overrides.all_streams_blanked,
				/* output */
				&p->FCLKChangeSupport[k]);

		*p->global_temp_read_or_ppt_supported &= dcn5_calculate_pstate_support_method(
				dml2_pstate_method_vactive,
				s->temp_read_or_ppt_latency_margin[k],
				reserved_vblank_time_us,
				p->mmSOCParameters.temp_read_or_ppt_blackout_us,
				p->display_cfg->overrides.all_streams_blanked,
				/* output */
				&p->temp_read_or_ppt_support[k]);

		*p->global_dram_clock_change_support_required |= p->uclk_pstate_switch_modes[k] != dml2_pstate_method_na;
		*p->global_dram_clock_change_supported &= dcn5_calculate_pstate_support_method(
				p->uclk_pstate_switch_modes[k],
				s->ActiveDRAMClockChangeLatencyMargin[k],
				reserved_vblank_time_us,
				p->mmSOCParameters.DRAMClockChangeLatency,
				p->display_cfg->overrides.all_streams_blanked,
				/* output */
				&p->DRAMClockChangeSupport[k]);

		s->dst_y_pstate = (unsigned int)(math_ceil2((p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.UrgentLatency) / (h_total / pixel_clock_mhz), 1));
		s->src_y_pstate_l = (unsigned int)(math_ceil2(s->dst_y_pstate * v_ratio, p->SwathHeightY[k]));
		s->src_y_ahead_l = (unsigned int)(math_floor2(p->DETBufferSizeY[k] / p->BytePerPixelDETY[k] / p->SwathWidthY[k], p->SwathHeightY[k]) + s->LBLatencyHidingSourceLinesY[k]);

		DML_LOG_VERBOSE("DML::%s: k=%u, DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, BytePerPixelDETY = %f\n", __func__, k, p->BytePerPixelDETY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, SwathWidthY = %u\n", __func__, k, p->SwathWidthY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, SwathHeightY = %u\n", __func__, k, p->SwathHeightY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, LBLatencyHidingSourceLinesY = %u\n", __func__, k, s->LBLatencyHidingSourceLinesY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dst_y_pstate = %u\n", __func__, k, s->dst_y_pstate);
		DML_LOG_VERBOSE("DML::%s: k=%u, src_y_pstate_l = %u\n", __func__, k, s->src_y_pstate_l);
		DML_LOG_VERBOSE("DML::%s: k=%u, src_y_ahead_l = %u\n", __func__, k, s->src_y_ahead_l);
		DML_LOG_VERBOSE("DML::%s: k=%u, meta_row_height_l = %u\n", __func__, k, p->meta_row_height_l[k]);

		if (p->BytePerPixelDETC[k] > 0) {
			s->src_y_pstate_c = (unsigned int)(math_ceil2(s->dst_y_pstate * v_ratio_c, p->SwathHeightC[k]));
			s->src_y_ahead_c = (unsigned int)(math_floor2(p->DETBufferSizeC[k] / p->BytePerPixelDETC[k] / p->SwathWidthC[k], p->SwathHeightC[k]) + s->LBLatencyHidingSourceLinesC[k]);

			DML_LOG_VERBOSE("DML::%s: k=%u, meta_row_height_c = %u\n", __func__, k, p->meta_row_height_c[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, src_y_pstate_c = %u\n", __func__, k, s->src_y_pstate_c);
			DML_LOG_VERBOSE("DML::%s: k=%u, src_y_ahead_c = %u\n", __func__, k, s->src_y_ahead_c);
			DML_LOG_VERBOSE("DML::%s: k=%u, sub_vp_lines_c = %u\n", __func__, k, s->sub_vp_lines_c);
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		DML_LOG_VERBOSE("DML::%s: k=%u, ActiveFCLKChangeLatencyMargin=%f\n", __func__, k, s->ActiveFCLKChangeLatencyMargin[k]);
		if (((!FoundCriticalSurface) || ((s->ActiveFCLKChangeLatencyMargin[k] + p->mmSOCParameters.FCLKChangeLatency) < *p->MaxActiveFCLKChangeLatencySupported))) {
			FoundCriticalSurface = true;
			*p->MaxActiveFCLKChangeLatencySupported = s->ActiveFCLKChangeLatencyMargin[k] + p->mmSOCParameters.FCLKChangeLatency;
		}
	}

	DML_LOG_VERBOSE("DML::%s: DRAMClockChangeSupport = %u\n", __func__, *p->global_dram_clock_change_supported);
	DML_LOG_VERBOSE("DML::%s: FCLKChangeSupport = %u\n", __func__, *p->global_fclk_change_supported);
	DML_LOG_VERBOSE("DML::%s: MaxActiveFCLKChangeLatencySupported = %f\n", __func__, *p->MaxActiveFCLKChangeLatencySupported);
	DML_LOG_VERBOSE("DML::%s: USRRetrainingSupport = %u\n", __func__, *p->USRRetrainingSupport);
}

void dcn5_calculate_pstate_keepout_dst_lines(
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_watermarks *watermarks,
		unsigned int pstate_keepout_dst_lines[])
{
	const struct dml2_stream_parameters *stream_descriptor;
	unsigned int i;

	for (i = 0; i < display_cfg->num_planes; i++) {
		(void)display_cfg;
		stream_descriptor = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[i].stream_index];

		pstate_keepout_dst_lines[i] =
				(unsigned int)math_ceil(watermarks->DRAMClockChangeWatermark / ((double)stream_descriptor->timing.h_total * 1000.0 / (double)stream_descriptor->timing.pixel_clock_khz));

		if (pstate_keepout_dst_lines[i] > stream_descriptor->timing.v_total - 1) {
			pstate_keepout_dst_lines[i] = stream_descriptor->timing.v_total - 1;
		}
	}
}

void dcn5_calculate_vactive_det_fill_latency(
		const struct dml2_display_cfg *display_cfg,
		unsigned int num_active_planes,
		unsigned int bytes_required_l[],
		unsigned int bytes_required_c[],
		double dcc_dram_bw_nom_overhead_factor_p0[],
		double dcc_dram_bw_nom_overhead_factor_p1[],
		double surface_read_bw_l[],
		double surface_read_bw_c[],
		double surface_avg_vactive_required_bw[],
		double surface_peak_required_bw[],
		/* output */
		double vactive_det_fill_delay_us[])
{
	(void)display_cfg;
	double effective_excess_bandwidth;
	double effective_excess_bandwidth_l;
	double effective_excess_bandwidth_c;
	unsigned int plane_index;

	for (plane_index = 0; plane_index < num_active_planes; plane_index++) {
		if (bytes_required_l[plane_index] <= 0 && bytes_required_c[plane_index] <= 0) {
			continue;
		}

		vactive_det_fill_delay_us[plane_index] = 0.0;
		effective_excess_bandwidth = (surface_peak_required_bw[plane_index] - surface_avg_vactive_required_bw[plane_index]);

		effective_excess_bandwidth_l = effective_excess_bandwidth * surface_read_bw_l[plane_index]
			/ (surface_read_bw_l[plane_index] + surface_read_bw_c[plane_index]) / dcc_dram_bw_nom_overhead_factor_p0[plane_index];
		if (effective_excess_bandwidth_l > 0.0) {
			vactive_det_fill_delay_us[plane_index] = math_max2(vactive_det_fill_delay_us[plane_index], bytes_required_l[plane_index] / effective_excess_bandwidth_l);
		}

		effective_excess_bandwidth_c = effective_excess_bandwidth * surface_read_bw_c[plane_index]
			/ (surface_read_bw_l[plane_index] + surface_read_bw_c[plane_index]) / dcc_dram_bw_nom_overhead_factor_p1[plane_index];
		if (effective_excess_bandwidth_c > 0.0) {
			vactive_det_fill_delay_us[plane_index] = math_max2(vactive_det_fill_delay_us[plane_index], bytes_required_c[plane_index] / effective_excess_bandwidth_c);
		}
	}
}

double dcn5_calculate_write_back_delay(
		enum dml2_source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackVTaps,
		unsigned int WritebackVTapsChroma,
		unsigned int WritebackDestinationWidth,
		unsigned int WritebackDestinationHeight,
		unsigned int WritebackSourceWidth,
		unsigned int WritebackSourceHeight,
		unsigned int HTotal)
{
	(void)WritebackHRatio;
	double CalculateWriteBackDelay;
	double Line_length;
	double Output_lines_last_notclamped;
	double WritebackVInit;

	WritebackVInit = (WritebackVRatio + WritebackVTaps + 1) / 2;
	Line_length = math_max2((double)WritebackDestinationWidth, math_ceil2((double)WritebackDestinationWidth / 6.0, 1.0) * WritebackVTaps);
	Output_lines_last_notclamped = WritebackDestinationHeight - 1 - math_ceil2(((double)WritebackSourceHeight - (double)WritebackVInit) / (double)WritebackVRatio, 1.0);
	if (Output_lines_last_notclamped < 0)
		CalculateWriteBackDelay = 0;
	else
		CalculateWriteBackDelay = Output_lines_last_notclamped * Line_length + (HTotal - WritebackSourceWidth) + 80;

	double v_ratio_chroma;
	double output_width_chroma;
	double output_height_chroma;

	if (WritebackPixelFormat == dml2_420_8 || WritebackPixelFormat == dml2_422_packed_8
		|| WritebackPixelFormat == dml2_420_10 || WritebackPixelFormat == dml2_422_packed_10)
		output_width_chroma = 0.5 * WritebackDestinationWidth;
	else
		output_width_chroma = WritebackDestinationWidth;

	if (WritebackPixelFormat == dml2_420_8 || WritebackPixelFormat == dml2_420_10) {
		v_ratio_chroma = 2.0 * WritebackVRatio;
		output_height_chroma = 0.5 * WritebackDestinationHeight;
	} else {
		v_ratio_chroma = WritebackVRatio;
		output_height_chroma = WritebackDestinationHeight;
	}

	double CalculateWriteBackDelay_chroma;
	double Line_length_chroma;
	double Output_lines_last_notclamped_chroma;
	double WritebackVInit_chroma;

	WritebackVInit_chroma = (v_ratio_chroma + WritebackVTapsChroma + 1) / 2;
	Line_length_chroma = math_max2((double)output_height_chroma, math_ceil2((double)output_width_chroma / 6.0, 1.0) * WritebackVTapsChroma);
	Output_lines_last_notclamped_chroma = output_height_chroma - 1 - math_ceil2(((double)WritebackSourceHeight - (double)WritebackVInit_chroma) / (double)v_ratio_chroma, 1.0);
	if (Output_lines_last_notclamped_chroma < 0)
		CalculateWriteBackDelay_chroma = 0;
	else
		CalculateWriteBackDelay_chroma = Output_lines_last_notclamped_chroma * Line_length_chroma + (HTotal - WritebackSourceWidth) + 80;

	return math_max2(CalculateWriteBackDelay, CalculateWriteBackDelay_chroma);
}

void dcn5_calculate_meta_and_pte_times(struct dml2_core_shared_CalculateMetaAndPTETimes_params *p)
{
	unsigned int meta_chunk_width;
	unsigned int min_meta_chunk_width;
	unsigned int meta_chunk_per_row_int;
	unsigned int meta_row_remainder;
	unsigned int meta_chunk_threshold;
	unsigned int meta_chunks_per_row_ub;
	unsigned int meta_chunk_width_chroma;
	unsigned int min_meta_chunk_width_chroma;
	unsigned int meta_chunk_per_row_int_chroma;
	unsigned int meta_row_remainder_chroma;
	unsigned int meta_chunk_threshold_chroma;
	unsigned int meta_chunks_per_row_ub_chroma;
	unsigned int dpte_group_width_luma;
	unsigned int dpte_groups_per_row_luma_ub;
	unsigned int dpte_group_width_chroma;
	unsigned int dpte_groups_per_row_chroma_ub;
	double pixel_clock_mhz;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->DST_Y_PER_PTE_ROW_NOM_L[k] = p->dpte_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		if (p->BytePerPixelC[k] == 0) {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		} else {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = p->dpte_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		}
		p->DST_Y_PER_META_ROW_NOM_L[k] = p->meta_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		if (p->BytePerPixelC[k] == 0) {
			p->DST_Y_PER_META_ROW_NOM_C[k] = 0;
		} else {
			p->DST_Y_PER_META_ROW_NOM_C[k] = p->meta_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->plane_descriptors[k].surface.dcc.enable == true && p->mrq_present) {
			meta_chunk_width = p->MetaChunkSize * 1024 * 256 / p->BytePerPixelY[k] / p->meta_row_height[k];
			min_meta_chunk_width = p->MinMetaChunkSizeBytes * 256 / p->BytePerPixelY[k] / p->meta_row_height[k];
			meta_chunk_per_row_int = p->meta_row_width[k] / meta_chunk_width;
			meta_row_remainder = p->meta_row_width[k] % meta_chunk_width;
			if (!dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
				meta_chunk_threshold = 2 * min_meta_chunk_width - p->meta_req_width[k];
			} else {
				meta_chunk_threshold = 2 * min_meta_chunk_width - p->meta_req_height[k];
			}
			if (meta_row_remainder <= meta_chunk_threshold) {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
			} else {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;
			}
			p->TimePerMetaChunkNominal[k] = p->meta_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio *
					p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					(p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub;
			p->TimePerMetaChunkVBlank[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					(p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub;
			p->TimePerMetaChunkFlip[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					(p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub;
			if (p->BytePerPixelC[k] == 0) {
				p->TimePerChromaMetaChunkNominal[k] = 0;
				p->TimePerChromaMetaChunkVBlank[k] = 0;
				p->TimePerChromaMetaChunkFlip[k] = 0;
			} else {
				meta_chunk_width_chroma = p->MetaChunkSize * 1024 * 256 / p->BytePerPixelC[k] / p->meta_row_height_chroma[k];
				min_meta_chunk_width_chroma = p->MinMetaChunkSizeBytes * 256 / p->BytePerPixelC[k] / p->meta_row_height_chroma[k];
				meta_chunk_per_row_int_chroma = (unsigned int)((double)p->meta_row_width_chroma[k] / meta_chunk_width_chroma);
				meta_row_remainder_chroma = p->meta_row_width_chroma[k] % meta_chunk_width_chroma;
				if (!dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma - p->meta_req_width_chroma[k];
				} else {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma - p->meta_req_height_chroma[k];
				}
				if (meta_row_remainder_chroma <= meta_chunk_threshold_chroma) {
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 1;
				} else {
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 2;
				}
				p->TimePerChromaMetaChunkNominal[k] = p->meta_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub_chroma;
				p->TimePerChromaMetaChunkVBlank[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub_chroma;
				p->TimePerChromaMetaChunkFlip[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub_chroma;
			}
		} else {
			p->TimePerMetaChunkNominal[k] = 0;
			p->TimePerMetaChunkVBlank[k] = 0;
			p->TimePerMetaChunkFlip[k] = 0;
			p->TimePerChromaMetaChunkNominal[k] = 0;
			p->TimePerChromaMetaChunkVBlank[k] = 0;
			p->TimePerChromaMetaChunkFlip[k] = 0;
		}

		DML_LOG_VERBOSE("DML::%s: k=%d, DST_Y_PER_META_ROW_NOM_L = %f\n", __func__, k, p->DST_Y_PER_META_ROW_NOM_L[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, DST_Y_PER_META_ROW_NOM_C = %f\n", __func__, k, p->DST_Y_PER_META_ROW_NOM_C[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, TimePerMetaChunkNominal  = %f\n", __func__, k, p->TimePerMetaChunkNominal[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, TimePerMetaChunkVBlank	 = %f\n", __func__, k, p->TimePerMetaChunkVBlank[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, TimePerMetaChunkFlip		 = %f\n", __func__, k, p->TimePerMetaChunkFlip[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, TimePerChromaMetaChunkNominal= %f\n", __func__, k, p->TimePerChromaMetaChunkNominal[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, TimePerChromaMetaChunkVBlank = %f\n", __func__, k, p->TimePerChromaMetaChunkVBlank[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, TimePerChromaMetaChunkFlip	 = %f\n", __func__, k, p->TimePerChromaMetaChunkFlip[k]);
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->DST_Y_PER_PTE_ROW_NOM_L[k] = p->dpte_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		if (p->BytePerPixelC[k] == 0) {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		} else {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = p->dpte_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		pixel_clock_mhz = ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

		if (p->display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut)
			p->time_per_tdlut_group[k] = 2 * p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / p->tdlut_groups_per_2row_ub[k];
		else
			p->time_per_tdlut_group[k] = 0;

		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_tdlut_group = %f\n", __func__, k, p->time_per_tdlut_group[k]);

		if (p->display_cfg->gpuvm_enable == true) {
			if (!dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
				dpte_group_width_luma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeY[k] * p->PixelPTEReqWidthY[k]);
			} else {
				dpte_group_width_luma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeY[k] * p->PixelPTEReqHeightY[k]);
			}
			if (p->use_one_row_for_frame[k]) {
				dpte_groups_per_row_luma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_luma_ub[k] / (double)dpte_group_width_luma / 2.0, 1.0));
			} else {
				dpte_groups_per_row_luma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_luma_ub[k] / (double)dpte_group_width_luma, 1.0));
			}
			if (dpte_groups_per_row_luma_ub <= 2) {
				dpte_groups_per_row_luma_ub = dpte_groups_per_row_luma_ub + 1;
			}
			DML_LOG_VERBOSE("DML::%s: k=%u, use_one_row_for_frame = %u\n", __func__, k, p->use_one_row_for_frame[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, dpte_group_bytes = %u\n", __func__, k, p->dpte_group_bytes[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PTERequestSizeY = %u\n", __func__, k, p->PTERequestSizeY[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEReqWidthY = %u\n", __func__, k, p->PixelPTEReqWidthY[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, PixelPTEReqHeightY = %u\n", __func__, k, p->PixelPTEReqHeightY[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, dpte_row_width_luma_ub = %u\n", __func__, k, p->dpte_row_width_luma_ub[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, dpte_group_width_luma = %u\n", __func__, k, dpte_group_width_luma);
			DML_LOG_VERBOSE("DML::%s: k=%u, dpte_groups_per_row_luma_ub = %u\n", __func__, k, dpte_groups_per_row_luma_ub);

			p->time_per_pte_group_nom_luma[k] = p->DST_Y_PER_PTE_ROW_NOM_L[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_luma_ub;
			p->time_per_pte_group_vblank_luma[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_luma_ub;
			p->time_per_pte_group_flip_luma[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_luma_ub;
			if (p->BytePerPixelC[k] == 0) {
				p->time_per_pte_group_nom_chroma[k] = 0;
				p->time_per_pte_group_vblank_chroma[k] = 0;
				p->time_per_pte_group_flip_chroma[k] = 0;
			} else {
				if (!dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
					dpte_group_width_chroma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeC[k] * p->PixelPTEReqWidthC[k]);
				} else {
					dpte_group_width_chroma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeC[k] * p->PixelPTEReqHeightC[k]);
				}

				if (p->use_one_row_for_frame[k]) {
					dpte_groups_per_row_chroma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_chroma_ub[k] / (double)dpte_group_width_chroma / 2.0, 1.0));
				} else {
					dpte_groups_per_row_chroma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_chroma_ub[k] / (double)dpte_group_width_chroma, 1.0));
				}
				if (dpte_groups_per_row_chroma_ub <= 2) {
					dpte_groups_per_row_chroma_ub = dpte_groups_per_row_chroma_ub + 1;
				}
				DML_LOG_VERBOSE("DML::%s: k=%u, dpte_row_width_chroma_ub = %u\n", __func__, k, p->dpte_row_width_chroma_ub[k]);
				DML_LOG_VERBOSE("DML::%s: k=%u, dpte_group_width_chroma = %u\n", __func__, k, dpte_group_width_chroma);
				DML_LOG_VERBOSE("DML::%s: k=%u, dpte_groups_per_row_chroma_ub = %u\n", __func__, k, dpte_groups_per_row_chroma_ub);

				p->time_per_pte_group_nom_chroma[k] = p->DST_Y_PER_PTE_ROW_NOM_C[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_chroma_ub;
				p->time_per_pte_group_vblank_chroma[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_chroma_ub;
				p->time_per_pte_group_flip_chroma[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_chroma_ub;
			}
		} else {
			p->time_per_pte_group_nom_luma[k] = 0;
			p->time_per_pte_group_vblank_luma[k] = 0;
			p->time_per_pte_group_flip_luma[k] = 0;
			p->time_per_pte_group_nom_chroma[k] = 0;
			p->time_per_pte_group_vblank_chroma[k] = 0;
			p->time_per_pte_group_flip_chroma[k] = 0;
		}
		DML_LOG_VERBOSE("DML::%s: k=%u, dst_y_per_row_vblank = %f\n", __func__, k, p->dst_y_per_row_vblank[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dst_y_per_row_flip = %f\n", __func__, k, p->dst_y_per_row_flip[k]);

		DML_LOG_VERBOSE("DML::%s: k=%u, DST_Y_PER_PTE_ROW_NOM_L = %f\n", __func__, k, p->DST_Y_PER_PTE_ROW_NOM_L[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, DST_Y_PER_PTE_ROW_NOM_C = %f\n", __func__, k, p->DST_Y_PER_PTE_ROW_NOM_C[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_pte_group_nom_luma = %f\n", __func__, k, p->time_per_pte_group_nom_luma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_pte_group_vblank_luma = %f\n", __func__, k, p->time_per_pte_group_vblank_luma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_pte_group_flip_luma = %f\n", __func__, k, p->time_per_pte_group_flip_luma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_pte_group_nom_chroma = %f\n", __func__, k, p->time_per_pte_group_nom_chroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_pte_group_vblank_chroma = %f\n", __func__, k, p->time_per_pte_group_vblank_chroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, time_per_pte_group_flip_chroma = %f\n", __func__, k, p->time_per_pte_group_flip_chroma[k]);
	}
}

void dcn5_calculate_vm_group_and_request_times(
		const struct dml2_display_cfg *display_cfg,
		unsigned int NumberOfActiveSurfaces,
		unsigned int BytePerPixelC[],
		double dst_y_per_vm_vblank[],
		double dst_y_per_vm_flip[],
		unsigned int dpte_row_width_luma_ub[],
		unsigned int dpte_row_width_chroma_ub[],
		unsigned int vm_group_bytes[],
		unsigned int dpde0_bytes_per_frame_ub_l[],
		unsigned int dpde0_bytes_per_frame_ub_c[],
		unsigned int tdlut_pte_bytes_per_frame[],
		unsigned int meta_pte_bytes_per_frame_ub_l[],
		unsigned int meta_pte_bytes_per_frame_ub_c[],
		bool mrq_present,

		// Output
		double TimePerVMGroupVBlank[],
		double TimePerVMGroupFlip[],
		double TimePerVMRequestVBlank[],
		double TimePerVMRequestFlip[])
{
	(void)dpte_row_width_luma_ub;
	(void)dpte_row_width_chroma_ub;
	unsigned int num_group_per_lower_vm_stage = 0;
	unsigned int num_req_per_lower_vm_stage = 0;
	unsigned int num_group_per_lower_vm_stage_flip;
	unsigned int num_group_per_lower_vm_stage_pref;
	unsigned int num_req_per_lower_vm_stage_flip;
	unsigned int num_req_per_lower_vm_stage_pref;
	double line_time;

	DML_LOG_VERBOSE("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		double pixel_clock_mhz = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		bool dcc_mrq_enable = display_cfg->plane_descriptors[k].surface.dcc.enable && mrq_present;
		DML_LOG_VERBOSE("DML::%s: k=%u, dcc_mrq_enable = %u\n", __func__, k, dcc_mrq_enable);
		DML_LOG_VERBOSE("DML::%s: k=%u, vm_group_bytes = %u\n", __func__, k, vm_group_bytes[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dpde0_bytes_per_frame_ub_l = %u\n", __func__, k, dpde0_bytes_per_frame_ub_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, dpde0_bytes_per_frame_ub_c = %u\n", __func__, k, dpde0_bytes_per_frame_ub_c[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, meta_pte_bytes_per_frame_ub_l = %d\n", __func__, k, meta_pte_bytes_per_frame_ub_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, meta_pte_bytes_per_frame_ub_c = %d\n", __func__, k, meta_pte_bytes_per_frame_ub_c[k]);

		if (display_cfg->gpuvm_enable) {
			if (display_cfg->gpuvm_max_page_table_levels >= 2) {
				num_group_per_lower_vm_stage += (unsigned int) math_ceil2((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);

				if (BytePerPixelC[k] > 0)
					num_group_per_lower_vm_stage += (unsigned int) math_ceil2((double) (dpde0_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
			}

			if (dcc_mrq_enable) {
				if (BytePerPixelC[k] > 0) {
					num_group_per_lower_vm_stage += (unsigned int)(2.0 /*for each mpde0 group*/ + math_ceil2((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1) +
							math_ceil2((double) (meta_pte_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1));
				} else {
					num_group_per_lower_vm_stage += (unsigned int)(1.0 + math_ceil2((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1));
				}
			}

			num_group_per_lower_vm_stage_flip = num_group_per_lower_vm_stage;
			num_group_per_lower_vm_stage_pref = num_group_per_lower_vm_stage;

			if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut && display_cfg->gpuvm_enable) {
				num_group_per_lower_vm_stage_pref += (unsigned int) math_ceil2(tdlut_pte_bytes_per_frame[k] / vm_group_bytes[k], 1);
				if (display_cfg->gpuvm_max_page_table_levels >= 2)
					num_group_per_lower_vm_stage_pref += 1; // tdpe0 group
			}

			if (display_cfg->gpuvm_max_page_table_levels >= 2) {
				num_req_per_lower_vm_stage += dpde0_bytes_per_frame_ub_l[k] / 64;
				if (BytePerPixelC[k] > 0)
					num_req_per_lower_vm_stage += dpde0_bytes_per_frame_ub_c[k];
			}

			if (dcc_mrq_enable) {
				num_req_per_lower_vm_stage += meta_pte_bytes_per_frame_ub_l[k] / 64;
				if (BytePerPixelC[k] > 0)
					num_req_per_lower_vm_stage += meta_pte_bytes_per_frame_ub_c[k] / 64;
			}

			num_req_per_lower_vm_stage_flip = num_req_per_lower_vm_stage;
			num_req_per_lower_vm_stage_pref = num_req_per_lower_vm_stage;

			if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut && display_cfg->gpuvm_enable) {
				num_req_per_lower_vm_stage_pref += tdlut_pte_bytes_per_frame[k] / 64;
			}

			line_time = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz;

			if (num_group_per_lower_vm_stage_pref > 0)
				TimePerVMGroupVBlank[k] = dst_y_per_vm_vblank[k] * line_time / num_group_per_lower_vm_stage_pref;
			else
				TimePerVMGroupVBlank[k] = 0;

			if (num_group_per_lower_vm_stage_flip > 0)
				TimePerVMGroupFlip[k] = dst_y_per_vm_flip[k] * line_time / num_group_per_lower_vm_stage_flip;
			else
				TimePerVMGroupFlip[k] = 0;

			if (num_req_per_lower_vm_stage_pref > 0)
				TimePerVMRequestVBlank[k] = dst_y_per_vm_vblank[k] * line_time / num_req_per_lower_vm_stage_pref;
			else
				TimePerVMRequestVBlank[k] = 0.0;
			if (num_req_per_lower_vm_stage_flip > 0)
				TimePerVMRequestFlip[k] = dst_y_per_vm_flip[k] * line_time / num_req_per_lower_vm_stage_flip;
			else
				TimePerVMRequestFlip[k] = 0.0;

			DML_LOG_VERBOSE("DML::%s: k=%u, dst_y_per_vm_vblank = %f\n", __func__, k, dst_y_per_vm_vblank[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, dst_y_per_vm_flip = %f\n", __func__, k, dst_y_per_vm_flip[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, line_time = %f\n", __func__, k, line_time);
			DML_LOG_VERBOSE("DML::%s: k=%u, num_group_per_lower_vm_stage_pref = %d\n", __func__, k, num_group_per_lower_vm_stage_pref);
			DML_LOG_VERBOSE("DML::%s: k=%u, num_group_per_lower_vm_stage_flip = %d\n", __func__, k, num_group_per_lower_vm_stage_flip);
			DML_LOG_VERBOSE("DML::%s: k=%u, num_req_per_lower_vm_stage_pref = %d\n", __func__, k, num_req_per_lower_vm_stage_pref);
			DML_LOG_VERBOSE("DML::%s: k=%u, num_req_per_lower_vm_stage_flip = %d\n", __func__, k, num_req_per_lower_vm_stage_flip);

			if (display_cfg->gpuvm_max_page_table_levels > 2) {
				TimePerVMGroupVBlank[k] = TimePerVMGroupVBlank[k] / 2;
				TimePerVMGroupFlip[k] = TimePerVMGroupFlip[k] / 2;
				TimePerVMRequestVBlank[k] = TimePerVMRequestVBlank[k] / 2;
				TimePerVMRequestFlip[k] = TimePerVMRequestFlip[k] / 2;
			}

		} else {
			TimePerVMGroupVBlank[k] = 0;
			TimePerVMGroupFlip[k] = 0;
			TimePerVMRequestVBlank[k] = 0;
			TimePerVMRequestFlip[k] = 0;
		}
		DML_LOG_VERBOSE("DML::%s: k=%u, TimePerVMGroupVBlank = %f\n", __func__, k, TimePerVMGroupVBlank[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, TimePerVMGroupFlip = %f\n", __func__, k, TimePerVMGroupFlip[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, TimePerVMRequestVBlank = %f\n", __func__, k, TimePerVMRequestVBlank[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, TimePerVMRequestFlip = %f\n", __func__, k, TimePerVMRequestFlip[k]);
	}
}

void dcn5_calculate_stutter_efficiency(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateStutterEfficiency_params *p)
{
	struct dml2_core_calcs_CalculateStutterEfficiency_locals *l = &scratch->CalculateStutterEfficiency_locals;

	unsigned int TotalNumberOfActiveOTG = 0;
	double SinglePixelClock = 0;
	unsigned int SingleHTotal = 0;
	unsigned int SingleVTotal = 0;
	bool SameTiming = true;
	bool at_least_one_single_pipe_single_plane_surface = false;
	bool FoundCriticalSurface = false;

	memset(l, 0, sizeof(struct dml2_core_calcs_CalculateStutterEfficiency_locals));

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->plane_descriptors[k].surface.dcc.enable == true) {
			if ((dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockWidth256BytesY[k] > p->SwathHeightY[k]) || (!dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockHeight256BytesY[k] > p->SwathHeightY[k]) || p->DCCYMaxUncompressedBlock[k] < 256) {
				l->MaximumEffectiveCompressionLuma = 2;
			} else {
				l->MaximumEffectiveCompressionLuma = 4;
			}
			l->TotalCompressedReadBandwidth = l->TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] / math_min2(p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane0, l->MaximumEffectiveCompressionLuma);
			DML_LOG_VERBOSE("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, p->ReadBandwidthSurfaceLuma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, NetDCCRateLuma = %f\n", __func__, k, p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane0);
			DML_LOG_VERBOSE("DML::%s: k=%u, MaximumEffectiveCompressionLuma = %f\n", __func__, k, l->MaximumEffectiveCompressionLuma);
			l->TotalZeroSizeRequestReadBandwidth = l->TotalZeroSizeRequestReadBandwidth + p->ReadBandwidthSurfaceLuma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane0;
			l->TotalZeroSizeCompressedReadBandwidth = l->TotalZeroSizeCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane0 / l->MaximumEffectiveCompressionLuma;

			if (p->ReadBandwidthSurfaceChroma[k] > 0) {
				if ((dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockWidth256BytesC[k] > p->SwathHeightC[k]) || (!dml2_core_utils_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockHeight256BytesC[k] > p->SwathHeightC[k]) || p->DCCCMaxUncompressedBlock[k] < 256) {
					l->MaximumEffectiveCompressionChroma = 2;
				} else {
					l->MaximumEffectiveCompressionChroma = 4;
				}
				l->TotalCompressedReadBandwidth = l->TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceChroma[k] / math_min2(p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane1, l->MaximumEffectiveCompressionChroma);
				DML_LOG_VERBOSE("DML::%s: k=%u, ReadBandwidthSurfaceChroma = %f\n", __func__, k, p->ReadBandwidthSurfaceChroma[k]);
				DML_LOG_VERBOSE("DML::%s: k=%u, NetDCCRateChroma = %f\n", __func__, k, p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane1);
				DML_LOG_VERBOSE("DML::%s: k=%u, MaximumEffectiveCompressionChroma = %f\n", __func__, k, l->MaximumEffectiveCompressionChroma);
				l->TotalZeroSizeRequestReadBandwidth = l->TotalZeroSizeRequestReadBandwidth + p->ReadBandwidthSurfaceChroma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane1;
				l->TotalZeroSizeCompressedReadBandwidth = l->TotalZeroSizeCompressedReadBandwidth + p->ReadBandwidthSurfaceChroma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane1 / l->MaximumEffectiveCompressionChroma;
			}
		} else {
			l->TotalCompressedReadBandwidth = l->TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] + p->ReadBandwidthSurfaceChroma[k];
		}
		l->TotalRowReadBandwidth = l->TotalRowReadBandwidth + p->DPPPerSurface[k] * (p->meta_row_bw[k] + p->dpte_row_bw[k]);
	}

	l->AverageDCCCompressionRate = p->TotalDataReadBandwidth / l->TotalCompressedReadBandwidth;
	l->AverageDCCZeroSizeFraction = l->TotalZeroSizeRequestReadBandwidth / p->TotalDataReadBandwidth;

	DML_LOG_VERBOSE("DML::%s: UnboundedRequestEnabled = %u\n", __func__, p->UnboundedRequestEnabled);
	DML_LOG_VERBOSE("DML::%s: TotalCompressedReadBandwidth = %f\n", __func__, l->TotalCompressedReadBandwidth);
	DML_LOG_VERBOSE("DML::%s: TotalZeroSizeRequestReadBandwidth = %f\n", __func__, l->TotalZeroSizeRequestReadBandwidth);
	DML_LOG_VERBOSE("DML::%s: TotalZeroSizeCompressedReadBandwidth = %f\n", __func__, l->TotalZeroSizeCompressedReadBandwidth);
	DML_LOG_VERBOSE("DML::%s: MaximumEffectiveCompressionLuma = %f\n", __func__, l->MaximumEffectiveCompressionLuma);
	DML_LOG_VERBOSE("DML::%s: MaximumEffectiveCompressionChroma = %f\n", __func__, l->MaximumEffectiveCompressionChroma);
	DML_LOG_VERBOSE("DML::%s: AverageDCCCompressionRate = %f\n", __func__, l->AverageDCCCompressionRate);
	DML_LOG_VERBOSE("DML::%s: AverageDCCZeroSizeFraction = %f\n", __func__, l->AverageDCCZeroSizeFraction);

	DML_LOG_VERBOSE("DML::%s: CompbufReservedSpace64B = %u (%f kbytes)\n", __func__, p->CompbufReservedSpace64B, p->CompbufReservedSpace64B * 64 / 1024.0);
	DML_LOG_VERBOSE("DML::%s: CompbufReservedSpaceZs = %u\n", __func__, p->CompbufReservedSpaceZs);
	DML_LOG_VERBOSE("DML::%s: CompressedBufferSizeInkByte = %u kbytes\n", __func__, p->CompressedBufferSizeInkByte);
	DML_LOG_VERBOSE("DML::%s: ROBBufferSizeInKByte = %u kbytes\n", __func__, p->ROBBufferSizeInKByte);
	if (l->AverageDCCZeroSizeFraction == 1) {
		l->AverageZeroSizeCompressionRate = l->TotalZeroSizeRequestReadBandwidth / l->TotalZeroSizeCompressedReadBandwidth;
		l->EffectiveCompressedBufferSize = (double)p->MetaFIFOSizeInKEntries * 1024 * 64 * l->AverageZeroSizeCompressionRate + ((double)p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 * l->AverageZeroSizeCompressionRate;


	} else if (l->AverageDCCZeroSizeFraction > 0) {
		l->AverageZeroSizeCompressionRate = l->TotalZeroSizeRequestReadBandwidth / l->TotalZeroSizeCompressedReadBandwidth;
		l->EffectiveCompressedBufferSize = math_min2((double)p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate,
				(double)p->MetaFIFOSizeInKEntries * 1024 * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate + 1 / l->AverageDCCCompressionRate)) +
						(p->rob_alloc_compressed ? math_min2(((double)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * l->AverageDCCCompressionRate,
								((double)p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate))
								: ((double)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64));


		DML_LOG_VERBOSE("DML::%s: min 1 = %f\n", __func__, p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate);
		DML_LOG_VERBOSE("DML::%s: min 2 = %f\n", __func__, p->MetaFIFOSizeInKEntries * 1024 * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate + 1 / l->AverageDCCCompressionRate));
		DML_LOG_VERBOSE("DML::%s: min 3 = %d\n", __func__, (p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64));
		DML_LOG_VERBOSE("DML::%s: min 4 = %f\n", __func__, (p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate));
	} else {
		l->EffectiveCompressedBufferSize = math_min2((double)p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate,
				(double)p->MetaFIFOSizeInKEntries * 1024 * 64 * l->AverageDCCCompressionRate) +
						((double)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * (p->rob_alloc_compressed ? l->AverageDCCCompressionRate : 1.0);

		DML_LOG_VERBOSE("DML::%s: min 1 = %f\n", __func__, p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate);
		DML_LOG_VERBOSE("DML::%s: min 2 = %f\n", __func__, p->MetaFIFOSizeInKEntries * 1024 * 64 * l->AverageDCCCompressionRate);
	}

	DML_LOG_VERBOSE("DML::%s: MetaFIFOSizeInKEntries = %u\n", __func__, p->MetaFIFOSizeInKEntries);
	DML_LOG_VERBOSE("DML::%s: ZeroSizeBufferEntries = %u\n", __func__, p->ZeroSizeBufferEntries);
	DML_LOG_VERBOSE("DML::%s: AverageZeroSizeCompressionRate = %f\n", __func__, l->AverageZeroSizeCompressionRate);
	DML_LOG_VERBOSE("DML::%s: EffectiveCompressedBufferSize = %f (%f kbytes)\n", __func__, l->EffectiveCompressedBufferSize, l->EffectiveCompressedBufferSize / 1024.0);

	*p->StutterPeriod = 0;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		l->LinesInDETY = ((double)p->DETBufferSizeY[k] + (p->UnboundedRequestEnabled == true ? l->EffectiveCompressedBufferSize : 0) * p->ReadBandwidthSurfaceLuma[k] / p->TotalDataReadBandwidth) / p->BytePerPixelDETY[k] / p->SwathWidthY[k];
		l->LinesInDETYRoundedDownToSwath = math_floor2(l->LinesInDETY, p->SwathHeightY[k]);
		l->DETBufferingTimeY = l->LinesInDETYRoundedDownToSwath * ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		at_least_one_single_pipe_single_plane_surface |= (p->DPPPerSurface[k] == 1) && (p->ReadBandwidthSurfaceChroma[k] == 0);
		DML_LOG_VERBOSE("DML::%s: k=%u, DETBufferSizeY = %u (%u kbytes)\n", __func__, k, p->DETBufferSizeY[k], p->DETBufferSizeY[k] / 1024);
		DML_LOG_VERBOSE("DML::%s: k=%u, BytePerPixelDETY = %f\n", __func__, k, p->BytePerPixelDETY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, SwathWidthY = %u\n", __func__, k, p->SwathWidthY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, p->ReadBandwidthSurfaceLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, TotalDataReadBandwidth = %f\n", __func__, k, p->TotalDataReadBandwidth);
		DML_LOG_VERBOSE("DML::%s: k=%u, LinesInDETY = %f\n", __func__, k, l->LinesInDETY);
		DML_LOG_VERBOSE("DML::%s: k=%u, LinesInDETYRoundedDownToSwath = %f\n", __func__, k, l->LinesInDETYRoundedDownToSwath);
		DML_LOG_VERBOSE("DML::%s: k=%u, VRatio = %f\n", __func__, k, p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u, DETBufferingTimeY = %f\n", __func__, k, l->DETBufferingTimeY);

		if (!FoundCriticalSurface || l->DETBufferingTimeY < *p->StutterPeriod) {
			bool isInterlaceTiming = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.interlaced && !p->ProgressiveToInterlaceUnitInOPP;

			FoundCriticalSurface = true;
			*p->StutterPeriod = l->DETBufferingTimeY;
			l->FrameTimeCriticalSurface = (isInterlaceTiming ? math_floor2((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total / 2.0, 1.0) : p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total) * (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
			l->VActiveTimeCriticalSurface = (isInterlaceTiming ? math_floor2((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_active / 2.0, 1.0) : p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_active) * (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
			l->BytePerPixelYCriticalSurface = p->BytePerPixelY[k];
			l->SwathWidthYCriticalSurface = p->SwathWidthY[k];
			l->SwathHeightYCriticalSurface = p->SwathHeightY[k];
			l->BlockWidth256BytesYCriticalSurface = p->BlockWidth256BytesY[k];
			l->DETBufferSizeYCriticalSurface = p->DETBufferSizeY[k];
			l->MinTTUVBlankCriticalSurface = p->MinTTUVBlank[k];
			l->SinglePlaneCriticalSurface = (p->ReadBandwidthSurfaceChroma[k] == 0);
			l->SinglePipeCriticalSurface = (p->DPPPerSurface[k] == 1);

			DML_LOG_VERBOSE("DML::%s: k=%u, FoundCriticalSurface = %u\n", __func__, k, FoundCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, StutterPeriod = %f\n", __func__, k, *p->StutterPeriod);
			DML_LOG_VERBOSE("DML::%s: k=%u, MinTTUVBlankCriticalSurface = %f\n", __func__, k, l->MinTTUVBlankCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, FrameTimeCriticalSurface= %f\n", __func__, k, l->FrameTimeCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, VActiveTimeCriticalSurface = %f\n", __func__, k, l->VActiveTimeCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, BytePerPixelYCriticalSurface = %u\n", __func__, k, l->BytePerPixelYCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, SwathWidthYCriticalSurface = %f\n", __func__, k, l->SwathWidthYCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, SwathHeightYCriticalSurface = %f\n", __func__, k, l->SwathHeightYCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, BlockWidth256BytesYCriticalSurface = %u\n", __func__, k, l->BlockWidth256BytesYCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, SinglePlaneCriticalSurface = %u\n", __func__, k, l->SinglePlaneCriticalSurface);
			DML_LOG_VERBOSE("DML::%s: k=%u, SinglePipeCriticalSurface = %u\n", __func__, k, l->SinglePipeCriticalSurface);
		}
	}

	// for bounded req, the stutter period is calculated only based on DET size, but during burst there can be some return inside ROB/compressed buffer
	// stutter period is calculated only on the det sizing
	// if (cdb + rob >= det) the stutter burst will be absorbed by the cdb + rob which is before decompress
	// else
	// the cdb + rob part will be in compressed rate with urg bw (idea bw)
	// the det part will be return at uncompressed rate with 64B/dcfclk
	//
	// for unbounded req, the stutter period should be calculated as total of CDB+ROB+DET, so the term "PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer"
	// should be == EffectiveCompressedBufferSize which will returned a compressed rate, the rest of stutter period is from the DET will be returned at uncompressed rate with 64B/dcfclk

	l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = math_min2(*p->StutterPeriod * p->TotalDataReadBandwidth, l->EffectiveCompressedBufferSize);
	DML_LOG_VERBOSE("DML::%s: AverageDCCCompressionRate = %f\n", __func__, l->AverageDCCCompressionRate);
	DML_LOG_VERBOSE("DML::%s: StutterPeriod*TotalDataReadBandwidth = %f (%f kbytes)\n", __func__, *p->StutterPeriod * p->TotalDataReadBandwidth, (*p->StutterPeriod * p->TotalDataReadBandwidth) / 1024.0);
	DML_LOG_VERBOSE("DML::%s: EffectiveCompressedBufferSize = %f (%f kbytes)\n", __func__, l->EffectiveCompressedBufferSize, l->EffectiveCompressedBufferSize / 1024.0);
	DML_LOG_VERBOSE("DML::%s: PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = %f (%f kbytes)\n", __func__, l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer, l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / 1024);
	DML_LOG_VERBOSE("DML::%s: ReturnBW = %f\n", __func__, p->ReturnBW);
	DML_LOG_VERBOSE("DML::%s: TotalDataReadBandwidth = %f\n", __func__, p->TotalDataReadBandwidth);
	DML_LOG_VERBOSE("DML::%s: TotalRowReadBandwidth = %f\n", __func__, l->TotalRowReadBandwidth);
	DML_LOG_VERBOSE("DML::%s: DCFCLK = %f\n", __func__, p->DCFCLK);

	l->StutterBurstTime = l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer
			/ (p->ReturnBW * (p->hw_debug5 ? 1 : l->AverageDCCCompressionRate)) +
			(*p->StutterPeriod * p->TotalDataReadBandwidth - l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer)
			/ math_min2(p->DCFCLK * 64, p->ReturnBW * (p->hw_debug5 ? 1 : l->AverageDCCCompressionRate)) +
			*p->StutterPeriod * l->TotalRowReadBandwidth / p->ReturnBW;
	DML_LOG_VERBOSE("DML::%s: Part 1 = %f\n", __func__, l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / p->ReturnBW / (p->hw_debug5 ? 1 : l->AverageDCCCompressionRate));
	DML_LOG_VERBOSE("DML::%s: Part 2 = %f\n", __func__, (*p->StutterPeriod * p->TotalDataReadBandwidth - l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (p->DCFCLK * 64));
	DML_LOG_VERBOSE("DML::%s: Part 3 = %f\n", __func__, *p->StutterPeriod * l->TotalRowReadBandwidth / p->ReturnBW);
	DML_LOG_VERBOSE("DML::%s: StutterBurstTime = %f\n", __func__, l->StutterBurstTime);
	l->TotalActiveWriteback = 0;
	memset(l->stream_visited, 0, DML2_MAX_PLANES * sizeof(bool));

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (!l->stream_visited[p->display_cfg->plane_descriptors[k].stream_index]) {

			for (unsigned int j = 0; j < p->display_cfg->stream_descriptors[k].writeback.active_writebacks_per_stream; j++)
				l->TotalActiveWriteback = l->TotalActiveWriteback + 1;

			if (TotalNumberOfActiveOTG == 0) { // first otg
				SinglePixelClock = ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				SingleHTotal = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total;
				SingleVTotal = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total;
			} else if (SinglePixelClock != ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) ||
					SingleHTotal != p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total ||
					SingleVTotal != p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total) {
				SameTiming = false;
			}
			TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
			l->stream_visited[p->display_cfg->plane_descriptors[k].stream_index] = 1;
		}
	}

	if (l->TotalActiveWriteback == 0) {
		DML_LOG_VERBOSE("DML::%s: SRExitTime = %f\n", __func__, p->SRExitTime);
		DML_LOG_VERBOSE("DML::%s: SRExitZ8Time = %f\n", __func__, p->SRExitZ8Time);
		DML_LOG_VERBOSE("DML::%s: StutterPeriod = %f\n", __func__, *p->StutterPeriod);
		*p->StutterEfficiencyNotIncludingVBlank = math_max2(0., 1 - (p->SRExitTime + l->StutterBurstTime) / *p->StutterPeriod) * 100;
		*p->Z8StutterEfficiencyNotIncludingVBlank = math_max2(0., 1 - (p->SRExitZ8Time + l->StutterBurstTime) / *p->StutterPeriod) * 100;
		*p->NumberOfStutterBurstsPerFrame = (*p->StutterEfficiencyNotIncludingVBlank > 0 ? (unsigned int)(math_ceil2(l->VActiveTimeCriticalSurface / *p->StutterPeriod, 1)) : 0);
		*p->Z8NumberOfStutterBurstsPerFrame = (*p->Z8StutterEfficiencyNotIncludingVBlank > 0 ? (unsigned int)(math_ceil2(l->VActiveTimeCriticalSurface / *p->StutterPeriod, 1)) : 0);
	} else {
		*p->StutterEfficiencyNotIncludingVBlank = 0.;
		*p->Z8StutterEfficiencyNotIncludingVBlank = 0.;
		*p->NumberOfStutterBurstsPerFrame = 0;
		*p->Z8NumberOfStutterBurstsPerFrame = 0;
	}
	DML_LOG_VERBOSE("DML::%s: VActiveTimeCriticalSurface = %f\n", __func__, l->VActiveTimeCriticalSurface);
	DML_LOG_VERBOSE("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->StutterEfficiencyNotIncludingVBlank);
	DML_LOG_VERBOSE("DML::%s: Z8StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->Z8StutterEfficiencyNotIncludingVBlank);
	DML_LOG_VERBOSE("DML::%s: NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->NumberOfStutterBurstsPerFrame);
	DML_LOG_VERBOSE("DML::%s: Z8NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->Z8NumberOfStutterBurstsPerFrame);

	if (*p->StutterEfficiencyNotIncludingVBlank > 0) {
		if (!((p->SynchronizeTimings || TotalNumberOfActiveOTG == 1) && SameTiming)) {
			*p->StutterEfficiency = *p->StutterEfficiencyNotIncludingVBlank;
		} else {
			*p->StutterEfficiency = (1 - (*p->NumberOfStutterBurstsPerFrame * p->SRExitTime + l->StutterBurstTime * l->VActiveTimeCriticalSurface / *p->StutterPeriod) / l->FrameTimeCriticalSurface) * 100;
		}
	} else {
		*p->StutterEfficiency = 0;
		*p->NumberOfStutterBurstsPerFrame = 0;
	}

	if (*p->Z8StutterEfficiencyNotIncludingVBlank > 0) {
		if (!((p->SynchronizeTimings || TotalNumberOfActiveOTG == 1) && SameTiming)) {
			*p->Z8StutterEfficiency = *p->Z8StutterEfficiencyNotIncludingVBlank;
		} else {
			*p->Z8StutterEfficiency = (1 - (*p->Z8NumberOfStutterBurstsPerFrame * p->SRExitZ8Time + l->StutterBurstTime * l->VActiveTimeCriticalSurface / *p->StutterPeriod) / l->FrameTimeCriticalSurface) * 100;
		}
	} else {
		*p->Z8StutterEfficiency = 0.;
		*p->Z8NumberOfStutterBurstsPerFrame = 0;
	}

	DML_LOG_VERBOSE("DML::%s: TotalNumberOfActiveOTG = %u\n", __func__, TotalNumberOfActiveOTG);
	DML_LOG_VERBOSE("DML::%s: SameTiming = %u\n", __func__, SameTiming);
	DML_LOG_VERBOSE("DML::%s: SynchronizeTimings = %u\n", __func__, p->SynchronizeTimings);
	DML_LOG_VERBOSE("DML::%s: LastZ8StutterPeriod = %f\n", __func__, *p->Z8StutterEfficiencyNotIncludingVBlank > 0 ? l->VActiveTimeCriticalSurface - (*p->Z8NumberOfStutterBurstsPerFrame - 1) * *p->StutterPeriod : 0);
	DML_LOG_VERBOSE("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, p->Z8StutterEnterPlusExitWatermark);
	DML_LOG_VERBOSE("DML::%s: StutterBurstTime = %f\n", __func__, l->StutterBurstTime);
	DML_LOG_VERBOSE("DML::%s: StutterPeriod = %f\n", __func__, *p->StutterPeriod);
	DML_LOG_VERBOSE("DML::%s: StutterEfficiency = %f\n", __func__, *p->StutterEfficiency);
	DML_LOG_VERBOSE("DML::%s: Z8StutterEfficiency = %f\n", __func__, *p->Z8StutterEfficiency);
	DML_LOG_VERBOSE("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->StutterEfficiencyNotIncludingVBlank);
	DML_LOG_VERBOSE("DML::%s: Z8NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->Z8NumberOfStutterBurstsPerFrame);

	*p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = !(!p->UnboundedRequestEnabled && (TotalNumberOfActiveOTG == 1) && at_least_one_single_pipe_single_plane_surface);

	DML_LOG_VERBOSE("DML::%s: DETBufferSizeYCriticalSurface = %u\n", __func__, l->DETBufferSizeYCriticalSurface);
	DML_LOG_VERBOSE("DML::%s: PixelChunkSizeInKByte = %u\n", __func__, p->PixelChunkSizeInKByte);
	DML_LOG_VERBOSE("DML::%s: DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = %u\n", __func__, *p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE);
}

void dcn5_check_urgent_bandwidth_support(
		double *frac_urg_bandwidth_nom,
		bool *bandwidth_support_ok,   // max of vm, prefetch, vactive all ok

		double non_urg_bandwidth_required,
		double urg_bandwidth_required,
		double urg_bandwidth_available)
{
	*bandwidth_support_ok = urg_bandwidth_required <= urg_bandwidth_available;
	*frac_urg_bandwidth_nom = non_urg_bandwidth_required / urg_bandwidth_available;
	*bandwidth_support_ok &= (*frac_urg_bandwidth_nom <= 1.0);

	DML_LOG_VERBOSE("DML::%s: frac_urg_bandwidth_nom = %f\n", __func__, *frac_urg_bandwidth_nom);
	DML_LOG_VERBOSE("DML::%s: bandwidth_support_ok = %d\n", __func__, *bandwidth_support_ok);
}

double dcn5_get_bandwidth_available_for_immediate_flip(
		double urg_bandwidth_required, // no flip
		double urg_bandwidth_available)
{
	double flip_bw_available_mbps = urg_bandwidth_available - urg_bandwidth_required;

	DML_LOG_VERBOSE("DML::%s: flip_bw_available_mbps = %f\n", __func__, flip_bw_available_mbps);

	return flip_bw_available_mbps;
}

void dcn5_check_immediate_flip_bandwidth_support(
		// Output
		double *frac_urg_bandwidth_flip,
		bool *flip_bandwidth_support_ok,

		// Input
		double urg_bandwidth_required_flip,
		double non_urg_bandwidth_required_flip,
		double urg_bandwidth_available)
{
	*frac_urg_bandwidth_flip = non_urg_bandwidth_required_flip / urg_bandwidth_available;
	*flip_bandwidth_support_ok = urg_bandwidth_available >= urg_bandwidth_required_flip;
	*flip_bandwidth_support_ok &= (*frac_urg_bandwidth_flip <= 1.0);

	DML_LOG_VERBOSE("DML::%s: frac_urg_bandwidth_flip = %f\n", __func__, *frac_urg_bandwidth_flip);
	DML_LOG_VERBOSE("DML::%s: flip_bandwidth_support_ok = %d\n", __func__, *flip_bandwidth_support_ok);
	DML_LOG_VERBOSE("DML::%s: urg_bandwidth_available=%f %s urg_bandwidth_required=%f\n",
					__func__, urg_bandwidth_available, (urg_bandwidth_available < urg_bandwidth_required_flip) ? "<" : ">=", urg_bandwidth_required_flip);
}

unsigned int dcn5_get_pipe_flip_bytes(
		double hostvm_inefficiency_factor,
		unsigned int vm_bytes,
		unsigned int dpte_row_bytes,
		unsigned int meta_row_bytes)
{
	unsigned int flip_bytes = 0;

	flip_bytes += (unsigned int) ((vm_bytes * hostvm_inefficiency_factor) + 2*meta_row_bytes);
	flip_bytes += (unsigned int) (2*dpte_row_bytes * hostvm_inefficiency_factor);

	return flip_bytes;
}

struct dml2_core_internal_g6_temp_read_blackouts_table {
	struct {
		unsigned int uclk_khz;
		unsigned int blackout_us;
	} entries[DML_MAX_CLK_TABLE_SIZE];
};

struct dml2_core_internal_g6_temp_read_blackouts_table core_dcn5_g6_temp_read_blackout_table = {
		.entries = {
				{
						.uclk_khz = 96000,
						.blackout_us = 23,
				},
				{
						.uclk_khz = 435000,
						.blackout_us = 10,
				},
				{
						.uclk_khz = 521000,
						.blackout_us = 10,
				},
				{
						.uclk_khz = 731000,
						.blackout_us = 8,
				},
				{
						.uclk_khz = 822000,
						.blackout_us = 8,
				},
				{
						.uclk_khz = 962000,
						.blackout_us = 5,
				},
				{
						.uclk_khz = 1069000,
						.blackout_us = 5,
				},
				{
						.uclk_khz = 1187000,
						.blackout_us = 5,
				},
		},
};

void dcn5_adjust_pixel_clock_for_progressive_to_interlace_unit(const struct dml2_display_cfg *display_cfg, bool ptoi_supported, double *PixelClockBackEnd)
{
	//unsigned int num_active_planes = display_cfg->num_planes;

	//Progressive To Interlace Unit Effect
	for (unsigned int k = 0; k < display_cfg->num_planes; ++k) {
		PixelClockBackEnd[k] = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced == 1 && ptoi_supported == true) {
			// FIXME_STAGE2... can sw pass the pixel rate for interlaced directly
			//display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz = 2 * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz;
		}
	}
}

static void dcn5_rq_dlg_get_wm_regs(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_dchub_watermark_regs *wm_regs)
{
	double refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : utm_soc_bb->dchub_refclk_mhz;

	wm_regs->fclk_pstate = (int unsigned)(mode_lib->mp.Watermark.FCLKChangeWatermark * refclk_freq_in_mhz);
	wm_regs->sr_enter = (int unsigned)(mode_lib->mp.Watermark.StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	wm_regs->sr_exit = (int unsigned)(mode_lib->mp.Watermark.StutterExitWatermark * refclk_freq_in_mhz);
	wm_regs->sr_enter_z8 = (int unsigned)(mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	wm_regs->sr_exit_z8 = (int unsigned)(mode_lib->mp.Watermark.Z8StutterExitWatermark * refclk_freq_in_mhz);
	wm_regs->temp_read_or_ppt = (int unsigned)(mode_lib->mp.Watermark.temp_read_or_ppt_watermark_us * refclk_freq_in_mhz);
	wm_regs->uclk_pstate = (int unsigned)(mode_lib->mp.Watermark.DRAMClockChangeWatermark * refclk_freq_in_mhz);
	wm_regs->urgent = (int unsigned)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	wm_regs->usr = (int unsigned)(mode_lib->mp.Watermark.USRRetrainingWatermark * refclk_freq_in_mhz);
	wm_regs->refcyc_per_trip_to_mem = (unsigned int)(mode_lib->mp.UrgentLatency * refclk_freq_in_mhz);
	wm_regs->refcyc_per_meta_trip_to_mem = (unsigned int)(mode_lib->mp.MetaTripToMemory * refclk_freq_in_mhz);
	wm_regs->frac_urg_bw_flip = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip * 1000);
	wm_regs->frac_urg_bw_nom = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidth * 1000);
}

void dcn5_get_mcif_arb_params(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_mcif_global_register_set *out)
{
	out->wm_regs[0].fclk_pstate = (unsigned int)(mode_lib->mp.Watermark.WritebackFCLKChangeWatermark * 1000.0);
	out->wm_regs[0].uclk_pstate = (unsigned int)(mode_lib->mp.Watermark.WritebackDRAMClockChangeWatermark * 1000.0);
	out->wm_regs[0].urgent = (unsigned int)(mode_lib->mp.Watermark.WritebackUrgentWatermark * 1000.0);
	out->wm_regs[0].temp_read_or_ppt = (unsigned int)(mode_lib->mp.Watermark.writeback_temp_read_or_ppt_watermark_us * 1000.0);
}

void dml2_core_dcn5_calcs_cursor_dlg_reg(struct dml2_cursor_dlg_regs *cursor_dlg_regs, const struct dml2_get_cursor_dlg_reg *p)
{
	int dst_x_offset = (int) ((p->cursor_x_position + (p->cursor_stereo_en == 0 ? 0 : math_max2(p->cursor_primary_offset, p->cursor_secondary_offset)) -
			(p->cursor_hotspot_x * (p->cursor_2x_magnify == 0 ? 1 : 2))) * p->dlg_refclk_mhz / p->pixel_rate_mhz / p->hratio);
	cursor_dlg_regs->dst_x_offset = (unsigned int) ((dst_x_offset > 0) ? dst_x_offset : 0);

	DML_LOG_VERBOSE("DML_DLG::%s: cursor_x_position=%d\n", __func__, p->cursor_x_position);
	DML_LOG_VERBOSE("DML_DLG::%s: dlg_refclk_mhz=%f\n", __func__, p->dlg_refclk_mhz);
	DML_LOG_VERBOSE("DML_DLG::%s: pixel_rate_mhz=%f\n", __func__, p->pixel_rate_mhz);
	DML_LOG_VERBOSE("DML_DLG::%s: dst_x_offset=%d\n", __func__, dst_x_offset);
	DML_LOG_VERBOSE("DML_DLG::%s: dst_x_offset=%d (reg)\n", __func__, cursor_dlg_regs->dst_x_offset);

	cursor_dlg_regs->chunk_hdl_adjust = 3;
	cursor_dlg_regs->dst_y_offset	 = 0;

	cursor_dlg_regs->qos_level_fixed  = 8;
	cursor_dlg_regs->qos_ramp_disable = 0;
}

void dcn5_rq_dlg_get_rq_reg(struct dml2_display_rq_regs *rq_regs,
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		unsigned int pipe_idx)
{
	unsigned int plane_idx = mode_lib->mp.pipe_plane[pipe_idx];
	enum dml2_source_format_class source_format = display_cfg->plane_descriptors[plane_idx].pixel_format;
	enum dml2_swizzle_mode sw_mode = display_cfg->plane_descriptors[plane_idx].surface.tiling;
	bool dual_plane = dml2_core_utils_is_dual_plane((enum dml2_source_format_class)(source_format));

	unsigned int pixel_chunk_bytes = 0;
	unsigned int min_pixel_chunk_bytes = 0;
	unsigned int dpte_group_bytes = 0;
	unsigned int mpte_group_bytes = 0;

	unsigned int p1_pixel_chunk_bytes = 0;
	unsigned int p1_min_pixel_chunk_bytes = 0;
	unsigned int p1_dpte_group_bytes = 0;
	unsigned int p1_mpte_group_bytes = 0;

	unsigned int detile_buf_plane1_addr = 0;
	unsigned int detile_buf_size_in_bytes;
	double stored_swath_l_bytes;
	double stored_swath_c_bytes;

	DML_LOG_VERBOSE("DML_DLG::%s: Calculation for pipe[%d] start\n", __func__, pipe_idx);

	pixel_chunk_bytes = (unsigned int)(mode_lib->ip.pixel_chunk_size_kbytes * 1024);
	min_pixel_chunk_bytes = (unsigned int)(mode_lib->ip.min_pixel_chunk_size_bytes);

	if (pixel_chunk_bytes == 64 * 1024)
		min_pixel_chunk_bytes = 0;

	dpte_group_bytes = (unsigned int)(mode_lib->mp.dpte_group_bytes[mode_lib->mp.pipe_plane[pipe_idx]]);
	mpte_group_bytes = (unsigned int)(mode_lib->mp.vm_group_bytes[mode_lib->mp.pipe_plane[pipe_idx]]);

	p1_pixel_chunk_bytes = pixel_chunk_bytes;
	p1_min_pixel_chunk_bytes = min_pixel_chunk_bytes;
	p1_dpte_group_bytes = dpte_group_bytes;
	p1_mpte_group_bytes = mpte_group_bytes;

	if (source_format == dml2_rgbe_alpha)
		p1_pixel_chunk_bytes = (unsigned int)(mode_lib->ip.alpha_pixel_chunk_size_kbytes * 1024);

	rq_regs->unbounded_request_enabled = mode_lib->mp.UnboundedRequestEnabled;
	rq_regs->pte_buffer_mode = mode_lib->mp.PTE_BUFFER_MODE[mode_lib->mp.pipe_plane[pipe_idx]];
	rq_regs->force_one_row_for_frame = mode_lib->mp.use_one_row_for_frame[mode_lib->mp.pipe_plane[pipe_idx]];
	rq_regs->rq_regs_l.chunk_size = dml2_core_utils_log_and_substract_if_non_zero(pixel_chunk_bytes, 10);
	rq_regs->rq_regs_c.chunk_size = dml2_core_utils_log_and_substract_if_non_zero(p1_pixel_chunk_bytes, 10);

	DML_LOG_VERBOSE("DML_DLG: %s: pte_buffer_mode = %u\n", __func__, rq_regs->pte_buffer_mode);
	DML_LOG_VERBOSE("DML_DLG: %s: force_one_row_for_frame = %u\n", __func__, rq_regs->force_one_row_for_frame);

	if (min_pixel_chunk_bytes == 0)
		rq_regs->rq_regs_l.min_chunk_size = 0;
	else
		rq_regs->rq_regs_l.min_chunk_size = dml2_core_utils_log_and_substract_if_non_zero(min_pixel_chunk_bytes, 8 - 1);

	if (p1_min_pixel_chunk_bytes == 0)
		rq_regs->rq_regs_c.min_chunk_size = 0;
	else
		rq_regs->rq_regs_c.min_chunk_size = dml2_core_utils_log_and_substract_if_non_zero(p1_min_pixel_chunk_bytes, 8 - 1);

	rq_regs->rq_regs_l.dpte_group_size = dml2_core_utils_log_and_substract_if_non_zero(dpte_group_bytes, 6);
	rq_regs->rq_regs_l.mpte_group_size = dml2_core_utils_log_and_substract_if_non_zero(mpte_group_bytes, 6);
	rq_regs->rq_regs_c.dpte_group_size = dml2_core_utils_log_and_substract_if_non_zero(p1_dpte_group_bytes, 6);
	rq_regs->rq_regs_c.mpte_group_size = dml2_core_utils_log_and_substract_if_non_zero(p1_mpte_group_bytes, 6);

	detile_buf_size_in_bytes = mode_lib->mp.DETBufferSizeInKByte[mode_lib->mp.pipe_plane[pipe_idx]] * 1024;

	if (dml2_core_utils_is_linear(sw_mode) && display_cfg->gpuvm_enable) {
		unsigned int p0_pte_row_height_linear = mode_lib->mp.dpte_row_height_linear[mode_lib->mp.pipe_plane[pipe_idx]];
		DML_LOG_VERBOSE("DML_DLG: %s: p0_pte_row_height_linear = %u\n", __func__, p0_pte_row_height_linear);
		DML_ASSERT(p0_pte_row_height_linear >= 8);

		rq_regs->rq_regs_l.pte_row_height_linear = math_log2_approx(p0_pte_row_height_linear) - 3;
		if (dual_plane) {
			unsigned int p1_pte_row_height_linear = mode_lib->mp.dpte_row_height_linear_chroma[mode_lib->mp.pipe_plane[pipe_idx]];
			DML_LOG_VERBOSE("DML_DLG: %s: p1_pte_row_height_linear = %u\n", __func__, p1_pte_row_height_linear);
			if (sw_mode == dml2_sw_linear) {
				DML_ASSERT(p1_pte_row_height_linear >= 8);
			}
			rq_regs->rq_regs_c.pte_row_height_linear = math_log2_approx(p1_pte_row_height_linear) - 3;
		}
	} else {
		rq_regs->rq_regs_l.pte_row_height_linear = 0;
		rq_regs->rq_regs_c.pte_row_height_linear = 0;
	}

	rq_regs->rq_regs_l.swath_height = dml2_core_utils_log_and_substract_if_non_zero(mode_lib->mp.SwathHeightY[mode_lib->mp.pipe_plane[pipe_idx]], 0);
	rq_regs->rq_regs_c.swath_height = dml2_core_utils_log_and_substract_if_non_zero(mode_lib->mp.SwathHeightC[mode_lib->mp.pipe_plane[pipe_idx]], 0);

	// FIXME_DCN4, programming guide has dGPU condition
	if (pixel_chunk_bytes >= 32 * 1024 || (dual_plane && p1_pixel_chunk_bytes >= 32 * 1024)) { //32kb
		rq_regs->drq_expansion_mode = 0;
	} else {
		rq_regs->drq_expansion_mode = 2;
	}
	rq_regs->prq_expansion_mode = 1;
	rq_regs->crq_expansion_mode = 1;
	rq_regs->mrq_expansion_mode = 1;

	stored_swath_l_bytes = mode_lib->mp.DETBufferSizeY[mode_lib->mp.pipe_plane[pipe_idx]];
	stored_swath_c_bytes = mode_lib->mp.DETBufferSizeC[mode_lib->mp.pipe_plane[pipe_idx]];

	// Note: detile_buf_plane1_addr is in unit of 1KB
	if (dual_plane) {
		if (stored_swath_l_bytes / stored_swath_c_bytes <= 1.5) {
			detile_buf_plane1_addr = (unsigned int)(detile_buf_size_in_bytes / 2.0 / 1024.0); // half to chroma
			DML_LOG_VERBOSE("DML_DLG: %s: detile_buf_plane1_addr = %d (1/2 to chroma)\n", __func__, detile_buf_plane1_addr);
		} else {
			detile_buf_plane1_addr = (unsigned int)(dml2_core_utils_round_to_multiple((unsigned int)((2.0 * detile_buf_size_in_bytes) / 3.0), 1024, 0) / 1024.0); // 2/3 to luma
			DML_LOG_VERBOSE("DML_DLG: %s: detile_buf_plane1_addr = %d (1/3 chroma)\n", __func__, detile_buf_plane1_addr);
		}
	}
	rq_regs->plane1_base_address = detile_buf_plane1_addr;

	DML_LOG_VERBOSE("DML_DLG: %s: stored_swath_l_bytes = %f\n", __func__, stored_swath_l_bytes);
	DML_LOG_VERBOSE("DML_DLG: %s: stored_swath_c_bytes = %f\n", __func__, stored_swath_c_bytes);
	DML_LOG_VERBOSE("DML_DLG: %s: detile_buf_size_in_bytes = %d\n", __func__, detile_buf_size_in_bytes);
	DML_LOG_VERBOSE("DML_DLG: %s: detile_buf_plane1_addr = %d\n", __func__, detile_buf_plane1_addr);
	DML_LOG_VERBOSE("DML_DLG: %s: plane1_base_address = %d\n", __func__, rq_regs->plane1_base_address);
	DML_LOG_VERBOSE("DML_DLG::%s: Calculation for pipe[%d] done\n", __func__, pipe_idx);
}

static void dcn5_rq_dlg_get_dlg_reg(
		struct dml2_core_internal_scratch *s,
		struct dml2_display_dlg_regs *disp_dlg_regs,
		struct dml2_display_ttu_regs *disp_ttu_regs,
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		const unsigned int pipe_idx,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	struct dml2_core_shared_rq_dlg_get_dlg_reg_locals *l = &s->rq_dlg_get_dlg_reg_locals;

	memset(l, 0, sizeof(struct dml2_core_shared_rq_dlg_get_dlg_reg_locals));

	DML_LOG_VERBOSE("DML_DLG::%s: Calculation for pipe_idx=%d\n", __func__, pipe_idx);

	l->plane_idx = mode_lib->mp.pipe_plane[pipe_idx];
	DML_ASSERT(l->plane_idx < DML2_MAX_PLANES);

	l->source_format = dml2_444_8;
	l->odm_mode = dml2_odm_mode_bypass;
	l->dual_plane = false;
	l->htotal = 0;
	l->hactive = 0;
	l->hblank_end = 0;
	l->vblank_end = 0;
	l->interlaced = false;
	l->pclk_freq_in_mhz = 0.0;
	l->refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : utm_soc_bb->dchub_refclk_mhz;
	l->ref_freq_to_pix_freq = 0.0;

	if (l->plane_idx < DML2_MAX_PLANES) {

		l->timing = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[l->plane_idx].stream_index].timing;
		l->source_format = display_cfg->plane_descriptors[l->plane_idx].pixel_format;
		l->odm_mode = mode_lib->mp.ODMMode[l->plane_idx];

		l->dual_plane = dml2_core_utils_is_dual_plane(l->source_format);

		l->htotal = l->timing->h_total;
		l->hactive = l->timing->h_active;
		l->hblank_end = l->timing->h_blank_end;
		l->vblank_end = l->timing->v_blank_end;
		l->interlaced = l->timing->interlaced;
		l->pclk_freq_in_mhz = (double)l->timing->pixel_clock_khz / 1000;
		l->ref_freq_to_pix_freq = l->refclk_freq_in_mhz / l->pclk_freq_in_mhz;

		DML_LOG_VERBOSE("DML_DLG::%s: plane_idx = %d\n", __func__, l->plane_idx);
		DML_LOG_VERBOSE("DML_DLG: %s: htotal = %d\n", __func__, l->htotal);
		DML_LOG_VERBOSE("DML_DLG: %s: refclk_freq_in_mhz = %3.2f\n", __func__, l->refclk_freq_in_mhz);
		DML_LOG_VERBOSE("DML_DLG: %s: dlg_ref_clk_mhz = %3.2f\n", __func__, display_cfg->overrides.hw.dlg_ref_clk_mhz);
		DML_LOG_VERBOSE("DML_DLG: %s: soc.refclk_mhz = %u\n", __func__, utm_soc_bb->dchub_refclk_mhz);
		DML_LOG_VERBOSE("DML_DLG: %s: pclk_freq_in_mhz = %3.2f\n", __func__, l->pclk_freq_in_mhz);
		DML_LOG_VERBOSE("DML_DLG: %s: ref_freq_to_pix_freq = %3.2f\n", __func__, l->ref_freq_to_pix_freq);
		DML_LOG_VERBOSE("DML_DLG: %s: interlaced = %d\n", __func__, l->interlaced);

		DML_ASSERT(l->refclk_freq_in_mhz != 0);
		DML_ASSERT(l->pclk_freq_in_mhz != 0);
		DML_ASSERT(l->ref_freq_to_pix_freq < 4.0);

		// Need to figure out which side of odm combine we're in
		// Assume the pipe instance under the same plane is in order

		if (l->odm_mode == dml2_odm_mode_bypass) {
			disp_dlg_regs->refcyc_h_blank_end = (unsigned int)((double)l->hblank_end * l->ref_freq_to_pix_freq);
		} else if (l->odm_mode == dml2_odm_mode_combine_2to1 || l->odm_mode == dml2_odm_mode_combine_3to1 || l->odm_mode == dml2_odm_mode_combine_4to1) {
			// find out how many pipe are in this plane
			l->num_active_pipes = mode_lib->mp.num_active_pipes;
			l->first_pipe_idx_in_plane = DML2_MAX_PLANES;
			l->pipe_idx_in_combine = 0; // pipe index within the plane
			l->odm_combine_factor = 2;

			if (l->odm_mode == dml2_odm_mode_combine_3to1)
				l->odm_combine_factor = 3;
			else if (l->odm_mode == dml2_odm_mode_combine_4to1)
				l->odm_combine_factor = 4;

			for (unsigned int i = 0; i < l->num_active_pipes; i++) {
				if (mode_lib->mp.pipe_plane[i] == l->plane_idx) {
					if (i < l->first_pipe_idx_in_plane) {
						l->first_pipe_idx_in_plane = i;
					}
				}
			}
			l->pipe_idx_in_combine = pipe_idx - l->first_pipe_idx_in_plane; // DML assumes the pipes in the same plane will have continuous indexing (i.e. plane 0 use pipe 0, 1, and plane 1 uses pipe 2, 3, etc.)

			disp_dlg_regs->refcyc_h_blank_end = (unsigned int)(((double)l->hblank_end + (double)l->pipe_idx_in_combine * (double)l->hactive / (double)l->odm_combine_factor) * l->ref_freq_to_pix_freq);
			DML_LOG_VERBOSE("DML_DLG: %s: pipe_idx = %d\n", __func__, pipe_idx);
			DML_LOG_VERBOSE("DML_DLG: %s: first_pipe_idx_in_plane = %d\n", __func__, l->first_pipe_idx_in_plane);
			DML_LOG_VERBOSE("DML_DLG: %s: pipe_idx_in_combine = %d\n", __func__, l->pipe_idx_in_combine);
			DML_LOG_VERBOSE("DML_DLG: %s: odm_combine_factor = %d\n", __func__, l->odm_combine_factor);
		}
		DML_LOG_VERBOSE("DML_DLG: %s: refcyc_h_blank_end = %d\n", __func__, disp_dlg_regs->refcyc_h_blank_end);

		DML_ASSERT(disp_dlg_regs->refcyc_h_blank_end < (unsigned int)math_pow(2, 13));

		disp_dlg_regs->ref_freq_to_pix_freq = (unsigned int)(l->ref_freq_to_pix_freq * math_pow(2, 19));
		disp_dlg_regs->refcyc_per_htotal = (unsigned int)(l->ref_freq_to_pix_freq * (double)l->htotal * math_pow(2, 8));
		disp_dlg_regs->dlg_vblank_end = l->interlaced ? (l->vblank_end / 2) : l->vblank_end; // 15 bits

		l->min_ttu_vblank = mode_lib->mp.MinTTUVBlank[mode_lib->mp.pipe_plane[pipe_idx]];
		l->min_dst_y_next_start = (unsigned int)(mode_lib->mp.MIN_DST_Y_NEXT_START[mode_lib->mp.pipe_plane[pipe_idx]]);

		DML_LOG_VERBOSE("DML_DLG: %s: min_ttu_vblank (us) = %3.2f\n", __func__, l->min_ttu_vblank);
		DML_LOG_VERBOSE("DML_DLG: %s: min_dst_y_next_start = %d\n", __func__, l->min_dst_y_next_start);
		DML_LOG_VERBOSE("DML_DLG: %s: ref_freq_to_pix_freq = %3.2f\n", __func__, l->ref_freq_to_pix_freq);

		l->vready_after_vcount0 = (unsigned int)(mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[mode_lib->mp.pipe_plane[pipe_idx]]);
		disp_dlg_regs->vready_after_vcount0 = l->vready_after_vcount0;

		DML_LOG_VERBOSE("DML_DLG: %s: vready_after_vcount0 = %d\n", __func__, disp_dlg_regs->vready_after_vcount0);

		l->dst_x_after_scaler = (unsigned int)(mode_lib->mp.DSTXAfterScaler[mode_lib->mp.pipe_plane[pipe_idx]]);
		l->dst_y_after_scaler = (unsigned int)(mode_lib->mp.DSTYAfterScaler[mode_lib->mp.pipe_plane[pipe_idx]]);

		DML_LOG_VERBOSE("DML_DLG: %s: dst_x_after_scaler = %d\n", __func__, l->dst_x_after_scaler);
		DML_LOG_VERBOSE("DML_DLG: %s: dst_y_after_scaler = %d\n", __func__, l->dst_y_after_scaler);

		l->dst_y_prefetch = mode_lib->mp.dst_y_prefetch[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_vm_vblank = mode_lib->mp.dst_y_per_vm_vblank[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_row_vblank = mode_lib->mp.dst_y_per_row_vblank[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_vm_flip = mode_lib->mp.dst_y_per_vm_flip[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_row_flip = mode_lib->mp.dst_y_per_row_flip[mode_lib->mp.pipe_plane[pipe_idx]];

		DML_LOG_VERBOSE("DML_DLG: %s: dst_y_prefetch (after rnd) = %3.2f\n", __func__, l->dst_y_prefetch);
		DML_LOG_VERBOSE("DML_DLG: %s: dst_y_per_vm_flip = %3.2f\n", __func__, l->dst_y_per_vm_flip);
		DML_LOG_VERBOSE("DML_DLG: %s: dst_y_per_row_flip = %3.2f\n", __func__, l->dst_y_per_row_flip);
		DML_LOG_VERBOSE("DML_DLG: %s: dst_y_per_vm_vblank = %3.2f\n", __func__, l->dst_y_per_vm_vblank);
		DML_LOG_VERBOSE("DML_DLG: %s: dst_y_per_row_vblank = %3.2f\n", __func__, l->dst_y_per_row_vblank);

		if (l->dst_y_prefetch > 0 && l->dst_y_per_vm_vblank > 0 && l->dst_y_per_row_vblank > 0) {
			DML_ASSERT(l->dst_y_prefetch > (l->dst_y_per_vm_vblank + l->dst_y_per_row_vblank));
		}

		l->vratio_pre_l = mode_lib->mp.VRatioPrefetchY[mode_lib->mp.pipe_plane[pipe_idx]];
		l->vratio_pre_c = mode_lib->mp.VRatioPrefetchC[mode_lib->mp.pipe_plane[pipe_idx]];

		DML_LOG_VERBOSE("DML_DLG: %s: vratio_pre_l = %3.2f\n", __func__, l->vratio_pre_l);
		DML_LOG_VERBOSE("DML_DLG: %s: vratio_pre_c = %3.2f\n", __func__, l->vratio_pre_c);

		// Active
		l->refcyc_per_line_delivery_pre_l = mode_lib->mp.DisplayPipeLineDeliveryTimeLumaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_line_delivery_l = mode_lib->mp.DisplayPipeLineDeliveryTimeLuma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_line_delivery_pre_l = %3.2f\n", __func__, l->refcyc_per_line_delivery_pre_l);
		DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_line_delivery_l = %3.2f\n", __func__, l->refcyc_per_line_delivery_l);

		l->refcyc_per_line_delivery_pre_c = 0.0;
		l->refcyc_per_line_delivery_c = 0.0;

		if (l->dual_plane) {
			l->refcyc_per_line_delivery_pre_c = mode_lib->mp.DisplayPipeLineDeliveryTimeChromaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
			l->refcyc_per_line_delivery_c = mode_lib->mp.DisplayPipeLineDeliveryTimeChroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

			DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_line_delivery_pre_c = %3.2f\n", __func__, l->refcyc_per_line_delivery_pre_c);
			DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_line_delivery_c = %3.2f\n", __func__, l->refcyc_per_line_delivery_c);
		}

		disp_dlg_regs->refcyc_per_vm_dmdata = (unsigned int)(mode_lib->mp.Tdmdl_vm[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);
		disp_dlg_regs->dmdata_dl_delta = (unsigned int)(mode_lib->mp.Tdmdl[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);

		l->refcyc_per_req_delivery_pre_l = mode_lib->mp.DisplayPipeRequestDeliveryTimeLumaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_req_delivery_l = mode_lib->mp.DisplayPipeRequestDeliveryTimeLuma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_req_delivery_pre_l = %3.2f\n", __func__, l->refcyc_per_req_delivery_pre_l);
		DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_req_delivery_l = %3.2f\n", __func__, l->refcyc_per_req_delivery_l);

		l->refcyc_per_req_delivery_pre_c = 0.0;
		l->refcyc_per_req_delivery_c = 0.0;
		if (l->dual_plane) {
			l->refcyc_per_req_delivery_pre_c = mode_lib->mp.DisplayPipeRequestDeliveryTimeChromaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
			l->refcyc_per_req_delivery_c = mode_lib->mp.DisplayPipeRequestDeliveryTimeChroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

			DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_req_delivery_pre_c = %3.2f\n", __func__, l->refcyc_per_req_delivery_pre_c);
			DML_LOG_VERBOSE("DML_DLG: %s: refcyc_per_req_delivery_c = %3.2f\n", __func__, l->refcyc_per_req_delivery_c);
		}

		// TTU - Cursor
		DML_ASSERT(display_cfg->plane_descriptors[l->plane_idx].cursor.num_cursors <= 1);

		// Assign to register structures
		disp_dlg_regs->min_dst_y_next_start = (unsigned int)((double)l->min_dst_y_next_start * math_pow(2, 2));
		DML_ASSERT(disp_dlg_regs->min_dst_y_next_start < (unsigned int)math_pow(2, 18));

		disp_dlg_regs->dst_y_after_scaler = l->dst_y_after_scaler; // in terms of line
		disp_dlg_regs->refcyc_x_after_scaler = (unsigned int)((double)l->dst_x_after_scaler * l->ref_freq_to_pix_freq); // in terms of refclk
		disp_dlg_regs->dst_y_prefetch = (unsigned int)(l->dst_y_prefetch * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_vm_vblank = (unsigned int)(l->dst_y_per_vm_vblank * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_row_vblank = (unsigned int)(l->dst_y_per_row_vblank * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_vm_flip = (unsigned int)(l->dst_y_per_vm_flip * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_row_flip = (unsigned int)(l->dst_y_per_row_flip * math_pow(2, 2));

		disp_dlg_regs->vratio_prefetch = (unsigned int)(l->vratio_pre_l * math_pow(2, 19));
		disp_dlg_regs->vratio_prefetch_c = (unsigned int)(l->vratio_pre_c * math_pow(2, 19));

		DML_LOG_VERBOSE("DML_DLG: %s: disp_dlg_regs->dst_y_per_vm_vblank = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_vm_vblank);
		DML_LOG_VERBOSE("DML_DLG: %s: disp_dlg_regs->dst_y_per_row_vblank = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_row_vblank);
		DML_LOG_VERBOSE("DML_DLG: %s: disp_dlg_regs->dst_y_per_vm_flip = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_vm_flip);
		DML_LOG_VERBOSE("DML_DLG: %s: disp_dlg_regs->dst_y_per_row_flip = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_row_flip);

		disp_dlg_regs->refcyc_per_vm_group_vblank = (unsigned int)(mode_lib->mp.TimePerVMGroupVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);
		disp_dlg_regs->refcyc_per_vm_group_flip = (unsigned int)(mode_lib->mp.TimePerVMGroupFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);
		disp_dlg_regs->refcyc_per_vm_req_vblank = (unsigned int)(mode_lib->mp.TimePerVMRequestVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz * math_pow(2, 10));
		disp_dlg_regs->refcyc_per_vm_req_flip = (unsigned int)(mode_lib->mp.TimePerVMRequestFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz * math_pow(2, 10));

		l->dst_y_per_pte_row_nom_l = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_L[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_pte_row_nom_c = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_C[mode_lib->mp.pipe_plane[pipe_idx]];
		l->refcyc_per_pte_group_nom_l = mode_lib->mp.time_per_pte_group_nom_luma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_nom_c = mode_lib->mp.time_per_pte_group_nom_chroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_vblank_l = mode_lib->mp.time_per_pte_group_vblank_luma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_vblank_c = mode_lib->mp.time_per_pte_group_vblank_chroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_flip_l = mode_lib->mp.time_per_pte_group_flip_luma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_flip_c = mode_lib->mp.time_per_pte_group_flip_chroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_tdlut_group = mode_lib->mp.time_per_tdlut_group[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		disp_dlg_regs->dst_y_per_pte_row_nom_l = (unsigned int)(l->dst_y_per_pte_row_nom_l * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_pte_row_nom_c = (unsigned int)(l->dst_y_per_pte_row_nom_c * math_pow(2, 2));

		disp_dlg_regs->refcyc_per_pte_group_nom_l = (unsigned int)(l->refcyc_per_pte_group_nom_l);
		disp_dlg_regs->refcyc_per_pte_group_nom_c = (unsigned int)(l->refcyc_per_pte_group_nom_c);
		disp_dlg_regs->refcyc_per_pte_group_vblank_l = (unsigned int)(l->refcyc_per_pte_group_vblank_l);
		disp_dlg_regs->refcyc_per_pte_group_vblank_c = (unsigned int)(l->refcyc_per_pte_group_vblank_c);
		disp_dlg_regs->refcyc_per_pte_group_flip_l = (unsigned int)(l->refcyc_per_pte_group_flip_l);
		disp_dlg_regs->refcyc_per_pte_group_flip_c = (unsigned int)(l->refcyc_per_pte_group_flip_c);
		disp_dlg_regs->refcyc_per_line_delivery_pre_l = (unsigned int)math_floor2(l->refcyc_per_line_delivery_pre_l, 1);
		disp_dlg_regs->refcyc_per_line_delivery_l = (unsigned int)math_floor2(l->refcyc_per_line_delivery_l, 1);
		disp_dlg_regs->refcyc_per_line_delivery_pre_c = (unsigned int)math_floor2(l->refcyc_per_line_delivery_pre_c, 1);
		disp_dlg_regs->refcyc_per_line_delivery_c = (unsigned int)math_floor2(l->refcyc_per_line_delivery_c, 1);

		l->dst_y_per_meta_row_nom_l = mode_lib->mp.DST_Y_PER_META_ROW_NOM_L[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_meta_row_nom_c = mode_lib->mp.DST_Y_PER_META_ROW_NOM_C[mode_lib->mp.pipe_plane[pipe_idx]];
		l->refcyc_per_meta_chunk_nom_l = mode_lib->mp.TimePerMetaChunkNominal[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_nom_c = mode_lib->mp.TimePerChromaMetaChunkNominal[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_vblank_l = mode_lib->mp.TimePerMetaChunkVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_vblank_c = mode_lib->mp.TimePerChromaMetaChunkVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_flip_l = mode_lib->mp.TimePerMetaChunkFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_flip_c = mode_lib->mp.TimePerChromaMetaChunkFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		disp_dlg_regs->dst_y_per_meta_row_nom_l = (unsigned int)(l->dst_y_per_meta_row_nom_l * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_meta_row_nom_c = (unsigned int)(l->dst_y_per_meta_row_nom_c * math_pow(2, 2));
		disp_dlg_regs->refcyc_per_meta_chunk_nom_l = (unsigned int)(l->refcyc_per_meta_chunk_nom_l);
		disp_dlg_regs->refcyc_per_meta_chunk_nom_c = (unsigned int)(l->refcyc_per_meta_chunk_nom_c);
		disp_dlg_regs->refcyc_per_meta_chunk_vblank_l = (unsigned int)(l->refcyc_per_meta_chunk_vblank_l);
		disp_dlg_regs->refcyc_per_meta_chunk_vblank_c = (unsigned int)(l->refcyc_per_meta_chunk_vblank_c);
		disp_dlg_regs->refcyc_per_meta_chunk_flip_l = (unsigned int)(l->refcyc_per_meta_chunk_flip_l);
		disp_dlg_regs->refcyc_per_meta_chunk_flip_c = (unsigned int)(l->refcyc_per_meta_chunk_flip_c);

		disp_dlg_regs->refcyc_per_tdlut_group = (unsigned int)(l->refcyc_per_tdlut_group);
		disp_dlg_regs->dst_y_delta_drq_limit = 0x7fff; // off

		disp_ttu_regs->refcyc_per_req_delivery_pre_l = (unsigned int)(l->refcyc_per_req_delivery_pre_l * math_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_l = (unsigned int)(l->refcyc_per_req_delivery_l * math_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_pre_c = (unsigned int)(l->refcyc_per_req_delivery_pre_c * math_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_c = (unsigned int)(l->refcyc_per_req_delivery_c * math_pow(2, 10));
		disp_ttu_regs->qos_level_low_wm = 0;

		disp_ttu_regs->qos_level_high_wm = (unsigned int)(4.0 * (double)l->htotal * l->ref_freq_to_pix_freq);

		disp_ttu_regs->qos_level_flip = 14;
		disp_ttu_regs->qos_level_fixed_l = 8;
		disp_ttu_regs->qos_level_fixed_c = 8;
		disp_ttu_regs->qos_ramp_disable_l = 0;
		disp_ttu_regs->qos_ramp_disable_c = 0;
		disp_ttu_regs->min_ttu_vblank = (unsigned int)(l->min_ttu_vblank * l->refclk_freq_in_mhz);

		// CHECK for HW registers' range, DML_ASSERT or clamp
		DML_ASSERT(l->refcyc_per_req_delivery_pre_l < math_pow(2, 13));
		DML_ASSERT(l->refcyc_per_req_delivery_l < math_pow(2, 13));
		DML_ASSERT(l->refcyc_per_req_delivery_pre_c < math_pow(2, 13));
		DML_ASSERT(l->refcyc_per_req_delivery_c < math_pow(2, 13));
		if (disp_dlg_regs->refcyc_per_vm_group_vblank >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_group_vblank = (unsigned int)(math_pow(2, 23) - 1);

		if (disp_dlg_regs->refcyc_per_vm_group_flip >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_group_flip = (unsigned int)(math_pow(2, 23) - 1);

		if (disp_dlg_regs->refcyc_per_vm_req_vblank >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_req_vblank = (unsigned int)(math_pow(2, 23) - 1);

		if (disp_dlg_regs->refcyc_per_vm_req_flip >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_req_flip = (unsigned int)(math_pow(2, 23) - 1);


		DML_ASSERT(disp_dlg_regs->dst_y_after_scaler < (unsigned int)8);
		DML_ASSERT(disp_dlg_regs->refcyc_x_after_scaler < (unsigned int)math_pow(2, 13));

		if (disp_dlg_regs->dst_y_per_pte_row_nom_l >= (unsigned int)math_pow(2, 17)) {
			DML_LOG_VERBOSE("DML_DLG: %s: Warning DST_Y_PER_PTE_ROW_NOM_L %u > register max U15.2 %u, clamp to max\n", __func__, disp_dlg_regs->dst_y_per_pte_row_nom_l, (unsigned int)math_pow(2, 17) - 1);
			l->dst_y_per_pte_row_nom_l = (unsigned int)math_pow(2, 17) - 1;
		}
		if (l->dual_plane) {
			if (disp_dlg_regs->dst_y_per_pte_row_nom_c >= (unsigned int)math_pow(2, 17)) {
				DML_LOG_VERBOSE("DML_DLG: %s: Warning DST_Y_PER_PTE_ROW_NOM_C %u > register max U15.2 %u, clamp to max\n", __func__, disp_dlg_regs->dst_y_per_pte_row_nom_c, (unsigned int)math_pow(2, 17) - 1);
				l->dst_y_per_pte_row_nom_c = (unsigned int)math_pow(2, 17) - 1;
			}
		}

		if (disp_dlg_regs->refcyc_per_pte_group_nom_l >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_pte_group_nom_l = (unsigned int)(math_pow(2, 23) - 1);
		if (l->dual_plane) {
			if (disp_dlg_regs->refcyc_per_pte_group_nom_c >= (unsigned int)math_pow(2, 23))
				disp_dlg_regs->refcyc_per_pte_group_nom_c = (unsigned int)(math_pow(2, 23) - 1);
		}
		DML_ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_l < (unsigned int)math_pow(2, 13));
		if (l->dual_plane) {
			DML_ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_c < (unsigned int)math_pow(2, 13));
		}

		DML_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_l < (unsigned int)math_pow(2, 13));
		DML_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_l < (unsigned int)math_pow(2, 13));
		DML_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_c < (unsigned int)math_pow(2, 13));
		DML_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_c < (unsigned int)math_pow(2, 13));
		DML_ASSERT(disp_ttu_regs->qos_level_low_wm < (unsigned int)math_pow(2, 14));
		DML_ASSERT(disp_ttu_regs->qos_level_high_wm < (unsigned int)math_pow(2, 14));
		DML_ASSERT(disp_ttu_regs->min_ttu_vblank < (unsigned int)math_pow(2, 24));
		DML_LOG_VERBOSE("DML_DLG::%s: Calculation for pipe[%d] done\n", __func__, pipe_idx);
	}
}

static void dcn5_rq_dlg_get_arb_params(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_display_arb_regs *arb_param)
{
	double refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : utm_soc_bb->dchub_refclk_mhz;

	arb_param->max_req_outstanding = utm_soc_bb->max_outstanding_reqs;
	arb_param->min_req_outstanding = utm_soc_bb->max_outstanding_reqs; // turn off the sat level feature if this set to max
	arb_param->sdpif_request_rate_limit = (3 * mode_lib->ip.words_per_channel * utm_soc_bb->dram_config.channel_count) / 4;
	arb_param->sdpif_request_rate_limit = arb_param->sdpif_request_rate_limit < 96 ? 96 : arb_param->sdpif_request_rate_limit;
	arb_param->sat_level_us = 60;
	arb_param->hvm_max_qos_commit_threshold = 0xf;
	arb_param->hvm_min_req_outstand_commit_threshold = 0xa;
	arb_param->compbuf_reserved_space_kbytes = mode_lib->mp.compbuf_reserved_space_64b * 64 / 1024;
	arb_param->compbuf_size = mode_lib->mp.CompressedBufferSizeInkByte / mode_lib->ip.compressed_buffer_segment_size_in_kbytes;
	arb_param->allow_sdpif_rate_limit_when_cstate_req = mode_lib->mp.hw_debug5;
	arb_param->dcfclk_deep_sleep_hysteresis = mode_lib->mp.dcfclk_deep_sleep_hysteresis;
	arb_param->pstate_stall_threshold = (unsigned int)(mode_lib->ip_caps.fams2.max_allow_delay_us * refclk_freq_in_mhz);

	DML_LOG_VERBOSE("DML::%s: max_req_outstanding = %d\n", __func__, arb_param->max_req_outstanding);
	DML_LOG_VERBOSE("DML::%s: sdpif_request_rate_limit = %d\n", __func__, arb_param->sdpif_request_rate_limit);
	DML_LOG_VERBOSE("DML::%s: compbuf_reserved_space_kbytes = %d\n", __func__, arb_param->compbuf_reserved_space_kbytes);
	DML_LOG_VERBOSE("DML::%s: allow_sdpif_rate_limit_when_cstate_req = %d\n", __func__, arb_param->allow_sdpif_rate_limit_when_cstate_req);
	DML_LOG_VERBOSE("DML::%s: dcfclk_deep_sleep_hysteresis = %d\n", __func__, arb_param->dcfclk_deep_sleep_hysteresis);
}

void dcn5_get_watermarks(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_dchub_watermark_regs *out)
{
	dcn5_rq_dlg_get_wm_regs(display_cfg, mode_lib, utm_soc_bb, out);
}

void dcn5_get_arb_params(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_display_arb_regs *out)
{
	dcn5_rq_dlg_get_arb_params(display_cfg, mode_lib, utm_soc_bb, out);
}

void dcn5_get_pipe_regs(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_dchub_per_pipe_register_set *out, int pipe_index,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		struct dml2_core_internal_scratch *s)
{
	dcn5_rq_dlg_get_rq_reg(&out->rq_regs, display_cfg, mode_lib, pipe_index);
	dcn5_rq_dlg_get_dlg_reg(s, &out->dlg_regs, &out->ttu_regs, display_cfg, mode_lib, pipe_index, utm_soc_bb);
	out->det_size = mode_lib->mp.DETBufferSizeInKByte[mode_lib->mp.pipe_plane[pipe_index]] / mode_lib->ip.config_return_buffer_segment_size_in_kbytes;
}

void dcn5_get_per_dwb_params(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_mcif_per_pipe_register_set *out,
		int stream_index,
		int dwb_index)
{
	double writeback_latency_hiding_us = dcn5_calculate_writeback_latency_hiding_us(display_cfg,
					mode_lib->ip.writeback_interface_buffer_size_kbytes * 1024,
					stream_index,
					dwb_index);

	out->max_scaled_time_ns = (unsigned int)math_max2(
			(writeback_latency_hiding_us - mode_lib->mp.Watermark.WritebackUrgentWatermark) * 1000.0,
			0.0);

	/* 1024ps units in U6.6 format */
	out->time_per_pixel = (unsigned int)((1000000.0 * math_pow(2, 6)) /
			(double)display_cfg->stream_descriptors[stream_index].timing.pixel_clock_khz);

	out->slice_lines = 31;
	out->arbitration_slice = 2;
}
